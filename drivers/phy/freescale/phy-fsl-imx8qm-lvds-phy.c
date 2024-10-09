// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2017-2020,2022 NXP
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/units.h>

#define REG_SET		0x4
#define REG_CLR		0x8

#define PHY_CTRL	0x0
#define  M_MASK		GENMASK(18, 17)
#define  M(n)		FIELD_PREP(M_MASK, (n))
#define  CCM_MASK	GENMASK(16, 14)
#define  CCM(n)		FIELD_PREP(CCM_MASK, (n))
#define  CA_MASK	GENMASK(13, 11)
#define  CA(n)		FIELD_PREP(CA_MASK, (n))
#define  TST_MASK	GENMASK(10, 5)
#define  TST(n)		FIELD_PREP(TST_MASK, (n))
#define  CH_EN(id)	BIT(3 + (id))
#define  NB		BIT(2)
#define  RFB		BIT(1)
#define  PD		BIT(0)

/* Power On Reset(POR) value */
#define  CTRL_RESET_VAL	(M(0x0) | CCM(0x4) | CA(0x4) | TST(0x25))

/* PHY initialization value and mask */
#define  CTRL_INIT_MASK	(M_MASK | CCM_MASK | CA_MASK | TST_MASK | NB | RFB)
#define  CTRL_INIT_VAL	(M(0x0) | CCM(0x5) | CA(0x4) | TST(0x25) | RFB)

#define PHY_STATUS	0x10
#define  LOCK		BIT(0)

#define PHY_NUM		2

#define MIN_CLKIN_FREQ	(25 * MEGA)
#define MAX_CLKIN_FREQ	(165 * MEGA)

#define PLL_LOCK_SLEEP		10
#define PLL_LOCK_TIMEOUT	1000

struct mixel_lvds_phy {
	struct phy *phy;
	struct phy_configure_opts_lvds cfg;
	unsigned int id;
};

struct mixel_lvds_phy_priv {
	struct regmap *regmap;
	struct mutex lock;	/* protect remap access and cfg of our own */
	struct clk *phy_ref_clk;
	struct mixel_lvds_phy *phys[PHY_NUM];
};

static int mixel_lvds_phy_init(struct phy *phy)
{
	struct mixel_lvds_phy_priv *priv = dev_get_drvdata(phy->dev.parent);

	mutex_lock(&priv->lock);
	regmap_update_bits(priv->regmap,
			   PHY_CTRL, CTRL_INIT_MASK, CTRL_INIT_VAL);
	mutex_unlock(&priv->lock);

	return 0;
}

static int mixel_lvds_phy_power_on(struct phy *phy)
{
	struct mixel_lvds_phy_priv *priv = dev_get_drvdata(phy->dev.parent);
	struct mixel_lvds_phy *lvds_phy = phy_get_drvdata(phy);
	struct mixel_lvds_phy *companion = priv->phys[lvds_phy->id ^ 1];
	struct phy_configure_opts_lvds *cfg = &lvds_phy->cfg;
	u32 val = 0;
	u32 locked;
	int ret;

	/* The master PHY would power on the slave PHY. */
	if (cfg->is_slave)
		return 0;

	ret = clk_prepare_enable(priv->phy_ref_clk);
	if (ret < 0) {
		dev_err(&phy->dev,
			"failed to enable PHY reference clock: %d\n", ret);
		return ret;
	}

	mutex_lock(&priv->lock);
	if (cfg->bits_per_lane_and_dclk_cycle == 7) {
		if (cfg->differential_clk_rate < 44000000)
			val |= M(0x2);
		else if (cfg->differential_clk_rate < 90000000)
			val |= M(0x1);
		else
			val |= M(0x0);
	} else {
		val = NB;

		if (cfg->differential_clk_rate < 32000000)
			val |= M(0x2);
		else if (cfg->differential_clk_rate < 63000000)
			val |= M(0x1);
		else
			val |= M(0x0);
	}
	regmap_update_bits(priv->regmap, PHY_CTRL, M_MASK | NB, val);

	/*
	 * Enable two channels synchronously,
	 * if the companion PHY is a slave PHY.
	 */
	if (companion->cfg.is_slave)
		val = CH_EN(0) | CH_EN(1);
	else
		val = CH_EN(lvds_phy->id);
	regmap_write(priv->regmap, PHY_CTRL + REG_SET, val);

	ret = regmap_read_poll_timeout(priv->regmap, PHY_STATUS, locked,
				       locked, PLL_LOCK_SLEEP,
				       PLL_LOCK_TIMEOUT);
	if (ret < 0) {
		dev_err(&phy->dev, "failed to get PHY lock: %d\n", ret);
		clk_disable_unprepare(priv->phy_ref_clk);
	}
	mutex_unlock(&priv->lock);

	return ret;
}

