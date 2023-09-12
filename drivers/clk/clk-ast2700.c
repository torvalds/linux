// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright ASPEED Technology

#define pr_fmt(fmt) "clk-ast2600: " fmt

#include <linux/clk-provider.h>

#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <dt-bindings/clock/aspeed,ast2700-clk.h>
#include <dt-bindings/reset/aspeed,ast2700-reset.h>

#include "clk-aspeed.h"

#define AST2700_CLK_25MHZ 25000000
#define AST2700_CLK_24MHZ 24000000
#define AST2700_CLK_192MHZ 192000000

/* CPU Die */
#define AST2700_CPU_RESET_CTRL 0x00
#define AST2700_CPU_RESET_CTRL2 0x20
#define AST2700_CPU_CLK_STOP 0x40
#define AST2700_CPU_CLK_SEL1 0x80
#define AST2700_CPU_CLK_SEL2 0x84
#define UART_DIV13_EN BIT(30)
#define AST2700_CPU_HPLL_PARAM 0x100
#define AST2700_CPU_DPLL_PARAM 0x108
#define AST2700_CPU_MPLL_PARAM 0x110
#define AST2700_CPU_D1CLK_PARAM 0x120
#define AST2700_CPU_D2CLK_PARAM 0x130
#define AST2700_CPU_CRT1CLK_PARAM 0x140
#define AST2700_CPU_CRT2CLK_PARAM 0x150
#define AST2700_CPU_MPHYCLK_PARAM 0x160

/* IO Die */
#define AST2700_IO_CLK_STOP 0x40
#define AST2700_IO_CLK_STOP2 0x60

#define AST2700_IO_CLK_SEL1 0x80
#define AST2700_IO_CLK_SEL2 0x84
#define UXCLK_MASK GENMASK(1, 0)
#define HUXCLK_MASK GENMASK(4, 3)

#define AST2700_IO_HPLL_PARAM 0x100
#define AST2700_IO_APLL_PARAM 0x110
#define AST2700_IO_DPLL_PARAM 0x120
#define AST2700_IO_UXCLK_CTRL 0x130
#define AST2700_IO_HUXCLK_CTRL 0x140

/* Globally visible clocks */
static DEFINE_SPINLOCK(ast2700_clk_lock);

/* Division of RGMII Clock */
static const struct clk_div_table ast2700_rgmii_div_table[] = {
	{ 0x0, 4 },
	{ 0x1, 4 },
	{ 0x2, 6 },
	{ 0x3, 8 },
	{ 0x4, 10 },
	{ 0x5, 12 },
	{ 0x6, 14 },
	{ 0x7, 16 },
	{ 0 }
};

/* Division of RMII Clock */
static const struct clk_div_table ast2700_rmii_div_table[] = {
	{ 0x0, 8 },
	{ 0x1, 8 },
	{ 0x2, 12 },
	{ 0x3, 16 },
	{ 0x4, 20 },
	{ 0x5, 24 },
	{ 0x6, 28 },
	{ 0x7, 32 },
	{ 0 }
};

/* Division of PCLK */
static const struct clk_div_table ast2700_pclk_div_table[] = {
	{ 0x0, 2 },
	{ 0x1, 4 },
	{ 0x2, 6 },
	{ 0x3, 8 },
	{ 0x4, 10 },
	{ 0x5, 12 },
	{ 0x6, 14 },
	{ 0x7, 16 },
	{ 0 }
};

/* Division of HCLK/SDIO/MAC/apll_divn CLK */
static const struct clk_div_table ast2700_clk_div_table[] = {
	{ 0x0, 2 },
	{ 0x1, 2 },
	{ 0x2, 3 },
	{ 0x3, 4 },
	{ 0x4, 5 },
	{ 0x5, 6 },
	{ 0x6, 7 },
	{ 0x7, 8 },
	{ 0 }
};

/* HPLL/DPLL: 2000Mhz(default) */
struct clk_hw *ast2700_cpu_hw_pll(const char *name, const char *parent_name, u32 val)
{
	unsigned int mult, div;

