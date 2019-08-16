// SPDX-License-Identifier: GPL-2.0+
/**
 *  Driver for Analog Devices Industrial Ethernet PHYs
 *
 * Copyright 2019 Analog Devices Inc.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/phy.h>

#define PHY_ID_ADIN1200				0x0283bc20
#define PHY_ID_ADIN1300				0x0283bc30

#define ADIN1300_INT_MASK_REG			0x0018
#define   ADIN1300_INT_MDIO_SYNC_EN		BIT(9)
#define   ADIN1300_INT_ANEG_STAT_CHNG_EN	BIT(8)
#define   ADIN1300_INT_ANEG_PAGE_RX_EN		BIT(6)
#define   ADIN1300_INT_IDLE_ERR_CNT_EN		BIT(5)
#define   ADIN1300_INT_MAC_FIFO_OU_EN		BIT(4)
#define   ADIN1300_INT_RX_STAT_CHNG_EN		BIT(3)
#define   ADIN1300_INT_LINK_STAT_CHNG_EN	BIT(2)
#define   ADIN1300_INT_SPEED_CHNG_EN		BIT(1)
#define   ADIN1300_INT_HW_IRQ_EN		BIT(0)
#define ADIN1300_INT_MASK_EN	\
	(ADIN1300_INT_LINK_STAT_CHNG_EN | ADIN1300_INT_HW_IRQ_EN)
#define ADIN1300_INT_STATUS_REG			0x0019

static int adin_config_init(struct phy_device *phydev)
{
	return genphy_config_init(phydev);
}

static int adin_phy_ack_intr(struct phy_device *phydev)
{
	/* Clear pending interrupts */
	int rc = phy_read(phydev, ADIN1300_INT_STATUS_REG);

	return rc < 0 ? rc : 0;
}

static int adin_phy_config_intr(struct phy_device *phydev)
{
	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		return phy_set_bits(phydev, ADIN1300_INT_MASK_REG,
				    ADIN1300_INT_MASK_EN);

	return phy_clear_bits(phydev, ADIN1300_INT_MASK_REG,
			      ADIN1300_INT_MASK_EN);
}

static struct phy_driver adin_driver[] = {
	{
		PHY_ID_MATCH_MODEL(PHY_ID_ADIN1200),
		.name		= "ADIN1200",
		.config_init	= adin_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.ack_interrupt	= adin_phy_ack_intr,
		.config_intr	= adin_phy_config_intr,
		.resume		= genphy_resume,
		.suspend	= genphy_suspend,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_ADIN1300),
		.name		= "ADIN1300",
		.config_init	= adin_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.ack_interrupt	= adin_phy_ack_intr,
		.config_intr	= adin_phy_config_intr,
		.resume		= genphy_resume,
		.suspend	= genphy_suspend,
	},
};

module_phy_driver(adin_driver);

static struct mdio_device_id __maybe_unused adin_tbl[] = {
	{ PHY_ID_MATCH_MODEL(PHY_ID_ADIN1200) },
	{ PHY_ID_MATCH_MODEL(PHY_ID_ADIN1300) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, adin_tbl);
MODULE_DESCRIPTION("Analog Devices Industrial Ethernet PHY driver");
MODULE_LICENSE("GPL");
