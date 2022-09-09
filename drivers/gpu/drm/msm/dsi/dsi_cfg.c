// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include "dsi_cfg.h"

static const char * const dsi_v2_bus_clk_names[] = {
	"core_mmss", "iface", "bus",
};

static const struct msm_dsi_config apq8064_dsi_cfg = {
	.io_offset = 0,
	.reg_cfg = {
		.num = 3,
		.regs = {
			{"vdda", 100000, 100},	/* 1.2 V */
			{"avdd", 10000, 100},	/* 3.0 V */
			{"vddio", 100000, 100},	/* 1.8 V */
		},
	},
	.bus_clk_names = dsi_v2_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_v2_bus_clk_names),
	.io_start = { 0x4700000, 0x5800000 },
	.num_dsi = 2,
};

static const char * const dsi_6g_bus_clk_names[] = {
	"mdp_core", "iface", "bus", "core_mmss",
};

static const struct msm_dsi_config msm8974_apq8084_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 3,
		.regs = {
			{"vdd", 150000, 100},	/* 3.0 V */
			{"vdda", 100000, 100},	/* 1.2 V */
			{"vddio", 100000, 100},	/* 1.8 V */
		},
	},
	.bus_clk_names = dsi_6g_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_6g_bus_clk_names),
	.io_start = { 0xfd922800, 0xfd922b00 },
	.num_dsi = 2,
};

static const char * const dsi_8916_bus_clk_names[] = {
	"mdp_core", "iface", "bus",
};

static const struct msm_dsi_config msm8916_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 2,
		.regs = {
			{"vdda", 100000, 100},	/* 1.2 V */
			{"vddio", 100000, 100},	/* 1.8 V */
		},
	},
	.bus_clk_names = dsi_8916_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_8916_bus_clk_names),
	.io_start = { 0x1a98000 },
	.num_dsi = 1,
};

static const char * const dsi_8976_bus_clk_names[] = {
	"mdp_core", "iface", "bus",
};

static const struct msm_dsi_config msm8976_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 2,
		.regs = {
			{"vdda", 100000, 100},	/* 1.2 V */
			{"vddio", 100000, 100},	/* 1.8 V */
		},
	},
	.bus_clk_names = dsi_8976_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_8976_bus_clk_names),
	.io_start = { 0x1a94000, 0x1a96000 },
	.num_dsi = 2,
};

static const struct msm_dsi_config msm8994_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 6,
		.regs = {
			{"vdda", 100000, 100},	/* 1.25 V */
			{"vddio", 100000, 100},	/* 1.8 V */
			{"vcca", 10000, 100},	/* 1.0 V */
			{"vdd", 100000, 100},	/* 1.8 V */
			{"lab_reg", -1, -1},
			{"ibb_reg", -1, -1},
		},
	},
	.bus_clk_names = dsi_6g_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_6g_bus_clk_names),
	.io_start = { 0xfd998000, 0xfd9a0000 },
	.num_dsi = 2,
};

static const char * const dsi_8996_bus_clk_names[] = {
	"mdp_core", "iface", "bus", "core_mmss",
};

static const struct msm_dsi_config msm8996_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 3,
		.regs = {
			{"vdda", 18160, 1 },	/* 1.25 V */
			{"vcca", 17000, 32 },	/* 0.925 V */
			{"vddio", 100000, 100 },/* 1.8 V */
		},
	},
	.bus_clk_names = dsi_8996_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_8996_bus_clk_names),
	.io_start = { 0x994000, 0x996000 },
	.num_dsi = 2,
};

static const char * const dsi_msm8998_bus_clk_names[] = {
	"iface", "bus", "core",
};

static const struct msm_dsi_config msm8998_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 2,
		.regs = {
			{"vdd", 367000, 16 },	/* 0.9 V */
			{"vdda", 62800, 2 },	/* 1.2 V */
		},
	},
	.bus_clk_names = dsi_msm8998_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_msm8998_bus_clk_names),
	.io_start = { 0xc994000, 0xc996000 },
	.num_dsi = 2,
};

static const char * const dsi_sdm660_bus_clk_names[] = {
	"iface", "bus", "core", "core_mmss",
};

static const struct msm_dsi_config sdm660_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vdda", 12560, 4 },	/* 1.2 V */
		},
	},
	.bus_clk_names = dsi_sdm660_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_sdm660_bus_clk_names),
	.io_start = { 0xc994000, 0xc996000 },
	.num_dsi = 2,
};

static const char * const dsi_sdm845_bus_clk_names[] = {
	"iface", "bus",
};

static const char * const dsi_sc7180_bus_clk_names[] = {
	"iface", "bus",
};

static const struct msm_dsi_config sdm845_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vdda", 21800, 4 },	/* 1.2 V */
		},
	},
	.bus_clk_names = dsi_sdm845_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_sdm845_bus_clk_names),
	.io_start = { 0xae94000, 0xae96000 },
	.num_dsi = 2,
};

