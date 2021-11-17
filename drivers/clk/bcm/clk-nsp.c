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

#include <dt-bindings/clock/bcm-nsp.h>
#include "clk-iproc.h"

#define REG_VAL(o, s, w) { .offset = o, .shift = s, .width = w, }

#define AON_VAL(o, pw, ps, is) { .offset = o, .pwr_width = pw, \
	.pwr_shift = ps, .iso_shift = is }

#define RESET_VAL(o, rs, prs) { .offset = o, .reset_shift = rs, \
	.p_reset_shift = prs }

#define DF_VAL(o, kis, kiw, kps, kpw, kas, kaw) { .offset = o, .ki_shift = kis,\
	.ki_width = kiw, .kp_shift = kps, .kp_width = kpw, .ka_shift = kas,    \
	.ka_width = kaw }

#define ENABLE_VAL(o, es, hs, bs) { .offset = o, .enable_shift = es, \
	.hold_shift = hs, .bypass_shift = bs }

static void __init nsp_armpll_init(struct device_node *node)
{
	iproc_armpll_setup(node);
}
CLK_OF_DECLARE(nsp_armpll, "brcm,nsp-armpll", nsp_armpll_init);

static const struct iproc_pll_ctrl genpll = {
	.flags = IPROC_CLK_PLL_HAS_NDIV_FRAC | IPROC_CLK_EMBED_PWRCTRL,
	.aon = AON_VAL(0x0, 1, 12, 0),
	.reset = RESET_VAL(0x0, 11, 10),
	.dig_filter = DF_VAL(0x0, 4, 3, 0, 4, 7, 3),
	.ndiv_int = REG_VAL(0x14, 20, 10),
	.ndiv_frac = REG_VAL(0x14, 0, 20),
	.pdiv = REG_VAL(0x18, 24, 3),
	.status = REG_VAL(0x20, 12, 1),
};

static const struct iproc_clk_ctrl genpll_clk[] = {
	[BCM_NSP_GENPLL_PHY_CLK] = {
		.channel = BCM_NSP_GENPLL_PHY_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x4, 12, 6, 18),
		.mdiv = REG_VAL(0x18, 16, 8),
	},
	[BCM_NSP_GENPLL_ENET_SW_CLK] = {
		.channel = BCM_NSP_GENPLL_ENET_SW_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x4, 13, 7, 19),
		.mdiv = REG_VAL(0x18, 8, 8),
	},
	[BCM_NSP_GENPLL_USB_PHY_REF_CLK] = {
		.channel = BCM_NSP_GENPLL_USB_PHY_REF_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x4, 14, 8, 20),
		.mdiv = REG_VAL(0x18, 0, 8),
	},
	[BCM_NSP_GENPLL_IPROCFAST_CLK] = {
		.channel = BCM_NSP_GENPLL_IPROCFAST_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x4, 15, 9, 21),
		.mdiv = REG_VAL(0x1c, 16, 8),
	},
	[BCM_NSP_GENPLL_SATA1_CLK] = {
		.channel = BCM_NSP_GENPLL_SATA1_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x4, 16, 10, 22),
		.mdiv = REG_VAL(0x1c, 8, 8),
	},
	[BCM_NSP_GENPLL_SATA2_CLK] = {
		.channel = BCM_NSP_GENPLL_SATA2_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x4, 17, 11, 23),
		.mdiv = REG_VAL(0x1c, 0, 8),
	},
};

static void __init nsp_genpll_clk_init(struct device_node *node)
{
	iproc_pll_clk_setup(node, &genpll, NULL, 0, genpll_clk,
			    ARRAY_SIZE(genpll_clk));
}
CLK_OF_DECLARE(nsp_genpll_clk, "brcm,nsp-genpll", nsp_genpll_clk_init);

static const struct iproc_pll_ctrl lcpll0 = {
	.flags = IPROC_CLK_PLL_HAS_NDIV_FRAC | IPROC_CLK_EMBED_PWRCTRL,
	.aon = AON_VAL(0x0, 1, 24, 0),
	.reset = RESET_VAL(0x0, 23, 22),
	.dig_filter = DF_VAL(0x0, 16, 3, 12, 4, 19, 4),
	.ndiv_int = REG_VAL(0x4, 20, 8),
	.ndiv_frac = REG_VAL(0x4, 0, 20),
	.pdiv = REG_VAL(0x4, 28, 3),
	.status = REG_VAL(0x10, 12, 1),
};

static const struct iproc_clk_ctrl lcpll0_clk[] = {
	[BCM_NSP_LCPLL0_PCIE_PHY_REF_CLK] = {
		.channel = BCM_NSP_LCPLL0_PCIE_PHY_REF_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 6, 3, 9),
		.mdiv = REG_VAL(0x8, 24, 8),
	},
	[BCM_NSP_LCPLL0_SDIO_CLK] = {
		.channel = BCM_NSP_LCPLL0_SDIO_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 7, 4, 10),
		.mdiv = REG_VAL(0x8, 16, 8),
	},
	[BCM_NSP_LCPLL0_DDR_PHY_CLK] = {
		.channel = BCM_NSP_LCPLL0_DDR_PHY_CLK,
		.flags = IPROC_CLK_AON,
		.enable = ENABLE_VAL(0x0, 8, 5, 11),
		.mdiv = REG_VAL(0x8, 8, 8),
	},
};

static void __init nsp_lcpll0_clk_init(struct device_node *node)
{
	iproc_pll_clk_setup(node, &lcpll0, NULL, 0, lcpll0_clk,
			    ARRAY_SIZE(lcpll0_clk));
}
CLK_OF_DECLARE(nsp_lcpll0_clk, "brcm,nsp-lcpll0", nsp_lcpll0_clk_init);
