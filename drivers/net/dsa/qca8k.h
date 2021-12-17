/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2009 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2011-2012 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#ifndef __QCA8K_H
#define __QCA8K_H

#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/gpio.h>

#define QCA8K_NUM_PORTS					7
#define QCA8K_NUM_CPU_PORTS				2
#define QCA8K_MAX_MTU					9000

#define PHY_ID_QCA8327					0x004dd034
#define QCA8K_ID_QCA8327				0x12
#define PHY_ID_QCA8337					0x004dd036
#define QCA8K_ID_QCA8337				0x13

#define QCA8K_BUSY_WAIT_TIMEOUT				2000

#define QCA8K_NUM_FDB_RECORDS				2048

#define QCA8K_PORT_VID_DEF				1

/* Global control registers */
#define QCA8K_REG_MASK_CTRL				0x000
#define   QCA8K_MASK_CTRL_REV_ID_MASK			GENMASK(7, 0)
#define   QCA8K_MASK_CTRL_REV_ID(x)			((x) >> 0)
#define   QCA8K_MASK_CTRL_DEVICE_ID_MASK		GENMASK(15, 8)
#define   QCA8K_MASK_CTRL_DEVICE_ID(x)			((x) >> 8)
#define QCA8K_REG_PORT0_PAD_CTRL			0x004
#define   QCA8K_PORT0_PAD_MAC06_EXCHANGE_EN		BIT(31)
#define   QCA8K_PORT0_PAD_SGMII_RXCLK_FALLING_EDGE	BIT(19)
#define   QCA8K_PORT0_PAD_SGMII_TXCLK_FALLING_EDGE	BIT(18)
#define QCA8K_REG_PORT5_PAD_CTRL			0x008
#define QCA8K_REG_PORT6_PAD_CTRL			0x00c
#define   QCA8K_PORT_PAD_RGMII_EN			BIT(26)
#define   QCA8K_PORT_PAD_RGMII_TX_DELAY_MASK		GENMASK(23, 22)
#define   QCA8K_PORT_PAD_RGMII_TX_DELAY(x)		((x) << 22)
#define   QCA8K_PORT_PAD_RGMII_RX_DELAY_MASK		GENMASK(21, 20)
#define   QCA8K_PORT_PAD_RGMII_RX_DELAY(x)		((x) << 20)
#define	  QCA8K_PORT_PAD_RGMII_TX_DELAY_EN		BIT(25)
#define   QCA8K_PORT_PAD_RGMII_RX_DELAY_EN		BIT(24)
#define   QCA8K_MAX_DELAY				3
#define   QCA8K_PORT_PAD_SGMII_EN			BIT(7)
#define QCA8K_REG_PWS					0x010
#define   QCA8K_PWS_POWER_ON_SEL			BIT(31)
/* This reg is only valid for QCA832x and toggle the package
 * type from 176 pin (by default) to 148 pin used on QCA8327
 */
#define   QCA8327_PWS_PACKAGE148_EN			BIT(30)
#define   QCA8K_PWS_LED_OPEN_EN_CSR			BIT(24)
#define   QCA8K_PWS_SERDES_AEN_DIS			BIT(7)
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
#define QCA8K_MAX_FRAME_SIZE				0x78
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
#define   QCA8K_PORT_STATUS_FLOW_AUTO			BIT(12)
#define QCA8K_REG_PORT_HDR_CTRL(_i)			(0x9c + (_i * 4))
#define   QCA8K_PORT_HDR_CTRL_RX_MASK			GENMASK(3, 2)
#define   QCA8K_PORT_HDR_CTRL_RX_S			2
#define   QCA8K_PORT_HDR_CTRL_TX_MASK			GENMASK(1, 0)
#define   QCA8K_PORT_HDR_CTRL_TX_S			0
#define   QCA8K_PORT_HDR_CTRL_ALL			2
#define   QCA8K_PORT_HDR_CTRL_MGMT			1
#define   QCA8K_PORT_HDR_CTRL_NONE			0
#define QCA8K_REG_SGMII_CTRL				0x0e0
#define   QCA8K_SGMII_EN_PLL				BIT(1)
#define   QCA8K_SGMII_EN_RX				BIT(2)
#define   QCA8K_SGMII_EN_TX				BIT(3)
#define   QCA8K_SGMII_EN_SD				BIT(4)
#define   QCA8K_SGMII_CLK125M_DELAY			BIT(7)
#define   QCA8K_SGMII_MODE_CTRL_MASK			(BIT(22) | BIT(23))
#define   QCA8K_SGMII_MODE_CTRL_BASEX			(0 << 22)
#define   QCA8K_SGMII_MODE_CTRL_PHY			(1 << 22)
#define   QCA8K_SGMII_MODE_CTRL_MAC			(2 << 22)

