// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) 2014, 2015, 2017, The Linux Foundation. All rights reserved.
 * Copyright (C) 2009 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2011-2012 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (c) 2016 John Crispin john@phrozen.org
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * Author: Matus Ujhelyi <ujhelyi.m@gmail.com>
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* QCA8337 Switch driver
 */

#include <linux/phy.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/of_net.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/qca8337.h>

static inline void split_addr(u32 regaddr, u16 *r1, u16 *r2, u16 *page)
{
	regaddr >>= 1;
	*r1 = regaddr & 0x1e;

	regaddr >>= 5;
	*r2 = regaddr & 0x7;

	regaddr >>= 3;
	*page = regaddr & 0x1ff;
}

u32 qca8337_read(struct qca8337_priv *priv, u32 reg)
{
	struct phy_device *phy = priv->phy;
	struct mii_bus *bus = phy->mdio.bus;
	u16 r1, r2, page;
	u16 lo, hi;

	mutex_lock(&bus->mdio_lock);

	split_addr(reg, &r1, &r2, &page);

	bus->write(bus, 0x18, 0, page);
	usleep_range(1000, 2000); /* wait for the page switch to propagate */
	lo = bus->read(bus, 0x10 | r2, r1);
	hi = bus->read(bus, 0x10 | r2, r1 + 1);

	mutex_unlock(&bus->mdio_lock);

	return (hi << 16) | lo;
}
EXPORT_SYMBOL_GPL(qca8337_read);

void qca8337_write(struct qca8337_priv *priv, u32 reg, u32 val)
{
	struct phy_device *phy = priv->phy;
	struct mii_bus *bus = phy->mdio.bus;
	u16 r1, r2, r3;
	u16 lo, hi;

	mutex_lock(&bus->mdio_lock);

	split_addr(reg, &r1, &r2, &r3);
	lo = val & 0xffff;
	hi = (u16)(val >> 16);

	bus->write(bus, 0x18, 0, r3);
	usleep_range(1000, 2000); /* wait for the page switch to propagate */
	bus->write(bus, 0x10 | r2, r1, lo);
	bus->write(bus, 0x10 | r2, r1 + 1, hi);

	mutex_unlock(&bus->mdio_lock);
}
EXPORT_SYMBOL_GPL(qca8337_write);

static u32
qca8337_rmw(struct qca8337_priv *priv, u32 reg, u32 mask, u32 val)
{
	u32 ret;

	ret = priv->ops->read(priv, reg);
	ret &= ~mask;
	ret |= val;
	priv->ops->write(priv, reg, ret);
	return ret;
}

static void
qca8337_reg_set(struct qca8337_priv *priv, u32 reg, u32 val)
{
	qca8337_rmw(priv, reg, 0, val);
}

static void qca8337_reset_switch(struct qca8337_priv *priv)
{
	u32 val = 0;
	int count = 0;

	qca8337_reg_set(priv, QCA8337_REG_MASK_CTRL, QCA8337_CTRL_RESET);

	/*Need wait so reset done*/
	for (count = 0; count < 100; count++) {
		usleep_range(5000, 10000);

		val = priv->ops->read(priv, QCA8337_REG_MASK_CTRL);
		if (!val && !(val & QCA8337_CTRL_RESET))
			break;
	}
}

static void
qca8337_port_set_status(struct qca8337_priv *priv)
{
	qca8337_write(priv, QCA8337_REG_PORT_STATUS(0),
		      (QCA8337_PORT_SPEED_1000M | QCA8337_PORT_STATUS_TXMAC |
		      QCA8337_PORT_STATUS_RXMAC | QCA8337_PORT_STATUS_TXFLOW |
		      QCA8337_PORT_STATUS_RXFLOW | QCA8337_PORT_STATUS_DUPLEX));

	qca8337_write(priv, QCA8337_REG_PORT_STATUS(6),
		      (QCA8337_PORT_SPEED_1000M | QCA8337_PORT_STATUS_TXMAC |
		      QCA8337_PORT_STATUS_RXMAC | QCA8337_PORT_STATUS_TXFLOW |
		      QCA8337_PORT_STATUS_RXFLOW | QCA8337_PORT_STATUS_DUPLEX));
}

