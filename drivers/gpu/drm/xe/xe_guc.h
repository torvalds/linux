/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_H_
#define _XE_GUC_H_

#include "xe_gt.h"
#include "xe_guc_types.h"
#include "xe_hw_engine_types.h"
#include "xe_macros.h"

/*
 * GuC version number components are defined to be only 8-bit size,
 * so converting to a 32bit 8.8.8 integer allows simple (and safe)
 * numerical comparisons.
 */
#define MAKE_GUC_VER(maj, min, pat)	(((maj) << 16) | ((min) << 8) | (pat))
#define MAKE_GUC_VER_STRUCT(ver)	MAKE_GUC_VER((ver).major, (ver).minor, (ver).patch)
#define MAKE_GUC_VER_ARGS(ver...) \
	(BUILD_BUG_ON_ZERO(COUNT_ARGS(ver) < 2 || COUNT_ARGS(ver) > 3) + \
	 MAKE_GUC_VER(PICK_ARG1(ver), PICK_ARG2(ver), IF_ARGS(PICK_ARG3(ver), 0, PICK_ARG3(ver))))

#define GUC_SUBMIT_VER(guc) \
	MAKE_GUC_VER_STRUCT((guc)->fw.versions.found[XE_UC_FW_VER_COMPATIBILITY])
#define GUC_FIRMWARE_VER(guc) \
	MAKE_GUC_VER_STRUCT((guc)->fw.versions.found[XE_UC_FW_VER_RELEASE])
#define GUC_FIRMWARE_VER_AT_LEAST(guc, ver...) \
	xe_guc_fw_version_at_least((guc), MAKE_GUC_VER_ARGS(ver))

struct drm_printer;

void xe_guc_comm_init_early(struct xe_guc *guc);
int xe_guc_init_noalloc(struct xe_guc *guc);
int xe_guc_init(struct xe_guc *guc);
int xe_guc_init_post_hwconfig(struct xe_guc *guc);
int xe_guc_post_load_init(struct xe_guc *guc);
int xe_guc_reset(struct xe_guc *guc);
int xe_guc_upload(struct xe_guc *guc);
int xe_guc_min_load_for_hwconfig(struct xe_guc *guc);
int xe_guc_enable_communication(struct xe_guc *guc);
int xe_guc_opt_in_features_enable(struct xe_guc *guc);
void xe_guc_runtime_suspend(struct xe_guc *guc);
void xe_guc_runtime_resume(struct xe_guc *guc);
int xe_guc_suspend(struct xe_guc *guc);
void xe_guc_notify(struct xe_guc *guc);
int xe_guc_auth_huc(struct xe_guc *guc, u32 rsa_addr);
int xe_guc_mmio_send(struct xe_guc *guc, const u32 *request, u32 len);
int xe_guc_mmio_send_recv(struct xe_guc *guc, const u32 *request, u32 len,
			  u32 *response_buf);
int xe_guc_self_cfg32(struct xe_guc *guc, u16 key, u32 val);
int xe_guc_self_cfg64(struct xe_guc *guc, u16 key, u64 val);
void xe_guc_irq_handler(struct xe_guc *guc, const u16 iir);
void xe_guc_sanitize(struct xe_guc *guc);
int xe_guc_print_info(struct xe_guc *guc, struct drm_printer *p);
int xe_guc_reset_prepare(struct xe_guc *guc);
void xe_guc_reset_wait(struct xe_guc *guc);
void xe_guc_stop_prepare(struct xe_guc *guc);
void xe_guc_stop(struct xe_guc *guc);
int xe_guc_start(struct xe_guc *guc);
void xe_guc_declare_wedged(struct xe_guc *guc);
bool xe_guc_using_main_gamctrl_queues(struct xe_guc *guc);

#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
int xe_guc_g2g_test_notification(struct xe_guc *guc, u32 *payload, u32 len);
#endif

static inline u16 xe_engine_class_to_guc_class(enum xe_engine_class class)
{
	switch (class) {
	case XE_ENGINE_CLASS_RENDER:
		return GUC_RENDER_CLASS;
	case XE_ENGINE_CLASS_VIDEO_DECODE:
		return GUC_VIDEO_CLASS;
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
		return GUC_VIDEOENHANCE_CLASS;
	case XE_ENGINE_CLASS_COPY:
		return GUC_BLITTER_CLASS;
	case XE_ENGINE_CLASS_COMPUTE:
		return GUC_COMPUTE_CLASS;
	case XE_ENGINE_CLASS_OTHER:
		return GUC_GSC_OTHER_CLASS;
	default:
		XE_WARN_ON(class);
		return -1;
	}
}

static inline struct xe_gt *guc_to_gt(struct xe_guc *guc)
{
	return container_of(guc, struct xe_gt, uc.guc);
}

static inline struct xe_device *guc_to_xe(struct xe_guc *guc)
{
	return gt_to_xe(guc_to_gt(guc));
}

static inline struct drm_device *guc_to_drm(struct xe_guc *guc)
{
	return &guc_to_xe(guc)->drm;
}

/**
 * xe_guc_fw_version_at_least() - Check if GuC is at least of given version.
 * @guc: the &xe_guc
 * @ver: the version to check
 *
 * The @ver should be prepared using MAKE_GUC_VER(major, minor, patch).
 *
 * Return: true if loaded GuC firmware is at least of given version,
 *         false otherwise.
 */
static inline bool xe_guc_fw_version_at_least(const struct xe_guc *guc, u32 ver)
{
	return GUC_FIRMWARE_VER(guc) >= ver;
}

#endif
