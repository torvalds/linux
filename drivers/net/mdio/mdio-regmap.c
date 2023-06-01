// SPDX-License-Identifier: GPL-2.0-or-later
/* Driver for MMIO-Mapped MDIO devices. Some IPs expose internal PHYs or PCS
 * within the MMIO-mapped area
 *
 * Copyright (C) 2023 Maxime Chevallier <maxime.chevallier@bootlin.com>
 */
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mdio/mdio-regmap.h>

#define DRV_NAME "mdio-regmap"

struct mdio_regmap_priv {
	struct regmap *regmap;
	u8 valid_addr;
};

static int mdio_regmap_read_c22(struct mii_bus *bus, int addr, int regnum)
{
	struct mdio_regmap_priv *ctx = bus->priv;
	unsigned int val;
	int ret;

	if (ctx->valid_addr != addr)
		return -ENODEV;

	ret = regmap_read(ctx->regmap, regnum, &val);
	if (ret < 0)
		return ret;

	return val;
}

static int mdio_regmap_write_c22(struct mii_bus *bus, int addr, int regnum,
				 u16 val)
{
	struct mdio_regmap_priv *ctx = bus->priv;

	if (ctx->valid_addr != addr)
		return -ENODEV;

	return regmap_write(ctx->regmap, regnum, val);
}

struct mii_bus *devm_mdio_regmap_register(struct device *dev,
					  const struct mdio_regmap_config *config)
{
	struct mdio_regmap_priv *mr;
	struct mii_bus *mii;
	int rc;

	if (!config->parent)
		return ERR_PTR(-EINVAL);

	mii = devm_mdiobus_alloc_size(config->parent, sizeof(*mr));
	if (!mii)
		return ERR_PTR(-ENOMEM);

	mr = mii->priv;
	mr->regmap = config->regmap;
	mr->valid_addr = config->valid_addr;

	mii->name = DRV_NAME;
	strscpy(mii->id, config->name, MII_BUS_ID_SIZE);
	mii->parent = config->parent;
	mii->read = mdio_regmap_read_c22;
	mii->write = mdio_regmap_write_c22;

	if (config->autoscan)
		mii->phy_mask = ~BIT(config->valid_addr);
	else
		mii->phy_mask = ~0;

	rc = devm_mdiobus_register(dev, mii);
	if (rc) {
		dev_err(config->parent, "Cannot register MDIO bus![%s] (%d)\n", mii->id, rc);
		return ERR_PTR(rc);
	}

	return mii;
}
EXPORT_SYMBOL_GPL(devm_mdio_regmap_register);

MODULE_DESCRIPTION("MDIO API over regmap");
MODULE_AUTHOR("Maxime Chevallier <maxime.chevallier@bootlin.com>");
MODULE_LICENSE("GPL");
