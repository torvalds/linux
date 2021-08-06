/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ZynqMP Display Driver
 *
 * Copyright (C) 2017 - 2020 Xilinx, Inc.
 *
 * Authors:
 * - Hyun Woo Kwon <hyun.kwon@xilinx.com>
 * - Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef _ZYNQMP_DISP_H_
#define _ZYNQMP_DISP_H_

#include <linux/types.h>

/*
 * 3840x2160 is advertised as the maximum resolution, but almost any
 * resolutions under a 300Mhz pixel rate would work. Pick 4096x4096.
 */
#define ZYNQMP_DISP_MAX_WIDTH				4096
#define ZYNQMP_DISP_MAX_HEIGHT				4096

/* The DPDMA is limited to 44 bit addressing. */
#define ZYNQMP_DISP_MAX_DMA_BIT				44

struct device;
struct drm_atomic_state;
struct drm_device;
struct drm_plane;
struct platform_device;
struct zynqmp_disp;
struct zynqmp_dpsub;

/**
 * enum zynqmp_dpsub_layer_id - Layer identifier
 * @ZYNQMP_DPSUB_LAYER_VID: Video layer
 * @ZYNQMP_DPSUB_LAYER_GFX: Graphics layer
 */
enum zynqmp_dpsub_layer_id {
	ZYNQMP_DPSUB_LAYER_VID,
	ZYNQMP_DPSUB_LAYER_GFX,
};

void zynqmp_disp_enable(struct zynqmp_disp *disp);
void zynqmp_disp_disable(struct zynqmp_disp *disp);
int zynqmp_disp_setup_clock(struct zynqmp_disp *disp,
			    unsigned long mode_clock);

void zynqmp_disp_plane_atomic_disable(struct drm_plane *plane,
				      struct drm_atomic_state *state);

int zynqmp_disp_drm_init(struct zynqmp_dpsub *dpsub);
int zynqmp_disp_probe(struct zynqmp_dpsub *dpsub, struct drm_device *drm);
void zynqmp_disp_remove(struct zynqmp_dpsub *dpsub);

#endif /* _ZYNQMP_DISP_H_ */
