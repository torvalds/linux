// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * Copyright (C) 2021
 * Author(s):
 * Jesse Taube <Mr.Bossman075@gmail.com>
 * Giulio Benetti <giulio.benetti@benettiengineering.com>
 */
#include <linux/clk.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/imxrt1050-clock.h>

#include "clk.h"

static const char * const pll_ref_sels[] = {"osc", "dummy", };
static const char * const per_sels[] = {"ipg_pdof", "osc", };
static const char * const pll1_bypass_sels[] = {"pll1_arm", "pll1_arm_ref_sel", };
static const char * const pll2_bypass_sels[] = {"pll2_sys", "pll2_sys_ref_sel", };
static const char * const pll3_bypass_sels[] = {"pll3_usb_otg", "pll3_usb_otg_ref_sel", };
static const char * const pll5_bypass_sels[] = {"pll5_video", "pll5_video_ref_sel", };
static const char *const pre_periph_sels[] = {
	"pll2_sys", "pll2_pfd2_396m", "pll2_pfd0_352m", "arm_podf", };
static const char *const periph_sels[] = { "pre_periph_sel", "todo", };
static const char *const usdhc_sels[] = { "pll2_pfd2_396m", "pll2_pfd0_352m", };
static const char *const lpuart_sels[] = { "pll3_80m", "osc", };
static const char *const lcdif_sels[] = {
	"pll2_sys", "pll3_pfd3_454_74m", "pll5_video", "pll2_pfd0_352m",
	"pll2_pfd1_594m", "pll3_pfd1_664_62m", };
static const char *const semc_alt_sels[] = { "pll2_pfd2_396m", "pll3_pfd1_664_62m", };
static const char *const semc_sels[] = { "periph_sel", "semc_alt_sel", };

static struct clk_hw **hws;
static struct clk_hw_onecell_data *clk_hw_data;

