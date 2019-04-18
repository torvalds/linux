// SPDX-License-Identifier: GPL-2.0
/**
 * PCIe SERDES driver for AM654x SoC
 *
 * Copyright (C) 2018 - 2019 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <dt-bindings/phy/phy.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/mux/consumer.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#define CMU_R07C		0x7c

#define COMLANE_R138		0xb38
#define VERSION			0x70

#define COMLANE_R190		0xb90

#define COMLANE_R194		0xb94

#define SERDES_CTRL		0x1fd0

#define WIZ_LANEXCTL_STS	0x1fe0
#define TX0_DISABLE_STATE	0x4
#define TX0_SLEEP_STATE		0x5
#define TX0_SNOOZE_STATE	0x6
#define TX0_ENABLE_STATE	0x7

#define RX0_DISABLE_STATE	0x4
#define RX0_SLEEP_STATE		0x5
#define RX0_SNOOZE_STATE	0x6
#define RX0_ENABLE_STATE	0x7

#define WIZ_PLL_CTRL		0x1ff4
#define PLL_DISABLE_STATE	0x4
#define PLL_SLEEP_STATE		0x5
#define PLL_SNOOZE_STATE	0x6
#define PLL_ENABLE_STATE	0x7

#define PLL_LOCK_TIME		100000	/* in microseconds */
#define SLEEP_TIME		100	/* in microseconds */

#define LANE_USB3		0x0
#define LANE_PCIE0_LANE0	0x1

#define LANE_PCIE1_LANE0	0x0
#define LANE_PCIE0_LANE1	0x1

#define SERDES_NUM_CLOCKS	3

#define AM654_SERDES_CTRL_CLKSEL_MASK	GENMASK(7, 4)
#define AM654_SERDES_CTRL_CLKSEL_SHIFT	4

struct serdes_am654_clk_mux {
	struct clk_hw	hw;
	struct regmap	*regmap;
	unsigned int	reg;
	int		clk_id;
	struct clk_init_data clk_data;
};

#define to_serdes_am654_clk_mux(_hw)	\
		container_of(_hw, struct serdes_am654_clk_mux, hw)

static struct regmap_config serdes_am654_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.fast_io = true,
};

static const struct reg_field cmu_master_cdn_o = REG_FIELD(CMU_R07C, 24, 24);
static const struct reg_field config_version = REG_FIELD(COMLANE_R138, 16, 23);
static const struct reg_field l1_master_cdn_o = REG_FIELD(COMLANE_R190, 9, 9);
static const struct reg_field cmu_ok_i_0 = REG_FIELD(COMLANE_R194, 19, 19);
static const struct reg_field por_en = REG_FIELD(SERDES_CTRL, 29, 29);
static const struct reg_field tx0_enable = REG_FIELD(WIZ_LANEXCTL_STS, 29, 31);
static const struct reg_field rx0_enable = REG_FIELD(WIZ_LANEXCTL_STS, 13, 15);
static const struct reg_field pll_enable = REG_FIELD(WIZ_PLL_CTRL, 29, 31);
static const struct reg_field pll_ok = REG_FIELD(WIZ_PLL_CTRL, 28, 28);

struct serdes_am654 {
	struct regmap		*regmap;
	struct regmap_field	*cmu_master_cdn_o;
	struct regmap_field	*config_version;
	struct regmap_field	*l1_master_cdn_o;
	struct regmap_field	*cmu_ok_i_0;
	struct regmap_field	*por_en;
	struct regmap_field	*tx0_enable;
	struct regmap_field	*rx0_enable;
	struct regmap_field	*pll_enable;
	struct regmap_field	*pll_ok;

	struct device		*dev;
	struct mux_control	*control;
	bool			busy;
	u32			type;
	struct device_node	*of_node;
	struct clk_onecell_data	clk_data;
	struct clk		*clks[SERDES_NUM_CLOCKS];
};

static int serdes_am654_enable_pll(struct serdes_am654 *phy)
{
	int ret;
	u32 val;

	ret = regmap_field_write(phy->pll_enable, PLL_ENABLE_STATE);
	if (ret)
		return ret;

	return regmap_field_read_poll_timeout(phy->pll_ok, val, val, 1000,
					      PLL_LOCK_TIME);
}

