// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * tsi.c - AMD SBTSI I2C/I3C core driver. Probes the SBTSI device over I2C/I3C
 *         and publishes an auxiliary device on the auxiliary bus.
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include "tsi-core.h"

#define SBTSI_REG_CONFIG		0x03 /* RO */

/*
 * Bit for reporting value with temperature measurement range.
 * bit == 0: Use default temperature range (0C to 255.875C).
 * bit == 1: Use extended temperature range (-49C to +206.875C).
 */
#define SBTSI_CONFIG_EXT_RANGE_SHIFT	2

/*
 * ReadOrder bit specifies the reading order of integer and decimal part of
 * CPU temperature for atomic reads. If bit == 0, reading integer part triggers
 * latching of the decimal part, so integer part should be read first.
 */
#define SBTSI_CONFIG_READ_ORDER_SHIFT	5

static void sbtsi_adev_release(struct device *dev)
{
	kfree(to_auxiliary_dev(dev));
}

static void sbtsi_unregister_hwmon_adev(void *_adev)
{
	struct auxiliary_device *adev = _adev;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

static void sbtsi_misc_unregister(void *arg)
{
	struct sbtsi_data *data = arg;

	misc_deregister(&data->sbtsi_misc_dev);

	guard(sbtsi)(data);
	data->detached = true;
}

static void sbtsi_driver_unref(void *arg)
{
	struct sbtsi_data *data = arg;

	kref_put(&data->kref, sbtsi_data_release);
}

/*
 * Create and publish an auxiliary device. The hwmon driver in
 * drivers/hwmon/sbtsi_temp.c binds to this device.
 *
 * @dev:      I2C device (parent of the auxiliary device)
 * @dev_addr: I2C address — used as the auxiliary device instance ID so that
 *            each socket gets a unique name.
 */
static int sbtsi_create_hwmon_adev(struct device *dev, u8 dev_addr)
{
	struct auxiliary_device *adev;
	int ret;

	adev = kzalloc_obj(*adev);
	if (!adev)
		return -ENOMEM;

	adev->name = AMD_SBTSI_AUX_HWMON;
	adev->id = dev_addr;
	adev->dev.parent = dev;
	adev->dev.release = sbtsi_adev_release;

	ret = auxiliary_device_init(adev);
	if (ret) {
		kfree(adev);
		return ret;
	}

	ret = __auxiliary_device_add(adev, AMD_SBTSI_ADEV);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	return devm_add_action_or_reset(dev, sbtsi_unregister_hwmon_adev, adev);
}

static int sbtsi_probe_common(struct device *dev, struct sbtsi_data *data)
{
	u8 val;
	int err;

	mutex_init(&data->lock);
	kref_init(&data->kref);

	err = devm_add_action_or_reset(dev, sbtsi_driver_unref, data);
	if (err)
		return err;

	err = sbtsi_xfer(data, SBTSI_REG_CONFIG, &val, true);
	if (err)
		return err;

	data->ext_range_mode = FIELD_GET(BIT(SBTSI_CONFIG_EXT_RANGE_SHIFT), val);
	data->read_order = FIELD_GET(BIT(SBTSI_CONFIG_READ_ORDER_SHIFT), val);

	dev_set_drvdata(dev, data);
	err = sbtsi_create_hwmon_adev(dev, data->dev_addr);
	if (err < 0)
		return err;

	err = create_misc_tsi_device(data, dev);
	if (err)
		return err;

	return devm_add_action_or_reset(dev, sbtsi_misc_unregister, data);
}

static int sbtsi_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct sbtsi_data *data;

	data = kzalloc_obj(*data);
	if (!data)
		return -ENOMEM;

	data->is_i3c = false;
	data->client = client;

	/* In a multi-socket system, devices that are otherwise identical do not
	 * share the same static address; each instance resides at a unique I2C
	 * client address on the same or different bus. Use the I2C client
	 * address as the auxiliary device instance ID to ensure each socket
	 * receives a distinct auxiliary device name.
	 */
	data->dev_addr = client->addr;
	return sbtsi_probe_common(dev, data);
}

static const struct i2c_device_id sbtsi_id[] = {
	{ .name = "sbtsi" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sbtsi_id);

static const struct of_device_id __maybe_unused sbtsi_of_match[] = {
	{
		.compatible = "amd,sbtsi",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, sbtsi_of_match);

static struct i2c_driver sbtsi_driver = {
	.driver = {
		.name = "sbtsi-i2c",
		.of_match_table = of_match_ptr(sbtsi_of_match),
	},
	.probe = sbtsi_i2c_probe,
	.id_table = sbtsi_id,
};

static int sbtsi_i3c_probe(struct i3c_device *i3cdev)
{
	struct device *dev = i3cdev_to_dev(i3cdev);
	struct i3c_device_info devinfo;
	struct sbtsi_i3c_priv *i3c_priv;
	struct sbtsi_data *data;

	/*
	 * AMD OOB devices differ on basis of Instance ID,
	 * for SBTSI, instance ID is 0.
	 * As the device Id match is not on basis of Instance ID,
	 * add the below check to probe the SBTSI device only and
	 * not other OOB devices.
	 */
	i3c_device_get_info(i3cdev, &devinfo);
	if (I3C_PID_INSTANCE_ID(devinfo.pid) != 0)
		return -ENXIO;

	i3c_priv = kzalloc_obj(*i3c_priv);
	if (!i3c_priv)
		return -ENOMEM;

	data = &i3c_priv->data;
	data->i3cdev = i3cdev;
	data->is_i3c = true;
	/*
	 * In a multi-socket system, otherwise identical devices do not share
	 * the same address; each instance is enumerated with a distinct dynamic
	 * (assigned) address on the I3C bus. Use that address (passed in as
	 * dev_addr) as the auxiliary device instance ID so that every socket
	 * gets a unique auxiliary device name.
	 */
	data->dev_addr = devinfo.dyn_addr;

	return sbtsi_probe_common(dev, data);
}

static const struct i3c_device_id sbtsi_i3c_id[] = {
	/* PID for AMD SBTSI device */
	I3C_DEVICE_EXTRA_INFO(0x112, 0x0, 0x1, NULL),	/* Socket:0, Turin and Genoa */
	I3C_DEVICE_EXTRA_INFO(0x0, 0x0, 0x118, NULL),	/* Socket:0, Venice */
	I3C_DEVICE_EXTRA_INFO(0x0, 0x100, 0x118, NULL),	/* Socket:1, Venice */
	I3C_DEVICE_EXTRA_INFO(0x112, 0x0, 0x119, NULL),	/* Socket:0, Venice */
	I3C_DEVICE_EXTRA_INFO(0x112, 0x100, 0x119, NULL),	/* Socket:1, Venice */
	{}
};
MODULE_DEVICE_TABLE(i3c, sbtsi_i3c_id);

static struct i3c_driver sbtsi_i3c_driver = {
	.driver = {
		.name = "sbtsi-i3c",
	},
	.probe = sbtsi_i3c_probe,
	.id_table = sbtsi_i3c_id,
};

module_i3c_i2c_driver(sbtsi_i3c_driver, &sbtsi_driver);

MODULE_DESCRIPTION("AMD SB-TSI I2C/I3C core driver");
MODULE_LICENSE("GPL");
