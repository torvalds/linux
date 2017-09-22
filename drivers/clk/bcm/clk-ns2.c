/*
 * Copyright (C) 2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <dt-bindings/clock/bcm-ns2.h>
#include "clk-iproc.h"

#define REG_VAL(o, s, w) { .offset = o, .shift = s, .width = w, }

#define AON_VAL(o, pw, ps, is) { .offset = o, .pwr_width = pw, \
	.pwr_shift = ps, .iso_shift = is }

#define RESET_VAL(o, rs, prs) { .offset = o, .reset_shift = rs, \
	.p_reset_shift = prs }

#define DF_VAL(o, kis, kiw, kps, kpw, kas, kaw) { .offset = o, .ki_shift = kis,\
	.ki_width = kiw, .kp_shift = kps, .kp_width = kpw, .ka_shift = kas,    \
	.ka_width = kaw }

#define VCO_CTRL_VAL(uo, lo) { .u_offset = uo, .l_offset = lo }

#define ENABLE_VAL(o, es, hs, bs) { .offset = o, .enable_shift = es, \
	.hold_shift = hs, .bypass_shift = bs }

static const struct iproc_pll_ctrl genpll_scr = {
	.flags = IPROC_CLK_AON | IPROC_CLK_PLL_SPLIT_STAT_CTRL,
	.aon = AON_VAL(0x0, 1, 15, 12),
	.reset = RESET_VAL(0x4, 2, 1),
	.dig_filter = DF_VAL(0x0, 9, 3, 5, 4, 2, 3),
	.ndiv_int = REG_VAL(0x8, 4, 10),
	.pdiv = REG_VAL(0x8, 0, 4),
	.vco_ctrl = VCO_CTRL_VAL(0x10, 0xc),
	.status = REG_VAL(0x0, 27, 1),
};


static const struct iproc_clk_ctrl genpll_scr_clk[] = {
	/* bypass_shift, the last value passed into ENABLE_VAL(), is not defined
	 * in NS2.  However, it doesn't appear to be used anywhere, so setting
	 * it to 0.
	 */
	[BCM_NS2_GENPLL_SCR_SCR_CLK] = {
		.channel = BCM_NS2_GENPLL_SCR_SCR_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 18, 12, 0),
		.mdiv = REG_VAL(0x18, 0, 8),
	},
	[BCM_NS2_GENPLL_SCR_FS_CLK] = {
		.channel = BCM_NS2_GENPLL_SCR_FS_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 19, 13, 0),
		.mdiv = REG_VAL(0x18, 8, 8),
	},
	[BCM_NS2_GENPLL_SCR_AUDIO_CLK] = {
		.channel = BCM_NS2_GENPLL_SCR_AUDIO_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 20, 14, 0),
		.mdiv = REG_VAL(0x14, 0, 8),
	},
	[BCM_NS2_GENPLL_SCR_CH3_UNUSED] = {
		.channel = BCM_NS2_GENPLL_SCR_CH3_UNUSED,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 21, 15, 0),
		.mdiv = REG_VAL(0x14, 8, 8),
	},
	[BCM_NS2_GENPLL_SCR_CH4_UNUSED] = {
		.channel = BCM_NS2_GENPLL_SCR_CH4_UNUSED,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 22, 16, 0),
		.mdiv = REG_VAL(0x14, 16, 8),
	},
	[BCM_NS2_GENPLL_SCR_CH5_UNUSED] = {
		.channel = BCM_NS2_GENPLL_SCR_CH5_UNUSED,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 23, 17, 0),
		.mdiv = REG_VAL(0x14, 24, 8),
	},
};

static void __init ns2_genpll_scr_clk_init(struct device_node *node)
{
	iproc_pll_clk_setup(node, &genpll_scr, NULL, 0, genpll_scr_clk,
			    ARRAY_SIZE(genpll_scr_clk));
}
CLK_OF_DECLARE(ns2_genpll_src_clk, "brcm,ns2-genpll-scr",
	       ns2_genpll_scr_clk_init);

