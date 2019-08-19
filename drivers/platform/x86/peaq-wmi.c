// SPDX-License-Identifier: GPL-2.0-only
/*
 * PEAQ 2-in-1 WMI hotkey driver
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/input-polldev.h>
#include <linux/kernel.h>
#include <linux/module.h>

#define PEAQ_DOLBY_BUTTON_GUID		"ABBC0F6F-8EA1-11D1-00A0-C90629100000"
#define PEAQ_DOLBY_BUTTON_METHOD_ID	5
#define PEAQ_POLL_INTERVAL_MS		250
#define PEAQ_POLL_IGNORE_MS		500
#define PEAQ_POLL_MAX_MS		1000

MODULE_ALIAS("wmi:"PEAQ_DOLBY_BUTTON_GUID);

static unsigned int peaq_ignore_events_counter;
static struct input_polled_dev *peaq_poll_dev;

/*
 * The Dolby button (yes really a Dolby button) causes an ACPI variable to get
 * set on both press and release. The WMI method checks and clears that flag.
 * So for a press + release we will get back One from the WMI method either once
 * (if polling after the release) or twice (polling between press and release).
 * We ignore events for 0.5s after the first event to avoid reporting 2 presses.
 */
static void peaq_wmi_poll(struct input_polled_dev *dev)
{
	union acpi_object obj;
	acpi_status status;
	u32 dummy = 0;

	struct acpi_buffer input = { sizeof(dummy), &dummy };
	struct acpi_buffer output = { sizeof(obj), &obj };

	status = wmi_evaluate_method(PEAQ_DOLBY_BUTTON_GUID, 0,
				     PEAQ_DOLBY_BUTTON_METHOD_ID,
				     &input, &output);
	if (ACPI_FAILURE(status))
		return;

	if (obj.type != ACPI_TYPE_INTEGER) {
		dev_err(&peaq_poll_dev->input->dev,
			"Error WMBC did not return an integer\n");
		return;
	}

	if (peaq_ignore_events_counter && peaq_ignore_events_counter--)
		return;

	if (obj.integer.value) {
		input_event(peaq_poll_dev->input, EV_KEY, KEY_SOUND, 1);
		input_sync(peaq_poll_dev->input);
		input_event(peaq_poll_dev->input, EV_KEY, KEY_SOUND, 0);
		input_sync(peaq_poll_dev->input);
		peaq_ignore_events_counter = max(1u,
			PEAQ_POLL_IGNORE_MS / peaq_poll_dev->poll_interval);
	}
}

/* Some other devices (Shuttle XS35) use the same WMI GUID for other purposes */
static const struct dmi_system_id peaq_dmi_table[] __initconst = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "PEAQ"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PEAQ PMM C1010 MD99187"),
		},
	},
	{}
};

static int __init peaq_wmi_init(void)
{
	/* WMI GUID is not unique, also check for a DMI match */
	if (!dmi_check_system(peaq_dmi_table))
		return -ENODEV;

	if (!wmi_has_guid(PEAQ_DOLBY_BUTTON_GUID))
		return -ENODEV;

	peaq_poll_dev = input_allocate_polled_device();
	if (!peaq_poll_dev)
		return -ENOMEM;

	peaq_poll_dev->poll = peaq_wmi_poll;
	peaq_poll_dev->poll_interval = PEAQ_POLL_INTERVAL_MS;
	peaq_poll_dev->poll_interval_max = PEAQ_POLL_MAX_MS;
	peaq_poll_dev->input->name = "PEAQ WMI hotkeys";
	peaq_poll_dev->input->phys = "wmi/input0";
	peaq_poll_dev->input->id.bustype = BUS_HOST;
	input_set_capability(peaq_poll_dev->input, EV_KEY, KEY_SOUND);

	return input_register_polled_device(peaq_poll_dev);
}

static void __exit peaq_wmi_exit(void)
{
	input_unregister_polled_device(peaq_poll_dev);
}

module_init(peaq_wmi_init);
module_exit(peaq_wmi_exit);

MODULE_DESCRIPTION("PEAQ 2-in-1 WMI hotkey driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
