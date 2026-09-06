// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
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
 * On the IPQ5332 SoC, the CMN PLL provides a single 50 MHZ clock output to
 * the Ethernet PHY (or switch) via the UNIPHY (PCS). It also supplies a 200
 * MHZ clock to the PPE. The remaining fixed-rate clocks to the GCC and PCS
 * are the same as those in the IPQ9574 SoC.
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
#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,ipq-cmn-pll.h>
#include <dt-bindings/clock/qcom,ipq5018-cmn-pll.h>
#include <dt-bindings/clock/qcom,ipq5332-cmn-pll.h>
#include <dt-bindings/clock/qcom,ipq5424-cmn-pll.h>
#include <dt-bindings/clock/qcom,ipq6018-cmn-pll.h>
#include <dt-bindings/clock/qcom,ipq8074-cmn-pll.h>

#include "clk-regmap.h"
#include "clk-regmap-divider.h"

#define CMN_PLL_REFCLK_SRC_SELECTION		0x28
#define CMN_PLL_REFCLK_SRC_DIV			GENMASK(9, 8)

#define CMN_PLL_LOCKED				0x64
#define CMN_PLL_CLKS_LOCKED			BIT(8)

#define CMN_PLL_NSS_PPE_FREQ_CTRL		0x98
#define CMN_PLL_NSS_CLK_SEL			GENMASK(13, 8)
#define CMN_PLL_PPE_CLK_SEL			GENMASK(5, 0)

#define CMN_PLL_PON_CONFIG			0x42c
#define CMN_PLL_PON_MODE_SEL			BIT(9)
#define CMN_PLL_PON_EN				BIT(8)
#define CMN_PLL_PON_DIV_CTRL			GENMASK(7, 0)
#define CMN_PLL_GEPHY_312P5M_125M_SEL		BIT(10)

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
 * enum cmn_pll_clk_type - CMN PLL output clock registration type
 * @CMN_PLL_CLK_FIXED_RATE: plain fixed rate clock
 * @CMN_PLL_CLK_NSS: NSS clock with configurable divider
 * @CMN_PLL_CLK_PPE: PPE clock with configurable divider
 * @CMN_PLL_CLK_PON: PON reference clock
 * @CMN_PLL_CLK_EPHY_RAW: EPHY-RAW clock
 */
enum cmn_pll_clk_type {
	CMN_PLL_CLK_FIXED_RATE,
	CMN_PLL_CLK_NSS,
	CMN_PLL_CLK_PPE,
	CMN_PLL_CLK_PON,
	CMN_PLL_CLK_EPHY_RAW,
};

/**
 * struct cmn_pll_fixed_output_clk - CMN PLL output clocks information
 * @id:	Clock specifier to be supplied
 * @name: Clock name to be registered
 * @type: Clock registration type
 * @rate: Clock rate
 */
struct cmn_pll_fixed_output_clk {
	unsigned int id;
	const char *name;
	enum cmn_pll_clk_type type;
	unsigned long rate;
};

/**
 * struct clk_cmn_pll - CMN PLL hardware specific data
 * @regmap: hardware regmap.
 * @hw: handle between common and hardware-specific interfaces
 * @div2_hw: fixed /2 clock derived from the CMN PLL output; present on
 *           every supported SoC.
 */
struct clk_cmn_pll {
	struct regmap *regmap;
	struct clk_hw hw;
	struct clk_hw *div2_hw;
};

#define CLK_PLL_OUTPUT(_id, _name, _rate) {		\
	.id =		_id,				\
	.name =		_name,				\
	.type =		CMN_PLL_CLK_FIXED_RATE,		\
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

static const struct cmn_pll_fixed_output_clk ipq6018_output_clks[] = {
	CLK_PLL_OUTPUT(IPQ6018_BIAS_PLL_CC_CLK, "bias_pll_cc_clk", 300000000UL),
	CLK_PLL_OUTPUT(IPQ6018_BIAS_PLL_NSS_NOC_CLK, "bias_pll_nss_noc_clk", 416500000UL),
	{ /* Sentinel */ }
};

