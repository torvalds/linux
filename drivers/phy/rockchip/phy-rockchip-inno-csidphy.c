// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip MIPI RX Innosilicon DPHY driver
 *
 * Copyright (C) 2021 Fuzhou Rockchip Electronics Co., Ltd.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-mipi-dphy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>

/* GRF */
#define RK1808_GRF_PD_VI_CON_OFFSET	0x0430

#define RK3326_GRF_PD_VI_CON_OFFSET	0x0430

#define RK3368_GRF_SOC_CON6_OFFSET	0x0418

#define RK3568_GRF_VI_CON0		0x0340
#define RK3568_GRF_VI_CON1		0x0344

/* PHY */
#define CSIDPHY_CTRL_LANE_ENABLE		0x00
#define CSIDPHY_CTRL_LANE_ENABLE_CK		BIT(6)
#define CSIDPHY_CTRL_LANE_ENABLE_MASK		GENMASK(5, 2)
#define CSIDPHY_CTRL_LANE_ENABLE_UNDEFINED	BIT(0)

/* not present on all variants */
#define CSIDPHY_CTRL_PWRCTL			0x04
#define CSIDPHY_CTRL_PWRCTL_UNDEFINED		GENMASK(7, 5)
#define CSIDPHY_CTRL_PWRCTL_SYNCRST		BIT(2)
#define CSIDPHY_CTRL_PWRCTL_LDO_PD		BIT(1)
#define CSIDPHY_CTRL_PWRCTL_PLL_PD		BIT(0)

#define CSIDPHY_CTRL_DIG_RST			0x80
#define CSIDPHY_CTRL_DIG_RST_UNDEFINED		0x1e
#define CSIDPHY_CTRL_DIG_RST_RESET		BIT(0)

/* offset after ths_settle_offset */
#define CSIDPHY_CLK_THS_SETTLE			0
#define CSIDPHY_LANE_THS_SETTLE(n)		(((n) + 1) * 0x80)
#define CSIDPHY_THS_SETTLE_MASK			GENMASK(6, 0)

/* offset after calib_offset */
#define CSIDPHY_CLK_CALIB_EN			0
#define CSIDPHY_LANE_CALIB_EN(n)		(((n) + 1) * 0x80)
#define CSIDPHY_CALIB_EN			BIT(7)

/* Configure the count time of the THS-SETTLE by protocol. */
#define RK1808_CSIDPHY_CLK_WR_THS_SETTLE	0x160
#define RK3326_CSIDPHY_CLK_WR_THS_SETTLE	0x100
#define RK3368_CSIDPHY_CLK_WR_THS_SETTLE	0x100
#define RK3568_CSIDPHY_CLK_WR_THS_SETTLE	0x160

/* Calibration reception enable */
#define RK1808_CSIDPHY_CLK_CALIB_EN		0x168
#define RK3568_CSIDPHY_CLK_CALIB_EN		0x168

/*
 * The higher 16-bit of this register is used for write protection
 * only if BIT(x + 16) set to 1 the BIT(x) can be written.
 */
#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

#define HZ_TO_MHZ(freq)				div_u64(freq, 1000 * 1000)

enum dphy_reg_id {
	/* rk1808 & rk3326 */
	GRF_DPHY_CSIPHY_FORCERXMODE,
	GRF_DPHY_CSIPHY_CLKLANE_EN,
	GRF_DPHY_CSIPHY_DATALANE_EN,
};

struct dphy_reg {
	u32 offset;
	u32 mask;
	u32 shift;
};

#define PHY_REG(_offset, _width, _shift) \
	{ .offset = _offset, .mask = BIT(_width) - 1, .shift = _shift, }

static const struct dphy_reg rk1808_grf_dphy_regs[] = {
	[GRF_DPHY_CSIPHY_FORCERXMODE] = PHY_REG(RK1808_GRF_PD_VI_CON_OFFSET, 4, 0),
	[GRF_DPHY_CSIPHY_CLKLANE_EN] = PHY_REG(RK1808_GRF_PD_VI_CON_OFFSET, 1, 8),
	[GRF_DPHY_CSIPHY_DATALANE_EN] = PHY_REG(RK1808_GRF_PD_VI_CON_OFFSET, 4, 4),
};

