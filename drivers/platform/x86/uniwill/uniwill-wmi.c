// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux hotkey driver for Uniwill notebooks.
 *
 * Special thanks go to Pőcze Barnabás, Christoffer Sandberg and Werner Sembach
 * for supporting the development of this driver either through prior work or
 * by answering questions regarding the underlying WMI interface.
 *
 * Copyright (C) 2025 Armin Wolf <W_Armin@gmx.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/notifier.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include "uniwill-wmi.h"

#define DRIVER_NAME		"uniwill-wmi"
#define UNIWILL_EVENT_GUID	"ABBC0F72-8EA1-11D1-00A0-C90629100000"

static BLOCKING_NOTIFIER_HEAD(uniwill_wmi_chain_head);

static void devm_uniwill_wmi_unregister_notifier(void *data)
{
	struct notifier_block *nb = data;

	blocking_notifier_chain_unregister(&uniwill_wmi_chain_head, nb);
}

int devm_uniwill_wmi_register_notifier(struct device *dev, struct notifier_block *nb)
{
	int ret;

	ret = blocking_notifier_chain_register(&uniwill_wmi_chain_head, nb);
	if (ret < 0)
		return ret;

	return devm_add_action_or_reset(dev, devm_uniwill_wmi_unregister_notifier, nb);
}

static void uniwill_wmi_notify(struct wmi_device *wdev, union acpi_object *obj)
{
	u32 value;

	if (obj->type != ACPI_TYPE_INTEGER)
		return;

	value = obj->integer.value;

	dev_dbg(&wdev->dev, "Received WMI event %u\n", value);

	blocking_notifier_call_chain(&uniwill_wmi_chain_head, value, NULL);
}

/*
 * We cannot fully trust this GUID since Uniwill just copied the WMI GUID
 * from the Windows driver example, and others probably did the same.
 *
 * Because of this we cannot use this WMI GUID for autoloading. Instead the
 * associated driver will be registered manually after matching a DMI table.
 */
static const struct wmi_device_id uniwill_wmi_id_table[] = {
	{ UNIWILL_EVENT_GUID, NULL },
	{ }
};

static struct wmi_driver uniwill_wmi_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = uniwill_wmi_id_table,
	.notify = uniwill_wmi_notify,
	.no_singleton = true,
};

int __init uniwill_wmi_register_driver(void)
{
	return wmi_driver_register(&uniwill_wmi_driver);
}

void __exit uniwill_wmi_unregister_driver(void)
{
	wmi_driver_unregister(&uniwill_wmi_driver);
}
