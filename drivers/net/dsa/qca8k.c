// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2011-2012 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (c) 2015, 2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016 John Crispin <john@phrozen.org>
 */

#include <linux/module.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <net/dsa.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/if_bridge.h>
#include <linux/mdio.h>
#include <linux/phylink.h>
#include <linux/gpio/consumer.h>
#include <linux/etherdevice.h>

#include "qca8k.h"

#define MIB_DESC(_s, _o, _n)	\
	{			\
		.size = (_s),	\
		.offset = (_o),	\
		.name = (_n),	\
	}

static const struct qca8k_mib_desc ar8327_mib[] = {
	MIB_DESC(1, 0x00, "RxBroad"),
	MIB_DESC(1, 0x04, "RxPause"),
	MIB_DESC(1, 0x08, "RxMulti"),
	MIB_DESC(1, 0x0c, "RxFcsErr"),
	MIB_DESC(1, 0x10, "RxAlignErr"),
	MIB_DESC(1, 0x14, "RxRunt"),
	MIB_DESC(1, 0x18, "RxFragment"),
	MIB_DESC(1, 0x1c, "Rx64Byte"),
	MIB_DESC(1, 0x20, "Rx128Byte"),
	MIB_DESC(1, 0x24, "Rx256Byte"),
	MIB_DESC(1, 0x28, "Rx512Byte"),
	MIB_DESC(1, 0x2c, "Rx1024Byte"),
	MIB_DESC(1, 0x30, "Rx1518Byte"),
	MIB_DESC(1, 0x34, "RxMaxByte"),
	MIB_DESC(1, 0x38, "RxTooLong"),
	MIB_DESC(2, 0x3c, "RxGoodByte"),
	MIB_DESC(2, 0x44, "RxBadByte"),
	MIB_DESC(1, 0x4c, "RxOverFlow"),
	MIB_DESC(1, 0x50, "Filtered"),
	MIB_DESC(1, 0x54, "TxBroad"),
	MIB_DESC(1, 0x58, "TxPause"),
	MIB_DESC(1, 0x5c, "TxMulti"),
	MIB_DESC(1, 0x60, "TxUnderRun"),
	MIB_DESC(1, 0x64, "Tx64Byte"),
	MIB_DESC(1, 0x68, "Tx128Byte"),
	MIB_DESC(1, 0x6c, "Tx256Byte"),
	MIB_DESC(1, 0x70, "Tx512Byte"),
	MIB_DESC(1, 0x74, "Tx1024Byte"),
	MIB_DESC(1, 0x78, "Tx1518Byte"),
	MIB_DESC(1, 0x7c, "TxMaxByte"),
	MIB_DESC(1, 0x80, "TxOverSize"),
	MIB_DESC(2, 0x84, "TxByte"),
	MIB_DESC(1, 0x8c, "TxCollision"),
	MIB_DESC(1, 0x90, "TxAbortCol"),
	MIB_DESC(1, 0x94, "TxMultiCol"),
	MIB_DESC(1, 0x98, "TxSingleCol"),
	MIB_DESC(1, 0x9c, "TxExcDefer"),
	MIB_DESC(1, 0xa0, "TxDefer"),
	MIB_DESC(1, 0xa4, "TxLateCol"),
};

/* The 32bit switch registers are accessed indirectly. To achieve this we need
 * to set the page of the register. Track the last page that was set to reduce
 * mdio writes
 */
static u16 qca8k_current_page = 0xffff;

static void
qca8k_split_addr(u32 regaddr, u16 *r1, u16 *r2, u16 *page)
{
	regaddr >>= 1;
	*r1 = regaddr & 0x1e;

	regaddr >>= 5;
	*r2 = regaddr & 0x7;

	regaddr >>= 3;
	*page = regaddr & 0x3ff;
}

static int
qca8k_mii_read32(struct mii_bus *bus, int phy_id, u32 regnum, u32 *val)
{
	int ret;

	ret = bus->read(bus, phy_id, regnum);
	if (ret >= 0) {
		*val = ret;
		ret = bus->read(bus, phy_id, regnum + 1);
		*val |= ret << 16;
	}

	if (ret < 0) {
		dev_err_ratelimited(&bus->dev,
				    "failed to read qca8k 32bit register\n");
		*val = 0;
		return ret;
	}

	return 0;
}

static void
qca8k_mii_write32(struct mii_bus *bus, int phy_id, u32 regnum, u32 val)
{
	u16 lo, hi;
	int ret;

	lo = val & 0xffff;
	hi = (u16)(val >> 16);

	ret = bus->write(bus, phy_id, regnum, lo);
	if (ret >= 0)
		ret = bus->write(bus, phy_id, regnum + 1, hi);
	if (ret < 0)
		dev_err_ratelimited(&bus->dev,
				    "failed to write qca8k 32bit register\n");
}

static int
qca8k_set_page(struct mii_bus *bus, u16 page)
{
	int ret;

	if (page == qca8k_current_page)
		return 0;

	ret = bus->write(bus, 0x18, 0, page);
	if (ret < 0) {
		dev_err_ratelimited(&bus->dev,
				    "failed to set qca8k page\n");
		return ret;
	}

	qca8k_current_page = page;
	usleep_range(1000, 2000);
	return 0;
}

static int
qca8k_read(struct qca8k_priv *priv, u32 reg, u32 *val)
{
	struct mii_bus *bus = priv->bus;
	u16 r1, r2, page;
	int ret;

	qca8k_split_addr(reg, &r1, &r2, &page);

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	ret = qca8k_set_page(bus, page);
	if (ret < 0)
		goto exit;

	ret = qca8k_mii_read32(bus, 0x10 | r2, r1, val);

exit:
	mutex_unlock(&bus->mdio_lock);
	return ret;
}

static int
qca8k_write(struct qca8k_priv *priv, u32 reg, u32 val)
{
	struct mii_bus *bus = priv->bus;
	u16 r1, r2, page;
	int ret;

	qca8k_split_addr(reg, &r1, &r2, &page);

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	ret = qca8k_set_page(bus, page);
	if (ret < 0)
		goto exit;

	qca8k_mii_write32(bus, 0x10 | r2, r1, val);

exit:
	mutex_unlock(&bus->mdio_lock);
	return ret;
}

static int
qca8k_rmw(struct qca8k_priv *priv, u32 reg, u32 mask, u32 write_val)
{
	struct mii_bus *bus = priv->bus;
	u16 r1, r2, page;
	u32 val;
	int ret;

	qca8k_split_addr(reg, &r1, &r2, &page);

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	ret = qca8k_set_page(bus, page);
	if (ret < 0)
		goto exit;

	ret = qca8k_mii_read32(bus, 0x10 | r2, r1, &val);
	if (ret < 0)
		goto exit;

	val &= ~mask;
	val |= write_val;
	qca8k_mii_write32(bus, 0x10 | r2, r1, val);

exit:
	mutex_unlock(&bus->mdio_lock);

	return ret;
}

static int
qca8k_reg_set(struct qca8k_priv *priv, u32 reg, u32 val)
{
	return qca8k_rmw(priv, reg, 0, val);
}

static int
qca8k_reg_clear(struct qca8k_priv *priv, u32 reg, u32 val)
{
	return qca8k_rmw(priv, reg, val, 0);
}

static int
qca8k_regmap_read(void *ctx, uint32_t reg, uint32_t *val)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ctx;

	return qca8k_read(priv, reg, val);
}

static int
qca8k_regmap_write(void *ctx, uint32_t reg, uint32_t val)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ctx;

	return qca8k_write(priv, reg, val);
}