static const struct cmn_pll_fixed_output_clk ipq8074_output_clks[] = {
	CLK_PLL_OUTPUT(IPQ8074_BIAS_PLL_CC_CLK, "bias_pll_cc_clk", 300000000UL),
	CLK_PLL_OUTPUT(IPQ8074_BIAS_PLL_NSS_NOC_CLK, "bias_pll_nss_noc_clk", 416500000UL),
	{ /* Sentinel */ }
};

static const struct cmn_pll_fixed_output_clk ipq5332_output_clks[] = {
	CLK_PLL_OUTPUT(IPQ5332_XO_24MHZ_CLK, "xo-24mhz", 24000000UL),
	CLK_PLL_OUTPUT(IPQ5332_SLEEP_32KHZ_CLK, "sleep-32khz", 32000UL),
	CLK_PLL_OUTPUT(IPQ5332_PCS_31P25MHZ_CLK, "pcs-31p25mhz", 31250000UL),
	CLK_PLL_OUTPUT(IPQ5332_NSS_300MHZ_CLK, "nss-300mhz", 300000000UL),
	CLK_PLL_OUTPUT(IPQ5332_PPE_200MHZ_CLK, "ppe-200mhz", 200000000UL),
	CLK_PLL_OUTPUT(IPQ5332_ETH_50MHZ_CLK, "eth-50mhz", 50000000UL),
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
	u32 val, factor, ref_div;

	/*
	 * The value of CMN_PLL_DIVIDER_CTRL_FACTOR is automatically adjusted
	 * by HW according to the parent clock rate.
	 */
	regmap_read(cmn_pll->regmap, CMN_PLL_DIVIDER_CTRL, &val);
	factor = FIELD_GET(CMN_PLL_DIVIDER_CTRL_FACTOR, val);
	if (WARN_ON(factor == 0))
		factor = 1;

	regmap_read(cmn_pll->regmap, CMN_PLL_REFCLK_CONFIG, &val);
	ref_div = FIELD_GET(CMN_PLL_REFCLK_DIV, val);
	if (WARN_ON(ref_div == 0))
		ref_div = 1;

	return div_u64((u64)parent_rate * 2 * factor, ref_div);
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

static struct clk_hw *ipq_cmn_pll_regmap_div_register(struct device *dev,
						      const char *name,
						      struct clk_hw *parent_hw,
						      u32 field_mask)
{
	struct clk_parent_data pdata = { .hw = parent_hw };
	struct clk_regmap_div *div_clk;
	int ret;

	div_clk = devm_kzalloc(dev, sizeof(*div_clk), GFP_KERNEL);
	if (!div_clk)
		return ERR_PTR(-ENOMEM);

	div_clk->reg = CMN_PLL_NSS_PPE_FREQ_CTRL;
	div_clk->shift = __ffs(field_mask);
	div_clk->width = hweight32(field_mask);
	/*
	 * CMN_PLL_NSS_CLK_SEL / CMN_PLL_PPE_CLK_SEL reset to a valid, non-zero
	 * divider in hardware. CLK_DIVIDER_ALLOW_ZERO is deliberately not set:
	 * a divider field read back as 0 is genuinely invalid and should trip
	 * the core clk-divider's zero-divisor WARN rather than be silently
	 * tolerated.
	 */
	div_clk->flags = CLK_DIVIDER_ONE_BASED;
	div_clk->clkr.hw.init = &(struct clk_init_data){
		.name = name,
		.parent_data = &pdata,
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	};

	ret = devm_clk_register_regmap(dev, &div_clk->clkr);
	if (ret)
		return ERR_PTR(ret);

	return &div_clk->clkr.hw;
}

/*
 * PON (Passive Optical Network) reference clock operations.
 * The PON refclk's parent is cmn_pll_div2 (CMN PLL rate / 2); it is
 * then divided by a configurable 8-bit divider (1-255).
 */
static unsigned long clk_pon_clk_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct clk_regmap *pon_clk = to_clk_regmap(hw);
	u32 val, div;
	int ret;

	ret = regmap_read(pon_clk->regmap, CMN_PLL_PON_CONFIG, &val);
	if (ret)
		return 0;

	/* Check if in UNIPHY mode (bit 9 = 0) - fixed 31.25 MHz */
	if (!(val & CMN_PLL_PON_MODE_SEL))
		return 31250000UL;

	/* PON mode: calculate from divider */
	div = FIELD_GET(CMN_PLL_PON_DIV_CTRL, val);
	if (!div)
		return 0;

	return DIV_ROUND_CLOSEST_ULL((u64)parent_rate, div);
}

