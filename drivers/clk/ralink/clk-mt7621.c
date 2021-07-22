// SPDX-License-Identifier: GPL-2.0
/*
 * Mediatek MT7621 Clock Driver
 * Author: Sergio Paracuellos <sergio.paracuellos@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <dt-bindings/clock/mt7621-clk.h>

/* Configuration registers */
#define SYSC_REG_SYSTEM_CONFIG0         0x10
#define SYSC_REG_SYSTEM_CONFIG1         0x14
#define SYSC_REG_CLKCFG0		0x2c
#define SYSC_REG_CLKCFG1		0x30
#define SYSC_REG_CUR_CLK_STS		0x44
#define MEMC_REG_CPU_PLL		0x648

#define XTAL_MODE_SEL_MASK		GENMASK(8, 6)
#define CPU_CLK_SEL_MASK		GENMASK(31, 30)
#define CUR_CPU_FDIV_MASK		GENMASK(12, 8)
#define CUR_CPU_FFRAC_MASK		GENMASK(4, 0)
#define CPU_PLL_PREDIV_MASK		GENMASK(13, 12)
#define CPU_PLL_FBDIV_MASK		GENMASK(10, 4)

struct mt7621_clk_priv {
	struct regmap *sysc;
	struct regmap *memc;
};

struct mt7621_clk {
	struct clk_hw hw;
	struct mt7621_clk_priv *priv;
};

struct mt7621_fixed_clk {
	u8 idx;
	const char *name;
	const char *parent_name;
	unsigned long rate;
	struct clk_hw *hw;
};

struct mt7621_gate {
	u8 idx;
	const char *name;
	const char *parent_name;
	struct mt7621_clk_priv *priv;
	u32 bit_idx;
	struct clk_hw hw;
};

#define GATE(_id, _name, _pname, _shift)	\
	{					\
		.idx		= _id,		\
		.name		= _name,	\
		.parent_name	= _pname,	\
		.bit_idx	= _shift	\
	}

static struct mt7621_gate mt7621_gates[] = {
	GATE(MT7621_CLK_HSDMA, "hsdma", "150m", BIT(5)),
	GATE(MT7621_CLK_FE, "fe", "250m", BIT(6)),
	GATE(MT7621_CLK_SP_DIVTX, "sp_divtx", "270m", BIT(7)),
	GATE(MT7621_CLK_TIMER, "timer", "50m", BIT(8)),
	GATE(MT7621_CLK_PCM, "pcm", "270m", BIT(11)),
	GATE(MT7621_CLK_PIO, "pio", "50m", BIT(13)),
	GATE(MT7621_CLK_GDMA, "gdma", "bus", BIT(14)),
	GATE(MT7621_CLK_NAND, "nand", "125m", BIT(15)),
	GATE(MT7621_CLK_I2C, "i2c", "50m", BIT(16)),
	GATE(MT7621_CLK_I2S, "i2s", "270m", BIT(17)),
	GATE(MT7621_CLK_SPI, "spi", "bus", BIT(18)),
	GATE(MT7621_CLK_UART1, "uart1", "50m", BIT(19)),
	GATE(MT7621_CLK_UART2, "uart2", "50m", BIT(20)),
	GATE(MT7621_CLK_UART3, "uart3", "50m", BIT(21)),
	GATE(MT7621_CLK_ETH, "eth", "50m", BIT(23)),
	GATE(MT7621_CLK_PCIE0, "pcie0", "125m", BIT(24)),
	GATE(MT7621_CLK_PCIE1, "pcie1", "125m", BIT(25)),
	GATE(MT7621_CLK_PCIE2, "pcie2", "125m", BIT(26)),
	GATE(MT7621_CLK_CRYPTO, "crypto", "250m", BIT(29)),
	GATE(MT7621_CLK_SHXC, "shxc", "50m", BIT(30))
};

static inline struct mt7621_gate *to_mt7621_gate(struct clk_hw *hw)
{
	return container_of(hw, struct mt7621_gate, hw);
}

