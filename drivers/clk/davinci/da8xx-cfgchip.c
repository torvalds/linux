// SPDX-License-Identifier: GPL-2.0
/*
 * Clock driver for DA8xx/AM17xx/AM18xx/OMAP-L13x CFGCHIP
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/init.h>
#include <linux/mfd/da8xx-cfgchip.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_data/clk-da8xx-cfgchip.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* --- Gate clocks --- */

#define DA8XX_GATE_CLOCK_IS_DIV4P5	BIT(1)

struct da8xx_cfgchip_gate_clk_info {
	const char *name;
	u32 cfgchip;
	u32 bit;
	u32 flags;
};

struct da8xx_cfgchip_gate_clk {
	struct clk_hw hw;
	struct regmap *regmap;
	u32 reg;
	u32 mask;
};

#define to_da8xx_cfgchip_gate_clk(_hw) \
	container_of((_hw), struct da8xx_cfgchip_gate_clk, hw)

static int da8xx_cfgchip_gate_clk_enable(struct clk_hw *hw)
{
	struct da8xx_cfgchip_gate_clk *clk = to_da8xx_cfgchip_gate_clk(hw);

	return regmap_write_bits(clk->regmap, clk->reg, clk->mask, clk->mask);
}

static void da8xx_cfgchip_gate_clk_disable(struct clk_hw *hw)
{
	struct da8xx_cfgchip_gate_clk *clk = to_da8xx_cfgchip_gate_clk(hw);

	regmap_write_bits(clk->regmap, clk->reg, clk->mask, 0);
}

static int da8xx_cfgchip_gate_clk_is_enabled(struct clk_hw *hw)
{
	struct da8xx_cfgchip_gate_clk *clk = to_da8xx_cfgchip_gate_clk(hw);
	unsigned int val;

	regmap_read(clk->regmap, clk->reg, &val);

	return !!(val & clk->mask);
}

static unsigned long da8xx_cfgchip_div4p5_recalc_rate(struct clk_hw *hw,
						      unsigned long parent_rate)
{
	/* this clock divides by 4.5 */
	return parent_rate * 2 / 9;
}

static const struct clk_ops da8xx_cfgchip_gate_clk_ops = {
	.enable		= da8xx_cfgchip_gate_clk_enable,
	.disable	= da8xx_cfgchip_gate_clk_disable,
	.is_enabled	= da8xx_cfgchip_gate_clk_is_enabled,
};

static const struct clk_ops da8xx_cfgchip_div4p5_clk_ops = {
	.enable		= da8xx_cfgchip_gate_clk_enable,
	.disable	= da8xx_cfgchip_gate_clk_disable,
	.is_enabled	= da8xx_cfgchip_gate_clk_is_enabled,
	.recalc_rate	= da8xx_cfgchip_div4p5_recalc_rate,
};

static struct da8xx_cfgchip_gate_clk * __init
da8xx_cfgchip_gate_clk_register(struct device *dev,
				const struct da8xx_cfgchip_gate_clk_info *info,
				struct regmap *regmap)
{
	struct clk *parent;
	const char *parent_name;
	struct da8xx_cfgchip_gate_clk *gate;
	struct clk_init_data init;
	int ret;

	parent = devm_clk_get(dev, NULL);
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	parent_name = __clk_get_name(parent);

	gate = devm_kzalloc(dev, sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = info->name;
	if (info->flags & DA8XX_GATE_CLOCK_IS_DIV4P5)
		init.ops = &da8xx_cfgchip_div4p5_clk_ops;
	else
		init.ops = &da8xx_cfgchip_gate_clk_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = 0;

	gate->hw.init = &init;
	gate->regmap = regmap;
	gate->reg = info->cfgchip;
	gate->mask = info->bit;

	ret = devm_clk_hw_register(dev, &gate->hw);
	if (ret < 0)
		return ERR_PTR(ret);

	return gate;
}

static const struct da8xx_cfgchip_gate_clk_info da8xx_tbclksync_info __initconst = {
	.name = "ehrpwm_tbclk",
	.cfgchip = CFGCHIP(1),
	.bit = CFGCHIP1_TBCLKSYNC,
};

static int __init da8xx_cfgchip_register_tbclk(struct device *dev,
					       struct regmap *regmap)
{
	struct da8xx_cfgchip_gate_clk *gate;

	gate = da8xx_cfgchip_gate_clk_register(dev, &da8xx_tbclksync_info,
					       regmap);
	if (IS_ERR(gate))
		return PTR_ERR(gate);

