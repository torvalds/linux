// SPDX-License-Identifier: GPL-2.0-only
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include <dt-bindings/clock/marvell,pxa1908.h>

#include "clk.h"

#define APMU_CLK_GATE_CTRL	0x40
#define APMU_CCIC1		0x24
#define APMU_ISP		0x38
#define APMU_DSI1		0x44
#define APMU_DISP1		0x4c
#define APMU_CCIC0		0x50
#define APMU_SDH0		0x54
#define APMU_SDH1		0x58
#define APMU_USB		0x5c
#define APMU_NF			0x60
#define APMU_VPU		0xa4
#define APMU_GC			0xcc
#define APMU_SDH2		0xe0
#define APMU_GC2D		0xf4
#define APMU_TRACE		0x108
#define APMU_DVC_DFC_DEBUG	0x140

#define APMU_NR_CLKS		17

struct pxa1908_clk_unit {
	struct mmp_clk_unit unit;
	void __iomem *base;
};

static DEFINE_SPINLOCK(pll1_lock);
static struct mmp_param_general_gate_clk pll1_gate_clks[] = {
	{PXA1908_CLK_PLL1_D2_GATE, "pll1_d2_gate", "pll1_d2", 0, APMU_CLK_GATE_CTRL, 29, 0, &pll1_lock},
	{PXA1908_CLK_PLL1_416_GATE, "pll1_416_gate", "pll1_416", 0, APMU_CLK_GATE_CTRL, 27, 0, &pll1_lock},
	{PXA1908_CLK_PLL1_624_GATE, "pll1_624_gate", "pll1_624", 0, APMU_CLK_GATE_CTRL, 26, 0, &pll1_lock},
	{PXA1908_CLK_PLL1_832_GATE, "pll1_832_gate", "pll1_832", 0, APMU_CLK_GATE_CTRL, 30, 0, &pll1_lock},
	{PXA1908_CLK_PLL1_1248_GATE, "pll1_1248_gate", "pll1_1248", 0, APMU_CLK_GATE_CTRL, 28, 0, &pll1_lock},
};

static DEFINE_SPINLOCK(sdh0_lock);
static DEFINE_SPINLOCK(sdh1_lock);
static DEFINE_SPINLOCK(sdh2_lock);

static const char * const sdh_parent_names[] = {"pll1_416", "pll1_624"};

static struct mmp_clk_mix_config sdh_mix_config = {
	.reg_info = DEFINE_MIX_REG_INFO(3, 8, 2, 6, 11),
};

static struct mmp_param_gate_clk apmu_gate_clks[] = {
	{PXA1908_CLK_USB, "usb_clk", NULL, 0, APMU_USB, 0x9, 0x9, 0x1, 0, NULL},
	{PXA1908_CLK_SDH0, "sdh0_clk", "sdh0_mix_clk", CLK_SET_RATE_PARENT | CLK_SET_RATE_UNGATE, APMU_SDH0, 0x12, 0x12, 0x0, 0, &sdh0_lock},
	{PXA1908_CLK_SDH1, "sdh1_clk", "sdh1_mix_clk", CLK_SET_RATE_PARENT | CLK_SET_RATE_UNGATE, APMU_SDH1, 0x12, 0x12, 0x0, 0, &sdh1_lock},
	{PXA1908_CLK_SDH2, "sdh2_clk", "sdh2_mix_clk", CLK_SET_RATE_PARENT | CLK_SET_RATE_UNGATE, APMU_SDH2, 0x12, 0x12, 0x0, 0, &sdh2_lock}
};

static void pxa1908_axi_periph_clk_init(struct pxa1908_clk_unit *pxa_unit)
{
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_general_gate_clks(unit, pll1_gate_clks,
			pxa_unit->base, ARRAY_SIZE(pll1_gate_clks));

	sdh_mix_config.reg_info.reg_clk_ctrl = pxa_unit->base + APMU_SDH0;
	mmp_clk_register_mix(NULL, "sdh0_mix_clk", sdh_parent_names,
			ARRAY_SIZE(sdh_parent_names), CLK_SET_RATE_PARENT,
			&sdh_mix_config, &sdh0_lock);
	sdh_mix_config.reg_info.reg_clk_ctrl = pxa_unit->base + APMU_SDH1;
	mmp_clk_register_mix(NULL, "sdh1_mix_clk", sdh_parent_names,
			ARRAY_SIZE(sdh_parent_names), CLK_SET_RATE_PARENT,
			&sdh_mix_config, &sdh1_lock);
	sdh_mix_config.reg_info.reg_clk_ctrl = pxa_unit->base + APMU_SDH2;
	mmp_clk_register_mix(NULL, "sdh2_mix_clk", sdh_parent_names,
			ARRAY_SIZE(sdh_parent_names), CLK_SET_RATE_PARENT,
			&sdh_mix_config, &sdh2_lock);

	mmp_register_gate_clks(unit, apmu_gate_clks, pxa_unit->base,
			ARRAY_SIZE(apmu_gate_clks));
}

static int pxa1908_apmu_probe(struct platform_device *pdev)
{
	struct pxa1908_clk_unit *pxa_unit;

	pxa_unit = devm_kzalloc(&pdev->dev, sizeof(*pxa_unit), GFP_KERNEL);
	if (IS_ERR(pxa_unit))
		return PTR_ERR(pxa_unit);

	pxa_unit->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pxa_unit->base))
		return PTR_ERR(pxa_unit->base);

	mmp_clk_init(pdev->dev.of_node, &pxa_unit->unit, APMU_NR_CLKS);

	pxa1908_axi_periph_clk_init(pxa_unit);

	return 0;
}

static const struct of_device_id pxa1908_apmu_match_table[] = {
	{ .compatible = "marvell,pxa1908-apmu" },
	{ }
};
MODULE_DEVICE_TABLE(of, pxa1908_apmu_match_table);

static struct platform_driver pxa1908_apmu_driver = {
	.probe = pxa1908_apmu_probe,
	.driver = {
		.name = "pxa1908-apmu",
		.of_match_table = pxa1908_apmu_match_table
	}
};
module_platform_driver(pxa1908_apmu_driver);

MODULE_AUTHOR("Duje Mihanović <duje.mihanovic@skole.hr>");
MODULE_DESCRIPTION("Marvell PXA1908 APMU Clock Driver");
MODULE_LICENSE("GPL");