static int clk_pon_clk_determine_rate(struct clk_hw *hw,
				      struct clk_rate_request *req)
{
	unsigned long div, pon_rate, uniphy_rate = 31250000UL;
	bool uniphy_rate_valid, pon_rate_valid;

	if (!req->rate || !req->best_parent_rate)
		return -EINVAL;

	div = DIV64_U64_ROUND_CLOSEST((u64)req->best_parent_rate, req->rate);

	/* Clamp to valid range (1-255) */
	div = clamp_t(unsigned long, div, 1, 255);

	pon_rate = DIV_ROUND_CLOSEST_ULL((u64)req->best_parent_rate, div);

	uniphy_rate_valid = uniphy_rate >= req->min_rate &&
			    uniphy_rate <= req->max_rate;
	pon_rate_valid = pon_rate >= req->min_rate &&
			 pon_rate <= req->max_rate;

	if (!uniphy_rate_valid && !pon_rate_valid)
		return -EINVAL;

	/* Pick whichever mode gets closer to the requested rate */
	if (uniphy_rate_valid && pon_rate_valid) {
		unsigned long diff_uniphy, diff_pon;

		diff_uniphy = abs_diff(req->rate, uniphy_rate);
		diff_pon = abs_diff(req->rate, pon_rate);
		req->rate = diff_uniphy < diff_pon ? uniphy_rate : pon_rate;
	} else {
		req->rate = uniphy_rate_valid ? uniphy_rate : pon_rate;
	}

	return 0;
}

static int clk_pon_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_regmap *pon_clk = to_clk_regmap(hw);
	unsigned long div;

	if (rate == 0)
		return -EINVAL;

	/*
	 * An exact request for 31.25 MHz is always satisfiable by UNIPHY
	 * mode, even though PON mode with a suitable divider can produce
	 * the same frequency for some parent rates. Preferring UNIPHY here
	 * is safe: this function is only ever called with a rate produced
	 * by clk_pon_clk_determine_rate() through the standard
	 * clk_set_rate() path, and either mode yields the identical output
	 * rate for this value.
	 */
	if (rate == 31250000UL)
		return regmap_clear_bits(pon_clk->regmap, CMN_PLL_PON_CONFIG,
					 CMN_PLL_PON_MODE_SEL);

	div = DIV64_U64_ROUND_CLOSEST((u64)parent_rate, rate);
	if (div == 0 || div > 255)
		return -EINVAL;

	/* Switch to PON mode and program the divider in a single write */
	return regmap_update_bits(pon_clk->regmap, CMN_PLL_PON_CONFIG,
				  CMN_PLL_PON_MODE_SEL | CMN_PLL_PON_DIV_CTRL,
				  CMN_PLL_PON_MODE_SEL |
				  FIELD_PREP(CMN_PLL_PON_DIV_CTRL, div));
}

static const struct clk_ops clk_pon_clk_ops = {
	.enable = clk_enable_regmap,
	.disable = clk_disable_regmap,
	.is_enabled = clk_is_enabled_regmap,
	.recalc_rate = clk_pon_clk_recalc_rate,
	.determine_rate = clk_pon_clk_determine_rate,
	.set_rate = clk_pon_clk_set_rate,
};

static struct clk_hw *ipq_cmn_pll_pon_clk_register(struct device *dev,
						   const char *name,
						   struct clk_hw *parent_hw)
{
	struct clk_parent_data pdata = { .hw = parent_hw };
	struct clk_init_data init = {};
	struct clk_regmap *pon_clk;
	int ret;

