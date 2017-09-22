/*
 * (C) COPYRIGHT 2016 ARM Limited. All rights reserved.
 * Author: Liviu Dudau <Liviu.Dudau@arm.com>
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * ARM Mali DP500/DP550/DP650 KMS/DRM driver structures
 */

#ifndef __MALIDP_DRV_H__
#define __MALIDP_DRV_H__

#include <linux/mutex.h>
#include <linux/wait.h>
#include <drm/drmP.h>
#include "malidp_hw.h"

struct malidp_drm {
	struct malidp_hw_device *dev;
	struct drm_fbdev_cma *fbdev;
	struct drm_crtc crtc;
	wait_queue_head_t wq;
	atomic_t config_valid;
	struct drm_atomic_state *pm_state;
	u32 core_id;
};

#define crtc_to_malidp_device(x) container_of(x, struct malidp_drm, crtc)

struct malidp_plane {
	struct drm_plane base;
	struct malidp_hw_device *hwdev;
	const struct malidp_layer *layer;
};

struct malidp_plane_state {
	struct drm_plane_state base;

	/* size of the required rotation memory if plane is rotated */
	u32 rotmem_size;
	/* internal format ID */
	u8 format;
	u8 n_planes;
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
void malidp_de_planes_destroy(struct drm_device *drm);
int malidp_crtc_init(struct drm_device *drm);

/* often used combination of rotational bits */
#define MALIDP_ROTATED_MASK	(DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270)

#endif  /* __MALIDP_DRV_H__ */
