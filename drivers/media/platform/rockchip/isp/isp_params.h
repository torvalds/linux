/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISP_PARAM_H
#define _RKISP_ISP_PARAM_H

#include <linux/rkisp1-config.h>
#include <linux/rk-preisp.h>
#include "common.h"

enum rkisp_params_type {
	RKISP_PARAMS_ALL,
	RKISP_PARAMS_IMD,
	RKISP_PARAMS_SHD,
};

struct rkisp_isp_params_vdev;
struct rkisp_isp_params_ops {
	void (*save_first_param)(struct rkisp_isp_params_vdev *params_vdev, void *param);
	void (*clear_first_param)(struct rkisp_isp_params_vdev *params_vdev);
	void (*get_param_size)(struct rkisp_isp_params_vdev *params_vdev, unsigned int sizes[]);
	void (*first_cfg)(struct rkisp_isp_params_vdev *params_vdev);
	void (*disable_isp)(struct rkisp_isp_params_vdev *params_vdev);
	void (*isr_hdl)(struct rkisp_isp_params_vdev *params_vdev, u32 isp_mis);
	void (*param_cfg)(struct rkisp_isp_params_vdev *params_vdev, u32 frame_id,
			  u32 rdbk_times, enum rkisp_params_type type);
};

/*
 * struct rkisp_isp_params_vdev - ISP input parameters device
 *
 * @cur_params: Current ISP parameters
 * @first_params: the first params should take effect immediately
 */
struct rkisp_isp_params_vdev {
	struct rkisp_vdev_node vnode;
	struct rkisp_device *dev;

	spinlock_t config_lock;
	struct list_head params;
	union {
		struct rkisp1_isp_params_cfg *isp1x_params;
		struct isp2x_isp_params_cfg *isp2x_params;
	};
	struct v4l2_format vdev_fmt;
	bool streamon;
	bool first_params;
	bool hdrtmo_en;

	enum v4l2_quantization quantization;
	enum rkisp_fmt_raw_pat_type raw_type;
	u32 in_mbus_code;

	struct preisp_hdrae_para_s hdrae_para;

	struct rkisp_isp_params_ops *ops;
	void *priv_ops;
	void *priv_cfg;
	void *priv_val;

	struct rkisp_buffer *cur_buf;
	u32 rdbk_times;
};

/* config params before ISP streaming */
void rkisp_params_first_cfg(struct rkisp_isp_params_vdev *params_vdev,
			    struct ispsd_in_fmt *in_fmt,
			    enum v4l2_quantization quantization);
void rkisp_params_disable_isp(struct rkisp_isp_params_vdev *params_vdev);

int rkisp_register_params_vdev(struct rkisp_isp_params_vdev *params_vdev,
			       struct v4l2_device *v4l2_dev,
			       struct rkisp_device *dev);

void rkisp_unregister_params_vdev(struct rkisp_isp_params_vdev *params_vdev);

void rkisp_params_isr(struct rkisp_isp_params_vdev *params_vdev, u32 isp_mis);

void rkisp_params_cfg(struct rkisp_isp_params_vdev *params_vdev,
		      u32 frame_id, u32 rdbk_times);

#endif /* _RKISP_ISP_PARAM_H */
