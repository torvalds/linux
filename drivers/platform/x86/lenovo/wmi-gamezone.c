// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Lenovo GameZone WMI interface driver.
 *
 * The GameZone WMI interface provides platform profile and fan curve settings
 * for devices that fall under the "Gaming Series" of Lenovo Legion devices.
 *
 * Copyright (C) 2025 Derek J. Clark <derekjohn.clark@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_profile.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include "wmi-events.h"
#include "wmi-gamezone.h"
#include "wmi-helpers.h"
#include "wmi-other.h"

#define LENOVO_GAMEZONE_GUID "887B54E3-DDDC-4B2C-8B88-68A26A8835D0"

#define LWMI_GZ_METHOD_ID_SMARTFAN_SUP 43
#define LWMI_GZ_METHOD_ID_SMARTFAN_SET 44
#define LWMI_GZ_METHOD_ID_SMARTFAN_GET 45

static BLOCKING_NOTIFIER_HEAD(gz_chain_head);

struct lwmi_gz_priv {
	enum thermal_mode current_mode;
	struct notifier_block event_nb;
	struct notifier_block mode_nb;
	spinlock_t gz_mode_lock; /* current_mode lock */
	struct wmi_device *wdev;
	int extreme_supported;
	struct device *ppdev;
};

struct quirk_entry {
	bool extreme_supported;
};

static struct quirk_entry quirk_no_extreme_bug = {
	.extreme_supported = false,
};

/**
 * lwmi_gz_mode_call() - Call method for lenovo-wmi-other driver notifier.
 *
 * @nb: The notifier_block registered to lenovo-wmi-other driver.
 * @cmd: The event type.
 * @data: Thermal mode enum pointer pointer for returning the thermal mode.
 *
 * For LWMI_GZ_GET_THERMAL_MODE, retrieve the current thermal mode.
 *
 * Return: Notifier_block status.
 */