	clk_hw_register_clkdev(&gate->hw, "tbclk", "ehrpwm.0");
	clk_hw_register_clkdev(&gate->hw, "tbclk", "ehrpwm.1");

	return 0;
}

static const struct da8xx_cfgchip_gate_clk_info da8xx_div4p5ena_info __initconst = {
	.name = "div4.5",
	.cfgchip = CFGCHIP(3),
	.bit = CFGCHIP3_DIV45PENA,
	.flags = DA8XX_GATE_CLOCK_IS_DIV4P5,
};

static int __init da8xx_cfgchip_register_div4p5(struct device *dev,
						struct regmap *regmap)
{
	struct da8xx_cfgchip_gate_clk *gate;

	gate = da8xx_cfgchip_gate_clk_register(dev, &da8xx_div4p5ena_info, regmap);

	return PTR_ERR_OR_ZERO(gate);
}

static int __init
of_da8xx_cfgchip_gate_clk_init(struct device *dev,
			       const struct da8xx_cfgchip_gate_clk_info *info,
			       struct regmap *regmap)
{
	struct da8xx_cfgchip_gate_clk *gate;

	gate = da8xx_cfgchip_gate_clk_register(dev, info, regmap);
	if (IS_ERR(gate))
		return PTR_ERR(gate);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, gate);
}

static int __init of_da8xx_tbclksync_init(struct device *dev,
					  struct regmap *regmap)
{
	return of_da8xx_cfgchip_gate_clk_init(dev, &da8xx_tbclksync_info, regmap);
}

static int __init of_da8xx_div4p5ena_init(struct device *dev,
					  struct regmap *regmap)
{
	return of_da8xx_cfgchip_gate_clk_init(dev, &da8xx_div4p5ena_info, regmap);
}

/* --- MUX clocks --- */

struct da8xx_cfgchip_mux_clk_info {
	const char *name;
	const char *parent0;
	const char *parent1;
	u32 cfgchip;
	u32 bit;
};

struct da8xx_cfgchip_mux_clk {
	struct clk_hw hw;
	struct regmap *regmap;
	u32 reg;
	u32 mask;
};

#define to_da8xx_cfgchip_mux_clk(_hw) \
	container_of((_hw), struct da8xx_cfgchip_mux_clk, hw)

static int da8xx_cfgchip_mux_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct da8xx_cfgchip_mux_clk *clk = to_da8xx_cfgchip_mux_clk(hw);
	unsigned int val = index ? clk->mask : 0;

	return regmap_write_bits(clk->regmap, clk->reg, clk->mask, val);
}

static u8 da8xx_cfgchip_mux_clk_get_parent(struct clk_hw *hw)
{
	struct da8xx_cfgchip_mux_clk *clk = to_da8xx_cfgchip_mux_clk(hw);
	unsigned int val;

	regmap_read(clk->regmap, clk->reg, &val);

	return (val & clk->mask) ? 1 : 0;
}

static const struct clk_ops da8xx_cfgchip_mux_clk_ops = {
	.determine_rate	= clk_hw_determine_rate_no_reparent,
	.set_parent	= da8xx_cfgchip_mux_clk_set_parent,
	.get_parent	= da8xx_cfgchip_mux_clk_get_parent,
};

static struct da8xx_cfgchip_mux_clk * __init
da8xx_cfgchip_mux_clk_register(struct device *dev,
			       const struct da8xx_cfgchip_mux_clk_info *info,
			       struct regmap *regmap)
{
	const char * const parent_names[] = { info->parent0, info->parent1 };
	struct da8xx_cfgchip_mux_clk *mux;
	struct clk_init_data init;
	int ret;

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	init.name = info->name;
	init.ops = &da8xx_cfgchip_mux_clk_ops;
	init.parent_names = parent_names;
	init.num_parents = 2;
	init.flags = 0;

	mux->hw.init = &init;
	mux->regmap = regmap;
	mux->reg = info->cfgchip;
	mux->mask = info->bit;

	ret = devm_clk_hw_register(dev, &mux->hw);
	if (ret < 0)
		return ERR_PTR(ret);

	return mux;
}

static const struct da8xx_cfgchip_mux_clk_info da850_async1_info __initconst = {
	.name = "async1",
	.parent0 = "pll0_sysclk3",
	.parent1 = "div4.5",
	.cfgchip = CFGCHIP(3),
	.bit = CFGCHIP3_EMA_CLKSRC,
};

