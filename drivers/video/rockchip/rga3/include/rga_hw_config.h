/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#ifndef __LINUX_RGA_HW_CONFIG_H_
#define __LINUX_RGA_HW_CONFIG_H_

#include "rga_drv.h"

struct rga_rect {
	int w;
	int h;
};

struct rga_win_data {
	const char *name;
	const uint32_t *raster_formats;
	const uint32_t *fbc_formats;
	const uint32_t *tile_formats;
	uint32_t num_of_raster_formats;
	uint32_t num_of_fbc_formats;
	uint32_t num_of_tile_formats;

	const unsigned int supported_rotations;
	const unsigned int scale_up_mode;
	const unsigned int scale_down_mode;
	const unsigned int rd_mode;

};

struct rga_hw_data {
	uint32_t version;
	uint32_t feature;

	uint32_t csc_r2y_mode;
	uint32_t csc_y2r_mode;

	struct rga_rect max_input;
	struct rga_rect max_output;
	struct rga_rect min_input;
	struct rga_rect min_output;

	unsigned int max_upscale_factor;
	unsigned int max_downscale_factor;

	const struct rga_win_data *win;
	unsigned int win_size;
};

extern const struct rga_hw_data rga3_data;
extern const struct rga_hw_data rga2e_data;

#endif /* __LINUX_RGA_HW_CONFIG_H_ */