	if (val & BIT(24)) {
		/* Pass through mode */
		mult = div = 1;
	} else {
		/* F = CLKIN(25MHz) * [(M+1) / 2(N+1)] / (P+1) */
		u32 m = val & 0x1fff;
		u32 n = (val >> 13) & 0x3f;
		u32 p = (val >> 19) & 0xf;

		mult = (m + 1) / (2 * (n + 1));
		div = (p + 1);
	}

	return clk_hw_register_fixed_factor(NULL, name, parent_name, 0, mult, div);
};

/* MPLL 1600Mhz(default) */
struct clk_hw *ast2700_calc_mpll(const char *name, const char *parent_name, u32 val)
{
	unsigned int mult, div;

	if (val & BIT(24)) {
		/* Pass through mode */
		mult = div = 1;
	} else {
		/* F = CLKIN(25MHz) * [CLKF/(CLKR+1)] /(CLKOD+1) */
		u32 m = val & 0x1fff;
		u32 n = (val >> 13) & 0x3f;
		u32 p = (val >> 19) & 0xf;

		mult = m / (n + 1);
		div = (p + 1);
	}
	return clk_hw_register_fixed_factor(NULL, name, parent_name, 0, mult, div);
};

static struct clk_hw *ast2700_calc_uclk(const char *name, u32 val)
{
	unsigned int mult, div;

	/* UARTCLK = UXCLK * R / (N * 2) */
	u32 r = val & 0x3f;
	u32 n = (val >> 8) & 0x3ff;

	mult = r;
	div = n * 2;

	return clk_hw_register_fixed_factor(NULL, name, "uxclk", 0, mult, div);
};

static struct clk_hw *ast2700_calc_huclk(const char *name, u32 val)
{
	unsigned int mult, div;

	/* UARTCLK = UXCLK * R / (N * 2) */
	u32 r = val & 0x3f;
	u32 n = (val >> 8) & 0x3ff;

	mult = r;
	div = n * 2;

	return clk_hw_register_fixed_factor(NULL, name, "huxclk", 0, mult, div);
};

struct clk_hw *ast2700_calc_io_pll(const char *name, const char *parent_name, u32 val)
{
	unsigned int mult, div;

	if (val & BIT(24)) {
		/* Pass through mode */
		mult = div = 1;
	} else {
		/* F = 25Mhz * [(M + 1) / (n + 1)] / (p + 1) */
		u32 m = val & 0x1fff;
		u32 n = (val >> 13) & 0x3f;
		u32 p = (val >> 19) & 0xf;

		mult = (m + 1) / (n + 1);
		div = (p + 1);
	}
	return clk_hw_register_fixed_factor(NULL, name, parent_name, 0, mult, div);
};

static int ast2700_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	u32 clk = BIT(gate->bit_idx);
	u32 reg;

	reg = readl(gate->reg);

	return !(reg & clk);
}

static int ast2700_clk_enable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	u32 clk = BIT(gate->bit_idx);

	if (readl(gate->reg) & clk)
		writel(clk, gate->reg + 0x04);

	return 0;
}

static void ast2700_clk_disable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	u32 clk = BIT(gate->bit_idx);

	/* Clock is set to enable, so use write to set register */
	writel(clk, gate->reg);
}

static const struct clk_ops ast2700_clk_gate_ops = {
	.enable = ast2700_clk_enable,
	.disable = ast2700_clk_disable,
	.is_enabled = ast2700_clk_is_enabled,
};

static struct clk_hw *ast2700_clk_hw_register_gate(struct device *dev, const char *name,
						   const char *parent_name, unsigned long flags,
						   void __iomem *reg, u8 clock_idx,
						   u8 clk_gate_flags, spinlock_t *lock)
{
	struct clk_gate *gate;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret = -EINVAL;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &ast2700_clk_gate_ops;
	init.flags = flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	gate->reg = reg;
	gate->bit_idx = clock_idx;
	gate->flags = clk_gate_flags;
	gate->lock = lock;
	gate->hw.init = &init;

	hw = &gate->hw;
	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(gate);
		hw = ERR_PTR(ret);
	}

	return hw;
}

