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
struct drm_format_info;
struct drm_plane_state;
struct platform_device;
struct zynqmp_disp;
struct zynqmp_disp_layer;
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

/**
 * enum zynqmp_dpsub_layer_mode - Layer mode
 * @ZYNQMP_DPSUB_LAYER_NONLIVE: non-live (memory) mode
 * @ZYNQMP_DPSUB_LAYER_LIVE: live (stream) mode
 */
enum zynqmp_dpsub_layer_mode {
	ZYNQMP_DPSUB_LAYER_NONLIVE,
	ZYNQMP_DPSUB_LAYER_LIVE,
};

void zynqmp_disp_enable(struct zynqmp_disp *disp);
void zynqmp_disp_disable(struct zynqmp_disp *disp);
int zynqmp_disp_setup_clock(struct zynqmp_disp *disp,
			    unsigned long mode_clock);

void zynqmp_disp_blend_set_global_alpha(struct zynqmp_disp *disp,
					bool enable, u32 alpha);

u32 *zynqmp_disp_layer_drm_formats(struct zynqmp_disp_layer *layer,
				   unsigned int *num_formats);
void zynqmp_disp_layer_enable(struct zynqmp_disp_layer *layer,
			      enum zynqmp_dpsub_layer_mode mode);
void zynqmp_disp_layer_disable(struct zynqmp_disp_layer *layer);
void zynqmp_disp_layer_set_format(struct zynqmp_disp_layer *layer,
				  const struct drm_format_info *info);
int zynqmp_disp_layer_update(struct zynqmp_disp_layer *layer,
			     struct drm_plane_state *state);

int zynqmp_disp_probe(struct zynqmp_dpsub *dpsub);
void zynqmp_disp_remove(struct zynqmp_dpsub *dpsub);

#endif /* _ZYNQMP_DISP_H_ */
