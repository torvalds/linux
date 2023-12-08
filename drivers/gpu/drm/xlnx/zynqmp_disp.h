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
struct drm_device;
struct platform_device;
struct zynqmp_disp;
struct zynqmp_dpsub;

void zynqmp_disp_handle_vblank(struct zynqmp_disp *disp);
bool zynqmp_disp_audio_enabled(struct zynqmp_disp *disp);
unsigned int zynqmp_disp_get_audio_clk_rate(struct zynqmp_disp *disp);
uint32_t zynqmp_disp_get_crtc_mask(struct zynqmp_disp *disp);

int zynqmp_disp_drm_init(struct zynqmp_dpsub *dpsub);
int zynqmp_disp_probe(struct zynqmp_dpsub *dpsub, struct drm_device *drm);
void zynqmp_disp_remove(struct zynqmp_dpsub *dpsub);

#endif /* _ZYNQMP_DISP_H_ */
