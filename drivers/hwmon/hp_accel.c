/*
 *  hp_accel.c - Interface between LIS3LV02DL driver and HP ACPI BIOS
 *
 *  Copyright (C) 2007-2008 Yan Burman
 *  Copyright (C) 2008 Eric Piel
 *  Copyright (C) 2008 Pavel Machek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/freezer.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include <acpi/acpi_drivers.h>
#include <asm/atomic.h>
#include "lis3lv02d.h"

#define DRIVER_NAME     "lis3lv02d"
#define ACPI_MDPS_CLASS "accelerometer"


/* For automatic insertion of the module */
static struct acpi_device_id lis3lv02d_device_ids[] = {
	{"HPQ0004", 0}, /* HP Mobile Data Protection System PNP */
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, lis3lv02d_device_ids);


/**
 * lis3lv02d_acpi_init - ACPI _INI method: initialize the device.
 * @handle: the handle of the device
 *
 * Returns AE_OK on success.
 */
acpi_status lis3lv02d_acpi_init(acpi_handle handle)
{
	return acpi_evaluate_object(handle, METHOD_NAME__INI, NULL, NULL);
}

/**
 * lis3lv02d_acpi_read - ACPI ALRD method: read a register
 * @handle: the handle of the device
 * @reg:    the register to read
 * @ret:    result of the operation
 *
 * Returns AE_OK on success.
 */
acpi_status lis3lv02d_acpi_read(acpi_handle handle, int reg, u8 *ret)
{
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };
	unsigned long long lret;
	acpi_status status;

	arg0.integer.value = reg;

	status = acpi_evaluate_integer(handle, "ALRD", &args, &lret);
	*ret = lret;
	return status;
}

/**
 * lis3lv02d_acpi_write - ACPI ALWR method: write to a register
 * @handle: the handle of the device
 * @reg:    the register to write to
 * @val:    the value to write
 *
 * Returns AE_OK on success.
 */
acpi_status lis3lv02d_acpi_write(acpi_handle handle, int reg, u8 val)
{
	unsigned long long ret; /* Not used when writting */
	union acpi_object in_obj[2];
	struct acpi_object_list args = { 2, in_obj };

	in_obj[0].type          = ACPI_TYPE_INTEGER;
	in_obj[0].integer.value = reg;
	in_obj[1].type          = ACPI_TYPE_INTEGER;
	in_obj[1].integer.value = val;

	return acpi_evaluate_integer(handle, "ALWR", &args, &ret);
}

static int lis3lv02d_dmi_matched(const struct dmi_system_id *dmi)
{
	adev.ac = *((struct axis_conversion *)dmi->driver_data);
	printk(KERN_INFO DRIVER_NAME ": hardware type %s found.\n", dmi->ident);

	return 1;
}

/* Represents, for each axis seen by userspace, the corresponding hw axis (+1).
 * If the value is negative, the opposite of the hw value is used. */
static struct axis_conversion lis3lv02d_axis_normal = {1, 2, 3};
static struct axis_conversion lis3lv02d_axis_y_inverted = {1, -2, 3};
static struct axis_conversion lis3lv02d_axis_x_inverted = {-1, 2, 3};
static struct axis_conversion lis3lv02d_axis_z_inverted = {1, 2, -3};
static struct axis_conversion lis3lv02d_axis_xy_rotated_left = {-2, 1, 3};
static struct axis_conversion lis3lv02d_axis_xy_swap_inverted = {-2, -1, 3};