static const struct regmap_range qca8k_readable_ranges[] = {
	regmap_reg_range(0x0000, 0x00e4), /* Global control */
	regmap_reg_range(0x0100, 0x0168), /* EEE control */
	regmap_reg_range(0x0200, 0x0270), /* Parser control */
	regmap_reg_range(0x0400, 0x0454), /* ACL */
	regmap_reg_range(0x0600, 0x0718), /* Lookup */
	regmap_reg_range(0x0800, 0x0b70), /* QM */
	regmap_reg_range(0x0c00, 0x0c80), /* PKT */
	regmap_reg_range(0x0e00, 0x0e98), /* L3 */
	regmap_reg_range(0x1000, 0x10ac), /* MIB - Port0 */
	regmap_reg_range(0x1100, 0x11ac), /* MIB - Port1 */
	regmap_reg_range(0x1200, 0x12ac), /* MIB - Port2 */
	regmap_reg_range(0x1300, 0x13ac), /* MIB - Port3 */
	regmap_reg_range(0x1400, 0x14ac), /* MIB - Port4 */
	regmap_reg_range(0x1500, 0x15ac), /* MIB - Port5 */
	regmap_reg_range(0x1600, 0x16ac), /* MIB - Port6 */

};

static const struct regmap_access_table qca8k_readable_table = {
	.yes_ranges = qca8k_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(qca8k_readable_ranges),
};

static struct regmap_config qca8k_regmap_config = {
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x16ac, /* end MIB - Port6 range */
	.reg_read = qca8k_regmap_read,
	.reg_write = qca8k_regmap_write,
	.rd_table = &qca8k_readable_table,
};

static int
qca8k_busy_wait(struct qca8k_priv *priv, u32 reg, u32 mask)
{
	int ret, ret1;
	u32 val;

	ret = read_poll_timeout(qca8k_read, ret1, !(val & mask),
				0, QCA8K_BUSY_WAIT_TIMEOUT * USEC_PER_MSEC, false,
				priv, reg, &val);

	/* Check if qca8k_read has failed for a different reason
	 * before returning -ETIMEDOUT
	 */
	if (ret < 0 && ret1 < 0)
		return ret1;

	return ret;
}

static int
qca8k_fdb_read(struct qca8k_priv *priv, struct qca8k_fdb *fdb)
{
	u32 reg[4], val;
	int i, ret;

	/* load the ARL table into an array */
	for (i = 0; i < 4; i++) {
		ret = qca8k_read(priv, QCA8K_REG_ATU_DATA0 + (i * 4), &val);
		if (ret < 0)
			return ret;

		reg[i] = val;
	}

	/* vid - 83:72 */
	fdb->vid = (reg[2] >> QCA8K_ATU_VID_S) & QCA8K_ATU_VID_M;
	/* aging - 67:64 */
	fdb->aging = reg[2] & QCA8K_ATU_STATUS_M;
	/* portmask - 54:48 */
	fdb->port_mask = (reg[1] >> QCA8K_ATU_PORT_S) & QCA8K_ATU_PORT_M;
	/* mac - 47:0 */
	fdb->mac[0] = (reg[1] >> QCA8K_ATU_ADDR0_S) & 0xff;
	fdb->mac[1] = reg[1] & 0xff;
	fdb->mac[2] = (reg[0] >> QCA8K_ATU_ADDR2_S) & 0xff;
	fdb->mac[3] = (reg[0] >> QCA8K_ATU_ADDR3_S) & 0xff;
	fdb->mac[4] = (reg[0] >> QCA8K_ATU_ADDR4_S) & 0xff;
	fdb->mac[5] = reg[0] & 0xff;

	return 0;
}

static void
qca8k_fdb_write(struct qca8k_priv *priv, u16 vid, u8 port_mask, const u8 *mac,
		u8 aging)
{
	u32 reg[3] = { 0 };
	int i;

	/* vid - 83:72 */
	reg[2] = (vid & QCA8K_ATU_VID_M) << QCA8K_ATU_VID_S;
	/* aging - 67:64 */
	reg[2] |= aging & QCA8K_ATU_STATUS_M;
	/* portmask - 54:48 */
	reg[1] = (port_mask & QCA8K_ATU_PORT_M) << QCA8K_ATU_PORT_S;
	/* mac - 47:0 */
	reg[1] |= mac[0] << QCA8K_ATU_ADDR0_S;
	reg[1] |= mac[1];
	reg[0] |= mac[2] << QCA8K_ATU_ADDR2_S;
	reg[0] |= mac[3] << QCA8K_ATU_ADDR3_S;
	reg[0] |= mac[4] << QCA8K_ATU_ADDR4_S;
	reg[0] |= mac[5];

	/* load the array into the ARL table */
	for (i = 0; i < 3; i++)
		qca8k_write(priv, QCA8K_REG_ATU_DATA0 + (i * 4), reg[i]);
}

static int
qca8k_fdb_access(struct qca8k_priv *priv, enum qca8k_fdb_cmd cmd, int port)
{
	u32 reg;
	int ret;

	/* Set the command and FDB index */
	reg = QCA8K_ATU_FUNC_BUSY;
	reg |= cmd;
	if (port >= 0) {
		reg |= QCA8K_ATU_FUNC_PORT_EN;
		reg |= (port & QCA8K_ATU_FUNC_PORT_M) << QCA8K_ATU_FUNC_PORT_S;
	}

	/* Write the function register triggering the table access */
	ret = qca8k_write(priv, QCA8K_REG_ATU_FUNC, reg);
	if (ret)
		return ret;

	/* wait for completion */
	ret = qca8k_busy_wait(priv, QCA8K_REG_ATU_FUNC, QCA8K_ATU_FUNC_BUSY);
	if (ret)
		return ret;

	/* Check for table full violation when adding an entry */
	if (cmd == QCA8K_FDB_LOAD) {
		ret = qca8k_read(priv, QCA8K_REG_ATU_FUNC, &reg);
		if (ret < 0)
			return ret;
		if (reg & QCA8K_ATU_FUNC_FULL)
			return -1;
	}

	return 0;
}

static int
qca8k_fdb_next(struct qca8k_priv *priv, struct qca8k_fdb *fdb, int port)
{
	int ret;

	qca8k_fdb_write(priv, fdb->vid, fdb->port_mask, fdb->mac, fdb->aging);
	ret = qca8k_fdb_access(priv, QCA8K_FDB_NEXT, port);
	if (ret < 0)
		return ret;

	return qca8k_fdb_read(priv, fdb);
}

static int
qca8k_fdb_add(struct qca8k_priv *priv, const u8 *mac, u16 port_mask,
	      u16 vid, u8 aging)
{
	int ret;

	mutex_lock(&priv->reg_mutex);
	qca8k_fdb_write(priv, vid, port_mask, mac, aging);
	ret = qca8k_fdb_access(priv, QCA8K_FDB_LOAD, -1);
	mutex_unlock(&priv->reg_mutex);

	return ret;
}

static int
qca8k_fdb_del(struct qca8k_priv *priv, const u8 *mac, u16 port_mask, u16 vid)
{
	int ret;

	mutex_lock(&priv->reg_mutex);
	qca8k_fdb_write(priv, vid, port_mask, mac, 0);
	ret = qca8k_fdb_access(priv, QCA8K_FDB_PURGE, -1);
	mutex_unlock(&priv->reg_mutex);

	return ret;
}

static void
qca8k_fdb_flush(struct qca8k_priv *priv)
{
	mutex_lock(&priv->reg_mutex);
	qca8k_fdb_access(priv, QCA8K_FDB_FLUSH, -1);
	mutex_unlock(&priv->reg_mutex);
}

static int
qca8k_vlan_access(struct qca8k_priv *priv, enum qca8k_vlan_cmd cmd, u16 vid)
{
	u32 reg;
	int ret;

	/* Set the command and VLAN index */
	reg = QCA8K_VTU_FUNC1_BUSY;
	reg |= cmd;
	reg |= vid << QCA8K_VTU_FUNC1_VID_S;

	/* Write the function register triggering the table access */
	ret = qca8k_write(priv, QCA8K_REG_VTU_FUNC1, reg);
	if (ret)
		return ret;

	/* wait for completion */
	ret = qca8k_busy_wait(priv, QCA8K_REG_VTU_FUNC1, QCA8K_VTU_FUNC1_BUSY);
	if (ret)
		return ret;

	/* Check for table full violation when adding an entry */
	if (cmd == QCA8K_VLAN_LOAD) {
		ret = qca8k_read(priv, QCA8K_REG_VTU_FUNC1, &reg);
		if (ret < 0)
			return ret;
		if (reg & QCA8K_VTU_FUNC1_FULL)
			return -ENOMEM;
	}

	return 0;
}

