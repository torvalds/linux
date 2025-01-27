// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NPCM8xx Clock Generator
 * All the clocks are initialized by the bootloader, so this driver allows only
 * reading of current settings directly from the hardware.
 *
 * Copyright (C) 2020 Nuvoton Technologies
 * Author: Tomer Maimon <tomer.maimon@nuvoton.com>
 */

#define pr_fmt(fmt) "npcm8xx_clk: " fmt

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <dt-bindings/clock/nuvoton,npcm845-clk.h>
#include <soc/nuvoton/clock-npcm8xx.h>

/* npcm8xx clock registers*/
#define NPCM8XX_CLKSEL		0x04
#define NPCM8XX_CLKDIV1		0x08
#define NPCM8XX_CLKDIV2		0x2C
#define NPCM8XX_CLKDIV3		0x58
#define NPCM8XX_CLKDIV4		0x7C
#define NPCM8XX_PLLCON0		0x0C
#define NPCM8XX_PLLCON1		0x10
#define NPCM8XX_PLLCON2		0x54
#define NPCM8XX_PLLCONG		0x60
#define NPCM8XX_THRTL_CNT	0xC0

#define PLLCON_LOKI	BIT(31)
#define PLLCON_LOKS	BIT(30)
#define PLLCON_FBDV	GENMASK(27, 16)
#define PLLCON_OTDV2	GENMASK(15, 13)
#define PLLCON_PWDEN	BIT(12)
#define PLLCON_OTDV1	GENMASK(10, 8)
#define PLLCON_INDV	GENMASK(5, 0)

static void __iomem *clk_base;

struct npcm8xx_clk_pll {
	void __iomem	*pllcon;
	unsigned int	id;
	const char	*name;
	unsigned long	flags;
	struct clk_hw	hw;
};

#define to_npcm8xx_clk_pll(_hw) container_of(_hw, struct npcm8xx_clk_pll, hw)

struct npcm8xx_clk_pll_data {
	const char *name;
	struct clk_parent_data parent;
	unsigned int reg;
	unsigned long flags;
	struct clk_hw hw;
};

struct npcm8xx_clk_div_data {
	u32 reg;
	u8 shift;
	u8 width;
	const char *name;
	const struct clk_hw *parent_hw;
	unsigned long clk_divider_flags;
	unsigned long flags;
	int onecell_idx;
	struct clk_hw hw;
};

struct npcm8xx_clk_mux_data {
	u8 shift;
	u32 mask;
	const u32 *table;
	const char *name;
	const struct clk_parent_data *parent_data;
	u8 num_parents;
	unsigned long flags;
	struct clk_hw hw;
};

static struct clk_hw hw_pll1_div2, hw_pll2_div2, hw_gfx_div2, hw_pre_clk;
static struct npcm8xx_clk_pll_data npcm8xx_pll_clks[] = {
	{ "pll0", { .index = 0 }, NPCM8XX_PLLCON0, 0 },
	{ "pll1", { .index = 0 }, NPCM8XX_PLLCON1, 0 },
	{ "pll2", { .index = 0 }, NPCM8XX_PLLCON2, 0 },
	{ "pll_gfx", { .index = 0 }, NPCM8XX_PLLCONG, 0 },
};

static const u32 cpuck_mux_table[] = { 0, 1, 2, 7 };
static const struct clk_parent_data cpuck_mux_parents[] = {
	{ .hw = &npcm8xx_pll_clks[0].hw },
	{ .hw = &npcm8xx_pll_clks[1].hw },
	{ .index = 0 },
	{ .hw = &npcm8xx_pll_clks[2].hw }
};

static const u32 pixcksel_mux_table[] = { 0, 2 };
static const struct clk_parent_data pixcksel_mux_parents[] = {
	{ .hw = &npcm8xx_pll_clks[3].hw },
	{ .index = 0 }
};

static const u32 default_mux_table[] = { 0, 1, 2, 3 };
static const struct clk_parent_data default_mux_parents[] = {
	{ .hw = &npcm8xx_pll_clks[0].hw },
	{ .hw = &npcm8xx_pll_clks[1].hw },
	{ .index = 0 },
	{ .hw = &hw_pll2_div2 }
};

static const u32 sucksel_mux_table[] = { 2, 3 };
static const struct clk_parent_data sucksel_mux_parents[] = {
	{ .index = 0 },
	{ .hw = &hw_pll2_div2 }
};

