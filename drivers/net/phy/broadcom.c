/*
 *	drivers/net/phy/broadcom.c
 *
 *	Broadcom BCM5411, BCM5421 and BCM5461 Gigabit Ethernet
 *	transceivers.
 *
 *	Copyright (c) 2006  Maciej W. Rozycki
 *
 *	Inspired by code written by Amy Fong.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/phy.h>

#define MII_BCM54XX_ECR		0x10	/* BCM54xx extended control register */
#define MII_BCM54XX_ECR_IM	0x1000	/* Interrupt mask */
#define MII_BCM54XX_ECR_IF	0x0800	/* Interrupt force */

#define MII_BCM54XX_ESR		0x11	/* BCM54xx extended status register */
#define MII_BCM54XX_ESR_IS	0x1000	/* Interrupt status */

#define MII_BCM54XX_ISR		0x1a	/* BCM54xx interrupt status register */
#define MII_BCM54XX_IMR		0x1b	/* BCM54xx interrupt mask register */
#define MII_BCM54XX_INT_CRCERR	0x0001	/* CRC error */
#define MII_BCM54XX_INT_LINK	0x0002	/* Link status changed */
#define MII_BCM54XX_INT_SPEED	0x0004	/* Link speed change */
#define MII_BCM54XX_INT_DUPLEX	0x0008	/* Duplex mode changed */
#define MII_BCM54XX_INT_LRS	0x0010	/* Local receiver status changed */
#define MII_BCM54XX_INT_RRS	0x0020	/* Remote receiver status changed */
#define MII_BCM54XX_INT_SSERR	0x0040	/* Scrambler synchronization error */
#define MII_BCM54XX_INT_UHCD	0x0080	/* Unsupported HCD negotiated */
#define MII_BCM54XX_INT_NHCD	0x0100	/* No HCD */
#define MII_BCM54XX_INT_NHCDL	0x0200	/* No HCD link */
#define MII_BCM54XX_INT_ANPR	0x0400	/* Auto-negotiation page received */
#define MII_BCM54XX_INT_LC	0x0800	/* All counters below 128 */
#define MII_BCM54XX_INT_HC	0x1000	/* Counter above 32768 */
#define MII_BCM54XX_INT_MDIX	0x2000	/* MDIX status change */
#define MII_BCM54XX_INT_PSERR	0x4000	/* Pair swap error */

MODULE_DESCRIPTION("Broadcom PHY driver");
MODULE_AUTHOR("Maciej W. Rozycki");
MODULE_LICENSE("GPL");

static int bcm54xx_config_init(struct phy_device *phydev)
{
	int reg, err;

	reg = phy_read(phydev, MII_BCM54XX_ECR);
	if (reg < 0)
		return reg;

	/* Mask interrupts globally.  */
	reg |= MII_BCM54XX_ECR_IM;
	err = phy_write(phydev, MII_BCM54XX_ECR, reg);
	if (err < 0)
		return err;

	/* Unmask events we are interested in.  */
	reg = ~(MII_BCM54XX_INT_DUPLEX |
		MII_BCM54XX_INT_SPEED |
		MII_BCM54XX_INT_LINK);
	err = phy_write(phydev, MII_BCM54XX_IMR, reg);
	if (err < 0)
		return err;
	return 0;
}

static int bcm54xx_ack_interrupt(struct phy_device *phydev)
{
	int reg;

	/* Clear pending interrupts.  */
	reg = phy_read(phydev, MII_BCM54XX_ISR);
	if (reg < 0)
		return reg;

	return 0;
}

static int bcm54xx_config_intr(struct phy_device *phydev)
{
	int reg, err;

	reg = phy_read(phydev, MII_BCM54XX_ECR);
	if (reg < 0)
		return reg;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		reg &= ~MII_BCM54XX_ECR_IM;
	else
		reg |= MII_BCM54XX_ECR_IM;

	err = phy_write(phydev, MII_BCM54XX_ECR, reg);
	return err;
}

static struct phy_driver bcm5411_driver = {
	.phy_id		= 0x00206070,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5411",
	.features	= PHY_GBIT_FEATURES,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= bcm54xx_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= bcm54xx_ack_interrupt,
	.config_intr	= bcm54xx_config_intr,
	.driver 	= { .owner = THIS_MODULE },
};

static struct phy_driver bcm5421_driver = {
	.phy_id		= 0x002060e0,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5421",
	.features	= PHY_GBIT_FEATURES,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= bcm54xx_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= bcm54xx_ack_interrupt,
	.config_intr	= bcm54xx_config_intr,
	.driver 	= { .owner = THIS_MODULE },
};

static struct phy_driver bcm5461_driver = {
	.phy_id		= 0x002060c0,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5461",
	.features	= PHY_GBIT_FEATURES,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= bcm54xx_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= bcm54xx_ack_interrupt,
	.config_intr	= bcm54xx_config_intr,
	.driver 	= { .owner = THIS_MODULE },
};

static int __init broadcom_init(void)
{
	int ret;

	ret = phy_driver_register(&bcm5411_driver);
	if (ret)
		goto out_5411;
	ret = phy_driver_register(&bcm5421_driver);
	if (ret)
		goto out_5421;
	ret = phy_driver_register(&bcm5461_driver);
	if (ret)
		goto out_5461;
	return ret;

out_5461:
	phy_driver_unregister(&bcm5421_driver);
out_5421:
	phy_driver_unregister(&bcm5411_driver);
out_5411:
	return ret;
}

static void __exit broadcom_exit(void)
{
	phy_driver_unregister(&bcm5461_driver);
	phy_driver_unregister(&bcm5421_driver);
	phy_driver_unregister(&bcm5411_driver);
}

module_init(broadcom_init);
module_exit(broadcom_exit);