static int mt7621_gate_enable(struct clk_hw *hw)
{
	struct mt7621_gate *clk_gate = to_mt7621_gate(hw);
	struct regmap *sysc = clk_gate->priv->sysc;

	return regmap_update_bits(sysc, SYSC_REG_CLKCFG1,
				  clk_gate->bit_idx, clk_gate->bit_idx);
}

static void mt7621_gate_disable(struct clk_hw *hw)
{
	struct mt7621_gate *clk_gate = to_mt7621_gate(hw);
	struct regmap *sysc = clk_gate->priv->sysc;

	regmap_update_bits(sysc, SYSC_REG_CLKCFG1, clk_gate->bit_idx, 0);
}

static int mt7621_gate_is_enabled(struct clk_hw *hw)
{
	struct mt7621_gate *clk_gate = to_mt7621_gate(hw);
	struct regmap *sysc = clk_gate->priv->sysc;
	u32 val;

	if (regmap_read(sysc, SYSC_REG_CLKCFG1, &val))
		return 0;

	return val & BIT(clk_gate->bit_idx);
}

static const struct clk_ops mt7621_gate_ops = {
	.enable = mt7621_gate_enable,
	.disable = mt7621_gate_disable,
	.is_enabled = mt7621_gate_is_enabled,
};

static int mt7621_gate_ops_init(struct device *dev,
				struct mt7621_gate *sclk)
{
	struct clk_init_data init = {
		/*
		 * Until now no clock driver existed so
		 * these SoC drivers are not prepared
		 * yet for the clock. We don't want kernel to
		 * disable anything so we add CLK_IS_CRITICAL
		 * flag here.
		 */
		.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
		.num_parents = 1,
		.parent_names = &sclk->parent_name,
		.ops = &mt7621_gate_ops,
		.name = sclk->name,
	};

	sclk->hw.init = &init;
	return devm_clk_hw_register(dev, &sclk->hw);
}

static int mt7621_register_gates(struct device *dev,
				 struct clk_hw_onecell_data *clk_data,
				 struct mt7621_clk_priv *priv)
{
	struct clk_hw **hws = clk_data->hws;
	struct mt7621_gate *sclk;
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(mt7621_gates); i++) {
		sclk = &mt7621_gates[i];
		sclk->priv = priv;
		ret = mt7621_gate_ops_init(dev, sclk);
		if (ret) {
			dev_err(dev, "Couldn't register clock %s\n", sclk->name);
			goto err_clk_unreg;
		}

		hws[sclk->idx] = &sclk->hw;
	}

	return 0;

err_clk_unreg:
	while (--i >= 0) {
		sclk = &mt7621_gates[i];
		clk_hw_unregister(&sclk->hw);
	}
	return ret;
}

#define FIXED(_id, _name, _rate)		\
	{					\
		.idx		= _id,		\
		.name		= _name,	\
		.parent_name	= "xtal",	\
		.rate		= _rate		\
	}

static struct mt7621_fixed_clk mt7621_fixed_clks[] = {
	FIXED(MT7621_CLK_50M, "50m", 50000000),
	FIXED(MT7621_CLK_125M, "125m", 125000000),
	FIXED(MT7621_CLK_150M, "150m", 150000000),
	FIXED(MT7621_CLK_250M, "250m", 250000000),
	FIXED(MT7621_CLK_270M, "270m", 270000000),
};

static int mt7621_register_fixed_clocks(struct device *dev,
					struct clk_hw_onecell_data *clk_data)
{
	struct clk_hw **hws = clk_data->hws;
	struct mt7621_fixed_clk *sclk;
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(mt7621_fixed_clks); i++) {
		sclk = &mt7621_fixed_clks[i];
		sclk->hw = clk_hw_register_fixed_rate(dev, sclk->name,
						      sclk->parent_name, 0,
						      sclk->rate);
		if (IS_ERR(sclk->hw)) {
			dev_err(dev, "Couldn't register clock %s\n", sclk->name);
			ret = PTR_ERR(sclk->hw);
			goto err_clk_unreg;
		}

		hws[sclk->idx] = sclk->hw;
	}

	return 0;

