//===--------- context.cpp - HIP Adapter ----------------------------------===//
//
// Copyright (C) 2023 Intel Corporation
//
// Part of the Unified-Runtime Project, under the Apache License v2.0 with LLVM
// Exceptions. See LICENSE.TXT
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "context.hpp"
#include "usm.hpp"

void ur_context_handle_t_::addPool(ur_usm_pool_handle_t Pool) {
  std::lock_guard<std::mutex> Lock(Mutex);
  PoolHandles.insert(Pool);
}

void ur_context_handle_t_::removePool(ur_usm_pool_handle_t Pool) {
  std::lock_guard<std::mutex> Lock(Mutex);
  PoolHandles.erase(Pool);
}

ur_usm_pool_handle_t
ur_context_handle_t_::getOwningURPool(umf_memory_pool_t *UMFPool) {
  std::lock_guard<std::mutex> Lock(Mutex);
  for (auto &Pool : PoolHandles) {
    if (Pool->hasUMFPool(UMFPool)) {
      return Pool;
    }
  }
  return nullptr;
}

/// Create a UR context.
///
UR_APIEXPORT ur_result_t UR_APICALL urContextCreate(
    uint32_t DeviceCount, const ur_device_handle_t *phDevices,
    const ur_context_properties_t *, ur_context_handle_t *phContext) {
  ur_result_t RetErr = UR_RESULT_SUCCESS;

  std::unique_ptr<ur_context_handle_t_> ContextPtr{nullptr};
  try {
    // Create a context.
    ContextPtr = std::unique_ptr<ur_context_handle_t_>(
        new ur_context_handle_t_{phDevices, DeviceCount});
    *phContext = ContextPtr.release();
  } catch (ur_result_t Err) {
    RetErr = Err;
  } catch (std::bad_alloc &) {
    return UR_RESULT_ERROR_OUT_OF_HOST_MEMORY;
  } catch (...) {
    return UR_RESULT_ERROR_UNKNOWN;
  }
  return RetErr;
}

UR_APIEXPORT ur_result_t UR_APICALL
urContextGetInfo(ur_context_handle_t hContext, ur_context_info_t propName,
                 size_t propSize, void *pPropValue, size_t *pPropSizeRet) {

  UrReturnHelper ReturnValue(propSize, pPropValue, pPropSizeRet);

  switch (uint32_t{propName}) {
  case UR_CONTEXT_INFO_NUM_DEVICES:
    return ReturnValue(static_cast<uint32_t>(hContext->Devices.size()));
  case UR_CONTEXT_INFO_DEVICES:
    return ReturnValue(hContext->getDevices().data(),
                       hContext->getDevices().size());
  case UR_CONTEXT_INFO_REFERENCE_COUNT:
    return ReturnValue(hContext->getReferenceCount());
  case UR_CONTEXT_INFO_ATOMIC_MEMORY_ORDER_CAPABILITIES:
  case UR_CONTEXT_INFO_ATOMIC_MEMORY_SCOPE_CAPABILITIES:
  case UR_CONTEXT_INFO_ATOMIC_FENCE_ORDER_CAPABILITIES:
  case UR_CONTEXT_INFO_ATOMIC_FENCE_SCOPE_CAPABILITIES: {
    // These queries should be dealt with in context_impl.cpp by calling the
    // queries of each device separately and building the intersection set.
    return UR_RESULT_ERROR_INVALID_ENUMERATION;
  }
  case UR_CONTEXT_INFO_USM_MEMCPY2D_SUPPORT:
    // 2D USM memcpy is supported.
    return ReturnValue(true);
  case UR_CONTEXT_INFO_USM_FILL2D_SUPPORT:
    // 2D USM operations currently not supported.
    return ReturnValue(false);

  default:
    break;
  }

  return UR_RESULT_ERROR_INVALID_ENUMERATION;
}

UR_APIEXPORT ur_result_t UR_APICALL
urContextRelease(ur_context_handle_t hContext) {
  if (hContext->decrementReferenceCount() == 0) {
    hContext->invokeExtendedDeleters();
    delete hContext;
  }
  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL
urContextRetain(ur_context_handle_t hContext) {
  assert(hContext->getReferenceCount() > 0);

  hContext->incrementReferenceCount();
  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urContextGetNativeHandle(
    ur_context_handle_t hContext, ur_native_handle_t *phNativeContext) {
  // FIXME: this entry point has been deprecated in the SYCL RT and should be
  // changed to unsupported once the deprecation period has elapsed
  // The below is extremely dodgy but is the equivalent for what went before
  // for continuity: apparently some users may be somehow using this API
  // currently, despite it not being well defined. This API should not have been
  // implemented in the HIP backend.  hipCtx_t is not natively supported by amd
  // devices and is meaningless for our purposes; all hipCtx_t APIs were added
  // for cuda compatibility only and are deprecated by HIP.

  hipCtx_t *Ctx = nullptr;
  UR_CHECK_ERROR(
      hipDevicePrimaryCtxRetain(Ctx, hContext->getDevices()[0]->get()));
  *phNativeContext = reinterpret_cast<ur_native_handle_t>(Ctx);
  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urContextCreateWithNativeHandle(
    [[maybe_unused]] ur_native_handle_t hNativeContext,
    [[maybe_unused]] uint32_t numDevices,
    [[maybe_unused]] const ur_device_handle_t *phDevices,
    [[maybe_unused]] const ur_context_native_properties_t *pProperties,
    [[maybe_unused]] ur_context_handle_t *phContext) {
  return UR_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

UR_APIEXPORT ur_result_t UR_APICALL urContextSetExtendedDeleter(
    ur_context_handle_t hContext, ur_context_extended_deleter_t pfnDeleter,
    void *pUserData) {
  hContext->setExtendedDeleter(pfnDeleter, pUserData);
  return UR_RESULT_SUCCESS;
}
