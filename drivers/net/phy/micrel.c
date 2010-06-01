/*
 * drivers/net/phy/micrel.c
 *
 * Driver for Micrel PHYs
 *
 * Author: David J. Choi
 *
 * Copyright (c) 2010 Micrel, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Support : ksz9021 , vsc8201, ks8001
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>

#define	PHY_ID_KSZ9021			0x00221611
#define	PHY_ID_VSC8201			0x000FC413
#define	PHY_ID_KS8001			0x0022161A


static int kszphy_config_init(struct phy_device *phydev)
{
	return 0;
}


static struct phy_driver ks8001_driver = {
	.phy_id		= PHY_ID_KS8001,
	.name		= "Micrel KS8001",
	.phy_id_mask	= 0x00fffff0,
	.features	= PHY_BASIC_FEATURES,
	.flags		= PHY_POLL,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.driver		= { .owner = THIS_MODULE,},
};

static struct phy_driver vsc8201_driver = {
	.phy_id		= PHY_ID_VSC8201,
	.name		= "Micrel VSC8201",
	.phy_id_mask	= 0x00fffff0,
	.features	= PHY_BASIC_FEATURES,
	.flags		= PHY_POLL,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.driver		= { .owner = THIS_MODULE,},
};

static struct phy_driver ksz9021_driver = {
	.phy_id		= PHY_ID_KSZ9021,
	.phy_id_mask	= 0x000fff10,
	.name		= "Micrel KSZ9021 Gigabit PHY",
	.features	= PHY_GBIT_FEATURES | SUPPORTED_Pause,
	.flags		= PHY_POLL,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.driver		= { .owner = THIS_MODULE, },
};

static int __init ksphy_init(void)
{
	int ret;

	ret = phy_driver_register(&ks8001_driver);
	if (ret)
		goto err1;
	ret = phy_driver_register(&vsc8201_driver);
	if (ret)
		goto err2;

	ret = phy_driver_register(&ksz9021_driver);
	if (ret)
		goto err3;
	return 0;

err3:
	phy_driver_unregister(&vsc8201_driver);
err2:
	phy_driver_unregister(&ks8001_driver);
err1:
	return ret;
}

static void __exit ksphy_exit(void)
{
	phy_driver_unregister(&ks8001_driver);
	phy_driver_unregister(&vsc8201_driver);
	phy_driver_unregister(&ksz9021_driver);
}

module_init(ksphy_init);
module_exit(ksphy_exit);

MODULE_DESCRIPTION("Micrel PHY driver");
MODULE_AUTHOR("David J. Choi");
MODULE_LICENSE("GPL");

static struct mdio_device_id micrel_tbl[] = {
	{ PHY_ID_KSZ9021, 0x000fff10 },
	{ PHY_ID_VSC8201, 0x00fffff0 },
	{ PHY_ID_KS8001, 0x00fffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, micrel_tbl);
