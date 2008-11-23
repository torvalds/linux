/*
 *  lis3lv02d.c - ST LIS3LV02DL accelerometer driver
 *
 *  Copyright (C) 2007-2008 Yan Burman
 *  Copyright (C) 2008 Eric Piel
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
#include <linux/uaccess.h>
#include <acpi/acpi_drivers.h>
#include <asm/atomic.h>
#include "lis3lv02d.h"

#define DRIVER_NAME     "lis3lv02d"
#define ACPI_MDPS_CLASS "accelerometer"

/* joystick device poll interval in milliseconds */
#define MDPS_POLL_INTERVAL 50
/*
 * The sensor can also generate interrupts (DRDY) but it's pretty pointless
 * because their are generated even if the data do not change. So it's better
 * to keep the interrupt for the free-fall event. The values are updated at
 * 40Hz (at the lowest frequency), but as it can be pretty time consuming on
 * some low processor, we poll the sensor only at 20Hz... enough for the
 * joystick.
 */

/* Maximum value our axis may get for the input device (signed 12 bits) */
#define MDPS_MAX_VAL 2048

struct axis_conversion {
	s8	x;
	s8	y;
	s8	z;
};

struct acpi_lis3lv02d {
	struct acpi_device	*device;   /* The ACPI device */
	struct input_dev	*idev;     /* input device */
	struct task_struct	*kthread;  /* kthread for input */
	struct mutex            lock;
	struct platform_device	*pdev;     /* platform device */
	atomic_t		count;     /* interrupt count after last read */
	int			xcalib;    /* calibrated null value for x */
	int			ycalib;    /* calibrated null value for y */
	int			zcalib;    /* calibrated null value for z */
	unsigned char		is_on;     /* whether the device is on or off */
	unsigned char		usage;     /* usage counter */
	struct axis_conversion	ac;        /* hw -> logical axis */
};

static struct acpi_lis3lv02d adev;

static int lis3lv02d_remove_fs(void);
static int lis3lv02d_add_fs(struct acpi_device *device);

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
static inline acpi_status lis3lv02d_acpi_init(acpi_handle handle)
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
static acpi_status lis3lv02d_acpi_read(acpi_handle handle, int reg, u8 *ret)
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
static acpi_status lis3lv02d_acpi_write(acpi_handle handle, int reg, u8 val)
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

static s16 lis3lv02d_read_16(acpi_handle handle, int reg)
{
	u8 lo, hi;

	lis3lv02d_acpi_read(handle, reg, &lo);
	lis3lv02d_acpi_read(handle, reg + 1, &hi);
	/* In "12 bit right justified" mode, bit 6, bit 7, bit 8 = bit 5 */
	return (s16)((hi << 8) | lo);
}

/**
 * lis3lv02d_get_axis - For the given axis, give the value converted
 * @axis:      1,2,3 - can also be negative
 * @hw_values: raw values returned by the hardware
 *
 * Returns the converted value.
 */
static inline int lis3lv02d_get_axis(s8 axis, int hw_values[3])
{
	if (axis > 0)
		return hw_values[axis - 1];
	else
		return -hw_values[-axis - 1];
}

/**
 * lis3lv02d_get_xyz - Get X, Y and Z axis values from the accelerometer
 * @handle: the handle to the device
 * @x:      where to store the X axis value
 * @y:      where to store the Y axis value
 * @z:      where to store the Z axis value
 *
 * Note that 40Hz input device can eat up about 10% CPU at 800MHZ
 */
static void lis3lv02d_get_xyz(acpi_handle handle, int *x, int *y, int *z)
{
	int position[3];

	position[0] = lis3lv02d_read_16(handle, OUTX_L);
	position[1] = lis3lv02d_read_16(handle, OUTY_L);
	position[2] = lis3lv02d_read_16(handle, OUTZ_L);

	*x = lis3lv02d_get_axis(adev.ac.x, position);
	*y = lis3lv02d_get_axis(adev.ac.y, position);
	*z = lis3lv02d_get_axis(adev.ac.z, position);
}