static int
qca8337_busy_wait(struct qca8337_priv *priv, u32 reg, u32 mask)
{
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(20);

	/* loop until the busy flag has cleared */
	do {
		u32 val = priv->ops->read(priv, reg);
		int busy = val & mask;

		if (!busy)
			break;
		cond_resched();
	} while (!time_after_eq(jiffies, timeout));

	return time_after_eq(jiffies, timeout);
}

static void
qca8337_mib_init(struct qca8337_priv *priv)
{
	qca8337_reg_set(priv, QCA8337_REG_MIB,
			QCA8337_MIB_FLUSH | QCA8337_MIB_BUSY);
	qca8337_busy_wait(priv, QCA8337_REG_MIB, QCA8337_MIB_BUSY);
	qca8337_reg_set(priv, QCA8337_REG_MIB, QCA8337_MIB_CPU_KEEP);
	priv->ops->write(priv, QCA8337_REG_MODULE_EN, QCA8337_MODULE_EN_MIB);
}

static void qca8337_vlan_config(struct qca8337_priv *priv)
{
	priv->ops->write(priv, QCA8337_REG_PORT_LOOKUP(0), 0x0014007e);
	priv->ops->write(priv, QCA8337_REG_PORT_VLAN0(0), 0x10001);

	priv->ops->write(priv, QCA8337_REG_PORT_LOOKUP(1), 0x0014007d);
	priv->ops->write(priv, QCA8337_REG_PORT_VLAN0(1), 0x10001);

	priv->ops->write(priv, QCA8337_REG_PORT_LOOKUP(2), 0x0014007b);
	priv->ops->write(priv, QCA8337_REG_PORT_VLAN0(2), 0x10001);

	priv->ops->write(priv, QCA8337_REG_PORT_LOOKUP(3), 0x00140077);
	priv->ops->write(priv, QCA8337_REG_PORT_VLAN0(3), 0x10001);

	priv->ops->write(priv, QCA8337_REG_PORT_LOOKUP(4), 0x0014006f);
	priv->ops->write(priv, QCA8337_REG_PORT_VLAN0(4), 0x10001);

	priv->ops->write(priv, QCA8337_REG_PORT_LOOKUP(5), 0x0014005f);
	priv->ops->write(priv, QCA8337_REG_PORT_VLAN0(5), 0x10001);

	priv->ops->write(priv, QCA8337_REG_PORT_LOOKUP(6), 0x0014001e);
	priv->ops->write(priv, QCA8337_REG_PORT_VLAN0(6), 0x10001);
}

static int qca8337_hw_init(struct qca8337_priv *priv)
{
	int i;

	/* set pad control for cpu port */
	qca8337_write(priv, QCA8337_REG_PAD0_CTRL, QCA8337_PAD_SGMII_EN);

	qca8337_write(priv, QCA8337_REG_PAD5_CTRL,
		      QCA8337_PAD_RGMII_RXCLK_DELAY_EN);

	qca8337_write(priv, QCA8337_REG_PAD6_CTRL,
		      (QCA8337_PAD_RGMII_EN | QCA8337_PAD_RGMII_RXCLK_DELAY_EN |
		      (0x1 << QCA8337_PAD_RGMII_TXCLK_DELAY_SEL_S) |
		      (0x2 << QCA8337_PAD_RGMII_RXCLK_DELAY_SEL_S)));

	/* Enable CPU Port */
	qca8337_reg_set(priv, QCA8337_REG_GLOBAL_FW_CTRL0,
			QCA8337_GLOBAL_FW_CTRL0_CPU_PORT_EN);

	qca8337_port_set_status(priv);

	/* Enable MIB counters */
	qca8337_mib_init(priv);

	/* Disable QCA header mode on the cpu port */
	priv->ops->write(priv, QCA8337_REG_PORT_HEADER(priv->cpu_port), 0);

	/* Disable forwarding by default on all ports */
	for (i = 0; i < priv->ports; i++)
		qca8337_rmw(priv, QCA8337_REG_PORT_LOOKUP(i),
			    QCA8337_PORT_LOOKUP_MEMBER, 0);

	qca8337_write(priv, QCA8337_REG_GLOBAL_FW_CTRL1,
		      (QCA8337_IGMP_JOIN_LEAVE_DPALL | QCA8337_BROAD_DPALL |
		       QCA8337_MULTI_FLOOD_DPALL | QCA8337_UNI_FLOOD_DPALL));

	/* Setup connection between CPU port & user ports */
	qca8337_vlan_config(priv);

	/* Disable AZ */
	priv->ops->write(priv, QCA8337_REG_EEE_CTRL, QCA8337_EEE_CTRL_DISABLE);
	return 0;
}

