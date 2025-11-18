// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Texas Instruments Incorporated - https://www.ti.com/
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-mipi-dphy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sys_soc.h>

#define DPHY_PMA_CMN(reg)		(reg)
#define DPHY_PCS(reg)			(0xb00 + (reg))
#define DPHY_ISO(reg)			(0xc00 + (reg))
#define DPHY_WRAP(reg)			(0x1000 + (reg))

#define DPHY_CMN_SSM			DPHY_PMA_CMN(0x20)
#define DPHY_CMN_RX_MODE_EN		BIT(10)
#define DPHY_CMN_RX_BANDGAP_TIMER_MASK	GENMASK(8, 1)
#define DPHY_CMN_SSM_EN			BIT(0)

#define DPHY_CMN_RX_BANDGAP_TIMER	0x14

#define DPHY_BAND_CFG			DPHY_PCS(0x0)
#define DPHY_BAND_CFG_RIGHT_BAND	GENMASK(9, 5)
#define DPHY_BAND_CFG_LEFT_BAND		GENMASK(4, 0)

#define DPHY_POWER_ISLAND_EN_DATA	DPHY_PCS(0x8)
#define DPHY_POWER_ISLAND_EN_DATA_VAL	0xaaaaaaaa

#define DPHY_POWER_ISLAND_EN_CLK	DPHY_PCS(0xc)
#define DPHY_POWER_ISLAND_EN_CLK_VAL	0xaa

#define DPHY_LANE			DPHY_WRAP(0x0)
#define DPHY_LANE_RESET_CMN_EN		BIT(23)

#define DPHY_ISO_CL_CTRL_L		DPHY_ISO(0x10)
#define DPHY_ISO_DL_CTRL_L0		DPHY_ISO(0x14)
#define DPHY_ISO_DL_CTRL_L1		DPHY_ISO(0x20)
#define DPHY_ISO_DL_CTRL_L2		DPHY_ISO(0x30)
#define DPHY_ISO_DL_CTRL_L3		DPHY_ISO(0x3c)

#define DPHY_ISO_LANE_READY_BIT		0
#define DPHY_ISO_LANE_READY_TIMEOUT_MS	100UL

#define DPHY_LANES_MIN			1
#define DPHY_LANES_MAX			4

struct cdns_dphy_rx {
	void __iomem *regs;
	struct device *dev;
	struct phy *phy;
};

struct cdns_dphy_rx_band {
	/* Rates are in Mbps. */
	unsigned int min_rate;
	unsigned int max_rate;
};

struct cdns_dphy_soc_data {
	bool has_hw_cmn_rstb;
};

/* Order of bands is important since the index is the band number. */
static const struct cdns_dphy_rx_band bands[] = {
	{ 80, 100 }, { 100, 120 }, { 120, 160 }, { 160, 200 }, { 200, 240 },
	{ 240, 280 }, { 280, 320 }, { 320, 360 }, { 360, 400 }, { 400, 480 },
	{ 480, 560 }, { 560, 640 }, { 640, 720 }, { 720, 800 }, { 800, 880 },
	{ 880, 1040 }, { 1040, 1200 }, { 1200, 1350 }, { 1350, 1500 },
	{ 1500, 1750 }, { 1750, 2000 }, { 2000, 2250 }, { 2250, 2500 }
};

static int cdns_dphy_rx_power_on(struct phy *phy)
{
	struct cdns_dphy_rx *dphy = phy_get_drvdata(phy);

	/* Start RX state machine. */
	writel(DPHY_CMN_SSM_EN | DPHY_CMN_RX_MODE_EN |
	       FIELD_PREP(DPHY_CMN_RX_BANDGAP_TIMER_MASK,
			  DPHY_CMN_RX_BANDGAP_TIMER),
	       dphy->regs + DPHY_CMN_SSM);

	return 0;
}

static int cdns_dphy_rx_power_off(struct phy *phy)
{
	struct cdns_dphy_rx *dphy = phy_get_drvdata(phy);

	writel(0, dphy->regs + DPHY_CMN_SSM);

	return 0;
}

static int cdns_dphy_rx_get_band_ctrl(unsigned long hs_clk_rate)
{
	unsigned int rate, i;

	rate = hs_clk_rate / 1000000UL;
	/* Since CSI-2 clock is DDR, the bit rate is twice the clock rate. */
	rate *= 2;

	if (rate < bands[0].min_rate)
		return -EOPNOTSUPP;

	for (i = 0; i < ARRAY_SIZE(bands); i++)
		if (rate < bands[i].max_rate)
			return i;

	return -EOPNOTSUPP;
}

static inline int cdns_dphy_rx_wait_for_bit(void __iomem *addr,
					    unsigned int bit)
{
	u32 val;

	return readl_relaxed_poll_timeout(addr, val, val & BIT(bit), 10,
					  DPHY_ISO_LANE_READY_TIMEOUT_MS * 1000);
}

