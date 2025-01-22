// SPDX-License-Identifier: GPL-2.0
/*
 * MTMIPS SoCs Clock Driver
 * Author: Sergio Paracuellos <sergio.paracuellos@gmail.com>
 */

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>

/* Configuration registers */
#define SYSC_REG_SYSTEM_CONFIG		0x10
#define SYSC_REG_CLKCFG0		0x2c
#define SYSC_REG_RESET_CTRL		0x34
#define SYSC_REG_CPU_SYS_CLKCFG		0x3c
#define SYSC_REG_CPLL_CONFIG0		0x54
#define SYSC_REG_CPLL_CONFIG1		0x58

/* RT2880 SoC */
#define RT2880_CONFIG_CPUCLK_SHIFT	20
#define RT2880_CONFIG_CPUCLK_MASK	0x3
#define RT2880_CONFIG_CPUCLK_250	0x0
#define RT2880_CONFIG_CPUCLK_266	0x1
#define RT2880_CONFIG_CPUCLK_280	0x2
#define RT2880_CONFIG_CPUCLK_300	0x3

/* RT305X SoC */
#define RT305X_SYSCFG_CPUCLK_SHIFT	18
#define RT305X_SYSCFG_CPUCLK_MASK	0x1
#define RT305X_SYSCFG_CPUCLK_LOW	0x0
#define RT305X_SYSCFG_CPUCLK_HIGH	0x1

/* RT3352 SoC */
#define RT3352_SYSCFG0_CPUCLK_SHIFT	8
#define RT3352_SYSCFG0_CPUCLK_MASK	0x1
#define RT3352_SYSCFG0_CPUCLK_LOW	0x0
#define RT3352_SYSCFG0_CPUCLK_HIGH	0x1

/* RT3383 SoC */
#define RT3883_SYSCFG0_DRAM_TYPE_DDR2	BIT(17)
#define RT3883_SYSCFG0_CPUCLK_SHIFT	8
#define RT3883_SYSCFG0_CPUCLK_MASK	0x3
#define RT3883_SYSCFG0_CPUCLK_250	0x0
#define RT3883_SYSCFG0_CPUCLK_384	0x1
#define RT3883_SYSCFG0_CPUCLK_480	0x2
#define RT3883_SYSCFG0_CPUCLK_500	0x3

/* RT5350 SoC */
#define RT5350_CLKCFG0_XTAL_SEL		BIT(20)
#define RT5350_SYSCFG0_CPUCLK_SHIFT	8
#define RT5350_SYSCFG0_CPUCLK_MASK	0x3
#define RT5350_SYSCFG0_CPUCLK_360	0x0
#define RT5350_SYSCFG0_CPUCLK_320	0x2
#define RT5350_SYSCFG0_CPUCLK_300	0x3

/* MT7620 and MT76x8 SoCs */
#define MT7620_XTAL_FREQ_SEL		BIT(6)
#define CPLL_CFG0_SW_CFG		BIT(31)
#define CPLL_CFG0_PLL_MULT_RATIO_SHIFT	16
#define CPLL_CFG0_PLL_MULT_RATIO_MASK   0x7
#define CPLL_CFG0_LC_CURFCK		BIT(15)
#define CPLL_CFG0_BYPASS_REF_CLK	BIT(14)
#define CPLL_CFG0_PLL_DIV_RATIO_SHIFT	10
#define CPLL_CFG0_PLL_DIV_RATIO_MASK	0x3
#define CPLL_CFG1_CPU_AUX1		BIT(25)
#define CPLL_CFG1_CPU_AUX0		BIT(24)
#define CLKCFG0_PERI_CLK_SEL		BIT(4)
#define CPU_SYS_CLKCFG_OCP_RATIO_SHIFT	16
#define CPU_SYS_CLKCFG_OCP_RATIO_MASK	0xf
#define CPU_SYS_CLKCFG_OCP_RATIO_1	0	/* 1:1   (Reserved) */
#define CPU_SYS_CLKCFG_OCP_RATIO_1_5	1	/* 1:1.5 (Reserved) */
#define CPU_SYS_CLKCFG_OCP_RATIO_2	2	/* 1:2   */
#define CPU_SYS_CLKCFG_OCP_RATIO_2_5	3       /* 1:2.5 (Reserved) */
#define CPU_SYS_CLKCFG_OCP_RATIO_3	4	/* 1:3   */
#define CPU_SYS_CLKCFG_OCP_RATIO_3_5	5	/* 1:3.5 (Reserved) */
#define CPU_SYS_CLKCFG_OCP_RATIO_4	6	/* 1:4   */
#define CPU_SYS_CLKCFG_OCP_RATIO_5	7	/* 1:5   */
#define CPU_SYS_CLKCFG_OCP_RATIO_10	8	/* 1:10  */
#define CPU_SYS_CLKCFG_CPU_FDIV_SHIFT	8
#define CPU_SYS_CLKCFG_CPU_FDIV_MASK	0x1f
#define CPU_SYS_CLKCFG_CPU_FFRAC_SHIFT	0
#define CPU_SYS_CLKCFG_CPU_FFRAC_MASK	0x1f

/* clock scaling */
#define CLKCFG_FDIV_MASK		0x1f00
#define CLKCFG_FDIV_USB_VAL		0x0300
#define CLKCFG_FFRAC_MASK		0x001f
#define CLKCFG_FFRAC_USB_VAL		0x0003

