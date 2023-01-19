// SPDX-License-Identifier: GPL-2.0
/*
 * MDIO I2C bridge
 *
 * Copyright (C) 2015-2016 Russell King
 * Copyright (C) 2021 Marek Behun
 *
 * Network PHYs can appear on I2C buses when they are part of SFP module.
 * This driver exposes these PHYs to the networking PHY code, allowing
 * our PHY drivers access to these PHYs, and so allowing configuration
 * of their settings.
 */
#include <linux/i2c.h>
#include <linux/mdio/mdio-i2c.h>
#include <linux/phy.h>
#include <linux/sfp.h>

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

static int i2c_mii_read_default_c45(struct mii_bus *bus, int phy_id, int devad,
				    int reg)
{
	struct i2c_adapter *i2c = bus->priv;
	struct i2c_msg msgs[2];
	u8 addr[3], data[2], *p;
	int bus_addr, ret;

	if (!i2c_mii_valid_phy_id(phy_id))
		return 0xffff;

	p = addr;
	if (devad >= 0) {
		*p++ = 0x20 | devad;
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

static int i2c_mii_write_default_c45(struct mii_bus *bus, int phy_id,
				     int devad, int reg, u16 val)
{
	struct i2c_adapter *i2c = bus->priv;
	struct i2c_msg msg;
	int ret;
	u8 data[5], *p;

	if (!i2c_mii_valid_phy_id(phy_id))
		return 0;

	p = data;
	if (devad >= 0) {
		*p++ = devad;
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

static int i2c_mii_read_default_c22(struct mii_bus *bus, int phy_id, int reg)
{
	return i2c_mii_read_default_c45(bus, phy_id, -1, reg);
}

static int i2c_mii_write_default_c22(struct mii_bus *bus, int phy_id, int reg,
				     u16 val)
{
	return i2c_mii_write_default_c45(bus, phy_id, -1, reg, val);
}

/* RollBall SFPs do not access internal PHY via I2C address 0x56, but
 * instead via address 0x51, when SFP page is set to 0x03 and password to
 * 0xffffffff.
 *
 * address  size  contents  description
 * -------  ----  --------  -----------
 * 0x80     1     CMD       0x01/0x02/0x04 for write/read/done
 * 0x81     1     DEV       Clause 45 device
 * 0x82     2     REG       Clause 45 register
 * 0x84     2     VAL       Register value
 */
#define ROLLBALL_PHY_I2C_ADDR		0x51

#define ROLLBALL_PASSWORD		(SFP_VSL + 3)

#define ROLLBALL_CMD_ADDR		0x80
#define ROLLBALL_DATA_ADDR		0x81

#define ROLLBALL_CMD_WRITE		0x01
#define ROLLBALL_CMD_READ		0x02
#define ROLLBALL_CMD_DONE		0x04

#define SFP_PAGE_ROLLBALL_MDIO		3

static int __i2c_transfer_err(struct i2c_adapter *i2c, struct i2c_msg *msgs,
			      int num)
{
	int ret;

	ret = __i2c_transfer(i2c, msgs, num);
	if (ret < 0)
		return ret;
	else if (ret != num)
		return -EIO;
	else
		return 0;
}

static int __i2c_rollball_get_page(struct i2c_adapter *i2c, int bus_addr,
				   u8 *page)
{
	struct i2c_msg msgs[2];
	u8 addr = SFP_PAGE;

	msgs[0].addr = bus_addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &addr;

	msgs[1].addr = bus_addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = page;

	return __i2c_transfer_err(i2c, msgs, 2);
}

static int __i2c_rollball_set_page(struct i2c_adapter *i2c, int bus_addr,
				   u8 page)
{
	struct i2c_msg msg;
	u8 buf[2];

	buf[0] = SFP_PAGE;
	buf[1] = page;

	msg.addr = bus_addr;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = buf;

	return __i2c_transfer_err(i2c, &msg, 1);
}

/* In order to not interfere with other SFP code (which possibly may manipulate
 * SFP_PAGE), for every transfer we do this:
 *   1. lock the bus
 *   2. save content of SFP_PAGE
 *   3. set SFP_PAGE to 3
 *   4. do the transfer
 *   5. restore original SFP_PAGE
 *   6. unlock the bus
 * Note that one might think that steps 2 to 5 could be theoretically done all
 * in one call to i2c_transfer (by constructing msgs array in such a way), but
 * unfortunately tests show that this does not work :-( Changed SFP_PAGE does
 * not take into account until i2c_transfer() is done.
 */
static int i2c_transfer_rollball(struct i2c_adapter *i2c,
				 struct i2c_msg *msgs, int num)
{
	int ret, main_err = 0;
	u8 saved_page;

	i2c_lock_bus(i2c, I2C_LOCK_SEGMENT);

	/* save original page */
	ret = __i2c_rollball_get_page(i2c, msgs->addr, &saved_page);
	if (ret)
		goto unlock;

	/* change to RollBall MDIO page */
	ret = __i2c_rollball_set_page(i2c, msgs->addr, SFP_PAGE_ROLLBALL_MDIO);
	if (ret)
		goto unlock;

	/* do the transfer; we try to restore original page if this fails */
	ret = __i2c_transfer_err(i2c, msgs, num);
	if (ret)
		main_err = ret;

	/* restore original page */
	ret = __i2c_rollball_set_page(i2c, msgs->addr, saved_page);

unlock:
	i2c_unlock_bus(i2c, I2C_LOCK_SEGMENT);

	return main_err ? : ret;
}

static int i2c_rollball_mii_poll(struct mii_bus *bus, int bus_addr, u8 *buf,
				 size_t len)
{
	struct i2c_adapter *i2c = bus->priv;
	struct i2c_msg msgs[2];
	u8 cmd_addr, tmp, *res;
	int i, ret;

	cmd_addr = ROLLBALL_CMD_ADDR;

	res = buf ? buf : &tmp;
	len = buf ? len : 1;

	msgs[0].addr = bus_addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &cmd_addr;

	msgs[1].addr = bus_addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = res;

	/* By experiment it takes up to 70 ms to access a register for these
	 * SFPs. Sleep 20ms between iterations and try 10 times.
	 */
	i = 10;
	do {
		msleep(20);

		ret = i2c_transfer_rollball(i2c, msgs, ARRAY_SIZE(msgs));
		if (ret)
			return ret;

		if (*res == ROLLBALL_CMD_DONE)
			return 0;
	} while (i-- > 0);

	dev_dbg(&bus->dev, "poll timed out\n");

	return -ETIMEDOUT;
}

static int i2c_rollball_mii_cmd(struct mii_bus *bus, int bus_addr, u8 cmd,
				u8 *data, size_t len)
{
	struct i2c_adapter *i2c = bus->priv;
	struct i2c_msg msgs[2];
	u8 cmdbuf[2];

	cmdbuf[0] = ROLLBALL_CMD_ADDR;
	cmdbuf[1] = cmd;

	msgs[0].addr = bus_addr;
	msgs[0].flags = 0;
	msgs[0].len = len;
	msgs[0].buf = data;

	msgs[1].addr = bus_addr;
	msgs[1].flags = 0;
	msgs[1].len = sizeof(cmdbuf);
	msgs[1].buf = cmdbuf;

	return i2c_transfer_rollball(i2c, msgs, ARRAY_SIZE(msgs));
}

static int i2c_mii_read_rollball(struct mii_bus *bus, int phy_id, int reg)
{
	u8 buf[4], res[6];
	int bus_addr, ret;
	u16 val;

	bus_addr = i2c_mii_phy_addr(phy_id);
	if (bus_addr != ROLLBALL_PHY_I2C_ADDR)
		return 0xffff;

	buf[0] = ROLLBALL_DATA_ADDR;
	buf[1] = (reg >> 16) & 0x1f;
	buf[2] = (reg >> 8) & 0xff;
	buf[3] = reg & 0xff;

	ret = i2c_rollball_mii_cmd(bus, bus_addr, ROLLBALL_CMD_READ, buf,
				   sizeof(buf));
	if (ret < 0)
		return ret;

	ret = i2c_rollball_mii_poll(bus, bus_addr, res, sizeof(res));
	if (ret == -ETIMEDOUT)
		return 0xffff;
	else if (ret < 0)
		return ret;

	val = res[4] << 8 | res[5];

	return val;
}

static int i2c_mii_write_rollball(struct mii_bus *bus, int phy_id, int reg,
				  u16 val)
{
	int bus_addr, ret;
	u8 buf[6];

	bus_addr = i2c_mii_phy_addr(phy_id);
	if (bus_addr != ROLLBALL_PHY_I2C_ADDR)
		return 0;

	buf[0] = ROLLBALL_DATA_ADDR;
	buf[1] = (reg >> 16) & 0x1f;
	buf[2] = (reg >> 8) & 0xff;
	buf[3] = reg & 0xff;
	buf[4] = val >> 8;
	buf[5] = val & 0xff;

	ret = i2c_rollball_mii_cmd(bus, bus_addr, ROLLBALL_CMD_WRITE, buf,
				   sizeof(buf));
	if (ret < 0)
		return ret;

	ret = i2c_rollball_mii_poll(bus, bus_addr, NULL, 0);
	if (ret < 0)
		return ret;

	return 0;
}

static int i2c_mii_init_rollball(struct i2c_adapter *i2c)
{
	struct i2c_msg msg;
	u8 pw[5];
	int ret;

	pw[0] = ROLLBALL_PASSWORD;
	pw[1] = 0xff;
	pw[2] = 0xff;
	pw[3] = 0xff;
	pw[4] = 0xff;

	msg.addr = ROLLBALL_PHY_I2C_ADDR;
	msg.flags = 0;
	msg.len = sizeof(pw);
	msg.buf = pw;

	ret = i2c_transfer(i2c, &msg, 1);
	if (ret < 0)
		return ret;
	else if (ret != 1)
		return -EIO;
	else
		return 0;
}

struct mii_bus *mdio_i2c_alloc(struct device *parent, struct i2c_adapter *i2c,
			       enum mdio_i2c_proto protocol)
{
	struct mii_bus *mii;
	int ret;

	if (!i2c_check_functionality(i2c, I2C_FUNC_I2C))
		return ERR_PTR(-EINVAL);

	mii = mdiobus_alloc();
	if (!mii)
		return ERR_PTR(-ENOMEM);

	snprintf(mii->id, MII_BUS_ID_SIZE, "i2c:%s", dev_name(parent));
	mii->parent = parent;
	mii->priv = i2c;

	switch (protocol) {
	case MDIO_I2C_ROLLBALL:
		ret = i2c_mii_init_rollball(i2c);
		if (ret < 0) {
			dev_err(parent,
				"Cannot initialize RollBall MDIO I2C protocol: %d\n",
				ret);
			mdiobus_free(mii);
			return ERR_PTR(ret);
		}

		mii->read = i2c_mii_read_rollball;
		mii->write = i2c_mii_write_rollball;
		break;
	default:
		mii->read = i2c_mii_read_default_c22;
		mii->write = i2c_mii_write_default_c22;
		mii->read_c45 = i2c_mii_read_default_c45;
		mii->write_c45 = i2c_mii_write_default_c45;
		break;
	}

	return mii;
}
EXPORT_SYMBOL_GPL(mdio_i2c_alloc);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("MDIO I2C bridge library");
MODULE_LICENSE("GPL v2");
