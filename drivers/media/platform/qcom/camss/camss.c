// SPDX-License-Identifier: GPL-2.0
/*
 * camss.c
 *
 * Qualcomm MSM Camera Subsystem - Core
 *
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015-2018 Linaro Ltd.
 */
#include <linux/clk.h>
#include <linux/interconnect.h>
#include <linux/media-bus-format.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-fwnode.h>

#include "camss.h"

#define CAMSS_CLOCK_MARGIN_NUMERATOR 105
#define CAMSS_CLOCK_MARGIN_DENOMINATOR 100

static const struct camss_subdev_resources csiphy_res_8x16[] = {
	/* CSIPHY0 */
	{
		.regulators = {},
		.clock = { "top_ahb", "ispif_ahb", "ahb", "csiphy0_timer" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000 } },
		.reg = { "csiphy0", "csiphy0_clk_mux" },
		.interrupt = { "csiphy0" },
		.ops = &csiphy_ops_2ph_1_0
	},

	/* CSIPHY1 */
	{
		.regulators = {},
		.clock = { "top_ahb", "ispif_ahb", "ahb", "csiphy1_timer" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000 } },
		.reg = { "csiphy1", "csiphy1_clk_mux" },
		.interrupt = { "csiphy1" },
		.ops = &csiphy_ops_2ph_1_0
	}
};

static const struct camss_subdev_resources csid_res_8x16[] = {
	/* CSID0 */
	{
		.regulators = { "vdda" },
		.clock = { "top_ahb", "ispif_ahb", "csi0_ahb", "ahb",
			   "csi0", "csi0_phy", "csi0_pix", "csi0_rdi" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "csid0" },
		.interrupt = { "csid0" },
		.ops = &csid_ops_4_1,
	},

	/* CSID1 */
	{
		.regulators = { "vdda" },
		.clock = { "top_ahb", "ispif_ahb", "csi1_ahb", "ahb",
			   "csi1", "csi1_phy", "csi1_pix", "csi1_rdi" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "csid1" },
		.interrupt = { "csid1" },
		.ops = &csid_ops_4_1,
	},
};

static const struct camss_subdev_resources ispif_res_8x16 = {
	/* ISPIF */
	.clock = { "top_ahb", "ahb", "ispif_ahb",
		   "csi0", "csi0_pix", "csi0_rdi",
		   "csi1", "csi1_pix", "csi1_rdi" },
	.clock_for_reset = { "vfe0", "csi_vfe0" },
	.reg = { "ispif", "csi_clk_mux" },
	.interrupt = { "ispif" }

};

static const struct camss_subdev_resources vfe_res_8x16[] = {
	/* VFE0 */
	{
		.regulators = {},
		.clock = { "top_ahb", "vfe0", "csi_vfe0",
			   "vfe_ahb", "vfe_axi", "ahb" },
		.clock_rate = { { 0 },
				{ 50000000, 80000000, 100000000, 160000000,
				  177780000, 200000000, 266670000, 320000000,
				  400000000, 465000000 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "vfe0" },
		.interrupt = { "vfe0" },
		.line_num = 3,
		.ops = &vfe_ops_4_1
	}
};

static const struct camss_subdev_resources csiphy_res_8x96[] = {
	/* CSIPHY0 */
	{
		.regulators = {},
		.clock = { "top_ahb", "ispif_ahb", "ahb", "csiphy0_timer" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 266666667 } },
		.reg = { "csiphy0", "csiphy0_clk_mux" },
		.interrupt = { "csiphy0" },
		.ops = &csiphy_ops_3ph_1_0
	},

	/* CSIPHY1 */
	{
		.regulators = {},
		.clock = { "top_ahb", "ispif_ahb", "ahb", "csiphy1_timer" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 266666667 } },
		.reg = { "csiphy1", "csiphy1_clk_mux" },
		.interrupt = { "csiphy1" },
		.ops = &csiphy_ops_3ph_1_0
	},

	/* CSIPHY2 */
	{
		.regulators = {},
		.clock = { "top_ahb", "ispif_ahb", "ahb", "csiphy2_timer" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 266666667 } },
		.reg = { "csiphy2", "csiphy2_clk_mux" },
		.interrupt = { "csiphy2" },
		.ops = &csiphy_ops_3ph_1_0
	}
};

static const struct camss_subdev_resources csid_res_8x96[] = {
	/* CSID0 */
	{
		.regulators = { "vdda" },
		.clock = { "top_ahb", "ispif_ahb", "csi0_ahb", "ahb",
			   "csi0", "csi0_phy", "csi0_pix", "csi0_rdi" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 266666667 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "csid0" },
		.interrupt = { "csid0" },
		.ops = &csid_ops_4_7,
	},

	/* CSID1 */
	{
		.regulators = { "vdda" },
		.clock = { "top_ahb", "ispif_ahb", "csi1_ahb", "ahb",
			   "csi1", "csi1_phy", "csi1_pix", "csi1_rdi" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 266666667 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "csid1" },
		.interrupt = { "csid1" },
		.ops = &csid_ops_4_7,
	},

	/* CSID2 */
	{
		.regulators = { "vdda" },
		.clock = { "top_ahb", "ispif_ahb", "csi2_ahb", "ahb",
			   "csi2", "csi2_phy", "csi2_pix", "csi2_rdi" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 266666667 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "csid2" },
		.interrupt = { "csid2" },
		.ops = &csid_ops_4_7,
	},

	/* CSID3 */
	{
		.regulators = { "vdda" },
		.clock = { "top_ahb", "ispif_ahb", "csi3_ahb", "ahb",
			   "csi3", "csi3_phy", "csi3_pix", "csi3_rdi" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 266666667 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "csid3" },
		.interrupt = { "csid3" },
		.ops = &csid_ops_4_7,
	}
};

static const struct camss_subdev_resources ispif_res_8x96 = {
	/* ISPIF */
	.clock = { "top_ahb", "ahb", "ispif_ahb",
		   "csi0", "csi0_pix", "csi0_rdi",
		   "csi1", "csi1_pix", "csi1_rdi",
		   "csi2", "csi2_pix", "csi2_rdi",
		   "csi3", "csi3_pix", "csi3_rdi" },
	.clock_for_reset = { "vfe0", "csi_vfe0", "vfe1", "csi_vfe1" },
	.reg = { "ispif", "csi_clk_mux" },
	.interrupt = { "ispif" }
};