static int lwmi_gz_mode_call(struct notifier_block *nb, unsigned long cmd,
			     void *data)
{
	enum thermal_mode **mode = data;
	struct lwmi_gz_priv *priv;

	priv = container_of(nb, struct lwmi_gz_priv, mode_nb);

	switch (cmd) {
	case LWMI_GZ_GET_THERMAL_MODE:
		scoped_guard(spinlock, &priv->gz_mode_lock) {
			**mode = priv->current_mode;
		}
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

/**
 * lwmi_gz_event_call() - Call method for lenovo-wmi-events driver notifier.
 * block call chain.
 * @nb: The notifier_block registered to lenovo-wmi-events driver.
 * @cmd: The event type.
 * @data: The data to be updated by the event.
 *
 * For LWMI_EVENT_THERMAL_MODE, set current_mode and notify platform_profile
 * of a change.
 *
 * Return: notifier_block status.
 */
static int lwmi_gz_event_call(struct notifier_block *nb, unsigned long cmd,
			      void *data)
{
	enum thermal_mode *mode = data;
	struct lwmi_gz_priv *priv;

	priv = container_of(nb, struct lwmi_gz_priv, event_nb);

	switch (cmd) {
	case LWMI_EVENT_THERMAL_MODE:
		scoped_guard(spinlock, &priv->gz_mode_lock) {
			priv->current_mode = *mode;
		}
		platform_profile_notify(priv->ppdev);
		return NOTIFY_STOP;
	default:
		return NOTIFY_DONE;
	}
}

/**
 * lwmi_gz_thermal_mode_supported() - Get the version of the WMI
 * interface to determine the support level.
 * @wdev: The Gamezone WMI device.
 * @supported: Pointer to return the support level with.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_gz_thermal_mode_supported(struct wmi_device *wdev,
					  int *supported)
{
	return lwmi_dev_evaluate_int(wdev, 0x0, LWMI_GZ_METHOD_ID_SMARTFAN_SUP,
				     NULL, 0, supported);
}

/**
 * lwmi_gz_thermal_mode_get() - Get the current thermal mode.
 * @wdev: The Gamezone interface WMI device.
 * @mode: Pointer to return the thermal mode with.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_gz_thermal_mode_get(struct wmi_device *wdev,
				    enum thermal_mode *mode)
{
	return lwmi_dev_evaluate_int(wdev, 0x0, LWMI_GZ_METHOD_ID_SMARTFAN_GET,
				     NULL, 0, mode);
}

/**
 * lwmi_gz_profile_get() - Get the current platform profile.
 * @dev: the Gamezone interface parent device.
 * @profile: Pointer to provide the current platform profile with.
 *
 * Call lwmi_gz_thermal_mode_get and convert the thermal mode into a platform
 * profile based on the support level of the interface.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_gz_profile_get(struct device *dev,
			       enum platform_profile_option *profile)
{
	struct lwmi_gz_priv *priv = dev_get_drvdata(dev);
	enum thermal_mode mode;
	int ret;

	ret = lwmi_gz_thermal_mode_get(priv->wdev, &mode);
	if (ret)
		return ret;

	switch (mode) {
	case LWMI_GZ_THERMAL_MODE_QUIET:
		*profile = PLATFORM_PROFILE_LOW_POWER;
		break;
	case LWMI_GZ_THERMAL_MODE_BALANCED:
		*profile = PLATFORM_PROFILE_BALANCED;
		break;
	case LWMI_GZ_THERMAL_MODE_PERFORMANCE:
		if (priv->extreme_supported) {
			*profile = PLATFORM_PROFILE_BALANCED_PERFORMANCE;
			break;
		}
		*profile = PLATFORM_PROFILE_PERFORMANCE;
		break;
	case LWMI_GZ_THERMAL_MODE_EXTREME:
		*profile = PLATFORM_PROFILE_PERFORMANCE;
		break;
	case LWMI_GZ_THERMAL_MODE_CUSTOM:
		*profile = PLATFORM_PROFILE_CUSTOM;
		break;
	default:
		return -EINVAL;
	}

	guard(spinlock)(&priv->gz_mode_lock);
	priv->current_mode = mode;

	return 0;
}

/**
 * lwmi_gz_profile_set() - Set the current platform profile.
 * @dev: The Gamezone interface parent device.
 * @profile: Pointer to the desired platform profile.
 *
 * Convert the given platform profile into a thermal mode based on the support
 * level of the interface, then call the WMI method to set the thermal mode.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_gz_profile_set(struct device *dev,
			       enum platform_profile_option profile)
{
	struct lwmi_gz_priv *priv = dev_get_drvdata(dev);
	struct wmi_method_args_32 args;
	enum thermal_mode mode;
	int ret;

	switch (profile) {
	case PLATFORM_PROFILE_LOW_POWER:
		mode = LWMI_GZ_THERMAL_MODE_QUIET;
		break;
	case PLATFORM_PROFILE_BALANCED:
		mode = LWMI_GZ_THERMAL_MODE_BALANCED;
		break;
	case PLATFORM_PROFILE_BALANCED_PERFORMANCE:
		mode = LWMI_GZ_THERMAL_MODE_PERFORMANCE;
		break;
	case PLATFORM_PROFILE_PERFORMANCE:
		if (priv->extreme_supported) {
			mode = LWMI_GZ_THERMAL_MODE_EXTREME;
			break;
		}
		mode = LWMI_GZ_THERMAL_MODE_PERFORMANCE;
		break;
	case PLATFORM_PROFILE_CUSTOM:
		mode = LWMI_GZ_THERMAL_MODE_CUSTOM;
		break;
	default:
		return -EOPNOTSUPP;
	}

	args.arg0 = mode;

	ret = lwmi_dev_evaluate_int(priv->wdev, 0x0,
				    LWMI_GZ_METHOD_ID_SMARTFAN_SET,
				    (u8 *)&args, sizeof(args), NULL);
	if (ret)
		return ret;

	guard(spinlock)(&priv->gz_mode_lock);
	priv->current_mode = mode;

	return 0;
}

static const struct dmi_system_id fwbug_list[] = {
	{
		.ident = "Legion Go 8APU1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Legion Go 8APU1"),
		},
		.driver_data = &quirk_no_extreme_bug,
	},
	{
		.ident = "Legion Go S 8APU1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Legion Go S 8APU1"),
		},
		.driver_data = &quirk_no_extreme_bug,
	},
	{
		.ident = "Legion Go S 8ARP1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Legion Go S 8ARP1"),
		},
		.driver_data = &quirk_no_extreme_bug,
	},
	{},

};

/**
 * lwmi_gz_extreme_supported() - Evaluate if a device supports extreme thermal mode.
 * @profile_support_ver: Version of the WMI interface.
 *
 * Determine if the extreme thermal mode is supported by the hardware.
 * Anything version 5 or lower does not. For devices with a version 6 or
 * greater do a DMI check, as some devices report a version that supports
 * extreme mode but have an incomplete entry in the BIOS. To ensure this
 * cannot be set, quirk them to prevent assignment.
 *
 * Return: bool.
 */
static bool lwmi_gz_extreme_supported(int profile_support_ver)
{
	const struct dmi_system_id *dmi_id;
	struct quirk_entry *quirks;

	if (profile_support_ver < 6)
		return false;

	dmi_id = dmi_first_match(fwbug_list);
	if (!dmi_id)
		return true;

	quirks = dmi_id->driver_data;

	return quirks->extreme_supported;
}

/**
 * lwmi_gz_platform_profile_probe - Enable and set up the platform profile
 * device.
 * @drvdata: Driver data for the interface.
 * @choices: Container for enabled platform profiles.
 *
 * Determine if thermal mode is supported, and if so to what feature level.
 * Then enable all supported platform profiles.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_gz_platform_profile_probe(void *drvdata, unsigned long *choices)
{
	struct lwmi_gz_priv *priv = drvdata;
	int profile_support_ver;
	int ret;

	ret = lwmi_gz_thermal_mode_supported(priv->wdev, &profile_support_ver);
	if (ret)
		return ret;

	if (profile_support_ver < 1)
		return -ENODEV;

	set_bit(PLATFORM_PROFILE_LOW_POWER, choices);
	set_bit(PLATFORM_PROFILE_BALANCED, choices);
	set_bit(PLATFORM_PROFILE_PERFORMANCE, choices);
	set_bit(PLATFORM_PROFILE_CUSTOM, choices);

	priv->extreme_supported = lwmi_gz_extreme_supported(profile_support_ver);
	if (priv->extreme_supported)
		set_bit(PLATFORM_PROFILE_BALANCED_PERFORMANCE, choices);

	return 0;
}

static const struct platform_profile_ops lwmi_gz_platform_profile_ops = {
	.probe = lwmi_gz_platform_profile_probe,
	.profile_get = lwmi_gz_profile_get,
	.profile_set = lwmi_gz_profile_set,
};

static int lwmi_gz_probe(struct wmi_device *wdev, const void *context)
{
	struct lwmi_gz_priv *priv;
	int ret;

	priv = devm_kzalloc(&wdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->wdev = wdev;
	dev_set_drvdata(&wdev->dev, priv);

	priv->ppdev = devm_platform_profile_register(&wdev->dev, "lenovo-wmi-gamezone",
						     priv, &lwmi_gz_platform_profile_ops);
	if (IS_ERR(priv->ppdev))
		return -ENODEV;

	spin_lock_init(&priv->gz_mode_lock);

	ret = lwmi_gz_thermal_mode_get(wdev, &priv->current_mode);
	if (ret)
		return ret;

	priv->event_nb.notifier_call = lwmi_gz_event_call;
	ret = devm_lwmi_events_register_notifier(&wdev->dev, &priv->event_nb);
	if (ret)
		return ret;

	priv->mode_nb.notifier_call = lwmi_gz_mode_call;
	return devm_lwmi_om_register_notifier(&wdev->dev, &priv->mode_nb);
}

static const struct wmi_device_id lwmi_gz_id_table[] = {
	{ LENOVO_GAMEZONE_GUID, NULL },
	{}
};

static struct wmi_driver lwmi_gz_driver = {
	.driver = {
		.name = "lenovo_wmi_gamezone",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = lwmi_gz_id_table,
	.probe = lwmi_gz_probe,
	.no_singleton = true,
};

module_wmi_driver(lwmi_gz_driver);

MODULE_IMPORT_NS("LENOVO_WMI_EVENTS");
MODULE_IMPORT_NS("LENOVO_WMI_HELPERS");
MODULE_IMPORT_NS("LENOVO_WMI_OTHER");
MODULE_DEVICE_TABLE(wmi, lwmi_gz_id_table);
MODULE_AUTHOR("Derek J. Clark <derekjohn.clark@gmail.com>");
MODULE_DESCRIPTION("Lenovo GameZone WMI Driver");
MODULE_LICENSE("GPL");
