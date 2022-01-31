// SPDX-License-Identifier: GPL-2.0
/*
 * Wrapper driver for SERDES used in J721E
 *
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <dt-bindings/phy/phy.h>
#include <dt-bindings/phy/phy-ti.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mux/consumer.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#define WIZ_SERDES_CTRL		0x404
#define WIZ_SERDES_TOP_CTRL	0x408
#define WIZ_SERDES_RST		0x40c
#define WIZ_SERDES_TYPEC	0x410
#define WIZ_LANECTL(n)		(0x480 + (0x40 * (n)))
#define WIZ_LANEDIV(n)		(0x484 + (0x40 * (n)))

#define WIZ_MAX_INPUT_CLOCKS	4
/* To include mux clocks, divider clocks and gate clocks */
#define WIZ_MAX_OUTPUT_CLOCKS	32

#define WIZ_MAX_LANES		4
#define WIZ_MUX_NUM_CLOCKS	3
#define WIZ_DIV_NUM_CLOCKS_16G	2
#define WIZ_DIV_NUM_CLOCKS_10G	1

#define WIZ_SERDES_TYPEC_LN10_SWAP	BIT(30)

enum wiz_lane_standard_mode {
	LANE_MODE_GEN1,
	LANE_MODE_GEN2,
	LANE_MODE_GEN3,
	LANE_MODE_GEN4,
};

enum wiz_refclk_mux_sel {
	PLL0_REFCLK,
	PLL1_REFCLK,
	REFCLK_DIG,
};

enum wiz_refclk_div_sel {
	CMN_REFCLK_DIG_DIV,
	CMN_REFCLK1_DIG_DIV,
};

enum wiz_clock_input {
	WIZ_CORE_REFCLK,
	WIZ_EXT_REFCLK,
	WIZ_CORE_REFCLK1,
	WIZ_EXT_REFCLK1,
};

static const struct reg_field por_en = REG_FIELD(WIZ_SERDES_CTRL, 31, 31);
static const struct reg_field phy_reset_n = REG_FIELD(WIZ_SERDES_RST, 31, 31);
static const struct reg_field phy_en_refclk = REG_FIELD(WIZ_SERDES_RST, 30, 30);
static const struct reg_field pll1_refclk_mux_sel =
					REG_FIELD(WIZ_SERDES_RST, 29, 29);
static const struct reg_field pll0_refclk_mux_sel =
					REG_FIELD(WIZ_SERDES_RST, 28, 28);
static const struct reg_field refclk_dig_sel_16g =
					REG_FIELD(WIZ_SERDES_RST, 24, 25);
static const struct reg_field refclk_dig_sel_10g =
					REG_FIELD(WIZ_SERDES_RST, 24, 24);
static const struct reg_field pma_cmn_refclk_int_mode =
					REG_FIELD(WIZ_SERDES_TOP_CTRL, 28, 29);
static const struct reg_field pma_cmn_refclk_mode =
					REG_FIELD(WIZ_SERDES_TOP_CTRL, 30, 31);
static const struct reg_field pma_cmn_refclk_dig_div =
					REG_FIELD(WIZ_SERDES_TOP_CTRL, 26, 27);
static const struct reg_field pma_cmn_refclk1_dig_div =
					REG_FIELD(WIZ_SERDES_TOP_CTRL, 24, 25);
static const char * const output_clk_names[] = {
	[TI_WIZ_PLL0_REFCLK] = "pll0-refclk",
	[TI_WIZ_PLL1_REFCLK] = "pll1-refclk",
	[TI_WIZ_REFCLK_DIG] = "refclk-dig",
	[TI_WIZ_PHY_EN_REFCLK] = "phy-en-refclk",
};

static const struct reg_field p_enable[WIZ_MAX_LANES] = {
	REG_FIELD(WIZ_LANECTL(0), 30, 31),
	REG_FIELD(WIZ_LANECTL(1), 30, 31),
	REG_FIELD(WIZ_LANECTL(2), 30, 31),
	REG_FIELD(WIZ_LANECTL(3), 30, 31),
};

enum p_enable { P_ENABLE = 2, P_ENABLE_FORCE = 1, P_ENABLE_DISABLE = 0 };

static const struct reg_field p_align[WIZ_MAX_LANES] = {
	REG_FIELD(WIZ_LANECTL(0), 29, 29),
	REG_FIELD(WIZ_LANECTL(1), 29, 29),
	REG_FIELD(WIZ_LANECTL(2), 29, 29),
	REG_FIELD(WIZ_LANECTL(3), 29, 29),
};

static const struct reg_field p_raw_auto_start[WIZ_MAX_LANES] = {
	REG_FIELD(WIZ_LANECTL(0), 28, 28),
	REG_FIELD(WIZ_LANECTL(1), 28, 28),
	REG_FIELD(WIZ_LANECTL(2), 28, 28),
	REG_FIELD(WIZ_LANECTL(3), 28, 28),
};

static const struct reg_field p_standard_mode[WIZ_MAX_LANES] = {
	REG_FIELD(WIZ_LANECTL(0), 24, 25),
	REG_FIELD(WIZ_LANECTL(1), 24, 25),
	REG_FIELD(WIZ_LANECTL(2), 24, 25),
	REG_FIELD(WIZ_LANECTL(3), 24, 25),
};

static const struct reg_field p0_fullrt_div[WIZ_MAX_LANES] = {
	REG_FIELD(WIZ_LANECTL(0), 22, 23),
	REG_FIELD(WIZ_LANECTL(1), 22, 23),
	REG_FIELD(WIZ_LANECTL(2), 22, 23),
	REG_FIELD(WIZ_LANECTL(3), 22, 23),
};

