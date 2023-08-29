// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lenovo-ymc.c - Lenovo Yoga Mode Control driver
 *
 * Copyright © 2022 Gergo Koteles <soyer@irl.hu>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/wmi.h>
#include "ideapad-laptop.h"

#define LENOVO_YMC_EVENT_GUID	"06129D99-6083-4164-81AD-F092F9D773A6"
#define LENOVO_YMC_QUERY_GUID	"09B0EE6E-C3FD-4243-8DA1-7911FF80BB8C"

#define LENOVO_YMC_QUERY_INSTANCE 0
#define LENOVO_YMC_QUERY_METHOD 0x01

static bool ec_trigger __read_mostly;
module_param(ec_trigger, bool, 0444);
MODULE_PARM_DESC(ec_trigger, "Enable EC triggering work-around to force emitting tablet mode events");

static bool force;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "Force loading on boards without a convertible DMI chassis-type");

static const struct dmi_system_id ec_trigger_quirk_dmi_table[] = {
	{
		/* Lenovo Yoga 7 14ARB7 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "82QF"),
		},
	},
	{
		/* Lenovo Yoga 7 14ACN6 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "82N7"),
		},
	},
	{ }
};

static const struct dmi_system_id allowed_chasis_types_dmi_table[] = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_CHASSIS_TYPE, "31" /* Convertible */),
		},
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_CHASSIS_TYPE, "32" /* Detachable */),
		},
	},
	{ }
};

struct lenovo_ymc_private {
	struct input_dev *input_dev;
	struct acpi_device *ec_acpi_dev;
};

static void lenovo_ymc_trigger_ec(struct wmi_device *wdev, struct lenovo_ymc_private *priv)
{
	int err;

	if (!priv->ec_acpi_dev)
		return;

	err = write_ec_cmd(priv->ec_acpi_dev->handle, VPCCMD_W_YMC, 1);
	if (err)
		dev_warn(&wdev->dev, "Could not write YMC: %d\n", err);
}

static const struct key_entry lenovo_ymc_keymap[] = {
	/* Laptop */
	{ KE_SW, 0x01, { .sw = { SW_TABLET_MODE, 0 } } },
	/* Tablet */
	{ KE_SW, 0x02, { .sw = { SW_TABLET_MODE, 1 } } },
	/* Drawing Board */
	{ KE_SW, 0x03, { .sw = { SW_TABLET_MODE, 1 } } },
	/* Tent */
	{ KE_SW, 0x04, { .sw = { SW_TABLET_MODE, 1 } } },
	{ KE_END },
};

static void lenovo_ymc_notify(struct wmi_device *wdev, union acpi_object *data)
{
	struct lenovo_ymc_private *priv = dev_get_drvdata(&wdev->dev);
	u32 input_val = 0;
	struct acpi_buffer input = { sizeof(input_val), &input_val };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	int code;

	status = wmi_evaluate_method(LENOVO_YMC_QUERY_GUID,
				LENOVO_YMC_QUERY_INSTANCE,
				LENOVO_YMC_QUERY_METHOD,
				&input, &output);

	if (ACPI_FAILURE(status)) {
		dev_warn(&wdev->dev,
			"Failed to evaluate query method: %s\n",
			acpi_format_exception(status));
		return;
	}

	obj = output.pointer;

	if (obj->type != ACPI_TYPE_INTEGER) {
		dev_warn(&wdev->dev,
			"WMI event data is not an integer\n");
		goto free_obj;
	}
	code = obj->integer.value;

	if (!sparse_keymap_report_event(priv->input_dev, code, 1, true))
		dev_warn(&wdev->dev, "Unknown key %d pressed\n", code);

free_obj:
	kfree(obj);
	lenovo_ymc_trigger_ec(wdev, priv);
}

static void acpi_dev_put_helper(void *p) { acpi_dev_put(p); }

static int lenovo_ymc_probe(struct wmi_device *wdev, const void *ctx)
{
	struct lenovo_ymc_private *priv;
	struct input_dev *input_dev;
	int err;

	if (!dmi_check_system(allowed_chasis_types_dmi_table)) {
		if (force)
			dev_info(&wdev->dev, "Force loading Lenovo YMC support\n");
		else
			return -ENODEV;
	}

	ec_trigger |= dmi_check_system(ec_trigger_quirk_dmi_table);

	priv = devm_kzalloc(&wdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (ec_trigger) {
		pr_debug("Lenovo YMC enable EC triggering.\n");
		priv->ec_acpi_dev = acpi_dev_get_first_match_dev("VPC2004", NULL, -1);

		if (!priv->ec_acpi_dev) {
			dev_err(&wdev->dev, "Could not find EC ACPI device.\n");
			return -ENODEV;
		}
		err = devm_add_action_or_reset(&wdev->dev,
				acpi_dev_put_helper, priv->ec_acpi_dev);
		if (err) {
			dev_err(&wdev->dev,
				"Could not clean up EC ACPI device: %d\n", err);
			return err;
		}
	}

	input_dev = devm_input_allocate_device(&wdev->dev);
	if (!input_dev)
		return -ENOMEM;

	input_dev->name = "Lenovo Yoga Tablet Mode Control switch";
	input_dev->phys = LENOVO_YMC_EVENT_GUID "/input0";
	input_dev->id.bustype = BUS_HOST;
	input_dev->dev.parent = &wdev->dev;
	err = sparse_keymap_setup(input_dev, lenovo_ymc_keymap, NULL);
	if (err) {
		dev_err(&wdev->dev,
			"Could not set up input device keymap: %d\n", err);
		return err;
	}

	err = input_register_device(input_dev);
	if (err) {
		dev_err(&wdev->dev,
			"Could not register input device: %d\n", err);
		return err;
	}

	priv->input_dev = input_dev;
	dev_set_drvdata(&wdev->dev, priv);

	/* Report the state for the first time on probe */
	lenovo_ymc_trigger_ec(wdev, priv);
	lenovo_ymc_notify(wdev, NULL);
	return 0;
}

static const struct wmi_device_id lenovo_ymc_wmi_id_table[] = {
	{ .guid_string = LENOVO_YMC_EVENT_GUID },
	{ }
};
MODULE_DEVICE_TABLE(wmi, lenovo_ymc_wmi_id_table);

static struct wmi_driver lenovo_ymc_driver = {
	.driver = {
		.name = "lenovo-ymc",
	},
	.id_table = lenovo_ymc_wmi_id_table,
	.probe = lenovo_ymc_probe,
	.notify = lenovo_ymc_notify,
};

module_wmi_driver(lenovo_ymc_driver);

MODULE_AUTHOR("Gergo Koteles <soyer@irl.hu>");
MODULE_DESCRIPTION("Lenovo Yoga Mode Control driver");
MODULE_LICENSE("GPL");
