/*
 * Cortina Gemini SoC Clock Controller driver
 * Copyright (c) 2017 Linus Walleij <linus.walleij@linaro.org>
 */

#define pr_fmt(fmt) "clk-gemini: " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/reset-controller.h>
#include <dt-bindings/reset/cortina,gemini-reset.h>
#include <dt-bindings/clock/cortina,gemini-clock.h>

/* Globally visible clocks */
static DEFINE_SPINLOCK(gemini_clk_lock);

#define GEMINI_GLOBAL_STATUS		0x04
#define PLL_OSC_SEL			BIT(30)
#define AHBSPEED_SHIFT			(15)
#define AHBSPEED_MASK			0x07
#define CPU_AHB_RATIO_SHIFT		(18)
#define CPU_AHB_RATIO_MASK		0x03

#define GEMINI_GLOBAL_PLL_CONTROL	0x08

#define GEMINI_GLOBAL_SOFT_RESET	0x0c

#define GEMINI_GLOBAL_MISC_CONTROL	0x30
#define PCI_CLK_66MHZ			BIT(18)

#define GEMINI_GLOBAL_CLOCK_CONTROL	0x34
#define PCI_CLKRUN_EN			BIT(16)
#define TVC_HALFDIV_SHIFT		(24)
#define TVC_HALFDIV_MASK		0x1f
#define SECURITY_CLK_SEL		BIT(29)

#define GEMINI_GLOBAL_PCI_DLL_CONTROL	0x44
#define PCI_DLL_BYPASS			BIT(31)
#define PCI_DLL_TAP_SEL_MASK		0x1f

/**
 * struct gemini_data_data - Gemini gated clocks
 * @bit_idx: the bit used to gate this clock in the clock register
 * @name: the clock name
 * @parent_name: the name of the parent clock
 * @flags: standard clock framework flags
 */
struct gemini_gate_data {
	u8 bit_idx;
	const char *name;
	const char *parent_name;
	unsigned long flags;
};

/**
 * struct clk_gemini_pci - Gemini PCI clock
 * @hw: corresponding clock hardware entry
 * @map: regmap to access the registers
 * @rate: current rate
 */
struct clk_gemini_pci {
	struct clk_hw hw;
	struct regmap *map;
	unsigned long rate;
};

/**
 * struct gemini_reset - gemini reset controller
 * @map: regmap to access the containing system controller
 * @rcdev: reset controller device
 */
struct gemini_reset {
	struct regmap *map;
	struct reset_controller_dev rcdev;
};

/* Keeps track of all clocks */
static struct clk_hw_onecell_data *gemini_clk_data;

static const struct gemini_gate_data gemini_gates[] = {
	{ 1, "security-gate", "secdiv", 0 },
	{ 2, "gmac0-gate", "ahb", 0 },
	{ 3, "gmac1-gate", "ahb", 0 },
	{ 4, "sata0-gate", "ahb", 0 },
	{ 5, "sata1-gate", "ahb", 0 },
	{ 6, "usb0-gate", "ahb", 0 },
	{ 7, "usb1-gate", "ahb", 0 },
	{ 8, "ide-gate", "ahb", 0 },
	{ 9, "pci-gate", "ahb", 0 },
	/*
	 * The DDR controller may never have a driver, but certainly must
	 * not be gated off.
	 */
	{ 10, "ddr-gate", "ahb", CLK_IS_CRITICAL },
	/*
	 * The flash controller must be on to access NOR flash through the
	 * memory map.
	 */
	{ 11, "flash-gate", "ahb", CLK_IGNORE_UNUSED },
	{ 12, "tvc-gate", "ahb", 0 },
	{ 13, "boot-gate", "apb", 0 },
};

#define to_pciclk(_hw) container_of(_hw, struct clk_gemini_pci, hw)

#define to_gemini_reset(p) container_of((p), struct gemini_reset, rcdev)

static unsigned long gemini_pci_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct clk_gemini_pci *pciclk = to_pciclk(hw);
	u32 val;

	regmap_read(pciclk->map, GEMINI_GLOBAL_MISC_CONTROL, &val);
	if (val & PCI_CLK_66MHZ)
		return 66000000;
	return 33000000;
}

static long gemini_pci_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *prate)
{
	/* We support 33 and 66 MHz */
	if (rate < 48000000)
		return 33000000;
	return 66000000;
}

static int gemini_pci_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct clk_gemini_pci *pciclk = to_pciclk(hw);

	if (rate == 33000000)
		return regmap_update_bits(pciclk->map,
					  GEMINI_GLOBAL_MISC_CONTROL,
					  PCI_CLK_66MHZ, 0);
	if (rate == 66000000)
		return regmap_update_bits(pciclk->map,
					  GEMINI_GLOBAL_MISC_CONTROL,
					  0, PCI_CLK_66MHZ);
	return -EINVAL;
}

static int gemini_pci_enable(struct clk_hw *hw)
{
	struct clk_gemini_pci *pciclk = to_pciclk(hw);

	regmap_update_bits(pciclk->map, GEMINI_GLOBAL_CLOCK_CONTROL,
			   0, PCI_CLKRUN_EN);
	return 0;
}

