/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_PM_H__
#define __INTEL_PXP_PM_H__

#include "intel_pxp_types.h"

#ifdef CONFIG_DRM_I915_PXP
void intel_pxp_suspend(struct intel_pxp *pxp, bool runtime);
void intel_pxp_resume(struct intel_pxp *pxp);
#else
static inline void intel_pxp_suspend(struct intel_pxp *pxp, bool runtime)
{
}

static inline void intel_pxp_resume(struct intel_pxp *pxp)
{
}
#endif

#endif /* __INTEL_PXP_PM_H__ */