struct mtmips_clk;
struct mtmips_clk_fixed;
struct mtmips_clk_factor;

struct mtmips_clk_data {
	struct mtmips_clk *clk_base;
	size_t num_clk_base;
	struct mtmips_clk_fixed *clk_fixed;
	size_t num_clk_fixed;
	struct mtmips_clk_factor *clk_factor;
	size_t num_clk_factor;
	struct mtmips_clk *clk_periph;
	size_t num_clk_periph;
};

struct mtmips_clk_priv {
	struct regmap *sysc;
	const struct mtmips_clk_data *data;
};

struct mtmips_clk {
	struct clk_hw hw;
	struct mtmips_clk_priv *priv;
};

struct mtmips_clk_fixed {
	const char *name;
	const char *parent;
	unsigned long rate;
	struct clk_hw *hw;
};

struct mtmips_clk_factor {
	const char *name;
	const char *parent;
	int mult;
	int div;
	unsigned long flags;
	struct clk_hw *hw;
};

static unsigned long mtmips_pherip_clk_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	return parent_rate;
}

static const struct clk_ops mtmips_periph_clk_ops = {
	.recalc_rate = mtmips_pherip_clk_rate,
};

#define CLK_PERIPH(_name, _parent) {				\
	.init = &(const struct clk_init_data) {			\
		.name = _name,					\
		.ops = &mtmips_periph_clk_ops,			\
		.parent_data = &(const struct clk_parent_data) {\
			.name = _parent,			\
			.fw_name = _parent			\
		},						\
		.num_parents = 1,				\
		/*						\
		 * There are drivers for these SoCs that are	\
		 * older than clock driver and are not prepared \
		 * for the clock. We don't want the kernel to   \
		 * disable anything so we add CLK_IS_CRITICAL	\
		 * flag here.					\
		 */						\
		.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL	\
	},							\
}

static struct mtmips_clk rt2880_pherip_clks[] = {
	{ CLK_PERIPH("300100.timer", "bus") },
	{ CLK_PERIPH("300120.watchdog", "bus") },
	{ CLK_PERIPH("300500.uart", "bus") },
	{ CLK_PERIPH("300900.i2c", "bus") },
	{ CLK_PERIPH("300c00.uartlite", "bus") },
	{ CLK_PERIPH("400000.ethernet", "bus") },
	{ CLK_PERIPH("480000.wmac", "xtal") }
};

static struct mtmips_clk rt305x_pherip_clks[] = {
	{ CLK_PERIPH("10000100.timer", "bus") },
	{ CLK_PERIPH("10000120.watchdog", "bus") },
	{ CLK_PERIPH("10000500.uart", "bus") },
	{ CLK_PERIPH("10000900.i2c", "bus") },
	{ CLK_PERIPH("10000a00.i2s", "bus") },
	{ CLK_PERIPH("10000b00.spi", "bus") },
	{ CLK_PERIPH("10000b40.spi", "bus") },
	{ CLK_PERIPH("10000c00.uartlite", "bus") },
	{ CLK_PERIPH("10100000.ethernet", "bus") },
	{ CLK_PERIPH("10180000.wmac", "xtal") }
};

static struct mtmips_clk rt5350_pherip_clks[] = {
	{ CLK_PERIPH("10000100.timer", "bus") },
	{ CLK_PERIPH("10000120.watchdog", "bus") },
	{ CLK_PERIPH("10000500.uart", "periph") },
	{ CLK_PERIPH("10000900.i2c", "periph") },
	{ CLK_PERIPH("10000a00.i2s", "periph") },
	{ CLK_PERIPH("10000b00.spi", "bus") },
	{ CLK_PERIPH("10000b40.spi", "bus") },
	{ CLK_PERIPH("10000c00.uartlite", "periph") },
	{ CLK_PERIPH("10100000.ethernet", "bus") },
	{ CLK_PERIPH("10180000.wmac", "xtal") }
};

static struct mtmips_clk mt7620_pherip_clks[] = {
	{ CLK_PERIPH("10000100.timer", "periph") },
	{ CLK_PERIPH("10000120.watchdog", "periph") },
	{ CLK_PERIPH("10000500.uart", "periph") },
	{ CLK_PERIPH("10000900.i2c", "periph") },
	{ CLK_PERIPH("10000a00.i2s", "periph") },
	{ CLK_PERIPH("10000b00.spi", "bus") },
	{ CLK_PERIPH("10000b40.spi", "bus") },
	{ CLK_PERIPH("10000c00.uartlite", "periph") },
	{ CLK_PERIPH("10130000.mmc", "sdhc") },
	{ CLK_PERIPH("10180000.wmac", "xtal") }
};

static struct mtmips_clk mt76x8_pherip_clks[] = {
	{ CLK_PERIPH("10000100.timer", "periph") },
	{ CLK_PERIPH("10000120.watchdog", "periph") },
	{ CLK_PERIPH("10000900.i2c", "periph") },
	{ CLK_PERIPH("10000a00.i2s", "pcmi2s") },
	{ CLK_PERIPH("10000b00.spi", "bus") },
	{ CLK_PERIPH("10000b40.spi", "bus") },
	{ CLK_PERIPH("10000c00.uart0", "periph") },
	{ CLK_PERIPH("10000d00.uart1", "periph") },
	{ CLK_PERIPH("10000e00.uart2", "periph") },
	{ CLK_PERIPH("10130000.mmc", "sdhc") },
	{ CLK_PERIPH("10300000.wmac", "xtal") }
};

