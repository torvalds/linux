// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright ASPEED Technology

#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/aspeed,ast1700-clk.h>

#define AST1700_CLK_25MHZ 25000000
#define AST1700_CLK_24MHZ 24000000
#define AST1700_CLK_192MHZ 192000000
/* IO Die */
#define AST1700_CLK_STOP 0x00
#define AST1700_CLK_STOP2 0x20
#define AST1700_CLK_SEL1 0x40
#define AST1700_CLK_SEL2 0x44
#define UXCLK_MASK GENMASK(1, 0)
#define HUXCLK_MASK GENMASK(4, 3)
#define AST1700_HPLL_PARAM 0xC0
#define AST1700_APLL_PARAM 0xD0
#define AST1700_DPLL_PARAM 0xE0
#define AST1700_UXCLK_CTRL 0xF0
#define AST1700_HUXCLK_CTRL 0x100

static DEFINE_IDA(ast1700_clk_ida);

/* Globally visible clocks */
static DEFINE_SPINLOCK(ast1700_clk_lock);

/* Division of RGMII Clock */
static const struct clk_div_table ast1700_rgmii_div_table[] = {
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
static const struct clk_div_table ast1700_rmii_div_table[] = {
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

/* Division of HCLK/SDIO/MAC/apll_divn CLK */
static const struct clk_div_table ast1700_clk_div_table[] = {
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

/* Division of PCLK/EMMC CLK */
static const struct clk_div_table ast1700_clk_div_table2[] = {
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

static struct clk_hw *AST1700_calc_uclk(const char *name, u32 val)
{
	unsigned int mult, div;

	/* UARTCLK = UXCLK * R / (N * 2) */
	u32 r = val & 0x3f;
	u32 n = (val >> 8) & 0x3ff;

	mult = r;
	div = n * 2;

	return clk_hw_register_fixed_factor(NULL, name, "ast1700-uxclk", 0, mult, div);
};

static struct clk_hw *AST1700_calc_huclk(const char *name, u32 val)
{
	unsigned int mult, div;

	/* UARTCLK = UXCLK * R / (N * 2) */
	u32 r = val & 0x3f;
	u32 n = (val >> 8) & 0x3ff;

	mult = r;
	div = n * 2;

	return clk_hw_register_fixed_factor(NULL, name, "ast1700-huxclk", 0, mult, div);
};

struct clk_hw *AST1700_calc_pll(const char *name, const char *parent_name, u32 val)
{
	unsigned int mult, div;

	if (val & BIT(24)) {
		/* Pass through mode */
		mult = 1;
		div = 1;
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

static int AST1700_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	u32 clk = BIT(gate->bit_idx);
	u32 reg;

	reg = readl(gate->reg);

	return !(reg & clk);
}

static int AST1700_clk_enable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	u32 clk = BIT(gate->bit_idx);

	if (readl(gate->reg) & clk)
		writel(clk, gate->reg + 0x04);

	return 0;
}

static void AST1700_clk_disable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	u32 clk = BIT(gate->bit_idx);

	/* Clock is set to enable, so use write to set register */
	writel(clk, gate->reg);
}

static const struct clk_ops AST1700_clk_gate_ops = {
	.enable = AST1700_clk_enable,
	.disable = AST1700_clk_disable,
	.is_enabled = AST1700_clk_is_enabled,
};

static struct clk_hw *AST1700_clk_hw_register_gate(struct device *dev, const char *name,
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
	init.ops = &AST1700_clk_gate_ops;
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

static const char *const sdclk_sel0[] = {
	"ast1700_0-hpll_divn",
	"ast1700_0-apll_divn",
};

static const char *const sdclk_sel1[] = {
	"ast1700_1-hpll_divn",
	"ast1700_1-apll_divn",
};

static const char *const uartclk_sel0[] = {
	"ast1700_0-uartxclk",
	"ast1700_0-huartxclk",
};

static const char *const uartclk_sel1[] = {
	"ast1700_1-uartxclk",
	"ast1700_1-huartxclk",
};

static const char *const uxclk_sel0[] = {
	"ast1700_0-apll_div4",
	"ast1700_0-apll_div2",
	"ast1700_0-apll",
	"ast1700_0-hpll",
};

static const char *const uxclk_sel1[] = {
	"ast1700_1-apll_div4",
	"ast1700_1-apll_div2",
	"ast1700_1-apll",
	"ast1700_1-hpll",
};

#define CREATE_CLK_NAME(id, suffix) kasprintf(GFP_KERNEL, "ast1700_%d-%s", id, suffix)

static int AST1700_clk_init(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device *dev = &pdev->dev;
	u32 uart_clk_source = 0;
	void __iomem *clk_base;
	struct clk_hw **clks;
	struct clk_hw *hw;
	u32 val;
	int ret;

	int id = ida_simple_get(&ast1700_clk_ida, 0, 0, GFP_KERNEL);

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, AST1700_NUM_CLKS), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = AST1700_NUM_CLKS;
	clks = clk_data->hws;

	clk_base = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(clk_base)))
		return PTR_ERR(clk_base);

	hw = clk_hw_register_fixed_rate(dev, CREATE_CLK_NAME(id, "clkin"), NULL, 0, AST1700_CLK_25MHZ);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	clks[AST1700_CLKIN] = hw;

	/* HPLL 1000Mhz */
	val = readl(clk_base + AST1700_HPLL_PARAM);
	clks[AST1700_CLK_HPLL] = AST1700_calc_pll(CREATE_CLK_NAME(id, "hpll"), CREATE_CLK_NAME(id, "clkin"), val);

	/* HPLL 800Mhz */
	val = readl(clk_base + AST1700_APLL_PARAM);
	clks[AST1700_CLK_APLL] = AST1700_calc_pll(CREATE_CLK_NAME(id, "apll"), CREATE_CLK_NAME(id, "clkin"), val);

	clks[AST1700_CLK_APLL_DIV2] =
		clk_hw_register_fixed_factor(dev, CREATE_CLK_NAME(id, "apll_div2"), CREATE_CLK_NAME(id, "apll"), 0, 1, 2);

	clks[AST1700_CLK_APLL_DIV4] =
		clk_hw_register_fixed_factor(dev, CREATE_CLK_NAME(id, "apll_div4"), CREATE_CLK_NAME(id, "apll"), 0, 1, 4);

	val = readl(clk_base + AST1700_DPLL_PARAM);
	clks[AST1700_CLK_DPLL] = AST1700_calc_pll(CREATE_CLK_NAME(id, "dpll"), CREATE_CLK_NAME(id, "clkin"), val);

	/* uxclk mux selection */
	clks[AST1700_CLK_UXCLK] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "uxclk"),
				    (id == 0) ? uxclk_sel0 : uxclk_sel1,
				    (id == 0) ? ARRAY_SIZE(uxclk_sel0) : ARRAY_SIZE(uxclk_sel1),
				    0, clk_base + AST1700_CLK_SEL2,
				    0, 2, 0, &ast1700_clk_lock);

	val = readl(clk_base + AST1700_UXCLK_CTRL);
	clks[AST1700_CLK_UARTX] = AST1700_calc_uclk(CREATE_CLK_NAME(id, "uartxclk"), val);

	/* huxclk mux selection */
	clks[AST1700_CLK_HUXCLK] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "huxclk"),
				    (id == 0) ? uxclk_sel0 : uxclk_sel1,
				    (id == 0) ? ARRAY_SIZE(uxclk_sel0) : ARRAY_SIZE(uxclk_sel1),
				    0, clk_base + AST1700_CLK_SEL2,
				    3, 2, 0, &ast1700_clk_lock);

	val = readl(clk_base + AST1700_HUXCLK_CTRL);
	clks[AST1700_CLK_HUARTX] = AST1700_calc_huclk(CREATE_CLK_NAME(id, "huartxclk"), val);

	/* AHB CLK = 200Mhz */
	clks[AST1700_CLK_AHB] =
		clk_hw_register_divider_table(dev, CREATE_CLK_NAME(id, "ahb"),
					      CREATE_CLK_NAME(id, "hpll"),
					      0, clk_base + AST1700_CLK_SEL2,
					      20, 3, 0, ast1700_clk_div_table, &ast1700_clk_lock);

	/* APB CLK = 100Mhz */
	clks[AST1700_CLK_APB] =
		clk_hw_register_divider_table(dev, CREATE_CLK_NAME(id, "apb"),
					      CREATE_CLK_NAME(id, "hpll"),
					      0, clk_base + AST1700_CLK_SEL1,
					      18, 3, 0, ast1700_clk_div_table2, &ast1700_clk_lock);

	//rmii
	clks[AST1700_CLK_RMII] =
		clk_hw_register_divider_table(dev, CREATE_CLK_NAME(id, "rmii"),
					      CREATE_CLK_NAME(id, "hpll"),
					      0, clk_base + AST1700_CLK_SEL2,
					      21, 3, 0, ast1700_rmii_div_table, &ast1700_clk_lock);

	//rgmii
	clks[AST1700_CLK_RGMII] =
		clk_hw_register_divider_table(dev, CREATE_CLK_NAME(id, "rgmii"),
					      CREATE_CLK_NAME(id, "hpll"),
					      0, clk_base + AST1700_CLK_SEL2,
					      25, 3, 0, ast1700_rgmii_div_table, &ast1700_clk_lock);

	//mac hclk
	clks[AST1700_CLK_MACHCLK] =
		clk_hw_register_divider_table(dev, CREATE_CLK_NAME(id, "machclk"),
					      CREATE_CLK_NAME(id, "hpll"),
					      0, clk_base + AST1700_CLK_SEL2,
					      29, 3, 0, ast1700_clk_div_table, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_LCLK0] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "lclk0-gate"), NULL,
					     CLK_IS_CRITICAL, clk_base + AST1700_CLK_STOP,
					     0, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_LCLK0] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "lclk1-gate"), NULL,
					     CLK_IS_CRITICAL, clk_base + AST1700_CLK_STOP,
					     1, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_ESPI0CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "espi0clk-gate"), NULL,
					     CLK_IS_CRITICAL, clk_base + AST1700_CLK_STOP,
					     2, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_ESPI1CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "espi1clk-gate"), NULL,
					     CLK_IS_CRITICAL, clk_base + AST1700_CLK_STOP,
					     3, 0, &ast1700_clk_lock);

	//sd pll divn
	clks[AST1700_CLK_HPLL_DIVN] =
		clk_hw_register_divider_table(dev, CREATE_CLK_NAME(id, "hpll_divn"),
					      CREATE_CLK_NAME(id, "hpll"),
					      0, clk_base + AST1700_CLK_SEL2,
					      20, 3, 0, ast1700_clk_div_table, &ast1700_clk_lock);

	clks[AST1700_CLK_APLL_DIVN] =
		clk_hw_register_divider_table(dev, CREATE_CLK_NAME(id, "apll_divn"),
					      CREATE_CLK_NAME(id, "apll"),
					      0, clk_base + AST1700_CLK_SEL2,
					      8, 3, 0, ast1700_clk_div_table, &ast1700_clk_lock);

	//sd clk
	clks[AST1700_CLK_SDCLK] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "sdclk"),
				    (id == 0) ? sdclk_sel0 : sdclk_sel1,
				    (id == 0) ? ARRAY_SIZE(sdclk_sel0) : ARRAY_SIZE(sdclk_sel1),
				    0, clk_base + AST1700_CLK_SEL1,
				    13, 1, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_SDCLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "sdclk-gate"),
					     CREATE_CLK_NAME(id, "sdclk"),
					     0, clk_base + AST1700_CLK_STOP,
					     4, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_REFCLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "io-refclk-gate"), NULL,
					     CLK_IS_CRITICAL, clk_base + AST1700_CLK_STOP,
					     6, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_LPCHCLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "lpchclk-gate"), NULL,
					     CLK_IS_CRITICAL, clk_base + AST1700_CLK_STOP,
					     7, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_MAC0CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "mac0clk-gate"), NULL,
					     0, clk_base + AST1700_CLK_STOP,
					     8, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_MAC1CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "mac1clk-gate"), NULL,
					     0, clk_base + AST1700_CLK_STOP,
					     9, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_MAC2CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "mac2clk-gate"), NULL,
					     0, clk_base + AST1700_CLK_STOP,
					     10, 0, &ast1700_clk_lock);

	of_property_read_u32(dev->of_node, "uart-clk-source", &uart_clk_source);

	if (uart_clk_source) {
		val = readl(clk_base + AST1700_CLK_SEL1) & GENMASK(12, 0);
		uart_clk_source &= GENMASK(12, 0);
		writel(val | uart_clk_source, clk_base + AST1700_CLK_SEL1);
	}

	//UART0
	clks[AST1700_CLK_UART0] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "uart0clk"),
				    (id == 0) ? uartclk_sel0 : uartclk_sel1,
				    (id == 0) ? ARRAY_SIZE(uartclk_sel0) : ARRAY_SIZE(uartclk_sel1),
				    0, clk_base + AST1700_CLK_SEL1,
				    0, 1, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_UART0CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "uart0clk-gate"),
					     CREATE_CLK_NAME(id, "uart0clk"),
					     0, clk_base + AST1700_CLK_STOP,
					     11, 0, &ast1700_clk_lock);

	//UART1
	clks[AST1700_CLK_UART1] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "uart1clk"),
				    (id == 0) ? uartclk_sel0 : uartclk_sel1,
				    (id == 0) ? ARRAY_SIZE(uartclk_sel0) : ARRAY_SIZE(uartclk_sel1),
				    0, clk_base + AST1700_CLK_SEL1,
				    1, 1, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_UART1CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "uart1clk-gate"),
					     CREATE_CLK_NAME(id, "uart1clk"),
					     0, clk_base + AST1700_CLK_STOP,
					     12, 0, &ast1700_clk_lock);

	//UART2
	clks[AST1700_CLK_UART2] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "uart2clk"),
				    (id == 0) ? uartclk_sel0 : uartclk_sel1,
				    (id == 0) ? ARRAY_SIZE(uartclk_sel0) : ARRAY_SIZE(uartclk_sel1),
				    0, clk_base + AST1700_CLK_SEL1,
				    2, 1, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_UART2CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "uart2clk-gate"),
					     CREATE_CLK_NAME(id, "uart2clk"),
					     0, clk_base + AST1700_CLK_STOP,
					     13, 0, &ast1700_clk_lock);

	//UART3
	clks[AST1700_CLK_UART3] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "uart3clk"),
				    (id == 0) ? uartclk_sel0 : uartclk_sel1,
				    (id == 0) ? ARRAY_SIZE(uartclk_sel0) : ARRAY_SIZE(uartclk_sel1),
				    0, clk_base + AST1700_CLK_SEL1,
				    3, 1, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_UART3CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "uart3clk-gate"),
					     CREATE_CLK_NAME(id, "uart3clk"),
					     0, clk_base + AST1700_CLK_STOP,
					     14, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C0CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c0clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     16, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C1CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c1clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     17, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C2CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c2clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     18, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C3CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c3clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     19, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C4CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c4clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     20, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C5CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c5clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     21, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C6CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c6clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     22, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C7CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c7clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     23, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C8CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c8clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     24, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C9CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c9clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     25, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C10CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c10clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     26, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C11CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c11clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     27, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C12CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c12clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     28, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C13CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c13clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     29, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C14CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c14clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     30, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_I3C15CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "i3c15clk-gate"),
					     CREATE_CLK_NAME(id, "ahb"),
					     0, clk_base + AST1700_CLK_STOP,
					     31, 0, &ast1700_clk_lock);

	/*clk stop 2 */
	//UART5
	clks[AST1700_CLK_UART5] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "uart5clk"),
				    (id == 0) ? uartclk_sel0 : uartclk_sel1,
				    (id == 0) ? ARRAY_SIZE(uartclk_sel0) : ARRAY_SIZE(uartclk_sel1),
				    0, clk_base + AST1700_CLK_SEL1,
				    5, 1, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_UART5CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "uart5clk-gate"),
					     CREATE_CLK_NAME(id, "uart5clk"),
					     0, clk_base + AST1700_CLK_STOP2,
					     0, 0, &ast1700_clk_lock);

	//UART6
	clks[AST1700_CLK_UART6] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "uart6clk"),
				    (id == 0) ? uartclk_sel0 : uartclk_sel1,
				    (id == 0) ? ARRAY_SIZE(uartclk_sel0) : ARRAY_SIZE(uartclk_sel1),
				    0, clk_base + AST1700_CLK_SEL1,
				    6, 1, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_UART6CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "uart6clk-gate"),
					     CREATE_CLK_NAME(id, "uart6clk"),
					     0, clk_base + AST1700_CLK_STOP2,
					     1, 0, &ast1700_clk_lock);

	//UART7
	clks[AST1700_CLK_UART7] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "uart7clk"),
				    (id == 0) ? uartclk_sel0 : uartclk_sel1,
				    (id == 0) ? ARRAY_SIZE(uartclk_sel0) : ARRAY_SIZE(uartclk_sel1),
				    0, clk_base + AST1700_CLK_SEL1,
				    7, 1, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_UART7CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "uart7clk-gate"),
					     CREATE_CLK_NAME(id, "uart7clk"),
					     0, clk_base + AST1700_CLK_STOP2,
					     2, 0, &ast1700_clk_lock);

	//UART8
	clks[AST1700_CLK_UART8] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "uart8clk"),
				    (id == 0) ? uartclk_sel0 : uartclk_sel1,
				    (id == 0) ? ARRAY_SIZE(uartclk_sel0) : ARRAY_SIZE(uartclk_sel1),
				    0, clk_base + AST1700_CLK_SEL1,
				    8, 1, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_UART8CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "uart8clk-gate"),
					     CREATE_CLK_NAME(id, "uart8clk"),
					     0, clk_base + AST1700_CLK_STOP2,
					     3, 0, &ast1700_clk_lock);

	//UART9
	clks[AST1700_CLK_UART9] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "uart9clk"),
				    (id == 0) ? uartclk_sel0 : uartclk_sel1,
				    (id == 0) ? ARRAY_SIZE(uartclk_sel0) : ARRAY_SIZE(uartclk_sel1),
				    0, clk_base + AST1700_CLK_SEL1,
				    9, 1, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_UART9CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "uart9clk-gate"),
					     CREATE_CLK_NAME(id, "uart9clk"),
					     0, clk_base + AST1700_CLK_STOP2,
					     4, 0, &ast1700_clk_lock);

	//UART10
	clks[AST1700_CLK_UART10] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "uart10clk"),
				    (id == 0) ? uartclk_sel0 : uartclk_sel1,
				    (id == 0) ? ARRAY_SIZE(uartclk_sel0) : ARRAY_SIZE(uartclk_sel1),
				    0, clk_base + AST1700_CLK_SEL1,
				    10, 1, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_UART10CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "uart10clk-gate"),
					     CREATE_CLK_NAME(id, "uart10clk"),
					     0, clk_base + AST1700_CLK_STOP2,
					     5, 0, &ast1700_clk_lock);

	//UART11
	clks[AST1700_CLK_UART11] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "uart11clk"),
				    (id == 0) ? uartclk_sel0 : uartclk_sel1,
				    (id == 0) ? ARRAY_SIZE(uartclk_sel0) : ARRAY_SIZE(uartclk_sel1),
				    0, clk_base + AST1700_CLK_SEL1,
				    11, 1, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_UART11CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "uart11clk-gate"),
					     CREATE_CLK_NAME(id, "uart11clk"),
					     0, clk_base + AST1700_CLK_STOP2,
					     6, 0, &ast1700_clk_lock);

	//uart12: call bmc uart
	clks[AST1700_CLK_UART12] =
		clk_hw_register_mux(dev, CREATE_CLK_NAME(id, "uart12clk"),
				    (id == 0) ? uartclk_sel0 : uartclk_sel1,
				    (id == 0) ? ARRAY_SIZE(uartclk_sel0) : ARRAY_SIZE(uartclk_sel1),
				    0, clk_base + AST1700_CLK_SEL1,
				    12, 1, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_UART12CLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "uart12clk-gate"),
					     CREATE_CLK_NAME(id, "uart12clk"),
					     0, clk_base + AST1700_CLK_STOP2,
					     7, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_FSICLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "fsiclk-gate"), NULL,
					     0, clk_base + AST1700_CLK_STOP2,
					     8, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_LTPIPHYCLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "ltpiphyclk-gate"), NULL,
					     CLK_IS_CRITICAL, clk_base + AST1700_CLK_STOP2,
					     9, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_LTPICLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "ltpiclk-gate"), NULL,
					     CLK_IS_CRITICAL, clk_base + AST1700_CLK_STOP2,
					     10, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_VGALCLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "vgalclk-gate"), NULL,
					     0, clk_base + AST1700_CLK_STOP2,
					     11, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_USBUARTCLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "usbuartclk-gate"), NULL,
					     0, clk_base + AST1700_CLK_STOP2,
					     12, 0, &ast1700_clk_lock);

	clk_hw_register_fixed_factor(dev, CREATE_CLK_NAME(id, "canclk"), CREATE_CLK_NAME(id, "apll"), 0, 1, 10);

	clks[AST1700_CLK_GATE_CANCLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "canclk-gate"),
					     CREATE_CLK_NAME(id, "canclk"),
					     0, clk_base + AST1700_CLK_STOP2,
					     13, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_PCICLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "pciclk-gate"), NULL,
					     0, clk_base + AST1700_CLK_STOP2,
					     14, 0, &ast1700_clk_lock);

	clks[AST1700_CLK_GATE_SLICLK] =
		AST1700_clk_hw_register_gate(NULL, CREATE_CLK_NAME(id, "sliclk-gate"), NULL,
					     0, clk_base + AST1700_CLK_STOP2,
					     15, 0, &ast1700_clk_lock);

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
	if (ret)
		return ret;

	return 0;
};

static int AST1700_clk_probe(struct platform_device *pdev)
{
	int (*probe)(struct platform_device *pdev);

	probe = of_device_get_match_data(&pdev->dev);

	if (probe)
		return probe(pdev);

	return 0;
}

static const struct of_device_id AST1700_clk_dt_ids[] = {
	{ .compatible = "aspeed,ast1700-clk", .data = AST1700_clk_init },
	{}
};

static struct platform_driver AST1700_clk_driver = {
	.probe	= AST1700_clk_probe,
	.driver = {
		.name = "ast1700_clk",
		.of_match_table = AST1700_clk_dt_ids,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(AST1700_clk_driver);