static int cdns_dphy_rx_wait_lane_ready(struct cdns_dphy_rx *dphy,
					unsigned int lanes)
{
	static const u32 data_lane_ctrl[] = {DPHY_ISO_DL_CTRL_L0,
					     DPHY_ISO_DL_CTRL_L1,
					     DPHY_ISO_DL_CTRL_L2,
					     DPHY_ISO_DL_CTRL_L3};
	void __iomem *reg = dphy->regs;
	unsigned int i;
	int ret;

	/* Clock lane */
	ret = cdns_dphy_rx_wait_for_bit(reg + DPHY_ISO_CL_CTRL_L,
					DPHY_ISO_LANE_READY_BIT);
	if (ret)
		return ret;

	for (i = 0; i < lanes; i++) {
		ret = cdns_dphy_rx_wait_for_bit(reg + data_lane_ctrl[i],
						DPHY_ISO_LANE_READY_BIT);
		if (ret)
			return ret;
	}

	return 0;
}

static struct cdns_dphy_soc_data j721e_soc_data = {
	.has_hw_cmn_rstb = true,
};

static const struct soc_device_attribute cdns_dphy_socinfo[] = {
	{
		.family = "J721E",
		.revision = "SR1.0",
		.data = &j721e_soc_data,
	},
	{/* sentinel */}
};

static int cdns_dphy_rx_configure(struct phy *phy,
				  union phy_configure_opts *opts)
{
	struct cdns_dphy_rx *dphy = phy_get_drvdata(phy);
	unsigned int reg, lanes = opts->mipi_dphy.lanes;
	const struct cdns_dphy_soc_data *soc_data = NULL;
	const struct soc_device_attribute *soc;
	int band_ctrl, ret;

	soc = soc_device_match(cdns_dphy_socinfo);
	if (soc && soc->data)
		soc_data = soc->data;
	if (!soc || (soc_data && !soc_data->has_hw_cmn_rstb)) {
		reg = DPHY_LANE_RESET_CMN_EN;
		writel(reg, dphy->regs + DPHY_LANE);
	}

	/* Data lanes. Minimum one lane is mandatory. */
	if (lanes < DPHY_LANES_MIN || lanes > DPHY_LANES_MAX)
		return -EINVAL;

	band_ctrl = cdns_dphy_rx_get_band_ctrl(opts->mipi_dphy.hs_clk_rate);
	if (band_ctrl < 0)
		return band_ctrl;

	reg = FIELD_PREP(DPHY_BAND_CFG_LEFT_BAND, band_ctrl) |
	      FIELD_PREP(DPHY_BAND_CFG_RIGHT_BAND, band_ctrl);
	writel(reg, dphy->regs + DPHY_BAND_CFG);

	/*
	 * Set the required power island phase 2 time. This is mandated by DPHY
	 * specs.
	 */
	reg = DPHY_POWER_ISLAND_EN_DATA_VAL;
	writel(reg, dphy->regs + DPHY_POWER_ISLAND_EN_DATA);
	reg = DPHY_POWER_ISLAND_EN_CLK_VAL;
	writel(reg, dphy->regs + DPHY_POWER_ISLAND_EN_CLK);

	ret = cdns_dphy_rx_wait_lane_ready(dphy, lanes);
	if (ret) {
		dev_err(dphy->dev, "DPHY wait for lane ready timeout\n");
		return ret;
	}

	return 0;
}

static int cdns_dphy_rx_validate(struct phy *phy, enum phy_mode mode,
				 int submode, union phy_configure_opts *opts)
{
	int ret;

	if (mode != PHY_MODE_MIPI_DPHY)
		return -EINVAL;

	ret = cdns_dphy_rx_get_band_ctrl(opts->mipi_dphy.hs_clk_rate);
	if (ret < 0)
		return ret;

	return phy_mipi_dphy_config_validate(&opts->mipi_dphy);
}

static const struct phy_ops cdns_dphy_rx_ops = {
	.power_on = cdns_dphy_rx_power_on,
	.power_off = cdns_dphy_rx_power_off,
	.configure = cdns_dphy_rx_configure,
	.validate = cdns_dphy_rx_validate,
};

static int cdns_dphy_rx_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct cdns_dphy_rx *dphy;

	dphy = devm_kzalloc(dev, sizeof(*dphy), GFP_KERNEL);
	if (!dphy)
		return -ENOMEM;

	dev_set_drvdata(dev, dphy);
	dphy->dev = dev;

	dphy->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dphy->regs))
		return PTR_ERR(dphy->regs);

	dphy->phy = devm_phy_create(dev, NULL, &cdns_dphy_rx_ops);
	if (IS_ERR(dphy->phy)) {
		dev_err(dev, "Failed to create PHY: %ld\n", PTR_ERR(dphy->phy));
		return PTR_ERR(dphy->phy);
	}

	phy_set_drvdata(dphy->phy, dphy);
	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "Failed to register PHY provider: %ld\n",
			PTR_ERR(provider));
		return PTR_ERR(provider);
	}

	return devm_pm_runtime_enable(dev);
}

static const struct of_device_id cdns_dphy_rx_of_match[] = {
	{ .compatible = "cdns,dphy-rx" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, cdns_dphy_rx_of_match);

static struct platform_driver cdns_dphy_rx_platform_driver = {
	.probe		= cdns_dphy_rx_probe,
	.driver		= {
		.name		= "cdns-mipi-dphy-rx",
		.of_match_table	= cdns_dphy_rx_of_match,
	},
};
module_platform_driver(cdns_dphy_rx_platform_driver);

MODULE_AUTHOR("Pratyush Yadav <p.yadav@ti.com>");
MODULE_DESCRIPTION("Cadence D-PHY Rx Driver");
MODULE_LICENSE("GPL");