err_clk_unreg:
	while (--i >= 0) {
		sclk = &mt7621_fixed_clks[i];
		clk_hw_unregister_fixed_rate(sclk->hw);
	}
	return ret;
}

static inline struct mt7621_clk *to_mt7621_clk(struct clk_hw *hw)
{
	return container_of(hw, struct mt7621_clk, hw);
}

static unsigned long mt7621_xtal_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct mt7621_clk *clk = to_mt7621_clk(hw);
	struct regmap *sysc = clk->priv->sysc;
	u32 val;

	regmap_read(sysc, SYSC_REG_SYSTEM_CONFIG0, &val);
	val = FIELD_GET(XTAL_MODE_SEL_MASK, val);

	if (val <= 2)
		return 20000000;
	if (val <= 5)
		return 40000000;

	return 25000000;
}

static unsigned long mt7621_cpu_recalc_rate(struct clk_hw *hw,
					    unsigned long xtal_clk)
{
	static const u32 prediv_tbl[] = { 0, 1, 2, 2 };
	struct mt7621_clk *clk = to_mt7621_clk(hw);
	struct regmap *sysc = clk->priv->sysc;
	struct regmap *memc = clk->priv->memc;
	u32 clkcfg, clk_sel, curclk, ffiv, ffrac;
	u32 pll, prediv, fbdiv;
	unsigned long cpu_clk;

	regmap_read(sysc, SYSC_REG_CLKCFG0, &clkcfg);
	clk_sel = FIELD_GET(CPU_CLK_SEL_MASK, clkcfg);

	regmap_read(sysc, SYSC_REG_CUR_CLK_STS, &curclk);
	ffiv = FIELD_GET(CUR_CPU_FDIV_MASK, curclk);
	ffrac = FIELD_GET(CUR_CPU_FFRAC_MASK, curclk);

	switch (clk_sel) {
	case 0:
		cpu_clk = 500000000;
		break;
	case 1:
		regmap_read(memc, MEMC_REG_CPU_PLL, &pll);
		fbdiv = FIELD_GET(CPU_PLL_FBDIV_MASK, pll);
		prediv = FIELD_GET(CPU_PLL_PREDIV_MASK, pll);
		cpu_clk = ((fbdiv + 1) * xtal_clk) >> prediv_tbl[prediv];
		break;
	default:
		cpu_clk = xtal_clk;
	}

	return cpu_clk / ffiv * ffrac;
}

static unsigned long mt7621_bus_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	return parent_rate / 4;
}

