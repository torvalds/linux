// SPDX-License-Identifier: GPL-2.0-only
/*
 * 3-axis accelerometer driver supporting many I2C Bosch-Sensortec chips
 * Copyright (c) 2014, Intel Corporation.
 */

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/regmap.h>

#include "bmc150-accel.h"

#ifdef CONFIG_ACPI
static const struct acpi_device_id bmc150_acpi_dual_accel_ids[] = {
	{"BOSC0200"},
	{"DUAL250E"},
	{ }
};

/*
 * The DUAL250E ACPI device for 360° hinges type 2-in-1s with 1 accelerometer
 * in the display and 1 in the hinge has an ACPI-method (DSM) to tell the
 * ACPI code about the angle between the 2 halves. This will make the ACPI
 * code enable/disable the keyboard and touchpad. We need to call this to avoid
 * the keyboard being disabled when the 2-in-1 is turned-on or resumed while
 * fully folded into tablet mode (which gets detected with a HALL-sensor).
 * If we don't call this then the keyboard won't work even when the 2-in-1 is
 * changed to be used in laptop mode after the power-on / resume.
 *
 * This DSM takes 2 angles, selected by setting aux0 to 0 or 1, these presumably
 * define the angle between the gravity vector measured by the accelerometer in
 * the display (aux0=0) resp. the base (aux0=1) and some reference vector.
 * The 2 angles get subtracted from each other so the reference vector does
 * not matter and we can simply leave the second angle at 0.
 */

#define BMC150_DSM_GUID				"7681541e-8827-4239-8d9d-36be7fe12542"
#define DUAL250E_SET_ANGLE_FN_INDEX		3

struct dual250e_set_angle_args {
	u32 aux0;
	u32 ang0;
	u32 rawx;
	u32 rawy;
	u32 rawz;
} __packed;

static bool bmc150_acpi_set_angle_dsm(struct i2c_client *client, u32 aux0, u32 ang0)
{
	struct acpi_device *adev = ACPI_COMPANION(&client->dev);
	struct dual250e_set_angle_args args = {
		.aux0 = aux0,
		.ang0 = ang0,
	};
	union acpi_object args_obj, *obj;
	guid_t guid;

	if (!acpi_dev_hid_uid_match(adev, "DUAL250E", NULL))
		return false;

	guid_parse(BMC150_DSM_GUID, &guid);

	if (!acpi_check_dsm(adev->handle, &guid, 0, BIT(DUAL250E_SET_ANGLE_FN_INDEX)))
		return false;

	/*
	 * Note this triggers the following warning:
	 * "ACPI Warning: \_SB.PCI0.I2C2.ACC1._DSM: Argument #4 type mismatch -
	 *                Found [Buffer], ACPI requires [Package]"
	 * This is unavoidable since the _DSM implementation expects a "naked"
	 * buffer, so wrapping it in a package will _not_ work.
	 */
	args_obj.type = ACPI_TYPE_BUFFER;
	args_obj.buffer.length = sizeof(args);
	args_obj.buffer.pointer = (u8 *)&args;

	obj = acpi_evaluate_dsm(adev->handle, &guid, 0, DUAL250E_SET_ANGLE_FN_INDEX, &args_obj);
	if (!obj) {
		dev_err(&client->dev, "Failed to call DSM to enable keyboard and touchpad\n");
		return false;
	}

	ACPI_FREE(obj);
	return true;
}

static bool bmc150_acpi_enable_keyboard(struct i2c_client *client)
{
	/*
	 * The EC must see a change for it to re-enable the kbd, so first
	 * set the angle to 270° (tent/stand mode) and then change it to
	 * 90° (laptop mode).
	 */
	if (!bmc150_acpi_set_angle_dsm(client, 0, 270))
		return false;

	/* The EC needs some time to notice the angle being changed */
	msleep(100);

	return bmc150_acpi_set_angle_dsm(client, 0, 90);
}

static void bmc150_acpi_resume_work(struct work_struct *work)
{
	struct bmc150_accel_data *data =
		container_of(work, struct bmc150_accel_data, resume_work.work);

	bmc150_acpi_enable_keyboard(data->second_device);
}

static void bmc150_acpi_resume_handler(struct device *dev)
{
	struct bmc150_accel_data *data = iio_priv(dev_get_drvdata(dev));

	/*
	 * Delay the bmc150_acpi_enable_keyboard() call till after the system
	 * resume has completed, otherwise it will not work.
	 */
	schedule_delayed_work(&data->resume_work, msecs_to_jiffies(1000));
}

/*
 * Some acpi_devices describe 2 accelerometers in a single ACPI device,
 * try instantiating a second i2c_client for an I2cSerialBusV2 ACPI resource
 * with index 1.
 */
