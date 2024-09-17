/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ZynqMP DisplayPort Driver
 *
 * Copyright (C) 2017 - 2020 Xilinx, Inc.
 *
 * Authors:
 * - Hyun Woo Kwon <hyun.kwon@xilinx.com>
 * - Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef _ZYNQMP_DP_H_
#define _ZYNQMP_DP_H_

struct drm_device;
struct platform_device;
struct zynqmp_dp;
struct zynqmp_dpsub;

void zynqmp_dp_enable_vblank(struct zynqmp_dp *dp);
void zynqmp_dp_disable_vblank(struct zynqmp_dp *dp);

int zynqmp_dp_drm_init(struct zynqmp_dpsub *dpsub);
int zynqmp_dp_probe(struct zynqmp_dpsub *dpsub, struct drm_device *drm);
void zynqmp_dp_remove(struct zynqmp_dpsub *dpsub);

#endif /* _ZYNQMP_DP_H_ */
