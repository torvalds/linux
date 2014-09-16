/*
 * drivers/net/phy/tlk106.c
 *
 * Driver for Texas Instruments TLK106 PHYs
 *
 * Author: Alex Eaton
 *
 * Copyright (c) 2014 Broadband Antenna Tracking Systems
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Support : tlk106 100/10 phy from Texas Instruments
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/tlk106_phy.h>

#define TLK106_PHYSCR			0x11	/* PHY Specific Control Register */
#define TLK106_MISR1			0x12	/* MII Interrupt Status Register 1  */
#define TLK106_MISR2			0x13	/* MII Interrupt Status Register 2  */

static int tlk106_ack_interrupt(struct phy_device *phydev)
{
	int temp;
	
	/* Read and clear interrupts */
	temp = phy_read(phydev, TLK106_MISR1);
	temp = phy_read(phydev, TLK106_MISR2);

	return(0);
}

static int tlk106_config_intr(struct phy_device *phydev)
{
	int temp, rc;

	if(PHY_INTERRUPT_ENABLED == phydev->interrupts)
	{
		temp = phy_read(phydev, TLK106_PHYSCR);
		temp |= 0x0010;	/* Set Interrupt Pin as Active Low */
		temp |= 0x0002;	/* Enable Interrupts */
		temp |= 0x0001;	/* Enable Interrupt Output Pin */
		rc = phy_write(phydev, TLK106_PHYSCR, temp);
		
		if(rc < 0)
		{
			return(rc);
		}
		
		temp = phy_read(phydev, TLK106_MISR1);
		temp |= 0x0020;	/* Enable Interrupt for Change of Link Status */
		temp |= 0x0010;	/* Enable Interrupt for Change of Speed */
		temp |= 0x0008;	/* Enable Interrupt for Change of Duplex Mode */
		temp |= 0x0004;	/* Enable Interrupt for Auto Negotiation Completed */
		temp |= 0x0002;	/* Enable Interrupt for False Carrier Counter Register half-full event */
		temp |= 0x0001;	/* Enable Interrupt for Receiver Error Counter Register half-full event */
		rc = phy_write(phydev, TLK106_MISR1, temp);
		
		if(rc < 0)
		{
			return(rc);
		}
		
		temp = phy_read(phydev, TLK106_MISR2);
		temp |= 0x0040;	/* Enable Interrupt for Auto-Negotiation error event */
		temp |= 0x0020;	/* Enable Interrupt for Page receive event */
		temp |= 0x0010;	/* Enable Interrupt for Loopback FIFO overflow/underflow event */
		temp |= 0x0008;	/* Enable Interrupt for Change of MDI/X status */
		temp |= 0x0004;	/* Enable Interrupt for Sleep mode event */
		temp |= 0x0002;	/* Enable Interrupt for Change of polarity status */
		temp |= 0x0001;	/* Enable Interrupt for Jabber detection event */
		rc = phy_write(phydev, TLK106_MISR2, temp);
		
		if(rc < 0)
		{
			return(rc);
		}
	}
	else
	{
		temp = phy_read(phydev, TLK106_PHYSCR);
		temp |= 0x0010;		/* Set Interrupt Pin as Active Low */
		temp &= ~0x0002;	/* Disable Interrupts */
		temp &= ~0x0001;	/* Disable Interrupt Output Pin */
		rc = phy_write(phydev, TLK106_PHYSCR, temp);
		
		if(rc < 0)
		{
			return(rc);
		}
		
		temp = phy_read(phydev, TLK106_MISR1);
		temp &= ~0x0020;	/* Disable Interrupt for Change of Link Status */
		temp &= ~0x0010;	/* Disable Interrupt for Change of Speed */
		temp &= ~0x0008;	/* Disable Interrupt for Change of Duplex Mode */
		temp &= ~0x0004;	/* Disable Interrupt for Auto Negotiation Completed */
		temp &= ~0x0002;	/* Disable Interrupt for False Carrier Counter Register half-full event */
		temp &= ~0x0001;	/* Disable Interrupt for Receiver Error Counter Register half-full event */
		rc = phy_write(phydev, TLK106_MISR1, temp);
		
		if(rc < 0)
		{
			return(rc);
		}
		
		temp = phy_read(phydev, TLK106_MISR2);
		temp &= ~0x0040;	/* Disable Interrupt for Auto-Negotiation error event */
		temp &= ~0x0020;	/* Disable Interrupt for Page receive event */
		temp &= ~0x0010;	/* Disable Interrupt for Loopback FIFO overflow/underflow event */
		temp &= ~0x0008;	/* Disable Interrupt for Change of MDI/X status */
		temp &= ~0x0004;	/* Disable Interrupt for Sleep mode event */
		temp &= ~0x0002;	/* Disable Interrupt for Change of polarity status */
		temp &= ~0x0001;	/* Disable Interrupt for Jabber detection event */
		rc = phy_write(phydev, TLK106_MISR2, temp);
		
		if(rc < 0)
		{
			return(rc);
		}
	}
	
	return(0);
}

static int tlk106_config_init(struct phy_device *phydev)
{
	return 0;
}