static const struct camss_subdev_resources vfe_res_8x96[] = {
	/* VFE0 */
	{
		.regulators = {},
		.clock = { "top_ahb", "ahb", "vfe0", "csi_vfe0", "vfe_ahb",
			   "vfe0_ahb", "vfe_axi", "vfe0_stream"},
		.clock_rate = { { 0 },
				{ 0 },
				{ 75000000, 100000000, 300000000,
				  320000000, 480000000, 600000000 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "vfe0" },
		.interrupt = { "vfe0" },
		.line_num = 3,
		.ops = &vfe_ops_4_7
	},

	/* VFE1 */
	{
		.regulators = {},
		.clock = { "top_ahb", "ahb", "vfe1", "csi_vfe1", "vfe_ahb",
			   "vfe1_ahb", "vfe_axi", "vfe1_stream"},
		.clock_rate = { { 0 },
				{ 0 },
				{ 75000000, 100000000, 300000000,
				  320000000, 480000000, 600000000 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "vfe1" },
		.interrupt = { "vfe1" },
		.line_num = 3,
		.ops = &vfe_ops_4_7
	}
};

static const struct camss_subdev_resources csiphy_res_660[] = {
	/* CSIPHY0 */
	{
		.regulators = {},
		.clock = { "top_ahb", "ispif_ahb", "ahb", "csiphy0_timer",
			   "csi0_phy", "csiphy_ahb2crif" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 269333333 },
				{ 0 } },
		.reg = { "csiphy0", "csiphy0_clk_mux" },
		.interrupt = { "csiphy0" },
		.ops = &csiphy_ops_3ph_1_0
	},

	/* CSIPHY1 */
	{
		.regulators = {},
		.clock = { "top_ahb", "ispif_ahb", "ahb", "csiphy1_timer",
			   "csi1_phy", "csiphy_ahb2crif" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 269333333 },
				{ 0 } },
		.reg = { "csiphy1", "csiphy1_clk_mux" },
		.interrupt = { "csiphy1" },
		.ops = &csiphy_ops_3ph_1_0
	},

	/* CSIPHY2 */
	{
		.regulators = {},
		.clock = { "top_ahb", "ispif_ahb", "ahb", "csiphy2_timer",
			   "csi2_phy", "csiphy_ahb2crif" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 269333333 },
				{ 0 } },
		.reg = { "csiphy2", "csiphy2_clk_mux" },
		.interrupt = { "csiphy2" },
		.ops = &csiphy_ops_3ph_1_0
	}
};

static const struct camss_subdev_resources csid_res_660[] = {
	/* CSID0 */
	{
		.regulators = { "vdda", "vdd_sec" },
		.clock = { "top_ahb", "ispif_ahb", "csi0_ahb", "ahb",
			   "csi0", "csi0_phy", "csi0_pix", "csi0_rdi",
			   "cphy_csid0" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 310000000,
				  404000000, 465000000 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "csid0" },
		.interrupt = { "csid0" },
		.ops = &csid_ops_4_7,
	},

	/* CSID1 */
	{
		.regulators = { "vdda", "vdd_sec" },
		.clock = { "top_ahb", "ispif_ahb", "csi1_ahb", "ahb",
			   "csi1", "csi1_phy", "csi1_pix", "csi1_rdi",
			   "cphy_csid1" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 310000000,
				  404000000, 465000000 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "csid1" },
		.interrupt = { "csid1" },
		.ops = &csid_ops_4_7,
	},

	/* CSID2 */
	{
		.regulators = { "vdda", "vdd_sec" },
		.clock = { "top_ahb", "ispif_ahb", "csi2_ahb", "ahb",
			   "csi2", "csi2_phy", "csi2_pix", "csi2_rdi",
			   "cphy_csid2" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 310000000,
				  404000000, 465000000 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "csid2" },
		.interrupt = { "csid2" },
		.ops = &csid_ops_4_7,
	},

	/* CSID3 */
	{
		.regulators = { "vdda", "vdd_sec" },
		.clock = { "top_ahb", "ispif_ahb", "csi3_ahb", "ahb",
			   "csi3", "csi3_phy", "csi3_pix", "csi3_rdi",
			   "cphy_csid3" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 310000000,
				  404000000, 465000000 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "csid3" },
		.interrupt = { "csid3" },
		.ops = &csid_ops_4_7,
	}
};

static const struct camss_subdev_resources ispif_res_660 = {
	/* ISPIF */
	.clock = { "top_ahb", "ahb", "ispif_ahb",
		   "csi0", "csi0_pix", "csi0_rdi",
		   "csi1", "csi1_pix", "csi1_rdi",
		   "csi2", "csi2_pix", "csi2_rdi",
		   "csi3", "csi3_pix", "csi3_rdi" },
	.clock_for_reset = { "vfe0", "csi_vfe0", "vfe1", "csi_vfe1" },
	.reg = { "ispif", "csi_clk_mux" },
	.interrupt = { "ispif" }
};

static const struct camss_subdev_resources vfe_res_660[] = {
	/* VFE0 */
	{
		.regulators = {},
		.clock = { "throttle_axi", "top_ahb", "ahb", "vfe0",
			   "csi_vfe0", "vfe_ahb", "vfe0_ahb", "vfe_axi",
			   "vfe0_stream"},
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 120000000, 200000000, 256000000,
				  300000000, 404000000, 480000000,
				  540000000, 576000000 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "vfe0" },
		.interrupt = { "vfe0" },
		.line_num = 3,
		.ops = &vfe_ops_4_8
	},

	/* VFE1 */
	{
		.regulators = {},
		.clock = { "throttle_axi", "top_ahb", "ahb", "vfe1",
			   "csi_vfe1", "vfe_ahb", "vfe1_ahb", "vfe_axi",
			   "vfe1_stream"},
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 120000000, 200000000, 256000000,
				  300000000, 404000000, 480000000,
				  540000000, 576000000 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "vfe1" },
		.interrupt = { "vfe1" },
		.line_num = 3,
		.ops = &vfe_ops_4_8
	}
};

static const struct camss_subdev_resources csiphy_res_845[] = {
	/* CSIPHY0 */
	{
		.regulators = {},
		.clock = { "camnoc_axi", "soc_ahb", "slow_ahb_src",
				"cpas_ahb", "cphy_rx_src", "csiphy0",
				"csiphy0_timer_src", "csiphy0_timer" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 19200000, 240000000, 269333333 } },
		.reg = { "csiphy0" },
		.interrupt = { "csiphy0" },
		.ops = &csiphy_ops_3ph_1_0
	},

	/* CSIPHY1 */
	{
		.regulators = {},
		.clock = { "camnoc_axi", "soc_ahb", "slow_ahb_src",
				"cpas_ahb", "cphy_rx_src", "csiphy1",
				"csiphy1_timer_src", "csiphy1_timer" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 19200000, 240000000, 269333333 } },
		.reg = { "csiphy1" },
		.interrupt = { "csiphy1" },
		.ops = &csiphy_ops_3ph_1_0
	},

	/* CSIPHY2 */
	{
		.regulators = {},
		.clock = { "camnoc_axi", "soc_ahb", "slow_ahb_src",
				"cpas_ahb", "cphy_rx_src", "csiphy2",
				"csiphy2_timer_src", "csiphy2_timer" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 19200000, 240000000, 269333333 } },
		.reg = { "csiphy2" },
		.interrupt = { "csiphy2" },
		.ops = &csiphy_ops_3ph_1_0
	},

	/* CSIPHY3 */
	{
		.regulators = {},
		.clock = { "camnoc_axi", "soc_ahb", "slow_ahb_src",
				"cpas_ahb", "cphy_rx_src", "csiphy3",
				"csiphy3_timer_src", "csiphy3_timer" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 19200000, 240000000, 269333333 } },
		.reg = { "csiphy3" },
		.interrupt = { "csiphy3" },
		.ops = &csiphy_ops_3ph_1_0
	}
};

