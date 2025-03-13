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

struct platform_device;
struct zynqmp_dp;
struct zynqmp_dpsub;

void zynqmp_dp_enable_vblank(struct zynqmp_dp *dp);
void zynqmp_dp_disable_vblank(struct zynqmp_dp *dp);

int zynqmp_dp_probe(struct zynqmp_dpsub *dpsub);
void zynqmp_dp_remove(struct zynqmp_dpsub *dpsub);

void zynqmp_dp_audio_set_channels(struct zynqmp_dp *dp,
				  unsigned int num_channels);
void zynqmp_dp_audio_enable(struct zynqmp_dp *dp);
void zynqmp_dp_audio_disable(struct zynqmp_dp *dp);

void zynqmp_dp_audio_write_n_m(struct zynqmp_dp *dp);

#endif /* _ZYNQMP_DP_H_ */
