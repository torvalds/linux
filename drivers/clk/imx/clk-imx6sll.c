// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP.
 */

#include <dt-bindings/clock/imx6sll-clock.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clk.h"

#define CCM_ANALOG_PLL_BYPASS		(0x1 << 16)
#define BM_CCM_CCDR_MMDC_CH0_MASK	(0x2 << 16)
#define xPLL_CLR(offset)		(offset + 0x8)

static const char *pll_bypass_src_sels[] = { "osc", "dummy", };
static const char *pll1_bypass_sels[] = { "pll1", "pll1_bypass_src", };
static const char *pll2_bypass_sels[] = { "pll2", "pll2_bypass_src", };
static const char *pll3_bypass_sels[] = { "pll3", "pll3_bypass_src", };
static const char *pll4_bypass_sels[] = { "pll4", "pll4_bypass_src", };
static const char *pll5_bypass_sels[] = { "pll5", "pll5_bypass_src", };
static const char *pll6_bypass_sels[] = { "pll6", "pll6_bypass_src", };
static const char *pll7_bypass_sels[] = { "pll7", "pll7_bypass_src", };
static const char *step_sels[] = { "osc", "pll2_pfd2_396m", };
static const char *pll1_sw_sels[] = { "pll1_sys", "step", };
static const char *axi_alt_sels[] = { "pll2_pfd2_396m", "pll3_pfd1_540m", };
static const char *axi_sels[] = {"periph", "axi_alt_sel", };
static const char *periph_pre_sels[] = { "pll2_bus", "pll2_pfd2_396m", "pll2_pfd0_352m", "pll2_198m", };
static const char *periph2_pre_sels[] = { "pll2_bus", "pll2_pfd2_396m", "pll2_pfd0_352m", "pll4_audio_div", };
static const char *periph_clk2_sels[] = { "pll3_usb_otg", "osc", "osc", };
static const char *periph2_clk2_sels[] = { "pll3_usb_otg", "osc", };
static const char *periph_sels[] = { "periph_pre", "periph_clk2", };
static const char *periph2_sels[] = { "periph2_pre", "periph2_clk2", };
static const char *usdhc_sels[] = { "pll2_pfd2_396m", "pll2_pfd0_352m", };
static const char *ssi_sels[] = {"pll3_pfd2_508m", "pll3_pfd3_454m", "pll4_audio_div", "dummy",};
static const char *spdif_sels[] = { "pll4_audio_div", "pll3_pfd2_508m", "pll5_video_div", "pll3_usb_otg", };
static const char *ldb_di0_div_sels[] = { "ldb_di0_div_3_5", "ldb_di0_div_7", };
static const char *ldb_di1_div_sels[] = { "ldb_di1_div_3_5", "ldb_di1_div_7", };
static const char *ldb_di0_sels[] = { "pll5_video_div", "pll2_pfd0_352m", "pll2_pfd2_396m", "pll2_pfd3_594m", "pll2_pfd1_594m", "pll3_pfd3_454m", };
static const char *ldb_di1_sels[] = { "pll3_usb_otg", "pll2_pfd0_352m", "pll2_pfd2_396m", "pll2_bus", "pll3_pfd3_454m", "pll3_pfd2_508m", };
static const char *lcdif_pre_sels[] = { "pll2_bus", "pll3_pfd3_454m", "pll5_video_div", "pll2_pfd0_352m", "pll2_pfd1_594m", "pll3_pfd1_540m", };
static const char *ecspi_sels[] = { "pll3_60m", "osc", };
static const char *uart_sels[] = { "pll3_80m", "osc", };
static const char *perclk_sels[] = { "ipg", "osc", };
static const char *lcdif_sels[] = { "lcdif_podf", "ipp_di0", "ipp_di1", "ldb_di0", "ldb_di1", };

static const char *epdc_pre_sels[] = { "pll2_bus", "pll3_usb_otg", "pll5_video_div", "pll2_pfd0_352m", "pll2_pfd2_396m", "pll3_pfd2_508m", };
static const char *epdc_sels[] = { "epdc_podf", "ipp_di0", "ipp_di1", "ldb_di0", "ldb_di1", };

static struct clk *clks[IMX6SLL_CLK_END];
static struct clk_onecell_data clk_data;

static const struct clk_div_table post_div_table[] = {
	{ .val = 2, .div = 1, },
	{ .val = 1, .div = 2, },
	{ .val = 0, .div = 4, },
	{ }
};

static const struct clk_div_table video_div_table[] = {
	{ .val = 0, .div = 1, },
	{ .val = 1, .div = 2, },
	{ .val = 2, .div = 1, },
	{ .val = 3, .div = 4, },
	{ }
};

static u32 share_count_audio;
static u32 share_count_ssi1;
static u32 share_count_ssi2;
static u32 share_count_ssi3;