static void qca8337_reg_init_lan(struct qca8337_priv *priv)
{
	priv->ops->write(priv, QCA8337_REG_POWER_ON_STRIP,
			 QCA8337_REG_POS_VAL);
	priv->ops->write(priv, QCA8337_MAC_PWR_SEL,
			 QCA8337_MAC_PWR_SEL_VAL);
	priv->ops->write(priv, QCA8337_SGMII_CTRL_REG,
			 QCA8337_SGMII_CTRL_VAL);
}

static void
qca8337_read_port_link(struct qca8337_priv *priv, int port,
		       struct port_link_info *port_link)
{
	u32 status;
	u32 speed;

	memset(port_link, '\0', sizeof(*port_link));

	status = priv->ops->read(priv, QCA8337_REG_PORT_STATUS(port));

	port_link->aneg = !!(status & QCA8337_PORT_STATUS_LINK_AUTO);
	if (port_link->aneg || port != priv->cpu_port) {
		port_link->link = !!(status & QCA8337_PORT_STATUS_LINK_UP);
		if (!port_link->link)
			return;
	} else {
		port_link->link = true;
	}

	port_link->duplex = !!(status & QCA8337_PORT_STATUS_DUPLEX);
	port_link->tx_flow = !!(status & QCA8337_PORT_STATUS_TXFLOW);
	port_link->rx_flow = !!(status & QCA8337_PORT_STATUS_RXFLOW);

	speed = (status & QCA8337_PORT_STATUS_SPEED) >>
		 QCA8337_PORT_STATUS_SPEED_S;

	switch (speed) {
	case QCA8337_PORT_SPEED_10M:
		port_link->speed = SPEED_10;
		break;
	case QCA8337_PORT_SPEED_100M:
		port_link->speed = SPEED_100;
		break;
	case QCA8337_PORT_SPEED_1000M:
		port_link->speed = SPEED_1000;
		break;
	default:
		port_link->speed = SPEED_UNKNOWN;
		break;
	}
}

static void qca8337_phy_enable(struct phy_device *phydev)
{
	int phyid = 0;
	ushort phy_val;
	struct mii_bus *bus;
	struct qca8337_priv *priv = phydev->priv;

	bus = priv->phy->mdio.bus;

	if (phydev->autoneg == AUTONEG_ENABLE) {
		int port;

		for (port = 1; port < priv->ports - 1; port++)
			qca8337_write(priv, QCA8337_REG_PORT_STATUS(port),
				      0x1280);

		for (phyid = 0; phyid < priv->num_phy ; phyid++) {
			/*enable phy prefer multi-port mode*/
			phy_val = mdiobus_read(bus, phyid, MII_CTRL1000);
			phy_val |= (ADVERTISE_MULTI_PORT_PREFER |
				ADVERTISE_1000FULL);
			mdiobus_write(bus, phyid, MII_CTRL1000, phy_val);

			/*enable extended next page. 0:enable, 1:disable*/
			phy_val = mdiobus_read(bus, phyid, MII_ADVERTISE);
			phy_val &= (~(ADVERTISE_RESV));
			mdiobus_write(bus, phyid, MII_ADVERTISE, phy_val);

			/*Phy power up*/
			mdiobus_write(bus, phyid, MII_BMCR, (BMCR_RESET |
				      BMCR_ANENABLE));
			/* wait for the page switch to propagate */
			usleep_range(100, 200);
		}
	} else {
		int port;
		u32 status = 0;

		linkmode_and(phydev->advertising, phydev->advertising, phydev->supported);

		for (port = 1; port < priv->ports - 1; port++) {
			status = 0;
			status |= phydev->duplex ?
				QCA8337_PORT_STATUS_DUPLEX : 0;
			status |= (linkmode_test_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
				phydev->advertising)) ? QCA8337_PORT_STATUS_TXFLOW : 0;
			status |= (linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT,
				phydev->advertising)) ? QCA8337_PORT_STATUS_RXFLOW : 0;

			if (phydev->speed == SPEED_1000)
				status |= QCA8337_PORT_SPEED_1000M;
			else if (phydev->speed == SPEED_100)
				status |= QCA8337_PORT_SPEED_100M;
			else if (phydev->speed == SPEED_10)
				status |= QCA8337_PORT_SPEED_10M;

			qca8337_write(priv, QCA8337_REG_PORT_STATUS(port),
				      status);
			/* wait for the page switch to propagate */
			usleep_range(100, 200);

			status |= QCA8337_PORT_STATUS_TXMAC |
				QCA8337_PORT_STATUS_RXMAC;
			qca8337_write(priv, QCA8337_REG_PORT_STATUS(port),
				      status);
		}

		for (phyid = 0; phyid < priv->num_phy ; phyid++) {
			phydev->drv->phy_id = phyid;
			genphy_setup_forced(phydev);
		}

		for (phyid = 0; phyid < priv->num_phy ; phyid++) {
			phydev->drv->phy_id = phyid;
			genphy_update_link(phydev);

			if (phydev->link)
				break;
		}
	}
}

