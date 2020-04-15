/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_PARAMS_H
#define _RKISPP_PARAMS_H

#include <linux/rkispp-config.h>
#include "common.h"

/* rkispp parameters device
 * config_lock: lock to protect config
 * params: queued buffer list
 * cur_params: ispp params config
 * cur_params: current buf of parameters
 * first_params: the first params should take effect immediately
 */
struct rkispp_params_vdev {
	struct rkispp_vdev_node vnode;
	struct rkispp_device *dev;

	spinlock_t config_lock;
	struct list_head params;
	struct rkispp_params_cfg *cur_params;
	struct rkispp_buffer *cur_buf;

	struct v4l2_format vdev_fmt;
	bool streamon;
	bool first_params;
};

int rkispp_register_params_vdev(struct rkispp_device *dev);
void rkispp_unregister_params_vdev(struct rkispp_device *dev);
void rkispp_params_configure(struct rkispp_params_vdev *params_vdev);
void rkispp_params_isr(struct rkispp_params_vdev *params_vdev, u32 mis);
#endif
