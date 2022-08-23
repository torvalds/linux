/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip CIF Driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 */

#ifndef _RKCIF_SDITF_H
#define _RKCIF_SDITF_H

#include <linux/mutex.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mc.h>
#include <linux/rk-camera-module.h>
#include "hw.h"
#include "../isp/isp_external.h"

#define RKISP0_DEVNAME "rkisp0"
#define RKISP1_DEVNAME "rkisp1"
#define RKISP_UNITE_DEVNAME "rkisp-unite"

#define RKCIF_TOISP_CH0	0
#define RKCIF_TOISP_CH1	1
#define RKCIF_TOISP_CH2	2
#define TOISP_CH_MAX 3

#define SDITF_PIXEL_RATE_MAX (1000000000)

struct capture_info {
	unsigned int offset_x;
	unsigned int offset_y;
	unsigned int width;
	unsigned int height;
};

enum toisp_link_mode {
	TOISP_NONE,
	TOISP0,
	TOISP1,
	TOISP_UNITE,
};

struct toisp_ch_info {
	bool is_valid;
	int id;
};

struct toisp_info {
	struct toisp_ch_info ch_info[TOISP_CH_MAX];
	enum toisp_link_mode link_mode;
};

struct sditf_work_struct {
	struct work_struct	work;
	struct rkisp_rx_buffer *buf;
};

struct sditf_priv {
	struct device *dev;
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev sd;
	struct media_pad pads[2];
	struct rkcif_device *cif_dev;
	struct rkmodule_hdr_cfg	hdr_cfg;
	struct capture_info cap_info;
	struct rkisp_vicap_mode mode;
	struct toisp_info toisp_inf;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_subdev *sensor_sd;
	struct sditf_work_struct buffree_work;
	struct list_head buf_free_list;
	int buf_num;
	int num_sensors;
	int combine_index;
	bool is_combine_mode;
	atomic_t power_cnt;
	atomic_t stream_cnt;
};

extern struct platform_driver rkcif_subdev_driver;
void sditf_change_to_online(struct sditf_priv *priv);

#endif