static void __init imx6sll_clocks_init(struct device_node *ccm_node)
{
	struct device_node *np;
	void __iomem *base;

	clks[IMX6SLL_CLK_DUMMY] = imx_clk_fixed("dummy", 0);

	clks[IMX6SLL_CLK_CKIL] = of_clk_get_by_name(ccm_node, "ckil");
	clks[IMX6SLL_CLK_OSC] = of_clk_get_by_name(ccm_node, "osc");

	/* ipp_di clock is external input */
	clks[IMX6SLL_CLK_IPP_DI0] = of_clk_get_by_name(ccm_node, "ipp_di0");
	clks[IMX6SLL_CLK_IPP_DI1] = of_clk_get_by_name(ccm_node, "ipp_di1");

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6sll-anatop");
	base = of_iomap(np, 0);
	of_node_put(np);
	WARN_ON(!base);

	/* Do not bypass PLLs initially */
	writel_relaxed(CCM_ANALOG_PLL_BYPASS, base + xPLL_CLR(0x0));
	writel_relaxed(CCM_ANALOG_PLL_BYPASS, base + xPLL_CLR(0x10));
	writel_relaxed(CCM_ANALOG_PLL_BYPASS, base + xPLL_CLR(0x20));
	writel_relaxed(CCM_ANALOG_PLL_BYPASS, base + xPLL_CLR(0x30));
	writel_relaxed(CCM_ANALOG_PLL_BYPASS, base + xPLL_CLR(0x70));
	writel_relaxed(CCM_ANALOG_PLL_BYPASS, base + xPLL_CLR(0xa0));
	writel_relaxed(CCM_ANALOG_PLL_BYPASS, base + xPLL_CLR(0xe0));

	clks[IMX6SLL_PLL1_BYPASS_SRC] = imx_clk_mux("pll1_bypass_src", base + 0x00, 14, 1, pll_bypass_src_sels, ARRAY_SIZE(pll_bypass_src_sels));
	clks[IMX6SLL_PLL2_BYPASS_SRC] = imx_clk_mux("pll2_bypass_src", base + 0x30, 14, 1, pll_bypass_src_sels, ARRAY_SIZE(pll_bypass_src_sels));
	clks[IMX6SLL_PLL3_BYPASS_SRC] = imx_clk_mux("pll3_bypass_src", base + 0x10, 14, 1, pll_bypass_src_sels, ARRAY_SIZE(pll_bypass_src_sels));
	clks[IMX6SLL_PLL4_BYPASS_SRC] = imx_clk_mux("pll4_bypass_src", base + 0x70, 14, 1, pll_bypass_src_sels, ARRAY_SIZE(pll_bypass_src_sels));
	clks[IMX6SLL_PLL5_BYPASS_SRC] = imx_clk_mux("pll5_bypass_src", base + 0xa0, 14, 1, pll_bypass_src_sels, ARRAY_SIZE(pll_bypass_src_sels));
	clks[IMX6SLL_PLL6_BYPASS_SRC] = imx_clk_mux("pll6_bypass_src", base + 0xe0, 14, 1, pll_bypass_src_sels, ARRAY_SIZE(pll_bypass_src_sels));
	clks[IMX6SLL_PLL7_BYPASS_SRC] = imx_clk_mux("pll7_bypass_src", base + 0x20, 14, 1, pll_bypass_src_sels, ARRAY_SIZE(pll_bypass_src_sels));

	clks[IMX6SLL_CLK_PLL1] = imx_clk_pllv3(IMX_PLLV3_SYS,	 "pll1", "pll1_bypass_src", base + 0x00, 0x7f);
	clks[IMX6SLL_CLK_PLL2] = imx_clk_pllv3(IMX_PLLV3_GENERIC, "pll2", "pll2_bypass_src", base + 0x30, 0x1);
	clks[IMX6SLL_CLK_PLL3] = imx_clk_pllv3(IMX_PLLV3_USB,	 "pll3", "pll3_bypass_src", base + 0x10, 0x3);
	clks[IMX6SLL_CLK_PLL4] = imx_clk_pllv3(IMX_PLLV3_AV,	 "pll4", "pll4_bypass_src", base + 0x70, 0x7f);
	clks[IMX6SLL_CLK_PLL5] = imx_clk_pllv3(IMX_PLLV3_AV,	 "pll5", "pll5_bypass_src", base + 0xa0, 0x7f);
	clks[IMX6SLL_CLK_PLL6] = imx_clk_pllv3(IMX_PLLV3_ENET,	 "pll6", "pll6_bypass_src", base + 0xe0, 0x3);
	clks[IMX6SLL_CLK_PLL7] = imx_clk_pllv3(IMX_PLLV3_USB,	 "pll7", "pll7_bypass_src", base + 0x20, 0x3);

	clks[IMX6SLL_PLL1_BYPASS] = imx_clk_mux_flags("pll1_bypass", base + 0x00, 16, 1, pll1_bypass_sels, ARRAY_SIZE(pll1_bypass_sels), CLK_SET_RATE_PARENT);
	clks[IMX6SLL_PLL2_BYPASS] = imx_clk_mux_flags("pll2_bypass", base + 0x30, 16, 1, pll2_bypass_sels, ARRAY_SIZE(pll2_bypass_sels), CLK_SET_RATE_PARENT);
	clks[IMX6SLL_PLL3_BYPASS] = imx_clk_mux_flags("pll3_bypass", base + 0x10, 16, 1, pll3_bypass_sels, ARRAY_SIZE(pll3_bypass_sels), CLK_SET_RATE_PARENT);
	clks[IMX6SLL_PLL4_BYPASS] = imx_clk_mux_flags("pll4_bypass", base + 0x70, 16, 1, pll4_bypass_sels, ARRAY_SIZE(pll4_bypass_sels), CLK_SET_RATE_PARENT);
	clks[IMX6SLL_PLL5_BYPASS] = imx_clk_mux_flags("pll5_bypass", base + 0xa0, 16, 1, pll5_bypass_sels, ARRAY_SIZE(pll5_bypass_sels), CLK_SET_RATE_PARENT);
	clks[IMX6SLL_PLL6_BYPASS] = imx_clk_mux_flags("pll6_bypass", base + 0xe0, 16, 1, pll6_bypass_sels, ARRAY_SIZE(pll6_bypass_sels), CLK_SET_RATE_PARENT);
	clks[IMX6SLL_PLL7_BYPASS] = imx_clk_mux_flags("pll7_bypass", base + 0x20, 16, 1, pll7_bypass_sels, ARRAY_SIZE(pll7_bypass_sels), CLK_SET_RATE_PARENT);

	clks[IMX6SLL_CLK_PLL1_SYS]	= imx_clk_fixed_factor("pll1_sys", "pll1_bypass", 1, 1);
	clks[IMX6SLL_CLK_PLL2_BUS]	= imx_clk_gate("pll2_bus",	   "pll2_bypass", base + 0x30, 13);
	clks[IMX6SLL_CLK_PLL3_USB_OTG]	= imx_clk_gate("pll3_usb_otg",	   "pll3_bypass", base + 0x10, 13);
	clks[IMX6SLL_CLK_PLL4_AUDIO]	= imx_clk_gate("pll4_audio",	   "pll4_bypass", base + 0x70, 13);
	clks[IMX6SLL_CLK_PLL5_VIDEO]	= imx_clk_gate("pll5_video",	   "pll5_bypass", base + 0xa0, 13);
	clks[IMX6SLL_CLK_PLL6_ENET]	= imx_clk_gate("pll6_enet",	   "pll6_bypass", base + 0xe0, 13);
	clks[IMX6SLL_CLK_PLL7_USB_HOST]	= imx_clk_gate("pll7_usb_host",	   "pll7_bypass", base + 0x20, 13);

	/*
	 * Bit 20 is the reserved and read-only bit, we do this only for:
	 * - Do nothing for usbphy clk_enable/disable
	 * - Keep refcount when do usbphy clk_enable/disable, in that case,
	 * the clk framework many need to enable/disable usbphy's parent
	 */
	clks[IMX6SLL_CLK_USBPHY1] = imx_clk_gate("usbphy1", "pll3_usb_otg",  base + 0x10, 20);
	clks[IMX6SLL_CLK_USBPHY2] = imx_clk_gate("usbphy2", "pll7_usb_host", base + 0x20, 20);

	/*
	 * usbphy*_gate needs to be on after system boots up, and software
	 * never needs to control it anymore.
	 */
	if (IS_ENABLED(CONFIG_USB_MXS_PHY)) {
		clks[IMX6SLL_CLK_USBPHY1_GATE] = imx_clk_gate_flags("usbphy1_gate", "dummy", base + 0x10, 6, CLK_IS_CRITICAL);
		clks[IMX6SLL_CLK_USBPHY2_GATE] = imx_clk_gate_flags("usbphy2_gate", "dummy", base + 0x20, 6, CLK_IS_CRITICAL);
	}

	/*					name		   parent_name	   reg		idx */
	clks[IMX6SLL_CLK_PLL2_PFD0] = imx_clk_pfd("pll2_pfd0_352m", "pll2_bus", base + 0x100, 0);
	clks[IMX6SLL_CLK_PLL2_PFD1] = imx_clk_pfd("pll2_pfd1_594m", "pll2_bus", base + 0x100, 1);
	clks[IMX6SLL_CLK_PLL2_PFD2] = imx_clk_pfd("pll2_pfd2_396m", "pll2_bus", base + 0x100, 2);
	clks[IMX6SLL_CLK_PLL2_PFD3] = imx_clk_pfd("pll2_pfd3_594m", "pll2_bus",	base + 0x100, 3);
	clks[IMX6SLL_CLK_PLL3_PFD0] = imx_clk_pfd("pll3_pfd0_720m", "pll3_usb_otg", base + 0xf0, 0);
	clks[IMX6SLL_CLK_PLL3_PFD1] = imx_clk_pfd("pll3_pfd1_540m", "pll3_usb_otg", base + 0xf0, 1);
	clks[IMX6SLL_CLK_PLL3_PFD2] = imx_clk_pfd("pll3_pfd2_508m", "pll3_usb_otg", base + 0xf0, 2);
	clks[IMX6SLL_CLK_PLL3_PFD3] = imx_clk_pfd("pll3_pfd3_454m", "pll3_usb_otg", base + 0xf0, 3);

	clks[IMX6SLL_CLK_PLL4_POST_DIV]  = clk_register_divider_table(NULL, "pll4_post_div", "pll4_audio",
		 CLK_SET_RATE_PARENT | CLK_SET_RATE_GATE, base + 0x70, 19, 2, 0, post_div_table, &imx_ccm_lock);
	clks[IMX6SLL_CLK_PLL4_AUDIO_DIV] = clk_register_divider(NULL, "pll4_audio_div", "pll4_post_div",
		 CLK_SET_RATE_PARENT | CLK_SET_RATE_GATE, base + 0x170, 15, 1, 0, &imx_ccm_lock);
	clks[IMX6SLL_CLK_PLL5_POST_DIV]  = clk_register_divider_table(NULL, "pll5_post_div", "pll5_video",
		 CLK_SET_RATE_PARENT | CLK_SET_RATE_GATE, base + 0xa0, 19, 2, 0, post_div_table, &imx_ccm_lock);
	clks[IMX6SLL_CLK_PLL5_VIDEO_DIV] = clk_register_divider_table(NULL, "pll5_video_div", "pll5_post_div",
		 CLK_SET_RATE_PARENT | CLK_SET_RATE_GATE, base + 0x170, 30, 2, 0, video_div_table, &imx_ccm_lock);

	/*						   name		parent_name	 mult  div */
	clks[IMX6SLL_CLK_PLL2_198M] = imx_clk_fixed_factor("pll2_198m", "pll2_pfd2_396m", 1, 2);
	clks[IMX6SLL_CLK_PLL3_120M] = imx_clk_fixed_factor("pll3_120m", "pll3_usb_otg",   1, 4);
	clks[IMX6SLL_CLK_PLL3_80M]  = imx_clk_fixed_factor("pll3_80m",  "pll3_usb_otg",   1, 6);
	clks[IMX6SLL_CLK_PLL3_60M]  = imx_clk_fixed_factor("pll3_60m",  "pll3_usb_otg",   1, 8);

	np = ccm_node;
	base = of_iomap(np, 0);
	WARN_ON(!base);

	clks[IMX6SLL_CLK_STEP] 	 	  = imx_clk_mux("step", base + 0x0c, 8, 1, step_sels, ARRAY_SIZE(step_sels));
	clks[IMX6SLL_CLK_PLL1_SW] 	  = imx_clk_mux_flags("pll1_sw",   base + 0x0c, 2,  1, pll1_sw_sels, ARRAY_SIZE(pll1_sw_sels), 0);
	clks[IMX6SLL_CLK_AXI_ALT_SEL]	  = imx_clk_mux("axi_alt_sel",	   base + 0x14, 7,  1, axi_alt_sels, ARRAY_SIZE(axi_alt_sels));
	clks[IMX6SLL_CLK_AXI_SEL] 	  = imx_clk_mux_flags("axi_sel",   base + 0x14, 6,  1, axi_sels, ARRAY_SIZE(axi_sels), 0);
	clks[IMX6SLL_CLK_PERIPH_PRE]	  = imx_clk_mux("periph_pre",      base + 0x18, 18, 2, periph_pre_sels, ARRAY_SIZE(periph_pre_sels));
	clks[IMX6SLL_CLK_PERIPH2_PRE]	  = imx_clk_mux("periph2_pre",     base + 0x18, 21, 2, periph2_pre_sels, ARRAY_SIZE(periph2_pre_sels));
	clks[IMX6SLL_CLK_PERIPH_CLK2_SEL]  = imx_clk_mux("periph_clk2_sel",  base + 0x18, 12, 2, periph_clk2_sels, ARRAY_SIZE(periph_clk2_sels));
	clks[IMX6SLL_CLK_PERIPH2_CLK2_SEL] = imx_clk_mux("periph2_clk2_sel", base + 0x18, 20, 1, periph2_clk2_sels, ARRAY_SIZE(periph2_clk2_sels));
	clks[IMX6SLL_CLK_USDHC1_SEL]	  = imx_clk_mux("usdhc1_sel",   base + 0x1c, 16, 1, usdhc_sels, ARRAY_SIZE(usdhc_sels));
	clks[IMX6SLL_CLK_USDHC2_SEL]	  = imx_clk_mux("usdhc2_sel",   base + 0x1c, 17, 1, usdhc_sels, ARRAY_SIZE(usdhc_sels));
	clks[IMX6SLL_CLK_USDHC3_SEL]	  = imx_clk_mux("usdhc3_sel",   base + 0x1c, 18, 1, usdhc_sels, ARRAY_SIZE(usdhc_sels));
	clks[IMX6SLL_CLK_SSI1_SEL]	  = imx_clk_mux("ssi1_sel",     base + 0x1c, 10, 2, ssi_sels, ARRAY_SIZE(ssi_sels));
	clks[IMX6SLL_CLK_SSI2_SEL]	  = imx_clk_mux("ssi2_sel",     base + 0x1c, 12, 2, ssi_sels, ARRAY_SIZE(ssi_sels));
	clks[IMX6SLL_CLK_SSI3_SEL]	  = imx_clk_mux("ssi3_sel",     base + 0x1c, 14, 2, ssi_sels, ARRAY_SIZE(ssi_sels));
	clks[IMX6SLL_CLK_PERCLK_SEL] 	  = imx_clk_mux("perclk_sel",	base + 0x1c, 6,  1, perclk_sels, ARRAY_SIZE(perclk_sels));
	clks[IMX6SLL_CLK_UART_SEL]	  = imx_clk_mux("uart_sel",	base + 0x24, 6,  1, uart_sels, ARRAY_SIZE(uart_sels));
	clks[IMX6SLL_CLK_SPDIF_SEL]	  = imx_clk_mux("spdif_sel",	base + 0x30, 20, 2, spdif_sels, ARRAY_SIZE(spdif_sels));
	clks[IMX6SLL_CLK_EXTERN_AUDIO_SEL] = imx_clk_mux("extern_audio_sel", base + 0x30, 7,  2, spdif_sels, ARRAY_SIZE(spdif_sels));
	clks[IMX6SLL_CLK_EPDC_PRE_SEL]	  = imx_clk_mux("epdc_pre_sel",	base + 0x34, 15, 3, epdc_pre_sels, ARRAY_SIZE(epdc_pre_sels));
	clks[IMX6SLL_CLK_EPDC_SEL]	  = imx_clk_mux("epdc_sel",	base + 0x34, 9, 3, epdc_sels, ARRAY_SIZE(epdc_sels));
	clks[IMX6SLL_CLK_ECSPI_SEL]	  = imx_clk_mux("ecspi_sel",	base + 0x38, 18, 1, ecspi_sels, ARRAY_SIZE(ecspi_sels));
	clks[IMX6SLL_CLK_LCDIF_PRE_SEL]	  = imx_clk_mux("lcdif_pre_sel", base + 0x38, 15, 3, lcdif_pre_sels, ARRAY_SIZE(lcdif_pre_sels));
	clks[IMX6SLL_CLK_LCDIF_SEL]	  = imx_clk_mux("lcdif_sel", 	 base + 0x38, 9, 3, lcdif_sels, ARRAY_SIZE(lcdif_sels));

	clks[IMX6SLL_CLK_PERIPH]  = imx_clk_busy_mux("periph",  base + 0x14, 25, 1, base + 0x48, 5, periph_sels, ARRAY_SIZE(periph_sels));
	clks[IMX6SLL_CLK_PERIPH2] = imx_clk_busy_mux("periph2", base + 0x14, 26, 1, base + 0x48, 3, periph2_sels, ARRAY_SIZE(periph2_sels));

	clks[IMX6SLL_CLK_PERIPH_CLK2]	= imx_clk_divider("periph_clk2",   "periph_clk2_sel",  	base + 0x14, 27, 3);
	clks[IMX6SLL_CLK_PERIPH2_CLK2]	= imx_clk_divider("periph2_clk2",  "periph2_clk2_sel", 	base + 0x14, 0,  3);
	clks[IMX6SLL_CLK_IPG]		= imx_clk_divider("ipg",	   "ahb",		base + 0x14, 8,	 2);
	clks[IMX6SLL_CLK_LCDIF_PODF]	= imx_clk_divider("lcdif_podf",	   "lcdif_pred",	base + 0x18, 23, 3);
	clks[IMX6SLL_CLK_PERCLK]	= imx_clk_divider("perclk",	   "perclk_sel",	base + 0x1c, 0,  6);
	clks[IMX6SLL_CLK_USDHC3_PODF]   = imx_clk_divider("usdhc3_podf",   "usdhc3_sel",	base + 0x24, 19, 3);
	clks[IMX6SLL_CLK_USDHC2_PODF]	= imx_clk_divider("usdhc2_podf",   "usdhc2_sel",	base + 0x24, 16, 3);
	clks[IMX6SLL_CLK_USDHC1_PODF]	= imx_clk_divider("usdhc1_podf",   "usdhc1_sel",	base + 0x24, 11, 3);
	clks[IMX6SLL_CLK_UART_PODF]	= imx_clk_divider("uart_podf",	   "uart_sel",		base + 0x24, 0,  6);
	clks[IMX6SLL_CLK_SSI3_PRED]	= imx_clk_divider("ssi3_pred",	   "ssi3_sel",		base + 0x28, 22, 3);
	clks[IMX6SLL_CLK_SSI3_PODF]	= imx_clk_divider("ssi3_podf",	   "ssi3_pred",		base + 0x28, 16, 6);
	clks[IMX6SLL_CLK_SSI1_PRED]	= imx_clk_divider("ssi1_pred",	   "ssi1_sel",		base + 0x28, 6,	 3);
	clks[IMX6SLL_CLK_SSI1_PODF]	= imx_clk_divider("ssi1_podf",	   "ssi1_pred",		base + 0x28, 0,	 6);
	clks[IMX6SLL_CLK_SSI2_PRED]	= imx_clk_divider("ssi2_pred",	   "ssi2_sel",		base + 0x2c, 6,	 3);
	clks[IMX6SLL_CLK_SSI2_PODF]	= imx_clk_divider("ssi2_podf",	   "ssi2_pred",		base + 0x2c, 0,  6);
	clks[IMX6SLL_CLK_SPDIF_PRED]	= imx_clk_divider("spdif_pred",	   "spdif_sel",		base + 0x30, 25, 3);
	clks[IMX6SLL_CLK_SPDIF_PODF]	= imx_clk_divider("spdif_podf",	   "spdif_pred",	base + 0x30, 22, 3);
	clks[IMX6SLL_CLK_EXTERN_AUDIO_PRED] = imx_clk_divider("extern_audio_pred", "extern_audio_sel",  base + 0x30, 12, 3);
	clks[IMX6SLL_CLK_EXTERN_AUDIO_PODF] = imx_clk_divider("extern_audio_podf", "extern_audio_pred", base + 0x30, 9,  3);
	clks[IMX6SLL_CLK_EPDC_PODF]  = imx_clk_divider("epdc_podf",  "epdc_pre_sel",  base + 0x34, 12, 3);
	clks[IMX6SLL_CLK_ECSPI_PODF] = imx_clk_divider("ecspi_podf", "ecspi_sel",     base + 0x38, 19, 6);
	clks[IMX6SLL_CLK_LCDIF_PRED] = imx_clk_divider("lcdif_pred", "lcdif_pre_sel", base + 0x38, 12, 3);

	clks[IMX6SLL_CLK_ARM]		= imx_clk_busy_divider("arm", 	    "pll1_sw",	base +	0x10, 0,  3,  base + 0x48, 16);
	clks[IMX6SLL_CLK_MMDC_PODF]	= imx_clk_busy_divider("mmdc_podf", "periph2",	base +  0x14, 3,  3,  base + 0x48, 2);
	clks[IMX6SLL_CLK_AXI_PODF]	= imx_clk_busy_divider("axi",       "axi_sel",	base +  0x14, 16, 3,  base + 0x48, 0);
	clks[IMX6SLL_CLK_AHB]		= imx_clk_busy_divider("ahb",	    "periph",	base +  0x14, 10, 3,  base + 0x48, 1);

	clks[IMX6SLL_CLK_LDB_DI0_DIV_3_5] = imx_clk_fixed_factor("ldb_di0_div_3_5", "ldb_di0_sel", 2, 7);
	clks[IMX6SLL_CLK_LDB_DI0_DIV_7]	  = imx_clk_fixed_factor("ldb_di0_div_7",   "ldb_di0_sel", 1, 7);
	clks[IMX6SLL_CLK_LDB_DI1_DIV_3_5] = imx_clk_fixed_factor("ldb_di1_div_3_5", "ldb_di1_sel", 2, 7);
	clks[IMX6SLL_CLK_LDB_DI1_DIV_7]	  = imx_clk_fixed_factor("ldb_di1_div_7",   "ldb_di1_sel", 1, 7);

	clks[IMX6SLL_CLK_LDB_DI0_SEL]	= imx_clk_mux("ldb_di0_sel", base + 0x2c, 9, 3, ldb_di0_sels, ARRAY_SIZE(ldb_di0_sels));
	clks[IMX6SLL_CLK_LDB_DI1_SEL]   = imx_clk_mux("ldb_di1_sel", base + 0x1c, 7, 3, ldb_di1_sels, ARRAY_SIZE(ldb_di1_sels));
	clks[IMX6SLL_CLK_LDB_DI0_DIV_SEL] = imx_clk_mux("ldb_di0_div_sel", base + 0x20, 10, 1, ldb_di0_div_sels, ARRAY_SIZE(ldb_di0_div_sels));
	clks[IMX6SLL_CLK_LDB_DI1_DIV_SEL] = imx_clk_mux("ldb_di1_div_sel", base + 0x20, 10, 1, ldb_di1_div_sels, ARRAY_SIZE(ldb_di1_div_sels));

	/* CCGR0 */
	clks[IMX6SLL_CLK_AIPSTZ1]	= imx_clk_gate2_flags("aips_tz1", "ahb", base + 0x68, 0, CLK_IS_CRITICAL);
	clks[IMX6SLL_CLK_AIPSTZ2]	= imx_clk_gate2_flags("aips_tz2", "ahb", base + 0x68, 2, CLK_IS_CRITICAL);
	clks[IMX6SLL_CLK_DCP]		= imx_clk_gate2("dcp", "ahb", base + 0x68, 10);
	clks[IMX6SLL_CLK_UART2_IPG]	= imx_clk_gate2("uart2_ipg", "ipg", base + 0x68, 28);
	clks[IMX6SLL_CLK_UART2_SERIAL]	= imx_clk_gate2("uart2_serial",	"uart_podf", base + 0x68, 28);
	clks[IMX6SLL_CLK_GPIO2]		= imx_clk_gate2("gpio2", "ipg", base + 0x68, 30);

	/* CCGR1 */
	clks[IMX6SLL_CLK_ECSPI1]	= imx_clk_gate2("ecspi1",	"ecspi_podf", base + 0x6c, 0);
	clks[IMX6SLL_CLK_ECSPI2]	= imx_clk_gate2("ecspi2",	"ecspi_podf", base + 0x6c, 2);
	clks[IMX6SLL_CLK_ECSPI3]	= imx_clk_gate2("ecspi3",	"ecspi_podf", base + 0x6c, 4);
	clks[IMX6SLL_CLK_ECSPI4]	= imx_clk_gate2("ecspi4",	"ecspi_podf", base + 0x6c, 6);
	clks[IMX6SLL_CLK_UART3_IPG]	= imx_clk_gate2("uart3_ipg",	"ipg", base + 0x6c, 10);
	clks[IMX6SLL_CLK_UART3_SERIAL]	= imx_clk_gate2("uart3_serial",	"uart_podf", base + 0x6c, 10);
	clks[IMX6SLL_CLK_EPIT1]		= imx_clk_gate2("epit1",	"perclk", base + 0x6c, 12);
	clks[IMX6SLL_CLK_EPIT2]		= imx_clk_gate2("epit2",	"perclk", base + 0x6c, 14);
	clks[IMX6SLL_CLK_GPT_BUS]	= imx_clk_gate2("gpt1_bus",	"perclk", base + 0x6c, 20);
	clks[IMX6SLL_CLK_GPT_SERIAL]	= imx_clk_gate2("gpt1_serial",	"perclk", base + 0x6c, 22);
	clks[IMX6SLL_CLK_UART4_IPG]	= imx_clk_gate2("uart4_ipg",	"ipg", base + 0x6c, 24);
	clks[IMX6SLL_CLK_UART4_SERIAL]	= imx_clk_gate2("uart4_serail",	"uart_podf", base + 0x6c, 24);
	clks[IMX6SLL_CLK_GPIO1]		= imx_clk_gate2("gpio1",	"ipg", base + 0x6c, 26);
	clks[IMX6SLL_CLK_GPIO5]		= imx_clk_gate2("gpio5",	"ipg", base + 0x6c, 30);

	/* CCGR2 */
	clks[IMX6SLL_CLK_GPIO6]		= imx_clk_gate2("gpio6",	"ipg",    base + 0x70, 0);
	clks[IMX6SLL_CLK_CSI]		= imx_clk_gate2("csi",		"axi",    base + 0x70,	2);
	clks[IMX6SLL_CLK_I2C1]		= imx_clk_gate2("i2c1",		"perclk", base + 0x70,	6);
	clks[IMX6SLL_CLK_I2C2]		= imx_clk_gate2("i2c2",		"perclk", base + 0x70,	8);
	clks[IMX6SLL_CLK_I2C3]		= imx_clk_gate2("i2c3",		"perclk", base + 0x70,	10);
	clks[IMX6SLL_CLK_OCOTP]		= imx_clk_gate2("ocotp",	"ipg",    base + 0x70,	12);
	clks[IMX6SLL_CLK_GPIO3]		= imx_clk_gate2("gpio3",	"ipg",    base + 0x70,	26);
	clks[IMX6SLL_CLK_LCDIF_APB]	= imx_clk_gate2("lcdif_apb",	"axi",    base + 0x70,	28);
	clks[IMX6SLL_CLK_PXP]		= imx_clk_gate2("pxp",		"axi",    base + 0x70,	30);

	/* CCGR3 */
	clks[IMX6SLL_CLK_UART5_IPG]	= imx_clk_gate2("uart5_ipg",	"ipg",		base + 0x74, 2);
	clks[IMX6SLL_CLK_UART5_SERIAL]	= imx_clk_gate2("uart5_serial",	"uart_podf",	base + 0x74, 2);
	clks[IMX6SLL_CLK_EPDC_AXI]	= imx_clk_gate2("epdc_aclk",	"axi",		base + 0x74, 4);
	clks[IMX6SLL_CLK_EPDC_PIX]	= imx_clk_gate2("epdc_pix",	"epdc_podf",	base + 0x74, 4);
	clks[IMX6SLL_CLK_LCDIF_PIX]	= imx_clk_gate2("lcdif_pix",	"lcdif_podf",	base + 0x74, 10);
	clks[IMX6SLL_CLK_GPIO4]		= imx_clk_gate2("gpio4",	"ipg",		base + 0x74, 12);
	clks[IMX6SLL_CLK_WDOG1]		= imx_clk_gate2("wdog1",	"ipg",		base + 0x74, 16);
	clks[IMX6SLL_CLK_MMDC_P0_FAST]	= imx_clk_gate_flags("mmdc_p0_fast", "mmdc_podf",  base + 0x74,	20, CLK_IS_CRITICAL);
	clks[IMX6SLL_CLK_MMDC_P0_IPG]	= imx_clk_gate2_flags("mmdc_p0_ipg", "ipg",	   base + 0x74,	24, CLK_IS_CRITICAL);
	clks[IMX6SLL_CLK_OCRAM]		= imx_clk_gate_flags("ocram","ahb",		   base + 0x74,	28, CLK_IS_CRITICAL);

	/* CCGR4 */
	clks[IMX6SLL_CLK_PWM1]		= imx_clk_gate2("pwm1", "perclk", base + 0x78, 16);
	clks[IMX6SLL_CLK_PWM2]		= imx_clk_gate2("pwm2", "perclk", base + 0x78, 18);
	clks[IMX6SLL_CLK_PWM3]		= imx_clk_gate2("pwm3", "perclk", base + 0x78, 20);
	clks[IMX6SLL_CLK_PWM4]		= imx_clk_gate2("pwm4", "perclk", base + 0x78, 22);

	/* CCGR5 */
	clks[IMX6SLL_CLK_ROM]		= imx_clk_gate2_flags("rom", "ahb", base + 0x7c, 0, CLK_IS_CRITICAL);
	clks[IMX6SLL_CLK_SDMA]		= imx_clk_gate2("sdma",	 "ahb",	base + 0x7c, 6);
	clks[IMX6SLL_CLK_WDOG2]		= imx_clk_gate2("wdog2", "ipg",	base + 0x7c, 10);
	clks[IMX6SLL_CLK_SPBA]		= imx_clk_gate2("spba",	 "ipg",	base + 0x7c, 12);
	clks[IMX6SLL_CLK_EXTERN_AUDIO]	= imx_clk_gate2_shared("extern_audio",  "extern_audio_podf", base + 0x7c, 14, &share_count_audio);
	clks[IMX6SLL_CLK_SPDIF]		= imx_clk_gate2_shared("spdif",		"spdif_podf",	base + 0x7c, 14, &share_count_audio);
	clks[IMX6SLL_CLK_SPDIF_GCLK]	= imx_clk_gate2_shared("spdif_gclk",	"ipg",		base + 0x7c, 14, &share_count_audio);
	clks[IMX6SLL_CLK_SSI1]		= imx_clk_gate2_shared("ssi1",		"ssi1_podf",	base + 0x7c, 18, &share_count_ssi1);
	clks[IMX6SLL_CLK_SSI1_IPG]	= imx_clk_gate2_shared("ssi1_ipg",	"ipg",		base + 0x7c, 18, &share_count_ssi1);
	clks[IMX6SLL_CLK_SSI2]		= imx_clk_gate2_shared("ssi2",		"ssi2_podf",	base + 0x7c, 20, &share_count_ssi2);
	clks[IMX6SLL_CLK_SSI2_IPG]	= imx_clk_gate2_shared("ssi2_ipg",	"ipg",		base + 0x7c, 20, &share_count_ssi2);
	clks[IMX6SLL_CLK_SSI3]		= imx_clk_gate2_shared("ssi3",		"ssi3_podf",	base + 0x7c, 22, &share_count_ssi3);
	clks[IMX6SLL_CLK_SSI3_IPG]	= imx_clk_gate2_shared("ssi3_ipg",	"ipg",		base + 0x7c, 22, &share_count_ssi3);
	clks[IMX6SLL_CLK_UART1_IPG]	= imx_clk_gate2("uart1_ipg",	"ipg",		base + 0x7c, 24);
	clks[IMX6SLL_CLK_UART1_SERIAL]	= imx_clk_gate2("uart1_serial",	"uart_podf",	base + 0x7c, 24);

	/* CCGR6 */
	clks[IMX6SLL_CLK_USBOH3]	= imx_clk_gate2("usboh3", "ipg",	  base + 0x80,	0);
	clks[IMX6SLL_CLK_USDHC1]	= imx_clk_gate2("usdhc1", "usdhc1_podf",  base + 0x80,	2);
	clks[IMX6SLL_CLK_USDHC2]	= imx_clk_gate2("usdhc2", "usdhc2_podf",  base + 0x80,	4);
	clks[IMX6SLL_CLK_USDHC3]	= imx_clk_gate2("usdhc3", "usdhc3_podf",  base + 0x80,	6);

	/* mask handshake of mmdc */
	writel_relaxed(BM_CCM_CCDR_MMDC_CH0_MASK, base + 0x4);

	imx_check_clocks(clks, ARRAY_SIZE(clks));

	clk_data.clks = clks;
	clk_data.clk_num = ARRAY_SIZE(clks);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);

	/* Lower the AHB clock rate before changing the clock source. */
	clk_set_rate(clks[IMX6SLL_CLK_AHB], 99000000);

	/* Change periph_pre clock to pll2_bus to adjust AXI rate to 264MHz */
	clk_set_parent(clks[IMX6SLL_CLK_PERIPH_CLK2_SEL], clks[IMX6SLL_CLK_PLL3_USB_OTG]);
	clk_set_parent(clks[IMX6SLL_CLK_PERIPH], clks[IMX6SLL_CLK_PERIPH_CLK2]);
	clk_set_parent(clks[IMX6SLL_CLK_PERIPH_PRE], clks[IMX6SLL_CLK_PLL2_BUS]);
	clk_set_parent(clks[IMX6SLL_CLK_PERIPH], clks[IMX6SLL_CLK_PERIPH_PRE]);

	clk_set_rate(clks[IMX6SLL_CLK_AHB], 132000000);
}
CLK_OF_DECLARE_DRIVER(imx6sll, "fsl,imx6sll-ccm", imx6sll_clocks_init);
