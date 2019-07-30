// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Texas Instruments DP83848 PHY
 *
 * Copyright (C) 2015-2016 Texas Instruments Incorporated - http://www.ti.com/
 */

#include <linux/module.h>
#include <linux/phy.h>

#define TI_DP83848C_PHY_ID		0x20005ca0
#define TI_DP83620_PHY_ID		0x20005ce0
#define NS_DP83848C_PHY_ID		0x20005c90
#define TLK10X_PHY_ID			0x2000a210

/* Registers */
#define DP83848_MICR			0x11 /* MII Interrupt Control Register */
#define DP83848_MISR			0x12 /* MII Interrupt Status Register */

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

#define DP83848_INT_EN_MASK		\
	(DP83848_MISR_ANC_INT_EN |	\
	 DP83848_MISR_DUP_INT_EN |	\
	 DP83848_MISR_SPD_INT_EN |	\
	 DP83848_MISR_LINK_INT_EN)

static int dp83848_ack_interrupt(struct phy_device *phydev)
{
	int err = phy_read(phydev, DP83848_MISR);

	return err < 0 ? err : 0;
}

static int dp83848_config_intr(struct phy_device *phydev)
{
	int control, ret;

	control = phy_read(phydev, DP83848_MICR);
	if (control < 0)
		return control;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		control |= DP83848_MICR_INT_OE;
		control |= DP83848_MICR_INTEN;

		ret = phy_write(phydev, DP83848_MISR, DP83848_INT_EN_MASK);
		if (ret < 0)
			return ret;
	} else {
		control &= ~DP83848_MICR_INTEN;
	}

	return phy_write(phydev, DP83848_MICR, control);
}

static int dp83848_config_init(struct phy_device *phydev)
{
	int err;
	int val;

	err = genphy_config_init(phydev);
	if (err < 0)
		return err;

	/* DP83620 always reports Auto Negotiation Ability on BMSR. Instead,
	 * we check initial value of BMCR Auto negotiation enable bit
	 */
	val = phy_read(phydev, MII_BMCR);
	if (!(val & BMCR_ANENABLE))
		phydev->autoneg = AUTONEG_DISABLE;

	return 0;
}

static struct mdio_device_id __maybe_unused dp83848_tbl[] = {
	{ TI_DP83848C_PHY_ID, 0xfffffff0 },
	{ NS_DP83848C_PHY_ID, 0xfffffff0 },
	{ TI_DP83620_PHY_ID, 0xfffffff0 },
	{ TLK10X_PHY_ID, 0xfffffff0 },
	{ }
};
MODULE_DEVICE_TABLE(mdio, dp83848_tbl);

#define DP83848_PHY_DRIVER(_id, _name, _config_init)		\
	{							\
		.phy_id		= _id,				\
		.phy_id_mask	= 0xfffffff0,			\
		.name		= _name,			\
		/* PHY_BASIC_FEATURES */			\
								\
		.soft_reset	= genphy_soft_reset,		\
		.config_init	= _config_init,			\
		.suspend	= genphy_suspend,		\
		.resume		= genphy_resume,		\
								\
		/* IRQ related */				\
		.ack_interrupt	= dp83848_ack_interrupt,	\
		.config_intr	= dp83848_config_intr,		\
	}

static struct phy_driver dp83848_driver[] = {
	DP83848_PHY_DRIVER(TI_DP83848C_PHY_ID, "TI DP83848C 10/100 Mbps PHY",
			   genphy_config_init),
	DP83848_PHY_DRIVER(NS_DP83848C_PHY_ID, "NS DP83848C 10/100 Mbps PHY",
			   genphy_config_init),
	DP83848_PHY_DRIVER(TI_DP83620_PHY_ID, "TI DP83620 10/100 Mbps PHY",
			   dp83848_config_init),
	DP83848_PHY_DRIVER(TLK10X_PHY_ID, "TI TLK10X 10/100 Mbps PHY",
			   genphy_config_init),
};
module_phy_driver(dp83848_driver);

MODULE_DESCRIPTION("Texas Instruments DP83848 PHY driver");
MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_LICENSE("GPL v2");
