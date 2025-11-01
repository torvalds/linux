// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

/*
 * CMN PLL block expects the reference clock from on-board Wi-Fi block,
 * and supplies fixed rate clocks as output to the networking hardware
 * blocks and to GCC. The networking related blocks include PPE (packet
 * process engine), the externally connected PHY or switch devices, and
 * the PCS.
 *
 * On the IPQ9574 SoC, there are three clocks with 50 MHZ and one clock
 * with 25 MHZ which are output from the CMN PLL to Ethernet PHY (or switch),
 * and one clock with 353 MHZ to PPE. The other fixed rate output clocks
 * are supplied to GCC (24 MHZ as XO and 32 KHZ as sleep clock), and to PCS
 * with 31.25 MHZ.
 *
 * On the IPQ5424 SoC, there is an output clock from CMN PLL to PPE at 375 MHZ,
 * and an output clock to NSS (network subsystem) at 300 MHZ. The other output
 * clocks from CMN PLL on IPQ5424 are the same as IPQ9574.
 *
 *               +---------+
 *               |   GCC   |
 *               +--+---+--+
 *           AHB CLK|   |SYS CLK
 *                  V   V
 *          +-------+---+------+
 *          |                  +-------------> eth0-50mhz
 * REF CLK  |     IPQ9574      |
 * -------->+                  +-------------> eth1-50mhz
 *          |  CMN PLL block   |
 *          |                  +-------------> eth2-50mhz
 *          |                  |
 *          +----+----+----+---+-------------> eth-25mhz
 *               |    |    |
 *               V    V    V
 *              GCC  PCS  NSS/PPE
 */

#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,ipq-cmn-pll.h>
#include <dt-bindings/clock/qcom,ipq5018-cmn-pll.h>
#include <dt-bindings/clock/qcom,ipq5424-cmn-pll.h>

#define CMN_PLL_REFCLK_SRC_SELECTION		0x28
#define CMN_PLL_REFCLK_SRC_DIV			GENMASK(9, 8)

#define CMN_PLL_LOCKED				0x64
#define CMN_PLL_CLKS_LOCKED			BIT(8)

#define CMN_PLL_POWER_ON_AND_RESET		0x780
#define CMN_ANA_EN_SW_RSTN			BIT(6)

#define CMN_PLL_REFCLK_CONFIG			0x784
#define CMN_PLL_REFCLK_EXTERNAL			BIT(9)
#define CMN_PLL_REFCLK_DIV			GENMASK(8, 4)
#define CMN_PLL_REFCLK_INDEX			GENMASK(3, 0)

#define CMN_PLL_CTRL				0x78c
#define CMN_PLL_CTRL_LOCK_DETECT_EN		BIT(15)

#define CMN_PLL_DIVIDER_CTRL			0x794
#define CMN_PLL_DIVIDER_CTRL_FACTOR		GENMASK(9, 0)

/**
 * struct cmn_pll_fixed_output_clk - CMN PLL output clocks information
 * @id:	Clock specifier to be supplied
 * @name: Clock name to be registered
 * @rate: Clock rate
 */
struct cmn_pll_fixed_output_clk {
	unsigned int id;
	const char *name;
	unsigned long rate;
};

/**
 * struct clk_cmn_pll - CMN PLL hardware specific data
 * @regmap: hardware regmap.
 * @hw: handle between common and hardware-specific interfaces
 */
struct clk_cmn_pll {
	struct regmap *regmap;
	struct clk_hw hw;
};

#define CLK_PLL_OUTPUT(_id, _name, _rate) {		\
	.id =		_id,				\
	.name =		_name,				\
	.rate =		_rate,				\
}

#define to_clk_cmn_pll(_hw) container_of(_hw, struct clk_cmn_pll, hw)

static const struct regmap_config ipq_cmn_pll_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x7fc,
};

static const struct cmn_pll_fixed_output_clk ipq5018_output_clks[] = {
	CLK_PLL_OUTPUT(IPQ5018_XO_24MHZ_CLK, "xo-24mhz", 24000000UL),
	CLK_PLL_OUTPUT(IPQ5018_SLEEP_32KHZ_CLK, "sleep-32khz", 32000UL),
	CLK_PLL_OUTPUT(IPQ5018_ETH_50MHZ_CLK, "eth-50mhz", 50000000UL),
	{ /* Sentinel */ }
};