static void serdes_am654_disable_pll(struct serdes_am654 *phy)
{
	struct device *dev = phy->dev;
	int ret;

	ret = regmap_field_write(phy->pll_enable, PLL_DISABLE_STATE);
	if (ret)
		dev_err(dev, "Failed to disable PLL\n");
}

static int serdes_am654_enable_txrx(struct serdes_am654 *phy)
{
	int ret;

	/* Enable TX */
	ret = regmap_field_write(phy->tx0_enable, TX0_ENABLE_STATE);
	if (ret)
		return ret;

	/* Enable RX */
	ret = regmap_field_write(phy->rx0_enable, RX0_ENABLE_STATE);
	if (ret)
		return ret;

	return 0;
}

static int serdes_am654_disable_txrx(struct serdes_am654 *phy)
{
	int ret;

	/* Disable TX */
	ret = regmap_field_write(phy->tx0_enable, TX0_DISABLE_STATE);
	if (ret)
		return ret;

	/* Disable RX */
	ret = regmap_field_write(phy->rx0_enable, RX0_DISABLE_STATE);
	if (ret)
		return ret;

	return 0;
}

static int serdes_am654_power_on(struct phy *x)
{
	struct serdes_am654 *phy = phy_get_drvdata(x);
	struct device *dev = phy->dev;
	int ret;
	u32 val;

	ret = serdes_am654_enable_pll(phy);
	if (ret) {
		dev_err(dev, "Failed to enable PLL\n");
		return ret;
	}

	ret = serdes_am654_enable_txrx(phy);
	if (ret) {
		dev_err(dev, "Failed to enable TX RX\n");
		return ret;
	}

	return regmap_field_read_poll_timeout(phy->cmu_ok_i_0, val, val,
					      SLEEP_TIME, PLL_LOCK_TIME);
}

static int serdes_am654_power_off(struct phy *x)
{
	struct serdes_am654 *phy = phy_get_drvdata(x);

	serdes_am654_disable_txrx(phy);
	serdes_am654_disable_pll(phy);

	return 0;
}

static int serdes_am654_init(struct phy *x)
{
	struct serdes_am654 *phy = phy_get_drvdata(x);
	int ret;

	ret = regmap_field_write(phy->config_version, VERSION);
	if (ret)
		return ret;

	ret = regmap_field_write(phy->cmu_master_cdn_o, 0x1);
	if (ret)
		return ret;

	ret = regmap_field_write(phy->l1_master_cdn_o, 0x1);
	if (ret)
		return ret;

	return 0;
}

static int serdes_am654_reset(struct phy *x)
{
	struct serdes_am654 *phy = phy_get_drvdata(x);
	int ret;

	ret = regmap_field_write(phy->por_en, 0x1);
	if (ret)
		return ret;

	mdelay(1);

	ret = regmap_field_write(phy->por_en, 0x0);
	if (ret)
		return ret;

	return 0;
}

static void serdes_am654_release(struct phy *x)
{
	struct serdes_am654 *phy = phy_get_drvdata(x);

	phy->type = PHY_NONE;
	phy->busy = false;
	mux_control_deselect(phy->control);
}

static struct phy *serdes_am654_xlate(struct device *dev,
				      struct of_phandle_args *args)
{
	struct serdes_am654 *am654_phy;
	struct phy *phy;
	int ret;

	phy = of_phy_simple_xlate(dev, args);
	if (IS_ERR(phy))
		return phy;

	am654_phy = phy_get_drvdata(phy);
	if (am654_phy->busy)
		return ERR_PTR(-EBUSY);

	ret = mux_control_select(am654_phy->control, args->args[1]);
	if (ret) {
		dev_err(dev, "Failed to select SERDES Lane Function\n");
		return ERR_PTR(ret);
	}

	am654_phy->busy = true;
	am654_phy->type = args->args[0];

	return phy;
}

static const struct phy_ops ops = {
	.reset		= serdes_am654_reset,
	.init		= serdes_am654_init,
	.power_on	= serdes_am654_power_on,
	.power_off	= serdes_am654_power_off,
	.release	= serdes_am654_release,
	.owner		= THIS_MODULE,
};

