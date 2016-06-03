/*
 * Rockchip VPU codec driver
 *
 * Copyright (c) 2014 Rockchip Electronics Co., Ltd.
 *	Alpha Lin <Alpha.Lin@rock-chips.com>
 *	Jeffy Chen <jeffy.chen@rock-chips.com>
 *
 * Copyright (C) 2014 Google, Inc.
 *	Tomasz Figa <tfiga@chromium.org>
 *
 * Based on s5p-mfc driver by Samsung Electronics Co., Ltd.
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef ROCKCHIP_VPU_ENC_H_
#define ROCKCHIP_VPU_ENC_H_

struct vb2_ops *get_enc_queue_ops(void);
const struct v4l2_ioctl_ops *get_enc_v4l2_ioctl_ops(void);
struct rockchip_vpu_fmt *get_enc_def_fmt(bool src);
int rockchip_vpu_enc_init(struct rockchip_vpu_ctx *ctx);
void rockchip_vpu_enc_exit(struct rockchip_vpu_ctx *ctx);
int rockchip_vpu_enc_init_dummy_ctx(struct rockchip_vpu_dev *dev);
void rockchip_vpu_enc_free_dummy_ctx(struct rockchip_vpu_dev *dev);

#endif				/* ROCKCHIP_VPU_ENC_H_  */
