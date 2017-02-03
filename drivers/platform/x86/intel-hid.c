/*
 *  Intel HID event driver for Windows 8
 *
 *  Copyright (C) 2015 Alex Hung <alex.hung@canonical.com>
 *  Copyright (C) 2015 Andrew Lutomirski <luto@kernel.org>
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
#include <acpi/acpi_bus.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex Hung");

static const struct acpi_device_id intel_hid_ids[] = {
	{"INT33D5", 0},
	{"", 0},
};

/* In theory, these are HID usages. */
static const struct key_entry intel_hid_keymap[] = {
	/* 1: LSuper (Page 0x07, usage 0xE3) -- unclear what to do */
	/* 2: Toggle SW_ROTATE_LOCK -- easy to implement if seen in wild */
	{ KE_KEY, 3, { KEY_NUMLOCK } },
	{ KE_KEY, 4, { KEY_HOME } },
	{ KE_KEY, 5, { KEY_END } },
	{ KE_KEY, 6, { KEY_PAGEUP } },
	{ KE_KEY, 7, { KEY_PAGEDOWN } },
	{ KE_KEY, 8, { KEY_RFKILL } },
	{ KE_KEY, 9, { KEY_POWER } },
	{ KE_KEY, 11, { KEY_SLEEP } },
	/* 13 has two different meanings in the spec -- ignore it. */
	{ KE_KEY, 14, { KEY_STOPCD } },
	{ KE_KEY, 15, { KEY_PLAYPAUSE } },
	{ KE_KEY, 16, { KEY_MUTE } },
	{ KE_KEY, 17, { KEY_VOLUMEUP } },
	{ KE_KEY, 18, { KEY_VOLUMEDOWN } },
	{ KE_KEY, 19, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 20, { KEY_BRIGHTNESSDOWN } },
	/* 27: wake -- needs special handling */
	{ KE_END },
};

struct intel_hid_priv {
	struct input_dev *input_dev;
};

static int intel_hid_set_enable(struct device *device, int enable)
{
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };
	acpi_status status;

	arg0.integer.value = enable;
	status = acpi_evaluate_object(ACPI_HANDLE(device), "HDSM", &args, NULL);
	if (!ACPI_SUCCESS(status)) {
		dev_warn(device, "failed to %sable hotkeys\n",
			 enable ? "en" : "dis");
		return -EIO;
	}

	return 0;
}

static int intel_hid_pl_suspend_handler(struct device *device)
{
	intel_hid_set_enable(device, 0);
	return 0;
}

static int intel_hid_pl_resume_handler(struct device *device)
{
	intel_hid_set_enable(device, 1);
	return 0;
}

static const struct dev_pm_ops intel_hid_pl_pm_ops = {
	.freeze  = intel_hid_pl_suspend_handler,
	.restore  = intel_hid_pl_resume_handler,
	.suspend  = intel_hid_pl_suspend_handler,
	.resume  = intel_hid_pl_resume_handler,
};

static int intel_hid_input_setup(struct platform_device *device)
{
	struct intel_hid_priv *priv = dev_get_drvdata(&device->dev);
	int ret;

	priv->input_dev = input_allocate_device();
	if (!priv->input_dev)
		return -ENOMEM;

	ret = sparse_keymap_setup(priv->input_dev, intel_hid_keymap, NULL);
	if (ret)
		goto err_free_device;

	priv->input_dev->dev.parent = &device->dev;
	priv->input_dev->name = "Intel HID events";
	priv->input_dev->id.bustype = BUS_HOST;
	set_bit(KEY_RFKILL, priv->input_dev->keybit);

	ret = input_register_device(priv->input_dev);
	if (ret)
		goto err_free_device;

	return 0;

err_free_device:
	input_free_device(priv->input_dev);
	return ret;
}

static void intel_hid_input_destroy(struct platform_device *device)
{
	struct intel_hid_priv *priv = dev_get_drvdata(&device->dev);

	input_unregister_device(priv->input_dev);
}