static const struct camss_subdev_resources csid_res_845[] = {
	/* CSID0 */
	{
		.regulators = { "vdda-phy", "vdda-pll" },
		.clock = { "cpas_ahb", "cphy_rx_src", "slow_ahb_src",
				"soc_ahb", "vfe0", "vfe0_src",
				"vfe0_cphy_rx", "csi0",
				"csi0_src" },
		.clock_rate = { { 0 },
				{ 384000000 },
				{ 80000000 },
				{ 0 },
				{ 19200000, 100000000, 320000000, 404000000, 480000000, 600000000 },
				{ 320000000 },
				{ 0 },
				{ 19200000, 75000000, 384000000, 538666667 },
				{ 384000000 } },
		.reg = { "csid0" },
		.interrupt = { "csid0" },
		.ops = &csid_ops_gen2
	},

	/* CSID1 */
	{
		.regulators = { "vdda-phy", "vdda-pll" },
		.clock = { "cpas_ahb", "cphy_rx_src", "slow_ahb_src",
				"soc_ahb", "vfe1", "vfe1_src",
				"vfe1_cphy_rx", "csi1",
				"csi1_src" },
		.clock_rate = { { 0 },
				{ 384000000 },
				{ 80000000 },
				{ 0 },
				{ 19200000, 100000000, 320000000, 404000000, 480000000, 600000000 },
				{ 320000000 },
				{ 0 },
				{ 19200000, 75000000, 384000000, 538666667 },
				{ 384000000 } },
		.reg = { "csid1" },
		.interrupt = { "csid1" },
		.ops = &csid_ops_gen2
	},

	/* CSID2 */
	{
		.regulators = { "vdda-phy", "vdda-pll" },
		.clock = { "cpas_ahb", "cphy_rx_src", "slow_ahb_src",
				"soc_ahb", "vfe_lite", "vfe_lite_src",
				"vfe_lite_cphy_rx", "csi2",
				"csi2_src" },
		.clock_rate = { { 0 },
				{ 384000000 },
				{ 80000000 },
				{ 0 },
				{ 19200000, 100000000, 320000000, 404000000, 480000000, 600000000 },
				{ 320000000 },
				{ 0 },
				{ 19200000, 75000000, 384000000, 538666667 },
				{ 384000000 } },
		.reg = { "csid2" },
		.interrupt = { "csid2" },
		.ops = &csid_ops_gen2
	}
};

static const struct camss_subdev_resources vfe_res_845[] = {
	/* VFE0 */
	{
		.regulators = {},
		.clock = { "camnoc_axi", "cpas_ahb", "slow_ahb_src",
				"soc_ahb", "vfe0", "vfe0_axi",
				"vfe0_src", "csi0",
				"csi0_src"},
		.clock_rate = { { 0 },
				{ 0 },
				{ 80000000 },
				{ 0 },
				{ 19200000, 100000000, 320000000, 404000000, 480000000, 600000000 },
				{ 0 },
				{ 320000000 },
				{ 19200000, 75000000, 384000000, 538666667 },
				{ 384000000 } },
		.reg = { "vfe0" },
		.interrupt = { "vfe0" },
		.line_num = 4,
		.ops = &vfe_ops_170
	},

	/* VFE1 */
	{
		.regulators = {},
		.clock = { "camnoc_axi", "cpas_ahb", "slow_ahb_src",
				"soc_ahb", "vfe1", "vfe1_axi",
				"vfe1_src", "csi1",
				"csi1_src"},
		.clock_rate = { { 0 },
				{ 0 },
				{ 80000000 },
				{ 0 },
				{ 19200000, 100000000, 320000000, 404000000, 480000000, 600000000 },
				{ 0 },
				{ 320000000 },
				{ 19200000, 75000000, 384000000, 538666667 },
				{ 384000000 } },
		.reg = { "vfe1" },
		.interrupt = { "vfe1" },
		.line_num = 4,
		.ops = &vfe_ops_170
	},

	/* VFE-lite */
	{
		.regulators = {},
		.clock = { "camnoc_axi", "cpas_ahb", "slow_ahb_src",
				"soc_ahb", "vfe_lite",
				"vfe_lite_src", "csi2",
				"csi2_src"},
		.clock_rate = { { 0 },
				{ 0 },
				{ 80000000 },
				{ 0 },
				{ 19200000, 100000000, 320000000, 404000000, 480000000, 600000000 },
				{ 320000000 },
				{ 19200000, 75000000, 384000000, 538666667 },
				{ 384000000 } },
		.reg = { "vfe_lite" },
		.interrupt = { "vfe_lite" },
		.line_num = 4,
		.ops = &vfe_ops_170
	}
};

static const struct camss_subdev_resources csiphy_res_8250[] = {
	/* CSIPHY0 */
	{
		.regulators = {},
		.clock = { "csiphy0", "csiphy0_timer" },
		.clock_rate = { { 400000000 },
				{ 300000000 } },
		.reg = { "csiphy0" },
		.interrupt = { "csiphy0" },
		.ops = &csiphy_ops_3ph_1_0
	},
	/* CSIPHY1 */
	{
		.regulators = {},
		.clock = { "csiphy1", "csiphy1_timer" },
		.clock_rate = { { 400000000 },
				{ 300000000 } },
		.reg = { "csiphy1" },
		.interrupt = { "csiphy1" },
		.ops = &csiphy_ops_3ph_1_0
	},
	/* CSIPHY2 */
	{
		.regulators = {},
		.clock = { "csiphy2", "csiphy2_timer" },
		.clock_rate = { { 400000000 },
				{ 300000000 } },
		.reg = { "csiphy2" },
		.interrupt = { "csiphy2" },
		.ops = &csiphy_ops_3ph_1_0
	},
	/* CSIPHY3 */
	{
		.regulators = {},
		.clock = { "csiphy3", "csiphy3_timer" },
		.clock_rate = { { 400000000 },
				{ 300000000 } },
		.reg = { "csiphy3" },
		.interrupt = { "csiphy3" },
		.ops = &csiphy_ops_3ph_1_0
	},
	/* CSIPHY4 */
	{
		.regulators = {},
		.clock = { "csiphy4", "csiphy4_timer" },
		.clock_rate = { { 400000000 },
				{ 300000000 } },
		.reg = { "csiphy4" },
		.interrupt = { "csiphy4" },
		.ops = &csiphy_ops_3ph_1_0
	},
	/* CSIPHY5 */
	{
		.regulators = {},
		.clock = { "csiphy5", "csiphy5_timer" },
		.clock_rate = { { 400000000 },
				{ 300000000 } },
		.reg = { "csiphy5" },
		.interrupt = { "csiphy5" },
		.ops = &csiphy_ops_3ph_1_0
	}
};

