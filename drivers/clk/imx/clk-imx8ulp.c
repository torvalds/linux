// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021 NXP
 */

#include <dt-bindings/clock/imx8ulp-clock.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>

#include "clk.h"

static const char * const pll_pre_sels[] = { "sosc", "frosc", };
static const char * const a35_sels[] = { "frosc", "spll2", "sosc", "lvds", };
static const char * const nic_sels[] = { "frosc", "spll3_pfd0", "sosc", "lvds", };
static const char * const pcc3_periph_bus_sels[] = { "dummy", "lposc", "sosc_div2",
						     "frosc_div2", "xbar_divbus", "spll3_pfd1_div1",
						     "spll3_pfd0_div2", "spll3_pfd0_div1", };
static const char * const pcc4_periph_bus_sels[] = { "dummy", "dummy", "lposc",
						     "sosc_div2", "frosc_div2", "xbar_divbus",
						     "spll3_vcodiv", "spll3_pfd0_div1", };
static const char * const pcc4_periph_plat_sels[] = { "dummy", "sosc_div1", "frosc_div1",
						      "spll3_pfd3_div2", "spll3_pfd3_div1",
						      "spll3_pfd2_div2", "spll3_pfd2_div1",
						      "spll3_pfd1_div2", };
static const char * const pcc5_periph_bus_sels[] = { "dummy", "dummy", "lposc",
						     "sosc_div2", "frosc_div2", "lpav_bus_clk",
						     "pll4_vcodiv", "pll4_pfd3_div1", };
static const char * const pcc5_periph_plat_sels[] = { "dummy", "pll4_pfd3_div2", "pll4_pfd2_div2",
						      "pll4_pfd2_div1", "pll4_pfd1_div2",
						      "pll4_pfd1_div1", "pll4_pfd0_div2",
						      "pll4_pfd0_div1", };
static const char * const hifi_sels[] = { "frosc", "pll4", "pll4_pfd0", "sosc",
					 "lvds", "dummy", "dummy", "dummy", };
static const char * const ddr_sels[] = { "frosc", "pll4_pfd1", "sosc", "lvds",
					 "pll4", "pll4", "pll4", "pll4", };
static const char * const lpav_sels[] = { "frosc", "pll4_pfd1", "sosc", "lvds", };
static const char * const sai45_sels[] = { "spll3_pfd1_div1", "aud_clk1", "aud_clk2", "sosc", };
static const char * const sai67_sels[] = { "spll1_pfd2_div", "spll3_pfd1_div1", "aud_clk0", "aud_clk1", "aud_clk2", "sosc", "dummy", "dummy", };
static const char * const aud_clk1_sels[] = { "ext_aud_mclk2", "sai4_rx_bclk", "sai4_tx_bclk", "sai5_rx_bclk", "sai5_tx_bclk", "dummy", "dummy", "dummy", };
static const char * const aud_clk2_sels[] = { "ext_aud_mclk3", "sai6_rx_bclk", "sai6_tx_bclk", "sai7_rx_bclk", "sai7_tx_bclk", "spdif_rx", "dummy", "dummy", };
static const char * const enet_ts_sels[] = { "ext_rmii_clk", "ext_ts_clk", "rosc", "ext_aud_mclk", "sosc", "dummy", "dummy", "dummy"};
static const char * const xbar_divbus[] = { "xbar_divbus" };
static const char * const nic_per_divplat[] = { "nic_per_divplat" };
static const char * const lpav_axi_div[] = { "lpav_axi_div" };
static const char * const lpav_bus_div[] = { "lpav_bus_div" };

struct pcc_reset_dev {
	void __iomem *base;
	struct reset_controller_dev rcdev;
	const u32 *resets;
	/* Set to imx_ccm_lock to protect register access shared with clock control */
	spinlock_t *lock;
};

#define PCC_SW_RST	BIT(28)
#define to_pcc_reset_dev(_rcdev)	container_of(_rcdev, struct pcc_reset_dev, rcdev)

static const u32 pcc3_resets[] = {
	0xa8, 0xac, 0xc8, 0xcc, 0xd0,
	0xd4, 0xd8, 0xdc, 0xe0, 0xe4,
	0xe8, 0xec, 0xf0
};

static const u32 pcc4_resets[] = {
	0x4, 0x8, 0xc, 0x10, 0x14,
	0x18, 0x1c, 0x20, 0x24, 0x34,
	0x38, 0x3c, 0x40, 0x44, 0x48,
	0x4c, 0x54
};

static const u32 pcc5_resets[] = {
	0xa0, 0xa4, 0xa8, 0xac, 0xb0,
	0xb4, 0xbc, 0xc0, 0xc8, 0xcc,
	0xd0, 0xf0, 0xf4, 0xf8
};

static int imx8ulp_pcc_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct pcc_reset_dev *pcc_reset = to_pcc_reset_dev(rcdev);
	u32 offset = pcc_reset->resets[id];
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(pcc_reset->lock, flags);

	val = readl(pcc_reset->base + offset);
	val &= ~PCC_SW_RST;
	writel(val, pcc_reset->base + offset);

	spin_unlock_irqrestore(pcc_reset->lock, flags);

	return 0;
}

static int imx8ulp_pcc_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct pcc_reset_dev *pcc_reset = to_pcc_reset_dev(rcdev);
	u32 offset = pcc_reset->resets[id];
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(pcc_reset->lock, flags);

	val = readl(pcc_reset->base + offset);
	val |= PCC_SW_RST;
	writel(val, pcc_reset->base + offset);

	spin_unlock_irqrestore(pcc_reset->lock, flags);

	return 0;
}

static const struct reset_control_ops imx8ulp_pcc_reset_ops = {
	.assert = imx8ulp_pcc_assert,
	.deassert = imx8ulp_pcc_deassert,
};

static int imx8ulp_pcc_reset_init(struct platform_device *pdev, void __iomem *base,
	 const u32 *resets, unsigned int nr_resets)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct pcc_reset_dev *pcc_reset;

	pcc_reset = devm_kzalloc(dev, sizeof(*pcc_reset), GFP_KERNEL);
	if (!pcc_reset)
		return -ENOMEM;

	pcc_reset->base = base;
	pcc_reset->lock = &imx_ccm_lock;
	pcc_reset->resets = resets;
	pcc_reset->rcdev.owner = THIS_MODULE;
	pcc_reset->rcdev.nr_resets = nr_resets;
	pcc_reset->rcdev.ops = &imx8ulp_pcc_reset_ops;
	pcc_reset->rcdev.of_node = np;

	return devm_reset_controller_register(dev, &pcc_reset->rcdev);
}

