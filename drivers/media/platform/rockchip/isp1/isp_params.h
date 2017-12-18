/*
 * Rockchip isp1 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _RKISP1_ISP_H
#define _RKISP1_ISP_H

#include <linux/rkisp1-config.h>
#include "common.h"

/*
 * struct rkisp1_isp_subdev - ISP input parameters device
 *
 * @cur_params: Current ISP parameters
 * @first_params: the first params should take effect immediately
 */
struct rkisp1_isp_params_vdev {
	struct rkisp1_vdev_node vnode;
	struct rkisp1_device *dev;

	spinlock_t config_lock;
	struct list_head params;
	struct rkisp1_isp_params_cfg cur_params;
	struct v4l2_format vdev_fmt;
	bool streamon;
	bool first_params;

	enum v4l2_quantization quantization;
	enum rkisp1_fmt_raw_pat_type raw_type;
};

/* config params before ISP streaming */
void rkisp1_params_configure_isp(struct rkisp1_isp_params_vdev *params_vdev,
			  struct ispsd_in_fmt *in_fmt,
			  enum v4l2_quantization quantization);
void rkisp1_params_disable_isp(struct rkisp1_isp_params_vdev *params_vdev);

int rkisp1_register_params_vdev(struct rkisp1_isp_params_vdev *params_vdev,
				struct v4l2_device *v4l2_dev,
				struct rkisp1_device *dev);

void rkisp1_unregister_params_vdev(struct rkisp1_isp_params_vdev *params_vdev);

void rkisp1_params_isr(struct rkisp1_isp_params_vdev *params_vdev, u32 isp_mis);

#endif /* _RKISP1_ISP_H */
