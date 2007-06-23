/*
    smsc47b397.c - Part of lm_sensors, Linux kernel modules
			for hardware monitoring

    Supports the SMSC LPC47B397-NC Super-I/O chip.

    Author/Maintainer: Mark M. Hoffman <mhoffman@lightlink.com>
	Copyright (C) 2004 Utilitek Systems, Inc.

    derived in part from smsc47m1.c:
	Copyright (C) 2002 Mark D. Studebaker <mdsxyz123@yahoo.com>
	Copyright (C) 2004 Jean Delvare <khali@linux-fr.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <asm/io.h>

static struct platform_device *pdev;

#define DRVNAME "smsc47b397"

/* Super-I/0 registers and commands */

#define	REG	0x2e	/* The register to read/write */
#define	VAL	0x2f	/* The value to read/write */

static inline void superio_outb(int reg, int val)
{
	outb(reg, REG);
	outb(val, VAL);
}

static inline int superio_inb(int reg)
{
	outb(reg, REG);
	return inb(VAL);
}

/* select superio logical device */
static inline void superio_select(int ld)
{
	superio_outb(0x07, ld);
}

static inline void superio_enter(void)
{
	outb(0x55, REG);
}

static inline void superio_exit(void)
{
	outb(0xAA, REG);
}

#define SUPERIO_REG_DEVID	0x20
#define SUPERIO_REG_DEVREV	0x21
#define SUPERIO_REG_BASE_MSB	0x60
#define SUPERIO_REG_BASE_LSB	0x61
#define SUPERIO_REG_LD8		0x08

#define SMSC_EXTENT		0x02

/* 0 <= nr <= 3 */
static u8 smsc47b397_reg_temp[] = {0x25, 0x26, 0x27, 0x80};
#define SMSC47B397_REG_TEMP(nr)	(smsc47b397_reg_temp[(nr)])

/* 0 <= nr <= 3 */
#define SMSC47B397_REG_FAN_LSB(nr) (0x28 + 2 * (nr))
#define SMSC47B397_REG_FAN_MSB(nr) (0x29 + 2 * (nr))

struct smsc47b397_data {
	unsigned short addr;
	const char *name;
	struct class_device *class_dev;
	struct mutex lock;

	struct mutex update_lock;
	unsigned long last_updated; /* in jiffies */
	int valid;

	/* register values */
	u16 fan[4];
	u8 temp[4];
};

static int smsc47b397_read_value(struct smsc47b397_data* data, u8 reg)
{
	int res;

	mutex_lock(&data->lock);
	outb(reg, data->addr);
	res = inb_p(data->addr + 1);
	mutex_unlock(&data->lock);
	return res;
}

static struct smsc47b397_data *smsc47b397_update_device(struct device *dev)
{
	struct smsc47b397_data *data = dev_get_drvdata(dev);
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		dev_dbg(dev, "starting device update...\n");

		/* 4 temperature inputs, 4 fan inputs */
		for (i = 0; i < 4; i++) {
			data->temp[i] = smsc47b397_read_value(data,
					SMSC47B397_REG_TEMP(i));

			/* must read LSB first */
			data->fan[i]  = smsc47b397_read_value(data,
					SMSC47B397_REG_FAN_LSB(i));
			data->fan[i] |= smsc47b397_read_value(data,
					SMSC47B397_REG_FAN_MSB(i)) << 8;
		}

		data->last_updated = jiffies;
		data->valid = 1;

		dev_dbg(dev, "... device update complete\n");
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* TEMP: 0.001C/bit (-128C to +127C)
   REG: 1C/bit, two's complement */
static int temp_from_reg(u8 reg)
{
	return (s8)reg * 1000;
}

static ssize_t show_temp(struct device *dev, struct device_attribute
			 *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct smsc47b397_data *data = smsc47b397_update_device(dev);
	return sprintf(buf, "%d\n", temp_from_reg(data->temp[attr->index]));
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, show_temp, NULL, 2);
static SENSOR_DEVICE_ATTR(temp4_input, S_IRUGO, show_temp, NULL, 3);

/* FAN: 1 RPM/bit
   REG: count of 90kHz pulses / revolution */
static int fan_from_reg(u16 reg)
{
	if (reg == 0 || reg == 0xffff)
		return 0;
	return 90000 * 60 / reg;
}

static ssize_t show_fan(struct device *dev, struct device_attribute
			*devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct smsc47b397_data *data = smsc47b397_update_device(dev);
	return sprintf(buf, "%d\n", fan_from_reg(data->fan[attr->index]));
}
static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, show_fan, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, show_fan, NULL, 1);
static SENSOR_DEVICE_ATTR(fan3_input, S_IRUGO, show_fan, NULL, 2);
static SENSOR_DEVICE_ATTR(fan4_input, S_IRUGO, show_fan, NULL, 3);