static int imx8ulp_clk_cgc1_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_hw_onecell_data *clk_data;
	struct clk_hw **clks;
	void __iomem *base;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, IMX8ULP_CLK_CGC1_END),
			   GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = IMX8ULP_CLK_CGC1_END;
	clks = clk_data->hws;

	clks[IMX8ULP_CLK_DUMMY] = imx_clk_hw_fixed("dummy", 0);

	/* CGC1 */
	base = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(base)))
		return PTR_ERR(base);

	clks[IMX8ULP_CLK_SPLL2_PRE_SEL]	= imx_clk_hw_mux_flags("spll2_pre_sel", base + 0x510, 0, 1, pll_pre_sels, ARRAY_SIZE(pll_pre_sels), CLK_SET_PARENT_GATE);
	clks[IMX8ULP_CLK_SPLL3_PRE_SEL]	= imx_clk_hw_mux_flags("spll3_pre_sel", base + 0x610, 0, 1, pll_pre_sels, ARRAY_SIZE(pll_pre_sels), CLK_SET_PARENT_GATE);

	clks[IMX8ULP_CLK_SPLL2] = imx_clk_hw_pllv4(IMX_PLLV4_IMX8ULP, "spll2", "spll2_pre_sel", base + 0x500);
	clks[IMX8ULP_CLK_SPLL3] = imx_clk_hw_pllv4(IMX_PLLV4_IMX8ULP, "spll3", "spll3_pre_sel", base + 0x600);
	clks[IMX8ULP_CLK_SPLL3_VCODIV] = imx_clk_hw_divider("spll3_vcodiv", "spll3", base + 0x604, 0, 6);

	clks[IMX8ULP_CLK_SPLL3_PFD0] = imx_clk_hw_pfdv2(IMX_PFDV2_IMX8ULP, "spll3_pfd0", "spll3_vcodiv", base + 0x614, 0);
	clks[IMX8ULP_CLK_SPLL3_PFD1] = imx_clk_hw_pfdv2(IMX_PFDV2_IMX8ULP, "spll3_pfd1", "spll3_vcodiv", base + 0x614, 1);
	clks[IMX8ULP_CLK_SPLL3_PFD2] = imx_clk_hw_pfdv2(IMX_PFDV2_IMX8ULP, "spll3_pfd2", "spll3_vcodiv", base + 0x614, 2);
	clks[IMX8ULP_CLK_SPLL3_PFD3] = imx_clk_hw_pfdv2(IMX_PFDV2_IMX8ULP, "spll3_pfd3", "spll3_vcodiv", base + 0x614, 3);

	clks[IMX8ULP_CLK_SPLL3_PFD0_DIV1_GATE] = imx_clk_hw_gate_dis("spll3_pfd0_div1_gate", "spll3_pfd0", base + 0x608, 7);
	clks[IMX8ULP_CLK_SPLL3_PFD0_DIV2_GATE] = imx_clk_hw_gate_dis("spll3_pfd0_div2_gate", "spll3_pfd0", base + 0x608, 15);
	clks[IMX8ULP_CLK_SPLL3_PFD1_DIV1_GATE] = imx_clk_hw_gate_dis("spll3_pfd1_div1_gate", "spll3_pfd1", base + 0x608, 23);
	clks[IMX8ULP_CLK_SPLL3_PFD1_DIV2_GATE] = imx_clk_hw_gate_dis("spll3_pfd1_div2_gate", "spll3_pfd1", base + 0x608, 31);
	clks[IMX8ULP_CLK_SPLL3_PFD2_DIV1_GATE] = imx_clk_hw_gate_dis("spll3_pfd2_div1_gate", "spll3_pfd2", base + 0x60c, 7);
	clks[IMX8ULP_CLK_SPLL3_PFD2_DIV2_GATE] = imx_clk_hw_gate_dis("spll3_pfd2_div2_gate", "spll3_pfd2", base + 0x60c, 15);
	clks[IMX8ULP_CLK_SPLL3_PFD3_DIV1_GATE] = imx_clk_hw_gate_dis("spll3_pfd3_div1_gate", "spll3_pfd3", base + 0x60c, 23);
	clks[IMX8ULP_CLK_SPLL3_PFD3_DIV2_GATE] = imx_clk_hw_gate_dis("spll3_pfd3_div2_gate", "spll3_pfd3", base + 0x60c, 31);
	clks[IMX8ULP_CLK_SPLL3_PFD0_DIV1] = imx_clk_hw_divider("spll3_pfd0_div1", "spll3_pfd0_div1_gate", base + 0x608, 0, 6);
	clks[IMX8ULP_CLK_SPLL3_PFD0_DIV2] = imx_clk_hw_divider("spll3_pfd0_div2", "spll3_pfd0_div2_gate", base + 0x608, 8, 6);
	clks[IMX8ULP_CLK_SPLL3_PFD1_DIV1] = imx_clk_hw_divider("spll3_pfd1_div1", "spll3_pfd1_div1_gate", base + 0x608, 16, 6);
	clks[IMX8ULP_CLK_SPLL3_PFD1_DIV2] = imx_clk_hw_divider("spll3_pfd1_div2", "spll3_pfd1_div2_gate", base + 0x608, 24, 6);
	clks[IMX8ULP_CLK_SPLL3_PFD2_DIV1] = imx_clk_hw_divider("spll3_pfd2_div1", "spll3_pfd2_div1_gate", base + 0x60c, 0, 6);
	clks[IMX8ULP_CLK_SPLL3_PFD2_DIV2] = imx_clk_hw_divider("spll3_pfd2_div2", "spll3_pfd2_div2_gate", base + 0x60c, 8, 6);
	clks[IMX8ULP_CLK_SPLL3_PFD3_DIV1] = imx_clk_hw_divider("spll3_pfd3_div1", "spll3_pfd3_div1_gate", base + 0x60c, 16, 6);
	clks[IMX8ULP_CLK_SPLL3_PFD3_DIV2] = imx_clk_hw_divider("spll3_pfd3_div2", "spll3_pfd3_div2_gate", base + 0x60c, 24, 6);

	clks[IMX8ULP_CLK_A35_SEL] = imx_clk_hw_mux2("a35_sel", base + 0x14, 28, 2, a35_sels, ARRAY_SIZE(a35_sels));
	clks[IMX8ULP_CLK_A35_DIV] = imx_clk_hw_divider_flags("a35_div", "a35_sel", base + 0x14, 21, 6, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);

	clks[IMX8ULP_CLK_NIC_SEL] = imx_clk_hw_mux2("nic_sel", base + 0x34, 28, 2, nic_sels, ARRAY_SIZE(nic_sels));
	clks[IMX8ULP_CLK_NIC_AD_DIVPLAT] = imx_clk_hw_divider_flags("nic_ad_divplat", "nic_sel", base + 0x34, 21, 6, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);
	clks[IMX8ULP_CLK_NIC_PER_DIVPLAT] = imx_clk_hw_divider_flags("nic_per_divplat", "nic_ad_divplat", base + 0x34, 14, 6, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);
	clks[IMX8ULP_CLK_XBAR_AD_DIVPLAT] = imx_clk_hw_divider_flags("xbar_ad_divplat", "nic_ad_divplat", base + 0x38, 14, 6, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);
	clks[IMX8ULP_CLK_XBAR_DIVBUS] = imx_clk_hw_divider_flags("xbar_divbus", "nic_ad_divplat", base + 0x38, 7, 6, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);
	clks[IMX8ULP_CLK_XBAR_AD_SLOW] = imx_clk_hw_divider_flags("xbar_ad_slow", "nic_ad_divplat", base + 0x38, 0, 6, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);

	clks[IMX8ULP_CLK_SOSC_DIV1_GATE] = imx_clk_hw_gate_dis("sosc_div1_gate", "sosc", base + 0x108, 7);
	clks[IMX8ULP_CLK_SOSC_DIV2_GATE] = imx_clk_hw_gate_dis("sosc_div2_gate", "sosc", base + 0x108, 15);
	clks[IMX8ULP_CLK_SOSC_DIV3_GATE] = imx_clk_hw_gate_dis("sosc_div3_gate", "sosc", base + 0x108, 23);
	clks[IMX8ULP_CLK_SOSC_DIV1] = imx_clk_hw_divider("sosc_div1", "sosc_div1_gate", base + 0x108, 0, 6);
	clks[IMX8ULP_CLK_SOSC_DIV2] = imx_clk_hw_divider("sosc_div2", "sosc_div2_gate", base + 0x108, 8, 6);
	clks[IMX8ULP_CLK_SOSC_DIV3] = imx_clk_hw_divider("sosc_div3", "sosc_div3_gate", base + 0x108, 16, 6);

	clks[IMX8ULP_CLK_FROSC_DIV1_GATE] = imx_clk_hw_gate_dis("frosc_div1_gate", "frosc", base + 0x208, 7);
	clks[IMX8ULP_CLK_FROSC_DIV2_GATE] = imx_clk_hw_gate_dis("frosc_div2_gate", "frosc", base + 0x208, 15);
	clks[IMX8ULP_CLK_FROSC_DIV3_GATE] = imx_clk_hw_gate_dis("frosc_div3_gate", "frosc", base + 0x208, 23);
	clks[IMX8ULP_CLK_FROSC_DIV1] = imx_clk_hw_divider("frosc_div1", "frosc_div1_gate", base + 0x208, 0, 6);
	clks[IMX8ULP_CLK_FROSC_DIV2] = imx_clk_hw_divider("frosc_div2", "frosc_div2_gate", base + 0x208, 8, 6);
	clks[IMX8ULP_CLK_FROSC_DIV3] = imx_clk_hw_divider("frosc_div3", "frosc_div3_gate", base + 0x208, 16, 6);
	clks[IMX8ULP_CLK_AUD_CLK1] = imx_clk_hw_mux2("aud_clk1", base + 0x900, 0, 3, aud_clk1_sels, ARRAY_SIZE(aud_clk1_sels));
	clks[IMX8ULP_CLK_SAI4_SEL] = imx_clk_hw_mux2("sai4_sel", base + 0x904, 0, 2, sai45_sels, ARRAY_SIZE(sai45_sels));
	clks[IMX8ULP_CLK_SAI5_SEL] = imx_clk_hw_mux2("sai5_sel", base + 0x904, 8, 2, sai45_sels, ARRAY_SIZE(sai45_sels));
	clks[IMX8ULP_CLK_ENET_TS_SEL] = imx_clk_hw_mux2("enet_ts", base + 0x700, 24, 3, enet_ts_sels, ARRAY_SIZE(enet_ts_sels));

	imx_check_clk_hws(clks, clk_data->num);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
}

