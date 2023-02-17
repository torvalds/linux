/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Techpoint Dev Driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef _TECHPOINT_DEV_H
#define _TECHPOINT_DEV_H

#include "techpoint_common.h"

struct regval {
	u8 addr;
	u8 val;
};

int techpoint_write_reg(struct i2c_client *client, u8 reg, u8 val);
int techpoint_read_reg(struct i2c_client *client, u8 reg, u8 *val);
int techpoint_write_array(struct i2c_client *client,
			  const struct regval *regs, int size);

void __techpoint_get_vc_fmt_inf(struct techpoint *techpoint,
				struct rkmodule_vc_fmt_info *inf);
void techpoint_get_vc_fmt_inf(struct techpoint *techpoint,
			      struct rkmodule_vc_fmt_info *inf);
void techpoint_get_vc_hotplug_inf(struct techpoint *techpoint,
				  struct rkmodule_vc_hotplug_info *inf);
void techpoint_set_quick_stream(struct techpoint *techpoint, u32 stream);

int techpoint_initialize_devices(struct techpoint *techpoint);
int techpoint_start_video_stream(struct techpoint *techpoint);
int techpoint_stop_video_stream(struct techpoint *techpoint);

#endif // _TECHPOINT_DEV_H
