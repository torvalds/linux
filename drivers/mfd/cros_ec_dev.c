// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cros_ec_dev - expose the Chrome OS Embedded Controller to user-space
 *
 * Copyright (C) 2014 Google, Inc.
 */

#include <linux/dmi.h>
#include <linux/kconfig.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/platform_data/cros_ec_chardev.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/slab.h>

#define DRV_NAME "cros-ec-dev"

static struct class cros_class = {
	.owner          = THIS_MODULE,
	.name           = "chromeos",
};

/**
 * struct cros_feature_to_name - CrOS feature id to name/short description.
 * @id: The feature identifier.
 * @name: Device name associated with the feature id.
 * @desc: Short name that will be displayed.
 */
struct cros_feature_to_name {
	unsigned int id;
	const char *name;
	const char *desc;
};

/**
 * struct cros_feature_to_cells - CrOS feature id to mfd cells association.
 * @id: The feature identifier.
 * @mfd_cells: Pointer to the array of mfd cells that needs to be added.
 * @num_cells: Number of mfd cells into the array.
 */
struct cros_feature_to_cells {
	unsigned int id;
	const struct mfd_cell *mfd_cells;
	unsigned int num_cells;
};

static const struct cros_feature_to_name cros_mcu_devices[] = {
	{
		.id	= EC_FEATURE_FINGERPRINT,
		.name	= CROS_EC_DEV_FP_NAME,
		.desc	= "Fingerprint",
	},
	{
		.id	= EC_FEATURE_ISH,
		.name	= CROS_EC_DEV_ISH_NAME,
		.desc	= "Integrated Sensor Hub",
	},
	{
		.id	= EC_FEATURE_SCP,
		.name	= CROS_EC_DEV_SCP_NAME,
		.desc	= "System Control Processor",
	},
	{
		.id	= EC_FEATURE_TOUCHPAD,
		.name	= CROS_EC_DEV_TP_NAME,
		.desc	= "Touchpad",
	},
};

static const struct mfd_cell cros_ec_cec_cells[] = {
	{ .name = "cros-ec-cec", },
};

static const struct mfd_cell cros_ec_rtc_cells[] = {
	{ .name = "cros-ec-rtc", },
};

static const struct mfd_cell cros_ec_sensorhub_cells[] = {
	{ .name = "cros-ec-sensorhub", },
};

static const struct mfd_cell cros_usbpd_charger_cells[] = {
	{ .name = "cros-usbpd-charger", },
	{ .name = "cros-usbpd-logger", },
};

static const struct mfd_cell cros_usbpd_notify_cells[] = {
	{ .name = "cros-usbpd-notify", },
};

static const struct cros_feature_to_cells cros_subdevices[] = {
	{
		.id		= EC_FEATURE_CEC,
		.mfd_cells	= cros_ec_cec_cells,
		.num_cells	= ARRAY_SIZE(cros_ec_cec_cells),
	},
	{
		.id		= EC_FEATURE_RTC,
		.mfd_cells	= cros_ec_rtc_cells,
		.num_cells	= ARRAY_SIZE(cros_ec_rtc_cells),
	},
	{
		.id		= EC_FEATURE_USB_PD,
		.mfd_cells	= cros_usbpd_charger_cells,
		.num_cells	= ARRAY_SIZE(cros_usbpd_charger_cells),
	},
};

static const struct mfd_cell cros_ec_platform_cells[] = {
	{ .name = "cros-ec-chardev", },
	{ .name = "cros-ec-debugfs", },
	{ .name = "cros-ec-sysfs", },
	{ .name = "cros-ec-pchg", },
};

static const struct mfd_cell cros_ec_lightbar_cells[] = {
	{ .name = "cros-ec-lightbar", }
};

static const struct mfd_cell cros_ec_vbc_cells[] = {
	{ .name = "cros-ec-vbc", }
};

static void cros_ec_class_release(struct device *dev)
{
	kfree(to_cros_ec_dev(dev));
}