static int __init da8xx_cfgchip_register_async1(struct device *dev,
						struct regmap *regmap)
{
	struct da8xx_cfgchip_mux_clk *mux;

	mux = da8xx_cfgchip_mux_clk_register(dev, &da850_async1_info, regmap);
	if (IS_ERR(mux))
		return PTR_ERR(mux);

	clk_hw_register_clkdev(&mux->hw, "async1", "da850-psc0");

	return 0;
}

static const struct da8xx_cfgchip_mux_clk_info da850_async3_info __initconst = {
	.name = "async3",
	.parent0 = "pll0_sysclk2",
	.parent1 = "pll1_sysclk2",
	.cfgchip = CFGCHIP(3),
	.bit = CFGCHIP3_ASYNC3_CLKSRC,
};

static int __init da850_cfgchip_register_async3(struct device *dev,
						struct regmap *regmap)
{
	struct da8xx_cfgchip_mux_clk *mux;
	struct clk_hw *parent;

	mux = da8xx_cfgchip_mux_clk_register(dev, &da850_async3_info, regmap);
	if (IS_ERR(mux))
		return PTR_ERR(mux);

	clk_hw_register_clkdev(&mux->hw, "async3", "da850-psc1");

	/* pll1_sysclk2 is not affected by CPU scaling, so use it for async3 */
	parent = clk_hw_get_parent_by_index(&mux->hw, 1);
	if (parent)
		clk_set_parent(mux->hw.clk, parent->clk);
	else
		dev_warn(dev, "Failed to find async3 parent clock\n");

	return 0;
}

static int __init
of_da8xx_cfgchip_init_mux_clock(struct device *dev,
				const struct da8xx_cfgchip_mux_clk_info *info,
				struct regmap *regmap)
{
	struct da8xx_cfgchip_mux_clk *mux;

	mux = da8xx_cfgchip_mux_clk_register(dev, info, regmap);
	if (IS_ERR(mux))
		return PTR_ERR(mux);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, &mux->hw);
}

static int __init of_da850_async1_init(struct device *dev, struct regmap *regmap)
{
	return of_da8xx_cfgchip_init_mux_clock(dev, &da850_async1_info, regmap);
}

static int __init of_da850_async3_init(struct device *dev, struct regmap *regmap)
{
	return of_da8xx_cfgchip_init_mux_clock(dev, &da850_async3_info, regmap);
}

/* --- USB 2.0 PHY clock --- */

struct da8xx_usb0_clk48 {
	struct clk_hw hw;
	struct clk *fck;
	struct regmap *regmap;
};

#define to_da8xx_usb0_clk48(_hw) \
	container_of((_hw), struct da8xx_usb0_clk48, hw)

static int da8xx_usb0_clk48_prepare(struct clk_hw *hw)
{
	struct da8xx_usb0_clk48 *usb0 = to_da8xx_usb0_clk48(hw);

	/* The USB 2.0 PSC clock is only needed temporarily during the USB 2.0
	 * PHY clock enable, but since clk_prepare() can't be called in an
	 * atomic context (i.e. in clk_enable()), we have to prepare it here.
	 */
	return clk_prepare(usb0->fck);
}

static void da8xx_usb0_clk48_unprepare(struct clk_hw *hw)
{
	struct da8xx_usb0_clk48 *usb0 = to_da8xx_usb0_clk48(hw);

	clk_unprepare(usb0->fck);
}

static int da8xx_usb0_clk48_enable(struct clk_hw *hw)
{
	struct da8xx_usb0_clk48 *usb0 = to_da8xx_usb0_clk48(hw);
	unsigned int mask, val;
	int ret;

	/* Locking the USB 2.O PLL requires that the USB 2.O PSC is enabled
	 * temporaily. It can be turned back off once the PLL is locked.
	 */
	clk_enable(usb0->fck);

	/* Turn on the USB 2.0 PHY, but just the PLL, and not OTG. The USB 1.1
	 * PHY may use the USB 2.0 PLL clock without USB 2.0 OTG being used.
	 */
	mask = CFGCHIP2_RESET | CFGCHIP2_PHYPWRDN | CFGCHIP2_PHY_PLLON;
	val = CFGCHIP2_PHY_PLLON;

	regmap_write_bits(usb0->regmap, CFGCHIP(2), mask, val);
	ret = regmap_read_poll_timeout(usb0->regmap, CFGCHIP(2), val,
				       val & CFGCHIP2_PHYCLKGD, 0, 500000);

	clk_disable(usb0->fck);

	return ret;
}