static const struct reg_field p_mac_div_sel0[WIZ_MAX_LANES] = {
	REG_FIELD(WIZ_LANEDIV(0), 16, 22),
	REG_FIELD(WIZ_LANEDIV(1), 16, 22),
	REG_FIELD(WIZ_LANEDIV(2), 16, 22),
	REG_FIELD(WIZ_LANEDIV(3), 16, 22),
};

static const struct reg_field p_mac_div_sel1[WIZ_MAX_LANES] = {
	REG_FIELD(WIZ_LANEDIV(0), 0, 8),
	REG_FIELD(WIZ_LANEDIV(1), 0, 8),
	REG_FIELD(WIZ_LANEDIV(2), 0, 8),
	REG_FIELD(WIZ_LANEDIV(3), 0, 8),
};

static const struct reg_field typec_ln10_swap =
					REG_FIELD(WIZ_SERDES_TYPEC, 30, 30);

struct wiz_clk_mux {
	struct clk_hw		hw;
	struct regmap_field	*field;
	const u32		*table;
	struct clk_init_data	clk_data;
};

#define to_wiz_clk_mux(_hw) container_of(_hw, struct wiz_clk_mux, hw)

struct wiz_clk_divider {
	struct clk_hw		hw;
	struct regmap_field	*field;
	const struct clk_div_table	*table;
	struct clk_init_data	clk_data;
};

#define to_wiz_clk_div(_hw) container_of(_hw, struct wiz_clk_divider, hw)

struct wiz_clk_mux_sel {
	u32			table[WIZ_MAX_INPUT_CLOCKS];
	const char		*node_name;
	u32			num_parents;
	u32			parents[WIZ_MAX_INPUT_CLOCKS];
};

struct wiz_clk_div_sel {
	const struct clk_div_table *table;
	const char		*node_name;
};

struct wiz_phy_en_refclk {
	struct clk_hw		hw;
	struct regmap_field	*phy_en_refclk;
	struct clk_init_data	clk_data;
};

#define to_wiz_phy_en_refclk(_hw) container_of(_hw, struct wiz_phy_en_refclk, hw)

static const struct wiz_clk_mux_sel clk_mux_sel_16g[] = {
	{
		/*
		 * Mux value to be configured for each of the input clocks
		 * in the order populated in device tree
		 */
		.table = { 1, 0 },
		.node_name = "pll0-refclk",
	},
	{
		.table = { 1, 0 },
		.node_name = "pll1-refclk",
	},
	{
		.table = { 1, 3, 0, 2 },
		.node_name = "refclk-dig",
	},
};

static const struct wiz_clk_mux_sel clk_mux_sel_10g[] = {
	{
		/*
		 * Mux value to be configured for each of the input clocks
		 * in the order populated in device tree
		 */
		.num_parents = 2,
		.parents = { WIZ_CORE_REFCLK, WIZ_EXT_REFCLK },
		.table = { 1, 0 },
		.node_name = "pll0-refclk",
	},
	{
		.num_parents = 2,
		.parents = { WIZ_CORE_REFCLK, WIZ_EXT_REFCLK },
		.table = { 1, 0 },
		.node_name = "pll1-refclk",
	},
	{
		.num_parents = 2,
		.parents = { WIZ_CORE_REFCLK, WIZ_EXT_REFCLK },
		.table = { 1, 0 },
		.node_name = "refclk-dig",
	},
};

static const struct clk_div_table clk_div_table[] = {
	{ .val = 0, .div = 1, },
	{ .val = 1, .div = 2, },
	{ .val = 2, .div = 4, },
	{ .val = 3, .div = 8, },
};

static const struct wiz_clk_div_sel clk_div_sel[] = {
	{
		.table = clk_div_table,
		.node_name = "cmn-refclk-dig-div",
	},
	{
		.table = clk_div_table,
		.node_name = "cmn-refclk1-dig-div",
	},
};

enum wiz_type {
	J721E_WIZ_16G,
	J721E_WIZ_10G,
	AM64_WIZ_10G,
};

#define WIZ_TYPEC_DIR_DEBOUNCE_MIN	100	/* ms */
#define WIZ_TYPEC_DIR_DEBOUNCE_MAX	1000

struct wiz {
	struct regmap		*regmap;
	enum wiz_type		type;
	const struct wiz_clk_mux_sel *clk_mux_sel;
	const struct wiz_clk_div_sel *clk_div_sel;
	unsigned int		clk_div_sel_num;
	struct regmap_field	*por_en;
	struct regmap_field	*phy_reset_n;
	struct regmap_field	*phy_en_refclk;
	struct regmap_field	*p_enable[WIZ_MAX_LANES];
	struct regmap_field	*p_align[WIZ_MAX_LANES];
	struct regmap_field	*p_raw_auto_start[WIZ_MAX_LANES];
	struct regmap_field	*p_standard_mode[WIZ_MAX_LANES];
	struct regmap_field	*p_mac_div_sel0[WIZ_MAX_LANES];
	struct regmap_field	*p_mac_div_sel1[WIZ_MAX_LANES];
	struct regmap_field	*p0_fullrt_div[WIZ_MAX_LANES];
	struct regmap_field	*pma_cmn_refclk_int_mode;
	struct regmap_field	*pma_cmn_refclk_mode;
	struct regmap_field	*pma_cmn_refclk_dig_div;
	struct regmap_field	*pma_cmn_refclk1_dig_div;
	struct regmap_field	*mux_sel_field[WIZ_MUX_NUM_CLOCKS];
	struct regmap_field	*div_sel_field[WIZ_DIV_NUM_CLOCKS_16G];
	struct regmap_field	*typec_ln10_swap;