static int
qca8k_vlan_add(struct qca8k_priv *priv, u8 port, u16 vid, bool untagged)
{
	u32 reg;
	int ret;

	/*
	   We do the right thing with VLAN 0 and treat it as untagged while
	   preserving the tag on egress.
	 */
	if (vid == 0)
		return 0;

	mutex_lock(&priv->reg_mutex);
	ret = qca8k_vlan_access(priv, QCA8K_VLAN_READ, vid);
	if (ret < 0)
		goto out;

	ret = qca8k_read(priv, QCA8K_REG_VTU_FUNC0, &reg);
	if (ret < 0)
		goto out;
	reg |= QCA8K_VTU_FUNC0_VALID | QCA8K_VTU_FUNC0_IVL_EN;
	reg &= ~(QCA8K_VTU_FUNC0_EG_MODE_MASK << QCA8K_VTU_FUNC0_EG_MODE_S(port));
	if (untagged)
		reg |= QCA8K_VTU_FUNC0_EG_MODE_UNTAG <<
				QCA8K_VTU_FUNC0_EG_MODE_S(port);
	else
		reg |= QCA8K_VTU_FUNC0_EG_MODE_TAG <<
				QCA8K_VTU_FUNC0_EG_MODE_S(port);

	ret = qca8k_write(priv, QCA8K_REG_VTU_FUNC0, reg);
	if (ret)
		goto out;
	ret = qca8k_vlan_access(priv, QCA8K_VLAN_LOAD, vid);

out:
	mutex_unlock(&priv->reg_mutex);

	return ret;
}

static int
qca8k_vlan_del(struct qca8k_priv *priv, u8 port, u16 vid)
{
	u32 reg, mask;
	int ret, i;
	bool del;

	mutex_lock(&priv->reg_mutex);
	ret = qca8k_vlan_access(priv, QCA8K_VLAN_READ, vid);
	if (ret < 0)
		goto out;

	ret = qca8k_read(priv, QCA8K_REG_VTU_FUNC0, &reg);
	if (ret < 0)
		goto out;
	reg &= ~(3 << QCA8K_VTU_FUNC0_EG_MODE_S(port));
	reg |= QCA8K_VTU_FUNC0_EG_MODE_NOT <<
			QCA8K_VTU_FUNC0_EG_MODE_S(port);

	/* Check if we're the last member to be removed */
	del = true;
	for (i = 0; i < QCA8K_NUM_PORTS; i++) {
		mask = QCA8K_VTU_FUNC0_EG_MODE_NOT;
		mask <<= QCA8K_VTU_FUNC0_EG_MODE_S(i);

		if ((reg & mask) != mask) {
			del = false;
			break;
		}
	}

	if (del) {
		ret = qca8k_vlan_access(priv, QCA8K_VLAN_PURGE, vid);
	} else {
		ret = qca8k_write(priv, QCA8K_REG_VTU_FUNC0, reg);
		if (ret)
			goto out;
		ret = qca8k_vlan_access(priv, QCA8K_VLAN_LOAD, vid);
	}

out:
	mutex_unlock(&priv->reg_mutex);

	return ret;
}

static int
qca8k_mib_init(struct qca8k_priv *priv)
{
	int ret;

	mutex_lock(&priv->reg_mutex);
	ret = qca8k_reg_set(priv, QCA8K_REG_MIB, QCA8K_MIB_FLUSH | QCA8K_MIB_BUSY);
	if (ret)
		goto exit;

	ret = qca8k_busy_wait(priv, QCA8K_REG_MIB, QCA8K_MIB_BUSY);
	if (ret)
		goto exit;

	ret = qca8k_reg_set(priv, QCA8K_REG_MIB, QCA8K_MIB_CPU_KEEP);
	if (ret)
		goto exit;

	ret = qca8k_write(priv, QCA8K_REG_MODULE_EN, QCA8K_MODULE_EN_MIB);

exit:
	mutex_unlock(&priv->reg_mutex);
	return ret;
}

static void
qca8k_port_set_status(struct qca8k_priv *priv, int port, int enable)
{
	u32 mask = QCA8K_PORT_STATUS_TXMAC | QCA8K_PORT_STATUS_RXMAC;

	/* Port 0 and 6 have no internal PHY */
	if (port > 0 && port < 6)
		mask |= QCA8K_PORT_STATUS_LINK_AUTO;

	if (enable)
		qca8k_reg_set(priv, QCA8K_REG_PORT_STATUS(port), mask);
	else
		qca8k_reg_clear(priv, QCA8K_REG_PORT_STATUS(port), mask);
}

static u32
qca8k_port_to_phy(int port)
{
	/* From Andrew Lunn:
	 * Port 0 has no internal phy.
	 * Port 1 has an internal PHY at MDIO address 0.
	 * Port 2 has an internal PHY at MDIO address 1.
	 * ...
	 * Port 5 has an internal PHY at MDIO address 4.
	 * Port 6 has no internal PHY.
	 */

	return port - 1;
}

static int
qca8k_mdio_busy_wait(struct mii_bus *bus, u32 reg, u32 mask)
{
	u16 r1, r2, page;
	u32 val;
	int ret, ret1;

	qca8k_split_addr(reg, &r1, &r2, &page);

	ret = read_poll_timeout(qca8k_mii_read32, ret1, !(val & mask), 0,
				QCA8K_BUSY_WAIT_TIMEOUT * USEC_PER_MSEC, false,
				bus, 0x10 | r2, r1, &val);

	/* Check if qca8k_read has failed for a different reason
	 * before returnting -ETIMEDOUT
	 */
	if (ret < 0 && ret1 < 0)
		return ret1;

	return ret;
}

static int
qca8k_mdio_write(struct mii_bus *salve_bus, int phy, int regnum, u16 data)
{
	struct qca8k_priv *priv = salve_bus->priv;
	struct mii_bus *bus = priv->bus;
	u16 r1, r2, page;
	u32 val;
	int ret;

	if (regnum >= QCA8K_MDIO_MASTER_MAX_REG)
		return -EINVAL;

	val = QCA8K_MDIO_MASTER_BUSY | QCA8K_MDIO_MASTER_EN |
	      QCA8K_MDIO_MASTER_WRITE | QCA8K_MDIO_MASTER_PHY_ADDR(phy) |
	      QCA8K_MDIO_MASTER_REG_ADDR(regnum) |
	      QCA8K_MDIO_MASTER_DATA(data);

	qca8k_split_addr(QCA8K_MDIO_MASTER_CTRL, &r1, &r2, &page);

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	ret = qca8k_set_page(bus, page);
	if (ret)
		goto exit;

	qca8k_mii_write32(bus, 0x10 | r2, r1, val);

	ret = qca8k_mdio_busy_wait(bus, QCA8K_MDIO_MASTER_CTRL,
				   QCA8K_MDIO_MASTER_BUSY);

exit:
	/* even if the busy_wait timeouts try to clear the MASTER_EN */
	qca8k_mii_write32(bus, 0x10 | r2, r1, 0);

	mutex_unlock(&bus->mdio_lock);

	return ret;
}

static int
qca8k_mdio_read(struct mii_bus *salve_bus, int phy, int regnum)
{
	struct qca8k_priv *priv = salve_bus->priv;
	struct mii_bus *bus = priv->bus;
	u16 r1, r2, page;
	u32 val;
	int ret;

	if (regnum >= QCA8K_MDIO_MASTER_MAX_REG)
		return -EINVAL;

	val = QCA8K_MDIO_MASTER_BUSY | QCA8K_MDIO_MASTER_EN |
	      QCA8K_MDIO_MASTER_READ | QCA8K_MDIO_MASTER_PHY_ADDR(phy) |
	      QCA8K_MDIO_MASTER_REG_ADDR(regnum);

	qca8k_split_addr(QCA8K_MDIO_MASTER_CTRL, &r1, &r2, &page);

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	ret = qca8k_set_page(bus, page);
	if (ret)
		goto exit;

	qca8k_mii_write32(bus, 0x10 | r2, r1, val);

	ret = qca8k_mdio_busy_wait(bus, QCA8K_MDIO_MASTER_CTRL,
				   QCA8K_MDIO_MASTER_BUSY);
	if (ret)
		goto exit;

	ret = qca8k_mii_read32(bus, 0x10 | r2, r1, &val);

exit:
	/* even if the busy_wait timeouts try to clear the MASTER_EN */
	qca8k_mii_write32(bus, 0x10 | r2, r1, 0);

	mutex_unlock(&bus->mdio_lock);

	if (ret >= 0)
		ret = val & QCA8K_MDIO_MASTER_DATA_MASK;

	return ret;
}