static int mtmips_register_pherip_clocks(struct device_node *np,
					 struct clk_hw_onecell_data *clk_data,
					 struct mtmips_clk_priv *priv)
{
	struct clk_hw **hws = clk_data->hws;
	struct mtmips_clk *sclk;
	size_t idx_start = priv->data->num_clk_base + priv->data->num_clk_fixed +
			   priv->data->num_clk_factor;
	int ret, i;

	for (i = 0; i < priv->data->num_clk_periph; i++) {
		int idx = idx_start + i;

		sclk = &priv->data->clk_periph[i];
		ret = of_clk_hw_register(np, &sclk->hw);
		if (ret) {
			pr_err("Couldn't register peripheral clock %d\n", idx);
			goto err_clk_unreg;
		}

		hws[idx] = &sclk->hw;
	}

	return 0;

err_clk_unreg:
	while (--i >= 0) {
		sclk = &priv->data->clk_periph[i];
		clk_hw_unregister(&sclk->hw);
	}
	return ret;
}

#define CLK_FIXED(_name, _parent, _rate) \
	{				 \
		.name = _name,		 \
		.parent = _parent,	 \
		.rate = _rate		 \
	}

static struct mtmips_clk_fixed rt3883_fixed_clocks[] = {
	CLK_FIXED("periph", "xtal", 40000000)
};

static struct mtmips_clk_fixed rt3352_fixed_clocks[] = {
	CLK_FIXED("periph", "xtal", 40000000)
};

static struct mtmips_clk_fixed mt7620_fixed_clocks[] = {
	CLK_FIXED("bbppll", "xtal", 480000000)
};

static struct mtmips_clk_fixed mt76x8_fixed_clocks[] = {
	CLK_FIXED("bbppll", "xtal", 480000000),
	CLK_FIXED("pcmi2s", "bbppll", 480000000),
	CLK_FIXED("periph", "xtal", 40000000)
};

static int mtmips_register_fixed_clocks(struct clk_hw_onecell_data *clk_data,
					struct mtmips_clk_priv *priv)
{
	struct clk_hw **hws = clk_data->hws;
	struct mtmips_clk_fixed *sclk;
	size_t idx_start = priv->data->num_clk_base;
	int ret, i;

	for (i = 0; i < priv->data->num_clk_fixed; i++) {
		int idx = idx_start + i;

		sclk = &priv->data->clk_fixed[i];
		sclk->hw = clk_hw_register_fixed_rate(NULL, sclk->name,
						      sclk->parent, 0,
						      sclk->rate);
		if (IS_ERR(sclk->hw)) {
			ret = PTR_ERR(sclk->hw);
			pr_err("Couldn't register fixed clock %d\n", idx);
			goto err_clk_unreg;
		}

		hws[idx] = sclk->hw;
	}

	return 0;

err_clk_unreg:
	while (--i >= 0) {
		sclk = &priv->data->clk_fixed[i];
		clk_hw_unregister_fixed_rate(sclk->hw);
	}
	return ret;
}

#define CLK_FACTOR(_name, _parent, _mult, _div)		\
	{						\
		.name = _name,				\
		.parent = _parent,			\
		.mult = _mult,				\
		.div = _div,				\
		.flags = CLK_SET_RATE_PARENT		\
	}

static struct mtmips_clk_factor rt2880_factor_clocks[] = {
	CLK_FACTOR("bus", "cpu", 1, 2)
};

static struct mtmips_clk_factor rt305x_factor_clocks[] = {
	CLK_FACTOR("bus", "cpu", 1, 3)
};

static struct mtmips_clk_factor mt7620_factor_clocks[] = {
	CLK_FACTOR("sdhc", "bbppll", 1, 10)
};

static struct mtmips_clk_factor mt76x8_factor_clocks[] = {
	CLK_FACTOR("bus", "cpu", 1, 3),
	CLK_FACTOR("sdhc", "bbppll", 1, 10)
};

static int mtmips_register_factor_clocks(struct clk_hw_onecell_data *clk_data,
					 struct mtmips_clk_priv *priv)
{
	struct clk_hw **hws = clk_data->hws;
	struct mtmips_clk_factor *sclk;
	size_t idx_start = priv->data->num_clk_base + priv->data->num_clk_fixed;
	int ret, i;

	for (i = 0; i < priv->data->num_clk_factor; i++) {
		int idx = idx_start + i;

		sclk = &priv->data->clk_factor[i];
		sclk->hw = clk_hw_register_fixed_factor(NULL, sclk->name,
						  sclk->parent, sclk->flags,
						  sclk->mult, sclk->div);
		if (IS_ERR(sclk->hw)) {
			ret = PTR_ERR(sclk->hw);
			pr_err("Couldn't register factor clock %d\n", idx);
			goto err_clk_unreg;
		}

		hws[idx] = sclk->hw;
	}

	return 0;

err_clk_unreg:
	while (--i >= 0) {
		sclk = &priv->data->clk_factor[i];
		clk_hw_unregister_fixed_factor(sclk->hw);
	}
	return ret;
}

static inline struct mtmips_clk *to_mtmips_clk(struct clk_hw *hw)
{
	return container_of(hw, struct mtmips_clk, hw);
}