#define SERDES_NUM_MUX_COMBINATIONS 16

#define LICLK 0
#define EXT_REFCLK 1
#define RICLK 2

static const int
serdes_am654_mux_table[SERDES_NUM_MUX_COMBINATIONS][SERDES_NUM_CLOCKS] = {
	/*
	 * Each combination maps to one of
	 * "Figure 12-1986. SerDes Reference Clock Distribution"
	 * in TRM.
	 */
	 /* Parent of CMU refclk, Left output, Right output
	  * either of EXT_REFCLK, LICLK, RICLK
	  */
	{ EXT_REFCLK, EXT_REFCLK, EXT_REFCLK },	/* 0000 */
	{ RICLK, EXT_REFCLK, EXT_REFCLK },	/* 0001 */
	{ EXT_REFCLK, RICLK, LICLK },		/* 0010 */
	{ RICLK, RICLK, EXT_REFCLK },		/* 0011 */
	{ LICLK, EXT_REFCLK, EXT_REFCLK },	/* 0100 */
	{ EXT_REFCLK, EXT_REFCLK, EXT_REFCLK },	/* 0101 */
	{ LICLK, RICLK, LICLK },		/* 0110 */
	{ EXT_REFCLK, RICLK, LICLK },		/* 0111 */
	{ EXT_REFCLK, EXT_REFCLK, LICLK },	/* 1000 */
	{ RICLK, EXT_REFCLK, LICLK },		/* 1001 */
	{ EXT_REFCLK, RICLK, EXT_REFCLK },	/* 1010 */
	{ RICLK, RICLK, EXT_REFCLK },		/* 1011 */
	{ LICLK, EXT_REFCLK, LICLK },		/* 1100 */
	{ EXT_REFCLK, EXT_REFCLK, LICLK },	/* 1101 */
	{ LICLK, RICLK, EXT_REFCLK },		/* 1110 */
	{ EXT_REFCLK, RICLK, EXT_REFCLK },	/* 1111 */
};

static u8 serdes_am654_clk_mux_get_parent(struct clk_hw *hw)
{
	struct serdes_am654_clk_mux *mux = to_serdes_am654_clk_mux(hw);
	struct regmap *regmap = mux->regmap;
	unsigned int reg = mux->reg;
	unsigned int val;

	regmap_read(regmap, reg, &val);
	val &= AM654_SERDES_CTRL_CLKSEL_MASK;
	val >>= AM654_SERDES_CTRL_CLKSEL_SHIFT;

	return serdes_am654_mux_table[val][mux->clk_id];
}

static int serdes_am654_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct serdes_am654_clk_mux *mux = to_serdes_am654_clk_mux(hw);
	struct regmap *regmap = mux->regmap;
	unsigned int reg = mux->reg;
	int clk_id = mux->clk_id;
	int parents[SERDES_NUM_CLOCKS];
	const int *p;
	u32 val;
	int found, i;
	int ret;

	/* get existing setting */
	regmap_read(regmap, reg, &val);
	val &= AM654_SERDES_CTRL_CLKSEL_MASK;
	val >>= AM654_SERDES_CTRL_CLKSEL_SHIFT;

	for (i = 0; i < SERDES_NUM_CLOCKS; i++)
		parents[i] = serdes_am654_mux_table[val][i];

	/* change parent of this clock. others left intact */
	parents[clk_id] = index;

	/* Find the match */
	for (val = 0; val < SERDES_NUM_MUX_COMBINATIONS; val++) {
		p = serdes_am654_mux_table[val];
		found = 1;
		for (i = 0; i < SERDES_NUM_CLOCKS; i++) {
			if (parents[i] != p[i]) {
				found = 0;
				break;
			}
		}

		if (found)
			break;
	}

	if (!found) {
		/*
		 * This can never happen, unless we missed
		 * a valid combination in serdes_am654_mux_table.
		 */
		WARN(1, "Failed to find the parent of %s clock\n",
		     hw->init->name);
		return -EINVAL;
	}

	val <<= AM654_SERDES_CTRL_CLKSEL_SHIFT;
	ret = regmap_update_bits(regmap, reg, AM654_SERDES_CTRL_CLKSEL_MASK,
				 val);

	return ret;
}

