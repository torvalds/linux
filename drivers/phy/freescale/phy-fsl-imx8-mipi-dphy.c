// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2017,2018 NXP
 * Copyright 2019 Purism SPC
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* DPHY registers */
#define DPHY_PD_DPHY			0x00
#define DPHY_M_PRG_HS_PREPARE		0x04
#define DPHY_MC_PRG_HS_PREPARE		0x08
#define DPHY_M_PRG_HS_ZERO		0x0c
#define DPHY_MC_PRG_HS_ZERO		0x10
#define DPHY_M_PRG_HS_TRAIL		0x14
#define DPHY_MC_PRG_HS_TRAIL		0x18
#define DPHY_PD_PLL			0x1c
#define DPHY_TST			0x20
#define DPHY_CN				0x24
#define DPHY_CM				0x28
#define DPHY_CO				0x2c
#define DPHY_LOCK			0x30
#define DPHY_LOCK_BYP			0x34
#define DPHY_REG_BYPASS_PLL		0x4C

#define MBPS(x) ((x) * 1000000)

#define DATA_RATE_MAX_SPEED MBPS(1500)
#define DATA_RATE_MIN_SPEED MBPS(80)

#define PLL_LOCK_SLEEP 10
#define PLL_LOCK_TIMEOUT 1000

#define CN_BUF	0xcb7a89c0
#define CO_BUF	0x63
#define CM(x)	(				  \
		((x) <	32) ? 0xe0 | ((x) - 16) : \
		((x) <	64) ? 0xc0 | ((x) - 32) : \
		((x) < 128) ? 0x80 | ((x) - 64) : \
		((x) - 128))
#define CN(x)	(((x) == 1) ? 0x1f : (((CN_BUF) >> ((x) - 1)) & 0x1f))
#define CO(x)	((CO_BUF) >> (8 - (x)) & 0x03)

/* PHY power on is active low */
#define PWR_ON	0
#define PWR_OFF	1

enum mixel_dphy_devtype {
	MIXEL_IMX8MQ,
};

struct mixel_dphy_devdata {
	u8 reg_tx_rcal;
	u8 reg_auto_pd_en;
	u8 reg_rxlprp;
	u8 reg_rxcdrp;
	u8 reg_rxhs_settle;
};

static const struct mixel_dphy_devdata mixel_dphy_devdata[] = {
	[MIXEL_IMX8MQ] = {
		.reg_tx_rcal = 0x38,
		.reg_auto_pd_en = 0x3c,
		.reg_rxlprp = 0x40,
		.reg_rxcdrp = 0x44,
		.reg_rxhs_settle = 0x48,
	},
};

struct mixel_dphy_cfg {
	/* DPHY PLL parameters */
	u32 cm;
	u32 cn;
	u32 co;
	/* DPHY register values */
	u8 mc_prg_hs_prepare;
	u8 m_prg_hs_prepare;
	u8 mc_prg_hs_zero;
	u8 m_prg_hs_zero;
	u8 mc_prg_hs_trail;
	u8 m_prg_hs_trail;
	u8 rxhs_settle;
};

struct mixel_dphy_priv {
	struct mixel_dphy_cfg cfg;
	struct regmap *regmap;
	struct clk *phy_ref_clk;
	const struct mixel_dphy_devdata *devdata;
};

static const struct regmap_config mixel_dphy_regmap_config = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = DPHY_REG_BYPASS_PLL,
	.name = "mipi-dphy",
};

static int phy_write(struct phy *phy, u32 value, unsigned int reg)
{
	struct mixel_dphy_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = regmap_write(priv->regmap, reg, value);
	if (ret < 0)
		dev_err(&phy->dev, "Failed to write DPHY reg %d: %d\n", reg,
			ret);
	return ret;
}

/*
 * Find a ratio close to the desired one using continued fraction
 * approximation ending either at exact match or maximum allowed
 * nominator, denominator.
 */
static void get_best_ratio(u32 *pnum, u32 *pdenom, u32 max_n, u32 max_d)
{
	u32 a = *pnum;
	u32 b = *pdenom;
	u32 c;
	u32 n[] = {0, 1};
	u32 d[] = {1, 0};
	u32 whole;
	unsigned int i = 1;

	while (b) {
		i ^= 1;
		whole = a / b;
		n[i] += (n[i ^ 1] * whole);
		d[i] += (d[i ^ 1] * whole);
		if ((n[i] > max_n) || (d[i] > max_d)) {
			i ^= 1;
			break;
		}
		c = a - (b * whole);
		a = b;
		b = c;
	}
	*pnum = n[i];
	*pdenom = d[i];
}

