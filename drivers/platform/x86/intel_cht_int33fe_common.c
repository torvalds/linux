// SPDX-License-Identifier: GPL-2.0
/*
 * Common code for Intel Cherry Trail ACPI INT33FE pseudo device drivers
 * (USB Micro-B and Type-C connector variants).
 *
 * Copyright (c) 2019 Yauhen Kharuzhy <jekhor@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "intel_cht_int33fe_common.h"

#define EXPECTED_PTYPE		4

static int cht_int33fe_i2c_res_filter(struct acpi_resource *ares, void *data)
{
	struct acpi_resource_i2c_serialbus *sb;
	int *count = data;

	if (i2c_acpi_get_i2c_resource(ares, &sb))
		(*count)++;

	return 1;
}

static int cht_int33fe_count_i2c_clients(struct device *dev)
{
	struct acpi_device *adev;
	LIST_HEAD(resource_list);
	int count = 0;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -EINVAL;

	acpi_dev_get_resources(adev, &resource_list,
			       cht_int33fe_i2c_res_filter, &count);

	acpi_dev_free_resource_list(&resource_list);

	return count;
}

static int cht_int33fe_check_hw_type(struct device *dev)
{
	unsigned long long ptyp;
	acpi_status status;
	int ret;

	status = acpi_evaluate_integer(ACPI_HANDLE(dev), "PTYP", NULL, &ptyp);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "Error getting PTYPE\n");
		return -ENODEV;
	}

	/*
	 * The same ACPI HID is used for different configurations check PTYP
	 * to ensure that we are dealing with the expected config.
	 */
	if (ptyp != EXPECTED_PTYPE)
		return -ENODEV;

	/* Check presence of INT34D3 (hardware-rev 3) expected for ptype == 4 */
	if (!acpi_dev_present("INT34D3", "1", 3)) {
		dev_err(dev, "Error PTYPE == %d, but no INT34D3 device\n",
			EXPECTED_PTYPE);
		return -ENODEV;
	}

	ret = cht_int33fe_count_i2c_clients(dev);
	if (ret < 0)
		return ret;

	switch (ret) {
	case 2:
		return INT33FE_HW_MICROB;
	case 4:
		return INT33FE_HW_TYPEC;
	default:
		return -ENODEV;
	}
}

static int cht_int33fe_probe(struct platform_device *pdev)
{
	struct cht_int33fe_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	ret = cht_int33fe_check_hw_type(dev);
	if (ret < 0)
		return ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;

	switch (ret) {
	case INT33FE_HW_MICROB:
		data->probe = cht_int33fe_microb_probe;
		data->remove = cht_int33fe_microb_remove;
		break;

	case INT33FE_HW_TYPEC:
		data->probe = cht_int33fe_typec_probe;
		data->remove = cht_int33fe_typec_remove;
		break;
	}

	platform_set_drvdata(pdev, data);

	return data->probe(data);
}

static int cht_int33fe_remove(struct platform_device *pdev)
{
	struct cht_int33fe_data *data = platform_get_drvdata(pdev);

	return data->remove(data);
}

static const struct acpi_device_id cht_int33fe_acpi_ids[] = {
	{ "INT33FE", },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cht_int33fe_acpi_ids);

static struct platform_driver cht_int33fe_driver = {
	.driver	= {
		.name = "Intel Cherry Trail ACPI INT33FE driver",
		.acpi_match_table = ACPI_PTR(cht_int33fe_acpi_ids),
	},
	.probe = cht_int33fe_probe,
	.remove = cht_int33fe_remove,
};

module_platform_driver(cht_int33fe_driver);

MODULE_DESCRIPTION("Intel Cherry Trail ACPI INT33FE pseudo device driver");
MODULE_AUTHOR("Yauhen Kharuzhy <jekhor@gmail.com>");
MODULE_LICENSE("GPL v2");
