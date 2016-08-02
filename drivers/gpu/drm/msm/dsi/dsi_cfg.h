/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MSM_DSI_CFG_H__
#define __MSM_DSI_CFG_H__

#include "dsi.h"

#define MSM_DSI_VER_MAJOR_V2	0x02
#define MSM_DSI_VER_MAJOR_6G	0x03
#define MSM_DSI_6G_VER_MINOR_V1_0	0x10000000
#define MSM_DSI_6G_VER_MINOR_V1_1	0x10010000
#define MSM_DSI_6G_VER_MINOR_V1_1_1	0x10010001
#define MSM_DSI_6G_VER_MINOR_V1_2	0x10020000
#define MSM_DSI_6G_VER_MINOR_V1_3	0x10030000
#define MSM_DSI_6G_VER_MINOR_V1_3_1	0x10030001

#define MSM_DSI_V2_VER_MINOR_8064	0x0

#define DSI_6G_REG_SHIFT	4

struct msm_dsi_config {
	u32 io_offset;
	struct dsi_reg_config reg_cfg;
	const char * const *bus_clk_names;
	const int num_bus_clks;
	const resource_size_t io_start[DSI_MAX];
	const int num_dsi;
};

struct msm_dsi_cfg_handler {
	u32 major;
	u32 minor;
	const struct msm_dsi_config *cfg;
};

const struct msm_dsi_cfg_handler *msm_dsi_cfg_get(u32 major, u32 minor);

#endif /* __MSM_DSI_CFG_H__ */

