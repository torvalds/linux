/*
 *	Configuration OSM
 *
 *	Copyright (C) 2005	Markus Lidel <Markus.Lidel@shadowconnect.com>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation; either version 2 of the License, or (at your
 *	option) any later version.
 *
 *	Fixes/additions:
 *		Markus Lidel <Markus.Lidel@shadowconnect.com>
 *			initial version.
 */

#include <linux/module.h>
#include "i2o.h"
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/fs.h>

#include <asm/uaccess.h>

#define OSM_NAME	"config-osm"
#define OSM_VERSION	"1.323"
#define OSM_DESCRIPTION	"I2O Configuration OSM"

/* access mode user rw */
#define S_IWRSR (S_IRUSR | S_IWUSR)

static struct i2o_driver i2o_config_driver;

/* Config OSM driver struct */
static struct i2o_driver i2o_config_driver = {
	.name = OSM_NAME,
};

#ifdef CONFIG_I2O_CONFIG_OLD_IOCTL
#include "i2o_config.c"
#endif

/**
 *	i2o_config_init - Configuration OSM initialization function
 *
 *	Registers Configuration OSM in the I2O core and if old ioctl's are
 *	compiled in initialize them.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int __init i2o_config_init(void)
{
	printk(KERN_INFO OSM_DESCRIPTION " v" OSM_VERSION "\n");

	if (i2o_driver_register(&i2o_config_driver)) {
		osm_err("handler register failed.\n");
		return -EBUSY;
	}
#ifdef CONFIG_I2O_CONFIG_OLD_IOCTL
	if (i2o_config_old_init()) {
		osm_err("old config handler initialization failed\n");
		i2o_driver_unregister(&i2o_config_driver);
		return -EBUSY;
	}
#endif

	return 0;
}

/**
 *	i2o_config_exit - Configuration OSM exit function
 *
 *	If old ioctl's are compiled in exit remove them and unregisters
 *	Configuration OSM from I2O core.
 */
static void i2o_config_exit(void)
{
#ifdef CONFIG_I2O_CONFIG_OLD_IOCTL
	i2o_config_old_exit();
#endif

	i2o_driver_unregister(&i2o_config_driver);
}

MODULE_AUTHOR("Markus Lidel <Markus.Lidel@shadowconnect.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(OSM_DESCRIPTION);
MODULE_VERSION(OSM_VERSION);

module_init(i2o_config_init);
module_exit(i2o_config_exit);