static const u32 mccksel_mux_table[] = { 0, 2 };
static const struct clk_parent_data mccksel_mux_parents[] = {
	{ .hw = &hw_pll1_div2 },
	{ .index = 0 }
};

static const u32 clkoutsel_mux_table[] = { 0, 1, 2, 3, 4 };
static const struct clk_parent_data clkoutsel_mux_parents[] = {
	{ .hw = &npcm8xx_pll_clks[0].hw },
	{ .hw = &npcm8xx_pll_clks[1].hw },
	{ .index = 0 },
	{ .hw = &hw_gfx_div2 },
	{ .hw = &hw_pll2_div2 }
};

static const u32 gfxmsel_mux_table[] = { 2, 3 };
static const struct clk_parent_data gfxmsel_mux_parents[] = {
	{ .index = 0 },
	{ .hw = &npcm8xx_pll_clks[2].hw }
};

static const u32 dvcssel_mux_table[] = { 2, 3 };
static const struct clk_parent_data dvcssel_mux_parents[] = {
	{ .index = 0 },
	{ .hw = &npcm8xx_pll_clks[2].hw }
};

static const u32 default3_mux_table[] = { 0, 1, 2 };
static const struct clk_parent_data default3_mux_parents[] = {
	{ .hw = &npcm8xx_pll_clks[0].hw },
	{ .hw = &npcm8xx_pll_clks[1].hw },
	{ .index = 0 }
};

static struct npcm8xx_clk_mux_data npcm8xx_muxes[] = {
	{ 0, 3, cpuck_mux_table, "cpu_mux", cpuck_mux_parents,
		ARRAY_SIZE(cpuck_mux_parents), CLK_IS_CRITICAL },
	{ 4, 2, pixcksel_mux_table, "gfx_pixel_mux", pixcksel_mux_parents,
		ARRAY_SIZE(pixcksel_mux_parents), 0 },
	{ 6, 2, default_mux_table, "sd_mux", default_mux_parents,
		ARRAY_SIZE(default_mux_parents), 0 },
	{ 8, 2, default_mux_table, "uart_mux", default_mux_parents,
		ARRAY_SIZE(default_mux_parents), 0 },
	{ 10, 2, sucksel_mux_table, "serial_usb_mux", sucksel_mux_parents,
		ARRAY_SIZE(sucksel_mux_parents), 0 },
	{ 12, 2, mccksel_mux_table, "mc_mux", mccksel_mux_parents,
		ARRAY_SIZE(mccksel_mux_parents), 0 },
	{ 14, 2, default_mux_table, "adc_mux", default_mux_parents,
		ARRAY_SIZE(default_mux_parents), 0 },
	{ 16, 2, default_mux_table, "gfx_mux", default_mux_parents,
		ARRAY_SIZE(default_mux_parents), 0 },
	{ 18, 3, clkoutsel_mux_table, "clkout_mux", clkoutsel_mux_parents,
		ARRAY_SIZE(clkoutsel_mux_parents), 0 },
	{ 21, 2, gfxmsel_mux_table, "gfxm_mux", gfxmsel_mux_parents,
		ARRAY_SIZE(gfxmsel_mux_parents), 0 },
	{ 23, 2, dvcssel_mux_table, "dvc_mux", dvcssel_mux_parents,
		ARRAY_SIZE(dvcssel_mux_parents), 0 },
	{ 25, 2, default3_mux_table, "rg_mux", default3_mux_parents,
		ARRAY_SIZE(default3_mux_parents), 0 },
	{ 27, 2, default3_mux_table, "rcp_mux", default3_mux_parents,
		ARRAY_SIZE(default3_mux_parents), 0 },
};

/* configurable pre dividers: */
static struct npcm8xx_clk_div_data npcm8xx_pre_divs[] = {
	{ NPCM8XX_CLKDIV1, 21, 5, "pre_adc", &npcm8xx_muxes[6].hw, CLK_DIVIDER_READ_ONLY, 0, -1 },
	{ NPCM8XX_CLKDIV1, 26, 2, "ahb", &hw_pre_clk, CLK_DIVIDER_READ_ONLY, CLK_IS_CRITICAL, NPCM8XX_CLK_AHB },
};