	pon_clk = devm_kzalloc(dev, sizeof(*pon_clk), GFP_KERNEL);
	if (!pon_clk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.parent_data = &pdata;
	init.num_parents = 1;
	init.ops = &clk_pon_clk_ops;
	/*
	 * The PON reference clock may already be enabled by bootloader
	 * or consumed by hardware without an in-kernel client driver.
	 * Add CLK_IGNORE_UNUSED so the clock framework does not disable
	 * it when no consumer has claimed it.
	 */
	init.flags = CLK_IGNORE_UNUSED;

	pon_clk->hw.init = &init;
	pon_clk->enable_reg = CMN_PLL_PON_CONFIG;
	pon_clk->enable_mask = CMN_PLL_PON_EN;

	ret = devm_clk_register_regmap(dev, pon_clk);
	if (ret)
		return ERR_PTR(ret);

	return &pon_clk->hw;
}

/*
 * EPHY-RAW clock operations for IPQ5210.
 * The output clock rate is selected via bit 10 of CMN_PLL_PON_CONFIG:
 *   0: 125 MHz  (for 1G link speed)
 *   1: 312.5 MHz (for 2.5G link speed)
 */
static unsigned long clk_ephy_raw_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct clk_regmap *ephy_raw_clk = to_clk_regmap(hw);
	u32 val;
	int ret;

	ret = regmap_read(ephy_raw_clk->regmap, CMN_PLL_PON_CONFIG, &val);
	if (WARN_ON_ONCE(ret))
		return 0;

	if (val & CMN_PLL_GEPHY_312P5M_125M_SEL)
		return 312500000UL;

	return 125000000UL;
}

static int clk_ephy_raw_determine_rate(struct clk_hw *hw,
				       struct clk_rate_request *req)
{
	unsigned long rate_125m = 125000000UL, rate_312p5m = 312500000UL;
	bool rate_125m_valid, rate_312p5m_valid;

	rate_125m_valid = rate_125m >= req->min_rate &&
			  rate_125m <= req->max_rate;
	rate_312p5m_valid = rate_312p5m >= req->min_rate &&
			    rate_312p5m <= req->max_rate;

	if (!rate_125m_valid && !rate_312p5m_valid)
		return -EINVAL;

	/* Pick whichever of the two supported rates is closer to the request */
	if (rate_125m_valid && rate_312p5m_valid) {
		unsigned long diff_125m, diff_312p5m;

		diff_125m = abs_diff(req->rate, rate_125m);
		diff_312p5m = abs_diff(req->rate, rate_312p5m);
		req->rate = diff_125m < diff_312p5m ? rate_125m : rate_312p5m;
	} else {
		req->rate = rate_125m_valid ? rate_125m : rate_312p5m;
	}

	return 0;
}

static int clk_ephy_raw_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct clk_regmap *ephy_raw_clk = to_clk_regmap(hw);

	if (rate == 125000000UL)
		return regmap_clear_bits(ephy_raw_clk->regmap,
					 CMN_PLL_PON_CONFIG,
					 CMN_PLL_GEPHY_312P5M_125M_SEL);

	if (rate == 312500000UL)
		return regmap_set_bits(ephy_raw_clk->regmap,
				       CMN_PLL_PON_CONFIG,
				       CMN_PLL_GEPHY_312P5M_125M_SEL);

	return -EINVAL;
}

static const struct clk_ops clk_ephy_raw_ops = {
	.recalc_rate = clk_ephy_raw_recalc_rate,
	.determine_rate = clk_ephy_raw_determine_rate,
	.set_rate = clk_ephy_raw_set_rate,
};

static struct clk_hw *ipq_cmn_pll_ephy_raw_register(struct device *dev,
						    const char *name,
						    struct clk_hw *parent_hw)
{
	struct clk_parent_data pdata = { .hw = parent_hw };
	struct clk_regmap *ephy_raw_clk;
	struct clk_init_data init = {};
	int ret;