static int mixel_lvds_phy_power_off(struct phy *phy)
{
	struct mixel_lvds_phy_priv *priv = dev_get_drvdata(phy->dev.parent);
	struct mixel_lvds_phy *lvds_phy = phy_get_drvdata(phy);
	struct mixel_lvds_phy *companion = priv->phys[lvds_phy->id ^ 1];
	struct phy_configure_opts_lvds *cfg = &lvds_phy->cfg;

	/* The master PHY would power off the slave PHY. */
	if (cfg->is_slave)
		return 0;

	mutex_lock(&priv->lock);
	if (companion->cfg.is_slave)
		regmap_write(priv->regmap, PHY_CTRL + REG_CLR,
			     CH_EN(0) | CH_EN(1));
	else
		regmap_write(priv->regmap, PHY_CTRL + REG_CLR,
			     CH_EN(lvds_phy->id));
	mutex_unlock(&priv->lock);

	clk_disable_unprepare(priv->phy_ref_clk);

	return 0;
}

static int mixel_lvds_phy_configure(struct phy *phy,
				    union phy_configure_opts *opts)
{
	struct mixel_lvds_phy_priv *priv = dev_get_drvdata(phy->dev.parent);
	struct phy_configure_opts_lvds *cfg = &opts->lvds;
	int ret;

	ret = clk_set_rate(priv->phy_ref_clk, cfg->differential_clk_rate);
	if (ret)
		dev_err(&phy->dev, "failed to set PHY reference clock rate(%lu): %d\n",
			cfg->differential_clk_rate, ret);

	return ret;
}

/* Assume the master PHY's configuration set is cached first. */
static int mixel_lvds_phy_check_slave(struct phy *slave_phy)
{
	struct device *dev = &slave_phy->dev;
	struct mixel_lvds_phy_priv *priv = dev_get_drvdata(dev->parent);
	struct mixel_lvds_phy *slv = phy_get_drvdata(slave_phy);
	struct mixel_lvds_phy *mst = priv->phys[slv->id ^ 1];
	struct phy_configure_opts_lvds *mst_cfg = &mst->cfg;
	struct phy_configure_opts_lvds *slv_cfg = &slv->cfg;

	if (mst_cfg->bits_per_lane_and_dclk_cycle !=
	    slv_cfg->bits_per_lane_and_dclk_cycle) {
		dev_err(dev, "number bits mismatch(mst: %u vs slv: %u)\n",
			mst_cfg->bits_per_lane_and_dclk_cycle,
			slv_cfg->bits_per_lane_and_dclk_cycle);
		return -EINVAL;
	}

	if (mst_cfg->differential_clk_rate !=
	    slv_cfg->differential_clk_rate) {
		dev_err(dev, "dclk rate mismatch(mst: %lu vs slv: %lu)\n",
			mst_cfg->differential_clk_rate,
			slv_cfg->differential_clk_rate);
		return -EINVAL;
	}

	if (mst_cfg->lanes != slv_cfg->lanes) {
		dev_err(dev, "lanes mismatch(mst: %u vs slv: %u)\n",
			mst_cfg->lanes, slv_cfg->lanes);
		return -EINVAL;
	}

	if (mst_cfg->is_slave == slv_cfg->is_slave) {
		dev_err(dev, "master PHY is not found\n");
		return -EINVAL;
	}

	return 0;
}

static int mixel_lvds_phy_validate(struct phy *phy, enum phy_mode mode,
				   int submode, union phy_configure_opts *opts)
{
	struct mixel_lvds_phy_priv *priv = dev_get_drvdata(phy->dev.parent);
	struct mixel_lvds_phy *lvds_phy = phy_get_drvdata(phy);
	struct phy_configure_opts_lvds *cfg = &opts->lvds;
	int ret = 0;

	if (mode != PHY_MODE_LVDS) {
		dev_err(&phy->dev, "invalid PHY mode(%d)\n", mode);
		return -EINVAL;
	}

	if (cfg->bits_per_lane_and_dclk_cycle != 7 &&
	    cfg->bits_per_lane_and_dclk_cycle != 10) {
		dev_err(&phy->dev, "invalid bits per data lane(%u)\n",
			cfg->bits_per_lane_and_dclk_cycle);
		return -EINVAL;
	}

	if (cfg->lanes != 4 && cfg->lanes != 3) {
		dev_err(&phy->dev, "invalid data lanes(%u)\n", cfg->lanes);
		return -EINVAL;
	}

	if (cfg->differential_clk_rate < MIN_CLKIN_FREQ ||
	    cfg->differential_clk_rate > MAX_CLKIN_FREQ) {
		dev_err(&phy->dev, "invalid differential clock rate(%lu)\n",
			cfg->differential_clk_rate);
		return -EINVAL;
	}

	mutex_lock(&priv->lock);
	/* cache configuration set of our own for check */
	memcpy(&lvds_phy->cfg, cfg, sizeof(*cfg));

	if (cfg->is_slave) {
		ret = mixel_lvds_phy_check_slave(phy);
		if (ret)
			dev_err(&phy->dev, "failed to check slave PHY: %d\n", ret);
	}
	mutex_unlock(&priv->lock);

	return ret;
}

static const struct phy_ops mixel_lvds_phy_ops = {
	.init = mixel_lvds_phy_init,
	.power_on = mixel_lvds_phy_power_on,
	.power_off = mixel_lvds_phy_power_off,
	.configure = mixel_lvds_phy_configure,
	.validate = mixel_lvds_phy_validate,
	.owner = THIS_MODULE,
};

