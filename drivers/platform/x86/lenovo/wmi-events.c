// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Lenovo WMI Events driver. Lenovo WMI interfaces provide various
 * hardware triggered events that many drivers need to have propagated.
 * This driver provides a uniform entrypoint for these events so that
 * any driver that needs to respond to these events can subscribe to a
 * notifier chain.
 *
 * Copyright (C) 2025 Derek J. Clark <derekjohn.clark@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include "wmi-events.h"
#include "wmi-gamezone.h"

#define THERMAL_MODE_EVENT_GUID "D320289E-8FEA-41E0-86F9-911D83151B5F"

#define LWMI_EVENT_DEVICE(guid, type)                        \
	.guid_string = (guid), .context = &(enum lwmi_events_type) \
	{                                                          \
		type                                               \
	}

static BLOCKING_NOTIFIER_HEAD(events_chain_head);

struct lwmi_events_priv {
	struct wmi_device *wdev;
	enum lwmi_events_type type;
};

/**
 * lwmi_events_register_notifier() - Add a notifier to the notifier chain.
 * @nb: The notifier_block struct to register
 *
 * Call blocking_notifier_chain_register to register the notifier block to the
 * lenovo-wmi-events driver blocking notifier chain.
 *
 * Return: 0 on success, %-EEXIST on error.
 */
int lwmi_events_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&events_chain_head, nb);
}
EXPORT_SYMBOL_NS_GPL(lwmi_events_register_notifier, "LENOVO_WMI_EVENTS");

/**
 * lwmi_events_unregister_notifier() - Remove a notifier from the notifier
 * chain.
 * @nb: The notifier_block struct to unregister
 *
 * Call blocking_notifier_chain_unregister to unregister the notifier block
 * from the lenovo-wmi-events driver blocking notifier chain.
 *
 * Return: 0 on success, %-ENOENT on error.
 */
int lwmi_events_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&events_chain_head, nb);
}
EXPORT_SYMBOL_NS_GPL(lwmi_events_unregister_notifier, "LENOVO_WMI_EVENTS");

/**
 * devm_lwmi_events_unregister_notifier() - Remove a notifier from the notifier
 * chain.
 * @data: Void pointer to the notifier_block struct to unregister.
 *
 * Call lwmi_events_unregister_notifier to unregister the notifier block from
 * the lenovo-wmi-events driver blocking notifier chain.
 *
 * Return: 0 on success, %-ENOENT on error.
 */
static void devm_lwmi_events_unregister_notifier(void *data)
{
	struct notifier_block *nb = data;

	lwmi_events_unregister_notifier(nb);
}

/**
 * devm_lwmi_events_register_notifier() - Add a notifier to the notifier chain.
 * @dev: The parent device of the notifier_block struct.
 * @nb: The notifier_block struct to register
 *
 * Call lwmi_events_register_notifier to register the notifier block to the
 * lenovo-wmi-events driver blocking notifier chain. Then add, as a device
 * managed action, unregister_notifier to automatically unregister the
 * notifier block upon its parent device removal.
 *
 * Return: 0 on success, or an error code.
 */
int devm_lwmi_events_register_notifier(struct device *dev,
				       struct notifier_block *nb)
{
	int ret;

	ret = lwmi_events_register_notifier(nb);
	if (ret < 0)
		return ret;

	return devm_add_action_or_reset(dev, devm_lwmi_events_unregister_notifier, nb);
}
EXPORT_SYMBOL_NS_GPL(devm_lwmi_events_register_notifier, "LENOVO_WMI_EVENTS");

/**
 * lwmi_events_notify() - Call functions for the notifier call chain.
 * @wdev: The parent WMI device of the driver.
 * @obj: ACPI object passed by the registered WMI Event.
 *
 * Validate WMI event data and notify all registered drivers of the event and
 * its output.
 *
 * Return: 0 on success, or an error code.
 */
static void lwmi_events_notify(struct wmi_device *wdev, union acpi_object *obj)
{
	struct lwmi_events_priv *priv = dev_get_drvdata(&wdev->dev);
	int sel_prof;
	int ret;

	switch (priv->type) {
	case LWMI_EVENT_THERMAL_MODE:
		if (obj->type != ACPI_TYPE_INTEGER)
			return;

		sel_prof = obj->integer.value;

		switch (sel_prof) {
		case LWMI_GZ_THERMAL_MODE_QUIET:
		case LWMI_GZ_THERMAL_MODE_BALANCED:
		case LWMI_GZ_THERMAL_MODE_PERFORMANCE:
		case LWMI_GZ_THERMAL_MODE_EXTREME:
		case LWMI_GZ_THERMAL_MODE_CUSTOM:
			ret = blocking_notifier_call_chain(&events_chain_head,
							   LWMI_EVENT_THERMAL_MODE,
							   &sel_prof);
			if (ret == NOTIFY_BAD)
				dev_err(&wdev->dev,
					"Failed to send notification to call chain for WMI Events\n");
			return;
		default:
			dev_err(&wdev->dev, "Got invalid thermal mode: %x",
				sel_prof);
			return;
		}
		break;
	default:
		return;
	}
}

static int lwmi_events_probe(struct wmi_device *wdev, const void *context)
{
	struct lwmi_events_priv *priv;

	if (!context)
		return -EINVAL;

	priv = devm_kzalloc(&wdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->wdev = wdev;
	priv->type = *(enum lwmi_events_type *)context;
	dev_set_drvdata(&wdev->dev, priv);

	return 0;
}

static const struct wmi_device_id lwmi_events_id_table[] = {
	{ LWMI_EVENT_DEVICE(THERMAL_MODE_EVENT_GUID, LWMI_EVENT_THERMAL_MODE) },
	{}
};

static struct wmi_driver lwmi_events_driver = {
	.driver = {
		.name = "lenovo_wmi_events",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = lwmi_events_id_table,
	.probe = lwmi_events_probe,
	.notify = lwmi_events_notify,
	.no_singleton = true,
};

module_wmi_driver(lwmi_events_driver);

MODULE_DEVICE_TABLE(wmi, lwmi_events_id_table);
MODULE_AUTHOR("Derek J. Clark <derekjohn.clark@gmail.com>");
MODULE_DESCRIPTION("Lenovo WMI Events Driver");
MODULE_LICENSE("GPL");