static const struct camss_subdev_resources csid_res_8250[] = {
	/* CSID0 */
	{
		.regulators = { "vdda-phy", "vdda-pll" },
		.clock = { "vfe0_csid", "vfe0_cphy_rx", "vfe0", "vfe0_areg", "vfe0_ahb" },
		.clock_rate = { { 400000000 },
				{ 400000000 },
				{ 350000000, 475000000, 576000000, 720000000 },
				{ 100000000, 200000000, 300000000, 400000000 },
				{ 0 } },
		.reg = { "csid0" },
		.interrupt = { "csid0" },
		.ops = &csid_ops_gen2
	},
	/* CSID1 */
	{
		.regulators = { "vdda-phy", "vdda-pll" },
		.clock = { "vfe1_csid", "vfe1_cphy_rx", "vfe1", "vfe1_areg", "vfe1_ahb" },
		.clock_rate = { { 400000000 },
				{ 400000000 },
				{ 350000000, 475000000, 576000000, 720000000 },
				{ 100000000, 200000000, 300000000, 400000000 },
				{ 0 } },
		.reg = { "csid1" },
		.interrupt = { "csid1" },
		.ops = &csid_ops_gen2
	},
	/* CSID2 */
	{
		.regulators = { "vdda-phy", "vdda-pll" },
		.clock = { "vfe_lite_csid", "vfe_lite_cphy_rx", "vfe_lite",  "vfe_lite_ahb" },
		.clock_rate = { { 400000000 },
				{ 400000000 },
				{ 400000000, 480000000 },
				{ 0 } },
		.reg = { "csid2" },
		.interrupt = { "csid2" },
		.ops = &csid_ops_gen2
	},
	/* CSID3 */
	{
		.regulators = { "vdda-phy", "vdda-pll" },
		.clock = { "vfe_lite_csid", "vfe_lite_cphy_rx", "vfe_lite",  "vfe_lite_ahb" },
		.clock_rate = { { 400000000 },
				{ 400000000 },
				{ 400000000, 480000000 },
				{ 0 } },
		.reg = { "csid3" },
		.interrupt = { "csid3" },
		.ops = &csid_ops_gen2
	}
};

static const struct camss_subdev_resources vfe_res_8250[] = {
	/* VFE0 */
	{
		.regulators = {},
		.clock = { "camnoc_axi_src", "slow_ahb_src", "cpas_ahb",
			   "camnoc_axi", "vfe0_ahb", "vfe0_areg", "vfe0",
			   "vfe0_axi", "cam_hf_axi" },
		.clock_rate = { { 19200000, 300000000, 400000000, 480000000 },
				{ 19200000, 80000000 },
				{ 19200000 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 300000000, 400000000 },
				{ 350000000, 475000000, 576000000, 720000000 },
				{ 0 },
				{ 0 } },
		.reg = { "vfe0" },
		.interrupt = { "vfe0" },
		.line_num = 3,
		.ops = &vfe_ops_480
	},
	/* VFE1 */
	{
		.regulators = {},
		.clock = { "camnoc_axi_src", "slow_ahb_src", "cpas_ahb",
			   "camnoc_axi", "vfe1_ahb", "vfe1_areg", "vfe1",
			   "vfe1_axi", "cam_hf_axi" },
		.clock_rate = { { 19200000, 300000000, 400000000, 480000000 },
				{ 19200000, 80000000 },
				{ 19200000 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000, 300000000, 400000000 },
				{ 350000000, 475000000, 576000000, 720000000 },
				{ 0 },
				{ 0 } },
		.reg = { "vfe1" },
		.interrupt = { "vfe1" },
		.line_num = 3,
		.ops = &vfe_ops_480
	},
	/* VFE2 (lite) */
	{
		.regulators = {},
		.clock = { "camnoc_axi_src", "slow_ahb_src", "cpas_ahb",
			   "camnoc_axi", "vfe_lite_ahb", "vfe_lite_axi",
			   "vfe_lite", "cam_hf_axi" },
		.clock_rate = { { 19200000, 300000000, 400000000, 480000000 },
				{ 19200000, 80000000 },
				{ 19200000 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 400000000, 480000000 },
				{ 0 } },
		.reg = { "vfe_lite0" },
		.interrupt = { "vfe_lite0" },
		.line_num = 4,
		.ops = &vfe_ops_480
	},
	/* VFE3 (lite) */
	{
		.regulators = {},
		.clock = { "camnoc_axi_src", "slow_ahb_src", "cpas_ahb",
			   "camnoc_axi", "vfe_lite_ahb", "vfe_lite_axi",
			   "vfe_lite", "cam_hf_axi" },
		.clock_rate = { { 19200000, 300000000, 400000000, 480000000 },
				{ 19200000, 80000000 },
				{ 19200000 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 400000000, 480000000 },
				{ 0 } },
		.reg = { "vfe_lite1" },
		.interrupt = { "vfe_lite1" },
		.line_num = 4,
		.ops = &vfe_ops_480
	},
};

static const struct resources_icc icc_res_sm8250[] = {
	{
		.name = "cam_ahb",
		.icc_bw_tbl.avg = 38400,
		.icc_bw_tbl.peak = 76800,
	},
	{
		.name = "cam_hf_0_mnoc",
		.icc_bw_tbl.avg = 2097152,
		.icc_bw_tbl.peak = 2097152,
	},
	{
		.name = "cam_sf_0_mnoc",
		.icc_bw_tbl.avg = 0,
		.icc_bw_tbl.peak = 2097152,
	},
	{
		.name = "cam_sf_icp_mnoc",
		.icc_bw_tbl.avg = 2097152,
		.icc_bw_tbl.peak = 2097152,
	},
};

/*
 * camss_add_clock_margin - Add margin to clock frequency rate
 * @rate: Clock frequency rate
 *
 * When making calculations with physical clock frequency values
 * some safety margin must be added. Add it.
 */
inline void camss_add_clock_margin(u64 *rate)
{
	*rate *= CAMSS_CLOCK_MARGIN_NUMERATOR;
	*rate = div_u64(*rate, CAMSS_CLOCK_MARGIN_DENOMINATOR);
}

/*
 * camss_enable_clocks - Enable multiple clocks
 * @nclocks: Number of clocks in clock array
 * @clock: Clock array
 * @dev: Device
 *
 * Return 0 on success or a negative error code otherwise
 */
int camss_enable_clocks(int nclocks, struct camss_clock *clock,
			struct device *dev)
{
	int ret;
	int i;

	for (i = 0; i < nclocks; i++) {
		ret = clk_prepare_enable(clock[i].clk);
		if (ret) {
			dev_err(dev, "clock enable failed: %d\n", ret);
			goto error;
		}
	}

