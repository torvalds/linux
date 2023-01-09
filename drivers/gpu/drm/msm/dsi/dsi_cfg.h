/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
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
#define MSM_DSI_6G_VER_MINOR_V1_4_1	0x10040001
#define MSM_DSI_6G_VER_MINOR_V1_4_2	0x10040002
#define MSM_DSI_6G_VER_MINOR_V2_1_0	0x20010000
#define MSM_DSI_6G_VER_MINOR_V2_2_0	0x20000000
#define MSM_DSI_6G_VER_MINOR_V2_2_1	0x20020001
#define MSM_DSI_6G_VER_MINOR_V2_3_0	0x20030000
#define MSM_DSI_6G_VER_MINOR_V2_4_0	0x20040000
#define MSM_DSI_6G_VER_MINOR_V2_4_1	0x20040001
#define MSM_DSI_6G_VER_MINOR_V2_5_0	0x20050000
#define MSM_DSI_6G_VER_MINOR_V2_6_0	0x20060000
#define MSM_DSI_6G_VER_MINOR_V2_7_0	0x20070000

#define MSM_DSI_V2_VER_MINOR_8064	0x0

#define DSI_6G_REG_SHIFT	4

struct msm_dsi_config {
	u32 io_offset;
	const struct regulator_bulk_data *regulator_data;
	int num_regulators;
	const char * const *bus_clk_names;
	const int num_bus_clks;
	const resource_size_t io_start[DSI_MAX];
	const int num_dsi;
};

struct msm_dsi_host_cfg_ops {
	int (*link_clk_set_rate)(struct msm_dsi_host *msm_host);
	int (*link_clk_enable)(struct msm_dsi_host *msm_host);
	void (*link_clk_disable)(struct msm_dsi_host *msm_host);
	int (*clk_init_ver)(struct msm_dsi_host *msm_host);
	int (*tx_buf_alloc)(struct msm_dsi_host *msm_host, int size);
	void* (*tx_buf_get)(struct msm_dsi_host *msm_host);
	void (*tx_buf_put)(struct msm_dsi_host *msm_host);
	int (*dma_base_get)(struct msm_dsi_host *msm_host, uint64_t *iova);
	int (*calc_clk_rate)(struct msm_dsi_host *msm_host, bool is_bonded_dsi);
};

struct msm_dsi_cfg_handler {
	u32 major;
	u32 minor;
	const struct msm_dsi_config *cfg;
	const struct msm_dsi_host_cfg_ops *ops;
};

const struct msm_dsi_cfg_handler *msm_dsi_cfg_get(u32 major, u32 minor);

/* Non autodetect configs */
extern const struct msm_dsi_cfg_handler qcm2290_dsi_cfg_handler;

#endif /* __MSM_DSI_CFG_H__ */

