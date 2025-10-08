// SPDX-License-Identifier: GPL-2.0-only

#include <linux/gpio/consumer.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/pcs/pcs-mtk-lynxi.h>
#include <linux/of_irq.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/regulator/consumer.h>
#include <net/dsa.h>

#include "mt7530.h"

static int
mt7530_regmap_write(void *context, unsigned int reg, unsigned int val)
{
	struct mt7530_priv *priv = context;
	struct mii_bus *bus = priv->bus;
	u16 page, r, lo, hi;
	int ret;

	page = (reg >> 6) & 0x3ff;
	r  = (reg >> 2) & 0xf;
	lo = val & 0xffff;
	hi = val >> 16;

	ret = bus->write(bus, priv->mdiodev->addr, 0x1f, page);
	if (ret < 0)
		return ret;

	ret = bus->write(bus, priv->mdiodev->addr, r, lo);
	if (ret < 0)
		return ret;

	ret = bus->write(bus, priv->mdiodev->addr, 0x10, hi);
	return ret;
}

static int
mt7530_regmap_read(void *context, unsigned int reg, unsigned int *val)
{
	struct mt7530_priv *priv = context;
	struct mii_bus *bus = priv->bus;
	u16 page, r, lo, hi;
	int ret;

	page = (reg >> 6) & 0x3ff;
	r = (reg >> 2) & 0xf;

	ret = bus->write(bus, priv->mdiodev->addr, 0x1f, page);
	if (ret < 0)
		return ret;

	lo = bus->read(bus, priv->mdiodev->addr, r);
	hi = bus->read(bus, priv->mdiodev->addr, 0x10);

	*val = (hi << 16) | (lo & 0xffff);

	return 0;
}

static void
mt7530_mdio_regmap_lock(void *mdio_lock)
{
	mutex_lock_nested(mdio_lock, MDIO_MUTEX_NESTED);
}

static void
mt7530_mdio_regmap_unlock(void *mdio_lock)
{
	mutex_unlock(mdio_lock);
}

static const struct regmap_bus mt7530_regmap_bus = {
	.reg_write = mt7530_regmap_write,
	.reg_read = mt7530_regmap_read,
};

static int
mt7531_create_sgmii(struct mt7530_priv *priv)
{
	struct regmap_config *mt7531_pcs_config[2] = {};
	struct phylink_pcs *pcs;
	struct regmap *regmap;
	int i, ret = 0;

	for (i = priv->p5_sgmii ? 0 : 1; i < 2; i++) {
		mt7531_pcs_config[i] = devm_kzalloc(priv->dev,
						    sizeof(struct regmap_config),
						    GFP_KERNEL);
		if (!mt7531_pcs_config[i]) {
			ret = -ENOMEM;
			break;
		}

		mt7531_pcs_config[i]->name = i ? "port6" : "port5";
		mt7531_pcs_config[i]->reg_bits = 16;
		mt7531_pcs_config[i]->val_bits = 32;
		mt7531_pcs_config[i]->reg_stride = 4;
		mt7531_pcs_config[i]->reg_base = MT7531_SGMII_REG_BASE(5 + i);
		mt7531_pcs_config[i]->max_register = 0x17c;
		mt7531_pcs_config[i]->lock = mt7530_mdio_regmap_lock;
		mt7531_pcs_config[i]->unlock = mt7530_mdio_regmap_unlock;
		mt7531_pcs_config[i]->lock_arg = &priv->bus->mdio_lock;

		regmap = devm_regmap_init(priv->dev, &mt7530_regmap_bus, priv,
					  mt7531_pcs_config[i]);
		if (IS_ERR(regmap)) {
			ret = PTR_ERR(regmap);
			break;
		}
		pcs = mtk_pcs_lynxi_create(priv->dev, regmap,
					   MT7531_PHYA_CTRL_SIGNAL3, 0);
		if (!pcs) {
			ret = -ENXIO;
			break;
		}
		priv->ports[5 + i].sgmii_pcs = pcs;
	}

	if (ret && i)
		mtk_pcs_lynxi_destroy(priv->ports[5].sgmii_pcs);

	return ret;
}

