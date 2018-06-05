// SPDX-License-Identifier: GPL-2.0
// Driver to detect Tablet Mode for ChromeOS convertible.
//
// Copyright (C) 2017 Google, Inc.
// Author: Gwendal Grignou <gwendal@chromium.org>
//
// On Chromebook using ACPI, this device listens for notification
// from GOOG0006 and issue method TBMC to retrieve the status.
//
// GOOG0006 issues the notification when it receives EC_HOST_EVENT_MODE_CHANGE
// from the EC.
// Method TBMC reads EC_ACPI_MEM_DEVICE_ORIENTATION byte from the shared
// memory region.

#include <linux/acpi.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/printk.h>

#define DRV_NAME "chromeos_tbmc"
#define ACPI_DRV_NAME "GOOG0006"

static int chromeos_tbmc_query_switch(struct acpi_device *adev,
				     struct input_dev *idev)
{
	unsigned long long state;
	acpi_status status;

	status = acpi_evaluate_integer(adev->handle, "TBMC", NULL, &state);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	/* input layer checks if event is redundant */
	input_report_switch(idev, SW_TABLET_MODE, state);
	input_sync(idev);

	return 0;
}

static __maybe_unused int chromeos_tbmc_resume(struct device *dev)
{
	struct acpi_device *adev = to_acpi_device(dev);

	return chromeos_tbmc_query_switch(adev, adev->driver_data);
}

static void chromeos_tbmc_notify(struct acpi_device *adev, u32 event)
{
	switch (event) {
	case 0x80:
		chromeos_tbmc_query_switch(adev, adev->driver_data);
		break;
	default:
		dev_err(&adev->dev, "Unexpected event: 0x%08X\n", event);
	}
}

static int chromeos_tbmc_open(struct input_dev *idev)
{
	struct acpi_device *adev = input_get_drvdata(idev);

	return chromeos_tbmc_query_switch(adev, idev);
}

static int chromeos_tbmc_add(struct acpi_device *adev)
{
	struct input_dev *idev;
	struct device *dev = &adev->dev;
	int ret;

	idev = devm_input_allocate_device(dev);
	if (!idev)
		return -ENOMEM;

	idev->name = "Tablet Mode Switch";
	idev->phys = acpi_device_hid(adev);

	idev->id.bustype = BUS_HOST;
	idev->id.version = 1;
	idev->id.product = 0;
	idev->open = chromeos_tbmc_open;

	input_set_drvdata(idev, adev);
	adev->driver_data = idev;

	input_set_capability(idev, EV_SW, SW_TABLET_MODE);
	ret = input_register_device(idev);
	if (ret) {
		dev_err(dev, "cannot register input device\n");
		return ret;
	}
	return 0;
}

static const struct acpi_device_id chromeos_tbmc_acpi_device_ids[] = {
	{ ACPI_DRV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, chromeos_tbmc_acpi_device_ids);

static const SIMPLE_DEV_PM_OPS(chromeos_tbmc_pm_ops, NULL,
		chromeos_tbmc_resume);

static struct acpi_driver chromeos_tbmc_driver = {
	.name = DRV_NAME,
	.class = DRV_NAME,
	.ids = chromeos_tbmc_acpi_device_ids,
	.ops = {
		.add = chromeos_tbmc_add,
		.notify = chromeos_tbmc_notify,
	},
	.drv.pm = &chromeos_tbmc_pm_ops,
};

module_acpi_driver(chromeos_tbmc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ChromeOS ACPI tablet switch driver");