static int imx8ulp_clk_cgc2_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_hw_onecell_data *clk_data;
	struct clk_hw **clks;
	void __iomem *base;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, IMX8ULP_CLK_CGC2_END),
			   GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = IMX8ULP_CLK_CGC2_END;
	clks = clk_data->hws;

	/* CGC2 */
	base = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(base)))
		return PTR_ERR(base);

	clks[IMX8ULP_CLK_PLL4_PRE_SEL] = imx_clk_hw_mux_flags("pll4_pre_sel", base + 0x610, 0, 1, pll_pre_sels, ARRAY_SIZE(pll_pre_sels), CLK_SET_PARENT_GATE);

	clks[IMX8ULP_CLK_PLL4]	= imx_clk_hw_pllv4(IMX_PLLV4_IMX8ULP, "pll4", "pll4_pre_sel", base + 0x600);
	clks[IMX8ULP_CLK_PLL4_VCODIV] = imx_clk_hw_divider("pll4_vcodiv", "pll4", base + 0x604, 0, 6);

	clks[IMX8ULP_CLK_HIFI_SEL] = imx_clk_hw_mux_flags("hifi_sel", base + 0x14, 28, 3, hifi_sels, ARRAY_SIZE(hifi_sels), CLK_SET_PARENT_GATE);
	clks[IMX8ULP_CLK_HIFI_DIVCORE] = imx_clk_hw_divider("hifi_core_div", "hifi_sel", base + 0x14, 21, 6);
	clks[IMX8ULP_CLK_HIFI_DIVPLAT] = imx_clk_hw_divider("hifi_plat_div", "hifi_core_div", base + 0x14, 14, 6);

	clks[IMX8ULP_CLK_DDR_SEL] = imx_clk_hw_mux_flags("ddr_sel", base + 0x40, 28, 3, ddr_sels, ARRAY_SIZE(ddr_sels), CLK_SET_PARENT_GATE);
	clks[IMX8ULP_CLK_DDR_DIV] = imx_clk_hw_divider_flags("ddr_div", "ddr_sel", base + 0x40, 21, 6, CLK_IS_CRITICAL);
	clks[IMX8ULP_CLK_LPAV_AXI_SEL] = imx_clk_hw_mux("lpav_sel", base + 0x3c, 28, 2, lpav_sels, ARRAY_SIZE(lpav_sels));
	clks[IMX8ULP_CLK_LPAV_AXI_DIV] = imx_clk_hw_divider_flags("lpav_axi_div", "lpav_sel", base + 0x3c, 21, 6, CLK_IS_CRITICAL);
	clks[IMX8ULP_CLK_LPAV_AHB_DIV] = imx_clk_hw_divider_flags("lpav_ahb_div", "lpav_axi_div", base + 0x3c, 14, 6, CLK_IS_CRITICAL);
	clks[IMX8ULP_CLK_LPAV_BUS_DIV] = imx_clk_hw_divider_flags("lpav_bus_div", "lpav_axi_div", base + 0x3c, 7, 6, CLK_IS_CRITICAL);

	clks[IMX8ULP_CLK_PLL4_PFD0] = imx_clk_hw_pfdv2(IMX_PFDV2_IMX8ULP, "pll4_pfd0", "pll4_vcodiv", base + 0x614, 0);
	clks[IMX8ULP_CLK_PLL4_PFD1] = imx_clk_hw_pfdv2(IMX_PFDV2_IMX8ULP, "pll4_pfd1", "pll4_vcodiv", base + 0x614, 1);
	clks[IMX8ULP_CLK_PLL4_PFD2] = imx_clk_hw_pfdv2(IMX_PFDV2_IMX8ULP, "pll4_pfd2", "pll4_vcodiv", base + 0x614, 2);
	clks[IMX8ULP_CLK_PLL4_PFD3] = imx_clk_hw_pfdv2(IMX_PFDV2_IMX8ULP, "pll4_pfd3", "pll4_vcodiv", base + 0x614, 3);

	clks[IMX8ULP_CLK_PLL4_PFD0_DIV1_GATE] = imx_clk_hw_gate_dis("pll4_pfd0_div1_gate", "pll4_pfd0", base + 0x608, 7);
	clks[IMX8ULP_CLK_PLL4_PFD0_DIV2_GATE] = imx_clk_hw_gate_dis("pll4_pfd0_div2_gate", "pll4_pfd0", base + 0x608, 15);
	clks[IMX8ULP_CLK_PLL4_PFD1_DIV1_GATE] = imx_clk_hw_gate_dis("pll4_pfd1_div1_gate", "pll4_pfd1", base + 0x608, 23);
	clks[IMX8ULP_CLK_PLL4_PFD1_DIV2_GATE] = imx_clk_hw_gate_dis("pll4_pfd1_div2_gate", "pll4_pfd1", base + 0x608, 31);
	clks[IMX8ULP_CLK_PLL4_PFD2_DIV1_GATE] = imx_clk_hw_gate_dis("pll4_pfd2_div1_gate", "pll4_pfd2", base + 0x60c, 7);
	clks[IMX8ULP_CLK_PLL4_PFD2_DIV2_GATE] = imx_clk_hw_gate_dis("pll4_pfd2_div2_gate", "pll4_pfd2", base + 0x60c, 15);
	clks[IMX8ULP_CLK_PLL4_PFD3_DIV1_GATE] = imx_clk_hw_gate_dis("pll4_pfd3_div1_gate", "pll4_pfd3", base + 0x60c, 23);
	clks[IMX8ULP_CLK_PLL4_PFD3_DIV2_GATE] = imx_clk_hw_gate_dis("pll4_pfd3_div2_gate", "pll4_pfd3", base + 0x60c, 31);
	clks[IMX8ULP_CLK_PLL4_PFD0_DIV1] = imx_clk_hw_divider("pll4_pfd0_div1", "pll4_pfd0_div1_gate", base + 0x608, 0, 6);
	clks[IMX8ULP_CLK_PLL4_PFD0_DIV2] = imx_clk_hw_divider("pll4_pfd0_div2", "pll4_pfd0_div2_gate", base + 0x608, 8, 6);
	clks[IMX8ULP_CLK_PLL4_PFD1_DIV1] = imx_clk_hw_divider("pll4_pfd1_div1", "pll4_pfd1_div1_gate", base + 0x608, 16, 6);
	clks[IMX8ULP_CLK_PLL4_PFD1_DIV2] = imx_clk_hw_divider("pll4_pfd1_div2", "pll4_pfd1_div2_gate", base + 0x608, 24, 6);
	clks[IMX8ULP_CLK_PLL4_PFD2_DIV1] = imx_clk_hw_divider("pll4_pfd2_div1", "pll4_pfd2_div1_gate", base + 0x60c, 0, 6);
	clks[IMX8ULP_CLK_PLL4_PFD2_DIV2] = imx_clk_hw_divider("pll4_pfd2_div2", "pll4_pfd2_div2_gate", base + 0x60c, 8, 6);
	clks[IMX8ULP_CLK_PLL4_PFD3_DIV1] = imx_clk_hw_divider("pll4_pfd3_div1", "pll4_pfd3_div1_gate", base + 0x60c, 16, 6);
	clks[IMX8ULP_CLK_PLL4_PFD3_DIV2] = imx_clk_hw_divider("pll4_pfd3_div2", "pll4_pfd3_div2_gate", base + 0x60c, 24, 6);

	clks[IMX8ULP_CLK_CGC2_SOSC_DIV1_GATE] = imx_clk_hw_gate_dis("cgc2_sosc_div1_gate", "sosc", base + 0x108, 7);
	clks[IMX8ULP_CLK_CGC2_SOSC_DIV2_GATE] = imx_clk_hw_gate_dis("cgc2_sosc_div2_gate", "sosc", base + 0x108, 15);
	clks[IMX8ULP_CLK_CGC2_SOSC_DIV3_GATE] = imx_clk_hw_gate_dis("cgc2_sosc_div3_gate", "sosc", base + 0x108, 23);
	clks[IMX8ULP_CLK_CGC2_SOSC_DIV1] = imx_clk_hw_divider("cgc2_sosc_div1", "cgc2_sosc_div1_gate", base + 0x108, 0, 6);
	clks[IMX8ULP_CLK_CGC2_SOSC_DIV2] = imx_clk_hw_divider("cgc2_sosc_div2", "cgc2_sosc_div2_gate", base + 0x108, 8, 6);
	clks[IMX8ULP_CLK_CGC2_SOSC_DIV3] = imx_clk_hw_divider("cgc2_sosc_div3", "cgc2_sosc_div3_gate", base + 0x108, 16, 6);

	clks[IMX8ULP_CLK_CGC2_FROSC_DIV1_GATE] = imx_clk_hw_gate_dis("cgc2_frosc_div1_gate", "frosc", base + 0x208, 7);
	clks[IMX8ULP_CLK_CGC2_FROSC_DIV2_GATE] = imx_clk_hw_gate_dis("cgc2_frosc_div2_gate", "frosc", base + 0x208, 15);
	clks[IMX8ULP_CLK_CGC2_FROSC_DIV3_GATE] = imx_clk_hw_gate_dis("cgc2_frosc_div3_gate", "frosc", base + 0x208, 23);
	clks[IMX8ULP_CLK_CGC2_FROSC_DIV1] = imx_clk_hw_divider("cgc2_frosc_div1", "cgc2_frosc_div1_gate", base + 0x208, 0, 6);
	clks[IMX8ULP_CLK_CGC2_FROSC_DIV2] = imx_clk_hw_divider("cgc2_frosc_div2", "cgc2_frosc_div2_gate", base + 0x208, 8, 6);
	clks[IMX8ULP_CLK_CGC2_FROSC_DIV3] = imx_clk_hw_divider("cgc2_frosc_div3", "cgc2_frosc_div3_gate", base + 0x208, 16, 6);
	clks[IMX8ULP_CLK_AUD_CLK2]  = imx_clk_hw_mux2("aud_clk2", base + 0x900, 0, 3, aud_clk2_sels, ARRAY_SIZE(aud_clk2_sels));
	clks[IMX8ULP_CLK_SAI6_SEL]  = imx_clk_hw_mux2("sai6_sel", base + 0x904, 0, 3, sai67_sels, ARRAY_SIZE(sai67_sels));
	clks[IMX8ULP_CLK_SAI7_SEL]  = imx_clk_hw_mux2("sai7_sel", base + 0x904, 8, 3, sai67_sels, ARRAY_SIZE(sai67_sels));
	clks[IMX8ULP_CLK_SPDIF_SEL] = imx_clk_hw_mux2("spdif_sel", base + 0x910, 0, 3, sai67_sels, ARRAY_SIZE(sai67_sels));
	clks[IMX8ULP_CLK_DSI_PHY_REF] = imx_clk_hw_fixed("dsi_phy_ref", 24000000);

	imx_check_clk_hws(clks, clk_data->num);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
}