static const struct iproc_pll_ctrl genpll_sw = {
	.flags = IPROC_CLK_AON | IPROC_CLK_PLL_SPLIT_STAT_CTRL,
	.aon = AON_VAL(0x0, 1, 11, 10),
	.reset = RESET_VAL(0x4, 2, 1),
	.dig_filter = DF_VAL(0x0, 9, 3, 5, 4, 2, 3),
	.ndiv_int = REG_VAL(0x8, 4, 10),
	.pdiv = REG_VAL(0x8, 0, 4),
	.vco_ctrl = VCO_CTRL_VAL(0x10, 0xc),
	.status = REG_VAL(0x0, 13, 1),
};

static const struct iproc_clk_ctrl genpll_sw_clk[] = {
	/* bypass_shift, the last value passed into ENABLE_VAL(), is not defined
	 * in NS2.  However, it doesn't appear to be used anywhere, so setting
	 * it to 0.
	 */
	[BCM_NS2_GENPLL_SW_RPE_CLK] = {
		.channel = BCM_NS2_GENPLL_SW_RPE_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 18, 12, 0),
		.mdiv = REG_VAL(0x18, 0, 8),
	},
	[BCM_NS2_GENPLL_SW_250_CLK] = {
		.channel = BCM_NS2_GENPLL_SW_250_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 19, 13, 0),
		.mdiv = REG_VAL(0x18, 8, 8),
	},
	[BCM_NS2_GENPLL_SW_NIC_CLK] = {
		.channel = BCM_NS2_GENPLL_SW_NIC_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 20, 14, 0),
		.mdiv = REG_VAL(0x14, 0, 8),
	},
	[BCM_NS2_GENPLL_SW_CHIMP_CLK] = {
		.channel = BCM_NS2_GENPLL_SW_CHIMP_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 21, 15, 0),
		.mdiv = REG_VAL(0x14, 8, 8),
	},
	[BCM_NS2_GENPLL_SW_PORT_CLK] = {
		.channel = BCM_NS2_GENPLL_SW_PORT_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 22, 16, 0),
		.mdiv = REG_VAL(0x14, 16, 8),
	},
	[BCM_NS2_GENPLL_SW_SDIO_CLK] = {
		.channel = BCM_NS2_GENPLL_SW_SDIO_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 23, 17, 0),
		.mdiv = REG_VAL(0x14, 24, 8),
	},
};

static void __init ns2_genpll_sw_clk_init(struct device_node *node)
{
	iproc_pll_clk_setup(node, &genpll_sw, NULL, 0, genpll_sw_clk,
			    ARRAY_SIZE(genpll_sw_clk));
}
CLK_OF_DECLARE(ns2_genpll_sw_clk, "brcm,ns2-genpll-sw",
	       ns2_genpll_sw_clk_init);

static const struct iproc_pll_ctrl lcpll_ddr = {
	.flags = IPROC_CLK_AON | IPROC_CLK_PLL_SPLIT_STAT_CTRL,
	.aon = AON_VAL(0x0, 2, 1, 0),
	.reset = RESET_VAL(0x4, 2, 1),
	.dig_filter = DF_VAL(0x0, 9, 3, 5, 4, 1, 4),
	.ndiv_int = REG_VAL(0x8, 4, 10),
	.pdiv = REG_VAL(0x8, 0, 4),
	.vco_ctrl = VCO_CTRL_VAL(0x10, 0xc),
	.status = REG_VAL(0x0, 0, 1),
};

static const struct iproc_clk_ctrl lcpll_ddr_clk[] = {
	/* bypass_shift, the last value passed into ENABLE_VAL(), is not defined
	 * in NS2.  However, it doesn't appear to be used anywhere, so setting
	 * it to 0.
	 */
	[BCM_NS2_LCPLL_DDR_PCIE_SATA_USB_CLK] = {
		.channel = BCM_NS2_LCPLL_DDR_PCIE_SATA_USB_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 18, 12, 0),
		.mdiv = REG_VAL(0x14, 0, 8),
	},
	[BCM_NS2_LCPLL_DDR_DDR_CLK] = {
		.channel = BCM_NS2_LCPLL_DDR_DDR_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 19, 13, 0),
		.mdiv = REG_VAL(0x14, 8, 8),
	},
	[BCM_NS2_LCPLL_DDR_CH2_UNUSED] = {
		.channel = BCM_NS2_LCPLL_DDR_CH2_UNUSED,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 20, 14, 0),
		.mdiv = REG_VAL(0x10, 0, 8),
	},
	[BCM_NS2_LCPLL_DDR_CH3_UNUSED] = {
		.channel = BCM_NS2_LCPLL_DDR_CH3_UNUSED,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 21, 15, 0),
		.mdiv = REG_VAL(0x10, 8, 8),
	},
	[BCM_NS2_LCPLL_DDR_CH4_UNUSED] = {
		.channel = BCM_NS2_LCPLL_DDR_CH4_UNUSED,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 22, 16, 0),
		.mdiv = REG_VAL(0x10, 16, 8),
	},
	[BCM_NS2_LCPLL_DDR_CH5_UNUSED] = {
		.channel = BCM_NS2_LCPLL_DDR_CH5_UNUSED,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 23, 17, 0),
		.mdiv = REG_VAL(0x10, 24, 8),
	},
};

