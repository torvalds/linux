// SPDX-License-Identifier: GPL-2.0
/* Copyright 2026 NXP */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/slab.h>

#define DS125DF111_NUM_CH			2
#define DS125DF111_NUM_VCO_GROUP_REG		5

#define DS125DF111_CH_SELECT			0xff
#define DS125DF111_CH_SELECT_TARGET_MASK	GENMASK(3, 0)
#define DS125DF111_CH_SELECT_EN			BIT(2)

#define DS125DF111_CH_CTRL			0x00
#define DS125DF111_CH_CTRL_RESET		BIT(2) /* self clearing */

#define DS125DF111_CH_RST_SLEEP_US		10
#define DS125DF111_CH_RST_TIMEOUT_US		10000

#define DS125DF111_VCO_GROUP_BASE		0x60

#define DS125DF111_RATIOS			0x2f
#define DS125DF111_RATIOS_RATE_MASK		GENMASK(7, 6)
#define DS125DF111_RATIOS_SUBRATE_MASK		GENMASK(5, 4)
#define DS125DF111_RATIOS_MASK			GENMASK(7, 4)

struct ds125df111_ch {
	struct phy *phy;
	struct ds125df111_priv *priv;
	int idx;
};

struct ds125df111_priv {
	struct ds125df111_ch ch[DS125DF111_NUM_CH];
	struct i2c_client *client;
	struct mutex mutex; /* protects access to shared registers */
};

enum ds125df111_mode {
	FREQ_1G,
	FREQ_10G,
};

static const struct ds125df111_config {
	u8 vco_group[DS125DF111_NUM_VCO_GROUP_REG];
	u8 rate;
	u8 subrate;
} ds125df111_cfg[] = {
	[FREQ_1G] = {
		/* VCO group #0 = 10GHz, VCO group #1 = 10GHz */
		.vco_group = {0x00, 0xB2, 0x00, 0xB2, 0xCC},
		/* By using the following combination of rate and subrate we
		 * select divide ratios of 1, 2, 4, 8 on both groups
		 */
		.rate = 0x1,
		.subrate = 0x2,
	},

	[FREQ_10G] = {
		/* VCO group #0 = 10.3125GHz, VCO group #1 = 10.3125GHz */
		.vco_group = {0x90, 0xB3, 0x90, 0xB3, 0xCD},
		/* By using the following combination of rate and subrate we
		 * select divide ratios of 1 on both groups
		 */
		.rate = 0x1,
		.subrate = 0x3,
	},
};

static int ds125df111_rmw(struct ds125df111_priv *priv, u8 reg, u8 clr, u8 set)
{
	struct i2c_client *i2c = priv->client;
	int err;
	u8 val;

	err = i2c_smbus_read_byte_data(i2c, reg);
	if (err < 0)
		return err;

	val = (u8)err;
	val &= ~clr;
	val |= set;

	err = i2c_smbus_write_byte_data(i2c, reg, val);
	if (err < 0)
		return err;

	return 0;
}

static int ds125df111_configure(struct phy *phy,
				const struct ds125df111_config *cfg)
{
	struct ds125df111_ch *ch = phy_get_drvdata(phy);
	struct ds125df111_priv *priv = ch->priv;
	struct i2c_client *i2c = priv->client;
	struct device *dev = &phy->dev;
	u8 ratios_val;
	int err, i;
	int val;

	mutex_lock(&priv->mutex);

	/* Make sure that any subsequent read/write operation will be directed
	 * only to the registers of the selected channel
	 */
	err = ds125df111_rmw(priv, DS125DF111_CH_SELECT,
			     DS125DF111_CH_SELECT_TARGET_MASK,
			     DS125DF111_CH_SELECT_EN | ch->idx);
	if (err < 0) {
		dev_err(dev, "Unable to select channel: %pe\n", ERR_PTR(err));
		goto out;
	}

	/* Reset channel registers and wait until the bit was cleared */
	err = ds125df111_rmw(priv, DS125DF111_CH_CTRL, 0,
			     DS125DF111_CH_CTRL_RESET);
	if (err < 0) {
		dev_err(dev, "Error resetting channel configuration: %pe\n",
			ERR_PTR(err));
		goto out;
	}

	err = read_poll_timeout(i2c_smbus_read_byte_data, val,
				val < 0 || !(val & DS125DF111_CH_CTRL_RESET),
				DS125DF111_CH_RST_SLEEP_US,
				DS125DF111_CH_RST_TIMEOUT_US, false, i2c,
				DS125DF111_CH_CTRL);
	if (err) {
		dev_err(dev, "Timed out waiting for channel reset: %pe\n",
			ERR_PTR(err));
		goto out;
	}

	if (val < 0) {
		dev_err(dev, "Error reading reset status: %pe\n", ERR_PTR(val));
		err = val;
		goto out;
	}

	/* Program the VCO group frequencies */
	for (i = 0; i < DS125DF111_NUM_VCO_GROUP_REG; i++) {
		err = i2c_smbus_write_byte_data(i2c,
						DS125DF111_VCO_GROUP_BASE + i,
						cfg->vco_group[i]);
		if (err < 0) {
			dev_err(dev, "Error programming VCO group: %pe\n",
				ERR_PTR(err));
			goto out;
		}
	}

	/* Set the divide ratios for the VCO groups */
	ratios_val = FIELD_PREP(DS125DF111_RATIOS_RATE_MASK, cfg->rate) |
		FIELD_PREP(DS125DF111_RATIOS_SUBRATE_MASK, cfg->subrate);
	err = ds125df111_rmw(priv, DS125DF111_RATIOS, DS125DF111_RATIOS_MASK,
			     ratios_val);
	if (err < 0) {
		dev_err(dev, "Error programming the divide ratios: %pe\n",
			ERR_PTR(err));
		goto out;
	}

out:
	mutex_unlock(&priv->mutex);

	return err;
}

