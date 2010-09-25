/*
 * Eurobraille/Iris power off support.
 *
 * Eurobraille's Iris machine is a PC with no APM or ACPI support.
 * It is shutdown by a special I/O sequence which this module provides.
 *
 *  Copyright (C) Shérab <Sebastien.Hinderer@ens-lyon.org>
 *
 * This program is free software ; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY ; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the program ; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/init.h>
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

static int force;

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
static int iris_init(void)
{
	unsigned char status;
	if (force != 1) {
		printk(KERN_ERR "The force parameter has not been set to 1 so the Iris poweroff handler will not be installed.\n");
		return -ENODEV;
	}
	status = inb(IRIS_GIO_INPUT);
	if (status == IRIS_GIO_NODEV) {
		printk(KERN_ERR "This machine does not seem to be an Iris. Power_off handler not installed.\n");
		return -ENODEV;
	}
	old_pm_power_off = pm_power_off;
	pm_power_off = &iris_power_off;
	printk(KERN_INFO "Iris power_off handler installed.\n");

	return 0;
}

static void iris_exit(void)
{
	pm_power_off = old_pm_power_off;
	printk(KERN_INFO "Iris power_off handler uninstalled.\n");
}

module_init(iris_init);
module_exit(iris_exit);
