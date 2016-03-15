/*
 * Driver for the Texas Instruments DP83848 PHY
 *
 * Copyright (C) 2015 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/phy.h>

#define DP83848_PHY_ID			0x20005c90

/* Registers */
#define DP83848_MICR			0x11
#define DP83848_MISR			0x12

/* MICR Register Fields */
#define DP83848_MICR_INT_OE		BIT(0) /* Interrupt Output Enable */
#define DP83848_MICR_INTEN		BIT(1) /* Interrupt Enable */

/* MISR Register Fields */
#define DP83848_MISR_RHF_INT_EN		BIT(0) /* Receive Error Counter */
#define DP83848_MISR_FHF_INT_EN		BIT(1) /* False Carrier Counter */
#define DP83848_MISR_ANC_INT_EN		BIT(2) /* Auto-negotiation complete */
#define DP83848_MISR_DUP_INT_EN		BIT(3) /* Duplex Status */
#define DP83848_MISR_SPD_INT_EN		BIT(4) /* Speed status */
#define DP83848_MISR_LINK_INT_EN	BIT(5) /* Link status */
#define DP83848_MISR_ED_INT_EN		BIT(6) /* Energy detect */
#define DP83848_MISR_LQM_INT_EN		BIT(7) /* Link Quality Monitor */

static int dp83848_ack_interrupt(struct phy_device *phydev)
{
	int err = phy_read(phydev, DP83848_MISR);

	return err < 0 ? err : 0;
}

static int dp83848_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = phy_write(phydev, DP83848_MICR,
				DP83848_MICR_INT_OE |
				DP83848_MICR_INTEN);
		if (err < 0)
			return err;

		return phy_write(phydev, DP83848_MISR,
				 DP83848_MISR_ANC_INT_EN |
				 DP83848_MISR_DUP_INT_EN |
				 DP83848_MISR_SPD_INT_EN |
				 DP83848_MISR_LINK_INT_EN);
	}

	return phy_write(phydev, DP83848_MICR, 0x0);
}

static struct mdio_device_id __maybe_unused dp83848_tbl[] = {
	{ DP83848_PHY_ID, 0xfffffff0 },
	{ }
};
MODULE_DEVICE_TABLE(mdio, dp83848_tbl);

static struct phy_driver dp83848_driver[] = {
	{
		.phy_id		= DP83848_PHY_ID,
		.phy_id_mask	= 0xfffffff0,
		.name		= "TI DP83848",
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,

		.soft_reset	= genphy_soft_reset,
		.config_init	= genphy_config_init,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,

		/* IRQ related */
		.ack_interrupt	= dp83848_ack_interrupt,
		.config_intr	= dp83848_config_intr,
	},
};
module_phy_driver(dp83848_driver);

MODULE_DESCRIPTION("Texas Instruments DP83848 PHY driver");
MODULE_AUTHOR("Andrew F. Davis <afd@ti.com");
MODULE_LICENSE("GPL");
