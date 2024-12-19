/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ZynqMP DPSUB Subsystem Driver
 *
 * Copyright (C) 2017 - 2020 Xilinx, Inc.
 *
 * Authors:
 * - Hyun Woo Kwon <hyun.kwon@xilinx.com>
 * - Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef _ZYNQMP_DPSUB_H_
#define _ZYNQMP_DPSUB_H_

#include <linux/types.h>

struct clk;
struct device;
struct drm_bridge;
struct zynqmp_disp;
struct zynqmp_disp_layer;
struct zynqmp_dp;
struct zynqmp_dpsub_drm;

#define ZYNQMP_DPSUB_NUM_LAYERS				2

enum zynqmp_dpsub_port {
	ZYNQMP_DPSUB_PORT_LIVE_VIDEO,
	ZYNQMP_DPSUB_PORT_LIVE_GFX,
	ZYNQMP_DPSUB_PORT_LIVE_AUDIO,
	ZYNQMP_DPSUB_PORT_OUT_VIDEO,
	ZYNQMP_DPSUB_PORT_OUT_AUDIO,
	ZYNQMP_DPSUB_PORT_OUT_DP,
	ZYNQMP_DPSUB_NUM_PORTS,
};

enum zynqmp_dpsub_format {
	ZYNQMP_DPSUB_FORMAT_RGB,
	ZYNQMP_DPSUB_FORMAT_YCRCB444,
	ZYNQMP_DPSUB_FORMAT_YCRCB422,
	ZYNQMP_DPSUB_FORMAT_YONLY,
};

struct zynqmp_dpsub_audio;

/**
 * struct zynqmp_dpsub - ZynqMP DisplayPort Subsystem
 * @dev: The physical device
 * @apb_clk: The APB clock
 * @vid_clk: Video clock
 * @vid_clk_from_ps: True of the video clock comes from PS, false from PL
 * @aud_clk: Audio clock
 * @aud_clk_from_ps: True of the audio clock comes from PS, false from PL
 * @connected_ports: Bitmask of connected ports in the device tree
 * @dma_enabled: True if the DMA interface is enabled, false if the DPSUB is
 *	driven by the live input
 * @drm: The DRM/KMS device data
 * @bridge: The DP encoder bridge
 * @disp: The display controller
 * @layers: Video and graphics layers
 * @dp: The DisplayPort controller
 * @dma_align: DMA alignment constraint (must be a power of 2)
 */
struct zynqmp_dpsub {
	struct device *dev;

	struct clk *apb_clk;
	struct clk *vid_clk;
	bool vid_clk_from_ps;
	struct clk *aud_clk;
	bool aud_clk_from_ps;

	unsigned int connected_ports;
	bool dma_enabled;

	struct zynqmp_dpsub_drm *drm;
	struct drm_bridge *bridge;

	struct zynqmp_disp *disp;
	struct zynqmp_disp_layer *layers[ZYNQMP_DPSUB_NUM_LAYERS];
	struct zynqmp_dp *dp;

	unsigned int dma_align;

	struct zynqmp_dpsub_audio *audio;
};

#ifdef CONFIG_DRM_ZYNQMP_DPSUB_AUDIO
int zynqmp_audio_init(struct zynqmp_dpsub *dpsub);
void zynqmp_audio_uninit(struct zynqmp_dpsub *dpsub);
#else
static inline int zynqmp_audio_init(struct zynqmp_dpsub *dpsub) { return 0; }
static inline void zynqmp_audio_uninit(struct zynqmp_dpsub *dpsub) { }
#endif

void zynqmp_dpsub_release(struct zynqmp_dpsub *dpsub);

#endif /* _ZYNQMP_DPSUB_H_ */