static inline void lis3lv02d_poweroff(acpi_handle handle)
{
	adev.is_on = 0;
	/* disable X,Y,Z axis and power down */
	lis3lv02d_acpi_write(handle, CTRL_REG1, 0x00);
}

static void lis3lv02d_poweron(acpi_handle handle)
{
	u8 val;

	adev.is_on = 1;
	lis3lv02d_acpi_init(handle);
	lis3lv02d_acpi_write(handle, FF_WU_CFG, 0);
	/*
	 * BDU: LSB and MSB values are not updated until both have been read.
	 *      So the value read will always be correct.
	 * IEN: Interrupt for free-fall and DD, not for data-ready.
	 */
	lis3lv02d_acpi_read(handle, CTRL_REG2, &val);
	val |= CTRL2_BDU | CTRL2_IEN;
	lis3lv02d_acpi_write(handle, CTRL_REG2, val);
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


/*
 * To be called before starting to use the device. It makes sure that the
 * device will always be on until a call to lis3lv02d_decrease_use(). Not to be
 * used from interrupt context.
 */
static void lis3lv02d_increase_use(struct acpi_lis3lv02d *dev)
{
	mutex_lock(&dev->lock);
	dev->usage++;
	if (dev->usage == 1) {
		if (!dev->is_on)
			lis3lv02d_poweron(dev->device->handle);
	}
	mutex_unlock(&dev->lock);
}

/*
 * To be called whenever a usage of the device is stopped.
 * It will make sure to turn off the device when there is not usage.
 */
static void lis3lv02d_decrease_use(struct acpi_lis3lv02d *dev)
{
	mutex_lock(&dev->lock);
	dev->usage--;
	if (dev->usage == 0)
		lis3lv02d_poweroff(dev->device->handle);
	mutex_unlock(&dev->lock);
}

/**
 * lis3lv02d_joystick_kthread - Kthread polling function
 * @data: unused - here to conform to threadfn prototype
 */
static int lis3lv02d_joystick_kthread(void *data)
{
	int x, y, z;

	while (!kthread_should_stop()) {
		lis3lv02d_get_xyz(adev.device->handle, &x, &y, &z);
		input_report_abs(adev.idev, ABS_X, x - adev.xcalib);
		input_report_abs(adev.idev, ABS_Y, y - adev.ycalib);
		input_report_abs(adev.idev, ABS_Z, z - adev.zcalib);

		input_sync(adev.idev);

		try_to_freeze();
		msleep_interruptible(MDPS_POLL_INTERVAL);
	}

	return 0;
}

static int lis3lv02d_joystick_open(struct input_dev *input)
{
	lis3lv02d_increase_use(&adev);
	adev.kthread = kthread_run(lis3lv02d_joystick_kthread, NULL, "klis3lv02d");
	if (IS_ERR(adev.kthread)) {
		lis3lv02d_decrease_use(&adev);
		return PTR_ERR(adev.kthread);
	}

	return 0;
}

static void lis3lv02d_joystick_close(struct input_dev *input)
{
	kthread_stop(adev.kthread);
	lis3lv02d_decrease_use(&adev);
}


static inline void lis3lv02d_calibrate_joystick(void)
{
	lis3lv02d_get_xyz(adev.device->handle, &adev.xcalib, &adev.ycalib, &adev.zcalib);
}

static int lis3lv02d_joystick_enable(void)
{
	int err;

	if (adev.idev)
		return -EINVAL;

	adev.idev = input_allocate_device();
	if (!adev.idev)
		return -ENOMEM;

	lis3lv02d_calibrate_joystick();

	adev.idev->name       = "ST LIS3LV02DL Accelerometer";
	adev.idev->phys       = DRIVER_NAME "/input0";
	adev.idev->id.bustype = BUS_HOST;
	adev.idev->id.vendor  = 0;
	adev.idev->dev.parent = &adev.pdev->dev;
	adev.idev->open       = lis3lv02d_joystick_open;
	adev.idev->close      = lis3lv02d_joystick_close;

	set_bit(EV_ABS, adev.idev->evbit);
	input_set_abs_params(adev.idev, ABS_X, -MDPS_MAX_VAL, MDPS_MAX_VAL, 3, 3);
	input_set_abs_params(adev.idev, ABS_Y, -MDPS_MAX_VAL, MDPS_MAX_VAL, 3, 3);
	input_set_abs_params(adev.idev, ABS_Z, -MDPS_MAX_VAL, MDPS_MAX_VAL, 3, 3);

	err = input_register_device(adev.idev);
	if (err) {
		input_free_device(adev.idev);
		adev.idev = NULL;
	}

	return err;
}

static void lis3lv02d_joystick_disable(void)
{
	if (!adev.idev)
		return;

	input_unregister_device(adev.idev);
	adev.idev = NULL;
}


/*
 * Initialise the accelerometer and the various subsystems.
 * Should be rather independant of the bus system.
 */
static int lis3lv02d_init_device(struct acpi_lis3lv02d *dev)
{
	mutex_init(&dev->lock);
	lis3lv02d_add_fs(dev->device);
	lis3lv02d_increase_use(dev);

	if (lis3lv02d_joystick_enable())
		printk(KERN_ERR DRIVER_NAME ": joystick initialization failed\n");

	lis3lv02d_decrease_use(dev);
	return 0;
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


/* Sysfs stuff */
static ssize_t lis3lv02d_position_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int x, y, z;

	lis3lv02d_increase_use(&adev);
	lis3lv02d_get_xyz(adev.device->handle, &x, &y, &z);
	lis3lv02d_decrease_use(&adev);
	return sprintf(buf, "(%d,%d,%d)\n", x, y, z);
}

static ssize_t lis3lv02d_calibrate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "(%d,%d,%d)\n", adev.xcalib, adev.ycalib, adev.zcalib);
}

