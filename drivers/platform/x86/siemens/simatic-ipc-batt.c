// SPDX-License-Identifier: GPL-2.0
/*
 * Siemens SIMATIC IPC driver for CMOS battery monitoring
 *
 * Copyright (c) Siemens AG, 2023
 *
 * Authors:
 *  Gerd Haeussler <gerd.haeussler.ext@siemens.com>
 *  Henning Schild <henning.schild@siemens.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/gpio/machine.h>
#include <linux/gpio/consumer.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/x86/simatic-ipc-base.h>
#include <linux/sizes.h>

#include "simatic-ipc-batt.h"

#define BATT_DELAY_MS	(1000 * 60 * 60 * 24)	/* 24 h delay */

#define SIMATIC_IPC_BATT_LEVEL_FULL	3000
#define SIMATIC_IPC_BATT_LEVEL_CRIT	2750
#define SIMATIC_IPC_BATT_LEVEL_EMPTY	   0

static struct simatic_ipc_batt {
	u8 devmode;
	long current_state;
	struct gpio_desc *gpios[3];
	unsigned long last_updated_jiffies;
} priv;

static long simatic_ipc_batt_read_gpio(void)
{
	long r = SIMATIC_IPC_BATT_LEVEL_FULL;

	if (priv.gpios[2]) {
		gpiod_set_value(priv.gpios[2], 1);
		msleep(150);
	}

	if (gpiod_get_value_cansleep(priv.gpios[0]))
		r = SIMATIC_IPC_BATT_LEVEL_EMPTY;
	else if (gpiod_get_value_cansleep(priv.gpios[1]))
		r = SIMATIC_IPC_BATT_LEVEL_CRIT;

	if (priv.gpios[2])
		gpiod_set_value(priv.gpios[2], 0);

	return r;
}

#define SIMATIC_IPC_BATT_PORT_BASE	0x404D
static struct resource simatic_ipc_batt_io_res =
	DEFINE_RES_IO_NAMED(SIMATIC_IPC_BATT_PORT_BASE, SZ_1, KBUILD_MODNAME);

static long simatic_ipc_batt_read_io(struct device *dev)
{
	long r = SIMATIC_IPC_BATT_LEVEL_FULL;
	struct resource *res = &simatic_ipc_batt_io_res;
	u8 val;

	if (!request_muxed_region(res->start, resource_size(res), res->name)) {
		dev_err(dev, "Unable to register IO resource at %pR\n", res);
		return -EBUSY;
	}

	val = inb(SIMATIC_IPC_BATT_PORT_BASE);
	release_region(simatic_ipc_batt_io_res.start, resource_size(&simatic_ipc_batt_io_res));

	if (val & (1 << 7))
		r = SIMATIC_IPC_BATT_LEVEL_EMPTY;
	else if (val & (1 << 6))
		r = SIMATIC_IPC_BATT_LEVEL_CRIT;

	return r;
}

static long simatic_ipc_batt_read_value(struct device *dev)
{
	unsigned long next_update;

	next_update = priv.last_updated_jiffies + msecs_to_jiffies(BATT_DELAY_MS);
	if (time_after(jiffies, next_update) || !priv.last_updated_jiffies) {
		if (priv.devmode == SIMATIC_IPC_DEVICE_227E)
			priv.current_state = simatic_ipc_batt_read_io(dev);
		else
			priv.current_state = simatic_ipc_batt_read_gpio();

		priv.last_updated_jiffies = jiffies;
		if (priv.current_state < SIMATIC_IPC_BATT_LEVEL_FULL)
			dev_warn(dev, "CMOS battery needs to be replaced.\n");
	}

	return priv.current_state;
}