static const struct msm_dsi_config sc7180_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vdda", 21800, 4 },	/* 1.2 V */
		},
	},
	.bus_clk_names = dsi_sc7180_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_sc7180_bus_clk_names),
	.io_start = { 0xae94000 },
	.num_dsi = 1,
};

static const char * const dsi_sc7280_bus_clk_names[] = {
	"iface", "bus",
};

static const struct msm_dsi_config sc7280_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vdda", 8350, 0 },	/* 1.2 V */
		},
	},
	.bus_clk_names = dsi_sc7280_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_sc7280_bus_clk_names),
	.io_start = { 0xae94000 },
	.num_dsi = 1,
};

static const char * const dsi_qcm2290_bus_clk_names[] = {
	"iface", "bus",
};

static const struct msm_dsi_config qcm2290_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vdda", 21800, 4 },	/* 1.2 V */
		},
	},
	.bus_clk_names = dsi_qcm2290_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_qcm2290_bus_clk_names),
	.io_start = { 0x5e94000 },
	.num_dsi = 1,
};

static const struct msm_dsi_host_cfg_ops msm_dsi_v2_host_ops = {
	.link_clk_set_rate = dsi_link_clk_set_rate_v2,
	.link_clk_enable = dsi_link_clk_enable_v2,
	.link_clk_disable = dsi_link_clk_disable_v2,
	.clk_init_ver = dsi_clk_init_v2,
	.tx_buf_alloc = dsi_tx_buf_alloc_v2,
	.tx_buf_get = dsi_tx_buf_get_v2,
	.tx_buf_put = NULL,
	.dma_base_get = dsi_dma_base_get_v2,
	.calc_clk_rate = dsi_calc_clk_rate_v2,
};

static const struct msm_dsi_host_cfg_ops msm_dsi_6g_host_ops = {
	.link_clk_set_rate = dsi_link_clk_set_rate_6g,
	.link_clk_enable = dsi_link_clk_enable_6g,
	.link_clk_disable = dsi_link_clk_disable_6g,
	.clk_init_ver = NULL,
	.tx_buf_alloc = dsi_tx_buf_alloc_6g,
	.tx_buf_get = dsi_tx_buf_get_6g,
	.tx_buf_put = dsi_tx_buf_put_6g,
	.dma_base_get = dsi_dma_base_get_6g,
	.calc_clk_rate = dsi_calc_clk_rate_6g,
};

static const struct msm_dsi_host_cfg_ops msm_dsi_6g_v2_host_ops = {
	.link_clk_set_rate = dsi_link_clk_set_rate_6g,
	.link_clk_enable = dsi_link_clk_enable_6g,
	.link_clk_disable = dsi_link_clk_disable_6g,
	.clk_init_ver = dsi_clk_init_6g_v2,
	.tx_buf_alloc = dsi_tx_buf_alloc_6g,
	.tx_buf_get = dsi_tx_buf_get_6g,
	.tx_buf_put = dsi_tx_buf_put_6g,
	.dma_base_get = dsi_dma_base_get_6g,
	.calc_clk_rate = dsi_calc_clk_rate_6g,
};

static const struct msm_dsi_cfg_handler dsi_cfg_handlers[] = {
	{MSM_DSI_VER_MAJOR_V2, MSM_DSI_V2_VER_MINOR_8064,
		&apq8064_dsi_cfg, &msm_dsi_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_0,
		&msm8974_apq8084_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_1,
		&msm8974_apq8084_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_1_1,
		&msm8974_apq8084_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_2,
		&msm8974_apq8084_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_3,
		&msm8994_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_3_1,
		&msm8916_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_4_1,
		&msm8996_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_4_2,
		&msm8976_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_1_0,
		&sdm660_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_2_0,
		&msm8998_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_2_1,
		&sdm845_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_3_0,
		&sdm845_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_4_0,
		&sdm845_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_4_1,
		&sc7180_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_5_0,
		&sc7280_dsi_cfg, &msm_dsi_6g_v2_host_ops},
};

const struct msm_dsi_cfg_handler *msm_dsi_cfg_get(u32 major, u32 minor)
{
	const struct msm_dsi_cfg_handler *cfg_hnd = NULL;
	int i;

	for (i = ARRAY_SIZE(dsi_cfg_handlers) - 1; i >= 0; i--) {
		if ((dsi_cfg_handlers[i].major == major) &&
			(dsi_cfg_handlers[i].minor == minor)) {
			cfg_hnd = &dsi_cfg_handlers[i];
			break;
		}
	}

	return cfg_hnd;
}

/*  Non autodetect configs */
const struct msm_dsi_cfg_handler qcm2290_dsi_cfg_handler = {
	.cfg = &qcm2290_dsi_cfg,
	.ops = &msm_dsi_6g_v2_host_ops,
};
