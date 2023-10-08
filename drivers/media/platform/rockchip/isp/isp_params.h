/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISP_PARAM_H
#define _RKISP_ISP_PARAM_H

#include <linux/rk-isp1-config.h>
#include <linux/rk-isp2-config.h>
#include <linux/rk-isp3-config.h>
#include <linux/rk-isp32-config.h>
#include <linux/rk-preisp.h>
#include "common.h"

#define ISP_PACK_4BYTE(a, b, c, d)	\
	(((a) & 0xFF) << 0 | ((b) & 0xFF) << 8 | \
	 ((c) & 0xFF) << 16 | ((d) & 0xFF) << 24)

#define ISP_PACK_2SHORT(a, b)	\
	(((a) & 0xFFFF) << 0 | ((b) & 0xFFFF) << 16)

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
			  enum rkisp_params_type type);
	void (*param_cfgsram)(struct rkisp_isp_params_vdev *params_vdev);
	void (*get_meshbuf_inf)(struct rkisp_isp_params_vdev *params_vdev, void *meshbuf);
	int (*set_meshbuf_size)(struct rkisp_isp_params_vdev *params_vdev, void *meshsize);
	void (*free_meshbuf)(struct rkisp_isp_params_vdev *params_vdev, u64 id);
	void (*stream_stop)(struct rkisp_isp_params_vdev *params_vdev);
	void (*fop_release)(struct rkisp_isp_params_vdev *params_vdev);
	bool (*check_bigmode)(struct rkisp_isp_params_vdev *params_vdev);
	int (*info2ddr_cfg)(struct rkisp_isp_params_vdev *params_vdev, void *arg);
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
		struct isp21_isp_params_cfg *isp21_params;
		struct isp3x_isp_params_cfg *isp3x_params;
		struct isp32_isp_params_cfg *isp32_params;
	};
	struct v4l2_format vdev_fmt;
	bool streamon;
	bool first_params;
	bool first_cfg_params;
	bool hdrtmo_en;
	bool afaemode_en;

	enum v4l2_quantization quantization;
	enum rkisp_fmt_raw_pat_type raw_type;
	u32 in_mbus_code;
	u32 cur_frame_id;
	struct preisp_hdrae_para_s hdrae_para;

	struct rkisp_isp_params_ops *ops;
	void *priv_ops;
	void *priv_cfg;
	void *priv_val;

	struct rkisp_buffer *cur_buf;
	u32 rdbk_times;

	struct sensor_exposure_cfg exposure;

	bool is_subs_evt;
	bool is_first_cfg;
};

static inline void
rkisp_iowrite32(struct rkisp_isp_params_vdev *params_vdev,
		u32 value, u32 addr)
{
	rkisp_write(params_vdev->dev, addr, value, false);
}

static inline u32
rkisp_ioread32(struct rkisp_isp_params_vdev *params_vdev,
	       u32 addr)
{
	return rkisp_read(params_vdev->dev, addr, false);
}

static inline void
isp_param_set_bits(struct rkisp_isp_params_vdev *params_vdev,
		   u32 reg, u32 bit_mask)
{
	u32 val;

	val = rkisp_ioread32(params_vdev, reg);
	rkisp_iowrite32(params_vdev, val | bit_mask, reg);
}

static inline void
isp_param_clear_bits(struct rkisp_isp_params_vdev *params_vdev,
		     u32 reg, u32 bit_mask)
{
	u32 val;

	val = rkisp_ioread32(params_vdev, reg);
	rkisp_iowrite32(params_vdev, val & ~bit_mask, reg);
}

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

void rkisp_params_cfg(struct rkisp_isp_params_vdev *params_vdev, u32 frame_id);

void rkisp_params_cfgsram(struct rkisp_isp_params_vdev *params_vdev, bool is_check);
void rkisp_params_get_meshbuf_inf(struct rkisp_isp_params_vdev *params_vdev, void *meshbuf);
int rkisp_params_set_meshbuf_size(struct rkisp_isp_params_vdev *params_vdev, void *meshsize);
void rkisp_params_meshbuf_free(struct rkisp_isp_params_vdev *params_vdev, u64 id);
void rkisp_params_stream_stop(struct rkisp_isp_params_vdev *params_vdev);
bool rkisp_params_check_bigmode(struct rkisp_isp_params_vdev *params_vdev);
int rkisp_params_info2ddr_cfg(struct rkisp_isp_params_vdev *params_vdev, void *arg);
#endif /* _RKISP_ISP_PARAM_H */