static int
qca8k_phy_write(struct dsa_switch *ds, int port, int regnum, u16 data)
{
	struct qca8k_priv *priv = ds->priv;

	/* Check if the legacy mapping should be used and the
	 * port is not correctly mapped to the right PHY in the
	 * devicetree
	 */
	if (priv->legacy_phy_port_mapping)
		port = qca8k_port_to_phy(port) % PHY_MAX_ADDR;

	return qca8k_mdio_write(priv->bus, port, regnum, data);
}

static int
qca8k_phy_read(struct dsa_switch *ds, int port, int regnum)
{
	struct qca8k_priv *priv = ds->priv;
	int ret;

	/* Check if the legacy mapping should be used and the
	 * port is not correctly mapped to the right PHY in the
	 * devicetree
	 */
	if (priv->legacy_phy_port_mapping)
		port = qca8k_port_to_phy(port) % PHY_MAX_ADDR;

	ret = qca8k_mdio_read(priv->bus, port, regnum);

	if (ret < 0)
		return 0xffff;

	return ret;
}

static int
qca8k_mdio_register(struct qca8k_priv *priv, struct device_node *mdio)
{
	struct dsa_switch *ds = priv->ds;
	struct mii_bus *bus;

	bus = devm_mdiobus_alloc(ds->dev);

	if (!bus)
		return -ENOMEM;

	bus->priv = (void *)priv;
	bus->name = "qca8k slave mii";
	bus->read = qca8k_mdio_read;
	bus->write = qca8k_mdio_write;
	snprintf(bus->id, MII_BUS_ID_SIZE, "qca8k-%d",
		 ds->index);

	bus->parent = ds->dev;
	bus->phy_mask = ~ds->phys_mii_mask;

	ds->slave_mii_bus = bus;

	return devm_of_mdiobus_register(priv->dev, bus, mdio);
}

static int
qca8k_setup_mdio_bus(struct qca8k_priv *priv)
{
	u32 internal_mdio_mask = 0, external_mdio_mask = 0, reg;
	struct device_node *ports, *port, *mdio;
	phy_interface_t mode;
	int err;

	ports = of_get_child_by_name(priv->dev->of_node, "ports");
	if (!ports)
		ports = of_get_child_by_name(priv->dev->of_node, "ethernet-ports");

	if (!ports)
		return -EINVAL;

	for_each_available_child_of_node(ports, port) {
		err = of_property_read_u32(port, "reg", &reg);
		if (err) {
			of_node_put(port);
			of_node_put(ports);
			return err;
		}

		if (!dsa_is_user_port(priv->ds, reg))
			continue;

		of_get_phy_mode(port, &mode);

		if (of_property_read_bool(port, "phy-handle") &&
		    mode != PHY_INTERFACE_MODE_INTERNAL)
			external_mdio_mask |= BIT(reg);
		else
			internal_mdio_mask |= BIT(reg);
	}

	of_node_put(ports);
	if (!external_mdio_mask && !internal_mdio_mask) {
		dev_err(priv->dev, "no PHYs are defined.\n");
		return -EINVAL;
	}

	/* The QCA8K_MDIO_MASTER_EN Bit, which grants access to PHYs through
	 * the MDIO_MASTER register also _disconnects_ the external MDC
	 * passthrough to the internal PHYs. It's not possible to use both
	 * configurations at the same time!
	 *
	 * Because this came up during the review process:
	 * If the external mdio-bus driver is capable magically disabling
	 * the QCA8K_MDIO_MASTER_EN and mutex/spin-locking out the qca8k's
	 * accessors for the time being, it would be possible to pull this
	 * off.
	 */
	if (!!external_mdio_mask && !!internal_mdio_mask) {
		dev_err(priv->dev, "either internal or external mdio bus configuration is supported.\n");
		return -EINVAL;
	}

	if (external_mdio_mask) {
		/* Make sure to disable the internal mdio bus in cases
		 * a dt-overlay and driver reload changed the configuration
		 */

		return qca8k_reg_clear(priv, QCA8K_MDIO_MASTER_CTRL,
				       QCA8K_MDIO_MASTER_EN);
	}

	/* Check if the devicetree declare the port:phy mapping */
	mdio = of_get_child_by_name(priv->dev->of_node, "mdio");
	if (of_device_is_available(mdio)) {
		err = qca8k_mdio_register(priv, mdio);
		if (err)
			of_node_put(mdio);

		return err;
	}

	/* If a mapping can't be found the legacy mapping is used,
	 * using the qca8k_port_to_phy function
	 */
	priv->legacy_phy_port_mapping = true;
	priv->ops.phy_read = qca8k_phy_read;
	priv->ops.phy_write = qca8k_phy_write;

	return 0;
}

static int
qca8k_setup_of_rgmii_delay(struct qca8k_priv *priv)
{
	struct device_node *port_dn;
	phy_interface_t mode;
	struct dsa_port *dp;
	u32 val;

	/* CPU port is already checked */
	dp = dsa_to_port(priv->ds, 0);

	port_dn = dp->dn;

	/* Check if port 0 is set to the correct type */
	of_get_phy_mode(port_dn, &mode);
	if (mode != PHY_INTERFACE_MODE_RGMII_ID &&
	    mode != PHY_INTERFACE_MODE_RGMII_RXID &&
	    mode != PHY_INTERFACE_MODE_RGMII_TXID) {
		return 0;
	}

	switch (mode) {
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		if (of_property_read_u32(port_dn, "rx-internal-delay-ps", &val))
			val = 2;
		else
			/* Switch regs accept value in ns, convert ps to ns */
			val = val / 1000;

		if (val > QCA8K_MAX_DELAY) {
			dev_err(priv->dev, "rgmii rx delay is limited to a max value of 3ns, setting to the max value");
			val = 3;
		}

		priv->rgmii_rx_delay = val;
		/* Stop here if we need to check only for rx delay */
		if (mode != PHY_INTERFACE_MODE_RGMII_ID)
			break;

		fallthrough;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		if (of_property_read_u32(port_dn, "tx-internal-delay-ps", &val))
			val = 1;
		else
			/* Switch regs accept value in ns, convert ps to ns */
			val = val / 1000;

		if (val > QCA8K_MAX_DELAY) {
			dev_err(priv->dev, "rgmii tx delay is limited to a max value of 3ns, setting to the max value");
			val = 3;
		}

		priv->rgmii_tx_delay = val;
		break;
	default:
		return 0;
	}

	return 0;
}