	struct device		*dev;
	u32			num_lanes;
	struct platform_device	*serdes_pdev;
	struct reset_controller_dev wiz_phy_reset_dev;
	struct gpio_desc	*gpio_typec_dir;
	int			typec_dir_delay;
	u32 lane_phy_type[WIZ_MAX_LANES];
	struct clk		*input_clks[WIZ_MAX_INPUT_CLOCKS];
	struct clk		*output_clks[WIZ_MAX_OUTPUT_CLOCKS];
	struct clk_onecell_data	clk_data;
};

static int wiz_reset(struct wiz *wiz)
{
	int ret;

	ret = regmap_field_write(wiz->por_en, 0x1);
	if (ret)
		return ret;

	mdelay(1);

	ret = regmap_field_write(wiz->por_en, 0x0);
	if (ret)
		return ret;

	return 0;
}

static int wiz_p_mac_div_sel(struct wiz *wiz)
{
	u32 num_lanes = wiz->num_lanes;
	int ret;
	int i;

	for (i = 0; i < num_lanes; i++) {
		if (wiz->lane_phy_type[i] == PHY_TYPE_QSGMII) {
			ret = regmap_field_write(wiz->p_mac_div_sel0[i], 1);
			if (ret)
				return ret;

			ret = regmap_field_write(wiz->p_mac_div_sel1[i], 2);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int wiz_mode_select(struct wiz *wiz)
{
	u32 num_lanes = wiz->num_lanes;
	enum wiz_lane_standard_mode mode;
	int ret;
	int i;

	for (i = 0; i < num_lanes; i++) {
		if (wiz->lane_phy_type[i] == PHY_TYPE_DP)
			mode = LANE_MODE_GEN1;
		else if (wiz->lane_phy_type[i] == PHY_TYPE_QSGMII)
			mode = LANE_MODE_GEN2;
		else
			continue;

		ret = regmap_field_write(wiz->p_standard_mode[i], mode);
		if (ret)
			return ret;
	}

	return 0;
}

static int wiz_init_raw_interface(struct wiz *wiz, bool enable)
{
	u32 num_lanes = wiz->num_lanes;
	int i;
	int ret;

	for (i = 0; i < num_lanes; i++) {
		ret = regmap_field_write(wiz->p_align[i], enable);
		if (ret)
			return ret;

		ret = regmap_field_write(wiz->p_raw_auto_start[i], enable);
		if (ret)
			return ret;
	}

	return 0;
}

static int wiz_init(struct wiz *wiz)
{
	struct device *dev = wiz->dev;
	int ret;

	ret = wiz_reset(wiz);
	if (ret) {
		dev_err(dev, "WIZ reset failed\n");
		return ret;
	}

	ret = wiz_mode_select(wiz);
	if (ret) {
		dev_err(dev, "WIZ mode select failed\n");
		return ret;
	}

	ret = wiz_p_mac_div_sel(wiz);
	if (ret) {
		dev_err(dev, "Configuring P0 MAC DIV SEL failed\n");
		return ret;
	}

	ret = wiz_init_raw_interface(wiz, true);
	if (ret) {
		dev_err(dev, "WIZ interface initialization failed\n");
		return ret;
	}

	return 0;
}

static int wiz_regfield_init(struct wiz *wiz)
{
	struct regmap *regmap = wiz->regmap;
	int num_lanes = wiz->num_lanes;
	struct device *dev = wiz->dev;
	int i;

	wiz->por_en = devm_regmap_field_alloc(dev, regmap, por_en);
	if (IS_ERR(wiz->por_en)) {
		dev_err(dev, "POR_EN reg field init failed\n");
		return PTR_ERR(wiz->por_en);
	}

	wiz->phy_reset_n = devm_regmap_field_alloc(dev, regmap,
						   phy_reset_n);
	if (IS_ERR(wiz->phy_reset_n)) {
		dev_err(dev, "PHY_RESET_N reg field init failed\n");
		return PTR_ERR(wiz->phy_reset_n);
	}

	wiz->pma_cmn_refclk_int_mode =
		devm_regmap_field_alloc(dev, regmap, pma_cmn_refclk_int_mode);
	if (IS_ERR(wiz->pma_cmn_refclk_int_mode)) {
		dev_err(dev, "PMA_CMN_REFCLK_INT_MODE reg field init failed\n");
		return PTR_ERR(wiz->pma_cmn_refclk_int_mode);
	}

	wiz->pma_cmn_refclk_mode =
		devm_regmap_field_alloc(dev, regmap, pma_cmn_refclk_mode);
	if (IS_ERR(wiz->pma_cmn_refclk_mode)) {
		dev_err(dev, "PMA_CMN_REFCLK_MODE reg field init failed\n");
		return PTR_ERR(wiz->pma_cmn_refclk_mode);
	}

	wiz->div_sel_field[CMN_REFCLK_DIG_DIV] =
		devm_regmap_field_alloc(dev, regmap, pma_cmn_refclk_dig_div);
	if (IS_ERR(wiz->div_sel_field[CMN_REFCLK_DIG_DIV])) {
		dev_err(dev, "PMA_CMN_REFCLK_DIG_DIV reg field init failed\n");
		return PTR_ERR(wiz->div_sel_field[CMN_REFCLK_DIG_DIV]);
	}

	if (wiz->type == J721E_WIZ_16G) {
		wiz->div_sel_field[CMN_REFCLK1_DIG_DIV] =
			devm_regmap_field_alloc(dev, regmap,
						pma_cmn_refclk1_dig_div);
		if (IS_ERR(wiz->div_sel_field[CMN_REFCLK1_DIG_DIV])) {
			dev_err(dev, "PMA_CMN_REFCLK1_DIG_DIV reg field init failed\n");
			return PTR_ERR(wiz->div_sel_field[CMN_REFCLK1_DIG_DIV]);
		}
	}

	wiz->mux_sel_field[PLL0_REFCLK] =
		devm_regmap_field_alloc(dev, regmap, pll0_refclk_mux_sel);
	if (IS_ERR(wiz->mux_sel_field[PLL0_REFCLK])) {
		dev_err(dev, "PLL0_REFCLK_SEL reg field init failed\n");
		return PTR_ERR(wiz->mux_sel_field[PLL0_REFCLK]);
	}

	wiz->mux_sel_field[PLL1_REFCLK] =
		devm_regmap_field_alloc(dev, regmap, pll1_refclk_mux_sel);
	if (IS_ERR(wiz->mux_sel_field[PLL1_REFCLK])) {
		dev_err(dev, "PLL1_REFCLK_SEL reg field init failed\n");
		return PTR_ERR(wiz->mux_sel_field[PLL1_REFCLK]);
	}

	if (wiz->type == J721E_WIZ_10G || wiz->type == AM64_WIZ_10G)
		wiz->mux_sel_field[REFCLK_DIG] =
			devm_regmap_field_alloc(dev, regmap,
						refclk_dig_sel_10g);
	else
		wiz->mux_sel_field[REFCLK_DIG] =
			devm_regmap_field_alloc(dev, regmap,
						refclk_dig_sel_16g);

	if (IS_ERR(wiz->mux_sel_field[REFCLK_DIG])) {
		dev_err(dev, "REFCLK_DIG_SEL reg field init failed\n");
		return PTR_ERR(wiz->mux_sel_field[REFCLK_DIG]);
	}

	for (i = 0; i < num_lanes; i++) {
		wiz->p_enable[i] = devm_regmap_field_alloc(dev, regmap,
							   p_enable[i]);
		if (IS_ERR(wiz->p_enable[i])) {
			dev_err(dev, "P%d_ENABLE reg field init failed\n", i);
			return PTR_ERR(wiz->p_enable[i]);
		}

		wiz->p_align[i] = devm_regmap_field_alloc(dev, regmap,
							  p_align[i]);
		if (IS_ERR(wiz->p_align[i])) {
			dev_err(dev, "P%d_ALIGN reg field init failed\n", i);
			return PTR_ERR(wiz->p_align[i]);
		}

		wiz->p_raw_auto_start[i] =
		  devm_regmap_field_alloc(dev, regmap, p_raw_auto_start[i]);
		if (IS_ERR(wiz->p_raw_auto_start[i])) {
			dev_err(dev, "P%d_RAW_AUTO_START reg field init fail\n",
				i);
			return PTR_ERR(wiz->p_raw_auto_start[i]);
		}

		wiz->p_standard_mode[i] =
		  devm_regmap_field_alloc(dev, regmap, p_standard_mode[i]);
		if (IS_ERR(wiz->p_standard_mode[i])) {
			dev_err(dev, "P%d_STANDARD_MODE reg field init fail\n",
				i);
			return PTR_ERR(wiz->p_standard_mode[i]);
		}

		wiz->p0_fullrt_div[i] = devm_regmap_field_alloc(dev, regmap, p0_fullrt_div[i]);
		if (IS_ERR(wiz->p0_fullrt_div[i])) {
			dev_err(dev, "P%d_FULLRT_DIV reg field init failed\n", i);
			return PTR_ERR(wiz->p0_fullrt_div[i]);
		}

		wiz->p_mac_div_sel0[i] =
		  devm_regmap_field_alloc(dev, regmap, p_mac_div_sel0[i]);
		if (IS_ERR(wiz->p_mac_div_sel0[i])) {
			dev_err(dev, "P%d_MAC_DIV_SEL0 reg field init fail\n",
				i);
			return PTR_ERR(wiz->p_mac_div_sel0[i]);
		}

		wiz->p_mac_div_sel1[i] =
		  devm_regmap_field_alloc(dev, regmap, p_mac_div_sel1[i]);
		if (IS_ERR(wiz->p_mac_div_sel1[i])) {
			dev_err(dev, "P%d_MAC_DIV_SEL1 reg field init fail\n",
				i);
			return PTR_ERR(wiz->p_mac_div_sel1[i]);
		}
	}

	wiz->typec_ln10_swap = devm_regmap_field_alloc(dev, regmap,
						       typec_ln10_swap);
	if (IS_ERR(wiz->typec_ln10_swap)) {
		dev_err(dev, "LN10_SWAP reg field init failed\n");
		return PTR_ERR(wiz->typec_ln10_swap);
	}

	wiz->phy_en_refclk = devm_regmap_field_alloc(dev, regmap, phy_en_refclk);
	if (IS_ERR(wiz->phy_en_refclk)) {
		dev_err(dev, "PHY_EN_REFCLK reg field init failed\n");
		return PTR_ERR(wiz->phy_en_refclk);
	}

	return 0;
}

static int wiz_phy_en_refclk_enable(struct clk_hw *hw)
{
	struct wiz_phy_en_refclk *wiz_phy_en_refclk = to_wiz_phy_en_refclk(hw);
	struct regmap_field *phy_en_refclk = wiz_phy_en_refclk->phy_en_refclk;

	regmap_field_write(phy_en_refclk, 1);

	return 0;
}

static void wiz_phy_en_refclk_disable(struct clk_hw *hw)
{
	struct wiz_phy_en_refclk *wiz_phy_en_refclk = to_wiz_phy_en_refclk(hw);
	struct regmap_field *phy_en_refclk = wiz_phy_en_refclk->phy_en_refclk;

	regmap_field_write(phy_en_refclk, 0);
}

static int wiz_phy_en_refclk_is_enabled(struct clk_hw *hw)
{
	struct wiz_phy_en_refclk *wiz_phy_en_refclk = to_wiz_phy_en_refclk(hw);
	struct regmap_field *phy_en_refclk = wiz_phy_en_refclk->phy_en_refclk;
	int val;

	regmap_field_read(phy_en_refclk, &val);

	return !!val;
}

static const struct clk_ops wiz_phy_en_refclk_ops = {
	.enable = wiz_phy_en_refclk_enable,
	.disable = wiz_phy_en_refclk_disable,
	.is_enabled = wiz_phy_en_refclk_is_enabled,
};

static int wiz_phy_en_refclk_register(struct wiz *wiz)
{
	struct wiz_phy_en_refclk *wiz_phy_en_refclk;
	struct device *dev = wiz->dev;
	struct clk_init_data *init;
	struct clk *clk;

	wiz_phy_en_refclk = devm_kzalloc(dev, sizeof(*wiz_phy_en_refclk), GFP_KERNEL);
	if (!wiz_phy_en_refclk)
		return -ENOMEM;

	init = &wiz_phy_en_refclk->clk_data;

	init->ops = &wiz_phy_en_refclk_ops;
	init->flags = 0;
	init->name = output_clk_names[TI_WIZ_PHY_EN_REFCLK];

	wiz_phy_en_refclk->phy_en_refclk = wiz->phy_en_refclk;
	wiz_phy_en_refclk->hw.init = init;

	clk = devm_clk_register(dev, &wiz_phy_en_refclk->hw);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	wiz->output_clks[TI_WIZ_PHY_EN_REFCLK] = clk;

	return 0;
}

static u8 wiz_clk_mux_get_parent(struct clk_hw *hw)
{
	struct wiz_clk_mux *mux = to_wiz_clk_mux(hw);
	struct regmap_field *field = mux->field;
	unsigned int val;

	regmap_field_read(field, &val);
	return clk_mux_val_to_index(hw, (u32 *)mux->table, 0, val);
}

static int wiz_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct wiz_clk_mux *mux = to_wiz_clk_mux(hw);
	struct regmap_field *field = mux->field;
	int val;

	val = mux->table[index];
	return regmap_field_write(field, val);
}

static const struct clk_ops wiz_clk_mux_ops = {
	.set_parent = wiz_clk_mux_set_parent,
	.get_parent = wiz_clk_mux_get_parent,
};

static int wiz_mux_clk_register(struct wiz *wiz, struct regmap_field *field,
				const struct wiz_clk_mux_sel *mux_sel, int clk_index)
{
	struct device *dev = wiz->dev;
	struct clk_init_data *init;
	const char **parent_names;
	unsigned int num_parents;
	struct wiz_clk_mux *mux;
	char clk_name[100];
	struct clk *clk;
	int ret = 0, i;

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	num_parents = mux_sel->num_parents;

	parent_names = kzalloc((sizeof(char *) * num_parents), GFP_KERNEL);
	if (!parent_names)
		return -ENOMEM;

	for (i = 0; i < num_parents; i++) {
		clk = wiz->input_clks[mux_sel->parents[i]];
		if (IS_ERR_OR_NULL(clk)) {
			dev_err(dev, "Failed to get parent clk for %s\n",
				output_clk_names[clk_index]);
			ret = -EINVAL;
			goto err;
		}
		parent_names[i] = __clk_get_name(clk);
	}

	snprintf(clk_name, sizeof(clk_name), "%s_%s", dev_name(dev), output_clk_names[clk_index]);

	init = &mux->clk_data;

	init->ops = &wiz_clk_mux_ops;
	init->flags = CLK_SET_RATE_NO_REPARENT;
	init->parent_names = parent_names;
	init->num_parents = num_parents;
	init->name = clk_name;

	mux->field = field;
	mux->table = mux_sel->table;
	mux->hw.init = init;

	clk = devm_clk_register(dev, &mux->hw);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto err;
	}

	wiz->output_clks[clk_index] = clk;

err:
	kfree(parent_names);

	return ret;
}

static int wiz_mux_of_clk_register(struct wiz *wiz, struct device_node *node,
				   struct regmap_field *field, const u32 *table)
{
	struct device *dev = wiz->dev;
	struct clk_init_data *init;
	const char **parent_names;
	unsigned int num_parents;
	struct wiz_clk_mux *mux;
	char clk_name[100];
	struct clk *clk;
	int ret;

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

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

	snprintf(clk_name, sizeof(clk_name), "%s_%s", dev_name(dev),
		 node->name);

	init = &mux->clk_data;

	init->ops = &wiz_clk_mux_ops;
	init->flags = CLK_SET_RATE_NO_REPARENT;
	init->parent_names = parent_names;
	init->num_parents = num_parents;
	init->name = clk_name;

	mux->field = field;
	mux->table = table;
	mux->hw.init = init;

	clk = devm_clk_register(dev, &mux->hw);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	if (ret)
		dev_err(dev, "Failed to add clock provider: %s\n", clk_name);

	return ret;
}

static unsigned long wiz_clk_div_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct wiz_clk_divider *div = to_wiz_clk_div(hw);
	struct regmap_field *field = div->field;
	int val;

	regmap_field_read(field, &val);

	return divider_recalc_rate(hw, parent_rate, val, div->table, 0x0, 2);
}

