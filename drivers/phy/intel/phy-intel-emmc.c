// SPDX-License-Identifier: GPL-2.0
/*
 * Intel eMMC PHY driver
 * Copyright (C) 2019 Intel, Corp.
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* eMMC phy register definitions */
#define EMMC_PHYCTRL0_REG	0xa8
#define DR_TY_MASK		GENMASK(30, 28)
#define DR_TY_SHIFT(x)		(((x) << 28) & DR_TY_MASK)
#define OTAPDLYENA		BIT(14)
#define OTAPDLYSEL_MASK		GENMASK(13, 10)
#define OTAPDLYSEL_SHIFT(x)	(((x) << 10) & OTAPDLYSEL_MASK)

#define EMMC_PHYCTRL1_REG	0xac
#define PDB_MASK		BIT(0)
#define PDB_SHIFT(x)		(((x) << 0) & PDB_MASK)
#define ENDLL_MASK		BIT(7)
#define ENDLL_SHIFT(x)		(((x) << 7) & ENDLL_MASK)

#define EMMC_PHYCTRL2_REG	0xb0
#define FRQSEL_25M		0
#define FRQSEL_50M		1
#define FRQSEL_100M		2
#define FRQSEL_150M		3
#define FRQSEL_MASK		GENMASK(24, 22)
#define FRQSEL_SHIFT(x)		(((x) << 22) & FRQSEL_MASK)

#define EMMC_PHYSTAT_REG	0xbc
#define CALDONE_MASK		BIT(9)
#define DLLRDY_MASK		BIT(8)
#define IS_CALDONE(x)	((x) & CALDONE_MASK)
#define IS_DLLRDY(x)	((x) & DLLRDY_MASK)

struct intel_emmc_phy {
	struct regmap *syscfg;
	struct clk *emmcclk;
};

static int intel_emmc_phy_power(struct phy *phy, bool on_off)
{
	struct intel_emmc_phy *priv = phy_get_drvdata(phy);
	unsigned int caldone;
	unsigned int dllrdy;
	unsigned int freqsel;
	unsigned long rate;
	int ret, quot;

	/*
	 * Keep phyctrl_pdb and phyctrl_endll low to allow
	 * initialization of CALIO state M/C DFFs
	 */
	ret = regmap_update_bits(priv->syscfg, EMMC_PHYCTRL1_REG, PDB_MASK,
				 PDB_SHIFT(0));
	if (ret) {
		dev_err(&phy->dev, "CALIO power down bar failed: %d\n", ret);
		return ret;
	}

	/* Already finish power_off above */
	if (!on_off)
		return 0;

	rate = clk_get_rate(priv->emmcclk);
	quot = DIV_ROUND_CLOSEST(rate, 50000000);
	if (quot > FRQSEL_150M)
		dev_warn(&phy->dev, "Unsupported rate: %lu\n", rate);
	freqsel = clamp_t(int, quot, FRQSEL_25M, FRQSEL_150M);

	/*
	 * According to the user manual, calpad calibration
	 * cycle takes more than 2us without the minimal recommended
	 * value, so we may need a little margin here
	 */
	udelay(5);

	ret = regmap_update_bits(priv->syscfg, EMMC_PHYCTRL1_REG, PDB_MASK,
				 PDB_SHIFT(1));
	if (ret) {
		dev_err(&phy->dev, "CALIO power down bar failed: %d\n", ret);
		return ret;
	}

	/*
	 * According to the user manual, it asks driver to wait 5us for
	 * calpad busy trimming. However it is documented that this value is
	 * PVT(A.K.A process,voltage and temperature) relevant, so some
	 * failure cases are found which indicates we should be more tolerant
	 * to calpad busy trimming.
	 */
	ret = regmap_read_poll_timeout(priv->syscfg, EMMC_PHYSTAT_REG,
				       caldone, IS_CALDONE(caldone),
				       0, 50);
	if (ret) {
		dev_err(&phy->dev, "caldone failed, ret=%d\n", ret);
		return ret;
	}

	/* Set the frequency of the DLL operation */
	ret = regmap_update_bits(priv->syscfg, EMMC_PHYCTRL2_REG, FRQSEL_MASK,
				 FRQSEL_SHIFT(freqsel));
	if (ret) {
		dev_err(&phy->dev, "set the frequency of dll failed:%d\n", ret);
		return ret;
	}

	/* Turn on the DLL */
	ret = regmap_update_bits(priv->syscfg, EMMC_PHYCTRL1_REG, ENDLL_MASK,
				 ENDLL_SHIFT(1));
	if (ret) {
		dev_err(&phy->dev, "turn on the dll failed: %d\n", ret);
		return ret;
	}

	/*
	 * After enabling analog DLL circuits docs say that we need 10.2 us if
	 * our source clock is at 50 MHz and that lock time scales linearly
	 * with clock speed.  If we are powering on the PHY and the card clock
	 * is super slow (like 100 kHZ) this could take as long as 5.1 ms as
	 * per the math: 10.2 us * (50000000 Hz / 100000 Hz) => 5.1 ms
	 * Hopefully we won't be running at 100 kHz, but we should still make
	 * sure we wait long enough.
	 *
	 * NOTE: There appear to be corner cases where the DLL seems to take
	 * extra long to lock for reasons that aren't understood.  In some
	 * extreme cases we've seen it take up to over 10ms (!).  We'll be
	 * generous and give it 50ms.
	 */
	ret = regmap_read_poll_timeout(priv->syscfg,
				       EMMC_PHYSTAT_REG,
				       dllrdy, IS_DLLRDY(dllrdy),
				       0, 50 * USEC_PER_MSEC);
	if (ret) {
		dev_err(&phy->dev, "dllrdy failed. ret=%d\n", ret);
		return ret;
	}

	return 0;
}