static const struct dphy_reg rk3326_grf_dphy_regs[] = {
	[GRF_DPHY_CSIPHY_FORCERXMODE] = PHY_REG(RK3326_GRF_PD_VI_CON_OFFSET, 4, 0),
	[GRF_DPHY_CSIPHY_CLKLANE_EN] = PHY_REG(RK3326_GRF_PD_VI_CON_OFFSET, 1, 8),
	[GRF_DPHY_CSIPHY_DATALANE_EN] = PHY_REG(RK3326_GRF_PD_VI_CON_OFFSET, 4, 4),
};

static const struct dphy_reg rk3368_grf_dphy_regs[] = {
	[GRF_DPHY_CSIPHY_FORCERXMODE] = PHY_REG(RK3368_GRF_SOC_CON6_OFFSET, 4, 8),
};

static const struct dphy_reg rk3568_grf_dphy_regs[] = {
	[GRF_DPHY_CSIPHY_FORCERXMODE] = PHY_REG(RK3568_GRF_VI_CON0, 4, 0),
	[GRF_DPHY_CSIPHY_DATALANE_EN] = PHY_REG(RK3568_GRF_VI_CON0, 4, 4),
	[GRF_DPHY_CSIPHY_CLKLANE_EN] = PHY_REG(RK3568_GRF_VI_CON0, 1, 8),
};

struct hsfreq_range {
	u32 range_h;
	u8 cfg_bit;
};

struct dphy_drv_data {
	int pwrctl_offset;
	int ths_settle_offset;
	int calib_offset;
	const struct hsfreq_range *hsfreq_ranges;
	int num_hsfreq_ranges;
	const struct dphy_reg *grf_regs;
};

struct rockchip_inno_csidphy {
	struct device *dev;
	void __iomem *phy_base;
	struct clk *pclk;
	struct regmap *grf;
	struct reset_control *rst;
	const struct dphy_drv_data *drv_data;
	struct phy_configure_opts_mipi_dphy config;
	u8 hsfreq;
};

static inline void write_grf_reg(struct rockchip_inno_csidphy *priv,
				 int index, u8 value)
{
	const struct dphy_drv_data *drv_data = priv->drv_data;
	const struct dphy_reg *reg = &drv_data->grf_regs[index];

	if (reg->offset)
		regmap_write(priv->grf, reg->offset,
			     HIWORD_UPDATE(value, reg->mask, reg->shift));
}

/* These tables must be sorted by .range_h ascending. */
static const struct hsfreq_range rk1808_mipidphy_hsfreq_ranges[] = {
	{ 109, 0x02}, { 149, 0x03}, { 199, 0x06}, { 249, 0x06},
	{ 299, 0x06}, { 399, 0x08}, { 499, 0x0b}, { 599, 0x0e},
	{ 699, 0x10}, { 799, 0x12}, { 999, 0x16}, {1199, 0x1e},
	{1399, 0x23}, {1599, 0x2d}, {1799, 0x32}, {1999, 0x37},
	{2199, 0x3c}, {2399, 0x41}, {2499, 0x46}
};

static const struct hsfreq_range rk3326_mipidphy_hsfreq_ranges[] = {
	{ 109, 0x00}, { 149, 0x01}, { 199, 0x02}, { 249, 0x03},
	{ 299, 0x04}, { 399, 0x05}, { 499, 0x06}, { 599, 0x07},
	{ 699, 0x08}, { 799, 0x09}, { 899, 0x0a}, {1099, 0x0b},
	{1249, 0x0c}, {1349, 0x0d}, {1500, 0x0e}
};

static const struct hsfreq_range rk3368_mipidphy_hsfreq_ranges[] = {
	{ 109, 0x00}, { 149, 0x01}, { 199, 0x02}, { 249, 0x03},
	{ 299, 0x04}, { 399, 0x05}, { 499, 0x06}, { 599, 0x07},
	{ 699, 0x08}, { 799, 0x09}, { 899, 0x0a}, {1099, 0x0b},
	{1249, 0x0c}, {1349, 0x0d}, {1500, 0x0e}
};