static const struct cmn_pll_fixed_output_clk ipq5424_output_clks[] = {
	CLK_PLL_OUTPUT(IPQ5424_XO_24MHZ_CLK, "xo-24mhz", 24000000UL),
	CLK_PLL_OUTPUT(IPQ5424_SLEEP_32KHZ_CLK, "sleep-32khz", 32000UL),
	CLK_PLL_OUTPUT(IPQ5424_PCS_31P25MHZ_CLK, "pcs-31p25mhz", 31250000UL),
	CLK_PLL_OUTPUT(IPQ5424_NSS_300MHZ_CLK, "nss-300mhz", 300000000UL),
	CLK_PLL_OUTPUT(IPQ5424_PPE_375MHZ_CLK, "ppe-375mhz", 375000000UL),
	CLK_PLL_OUTPUT(IPQ5424_ETH0_50MHZ_CLK, "eth0-50mhz", 50000000UL),
	CLK_PLL_OUTPUT(IPQ5424_ETH1_50MHZ_CLK, "eth1-50mhz", 50000000UL),
	CLK_PLL_OUTPUT(IPQ5424_ETH2_50MHZ_CLK, "eth2-50mhz", 50000000UL),
	CLK_PLL_OUTPUT(IPQ5424_ETH_25MHZ_CLK, "eth-25mhz", 25000000UL),
	{ /* Sentinel */ }
};

static const struct cmn_pll_fixed_output_clk ipq9574_output_clks[] = {
	CLK_PLL_OUTPUT(XO_24MHZ_CLK, "xo-24mhz", 24000000UL),
	CLK_PLL_OUTPUT(SLEEP_32KHZ_CLK, "sleep-32khz", 32000UL),
	CLK_PLL_OUTPUT(PCS_31P25MHZ_CLK, "pcs-31p25mhz", 31250000UL),
	CLK_PLL_OUTPUT(NSS_1200MHZ_CLK, "nss-1200mhz", 1200000000UL),
	CLK_PLL_OUTPUT(PPE_353MHZ_CLK, "ppe-353mhz", 353000000UL),
	CLK_PLL_OUTPUT(ETH0_50MHZ_CLK, "eth0-50mhz", 50000000UL),
	CLK_PLL_OUTPUT(ETH1_50MHZ_CLK, "eth1-50mhz", 50000000UL),
	CLK_PLL_OUTPUT(ETH2_50MHZ_CLK, "eth2-50mhz", 50000000UL),
	CLK_PLL_OUTPUT(ETH_25MHZ_CLK, "eth-25mhz", 25000000UL),
	{ /* Sentinel */ }
};

/*
 * CMN PLL has the single parent clock, which supports the several
 * possible parent clock rates, each parent clock rate is reflected
 * by the specific reference index value in the hardware.
 */
static int ipq_cmn_pll_find_freq_index(unsigned long parent_rate)
{
	int index = -EINVAL;

	switch (parent_rate) {
	case 25000000:
		index = 3;
		break;
	case 31250000:
		index = 4;
		break;
	case 40000000:
		index = 6;
		break;
	case 48000000:
	case 96000000:
		/*
		 * Parent clock rate 48 MHZ and 96 MHZ take the same value
		 * of reference clock index. 96 MHZ needs the source clock
		 * divider to be programmed as 2.
		 */
		index = 7;
		break;
	case 50000000:
		index = 8;
		break;
	default:
		break;
	}

	return index;
}

static unsigned long clk_cmn_pll_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct clk_cmn_pll *cmn_pll = to_clk_cmn_pll(hw);
	u32 val, factor;

	/*
	 * The value of CMN_PLL_DIVIDER_CTRL_FACTOR is automatically adjusted
	 * by HW according to the parent clock rate.
	 */
	regmap_read(cmn_pll->regmap, CMN_PLL_DIVIDER_CTRL, &val);
	factor = FIELD_GET(CMN_PLL_DIVIDER_CTRL_FACTOR, val);

	return parent_rate * 2 * factor;
}

static int clk_cmn_pll_determine_rate(struct clk_hw *hw,
				      struct clk_rate_request *req)
{
	int ret;