static int
qca8k_setup(struct dsa_switch *ds)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	int ret, i;
	u32 mask;

	/* Make sure that port 0 is the cpu port */
	if (!dsa_is_cpu_port(ds, 0)) {
		dev_err(priv->dev, "port 0 is not the CPU port");
		return -EINVAL;
	}

	mutex_init(&priv->reg_mutex);

	/* Start by setting up the register mapping */
	priv->regmap = devm_regmap_init(ds->dev, NULL, priv,
					&qca8k_regmap_config);
	if (IS_ERR(priv->regmap))
		dev_warn(priv->dev, "regmap initialization failed");

	ret = qca8k_setup_mdio_bus(priv);
	if (ret)
		return ret;

	ret = qca8k_setup_of_rgmii_delay(priv);
	if (ret)
		return ret;

	/* Enable CPU Port */
	ret = qca8k_reg_set(priv, QCA8K_REG_GLOBAL_FW_CTRL0,
			    QCA8K_GLOBAL_FW_CTRL0_CPU_PORT_EN);
	if (ret) {
		dev_err(priv->dev, "failed enabling CPU port");
		return ret;
	}

	/* Enable MIB counters */
	ret = qca8k_mib_init(priv);
	if (ret)
		dev_warn(priv->dev, "mib init failed");

	/* Enable QCA header mode on the cpu port */
	ret = qca8k_write(priv, QCA8K_REG_PORT_HDR_CTRL(QCA8K_CPU_PORT),
			  QCA8K_PORT_HDR_CTRL_ALL << QCA8K_PORT_HDR_CTRL_TX_S |
			  QCA8K_PORT_HDR_CTRL_ALL << QCA8K_PORT_HDR_CTRL_RX_S);
	if (ret) {
		dev_err(priv->dev, "failed enabling QCA header mode");
		return ret;
	}

	/* Disable forwarding by default on all ports */
	for (i = 0; i < QCA8K_NUM_PORTS; i++) {
		ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(i),
				QCA8K_PORT_LOOKUP_MEMBER, 0);
		if (ret)
			return ret;
	}

	/* Disable MAC by default on all ports */
	for (i = 1; i < QCA8K_NUM_PORTS; i++)
		qca8k_port_set_status(priv, i, 0);

	/* Forward all unknown frames to CPU port for Linux processing */
	ret = qca8k_write(priv, QCA8K_REG_GLOBAL_FW_CTRL1,
			  BIT(0) << QCA8K_GLOBAL_FW_CTRL1_IGMP_DP_S |
			  BIT(0) << QCA8K_GLOBAL_FW_CTRL1_BC_DP_S |
			  BIT(0) << QCA8K_GLOBAL_FW_CTRL1_MC_DP_S |
			  BIT(0) << QCA8K_GLOBAL_FW_CTRL1_UC_DP_S);
	if (ret)
		return ret;

	/* Setup connection between CPU port & user ports */
	for (i = 0; i < QCA8K_NUM_PORTS; i++) {
		/* CPU port gets connected to all user ports of the switch */
		if (dsa_is_cpu_port(ds, i)) {
			ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(QCA8K_CPU_PORT),
					QCA8K_PORT_LOOKUP_MEMBER, dsa_user_ports(ds));
			if (ret)
				return ret;
		}

		/* Individual user ports get connected to CPU port only */
		if (dsa_is_user_port(ds, i)) {
			int shift = 16 * (i % 2);

			ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(i),
					QCA8K_PORT_LOOKUP_MEMBER,
					BIT(QCA8K_CPU_PORT));
			if (ret)
				return ret;

			/* Enable ARP Auto-learning by default */
			ret = qca8k_reg_set(priv, QCA8K_PORT_LOOKUP_CTRL(i),
					    QCA8K_PORT_LOOKUP_LEARN);
			if (ret)
				return ret;

			/* For port based vlans to work we need to set the
			 * default egress vid
			 */
			ret = qca8k_rmw(priv, QCA8K_EGRESS_VLAN(i),
					0xfff << shift,
					QCA8K_PORT_VID_DEF << shift);
			if (ret)
				return ret;

			ret = qca8k_write(priv, QCA8K_REG_PORT_VLAN_CTRL0(i),
					  QCA8K_PORT_VLAN_CVID(QCA8K_PORT_VID_DEF) |
					  QCA8K_PORT_VLAN_SVID(QCA8K_PORT_VID_DEF));
			if (ret)
				return ret;
		}
	}

	/* The port 5 of the qca8337 have some problem in flood condition. The
	 * original legacy driver had some specific buffer and priority settings
	 * for the different port suggested by the QCA switch team. Add this
	 * missing settings to improve switch stability under load condition.
	 * This problem is limited to qca8337 and other qca8k switch are not affected.
	 */
	if (priv->switch_id == QCA8K_ID_QCA8337) {
		for (i = 0; i < QCA8K_NUM_PORTS; i++) {
			switch (i) {
			/* The 2 CPU port and port 5 requires some different
			 * priority than any other ports.
			 */
			case 0:
			case 5:
			case 6:
				mask = QCA8K_PORT_HOL_CTRL0_EG_PRI0(0x3) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI1(0x4) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI2(0x4) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI3(0x4) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI4(0x6) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI5(0x8) |
					QCA8K_PORT_HOL_CTRL0_EG_PORT(0x1e);
				break;
			default:
				mask = QCA8K_PORT_HOL_CTRL0_EG_PRI0(0x3) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI1(0x4) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI2(0x6) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI3(0x8) |
					QCA8K_PORT_HOL_CTRL0_EG_PORT(0x19);
			}
			qca8k_write(priv, QCA8K_REG_PORT_HOL_CTRL0(i), mask);

			mask = QCA8K_PORT_HOL_CTRL1_ING(0x6) |
			QCA8K_PORT_HOL_CTRL1_EG_PRI_BUF_EN |
			QCA8K_PORT_HOL_CTRL1_EG_PORT_BUF_EN |
			QCA8K_PORT_HOL_CTRL1_WRED_EN;
			qca8k_rmw(priv, QCA8K_REG_PORT_HOL_CTRL1(i),
				  QCA8K_PORT_HOL_CTRL1_ING_BUF |
				  QCA8K_PORT_HOL_CTRL1_EG_PRI_BUF_EN |
				  QCA8K_PORT_HOL_CTRL1_EG_PORT_BUF_EN |
				  QCA8K_PORT_HOL_CTRL1_WRED_EN,
				  mask);
		}
	}

	/* Special GLOBAL_FC_THRESH value are needed for ar8327 switch */
	if (priv->switch_id == QCA8K_ID_QCA8327) {
		mask = QCA8K_GLOBAL_FC_GOL_XON_THRES(288) |
		       QCA8K_GLOBAL_FC_GOL_XOFF_THRES(496);
		qca8k_rmw(priv, QCA8K_REG_GLOBAL_FC_THRESH,
			  QCA8K_GLOBAL_FC_GOL_XON_THRES_S |
			  QCA8K_GLOBAL_FC_GOL_XOFF_THRES_S,
			  mask);
	}

	/* Setup our port MTUs to match power on defaults */
	for (i = 0; i < QCA8K_NUM_PORTS; i++)
		priv->port_mtu[i] = ETH_FRAME_LEN + ETH_FCS_LEN;
	ret = qca8k_write(priv, QCA8K_MAX_FRAME_SIZE, ETH_FRAME_LEN + ETH_FCS_LEN);
	if (ret)
		dev_warn(priv->dev, "failed setting MTU settings");

	/* Flush the FDB table */
	qca8k_fdb_flush(priv);

	/* We don't have interrupts for link changes, so we need to poll */
	ds->pcs_poll = true;

	return 0;
}