struct aspeed_rst_data {
	struct reset_controller_dev rcdev;
	void __iomem *base;
};

#define to_rc_data(p) container_of(p, struct aspeed_rst_data, rcdev)

static int aspeed_rst_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct aspeed_rst_data *rc = to_rc_data(rcdev);
	u32 rst = BIT(id % 32);
	u32 reg = id >= 32 ? AST2700_CPU_RESET_CTRL2 : AST2700_CPU_RESET_CTRL;

	/* Use set to clear register */
	writel(rst, rc->base + reg + 0x04);
	return 0;
}

static int aspeed_rst_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct aspeed_rst_data *rc = to_rc_data(rcdev);
	u32 rst = BIT(id % 32);
	u32 reg = id >= 32 ? AST2700_CPU_RESET_CTRL2 : AST2700_CPU_RESET_CTRL;

	writel(rst, rc->base + reg);
	return 0;
}

static int aspeed_rst_status(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct aspeed_rst_data *rc = to_rc_data(rcdev);
	u32 rst = BIT(id % 32);
	u32 reg = id >= 32 ? AST2700_CPU_RESET_CTRL2 : AST2700_CPU_RESET_CTRL;

	return (readl(rc->base + reg) & rst);
}

static const struct reset_control_ops aspeed_rst_ops = {
	.assert = aspeed_rst_assert,
	.deassert = aspeed_rst_deassert,
	.status = aspeed_rst_status,
};

static int ast2700_reset_init(struct platform_device *pdev, void __iomem *base,
			      unsigned int nr_resets)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct aspeed_rst_data *rstc;

	rstc = devm_kzalloc(&pdev->dev, sizeof(*rstc), GFP_KERNEL);
	if (!rstc)
		return -ENOMEM;

	rstc->rcdev.owner = THIS_MODULE;
	rstc->rcdev.ops = &aspeed_rst_ops;
	rstc->rcdev.of_node = np;
	rstc->rcdev.nr_resets = nr_resets;
	rstc->base = base;

	return devm_reset_controller_register(dev, &rstc->rcdev);
}

static const char *const sdclk_sel[] = {
	"io-hpll_divn",
	"io-apll_divn",
};

static const char *const cpu_uartclk_sel[] = {
	"cpu-clk24Mhz",
	"cpu-clk192Mhz",
};

static const char *const uartclk_sel[] = {
	"uxclk",
	"huxclk",
};

static const char *const uxclk_sel[] = {
	"apll_div4",
	"apll_div2",
	"apll",
	"io-hpll",
};