static int simatic_ipc_batt_read(struct device *dev, enum hwmon_sensor_types type,
				 u32 attr, int channel, long *val)
{
	switch (attr) {
	case hwmon_in_input:
		*val = simatic_ipc_batt_read_value(dev);
		break;
	case hwmon_in_lcrit:
		*val = SIMATIC_IPC_BATT_LEVEL_CRIT;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static umode_t simatic_ipc_batt_is_visible(const void *data, enum hwmon_sensor_types type,
					   u32 attr, int channel)
{
	if (attr == hwmon_in_input || attr == hwmon_in_lcrit)
		return 0444;

	return 0;
}

static const struct hwmon_ops simatic_ipc_batt_ops = {
	.is_visible = simatic_ipc_batt_is_visible,
	.read = simatic_ipc_batt_read,
};

static const struct hwmon_channel_info *simatic_ipc_batt_info[] = {
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT | HWMON_I_LCRIT),
	NULL
};

static const struct hwmon_chip_info simatic_ipc_batt_chip_info = {
	.ops = &simatic_ipc_batt_ops,
	.info = simatic_ipc_batt_info,
};

void simatic_ipc_batt_remove(struct platform_device *pdev, struct gpiod_lookup_table *table)
{
	gpiod_remove_lookup_table(table);
}
EXPORT_SYMBOL_GPL(simatic_ipc_batt_remove);

int simatic_ipc_batt_probe(struct platform_device *pdev, struct gpiod_lookup_table *table)
{
	struct simatic_ipc_platform *plat;
	struct device *dev = &pdev->dev;
	struct device *hwmon_dev;
	unsigned long flags;
	int err;

	plat = pdev->dev.platform_data;
	priv.devmode = plat->devmode;

	switch (priv.devmode) {
	case SIMATIC_IPC_DEVICE_127E:
	case SIMATIC_IPC_DEVICE_227G:
	case SIMATIC_IPC_DEVICE_BX_39A:
	case SIMATIC_IPC_DEVICE_BX_21A:
	case SIMATIC_IPC_DEVICE_BX_59A:
		table->dev_id = dev_name(dev);
		gpiod_add_lookup_table(table);
		break;
	case SIMATIC_IPC_DEVICE_227E:
		goto nogpio;
	default:
		return -ENODEV;
	}

	priv.gpios[0] = devm_gpiod_get_index(dev, "CMOSBattery empty", 0, GPIOD_IN);
	if (IS_ERR(priv.gpios[0])) {
		err = PTR_ERR(priv.gpios[0]);
		priv.gpios[0] = NULL;
		goto out;
	}
	priv.gpios[1] = devm_gpiod_get_index(dev, "CMOSBattery low", 1, GPIOD_IN);
	if (IS_ERR(priv.gpios[1])) {
		err = PTR_ERR(priv.gpios[1]);
		priv.gpios[1] = NULL;
		goto out;
	}

	if (table->table[2].key) {
		flags = GPIOD_OUT_HIGH;
		if (priv.devmode == SIMATIC_IPC_DEVICE_BX_21A ||
		    priv.devmode == SIMATIC_IPC_DEVICE_BX_59A)
			flags = GPIOD_OUT_LOW;
		priv.gpios[2] = devm_gpiod_get_index(dev, "CMOSBattery meter", 2, flags);
		if (IS_ERR(priv.gpios[2])) {
			err = PTR_ERR(priv.gpios[2]);
			priv.gpios[2] = NULL;
			goto out;
		}
	} else {
		priv.gpios[2] = NULL;
	}

nogpio:
	hwmon_dev = devm_hwmon_device_register_with_info(dev, KBUILD_MODNAME,
							 &priv,
							 &simatic_ipc_batt_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev)) {
		err = PTR_ERR(hwmon_dev);
		goto out;
	}

	/* warn about aging battery even if userspace never reads hwmon */
	simatic_ipc_batt_read_value(dev);

	return 0;
out:
	simatic_ipc_batt_remove(pdev, table);

	return err;
}
EXPORT_SYMBOL_GPL(simatic_ipc_batt_probe);

static void simatic_ipc_batt_io_remove(struct platform_device *pdev)
{
	simatic_ipc_batt_remove(pdev, NULL);
}

static int simatic_ipc_batt_io_probe(struct platform_device *pdev)
{
	return simatic_ipc_batt_probe(pdev, NULL);
}

static struct platform_driver simatic_ipc_batt_driver = {
	.probe = simatic_ipc_batt_io_probe,
	.remove_new = simatic_ipc_batt_io_remove,
	.driver = {
		.name = KBUILD_MODNAME,
	},
};

module_platform_driver(simatic_ipc_batt_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_AUTHOR("Henning Schild <henning.schild@siemens.com>");
