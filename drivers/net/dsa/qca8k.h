/*
 * Copyright (C) 2009 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2011-2012 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __QCA8K_H
#define __QCA8K_H

#include <linux/delay.h>
#include <linux/regmap.h>

#define QCA8K_NUM_PORTS					7

#define PHY_ID_QCA8337					0x004dd036
#define QCA8K_ID_QCA8337				0x13

#define QCA8K_NUM_FDB_RECORDS				2048

#define QCA8K_CPU_PORT					0

/* Global control registers */
#define QCA8K_REG_MASK_CTRL				0x000
#define   QCA8K_MASK_CTRL_ID_M				0xff
#define   QCA8K_MASK_CTRL_ID_S				8
#define QCA8K_REG_PORT0_PAD_CTRL			0x004
#define QCA8K_REG_PORT5_PAD_CTRL			0x008
#define QCA8K_REG_PORT6_PAD_CTRL			0x00c
#define   QCA8K_PORT_PAD_RGMII_EN			BIT(26)
#define   QCA8K_PORT_PAD_RGMII_TX_DELAY(x)		\
						((0x8 + (x & 0x3)) << 22)
#define   QCA8K_PORT_PAD_RGMII_RX_DELAY(x)		\
						((0x10 + (x & 0x3)) << 20)
#define   QCA8K_MAX_DELAY				3
#define   QCA8K_PORT_PAD_RGMII_RX_DELAY_EN		BIT(24)
#define   QCA8K_PORT_PAD_SGMII_EN			BIT(7)
#define QCA8K_REG_MODULE_EN				0x030
#define   QCA8K_MODULE_EN_MIB				BIT(0)
#define QCA8K_REG_MIB					0x034
#define   QCA8K_MIB_FLUSH				BIT(24)
#define   QCA8K_MIB_CPU_KEEP				BIT(20)
#define   QCA8K_MIB_BUSY				BIT(17)
#define QCA8K_MDIO_MASTER_CTRL				0x3c
#define   QCA8K_MDIO_MASTER_BUSY			BIT(31)
#define   QCA8K_MDIO_MASTER_EN				BIT(30)
#define   QCA8K_MDIO_MASTER_READ			BIT(27)
#define   QCA8K_MDIO_MASTER_WRITE			0
#define   QCA8K_MDIO_MASTER_SUP_PRE			BIT(26)
#define   QCA8K_MDIO_MASTER_PHY_ADDR(x)			((x) << 21)
#define   QCA8K_MDIO_MASTER_REG_ADDR(x)			((x) << 16)
#define   QCA8K_MDIO_MASTER_DATA(x)			(x)
#define   QCA8K_MDIO_MASTER_DATA_MASK			GENMASK(15, 0)
#define   QCA8K_MDIO_MASTER_MAX_PORTS			5
#define   QCA8K_MDIO_MASTER_MAX_REG			32
#define QCA8K_GOL_MAC_ADDR0				0x60
#define QCA8K_GOL_MAC_ADDR1				0x64
#define QCA8K_REG_PORT_STATUS(_i)			(0x07c + (_i) * 4)
#define   QCA8K_PORT_STATUS_SPEED			GENMASK(1, 0)
#define   QCA8K_PORT_STATUS_SPEED_10			0
#define   QCA8K_PORT_STATUS_SPEED_100			0x1
#define   QCA8K_PORT_STATUS_SPEED_1000			0x2
#define   QCA8K_PORT_STATUS_TXMAC			BIT(2)
#define   QCA8K_PORT_STATUS_RXMAC			BIT(3)
#define   QCA8K_PORT_STATUS_TXFLOW			BIT(4)
#define   QCA8K_PORT_STATUS_RXFLOW			BIT(5)
#define   QCA8K_PORT_STATUS_DUPLEX			BIT(6)
#define   QCA8K_PORT_STATUS_LINK_UP			BIT(8)
#define   QCA8K_PORT_STATUS_LINK_AUTO			BIT(9)
#define   QCA8K_PORT_STATUS_LINK_PAUSE			BIT(10)
#define QCA8K_REG_PORT_HDR_CTRL(_i)			(0x9c + (_i * 4))
#define   QCA8K_PORT_HDR_CTRL_RX_MASK			GENMASK(3, 2)
#define   QCA8K_PORT_HDR_CTRL_RX_S			2
#define   QCA8K_PORT_HDR_CTRL_TX_MASK			GENMASK(1, 0)
#define   QCA8K_PORT_HDR_CTRL_TX_S			0
#define   QCA8K_PORT_HDR_CTRL_ALL			2
#define   QCA8K_PORT_HDR_CTRL_MGMT			1
#define   QCA8K_PORT_HDR_CTRL_NONE			0

/* EEE control registers */
#define QCA8K_REG_EEE_CTRL				0x100
#define  QCA8K_REG_EEE_CTRL_LPI_EN(_i)			((_i + 1) * 2)

/* ACL registers */
#define QCA8K_REG_PORT_VLAN_CTRL0(_i)			(0x420 + (_i * 8))
#define   QCA8K_PORT_VLAN_CVID(x)			(x << 16)
#define   QCA8K_PORT_VLAN_SVID(x)			x
#define QCA8K_REG_PORT_VLAN_CTRL1(_i)			(0x424 + (_i * 8))
#define QCA8K_REG_IPV4_PRI_BASE_ADDR			0x470
#define QCA8K_REG_IPV4_PRI_ADDR_MASK			0x474