	ephy_raw_clk = devm_kzalloc(dev, sizeof(*ephy_raw_clk), GFP_KERNEL);
	if (!ephy_raw_clk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.parent_data = &pdata;
	init.num_parents = 1;
	init.ops = &clk_ephy_raw_ops;

	ephy_raw_clk->hw.init = &init;

	ret = devm_clk_register_regmap(dev, ephy_raw_clk);
	if (ret)
		return ERR_PTR(ret);

	return &ephy_raw_clk->hw;
}

static int ipq_cmn_pll_register_clks(struct platform_device *pdev)
{
	const struct cmn_pll_fixed_output_clk *p, *fixed_clk;
	struct clk_hw_onecell_data *hw_data;
	struct device *dev = &pdev->dev;
	struct clk_cmn_pll *cmn_pll;
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

	cmn_pll = to_clk_cmn_pll(cmn_pll_hw);
	cmn_pll->div2_hw = devm_clk_hw_register_fixed_factor_parent_hw(dev,
								       "cmn_pll_div2",
								       cmn_pll_hw,
								       0, 1, 2);
	if (IS_ERR(cmn_pll->div2_hw))
		return PTR_ERR(cmn_pll->div2_hw);

	/* Register the fixed rate output clocks. */
	for (i = 0; i < num_clks; i++) {
		hw = ERR_PTR(-EINVAL);

		switch (fixed_clk[i].type) {
		case CMN_PLL_CLK_FIXED_RATE: {
			struct clk_parent_data pdata = { .hw = cmn_pll_hw };

			hw = devm_clk_hw_register_fixed_rate_parent_data(dev,
									 fixed_clk[i].name,
									 &pdata, 0,
									 fixed_clk[i].rate);
			break;
		}
		case CMN_PLL_CLK_NSS:
			hw = ipq_cmn_pll_regmap_div_register(dev,
							     fixed_clk[i].name,
							     cmn_pll->div2_hw,
							     CMN_PLL_NSS_CLK_SEL);
			break;
		case CMN_PLL_CLK_PPE:
			hw = ipq_cmn_pll_regmap_div_register(dev,
							     fixed_clk[i].name,
							     cmn_pll->div2_hw,
							     CMN_PLL_PPE_CLK_SEL);
			break;
		case CMN_PLL_CLK_PON:
			hw = ipq_cmn_pll_pon_clk_register(dev,
							  fixed_clk[i].name,
							  cmn_pll->div2_hw);
			break;
		case CMN_PLL_CLK_EPHY_RAW:
			hw = ipq_cmn_pll_ephy_raw_register(dev,
							   fixed_clk[i].name,
							   cmn_pll->div2_hw);
			break;
		}

		if (IS_ERR(hw))
			return PTR_ERR(hw);

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
		return ret;

	platform_set_drvdata(pdev, hw_data);

	return 0;
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

static const struct dev_pm_ops ipq_cmn_pll_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_clk_suspend, pm_clk_resume, NULL)
};

static const struct of_device_id ipq_cmn_pll_clk_ids[] = {
	{ .compatible = "qcom,ipq5018-cmn-pll", .data = &ipq5018_output_clks },
	{ .compatible = "qcom,ipq5332-cmn-pll", .data = &ipq5332_output_clks },
	{ .compatible = "qcom,ipq5424-cmn-pll", .data = &ipq5424_output_clks },
	{ .compatible = "qcom,ipq6018-cmn-pll", .data = &ipq6018_output_clks },
	{ .compatible = "qcom,ipq8074-cmn-pll", .data = &ipq8074_output_clks },
	{ .compatible = "qcom,ipq9574-cmn-pll", .data = &ipq9574_output_clks },
	{ }
};
MODULE_DEVICE_TABLE(of, ipq_cmn_pll_clk_ids);

static struct platform_driver ipq_cmn_pll_clk_driver = {
	.probe = ipq_cmn_pll_clk_probe,
	.driver = {
		.name = "ipq_cmn_pll",
		.of_match_table = ipq_cmn_pll_clk_ids,
		.pm = &ipq_cmn_pll_pm_ops,
	},
};
module_platform_driver(ipq_cmn_pll_clk_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. IPQ CMN PLL Driver");
MODULE_LICENSE("GPL");
