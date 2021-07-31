// SPDX-License-Identifier: GPL-2.0+
/*
 *  Intel HID event & 5 button array driver
 *
 *  Copyright (C) 2015 Alex Hung <alex.hung@canonical.com>
 *  Copyright (C) 2015 Andrew Lutomirski <luto@kernel.org>
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
#define TABLET_MODE_FLAG BIT(6)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex Hung");

static const struct acpi_device_id intel_hid_ids[] = {
	{"INT33D5", 0},
	{"INTC1051", 0},
	{"INTC1054", 0},
	{"INTC1070", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, intel_hid_ids);

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

/* 5 button array notification value. */
static const struct key_entry intel_array_keymap[] = {
	{ KE_KEY,    0xC2, { KEY_LEFTMETA } },                /* Press */
	{ KE_IGNORE, 0xC3, { KEY_LEFTMETA } },                /* Release */
	{ KE_KEY,    0xC4, { KEY_VOLUMEUP } },                /* Press */
	{ KE_IGNORE, 0xC5, { KEY_VOLUMEUP } },                /* Release */
	{ KE_KEY,    0xC6, { KEY_VOLUMEDOWN } },              /* Press */
	{ KE_IGNORE, 0xC7, { KEY_VOLUMEDOWN } },              /* Release */
	{ KE_KEY,    0xC8, { KEY_ROTATE_LOCK_TOGGLE } },      /* Press */
	{ KE_IGNORE, 0xC9, { KEY_ROTATE_LOCK_TOGGLE } },      /* Release */
	{ KE_KEY,    0xCE, { KEY_POWER } },                   /* Press */
	{ KE_IGNORE, 0xCF, { KEY_POWER } },                   /* Release */
	{ KE_END },
};

static const struct dmi_system_id button_array_table[] = {
	{
		.ident = "Wacom MobileStudio Pro 13",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Wacom Co.,Ltd"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Wacom MobileStudio Pro 13"),
		},
	},
	{
		.ident = "Wacom MobileStudio Pro 16",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Wacom Co.,Ltd"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Wacom MobileStudio Pro 16"),
		},
	},
	{
		.ident = "HP Spectre x2 (2015)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Spectre x2 Detachable"),
		},
	},
	{
		.ident = "Lenovo ThinkPad X1 Tablet Gen 2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_FAMILY, "ThinkPad X1 Tablet Gen 2"),
		},
	},
	{ }
};

/*
 * Some convertible use the intel-hid ACPI interface to report SW_TABLET_MODE,
 * these need to be compared via a DMI based authorization list because some
 * models have unreliable VGBS return which could cause incorrect
 * SW_TABLET_MODE report.
 */
static const struct dmi_system_id dmi_vgbs_allow_list[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Spectre x360 Convertible 15-df0xxx"),
		},
	},
	{ }
};

struct intel_hid_priv {
	struct input_dev *input_dev;
	struct input_dev *array;
	struct input_dev *switches;
	bool wakeup_mode;
};

#define HID_EVENT_FILTER_UUID	"eeec56b3-4442-408f-a792-4edd4d758054"

enum intel_hid_dsm_fn_codes {
	INTEL_HID_DSM_FN_INVALID,
	INTEL_HID_DSM_BTNL_FN,
	INTEL_HID_DSM_HDMM_FN,
	INTEL_HID_DSM_HDSM_FN,
	INTEL_HID_DSM_HDEM_FN,
	INTEL_HID_DSM_BTNS_FN,
	INTEL_HID_DSM_BTNE_FN,
	INTEL_HID_DSM_HEBC_V1_FN,
	INTEL_HID_DSM_VGBS_FN,
	INTEL_HID_DSM_HEBC_V2_FN,
	INTEL_HID_DSM_FN_MAX
};

static const char *intel_hid_dsm_fn_to_method[INTEL_HID_DSM_FN_MAX] = {
	NULL,
	"BTNL",
	"HDMM",
	"HDSM",
	"HDEM",
	"BTNS",
	"BTNE",
	"HEBC",
	"VGBS",
	"HEBC"
};