static unsigned long rt2880_xtal_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	return 40000000;
}

static unsigned long rt5350_xtal_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct mtmips_clk *clk = to_mtmips_clk(hw);
	struct regmap *sysc = clk->priv->sysc;
	u32 val;

	regmap_read(sysc, SYSC_REG_SYSTEM_CONFIG, &val);
	if (!(val & RT5350_CLKCFG0_XTAL_SEL))
		return 20000000;

	return 40000000;
}

static unsigned long rt5350_cpu_recalc_rate(struct clk_hw *hw,
					    unsigned long xtal_clk)
{
	struct mtmips_clk *clk = to_mtmips_clk(hw);
	struct regmap *sysc = clk->priv->sysc;
	u32 t;

	regmap_read(sysc, SYSC_REG_SYSTEM_CONFIG, &t);
	t = (t >> RT5350_SYSCFG0_CPUCLK_SHIFT) & RT5350_SYSCFG0_CPUCLK_MASK;

	switch (t) {
	case RT5350_SYSCFG0_CPUCLK_360:
		return 360000000;
	case RT5350_SYSCFG0_CPUCLK_320:
		return 320000000;
	case RT5350_SYSCFG0_CPUCLK_300:
		return 300000000;
	default:
		BUG();
	}
}

static unsigned long rt5350_bus_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	if (parent_rate == 320000000)
		return parent_rate / 4;

	return parent_rate / 3;
}

static unsigned long rt3352_cpu_recalc_rate(struct clk_hw *hw,
					    unsigned long xtal_clk)
{
	struct mtmips_clk *clk = to_mtmips_clk(hw);
	struct regmap *sysc = clk->priv->sysc;
	u32 t;

	regmap_read(sysc, SYSC_REG_SYSTEM_CONFIG, &t);
	t = (t >> RT3352_SYSCFG0_CPUCLK_SHIFT) & RT3352_SYSCFG0_CPUCLK_MASK;

	switch (t) {
	case RT3352_SYSCFG0_CPUCLK_LOW:
		return 384000000;
	case RT3352_SYSCFG0_CPUCLK_HIGH:
		return 400000000;
	default:
		BUG();
	}
}

static unsigned long rt305x_cpu_recalc_rate(struct clk_hw *hw,
					    unsigned long xtal_clk)
{
	struct mtmips_clk *clk = to_mtmips_clk(hw);
	struct regmap *sysc = clk->priv->sysc;
	u32 t;

	regmap_read(sysc, SYSC_REG_SYSTEM_CONFIG, &t);
	t = (t >> RT305X_SYSCFG_CPUCLK_SHIFT) & RT305X_SYSCFG_CPUCLK_MASK;

	switch (t) {
	case RT305X_SYSCFG_CPUCLK_LOW:
		return 320000000;
	case RT305X_SYSCFG_CPUCLK_HIGH:
		return 384000000;
	default:
		BUG();
	}
}

static unsigned long rt3883_cpu_recalc_rate(struct clk_hw *hw,
					    unsigned long xtal_clk)
{
	struct mtmips_clk *clk = to_mtmips_clk(hw);
	struct regmap *sysc = clk->priv->sysc;
	u32 t;

	regmap_read(sysc, SYSC_REG_SYSTEM_CONFIG, &t);
	t = (t >> RT3883_SYSCFG0_CPUCLK_SHIFT) & RT3883_SYSCFG0_CPUCLK_MASK;

	switch (t) {
	case RT3883_SYSCFG0_CPUCLK_250:
		return 250000000;
	case RT3883_SYSCFG0_CPUCLK_384:
		return 384000000;
	case RT3883_SYSCFG0_CPUCLK_480:
		return 480000000;
	case RT3883_SYSCFG0_CPUCLK_500:
		return 500000000;
	default:
		BUG();
	}
}

static unsigned long rt3883_bus_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct mtmips_clk *clk = to_mtmips_clk(hw);
	struct regmap *sysc = clk->priv->sysc;
	u32 ddr2;
	u32 t;

	regmap_read(sysc, SYSC_REG_SYSTEM_CONFIG, &t);
	ddr2 = t & RT3883_SYSCFG0_DRAM_TYPE_DDR2;

	switch (parent_rate) {
	case 250000000:
		return (ddr2) ? 125000000 : 83000000;
	case 384000000:
		return (ddr2) ? 128000000 : 96000000;
	case 480000000:
		return (ddr2) ? 160000000 : 120000000;
	case 500000000:
		return (ddr2) ? 166000000 : 125000000;
	default:
		WARN_ON_ONCE(parent_rate == 0);
		return parent_rate / 4;
	}
}

static unsigned long rt2880_cpu_recalc_rate(struct clk_hw *hw,
					    unsigned long xtal_clk)
{
	struct mtmips_clk *clk = to_mtmips_clk(hw);
	struct regmap *sysc = clk->priv->sysc;
	u32 t;

	regmap_read(sysc, SYSC_REG_SYSTEM_CONFIG, &t);
	t = (t >> RT2880_CONFIG_CPUCLK_SHIFT) & RT2880_CONFIG_CPUCLK_MASK;

	switch (t) {
	case RT2880_CONFIG_CPUCLK_250:
		return 250000000;
	case RT2880_CONFIG_CPUCLK_266:
		return 266000000;
	case RT2880_CONFIG_CPUCLK_280:
		return 280000000;
	case RT2880_CONFIG_CPUCLK_300:
		return 300000000;
	default:
		BUG();
	}
}

