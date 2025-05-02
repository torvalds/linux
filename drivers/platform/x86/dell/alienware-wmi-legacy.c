// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Alienware LEGACY WMI device driver
 *
 * Copyright (C) 2025 Kurt Borja <kuurtb@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/wmi.h>
#include "alienware-wmi.h"

struct legacy_led_args {
	struct color_platform colors;
	u8 brightness;
	u8 state;
} __packed;


/*
 * Legacy WMI driver
 */
static int legacy_wmi_update_led(struct alienfx_priv *priv,
				 struct wmi_device *wdev, u8 location)
{
	struct legacy_led_args legacy_args = {
		.colors = priv->colors[location],
		.brightness = priv->global_brightness,
		.state = 0,
	};
	struct acpi_buffer input;
	acpi_status status;

	if (legacy_args.state != LEGACY_RUNNING) {
		legacy_args.state = priv->lighting_control_state;

		input.length = sizeof(legacy_args);
		input.pointer = &legacy_args;

		status = wmi_evaluate_method(LEGACY_POWER_CONTROL_GUID, 0,
					     location + 1, &input, NULL);
		if (ACPI_FAILURE(status))
			return -EIO;

		return 0;
	}

	return alienware_wmi_command(wdev, location + 1, &legacy_args,
				     sizeof(legacy_args), NULL);
}

static int legacy_wmi_update_brightness(struct alienfx_priv *priv,
					struct wmi_device *wdev, u8 brightness)
{
	return legacy_wmi_update_led(priv, wdev, 0);
}

static int legacy_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct alienfx_platdata pdata = {
		.wdev = wdev,
		.ops = {
			.upd_led = legacy_wmi_update_led,
			.upd_brightness = legacy_wmi_update_brightness,
		},
	};

	return alienware_alienfx_setup(&pdata);
}

static const struct wmi_device_id alienware_legacy_device_id_table[] = {
	{ LEGACY_CONTROL_GUID, NULL },
	{ },
};
MODULE_DEVICE_TABLE(wmi, alienware_legacy_device_id_table);

static struct wmi_driver alienware_legacy_wmi_driver = {
	.driver = {
		.name = "alienware-wmi-alienfx",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = alienware_legacy_device_id_table,
	.probe = legacy_wmi_probe,
	.no_singleton = true,
};

int __init alienware_legacy_wmi_init(void)
{
	return wmi_driver_register(&alienware_legacy_wmi_driver);
}

void __exit alienware_legacy_wmi_exit(void)
{
	wmi_driver_unregister(&alienware_legacy_wmi_driver);
}
