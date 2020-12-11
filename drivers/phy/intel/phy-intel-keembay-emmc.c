// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay eMMC PHY driver
 * Copyright (C) 2020 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* eMMC/SD/SDIO core/phy configuration registers */
#define PHY_CFG_0		0x24
#define  SEL_DLY_TXCLK_MASK	BIT(29)
#define  OTAP_DLY_ENA_MASK	BIT(27)
#define  OTAP_DLY_SEL_MASK	GENMASK(26, 23)
#define  DLL_EN_MASK		BIT(10)
#define  PWR_DOWN_MASK		BIT(0)

#define PHY_CFG_2		0x2c
#define  SEL_FREQ_MASK		GENMASK(12, 10)

#define PHY_STAT		0x40
#define  CAL_DONE_MASK		BIT(6)
#define  IS_CALDONE(x)		((x) & CAL_DONE_MASK)
#define  DLL_RDY_MASK		BIT(5)
#define  IS_DLLRDY(x)		((x) & DLL_RDY_MASK)

/* From ACS_eMMC51_16nFFC_RO1100_Userguide_v1p0.pdf p17 */
#define FREQSEL_200M_170M	0x0
#define FREQSEL_170M_140M	0x1
#define FREQSEL_140M_110M	0x2
#define FREQSEL_110M_80M	0x3
#define FREQSEL_80M_50M		0x4

struct keembay_emmc_phy {
	struct regmap *syscfg;
	struct clk *emmcclk;
};

