/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_PARAMS_H
#define _RKISPP_PARAMS_H

#include <linux/fec-config.h>
#include <linux/rkispp-config.h>
#include "common.h"

/* rkispp parameters device
 * config_lock: lock to protect config
 * params: queued buffer list
 * cur_params: ispp params config
 * cur_params: current buf of parameters
 * first_params: the first params should take effect immediately
 */
#define ISPP_PACK_4BYTE(a, b, c, d) \
	(((a) & 0xFF) << 0 | ((b) & 0xFF) << 8 | \
	 ((c) & 0xFF) << 16 | ((d) & 0xFF) << 24)

#define ISPP_PACK_4BIT(a, b, c, d, e, f, g, h) \
	(((a) & 0xf) << 0 | ((b) & 0xf) << 4 | \
	 ((c) & 0xf) << 8 | ((d) & 0xf) << 12 | \
	 ((e) & 0xf) << 16 | ((f) & 0xf) << 20 | \
	 ((g) & 0xf) << 24 | ((h) & 0xf) << 28)

#define ISPP_PACK_4BYTE(a, b, c, d) \
	(((a) & 0xFF) << 0 | ((b) & 0xFF) << 8 | \
	 ((c) & 0xFF) << 16 | ((d) & 0xFF) << 24)

#define ISPP_PACK_2SHORT(a, b) \
	(((a) & 0xFFFF) << 0 | ((b) & 0xFFFF) << 16)

#define ISPP_NOBIG_OVERFLOW_SIZE	(2560 * 1440)

struct rkispp_params_vdev {
	struct rkispp_vdev_node vnode;
	struct rkispp_device *dev;
	struct rkispp_params_ops *params_ops;

	spinlock_t config_lock;
	struct list_head params;
	struct rkispp_params_cfg *cur_params;
	struct fec_params_cfg *fec_params;
	struct rkispp_buffer *cur_buf;

	struct v4l2_format vdev_fmt;
	bool streamon;
	bool first_params;
	bool is_subs_evt;

	struct rkispp_dummy_buffer buf_fec[FEC_MESH_BUF_NUM];
	u32 buf_fec_idx;
};

struct rkispp_params_ops {
	void (*rkispp_params_cfg)(struct rkispp_params_vdev *params_vdev, u32 frame_id);
	void (*rkispp_params_vb2_buf_queue)(struct vb2_buffer *vb);
};

int rkispp_register_params_vdev(struct rkispp_device *dev);
void rkispp_unregister_params_vdev(struct rkispp_device *dev);
void rkispp_params_get_fecbuf_inf(struct rkispp_params_vdev *params_vdev,
				  struct rkispp_fecbuf_info *fecbuf);
void rkispp_params_set_fecbuf_size(struct rkispp_params_vdev *params_vdev,
				   struct rkispp_fecbuf_size *fecsize);

#endif
