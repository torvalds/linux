// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Eurobraille/Iris power off support.
 *
 * Eurobraille's Iris machine is a PC with no APM or ACPI support.
 * It is shutdown by a special I/O sequence which this module provides.
 *
 *  Copyright (C) Shérab <Sebastien.Hinderer@ens-lyon.org>
 */

#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <asm/io.h>

#define IRIS_GIO_BASE		0x340
#define IRIS_GIO_INPUT		IRIS_GIO_BASE
#define IRIS_GIO_OUTPUT		(IRIS_GIO_BASE + 1)
#define IRIS_GIO_PULSE		0x80 /* First byte to send */
#define IRIS_GIO_REST		0x00 /* Second byte to send */
#define IRIS_GIO_NODEV		0xff /* Likely not an Iris */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sébastien Hinderer <Sebastien.Hinderer@ens-lyon.org>");
MODULE_DESCRIPTION("A power_off handler for Iris devices from EuroBraille");
MODULE_SUPPORTED_DEVICE("Eurobraille/Iris");

static bool force;

module_param(force, bool, 0);
MODULE_PARM_DESC(force, "Set to one to force poweroff handler installation.");

static void (*old_pm_power_off)(void);

static void iris_power_off(void)
{
	outb(IRIS_GIO_PULSE, IRIS_GIO_OUTPUT);
	msleep(850);
	outb(IRIS_GIO_REST, IRIS_GIO_OUTPUT);
}

/*
 * Before installing the power_off handler, try to make sure the OS is
 * running on an Iris.  Since Iris does not support DMI, this is done
 * by reading its input port and seeing whether the read value is
 * meaningful.
 */
static int iris_probe(struct platform_device *pdev)
{
	unsigned char status = inb(IRIS_GIO_INPUT);
	if (status == IRIS_GIO_NODEV) {
		printk(KERN_ERR "This machine does not seem to be an Iris. "
			"Power off handler not installed.\n");
		return -ENODEV;
	}
	old_pm_power_off = pm_power_off;
	pm_power_off = &iris_power_off;
	printk(KERN_INFO "Iris power_off handler installed.\n");
	return 0;
}

static int iris_remove(struct platform_device *pdev)
{
	pm_power_off = old_pm_power_off;
	printk(KERN_INFO "Iris power_off handler uninstalled.\n");
	return 0;
}

static struct platform_driver iris_driver = {
	.driver		= {
		.name   = "iris",
	},
	.probe          = iris_probe,
	.remove         = iris_remove,
};

static struct resource iris_resources[] = {
	{
		.start  = IRIS_GIO_BASE,
		.end    = IRIS_GIO_OUTPUT,
		.flags  = IORESOURCE_IO,
		.name   = "address"
	}
};

static struct platform_device *iris_device;

static int iris_init(void)
{
	int ret;
	if (force != 1) {
		printk(KERN_ERR "The force parameter has not been set to 1."
			" The Iris poweroff handler will not be installed.\n");
		return -ENODEV;
	}
	ret = platform_driver_register(&iris_driver);
	if (ret < 0) {
		printk(KERN_ERR "Failed to register iris platform driver: %d\n",
			ret);
		return ret;
	}
	iris_device = platform_device_register_simple("iris", (-1),
				iris_resources, ARRAY_SIZE(iris_resources));
	if (IS_ERR(iris_device)) {
		printk(KERN_ERR "Failed to register iris platform device\n");
		platform_driver_unregister(&iris_driver);
		return PTR_ERR(iris_device);
	}
	return 0;
}

static void iris_exit(void)
{
	platform_device_unregister(iris_device);
	platform_driver_unregister(&iris_driver);
}

module_init(iris_init);
module_exit(iris_exit);
