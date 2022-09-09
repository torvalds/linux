// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2017,2018 NXP
 * Copyright 2019 Purism SPC
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/firmware/imx/ipc.h>
#include <linux/firmware/imx/svc/misc.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <dt-bindings/firmware/imx/rsrc.h>

/* Control and Status Registers(CSR) */
#define PHY_CTRL			0x00
#define  CCM_MASK			GENMASK(7, 5)
#define  CCM(n)				FIELD_PREP(CCM_MASK, (n))
#define  CCM_1_2V			0x5
#define  CA_MASK			GENMASK(4, 2)
#define  CA_3_51MA			0x4
#define  CA(n)				FIELD_PREP(CA_MASK, (n))
#define  RFB				BIT(1)
#define  LVDS_EN			BIT(0)

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

#define MIN_VCO_FREQ 640000000
#define MAX_VCO_FREQ 1500000000

#define MIN_LVDS_REFCLK_FREQ 24000000
#define MAX_LVDS_REFCLK_FREQ 150000000

enum mixel_dphy_devtype {
	MIXEL_IMX8MQ,
	MIXEL_IMX8QXP,
};

struct mixel_dphy_devdata {
	u8 reg_tx_rcal;
	u8 reg_auto_pd_en;
	u8 reg_rxlprp;
	u8 reg_rxcdrp;
	u8 reg_rxhs_settle;
	bool is_combo;	/* MIPI DPHY and LVDS PHY combo */
};