	return 0;

error:
	for (i--; i >= 0; i--)
		clk_disable_unprepare(clock[i].clk);

	return ret;
}

/*
 * camss_disable_clocks - Disable multiple clocks
 * @nclocks: Number of clocks in clock array
 * @clock: Clock array
 */
void camss_disable_clocks(int nclocks, struct camss_clock *clock)
{
	int i;

	for (i = nclocks - 1; i >= 0; i--)
		clk_disable_unprepare(clock[i].clk);
}

/*
 * camss_find_sensor - Find a linked media entity which represents a sensor
 * @entity: Media entity to start searching from
 *
 * Return a pointer to sensor media entity or NULL if not found
 */
struct media_entity *camss_find_sensor(struct media_entity *entity)
{
	struct media_pad *pad;

	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			return NULL;

		pad = media_pad_remote_pad_first(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			return NULL;

		entity = pad->entity;

		if (entity->function == MEDIA_ENT_F_CAM_SENSOR)
			return entity;
	}
}

/**
 * camss_get_link_freq - Get link frequency from sensor
 * @entity: Media entity in the current pipeline
 * @bpp: Number of bits per pixel for the current format
 * @lanes: Number of lanes in the link to the sensor
 *
 * Return link frequency on success or a negative error code otherwise
 */
s64 camss_get_link_freq(struct media_entity *entity, unsigned int bpp,
			unsigned int lanes)
{
	struct media_entity *sensor;
	struct v4l2_subdev *subdev;

	sensor = camss_find_sensor(entity);
	if (!sensor)
		return -ENODEV;

	subdev = media_entity_to_v4l2_subdev(sensor);

	return v4l2_get_link_freq(subdev->ctrl_handler, bpp, 2 * lanes);
}

/*
 * camss_get_pixel_clock - Get pixel clock rate from sensor
 * @entity: Media entity in the current pipeline
 * @pixel_clock: Received pixel clock value
 *
 * Return 0 on success or a negative error code otherwise
 */
int camss_get_pixel_clock(struct media_entity *entity, u64 *pixel_clock)
{
	struct media_entity *sensor;
	struct v4l2_subdev *subdev;
	struct v4l2_ctrl *ctrl;

	sensor = camss_find_sensor(entity);
	if (!sensor)
		return -ENODEV;

	subdev = media_entity_to_v4l2_subdev(sensor);

	ctrl = v4l2_ctrl_find(subdev->ctrl_handler, V4L2_CID_PIXEL_RATE);

	if (!ctrl)
		return -EINVAL;

	*pixel_clock = v4l2_ctrl_g_ctrl_int64(ctrl);

	return 0;
}

int camss_pm_domain_on(struct camss *camss, int id)
{
	int ret = 0;

	if (id < camss->res->vfe_num) {
		struct vfe_device *vfe = &camss->vfe[id];

		ret = vfe->ops->pm_domain_on(vfe);
	}

	return ret;
}

void camss_pm_domain_off(struct camss *camss, int id)
{
	if (id < camss->res->vfe_num) {
		struct vfe_device *vfe = &camss->vfe[id];

		vfe->ops->pm_domain_off(vfe);
	}
}

/*
 * camss_of_parse_endpoint_node - Parse port endpoint node
 * @dev: Device
 * @node: Device node to be parsed
 * @csd: Parsed data from port endpoint node
 *
 * Return 0 on success or a negative error code on failure
 */
static int camss_of_parse_endpoint_node(struct device *dev,
					struct device_node *node,
					struct camss_async_subdev *csd)
{
	struct csiphy_lanes_cfg *lncfg = &csd->interface.csi2.lane_cfg;
	struct v4l2_mbus_config_mipi_csi2 *mipi_csi2;
	struct v4l2_fwnode_endpoint vep = { { 0 } };
	unsigned int i;

	v4l2_fwnode_endpoint_parse(of_fwnode_handle(node), &vep);

	csd->interface.csiphy_id = vep.base.port;

	mipi_csi2 = &vep.bus.mipi_csi2;
	lncfg->clk.pos = mipi_csi2->clock_lane;
	lncfg->clk.pol = mipi_csi2->lane_polarities[0];
	lncfg->num_data = mipi_csi2->num_data_lanes;

	lncfg->data = devm_kcalloc(dev,
				   lncfg->num_data, sizeof(*lncfg->data),
				   GFP_KERNEL);
	if (!lncfg->data)
		return -ENOMEM;

	for (i = 0; i < lncfg->num_data; i++) {
		lncfg->data[i].pos = mipi_csi2->data_lanes[i];
		lncfg->data[i].pol = mipi_csi2->lane_polarities[i + 1];
	}

	return 0;
}

/*
 * camss_of_parse_ports - Parse ports node
 * @dev: Device
 * @notifier: v4l2_device notifier data
 *
 * Return number of "port" nodes found in "ports" node
 */
static int camss_of_parse_ports(struct camss *camss)
{
	struct device *dev = camss->dev;
	struct device_node *node = NULL;
	struct device_node *remote = NULL;
	int ret, num_subdevs = 0;

	for_each_endpoint_of_node(dev->of_node, node) {
		struct camss_async_subdev *csd;

		if (!of_device_is_available(node))
			continue;

		remote = of_graph_get_remote_port_parent(node);
		if (!remote) {
			dev_err(dev, "Cannot get remote parent\n");
			ret = -EINVAL;
			goto err_cleanup;
		}

		csd = v4l2_async_nf_add_fwnode(&camss->notifier,
					       of_fwnode_handle(remote),
					       struct camss_async_subdev);
		of_node_put(remote);
		if (IS_ERR(csd)) {
			ret = PTR_ERR(csd);
			goto err_cleanup;
		}

		ret = camss_of_parse_endpoint_node(dev, node, csd);
		if (ret < 0)
			goto err_cleanup;

		num_subdevs++;
	}

	return num_subdevs;

err_cleanup:
	of_node_put(node);
	return ret;
}

/*
 * camss_init_subdevices - Initialize subdev structures and resources
 * @camss: CAMSS device
 *
 * Return 0 on success or a negative error code on failure
 */
static int camss_init_subdevices(struct camss *camss)
{
	const struct camss_resources *res = camss->res;
	unsigned int i;
	int ret;

	for (i = 0; i < camss->res->csiphy_num; i++) {
		ret = msm_csiphy_subdev_init(camss, &camss->csiphy[i],
					     &res->csiphy_res[i], i);
		if (ret < 0) {
			dev_err(camss->dev,
				"Failed to init csiphy%d sub-device: %d\n",
				i, ret);
			return ret;
		}
	}

	/* note: SM8250 requires VFE to be initialized before CSID */
	for (i = 0; i < camss->vfe_total_num; i++) {
		ret = msm_vfe_subdev_init(camss, &camss->vfe[i],
					  &res->vfe_res[i], i);
		if (ret < 0) {
			dev_err(camss->dev,
				"Fail to init vfe%d sub-device: %d\n", i, ret);
			return ret;
		}
	}

	for (i = 0; i < camss->res->csid_num; i++) {
		ret = msm_csid_subdev_init(camss, &camss->csid[i],
					   &res->csid_res[i], i);
		if (ret < 0) {
			dev_err(camss->dev,
				"Failed to init csid%d sub-device: %d\n",
				i, ret);
			return ret;
		}
	}

	ret = msm_ispif_subdev_init(camss, res->ispif_res);
	if (ret < 0) {
		dev_err(camss->dev, "Failed to init ispif sub-device: %d\n",
		ret);
		return ret;
	}

	return 0;
}

