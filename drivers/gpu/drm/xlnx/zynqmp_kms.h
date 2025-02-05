/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ZynqMP DisplayPort Subsystem - KMS API
 *
 * Copyright (C) 2017 - 2021 Xilinx, Inc.
 *
 * Authors:
 * - Hyun Woo Kwon <hyun.kwon@xilinx.com>
 * - Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef _ZYNQMP_KMS_H_
#define _ZYNQMP_KMS_H_

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_plane.h>

#include "zynqmp_dpsub.h"

struct zynqmp_dpsub;

/**
 * struct zynqmp_dpsub_drm - ZynqMP DisplayPort Subsystem DRM/KMS data
 * @dpsub: Backpointer to the DisplayPort subsystem
 * @dev: The DRM/KMS device
 * @planes: The DRM planes
 * @crtc: The DRM CRTC
 * @encoder: The dummy DRM encoder
 */
struct zynqmp_dpsub_drm {
	struct zynqmp_dpsub *dpsub;

	struct drm_device dev;
	struct drm_plane planes[ZYNQMP_DPSUB_NUM_LAYERS];
	struct drm_crtc crtc;
	struct drm_encoder encoder;
};

void zynqmp_dpsub_drm_handle_vblank(struct zynqmp_dpsub *dpsub);

int zynqmp_dpsub_drm_init(struct zynqmp_dpsub *dpsub);
void zynqmp_dpsub_drm_cleanup(struct zynqmp_dpsub *dpsub);

#endif /* _ZYNQMP_KMS_H_ */