#define AXIS_DMI_MATCH(_ident, _name, _axis) {		\
	.ident = _ident,				\
	.callback = lis3lv02d_dmi_matched,		\
	.matches = {					\
		DMI_MATCH(DMI_PRODUCT_NAME, _name)	\
	},						\
	.driver_data = &lis3lv02d_axis_##_axis		\
}
static struct dmi_system_id lis3lv02d_dmi_ids[] = {
	/* product names are truncated to match all kinds of a same model */
	AXIS_DMI_MATCH("NC64x0", "HP Compaq nc64", x_inverted),
	AXIS_DMI_MATCH("NC84x0", "HP Compaq nc84", z_inverted),
	AXIS_DMI_MATCH("NX9420", "HP Compaq nx9420", x_inverted),
	AXIS_DMI_MATCH("NW9440", "HP Compaq nw9440", x_inverted),
	AXIS_DMI_MATCH("NC2510", "HP Compaq 2510", y_inverted),
	AXIS_DMI_MATCH("NC8510", "HP Compaq 8510", xy_swap_inverted),
	AXIS_DMI_MATCH("HP2133", "HP 2133", xy_rotated_left),
	{ NULL, }
/* Laptop models without axis info (yet):
 * "NC651xx" "HP Compaq 651"
 * "NC671xx" "HP Compaq 671"
 * "NC6910" "HP Compaq 6910"
 * HP Compaq 8710x Notebook PC / Mobile Workstation
 * "NC2400" "HP Compaq nc2400"
 * "NX74x0" "HP Compaq nx74"
 * "NX6325" "HP Compaq nx6325"
 * "NC4400" "HP Compaq nc4400"
 */
};


static int lis3lv02d_add(struct acpi_device *device)
{
	u8 val;

	if (!device)
		return -EINVAL;

	adev.device = device;
	adev.init = lis3lv02d_acpi_init;
	adev.read = lis3lv02d_acpi_read;
	adev.write = lis3lv02d_acpi_write;
	strcpy(acpi_device_name(device), DRIVER_NAME);
	strcpy(acpi_device_class(device), ACPI_MDPS_CLASS);
	device->driver_data = &adev;

	lis3lv02d_acpi_read(device->handle, WHO_AM_I, &val);
	if ((val != LIS3LV02DL_ID) && (val != LIS302DL_ID)) {
		printk(KERN_ERR DRIVER_NAME
				": Accelerometer chip not LIS3LV02D{L,Q}\n");
	}

	/* If possible use a "standard" axes order */
	if (dmi_check_system(lis3lv02d_dmi_ids) == 0) {
		printk(KERN_INFO DRIVER_NAME ": laptop model unknown, "
				 "using default axes configuration\n");
		adev.ac = lis3lv02d_axis_normal;
	}

	return lis3lv02d_init_device(&adev);
}

static int lis3lv02d_remove(struct acpi_device *device, int type)
{
	if (!device)
		return -EINVAL;

	lis3lv02d_joystick_disable();
	lis3lv02d_poweroff(device->handle);

	return lis3lv02d_remove_fs();
}


#ifdef CONFIG_PM
static int lis3lv02d_suspend(struct acpi_device *device, pm_message_t state)
{
	/* make sure the device is off when we suspend */
	lis3lv02d_poweroff(device->handle);
	return 0;
}

static int lis3lv02d_resume(struct acpi_device *device)
{
	/* put back the device in the right state (ACPI might turn it on) */
	mutex_lock(&adev.lock);
	if (adev.usage > 0)
		lis3lv02d_poweron(device->handle);
	else
		lis3lv02d_poweroff(device->handle);
	mutex_unlock(&adev.lock);
	return 0;
}
#else
#define lis3lv02d_suspend NULL
#define lis3lv02d_resume NULL
#endif

/* For the HP MDPS aka 3D Driveguard */
static struct acpi_driver lis3lv02d_driver = {
	.name  = DRIVER_NAME,
	.class = ACPI_MDPS_CLASS,
	.ids   = lis3lv02d_device_ids,
	.ops = {
		.add     = lis3lv02d_add,
		.remove  = lis3lv02d_remove,
		.suspend = lis3lv02d_suspend,
		.resume  = lis3lv02d_resume,
	}
};

static int __init lis3lv02d_init_module(void)
{
	int ret;

	if (acpi_disabled)
		return -ENODEV;

	ret = acpi_bus_register_driver(&lis3lv02d_driver);
	if (ret < 0)
		return ret;

	printk(KERN_INFO DRIVER_NAME " driver loaded.\n");

	return 0;
}

static void __exit lis3lv02d_exit_module(void)
{
	acpi_bus_unregister_driver(&lis3lv02d_driver);
}

MODULE_DESCRIPTION("Glue between LIS3LV02Dx and HP ACPI BIOS");
MODULE_AUTHOR("Yan Burman, Eric Piel, Pavel Machek");
MODULE_LICENSE("GPL");

module_init(lis3lv02d_init_module);
module_exit(lis3lv02d_exit_module);