#define CLK_BASE(_name, _parent, _recalc) {				\
	.init = &(struct clk_init_data) {				\
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

static struct mt7621_clk mt7621_clks_base[] = {
	{ CLK_BASE("xtal", NULL, mt7621_xtal_recalc_rate) },
	{ CLK_BASE("cpu", "xtal", mt7621_cpu_recalc_rate) },
	{ CLK_BASE("bus", "cpu", mt7621_bus_recalc_rate) },
};

static struct clk_hw *mt7621_clk_early[MT7621_CLK_MAX];

static int mt7621_register_early_clocks(struct device_node *np,
					struct clk_hw_onecell_data *clk_data,
					struct mt7621_clk_priv *priv)
{
	struct clk_hw **hws = clk_data->hws;
	struct mt7621_clk *sclk;
	int ret, i, j;

	for (i = 0; i < ARRAY_SIZE(mt7621_clks_base); i++) {
		sclk = &mt7621_clks_base[i];
		sclk->priv = priv;
		ret = of_clk_hw_register(np, &sclk->hw);
		if (ret) {
			pr_err("Couldn't register top clock %i\n", i);
			goto err_clk_unreg;
		}

		hws[i] = &sclk->hw;
		mt7621_clk_early[i] = &sclk->hw;
	}

	for (j = i; j < MT7621_CLK_MAX; j++)
		mt7621_clk_early[j] = ERR_PTR(-EPROBE_DEFER);

	return 0;

err_clk_unreg:
	while (--i >= 0) {
		sclk = &mt7621_clks_base[i];
		clk_hw_unregister(&sclk->hw);
	}
	return ret;
}

static void __init mt7621_clk_init(struct device_node *node)
{
	struct mt7621_clk_priv *priv;
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

	priv->memc = syscon_regmap_lookup_by_phandle(node, "ralink,memctl");
	if (IS_ERR(priv->memc)) {
		pr_err("Could not get memc syscon regmap\n");
		goto free_clk_priv;
	}

	count = ARRAY_SIZE(mt7621_clks_base) +
		ARRAY_SIZE(mt7621_fixed_clks) + ARRAY_SIZE(mt7621_gates);
	clk_data = kzalloc(struct_size(clk_data, hws, count), GFP_KERNEL);
	if (!clk_data)
		goto free_clk_priv;

	ret = mt7621_register_early_clocks(node, clk_data, priv);
	if (ret) {
		pr_err("Couldn't register top clocks\n");
		goto free_clk_data;
	}

	clk_data->num = count;

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (ret) {
		pr_err("Couldn't add clk hw provider\n");
		goto unreg_clk_top;
	}

	return;

unreg_clk_top:
	for (i = 0; i < ARRAY_SIZE(mt7621_clks_base); i++) {
		struct mt7621_clk *sclk = &mt7621_clks_base[i];

		clk_hw_unregister(&sclk->hw);
	}

free_clk_data:
	kfree(clk_data);

free_clk_priv:
	kfree(priv);
}
CLK_OF_DECLARE_DRIVER(mt7621_clk, "mediatek,mt7621-sysc", mt7621_clk_init);

static int mt7621_clk_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data;
	struct device *dev = &pdev->dev;
	struct mt7621_clk_priv *priv;
	int ret, i, count;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->sysc = syscon_node_to_regmap(np);
	if (IS_ERR(priv->sysc)) {
		ret = PTR_ERR(priv->sysc);
		dev_err(dev, "Could not get sysc syscon regmap\n");
		return ret;
	}

	priv->memc = syscon_regmap_lookup_by_phandle(np, "ralink,memctl");
	if (IS_ERR(priv->memc)) {
		ret = PTR_ERR(priv->memc);
		dev_err(dev, "Could not get memc syscon regmap\n");
		return ret;
	}

	count = ARRAY_SIZE(mt7621_clks_base) +
		ARRAY_SIZE(mt7621_fixed_clks) + ARRAY_SIZE(mt7621_gates);
	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, count),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(mt7621_clks_base); i++)
		clk_data->hws[i] = mt7621_clk_early[i];

	ret = mt7621_register_fixed_clocks(dev, clk_data);
	if (ret) {
		dev_err(dev, "Couldn't register fixed clocks\n");
		return ret;
	}

	ret = mt7621_register_gates(dev, clk_data, priv);
	if (ret) {
		dev_err(dev, "Couldn't register fixed clock gates\n");
		goto unreg_clk_fixed;
	}

	clk_data->num = count;

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
	if (ret) {
		dev_err(dev, "Couldn't add clk hw provider\n");
		goto unreg_clk_gates;
	}

	return 0;

unreg_clk_gates:
	for (i = 0; i < ARRAY_SIZE(mt7621_gates); i++) {
		struct mt7621_gate *sclk = &mt7621_gates[i];

		clk_hw_unregister(&sclk->hw);
	}

unreg_clk_fixed:
	for (i = 0; i < ARRAY_SIZE(mt7621_fixed_clks); i++) {
		struct mt7621_fixed_clk *sclk = &mt7621_fixed_clks[i];

		clk_hw_unregister_fixed_rate(sclk->hw);
	}

	return ret;
}

static const struct of_device_id mt7621_clk_of_match[] = {
	{ .compatible = "mediatek,mt7621-sysc" },
	{}
};

static struct platform_driver mt7621_clk_driver = {
	.probe = mt7621_clk_probe,
	.driver = {
		.name = "mt7621-clk",
		.of_match_table = mt7621_clk_of_match,
	},
};
builtin_platform_driver(mt7621_clk_driver);