static int mixel_dphy_config_from_opts(struct phy *phy,
	       struct phy_configure_opts_mipi_dphy *dphy_opts,
	       struct mixel_dphy_cfg *cfg)
{
	struct mixel_dphy_priv *priv = dev_get_drvdata(phy->dev.parent);
	unsigned long ref_clk = clk_get_rate(priv->phy_ref_clk);
	u32 lp_t, numerator, denominator;
	unsigned long long tmp;
	u32 n;
	int i;

	if (dphy_opts->hs_clk_rate > DATA_RATE_MAX_SPEED ||
	    dphy_opts->hs_clk_rate < DATA_RATE_MIN_SPEED)
		return -EINVAL;

	numerator = dphy_opts->hs_clk_rate;
	denominator = ref_clk;
	get_best_ratio(&numerator, &denominator, 255, 256);
	if (!numerator || !denominator) {
		dev_err(&phy->dev, "Invalid %d/%d for %ld/%ld\n",
			numerator, denominator,
			dphy_opts->hs_clk_rate, ref_clk);
		return -EINVAL;
	}

	while ((numerator < 16) && (denominator <= 128)) {
		numerator <<= 1;
		denominator <<= 1;
	}
	/*
	 * CM ranges between 16 and 255
	 * CN ranges between 1 and 32
	 * CO is power of 2: 1, 2, 4, 8
	 */
	i = __ffs(denominator);
	if (i > 3)
		i = 3;
	cfg->cn = denominator >> i;
	cfg->co = 1 << i;
	cfg->cm = numerator;

	if (cfg->cm < 16 || cfg->cm > 255 ||
	    cfg->cn < 1 || cfg->cn > 32 ||
	    cfg->co < 1 || cfg->co > 8) {
		dev_err(&phy->dev, "Invalid CM/CN/CO values: %u/%u/%u\n",
			cfg->cm, cfg->cn, cfg->co);
		dev_err(&phy->dev, "for hs_clk/ref_clk=%ld/%ld ~ %d/%d\n",
			dphy_opts->hs_clk_rate, ref_clk,
			numerator, denominator);
		return -EINVAL;
	}

	dev_dbg(&phy->dev, "hs_clk/ref_clk=%ld/%ld ~ %d/%d\n",
		dphy_opts->hs_clk_rate, ref_clk, numerator, denominator);

	/* LP clock period */
	tmp = 1000000000000LL;
	do_div(tmp, dphy_opts->lp_clk_rate); /* ps */
	if (tmp > ULONG_MAX)
		return -EINVAL;

	lp_t = tmp;
	dev_dbg(&phy->dev, "LP clock %lu, period: %u ps\n",
		dphy_opts->lp_clk_rate, lp_t);

	/* hs_prepare: in lp clock periods */
	if (2 * dphy_opts->hs_prepare > 5 * lp_t) {
		dev_err(&phy->dev,
			"hs_prepare (%u) > 2.5 * lp clock period (%u)\n",
			dphy_opts->hs_prepare, lp_t);
		return -EINVAL;
	}
	/* 00: lp_t, 01: 1.5 * lp_t, 10: 2 * lp_t, 11: 2.5 * lp_t */
	if (dphy_opts->hs_prepare < lp_t) {
		n = 0;
	} else {
		tmp = 2 * (dphy_opts->hs_prepare - lp_t);
		do_div(tmp, lp_t);
		n = tmp;
	}
	cfg->m_prg_hs_prepare = n;

	/* clk_prepare: in lp clock periods */
	if (2 * dphy_opts->clk_prepare > 3 * lp_t) {
		dev_err(&phy->dev,
			"clk_prepare (%u) > 1.5 * lp clock period (%u)\n",
			dphy_opts->clk_prepare, lp_t);
		return -EINVAL;
	}
	/* 00: lp_t, 01: 1.5 * lp_t */
	cfg->mc_prg_hs_prepare = dphy_opts->clk_prepare > lp_t ? 1 : 0;

	/* hs_zero: formula from NXP BSP */
	n = (144 * (dphy_opts->hs_clk_rate / 1000000) - 47500) / 10000;
	cfg->m_prg_hs_zero = n < 1 ? 1 : n;

	/* clk_zero: formula from NXP BSP */
	n = (34 * (dphy_opts->hs_clk_rate / 1000000) - 2500) / 1000;
	cfg->mc_prg_hs_zero = n < 1 ? 1 : n;