static void da8xx_usb0_clk48_disable(struct clk_hw *hw)
{
	struct da8xx_usb0_clk48 *usb0 = to_da8xx_usb0_clk48(hw);
	unsigned int val;

	val = CFGCHIP2_PHYPWRDN;
	regmap_write_bits(usb0->regmap, CFGCHIP(2), val, val);
}

static int da8xx_usb0_clk48_is_enabled(struct clk_hw *hw)
{
	struct da8xx_usb0_clk48 *usb0 = to_da8xx_usb0_clk48(hw);
	unsigned int val;

	regmap_read(usb0->regmap, CFGCHIP(2), &val);

	return !!(val & CFGCHIP2_PHYCLKGD);
}

static unsigned long da8xx_usb0_clk48_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct da8xx_usb0_clk48 *usb0 = to_da8xx_usb0_clk48(hw);
	unsigned int mask, val;

	/* The parent clock rate must be one of the following */
	mask = CFGCHIP2_REFFREQ_MASK;
	switch (parent_rate) {
	case 12000000:
		val = CFGCHIP2_REFFREQ_12MHZ;
		break;
	case 13000000:
		val = CFGCHIP2_REFFREQ_13MHZ;
		break;
	case 19200000:
		val = CFGCHIP2_REFFREQ_19_2MHZ;
		break;
	case 20000000:
		val = CFGCHIP2_REFFREQ_20MHZ;
		break;
	case 24000000:
		val = CFGCHIP2_REFFREQ_24MHZ;
		break;
	case 26000000:
		val = CFGCHIP2_REFFREQ_26MHZ;
		break;
	case 38400000:
		val = CFGCHIP2_REFFREQ_38_4MHZ;
		break;
	case 40000000:
		val = CFGCHIP2_REFFREQ_40MHZ;
		break;
	case 48000000:
		val = CFGCHIP2_REFFREQ_48MHZ;
		break;
	default:
		return 0;
	}

	regmap_write_bits(usb0->regmap, CFGCHIP(2), mask, val);

	/* USB 2.0 PLL always supplies 48MHz */
	return 48000000;
}

static int da8xx_usb0_clk48_determine_rate(struct clk_hw *hw,
					   struct clk_rate_request *req)
{
	req->rate = 48000000;

	return 0;
}

static int da8xx_usb0_clk48_set_parent(struct clk_hw *hw, u8 index)
{
	struct da8xx_usb0_clk48 *usb0 = to_da8xx_usb0_clk48(hw);

	return regmap_write_bits(usb0->regmap, CFGCHIP(2),
				 CFGCHIP2_USB2PHYCLKMUX,
				 index ? CFGCHIP2_USB2PHYCLKMUX : 0);
}

static u8 da8xx_usb0_clk48_get_parent(struct clk_hw *hw)
{
	struct da8xx_usb0_clk48 *usb0 = to_da8xx_usb0_clk48(hw);
	unsigned int val;

	regmap_read(usb0->regmap, CFGCHIP(2), &val);

	return (val & CFGCHIP2_USB2PHYCLKMUX) ? 1 : 0;
}

static const struct clk_ops da8xx_usb0_clk48_ops = {
	.prepare	= da8xx_usb0_clk48_prepare,
	.unprepare	= da8xx_usb0_clk48_unprepare,
	.enable		= da8xx_usb0_clk48_enable,
	.disable	= da8xx_usb0_clk48_disable,
	.is_enabled	= da8xx_usb0_clk48_is_enabled,
	.recalc_rate	= da8xx_usb0_clk48_recalc_rate,
	.determine_rate	= da8xx_usb0_clk48_determine_rate,
	.set_parent	= da8xx_usb0_clk48_set_parent,
	.get_parent	= da8xx_usb0_clk48_get_parent,
};

static struct da8xx_usb0_clk48 *
da8xx_cfgchip_register_usb0_clk48(struct device *dev,
				  struct regmap *regmap)
{
	const char * const parent_names[] = { "usb_refclkin", "pll0_auxclk" };
	struct clk *fck_clk;
	struct da8xx_usb0_clk48 *usb0;
	struct clk_init_data init = {};
	int ret;

	fck_clk = devm_clk_get(dev, "fck");
	if (IS_ERR(fck_clk)) {
		dev_err_probe(dev, PTR_ERR(fck_clk), "Missing fck clock\n");
		return ERR_CAST(fck_clk);
	}