static const struct regmap_config keembay_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int keembay_emmc_phy_power(struct phy *phy, bool on_off)
{
	struct keembay_emmc_phy *priv = phy_get_drvdata(phy);
	unsigned int caldone;
	unsigned int dllrdy;
	unsigned int freqsel;
	unsigned int mhz;
	int ret;

	/*
	 * Keep phyctrl_pdb and phyctrl_endll low to allow
	 * initialization of CALIO state M/C DFFs
	 */
	ret = regmap_update_bits(priv->syscfg, PHY_CFG_0, PWR_DOWN_MASK,
				 FIELD_PREP(PWR_DOWN_MASK, 0));
	if (ret) {
		dev_err(&phy->dev, "CALIO power down bar failed: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(priv->syscfg, PHY_CFG_0, DLL_EN_MASK,
				 FIELD_PREP(DLL_EN_MASK, 0));
	if (ret) {
		dev_err(&phy->dev, "turn off the dll failed: %d\n", ret);
		return ret;
	}

	/* Already finish power off above */
	if (!on_off)
		return 0;

	mhz = DIV_ROUND_CLOSEST(clk_get_rate(priv->emmcclk), 1000000);
	if (mhz <= 200 && mhz >= 170)
		freqsel = FREQSEL_200M_170M;
	else if (mhz <= 170 && mhz >= 140)
		freqsel = FREQSEL_170M_140M;
	else if (mhz <= 140 && mhz >= 110)
		freqsel = FREQSEL_140M_110M;
	else if (mhz <= 110 && mhz >= 80)
		freqsel = FREQSEL_110M_80M;
	else if (mhz <= 80 && mhz >= 50)
		freqsel = FREQSEL_80M_50M;
	else
		freqsel = 0x0;

	if (mhz < 50 || mhz > 200)
		dev_warn(&phy->dev, "Unsupported rate: %d MHz\n", mhz);

	/*
	 * According to the user manual, calpad calibration
	 * cycle takes more than 2us without the minimal recommended
	 * value, so we may need a little margin here
	 */
	udelay(5);

	ret = regmap_update_bits(priv->syscfg, PHY_CFG_0, PWR_DOWN_MASK,
				 FIELD_PREP(PWR_DOWN_MASK, 1));
	if (ret) {
		dev_err(&phy->dev, "CALIO power down bar failed: %d\n", ret);
		return ret;
	}

	/*
	 * According to the user manual, it asks driver to wait 5us for
	 * calpad busy trimming. However it is documented that this value is
	 * PVT(A.K.A. process, voltage and temperature) relevant, so some
	 * failure cases are found which indicates we should be more tolerant
	 * to calpad busy trimming.
	 */
	ret = regmap_read_poll_timeout(priv->syscfg, PHY_STAT,
				       caldone, IS_CALDONE(caldone),
				       0, 50);
	if (ret) {
		dev_err(&phy->dev, "caldone failed, ret=%d\n", ret);
		return ret;
	}

	/* Set the frequency of the DLL operation */
	ret = regmap_update_bits(priv->syscfg, PHY_CFG_2, SEL_FREQ_MASK,
				 FIELD_PREP(SEL_FREQ_MASK, freqsel));
	if (ret) {
		dev_err(&phy->dev, "set the frequency of dll failed:%d\n", ret);
		return ret;
	}

	/* Turn on the DLL */
	ret = regmap_update_bits(priv->syscfg, PHY_CFG_0, DLL_EN_MASK,
				 FIELD_PREP(DLL_EN_MASK, 1));
	if (ret) {
		dev_err(&phy->dev, "turn on the dll failed: %d\n", ret);
		return ret;
	}

	/*
	 * We turned on the DLL even though the rate was 0 because we the
	 * clock might be turned on later.  ...but we can't wait for the DLL
	 * to lock when the rate is 0 because it will never lock with no
	 * input clock.
	 *
	 * Technically we should be checking the lock later when the clock
	 * is turned on, but for now we won't.
	 */
	if (mhz == 0)
		return 0;

	/*
	 * After enabling analog DLL circuits docs say that we need 10.2 us if
	 * our source clock is at 50 MHz and that lock time scales linearly
	 * with clock speed. If we are powering on the PHY and the card clock
	 * is super slow (like 100kHz) this could take as long as 5.1 ms as
	 * per the math: 10.2 us * (50000000 Hz / 100000 Hz) => 5.1 ms
	 * hopefully we won't be running at 100 kHz, but we should still make
	 * sure we wait long enough.
	 *
	 * NOTE: There appear to be corner cases where the DLL seems to take
	 * extra long to lock for reasons that aren't understood. In some
	 * extreme cases we've seen it take up to over 10ms (!). We'll be
	 * generous and give it 50ms.
	 */
	ret = regmap_read_poll_timeout(priv->syscfg, PHY_STAT,
				       dllrdy, IS_DLLRDY(dllrdy),
				       0, 50 * USEC_PER_MSEC);
	if (ret)
		dev_err(&phy->dev, "dllrdy failed, ret=%d\n", ret);

	return ret;
}

static int keembay_emmc_phy_init(struct phy *phy)
{
	struct keembay_emmc_phy *priv = phy_get_drvdata(phy);

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
	 */
	priv->emmcclk = clk_get_optional(&phy->dev, "emmcclk");

	return PTR_ERR_OR_ZERO(priv->emmcclk);
}

static int keembay_emmc_phy_exit(struct phy *phy)
{
	struct keembay_emmc_phy *priv = phy_get_drvdata(phy);

	clk_put(priv->emmcclk);

	return 0;
};

static int keembay_emmc_phy_power_on(struct phy *phy)
{
	struct keembay_emmc_phy *priv = phy_get_drvdata(phy);
	int ret;

	/* Delay chain based txclk: enable */
	ret = regmap_update_bits(priv->syscfg, PHY_CFG_0, SEL_DLY_TXCLK_MASK,
				 FIELD_PREP(SEL_DLY_TXCLK_MASK, 1));
	if (ret) {
		dev_err(&phy->dev, "ERROR: delay chain txclk set: %d\n", ret);
		return ret;
	}

	/* Output tap delay: enable */
	ret = regmap_update_bits(priv->syscfg, PHY_CFG_0, OTAP_DLY_ENA_MASK,
				 FIELD_PREP(OTAP_DLY_ENA_MASK, 1));
	if (ret) {
		dev_err(&phy->dev, "ERROR: output tap delay set: %d\n", ret);
		return ret;
	}

	/* Output tap delay */
	ret = regmap_update_bits(priv->syscfg, PHY_CFG_0, OTAP_DLY_SEL_MASK,
				 FIELD_PREP(OTAP_DLY_SEL_MASK, 2));
	if (ret) {
		dev_err(&phy->dev, "ERROR: output tap delay select: %d\n", ret);
		return ret;
	}

	/* Power up eMMC phy analog blocks */
	return keembay_emmc_phy_power(phy, true);
}

static int keembay_emmc_phy_power_off(struct phy *phy)
{
	/* Power down eMMC phy analog blocks */
	return keembay_emmc_phy_power(phy, false);
}

static const struct phy_ops ops = {
	.init		= keembay_emmc_phy_init,
	.exit		= keembay_emmc_phy_exit,
	.power_on	= keembay_emmc_phy_power_on,
	.power_off	= keembay_emmc_phy_power_off,
	.owner		= THIS_MODULE,
};

static int keembay_emmc_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct keembay_emmc_phy *priv;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;
	void __iomem *base;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->syscfg = devm_regmap_init_mmio(dev, base, &keembay_regmap_config);
	if (IS_ERR(priv->syscfg))
		return PTR_ERR(priv->syscfg);

	generic_phy = devm_phy_create(dev, np, &ops);
	if (IS_ERR(generic_phy))
		return dev_err_probe(dev, PTR_ERR(generic_phy),
				     "failed to create PHY\n");

	phy_set_drvdata(generic_phy, priv);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id keembay_emmc_phy_dt_ids[] = {
	{ .compatible = "intel,keembay-emmc-phy" },
	{}
};
MODULE_DEVICE_TABLE(of, keembay_emmc_phy_dt_ids);

static struct platform_driver keembay_emmc_phy_driver = {
	.probe		= keembay_emmc_phy_probe,
	.driver		= {
		.name	= "keembay-emmc-phy",
		.of_match_table = keembay_emmc_phy_dt_ids,
	},
};
module_platform_driver(keembay_emmc_phy_driver);

MODULE_AUTHOR("Wan Ahmad Zainie <wan.ahmad.zainie.wan.mohamad@intel.com>");
MODULE_DESCRIPTION("Intel Keem Bay eMMC PHY driver");
MODULE_LICENSE("GPL v2");