static const struct clk_ops serdes_am654_clk_mux_ops = {
	.set_parent = serdes_am654_clk_mux_set_parent,
	.get_parent = serdes_am654_clk_mux_get_parent,
};

static int serdes_am654_clk_register(struct serdes_am654 *am654_phy,
				     const char *clock_name, int clock_num)
{
	struct device_node *node = am654_phy->of_node;
	struct device *dev = am654_phy->dev;
	struct serdes_am654_clk_mux *mux;
	struct device_node *regmap_node;
	const char **parent_names;
	struct clk_init_data *init;
	unsigned int num_parents;
	struct regmap *regmap;
	const __be32 *addr;
	unsigned int reg;
	struct clk *clk;

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	init = &mux->clk_data;

	regmap_node = of_parse_phandle(node, "ti,serdes-clk", 0);
	of_node_put(regmap_node);
	if (!regmap_node) {
		dev_err(dev, "Fail to get serdes-clk node\n");
		return -ENODEV;
	}

	regmap = syscon_node_to_regmap(regmap_node->parent);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Fail to get Syscon regmap\n");
		return PTR_ERR(regmap);
	}

	num_parents = of_clk_get_parent_count(node);
	if (num_parents < 2) {
		dev_err(dev, "SERDES clock must have parents\n");
		return -EINVAL;
	}

	parent_names = devm_kzalloc(dev, (sizeof(char *) * num_parents),
				    GFP_KERNEL);
	if (!parent_names)
		return -ENOMEM;

	of_clk_parent_fill(node, parent_names, num_parents);

	addr = of_get_address(regmap_node, 0, NULL, NULL);
	if (!addr)
		return -EINVAL;

	reg = be32_to_cpu(*addr);

	init->ops = &serdes_am654_clk_mux_ops;
	init->flags = CLK_SET_RATE_NO_REPARENT;
	init->parent_names = parent_names;
	init->num_parents = num_parents;
	init->name = clock_name;

	mux->regmap = regmap;
	mux->reg = reg;
	mux->clk_id = clock_num;
	mux->hw.init = init;

	clk = devm_clk_register(dev, &mux->hw);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	am654_phy->clks[clock_num] = clk;

	return 0;
}

static const struct of_device_id serdes_am654_id_table[] = {
	{
		.compatible = "ti,phy-am654-serdes",
	},
	{}
};
MODULE_DEVICE_TABLE(of, serdes_am654_id_table);

static int serdes_am654_regfield_init(struct serdes_am654 *am654_phy)
{
	struct regmap *regmap = am654_phy->regmap;
	struct device *dev = am654_phy->dev;

	am654_phy->cmu_master_cdn_o = devm_regmap_field_alloc(dev, regmap,
							      cmu_master_cdn_o);
	if (IS_ERR(am654_phy->cmu_master_cdn_o)) {
		dev_err(dev, "CMU_MASTER_CDN_O reg field init failed\n");
		return PTR_ERR(am654_phy->cmu_master_cdn_o);
	}

	am654_phy->config_version = devm_regmap_field_alloc(dev, regmap,
							    config_version);
	if (IS_ERR(am654_phy->config_version)) {
		dev_err(dev, "CONFIG_VERSION reg field init failed\n");
		return PTR_ERR(am654_phy->config_version);
	}

	am654_phy->l1_master_cdn_o = devm_regmap_field_alloc(dev, regmap,
							     l1_master_cdn_o);
	if (IS_ERR(am654_phy->l1_master_cdn_o)) {
		dev_err(dev, "L1_MASTER_CDN_O reg field init failed\n");
		return PTR_ERR(am654_phy->l1_master_cdn_o);
	}

	am654_phy->cmu_ok_i_0 = devm_regmap_field_alloc(dev, regmap,
							cmu_ok_i_0);
	if (IS_ERR(am654_phy->cmu_ok_i_0)) {
		dev_err(dev, "CMU_OK_I_0 reg field init failed\n");
		return PTR_ERR(am654_phy->cmu_ok_i_0);
	}

	am654_phy->por_en = devm_regmap_field_alloc(dev, regmap, por_en);
	if (IS_ERR(am654_phy->por_en)) {
		dev_err(dev, "POR_EN reg field init failed\n");
		return PTR_ERR(am654_phy->por_en);
	}

	am654_phy->tx0_enable = devm_regmap_field_alloc(dev, regmap,
							tx0_enable);
	if (IS_ERR(am654_phy->tx0_enable)) {
		dev_err(dev, "TX0_ENABLE reg field init failed\n");
		return PTR_ERR(am654_phy->tx0_enable);
	}

	am654_phy->rx0_enable = devm_regmap_field_alloc(dev, regmap,
							rx0_enable);
	if (IS_ERR(am654_phy->rx0_enable)) {
		dev_err(dev, "RX0_ENABLE reg field init failed\n");
		return PTR_ERR(am654_phy->rx0_enable);
	}

	am654_phy->pll_enable = devm_regmap_field_alloc(dev, regmap,
							pll_enable);
	if (IS_ERR(am654_phy->pll_enable)) {
		dev_err(dev, "PLL_ENABLE reg field init failed\n");
		return PTR_ERR(am654_phy->pll_enable);
	}

	am654_phy->pll_ok = devm_regmap_field_alloc(dev, regmap, pll_ok);
	if (IS_ERR(am654_phy->pll_ok)) {
		dev_err(dev, "PLL_OK reg field init failed\n");
		return PTR_ERR(am654_phy->pll_ok);
	}

	return 0;
}

