// SPDX-License-Identifier: GPL-2.0
/* Author: Dan Scally <djrscally@gmail.com> */

#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tps68470.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "common.h"

#define DESIGNED_FOR_CHROMEOS		1
#define DESIGNED_FOR_WINDOWS		2

static const struct mfd_cell tps68470_cros[] = {
	{ .name = "tps68470-gpio" },
	{ .name = "tps68470_pmic_opregion" },
};

static const struct mfd_cell tps68470_win[] = {
	{ .name = "tps68470-gpio" },
	{ .name = "tps68470-clk" },
	{ .name = "tps68470-regulator" },
};

static const struct regmap_config tps68470_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TPS68470_REG_MAX,
};

static int tps68470_chip_init(struct device *dev, struct regmap *regmap)
{
	unsigned int version;
	int ret;

	/* Force software reset */
	ret = regmap_write(regmap, TPS68470_REG_RESET, TPS68470_REG_RESET_MASK);
	if (ret)
		return ret;

	ret = regmap_read(regmap, TPS68470_REG_REVID, &version);
	if (ret) {
		dev_err(dev, "Failed to read revision register: %d\n", ret);
		return ret;
	}

	dev_info(dev, "TPS68470 REVID: 0x%02x\n", version);

	return 0;
}

/** skl_int3472_tps68470_calc_type: Check what platform a device is designed for
 * @adev: A pointer to a &struct acpi_device
 *
 * Check CLDB buffer against the PMIC's adev. If present, then we check
 * the value of control_logic_type field and follow one of the
 * following scenarios:
 *
 *	1. No CLDB - likely ACPI tables designed for ChromeOS. We
 *	create platform devices for the GPIOs and OpRegion drivers.
 *
 *	2. CLDB, with control_logic_type = 2 - probably ACPI tables
 *	made for Windows 2-in-1 platforms. Register pdevs for GPIO,
 *	Clock and Regulator drivers to bind to.
 *
 *	3. Any other value in control_logic_type, we should never have
 *	gotten to this point; fail probe and return.
 *
 * Return:
 * * 1		Device intended for ChromeOS
 * * 2		Device intended for Windows
 * * -EINVAL	Where @adev has an object named CLDB but it does not conform to
 *		our expectations
 */
static int skl_int3472_tps68470_calc_type(struct acpi_device *adev)
{
	struct int3472_cldb cldb = { 0 };
	int ret;

	/*
	 * A CLDB buffer that exists, but which does not match our expectations
	 * should trigger an error so we don't blindly continue.
	 */
	ret = skl_int3472_fill_cldb(adev, &cldb);
	if (ret && ret != -ENODEV)
		return ret;

	if (ret)
		return DESIGNED_FOR_CHROMEOS;

	if (cldb.control_logic_type != 2)
		return -EINVAL;

	return DESIGNED_FOR_WINDOWS;
}

static int skl_int3472_tps68470_probe(struct i2c_client *client)
{
	struct acpi_device *adev = ACPI_COMPANION(&client->dev);
	struct regmap *regmap;
	int device_type;
	int ret;

	regmap = devm_regmap_init_i2c(client, &tps68470_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to create regmap: %ld\n", PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	i2c_set_clientdata(client, regmap);

	ret = tps68470_chip_init(&client->dev, regmap);
	if (ret < 0) {
		dev_err(&client->dev, "TPS68470 init error %d\n", ret);
		return ret;
	}

	device_type = skl_int3472_tps68470_calc_type(adev);
	switch (device_type) {
	case DESIGNED_FOR_WINDOWS:
		ret = devm_mfd_add_devices(&client->dev, PLATFORM_DEVID_NONE,
					   tps68470_win, ARRAY_SIZE(tps68470_win),
					   NULL, 0, NULL);
		break;
	case DESIGNED_FOR_CHROMEOS:
		ret = devm_mfd_add_devices(&client->dev, PLATFORM_DEVID_NONE,
					   tps68470_cros, ARRAY_SIZE(tps68470_cros),
					   NULL, 0, NULL);
		break;
	default:
		dev_err(&client->dev, "Failed to add MFD devices\n");
		return device_type;
	}

	return ret;
}

static const struct acpi_device_id int3472_device_id[] = {
	{ "INT3472", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, int3472_device_id);

static struct i2c_driver int3472_tps68470 = {
	.driver = {
		.name = "int3472-tps68470",
		.acpi_match_table = int3472_device_id,
	},
	.probe_new = skl_int3472_tps68470_probe,
};
module_i2c_driver(int3472_tps68470);

MODULE_DESCRIPTION("Intel SkyLake INT3472 ACPI TPS68470 Device Driver");
MODULE_AUTHOR("Daniel Scally <djrscally@gmail.com>");
MODULE_LICENSE("GPL v2");