static int ec_device_probe(struct platform_device *pdev)
{
	int retval = -ENOMEM;
	struct device_node *node;
	struct device *dev = &pdev->dev;
	struct cros_ec_platform *ec_platform = dev_get_platdata(dev);
	struct cros_ec_dev *ec = kzalloc(sizeof(*ec), GFP_KERNEL);
	int i;

	if (!ec)
		return retval;

	dev_set_drvdata(dev, ec);
	ec->ec_dev = dev_get_drvdata(dev->parent);
	ec->dev = dev;
	ec->cmd_offset = ec_platform->cmd_offset;
	ec->features[0] = -1U; /* Not cached yet */
	ec->features[1] = -1U; /* Not cached yet */
	device_initialize(&ec->class_dev);

	for (i = 0; i < ARRAY_SIZE(cros_mcu_devices); i++) {
		/*
		 * Check whether this is actually a dedicated MCU rather
		 * than an standard EC.
		 */
		if (cros_ec_check_features(ec, cros_mcu_devices[i].id)) {
			dev_info(dev, "CrOS %s MCU detected\n",
				 cros_mcu_devices[i].desc);
			/*
			 * Help userspace differentiating ECs from other MCU,
			 * regardless of the probing order.
			 */
			ec_platform->ec_name = cros_mcu_devices[i].name;
			break;
		}
	}

	/*
	 * Add the class device
	 */
	ec->class_dev.class = &cros_class;
	ec->class_dev.parent = dev;
	ec->class_dev.release = cros_ec_class_release;

	retval = dev_set_name(&ec->class_dev, "%s", ec_platform->ec_name);
	if (retval) {
		dev_err(dev, "dev_set_name failed => %d\n", retval);
		goto failed;
	}

	retval = device_add(&ec->class_dev);
	if (retval)
		goto failed;

	/* check whether this EC is a sensor hub. */
	if (cros_ec_get_sensor_count(ec) > 0) {
		retval = mfd_add_hotplug_devices(ec->dev,
				cros_ec_sensorhub_cells,
				ARRAY_SIZE(cros_ec_sensorhub_cells));
		if (retval)
			dev_err(ec->dev, "failed to add %s subdevice: %d\n",
				cros_ec_sensorhub_cells->name, retval);
	}

	/*
	 * The following subdevices can be detected by sending the
	 * EC_FEATURE_GET_CMD Embedded Controller device.
	 */
	for (i = 0; i < ARRAY_SIZE(cros_subdevices); i++) {
		if (cros_ec_check_features(ec, cros_subdevices[i].id)) {
			retval = mfd_add_hotplug_devices(ec->dev,
						cros_subdevices[i].mfd_cells,
						cros_subdevices[i].num_cells);
			if (retval)
				dev_err(ec->dev,
					"failed to add %s subdevice: %d\n",
					cros_subdevices[i].mfd_cells->name,
					retval);
		}
	}

	/*
	 * Lightbar is a special case. Newer devices support autodetection,
	 * but older ones do not.
	 */
	if (cros_ec_check_features(ec, EC_FEATURE_LIGHTBAR) ||
	    dmi_match(DMI_PRODUCT_NAME, "Link")) {
		retval = mfd_add_hotplug_devices(ec->dev,
					cros_ec_lightbar_cells,
					ARRAY_SIZE(cros_ec_lightbar_cells));
		if (retval)
			dev_warn(ec->dev, "failed to add lightbar: %d\n",
				 retval);
	}

	/*
	 * The PD notifier driver cell is separate since it only needs to be
	 * explicitly added on platforms that don't have the PD notifier ACPI
	 * device entry defined.
	 */
	if (IS_ENABLED(CONFIG_OF) && ec->ec_dev->dev->of_node) {
		if (cros_ec_check_features(ec, EC_FEATURE_USB_PD)) {
			retval = mfd_add_hotplug_devices(ec->dev,
					cros_usbpd_notify_cells,
					ARRAY_SIZE(cros_usbpd_notify_cells));
			if (retval)
				dev_err(ec->dev,
					"failed to add PD notify devices: %d\n",
					retval);
		}
	}

	/*
	 * The following subdevices cannot be detected by sending the
	 * EC_FEATURE_GET_CMD to the Embedded Controller device.
	 */
	retval = mfd_add_hotplug_devices(ec->dev, cros_ec_platform_cells,
					 ARRAY_SIZE(cros_ec_platform_cells));
	if (retval)
		dev_warn(ec->dev,
			 "failed to add cros-ec platform devices: %d\n",
			 retval);

	/* Check whether this EC instance has a VBC NVRAM */
	node = ec->ec_dev->dev->of_node;
	if (of_property_read_bool(node, "google,has-vbc-nvram")) {
		retval = mfd_add_hotplug_devices(ec->dev, cros_ec_vbc_cells,
						ARRAY_SIZE(cros_ec_vbc_cells));
		if (retval)
			dev_warn(ec->dev, "failed to add VBC devices: %d\n",
				 retval);
	}

	return 0;

failed:
	put_device(&ec->class_dev);
	return retval;
}

static int ec_device_remove(struct platform_device *pdev)
{
	struct cros_ec_dev *ec = dev_get_drvdata(&pdev->dev);

	mfd_remove_devices(ec->dev);
	device_unregister(&ec->class_dev);
	return 0;
}

static const struct platform_device_id cros_ec_id[] = {
	{ DRV_NAME, 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, cros_ec_id);

static struct platform_driver cros_ec_dev_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.id_table = cros_ec_id,
	.probe = ec_device_probe,
	.remove = ec_device_remove,
};

static int __init cros_ec_dev_init(void)
{
	int ret;

	ret  = class_register(&cros_class);
	if (ret) {
		pr_err(CROS_EC_DEV_NAME ": failed to register device class\n");
		return ret;
	}

	/* Register the driver */
	ret = platform_driver_register(&cros_ec_dev_driver);
	if (ret < 0) {
		pr_warn(CROS_EC_DEV_NAME ": can't register driver: %d\n", ret);
		goto failed_devreg;
	}
	return 0;

failed_devreg:
	class_unregister(&cros_class);
	return ret;
}

static void __exit cros_ec_dev_exit(void)
{
	platform_driver_unregister(&cros_ec_dev_driver);
	class_unregister(&cros_class);
}

module_init(cros_ec_dev_init);
module_exit(cros_ec_dev_exit);

MODULE_ALIAS("platform:" DRV_NAME);
MODULE_AUTHOR("Bill Richardson <wfrichar@chromium.org>");
MODULE_DESCRIPTION("Userspace interface to the Chrome OS Embedded Controller");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
