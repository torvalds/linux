// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the ChromeOS human presence sensor (HPS), attached via I2C.
 *
 * The driver exposes HPS as a character device, although currently no read or
 * write operations are supported. Instead, the driver only controls the power
 * state of the sensor, keeping it on only while userspace holds an open file
 * descriptor to the HPS device.
 *
 * Copyright 2022 Google LLC.
 */

#include <linux/acpi.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#define HPS_ACPI_ID		"GOOG0020"

struct hps_drvdata {
	struct i2c_client *client;
	struct miscdevice misc_device;
	struct gpio_desc *enable_gpio;
};

static void hps_set_power(struct hps_drvdata *hps, bool state)
{
	gpiod_set_value_cansleep(hps->enable_gpio, state);
}

static int hps_open(struct inode *inode, struct file *file)
{
	struct hps_drvdata *hps = container_of(file->private_data,
					       struct hps_drvdata, misc_device);
	struct device *dev = &hps->client->dev;

	return pm_runtime_resume_and_get(dev);
}

static int hps_release(struct inode *inode, struct file *file)
{
	struct hps_drvdata *hps = container_of(file->private_data,
					       struct hps_drvdata, misc_device);
	struct device *dev = &hps->client->dev;

	return pm_runtime_put(dev);
}

static const struct file_operations hps_fops = {
	.owner = THIS_MODULE,
	.open = hps_open,
	.release = hps_release,
};

static int hps_i2c_probe(struct i2c_client *client)
{
	struct hps_drvdata *hps;
	int ret;

	hps = devm_kzalloc(&client->dev, sizeof(*hps), GFP_KERNEL);
	if (!hps)
		return -ENOMEM;

	hps->misc_device.parent = &client->dev;
	hps->misc_device.minor = MISC_DYNAMIC_MINOR;
	hps->misc_device.name = "cros-hps";
	hps->misc_device.fops = &hps_fops;

	i2c_set_clientdata(client, hps);
	hps->client = client;

	/*
	 * HPS is powered on from firmware before entering the kernel, so we
	 * acquire the line with GPIOD_OUT_HIGH here to preserve the existing
	 * state. The peripheral is powered off after successful probe below.
	 */
	hps->enable_gpio = devm_gpiod_get(&client->dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(hps->enable_gpio)) {
		ret = PTR_ERR(hps->enable_gpio);
		dev_err(&client->dev, "failed to get enable gpio: %d\n", ret);
		return ret;
	}

	ret = misc_register(&hps->misc_device);
	if (ret) {
		dev_err(&client->dev, "failed to initialize misc device: %d\n", ret);
		return ret;
	}

	hps_set_power(hps, false);
	pm_runtime_enable(&client->dev);
	return 0;
}

static void hps_i2c_remove(struct i2c_client *client)
{
	struct hps_drvdata *hps = i2c_get_clientdata(client);

	pm_runtime_disable(&client->dev);
	misc_deregister(&hps->misc_device);

	/*
	 * Re-enable HPS, in order to return it to its default state
	 * (i.e. powered on).
	 */
	hps_set_power(hps, true);
}

static int hps_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct hps_drvdata *hps = i2c_get_clientdata(client);

	hps_set_power(hps, false);
	return 0;
}

static int hps_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct hps_drvdata *hps = i2c_get_clientdata(client);

	hps_set_power(hps, true);
	return 0;
}
static DEFINE_RUNTIME_DEV_PM_OPS(hps_pm_ops, hps_suspend, hps_resume, NULL);

static const struct i2c_device_id hps_i2c_id[] = {
	{ "cros-hps", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hps_i2c_id);

#ifdef CONFIG_ACPI
static const struct acpi_device_id hps_acpi_id[] = {
	{ HPS_ACPI_ID, 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, hps_acpi_id);
#endif /* CONFIG_ACPI */

static struct i2c_driver hps_i2c_driver = {
	.probe = hps_i2c_probe,
	.remove = hps_i2c_remove,
	.id_table = hps_i2c_id,
	.driver = {
		.name = "cros-hps",
		.pm = pm_ptr(&hps_pm_ops),
		.acpi_match_table = ACPI_PTR(hps_acpi_id),
	},
};
module_i2c_driver(hps_i2c_driver);

MODULE_ALIAS("acpi:" HPS_ACPI_ID);
MODULE_AUTHOR("Sami Kyöstilä <skyostil@chromium.org>");
MODULE_DESCRIPTION("Driver for ChromeOS HPS");
MODULE_LICENSE("GPL");