static ssize_t show_name(struct device *dev, struct device_attribute
			 *devattr, char *buf)
{
	struct smsc47b397_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", data->name);
}
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static struct attribute *smsc47b397_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,

	&dev_attr_name.attr,
	NULL
};

static const struct attribute_group smsc47b397_group = {
	.attrs = smsc47b397_attributes,
};

static int __devexit smsc47b397_remove(struct platform_device *pdev)
{
	struct smsc47b397_data *data = platform_get_drvdata(pdev);
	struct resource *res;

	hwmon_device_unregister(data->class_dev);
	sysfs_remove_group(&pdev->dev.kobj, &smsc47b397_group);
	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	release_region(res->start, SMSC_EXTENT);
	kfree(data);

	return 0;
}

static int smsc47b397_probe(struct platform_device *pdev);

static struct platform_driver smsc47b397_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe		= smsc47b397_probe,
	.remove		= __devexit_p(smsc47b397_remove),
};

static int __devinit smsc47b397_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct smsc47b397_data *data;
	struct resource *res;
	int err = 0;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!request_region(res->start, SMSC_EXTENT,
			    smsc47b397_driver.driver.name)) {
		dev_err(dev, "Region 0x%lx-0x%lx already in use!\n",
			(unsigned long)res->start,
			(unsigned long)res->start + SMSC_EXTENT - 1);
		return -EBUSY;
	}

	if (!(data = kzalloc(sizeof(struct smsc47b397_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto error_release;
	}

	data->addr = res->start;
	data->name = "smsc47b397";
	mutex_init(&data->lock);
	mutex_init(&data->update_lock);
	platform_set_drvdata(pdev, data);

	if ((err = sysfs_create_group(&dev->kobj, &smsc47b397_group)))
		goto error_free;

	data->class_dev = hwmon_device_register(dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto error_remove;
	}

	return 0;

error_remove:
	sysfs_remove_group(&dev->kobj, &smsc47b397_group);
error_free:
	kfree(data);
error_release:
	release_region(res->start, SMSC_EXTENT);
	return err;
}

static int __init smsc47b397_device_add(unsigned short address)
{
	struct resource res = {
		.start	= address,
		.end	= address + SMSC_EXTENT - 1,
		.name	= DRVNAME,
		.flags	= IORESOURCE_IO,
	};
	int err;

	pdev = platform_device_alloc(DRVNAME, address);
	if (!pdev) {
		err = -ENOMEM;
		printk(KERN_ERR DRVNAME ": Device allocation failed\n");
		goto exit;
	}

	err = platform_device_add_resources(pdev, &res, 1);
	if (err) {
		printk(KERN_ERR DRVNAME ": Device resource addition failed "
		       "(%d)\n", err);
		goto exit_device_put;
	}

	err = platform_device_add(pdev);
	if (err) {
		printk(KERN_ERR DRVNAME ": Device addition failed (%d)\n",
		       err);
		goto exit_device_put;
	}

	return 0;

exit_device_put:
	platform_device_put(pdev);
exit:
	return err;
}

static int __init smsc47b397_find(unsigned short *addr)
{
	u8 id, rev;

	superio_enter();
	id = superio_inb(SUPERIO_REG_DEVID);

	if ((id != 0x6f) && (id != 0x81) && (id != 0x85)) {
		superio_exit();
		return -ENODEV;
	}

	rev = superio_inb(SUPERIO_REG_DEVREV);

	superio_select(SUPERIO_REG_LD8);
	*addr = (superio_inb(SUPERIO_REG_BASE_MSB) << 8)
		 |  superio_inb(SUPERIO_REG_BASE_LSB);

	printk(KERN_INFO DRVNAME ": found SMSC %s "
		"(base address 0x%04x, revision %u)\n",
		id == 0x81 ? "SCH5307-NS" : id == 0x85 ? "SCH5317" :
	       "LPC47B397-NC", *addr, rev);

	superio_exit();
	return 0;
}

static int __init smsc47b397_init(void)
{
	unsigned short address;
	int ret;

	if ((ret = smsc47b397_find(&address)))
		return ret;

	ret = platform_driver_register(&smsc47b397_driver);
	if (ret)
		goto exit;

	/* Sets global pdev as a side effect */
	ret = smsc47b397_device_add(address);
	if (ret)
		goto exit_driver;

	return 0;

exit_driver:
	platform_driver_unregister(&smsc47b397_driver);
exit:
	return ret;
}

static void __exit smsc47b397_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&smsc47b397_driver);
}

MODULE_AUTHOR("Mark M. Hoffman <mhoffman@lightlink.com>");
MODULE_DESCRIPTION("SMSC LPC47B397 driver");
MODULE_LICENSE("GPL");

module_init(smsc47b397_init);
module_exit(smsc47b397_exit);