static void rockchip_inno_csidphy_ths_settle(struct rockchip_inno_csidphy *priv,
					     int hsfreq, int offset)
{
	const struct dphy_drv_data *drv_data = priv->drv_data;
	u32 val;

	val = readl(priv->phy_base + drv_data->ths_settle_offset + offset);
	val &= ~CSIDPHY_THS_SETTLE_MASK;
	val |= hsfreq;
	writel(val, priv->phy_base + drv_data->ths_settle_offset + offset);
}

static int rockchip_inno_csidphy_configure(struct phy *phy,
					   union phy_configure_opts *opts)
{
	struct rockchip_inno_csidphy *priv = phy_get_drvdata(phy);
	const struct dphy_drv_data *drv_data = priv->drv_data;
	struct phy_configure_opts_mipi_dphy *config = &opts->mipi_dphy;
	unsigned int hsfreq = 0;
	unsigned int i;
	u64 data_rate_mbps;
	int ret;

	/* pass with phy_mipi_dphy_get_default_config (with pixel rate?) */
	ret = phy_mipi_dphy_config_validate(config);
	if (ret)
		return ret;

	data_rate_mbps = HZ_TO_MHZ(config->hs_clk_rate);

	dev_dbg(priv->dev, "lanes %d - data_rate_mbps %llu\n",
		config->lanes, data_rate_mbps);
	for (i = 0; i < drv_data->num_hsfreq_ranges; i++) {
		if (drv_data->hsfreq_ranges[i].range_h >= data_rate_mbps) {
			hsfreq = drv_data->hsfreq_ranges[i].cfg_bit;
			break;
		}
	}
	if (!hsfreq)
		return -EINVAL;

	priv->hsfreq = hsfreq;
	priv->config = *config;
	return 0;
}

static int rockchip_inno_csidphy_power_on(struct phy *phy)
{
	struct rockchip_inno_csidphy *priv = phy_get_drvdata(phy);
	const struct dphy_drv_data *drv_data = priv->drv_data;
	u64 data_rate_mbps = HZ_TO_MHZ(priv->config.hs_clk_rate);
	u32 val;
	int ret, i;

	ret = clk_enable(priv->pclk);
	if (ret < 0)
		return ret;

	ret = pm_runtime_resume_and_get(priv->dev);
	if (ret < 0) {
		clk_disable(priv->pclk);
		return ret;
	}

	/* phy start */
	if (drv_data->pwrctl_offset >= 0)
		writel(CSIDPHY_CTRL_PWRCTL_UNDEFINED |
		       CSIDPHY_CTRL_PWRCTL_SYNCRST,
		       priv->phy_base + drv_data->pwrctl_offset);

	/* set data lane num and enable clock lane */
	val = FIELD_PREP(CSIDPHY_CTRL_LANE_ENABLE_MASK, GENMASK(priv->config.lanes - 1, 0)) |
	      FIELD_PREP(CSIDPHY_CTRL_LANE_ENABLE_CK, 1) |
	      FIELD_PREP(CSIDPHY_CTRL_LANE_ENABLE_UNDEFINED, 1);
	writel(val, priv->phy_base + CSIDPHY_CTRL_LANE_ENABLE);

	/* Reset dphy analog part */
	if (drv_data->pwrctl_offset >= 0)
		writel(CSIDPHY_CTRL_PWRCTL_UNDEFINED,
		       priv->phy_base + drv_data->pwrctl_offset);
	usleep_range(500, 1000);

	/* Reset dphy digital part */
	writel(CSIDPHY_CTRL_DIG_RST_UNDEFINED,
	       priv->phy_base + CSIDPHY_CTRL_DIG_RST);
	writel(CSIDPHY_CTRL_DIG_RST_UNDEFINED + CSIDPHY_CTRL_DIG_RST_RESET,
	       priv->phy_base + CSIDPHY_CTRL_DIG_RST);

	/* not into receive mode/wait stopstate */
	write_grf_reg(priv, GRF_DPHY_CSIPHY_FORCERXMODE, 0x0);

	/* enable calibration */
	if (data_rate_mbps > 1500 && drv_data->calib_offset >= 0) {
		writel(CSIDPHY_CALIB_EN,
		       priv->phy_base + drv_data->calib_offset +
					CSIDPHY_CLK_CALIB_EN);
		for (i = 0; i < priv->config.lanes; i++)
			writel(CSIDPHY_CALIB_EN,
			       priv->phy_base + drv_data->calib_offset +
						CSIDPHY_LANE_CALIB_EN(i));
	}

	rockchip_inno_csidphy_ths_settle(priv, priv->hsfreq,
					 CSIDPHY_CLK_THS_SETTLE);
	for (i = 0; i < priv->config.lanes; i++)
		rockchip_inno_csidphy_ths_settle(priv, priv->hsfreq,
						 CSIDPHY_LANE_THS_SETTLE(i));

	write_grf_reg(priv, GRF_DPHY_CSIPHY_CLKLANE_EN, 0x1);
	write_grf_reg(priv, GRF_DPHY_CSIPHY_DATALANE_EN,
		      GENMASK(priv->config.lanes - 1, 0));

	return 0;
}