static int imx8ulp_clk_pcc3_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_hw_onecell_data *clk_data;
	struct clk_hw **clks;
	void __iomem *base;
	int ret;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, IMX8ULP_CLK_PCC3_END),
			   GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = IMX8ULP_CLK_PCC3_END;
	clks = clk_data->hws;

	/* PCC3 */
	base = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(base)))
		return PTR_ERR(base);

	clks[IMX8ULP_CLK_WDOG3] = imx8ulp_clk_hw_composite("wdog3", pcc3_periph_bus_sels, ARRAY_SIZE(pcc3_periph_bus_sels), true, true, true, base + 0xa8, 1);
	clks[IMX8ULP_CLK_WDOG4] = imx8ulp_clk_hw_composite("wdog4", pcc3_periph_bus_sels, ARRAY_SIZE(pcc3_periph_bus_sels), true, true, true, base + 0xac, 1);
	clks[IMX8ULP_CLK_LPIT1] = imx8ulp_clk_hw_composite("lpit1", pcc3_periph_bus_sels, ARRAY_SIZE(pcc3_periph_bus_sels), true, true, true, base + 0xc8, 1);
	clks[IMX8ULP_CLK_TPM4] = imx8ulp_clk_hw_composite("tpm4", pcc3_periph_bus_sels, ARRAY_SIZE(pcc3_periph_bus_sels), true, true, true, base + 0xcc, 1);
	clks[IMX8ULP_CLK_TPM5] = imx8ulp_clk_hw_composite("tpm5", pcc3_periph_bus_sels, ARRAY_SIZE(pcc3_periph_bus_sels), true, true, true, base + 0xd0, 1);
	clks[IMX8ULP_CLK_FLEXIO1] = imx8ulp_clk_hw_composite("flexio1", pcc3_periph_bus_sels, ARRAY_SIZE(pcc3_periph_bus_sels), true, true, true, base + 0xd4, 1);
	clks[IMX8ULP_CLK_I3C2] = imx8ulp_clk_hw_composite("i3c2", pcc3_periph_bus_sels, ARRAY_SIZE(pcc3_periph_bus_sels), true, true, true, base + 0xd8, 1);
	clks[IMX8ULP_CLK_LPI2C4] = imx8ulp_clk_hw_composite("lpi2c4", pcc3_periph_bus_sels, ARRAY_SIZE(pcc3_periph_bus_sels), true, true, true, base + 0xdc, 1);
	clks[IMX8ULP_CLK_LPI2C5] = imx8ulp_clk_hw_composite("lpi2c5", pcc3_periph_bus_sels, ARRAY_SIZE(pcc3_periph_bus_sels), true, true, true, base + 0xe0, 1);
	clks[IMX8ULP_CLK_LPUART4] = imx8ulp_clk_hw_composite("lpuart4", pcc3_periph_bus_sels, ARRAY_SIZE(pcc3_periph_bus_sels), true, true, true, base + 0xe4, 1);
	clks[IMX8ULP_CLK_LPUART5] = imx8ulp_clk_hw_composite("lpuart5", pcc3_periph_bus_sels, ARRAY_SIZE(pcc3_periph_bus_sels), true, true, true, base + 0xe8, 1);
	clks[IMX8ULP_CLK_LPSPI4] = imx8ulp_clk_hw_composite("lpspi4", pcc3_periph_bus_sels, ARRAY_SIZE(pcc3_periph_bus_sels), true, true, true, base + 0xec, 1);
	clks[IMX8ULP_CLK_LPSPI5] = imx8ulp_clk_hw_composite("lpspi5", pcc3_periph_bus_sels, ARRAY_SIZE(pcc3_periph_bus_sels), true, true, true, base + 0xf0, 1);

	clks[IMX8ULP_CLK_DMA1_MP] = imx_clk_hw_gate("pcc_dma1_mp", "xbar_ad_divplat", base + 0x4, 30);
	clks[IMX8ULP_CLK_DMA1_CH0] = imx_clk_hw_gate("pcc_dma1_ch0", "xbar_ad_divplat", base + 0x8, 30);
	clks[IMX8ULP_CLK_DMA1_CH1] = imx_clk_hw_gate("pcc_dma1_ch1", "xbar_ad_divplat", base + 0xc, 30);
	clks[IMX8ULP_CLK_DMA1_CH2] = imx_clk_hw_gate("pcc_dma1_ch2", "xbar_ad_divplat", base + 0x10, 30);
	clks[IMX8ULP_CLK_DMA1_CH3] = imx_clk_hw_gate("pcc_dma1_ch3", "xbar_ad_divplat", base + 0x14, 30);
	clks[IMX8ULP_CLK_DMA1_CH4] = imx_clk_hw_gate("pcc_dma1_ch4", "xbar_ad_divplat", base + 0x18, 30);
	clks[IMX8ULP_CLK_DMA1_CH5] = imx_clk_hw_gate("pcc_dma1_ch5", "xbar_ad_divplat", base + 0x1c, 30);
	clks[IMX8ULP_CLK_DMA1_CH6] = imx_clk_hw_gate("pcc_dma1_ch6", "xbar_ad_divplat", base + 0x20, 30);
	clks[IMX8ULP_CLK_DMA1_CH7] = imx_clk_hw_gate("pcc_dma1_ch7", "xbar_ad_divplat", base + 0x24, 30);
	clks[IMX8ULP_CLK_DMA1_CH8] = imx_clk_hw_gate("pcc_dma1_ch8", "xbar_ad_divplat", base + 0x28, 30);
	clks[IMX8ULP_CLK_DMA1_CH9] = imx_clk_hw_gate("pcc_dma1_ch9", "xbar_ad_divplat", base + 0x2c, 30);
	clks[IMX8ULP_CLK_DMA1_CH10] = imx_clk_hw_gate("pcc_dma1_ch10", "xbar_ad_divplat", base + 0x30, 30);
	clks[IMX8ULP_CLK_DMA1_CH11] = imx_clk_hw_gate("pcc_dma1_ch11", "xbar_ad_divplat", base + 0x34, 30);
	clks[IMX8ULP_CLK_DMA1_CH12] = imx_clk_hw_gate("pcc_dma1_ch12", "xbar_ad_divplat", base + 0x38, 30);
	clks[IMX8ULP_CLK_DMA1_CH13] = imx_clk_hw_gate("pcc_dma1_ch13", "xbar_ad_divplat", base + 0x3c, 30);
	clks[IMX8ULP_CLK_DMA1_CH14] = imx_clk_hw_gate("pcc_dma1_ch14", "xbar_ad_divplat", base + 0x40, 30);
	clks[IMX8ULP_CLK_DMA1_CH15] = imx_clk_hw_gate("pcc_dma1_ch15", "xbar_ad_divplat", base + 0x44, 30);
	clks[IMX8ULP_CLK_DMA1_CH16] = imx_clk_hw_gate("pcc_dma1_ch16", "xbar_ad_divplat", base + 0x48, 30);
	clks[IMX8ULP_CLK_DMA1_CH17] = imx_clk_hw_gate("pcc_dma1_ch17", "xbar_ad_divplat", base + 0x4c, 30);
	clks[IMX8ULP_CLK_DMA1_CH18] = imx_clk_hw_gate("pcc_dma1_ch18", "xbar_ad_divplat", base + 0x50, 30);
	clks[IMX8ULP_CLK_DMA1_CH19] = imx_clk_hw_gate("pcc_dma1_ch19", "xbar_ad_divplat", base + 0x54, 30);
	clks[IMX8ULP_CLK_DMA1_CH20] = imx_clk_hw_gate("pcc_dma1_ch20", "xbar_ad_divplat", base + 0x58, 30);
	clks[IMX8ULP_CLK_DMA1_CH21] = imx_clk_hw_gate("pcc_dma1_ch21", "xbar_ad_divplat", base + 0x5c, 30);
	clks[IMX8ULP_CLK_DMA1_CH22] = imx_clk_hw_gate("pcc_dma1_ch22", "xbar_ad_divplat", base + 0x60, 30);
	clks[IMX8ULP_CLK_DMA1_CH23] = imx_clk_hw_gate("pcc_dma1_ch23", "xbar_ad_divplat", base + 0x64, 30);
	clks[IMX8ULP_CLK_DMA1_CH24] = imx_clk_hw_gate("pcc_dma1_ch24", "xbar_ad_divplat", base + 0x68, 30);
	clks[IMX8ULP_CLK_DMA1_CH25] = imx_clk_hw_gate("pcc_dma1_ch25", "xbar_ad_divplat", base + 0x6c, 30);
	clks[IMX8ULP_CLK_DMA1_CH26] = imx_clk_hw_gate("pcc_dma1_ch26", "xbar_ad_divplat", base + 0x70, 30);
	clks[IMX8ULP_CLK_DMA1_CH27] = imx_clk_hw_gate("pcc_dma1_ch27", "xbar_ad_divplat", base + 0x74, 30);
	clks[IMX8ULP_CLK_DMA1_CH28] = imx_clk_hw_gate("pcc_dma1_ch28", "xbar_ad_divplat", base + 0x78, 30);
	clks[IMX8ULP_CLK_DMA1_CH29] = imx_clk_hw_gate("pcc_dma1_ch29", "xbar_ad_divplat", base + 0x7c, 30);
	clks[IMX8ULP_CLK_DMA1_CH30] = imx_clk_hw_gate("pcc_dma1_ch30", "xbar_ad_divplat", base + 0x80, 30);
	clks[IMX8ULP_CLK_DMA1_CH31] = imx_clk_hw_gate("pcc_dma1_ch31", "xbar_ad_divplat", base + 0x84, 30);
	clks[IMX8ULP_CLK_MU0_B] = imx_clk_hw_gate("mu0_b", "xbar_ad_divplat", base + 0x88, 30);
	clks[IMX8ULP_CLK_MU3_A] = imx_clk_hw_gate("mu3_a", "xbar_ad_divplat", base + 0x8c, 30);

	imx_check_clk_hws(clks, clk_data->num);

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
	if (ret)
		return ret;

	imx_register_uart_clocks();

	/* register the pcc3 reset controller */
	return imx8ulp_pcc_reset_init(pdev, base, pcc3_resets, ARRAY_SIZE(pcc3_resets));
}