/* configurable dividers: */
static struct npcm8xx_clk_div_data npcm8xx_divs[] = {
	{ NPCM8XX_CLKDIV1, 28, 3, "adc", &npcm8xx_pre_divs[0].hw, CLK_DIVIDER_READ_ONLY | CLK_DIVIDER_POWER_OF_TWO, 0, NPCM8XX_CLK_ADC },
	{ NPCM8XX_CLKDIV1, 16, 5, "uart", &npcm8xx_muxes[3].hw, 0, 0, NPCM8XX_CLK_UART },
	{ NPCM8XX_CLKDIV1, 11, 5, "mmc", &npcm8xx_muxes[2].hw, CLK_DIVIDER_READ_ONLY, 0, NPCM8XX_CLK_MMC },
	{ NPCM8XX_CLKDIV1, 6, 5, "spi3", &npcm8xx_pre_divs[1].hw, 0, 0, NPCM8XX_CLK_SPI3 },
	{ NPCM8XX_CLKDIV1, 2, 4, "pci", &npcm8xx_muxes[7].hw, CLK_DIVIDER_READ_ONLY, 0, NPCM8XX_CLK_PCI },

	{ NPCM8XX_CLKDIV2, 30, 2, "apb4", &npcm8xx_pre_divs[1].hw, CLK_DIVIDER_READ_ONLY | CLK_DIVIDER_POWER_OF_TWO, 0, NPCM8XX_CLK_APB4 },
	{ NPCM8XX_CLKDIV2, 28, 2, "apb3", &npcm8xx_pre_divs[1].hw, CLK_DIVIDER_READ_ONLY | CLK_DIVIDER_POWER_OF_TWO, 0, NPCM8XX_CLK_APB3 },
	{ NPCM8XX_CLKDIV2, 26, 2, "apb2", &npcm8xx_pre_divs[1].hw, CLK_DIVIDER_READ_ONLY | CLK_DIVIDER_POWER_OF_TWO, 0, NPCM8XX_CLK_APB2 },
	{ NPCM8XX_CLKDIV2, 24, 2, "apb1", &npcm8xx_pre_divs[1].hw, CLK_DIVIDER_READ_ONLY | CLK_DIVIDER_POWER_OF_TWO, 0, NPCM8XX_CLK_APB1 },
	{ NPCM8XX_CLKDIV2, 22, 2, "apb5", &npcm8xx_pre_divs[1].hw, CLK_DIVIDER_READ_ONLY | CLK_DIVIDER_POWER_OF_TWO, 0, NPCM8XX_CLK_APB5 },
	{ NPCM8XX_CLKDIV2, 16, 5, "clkout", &npcm8xx_muxes[8].hw, CLK_DIVIDER_READ_ONLY, 0, NPCM8XX_CLK_CLKOUT },
	{ NPCM8XX_CLKDIV2, 13, 3, "gfx", &npcm8xx_muxes[7].hw, CLK_DIVIDER_READ_ONLY, 0, NPCM8XX_CLK_GFX },
	{ NPCM8XX_CLKDIV2, 8, 5, "usb_bridge", &npcm8xx_muxes[4].hw, CLK_DIVIDER_READ_ONLY, 0, NPCM8XX_CLK_SU },
	{ NPCM8XX_CLKDIV2, 4, 4, "usb_host", &npcm8xx_muxes[4].hw, CLK_DIVIDER_READ_ONLY, 0, NPCM8XX_CLK_SU48 },
	{ NPCM8XX_CLKDIV2, 0, 4, "sdhc", &npcm8xx_muxes[2].hw, CLK_DIVIDER_READ_ONLY, 0, NPCM8XX_CLK_SDHC },

	{ NPCM8XX_CLKDIV3, 16, 8, "spi1", &npcm8xx_pre_divs[1].hw, CLK_DIVIDER_READ_ONLY, 0, NPCM8XX_CLK_SPI1 },
	{ NPCM8XX_CLKDIV3, 11, 5, "uart2", &npcm8xx_muxes[3].hw, CLK_DIVIDER_READ_ONLY, 0, NPCM8XX_CLK_UART2 },
	{ NPCM8XX_CLKDIV3, 6, 5, "spi0", &npcm8xx_pre_divs[1].hw, CLK_DIVIDER_READ_ONLY, 0, NPCM8XX_CLK_SPI0 },
	{ NPCM8XX_CLKDIV3, 1, 5, "spix", &npcm8xx_pre_divs[1].hw, CLK_DIVIDER_READ_ONLY, 0, NPCM8XX_CLK_SPIX },