static long wiz_clk_div_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *prate)
{
	struct wiz_clk_divider *div = to_wiz_clk_div(hw);

	return divider_round_rate(hw, rate, prate, div->table, 2, 0x0);
}

static int wiz_clk_div_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct wiz_clk_divider *div = to_wiz_clk_div(hw);
	struct regmap_field *field = div->field;
	int val;

	val = divider_get_val(rate, parent_rate, div->table, 2, 0x0);
	if (val < 0)
		return val;

	return regmap_field_write(field, val);
}

static const struct clk_ops wiz_clk_div_ops = {
	.recalc_rate = wiz_clk_div_recalc_rate,
	.round_rate = wiz_clk_div_round_rate,
	.set_rate = wiz_clk_div_set_rate,
};

static int wiz_div_clk_register(struct wiz *wiz, struct device_node *node,
				struct regmap_field *field,
				const struct clk_div_table *table)
{
	struct device *dev = wiz->dev;
	struct wiz_clk_divider *div;
	struct clk_init_data *init;
	const char **parent_names;
	char clk_name[100];
	struct clk *clk;
	int ret;

	div = devm_kzalloc(dev, sizeof(*div), GFP_KERNEL);
	if (!div)
		return -ENOMEM;

	snprintf(clk_name, sizeof(clk_name), "%s_%s", dev_name(dev),
		 node->name);

	parent_names = devm_kzalloc(dev, sizeof(char *), GFP_KERNEL);
	if (!parent_names)
		return -ENOMEM;

	of_clk_parent_fill(node, parent_names, 1);

	init = &div->clk_data;

	init->ops = &wiz_clk_div_ops;
	init->flags = 0;
	init->parent_names = parent_names;
	init->num_parents = 1;
	init->name = clk_name;

	div->field = field;
	div->table = table;
	div->hw.init = init;

	clk = devm_clk_register(dev, &div->hw);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	if (ret)
		dev_err(dev, "Failed to add clock provider: %s\n", clk_name);

	return ret;
}

