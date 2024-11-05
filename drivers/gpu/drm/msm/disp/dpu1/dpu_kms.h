/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __DPU_KMS_H__
#define __DPU_KMS_H__

#include <linux/interconnect.h>

#include <drm/drm_drv.h>

#include "msm_drv.h"
#include "msm_kms.h"
#include "msm_mmu.h"
#include "msm_gem.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_ctl.h"
#include "dpu_hw_lm.h"
#include "dpu_hw_interrupts.h"
#include "dpu_hw_top.h"
#include "dpu_rm.h"
#include "dpu_core_perf.h"

#define DRMID(x) ((x) ? (x)->base.id : -1)

/**
 * DPU_DEBUG - macro for kms/plane/crtc/encoder/connector logs
 * @fmt: Pointer to format string
 */
#define DPU_DEBUG(fmt, ...)                                                \
	DRM_DEBUG_DRIVER(fmt, ##__VA_ARGS__)

/**
 * DPU_DEBUG_DRIVER - macro for hardware driver logging
 * @fmt: Pointer to format string
 */
#define DPU_DEBUG_DRIVER(fmt, ...)                                         \
	DRM_DEBUG_DRIVER(fmt, ##__VA_ARGS__)

#define DPU_ERROR(fmt, ...) pr_err("[dpu error]" fmt, ##__VA_ARGS__)
#define DPU_ERROR_RATELIMITED(fmt, ...) pr_err_ratelimited("[dpu error]" fmt, ##__VA_ARGS__)

/**
 * ktime_compare_safe - compare two ktime structures
 *	This macro is similar to the standard ktime_compare() function, but
 *	attempts to also handle ktime overflows.
 * @A: First ktime value
 * @B: Second ktime value
 * Returns: -1 if A < B, 0 if A == B, 1 if A > B
 */
#define ktime_compare_safe(A, B) \
	ktime_compare(ktime_sub((A), (B)), ktime_set(0, 0))

struct dpu_kms {
	struct msm_kms base;
	struct drm_device *dev;
	const struct dpu_mdss_cfg *catalog;
	const struct msm_mdss_data *mdss;

	/* io/register spaces: */
	void __iomem *mmio, *vbif[VBIF_MAX];

	struct regulator *vdd;
	struct regulator *mmagic;
	struct regulator *venus;

	struct dpu_hw_intr *hw_intr;

	struct dpu_core_perf perf;

	/*
	 * Global private object state, Do not access directly, use
	 * dpu_kms_global_get_state()
	 */
	struct drm_private_obj global_state;

	struct dpu_rm rm;

	struct dpu_hw_vbif *hw_vbif[VBIF_MAX];
	struct dpu_hw_mdp *hw_mdp;

	bool has_danger_ctrl;

	struct platform_device *pdev;
	bool rpm_enabled;

	struct clk_bulk_data *clocks;
	size_t num_clocks;

	/* reference count bandwidth requests, so we know when we can
	 * release bandwidth.  Each atomic update increments, and frame-
	 * done event decrements.  Additionally, for video mode, the
	 * reference is incremented when crtc is enabled, and decremented
	 * when disabled.
	 */
	atomic_t bandwidth_ref;
	struct icc_path *path[2];
	u32 num_paths;
};

struct vsync_info {
	u32 frame_count;
	u32 line_count;
};

#define DPU_ENC_WR_PTR_START_TIMEOUT_US 20000

#define DPU_ENC_MAX_POLL_TIMEOUT_US	2000

#define to_dpu_kms(x) container_of(x, struct dpu_kms, base)

#define to_dpu_global_state(x) container_of(x, struct dpu_global_state, base)

/* Global private object state for tracking resources that are shared across
 * multiple kms objects (planes/crtcs/etc).
 */
struct dpu_global_state {
	struct drm_private_state base;

	struct dpu_rm *rm;

	uint32_t pingpong_to_enc_id[PINGPONG_MAX - PINGPONG_0];
	uint32_t mixer_to_enc_id[LM_MAX - LM_0];
	uint32_t ctl_to_enc_id[CTL_MAX - CTL_0];
	uint32_t dspp_to_enc_id[DSPP_MAX - DSPP_0];
	uint32_t dsc_to_enc_id[DSC_MAX - DSC_0];
	uint32_t cdm_to_enc_id;
};

struct dpu_global_state
	*dpu_kms_get_existing_global_state(struct dpu_kms *dpu_kms);
struct dpu_global_state
	*__must_check dpu_kms_get_global_state(struct drm_atomic_state *s);

/**
 * Debugfs functions - extra helper functions for debugfs support
 *
 * Main debugfs documentation is located at,
 *
 * Documentation/filesystems/debugfs.rst
 *
 * @dpu_debugfs_create_regset32: Create 32-bit register dump file
 */

void dpu_debugfs_create_regset32(const char *name, umode_t mode,
		void *parent,
		uint32_t offset, uint32_t length, struct dpu_kms *dpu_kms);

/**
 * DPU info management functions
 * These functions/definitions allow for building up a 'dpu_info' structure
 * containing one or more "key=value\n" entries.
 */
#define DPU_KMS_INFO_MAX_SIZE	4096

/**
 * Vblank enable/disable functions
 */
int dpu_enable_vblank(struct msm_kms *kms, struct drm_crtc *crtc);
void dpu_disable_vblank(struct msm_kms *kms, struct drm_crtc *crtc);

unsigned long dpu_kms_get_clk_rate(struct dpu_kms *dpu_kms, char *clock_name);

#endif /* __dpu_kms_H__ */
