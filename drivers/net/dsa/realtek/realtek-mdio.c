// SPDX-License-Identifier: GPL-2.0+
/* Realtek MDIO interface driver
 *
 * ASICs we intend to support with this driver:
 *
 * RTL8366   - The original version, apparently
 * RTL8369   - Similar enough to have the same datsheet as RTL8366
 * RTL8366RB - Probably reads out "RTL8366 revision B", has a quite
 *             different register layout from the other two
 * RTL8366S  - Is this "RTL8366 super"?
 * RTL8367   - Has an OpenWRT driver as well
 * RTL8368S  - Seems to be an alternative name for RTL8366RB
 * RTL8370   - Also uses SMI
 *
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 * Copyright (C) 2010 Antti Seppälä <a.seppala@gmail.com>
 * Copyright (C) 2010 Roman Yeryomin <roman@advem.lv>
 * Copyright (C) 2011 Colin Leitner <colin.leitner@googlemail.com>
 * Copyright (C) 2009-2010 Gabor Juhos <juhosg@openwrt.org>
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/overflow.h>
#include <linux/regmap.h>

#include "realtek.h"

/* Read/write via mdiobus */
#define REALTEK_MDIO_CTRL0_REG		31
#define REALTEK_MDIO_START_REG		29
#define REALTEK_MDIO_CTRL1_REG		21
#define REALTEK_MDIO_ADDRESS_REG	23
#define REALTEK_MDIO_DATA_WRITE_REG	24
#define REALTEK_MDIO_DATA_READ_REG	25

#define REALTEK_MDIO_START_OP		0xFFFF
#define REALTEK_MDIO_ADDR_OP		0x000E
#define REALTEK_MDIO_READ_OP		0x0001
#define REALTEK_MDIO_WRITE_OP		0x0003

static int realtek_mdio_write(void *ctx, u32 reg, u32 val)
{
	struct realtek_priv *priv = ctx;
	struct mii_bus *bus = priv->bus;
	int ret;

	mutex_lock(&bus->mdio_lock);

	ret = bus->write(bus, priv->mdio_addr, REALTEK_MDIO_CTRL0_REG, REALTEK_MDIO_ADDR_OP);
	if (ret)
		goto out_unlock;

	ret = bus->write(bus, priv->mdio_addr, REALTEK_MDIO_ADDRESS_REG, reg);
	if (ret)
		goto out_unlock;

	ret = bus->write(bus, priv->mdio_addr, REALTEK_MDIO_DATA_WRITE_REG, val);
	if (ret)
		goto out_unlock;

	ret = bus->write(bus, priv->mdio_addr, REALTEK_MDIO_CTRL1_REG, REALTEK_MDIO_WRITE_OP);

out_unlock:
	mutex_unlock(&bus->mdio_lock);

	return ret;
}

static int realtek_mdio_read(void *ctx, u32 reg, u32 *val)
{
	struct realtek_priv *priv = ctx;
	struct mii_bus *bus = priv->bus;
	int ret;

	mutex_lock(&bus->mdio_lock);

	ret = bus->write(bus, priv->mdio_addr, REALTEK_MDIO_CTRL0_REG, REALTEK_MDIO_ADDR_OP);
	if (ret)
		goto out_unlock;

	ret = bus->write(bus, priv->mdio_addr, REALTEK_MDIO_ADDRESS_REG, reg);
	if (ret)
		goto out_unlock;

	ret = bus->write(bus, priv->mdio_addr, REALTEK_MDIO_CTRL1_REG, REALTEK_MDIO_READ_OP);
	if (ret)
		goto out_unlock;

	ret = bus->read(bus, priv->mdio_addr, REALTEK_MDIO_DATA_READ_REG);
	if (ret >= 0) {
		*val = ret;
		ret = 0;
	}

out_unlock:
	mutex_unlock(&bus->mdio_lock);

	return ret;
}

static void realtek_mdio_lock(void *ctx)
{
	struct realtek_priv *priv = ctx;

	mutex_lock(&priv->map_lock);
}

static void realtek_mdio_unlock(void *ctx)
{
	struct realtek_priv *priv = ctx;

	mutex_unlock(&priv->map_lock);
}

static const struct regmap_config realtek_mdio_regmap_config = {
	.reg_bits = 10, /* A4..A0 R4..R0 */
	.val_bits = 16,
	.reg_stride = 1,
	/* PHY regs are at 0x8000 */
	.max_register = 0xffff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.reg_read = realtek_mdio_read,
	.reg_write = realtek_mdio_write,
	.cache_type = REGCACHE_NONE,
	.lock = realtek_mdio_lock,
	.unlock = realtek_mdio_unlock,
};

static const struct regmap_config realtek_mdio_nolock_regmap_config = {
	.reg_bits = 10, /* A4..A0 R4..R0 */
	.val_bits = 16,
	.reg_stride = 1,
	/* PHY regs are at 0x8000 */
	.max_register = 0xffff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.reg_read = realtek_mdio_read,
	.reg_write = realtek_mdio_write,
	.cache_type = REGCACHE_NONE,
	.disable_locking = true,
};