	/* Validate the rate of the single parent clock. */
	ret = ipq_cmn_pll_find_freq_index(req->best_parent_rate);

	return ret < 0 ? ret : 0;
}

/*
 * This function is used to initialize the CMN PLL to enable the fixed
 * rate output clocks. It is expected to be configured once.
 */
static int clk_cmn_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_cmn_pll *cmn_pll = to_clk_cmn_pll(hw);
	int ret, index;
	u32 val;

	/*
	 * Configure the reference input clock selection as per the given
	 * parent clock. The output clock rates are always of fixed value.
	 */
	index = ipq_cmn_pll_find_freq_index(parent_rate);
	if (index < 0)
		return index;

	ret = regmap_update_bits(cmn_pll->regmap, CMN_PLL_REFCLK_CONFIG,
				 CMN_PLL_REFCLK_INDEX,
				 FIELD_PREP(CMN_PLL_REFCLK_INDEX, index));
	if (ret)
		return ret;

	/*
	 * Update the source clock rate selection and source clock
	 * divider as 2 when the parent clock rate is 96 MHZ.
	 */
	if (parent_rate == 96000000) {
		ret = regmap_update_bits(cmn_pll->regmap, CMN_PLL_REFCLK_CONFIG,
					 CMN_PLL_REFCLK_DIV,
					 FIELD_PREP(CMN_PLL_REFCLK_DIV, 2));
		if (ret)
			return ret;

		ret = regmap_update_bits(cmn_pll->regmap, CMN_PLL_REFCLK_SRC_SELECTION,
					 CMN_PLL_REFCLK_SRC_DIV,
					 FIELD_PREP(CMN_PLL_REFCLK_SRC_DIV, 0));
		if (ret)
			return ret;
	}

	/* Enable PLL locked detect. */
	ret = regmap_set_bits(cmn_pll->regmap, CMN_PLL_CTRL,
			      CMN_PLL_CTRL_LOCK_DETECT_EN);
	if (ret)
		return ret;

	/*
	 * Reset the CMN PLL block to ensure the updated configurations
	 * take effect.
	 */
	ret = regmap_clear_bits(cmn_pll->regmap, CMN_PLL_POWER_ON_AND_RESET,
				CMN_ANA_EN_SW_RSTN);
	if (ret)
		return ret;

	usleep_range(1000, 1200);
	ret = regmap_set_bits(cmn_pll->regmap, CMN_PLL_POWER_ON_AND_RESET,
			      CMN_ANA_EN_SW_RSTN);
	if (ret)
		return ret;

	/* Stability check of CMN PLL output clocks. */
	return regmap_read_poll_timeout(cmn_pll->regmap, CMN_PLL_LOCKED, val,
					(val & CMN_PLL_CLKS_LOCKED),
					100, 100 * USEC_PER_MSEC);
}

static const struct clk_ops clk_cmn_pll_ops = {
	.recalc_rate = clk_cmn_pll_recalc_rate,
	.determine_rate = clk_cmn_pll_determine_rate,
	.set_rate = clk_cmn_pll_set_rate,
};

static struct clk_hw *ipq_cmn_pll_clk_hw_register(struct platform_device *pdev)
{
	struct clk_parent_data pdata = { .index = 0 };
	struct device *dev = &pdev->dev;
	struct clk_init_data init = {};
	struct clk_cmn_pll *cmn_pll;
	struct regmap *regmap;
	void __iomem *base;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return ERR_CAST(base);

	regmap = devm_regmap_init_mmio(dev, base, &ipq_cmn_pll_regmap_config);
	if (IS_ERR(regmap))
		return ERR_CAST(regmap);

	cmn_pll = devm_kzalloc(dev, sizeof(*cmn_pll), GFP_KERNEL);
	if (!cmn_pll)
		return ERR_PTR(-ENOMEM);

	init.name = "cmn_pll";
	init.parent_data = &pdata;
	init.num_parents = 1;
	init.ops = &clk_cmn_pll_ops;

	cmn_pll->hw.init = &init;
	cmn_pll->regmap = regmap;

	ret = devm_clk_hw_register(dev, &cmn_pll->hw);
	if (ret)
		return ERR_PTR(ret);

	return &cmn_pll->hw;
}