static int ast2700_io_clk_init(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device *dev = &pdev->dev;
	void __iomem *clk_base;
	struct clk_hw **clks;
	struct clk_hw *hw;
	u32 val;
	int ret;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, AST2700_IO_NUM_CLKS), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = AST2700_IO_NUM_CLKS;
	clks = clk_data->hws;

	clk_base = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(clk_base)))
		return PTR_ERR(clk_base);

	hw = clk_hw_register_fixed_rate(dev, "io-clkin", NULL, 0, AST2700_CLK_25MHZ);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	clks[AST2700_IO_CLKIN] = hw;

	/* HPLL 1000Mhz */
	val = readl(clk_base + AST2700_IO_HPLL_PARAM);
	clks[AST2700_IO_CLK_HPLL] = ast2700_calc_io_pll("io-hpll", "io-clkin", val);

	/* HPLL 800Mhz */
	val = readl(clk_base + AST2700_IO_APLL_PARAM);
	clks[AST2700_IO_CLK_APLL] = ast2700_calc_io_pll("apll", "io-clkin", val);

	clks[AST2700_IO_CLK_APLL_DIV2] =
		clk_hw_register_fixed_factor(dev, "apll_div2", "apll", 0, 1, 2);

	clks[AST2700_IO_CLK_APLL_DIV4] =
		clk_hw_register_fixed_factor(dev, "apll_div4", "apll", 0, 1, 4);

	val = readl(clk_base + AST2700_IO_DPLL_PARAM);
	clks[AST2700_IO_CLK_DPLL] = ast2700_calc_io_pll("dpll", "io-clkin", val);

	/* uxclk mux selection */
	clks[AST2700_IO_CLK_UXCLK] =
		clk_hw_register_mux(dev, "uxclk", uxclk_sel, ARRAY_SIZE(uxclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL2,
				    0, 2, 0, &ast2700_clk_lock);

	val = readl(clk_base + AST2700_IO_UXCLK_CTRL);
	clks[AST2700_IO_CLK_UARTCLK] = ast2700_calc_uclk("uartclk", val);

	/* huxclk mux selection */
	clks[AST2700_IO_CLK_HUXCLK] =
		clk_hw_register_mux(dev, "huxclk", uxclk_sel, ARRAY_SIZE(uxclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL2,
				    3, 2, 0, &ast2700_clk_lock);

	val = readl(clk_base + AST2700_IO_HUXCLK_CTRL);
	clks[AST2700_IO_CLK_HUARTCLK] = ast2700_calc_huclk("huartclk", val);

	/* AHB CLK = 200Mhz */
	clks[AST2700_IO_CLK_AHB] =
		clk_hw_register_divider_table(dev, "io-ahb", "io-hpll",
					      0, clk_base + AST2700_CPU_CLK_SEL2,
					      20, 3, 0, ast2700_clk_div_table, &ast2700_clk_lock);

	/* APB CLK = 100Mhz */
	clks[AST2700_IO_CLK_APB] =
		clk_hw_register_divider_table(dev, "io-apb", "io-hpll",
					      0, clk_base + AST2700_CPU_CLK_SEL1,
					      18, 3, 0, ast2700_pclk_div_table, &ast2700_clk_lock);

	//rmii
	clks[AST2700_IO_CLK_RMII] =
		clk_hw_register_divider_table(dev, "rmii", "io-hpll",
					      0, clk_base + AST2700_CPU_CLK_SEL2,
					      21, 3, 0, ast2700_rmii_div_table, &ast2700_clk_lock);

	//rgmii
	clks[AST2700_IO_CLK_RGMII] =
		clk_hw_register_divider_table(dev, "rgmii", "io-hpll",
					      0, clk_base + AST2700_CPU_CLK_SEL2,
					      25, 3, 0, ast2700_rgmii_div_table, &ast2700_clk_lock);

	//mac hclk
	clks[AST2700_IO_CLK_MACHCLK] =
		clk_hw_register_divider_table(dev, "machclk", "io-hpll",
					      0, clk_base + AST2700_CPU_CLK_SEL2,
					      29, 3, 0, ast2700_clk_div_table, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_LCLK0] =
		ast2700_clk_hw_register_gate(NULL, "lclk0-gate", NULL,
					     CLK_IS_CRITICAL, clk_base + AST2700_IO_CLK_STOP,
					     0, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_LCLK0] =
		ast2700_clk_hw_register_gate(NULL, "lclk1-gate", NULL,
					     CLK_IS_CRITICAL, clk_base + AST2700_IO_CLK_STOP,
					     1, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_ESPI0CLK] =
		ast2700_clk_hw_register_gate(NULL, "espi0clk-gate", NULL,
					     CLK_IS_CRITICAL, clk_base + AST2700_IO_CLK_STOP,
					     2, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_ESPI1CLK] =
		ast2700_clk_hw_register_gate(NULL, "espi1clk-gate", NULL,
					     CLK_IS_CRITICAL, clk_base + AST2700_IO_CLK_STOP,
					     3, 0, &ast2700_clk_lock);

	//sd pll divn
	clks[AST2700_IO_CLK_HPLL_DIVN] =
		clk_hw_register_divider_table(dev, "io-hpll_divn", "io-hpll",
					      0, clk_base + AST2700_CPU_CLK_SEL2,
					      20, 3, 0, ast2700_clk_div_table, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_APLL_DIVN] =
		clk_hw_register_divider_table(dev, "io-apll_divn", "apll",
					      0, clk_base + AST2700_CPU_CLK_SEL2,
					      8, 3, 0, ast2700_clk_div_table, &ast2700_clk_lock);

	//sd clk
	clks[AST2700_IO_CLK_SDCLK] =
		clk_hw_register_mux(dev, "sdclk", sdclk_sel, ARRAY_SIZE(sdclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL1,
				    13, 1, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_SDCLK] =
		ast2700_clk_hw_register_gate(NULL, "sdclk-gate", "sdclk",
					     0, clk_base + AST2700_IO_CLK_STOP,
					     4, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_REFCLK] =
		ast2700_clk_hw_register_gate(NULL, "io-refclk-gate", NULL,
					     CLK_IS_CRITICAL, clk_base + AST2700_IO_CLK_STOP,
					     6, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_LPCHCLK] =
		ast2700_clk_hw_register_gate(NULL, "lpchclk-gate", NULL,
					     CLK_IS_CRITICAL, clk_base + AST2700_IO_CLK_STOP,
					     7, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_MAC0CLK] =
		ast2700_clk_hw_register_gate(NULL, "mac0clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     8, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_MAC1CLK] =
		ast2700_clk_hw_register_gate(NULL, "mac1clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     9, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_MAC2CLK] =
		ast2700_clk_hw_register_gate(NULL, "mac2clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     10, 0, &ast2700_clk_lock);

	//UART0
	clks[AST2700_IO_CLK_UART0] =
		clk_hw_register_mux(dev, "uart0clk", uartclk_sel, ARRAY_SIZE(uartclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL1,
				    0, 1, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_UART0CLK] =
		ast2700_clk_hw_register_gate(NULL, "uart0clk-gate", "uart0clk",
					     0, clk_base + AST2700_IO_CLK_STOP,
					     11, 0, &ast2700_clk_lock);

	//UART1
	clks[AST2700_IO_CLK_UART1] =
		clk_hw_register_mux(dev, "uart1clk", uartclk_sel, ARRAY_SIZE(uartclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL1,
				    1, 1, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_UART1CLK] =
		ast2700_clk_hw_register_gate(NULL, "uart1clk-gate", "uart1clk",
					     0, clk_base + AST2700_IO_CLK_STOP,
					     12, 0, &ast2700_clk_lock);

	//UART2
	clks[AST2700_IO_CLK_UART2] =
		clk_hw_register_mux(dev, "uart2clk", uartclk_sel, ARRAY_SIZE(uartclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL1,
				    2, 1, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_UART2CLK] =
		ast2700_clk_hw_register_gate(NULL, "uart2clk-gate", "uart2clk",
					     0, clk_base + AST2700_IO_CLK_STOP,
					     13, 0, &ast2700_clk_lock);

	//UART3
	clks[AST2700_IO_CLK_UART3] =
		clk_hw_register_mux(dev, "uart3clk", uartclk_sel, ARRAY_SIZE(uartclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL1,
				    3, 1, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_UART3CLK] =
		ast2700_clk_hw_register_gate(NULL, "uart3clk-gate", "uart3clk",
					     0, clk_base + AST2700_IO_CLK_STOP,
					     14, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C0CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c0clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     16, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C1CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c1clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     17, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C2CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c2clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     18, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C3CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c3clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     19, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C4CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c4clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     20, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C5CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c5clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     21, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C6CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c6clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     22, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C7CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c7clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     23, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C8CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c8clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     24, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C9CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c9clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     25, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C10CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c10clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     26, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C11CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c11clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     27, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C12CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c12clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     28, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C13CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c13clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     29, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C14CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c14clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     30, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_I3C15CLK] =
		ast2700_clk_hw_register_gate(NULL, "i3c15clk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP,
					     31, 0, &ast2700_clk_lock);

	/*clk stop 2 */
	//UART5
	clks[AST2700_IO_CLK_UART5] =
		clk_hw_register_mux(dev, "uart5clk", uartclk_sel, ARRAY_SIZE(uartclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL1,
				    5, 1, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_UART5CLK] =
		ast2700_clk_hw_register_gate(NULL, "uart5clk-gate", "uart5clk",
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     0, 0, &ast2700_clk_lock);

	//UART6
	clks[AST2700_IO_CLK_UART6] =
		clk_hw_register_mux(dev, "uart6clk", uartclk_sel, ARRAY_SIZE(uartclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL1,
				    6, 1, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_UART6CLK] =
		ast2700_clk_hw_register_gate(NULL, "uart6clk-gate", "uart6clk",
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     1, 0, &ast2700_clk_lock);

	//UART7
	clks[AST2700_IO_CLK_UART7] =
		clk_hw_register_mux(dev, "uart7clk", uartclk_sel, ARRAY_SIZE(uartclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL1,
				    7, 1, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_UART7CLK] =
		ast2700_clk_hw_register_gate(NULL, "uart7clk-gate", "uart7clk",
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     2, 0, &ast2700_clk_lock);

	//UART8
	clks[AST2700_IO_CLK_UART8] =
		clk_hw_register_mux(dev, "uart8clk", uartclk_sel, ARRAY_SIZE(uartclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL1,
				    8, 1, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_UART8CLK] =
		ast2700_clk_hw_register_gate(NULL, "uart8clk-gate", "uart8clk",
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     3, 0, &ast2700_clk_lock);

	//UART9
	clks[AST2700_IO_CLK_UART9] =
		clk_hw_register_mux(dev, "uart9clk", uartclk_sel, ARRAY_SIZE(uartclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL1,
				    9, 1, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_UART9CLK] =
		ast2700_clk_hw_register_gate(NULL, "uart9clk-gate", "uart9clk",
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     4, 0, &ast2700_clk_lock);

	//UART10
	clks[AST2700_IO_CLK_UART10] =
		clk_hw_register_mux(dev, "uart10clk", uartclk_sel, ARRAY_SIZE(uartclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL1,
				    10, 1, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_UART10CLK] =
		ast2700_clk_hw_register_gate(NULL, "uart10clk-gate", "uart10clk",
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     5, 0, &ast2700_clk_lock);

	//UART11
	clks[AST2700_IO_CLK_UART11] =
		clk_hw_register_mux(dev, "uart11clk", uartclk_sel, ARRAY_SIZE(uartclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL1,
				    11, 1, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_UART11CLK] =
		ast2700_clk_hw_register_gate(NULL, "uart11clk-gate", "uart11clks",
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     6, 0, &ast2700_clk_lock);

	//uart12: call bmc uart
	clks[AST2700_IO_CLK_UART12] =
		clk_hw_register_mux(dev, "uart12clk", uartclk_sel, ARRAY_SIZE(uartclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL1,
				    12, 1, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_UART12CLK] =
		ast2700_clk_hw_register_gate(NULL, "uart12clk-gate", "uart12clk",
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     7, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_FSICLK] =
		ast2700_clk_hw_register_gate(NULL, "fsiclk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     8, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_LTPIPHYCLK] =
		ast2700_clk_hw_register_gate(NULL, "ltpiphyclk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     9, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_LTPICLK] =
		ast2700_clk_hw_register_gate(NULL, "ltpiclk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     10, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_VGALCLK] =
		ast2700_clk_hw_register_gate(NULL, "vgalclk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     11, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_USBUARTCLK] =
		ast2700_clk_hw_register_gate(NULL, "usbuartclk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     12, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_CANCLK] =
		ast2700_clk_hw_register_gate(NULL, "canclk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     13, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_PCICLK] =
		ast2700_clk_hw_register_gate(NULL, "pciclk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     14, 0, &ast2700_clk_lock);

	clks[AST2700_IO_CLK_GATE_SLICLK] =
		ast2700_clk_hw_register_gate(NULL, "sliclk-gate", NULL,
					     0, clk_base + AST2700_IO_CLK_STOP2,
					     15, 0, &ast2700_clk_lock);

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
	if (ret)
		return ret;

	return ast2700_reset_init(pdev, clk_base, ASPEED_IO_RESET_NUMS);
};

static int ast2700_cpu_clk_init(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device *dev = &pdev->dev;
	void __iomem *clk_base;
	struct clk_hw **clks;
	int ret;
	int div, axi_div, ahb_div, apb_div;
	u32 val;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, AST2700_CPU_NUM_CLKS), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = AST2700_CPU_NUM_CLKS;
	clks = clk_data->hws;

	clk_base = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(clk_base)))
		return PTR_ERR(clk_base);

	//refclk
	clks[AST2700_CPU_CLKIN] =
		clk_hw_register_fixed_rate(dev, "cpu-clkin", NULL, 0, AST2700_CLK_25MHZ);

	clks[AST2700_CPU_CLK_24M] =
		clk_hw_register_fixed_rate(dev, "cpu-clk24Mhz", NULL, 0, AST2700_CLK_24MHZ);

	clks[AST2700_CPU_CLK_192M] =
		clk_hw_register_fixed_rate(dev, "cpu-clk192Mhz", NULL, 0, AST2700_CLK_192MHZ);

	//hpll
	val = readl(clk_base + AST2700_CPU_HPLL_PARAM);
	clks[AST2700_CPU_CLK_HPLL] = ast2700_cpu_hw_pll("cpu-hpll", "cpu-clkin", val);

	//dpll
	val = readl(clk_base + AST2700_CPU_DPLL_PARAM);
	clks[AST2700_CPU_CLK_DPLL] = ast2700_cpu_hw_pll("dpll", "cpu-clkin", val);

	//mpll
	val = readl(clk_base + AST2700_CPU_MPLL_PARAM);
	clks[AST2700_CPU_CLK_MPLL] = ast2700_calc_mpll("mpll", "cpu-clkin", val);

	//d1clk
	val = readl(clk_base + AST2700_CPU_D1CLK_PARAM);
	clks[AST2700_CPU_CLK_D1CLK] = ast2700_cpu_hw_pll("d1clk", "cpu-clkin", val);

	//d2clk
	val = readl(clk_base + AST2700_CPU_D2CLK_PARAM);
	clks[AST2700_CPU_CLK_D2CLK] = ast2700_cpu_hw_pll("d2clk", "cpu-clkin", val);

	val = readl(clk_base + AST2700_CPU_CRT1CLK_PARAM);
	clks[AST2700_CPU_CLK_CRT1] = ast2700_cpu_hw_pll("crt1clk", "cpu-clkin", val);

	val = readl(clk_base + AST2700_CPU_CRT2CLK_PARAM);
	clks[AST2700_CPU_CLK_CRT2] = ast2700_cpu_hw_pll("crt2clk", "cpu-clkin", val);

	val = readl(clk_base + AST2700_CPU_MPHYCLK_PARAM);
	clks[AST2700_CPU_CLK_MPHY] = ast2700_cpu_hw_pll("mphyclk", "cpu-clkin", val);

	//fixed-factor
	/* AXI CLK MPLL/2 = 800Mhz */
	axi_div = 2;
	clks[AST2700_CPU_CLK_AXI] =
		clk_hw_register_fixed_factor(NULL, "axi", "mpll", 0, 1, axi_div);

	/* AHB CLK MPLL/4 = 400Mhz */
	ahb_div = 4;
	clks[AST2700_CPU_CLK_AHB] =
		clk_hw_register_fixed_factor(NULL, "cpu-ahb", "mpll", 0, 1, ahb_div);

	/* APB CLK MPLL/16 = 100Mhz */
	apb_div = 4;
	clks[AST2700_CPU_CLK_APB] =
		clk_hw_register_fixed_factor(NULL, "cpu-apb", "mpll", 0, 1, apb_div);

	clks[AST2700_CPU_CLK_GATE_MCLK] =
		ast2700_clk_hw_register_gate(NULL, "mclk", "mpll",
					     CLK_IS_CRITICAL, clk_base + AST2700_CPU_CLK_STOP,
					     0, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_ECLK] =
		ast2700_clk_hw_register_gate(NULL, "eclk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     1, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_GCLK] =
		ast2700_clk_hw_register_gate(NULL, "gclk", NULL,
					     CLK_IS_CRITICAL, clk_base + AST2700_CPU_CLK_STOP,
					     2, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_VCLK] =
		ast2700_clk_hw_register_gate(NULL, "vclk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     3, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_BCLK] =
		ast2700_clk_hw_register_gate(NULL, "bclk", NULL,
					     CLK_IS_CRITICAL, clk_base + AST2700_CPU_CLK_STOP,
					     4, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_D1CLK] =
		ast2700_clk_hw_register_gate(NULL, "d1clk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     5, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_REFCLK] =
		ast2700_clk_hw_register_gate(NULL, "cpu-refclk-gate", "cpu-clkin",
					     CLK_IS_CRITICAL, clk_base + AST2700_CPU_CLK_STOP,
					     6, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_USB0CLK] =
		ast2700_clk_hw_register_gate(NULL, "usb0clk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     7, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_USB1CLK] =
		ast2700_clk_hw_register_gate(NULL, "usb1clk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     9, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_D2CLK] =
		ast2700_clk_hw_register_gate(NULL, "d2clk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     10, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_YCLK] =
		ast2700_clk_hw_register_gate(NULL, "yclk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     13, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_USB2CLK] =
		ast2700_clk_hw_register_gate(NULL, "usb2clk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     14, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_UART] =
		clk_hw_register_mux(dev, "cpu-uartclk", cpu_uartclk_sel, ARRAY_SIZE(cpu_uartclk_sel),
				    0, clk_base + AST2700_CPU_CLK_SEL2,
				    14, 1, 0, &ast2700_clk_lock);

	if (readl(clk_base + AST2700_CPU_CLK_SEL2) & UART_DIV13_EN)
		div = 13;
	else
		div = 1;

	clks[AST2700_CPU_CLK_UART4] =
		clk_hw_register_fixed_factor(NULL, "uart4clk", "cpu-uartclk", 0, 1, div);

	clks[AST2700_CPU_CLK_GATE_UART4CLK] =
		ast2700_clk_hw_register_gate(NULL, "uart4clk-gate", "uart4clk",
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     15, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_DACCLK] =
		ast2700_clk_hw_register_gate(NULL, "dacclk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     17, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_CRT1CLK] =
		ast2700_clk_hw_register_gate(NULL, "crt1clk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     20, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_CRT2CLK] =
		ast2700_clk_hw_register_gate(NULL, "crt2clk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     21, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_ECCCLK] =
		ast2700_clk_hw_register_gate(NULL, "eccclk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     23, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_RSACLK] =
		ast2700_clk_hw_register_gate(NULL, "rsaclk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     24, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_RVAS0CLK] =
		ast2700_clk_hw_register_gate(NULL, "rvasclk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     25, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_UFSCLK] =
		ast2700_clk_hw_register_gate(NULL, "ufsclk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     26, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_EMMCCLK] =
		ast2700_clk_hw_register_gate(NULL, "emmcclk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     27, 0, &ast2700_clk_lock);

	clks[AST2700_CPU_CLK_GATE_RVAS1CLK] =
		ast2700_clk_hw_register_gate(NULL, "rvas2clk", NULL,
					     0, clk_base + AST2700_CPU_CLK_STOP,
					     28, 0, &ast2700_clk_lock);

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
	if (ret)
		return ret;

	return ast2700_reset_init(pdev, clk_base, ASPEED_CPU_RESET_NUMS);
};

static int ast2700_clk_probe(struct platform_device *pdev)
{
	int (*probe)(struct platform_device *pdev);

	probe = of_device_get_match_data(&pdev->dev);

	if (probe)
		return probe(pdev);

	return 0;
}

static const struct of_device_id ast2700_clk_dt_ids[] = {
	{ .compatible = "aspeed,ast2700_cpu-clk", .data = ast2700_cpu_clk_init },
	{ .compatible = "aspeed,ast2700_io-clk", .data = ast2700_io_clk_init },
	{}
};

static struct platform_driver ast2700_clk_driver = {
	.probe	= ast2700_clk_probe,
	.driver = {
		.name = "ast2700_clk",
		.of_match_table = ast2700_clk_dt_ids,
		.suppress_bind_attrs = true,
	},
};

static int __init ast2700_clk_driver_init(void)
{
	return platform_driver_register(&ast2700_clk_driver);
}
core_initcall(ast2700_clk_driver_init);
