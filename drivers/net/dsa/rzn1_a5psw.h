/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Schneider Electric
 *
 * Clément Léger <clement.leger@bootlin.com>
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/platform_device.h>
#include <linux/pcs-rzn1-miic.h>
#include <net/dsa.h>

#define A5PSW_REVISION			0x0
#define A5PSW_PORT_OFFSET(port)		(0x400 * (port))

#define A5PSW_PORT_ENA			0x8
#define A5PSW_PORT_ENA_RX_SHIFT		16
#define A5PSW_PORT_ENA_TX_RX(port)	(BIT((port) + A5PSW_PORT_ENA_RX_SHIFT) | \
					 BIT(port))
#define A5PSW_UCAST_DEF_MASK		0xC

#define A5PSW_VLAN_VERIFY		0x10
#define A5PSW_VLAN_VERI_SHIFT		0
#define A5PSW_VLAN_DISC_SHIFT		16

#define A5PSW_BCAST_DEF_MASK		0x14
#define A5PSW_MCAST_DEF_MASK		0x18

#define A5PSW_INPUT_LEARN		0x1C
#define A5PSW_INPUT_LEARN_DIS(p)	BIT((p) + 16)
#define A5PSW_INPUT_LEARN_BLOCK(p)	BIT(p)

#define A5PSW_MGMT_CFG			0x20
#define A5PSW_MGMT_CFG_DISCARD		BIT(7)

#define A5PSW_MODE_CFG			0x24
#define A5PSW_MODE_STATS_RESET		BIT(31)

#define A5PSW_VLAN_IN_MODE		0x28
#define A5PSW_VLAN_IN_MODE_PORT_SHIFT(port)	((port) * 2)
#define A5PSW_VLAN_IN_MODE_PORT(port)		(GENMASK(1, 0) << \
					A5PSW_VLAN_IN_MODE_PORT_SHIFT(port))
#define A5PSW_VLAN_IN_MODE_SINGLE_PASSTHROUGH	0x0
#define A5PSW_VLAN_IN_MODE_SINGLE_REPLACE	0x1
#define A5PSW_VLAN_IN_MODE_TAG_ALWAYS		0x2

#define A5PSW_VLAN_OUT_MODE		0x2C
#define A5PSW_VLAN_OUT_MODE_PORT(port)	(GENMASK(1, 0) << ((port) * 2))
#define A5PSW_VLAN_OUT_MODE_DIS		0x0
#define A5PSW_VLAN_OUT_MODE_STRIP	0x1
#define A5PSW_VLAN_OUT_MODE_TAG_THROUGH	0x2
#define A5PSW_VLAN_OUT_MODE_TRANSPARENT	0x3

#define A5PSW_VLAN_IN_MODE_ENA		0x30
#define A5PSW_VLAN_TAG_ID		0x34

#define A5PSW_SYSTEM_TAGINFO(port)	(0x200 + A5PSW_PORT_OFFSET(port))

#define A5PSW_AUTH_PORT(port)		(0x240 + 4 * (port))
#define A5PSW_AUTH_PORT_AUTHORIZED	BIT(0)

#define A5PSW_VLAN_RES(entry)		(0x280 + 4 * (entry))
#define A5PSW_VLAN_RES_WR_PORTMASK	BIT(30)
#define A5PSW_VLAN_RES_WR_TAGMASK	BIT(29)
#define A5PSW_VLAN_RES_RD_TAGMASK	BIT(28)
#define A5PSW_VLAN_RES_ID		GENMASK(16, 5)
#define A5PSW_VLAN_RES_PORTMASK		GENMASK(4, 0)

#define A5PSW_RXMATCH_CONFIG(port)	(0x3e80 + 4 * (port))
#define A5PSW_RXMATCH_CONFIG_PATTERN(p)	BIT(p)

#define A5PSW_PATTERN_CTRL(p)		(0x3eb0 + 4  * (p))
#define A5PSW_PATTERN_CTRL_MGMTFWD	BIT(1)

#define A5PSW_LK_CTRL		0x400
#define A5PSW_LK_ADDR_CTRL_BLOCKING	BIT(0)
#define A5PSW_LK_ADDR_CTRL_LEARNING	BIT(1)
#define A5PSW_LK_ADDR_CTRL_AGEING	BIT(2)
#define A5PSW_LK_ADDR_CTRL_ALLOW_MIGR	BIT(3)
#define A5PSW_LK_ADDR_CTRL_CLEAR_TABLE	BIT(6)

#define A5PSW_LK_ADDR_CTRL		0x408
#define A5PSW_LK_ADDR_CTRL_BUSY		BIT(31)
#define A5PSW_LK_ADDR_CTRL_DELETE_PORT	BIT(30)
#define A5PSW_LK_ADDR_CTRL_CLEAR	BIT(29)
#define A5PSW_LK_ADDR_CTRL_LOOKUP	BIT(28)
#define A5PSW_LK_ADDR_CTRL_WAIT		BIT(27)
#define A5PSW_LK_ADDR_CTRL_READ		BIT(26)
#define A5PSW_LK_ADDR_CTRL_WRITE	BIT(25)
#define A5PSW_LK_ADDR_CTRL_ADDRESS	GENMASK(12, 0)

