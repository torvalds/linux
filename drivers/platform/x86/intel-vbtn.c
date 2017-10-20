/*
 *  Intel Virtual Button driver for Windows 8.1+
 *
 *  Copyright (C) 2016 AceLan Kao <acelan.kao@canonical.com>
 *  Copyright (C) 2016 Alex Hung <alex.hung@canonical.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/input/sparse-keymap.h>
#include <linux/acpi.h>
#include <linux/suspend.h>
#include <acpi/acpi_bus.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AceLan Kao");

static const struct acpi_device_id intel_vbtn_ids[] = {
	{"INT33D6", 0},
	{"", 0},
};

/* In theory, these are HID usages. */
static const struct key_entry intel_vbtn_keymap[] = {
	{ KE_KEY, 0xC0, { KEY_POWER } },	/* power key press */
	{ KE_IGNORE, 0xC1, { KEY_POWER } },	/* power key release */
	{ KE_KEY, 0xC4, { KEY_VOLUMEUP } },		/* volume-up key press */
	{ KE_IGNORE, 0xC5, { KEY_VOLUMEUP } },		/* volume-up key release */
	{ KE_KEY, 0xC6, { KEY_VOLUMEDOWN } },		/* volume-down key press */
	{ KE_IGNORE, 0xC7, { KEY_VOLUMEDOWN } },	/* volume-down key release */
	{ KE_END },
};

struct intel_vbtn_priv {
	struct input_dev *input_dev;
	bool wakeup_mode;
};

static int intel_vbtn_input_setup(struct platform_device *device)
{
	struct intel_vbtn_priv *priv = dev_get_drvdata(&device->dev);
	int ret;

	priv->input_dev = devm_input_allocate_device(&device->dev);
	if (!priv->input_dev)
		return -ENOMEM;

	ret = sparse_keymap_setup(priv->input_dev, intel_vbtn_keymap, NULL);
	if (ret)
		return ret;

	priv->input_dev->dev.parent = &device->dev;
	priv->input_dev->name = "Intel Virtual Button driver";
	priv->input_dev->id.bustype = BUS_HOST;

	return input_register_device(priv->input_dev);
}

static void notify_handler(acpi_handle handle, u32 event, void *context)
{
	struct platform_device *device = context;
	struct intel_vbtn_priv *priv = dev_get_drvdata(&device->dev);

	if (priv->wakeup_mode) {
		if (sparse_keymap_entry_from_scancode(priv->input_dev, event)) {
			pm_wakeup_hard_event(&device->dev);
			return;
		}
	} else if (sparse_keymap_report_event(priv->input_dev, event, 1, true)) {
		return;
	}
	dev_dbg(&device->dev, "unknown event index 0x%x\n", event);
}

static int intel_vbtn_probe(struct platform_device *device)
{
	acpi_handle handle = ACPI_HANDLE(&device->dev);
	struct intel_vbtn_priv *priv;
	acpi_status status;
	int err;

	status = acpi_evaluate_object(handle, "VBDL", NULL, NULL);
	if (ACPI_FAILURE(status)) {
		dev_warn(&device->dev, "failed to read Intel Virtual Button driver\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(&device->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_set_drvdata(&device->dev, priv);

	err = intel_vbtn_input_setup(device);
	if (err) {
		pr_err("Failed to setup Intel Virtual Button\n");
		return err;
	}

	status = acpi_install_notify_handler(handle,
					     ACPI_DEVICE_NOTIFY,
					     notify_handler,
					     device);
	if (ACPI_FAILURE(status))
		return -EBUSY;

	device_init_wakeup(&device->dev, true);
	return 0;
}

static int intel_vbtn_remove(struct platform_device *device)
{
	acpi_handle handle = ACPI_HANDLE(&device->dev);

	acpi_remove_notify_handler(handle, ACPI_DEVICE_NOTIFY, notify_handler);

	/*
	 * Even if we failed to shut off the event stream, we can still
	 * safely detach from the device.
	 */
	return 0;
}

static int intel_vbtn_pm_prepare(struct device *dev)
{
	struct intel_vbtn_priv *priv = dev_get_drvdata(dev);

	priv->wakeup_mode = true;
	return 0;
}

static int intel_vbtn_pm_resume(struct device *dev)
{
	struct intel_vbtn_priv *priv = dev_get_drvdata(dev);

	priv->wakeup_mode = false;
	return 0;
}

static const struct dev_pm_ops intel_vbtn_pm_ops = {
	.prepare = intel_vbtn_pm_prepare,
	.resume = intel_vbtn_pm_resume,
	.restore = intel_vbtn_pm_resume,
	.thaw = intel_vbtn_pm_resume,
};

static struct platform_driver intel_vbtn_pl_driver = {
	.driver = {
		.name = "intel-vbtn",
		.acpi_match_table = intel_vbtn_ids,
		.pm = &intel_vbtn_pm_ops,
	},
	.probe = intel_vbtn_probe,
	.remove = intel_vbtn_remove,
};
MODULE_DEVICE_TABLE(acpi, intel_vbtn_ids);

static acpi_status __init
check_acpi_dev(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	const struct acpi_device_id *ids = context;
	struct acpi_device *dev;

	if (acpi_bus_get_device(handle, &dev) != 0)
		return AE_OK;

	if (acpi_match_device_ids(dev, ids) == 0)
		if (acpi_create_platform_device(dev, NULL))
			dev_info(&dev->dev,
				 "intel-vbtn: created platform device\n");

	return AE_OK;
}

static int __init intel_vbtn_init(void)
{
	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX, check_acpi_dev, NULL,
			    (void *)intel_vbtn_ids, NULL);

	return platform_driver_register(&intel_vbtn_pl_driver);
}
module_init(intel_vbtn_init);

static void __exit intel_vbtn_exit(void)
{
	platform_driver_unregister(&intel_vbtn_pl_driver);
}
module_exit(intel_vbtn_exit);