static u32 mt7620_calc_rate(u32 ref_rate, u32 mul, u32 div)
{
	u64 t;

	t = ref_rate;
	t *= mul;
	t = div_u64(t, div);

	return t;
}

static unsigned long mt7620_pll_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	static const u32 clk_divider[] = { 2, 3, 4, 8 };
	struct mtmips_clk *clk = to_mtmips_clk(hw);
	struct regmap *sysc = clk->priv->sysc;
	unsigned long cpu_pll;
	u32 t;
	u32 mul;
	u32 div;

	regmap_read(sysc, SYSC_REG_CPLL_CONFIG0, &t);
	if (t & CPLL_CFG0_BYPASS_REF_CLK) {
		cpu_pll = parent_rate;
	} else if ((t & CPLL_CFG0_SW_CFG) == 0) {
		cpu_pll = 600000000;
	} else {
		mul = (t >> CPLL_CFG0_PLL_MULT_RATIO_SHIFT) &
			CPLL_CFG0_PLL_MULT_RATIO_MASK;
		mul += 24;
		if (t & CPLL_CFG0_LC_CURFCK)
			mul *= 2;

		div = (t >> CPLL_CFG0_PLL_DIV_RATIO_SHIFT) &
			CPLL_CFG0_PLL_DIV_RATIO_MASK;

		WARN_ON_ONCE(div >= ARRAY_SIZE(clk_divider));

		cpu_pll = mt7620_calc_rate(parent_rate, mul, clk_divider[div]);
	}

	regmap_read(sysc, SYSC_REG_CPLL_CONFIG1, &t);
	if (t & CPLL_CFG1_CPU_AUX1)
		return parent_rate;

	if (t & CPLL_CFG1_CPU_AUX0)
		return 480000000;

	return cpu_pll;
}

static unsigned long mt7620_cpu_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct mtmips_clk *clk = to_mtmips_clk(hw);
	struct regmap *sysc = clk->priv->sysc;
	u32 t;
	u32 mul;
	u32 div;

	regmap_read(sysc, SYSC_REG_CPU_SYS_CLKCFG, &t);
	mul = t & CPU_SYS_CLKCFG_CPU_FFRAC_MASK;
	div = (t >> CPU_SYS_CLKCFG_CPU_FDIV_SHIFT) &
		CPU_SYS_CLKCFG_CPU_FDIV_MASK;

	return mt7620_calc_rate(parent_rate, mul, div);
}

static unsigned long mt7620_bus_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	static const u32 ocp_dividers[16] = {
		[CPU_SYS_CLKCFG_OCP_RATIO_2] = 2,
		[CPU_SYS_CLKCFG_OCP_RATIO_3] = 3,
		[CPU_SYS_CLKCFG_OCP_RATIO_4] = 4,
		[CPU_SYS_CLKCFG_OCP_RATIO_5] = 5,
		[CPU_SYS_CLKCFG_OCP_RATIO_10] = 10,
	};
	struct mtmips_clk *clk = to_mtmips_clk(hw);
	struct regmap *sysc = clk->priv->sysc;
	u32 t;
	u32 ocp_ratio;
	u32 div;

	regmap_read(sysc, SYSC_REG_CPU_SYS_CLKCFG, &t);
	ocp_ratio = (t >> CPU_SYS_CLKCFG_OCP_RATIO_SHIFT) &
		CPU_SYS_CLKCFG_OCP_RATIO_MASK;

	if (WARN_ON_ONCE(ocp_ratio >= ARRAY_SIZE(ocp_dividers)))
		return parent_rate;

	div = ocp_dividers[ocp_ratio];

	if (WARN(!div, "invalid divider for OCP ratio %u", ocp_ratio))
		return parent_rate;

	return parent_rate / div;
}

static unsigned long mt7620_periph_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct mtmips_clk *clk = to_mtmips_clk(hw);
	struct regmap *sysc = clk->priv->sysc;
	u32 t;

	regmap_read(sysc, SYSC_REG_CLKCFG0, &t);
	if (t & CLKCFG0_PERI_CLK_SEL)
		return parent_rate;

	return 40000000;
}

static unsigned long mt76x8_xtal_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct mtmips_clk *clk = to_mtmips_clk(hw);
	struct regmap *sysc = clk->priv->sysc;
	u32 t;

	regmap_read(sysc, SYSC_REG_SYSTEM_CONFIG, &t);
	if (t & MT7620_XTAL_FREQ_SEL)
		return 40000000;

	return 20000000;
}

static unsigned long mt76x8_cpu_recalc_rate(struct clk_hw *hw,
					    unsigned long xtal_clk)
{
	if (xtal_clk == 40000000)
		return 580000000;

	return 575000000;
}

#define CLK_BASE(_name, _parent, _recalc) {				\
	.init = &(const struct clk_init_data) {				\
		.name = _name,						\
		.ops = &(const struct clk_ops) {			\
			.recalc_rate = _recalc,				\
		},							\
		.parent_data = &(const struct clk_parent_data) {	\
			.name = _parent,				\
			.fw_name = _parent				\
		},							\
		.num_parents = _parent ? 1 : 0				\
	},								\
}

static struct mtmips_clk rt2880_clks_base[] = {
	{ CLK_BASE("xtal", NULL, rt2880_xtal_recalc_rate) },
	{ CLK_BASE("cpu", "xtal", rt2880_cpu_recalc_rate) }
};