static int qca8337_config_aneg(struct phy_device *phydev)
{
	qca8337_phy_enable(phydev);

	return 0;
}

static int qca8337_read_status(struct phy_device *phydev)
{
	struct qca8337_priv *priv = phydev->priv;
	struct port_link_info port_link;
	int i, port_status = 0;
	int speed = -1, duplex = 0;

	for (i = 1; i < priv->ports - 1; i++) {
		qca8337_read_port_link(priv, i, &port_link);

		if (port_link.link) {
			speed = (speed < port_link.speed) ?
				port_link.speed : speed;
			duplex = (duplex < port_link.duplex) ?
				port_link.duplex : duplex;
			port_status |= 1 << i;
		}
	}

	qca8337_read_port_link(priv, priv->cpu_port, &port_link);
	phydev->link = (port_status) ? !!port_link.link : 0;
	phydev->speed = speed;
	phydev->duplex = duplex;

	return 0;
}

static int qca8337_aneg_done(struct phy_device *phydev)
{
	int phyid = 0;
	int retval = 0;
	int aneg_status = 0;
	struct qca8337_priv *priv = phydev->priv;
	struct mii_bus *bus = priv->phy->mdio.bus;

	for (phyid = 0; phyid < priv->num_phy ; phyid++) {
		retval = mdiobus_read(bus, phyid, MII_BMSR);
		if (retval < 0)
			return retval;

		(retval & BMSR_ANEGCOMPLETE) ?
			(aneg_status |= 1 << phyid) :
			(aneg_status |= 0 << phyid);
	}
	return aneg_status;
}

static int
qca8337_regmap_read(void *ctx, uint32_t reg, uint32_t *val)
{
	struct qca8337_priv *priv = (struct qca8337_priv *)ctx;

	if (!priv->phy->link)
		return -EPERM;

	*val = priv->ops->read(priv, reg);
	return 0;
}

static int
qca8337_regmap_write(void *ctx, uint32_t reg, uint32_t val)
{
	struct qca8337_priv *priv = (struct qca8337_priv *)ctx;

	if (!priv->phy->link)
		return -EPERM;

	priv->ops->write(priv, reg, val);
	return 0;
}

static const struct regmap_range qca8337_readable_ranges[] = {
	regmap_reg_range(0x0000, 0x00e4), /* Global control registers */
	regmap_reg_range(0x0100, 0x0168), /* EEE control registers */
	regmap_reg_range(0x0200, 0x0270), /* Parser control registers */
	regmap_reg_range(0x0400, 0x0454), /* ACL control registers */
	regmap_reg_range(0x0600, 0x0718), /* Lookup control registers */
	regmap_reg_range(0x0800, 0x0b70), /* QM control registers */
	regmap_reg_range(0x0c00, 0x0c80), /* PKT edit  control registers */
	regmap_reg_range(0x0e00, 0x0e98), /* L3 */
	regmap_reg_range(0x1000, 0x10ac), /* MIB - Port0 */
	regmap_reg_range(0x1100, 0x11ac), /* MIB - Port1 */
	regmap_reg_range(0x1200, 0x12ac), /* MIB - Port2 */
	regmap_reg_range(0x1300, 0x13ac), /* MIB - Port3 */
	regmap_reg_range(0x1400, 0x14ac), /* MIB - Port4 */
	regmap_reg_range(0x1500, 0x15ac), /* MIB - Port5 */
	regmap_reg_range(0x1600, 0x16ac), /* MIB - Port6 */
};

