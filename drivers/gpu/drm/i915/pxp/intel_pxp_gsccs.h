/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2022, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_GSCCS_H__
#define __INTEL_PXP_GSCCS_H__

#include <linux/types.h>

struct intel_pxp;

#define GSC_REPLY_LATENCY_MS 200
#define GSC_PENDING_RETRY_MAXCOUNT 40
#define GSC_PENDING_RETRY_PAUSE_MS 50

#ifdef CONFIG_DRM_I915_PXP
void intel_pxp_gsccs_fini(struct intel_pxp *pxp);
int intel_pxp_gsccs_init(struct intel_pxp *pxp);

#else
static inline void intel_pxp_gsccs_fini(struct intel_pxp *pxp)
{
}

static inline int intel_pxp_gsccs_init(struct intel_pxp *pxp)
{
	return 0;
}

#endif

#endif /*__INTEL_PXP_GSCCS_H__ */