static unsigned long long intel_hid_dsm_fn_mask;
static guid_t intel_dsm_guid;

static bool intel_hid_execute_method(acpi_handle handle,
				     enum intel_hid_dsm_fn_codes fn_index,
				     unsigned long long arg)
{
	union acpi_object *obj, argv4, req;
	acpi_status status;
	char *method_name;

	if (fn_index <= INTEL_HID_DSM_FN_INVALID ||
	    fn_index >= INTEL_HID_DSM_FN_MAX)
		return false;

	method_name = (char *)intel_hid_dsm_fn_to_method[fn_index];

	if (!(intel_hid_dsm_fn_mask & BIT(fn_index)))
		goto skip_dsm_exec;

	/* All methods expects a package with one integer element */
	req.type = ACPI_TYPE_INTEGER;
	req.integer.value = arg;

	argv4.type = ACPI_TYPE_PACKAGE;
	argv4.package.count = 1;
	argv4.package.elements = &req;

	obj = acpi_evaluate_dsm(handle, &intel_dsm_guid, 1, fn_index, &argv4);
	if (obj) {
		acpi_handle_debug(handle, "Exec DSM Fn code: %d[%s] success\n",
				  fn_index, method_name);
		ACPI_FREE(obj);
		return true;
	}

skip_dsm_exec:
	status = acpi_execute_simple_method(handle, method_name, arg);
	if (ACPI_SUCCESS(status))
		return true;

	return false;
}

static bool intel_hid_evaluate_method(acpi_handle handle,
				      enum intel_hid_dsm_fn_codes fn_index,
				      unsigned long long *result)
{
	union acpi_object *obj;
	acpi_status status;
	char *method_name;

	if (fn_index <= INTEL_HID_DSM_FN_INVALID ||
	    fn_index >= INTEL_HID_DSM_FN_MAX)
		return false;

	method_name = (char *)intel_hid_dsm_fn_to_method[fn_index];

	if (!(intel_hid_dsm_fn_mask & fn_index))
		goto skip_dsm_eval;

	obj = acpi_evaluate_dsm_typed(handle, &intel_dsm_guid,
				      1, fn_index,
				      NULL,  ACPI_TYPE_INTEGER);
	if (obj) {
		*result = obj->integer.value;
		acpi_handle_debug(handle,
				  "Eval DSM Fn code: %d[%s] results: 0x%llx\n",
				  fn_index, method_name, *result);
		ACPI_FREE(obj);
		return true;
	}

skip_dsm_eval:
	status = acpi_evaluate_integer(handle, method_name, NULL, result);
	if (ACPI_SUCCESS(status))
		return true;

	return false;
}

static void intel_hid_init_dsm(acpi_handle handle)
{
	union acpi_object *obj;

	guid_parse(HID_EVENT_FILTER_UUID, &intel_dsm_guid);

	obj = acpi_evaluate_dsm_typed(handle, &intel_dsm_guid, 1, 0, NULL,
				      ACPI_TYPE_BUFFER);
	if (obj) {
		switch (obj->buffer.length) {
		default:
		case 2:
			intel_hid_dsm_fn_mask = *(u16 *)obj->buffer.pointer;
			break;
		case 1:
			intel_hid_dsm_fn_mask = *obj->buffer.pointer;
			break;
		case 0:
			acpi_handle_warn(handle, "intel_hid_dsm_fn_mask length is zero\n");
			intel_hid_dsm_fn_mask = 0;
			break;
		}
		ACPI_FREE(obj);
	}

	acpi_handle_debug(handle, "intel_hid_dsm_fn_mask = %llx\n",
			  intel_hid_dsm_fn_mask);
}

static int intel_hid_set_enable(struct device *device, bool enable)
{
	acpi_handle handle = ACPI_HANDLE(device);

	/* Enable|disable features - power button is always enabled */
	if (!intel_hid_execute_method(handle, INTEL_HID_DSM_HDSM_FN,
				      enable)) {
		dev_warn(device, "failed to %sable hotkeys\n",
			 enable ? "en" : "dis");
		return -EIO;
	}

	return 0;
}