static void
qca8k_phylink_mac_config(struct dsa_switch *ds, int port, unsigned int mode,
			 const struct phylink_link_state *state)
{
	struct qca8k_priv *priv = ds->priv;
	u32 reg, val;
	int ret;

	switch (port) {
	case 0: /* 1st CPU port */
		if (state->interface != PHY_INTERFACE_MODE_RGMII &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_ID &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_TXID &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_RXID &&
		    state->interface != PHY_INTERFACE_MODE_SGMII)
			return;

		reg = QCA8K_REG_PORT0_PAD_CTRL;
		break;
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
		/* Internal PHY, nothing to do */
		return;
	case 6: /* 2nd CPU port / external PHY */
		if (state->interface != PHY_INTERFACE_MODE_RGMII &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_ID &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_TXID &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_RXID &&
		    state->interface != PHY_INTERFACE_MODE_SGMII &&
		    state->interface != PHY_INTERFACE_MODE_1000BASEX)
			return;

		reg = QCA8K_REG_PORT6_PAD_CTRL;
		break;
	default:
		dev_err(ds->dev, "%s: unsupported port: %i\n", __func__, port);
		return;
	}

	if (port != 6 && phylink_autoneg_inband(mode)) {
		dev_err(ds->dev, "%s: in-band negotiation unsupported\n",
			__func__);
		return;
	}

	switch (state->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		/* RGMII mode means no delay so don't enable the delay */
		qca8k_write(priv, reg, QCA8K_PORT_PAD_RGMII_EN);
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		/* RGMII_ID needs internal delay. This is enabled through
		 * PORT5_PAD_CTRL for all ports, rather than individual port
		 * registers
		 */
		qca8k_write(priv, reg,
			    QCA8K_PORT_PAD_RGMII_EN |
			    QCA8K_PORT_PAD_RGMII_TX_DELAY(priv->rgmii_tx_delay) |
			    QCA8K_PORT_PAD_RGMII_RX_DELAY(priv->rgmii_rx_delay) |
			    QCA8K_PORT_PAD_RGMII_TX_DELAY_EN |
			    QCA8K_PORT_PAD_RGMII_RX_DELAY_EN);
		/* QCA8337 requires to set rgmii rx delay */
		if (priv->switch_id == QCA8K_ID_QCA8337)
			qca8k_write(priv, QCA8K_REG_PORT5_PAD_CTRL,
				    QCA8K_PORT_PAD_RGMII_RX_DELAY_EN);
		break;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		/* Enable SGMII on the port */
		qca8k_write(priv, reg, QCA8K_PORT_PAD_SGMII_EN);

		/* Enable/disable SerDes auto-negotiation as necessary */
		ret = qca8k_read(priv, QCA8K_REG_PWS, &val);
		if (ret)
			return;
		if (phylink_autoneg_inband(mode))
			val &= ~QCA8K_PWS_SERDES_AEN_DIS;
		else
			val |= QCA8K_PWS_SERDES_AEN_DIS;
		qca8k_write(priv, QCA8K_REG_PWS, val);

		/* Configure the SGMII parameters */
		ret = qca8k_read(priv, QCA8K_REG_SGMII_CTRL, &val);
		if (ret)
			return;

		val |= QCA8K_SGMII_EN_PLL | QCA8K_SGMII_EN_RX |
			QCA8K_SGMII_EN_TX | QCA8K_SGMII_EN_SD;

		if (dsa_is_cpu_port(ds, port)) {
			/* CPU port, we're talking to the CPU MAC, be a PHY */
			val &= ~QCA8K_SGMII_MODE_CTRL_MASK;
			val |= QCA8K_SGMII_MODE_CTRL_PHY;
		} else if (state->interface == PHY_INTERFACE_MODE_SGMII) {
			val &= ~QCA8K_SGMII_MODE_CTRL_MASK;
			val |= QCA8K_SGMII_MODE_CTRL_MAC;
		} else if (state->interface == PHY_INTERFACE_MODE_1000BASEX) {
			val &= ~QCA8K_SGMII_MODE_CTRL_MASK;
			val |= QCA8K_SGMII_MODE_CTRL_BASEX;
		}

		qca8k_write(priv, QCA8K_REG_SGMII_CTRL, val);
		break;
	default:
		dev_err(ds->dev, "xMII mode %s not supported for port %d\n",
			phy_modes(state->interface), port);
		return;
	}
}

static void
qca8k_phylink_validate(struct dsa_switch *ds, int port,
		       unsigned long *supported,
		       struct phylink_link_state *state)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	switch (port) {
	case 0: /* 1st CPU port */
		if (state->interface != PHY_INTERFACE_MODE_NA &&
		    state->interface != PHY_INTERFACE_MODE_RGMII &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_ID &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_TXID &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_RXID &&
		    state->interface != PHY_INTERFACE_MODE_SGMII)
			goto unsupported;
		break;
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
		/* Internal PHY */
		if (state->interface != PHY_INTERFACE_MODE_NA &&
		    state->interface != PHY_INTERFACE_MODE_GMII &&
		    state->interface != PHY_INTERFACE_MODE_INTERNAL)
			goto unsupported;
		break;
	case 6: /* 2nd CPU port / external PHY */
		if (state->interface != PHY_INTERFACE_MODE_NA &&
		    state->interface != PHY_INTERFACE_MODE_RGMII &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_ID &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_TXID &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_RXID &&
		    state->interface != PHY_INTERFACE_MODE_SGMII &&
		    state->interface != PHY_INTERFACE_MODE_1000BASEX)
			goto unsupported;
		break;
	default:
unsupported:
		linkmode_zero(supported);
		return;
	}

	phylink_set_port_modes(mask);
	phylink_set(mask, Autoneg);

	phylink_set(mask, 1000baseT_Full);
	phylink_set(mask, 10baseT_Half);
	phylink_set(mask, 10baseT_Full);
	phylink_set(mask, 100baseT_Half);
	phylink_set(mask, 100baseT_Full);

	if (state->interface == PHY_INTERFACE_MODE_1000BASEX)
		phylink_set(mask, 1000baseX_Full);

	phylink_set(mask, Pause);
	phylink_set(mask, Asym_Pause);

	linkmode_and(supported, supported, mask);
	linkmode_and(state->advertising, state->advertising, mask);
}

static int
qca8k_phylink_mac_link_state(struct dsa_switch *ds, int port,
			     struct phylink_link_state *state)
{
	struct qca8k_priv *priv = ds->priv;
	u32 reg;
	int ret;

	ret = qca8k_read(priv, QCA8K_REG_PORT_STATUS(port), &reg);
	if (ret < 0)
		return ret;

	state->link = !!(reg & QCA8K_PORT_STATUS_LINK_UP);
	state->an_complete = state->link;
	state->an_enabled = !!(reg & QCA8K_PORT_STATUS_LINK_AUTO);
	state->duplex = (reg & QCA8K_PORT_STATUS_DUPLEX) ? DUPLEX_FULL :
							   DUPLEX_HALF;

	switch (reg & QCA8K_PORT_STATUS_SPEED) {
	case QCA8K_PORT_STATUS_SPEED_10:
		state->speed = SPEED_10;
		break;
	case QCA8K_PORT_STATUS_SPEED_100:
		state->speed = SPEED_100;
		break;
	case QCA8K_PORT_STATUS_SPEED_1000:
		state->speed = SPEED_1000;
		break;
	default:
		state->speed = SPEED_UNKNOWN;
		break;
	}

	state->pause = MLO_PAUSE_NONE;
	if (reg & QCA8K_PORT_STATUS_RXFLOW)
		state->pause |= MLO_PAUSE_RX;
	if (reg & QCA8K_PORT_STATUS_TXFLOW)
		state->pause |= MLO_PAUSE_TX;

	return 1;
}

static void
qca8k_phylink_mac_link_down(struct dsa_switch *ds, int port, unsigned int mode,
			    phy_interface_t interface)
{
	struct qca8k_priv *priv = ds->priv;

	qca8k_port_set_status(priv, port, 0);
}

static void
qca8k_phylink_mac_link_up(struct dsa_switch *ds, int port, unsigned int mode,
			  phy_interface_t interface, struct phy_device *phydev,
			  int speed, int duplex, bool tx_pause, bool rx_pause)
{
	struct qca8k_priv *priv = ds->priv;
	u32 reg;

	if (phylink_autoneg_inband(mode)) {
		reg = QCA8K_PORT_STATUS_LINK_AUTO;
	} else {
		switch (speed) {
		case SPEED_10:
			reg = QCA8K_PORT_STATUS_SPEED_10;
			break;
		case SPEED_100:
			reg = QCA8K_PORT_STATUS_SPEED_100;
			break;
		case SPEED_1000:
			reg = QCA8K_PORT_STATUS_SPEED_1000;
			break;
		default:
			reg = QCA8K_PORT_STATUS_LINK_AUTO;
			break;
		}

		if (duplex == DUPLEX_FULL)
			reg |= QCA8K_PORT_STATUS_DUPLEX;

		if (rx_pause || dsa_is_cpu_port(ds, port))
			reg |= QCA8K_PORT_STATUS_RXFLOW;

		if (tx_pause || dsa_is_cpu_port(ds, port))
			reg |= QCA8K_PORT_STATUS_TXFLOW;
	}

	reg |= QCA8K_PORT_STATUS_TXMAC | QCA8K_PORT_STATUS_RXMAC;

	qca8k_write(priv, QCA8K_REG_PORT_STATUS(port), reg);
}