static struct phy_driver tlk106_0_driver = {
	.phy_id			= PHY_ID_TLK106_0,
	.phy_id_mask	= TLK106_PHY_ID_MASK,
	.name			= "Texas Instruments TLK106 PHY Rev 0",
	.features		= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags			= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= tlk106_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= tlk106_ack_interrupt,
	.config_intr	= tlk106_config_intr,
	.suspend		= genphy_suspend,
	.resume			= genphy_resume,
	.driver			= { .owner = THIS_MODULE, },
};

static struct phy_driver tlk106_1_driver = {
	.phy_id			= PHY_ID_TLK106_1,
	.phy_id_mask	= TLK106_PHY_ID_MASK,
	.name			= "Texas Instruments TLK106 PHY Rev 1",
	.features		= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags			= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= tlk106_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= tlk106_ack_interrupt,
	.config_intr	= tlk106_config_intr,
	.suspend		= genphy_suspend,
	.resume			= genphy_resume,
	.driver			= { .owner = THIS_MODULE, },
};

static struct phy_driver tlk106_2_driver = {
	.phy_id			= PHY_ID_TLK106_2,
	.phy_id_mask	= TLK106_PHY_ID_MASK,
	.name			= "Texas Instruments TLK106 PHY Rev 2",
	.features		= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags			= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= tlk106_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= tlk106_ack_interrupt,
	.config_intr	= tlk106_config_intr,
	.suspend		= genphy_suspend,
	.resume			= genphy_resume,
	.driver			= { .owner = THIS_MODULE, },
};

static struct phy_driver tlk106_3_driver = {
	.phy_id			= PHY_ID_TLK106_3,
	.phy_id_mask	= TLK106_PHY_ID_MASK,
	.name			= "Texas Instruments TLK106 PHY Rev 3",
	.features		= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags			= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= tlk106_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= tlk106_ack_interrupt,
	.config_intr	= tlk106_config_intr,
	.suspend		= genphy_suspend,
	.resume			= genphy_resume,
	.driver			= { .owner = THIS_MODULE, },
};

static struct phy_driver tlk106_4_driver = {
	.phy_id			= PHY_ID_TLK106_4,
	.phy_id_mask	= TLK106_PHY_ID_MASK,
	.name			= "Texas Instruments TLK106 PHY Rev 4",
	.features		= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags			= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= tlk106_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= tlk106_ack_interrupt,
	.config_intr	= tlk106_config_intr,
	.suspend		= genphy_suspend,
	.resume			= genphy_resume,
	.driver			= { .owner = THIS_MODULE, },
};

static struct phy_driver tlk106_5_driver = {
	.phy_id			= PHY_ID_TLK106_5,
	.phy_id_mask	= TLK106_PHY_ID_MASK,
	.name			= "Texas Instruments TLK106 PHY Rev 5",
	.features		= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags			= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= tlk106_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= tlk106_ack_interrupt,
	.config_intr	= tlk106_config_intr,
	.suspend		= genphy_suspend,
	.resume			= genphy_resume,
	.driver			= { .owner = THIS_MODULE, },
};

static int __init tlk106_init(void)
{
	int ret;

	ret = phy_driver_register(&tlk106_0_driver);
	if (ret)
		goto err0;
	ret = phy_driver_register(&tlk106_1_driver);
	if (ret)
		goto err1;
	ret = phy_driver_register(&tlk106_2_driver);
	if (ret)
		goto err2;
	ret = phy_driver_register(&tlk106_3_driver);
	if (ret)
		goto err3;
	ret = phy_driver_register(&tlk106_4_driver);
	if (ret)
		goto err4;
	ret = phy_driver_register(&tlk106_5_driver);
	if (ret)
		goto err5;

	return 0;

err5:
	phy_driver_unregister(&tlk106_4_driver);
err4:
	phy_driver_unregister(&tlk106_3_driver);
err3:
	phy_driver_unregister(&tlk106_2_driver);
err2:
	phy_driver_unregister(&tlk106_1_driver);
err1:
	phy_driver_unregister(&tlk106_0_driver);
err0:
	return ret;
}

static void __exit tlk106_exit(void)
{
	phy_driver_unregister(&tlk106_0_driver);
	phy_driver_unregister(&tlk106_1_driver);
	phy_driver_unregister(&tlk106_2_driver);
	phy_driver_unregister(&tlk106_3_driver);
	phy_driver_unregister(&tlk106_4_driver);
	phy_driver_unregister(&tlk106_5_driver);
}

module_init(tlk106_init);
module_exit(tlk106_exit);

MODULE_DESCRIPTION("TLK106 PHY driver");
MODULE_AUTHOR("Alex Eaton");
MODULE_LICENSE("GPL");

static struct mdio_device_id __maybe_unused tlk106_tbl[] = {
	{ PHY_ID_TLK106_0, TLK106_PHY_ID_MASK },
	{ PHY_ID_TLK106_1, TLK106_PHY_ID_MASK },
	{ PHY_ID_TLK106_2, TLK106_PHY_ID_MASK },
	{ PHY_ID_TLK106_3, TLK106_PHY_ID_MASK },
	{ PHY_ID_TLK106_4, TLK106_PHY_ID_MASK },
	{ PHY_ID_TLK106_5, TLK106_PHY_ID_MASK },
	{ }
};

MODULE_DEVICE_TABLE(mdio, tlk106_tbl);
