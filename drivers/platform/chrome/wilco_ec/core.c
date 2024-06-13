// SPDX-License-Identifier: GPL-2.0
/*
 * Core driver for Wilco Embedded Controller
 *
 * Copyright 2018 Google LLC
 *
 * This is the entry point for the drivers that control the Wilco EC.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_data/wilco-ec.h>
#include <linux/platform_device.h>

#include "../cros_ec_lpc_mec.h"

#define DRV_NAME "wilco-ec"

static struct resource *wilco_get_resource(struct platform_device *pdev,
					   int index)
{
	struct device *dev = &pdev->dev;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_IO, index);
	if (!res) {
		dev_dbg(dev, "Couldn't find IO resource %d\n", index);
		return res;
	}

	return devm_request_region(dev, res->start, resource_size(res),
				   dev_name(dev));
}

static int wilco_ec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wilco_ec_device *ec;
	int ret;

	ec = devm_kzalloc(dev, sizeof(*ec), GFP_KERNEL);
	if (!ec)
		return -ENOMEM;

	platform_set_drvdata(pdev, ec);
	ec->dev = dev;
	mutex_init(&ec->mailbox_lock);

	ec->data_size = sizeof(struct wilco_ec_response) + EC_MAILBOX_DATA_SIZE;
	ec->data_buffer = devm_kzalloc(dev, ec->data_size, GFP_KERNEL);
	if (!ec->data_buffer)
		return -ENOMEM;

	/* Prepare access to IO regions provided by ACPI */
	ec->io_data = wilco_get_resource(pdev, 0);	/* Host Data */
	ec->io_command = wilco_get_resource(pdev, 1);	/* Host Command */
	ec->io_packet = wilco_get_resource(pdev, 2);	/* MEC EMI */
	if (!ec->io_data || !ec->io_command || !ec->io_packet)
		return -ENODEV;

	/* Initialize cros_ec register interface for communication */
	cros_ec_lpc_mec_init(ec->io_packet->start,
			     ec->io_packet->start + EC_MAILBOX_DATA_SIZE);

	/*
	 * Register a child device that will be found by the debugfs driver.
	 * Ignore failure.
	 */
	ec->debugfs_pdev = platform_device_register_data(dev,
							 "wilco-ec-debugfs",
							 PLATFORM_DEVID_AUTO,
							 NULL, 0);

	/* Register a child device that will be found by the RTC driver. */
	ec->rtc_pdev = platform_device_register_data(dev, "rtc-wilco-ec",
						     PLATFORM_DEVID_AUTO,
						     NULL, 0);
	if (IS_ERR(ec->rtc_pdev)) {
		dev_err(dev, "Failed to create RTC platform device\n");
		ret = PTR_ERR(ec->rtc_pdev);
		goto unregister_debugfs;
	}

	/* Set up the keyboard backlight LEDs. */
	ret = wilco_keyboard_leds_init(ec);
	if (ret < 0) {
		dev_err(dev,
			"Failed to initialize keyboard LEDs: %d\n",
			ret);
		goto unregister_rtc;
	}

	ret = wilco_ec_add_sysfs(ec);
	if (ret < 0) {
		dev_err(dev, "Failed to create sysfs entries: %d\n", ret);
		goto unregister_rtc;
	}

	/* Register child device to be found by charger config driver. */
	ec->charger_pdev = platform_device_register_data(dev, "wilco-charger",
							 PLATFORM_DEVID_AUTO,
							 NULL, 0);
	if (IS_ERR(ec->charger_pdev)) {
		dev_err(dev, "Failed to create charger platform device\n");
		ret = PTR_ERR(ec->charger_pdev);
		goto remove_sysfs;
	}

	/* Register child device that will be found by the telemetry driver. */
	ec->telem_pdev = platform_device_register_data(dev, "wilco_telem",
						       PLATFORM_DEVID_AUTO,
						       ec, sizeof(*ec));
	if (IS_ERR(ec->telem_pdev)) {
		dev_err(dev, "Failed to create telemetry platform device\n");
		ret = PTR_ERR(ec->telem_pdev);
		goto unregister_charge_config;
	}

	return 0;

unregister_charge_config:
	platform_device_unregister(ec->charger_pdev);
remove_sysfs:
	wilco_ec_remove_sysfs(ec);
unregister_rtc:
	platform_device_unregister(ec->rtc_pdev);
unregister_debugfs:
	if (ec->debugfs_pdev)
		platform_device_unregister(ec->debugfs_pdev);
	cros_ec_lpc_mec_destroy();
	return ret;
}

static int wilco_ec_remove(struct platform_device *pdev)
{
	struct wilco_ec_device *ec = platform_get_drvdata(pdev);

	platform_device_unregister(ec->telem_pdev);
	platform_device_unregister(ec->charger_pdev);
	wilco_ec_remove_sysfs(ec);
	platform_device_unregister(ec->rtc_pdev);
	if (ec->debugfs_pdev)
		platform_device_unregister(ec->debugfs_pdev);

	/* Teardown cros_ec interface */
	cros_ec_lpc_mec_destroy();

	return 0;
}

static const struct acpi_device_id wilco_ec_acpi_device_ids[] = {
	{ "GOOG000C", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, wilco_ec_acpi_device_ids);

static struct platform_driver wilco_ec_driver = {
	.driver = {
		.name = DRV_NAME,
		.acpi_match_table = wilco_ec_acpi_device_ids,
	},
	.probe = wilco_ec_probe,
	.remove = wilco_ec_remove,
};

module_platform_driver(wilco_ec_driver);

MODULE_AUTHOR("Nick Crews <ncrews@chromium.org>");
MODULE_AUTHOR("Duncan Laurie <dlaurie@chromium.org>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ChromeOS Wilco Embedded Controller driver");
MODULE_ALIAS("platform:" DRV_NAME);