static void
qca8k_get_strings(struct dsa_switch *ds, int port, u32 stringset, uint8_t *data)
{
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(ar8327_mib); i++)
		strncpy(data + i * ETH_GSTRING_LEN, ar8327_mib[i].name,
			ETH_GSTRING_LEN);
}

static void
qca8k_get_ethtool_stats(struct dsa_switch *ds, int port,
			uint64_t *data)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	const struct qca8k_mib_desc *mib;
	u32 reg, i, val;
	u32 hi = 0;
	int ret;

	for (i = 0; i < ARRAY_SIZE(ar8327_mib); i++) {
		mib = &ar8327_mib[i];
		reg = QCA8K_PORT_MIB_COUNTER(port) + mib->offset;

		ret = qca8k_read(priv, reg, &val);
		if (ret < 0)
			continue;

		if (mib->size == 2) {
			ret = qca8k_read(priv, reg + 4, &hi);
			if (ret < 0)
				continue;
		}

		data[i] = val;
		if (mib->size == 2)
			data[i] |= (u64)hi << 32;
	}
}

static int
qca8k_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	if (sset != ETH_SS_STATS)
		return 0;

	return ARRAY_SIZE(ar8327_mib);
}

static int
qca8k_set_mac_eee(struct dsa_switch *ds, int port, struct ethtool_eee *eee)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	u32 lpi_en = QCA8K_REG_EEE_CTRL_LPI_EN(port);
	u32 reg;
	int ret;

	mutex_lock(&priv->reg_mutex);
	ret = qca8k_read(priv, QCA8K_REG_EEE_CTRL, &reg);
	if (ret < 0)
		goto exit;

	if (eee->eee_enabled)
		reg |= lpi_en;
	else
		reg &= ~lpi_en;
	ret = qca8k_write(priv, QCA8K_REG_EEE_CTRL, reg);

exit:
	mutex_unlock(&priv->reg_mutex);
	return ret;
}

static int
qca8k_get_mac_eee(struct dsa_switch *ds, int port, struct ethtool_eee *e)
{
	/* Nothing to do on the port's MAC */
	return 0;
}

static void
qca8k_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	u32 stp_state;

	switch (state) {
	case BR_STATE_DISABLED:
		stp_state = QCA8K_PORT_LOOKUP_STATE_DISABLED;
		break;
	case BR_STATE_BLOCKING:
		stp_state = QCA8K_PORT_LOOKUP_STATE_BLOCKING;
		break;
	case BR_STATE_LISTENING:
		stp_state = QCA8K_PORT_LOOKUP_STATE_LISTENING;
		break;
	case BR_STATE_LEARNING:
		stp_state = QCA8K_PORT_LOOKUP_STATE_LEARNING;
		break;
	case BR_STATE_FORWARDING:
	default:
		stp_state = QCA8K_PORT_LOOKUP_STATE_FORWARD;
		break;
	}

	qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
		  QCA8K_PORT_LOOKUP_STATE_MASK, stp_state);
}

static int
qca8k_port_bridge_join(struct dsa_switch *ds, int port, struct net_device *br)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	int port_mask = BIT(QCA8K_CPU_PORT);
	int i, ret;

	for (i = 1; i < QCA8K_NUM_PORTS; i++) {
		if (dsa_to_port(ds, i)->bridge_dev != br)
			continue;
		/* Add this port to the portvlan mask of the other ports
		 * in the bridge
		 */
		ret = qca8k_reg_set(priv,
				    QCA8K_PORT_LOOKUP_CTRL(i),
				    BIT(port));
		if (ret)
			return ret;
		if (i != port)
			port_mask |= BIT(i);
	}

	/* Add all other ports to this ports portvlan mask */
	ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
			QCA8K_PORT_LOOKUP_MEMBER, port_mask);

	return ret;
}

static void
qca8k_port_bridge_leave(struct dsa_switch *ds, int port, struct net_device *br)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	int i;

	for (i = 1; i < QCA8K_NUM_PORTS; i++) {
		if (dsa_to_port(ds, i)->bridge_dev != br)
			continue;
		/* Remove this port to the portvlan mask of the other ports
		 * in the bridge
		 */
		qca8k_reg_clear(priv,
				QCA8K_PORT_LOOKUP_CTRL(i),
				BIT(port));
	}

	/* Set the cpu port to be the only one in the portvlan mask of
	 * this port
	 */
	qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
		  QCA8K_PORT_LOOKUP_MEMBER, BIT(QCA8K_CPU_PORT));
}

static int
qca8k_port_enable(struct dsa_switch *ds, int port,
		  struct phy_device *phy)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;

	qca8k_port_set_status(priv, port, 1);
	priv->port_sts[port].enabled = 1;

	if (dsa_is_user_port(ds, port))
		phy_support_asym_pause(phy);

	return 0;
}

static void
qca8k_port_disable(struct dsa_switch *ds, int port)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;

	qca8k_port_set_status(priv, port, 0);
	priv->port_sts[port].enabled = 0;
}

static int
qca8k_port_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	struct qca8k_priv *priv = ds->priv;
	int i, mtu = 0;

	priv->port_mtu[port] = new_mtu;

	for (i = 0; i < QCA8K_NUM_PORTS; i++)
		if (priv->port_mtu[i] > mtu)
			mtu = priv->port_mtu[i];

	/* Include L2 header / FCS length */
	return qca8k_write(priv, QCA8K_MAX_FRAME_SIZE, mtu + ETH_HLEN + ETH_FCS_LEN);
}

static int
qca8k_port_max_mtu(struct dsa_switch *ds, int port)
{
	return QCA8K_MAX_MTU;
}

static int
qca8k_port_fdb_insert(struct qca8k_priv *priv, const u8 *addr,
		      u16 port_mask, u16 vid)
{
	/* Set the vid to the port vlan id if no vid is set */
	if (!vid)
		vid = QCA8K_PORT_VID_DEF;

	return qca8k_fdb_add(priv, addr, port_mask, vid,
			     QCA8K_ATU_STATUS_STATIC);
}

static int
qca8k_port_fdb_add(struct dsa_switch *ds, int port,
		   const unsigned char *addr, u16 vid)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	u16 port_mask = BIT(port);

	return qca8k_port_fdb_insert(priv, addr, port_mask, vid);
}

static int
qca8k_port_fdb_del(struct dsa_switch *ds, int port,
		   const unsigned char *addr, u16 vid)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	u16 port_mask = BIT(port);

	if (!vid)
		vid = QCA8K_PORT_VID_DEF;

	return qca8k_fdb_del(priv, addr, port_mask, vid);
}

static int
qca8k_port_fdb_dump(struct dsa_switch *ds, int port,
		    dsa_fdb_dump_cb_t *cb, void *data)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	struct qca8k_fdb _fdb = { 0 };
	int cnt = QCA8K_NUM_FDB_RECORDS;
	bool is_static;
	int ret = 0;

	mutex_lock(&priv->reg_mutex);
	while (cnt-- && !qca8k_fdb_next(priv, &_fdb, port)) {
		if (!_fdb.aging)
			break;
		is_static = (_fdb.aging == QCA8K_ATU_STATUS_STATIC);
		ret = cb(_fdb.mac, _fdb.vid, is_static, data);
		if (ret)
			break;
	}
	mutex_unlock(&priv->reg_mutex);

	return 0;
}

static int
qca8k_port_vlan_filtering(struct dsa_switch *ds, int port, bool vlan_filtering,
			  struct netlink_ext_ack *extack)
{
	struct qca8k_priv *priv = ds->priv;
	int ret;

	if (vlan_filtering) {
		ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
				QCA8K_PORT_LOOKUP_VLAN_MODE,
				QCA8K_PORT_LOOKUP_VLAN_MODE_SECURE);
	} else {
		ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
				QCA8K_PORT_LOOKUP_VLAN_MODE,
				QCA8K_PORT_LOOKUP_VLAN_MODE_NONE);
	}

	return ret;
}