static int rockchip_inno_csidphy_power_off(struct phy *phy)
{
	struct rockchip_inno_csidphy *priv = phy_get_drvdata(phy);
	const struct dphy_drv_data *drv_data = priv->drv_data;

	/* disable all lanes */
	writel(CSIDPHY_CTRL_LANE_ENABLE_UNDEFINED,
	       priv->phy_base + CSIDPHY_CTRL_LANE_ENABLE);

	/* disable pll and ldo */
	if (drv_data->pwrctl_offset >= 0)
		writel(CSIDPHY_CTRL_PWRCTL_UNDEFINED |
		       CSIDPHY_CTRL_PWRCTL_LDO_PD |
		       CSIDPHY_CTRL_PWRCTL_PLL_PD,
		       priv->phy_base + drv_data->pwrctl_offset);
	usleep_range(500, 1000);

	pm_runtime_put(priv->dev);
	clk_disable(priv->pclk);

	return 0;
}

static int rockchip_inno_csidphy_init(struct phy *phy)
{
	struct rockchip_inno_csidphy *priv = phy_get_drvdata(phy);

	return clk_prepare(priv->pclk);
}

static int rockchip_inno_csidphy_exit(struct phy *phy)
{
	struct rockchip_inno_csidphy *priv = phy_get_drvdata(phy);

	clk_unprepare(priv->pclk);

	return 0;
}

static const struct phy_ops rockchip_inno_csidphy_ops = {
	.power_on	= rockchip_inno_csidphy_power_on,
	.power_off	= rockchip_inno_csidphy_power_off,
	.init		= rockchip_inno_csidphy_init,
	.exit		= rockchip_inno_csidphy_exit,
	.configure	= rockchip_inno_csidphy_configure,
	.owner		= THIS_MODULE,
};

static const struct dphy_drv_data rk1808_mipidphy_drv_data = {
	.pwrctl_offset = -1,
	.ths_settle_offset = RK1808_CSIDPHY_CLK_WR_THS_SETTLE,
	.calib_offset = RK1808_CSIDPHY_CLK_CALIB_EN,
	.hsfreq_ranges = rk1808_mipidphy_hsfreq_ranges,
	.num_hsfreq_ranges = ARRAY_SIZE(rk1808_mipidphy_hsfreq_ranges),
	.grf_regs = rk1808_grf_dphy_regs,
};

static const struct dphy_drv_data rk3326_mipidphy_drv_data = {
	.pwrctl_offset = CSIDPHY_CTRL_PWRCTL,
	.ths_settle_offset = RK3326_CSIDPHY_CLK_WR_THS_SETTLE,
	.calib_offset = -1,
	.hsfreq_ranges = rk3326_mipidphy_hsfreq_ranges,
	.num_hsfreq_ranges = ARRAY_SIZE(rk3326_mipidphy_hsfreq_ranges),
	.grf_regs = rk3326_grf_dphy_regs,
};