static void __init ns2_lcpll_ddr_clk_init(struct device_node *node)
{
	iproc_pll_clk_setup(node, &lcpll_ddr, NULL, 0, lcpll_ddr_clk,
			    ARRAY_SIZE(lcpll_ddr_clk));
}
CLK_OF_DECLARE(ns2_lcpll_ddr_clk, "brcm,ns2-lcpll-ddr",
	       ns2_lcpll_ddr_clk_init);

static const struct iproc_pll_ctrl lcpll_ports = {
	.flags = IPROC_CLK_AON | IPROC_CLK_PLL_SPLIT_STAT_CTRL,
	.aon = AON_VAL(0x0, 2, 5, 4),
	.reset = RESET_VAL(0x4, 2, 1),
	.dig_filter = DF_VAL(0x0, 9, 3, 5, 4, 1, 4),
	.ndiv_int = REG_VAL(0x8, 4, 10),
	.pdiv = REG_VAL(0x8, 0, 4),
	.vco_ctrl = VCO_CTRL_VAL(0x10, 0xc),
	.status = REG_VAL(0x0, 0, 1),
};

static const struct iproc_clk_ctrl lcpll_ports_clk[] = {
	/* bypass_shift, the last value passed into ENABLE_VAL(), is not defined
	 * in NS2.  However, it doesn't appear to be used anywhere, so setting
	 * it to 0.
	 */
	[BCM_NS2_LCPLL_PORTS_WAN_CLK] = {
		.channel = BCM_NS2_LCPLL_PORTS_WAN_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 18, 12, 0),
		.mdiv = REG_VAL(0x14, 0, 8),
	},
	[BCM_NS2_LCPLL_PORTS_RGMII_CLK] = {
		.channel = BCM_NS2_LCPLL_PORTS_RGMII_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 19, 13, 0),
		.mdiv = REG_VAL(0x14, 8, 8),
	},
	[BCM_NS2_LCPLL_PORTS_CH2_UNUSED] = {
		.channel = BCM_NS2_LCPLL_PORTS_CH2_UNUSED,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 20, 14, 0),
		.mdiv = REG_VAL(0x10, 0, 8),
	},
	[BCM_NS2_LCPLL_PORTS_CH3_UNUSED] = {
		.channel = BCM_NS2_LCPLL_PORTS_CH3_UNUSED,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 21, 15, 0),
		.mdiv = REG_VAL(0x10, 8, 8),
	},
	[BCM_NS2_LCPLL_PORTS_CH4_UNUSED] = {
		.channel = BCM_NS2_LCPLL_PORTS_CH4_UNUSED,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 22, 16, 0),
		.mdiv = REG_VAL(0x10, 16, 8),
	},
	[BCM_NS2_LCPLL_PORTS_CH5_UNUSED] = {
		.channel = BCM_NS2_LCPLL_PORTS_CH5_UNUSED,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 23, 17, 0),
		.mdiv = REG_VAL(0x10, 24, 8),
	},
};

static void __init ns2_lcpll_ports_clk_init(struct device_node *node)
{
	iproc_pll_clk_setup(node, &lcpll_ports, NULL, 0, lcpll_ports_clk,
			    ARRAY_SIZE(lcpll_ports_clk));
}
CLK_OF_DECLARE(ns2_lcpll_ports_clk, "brcm,ns2-lcpll-ports",
	       ns2_lcpll_ports_clk_init);