static void gemini_pci_disable(struct clk_hw *hw)
{
	struct clk_gemini_pci *pciclk = to_pciclk(hw);

	regmap_update_bits(pciclk->map, GEMINI_GLOBAL_CLOCK_CONTROL,
			   PCI_CLKRUN_EN, 0);
}

static int gemini_pci_is_enabled(struct clk_hw *hw)
{
	struct clk_gemini_pci *pciclk = to_pciclk(hw);
	unsigned int val;

	regmap_read(pciclk->map, GEMINI_GLOBAL_CLOCK_CONTROL, &val);
	return !!(val & PCI_CLKRUN_EN);
}

static const struct clk_ops gemini_pci_clk_ops = {
	.recalc_rate = gemini_pci_recalc_rate,
	.round_rate = gemini_pci_round_rate,
	.set_rate = gemini_pci_set_rate,
	.enable = gemini_pci_enable,
	.disable = gemini_pci_disable,
	.is_enabled = gemini_pci_is_enabled,
};

static struct clk_hw *gemini_pci_clk_setup(const char *name,
					   const char *parent_name,
					   struct regmap *map)
{
	struct clk_gemini_pci *pciclk;
	struct clk_init_data init;
	int ret;

	pciclk = kzalloc(sizeof(*pciclk), GFP_KERNEL);
	if (!pciclk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &gemini_pci_clk_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	pciclk->map = map;
	pciclk->hw.init = &init;

	ret = clk_hw_register(NULL, &pciclk->hw);
	if (ret) {
		kfree(pciclk);
		return ERR_PTR(ret);
	}

	return &pciclk->hw;
}

/*
 * This is a self-deasserting reset controller.
 */
static int gemini_reset(struct reset_controller_dev *rcdev,
			unsigned long id)
{
	struct gemini_reset *gr = to_gemini_reset(rcdev);

	/* Manual says to always set BIT 30 (CPU1) to 1 */
	return regmap_write(gr->map,
			    GEMINI_GLOBAL_SOFT_RESET,
			    BIT(GEMINI_RESET_CPU1) | BIT(id));
}

static int gemini_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	return 0;
}

static int gemini_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return 0;
}

static int gemini_reset_status(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	struct gemini_reset *gr = to_gemini_reset(rcdev);
	u32 val;
	int ret;

	ret = regmap_read(gr->map, GEMINI_GLOBAL_SOFT_RESET, &val);
	if (ret)
		return ret;

	return !!(val & BIT(id));
}

static const struct reset_control_ops gemini_reset_ops = {
	.reset = gemini_reset,
	.assert = gemini_reset_assert,
	.deassert = gemini_reset_deassert,
	.status = gemini_reset_status,
};

static int gemini_clk_probe(struct platform_device *pdev)
{
	/* Gives the fracions 1x, 1.5x, 1.85x and 2x */
	unsigned int cpu_ahb_mult[4] = { 1, 3, 24, 2 };
	unsigned int cpu_ahb_div[4] = { 1, 2, 13, 1 };
	void __iomem *base;
	struct gemini_reset *gr;
	struct regmap *map;
	struct clk_hw *hw;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	unsigned int mult, div;
	struct resource *res;
	u32 val;
	int ret;
	int i;

	gr = devm_kzalloc(dev, sizeof(*gr), GFP_KERNEL);
	if (!gr)
		return -ENOMEM;

	/* Remap the system controller for the exclusive register */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	map = syscon_node_to_regmap(np);
	if (IS_ERR(map)) {
		dev_err(dev, "no syscon regmap\n");
		return PTR_ERR(map);
	}

	gr->map = map;
	gr->rcdev.owner = THIS_MODULE;
	gr->rcdev.nr_resets = 32;
	gr->rcdev.ops = &gemini_reset_ops;
	gr->rcdev.of_node = np;

	ret = devm_reset_controller_register(dev, &gr->rcdev);
	if (ret) {
		dev_err(dev, "could not register reset controller\n");
		return ret;
	}

	/* RTC clock 32768 Hz */
	hw = clk_hw_register_fixed_rate(NULL, "rtc", NULL, 0, 32768);
	gemini_clk_data->hws[GEMINI_CLK_RTC] = hw;

	/* CPU clock derived as a fixed ratio from the AHB clock */
	regmap_read(map, GEMINI_GLOBAL_STATUS, &val);
	val >>= CPU_AHB_RATIO_SHIFT;
	val &= CPU_AHB_RATIO_MASK;
	hw = clk_hw_register_fixed_factor(NULL, "cpu", "ahb", 0,
					  cpu_ahb_mult[val],
					  cpu_ahb_div[val]);
	gemini_clk_data->hws[GEMINI_CLK_CPU] = hw;

	/* Security clock is 1:1 or 0.75 of APB */
	regmap_read(map, GEMINI_GLOBAL_CLOCK_CONTROL, &val);
	if (val & SECURITY_CLK_SEL) {
		mult = 1;
		div = 1;
	} else {
		mult = 3;
		div = 4;
	}
	hw = clk_hw_register_fixed_factor(NULL, "secdiv", "ahb", 0, mult, div);

	/*
	 * These are the leaf gates, at boot no clocks are gated.
	 */
	for (i = 0; i < ARRAY_SIZE(gemini_gates); i++) {
		const struct gemini_gate_data *gd;

		gd = &gemini_gates[i];
		gemini_clk_data->hws[GEMINI_CLK_GATES + i] =
			clk_hw_register_gate(NULL, gd->name,
					     gd->parent_name,
					     gd->flags,
					     base + GEMINI_GLOBAL_CLOCK_CONTROL,
					     gd->bit_idx,
					     CLK_GATE_SET_TO_DISABLE,
					     &gemini_clk_lock);
	}

	/*
	 * The TV Interface Controller has a 5-bit half divider register.
	 * This clock is supposed to be 27MHz as this is an exact multiple
	 * of PAL and NTSC frequencies. The register is undocumented :(
	 * FIXME: figure out the parent and how the divider works.
	 */
	mult = 1;
	div = ((val >> TVC_HALFDIV_SHIFT) & TVC_HALFDIV_MASK);
	dev_dbg(dev, "TVC half divider value = %d\n", div);
	div += 1;
	hw = clk_hw_register_fixed_rate(NULL, "tvcdiv", "xtal", 0, 27000000);
	gemini_clk_data->hws[GEMINI_CLK_TVC] = hw;

	/* FIXME: very unclear what the parent is */
	hw = gemini_pci_clk_setup("PCI", "xtal", map);
	gemini_clk_data->hws[GEMINI_CLK_PCI] = hw;

	/* FIXME: very unclear what the parent is */
	hw = clk_hw_register_fixed_rate(NULL, "uart", "xtal", 0, 48000000);
	gemini_clk_data->hws[GEMINI_CLK_UART] = hw;

	return 0;
}