/* Lookup registers */
#define QCA8K_REG_ATU_DATA0				0x600
#define   QCA8K_ATU_ADDR2_S				24
#define   QCA8K_ATU_ADDR3_S				16
#define   QCA8K_ATU_ADDR4_S				8
#define QCA8K_REG_ATU_DATA1				0x604
#define   QCA8K_ATU_PORT_M				0x7f
#define   QCA8K_ATU_PORT_S				16
#define   QCA8K_ATU_ADDR0_S				8
#define QCA8K_REG_ATU_DATA2				0x608
#define   QCA8K_ATU_VID_M				0xfff
#define   QCA8K_ATU_VID_S				8
#define   QCA8K_ATU_STATUS_M				0xf
#define   QCA8K_ATU_STATUS_STATIC			0xf
#define QCA8K_REG_ATU_FUNC				0x60c
#define   QCA8K_ATU_FUNC_BUSY				BIT(31)
#define   QCA8K_ATU_FUNC_PORT_EN			BIT(14)
#define   QCA8K_ATU_FUNC_MULTI_EN			BIT(13)
#define   QCA8K_ATU_FUNC_FULL				BIT(12)
#define   QCA8K_ATU_FUNC_PORT_M				0xf
#define   QCA8K_ATU_FUNC_PORT_S				8
#define QCA8K_REG_GLOBAL_FW_CTRL0			0x620
#define   QCA8K_GLOBAL_FW_CTRL0_CPU_PORT_EN		BIT(10)
#define QCA8K_REG_GLOBAL_FW_CTRL1			0x624
#define   QCA8K_GLOBAL_FW_CTRL1_IGMP_DP_S		24
#define   QCA8K_GLOBAL_FW_CTRL1_BC_DP_S			16
#define   QCA8K_GLOBAL_FW_CTRL1_MC_DP_S			8
#define   QCA8K_GLOBAL_FW_CTRL1_UC_DP_S			0
#define QCA8K_PORT_LOOKUP_CTRL(_i)			(0x660 + (_i) * 0xc)
#define   QCA8K_PORT_LOOKUP_MEMBER			GENMASK(6, 0)
#define   QCA8K_PORT_LOOKUP_STATE_MASK			GENMASK(18, 16)
#define   QCA8K_PORT_LOOKUP_STATE_DISABLED		(0 << 16)
#define   QCA8K_PORT_LOOKUP_STATE_BLOCKING		(1 << 16)
#define   QCA8K_PORT_LOOKUP_STATE_LISTENING		(2 << 16)
#define   QCA8K_PORT_LOOKUP_STATE_LEARNING		(3 << 16)
#define   QCA8K_PORT_LOOKUP_STATE_FORWARD		(4 << 16)
#define   QCA8K_PORT_LOOKUP_STATE			GENMASK(18, 16)
#define   QCA8K_PORT_LOOKUP_LEARN			BIT(20)

/* Pkt edit registers */
#define QCA8K_EGRESS_VLAN(x)				(0x0c70 + (4 * (x / 2)))

/* L3 registers */
#define QCA8K_HROUTER_CONTROL				0xe00
#define   QCA8K_HROUTER_CONTROL_GLB_LOCKTIME_M		GENMASK(17, 16)
#define   QCA8K_HROUTER_CONTROL_GLB_LOCKTIME_S		16
#define   QCA8K_HROUTER_CONTROL_ARP_AGE_MODE		1
#define QCA8K_HROUTER_PBASED_CONTROL1			0xe08
#define QCA8K_HROUTER_PBASED_CONTROL2			0xe0c
#define QCA8K_HNAT_CONTROL				0xe38

/* MIB registers */
#define QCA8K_PORT_MIB_COUNTER(_i)			(0x1000 + (_i) * 0x100)

/* QCA specific MII registers */
#define MII_ATH_MMD_ADDR				0x0d
#define MII_ATH_MMD_DATA				0x0e

enum {
	QCA8K_PORT_SPEED_10M = 0,
	QCA8K_PORT_SPEED_100M = 1,
	QCA8K_PORT_SPEED_1000M = 2,
	QCA8K_PORT_SPEED_ERR = 3,
};

enum qca8k_fdb_cmd {
	QCA8K_FDB_FLUSH	= 1,
	QCA8K_FDB_LOAD = 2,
	QCA8K_FDB_PURGE = 3,
	QCA8K_FDB_NEXT = 6,
	QCA8K_FDB_SEARCH = 7,
};

struct ar8xxx_port_status {
	int enabled;
};

struct qca8k_priv {
	struct regmap *regmap;
	struct mii_bus *bus;
	struct ar8xxx_port_status port_sts[QCA8K_NUM_PORTS];
	struct dsa_switch *ds;
	struct mutex reg_mutex;
	struct device *dev;
	struct dsa_switch_ops ops;
};

struct qca8k_mib_desc {
	unsigned int size;
	unsigned int offset;
	const char *name;
};

struct qca8k_fdb {
	u16 vid;
	u8 port_mask;
	u8 aging;
	u8 mac[6];
};

#endif /* __QCA8K_H */
