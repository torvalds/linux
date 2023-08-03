/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 *
 */

#ifndef __MAXIM4C_DRV_H__
#define __MAXIM4C_DRV_H__

#include <linux/workqueue.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "maxim4c_i2c.h"
#include "maxim4c_link.h"
#include "maxim4c_mipi_txphy.h"
#include "maxim4c_remote.h"
#include "maxim4c_pattern.h"

#define MAXIM4C_REG_CHIP_ID		0x0D
#define MAX96712_CHIP_ID		0xA0
#define MAX96722_CHIP_ID		0xA1

enum {
	MAXIM4C_HOT_PLUG_OUT = 0,
	MAXIM4C_HOT_PLUG_IN,
};

struct maxim4c_hot_plug_work {
	struct workqueue_struct *state_check_wq;
	struct delayed_work state_d_work;
	u32 hot_plug_state;
};

struct maxim4c_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 link_freq_idx;
	u32 bus_fmt;
	u32 bpp;
	const struct regval *reg_list;
	u32 vc[PAD_MAX];
};

typedef struct maxim4c {
	struct i2c_client *client;
	struct clk *xvclk;
	struct gpio_desc *power_gpio;
	struct gpio_desc *pocen_gpio;
	struct gpio_desc *lock_gpio;

	struct mutex mutex;

	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *anal_gain;
	struct v4l2_ctrl *digi_gain;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *link_freq;
	struct v4l2_fwnode_endpoint bus_cfg;

	u32 chipid;

	bool streaming;
	bool power_on;
	bool hot_plug;
	u8 is_reset;
	int hot_plug_irq;
	u32 hot_plug_state;
	u32 link_lock_state;
	struct maxim4c_hot_plug_work hot_plug_work;

	struct maxim4c_mode supported_mode;
	const struct maxim4c_mode *cur_mode;
	u32 cfg_modes_num;

	u32 module_index;
	const char *module_facing;
	const char *module_name;
	const char *len_name;

	maxim4c_gmsl_link_t gmsl_link;
	maxim4c_video_pipe_t video_pipe;
	maxim4c_mipi_txphy_t mipi_txphy;

	struct maxim4c_pattern pattern;

	struct maxim4c_i2c_init_seq extra_init_seq;

	maxim4c_remote_t *remote_device[MAXIM4C_LINK_ID_MAX];
} maxim4c_t;

#endif /* __MAXIM4C_DRV_H__ */