static const struct of_device_id gemini_clk_dt_ids[] = {
	{ .compatible = "cortina,gemini-syscon", },
	{ /* sentinel */ },
};

static struct platform_driver gemini_clk_driver = {
	.probe  = gemini_clk_probe,
	.driver = {
		.name = "gemini-clk",
		.of_match_table = gemini_clk_dt_ids,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(gemini_clk_driver);

static void __init gemini_cc_init(struct device_node *np)
{
	struct regmap *map;
	struct clk_hw *hw;
	unsigned long freq;
	unsigned int mult, div;
	u32 val;
	int ret;
	int i;

	gemini_clk_data = kzalloc(sizeof(*gemini_clk_data) +
			sizeof(*gemini_clk_data->hws) * GEMINI_NUM_CLKS,
			GFP_KERNEL);
	if (!gemini_clk_data)
		return;

	/*
	 * This way all clock fetched before the platform device probes,
	 * except those we assign here for early use, will be deferred.
	 */
	for (i = 0; i < GEMINI_NUM_CLKS; i++)
		gemini_clk_data->hws[i] = ERR_PTR(-EPROBE_DEFER);

	map = syscon_node_to_regmap(np);
	if (IS_ERR(map)) {
		pr_err("no syscon regmap\n");
		return;
	}
	/*
	 * We check that the regmap works on this very first access,
	 * but as this is an MMIO-backed regmap, subsequent regmap
	 * access is not going to fail and we skip error checks from
	 * this point.
	 */
	ret = regmap_read(map, GEMINI_GLOBAL_STATUS, &val);
	if (ret) {
		pr_err("failed to read global status register\n");
		return;
	}

	/*
	 * XTAL is the crystal oscillator, 60 or 30 MHz selected from
	 * strap pin E6
	 */
	if (val & PLL_OSC_SEL)
		freq = 30000000;
	else
		freq = 60000000;
	hw = clk_hw_register_fixed_rate(NULL, "xtal", NULL, 0, freq);
	pr_debug("main crystal @%lu MHz\n", freq / 1000000);

	/* VCO clock derived from the crystal */
	mult = 13 + ((val >> AHBSPEED_SHIFT) & AHBSPEED_MASK);
	div = 2;
	/* If we run on 30 MHz crystal we have to multiply with two */
	if (val & PLL_OSC_SEL)
		mult *= 2;
	hw = clk_hw_register_fixed_factor(NULL, "vco", "xtal", 0, mult, div);

	/* The AHB clock is always 1/3 of the VCO */
	hw = clk_hw_register_fixed_factor(NULL, "ahb", "vco", 0, 1, 3);
	gemini_clk_data->hws[GEMINI_CLK_AHB] = hw;

	/* The APB clock is always 1/6 of the AHB */
	hw = clk_hw_register_fixed_factor(NULL, "apb", "ahb", 0, 1, 6);
	gemini_clk_data->hws[GEMINI_CLK_APB] = hw;

	/* Register the clocks to be accessed by the device tree */
	gemini_clk_data->num = GEMINI_NUM_CLKS;
	of_clk_add_hw_provider(np, of_clk_hw_onecell_get, gemini_clk_data);
}
CLK_OF_DECLARE_DRIVER(gemini_cc, "cortina,gemini-syscon", gemini_cc_init);
