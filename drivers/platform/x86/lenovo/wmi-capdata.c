// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Lenovo Capability Data WMI Data Block driver.
 *
 * Lenovo Capability Data provides information on tunable attributes used by
 * the "Other Mode" WMI interface.
 *
 * Capability Data 01 includes if the attribute is supported by the hardware,
 * and the default_value, max_value, min_value, and step increment. Each
 * attribute has multiple pages, one for each of the thermal modes managed by
 * the Gamezone interface.
 *
 * Copyright (C) 2025 Derek J. Clark <derekjohn.clark@gmail.com>
 *   - Initial implementation (formerly named lenovo-wmi-capdata01)
 */

#include <linux/acpi.h>
#include <linux/cleanup.h>
#include <linux/component.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/gfp_types.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mutex_types.h>
#include <linux/notifier.h>
#include <linux/overflow.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include "wmi-capdata.h"

#define LENOVO_CAPABILITY_DATA_01_GUID "7A8F5407-CB67-4D6E-B547-39B3BE018154"

#define ACPI_AC_CLASS "ac_adapter"
#define ACPI_AC_NOTIFY_STATUS 0x80

struct lwmi_cd_priv {
	struct notifier_block acpi_nb; /* ACPI events */
	struct wmi_device *wdev;
	struct cd_list *list;
};

struct cd_list {
	struct mutex list_mutex; /* list R/W mutex */
	u8 count;
	struct capdata01 data[];
};

/**
 * lwmi_cd_component_bind() - Bind component to master device.
 * @cd_dev: Pointer to the lenovo-wmi-capdata driver parent device.
 * @om_dev: Pointer to the lenovo-wmi-other driver parent device.
 * @data: cd_list object pointer used to return the capability data.
 *
 * On lenovo-wmi-other's master bind, provide a pointer to the local capdata
 * list. This is used to call lwmi_cd*_get_data to look up attribute data
 * from the lenovo-wmi-other driver.
 *
 * Return: 0
 */
static int lwmi_cd_component_bind(struct device *cd_dev,
				  struct device *om_dev, void *data)
{
	struct lwmi_cd_priv *priv = dev_get_drvdata(cd_dev);
	struct cd_list **cd_list = data;

	*cd_list = priv->list;

	return 0;
}

static const struct component_ops lwmi_cd_component_ops = {
	.bind = lwmi_cd_component_bind,
};

/**
 * lwmi_cd01_get_data - Get the data of the specified attribute
 * @list: The lenovo-wmi-capdata pointer to its cd_list struct.
 * @attribute_id: The capdata attribute ID to be found.
 * @output: Pointer to a capdata01 struct to return the data.
 *
 * Retrieves the capability data 01 struct pointer for the given
 * attribute for its specified thermal mode.
 *
 * Return: 0 on success, or -EINVAL.
 */