static int imxrt1050_clocks_probe(struct platform_device *pdev)
{
	void __iomem *ccm_base;
	void __iomem *pll_base;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *anp;
	int ret;

	clk_hw_data = devm_kzalloc(dev, struct_size(clk_hw_data, hws,
					  IMXRT1050_CLK_END), GFP_KERNEL);
	if (WARN_ON(!clk_hw_data))
		return -ENOMEM;

	clk_hw_data->num = IMXRT1050_CLK_END;
	hws = clk_hw_data->hws;

	hws[IMXRT1050_CLK_OSC] = imx_obtain_fixed_clk_hw(np, "osc");

	anp = of_find_compatible_node(NULL, NULL, "fsl,imxrt-anatop");
	pll_base = devm_of_iomap(dev, anp, 0, NULL);
	of_node_put(anp);
	if (WARN_ON(IS_ERR(pll_base))) {
		ret = PTR_ERR(pll_base);
		goto unregister_hws;
	}

	/* Anatop clocks */
	hws[IMXRT1050_CLK_DUMMY] = imx_clk_hw_fixed("dummy", 0UL);

	hws[IMXRT1050_CLK_PLL1_REF_SEL] = imx_clk_hw_mux("pll1_arm_ref_sel",
		pll_base + 0x0, 14, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMXRT1050_CLK_PLL2_REF_SEL] = imx_clk_hw_mux("pll2_sys_ref_sel",
		pll_base + 0x30, 14, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMXRT1050_CLK_PLL3_REF_SEL] = imx_clk_hw_mux("pll3_usb_otg_ref_sel",
		pll_base + 0x10, 14, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMXRT1050_CLK_PLL5_REF_SEL] = imx_clk_hw_mux("pll5_video_ref_sel",
		pll_base + 0xa0, 14, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));

	hws[IMXRT1050_CLK_PLL1_ARM] = imx_clk_hw_pllv3(IMX_PLLV3_SYS, "pll1_arm",
		"pll1_arm_ref_sel", pll_base + 0x0, 0x7f);
	hws[IMXRT1050_CLK_PLL2_SYS] = imx_clk_hw_pllv3(IMX_PLLV3_GENERIC, "pll2_sys",
		"pll2_sys_ref_sel", pll_base + 0x30, 0x1);
	hws[IMXRT1050_CLK_PLL3_USB_OTG] = imx_clk_hw_pllv3(IMX_PLLV3_USB, "pll3_usb_otg",
		"pll3_usb_otg_ref_sel", pll_base + 0x10, 0x1);
	hws[IMXRT1050_CLK_PLL5_VIDEO] = imx_clk_hw_pllv3(IMX_PLLV3_AV, "pll5_video",
		"pll5_video_ref_sel", pll_base + 0xa0, 0x7f);

	/* PLL bypass out */
	hws[IMXRT1050_CLK_PLL1_BYPASS] = imx_clk_hw_mux_flags("pll1_bypass", pll_base + 0x0, 16, 1,
		pll1_bypass_sels, ARRAY_SIZE(pll1_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMXRT1050_CLK_PLL2_BYPASS] = imx_clk_hw_mux_flags("pll2_bypass", pll_base + 0x30, 16, 1,
		pll2_bypass_sels, ARRAY_SIZE(pll2_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMXRT1050_CLK_PLL3_BYPASS] = imx_clk_hw_mux_flags("pll3_bypass", pll_base + 0x10, 16, 1,
		pll3_bypass_sels, ARRAY_SIZE(pll3_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMXRT1050_CLK_PLL5_BYPASS] = imx_clk_hw_mux_flags("pll5_bypass", pll_base + 0xa0, 16, 1,
		pll5_bypass_sels, ARRAY_SIZE(pll5_bypass_sels), CLK_SET_RATE_PARENT);

	hws[IMXRT1050_CLK_VIDEO_POST_DIV_SEL] = imx_clk_hw_divider("video_post_div_sel",
		"pll5_video", pll_base + 0xa0, 19, 2);
	hws[IMXRT1050_CLK_VIDEO_DIV] = imx_clk_hw_divider("video_div",
		"video_post_div_sel", pll_base + 0x170, 30, 2);

	hws[IMXRT1050_CLK_PLL3_80M] = imx_clk_hw_fixed_factor("pll3_80m",  "pll3_usb_otg", 1, 6);

	hws[IMXRT1050_CLK_PLL2_PFD0_352M] = imx_clk_hw_pfd("pll2_pfd0_352m", "pll2_sys", pll_base + 0x100, 0);
	hws[IMXRT1050_CLK_PLL2_PFD1_594M] = imx_clk_hw_pfd("pll2_pfd1_594m", "pll2_sys", pll_base + 0x100, 1);
	hws[IMXRT1050_CLK_PLL2_PFD2_396M] = imx_clk_hw_pfd("pll2_pfd2_396m", "pll2_sys", pll_base + 0x100, 2);
	hws[IMXRT1050_CLK_PLL3_PFD1_664_62M] = imx_clk_hw_pfd("pll3_pfd1_664_62m", "pll3_usb_otg", pll_base + 0xf0, 1);
	hws[IMXRT1050_CLK_PLL3_PFD3_454_74M] = imx_clk_hw_pfd("pll3_pfd3_454_74m", "pll3_usb_otg", pll_base + 0xf0, 3);

	/* CCM clocks */
	ccm_base = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(ccm_base))) {
		ret = PTR_ERR(ccm_base);
		goto unregister_hws;
	}

	hws[IMXRT1050_CLK_ARM_PODF] = imx_clk_hw_divider("arm_podf", "pll1_arm", ccm_base + 0x10, 0, 3);
	hws[IMXRT1050_CLK_PRE_PERIPH_SEL] = imx_clk_hw_mux("pre_periph_sel", ccm_base + 0x18, 18, 2,
		pre_periph_sels, ARRAY_SIZE(pre_periph_sels));
	hws[IMXRT1050_CLK_PERIPH_SEL] = imx_clk_hw_mux("periph_sel", ccm_base + 0x14, 25, 1,
		periph_sels, ARRAY_SIZE(periph_sels));
	hws[IMXRT1050_CLK_USDHC1_SEL] = imx_clk_hw_mux("usdhc1_sel", ccm_base + 0x1c, 16, 1,
		usdhc_sels, ARRAY_SIZE(usdhc_sels));
	hws[IMXRT1050_CLK_USDHC2_SEL] = imx_clk_hw_mux("usdhc2_sel", ccm_base + 0x1c, 17, 1,
		usdhc_sels, ARRAY_SIZE(usdhc_sels));
	hws[IMXRT1050_CLK_LPUART_SEL] = imx_clk_hw_mux("lpuart_sel", ccm_base + 0x24, 6, 1,
		lpuart_sels, ARRAY_SIZE(lpuart_sels));
	hws[IMXRT1050_CLK_LCDIF_SEL] = imx_clk_hw_mux("lcdif_sel", ccm_base + 0x38, 15, 3,
		lcdif_sels, ARRAY_SIZE(lcdif_sels));
	hws[IMXRT1050_CLK_PER_CLK_SEL] = imx_clk_hw_mux("per_sel", ccm_base + 0x1C, 6, 1,
		per_sels, ARRAY_SIZE(per_sels));
	hws[IMXRT1050_CLK_SEMC_ALT_SEL] = imx_clk_hw_mux("semc_alt_sel", ccm_base + 0x14, 7, 1,
		semc_alt_sels, ARRAY_SIZE(semc_alt_sels));
	hws[IMXRT1050_CLK_SEMC_SEL] = imx_clk_hw_mux_flags("semc_sel", ccm_base + 0x14, 6, 1,
		semc_sels, ARRAY_SIZE(semc_sels), CLK_IS_CRITICAL);

	hws[IMXRT1050_CLK_AHB_PODF] = imx_clk_hw_divider("ahb", "periph_sel", ccm_base + 0x14, 10, 3);
	hws[IMXRT1050_CLK_IPG_PDOF] = imx_clk_hw_divider("ipg", "ahb", ccm_base + 0x14, 8, 2);
	hws[IMXRT1050_CLK_PER_PDOF] = imx_clk_hw_divider("per", "per_sel", ccm_base + 0x1C, 0, 5);

	hws[IMXRT1050_CLK_USDHC1_PODF] = imx_clk_hw_divider("usdhc1_podf", "usdhc1_sel", ccm_base + 0x24, 11, 3);
	hws[IMXRT1050_CLK_USDHC2_PODF] = imx_clk_hw_divider("usdhc2_podf", "usdhc2_sel", ccm_base + 0x24, 16, 3);
	hws[IMXRT1050_CLK_LPUART_PODF] = imx_clk_hw_divider("lpuart_podf", "lpuart_sel", ccm_base + 0x24, 0, 6);
	hws[IMXRT1050_CLK_LCDIF_PRED] = imx_clk_hw_divider("lcdif_pred", "lcdif_sel", ccm_base + 0x38, 12, 3);
	hws[IMXRT1050_CLK_LCDIF_PODF] = imx_clk_hw_divider("lcdif_podf", "lcdif_pred", ccm_base + 0x18, 23, 3);

	hws[IMXRT1050_CLK_USDHC1] = imx_clk_hw_gate2("usdhc1", "usdhc1_podf", ccm_base + 0x80, 2);
	hws[IMXRT1050_CLK_USDHC2] = imx_clk_hw_gate2("usdhc2", "usdhc2_podf", ccm_base + 0x80, 4);
	hws[IMXRT1050_CLK_LPUART1] = imx_clk_hw_gate2("lpuart1", "lpuart_podf", ccm_base + 0x7c, 24);
	hws[IMXRT1050_CLK_LCDIF_APB] = imx_clk_hw_gate2("lcdif", "lcdif_podf", ccm_base + 0x70, 28);
	hws[IMXRT1050_CLK_DMA] = imx_clk_hw_gate("dma", "ipg", ccm_base + 0x7C, 6);
	hws[IMXRT1050_CLK_DMA_MUX] = imx_clk_hw_gate("dmamux0", "ipg", ccm_base + 0x7C, 7);
	imx_check_clk_hws(hws, IMXRT1050_CLK_END);

	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_hw_data);
	if (ret < 0) {
		dev_err(dev, "Failed to register clks for i.MXRT1050.\n");
		goto unregister_hws;
	}
	return 0;

unregister_hws:
	imx_unregister_hw_clocks(hws, IMXRT1050_CLK_END);
	return ret;
}
static const struct of_device_id imxrt1050_clk_of_match[] = {
	{ .compatible = "fsl,imxrt1050-ccm" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, imxrt1050_clk_of_match);

static struct platform_driver imxrt1050_clk_driver = {
	.probe = imxrt1050_clocks_probe,
	.driver = {
		.name = "imxrt1050-ccm",
		.of_match_table = imxrt1050_clk_of_match,
	},
};
module_platform_driver(imxrt1050_clk_driver);
