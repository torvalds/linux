// SPDX-License-Identifier: GPL-2.0-only
/*
 * Read-only fan monitoring for the HONOR FMI-XX.
 *
 * The firmware-provided \GFNS ACPI method accepts a three-byte buffer.
 * Byte 2 selects fan 0 or 1. It returns a status byte followed by a
 * little-endian 16-bit fan speed in RPM. The method owns all Embedded
 * Controller access and serialization; this driver deliberately exposes no
 * fan control interface.
 */

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define HONOR_FMI_GFNS_RESULT_SIZE	3

struct honor_fmi_data {
	acpi_handle gfns;
};

static const struct dmi_system_id honor_fmi_dmi_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HONOR"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "FMI-XX"),
		},
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, honor_fmi_dmi_table);

static int honor_fmi_read_rpm(struct honor_fmi_data *data, int channel,
			      long *rpm)
{
	union acpi_object input = {
		.buffer = {
			.type = ACPI_TYPE_BUFFER,
			.length = 3,
		},
	};
	struct acpi_object_list arguments = {
		.count = 1,
		.pointer = &input,
	};
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *result;
	u8 input_bytes[3] = { 0, 0, channel };
	acpi_status status;
	int ret = 0;

	input.buffer.pointer = input_bytes;

	status = acpi_evaluate_object(data->gfns, NULL, &arguments, &output);
	if (ACPI_FAILURE(status))
		return -EIO;

	result = output.pointer;
	if (!result || result->type != ACPI_TYPE_BUFFER ||
	    result->buffer.length < HONOR_FMI_GFNS_RESULT_SIZE) {
		ret = -EPROTO;
		goto out_free;
	}

	if (result->buffer.pointer[0]) {
		ret = -EIO;
		goto out_free;
	}

	*rpm = result->buffer.pointer[1] |
	       (result->buffer.pointer[2] << 8);

out_free:
	kfree(output.pointer);
	return ret;
}

static umode_t honor_fmi_is_visible(const void *data,
				    enum hwmon_sensor_types type, u32 attr,
				    int channel)
{
	return 0444;
}

static int honor_fmi_read(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long *value)
{
	struct honor_fmi_data *data = dev_get_drvdata(dev);

	return honor_fmi_read_rpm(data, channel, value);
}

static const struct hwmon_ops honor_fmi_hwmon_ops = {
	.is_visible = honor_fmi_is_visible,
	.read = honor_fmi_read,
};

static const struct hwmon_channel_info * const honor_fmi_hwmon_info[] = {
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT, HWMON_F_INPUT),
	NULL
};

static const struct hwmon_chip_info honor_fmi_chip_info = {
	.ops = &honor_fmi_hwmon_ops,
	.info = honor_fmi_hwmon_info,
};

static int honor_fmi_probe(struct platform_device *pdev)
{
	struct honor_fmi_data *data;
	struct device *hwmon_dev;
	acpi_status status;

	if (!dmi_check_system(honor_fmi_dmi_table))
		return -ENODEV;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	status = acpi_get_handle(NULL, "\\GFNS", &data->gfns);
	if (ACPI_FAILURE(status))
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "firmware does not provide \\GFNS\n");

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev, "honor_fmi",
							 data,
							 &honor_fmi_chip_info,
							 NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct platform_driver honor_fmi_driver = {
	.probe = honor_fmi_probe,
	.driver = {
		.name = "honor-fmi-hwmon",
	},
};

static struct platform_device *honor_fmi_device;

static int __init honor_fmi_init(void)
{
	int ret;

	if (!dmi_check_system(honor_fmi_dmi_table))
		return -ENODEV;

	ret = platform_driver_register(&honor_fmi_driver);
	if (ret)
		return ret;

	honor_fmi_device = platform_device_register_simple("honor-fmi-hwmon",
							   PLATFORM_DEVID_NONE,
							   NULL, 0);
	if (IS_ERR(honor_fmi_device)) {
		platform_driver_unregister(&honor_fmi_driver);
		return PTR_ERR(honor_fmi_device);
	}

	return 0;
}

static void __exit honor_fmi_exit(void)
{
	platform_device_unregister(honor_fmi_device);
	platform_driver_unregister(&honor_fmi_driver);
}

module_init(honor_fmi_init);
module_exit(honor_fmi_exit);

MODULE_AUTHOR("Nikita Dubrovskih <testname142@gmail.com>");
MODULE_DESCRIPTION("HONOR FMI-XX fan speed monitor");
MODULE_LICENSE("GPL");