static void intel_button_array_enable(struct device *device, bool enable)
{
	struct intel_hid_priv *priv = dev_get_drvdata(device);
	acpi_handle handle = ACPI_HANDLE(device);
	unsigned long long button_cap;
	acpi_status status;

	if (!priv->array)
		return;

	/* Query supported platform features */
	status = acpi_evaluate_integer(handle, "BTNC", NULL, &button_cap);
	if (ACPI_FAILURE(status)) {
		dev_warn(device, "failed to get button capability\n");
		return;
	}

	/* Enable|disable features - power button is always enabled */
	if (!intel_hid_execute_method(handle, INTEL_HID_DSM_BTNE_FN,
				      enable ? button_cap : 1))
		dev_warn(device, "failed to set button capability\n");
}

static int intel_hid_pm_prepare(struct device *device)
{
	if (device_may_wakeup(device)) {
		struct intel_hid_priv *priv = dev_get_drvdata(device);

		priv->wakeup_mode = true;
	}
	return 0;
}

static void intel_hid_pm_complete(struct device *device)
{
	struct intel_hid_priv *priv = dev_get_drvdata(device);

	priv->wakeup_mode = false;
}

static int intel_hid_pl_suspend_handler(struct device *device)
{
	intel_button_array_enable(device, false);

	if (!pm_suspend_no_platform())
		intel_hid_set_enable(device, false);

	return 0;
}

static int intel_hid_pl_resume_handler(struct device *device)
{
	intel_hid_pm_complete(device);

	if (!pm_suspend_no_platform())
		intel_hid_set_enable(device, true);

	intel_button_array_enable(device, true);
	return 0;
}

static const struct dev_pm_ops intel_hid_pl_pm_ops = {
	.prepare = intel_hid_pm_prepare,
	.complete = intel_hid_pm_complete,
	.freeze  = intel_hid_pl_suspend_handler,
	.thaw  = intel_hid_pl_resume_handler,
	.restore  = intel_hid_pl_resume_handler,
	.suspend  = intel_hid_pl_suspend_handler,
	.resume  = intel_hid_pl_resume_handler,
};

static int intel_hid_input_setup(struct platform_device *device)
{
	struct intel_hid_priv *priv = dev_get_drvdata(&device->dev);
	int ret;

	priv->input_dev = devm_input_allocate_device(&device->dev);
	if (!priv->input_dev)
		return -ENOMEM;

	ret = sparse_keymap_setup(priv->input_dev, intel_hid_keymap, NULL);
	if (ret)
		return ret;

	priv->input_dev->name = "Intel HID events";
	priv->input_dev->id.bustype = BUS_HOST;

	return input_register_device(priv->input_dev);
}

static int intel_button_array_input_setup(struct platform_device *device)
{
	struct intel_hid_priv *priv = dev_get_drvdata(&device->dev);
	int ret;

	/* Setup input device for 5 button array */
	priv->array = devm_input_allocate_device(&device->dev);
	if (!priv->array)
		return -ENOMEM;

	ret = sparse_keymap_setup(priv->array, intel_array_keymap, NULL);
	if (ret)
		return ret;

	priv->array->name = "Intel HID 5 button array";
	priv->array->id.bustype = BUS_HOST;

	return input_register_device(priv->array);
}

static int intel_hid_switches_setup(struct platform_device *device)
{
	struct intel_hid_priv *priv = dev_get_drvdata(&device->dev);

	/* Setup input device for switches */
	priv->switches = devm_input_allocate_device(&device->dev);
	if (!priv->switches)
		return -ENOMEM;

	__set_bit(EV_SW, priv->switches->evbit);
	__set_bit(SW_TABLET_MODE, priv->switches->swbit);

	priv->switches->name = "Intel HID switches";
	priv->switches->id.bustype = BUS_HOST;
	return input_register_device(priv->switches);
}