static void bmc150_acpi_dual_accel_probe(struct i2c_client *client)
{
	struct bmc150_accel_data *data = iio_priv(i2c_get_clientdata(client));
	struct acpi_device *adev = ACPI_COMPANION(&client->dev);
	char dev_name[16];
	struct i2c_board_info board_info = {
		.type = "bmc150_accel",
		.dev_name = dev_name,
		.fwnode = client->dev.fwnode,
	};

	if (acpi_match_device_ids(adev, bmc150_acpi_dual_accel_ids))
		return;

	/*
	 * The 2nd accel sits in the base of 2-in-1s. The suffix is static, as
	 * there should never be more then 1 ACPI node with 2 accelerometers.
	 */
	snprintf(dev_name, sizeof(dev_name), "%s:base", acpi_device_hid(adev));

	board_info.irq = acpi_dev_gpio_irq_get(adev, 1);

	data->second_device = i2c_acpi_new_device(&client->dev, 1, &board_info);

	if (!IS_ERR(data->second_device) && bmc150_acpi_enable_keyboard(data->second_device)) {
		INIT_DELAYED_WORK(&data->resume_work, bmc150_acpi_resume_work);
		data->resume_callback = bmc150_acpi_resume_handler;
	}
}

static void bmc150_acpi_dual_accel_remove(struct i2c_client *client)
{
	struct bmc150_accel_data *data = iio_priv(i2c_get_clientdata(client));

	if (data->resume_callback)
		cancel_delayed_work_sync(&data->resume_work);

	i2c_unregister_device(data->second_device);
}
#else
static void bmc150_acpi_dual_accel_probe(struct i2c_client *client) {}
static void bmc150_acpi_dual_accel_remove(struct i2c_client *client) {}
#endif

static int bmc150_accel_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct regmap *regmap;
	const char *name = NULL;
	enum bmc150_type type = BOSCH_UNKNOWN;
	bool block_supported =
		i2c_check_functionality(client->adapter, I2C_FUNC_I2C) ||
		i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_READ_I2C_BLOCK);
	int ret;

	regmap = devm_regmap_init_i2c(client, &bmc150_regmap_conf);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to initialize i2c regmap\n");
		return PTR_ERR(regmap);
	}

	if (id) {
		name = id->name;
		type = id->driver_data;
	}

	ret = bmc150_accel_core_probe(&client->dev, regmap, client->irq,
				      type, name, block_supported);
	if (ret)
		return ret;

	/*
	 * The !id check avoids recursion when probe() gets called
	 * for the second client.
	 */
	if (!id && has_acpi_companion(&client->dev))
		bmc150_acpi_dual_accel_probe(client);

	return 0;
}

static void bmc150_accel_remove(struct i2c_client *client)
{
	bmc150_acpi_dual_accel_remove(client);

	bmc150_accel_core_remove(&client->dev);
}

static const struct acpi_device_id bmc150_accel_acpi_match[] = {
	{"BMA0255"},
	{"BMA0280"},
	{"BMA222"},
	{"BMA222E"},
	{"BMA250E"},
	{"BMC150A"},
	{"BMI055A"},
	{"BOSC0200"},
	{"BSBA0150"},
	{"DUAL250E"},
	{ },
};
MODULE_DEVICE_TABLE(acpi, bmc150_accel_acpi_match);

static const struct i2c_device_id bmc150_accel_id[] = {
	{"bma222"},
	{"bma222e"},
	{"bma250e"},
	{"bma253"},
	{"bma254"},
	{"bma255"},
	{"bma280"},
	{"bmc150_accel"},
	{"bmc156_accel", BOSCH_BMC156},
	{"bmi055_accel"},
	{}
};

MODULE_DEVICE_TABLE(i2c, bmc150_accel_id);

static const struct of_device_id bmc150_accel_of_match[] = {
	{ .compatible = "bosch,bma222" },
	{ .compatible = "bosch,bma222e" },
	{ .compatible = "bosch,bma250e" },
	{ .compatible = "bosch,bma253" },
	{ .compatible = "bosch,bma254" },
	{ .compatible = "bosch,bma255" },
	{ .compatible = "bosch,bma280" },
	{ .compatible = "bosch,bmc150_accel" },
	{ .compatible = "bosch,bmc156_accel" },
	{ .compatible = "bosch,bmi055_accel" },
	{ },
};
MODULE_DEVICE_TABLE(of, bmc150_accel_of_match);

static struct i2c_driver bmc150_accel_driver = {
	.driver = {
		.name	= "bmc150_accel_i2c",
		.of_match_table = bmc150_accel_of_match,
		.acpi_match_table = ACPI_PTR(bmc150_accel_acpi_match),
		.pm	= &bmc150_accel_pm_ops,
	},
	.probe		= bmc150_accel_probe,
	.remove		= bmc150_accel_remove,
	.id_table	= bmc150_accel_id,
};
module_i2c_driver(bmc150_accel_driver);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BMC150 I2C accelerometer driver");
MODULE_IMPORT_NS(IIO_BMC150);