static void wiz_clock_cleanup(struct wiz *wiz, struct device_node *node)
{
	const struct wiz_clk_mux_sel *clk_mux_sel = wiz->clk_mux_sel;
	struct device *dev = wiz->dev;
	struct device_node *clk_node;
	int i;

	if (wiz->type == AM64_WIZ_10G) {
		of_clk_del_provider(dev->of_node);
		return;
	}

	for (i = 0; i < WIZ_MUX_NUM_CLOCKS; i++) {
		clk_node = of_get_child_by_name(node, clk_mux_sel[i].node_name);
		of_clk_del_provider(clk_node);
		of_node_put(clk_node);
	}

	for (i = 0; i < wiz->clk_div_sel_num; i++) {
		clk_node = of_get_child_by_name(node, clk_div_sel[i].node_name);
		of_clk_del_provider(clk_node);
		of_node_put(clk_node);
	}

	of_clk_del_provider(wiz->dev->of_node);
}

static int wiz_clock_register(struct wiz *wiz)
{
	const struct wiz_clk_mux_sel *clk_mux_sel = wiz->clk_mux_sel;
	struct device *dev = wiz->dev;
	struct device_node *node = dev->of_node;
	int clk_index;
	int ret;
	int i;

	if (wiz->type != AM64_WIZ_10G)
		return 0;

	clk_index = TI_WIZ_PLL0_REFCLK;
	for (i = 0; i < WIZ_MUX_NUM_CLOCKS; i++, clk_index++) {
		ret = wiz_mux_clk_register(wiz, wiz->mux_sel_field[i], &clk_mux_sel[i], clk_index);
		if (ret) {
			dev_err(dev, "Failed to register clk: %s\n", output_clk_names[clk_index]);
			return ret;
		}
	}

	ret = wiz_phy_en_refclk_register(wiz);
	if (ret) {
		dev_err(dev, "Failed to add phy-en-refclk\n");
		return ret;
	}

	wiz->clk_data.clks = wiz->output_clks;
	wiz->clk_data.clk_num = WIZ_MAX_OUTPUT_CLOCKS;
	ret = of_clk_add_provider(node, of_clk_src_onecell_get, &wiz->clk_data);
	if (ret)
		dev_err(dev, "Failed to add clock provider: %s\n", node->name);

	return ret;
}