static int
qca8k_port_vlan_add(struct dsa_switch *ds, int port,
		    const struct switchdev_obj_port_vlan *vlan,
		    struct netlink_ext_ack *extack)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct qca8k_priv *priv = ds->priv;
	int ret;

	ret = qca8k_vlan_add(priv, port, vlan->vid, untagged);
	if (ret) {
		dev_err(priv->dev, "Failed to add VLAN to port %d (%d)", port, ret);
		return ret;
	}

	if (pvid) {
		int shift = 16 * (port % 2);

		ret = qca8k_rmw(priv, QCA8K_EGRESS_VLAN(port),
				0xfff << shift, vlan->vid << shift);
		if (ret)
			return ret;

		ret = qca8k_write(priv, QCA8K_REG_PORT_VLAN_CTRL0(port),
				  QCA8K_PORT_VLAN_CVID(vlan->vid) |
				  QCA8K_PORT_VLAN_SVID(vlan->vid));
	}

	return ret;
}

static int
qca8k_port_vlan_del(struct dsa_switch *ds, int port,
		    const struct switchdev_obj_port_vlan *vlan)
{
	struct qca8k_priv *priv = ds->priv;
	int ret;

	ret = qca8k_vlan_del(priv, port, vlan->vid);
	if (ret)
		dev_err(priv->dev, "Failed to delete VLAN from port %d (%d)", port, ret);

	return ret;
}

static u32 qca8k_get_phy_flags(struct dsa_switch *ds, int port)
{
	struct qca8k_priv *priv = ds->priv;

	/* Communicate to the phy internal driver the switch revision.
	 * Based on the switch revision different values needs to be
	 * set to the dbg and mmd reg on the phy.
	 * The first 2 bit are used to communicate the switch revision
	 * to the phy driver.
	 */
	if (port > 0 && port < 6)
		return priv->switch_revision;

	return 0;
}

static enum dsa_tag_protocol
qca8k_get_tag_protocol(struct dsa_switch *ds, int port,
		       enum dsa_tag_protocol mp)
{
	return DSA_TAG_PROTO_QCA;
}

static const struct dsa_switch_ops qca8k_switch_ops = {
	.get_tag_protocol	= qca8k_get_tag_protocol,
	.setup			= qca8k_setup,
	.get_strings		= qca8k_get_strings,
	.get_ethtool_stats	= qca8k_get_ethtool_stats,
	.get_sset_count		= qca8k_get_sset_count,
	.get_mac_eee		= qca8k_get_mac_eee,
	.set_mac_eee		= qca8k_set_mac_eee,
	.port_enable		= qca8k_port_enable,
	.port_disable		= qca8k_port_disable,
	.port_change_mtu	= qca8k_port_change_mtu,
	.port_max_mtu		= qca8k_port_max_mtu,
	.port_stp_state_set	= qca8k_port_stp_state_set,
	.port_bridge_join	= qca8k_port_bridge_join,
	.port_bridge_leave	= qca8k_port_bridge_leave,
	.port_fdb_add		= qca8k_port_fdb_add,
	.port_fdb_del		= qca8k_port_fdb_del,
	.port_fdb_dump		= qca8k_port_fdb_dump,
	.port_vlan_filtering	= qca8k_port_vlan_filtering,
	.port_vlan_add		= qca8k_port_vlan_add,
	.port_vlan_del		= qca8k_port_vlan_del,
	.phylink_validate	= qca8k_phylink_validate,
	.phylink_mac_link_state	= qca8k_phylink_mac_link_state,
	.phylink_mac_config	= qca8k_phylink_mac_config,
	.phylink_mac_link_down	= qca8k_phylink_mac_link_down,
	.phylink_mac_link_up	= qca8k_phylink_mac_link_up,
	.get_phy_flags		= qca8k_get_phy_flags,
};

static int qca8k_read_switch_id(struct qca8k_priv *priv)
{
	const struct qca8k_match_data *data;
	u32 val;
	u8 id;
	int ret;

	/* get the switches ID from the compatible */
	data = of_device_get_match_data(priv->dev);
	if (!data)
		return -ENODEV;

	ret = qca8k_read(priv, QCA8K_REG_MASK_CTRL, &val);
	if (ret < 0)
		return -ENODEV;

	id = QCA8K_MASK_CTRL_DEVICE_ID(val & QCA8K_MASK_CTRL_DEVICE_ID_MASK);
	if (id != data->id) {
		dev_err(priv->dev, "Switch id detected %x but expected %x", id, data->id);
		return -ENODEV;
	}

	priv->switch_id = id;

	/* Save revision to communicate to the internal PHY driver */
	priv->switch_revision = (val & QCA8K_MASK_CTRL_REV_ID_MASK);

	return 0;
}

static int
qca8k_sw_probe(struct mdio_device *mdiodev)
{
	struct qca8k_priv *priv;
	int ret;

	/* allocate the private data struct so that we can probe the switches
	 * ID register
	 */
	priv = devm_kzalloc(&mdiodev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->bus = mdiodev->bus;
	priv->dev = &mdiodev->dev;

	priv->reset_gpio = devm_gpiod_get_optional(priv->dev, "reset",
						   GPIOD_ASIS);
	if (IS_ERR(priv->reset_gpio))
		return PTR_ERR(priv->reset_gpio);

	if (priv->reset_gpio) {
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
		/* The active low duration must be greater than 10 ms
		 * and checkpatch.pl wants 20 ms.
		 */
		msleep(20);
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
	}

	/* Check the detected switch id */
	ret = qca8k_read_switch_id(priv);
	if (ret)
		return ret;

	priv->ds = devm_kzalloc(&mdiodev->dev, sizeof(*priv->ds), GFP_KERNEL);
	if (!priv->ds)
		return -ENOMEM;

	priv->ds->dev = &mdiodev->dev;
	priv->ds->num_ports = QCA8K_NUM_PORTS;
	priv->ds->priv = priv;
	priv->ops = qca8k_switch_ops;
	priv->ds->ops = &priv->ops;
	mutex_init(&priv->reg_mutex);
	dev_set_drvdata(&mdiodev->dev, priv);

	return dsa_register_switch(priv->ds);
}

static void
qca8k_sw_remove(struct mdio_device *mdiodev)
{
	struct qca8k_priv *priv = dev_get_drvdata(&mdiodev->dev);
	int i;

	for (i = 0; i < QCA8K_NUM_PORTS; i++)
		qca8k_port_set_status(priv, i, 0);

	dsa_unregister_switch(priv->ds);
}

#ifdef CONFIG_PM_SLEEP
static void
qca8k_set_pm(struct qca8k_priv *priv, int enable)
{
	int i;

	for (i = 0; i < QCA8K_NUM_PORTS; i++) {
		if (!priv->port_sts[i].enabled)
			continue;

		qca8k_port_set_status(priv, i, enable);
	}
}

static int qca8k_suspend(struct device *dev)
{
	struct qca8k_priv *priv = dev_get_drvdata(dev);

	qca8k_set_pm(priv, 0);

	return dsa_switch_suspend(priv->ds);
}

static int qca8k_resume(struct device *dev)
{
	struct qca8k_priv *priv = dev_get_drvdata(dev);

	qca8k_set_pm(priv, 1);

	return dsa_switch_resume(priv->ds);
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(qca8k_pm_ops,
			 qca8k_suspend, qca8k_resume);

static const struct qca8k_match_data qca832x = {
	.id = QCA8K_ID_QCA8327,
};

static const struct qca8k_match_data qca833x = {
	.id = QCA8K_ID_QCA8337,
};

static const struct of_device_id qca8k_of_match[] = {
	{ .compatible = "qca,qca8327", .data = &qca832x },
	{ .compatible = "qca,qca8334", .data = &qca833x },
	{ .compatible = "qca,qca8337", .data = &qca833x },
	{ /* sentinel */ },
};

static struct mdio_driver qca8kmdio_driver = {
	.probe  = qca8k_sw_probe,
	.remove = qca8k_sw_remove,
	.mdiodrv.driver = {
		.name = "qca8k",
		.of_match_table = qca8k_of_match,
		.pm = &qca8k_pm_ops,
	},
};

mdio_module_driver(qca8kmdio_driver);

MODULE_AUTHOR("Mathieu Olivari, John Crispin <john@phrozen.org>");
MODULE_DESCRIPTION("Driver for QCA8K ethernet switch family");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qca8k");