	/* clk_trail, hs_trail: formula from NXP BSP */
	n = (103 * (dphy_opts->hs_clk_rate / 1000000) + 10000) / 10000;
	if (n > 15)
		n = 15;
	if (n < 1)
		n = 1;
	cfg->m_prg_hs_trail = n;
	cfg->mc_prg_hs_trail = n;

	/* rxhs_settle: formula from NXP BSP */
	if (dphy_opts->hs_clk_rate < MBPS(80))
		cfg->rxhs_settle = 0x0d;
	else if (dphy_opts->hs_clk_rate < MBPS(90))
		cfg->rxhs_settle = 0x0c;
	else if (dphy_opts->hs_clk_rate < MBPS(125))
		cfg->rxhs_settle = 0x0b;
	else if (dphy_opts->hs_clk_rate < MBPS(150))
		cfg->rxhs_settle = 0x0a;
	else if (dphy_opts->hs_clk_rate < MBPS(225))
		cfg->rxhs_settle = 0x09;
	else if (dphy_opts->hs_clk_rate < MBPS(500))
		cfg->rxhs_settle = 0x08;
	else
		cfg->rxhs_settle = 0x07;

	dev_dbg(&phy->dev, "phy_config: %u %u %u %u %u %u %u\n",
		cfg->m_prg_hs_prepare, cfg->mc_prg_hs_prepare,
		cfg->m_prg_hs_zero, cfg->mc_prg_hs_zero,
		cfg->m_prg_hs_trail, cfg->mc_prg_hs_trail,
		cfg->rxhs_settle);

	return 0;
}

static void mixel_phy_set_hs_timings(struct phy *phy)
{
	struct mixel_dphy_priv *priv = phy_get_drvdata(phy);

	phy_write(phy, priv->cfg.m_prg_hs_prepare, DPHY_M_PRG_HS_PREPARE);
	phy_write(phy, priv->cfg.mc_prg_hs_prepare, DPHY_MC_PRG_HS_PREPARE);
	phy_write(phy, priv->cfg.m_prg_hs_zero, DPHY_M_PRG_HS_ZERO);
	phy_write(phy, priv->cfg.mc_prg_hs_zero, DPHY_MC_PRG_HS_ZERO);
	phy_write(phy, priv->cfg.m_prg_hs_trail, DPHY_M_PRG_HS_TRAIL);
	phy_write(phy, priv->cfg.mc_prg_hs_trail, DPHY_MC_PRG_HS_TRAIL);
	phy_write(phy, priv->cfg.rxhs_settle, priv->devdata->reg_rxhs_settle);
}

static int mixel_dphy_set_pll_params(struct phy *phy)
{
	struct mixel_dphy_priv *priv = dev_get_drvdata(phy->dev.parent);

	if (priv->cfg.cm < 16 || priv->cfg.cm > 255 ||
	    priv->cfg.cn < 1 || priv->cfg.cn > 32 ||
	    priv->cfg.co < 1 || priv->cfg.co > 8) {
		dev_err(&phy->dev, "Invalid CM/CN/CO values! (%u/%u/%u)\n",
			priv->cfg.cm, priv->cfg.cn, priv->cfg.co);
		return -EINVAL;
	}
	dev_dbg(&phy->dev, "Using CM:%u CN:%u CO:%u\n",
		priv->cfg.cm, priv->cfg.cn, priv->cfg.co);
	phy_write(phy, CM(priv->cfg.cm), DPHY_CM);
	phy_write(phy, CN(priv->cfg.cn), DPHY_CN);
	phy_write(phy, CO(priv->cfg.co), DPHY_CO);
	return 0;
}

static int mixel_dphy_configure(struct phy *phy, union phy_configure_opts *opts)
{
	struct mixel_dphy_priv *priv = phy_get_drvdata(phy);
	struct mixel_dphy_cfg cfg = { 0 };
	int ret;

	ret = mixel_dphy_config_from_opts(phy, &opts->mipi_dphy, &cfg);
	if (ret)
		return ret;

	/* Update the configuration */
	memcpy(&priv->cfg, &cfg, sizeof(struct mixel_dphy_cfg));

	phy_write(phy, 0x00, DPHY_LOCK_BYP);
	phy_write(phy, 0x01, priv->devdata->reg_tx_rcal);
	phy_write(phy, 0x00, priv->devdata->reg_auto_pd_en);
	phy_write(phy, 0x02, priv->devdata->reg_rxlprp);
	phy_write(phy, 0x02, priv->devdata->reg_rxcdrp);
	phy_write(phy, 0x25, DPHY_TST);

	mixel_phy_set_hs_timings(phy);
	ret = mixel_dphy_set_pll_params(phy);
	if (ret < 0)
		return ret;

	return 0;
}