#define A5PSW_LK_DATA_LO		0x40C
#define A5PSW_LK_DATA_HI		0x410
#define A5PSW_LK_DATA_HI_VALID		BIT(16)
#define A5PSW_LK_DATA_HI_PORT		BIT(16)

#define A5PSW_LK_LEARNCOUNT		0x418
#define A5PSW_LK_LEARNCOUNT_COUNT	GENMASK(13, 0)
#define A5PSW_LK_LEARNCOUNT_MODE	GENMASK(31, 30)
#define A5PSW_LK_LEARNCOUNT_MODE_SET	0x0
#define A5PSW_LK_LEARNCOUNT_MODE_INC	0x1
#define A5PSW_LK_LEARNCOUNT_MODE_DEC	0x2

#define A5PSW_MGMT_TAG_CFG		0x480
#define A5PSW_MGMT_TAG_CFG_TAGFIELD	GENMASK(31, 16)
#define A5PSW_MGMT_TAG_CFG_ALL_FRAMES	BIT(1)
#define A5PSW_MGMT_TAG_CFG_ENABLE	BIT(0)

#define A5PSW_LK_AGETIME		0x41C
#define A5PSW_LK_AGETIME_MASK		GENMASK(23, 0)

#define A5PSW_MDIO_CFG_STATUS		0x700
#define A5PSW_MDIO_CFG_STATUS_CLKDIV	GENMASK(15, 7)
#define A5PSW_MDIO_CFG_STATUS_READERR	BIT(1)
#define A5PSW_MDIO_CFG_STATUS_BUSY	BIT(0)

#define A5PSW_MDIO_COMMAND		0x704
/* Register is named TRAININIT in datasheet and should be set when reading */
#define A5PSW_MDIO_COMMAND_READ		BIT(15)
#define A5PSW_MDIO_COMMAND_PHY_ADDR	GENMASK(9, 5)
#define A5PSW_MDIO_COMMAND_REG_ADDR	GENMASK(4, 0)

#define A5PSW_MDIO_DATA			0x708
#define A5PSW_MDIO_DATA_MASK		GENMASK(15, 0)

#define A5PSW_CMD_CFG(port)		(0x808 + A5PSW_PORT_OFFSET(port))
#define A5PSW_CMD_CFG_CNTL_FRM_ENA	BIT(23)
#define A5PSW_CMD_CFG_SW_RESET		BIT(13)
#define A5PSW_CMD_CFG_TX_CRC_APPEND	BIT(11)
#define A5PSW_CMD_CFG_HD_ENA		BIT(10)
#define A5PSW_CMD_CFG_PAUSE_IGNORE	BIT(8)
#define A5PSW_CMD_CFG_CRC_FWD		BIT(6)
#define A5PSW_CMD_CFG_ETH_SPEED		BIT(3)
#define A5PSW_CMD_CFG_RX_ENA		BIT(1)
#define A5PSW_CMD_CFG_TX_ENA		BIT(0)

#define A5PSW_FRM_LENGTH(port)		(0x814 + A5PSW_PORT_OFFSET(port))
#define A5PSW_FRM_LENGTH_MASK		GENMASK(13, 0)

#define A5PSW_STATUS(port)		(0x840 + A5PSW_PORT_OFFSET(port))

#define A5PSW_STATS_HIWORD		0x900

#define A5PSW_VLAN_TAG(prio, id)	(((prio) << 12) | (id))
#define A5PSW_PORTS_NUM			5
#define A5PSW_CPU_PORT			(A5PSW_PORTS_NUM - 1)
#define A5PSW_MDIO_DEF_FREQ		2500000
#define A5PSW_MDIO_TIMEOUT		100
#define A5PSW_JUMBO_LEN			(10 * SZ_1K)
#define A5PSW_MDIO_CLK_DIV_MIN		5
#define A5PSW_TAG_LEN			8
#define A5PSW_VLAN_COUNT		32

/* Ensure enough space for 2 VLAN tags */
#define A5PSW_EXTRA_MTU_LEN		(A5PSW_TAG_LEN + 8)
#define A5PSW_MAX_MTU			(A5PSW_JUMBO_LEN - A5PSW_EXTRA_MTU_LEN)

#define A5PSW_PATTERN_MGMTFWD		0

#define A5PSW_LK_BUSY_USEC_POLL		10
#define A5PSW_CTRL_TIMEOUT		1000
#define A5PSW_TABLE_ENTRIES		8192

/**
 * struct a5psw - switch struct
 * @base: Base address of the switch
 * @hclk: hclk_switch clock
 * @clk: clk_switch clock
 * @dev: Device associated to the switch
 * @mii_bus: MDIO bus struct
 * @mdio_freq: MDIO bus frequency requested
 * @pcs: Array of PCS connected to the switch ports (not for the CPU)
 * @ds: DSA switch struct
 * @lk_lock: Lock for the lookup table
 * @reg_lock: Lock for register read-modify-write operation
 * @bridged_ports: Mask of ports that are bridged and should be flooded
 * @br_dev: Bridge net device
 */
struct a5psw {
	void __iomem *base;
	struct clk *hclk;
	struct clk *clk;
	struct device *dev;
	struct mii_bus	*mii_bus;
	struct phylink_pcs *pcs[A5PSW_PORTS_NUM - 1];
	struct dsa_switch ds;
	struct mutex lk_lock;
	spinlock_t reg_lock;
	u32 bridged_ports;
	struct net_device *br_dev;
};
