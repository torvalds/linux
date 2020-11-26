// SPDX-License-Identifier: GPL-2.0
/*
 * MDIO I2C bridge
 *
 * Copyright (C) 2015-2016 Russell King
 *
 * Network PHYs can appear on I2C buses when they are part of SFP module.
 * This driver exposes these PHYs to the networking PHY code, allowing
 * our PHY drivers access to these PHYs, and so allowing configuration
 * of their settings.
 */
#include <linux/i2c.h>
#include <linux/mdio/mdio-i2c.h>
#include <linux/phy.h>

/*
 * I2C bus addresses 0x50 and 0x51 are normally an EEPROM, which is
 * specified to be present in SFP modules.  These correspond with PHY
 * addresses 16 and 17.  Disallow access to these "phy" addresses.
 */
static bool i2c_mii_valid_phy_id(int phy_id)
{
	return phy_id != 0x10 && phy_id != 0x11;
}

static unsigned int i2c_mii_phy_addr(int phy_id)
{
	return phy_id + 0x40;
}

static int i2c_mii_read(struct mii_bus *bus, int phy_id, int reg)
{
	struct i2c_adapter *i2c = bus->priv;
	struct i2c_msg msgs[2];
	u8 addr[3], data[2], *p;
	int bus_addr, ret;

	if (!i2c_mii_valid_phy_id(phy_id))
		return 0xffff;

	p = addr;
	if (reg & MII_ADDR_C45) {
		*p++ = 0x20 | ((reg >> 16) & 31);
		*p++ = reg >> 8;
	}
	*p++ = reg;

	bus_addr = i2c_mii_phy_addr(phy_id);
	msgs[0].addr = bus_addr;
	msgs[0].flags = 0;
	msgs[0].len = p - addr;
	msgs[0].buf = addr;
	msgs[1].addr = bus_addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = sizeof(data);
	msgs[1].buf = data;

	ret = i2c_transfer(i2c, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return 0xffff;

	return data[0] << 8 | data[1];
}

static int i2c_mii_write(struct mii_bus *bus, int phy_id, int reg, u16 val)
{
	struct i2c_adapter *i2c = bus->priv;
	struct i2c_msg msg;
	int ret;
	u8 data[5], *p;

	if (!i2c_mii_valid_phy_id(phy_id))
		return 0;

	p = data;
	if (reg & MII_ADDR_C45) {
		*p++ = (reg >> 16) & 31;
		*p++ = reg >> 8;
	}
	*p++ = reg;
	*p++ = val >> 8;
	*p++ = val;

	msg.addr = i2c_mii_phy_addr(phy_id);
	msg.flags = 0;
	msg.len = p - data;
	msg.buf = data;

	ret = i2c_transfer(i2c, &msg, 1);

	return ret < 0 ? ret : 0;
}

struct mii_bus *mdio_i2c_alloc(struct device *parent, struct i2c_adapter *i2c)
{
	struct mii_bus *mii;

	if (!i2c_check_functionality(i2c, I2C_FUNC_I2C))
		return ERR_PTR(-EINVAL);

	mii = mdiobus_alloc();
	if (!mii)
		return ERR_PTR(-ENOMEM);

	snprintf(mii->id, MII_BUS_ID_SIZE, "i2c:%s", dev_name(parent));
	mii->parent = parent;
	mii->read = i2c_mii_read;
	mii->write = i2c_mii_write;
	mii->priv = i2c;

	return mii;
}
EXPORT_SYMBOL_GPL(mdio_i2c_alloc);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("MDIO I2C bridge library");
MODULE_LICENSE("GPL v2");