static int mixel_dphy_validate(struct phy *phy, enum phy_mode mode, int submode,
			       union phy_configure_opts *opts)
{
	struct mixel_dphy_cfg cfg = { 0 };

	if (mode != PHY_MODE_MIPI_DPHY)
		return -EINVAL;

	return mixel_dphy_config_from_opts(phy, &opts->mipi_dphy, &cfg);
}

static int mixel_dphy_init(struct phy *phy)
{
	phy_write(phy, PWR_OFF, DPHY_PD_PLL);
	phy_write(phy, PWR_OFF, DPHY_PD_DPHY);

	return 0;
}

static int mixel_dphy_exit(struct phy *phy)
{
	phy_write(phy, 0, DPHY_CM);
	phy_write(phy, 0, DPHY_CN);
	phy_write(phy, 0, DPHY_CO);

	return 0;
}

static int mixel_dphy_power_on(struct phy *phy)
{
	struct mixel_dphy_priv *priv = phy_get_drvdata(phy);
	u32 locked;
	int ret;

	ret = clk_prepare_enable(priv->phy_ref_clk);
	if (ret < 0)
		return ret;

	phy_write(phy, PWR_ON, DPHY_PD_PLL);
	ret = regmap_read_poll_timeout(priv->regmap, DPHY_LOCK, locked,
				       locked, PLL_LOCK_SLEEP,
				       PLL_LOCK_TIMEOUT);
	if (ret < 0) {
		dev_err(&phy->dev, "Could not get DPHY lock (%d)!\n", ret);
		goto clock_disable;
	}
	phy_write(phy, PWR_ON, DPHY_PD_DPHY);

	return 0;
clock_disable:
	clk_disable_unprepare(priv->phy_ref_clk);
	return ret;
}

static int mixel_dphy_power_off(struct phy *phy)
{
	struct mixel_dphy_priv *priv = phy_get_drvdata(phy);

	phy_write(phy, PWR_OFF, DPHY_PD_PLL);
	phy_write(phy, PWR_OFF, DPHY_PD_DPHY);

	clk_disable_unprepare(priv->phy_ref_clk);

	return 0;
}

static const struct phy_ops mixel_dphy_phy_ops = {
	.init = mixel_dphy_init,
	.exit = mixel_dphy_exit,
	.power_on = mixel_dphy_power_on,
	.power_off = mixel_dphy_power_off,
	.configure = mixel_dphy_configure,
	.validate = mixel_dphy_validate,
	.owner = THIS_MODULE,
};

static const struct of_device_id mixel_dphy_of_match[] = {
	{ .compatible = "fsl,imx8mq-mipi-dphy",
	  .data = &mixel_dphy_devdata[MIXEL_IMX8MQ] },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mixel_dphy_of_match);

static int mixel_dphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct phy_provider *phy_provider;
	struct mixel_dphy_priv *priv;
	struct phy *phy;
	void __iomem *base;

	if (!np)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->devdata = of_device_get_match_data(&pdev->dev);
	if (!priv->devdata)
		return -EINVAL;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					     &mixel_dphy_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(dev, "Couldn't create the DPHY regmap\n");
		return PTR_ERR(priv->regmap);
	}

	priv->phy_ref_clk = devm_clk_get(&pdev->dev, "phy_ref");
	if (IS_ERR(priv->phy_ref_clk)) {
		dev_err(dev, "No phy_ref clock found\n");
		return PTR_ERR(priv->phy_ref_clk);
	}
	dev_dbg(dev, "phy_ref clock rate: %lu\n",
		clk_get_rate(priv->phy_ref_clk));

	dev_set_drvdata(dev, priv);

	phy = devm_phy_create(dev, np, &mixel_dphy_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "Failed to create phy %ld\n", PTR_ERR(phy));
		return PTR_ERR(phy);
	}
	phy_set_drvdata(phy, priv);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver mixel_dphy_driver = {
	.probe	= mixel_dphy_probe,
	.driver = {
		.name = "mixel-mipi-dphy",
		.of_match_table	= mixel_dphy_of_match,
	}
};
module_platform_driver(mixel_dphy_driver);

MODULE_AUTHOR("NXP Semiconductor");
MODULE_DESCRIPTION("Mixel MIPI-DSI PHY driver");
MODULE_LICENSE("GPL");