static void notify_handler(acpi_handle handle, u32 event, void *context)
{
	struct platform_device *device = context;
	struct intel_hid_priv *priv = dev_get_drvdata(&device->dev);
	unsigned long long ev_index;
	acpi_status status;

	/* The platform spec only defines one event code: 0xC0. */
	if (event != 0xc0) {
		dev_warn(&device->dev, "received unknown event (0x%x)\n",
			 event);
		return;
	}

	status = acpi_evaluate_integer(handle, "HDEM", NULL, &ev_index);
	if (!ACPI_SUCCESS(status)) {
		dev_warn(&device->dev, "failed to get event index\n");
		return;
	}

	if (!sparse_keymap_report_event(priv->input_dev, ev_index, 1, true))
		dev_info(&device->dev, "unknown event index 0x%llx\n",
			 ev_index);
}

static int intel_hid_probe(struct platform_device *device)
{
	acpi_handle handle = ACPI_HANDLE(&device->dev);
	struct intel_hid_priv *priv;
	unsigned long long mode;
	acpi_status status;
	int err;

	status = acpi_evaluate_integer(handle, "HDMM", NULL, &mode);
	if (!ACPI_SUCCESS(status)) {
		dev_warn(&device->dev, "failed to read mode\n");
		return -ENODEV;
	}

	if (mode != 0) {
		/*
		 * This driver only implements "simple" mode.  There appear
		 * to be no other modes, but we should be paranoid and check
		 * for compatibility.
		 */
		dev_info(&device->dev, "platform is not in simple mode\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(&device->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_set_drvdata(&device->dev, priv);

	err = intel_hid_input_setup(device);
	if (err) {
		pr_err("Failed to setup Intel HID hotkeys\n");
		return err;
	}

	status = acpi_install_notify_handler(handle,
					     ACPI_DEVICE_NOTIFY,
					     notify_handler,
					     device);
	if (ACPI_FAILURE(status)) {
		err = -EBUSY;
		goto err_remove_input;
	}

	err = intel_hid_set_enable(&device->dev, 1);
	if (err)
		goto err_remove_notify;

	return 0;

err_remove_notify:
	acpi_remove_notify_handler(handle, ACPI_DEVICE_NOTIFY, notify_handler);

err_remove_input:
	intel_hid_input_destroy(device);

	return err;
}

static int intel_hid_remove(struct platform_device *device)
{
	acpi_handle handle = ACPI_HANDLE(&device->dev);

	acpi_remove_notify_handler(handle, ACPI_DEVICE_NOTIFY, notify_handler);
	intel_hid_input_destroy(device);
	intel_hid_set_enable(&device->dev, 0);

	/*
	 * Even if we failed to shut off the event stream, we can still
	 * safely detach from the device.
	 */
	return 0;
}

static struct platform_driver intel_hid_pl_driver = {
	.driver = {
		.name = "intel-hid",
		.acpi_match_table = intel_hid_ids,
		.pm = &intel_hid_pl_pm_ops,
	},
	.probe = intel_hid_probe,
	.remove = intel_hid_remove,
};
MODULE_DEVICE_TABLE(acpi, intel_hid_ids);

/*
 * Unfortunately, some laptops provide a _HID="INT33D5" device with
 * _CID="PNP0C02".  This causes the pnpacpi scan driver to claim the
 * ACPI node, so no platform device will be created.  The pnpacpi
 * driver rejects this device in subsequent processing, so no physical
 * node is created at all.
 *
 * As a workaround until the ACPI core figures out how to handle
 * this corner case, manually ask the ACPI platform device code to
 * claim the ACPI node.
 */
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
				 "intel-hid: created platform device\n");

	return AE_OK;
}

static int __init intel_hid_init(void)
{
	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX, check_acpi_dev, NULL,
			    (void *)intel_hid_ids, NULL);

	return platform_driver_register(&intel_hid_pl_driver);
}
module_init(intel_hid_init);

static void __exit intel_hid_exit(void)
{
	platform_driver_unregister(&intel_hid_pl_driver);
}
module_exit(intel_hid_exit);