/*
 * camss_register_entities - Register subdev nodes and create links
 * @camss: CAMSS device
 *
 * Return 0 on success or a negative error code on failure
 */
static int camss_register_entities(struct camss *camss)
{
	int i, j, k;
	int ret;

	for (i = 0; i < camss->res->csiphy_num; i++) {
		ret = msm_csiphy_register_entity(&camss->csiphy[i],
						 &camss->v4l2_dev);
		if (ret < 0) {
			dev_err(camss->dev,
				"Failed to register csiphy%d entity: %d\n",
				i, ret);
			goto err_reg_csiphy;
		}
	}

	for (i = 0; i < camss->res->csid_num; i++) {
		ret = msm_csid_register_entity(&camss->csid[i],
					       &camss->v4l2_dev);
		if (ret < 0) {
			dev_err(camss->dev,
				"Failed to register csid%d entity: %d\n",
				i, ret);
			goto err_reg_csid;
		}
	}

	ret = msm_ispif_register_entities(camss->ispif,
					  &camss->v4l2_dev);
	if (ret < 0) {
		dev_err(camss->dev, "Failed to register ispif entities: %d\n",
		ret);
		goto err_reg_ispif;
	}

	for (i = 0; i < camss->vfe_total_num; i++) {
		ret = msm_vfe_register_entities(&camss->vfe[i],
						&camss->v4l2_dev);
		if (ret < 0) {
			dev_err(camss->dev,
				"Failed to register vfe%d entities: %d\n",
				i, ret);
			goto err_reg_vfe;
		}
	}

	for (i = 0; i < camss->res->csiphy_num; i++) {
		for (j = 0; j < camss->res->csid_num; j++) {
			ret = media_create_pad_link(
				&camss->csiphy[i].subdev.entity,
				MSM_CSIPHY_PAD_SRC,
				&camss->csid[j].subdev.entity,
				MSM_CSID_PAD_SINK,
				0);
			if (ret < 0) {
				dev_err(camss->dev,
					"Failed to link %s->%s entities: %d\n",
					camss->csiphy[i].subdev.entity.name,
					camss->csid[j].subdev.entity.name,
					ret);
				goto err_link;
			}
		}
	}

	if (camss->ispif) {
		for (i = 0; i < camss->res->csid_num; i++) {
			for (j = 0; j < camss->ispif->line_num; j++) {
				ret = media_create_pad_link(
					&camss->csid[i].subdev.entity,
					MSM_CSID_PAD_SRC,
					&camss->ispif->line[j].subdev.entity,
					MSM_ISPIF_PAD_SINK,
					0);
				if (ret < 0) {
					dev_err(camss->dev,
						"Failed to link %s->%s entities: %d\n",
						camss->csid[i].subdev.entity.name,
						camss->ispif->line[j].subdev.entity.name,
						ret);
					goto err_link;
				}
			}
		}

		for (i = 0; i < camss->ispif->line_num; i++)
			for (k = 0; k < camss->res->vfe_num; k++)
				for (j = 0; j < camss->vfe[k].line_num; j++) {
					struct v4l2_subdev *ispif = &camss->ispif->line[i].subdev;
					struct v4l2_subdev *vfe = &camss->vfe[k].line[j].subdev;

					ret = media_create_pad_link(&ispif->entity,
								    MSM_ISPIF_PAD_SRC,
								    &vfe->entity,
								    MSM_VFE_PAD_SINK,
								    0);
					if (ret < 0) {
						dev_err(camss->dev,
							"Failed to link %s->%s entities: %d\n",
							ispif->entity.name,
							vfe->entity.name,
							ret);
						goto err_link;
					}
				}
	} else {
		for (i = 0; i < camss->res->csid_num; i++)
			for (k = 0; k < camss->vfe_total_num; k++)
				for (j = 0; j < camss->vfe[k].line_num; j++) {
					struct v4l2_subdev *csid = &camss->csid[i].subdev;
					struct v4l2_subdev *vfe = &camss->vfe[k].line[j].subdev;

					ret = media_create_pad_link(&csid->entity,
								    MSM_CSID_PAD_FIRST_SRC + j,
								    &vfe->entity,
								    MSM_VFE_PAD_SINK,
								    0);
					if (ret < 0) {
						dev_err(camss->dev,
							"Failed to link %s->%s entities: %d\n",
							csid->entity.name,
							vfe->entity.name,
							ret);
						goto err_link;
					}
				}
	}

	return 0;

err_link:
	i = camss->vfe_total_num;
err_reg_vfe:
	for (i--; i >= 0; i--)
		msm_vfe_unregister_entities(&camss->vfe[i]);

err_reg_ispif:
	msm_ispif_unregister_entities(camss->ispif);

	i = camss->res->csid_num;
err_reg_csid:
	for (i--; i >= 0; i--)
		msm_csid_unregister_entity(&camss->csid[i]);

	i = camss->res->csiphy_num;
err_reg_csiphy:
	for (i--; i >= 0; i--)
		msm_csiphy_unregister_entity(&camss->csiphy[i]);

	return ret;
}

/*
 * camss_unregister_entities - Unregister subdev nodes
 * @camss: CAMSS device
 *
 * Return 0 on success or a negative error code on failure
 */
static void camss_unregister_entities(struct camss *camss)
{
	unsigned int i;

	for (i = 0; i < camss->res->csiphy_num; i++)
		msm_csiphy_unregister_entity(&camss->csiphy[i]);

	for (i = 0; i < camss->res->csid_num; i++)
		msm_csid_unregister_entity(&camss->csid[i]);

	msm_ispif_unregister_entities(camss->ispif);

	for (i = 0; i < camss->vfe_total_num; i++)
		msm_vfe_unregister_entities(&camss->vfe[i]);
}

static int camss_subdev_notifier_bound(struct v4l2_async_notifier *async,
				       struct v4l2_subdev *subdev,
				       struct v4l2_async_connection *asd)
{
	struct camss *camss = container_of(async, struct camss, notifier);
	struct camss_async_subdev *csd =
		container_of(asd, struct camss_async_subdev, asd);
	u8 id = csd->interface.csiphy_id;
	struct csiphy_device *csiphy = &camss->csiphy[id];

	csiphy->cfg.csi2 = &csd->interface.csi2;
	subdev->host_priv = csiphy;

	return 0;
}