static const struct regmap_access_table qca8337_readable_table = {
	.yes_ranges = qca8337_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(qca8337_readable_ranges),
};

static struct regmap_config qca8337_regmap_config = {
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x16ac, /* end MIB - Port6 range */
	.reg_read = qca8337_regmap_read,
	.reg_write = qca8337_regmap_write,
	.rd_table =  &qca8337_readable_table,
};

static int qca8337_config_init(struct phy_device *phydev)
{
	struct qca8337_priv *priv = phydev->priv;
	int ret = 0;

	/*Software reset*/
	priv->ops->reset_switch(priv);
	/* Add delay to settle reset */
	usleep_range(100, 200);

	ret = priv->ops->hw_init(priv);
	if (ret)
		return ret;

	qca8337_reg_init_lan(priv);
	return 0;
}

static struct qca8337_switch_ops switch_ops = {
	.hw_init	=	qca8337_hw_init,
	.reset_switch	=	qca8337_reset_switch,
	.read		=	qca8337_read,
	.write		=	qca8337_write,
};

static int qca8337_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct qca8337_priv *priv = NULL;
	u32 val = 0;
	u16 id = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->phy = phydev;
	priv->dev = &phydev->mdio.dev;
	priv->cpu_port = QCA8337_CPU_PORT;
	priv->vlans = QCA8337_MAX_VLANS;
	priv->ports = QCA8337_NUM_PORTS;
	priv->num_phy = QCA8337_NUM_PHYS;
	priv->ops = &switch_ops;

	/* Setup the register mapping */
	priv->regmap = devm_regmap_init(priv->dev, NULL, priv,
					&qca8337_regmap_config);
	if (IS_ERR(priv->regmap))
		pr_warn("regmap initialization failed\n");

	/* read the switches ID register */
	val = qca8337_read(priv, QCA8337_REG_MASK_CTRL);
	id = val & (QCA8337_CTRL_REVISION | QCA8337_CTRL_VERSION);

	priv->chip_ver = (id & QCA8337_CTRL_VERSION) >> QCA8337_CTRL_VERSION_S;
	priv->chip_rev = (id & QCA8337_CTRL_REVISION);

	if (priv->chip_ver != QCA8337_ID_QCA8337) {
		dev_err(dev, "qca8337: unknown Atheros device\n");
		dev_err(dev, "[ver=%d, rev=%d, phy_id=%04x%04x]\n",
			priv->chip_ver, priv->chip_rev,
			mdiobus_read(priv->phy->mdio.bus, priv->phy->drv->phy_id, 2),
			mdiobus_read(priv->phy->mdio.bus, priv->phy->drv->phy_id, 3));

		return -ENODEV;
	}

	dev_dbg(dev, "qca8337: Switch probed successfully ");
	dev_dbg(dev, "[ver=%d, rev=%d, phy_id=%04x%04x]\n",
		priv->chip_ver, priv->chip_rev,
		mdiobus_read(priv->phy->mdio.bus, priv->phy->drv->phy_id, 2),
		mdiobus_read(priv->phy->mdio.bus, priv->phy->drv->phy_id, 3));

	phydev->priv = priv;
	return 0;
}

static void qca8337_remove(struct phy_device *phydev)
{
	struct qca8337_priv *priv = phydev->priv;

	if (!priv)
		return;
}

static struct phy_driver qca8337_driver = {
	.phy_id			= QCA8337_PHY_ID,
	.name			= "Atheros QCA8337",
	.phy_id_mask		= 0xffffffef,
	.probe			= qca8337_probe,
	.config_init		= qca8337_config_init,
	.features		= PHY_GBIT_FEATURES,
	.flags			= PHY_IS_INTERNAL,
	.config_aneg		= qca8337_config_aneg,
	.read_status		= qca8337_read_status,
	.aneg_done		= qca8337_aneg_done,
	.remove			= qca8337_remove,
};

static int __init qca8337_init(void)
{
	return phy_driver_register(&qca8337_driver, THIS_MODULE);
}

static void __exit qca8337_exit(void)
{
	 phy_driver_unregister(&qca8337_driver);
}

module_init(qca8337_init);
module_exit(qca8337_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:qca8337");