static int imx8ulp_clk_pcc4_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_hw_onecell_data *clk_data;
	struct clk_hw **clks;
	void __iomem *base;
	int ret;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, IMX8ULP_CLK_PCC4_END),
			   GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = IMX8ULP_CLK_PCC4_END;
	clks = clk_data->hws;

	/* PCC4 */
	base = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(base)))
		return PTR_ERR(base);

	clks[IMX8ULP_CLK_FLEXSPI2] = imx8ulp_clk_hw_composite("flexspi2", pcc4_periph_plat_sels, ARRAY_SIZE(pcc4_periph_plat_sels), true, true, true, base + 0x4, 1);
	clks[IMX8ULP_CLK_TPM6] = imx8ulp_clk_hw_composite("tpm6", pcc4_periph_bus_sels, ARRAY_SIZE(pcc4_periph_bus_sels), true, true, true, base + 0x8, 1);
	clks[IMX8ULP_CLK_TPM7] = imx8ulp_clk_hw_composite("tpm7", pcc4_periph_bus_sels, ARRAY_SIZE(pcc4_periph_bus_sels), true, true, true, base + 0xc, 1);
	clks[IMX8ULP_CLK_LPI2C6] = imx8ulp_clk_hw_composite("lpi2c6", pcc4_periph_bus_sels, ARRAY_SIZE(pcc4_periph_bus_sels), true, true, true, base + 0x10, 1);
	clks[IMX8ULP_CLK_LPI2C7] = imx8ulp_clk_hw_composite("lpi2c7", pcc4_periph_bus_sels, ARRAY_SIZE(pcc4_periph_bus_sels), true, true, true, base + 0x14, 1);
	clks[IMX8ULP_CLK_LPUART6] = imx8ulp_clk_hw_composite("lpuart6", pcc4_periph_bus_sels, ARRAY_SIZE(pcc4_periph_bus_sels), true, true, true, base + 0x18, 1);
	clks[IMX8ULP_CLK_LPUART7] = imx8ulp_clk_hw_composite("lpuart7", pcc4_periph_bus_sels, ARRAY_SIZE(pcc4_periph_bus_sels), true, true, true, base + 0x1c, 1);
	clks[IMX8ULP_CLK_SAI4] = imx8ulp_clk_hw_composite("sai4", xbar_divbus, 1, false, false, true, base + 0x20, 1); /* sai ipg, NOT from sai sel */
	clks[IMX8ULP_CLK_SAI5] = imx8ulp_clk_hw_composite("sai5", xbar_divbus, 1, false, false, true, base + 0x24, 1); /* sai ipg */
	clks[IMX8ULP_CLK_PCTLE] = imx_clk_hw_gate("pctle", "xbar_divbus", base + 0x28, 30);
	clks[IMX8ULP_CLK_PCTLF] = imx_clk_hw_gate("pctlf", "xbar_divbus", base + 0x2c, 30);
	clks[IMX8ULP_CLK_USDHC0] = imx8ulp_clk_hw_composite("usdhc0", pcc4_periph_plat_sels, ARRAY_SIZE(pcc4_periph_plat_sels), true, false, true, base + 0x34, 1);
	clks[IMX8ULP_CLK_USDHC1] = imx8ulp_clk_hw_composite("usdhc1", pcc4_periph_plat_sels, ARRAY_SIZE(pcc4_periph_plat_sels), true, false, true, base + 0x38, 1);
	clks[IMX8ULP_CLK_USDHC2] = imx8ulp_clk_hw_composite("usdhc2", pcc4_periph_plat_sels, ARRAY_SIZE(pcc4_periph_plat_sels), true, false, true, base + 0x3c, 1);
	clks[IMX8ULP_CLK_USB0] = imx8ulp_clk_hw_composite("usb0", nic_per_divplat, 1, false, false, true, base + 0x40, 1);
	clks[IMX8ULP_CLK_USB0_PHY] = imx8ulp_clk_hw_composite("usb0_phy", xbar_divbus, 1, false, false, true, base + 0x44, 1);
	clks[IMX8ULP_CLK_USB1] = imx8ulp_clk_hw_composite("usb1", nic_per_divplat, 1, false, false, true, base + 0x48, 1);
	clks[IMX8ULP_CLK_USB1_PHY] = imx8ulp_clk_hw_composite("usb1_phy", xbar_divbus, 1, false, false, true, base + 0x4c, 1);
	clks[IMX8ULP_CLK_USB_XBAR] = imx_clk_hw_gate("usb_xbar", "xbar_divbus", base + 0x50, 30);
	clks[IMX8ULP_CLK_ENET] = imx8ulp_clk_hw_composite("enet", nic_per_divplat, 1, false, false, true, base + 0x54, 1);
	clks[IMX8ULP_CLK_RGPIOE] = imx_clk_hw_gate("rgpioe", "nic_per_divplat", base + 0x78, 30);
	clks[IMX8ULP_CLK_RGPIOF] = imx_clk_hw_gate("rgpiof", "nic_per_divplat", base + 0x7c, 30);

	imx_check_clk_hws(clks, clk_data->num);

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
	if (ret)
		return ret;

	/* register the pcc4 reset controller */
	return imx8ulp_pcc_reset_init(pdev, base, pcc4_resets, ARRAY_SIZE(pcc4_resets));

}