static int mixel_lvds_phy_reset(struct device *dev)
{
	struct mixel_lvds_phy_priv *priv = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		dev_err(dev, "failed to get PM runtime: %d\n", ret);
		return ret;
	}

	regmap_write(priv->regmap, PHY_CTRL, CTRL_RESET_VAL);

	ret = pm_runtime_put(dev);
	if (ret < 0)
		dev_err(dev, "failed to put PM runtime: %d\n", ret);

	return ret;
}

static struct phy *mixel_lvds_phy_xlate(struct device *dev,
					const struct of_phandle_args *args)
{
	struct mixel_lvds_phy_priv *priv = dev_get_drvdata(dev);
	unsigned int phy_id;

	if (args->args_count != 1) {
		dev_err(dev,
			"invalid argument number(%d) for 'phys' property\n",
			args->args_count);
		return ERR_PTR(-EINVAL);
	}

	phy_id = args->args[0];

	if (phy_id >= PHY_NUM) {
		dev_err(dev, "invalid PHY index(%d)\n", phy_id);
		return ERR_PTR(-ENODEV);
	}

	return priv->phys[phy_id]->phy;
}

static int mixel_lvds_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	struct mixel_lvds_phy_priv *priv;
	struct mixel_lvds_phy *lvds_phy;
	struct phy *phy;
	int i;
	int ret;

	if (!dev->of_node)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = syscon_node_to_regmap(dev->of_node->parent);
	if (IS_ERR(priv->regmap))
		return dev_err_probe(dev, PTR_ERR(priv->regmap),
				     "failed to get regmap\n");

	priv->phy_ref_clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->phy_ref_clk))
		return dev_err_probe(dev, PTR_ERR(priv->phy_ref_clk),
				     "failed to get PHY reference clock\n");

	mutex_init(&priv->lock);

	dev_set_drvdata(dev, priv);

	pm_runtime_enable(dev);

	ret = mixel_lvds_phy_reset(dev);
	if (ret) {
		dev_err(dev, "failed to do POR reset: %d\n", ret);
		return ret;
	}

	for (i = 0; i < PHY_NUM; i++) {
		lvds_phy = devm_kzalloc(dev, sizeof(*lvds_phy), GFP_KERNEL);
		if (!lvds_phy) {
			ret = -ENOMEM;
			goto err;
		}

		phy = devm_phy_create(dev, NULL, &mixel_lvds_phy_ops);
		if (IS_ERR(phy)) {
			ret = PTR_ERR(phy);
			dev_err(dev, "failed to create PHY for channel%d: %d\n",
				i, ret);
			goto err;
		}

		lvds_phy->phy = phy;
		lvds_phy->id = i;
		priv->phys[i] = lvds_phy;

		phy_set_drvdata(phy, lvds_phy);
	}

	phy_provider = devm_of_phy_provider_register(dev, mixel_lvds_phy_xlate);
	if (IS_ERR(phy_provider)) {
		ret = PTR_ERR(phy_provider);
		dev_err(dev, "failed to register PHY provider: %d\n", ret);
		goto err;
	}

	return 0;
err:
	pm_runtime_disable(dev);

	return ret;
}

static void mixel_lvds_phy_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static int __maybe_unused mixel_lvds_phy_runtime_suspend(struct device *dev)
{
	struct mixel_lvds_phy_priv *priv = dev_get_drvdata(dev);

	/* power down */
	mutex_lock(&priv->lock);
	regmap_write(priv->regmap, PHY_CTRL + REG_SET, PD);
	mutex_unlock(&priv->lock);

	return 0;
}

static int __maybe_unused mixel_lvds_phy_runtime_resume(struct device *dev)
{
	struct mixel_lvds_phy_priv *priv = dev_get_drvdata(dev);

	/* power up + control initialization */
	mutex_lock(&priv->lock);
	regmap_update_bits(priv->regmap, PHY_CTRL,
			   CTRL_INIT_MASK | PD, CTRL_INIT_VAL);
	mutex_unlock(&priv->lock);

	return 0;
}

static const struct dev_pm_ops mixel_lvds_phy_pm_ops = {
	SET_RUNTIME_PM_OPS(mixel_lvds_phy_runtime_suspend,
			   mixel_lvds_phy_runtime_resume, NULL)
};

static const struct of_device_id mixel_lvds_phy_of_match[] = {
	{ .compatible = "fsl,imx8qm-lvds-phy" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mixel_lvds_phy_of_match);

static struct platform_driver mixel_lvds_phy_driver = {
	.probe = mixel_lvds_phy_probe,
	.remove = mixel_lvds_phy_remove,
	.driver = {
		.pm = &mixel_lvds_phy_pm_ops,
		.name = "mixel-lvds-phy",
		.of_match_table = mixel_lvds_phy_of_match,
	}
};
module_platform_driver(mixel_lvds_phy_driver);

MODULE_DESCRIPTION("Mixel LVDS PHY driver");
MODULE_AUTHOR("Liu Ying <victor.liu@nxp.com>");
MODULE_LICENSE("GPL");