	{ NPCM8XX_CLKDIV4, 28, 4, "rg", &npcm8xx_muxes[11].hw, CLK_DIVIDER_READ_ONLY, 0, NPCM8XX_CLK_RG },
	{ NPCM8XX_CLKDIV4, 12, 4, "rcp", &npcm8xx_muxes[12].hw, CLK_DIVIDER_READ_ONLY, 0, NPCM8XX_CLK_RCP },

	{ NPCM8XX_THRTL_CNT, 0, 2, "th", &npcm8xx_muxes[0].hw, CLK_DIVIDER_READ_ONLY | CLK_DIVIDER_POWER_OF_TWO, 0, NPCM8XX_CLK_TH },
};

static unsigned long npcm8xx_clk_pll_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct npcm8xx_clk_pll *pll = to_npcm8xx_clk_pll(hw);
	unsigned long fbdv, indv, otdv1, otdv2;
	unsigned int val;
	u64 ret;

	if (parent_rate == 0) {
		pr_debug("%s: parent rate is zero\n", __func__);
		return 0;
	}

	val = readl_relaxed(pll->pllcon);

	indv = FIELD_GET(PLLCON_INDV, val);
	fbdv = FIELD_GET(PLLCON_FBDV, val);
	otdv1 = FIELD_GET(PLLCON_OTDV1, val);
	otdv2 = FIELD_GET(PLLCON_OTDV2, val);

	ret = (u64)parent_rate * fbdv;
	do_div(ret, indv * otdv1 * otdv2);

	return ret;
}

static const struct clk_ops npcm8xx_clk_pll_ops = {
	.recalc_rate = npcm8xx_clk_pll_recalc_rate,
};