static ssize_t lis3lv02d_calibrate_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	lis3lv02d_increase_use(&adev);
	lis3lv02d_calibrate_joystick();
	lis3lv02d_decrease_use(&adev);
	return count;
}

/* conversion btw sampling rate and the register values */
static int lis3lv02dl_df_val[4] = {40, 160, 640, 2560};
static ssize_t lis3lv02d_rate_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	u8 ctrl;
	int val;

	lis3lv02d_increase_use(&adev);
	lis3lv02d_acpi_read(adev.device->handle, CTRL_REG1, &ctrl);
	lis3lv02d_decrease_use(&adev);
	val = (ctrl & (CTRL1_DF0 | CTRL1_DF1)) >> 4;
	return sprintf(buf, "%d\n", lis3lv02dl_df_val[val]);
}

static DEVICE_ATTR(position, S_IRUGO, lis3lv02d_position_show, NULL);
static DEVICE_ATTR(calibrate, S_IRUGO|S_IWUSR, lis3lv02d_calibrate_show,
	lis3lv02d_calibrate_store);
static DEVICE_ATTR(rate, S_IRUGO, lis3lv02d_rate_show, NULL);

static struct attribute *lis3lv02d_attributes[] = {
	&dev_attr_position.attr,
	&dev_attr_calibrate.attr,
	&dev_attr_rate.attr,
	NULL
};

static struct attribute_group lis3lv02d_attribute_group = {
	.attrs = lis3lv02d_attributes
};

static int lis3lv02d_add_fs(struct acpi_device *device)
{
	adev.pdev = platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
	if (IS_ERR(adev.pdev))
		return PTR_ERR(adev.pdev);

	return sysfs_create_group(&adev.pdev->dev.kobj, &lis3lv02d_attribute_group);
}

static int lis3lv02d_remove_fs(void)
{
	sysfs_remove_group(&adev.pdev->dev.kobj, &lis3lv02d_attribute_group);
	platform_device_unregister(adev.pdev);
	return 0;
}

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

MODULE_DESCRIPTION("ST LIS3LV02Dx three-axis digital accelerometer driver");
MODULE_AUTHOR("Yan Burman and Eric Piel");
MODULE_LICENSE("GPL");

module_init(lis3lv02d_init_module);
module_exit(lis3lv02d_exit_module);