static const struct mixel_dphy_devdata mixel_dphy_devdata[] = {
	[MIXEL_IMX8MQ] = {
		.reg_tx_rcal = 0x38,
		.reg_auto_pd_en = 0x3c,
		.reg_rxlprp = 0x40,
		.reg_rxcdrp = 0x44,
		.reg_rxhs_settle = 0x48,
		.is_combo = false,
	},
	[MIXEL_IMX8QXP] = {
		.is_combo = true,
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
	struct regmap *lvds_regmap;
	struct clk *phy_ref_clk;
	const struct mixel_dphy_devdata *devdata;
	struct imx_sc_ipc *ipc_handle;
	bool is_slave;
	int id;
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

static int
mixel_dphy_configure_mipi_dphy(struct phy *phy, union phy_configure_opts *opts)
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

static int
mixel_dphy_configure_lvds_phy(struct phy *phy, union phy_configure_opts *opts)
{
	struct mixel_dphy_priv *priv = phy_get_drvdata(phy);
	struct phy_configure_opts_lvds *lvds_opts = &opts->lvds;
	unsigned long data_rate;
	unsigned long fvco;
	u32 rsc;
	u32 co;
	int ret;

	priv->is_slave = lvds_opts->is_slave;

	/* LVDS interface pins */
	regmap_write(priv->lvds_regmap, PHY_CTRL,
		     CCM(CCM_1_2V) | CA(CA_3_51MA) | RFB);

	/* enable MODE8 only for slave LVDS PHY */
	rsc = priv->id ? IMX_SC_R_MIPI_1 : IMX_SC_R_MIPI_0;
	ret = imx_sc_misc_set_control(priv->ipc_handle, rsc, IMX_SC_C_DUAL_MODE,
				      lvds_opts->is_slave);
	if (ret) {
		dev_err(&phy->dev, "Failed to configure MODE8: %d\n", ret);
		return ret;
	}

	/*
	 * Choose an appropriate divider ratio to meet the requirement of
	 * PLL VCO frequency range.
	 *
	 *  -----  640MHz ~ 1500MHz   ------------      ---------------
	 * | VCO | ----------------> | CO divider | -> | LVDS data rate|
	 *  -----       FVCO          ------------      ---------------
	 *                            1/2/4/8 div     7 * differential_clk_rate
	 */
	data_rate = 7 * lvds_opts->differential_clk_rate;
	for (co = 1; co <= 8; co *= 2) {
		fvco = data_rate * co;

		if (fvco >= MIN_VCO_FREQ)
			break;
	}

	if (fvco < MIN_VCO_FREQ || fvco > MAX_VCO_FREQ) {
		dev_err(&phy->dev, "VCO frequency %lu is out of range\n", fvco);
		return -ERANGE;
	}

	/*
	 * CO is configurable, while CN and CM are not,
	 * as fixed ratios 1 and 7 are applied respectively.
	 */
	phy_write(phy, __ffs(co), DPHY_CO);

	/* set reference clock rate */
	clk_set_rate(priv->phy_ref_clk, lvds_opts->differential_clk_rate);

	return ret;
}

static int mixel_dphy_configure(struct phy *phy, union phy_configure_opts *opts)
{
	if (!opts) {
		dev_err(&phy->dev, "No configuration options\n");
		return -EINVAL;
	}

	if (phy->attrs.mode == PHY_MODE_MIPI_DPHY)
		return mixel_dphy_configure_mipi_dphy(phy, opts);
	else if (phy->attrs.mode == PHY_MODE_LVDS)
		return mixel_dphy_configure_lvds_phy(phy, opts);

	dev_err(&phy->dev,
		"Failed to configure PHY with invalid PHY mode: %d\n", phy->attrs.mode);

	return -EINVAL;
}

static int
mixel_dphy_validate_lvds_phy(struct phy *phy, union phy_configure_opts *opts)
{
	struct phy_configure_opts_lvds *lvds_cfg = &opts->lvds;

	if (lvds_cfg->bits_per_lane_and_dclk_cycle != 7) {
		dev_err(&phy->dev, "Invalid bits per LVDS data lane: %u\n",
			lvds_cfg->bits_per_lane_and_dclk_cycle);
		return -EINVAL;
	}

	if (lvds_cfg->lanes != 4) {
		dev_err(&phy->dev, "Invalid LVDS data lanes: %u\n", lvds_cfg->lanes);
		return -EINVAL;
	}

	if (lvds_cfg->differential_clk_rate < MIN_LVDS_REFCLK_FREQ ||
	    lvds_cfg->differential_clk_rate > MAX_LVDS_REFCLK_FREQ) {
		dev_err(&phy->dev,
			"Invalid LVDS differential clock rate: %lu\n",
			lvds_cfg->differential_clk_rate);
		return -EINVAL;
	}

	return 0;
}

static int mixel_dphy_validate(struct phy *phy, enum phy_mode mode, int submode,
			       union phy_configure_opts *opts)
{
	if (mode == PHY_MODE_MIPI_DPHY) {
		struct mixel_dphy_cfg mipi_dphy_cfg = { 0 };

		return mixel_dphy_config_from_opts(phy, &opts->mipi_dphy,
						   &mipi_dphy_cfg);
	} else if (mode == PHY_MODE_LVDS) {
		return mixel_dphy_validate_lvds_phy(phy, opts);
	}

	dev_err(&phy->dev,
		"Failed to validate PHY with invalid PHY mode: %d\n", mode);
	return -EINVAL;
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

static int mixel_dphy_power_on_mipi_dphy(struct phy *phy)
{
	struct mixel_dphy_priv *priv = phy_get_drvdata(phy);
	u32 locked;
	int ret;

	phy_write(phy, PWR_ON, DPHY_PD_PLL);
	ret = regmap_read_poll_timeout(priv->regmap, DPHY_LOCK, locked,
				       locked, PLL_LOCK_SLEEP,
				       PLL_LOCK_TIMEOUT);
	if (ret < 0) {
		dev_err(&phy->dev, "Could not get DPHY lock (%d)!\n", ret);
		return ret;
	}
	phy_write(phy, PWR_ON, DPHY_PD_DPHY);

	return 0;
}

static int mixel_dphy_power_on_lvds_phy(struct phy *phy)
{
	struct mixel_dphy_priv *priv = phy_get_drvdata(phy);
	u32 locked;
	int ret;

	regmap_update_bits(priv->lvds_regmap, PHY_CTRL, LVDS_EN, LVDS_EN);

	phy_write(phy, PWR_ON, DPHY_PD_DPHY);
	phy_write(phy, PWR_ON, DPHY_PD_PLL);

	/* do not wait for slave LVDS PHY being locked */
	if (priv->is_slave)
		return 0;

	ret = regmap_read_poll_timeout(priv->regmap, DPHY_LOCK, locked,
				       locked, PLL_LOCK_SLEEP,
				       PLL_LOCK_TIMEOUT);
	if (ret < 0) {
		dev_err(&phy->dev, "Could not get LVDS PHY lock (%d)!\n", ret);
		return ret;
	}

	return 0;
}

static int mixel_dphy_power_on(struct phy *phy)
{
	struct mixel_dphy_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = clk_prepare_enable(priv->phy_ref_clk);
	if (ret < 0)
		return ret;

	if (phy->attrs.mode == PHY_MODE_MIPI_DPHY) {
		ret = mixel_dphy_power_on_mipi_dphy(phy);
	} else if (phy->attrs.mode == PHY_MODE_LVDS) {
		ret = mixel_dphy_power_on_lvds_phy(phy);
	} else {
		dev_err(&phy->dev,
			"Failed to power on PHY with invalid PHY mode: %d\n",
							phy->attrs.mode);
		ret = -EINVAL;
	}

	if (ret)
		goto clock_disable;

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

	if (phy->attrs.mode == PHY_MODE_LVDS)
		regmap_update_bits(priv->lvds_regmap, PHY_CTRL, LVDS_EN, 0);

	clk_disable_unprepare(priv->phy_ref_clk);

	return 0;
}

static int mixel_dphy_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct mixel_dphy_priv *priv = phy_get_drvdata(phy);
	int ret;

	if (priv->devdata->is_combo && mode != PHY_MODE_LVDS) {
		dev_err(&phy->dev, "Failed to set PHY mode for combo PHY\n");
		return -EINVAL;
	}

	if (!priv->devdata->is_combo && mode != PHY_MODE_MIPI_DPHY) {
		dev_err(&phy->dev, "Failed to set PHY mode to MIPI DPHY\n");
		return -EINVAL;
	}

	if (priv->devdata->is_combo) {
		u32 rsc = priv->id ? IMX_SC_R_MIPI_1 : IMX_SC_R_MIPI_0;

		ret = imx_sc_misc_set_control(priv->ipc_handle,
					      rsc, IMX_SC_C_MODE,
					      mode == PHY_MODE_LVDS);
		if (ret) {
			dev_err(&phy->dev,
				"Failed to set PHY mode via SCU ipc: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static const struct phy_ops mixel_dphy_phy_ops = {
	.init = mixel_dphy_init,
	.exit = mixel_dphy_exit,
	.power_on = mixel_dphy_power_on,
	.power_off = mixel_dphy_power_off,
	.set_mode = mixel_dphy_set_mode,
	.configure = mixel_dphy_configure,
	.validate = mixel_dphy_validate,
	.owner = THIS_MODULE,
};

static const struct of_device_id mixel_dphy_of_match[] = {
	{ .compatible = "fsl,imx8mq-mipi-dphy",
	  .data = &mixel_dphy_devdata[MIXEL_IMX8MQ] },
	{ .compatible = "fsl,imx8qxp-mipi-dphy",
	  .data = &mixel_dphy_devdata[MIXEL_IMX8QXP] },
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
	int ret;

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

	if (priv->devdata->is_combo) {
		priv->lvds_regmap =
			syscon_regmap_lookup_by_phandle(np, "fsl,syscon");
		if (IS_ERR(priv->lvds_regmap)) {
			ret = PTR_ERR(priv->lvds_regmap);
			dev_err_probe(dev, ret, "Failed to get LVDS regmap\n");
			return ret;
		}

		priv->id = of_alias_get_id(np, "mipi_dphy");
		if (priv->id < 0) {
			dev_err(dev, "Failed to get phy node alias id: %d\n",
				priv->id);
			return priv->id;
		}

		ret = imx_scu_get_handle(&priv->ipc_handle);
		if (ret) {
			dev_err_probe(dev, ret,
				      "Failed to get SCU ipc handle\n");
			return ret;
		}
	}

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