static void report_tablet_mode_state(struct platform_device *device)
{
	struct intel_hid_priv *priv = dev_get_drvdata(&device->dev);
	acpi_handle handle = ACPI_HANDLE(&device->dev);
	unsigned long long vgbs;
	int m;

	if (!intel_hid_evaluate_method(handle, INTEL_HID_DSM_VGBS_FN, &vgbs))
		return;

	m = !(vgbs & TABLET_MODE_FLAG);
	input_report_switch(priv->switches, SW_TABLET_MODE, m);
	input_sync(priv->switches);
}

static bool report_tablet_mode_event(struct input_dev *input_dev, u32 event)
{
	if (!input_dev)
		return false;

	switch (event) {
	case 0xcc:
		input_report_switch(input_dev, SW_TABLET_MODE, 1);
		input_sync(input_dev);
		return true;
	case 0xcd:
		input_report_switch(input_dev, SW_TABLET_MODE, 0);
		input_sync(input_dev);
		return true;
	default:
		return false;
	}
}

static void notify_handler(acpi_handle handle, u32 event, void *context)
{
	struct platform_device *device = context;
	struct intel_hid_priv *priv = dev_get_drvdata(&device->dev);
	unsigned long long ev_index;
	int err;

	/*
	 * Some convertible have unreliable VGBS return which could cause incorrect
	 * SW_TABLET_MODE report, in these cases we enable support when receiving
	 * the first event instead of during driver setup.
	 *
	 * Some 360 degree hinges (yoga) style 2-in-1 devices use 2 accelerometers
	 * to allow the OS to determine the angle between the display and the base
	 * of the device. On Windows these are read by a special HingeAngleService
	 * process which calls an ACPI DSM (Device Specific Method) on the
	 * ACPI KIOX010A device node for the sensor in the display, to let the
	 * firmware know if the 2-in-1 is in tablet- or laptop-mode so that it can
	 * disable the kbd and touchpad to avoid spurious input in tablet-mode.
	 *
	 * The linux kxcjk1013 driver calls the DSM for this once at probe time
	 * to ensure that the builtin kbd and touchpad work. On some devices this
	 * causes a "spurious" 0xcd event on the intel-hid ACPI dev. In this case
	 * there is not a functional tablet-mode switch, so we should not register
	 * the tablet-mode switch device.
	 */
	if (!priv->switches && (event == 0xcc || event == 0xcd) &&
	    !acpi_dev_present("KIOX010A", NULL, -1)) {
		dev_info(&device->dev, "switch event received, enable switches supports\n");
		err = intel_hid_switches_setup(device);
		if (err)
			pr_err("Failed to setup Intel HID switches\n");
	}

	if (priv->wakeup_mode) {
		/*
		 * Needed for wakeup from suspend-to-idle to work on some
		 * platforms that don't expose the 5-button array, but still
		 * send notifies with the power button event code to this
		 * device object on power button actions while suspended.
		 */
		if (event == 0xce)
			goto wakeup;

		/*
		 * Some devices send (duplicate) tablet-mode events when moved
		 * around even though the mode has not changed; and they do this
		 * even when suspended.
		 * Update the switch state in case it changed and then return
		 * without waking up to avoid spurious wakeups.
		 */
		if (event == 0xcc || event == 0xcd) {
			report_tablet_mode_event(priv->switches, event);
			return;
		}

		/* Wake up on 5-button array events only. */
		if (event == 0xc0 || !priv->array)
			return;

		if (!sparse_keymap_entry_from_scancode(priv->array, event)) {
			dev_info(&device->dev, "unknown event 0x%x\n", event);
			return;
		}

wakeup:
		pm_wakeup_hard_event(&device->dev);

		return;
	}

	/*
	 * Needed for suspend to work on some platforms that don't expose
	 * the 5-button array, but still send notifies with power button
	 * event code to this device object on power button actions.
	 *
	 * Report the power button press and release.
	 */
	if (!priv->array) {
		if (event == 0xce) {
			input_report_key(priv->input_dev, KEY_POWER, 1);
			input_sync(priv->input_dev);
			return;
		}

		if (event == 0xcf) {
			input_report_key(priv->input_dev, KEY_POWER, 0);
			input_sync(priv->input_dev);
			return;
		}
	}

	if (report_tablet_mode_event(priv->switches, event))
		return;

	/* 0xC0 is for HID events, other values are for 5 button array */
	if (event != 0xc0) {
		if (!priv->array ||
		    !sparse_keymap_report_event(priv->array, event, 1, true))
			dev_dbg(&device->dev, "unknown event 0x%x\n", event);
		return;
	}

	if (!intel_hid_evaluate_method(handle, INTEL_HID_DSM_HDEM_FN,
				       &ev_index)) {
		dev_warn(&device->dev, "failed to get event index\n");
		return;
	}

	if (!sparse_keymap_report_event(priv->input_dev, ev_index, 1, true))
		dev_dbg(&device->dev, "unknown event index 0x%llx\n",
			 ev_index);
}