static int wiz_clock_init(struct wiz *wiz, struct device_node *node)
{
	const struct wiz_clk_mux_sel *clk_mux_sel = wiz->clk_mux_sel;
	struct device *dev = wiz->dev;
	struct device_node *clk_node;
	const char *node_name;
	unsigned long rate;
	struct clk *clk;
	int ret;
	int i;

	clk = devm_clk_get(dev, "core_ref_clk");
	if (IS_ERR(clk)) {
		dev_err(dev, "core_ref_clk clock not found\n");
		ret = PTR_ERR(clk);
		return ret;
	}
	wiz->input_clks[WIZ_CORE_REFCLK] = clk;

	rate = clk_get_rate(clk);
	if (rate >= 100000000)
		regmap_field_write(wiz->pma_cmn_refclk_int_mode, 0x1);
	else
		regmap_field_write(wiz->pma_cmn_refclk_int_mode, 0x3);

	clk = devm_clk_get(dev, "ext_ref_clk");
	if (IS_ERR(clk)) {
		dev_err(dev, "ext_ref_clk clock not found\n");
		ret = PTR_ERR(clk);
		return ret;
	}
	wiz->input_clks[WIZ_EXT_REFCLK] = clk;

	rate = clk_get_rate(clk);
	if (rate >= 100000000)
		regmap_field_write(wiz->pma_cmn_refclk_mode, 0x0);
	else
		regmap_field_write(wiz->pma_cmn_refclk_mode, 0x2);

	if (wiz->type == AM64_WIZ_10G) {
		ret = wiz_clock_register(wiz);
		if (ret)
			dev_err(dev, "Failed to register wiz clocks\n");
		return ret;
	}

	for (i = 0; i < WIZ_MUX_NUM_CLOCKS; i++) {
		node_name = clk_mux_sel[i].node_name;
		clk_node = of_get_child_by_name(node, node_name);
		if (!clk_node) {
			dev_err(dev, "Unable to get %s node\n", node_name);
			ret = -EINVAL;
			goto err;
		}

		ret = wiz_mux_of_clk_register(wiz, clk_node, wiz->mux_sel_field[i],
					      clk_mux_sel[i].table);
		if (ret) {
			dev_err(dev, "Failed to register %s clock\n",
				node_name);
			of_node_put(clk_node);
			goto err;
		}

		of_node_put(clk_node);
	}

	for (i = 0; i < wiz->clk_div_sel_num; i++) {
		node_name = clk_div_sel[i].node_name;
		clk_node = of_get_child_by_name(node, node_name);
		if (!clk_node) {
			dev_err(dev, "Unable to get %s node\n", node_name);
			ret = -EINVAL;
			goto err;
		}

		ret = wiz_div_clk_register(wiz, clk_node, wiz->div_sel_field[i],
					   clk_div_sel[i].table);
		if (ret) {
			dev_err(dev, "Failed to register %s clock\n",
				node_name);
			of_node_put(clk_node);
			goto err;
		}

		of_node_put(clk_node);
	}

	return 0;
err:
	wiz_clock_cleanup(wiz, node);

	return ret;
}