static struct mtmips_clk rt305x_clks_base[] = {
	{ CLK_BASE("xtal", NULL, rt2880_xtal_recalc_rate) },
	{ CLK_BASE("cpu", "xtal", rt305x_cpu_recalc_rate) }
};

static struct mtmips_clk rt3352_clks_base[] = {
	{ CLK_BASE("xtal", NULL, rt5350_xtal_recalc_rate) },
	{ CLK_BASE("cpu", "xtal", rt3352_cpu_recalc_rate) }
};

static struct mtmips_clk rt3883_clks_base[] = {
	{ CLK_BASE("xtal", NULL, rt2880_xtal_recalc_rate) },
	{ CLK_BASE("cpu", "xtal", rt3883_cpu_recalc_rate) },
	{ CLK_BASE("bus", "cpu", rt3883_bus_recalc_rate) }
};

static struct mtmips_clk rt5350_clks_base[] = {
	{ CLK_BASE("xtal", NULL, rt5350_xtal_recalc_rate) },
	{ CLK_BASE("cpu", "xtal", rt5350_cpu_recalc_rate) },
	{ CLK_BASE("bus", "cpu", rt5350_bus_recalc_rate) }
};

static struct mtmips_clk mt7620_clks_base[] = {
	{ CLK_BASE("xtal", NULL, mt76x8_xtal_recalc_rate) },
	{ CLK_BASE("pll", "xtal", mt7620_pll_recalc_rate) },
	{ CLK_BASE("cpu", "pll", mt7620_cpu_recalc_rate) },
	{ CLK_BASE("periph", "xtal", mt7620_periph_recalc_rate) },
	{ CLK_BASE("bus", "cpu", mt7620_bus_recalc_rate) }
};

static struct mtmips_clk mt76x8_clks_base[] = {
	{ CLK_BASE("xtal", NULL, mt76x8_xtal_recalc_rate) },
	{ CLK_BASE("cpu", "xtal", mt76x8_cpu_recalc_rate) }
};

static int mtmips_register_clocks(struct device_node *np,
				  struct clk_hw_onecell_data *clk_data,
				  struct mtmips_clk_priv *priv)
{
	struct clk_hw **hws = clk_data->hws;
	struct mtmips_clk *sclk;
	int ret, i;

	for (i = 0; i < priv->data->num_clk_base; i++) {
		sclk = &priv->data->clk_base[i];
		sclk->priv = priv;
		ret = of_clk_hw_register(np, &sclk->hw);
		if (ret) {
			pr_err("Couldn't register top clock %i\n", i);
			goto err_clk_unreg;
		}

		hws[i] = &sclk->hw;
	}

	return 0;

err_clk_unreg:
	while (--i >= 0) {
		sclk = &priv->data->clk_base[i];
		clk_hw_unregister(&sclk->hw);
	}
	return ret;
}

static const struct mtmips_clk_data rt2880_clk_data = {
	.clk_base = rt2880_clks_base,
	.num_clk_base = ARRAY_SIZE(rt2880_clks_base),
	.clk_fixed = NULL,
	.num_clk_fixed = 0,
	.clk_factor = rt2880_factor_clocks,
	.num_clk_factor = ARRAY_SIZE(rt2880_factor_clocks),
	.clk_periph = rt2880_pherip_clks,
	.num_clk_periph = ARRAY_SIZE(rt2880_pherip_clks),
};

static const struct mtmips_clk_data rt305x_clk_data = {
	.clk_base = rt305x_clks_base,
	.num_clk_base = ARRAY_SIZE(rt305x_clks_base),
	.clk_fixed = NULL,
	.num_clk_fixed = 0,
	.clk_factor = rt305x_factor_clocks,
	.num_clk_factor = ARRAY_SIZE(rt305x_factor_clocks),
	.clk_periph = rt305x_pherip_clks,
	.num_clk_periph = ARRAY_SIZE(rt305x_pherip_clks),
};

static const struct mtmips_clk_data rt3352_clk_data = {
	.clk_base = rt3352_clks_base,
	.num_clk_base = ARRAY_SIZE(rt3352_clks_base),
	.clk_fixed = rt3352_fixed_clocks,
	.num_clk_fixed = ARRAY_SIZE(rt3352_fixed_clocks),
	.clk_factor = rt305x_factor_clocks,
	.num_clk_factor = ARRAY_SIZE(rt305x_factor_clocks),
	.clk_periph = rt5350_pherip_clks,
	.num_clk_periph = ARRAY_SIZE(rt5350_pherip_clks),
};

static const struct mtmips_clk_data rt3883_clk_data = {
	.clk_base = rt3883_clks_base,
	.num_clk_base = ARRAY_SIZE(rt3883_clks_base),
	.clk_fixed = rt3883_fixed_clocks,
	.num_clk_fixed = ARRAY_SIZE(rt3883_fixed_clocks),
	.clk_factor = NULL,
	.num_clk_factor = 0,
	.clk_periph = rt5350_pherip_clks,
	.num_clk_periph = ARRAY_SIZE(rt5350_pherip_clks),
};