static int realtek_mdio_probe(struct mdio_device *mdiodev)
{
	struct realtek_priv *priv;
	struct device *dev = &mdiodev->dev;
	const struct realtek_variant *var;
	struct regmap_config rc;
	struct device_node *np;
	int ret;

	var = of_device_get_match_data(dev);
	if (!var)
		return -EINVAL;

	priv = devm_kzalloc(&mdiodev->dev,
			    size_add(sizeof(*priv), var->chip_data_sz),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->map_lock);

	rc = realtek_mdio_regmap_config;
	rc.lock_arg = priv;
	priv->map = devm_regmap_init(dev, NULL, priv, &rc);
	if (IS_ERR(priv->map)) {
		ret = PTR_ERR(priv->map);
		dev_err(dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	rc = realtek_mdio_nolock_regmap_config;
	priv->map_nolock = devm_regmap_init(dev, NULL, priv, &rc);
	if (IS_ERR(priv->map_nolock)) {
		ret = PTR_ERR(priv->map_nolock);
		dev_err(dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	priv->mdio_addr = mdiodev->addr;
	priv->bus = mdiodev->bus;
	priv->dev = &mdiodev->dev;
	priv->chip_data = (void *)priv + sizeof(*priv);

	priv->clk_delay = var->clk_delay;
	priv->cmd_read = var->cmd_read;
	priv->cmd_write = var->cmd_write;
	priv->ops = var->ops;

	priv->write_reg_noack = realtek_mdio_write;

	np = dev->of_node;

	dev_set_drvdata(dev, priv);

	/* TODO: if power is software controlled, set up any regulators here */
	priv->leds_disabled = of_property_read_bool(np, "realtek,disable-leds");

	priv->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(priv->reset)) {
		dev_err(dev, "failed to get RESET GPIO\n");
		return PTR_ERR(priv->reset);
	}

	if (priv->reset) {
		gpiod_set_value(priv->reset, 1);
		dev_dbg(dev, "asserted RESET\n");
		msleep(REALTEK_HW_STOP_DELAY);
		gpiod_set_value(priv->reset, 0);
		msleep(REALTEK_HW_START_DELAY);
		dev_dbg(dev, "deasserted RESET\n");
	}

	ret = priv->ops->detect(priv);
	if (ret) {
		dev_err(dev, "unable to detect switch\n");
		return ret;
	}

	priv->ds = devm_kzalloc(dev, sizeof(*priv->ds), GFP_KERNEL);
	if (!priv->ds)
		return -ENOMEM;

	priv->ds->dev = dev;
	priv->ds->num_ports = priv->num_ports;
	priv->ds->priv = priv;
	priv->ds->ops = var->ds_ops_mdio;

	ret = dsa_register_switch(priv->ds);
	if (ret) {
		dev_err(priv->dev, "unable to register switch ret = %d\n", ret);
		return ret;
	}

	return 0;
}

static void realtek_mdio_remove(struct mdio_device *mdiodev)
{
	struct realtek_priv *priv = dev_get_drvdata(&mdiodev->dev);

	if (!priv)
		return;

	dsa_unregister_switch(priv->ds);

	/* leave the device reset asserted */
	if (priv->reset)
		gpiod_set_value(priv->reset, 1);
}

static void realtek_mdio_shutdown(struct mdio_device *mdiodev)
{
	struct realtek_priv *priv = dev_get_drvdata(&mdiodev->dev);

	if (!priv)
		return;

	dsa_switch_shutdown(priv->ds);

	dev_set_drvdata(&mdiodev->dev, NULL);
}

static const struct of_device_id realtek_mdio_of_match[] = {
#if IS_ENABLED(CONFIG_NET_DSA_REALTEK_RTL8366RB)
	{ .compatible = "realtek,rtl8366rb", .data = &rtl8366rb_variant, },
#endif
#if IS_ENABLED(CONFIG_NET_DSA_REALTEK_RTL8365MB)
	{ .compatible = "realtek,rtl8365mb", .data = &rtl8365mb_variant, },
#endif
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, realtek_mdio_of_match);

static struct mdio_driver realtek_mdio_driver = {
	.mdiodrv.driver = {
		.name = "realtek-mdio",
		.of_match_table = of_match_ptr(realtek_mdio_of_match),
	},
	.probe  = realtek_mdio_probe,
	.remove = realtek_mdio_remove,
	.shutdown = realtek_mdio_shutdown,
};

mdio_module_driver(realtek_mdio_driver);

MODULE_AUTHOR("Luiz Angelo Daros de Luca <luizluca@gmail.com>");
MODULE_DESCRIPTION("Driver for Realtek ethernet switch connected via MDIO interface");
MODULE_LICENSE("GPL");