static int camss_subdev_notifier_complete(struct v4l2_async_notifier *async)
{
	struct camss *camss = container_of(async, struct camss, notifier);
	struct v4l2_device *v4l2_dev = &camss->v4l2_dev;
	struct v4l2_subdev *sd;
	int ret;

	list_for_each_entry(sd, &v4l2_dev->subdevs, list) {
		if (sd->host_priv) {
			struct media_entity *sensor = &sd->entity;
			struct csiphy_device *csiphy =
					(struct csiphy_device *) sd->host_priv;
			struct media_entity *input = &csiphy->subdev.entity;
			unsigned int i;

			for (i = 0; i < sensor->num_pads; i++) {
				if (sensor->pads[i].flags & MEDIA_PAD_FL_SOURCE)
					break;
			}
			if (i == sensor->num_pads) {
				dev_err(camss->dev,
					"No source pad in external entity\n");
				return -EINVAL;
			}

			ret = media_create_pad_link(sensor, i,
				input, MSM_CSIPHY_PAD_SINK,
				MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);
			if (ret < 0) {
				dev_err(camss->dev,
					"Failed to link %s->%s entities: %d\n",
					sensor->name, input->name, ret);
				return ret;
			}
		}
	}

	ret = v4l2_device_register_subdev_nodes(&camss->v4l2_dev);
	if (ret < 0)
		return ret;

	return media_device_register(&camss->media_dev);
}

static const struct v4l2_async_notifier_operations camss_subdev_notifier_ops = {
	.bound = camss_subdev_notifier_bound,
	.complete = camss_subdev_notifier_complete,
};

static const struct media_device_ops camss_media_ops = {
	.link_notify = v4l2_pipeline_link_notify,
};

static int camss_configure_pd(struct camss *camss)
{
	struct device *dev = camss->dev;
	int i;
	int ret;

	camss->genpd_num = of_count_phandle_with_args(dev->of_node,
						      "power-domains",
						      "#power-domain-cells");
	if (camss->genpd_num < 0) {
		dev_err(dev, "Power domains are not defined for camss\n");
		return camss->genpd_num;
	}

	/*
	 * If a platform device has just one power domain, then it is attached
	 * at platform_probe() level, thus there shall be no need and even no
	 * option to attach it again, this is the case for CAMSS on MSM8916.
	 */
	if (camss->genpd_num == 1)
		return 0;

	camss->genpd = devm_kmalloc_array(dev, camss->genpd_num,
					  sizeof(*camss->genpd), GFP_KERNEL);
	if (!camss->genpd)
		return -ENOMEM;

	camss->genpd_link = devm_kmalloc_array(dev, camss->genpd_num,
					       sizeof(*camss->genpd_link),
					       GFP_KERNEL);
	if (!camss->genpd_link)
		return -ENOMEM;

	/*
	 * VFE power domains are in the beginning of the list, and while all
	 * power domains should be attached, only if TITAN_TOP power domain is
	 * found in the list, it should be linked over here.
	 */
	for (i = 0; i < camss->genpd_num; i++) {
		camss->genpd[i] = dev_pm_domain_attach_by_id(camss->dev, i);
		if (IS_ERR(camss->genpd[i])) {
			ret = PTR_ERR(camss->genpd[i]);
			goto fail_pm;
		}
	}

	if (i > camss->res->vfe_num) {
		camss->genpd_link[i - 1] = device_link_add(camss->dev, camss->genpd[i - 1],
							   DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME |
							   DL_FLAG_RPM_ACTIVE);
		if (!camss->genpd_link[i - 1]) {
			ret = -EINVAL;
			goto fail_pm;
		}
	}

	return 0;

fail_pm:
	for (--i ; i >= 0; i--)
		dev_pm_domain_detach(camss->genpd[i], true);

	return ret;
}

static int camss_icc_get(struct camss *camss)
{
	const struct resources_icc *icc_res;
	int i;

	icc_res = camss->res->icc_res;

	for (i = 0; i < camss->res->icc_path_num; i++) {
		camss->icc_path[i] = devm_of_icc_get(camss->dev,
						     icc_res[i].name);
		if (IS_ERR(camss->icc_path[i]))
			return PTR_ERR(camss->icc_path[i]);
	}

	return 0;
}

static void camss_genpd_cleanup(struct camss *camss)
{
	int i;

	if (camss->genpd_num == 1)
		return;

	if (camss->genpd_num > camss->res->vfe_num)
		device_link_del(camss->genpd_link[camss->genpd_num - 1]);

	for (i = 0; i < camss->genpd_num; i++)
		dev_pm_domain_detach(camss->genpd[i], true);
}

/*
 * camss_probe - Probe CAMSS platform device
 * @pdev: Pointer to CAMSS platform device
 *
 * Return 0 on success or a negative error code on failure
 */
static int camss_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct camss *camss;
	int num_subdevs;
	int ret;

	camss = devm_kzalloc(dev, sizeof(*camss), GFP_KERNEL);
	if (!camss)
		return -ENOMEM;

	camss->res = of_device_get_match_data(dev);

	atomic_set(&camss->ref_count, 0);
	camss->dev = dev;
	platform_set_drvdata(pdev, camss);

	camss->csiphy = devm_kcalloc(dev, camss->res->csiphy_num,
				     sizeof(*camss->csiphy), GFP_KERNEL);
	if (!camss->csiphy)
		return -ENOMEM;

	camss->csid = devm_kcalloc(dev, camss->res->csid_num, sizeof(*camss->csid),
				   GFP_KERNEL);
	if (!camss->csid)
		return -ENOMEM;

	if (camss->res->version == CAMSS_8x16 ||
	    camss->res->version == CAMSS_8x96) {
		camss->ispif = devm_kcalloc(dev, 1, sizeof(*camss->ispif), GFP_KERNEL);
		if (!camss->ispif)
			return -ENOMEM;
	}

	camss->vfe_total_num = camss->res->vfe_num + camss->res->vfe_lite_num;
	camss->vfe = devm_kcalloc(dev, camss->vfe_total_num,
				  sizeof(*camss->vfe), GFP_KERNEL);
	if (!camss->vfe)
		return -ENOMEM;

	ret = camss_icc_get(camss);
	if (ret < 0)
		return ret;

	ret = camss_configure_pd(camss);
	if (ret < 0) {
		dev_err(dev, "Failed to configure power domains: %d\n", ret);
		return ret;
	}

	ret = camss_init_subdevices(camss);
	if (ret < 0)
		goto err_genpd_cleanup;

	ret = dma_set_mask_and_coherent(dev, 0xffffffff);
	if (ret)
		goto err_genpd_cleanup;

	camss->media_dev.dev = camss->dev;
	strscpy(camss->media_dev.model, "Qualcomm Camera Subsystem",
		sizeof(camss->media_dev.model));
	camss->media_dev.ops = &camss_media_ops;
	media_device_init(&camss->media_dev);

	camss->v4l2_dev.mdev = &camss->media_dev;
	ret = v4l2_device_register(camss->dev, &camss->v4l2_dev);
	if (ret < 0) {
		dev_err(dev, "Failed to register V4L2 device: %d\n", ret);
		goto err_genpd_cleanup;
	}

	v4l2_async_nf_init(&camss->notifier, &camss->v4l2_dev);

	num_subdevs = camss_of_parse_ports(camss);
	if (num_subdevs < 0) {
		ret = num_subdevs;
		goto err_v4l2_device_unregister;
	}

	ret = camss_register_entities(camss);
	if (ret < 0)
		goto err_v4l2_device_unregister;

	if (num_subdevs) {
		camss->notifier.ops = &camss_subdev_notifier_ops;

		ret = v4l2_async_nf_register(&camss->notifier);
		if (ret) {
			dev_err(dev,
				"Failed to register async subdev nodes: %d\n",
				ret);
			goto err_register_subdevs;
		}
	} else {
		ret = v4l2_device_register_subdev_nodes(&camss->v4l2_dev);
		if (ret < 0) {
			dev_err(dev, "Failed to register subdev nodes: %d\n",
				ret);
			goto err_register_subdevs;
		}

		ret = media_device_register(&camss->media_dev);
		if (ret < 0) {
			dev_err(dev, "Failed to register media device: %d\n",
				ret);
			goto err_register_subdevs;
		}
	}

	pm_runtime_enable(dev);

	return 0;