	usb0 = devm_kzalloc(dev, sizeof(*usb0), GFP_KERNEL);
	if (!usb0)
		return ERR_PTR(-ENOMEM);

	init.name = "usb0_clk48";
	init.ops = &da8xx_usb0_clk48_ops;
	init.parent_names = parent_names;
	init.num_parents = 2;

	usb0->hw.init = &init;
	usb0->fck = fck_clk;
	usb0->regmap = regmap;

	ret = devm_clk_hw_register(dev, &usb0->hw);
	if (ret < 0)
		return ERR_PTR(ret);

	return usb0;
}

/* --- USB 1.1 PHY clock --- */

struct da8xx_usb1_clk48 {
	struct clk_hw hw;
	struct regmap *regmap;
};

#define to_da8xx_usb1_clk48(_hw) \
	container_of((_hw), struct da8xx_usb1_clk48, hw)

static int da8xx_usb1_clk48_set_parent(struct clk_hw *hw, u8 index)
{
	struct da8xx_usb1_clk48 *usb1 = to_da8xx_usb1_clk48(hw);

	return regmap_write_bits(usb1->regmap, CFGCHIP(2),
				 CFGCHIP2_USB1PHYCLKMUX,
				 index ? CFGCHIP2_USB1PHYCLKMUX : 0);
}

static u8 da8xx_usb1_clk48_get_parent(struct clk_hw *hw)
{
	struct da8xx_usb1_clk48 *usb1 = to_da8xx_usb1_clk48(hw);
	unsigned int val;

	regmap_read(usb1->regmap, CFGCHIP(2), &val);

	return (val & CFGCHIP2_USB1PHYCLKMUX) ? 1 : 0;
}

static const struct clk_ops da8xx_usb1_clk48_ops = {
	.determine_rate	= clk_hw_determine_rate_no_reparent,
	.set_parent	= da8xx_usb1_clk48_set_parent,
	.get_parent	= da8xx_usb1_clk48_get_parent,
};

/**
 * da8xx_cfgchip_register_usb1_clk48 - Register a new USB 1.1 PHY clock
 * @dev: The device
 * @regmap: The CFGCHIP regmap
 */
static struct da8xx_usb1_clk48 *
da8xx_cfgchip_register_usb1_clk48(struct device *dev,
				  struct regmap *regmap)
{
	const char * const parent_names[] = { "usb0_clk48", "usb_refclkin" };
	struct da8xx_usb1_clk48 *usb1;
	struct clk_init_data init = {};
	int ret;

	usb1 = devm_kzalloc(dev, sizeof(*usb1), GFP_KERNEL);
	if (!usb1)
		return ERR_PTR(-ENOMEM);

	init.name = "usb1_clk48";
	init.ops = &da8xx_usb1_clk48_ops;
	init.parent_names = parent_names;
	init.num_parents = 2;

	usb1->hw.init = &init;
	usb1->regmap = regmap;

	ret = devm_clk_hw_register(dev, &usb1->hw);
	if (ret < 0)
		return ERR_PTR(ret);

	return usb1;
}

static int da8xx_cfgchip_register_usb_phy_clk(struct device *dev,
					      struct regmap *regmap)
{
	struct da8xx_usb0_clk48 *usb0;
	struct da8xx_usb1_clk48 *usb1;
	struct clk_hw *parent;

	usb0 = da8xx_cfgchip_register_usb0_clk48(dev, regmap);
	if (IS_ERR(usb0))
		return PTR_ERR(usb0);

	/*
	 * All existing boards use pll0_auxclk as the parent and new boards
	 * should use device tree, so hard-coding the value (1) here.
	 */
	parent = clk_hw_get_parent_by_index(&usb0->hw, 1);
	if (parent)
		clk_set_parent(usb0->hw.clk, parent->clk);
	else
		dev_warn(dev, "Failed to find usb0 parent clock\n");

	usb1 = da8xx_cfgchip_register_usb1_clk48(dev, regmap);
	if (IS_ERR(usb1))
		return PTR_ERR(usb1);

	/*
	 * All existing boards use usb0_clk48 as the parent and new boards
	 * should use device tree, so hard-coding the value (0) here.
	 */
	parent = clk_hw_get_parent_by_index(&usb1->hw, 0);
	if (parent)
		clk_set_parent(usb1->hw.clk, parent->clk);
	else
		dev_warn(dev, "Failed to find usb1 parent clock\n");