/* MAC_PWR_SEL registers */
#define QCA8K_REG_MAC_PWR_SEL				0x0e4
#define   QCA8K_MAC_PWR_RGMII1_1_8V			BIT(18)
#define   QCA8K_MAC_PWR_RGMII0_1_8V			BIT(19)

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
#define QCA8K_REG_VTU_FUNC0				0x610
#define   QCA8K_VTU_FUNC0_VALID				BIT(20)
#define   QCA8K_VTU_FUNC0_IVL_EN			BIT(19)
#define   QCA8K_VTU_FUNC0_EG_MODE_S(_i)			(4 + (_i) * 2)
#define   QCA8K_VTU_FUNC0_EG_MODE_MASK			3
#define   QCA8K_VTU_FUNC0_EG_MODE_UNMOD			0
#define   QCA8K_VTU_FUNC0_EG_MODE_UNTAG			1
#define   QCA8K_VTU_FUNC0_EG_MODE_TAG			2
#define   QCA8K_VTU_FUNC0_EG_MODE_NOT			3
#define QCA8K_REG_VTU_FUNC1				0x614
#define   QCA8K_VTU_FUNC1_BUSY				BIT(31)
#define   QCA8K_VTU_FUNC1_VID_S				16
#define   QCA8K_VTU_FUNC1_FULL				BIT(4)
#define QCA8K_REG_GLOBAL_FW_CTRL0			0x620
#define   QCA8K_GLOBAL_FW_CTRL0_CPU_PORT_EN		BIT(10)
#define QCA8K_REG_GLOBAL_FW_CTRL1			0x624
#define   QCA8K_GLOBAL_FW_CTRL1_IGMP_DP_S		24
#define   QCA8K_GLOBAL_FW_CTRL1_BC_DP_S			16
#define   QCA8K_GLOBAL_FW_CTRL1_MC_DP_S			8
#define   QCA8K_GLOBAL_FW_CTRL1_UC_DP_S			0
#define QCA8K_PORT_LOOKUP_CTRL(_i)			(0x660 + (_i) * 0xc)
#define   QCA8K_PORT_LOOKUP_MEMBER			GENMASK(6, 0)
#define   QCA8K_PORT_LOOKUP_VLAN_MODE			GENMASK(9, 8)
#define   QCA8K_PORT_LOOKUP_VLAN_MODE_NONE		(0 << 8)
#define   QCA8K_PORT_LOOKUP_VLAN_MODE_FALLBACK		(1 << 8)
#define   QCA8K_PORT_LOOKUP_VLAN_MODE_CHECK		(2 << 8)
#define   QCA8K_PORT_LOOKUP_VLAN_MODE_SECURE		(3 << 8)
#define   QCA8K_PORT_LOOKUP_STATE_MASK			GENMASK(18, 16)
#define   QCA8K_PORT_LOOKUP_STATE_DISABLED		(0 << 16)
#define   QCA8K_PORT_LOOKUP_STATE_BLOCKING		(1 << 16)
#define   QCA8K_PORT_LOOKUP_STATE_LISTENING		(2 << 16)
#define   QCA8K_PORT_LOOKUP_STATE_LEARNING		(3 << 16)
#define   QCA8K_PORT_LOOKUP_STATE_FORWARD		(4 << 16)
#define   QCA8K_PORT_LOOKUP_STATE			GENMASK(18, 16)
#define   QCA8K_PORT_LOOKUP_LEARN			BIT(20)

#define QCA8K_REG_GLOBAL_FC_THRESH			0x800
#define   QCA8K_GLOBAL_FC_GOL_XON_THRES(x)		((x) << 16)
#define   QCA8K_GLOBAL_FC_GOL_XON_THRES_S		GENMASK(24, 16)
#define   QCA8K_GLOBAL_FC_GOL_XOFF_THRES(x)		((x) << 0)
#define   QCA8K_GLOBAL_FC_GOL_XOFF_THRES_S		GENMASK(8, 0)