static int ipq_cmn_pll_register_clks(struct platform_device *pdev)
{
	const struct cmn_pll_fixed_output_clk *p, *fixed_clk;
	struct clk_hw_onecell_data *hw_data;
	struct device *dev = &pdev->dev;
	struct clk_hw *cmn_pll_hw;
	unsigned int num_clks;
	struct clk_hw *hw;
	int ret, i;

	fixed_clk = device_get_match_data(dev);
	if (!fixed_clk)
		return -EINVAL;

	num_clks = 0;
	for (p = fixed_clk; p->name; p++)
		num_clks++;

	hw_data = devm_kzalloc(dev, struct_size(hw_data, hws, num_clks + 1),
			       GFP_KERNEL);
	if (!hw_data)
		return -ENOMEM;

	/*
	 * Register the CMN PLL clock, which is the parent clock of
	 * the fixed rate output clocks.
	 */
	cmn_pll_hw = ipq_cmn_pll_clk_hw_register(pdev);
	if (IS_ERR(cmn_pll_hw))
		return PTR_ERR(cmn_pll_hw);

	/* Register the fixed rate output clocks. */
	for (i = 0; i < num_clks; i++) {
		hw = clk_hw_register_fixed_rate_parent_hw(dev, fixed_clk[i].name,
							  cmn_pll_hw, 0,
							  fixed_clk[i].rate);
		if (IS_ERR(hw)) {
			ret = PTR_ERR(hw);
			goto unregister_fixed_clk;
		}

		hw_data->hws[fixed_clk[i].id] = hw;
	}

	/*
	 * Provide the CMN PLL clock. The clock rate of CMN PLL
	 * is configured to 12 GHZ by DT property assigned-clock-rates-u64.
	 */
	hw_data->hws[CMN_PLL_CLK] = cmn_pll_hw;
	hw_data->num = num_clks + 1;

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, hw_data);
	if (ret)
		goto unregister_fixed_clk;

	platform_set_drvdata(pdev, hw_data);

	return 0;

unregister_fixed_clk:
	while (i > 0)
		clk_hw_unregister(hw_data->hws[fixed_clk[--i].id]);

	return ret;
}

static int ipq_cmn_pll_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = devm_pm_clk_create(dev);
	if (ret)
		return ret;

	/*
	 * To access the CMN PLL registers, the GCC AHB & SYS clocks
	 * of CMN PLL block need to be enabled.
	 */
	ret = pm_clk_add(dev, "ahb");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add AHB clock\n");

	ret = pm_clk_add(dev, "sys");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add SYS clock\n");

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	/* Register CMN PLL clock and fixed rate output clocks. */
	ret = ipq_cmn_pll_register_clks(pdev);
	pm_runtime_put(dev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register CMN PLL clocks\n");

	return 0;
}

static void ipq_cmn_pll_clk_remove(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *hw_data = platform_get_drvdata(pdev);
	int i;

	/*
	 * The clock with index CMN_PLL_CLK is unregistered by
	 * device management.
	 */
	for (i = 0; i < hw_data->num; i++) {
		if (i != CMN_PLL_CLK)
			clk_hw_unregister(hw_data->hws[i]);
	}
}

static const struct dev_pm_ops ipq_cmn_pll_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_clk_suspend, pm_clk_resume, NULL)
};

static const struct of_device_id ipq_cmn_pll_clk_ids[] = {
	{ .compatible = "qcom,ipq5018-cmn-pll", .data = &ipq5018_output_clks },
	{ .compatible = "qcom,ipq5424-cmn-pll", .data = &ipq5424_output_clks },
	{ .compatible = "qcom,ipq9574-cmn-pll", .data = &ipq9574_output_clks },
	{ }
};
MODULE_DEVICE_TABLE(of, ipq_cmn_pll_clk_ids);

static struct platform_driver ipq_cmn_pll_clk_driver = {
	.probe = ipq_cmn_pll_clk_probe,
	.remove = ipq_cmn_pll_clk_remove,
	.driver = {
		.name = "ipq_cmn_pll",
		.of_match_table = ipq_cmn_pll_clk_ids,
		.pm = &ipq_cmn_pll_pm_ops,
	},
};
module_platform_driver(ipq_cmn_pll_clk_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. IPQ CMN PLL Driver");
MODULE_LICENSE("GPL");