static struct clk_hw *
npcm8xx_clk_register_pll(struct device *dev, void __iomem *pllcon,
			 const char *name, const struct clk_parent_data *parent,
			 unsigned long flags)
{
	struct npcm8xx_clk_pll *pll;
	struct clk_init_data init = {};
	int ret;

	pll = devm_kzalloc(dev, sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &npcm8xx_clk_pll_ops;
	init.parent_data = parent;
	init.num_parents = 1;
	init.flags = flags;

	pll->pllcon = pllcon;
	pll->hw.init = &init;

	ret = devm_clk_hw_register(dev, &pll->hw);
	if (ret)
		return ERR_PTR(ret);

	return &pll->hw;
}

static DEFINE_SPINLOCK(npcm8xx_clk_lock);

static int npcm8xx_clk_probe(struct auxiliary_device *adev,
			     const struct auxiliary_device_id *id)
{
	struct npcm_clock_adev *rdev = to_npcm_clock_adev(adev);
	struct clk_hw_onecell_data *npcm8xx_clk_data;
	struct device *dev = &adev->dev;
	struct clk_hw *hw;
	unsigned int i;

	npcm8xx_clk_data = devm_kzalloc(dev, struct_size(npcm8xx_clk_data, hws,
							 NPCM8XX_NUM_CLOCKS),
					GFP_KERNEL);
	if (!npcm8xx_clk_data)
		return -ENOMEM;

	clk_base = rdev->base;

	npcm8xx_clk_data->num = NPCM8XX_NUM_CLOCKS;

	for (i = 0; i < NPCM8XX_NUM_CLOCKS; i++)
		npcm8xx_clk_data->hws[i] = ERR_PTR(-EPROBE_DEFER);

	/* Register plls */
	for (i = 0; i < ARRAY_SIZE(npcm8xx_pll_clks); i++) {
		struct npcm8xx_clk_pll_data *pll_clk = &npcm8xx_pll_clks[i];

		hw = npcm8xx_clk_register_pll(dev, clk_base + pll_clk->reg,
					      pll_clk->name, &pll_clk->parent,
					      pll_clk->flags);
		if (IS_ERR(hw))
			return dev_err_probe(dev, PTR_ERR(hw), "Can't register pll\n");
		pll_clk->hw = *hw;
	}

	/* Register fixed dividers */
	hw = devm_clk_hw_register_fixed_factor(dev, "pll1_div2", "pll1", 0, 1, 2);
	if (IS_ERR(hw))
		return dev_err_probe(dev, PTR_ERR(hw), "Can't register fixed div\n");
	hw_pll1_div2 = *hw;

	hw = devm_clk_hw_register_fixed_factor(dev, "pll2_div2", "pll2", 0, 1, 2);
	if (IS_ERR(hw))
		return dev_err_probe(dev, PTR_ERR(hw), "Can't register pll2 div2\n");
	hw_pll2_div2 = *hw;

	hw = devm_clk_hw_register_fixed_factor(dev, "pll_gfx_div2", "pll_gfx", 0, 1, 2);
	if (IS_ERR(hw))
		return dev_err_probe(dev, PTR_ERR(hw), "Can't register gfx div2\n");
	hw_gfx_div2 = *hw;

	/* Register muxes */
	for (i = 0; i < ARRAY_SIZE(npcm8xx_muxes); i++) {
		struct npcm8xx_clk_mux_data *mux_data = &npcm8xx_muxes[i];

		hw = devm_clk_hw_register_mux_parent_data_table(dev,
								mux_data->name,
								mux_data->parent_data,
								mux_data->num_parents,
								mux_data->flags,
								clk_base + NPCM8XX_CLKSEL,
								mux_data->shift,
								mux_data->mask,
								0,
								mux_data->table,
								&npcm8xx_clk_lock);
		if (IS_ERR(hw))
			return dev_err_probe(dev, PTR_ERR(hw), "Can't register mux\n");
		mux_data->hw = *hw;
	}

	hw = devm_clk_hw_register_fixed_factor(dev, "pre_clk", "cpu_mux", 0, 1, 2);
	if (IS_ERR(hw))
		return dev_err_probe(dev, PTR_ERR(hw), "Can't register pre clk div2\n");
	hw_pre_clk = *hw;

	hw = devm_clk_hw_register_fixed_factor(dev, "axi", "th", 0, 1, 2);
	if (IS_ERR(hw))
		return dev_err_probe(dev, PTR_ERR(hw), "Can't register axi div2\n");
	npcm8xx_clk_data->hws[NPCM8XX_CLK_AXI] = hw;

	hw = devm_clk_hw_register_fixed_factor(dev, "atb", "axi", 0, 1, 2);
	if (IS_ERR(hw))
		return dev_err_probe(dev, PTR_ERR(hw), "Can't register atb div2\n");
	npcm8xx_clk_data->hws[NPCM8XX_CLK_ATB] = hw;

	/* Register pre dividers */
	for (i = 0; i < ARRAY_SIZE(npcm8xx_pre_divs); i++) {
		struct npcm8xx_clk_div_data *div_data = &npcm8xx_pre_divs[i];

		hw = devm_clk_hw_register_divider_parent_hw(dev, div_data->name,
							    div_data->parent_hw,
							    div_data->flags,
							    clk_base + div_data->reg,
							    div_data->shift,
							    div_data->width,
							    div_data->clk_divider_flags,
							    &npcm8xx_clk_lock);
		if (IS_ERR(hw))
			return dev_err_probe(dev, PTR_ERR(hw), "Can't register pre div\n");
		div_data->hw = *hw;

		if (div_data->onecell_idx >= 0)
			npcm8xx_clk_data->hws[div_data->onecell_idx] = hw;
	}

	/* Register dividers */
	for (i = 0; i < ARRAY_SIZE(npcm8xx_divs); i++) {
		struct npcm8xx_clk_div_data *div_data = &npcm8xx_divs[i];

		hw = devm_clk_hw_register_divider_parent_hw(dev, div_data->name,
							    div_data->parent_hw,
							    div_data->flags,
							    clk_base + div_data->reg,
							    div_data->shift,
							    div_data->width,
							    div_data->clk_divider_flags,
							    &npcm8xx_clk_lock);
		if (IS_ERR(hw))
			return dev_err_probe(dev, PTR_ERR(hw), "Can't register div\n");

		if (div_data->onecell_idx >= 0)
			npcm8xx_clk_data->hws[div_data->onecell_idx] = hw;
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					  npcm8xx_clk_data);
}

static const struct auxiliary_device_id npcm8xx_clock_ids[] = {
	{
		.name = "reset_npcm.clk-npcm8xx",
	},
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, npcm8xx_clock_ids);

static struct auxiliary_driver npcm8xx_clock_driver = {
	.probe		= npcm8xx_clk_probe,
	.id_table	= npcm8xx_clock_ids,
};
module_auxiliary_driver(npcm8xx_clock_driver);

MODULE_DESCRIPTION("Clock driver for Nuvoton NPCM8XX BMC SoC");
MODULE_AUTHOR("Tomer Maimon <tomer.maimon@nuvoton.com>");
MODULE_LICENSE("GPL v2");