#define QCA8K_REG_PORT_HOL_CTRL0(_i)			(0x970 + (_i) * 0x8)
#define   QCA8K_PORT_HOL_CTRL0_EG_PRI0_BUF		GENMASK(3, 0)
#define   QCA8K_PORT_HOL_CTRL0_EG_PRI0(x)		((x) << 0)
#define   QCA8K_PORT_HOL_CTRL0_EG_PRI1_BUF		GENMASK(7, 4)
#define   QCA8K_PORT_HOL_CTRL0_EG_PRI1(x)		((x) << 4)
#define   QCA8K_PORT_HOL_CTRL0_EG_PRI2_BUF		GENMASK(11, 8)
#define   QCA8K_PORT_HOL_CTRL0_EG_PRI2(x)		((x) << 8)
#define   QCA8K_PORT_HOL_CTRL0_EG_PRI3_BUF		GENMASK(15, 12)
#define   QCA8K_PORT_HOL_CTRL0_EG_PRI3(x)		((x) << 12)
#define   QCA8K_PORT_HOL_CTRL0_EG_PRI4_BUF		GENMASK(19, 16)
#define   QCA8K_PORT_HOL_CTRL0_EG_PRI4(x)		((x) << 16)
#define   QCA8K_PORT_HOL_CTRL0_EG_PRI5_BUF		GENMASK(23, 20)
#define   QCA8K_PORT_HOL_CTRL0_EG_PRI5(x)		((x) << 20)
#define   QCA8K_PORT_HOL_CTRL0_EG_PORT_BUF		GENMASK(29, 24)
#define   QCA8K_PORT_HOL_CTRL0_EG_PORT(x)		((x) << 24)

#define QCA8K_REG_PORT_HOL_CTRL1(_i)			(0x974 + (_i) * 0x8)
#define   QCA8K_PORT_HOL_CTRL1_ING_BUF			GENMASK(3, 0)
#define   QCA8K_PORT_HOL_CTRL1_ING(x)			((x) << 0)
#define   QCA8K_PORT_HOL_CTRL1_EG_PRI_BUF_EN		BIT(6)
#define   QCA8K_PORT_HOL_CTRL1_EG_PORT_BUF_EN		BIT(7)
#define   QCA8K_PORT_HOL_CTRL1_WRED_EN			BIT(8)
#define   QCA8K_PORT_HOL_CTRL1_EG_MIRROR_EN		BIT(16)

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

enum qca8k_vlan_cmd {
	QCA8K_VLAN_FLUSH = 1,
	QCA8K_VLAN_LOAD = 2,
	QCA8K_VLAN_PURGE = 3,
	QCA8K_VLAN_REMOVE_PORT = 4,
	QCA8K_VLAN_NEXT = 5,
	QCA8K_VLAN_READ = 6,
};

struct ar8xxx_port_status {
	int enabled;
};

struct qca8k_match_data {
	u8 id;
	bool reduced_package;
};

enum {
	QCA8K_CPU_PORT0,
	QCA8K_CPU_PORT6,
};

struct qca8k_ports_config {
	bool sgmii_rx_clk_falling_edge;
	bool sgmii_tx_clk_falling_edge;
	bool sgmii_enable_pll;
	u8 rgmii_rx_delay[QCA8K_NUM_CPU_PORTS]; /* 0: CPU port0, 1: CPU port6 */
	u8 rgmii_tx_delay[QCA8K_NUM_CPU_PORTS]; /* 0: CPU port0, 1: CPU port6 */
};

struct qca8k_priv {
	u8 switch_id;
	u8 switch_revision;
	bool legacy_phy_port_mapping;
	struct qca8k_ports_config ports_config;
	struct regmap *regmap;
	struct mii_bus *bus;
	struct ar8xxx_port_status port_sts[QCA8K_NUM_PORTS];
	struct dsa_switch *ds;
	struct mutex reg_mutex;
	struct device *dev;
	struct dsa_switch_ops ops;
	struct gpio_desc *reset_gpio;
	unsigned int port_mtu[QCA8K_NUM_PORTS];
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