static int imx8ulp_clk_pcc5_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_hw_onecell_data *clk_data;
	struct clk_hw **clks;
	void __iomem *base;
	int ret;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, IMX8ULP_CLK_PCC5_END),
			   GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = IMX8ULP_CLK_PCC5_END;
	clks = clk_data->hws;

	/* PCC5 */
	base = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(base)))
		return PTR_ERR(base);

	clks[IMX8ULP_CLK_DMA2_MP] = imx_clk_hw_gate("pcc_dma2_mp", "lpav_axi_div", base + 0x0, 30);
	clks[IMX8ULP_CLK_DMA2_CH0] = imx_clk_hw_gate("pcc_dma2_ch0", "lpav_axi_div", base + 0x4, 30);
	clks[IMX8ULP_CLK_DMA2_CH1] = imx_clk_hw_gate("pcc_dma2_ch1", "lpav_axi_div", base + 0x8, 30);
	clks[IMX8ULP_CLK_DMA2_CH2] = imx_clk_hw_gate("pcc_dma2_ch2", "lpav_axi_div", base + 0xc, 30);
	clks[IMX8ULP_CLK_DMA2_CH3] = imx_clk_hw_gate("pcc_dma2_ch3", "lpav_axi_div", base + 0x10, 30);
	clks[IMX8ULP_CLK_DMA2_CH4] = imx_clk_hw_gate("pcc_dma2_ch4", "lpav_axi_div", base + 0x14, 30);
	clks[IMX8ULP_CLK_DMA2_CH5] = imx_clk_hw_gate("pcc_dma2_ch5", "lpav_axi_div", base + 0x18, 30);
	clks[IMX8ULP_CLK_DMA2_CH6] = imx_clk_hw_gate("pcc_dma2_ch6", "lpav_axi_div", base + 0x1c, 30);
	clks[IMX8ULP_CLK_DMA2_CH7] = imx_clk_hw_gate("pcc_dma2_ch7", "lpav_axi_div", base + 0x20, 30);
	clks[IMX8ULP_CLK_DMA2_CH8] = imx_clk_hw_gate("pcc_dma2_ch8", "lpav_axi_div", base + 0x24, 30);
	clks[IMX8ULP_CLK_DMA2_CH9] = imx_clk_hw_gate("pcc_dma2_ch9", "lpav_axi_div", base + 0x28, 30);
	clks[IMX8ULP_CLK_DMA2_CH10] = imx_clk_hw_gate("pcc_dma2_ch10", "lpav_axi_div", base + 0x2c, 30);
	clks[IMX8ULP_CLK_DMA2_CH11] = imx_clk_hw_gate("pcc_dma2_ch11", "lpav_axi_div", base + 0x30, 30);
	clks[IMX8ULP_CLK_DMA2_CH12] = imx_clk_hw_gate("pcc_dma2_ch12", "lpav_axi_div", base + 0x34, 30);
	clks[IMX8ULP_CLK_DMA2_CH13] = imx_clk_hw_gate("pcc_dma2_ch13", "lpav_axi_div", base + 0x38, 30);
	clks[IMX8ULP_CLK_DMA2_CH14] = imx_clk_hw_gate("pcc_dma2_ch14", "lpav_axi_div", base + 0x3c, 30);
	clks[IMX8ULP_CLK_DMA2_CH15] = imx_clk_hw_gate("pcc_dma2_ch15", "lpav_axi_div", base + 0x40, 30);
	clks[IMX8ULP_CLK_DMA2_CH16] = imx_clk_hw_gate("pcc_dma2_ch16", "lpav_axi_div", base + 0x44, 30);
	clks[IMX8ULP_CLK_DMA2_CH17] = imx_clk_hw_gate("pcc_dma2_ch17", "lpav_axi_div", base + 0x48, 30);
	clks[IMX8ULP_CLK_DMA2_CH18] = imx_clk_hw_gate("pcc_dma2_ch18", "lpav_axi_div", base + 0x4c, 30);
	clks[IMX8ULP_CLK_DMA2_CH19] = imx_clk_hw_gate("pcc_dma2_ch19", "lpav_axi_div", base + 0x50, 30);
	clks[IMX8ULP_CLK_DMA2_CH20] = imx_clk_hw_gate("pcc_dma2_ch20", "lpav_axi_div", base + 0x54, 30);
	clks[IMX8ULP_CLK_DMA2_CH21] = imx_clk_hw_gate("pcc_dma2_ch21", "lpav_axi_div", base + 0x58, 30);
	clks[IMX8ULP_CLK_DMA2_CH22] = imx_clk_hw_gate("pcc_dma2_ch22", "lpav_axi_div", base + 0x5c, 30);
	clks[IMX8ULP_CLK_DMA2_CH23] = imx_clk_hw_gate("pcc_dma2_ch23", "lpav_axi_div", base + 0x60, 30);
	clks[IMX8ULP_CLK_DMA2_CH24] = imx_clk_hw_gate("pcc_dma2_ch24", "lpav_axi_div", base + 0x64, 30);
	clks[IMX8ULP_CLK_DMA2_CH25] = imx_clk_hw_gate("pcc_dma2_ch25", "lpav_axi_div", base + 0x68, 30);
	clks[IMX8ULP_CLK_DMA2_CH26] = imx_clk_hw_gate("pcc_dma2_ch26", "lpav_axi_div", base + 0x6c, 30);
	clks[IMX8ULP_CLK_DMA2_CH27] = imx_clk_hw_gate("pcc_dma2_ch27", "lpav_axi_div", base + 0x70, 30);
	clks[IMX8ULP_CLK_DMA2_CH28] = imx_clk_hw_gate("pcc_dma2_ch28", "lpav_axi_div", base + 0x74, 30);
	clks[IMX8ULP_CLK_DMA2_CH29] = imx_clk_hw_gate("pcc_dma2_ch29", "lpav_axi_div", base + 0x78, 30);
	clks[IMX8ULP_CLK_DMA2_CH30] = imx_clk_hw_gate("pcc_dma2_ch30", "lpav_axi_div", base + 0x7c, 30);
	clks[IMX8ULP_CLK_DMA2_CH31] = imx_clk_hw_gate("pcc_dma2_ch31", "lpav_axi_div", base + 0x80, 30);

	clks[IMX8ULP_CLK_AVD_SIM] = imx_clk_hw_gate("avd_sim", "lpav_bus_div", base + 0x94, 30);
	clks[IMX8ULP_CLK_TPM8] = imx8ulp_clk_hw_composite("tpm8", pcc5_periph_bus_sels, ARRAY_SIZE(pcc5_periph_bus_sels), true, true, true, base + 0xa0, 1);
	clks[IMX8ULP_CLK_MU2_B] = imx_clk_hw_gate("mu2_b", "lpav_bus_div", base + 0x84, 30);
	clks[IMX8ULP_CLK_MU3_B] = imx_clk_hw_gate("mu3_b", "lpav_bus_div", base + 0x88, 30);
	clks[IMX8ULP_CLK_SAI6] = imx8ulp_clk_hw_composite("sai6", lpav_bus_div, 1, false, false, true, base + 0xa4, 1);
	clks[IMX8ULP_CLK_SAI7] = imx8ulp_clk_hw_composite("sai7", lpav_bus_div, 1, false, false, true, base + 0xa8, 1);
	clks[IMX8ULP_CLK_SPDIF] = imx8ulp_clk_hw_composite("spdif", lpav_bus_div, 1, false, false, true, base + 0xac, 1);
	clks[IMX8ULP_CLK_ISI] = imx8ulp_clk_hw_composite("isi", lpav_axi_div, 1, false, false, true, base + 0xb0, 1);
	clks[IMX8ULP_CLK_CSI_REGS] = imx8ulp_clk_hw_composite("csi_regs", lpav_bus_div, 1, false, false, true, base + 0xb4, 1);
	clks[IMX8ULP_CLK_CSI] = imx8ulp_clk_hw_composite("csi", pcc5_periph_plat_sels, ARRAY_SIZE(pcc5_periph_plat_sels), true, true, true, base + 0xbc, 1);
	clks[IMX8ULP_CLK_DSI] = imx8ulp_clk_hw_composite("dsi", pcc5_periph_plat_sels, ARRAY_SIZE(pcc5_periph_plat_sels), true, true, true, base + 0xc0, 1);
	clks[IMX8ULP_CLK_WDOG5] = imx8ulp_clk_hw_composite("wdog5", pcc5_periph_bus_sels, ARRAY_SIZE(pcc5_periph_bus_sels), true, true, true, base + 0xc8, 1);
	clks[IMX8ULP_CLK_EPDC] = imx8ulp_clk_hw_composite("epdc", pcc5_periph_plat_sels, ARRAY_SIZE(pcc5_periph_plat_sels), true, true, true, base + 0xcc, 1);
	clks[IMX8ULP_CLK_PXP] = imx8ulp_clk_hw_composite("pxp", lpav_axi_div, 1, false, false, true, base + 0xd0, 1);
	clks[IMX8ULP_CLK_GPU2D] = imx8ulp_clk_hw_composite("gpu2d", pcc5_periph_plat_sels, ARRAY_SIZE(pcc5_periph_plat_sels), true, true, true, base + 0xf0, 1);
	clks[IMX8ULP_CLK_GPU3D] = imx8ulp_clk_hw_composite("gpu3d", pcc5_periph_plat_sels, ARRAY_SIZE(pcc5_periph_plat_sels), true, true, true, base + 0xf4, 1);
	clks[IMX8ULP_CLK_DC_NANO] = imx8ulp_clk_hw_composite("dc_nano", pcc5_periph_plat_sels, ARRAY_SIZE(pcc5_periph_plat_sels), true, true, true, base + 0xf8, 1);
	clks[IMX8ULP_CLK_CSI_CLK_UI] = imx8ulp_clk_hw_composite("csi_clk_ui", pcc5_periph_plat_sels, ARRAY_SIZE(pcc5_periph_plat_sels), true, true, true, base + 0x10c, 1);
	clks[IMX8ULP_CLK_CSI_CLK_ESC] = imx8ulp_clk_hw_composite("csi_clk_esc", pcc5_periph_plat_sels, ARRAY_SIZE(pcc5_periph_plat_sels), true, true, true, base + 0x110, 1);
	clks[IMX8ULP_CLK_RGPIOD] = imx_clk_hw_gate("rgpiod", "lpav_axi_div", base + 0x114, 30);
	clks[IMX8ULP_CLK_DSI_TX_ESC] = imx_clk_hw_fixed_factor("mipi_dsi_tx_esc", "dsi", 1, 4);

	imx_check_clk_hws(clks, clk_data->num);

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
	if (ret)
		return ret;

	/* register the pcc5 reset controller */
	return imx8ulp_pcc_reset_init(pdev, base, pcc5_resets, ARRAY_SIZE(pcc5_resets));
}