static const struct mtmips_clk_data rt5350_clk_data = {
	.clk_base = rt5350_clks_base,
	.num_clk_base = ARRAY_SIZE(rt5350_clks_base),
	.clk_fixed = rt3352_fixed_clocks,
	.num_clk_fixed = ARRAY_SIZE(rt3352_fixed_clocks),
	.clk_factor = NULL,
	.num_clk_factor = 0,
	.clk_periph = rt5350_pherip_clks,
	.num_clk_periph = ARRAY_SIZE(rt5350_pherip_clks),
};

static const struct mtmips_clk_data mt7620_clk_data = {
	.clk_base = mt7620_clks_base,
	.num_clk_base = ARRAY_SIZE(mt7620_clks_base),
	.clk_fixed = mt7620_fixed_clocks,
	.num_clk_fixed = ARRAY_SIZE(mt7620_fixed_clocks),
	.clk_factor = mt7620_factor_clocks,
	.num_clk_factor = ARRAY_SIZE(mt7620_factor_clocks),
	.clk_periph = mt7620_pherip_clks,
	.num_clk_periph = ARRAY_SIZE(mt7620_pherip_clks),
};

static const struct mtmips_clk_data mt76x8_clk_data = {
	.clk_base = mt76x8_clks_base,
	.num_clk_base = ARRAY_SIZE(mt76x8_clks_base),
	.clk_fixed = mt76x8_fixed_clocks,
	.num_clk_fixed = ARRAY_SIZE(mt76x8_fixed_clocks),
	.clk_factor = mt76x8_factor_clocks,
	.num_clk_factor = ARRAY_SIZE(mt76x8_factor_clocks),
	.clk_periph = mt76x8_pherip_clks,
	.num_clk_periph = ARRAY_SIZE(mt76x8_pherip_clks),
};

static const struct of_device_id mtmips_of_match[] = {
	{
		.compatible = "ralink,rt2880-reset",
		.data = NULL,
	},
	{
		.compatible = "ralink,rt2880-sysc",
		.data = &rt2880_clk_data,
	},
	{
		.compatible = "ralink,rt3050-sysc",
		.data = &rt305x_clk_data,
	},
	{
		.compatible = "ralink,rt3052-sysc",
		.data = &rt305x_clk_data,
	},
	{
		.compatible = "ralink,rt3352-sysc",
		.data = &rt3352_clk_data,
	},
	{
		.compatible = "ralink,rt3883-sysc",
		.data = &rt3883_clk_data,
	},
	{
		.compatible = "ralink,rt5350-sysc",
		.data = &rt5350_clk_data,
	},
	{
		.compatible = "ralink,mt7620-sysc",
		.data = &mt7620_clk_data,
	},
	{
		.compatible = "ralink,mt7628-sysc",
		.data = &mt76x8_clk_data,
	},
	{
		.compatible = "ralink,mt7688-sysc",
		.data = &mt76x8_clk_data,
	},
	{}
};

static void __init mtmips_clk_regs_init(struct device_node *node,
					struct mtmips_clk_priv *priv)
{
	u32 t;

	if (!of_device_is_compatible(node, "ralink,mt7620-sysc"))
		return;

	/*
	 * When the CPU goes into sleep mode, the BUS
	 * clock will be too low for USB to function properly.
	 * Adjust the busses fractional divider to fix this
	 */
	regmap_read(priv->sysc, SYSC_REG_CPU_SYS_CLKCFG, &t);
	t &= ~(CLKCFG_FDIV_MASK | CLKCFG_FFRAC_MASK);
	t |= CLKCFG_FDIV_USB_VAL | CLKCFG_FFRAC_USB_VAL;
	regmap_write(priv->sysc, SYSC_REG_CPU_SYS_CLKCFG, t);
}

