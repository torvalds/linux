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

struct clk;
struct device;
struct drm_device;
struct zynqmp_disp;
struct zynqmp_dp;

enum zynqmp_dpsub_format {
	ZYNQMP_DPSUB_FORMAT_RGB,
	ZYNQMP_DPSUB_FORMAT_YCRCB444,
	ZYNQMP_DPSUB_FORMAT_YCRCB422,
	ZYNQMP_DPSUB_FORMAT_YONLY,
};

/**
 * struct zynqmp_dpsub - ZynqMP DisplayPort Subsystem
 * @drm: The DRM/KMS device
 * @dev: The physical device
 * @apb_clk: The APB clock
 * @disp: The display controller
 * @dp: The DisplayPort controller
 * @dma_align: DMA alignment constraint (must be a power of 2)
 */
struct zynqmp_dpsub {
	struct drm_device drm;
	struct device *dev;

	struct clk *apb_clk;

	struct zynqmp_disp *disp;
	struct zynqmp_dp *dp;

	unsigned int dma_align;
};

static inline struct zynqmp_dpsub *to_zynqmp_dpsub(struct drm_device *drm)
{
	return container_of(drm, struct zynqmp_dpsub, drm);
}

#endif /* _ZYNQMP_DPSUB_H_ */