static int imx8ulp_clk_probe(struct platform_device *pdev)
{
	int (*probe)(struct platform_device *pdev);

	probe = of_device_get_match_data(&pdev->dev);

	if (probe)
		return probe(pdev);

	return 0;
}

static const struct of_device_id imx8ulp_clk_dt_ids[] = {
	{ .compatible = "fsl,imx8ulp-pcc3", .data = imx8ulp_clk_pcc3_init },
	{ .compatible = "fsl,imx8ulp-pcc4", .data = imx8ulp_clk_pcc4_init },
	{ .compatible = "fsl,imx8ulp-pcc5", .data = imx8ulp_clk_pcc5_init },
	{ .compatible = "fsl,imx8ulp-cgc2", .data = imx8ulp_clk_cgc2_init },
	{ .compatible = "fsl,imx8ulp-cgc1", .data = imx8ulp_clk_cgc1_init },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, imx8ulp_clk_dt_ids);

static struct platform_driver imx8ulp_clk_driver = {
	.probe	= imx8ulp_clk_probe,
	.driver = {
		.name		= KBUILD_MODNAME,
		.suppress_bind_attrs = true,
		.of_match_table	= imx8ulp_clk_dt_ids,
	},
};
module_platform_driver(imx8ulp_clk_driver);

MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_DESCRIPTION("NXP i.MX8ULP clock driver");
MODULE_LICENSE("GPL v2");