static int wiz_phy_reset_assert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct device *dev = rcdev->dev;
	struct wiz *wiz = dev_get_drvdata(dev);
	int ret = 0;

	if (id == 0) {
		ret = regmap_field_write(wiz->phy_reset_n, false);
		return ret;
	}

	ret = regmap_field_write(wiz->p_enable[id - 1], P_ENABLE_DISABLE);
	return ret;
}

static int wiz_phy_fullrt_div(struct wiz *wiz, int lane)
{
	if (wiz->type != AM64_WIZ_10G)
		return 0;

	if (wiz->lane_phy_type[lane] == PHY_TYPE_PCIE)
		return regmap_field_write(wiz->p0_fullrt_div[lane], 0x1);

	return 0;
}

static int wiz_phy_reset_deassert(struct reset_controller_dev *rcdev,
				  unsigned long id)
{
	struct device *dev = rcdev->dev;
	struct wiz *wiz = dev_get_drvdata(dev);
	int ret;

	/* if typec-dir gpio was specified, set LN10 SWAP bit based on that */
	if (id == 0 && wiz->gpio_typec_dir) {
		if (wiz->typec_dir_delay)
			msleep_interruptible(wiz->typec_dir_delay);

		if (gpiod_get_value_cansleep(wiz->gpio_typec_dir))
			regmap_field_write(wiz->typec_ln10_swap, 1);
		else
			regmap_field_write(wiz->typec_ln10_swap, 0);
	}

	if (id == 0) {
		ret = regmap_field_write(wiz->phy_reset_n, true);
		return ret;
	}

	ret = wiz_phy_fullrt_div(wiz, id - 1);
	if (ret)
		return ret;

	if (wiz->lane_phy_type[id - 1] == PHY_TYPE_DP)
		ret = regmap_field_write(wiz->p_enable[id - 1], P_ENABLE);
	else
		ret = regmap_field_write(wiz->p_enable[id - 1], P_ENABLE_FORCE);

	return ret;
}

static const struct reset_control_ops wiz_phy_reset_ops = {
	.assert = wiz_phy_reset_assert,
	.deassert = wiz_phy_reset_deassert,
};

static const struct regmap_config wiz_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.fast_io = true,
};

static const struct of_device_id wiz_id_table[] = {
	{
		.compatible = "ti,j721e-wiz-16g", .data = (void *)J721E_WIZ_16G
	},
	{
		.compatible = "ti,j721e-wiz-10g", .data = (void *)J721E_WIZ_10G
	},
	{
		.compatible = "ti,am64-wiz-10g", .data = (void *)AM64_WIZ_10G
	},
	{}
};
MODULE_DEVICE_TABLE(of, wiz_id_table);

static int wiz_get_lane_phy_types(struct device *dev, struct wiz *wiz)
{
	struct device_node *serdes, *subnode;

	serdes = of_get_child_by_name(dev->of_node, "serdes");
	if (!serdes) {
		dev_err(dev, "%s: Getting \"serdes\"-node failed\n", __func__);
		return -EINVAL;
	}

	for_each_child_of_node(serdes, subnode) {
		u32 reg, num_lanes = 1, phy_type = PHY_NONE;
		int ret, i;

		if (!(of_node_name_eq(subnode, "phy") ||
		      of_node_name_eq(subnode, "link")))
			continue;

		ret = of_property_read_u32(subnode, "reg", &reg);
		if (ret) {
			of_node_put(subnode);
			dev_err(dev,
				"%s: Reading \"reg\" from \"%s\" failed: %d\n",
				__func__, subnode->name, ret);
			return ret;
		}
		of_property_read_u32(subnode, "cdns,num-lanes", &num_lanes);
		of_property_read_u32(subnode, "cdns,phy-type", &phy_type);

		dev_dbg(dev, "%s: Lanes %u-%u have phy-type %u\n", __func__,
			reg, reg + num_lanes - 1, phy_type);

		for (i = reg; i < reg + num_lanes; i++)
			wiz->lane_phy_type[i] = phy_type;
	}

	return 0;
}

