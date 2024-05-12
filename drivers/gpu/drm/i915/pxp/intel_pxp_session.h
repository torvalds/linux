/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_SESSION_H__
#define __INTEL_PXP_SESSION_H__

#include <linux/types.h>

struct intel_pxp;

#ifdef CONFIG_DRM_I915_PXP
void intel_pxp_session_management_init(struct intel_pxp *pxp);
void intel_pxp_terminate(struct intel_pxp *pxp, bool post_invalidation_needs_restart);
#else
static inline void intel_pxp_session_management_init(struct intel_pxp *pxp)
{
}

static inline void intel_pxp_terminate(struct intel_pxp *pxp, bool post_invalidation_needs_restart)
{
}
#endif
#endif /* __INTEL_PXP_SESSION_H__ */
