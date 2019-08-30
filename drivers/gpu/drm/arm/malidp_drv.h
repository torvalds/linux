/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * (C) COPYRIGHT 2016 ARM Limited. All rights reserved.
 * Author: Liviu Dudau <Liviu.Dudau@arm.com>
 *
 * ARM Mali DP500/DP550/DP650 KMS/DRM driver structures
 */

#ifndef __MALIDP_DRV_H__
#define __MALIDP_DRV_H__

#include <drm/drm_writeback.h>
#include <drm/drm_encoder.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <drm/drmP.h>
#include "malidp_hw.h"

#define MALIDP_CONFIG_VALID_INIT	0
#define MALIDP_CONFIG_VALID_DONE	1
#define MALIDP_CONFIG_START		0xd0

struct malidp_error_stats {
	s32 num_errors;
	u32 last_error_status;
	s64 last_error_vblank;
};

struct malidp_drm {
	struct malidp_hw_device *dev;
	struct drm_crtc crtc;
	struct drm_writeback_connector mw_connector;
	wait_queue_head_t wq;
	struct drm_pending_vblank_event *event;
	atomic_t config_valid;
	u32 core_id;
#ifdef CONFIG_DEBUG_FS
	struct malidp_error_stats de_errors;
	struct malidp_error_stats se_errors;
	/* Protects errors stats */
	spinlock_t errors_lock;
#endif
};

#define crtc_to_malidp_device(x) container_of(x, struct malidp_drm, crtc)

struct malidp_plane {
	struct drm_plane base;
	struct malidp_hw_device *hwdev;
	const struct malidp_layer *layer;
};

enum mmu_prefetch_mode {
	MALIDP_PREFETCH_MODE_NONE,
	MALIDP_PREFETCH_MODE_PARTIAL,
	MALIDP_PREFETCH_MODE_FULL,
};

struct malidp_plane_state {
	struct drm_plane_state base;

	/* size of the required rotation memory if plane is rotated */
	u32 rotmem_size;
	/* internal format ID */
	u8 format;
	u8 n_planes;
	enum mmu_prefetch_mode mmu_prefetch_mode;
	u32 mmu_prefetch_pgsize;
};

#define to_malidp_plane(x) container_of(x, struct malidp_plane, base)
#define to_malidp_plane_state(x) container_of(x, struct malidp_plane_state, base)

struct malidp_crtc_state {
	struct drm_crtc_state base;
	u32 gamma_coeffs[MALIDP_COEFFTAB_NUM_COEFFS];
	u32 coloradj_coeffs[MALIDP_COLORADJ_NUM_COEFFS];
	struct malidp_se_config scaler_config;
	/* Bitfield of all the planes that have requested a scaled output. */
	u8 scaled_planes_mask;
};

#define to_malidp_crtc_state(x) container_of(x, struct malidp_crtc_state, base)

int malidp_de_planes_init(struct drm_device *drm);
int malidp_crtc_init(struct drm_device *drm);

bool malidp_hw_format_is_linear_only(u32 format);
bool malidp_hw_format_is_afbc_only(u32 format);

bool malidp_format_mod_supported(struct drm_device *drm,
				 u32 format, u64 modifier);

#ifdef CONFIG_DEBUG_FS
void malidp_error(struct malidp_drm *malidp,
		  struct malidp_error_stats *error_stats, u32 status,
		  u64 vblank);
#endif

/* often used combination of rotational bits */
#define MALIDP_ROTATED_MASK	(DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270)

#endif  /* __MALIDP_DRV_H__ */
