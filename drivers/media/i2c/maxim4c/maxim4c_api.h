/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Maxim Quad GMSL Deserializer driver API function declaration
 *
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Cai Wenzhong <cwz@rock-chips.com>
 */

#ifndef __MAXIM4C_API_H__
#define __MAXIM4C_API_H__

#include "maxim4c_i2c.h"
#include "maxim4c_link.h"
#include "maxim4c_video_pipe.h"
#include "maxim4c_mipi_txphy.h"
#include "maxim4c_remote.h"
#include "maxim4c_pattern.h"
#include "maxim4c_drv.h"

#define MAXIM4C_NAME			"maxim4c"

/* Maxim Deserializer Test Pattern */
#define MAXIM4C_TEST_PATTERN		0

/* maxim4c i2c api */
int maxim4c_i2c_write_reg(struct i2c_client *client,
		u16 reg_addr, u16 reg_len, u16 val_len, u32 reg_val);
int maxim4c_i2c_read_reg(struct i2c_client *client,
		u16 reg_addr, u16 reg_len, u16 val_len, u32 *reg_val);
int maxim4c_i2c_update_reg(struct i2c_client *client,
		u16 reg_addr, u16 reg_len,
		u32 val_len, u32 val_mask, u32 reg_val);

int maxim4c_i2c_write_byte(struct i2c_client *client,
		u16 reg_addr, u16 reg_len, u8 reg_val);
int maxim4c_i2c_read_byte(struct i2c_client *client,
		u16 reg_addr, u16 reg_len, u8 *reg_val);
int maxim4c_i2c_update_byte(struct i2c_client *client,
		u16 reg_addr, u16 reg_len, u8 val_mask, u8 reg_val);

int maxim4c_i2c_write_array(struct i2c_client *client,
				const struct maxim4c_i2c_regval *regs);
int maxim4c_i2c_load_init_seq(struct device *dev,
		struct device_node *node, struct maxim4c_i2c_init_seq *init_seq);
int maxim4c_i2c_run_init_seq(struct i2c_client *client,
			struct maxim4c_i2c_init_seq *init_seq);

/* maxim4c link api */
u8 maxim4c_link_get_lock_state(maxim4c_t *maxim4c, u8 link_mask);
int maxim4c_link_oneshot_reset(maxim4c_t *maxim4c, u8 link_mask);
int maxim4c_link_mask_enable(maxim4c_t *maxim4c, u8 link_mask, bool enable);
int maxim4c_link_wait_linklock(maxim4c_t *maxim4c, u8 link_mask);
int maxim4c_link_select_remote_enable(maxim4c_t *maxim4c, u8 link_mask);
int maxim4c_link_select_remote_control(maxim4c_t *maxim4c, u8 link_mask);
int maxim4c_link_hw_init(maxim4c_t *maxim4c);
void maxim4c_link_data_init(maxim4c_t *maxim4c);
int maxim4c_link_parse_dt(maxim4c_t *maxim4c, struct device_node *of_node);

/* maxim4c video pipe api */
int maxim4c_video_pipe_hw_init(maxim4c_t *maxim4c);
int maxim4c_video_pipe_mask_enable(maxim4c_t *maxim4c, u8 video_pipe_mask, bool enable);
int maxim4c_video_pipe_linkid_enable(maxim4c_t *maxim4c, u8 link_id, bool enable);
void maxim4c_video_pipe_data_init(maxim4c_t *maxim4c);
int maxim4c_video_pipe_parse_dt(maxim4c_t *maxim4c, struct device_node *of_node);

/* maxim4c mipi txphy api */
int maxim4c_mipi_txphy_hw_init(maxim4c_t *maxim4c);
void maxim4c_mipi_txphy_data_init(maxim4c_t *maxim4c);
int maxim4c_mipi_txphy_parse_dt(maxim4c_t *maxim4c, struct device_node *of_node);
int maxim4c_mipi_txphy_enable(maxim4c_t *maxim4c, bool enable);
int maxim4c_dphy_dpll_predef_set(maxim4c_t *maxim4c, s64 link_freq_hz);
int maxim4c_mipi_csi_output(maxim4c_t *maxim4c, bool enable);

/* maxim4c remote api */
int maxim4c_remote_mfd_add_devices(maxim4c_t *maxim4c);
int maxim4c_remote_devices_init(maxim4c_t *maxim4c, u8 link_init_mask);
int maxim4c_remote_devices_deinit(maxim4c_t *maxim4c, u8 link_init_mask);
int maxim4c_remote_load_init_seq(maxim4c_remote_t *remote_device);
int maxim4c_remote_device_register(maxim4c_t *maxim4c,
		maxim4c_remote_t *remote_device);

/* maxim4c v4l2 subdev api */
int maxim4c_v4l2_subdev_init(maxim4c_t *maxim4c);
void maxim4c_v4l2_subdev_deinit(maxim4c_t *maxim4c);

/* maxim4c pattern api */
int maxim4c_pattern_init(maxim4c_t *maxim4c);
int maxim4c_pattern_enable(maxim4c_t *maxim4c, bool enable);

#endif /* __MAXIM4C_API_H__ */