err_register_subdevs:
	camss_unregister_entities(camss);
err_v4l2_device_unregister:
	v4l2_device_unregister(&camss->v4l2_dev);
	v4l2_async_nf_cleanup(&camss->notifier);
err_genpd_cleanup:
	camss_genpd_cleanup(camss);

	return ret;
}

void camss_delete(struct camss *camss)
{
	v4l2_device_unregister(&camss->v4l2_dev);
	media_device_unregister(&camss->media_dev);
	media_device_cleanup(&camss->media_dev);

	pm_runtime_disable(camss->dev);
}

/*
 * camss_remove - Remove CAMSS platform device
 * @pdev: Pointer to CAMSS platform device
 *
 * Always returns 0.
 */
static void camss_remove(struct platform_device *pdev)
{
	struct camss *camss = platform_get_drvdata(pdev);

	v4l2_async_nf_unregister(&camss->notifier);
	v4l2_async_nf_cleanup(&camss->notifier);
	camss_unregister_entities(camss);

	if (atomic_read(&camss->ref_count) == 0)
		camss_delete(camss);

	camss_genpd_cleanup(camss);
}

static const struct camss_resources msm8916_resources = {
	.version = CAMSS_8x16,
	.csiphy_res = csiphy_res_8x16,
	.csid_res = csid_res_8x16,
	.ispif_res = &ispif_res_8x16,
	.vfe_res = vfe_res_8x16,
	.csiphy_num = ARRAY_SIZE(csiphy_res_8x16),
	.csid_num = ARRAY_SIZE(csid_res_8x16),
	.vfe_num = ARRAY_SIZE(vfe_res_8x16),
};

static const struct camss_resources msm8996_resources = {
	.version = CAMSS_8x96,
	.csiphy_res = csiphy_res_8x96,
	.csid_res = csid_res_8x96,
	.ispif_res = &ispif_res_8x96,
	.vfe_res = vfe_res_8x96,
	.csiphy_num = ARRAY_SIZE(csiphy_res_8x96),
	.csid_num = ARRAY_SIZE(csid_res_8x96),
	.vfe_num = ARRAY_SIZE(vfe_res_8x96),
};

static const struct camss_resources sdm660_resources = {
	.version = CAMSS_660,
	.csiphy_res = csiphy_res_660,
	.csid_res = csid_res_660,
	.ispif_res = &ispif_res_660,
	.vfe_res = vfe_res_660,
	.csiphy_num = ARRAY_SIZE(csiphy_res_660),
	.csid_num = ARRAY_SIZE(csid_res_660),
	.vfe_num = ARRAY_SIZE(vfe_res_660),
};

static const struct camss_resources sdm845_resources = {
	.version = CAMSS_845,
	.csiphy_res = csiphy_res_845,
	.csid_res = csid_res_845,
	.vfe_res = vfe_res_845,
	.csiphy_num = ARRAY_SIZE(csiphy_res_845),
	.csid_num = ARRAY_SIZE(csid_res_845),
	.vfe_num = 2,
	.vfe_lite_num = 1,
};

static const struct camss_resources sm8250_resources = {
	.version = CAMSS_8250,
	.csiphy_res = csiphy_res_8250,
	.csid_res = csid_res_8250,
	.vfe_res = vfe_res_8250,
	.icc_res = icc_res_sm8250,
	.icc_path_num = ARRAY_SIZE(icc_res_sm8250),
	.csiphy_num = ARRAY_SIZE(csiphy_res_8250),
	.csid_num = ARRAY_SIZE(csid_res_8250),
	.vfe_num = 2,
	.vfe_lite_num = 2,
};

static const struct of_device_id camss_dt_match[] = {
	{ .compatible = "qcom,msm8916-camss", .data = &msm8916_resources },
	{ .compatible = "qcom,msm8996-camss", .data = &msm8996_resources },
	{ .compatible = "qcom,sdm660-camss", .data = &sdm660_resources },
	{ .compatible = "qcom,sdm845-camss", .data = &sdm845_resources },
	{ .compatible = "qcom,sm8250-camss", .data = &sm8250_resources },
	{ }
};

MODULE_DEVICE_TABLE(of, camss_dt_match);

static int __maybe_unused camss_runtime_suspend(struct device *dev)
{
	struct camss *camss = dev_get_drvdata(dev);
	int i;
	int ret;

	for (i = 0; i < camss->res->icc_path_num; i++) {
		ret = icc_set_bw(camss->icc_path[i], 0, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static int __maybe_unused camss_runtime_resume(struct device *dev)
{
	struct camss *camss = dev_get_drvdata(dev);
	const struct resources_icc *icc_res = camss->res->icc_res;
	int i;
	int ret;

	for (i = 0; i < camss->res->icc_path_num; i++) {
		ret = icc_set_bw(camss->icc_path[i],
				 icc_res[i].icc_bw_tbl.avg,
				 icc_res[i].icc_bw_tbl.peak);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct dev_pm_ops camss_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(camss_runtime_suspend, camss_runtime_resume, NULL)
};

static struct platform_driver qcom_camss_driver = {
	.probe = camss_probe,
	.remove_new = camss_remove,
	.driver = {
		.name = "qcom-camss",
		.of_match_table = camss_dt_match,
		.pm = &camss_pm_ops,
	},
};

module_platform_driver(qcom_camss_driver);

MODULE_ALIAS("platform:qcom-camss");
MODULE_DESCRIPTION("Qualcomm Camera Subsystem driver");
MODULE_AUTHOR("Todor Tomov <todor.tomov@linaro.org>");
MODULE_LICENSE("GPL v2");
