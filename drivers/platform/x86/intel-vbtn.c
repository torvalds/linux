// SPDX-License-Identifier: GPL-2.0+
/*
 *  Intel Virtual Button driver for Windows 8.1+
 *
 *  Copyright (C) 2016 AceLan Kao <acelan.kao@canonical.com>
 *  Copyright (C) 2016 Alex Hung <alex.hung@canonical.com>
 */

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>

/* When NOT in tablet mode, VGBS returns with the flag 0x40 */
#define TABLET_MODE_FLAG 0x40
#define DOCK_MODE_FLAG   0x80

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
	{ KE_KEY, 0xC2, { KEY_LEFTMETA } },		/* 'Windows' key press */
	{ KE_KEY, 0xC3, { KEY_LEFTMETA } },		/* 'Windows' key release */
	{ KE_KEY, 0xC4, { KEY_VOLUMEUP } },		/* volume-up key press */
	{ KE_IGNORE, 0xC5, { KEY_VOLUMEUP } },		/* volume-up key release */
	{ KE_KEY, 0xC6, { KEY_VOLUMEDOWN } },		/* volume-down key press */
	{ KE_IGNORE, 0xC7, { KEY_VOLUMEDOWN } },	/* volume-down key release */
	{ KE_KEY,    0xC8, { KEY_ROTATE_LOCK_TOGGLE } },	/* rotate-lock key press */
	{ KE_KEY,    0xC9, { KEY_ROTATE_LOCK_TOGGLE } },	/* rotate-lock key release */
};

static const struct key_entry intel_vbtn_switchmap[] = {
	{ KE_SW,     0xCA, { .sw = { SW_DOCK, 1 } } },		/* Docked */
	{ KE_SW,     0xCB, { .sw = { SW_DOCK, 0 } } },		/* Undocked */
	{ KE_SW,     0xCC, { .sw = { SW_TABLET_MODE, 1 } } },	/* Tablet */
	{ KE_SW,     0xCD, { .sw = { SW_TABLET_MODE, 0 } } },	/* Laptop */
};

#define KEYMAP_LEN \
	(ARRAY_SIZE(intel_vbtn_keymap) + ARRAY_SIZE(intel_vbtn_switchmap) + 1)

struct intel_vbtn_priv {
	struct key_entry keymap[KEYMAP_LEN];
	struct input_dev *input_dev;
	bool has_switches;
	bool wakeup_mode;
};

static int intel_vbtn_input_setup(struct platform_device *device)
{
	struct intel_vbtn_priv *priv = dev_get_drvdata(&device->dev);
	int ret, keymap_len = 0;

	if (true) {
		memcpy(&priv->keymap[keymap_len], intel_vbtn_keymap,
		       ARRAY_SIZE(intel_vbtn_keymap) *
		       sizeof(struct key_entry));
		keymap_len += ARRAY_SIZE(intel_vbtn_keymap);
	}

	if (priv->has_switches) {
		memcpy(&priv->keymap[keymap_len], intel_vbtn_switchmap,
		       ARRAY_SIZE(intel_vbtn_switchmap) *
		       sizeof(struct key_entry));
		keymap_len += ARRAY_SIZE(intel_vbtn_switchmap);
	}

	priv->keymap[keymap_len].type = KE_END;

	priv->input_dev = devm_input_allocate_device(&device->dev);
	if (!priv->input_dev)
		return -ENOMEM;

	ret = sparse_keymap_setup(priv->input_dev, priv->keymap, NULL);
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
	unsigned int val = !(event & 1); /* Even=press, Odd=release */
	const struct key_entry *ke, *ke_rel;
	bool autorelease;

	if (priv->wakeup_mode) {
		ke = sparse_keymap_entry_from_scancode(priv->input_dev, event);
		if (ke) {
			pm_wakeup_hard_event(&device->dev);

			/*
			 * Switch events like tablet mode will wake the device
			 * and report the new switch position to the input
			 * subsystem.
			 */
			if (ke->type == KE_SW)
				sparse_keymap_report_event(priv->input_dev,
							   event,
							   val,
							   0);
			return;
		}
		goto out_unknown;
	}

	/*
	 * Even press events are autorelease if there is no corresponding odd
	 * release event, or if the odd event is KE_IGNORE.
	 */
	ke_rel = sparse_keymap_entry_from_scancode(priv->input_dev, event | 1);
	autorelease = val && (!ke_rel || ke_rel->type == KE_IGNORE);

	if (sparse_keymap_report_event(priv->input_dev, event, val, autorelease))
		return;

out_unknown:
	dev_dbg(&device->dev, "unknown event index 0x%x\n", event);
}

static void detect_tablet_mode(struct platform_device *device)
{
	struct intel_vbtn_priv *priv = dev_get_drvdata(&device->dev);
	acpi_handle handle = ACPI_HANDLE(&device->dev);
	unsigned long long vgbs;
	acpi_status status;
	int m;

	status = acpi_evaluate_integer(handle, "VGBS", NULL, &vgbs);
	if (ACPI_FAILURE(status))
		return;

	m = !(vgbs & TABLET_MODE_FLAG);
	input_report_switch(priv->input_dev, SW_TABLET_MODE, m);
	m = (vgbs & DOCK_MODE_FLAG) ? 1 : 0;
	input_report_switch(priv->input_dev, SW_DOCK, m);
}

static bool intel_vbtn_has_switches(acpi_handle handle)
{
	const char *chassis_type = dmi_get_system_info(DMI_CHASSIS_TYPE);
	unsigned long long vgbs;
	acpi_status status;

	if (!(chassis_type && strcmp(chassis_type, "31") == 0))
		return false;

	status = acpi_evaluate_integer(handle, "VGBS", NULL, &vgbs);
	return ACPI_SUCCESS(status);
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

	priv->has_switches = intel_vbtn_has_switches(handle);

	err = intel_vbtn_input_setup(device);
	if (err) {
		pr_err("Failed to setup Intel Virtual Button\n");
		return err;
	}

	if (priv->has_switches)
		detect_tablet_mode(device);

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

	device_init_wakeup(&device->dev, false);
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