static int intel_emmc_phy_init(struct phy *phy)
{
	struct intel_emmc_phy *priv = phy_get_drvdata(phy);

	/*
	 * We purposely get the clock here and not in probe to avoid the
	 * circular dependency problem. We expect:
	 * - PHY driver to probe
	 * - SDHCI driver to start probe
	 * - SDHCI driver to register it's clock
	 * - SDHCI driver to get the PHY
	 * - SDHCI driver to init the PHY
	 *
	 * The clock is optional, so upon any error just return it like
	 * any other error to user.
	 *
	 */
	priv->emmcclk = clk_get_optional(&phy->dev, "emmcclk");
	if (IS_ERR(priv->emmcclk)) {
		dev_err(&phy->dev, "ERROR: getting emmcclk\n");
		return PTR_ERR(priv->emmcclk);
	}

	return 0;
}

static int intel_emmc_phy_exit(struct phy *phy)
{
	struct intel_emmc_phy *priv = phy_get_drvdata(phy);

	clk_put(priv->emmcclk);

	return 0;
}

static int intel_emmc_phy_power_on(struct phy *phy)
{
	struct intel_emmc_phy *priv = phy_get_drvdata(phy);
	int ret;

	/* Drive impedance: 50 Ohm */
	ret = regmap_update_bits(priv->syscfg, EMMC_PHYCTRL0_REG, DR_TY_MASK,
				 DR_TY_SHIFT(6));
	if (ret) {
		dev_err(&phy->dev, "ERROR set drive-impednce-50ohm: %d\n", ret);
		return ret;
	}

	/* Output tap delay: disable */
	ret = regmap_update_bits(priv->syscfg, EMMC_PHYCTRL0_REG, OTAPDLYENA,
				 0);
	if (ret) {
		dev_err(&phy->dev, "ERROR Set output tap delay : %d\n", ret);
		return ret;
	}

	/* Output tap delay */
	ret = regmap_update_bits(priv->syscfg, EMMC_PHYCTRL0_REG,
				 OTAPDLYSEL_MASK, OTAPDLYSEL_SHIFT(4));
	if (ret) {
		dev_err(&phy->dev, "ERROR: output tap dly select: %d\n", ret);
		return ret;
	}

	/* Power up eMMC phy analog blocks */
	return intel_emmc_phy_power(phy, true);
}

static int intel_emmc_phy_power_off(struct phy *phy)
{
	/* Power down eMMC phy analog blocks */
	return intel_emmc_phy_power(phy, false);
}

static const struct phy_ops ops = {
	.init		= intel_emmc_phy_init,
	.exit		= intel_emmc_phy_exit,
	.power_on	= intel_emmc_phy_power_on,
	.power_off	= intel_emmc_phy_power_off,
	.owner		= THIS_MODULE,
};

static int intel_emmc_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct intel_emmc_phy *priv;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Get eMMC phy (accessed via chiptop) regmap */
	priv->syscfg = syscon_regmap_lookup_by_phandle(np, "intel,syscon");
	if (IS_ERR(priv->syscfg)) {
		dev_err(dev, "failed to find syscon\n");
		return PTR_ERR(priv->syscfg);
	}

	generic_phy = devm_phy_create(dev, np, &ops);
	if (IS_ERR(generic_phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(generic_phy);
	}

	phy_set_drvdata(generic_phy, priv);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id intel_emmc_phy_dt_ids[] = {
	{ .compatible = "intel,lgm-emmc-phy" },
	{}
};

MODULE_DEVICE_TABLE(of, intel_emmc_phy_dt_ids);

static struct platform_driver intel_emmc_driver = {
	.probe		= intel_emmc_phy_probe,
	.driver		= {
		.name	= "intel-emmc-phy",
		.of_match_table = intel_emmc_phy_dt_ids,
	},
};

module_platform_driver(intel_emmc_driver);

MODULE_AUTHOR("Peter Harliman Liem <peter.harliman.liem@intel.com>");
MODULE_DESCRIPTION("Intel eMMC PHY driver");
MODULE_LICENSE("GPL v2");
