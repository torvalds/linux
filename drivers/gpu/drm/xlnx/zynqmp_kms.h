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

struct zynqmp_dpsub;

extern const struct drm_driver zynqmp_dpsub_drm_driver;

void zynqmp_dpsub_handle_vblank(struct zynqmp_dpsub *dpsub);

int zynqmp_dpsub_drm_init(struct zynqmp_dpsub *dpsub);
void zynqmp_dpsub_drm_cleanup(struct zynqmp_dpsub *dpsub);

#endif /* _ZYNQMP_KMS_H_ */