static void __init mtmips_clk_init(struct device_node *node)
{
	const struct of_device_id *match;
	const struct mtmips_clk_data *data;
	struct mtmips_clk_priv *priv;
	struct clk_hw_onecell_data *clk_data;
	int ret, i, count;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return;

	priv->sysc = syscon_node_to_regmap(node);
	if (IS_ERR(priv->sysc)) {
		pr_err("Could not get sysc syscon regmap\n");
		goto free_clk_priv;
	}

	mtmips_clk_regs_init(node, priv);

	match = of_match_node(mtmips_of_match, node);
	if (WARN_ON(!match))
		return;

	data = match->data;
	priv->data = data;
	count = priv->data->num_clk_base + priv->data->num_clk_fixed +
		priv->data->num_clk_factor + priv->data->num_clk_periph;
	clk_data = kzalloc(struct_size(clk_data, hws, count), GFP_KERNEL);
	if (!clk_data)
		goto free_clk_priv;

	ret = mtmips_register_clocks(node, clk_data, priv);
	if (ret) {
		pr_err("Couldn't register top clocks\n");
		goto free_clk_data;
	}

	ret = mtmips_register_fixed_clocks(clk_data, priv);
	if (ret) {
		pr_err("Couldn't register fixed clocks\n");
		goto unreg_clk_top;
	}

	ret = mtmips_register_factor_clocks(clk_data, priv);
	if (ret) {
		pr_err("Couldn't register factor clocks\n");
		goto unreg_clk_fixed;
	}

	ret = mtmips_register_pherip_clocks(node, clk_data, priv);
	if (ret) {
		pr_err("Couldn't register peripheral clocks\n");
		goto unreg_clk_factor;
	}

	clk_data->num = count;

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (ret) {
		pr_err("Couldn't add clk hw provider\n");
		goto unreg_clk_periph;
	}

	return;

unreg_clk_periph:
	for (i = 0; i < priv->data->num_clk_periph; i++) {
		struct mtmips_clk *sclk = &priv->data->clk_periph[i];

		clk_hw_unregister(&sclk->hw);
	}

unreg_clk_factor:
	for (i = 0; i < priv->data->num_clk_factor; i++) {
		struct mtmips_clk_factor *sclk = &priv->data->clk_factor[i];

		clk_hw_unregister_fixed_factor(sclk->hw);
	}

unreg_clk_fixed:
	for (i = 0; i < priv->data->num_clk_fixed; i++) {
		struct mtmips_clk_fixed *sclk = &priv->data->clk_fixed[i];

		clk_hw_unregister_fixed_rate(sclk->hw);
	}

unreg_clk_top:
	for (i = 0; i < priv->data->num_clk_base; i++) {
		struct mtmips_clk *sclk = &priv->data->clk_base[i];

		clk_hw_unregister(&sclk->hw);
	}

free_clk_data:
	kfree(clk_data);

free_clk_priv:
	kfree(priv);
}
CLK_OF_DECLARE_DRIVER(rt2880_clk, "ralink,rt2880-sysc", mtmips_clk_init);
CLK_OF_DECLARE_DRIVER(rt3050_clk, "ralink,rt3050-sysc", mtmips_clk_init);
CLK_OF_DECLARE_DRIVER(rt3052_clk, "ralink,rt3052-sysc", mtmips_clk_init);
CLK_OF_DECLARE_DRIVER(rt3352_clk, "ralink,rt3352-sysc", mtmips_clk_init);
CLK_OF_DECLARE_DRIVER(rt3883_clk, "ralink,rt3883-sysc", mtmips_clk_init);
CLK_OF_DECLARE_DRIVER(rt5350_clk, "ralink,rt5350-sysc", mtmips_clk_init);
CLK_OF_DECLARE_DRIVER(mt7620_clk, "ralink,mt7620-sysc", mtmips_clk_init);
CLK_OF_DECLARE_DRIVER(mt7628_clk, "ralink,mt7628-sysc", mtmips_clk_init);
CLK_OF_DECLARE_DRIVER(mt7688_clk, "ralink,mt7688-sysc", mtmips_clk_init);

struct mtmips_rst {
	struct reset_controller_dev rcdev;
	struct regmap *sysc;
};

static struct mtmips_rst *to_mtmips_rst(struct reset_controller_dev *dev)
{
	return container_of(dev, struct mtmips_rst, rcdev);
}

static int mtmips_assert_device(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct mtmips_rst *data = to_mtmips_rst(rcdev);
	struct regmap *sysc = data->sysc;

	return regmap_update_bits(sysc, SYSC_REG_RESET_CTRL, BIT(id), BIT(id));
}

static int mtmips_deassert_device(struct reset_controller_dev *rcdev,
				  unsigned long id)
{
	struct mtmips_rst *data = to_mtmips_rst(rcdev);
	struct regmap *sysc = data->sysc;

	return regmap_update_bits(sysc, SYSC_REG_RESET_CTRL, BIT(id), 0);
}

static int mtmips_reset_device(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	int ret;

	ret = mtmips_assert_device(rcdev, id);
	if (ret < 0)
		return ret;

	return mtmips_deassert_device(rcdev, id);
}

static int mtmips_rst_xlate(struct reset_controller_dev *rcdev,
			    const struct of_phandle_args *reset_spec)
{
	unsigned long id = reset_spec->args[0];

	if (id == 0 || id >= rcdev->nr_resets)
		return -EINVAL;

	return id;
}

static const struct reset_control_ops reset_ops = {
	.reset = mtmips_reset_device,
	.assert = mtmips_assert_device,
	.deassert = mtmips_deassert_device
};

static int mtmips_reset_init(struct device *dev, struct regmap *sysc)
{
	struct mtmips_rst *rst_data;

	rst_data = devm_kzalloc(dev, sizeof(*rst_data), GFP_KERNEL);
	if (!rst_data)
		return -ENOMEM;

	rst_data->sysc = sysc;
	rst_data->rcdev.ops = &reset_ops;
	rst_data->rcdev.owner = THIS_MODULE;
	rst_data->rcdev.nr_resets = 32;
	rst_data->rcdev.of_reset_n_cells = 1;
	rst_data->rcdev.of_xlate = mtmips_rst_xlate;
	rst_data->rcdev.of_node = dev_of_node(dev);

	return devm_reset_controller_register(dev, &rst_data->rcdev);
}

static int mtmips_clk_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct mtmips_clk_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->sysc = syscon_node_to_regmap(np);
	if (IS_ERR(priv->sysc))
		return dev_err_probe(dev, PTR_ERR(priv->sysc),
				     "Could not get sysc syscon regmap\n");

	ret = mtmips_reset_init(dev, priv->sysc);
	if (ret)
		return dev_err_probe(dev, ret, "Could not init reset controller\n");

	return 0;
}

static struct platform_driver mtmips_clk_driver = {
	.probe = mtmips_clk_probe,
	.driver = {
		.name = "mtmips-clk",
		.of_match_table = mtmips_of_match,
	},
};

static int __init mtmips_clk_reset_init(void)
{
	return platform_driver_register(&mtmips_clk_driver);
}
arch_initcall(mtmips_clk_reset_init);