static const struct dphy_drv_data rk3368_mipidphy_drv_data = {
	.pwrctl_offset = CSIDPHY_CTRL_PWRCTL,
	.ths_settle_offset = RK3368_CSIDPHY_CLK_WR_THS_SETTLE,
	.calib_offset = -1,
	.hsfreq_ranges = rk3368_mipidphy_hsfreq_ranges,
	.num_hsfreq_ranges = ARRAY_SIZE(rk3368_mipidphy_hsfreq_ranges),
	.grf_regs = rk3368_grf_dphy_regs,
};

static const struct dphy_drv_data rk3568_mipidphy_drv_data = {
	.pwrctl_offset = -1,
	.ths_settle_offset = RK3568_CSIDPHY_CLK_WR_THS_SETTLE,
	.calib_offset = RK3568_CSIDPHY_CLK_CALIB_EN,
	.hsfreq_ranges = rk1808_mipidphy_hsfreq_ranges,
	.num_hsfreq_ranges = ARRAY_SIZE(rk1808_mipidphy_hsfreq_ranges),
	.grf_regs = rk3568_grf_dphy_regs,
};

static const struct of_device_id rockchip_inno_csidphy_match_id[] = {
	{
		.compatible = "rockchip,px30-csi-dphy",
		.data = &rk3326_mipidphy_drv_data,
	},
	{
		.compatible = "rockchip,rk1808-csi-dphy",
		.data = &rk1808_mipidphy_drv_data,
	},
	{
		.compatible = "rockchip,rk3326-csi-dphy",
		.data = &rk3326_mipidphy_drv_data,
	},
	{
		.compatible = "rockchip,rk3368-csi-dphy",
		.data = &rk3368_mipidphy_drv_data,
	},
	{
		.compatible = "rockchip,rk3568-csi-dphy",
		.data = &rk3568_mipidphy_drv_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_inno_csidphy_match_id);

static int rockchip_inno_csidphy_probe(struct platform_device *pdev)
{
	struct rockchip_inno_csidphy *priv;
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	struct phy *phy;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	platform_set_drvdata(pdev, priv);

	priv->drv_data = of_device_get_match_data(dev);
	if (!priv->drv_data) {
		dev_err(dev, "Can't find device data\n");
		return -ENODEV;
	}

	priv->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
						    "rockchip,grf");
	if (IS_ERR(priv->grf)) {
		dev_err(dev, "Can't find GRF syscon\n");
		return PTR_ERR(priv->grf);
	}

	priv->phy_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->phy_base))
		return PTR_ERR(priv->phy_base);

	priv->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(priv->pclk)) {
		dev_err(dev, "failed to get pclk\n");
		return PTR_ERR(priv->pclk);
	}

	priv->rst = devm_reset_control_get(dev, "apb");
	if (IS_ERR(priv->rst)) {
		dev_err(dev, "failed to get system reset control\n");
		return PTR_ERR(priv->rst);
	}

	phy = devm_phy_create(dev, NULL, &rockchip_inno_csidphy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create phy\n");
		return PTR_ERR(phy);
	}

	phy_set_drvdata(phy, priv);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(dev, "failed to register phy provider\n");
		return PTR_ERR(phy_provider);
	}

	pm_runtime_enable(dev);

	return 0;
}

static int rockchip_inno_csidphy_remove(struct platform_device *pdev)
{
	struct rockchip_inno_csidphy *priv = platform_get_drvdata(pdev);

	pm_runtime_disable(priv->dev);

	return 0;
}

static struct platform_driver rockchip_inno_csidphy_driver = {
	.driver = {
		.name = "rockchip-inno-csidphy",
		.of_match_table = rockchip_inno_csidphy_match_id,
	},
	.probe = rockchip_inno_csidphy_probe,
	.remove = rockchip_inno_csidphy_remove,
};

module_platform_driver(rockchip_inno_csidphy_driver);
MODULE_AUTHOR("Heiko Stuebner <heiko.stuebner@theobroma-systems.com>");
MODULE_DESCRIPTION("Rockchip MIPI Innosilicon CSI-DPHY driver");
MODULE_LICENSE("GPL v2");