	clk_hw_register_clkdev(&usb0->hw, "usb0_clk48", "da8xx-usb-phy");
	clk_hw_register_clkdev(&usb1->hw, "usb1_clk48", "da8xx-usb-phy");

	return 0;
}

static int of_da8xx_usb_phy_clk_init(struct device *dev, struct regmap *regmap)
{
	struct clk_hw_onecell_data *clk_data;
	struct da8xx_usb0_clk48 *usb0;
	struct da8xx_usb1_clk48 *usb1;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, 2),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = 2;

	usb0 = da8xx_cfgchip_register_usb0_clk48(dev, regmap);
	if (IS_ERR(usb0)) {
		if (PTR_ERR(usb0) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		dev_warn(dev, "Failed to register usb0_clk48 (%ld)\n",
			 PTR_ERR(usb0));

		clk_data->hws[0] = ERR_PTR(-ENOENT);
	} else {
		clk_data->hws[0] = &usb0->hw;
	}

	usb1 = da8xx_cfgchip_register_usb1_clk48(dev, regmap);
	if (IS_ERR(usb1)) {
		if (PTR_ERR(usb1) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		dev_warn(dev, "Failed to register usb1_clk48 (%ld)\n",
			 PTR_ERR(usb1));

		clk_data->hws[1] = ERR_PTR(-ENOENT);
	} else {
		clk_data->hws[1] = &usb1->hw;
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
}

/* --- platform device --- */

static const struct of_device_id da8xx_cfgchip_of_match[] = {
	{
		.compatible = "ti,da830-tbclksync",
		.data = of_da8xx_tbclksync_init,
	},
	{
		.compatible = "ti,da830-div4p5ena",
		.data = of_da8xx_div4p5ena_init,
	},
	{
		.compatible = "ti,da850-async1-clksrc",
		.data = of_da850_async1_init,
	},
	{
		.compatible = "ti,da850-async3-clksrc",
		.data = of_da850_async3_init,
	},
	{
		.compatible = "ti,da830-usb-phy-clocks",
		.data = of_da8xx_usb_phy_clk_init,
	},
	{ }
};

static const struct platform_device_id da8xx_cfgchip_id_table[] = {
	{
		.name = "da830-tbclksync",
		.driver_data = (kernel_ulong_t)da8xx_cfgchip_register_tbclk,
	},
	{
		.name = "da830-div4p5ena",
		.driver_data = (kernel_ulong_t)da8xx_cfgchip_register_div4p5,
	},
	{
		.name = "da850-async1-clksrc",
		.driver_data = (kernel_ulong_t)da8xx_cfgchip_register_async1,
	},
	{
		.name = "da850-async3-clksrc",
		.driver_data = (kernel_ulong_t)da850_cfgchip_register_async3,
	},
	{
		.name = "da830-usb-phy-clks",
		.driver_data = (kernel_ulong_t)da8xx_cfgchip_register_usb_phy_clk,
	},
	{ }
};

typedef int (*da8xx_cfgchip_init)(struct device *dev, struct regmap *regmap);

static int da8xx_cfgchip_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct da8xx_cfgchip_clk_platform_data *pdata = dev->platform_data;
	da8xx_cfgchip_init clk_init = NULL;
	struct regmap *regmap = NULL;

	clk_init = device_get_match_data(dev);
	if (clk_init) {
		struct device_node *parent;

		parent = of_get_parent(dev->of_node);
		regmap = syscon_node_to_regmap(parent);
		of_node_put(parent);
	} else if (pdev->id_entry && pdata) {
		clk_init = (void *)pdev->id_entry->driver_data;
		regmap = pdata->cfgchip;
	}

	if (!clk_init) {
		dev_err(dev, "unable to find driver data\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(regmap)) {
		dev_err(dev, "no regmap for CFGCHIP syscon\n");
		return regmap ? PTR_ERR(regmap) : -ENOENT;
	}

	return clk_init(dev, regmap);
}

static struct platform_driver da8xx_cfgchip_driver = {
	.probe		= da8xx_cfgchip_probe,
	.driver		= {
		.name		= "da8xx-cfgchip-clk",
		.of_match_table	= da8xx_cfgchip_of_match,
	},
	.id_table	= da8xx_cfgchip_id_table,
};

static int __init da8xx_cfgchip_driver_init(void)
{
	return platform_driver_register(&da8xx_cfgchip_driver);
}

/* has to be postcore_initcall because PSC devices depend on the async3 clock */
postcore_initcall(da8xx_cfgchip_driver_init);