static int ds125df111_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	const struct ds125df111_config *cfg;

	if (mode != PHY_MODE_ETHERNET)
		return -EINVAL;

	switch (submode) {
	case PHY_INTERFACE_MODE_10GBASER:
		cfg = &ds125df111_cfg[FREQ_10G];
		break;
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_SGMII:
		cfg = &ds125df111_cfg[FREQ_1G];
		break;
	default:
		return -EINVAL;
	}

	return ds125df111_configure(phy, cfg);
}

static int ds125df111_validate(struct phy *phy, enum phy_mode mode, int submode,
			       union phy_configure_opts *opts __always_unused)
{
	if (mode != PHY_MODE_ETHERNET)
		return -EINVAL;

	switch (submode) {
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_SGMII:
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct phy_ops ds125df111_ops = {
	.validate	= ds125df111_validate,
	.set_mode	= ds125df111_set_mode,
	.owner		= THIS_MODULE,
};

static struct phy *ds125df111_xlate(struct device *dev,
				    const struct of_phandle_args *args)
{
	struct ds125df111_priv *priv = dev_get_drvdata(dev);
	u32 idx;

	if (args->args_count != 1)
		return ERR_PTR(-EINVAL);

	idx = args->args[0];
	if (idx >= DS125DF111_NUM_CH) {
		dev_err(dev, "Maximum number of channels is %d\n",
			DS125DF111_NUM_CH);
		return ERR_PTR(-EINVAL);
	}

	return priv->ch[idx].phy;
}

static int ds125df111_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct phy_provider *provider;
	struct ds125df111_priv *priv;
	int i, err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->client = client;
	err = devm_mutex_init(dev, &priv->mutex);
	if (err)
		return err;

	i2c_set_clientdata(client, priv);

	for (i = 0; i < DS125DF111_NUM_CH; i++) {
		struct ds125df111_ch *ch = &priv->ch[i];
		struct phy *phy;

		phy = devm_phy_create(dev, NULL, &ds125df111_ops);
		if (IS_ERR(phy))
			return PTR_ERR(phy);

		ch->idx = i;
		ch->priv = priv;
		ch->phy = phy;

		phy_set_drvdata(phy, ch);
	}

	provider = devm_of_phy_provider_register(dev, ds125df111_xlate);

	return PTR_ERR_OR_ZERO(provider);
}

static const struct of_device_id ds125df111_dt_ids[] = {
	{ .compatible = "ti,ds125df111", },
	{}
};
MODULE_DEVICE_TABLE(of, ds125df111_dt_ids);

static struct i2c_driver ds125df111_driver = {
	.driver = {
		.name = "ds125df111",
		.of_match_table = ds125df111_dt_ids,
	},
	.probe = ds125df111_probe,
};
module_i2c_driver(ds125df111_driver);

MODULE_AUTHOR("Ioana Ciornei <ioana.ciornei@nxp.com>");
MODULE_DESCRIPTION("TI DS125DF111 Retimer driver");
MODULE_LICENSE("GPL");