static bool button_array_present(struct platform_device *device)
{
	acpi_handle handle = ACPI_HANDLE(&device->dev);
	unsigned long long event_cap;

	if (intel_hid_evaluate_method(handle, INTEL_HID_DSM_HEBC_V2_FN,
				      &event_cap)) {
		/* Check presence of 5 button array or v2 power button */
		if (event_cap & 0x60000)
			return true;
	}

	if (intel_hid_evaluate_method(handle, INTEL_HID_DSM_HEBC_V1_FN,
				      &event_cap)) {
		if (event_cap & 0x20000)
			return true;
	}

	if (dmi_check_system(button_array_table))
		return true;

	return false;
}

static int intel_hid_probe(struct platform_device *device)
{
	acpi_handle handle = ACPI_HANDLE(&device->dev);
	unsigned long long mode;
	struct intel_hid_priv *priv;
	acpi_status status;
	int err;

	intel_hid_init_dsm(handle);

	if (!intel_hid_evaluate_method(handle, INTEL_HID_DSM_HDMM_FN, &mode)) {
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

	/* Setup 5 button array */
	if (button_array_present(device)) {
		dev_info(&device->dev, "platform supports 5 button array\n");
		err = intel_button_array_input_setup(device);
		if (err)
			pr_err("Failed to setup Intel 5 button array hotkeys\n");
	}

	/* Setup switches for devices that we know VGBS return correctly */
	if (dmi_check_system(dmi_vgbs_allow_list)) {
		dev_info(&device->dev, "platform supports switches\n");
		err = intel_hid_switches_setup(device);
		if (err)
			pr_err("Failed to setup Intel HID switches\n");
		else
			report_tablet_mode_state(device);
	}

	status = acpi_install_notify_handler(handle,
					     ACPI_DEVICE_NOTIFY,
					     notify_handler,
					     device);
	if (ACPI_FAILURE(status))
		return -EBUSY;

	err = intel_hid_set_enable(&device->dev, true);
	if (err)
		goto err_remove_notify;

	if (priv->array) {
		unsigned long long dummy;

		intel_button_array_enable(&device->dev, true);

		/* Call button load method to enable HID power button */
		if (!intel_hid_evaluate_method(handle, INTEL_HID_DSM_BTNL_FN,
					       &dummy)) {
			dev_warn(&device->dev,
				 "failed to enable HID power button\n");
		}
	}

	device_init_wakeup(&device->dev, true);
	/*
	 * In order for system wakeup to work, the EC GPE has to be marked as
	 * a wakeup one, so do that here (this setting will persist, but it has
	 * no effect until the wakeup mask is set for the EC GPE).
	 */
	acpi_ec_mark_gpe_for_wake();
	return 0;

err_remove_notify:
	acpi_remove_notify_handler(handle, ACPI_DEVICE_NOTIFY, notify_handler);

	return err;
}

static int intel_hid_remove(struct platform_device *device)
{
	acpi_handle handle = ACPI_HANDLE(&device->dev);

	device_init_wakeup(&device->dev, false);
	acpi_remove_notify_handler(handle, ACPI_DEVICE_NOTIFY, notify_handler);
	intel_hid_set_enable(&device->dev, false);
	intel_button_array_enable(&device->dev, false);

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
		if (!IS_ERR_OR_NULL(acpi_create_platform_device(dev, NULL)))
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