static int serdes_am654_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct clk_onecell_data *clk_data;
	struct serdes_am654 *am654_phy;
	struct mux_control *control;
	const char *clock_name;
	struct regmap *regmap;
	void __iomem *base;
	struct phy *phy;
	int ret;
	int i;

	am654_phy = devm_kzalloc(dev, sizeof(*am654_phy), GFP_KERNEL);
	if (!am654_phy)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(dev, base, &serdes_am654_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to initialize regmap\n");
		return PTR_ERR(regmap);
	}

	control = devm_mux_control_get(dev, NULL);
	if (IS_ERR(control))
		return PTR_ERR(control);

	am654_phy->dev = dev;
	am654_phy->of_node = node;
	am654_phy->regmap = regmap;
	am654_phy->control = control;
	am654_phy->type = PHY_NONE;

	ret = serdes_am654_regfield_init(am654_phy);
	if (ret) {
		dev_err(dev, "Failed to initialize regfields\n");
		return ret;
	}

	platform_set_drvdata(pdev, am654_phy);

	for (i = 0; i < SERDES_NUM_CLOCKS; i++) {
		ret = of_property_read_string_index(node, "clock-output-names",
						    i, &clock_name);
		if (ret) {
			dev_err(dev, "Failed to get clock name\n");
			return ret;
		}

		ret = serdes_am654_clk_register(am654_phy, clock_name, i);
		if (ret) {
			dev_err(dev, "Failed to initialize clock %s\n",
				clock_name);
			return ret;
		}
	}

	clk_data = &am654_phy->clk_data;
	clk_data->clks = am654_phy->clks;
	clk_data->clk_num = SERDES_NUM_CLOCKS;
	ret = of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
	if (ret)
		return ret;

	pm_runtime_enable(dev);

	phy = devm_phy_create(dev, NULL, &ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	phy_set_drvdata(phy, am654_phy);
	phy_provider = devm_of_phy_provider_register(dev, serdes_am654_xlate);
	if (IS_ERR(phy_provider)) {
		ret = PTR_ERR(phy_provider);
		goto clk_err;
	}

	return 0;

clk_err:
	of_clk_del_provider(node);

	return ret;
}

static int serdes_am654_remove(struct platform_device *pdev)
{
	struct serdes_am654 *am654_phy = platform_get_drvdata(pdev);
	struct device_node *node = am654_phy->of_node;

	pm_runtime_disable(&pdev->dev);
	of_clk_del_provider(node);

	return 0;
}

static struct platform_driver serdes_am654_driver = {
	.probe		= serdes_am654_probe,
	.remove		= serdes_am654_remove,
	.driver		= {
		.name	= "phy-am654",
		.of_match_table = serdes_am654_id_table,
	},
};
module_platform_driver(serdes_am654_driver);

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TI AM654x SERDES driver");
MODULE_LICENSE("GPL v2");
