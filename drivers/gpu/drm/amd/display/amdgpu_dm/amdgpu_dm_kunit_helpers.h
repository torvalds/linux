/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#ifndef AMDGPU_DM_KUNIT_HELPERS_H
#define AMDGPU_DM_KUNIT_HELPERS_H

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
#define STATIC_IFN_KUNIT
#define INLINE_IFN_KUNIT inline
#define EXPORT_IF_KUNIT(symbol) EXPORT_SYMBOL(symbol)

#else
#define STATIC_IFN_KUNIT static
#define INLINE_IFN_KUNIT
#define EXPORT_IF_KUNIT(symbol)
#endif

#endif /* AMDGPU_DM_KUNIT_HELPERS_H */