static const struct of_device_id mt7530_of_match[] = {
	{ .compatible = "mediatek,mt7621", .data = &mt753x_table[ID_MT7621], },
	{ .compatible = "mediatek,mt7530", .data = &mt753x_table[ID_MT7530], },
	{ .compatible = "mediatek,mt7531", .data = &mt753x_table[ID_MT7531], },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mt7530_of_match);

static const struct regmap_config regmap_config = {
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = MT7530_CREV,
	.disable_locking = true,
};

static int
mt7530_probe(struct mdio_device *mdiodev)
{
	struct mt7530_priv *priv;
	struct device_node *dn;
	int ret;

	dn = mdiodev->dev.of_node;

	priv = devm_kzalloc(&mdiodev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->bus = mdiodev->bus;
	priv->dev = &mdiodev->dev;
	priv->mdiodev = mdiodev;

	ret = mt7530_probe_common(priv);
	if (ret)
		return ret;

	/* Use medatek,mcm property to distinguish hardware type that would
	 * cause a little bit differences on power-on sequence.
	 * Not MCM that indicates switch works as the remote standalone
	 * integrated circuit so the GPIO pin would be used to complete
	 * the reset, otherwise memory-mapped register accessing used
	 * through syscon provides in the case of MCM.
	 */
	priv->mcm = of_property_read_bool(dn, "mediatek,mcm");
	if (priv->mcm) {
		dev_info(&mdiodev->dev, "MT7530 adapts as multi-chip module\n");

		priv->rstc = devm_reset_control_get(&mdiodev->dev, "mcm");
		if (IS_ERR(priv->rstc)) {
			dev_err(&mdiodev->dev, "Couldn't get our reset line\n");
			return PTR_ERR(priv->rstc);
		}
	} else {
		priv->reset = devm_gpiod_get_optional(&mdiodev->dev, "reset",
						      GPIOD_OUT_LOW);
		if (IS_ERR(priv->reset)) {
			dev_err(&mdiodev->dev, "Couldn't get our reset line\n");
			return PTR_ERR(priv->reset);
		}
	}

	if (priv->id == ID_MT7530) {
		priv->core_pwr = devm_regulator_get(&mdiodev->dev, "core");
		if (IS_ERR(priv->core_pwr))
			return PTR_ERR(priv->core_pwr);

		priv->io_pwr = devm_regulator_get(&mdiodev->dev, "io");
		if (IS_ERR(priv->io_pwr))
			return PTR_ERR(priv->io_pwr);
	}

	priv->regmap = devm_regmap_init(priv->dev, &mt7530_regmap_bus, priv,
					&regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	if (priv->id == ID_MT7531)
		priv->create_sgmii = mt7531_create_sgmii;

	return dsa_register_switch(priv->ds);
}

static void
mt7530_remove(struct mdio_device *mdiodev)
{
	struct mt7530_priv *priv = dev_get_drvdata(&mdiodev->dev);
	int ret = 0, i;

	if (!priv)
		return;

	ret = regulator_disable(priv->core_pwr);
	if (ret < 0)
		dev_err(priv->dev,
			"Failed to disable core power: %d\n", ret);

	ret = regulator_disable(priv->io_pwr);
	if (ret < 0)
		dev_err(priv->dev, "Failed to disable io pwr: %d\n",
			ret);

	mt7530_remove_common(priv);

	for (i = 0; i < 2; ++i)
		mtk_pcs_lynxi_destroy(priv->ports[5 + i].sgmii_pcs);
}

static void mt7530_shutdown(struct mdio_device *mdiodev)
{
	struct mt7530_priv *priv = dev_get_drvdata(&mdiodev->dev);

	if (!priv)
		return;

	dsa_switch_shutdown(priv->ds);

	dev_set_drvdata(&mdiodev->dev, NULL);
}

static struct mdio_driver mt7530_mdio_driver = {
	.probe  = mt7530_probe,
	.remove = mt7530_remove,
	.shutdown = mt7530_shutdown,
	.mdiodrv.driver = {
		.name = "mt7530-mdio",
		.of_match_table = mt7530_of_match,
	},
};

mdio_module_driver(mt7530_mdio_driver);

MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_DESCRIPTION("Driver for Mediatek MT7530 Switch (MDIO)");
MODULE_LICENSE("GPL");