int lwmi_cd01_get_data(struct cd_list *list, u32 attribute_id, struct capdata01 *output)
{
	u8 idx;

	guard(mutex)(&list->list_mutex);
	for (idx = 0; idx < list->count; idx++) {
		if (list->data[idx].id != attribute_id)
			continue;
		memcpy(output, &list->data[idx], sizeof(list->data[idx]));
		return 0;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_NS_GPL(lwmi_cd01_get_data, "LENOVO_WMI_CAPDATA");

/**
 * lwmi_cd_cache() - Cache all WMI data block information
 * @priv: lenovo-wmi-capdata driver data.
 *
 * Loop through each WMI data block and cache the data.
 *
 * Return: 0 on success, or an error.
 */
static int lwmi_cd_cache(struct lwmi_cd_priv *priv)
{
	int idx;

	guard(mutex)(&priv->list->list_mutex);
	for (idx = 0; idx < priv->list->count; idx++) {
		union acpi_object *ret_obj __free(kfree) = NULL;

		ret_obj = wmidev_block_query(priv->wdev, idx);
		if (!ret_obj)
			return -ENODEV;

		if (ret_obj->type != ACPI_TYPE_BUFFER ||
		    ret_obj->buffer.length < sizeof(priv->list->data[idx]))
			continue;

		memcpy(&priv->list->data[idx], ret_obj->buffer.pointer,
		       ret_obj->buffer.length);
	}

	return 0;
}

/**
 * lwmi_cd_alloc() - Allocate a cd_list struct in drvdata
 * @priv: lenovo-wmi-capdata driver data.
 *
 * Allocate a cd_list struct large enough to contain data from all WMI data
 * blocks provided by the interface.
 *
 * Return: 0 on success, or an error.
 */
static int lwmi_cd_alloc(struct lwmi_cd_priv *priv)
{
	struct cd_list *list;
	size_t list_size;
	int count, ret;

	count = wmidev_instance_count(priv->wdev);
	list_size = struct_size(list, data, count);

	list = devm_kzalloc(&priv->wdev->dev, list_size, GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	ret = devm_mutex_init(&priv->wdev->dev, &list->list_mutex);
	if (ret)
		return ret;

	list->count = count;
	priv->list = list;

	return 0;
}

/**
 * lwmi_cd_setup() - Cache all WMI data block information
 * @priv: lenovo-wmi-capdata driver data.
 *
 * Allocate a cd_list struct large enough to contain data from all WMI data
 * blocks provided by the interface. Then loop through each data block and
 * cache the data.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_cd_setup(struct lwmi_cd_priv *priv)
{
	int ret;

	ret = lwmi_cd_alloc(priv);
	if (ret)
		return ret;

	return lwmi_cd_cache(priv);
}

/**
 * lwmi_cd01_notifier_call() - Call method for cd01 notifier.
 * block call chain.
 * @nb: The notifier_block registered to lenovo-wmi-events driver.
 * @action: Unused.
 * @data: The ACPI event.
 *
 * For LWMI_EVENT_THERMAL_MODE, set current_mode and notify platform_profile
 * of a change.
 *
 * Return: notifier_block status.
 */
static int lwmi_cd01_notifier_call(struct notifier_block *nb, unsigned long action,
				   void *data)
{
	struct acpi_bus_event *event = data;
	struct lwmi_cd_priv *priv;
	int ret;

	if (strcmp(event->device_class, ACPI_AC_CLASS) != 0)
		return NOTIFY_DONE;

	priv = container_of(nb, struct lwmi_cd_priv, acpi_nb);

	switch (event->type) {
	case ACPI_AC_NOTIFY_STATUS:
		ret = lwmi_cd_cache(priv);
		if (ret)
			return NOTIFY_BAD;

		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

/**
 * lwmi_cd01_unregister() - Unregister the cd01 ACPI notifier_block.
 * @data: The ACPI event notifier_block to unregister.
 */
static void lwmi_cd01_unregister(void *data)
{
	struct notifier_block *acpi_nb = data;

	unregister_acpi_notifier(acpi_nb);
}

static int lwmi_cd_probe(struct wmi_device *wdev, const void *context)
{
	struct lwmi_cd_priv *priv;
	int ret;

	priv = devm_kzalloc(&wdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->wdev = wdev;
	dev_set_drvdata(&wdev->dev, priv);

	ret = lwmi_cd_setup(priv);
	if (ret)
		return ret;

	priv->acpi_nb.notifier_call = lwmi_cd01_notifier_call;

	ret = register_acpi_notifier(&priv->acpi_nb);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&wdev->dev, lwmi_cd01_unregister, &priv->acpi_nb);
	if (ret)
		return ret;

	return component_add(&wdev->dev, &lwmi_cd_component_ops);
}

static void lwmi_cd_remove(struct wmi_device *wdev)
{
	component_del(&wdev->dev, &lwmi_cd_component_ops);
}

static const struct wmi_device_id lwmi_cd_id_table[] = {
	{ LENOVO_CAPABILITY_DATA_01_GUID, NULL },
	{}
};

static struct wmi_driver lwmi_cd_driver = {
	.driver = {
		.name = "lenovo_wmi_capdata",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = lwmi_cd_id_table,
	.probe = lwmi_cd_probe,
	.remove = lwmi_cd_remove,
	.no_singleton = true,
};

/**
 * lwmi_cd01_match() - Match rule for the master driver.
 * @dev: Pointer to the capability data 01 parent device.
 * @data: Unused void pointer for passing match criteria.
 *
 * Return: int.
 */
int lwmi_cd01_match(struct device *dev, void *data)
{
	return dev->driver == &lwmi_cd_driver.driver;
}
EXPORT_SYMBOL_NS_GPL(lwmi_cd01_match, "LENOVO_WMI_CAPDATA");

module_wmi_driver(lwmi_cd_driver);

MODULE_DEVICE_TABLE(wmi, lwmi_cd_id_table);
MODULE_AUTHOR("Derek J. Clark <derekjohn.clark@gmail.com>");
MODULE_DESCRIPTION("Lenovo Capability Data WMI Driver");
MODULE_LICENSE("GPL");