static int wiz_probe(struct platform_device *pdev)
{
	struct reset_controller_dev *phy_reset_dev;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct platform_device *serdes_pdev;
	bool already_configured = false;
	struct device_node *child_node;
	struct regmap *regmap;
	struct resource res;
	void __iomem *base;
	struct wiz *wiz;
	int ret, val, i;
	u32 num_lanes;

	wiz = devm_kzalloc(dev, sizeof(*wiz), GFP_KERNEL);
	if (!wiz)
		return -ENOMEM;

	wiz->type = (enum wiz_type)of_device_get_match_data(dev);

	child_node = of_get_child_by_name(node, "serdes");
	if (!child_node) {
		dev_err(dev, "Failed to get SERDES child DT node\n");
		return -ENODEV;
	}

	ret = of_address_to_resource(child_node, 0, &res);
	if (ret) {
		dev_err(dev, "Failed to get memory resource\n");
		goto err_addr_to_resource;
	}

	base = devm_ioremap(dev, res.start, resource_size(&res));
	if (!base) {
		ret = -ENOMEM;
		goto err_addr_to_resource;
	}

	regmap = devm_regmap_init_mmio(dev, base, &wiz_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to initialize regmap\n");
		ret = PTR_ERR(regmap);
		goto err_addr_to_resource;
	}

	ret = of_property_read_u32(node, "num-lanes", &num_lanes);
	if (ret) {
		dev_err(dev, "Failed to read num-lanes property\n");
		goto err_addr_to_resource;
	}

	if (num_lanes > WIZ_MAX_LANES) {
		dev_err(dev, "Cannot support %d lanes\n", num_lanes);
		ret = -ENODEV;
		goto err_addr_to_resource;
	}

	wiz->gpio_typec_dir = devm_gpiod_get_optional(dev, "typec-dir",
						      GPIOD_IN);
	if (IS_ERR(wiz->gpio_typec_dir)) {
		ret = PTR_ERR(wiz->gpio_typec_dir);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to request typec-dir gpio: %d\n",
				ret);
		goto err_addr_to_resource;
	}

	if (wiz->gpio_typec_dir) {
		ret = of_property_read_u32(node, "typec-dir-debounce-ms",
					   &wiz->typec_dir_delay);
		if (ret && ret != -EINVAL) {
			dev_err(dev, "Invalid typec-dir-debounce property\n");
			goto err_addr_to_resource;
		}

		/* use min. debounce from Type-C spec if not provided in DT  */
		if (ret == -EINVAL)
			wiz->typec_dir_delay = WIZ_TYPEC_DIR_DEBOUNCE_MIN;

		if (wiz->typec_dir_delay < WIZ_TYPEC_DIR_DEBOUNCE_MIN ||
		    wiz->typec_dir_delay > WIZ_TYPEC_DIR_DEBOUNCE_MAX) {
			ret = -EINVAL;
			dev_err(dev, "Invalid typec-dir-debounce property\n");
			goto err_addr_to_resource;
		}
	}

	ret = wiz_get_lane_phy_types(dev, wiz);
	if (ret)
		return ret;

	wiz->dev = dev;
	wiz->regmap = regmap;
	wiz->num_lanes = num_lanes;
	if (wiz->type == J721E_WIZ_10G || wiz->type == AM64_WIZ_10G)
		wiz->clk_mux_sel = clk_mux_sel_10g;
	else
		wiz->clk_mux_sel = clk_mux_sel_16g;

	wiz->clk_div_sel = clk_div_sel;

	if (wiz->type == J721E_WIZ_10G || wiz->type == AM64_WIZ_10G)
		wiz->clk_div_sel_num = WIZ_DIV_NUM_CLOCKS_10G;
	else
		wiz->clk_div_sel_num = WIZ_DIV_NUM_CLOCKS_16G;

	platform_set_drvdata(pdev, wiz);

	ret = wiz_regfield_init(wiz);
	if (ret) {
		dev_err(dev, "Failed to initialize regfields\n");
		goto err_addr_to_resource;
	}

	phy_reset_dev = &wiz->wiz_phy_reset_dev;
	phy_reset_dev->dev = dev;
	phy_reset_dev->ops = &wiz_phy_reset_ops,
	phy_reset_dev->owner = THIS_MODULE,
	phy_reset_dev->of_node = node;
	/* Reset for each of the lane and one for the entire SERDES */
	phy_reset_dev->nr_resets = num_lanes + 1;

	ret = devm_reset_controller_register(dev, phy_reset_dev);
	if (ret < 0) {
		dev_warn(dev, "Failed to register reset controller\n");
		goto err_addr_to_resource;
	}

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "pm_runtime_get_sync failed\n");
		goto err_get_sync;
	}

	ret = wiz_clock_init(wiz, node);
	if (ret < 0) {
		dev_warn(dev, "Failed to initialize clocks\n");
		goto err_get_sync;
	}

	for (i = 0; i < wiz->num_lanes; i++) {
		regmap_field_read(wiz->p_enable[i], &val);
		if (val & (P_ENABLE | P_ENABLE_FORCE)) {
			already_configured = true;
			break;
		}
	}

	if (!already_configured) {
		ret = wiz_init(wiz);
		if (ret) {
			dev_err(dev, "WIZ initialization failed\n");
			goto err_wiz_init;
		}
	}

	serdes_pdev = of_platform_device_create(child_node, NULL, dev);
	if (!serdes_pdev) {
		dev_WARN(dev, "Unable to create SERDES platform device\n");
		ret = -ENOMEM;
		goto err_wiz_init;
	}
	wiz->serdes_pdev = serdes_pdev;

	of_node_put(child_node);
	return 0;

err_wiz_init:
	wiz_clock_cleanup(wiz, node);

err_get_sync:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

err_addr_to_resource:
	of_node_put(child_node);

	return ret;
}

static int wiz_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct platform_device *serdes_pdev;
	struct wiz *wiz;

	wiz = dev_get_drvdata(dev);
	serdes_pdev = wiz->serdes_pdev;

	of_platform_device_destroy(&serdes_pdev->dev, NULL);
	wiz_clock_cleanup(wiz, node);
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return 0;
}

static struct platform_driver wiz_driver = {
	.probe		= wiz_probe,
	.remove		= wiz_remove,
	.driver		= {
		.name	= "wiz",
		.of_match_table = wiz_id_table,
	},
};
module_platform_driver(wiz_driver);

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TI J721E WIZ driver");
MODULE_LICENSE("GPL v2");
