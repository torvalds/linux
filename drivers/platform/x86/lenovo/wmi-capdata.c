// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Lenovo Capability Data WMI Data Block driver.
 *
 * Lenovo Capability Data provides information on tunable attributes used by
 * the "Other Mode" WMI interface.
 *
 * Capability Data 00 includes if the attribute is supported by the hardware,
 * and the default_value. All attributes are independent of thermal modes.
 *
 * Capability Data 01 includes if the attribute is supported by the hardware,
 * and the default_value, max_value, min_value, and step increment. Each
 * attribute has multiple pages, one for each of the thermal modes managed by
 * the Gamezone interface.
 *
 * Fan Test Data includes the max/min fan speed RPM for each fan. This is
 * reference data for self-test. If the fan is in good condition, it is capable
 * to spin faster than max RPM or slower than min RPM.
 *
 * Copyright (C) 2025 Derek J. Clark <derekjohn.clark@gmail.com>
 *   - Initial implementation (formerly named lenovo-wmi-capdata01)
 *
 * Copyright (C) 2025 Rong Zhang <i@rong.moe>
 *   - Unified implementation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bug.h>
#include <linux/cleanup.h>
#include <linux/component.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/gfp_types.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mutex_types.h>
#include <linux/notifier.h>
#include <linux/overflow.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include "wmi-capdata.h"

#define LENOVO_CAPABILITY_DATA_00_GUID "362A3AFE-3D96-4665-8530-96DAD5BB300E"
#define LENOVO_CAPABILITY_DATA_01_GUID "7A8F5407-CB67-4D6E-B547-39B3BE018154"
#define LENOVO_FAN_TEST_DATA_GUID "B642801B-3D21-45DE-90AE-6E86F164FB21"

#define ACPI_AC_CLASS "ac_adapter"
#define ACPI_AC_NOTIFY_STATUS 0x80

enum lwmi_cd_type {
	LENOVO_CAPABILITY_DATA_00,
	LENOVO_CAPABILITY_DATA_01,
	LENOVO_FAN_TEST_DATA,
};

#define LWMI_CD_TABLE_ITEM(_type)		\
	[_type] = {				\
		.name = #_type,			\
		.type = _type,			\
	}

static const struct lwmi_cd_info {
	const char *name;
	enum lwmi_cd_type type;
} lwmi_cd_table[] = {
	LWMI_CD_TABLE_ITEM(LENOVO_CAPABILITY_DATA_00),
	LWMI_CD_TABLE_ITEM(LENOVO_CAPABILITY_DATA_01),
	LWMI_CD_TABLE_ITEM(LENOVO_FAN_TEST_DATA),
};

struct lwmi_cd_priv {
	struct notifier_block acpi_nb; /* ACPI events */
	struct wmi_device *wdev;
	struct cd_list *list;
};

struct cd_list {
	struct mutex list_mutex; /* list R/W mutex */
	enum lwmi_cd_type type;
	u8 count;

	union {
		DECLARE_FLEX_ARRAY(struct capdata00, cd00);
		DECLARE_FLEX_ARRAY(struct capdata01, cd01);
		DECLARE_FLEX_ARRAY(struct capdata_fan, cd_fan);
	};
};

static struct wmi_driver lwmi_cd_driver;

/**
 * lwmi_cd_match() - Match rule for the master driver.
 * @dev: Pointer to the capability data parent device.
 * @type: Pointer to capability data type (enum lwmi_cd_type *) to match.
 *
 * Return: int.
 */
static int lwmi_cd_match(struct device *dev, void *type)
{
	struct lwmi_cd_priv *priv;

	if (dev->driver != &lwmi_cd_driver.driver)
		return false;

	priv = dev_get_drvdata(dev);
	return priv->list->type == *(enum lwmi_cd_type *)type;
}

/**
 * lwmi_cd_match_add_all() - Add all match rule for the master driver.
 * @master: Pointer to the master device.
 * @matchptr: Pointer to the returned component_match pointer.
 *
 * Adds all component matches to the list stored in @matchptr for the @master
 * device. @matchptr must be initialized to NULL.
 */
void lwmi_cd_match_add_all(struct device *master, struct component_match **matchptr)
{
	int i;

	if (WARN_ON(*matchptr))
		return;

	for (i = 0; i < ARRAY_SIZE(lwmi_cd_table); i++) {
		/* Skip sub-components. */
		if (lwmi_cd_table[i].type == LENOVO_FAN_TEST_DATA)
			continue;

		component_match_add(master, matchptr, lwmi_cd_match,
				    (void *)&lwmi_cd_table[i].type);
		if (IS_ERR(*matchptr))
			return;
	}
}
EXPORT_SYMBOL_NS_GPL(lwmi_cd_match_add_all, "LENOVO_WMI_CAPDATA");

/**
 * lwmi_cd_component_bind() - Bind component to master device.
 * @cd_dev: Pointer to the lenovo-wmi-capdata driver parent device.
 * @om_dev: Pointer to the lenovo-wmi-other driver parent device.
 * @data: lwmi_cd_binder object pointer used to return the capability data.
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
	struct lwmi_cd_binder *binder = data;

	switch (priv->list->type) {
	case LENOVO_CAPABILITY_DATA_00:
		binder->cd00_list = priv->list;
		break;
	case LENOVO_CAPABILITY_DATA_01:
		binder->cd01_list = priv->list;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct component_ops lwmi_cd_component_ops = {
	.bind = lwmi_cd_component_bind,
};

/*
 * lwmi_cd*_get_data - Get the data of the specified attribute
 * @list: The lenovo-wmi-capdata pointer to its cd_list struct.
 * @attribute_id: The capdata attribute ID to be found.
 * @output: Pointer to a capdata* struct to return the data.
 *
 * Retrieves the capability data struct pointer for the given
 * attribute.
 *
 * Return: 0 on success, or -EINVAL.
 */
#define DEF_LWMI_CDXX_GET_DATA(_cdxx, _cd_type, _output_t)					\
	int lwmi_##_cdxx##_get_data(struct cd_list *list, u32 attribute_id, _output_t *output)	\
	{											\
		u8 idx;										\
												\
		if (WARN_ON(list->type != _cd_type))						\
			return -EINVAL;								\
												\
		guard(mutex)(&list->list_mutex);						\
		for (idx = 0; idx < list->count; idx++) {					\
			if (list->_cdxx[idx].id != attribute_id)				\
				continue;							\
			memcpy(output, &list->_cdxx[idx], sizeof(list->_cdxx[idx]));		\
			return 0;								\
		}										\
		return -EINVAL;									\
	}

DEF_LWMI_CDXX_GET_DATA(cd00, LENOVO_CAPABILITY_DATA_00, struct capdata00);
EXPORT_SYMBOL_NS_GPL(lwmi_cd00_get_data, "LENOVO_WMI_CAPDATA");

DEF_LWMI_CDXX_GET_DATA(cd01, LENOVO_CAPABILITY_DATA_01, struct capdata01);
EXPORT_SYMBOL_NS_GPL(lwmi_cd01_get_data, "LENOVO_WMI_CAPDATA");

DEF_LWMI_CDXX_GET_DATA(cd_fan, LENOVO_FAN_TEST_DATA, struct capdata_fan);
EXPORT_SYMBOL_NS_GPL(lwmi_cd_fan_get_data, "LENOVO_WMI_CAPDATA");

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
	size_t size;
	int idx;
	void *p;

	switch (priv->list->type) {
	case LENOVO_CAPABILITY_DATA_00:
		p = &priv->list->cd00[0];
		size = sizeof(priv->list->cd00[0]);
		break;
	case LENOVO_CAPABILITY_DATA_01:
		p = &priv->list->cd01[0];
		size = sizeof(priv->list->cd01[0]);
		break;
	case LENOVO_FAN_TEST_DATA:
		/* Done by lwmi_cd_alloc() => lwmi_cd_fan_list_alloc_cache(). */
		return 0;
	default:
		return -EINVAL;
	}

	guard(mutex)(&priv->list->list_mutex);
	for (idx = 0; idx < priv->list->count; idx++, p += size) {
		union acpi_object *ret_obj __free(kfree) = NULL;

		ret_obj = wmidev_block_query(priv->wdev, idx);
		if (!ret_obj)
			return -ENODEV;

		if (ret_obj->type != ACPI_TYPE_BUFFER ||
		    ret_obj->buffer.length < size)
			continue;

		memcpy(p, ret_obj->buffer.pointer, size);
	}

	return 0;
}

/**
 * lwmi_cd_fan_list_alloc_cache() - Alloc and cache Fan Test Data list
 * @priv: lenovo-wmi-capdata driver data.
 * @listptr: Pointer to returned cd_list pointer.
 *
 * Return: count of fans found, or an error.
 */
static int lwmi_cd_fan_list_alloc_cache(struct lwmi_cd_priv *priv, struct cd_list **listptr)
{
	struct cd_list *list;
	size_t size;
	u32 count;
	int idx;

	/* Emit unaligned access to u8 buffer with __packed. */
	struct cd_fan_block {
		u32 nr;
		u32 data[]; /* id[nr], max_rpm[nr], min_rpm[nr] */
	} __packed * block;

	union acpi_object *ret_obj __free(kfree) = wmidev_block_query(priv->wdev, 0);
	if (!ret_obj)
		return -ENODEV;

	if (ret_obj->type == ACPI_TYPE_BUFFER) {
		block = (struct cd_fan_block *)ret_obj->buffer.pointer;
		size = ret_obj->buffer.length;

		count = size >= sizeof(*block) ? block->nr : 0;
		if (size < struct_size(block, data, count * 3)) {
			dev_warn(&priv->wdev->dev,
				 "incomplete fan test data block: %zu < %zu, ignoring\n",
				 size, struct_size(block, data, count * 3));
			count = 0;
		} else if (count > U8_MAX) {
			dev_warn(&priv->wdev->dev,
				 "too many fans reported: %u > %u, truncating\n",
				 count, U8_MAX);
			count = U8_MAX;
		}
	} else {
		/*
		 * This is usually caused by a dummy ACPI method. Do not return an error
		 * as failing to probe this device will result in sub-master device being
		 * unbound. This behavior aligns with lwmi_cd_cache().
		 */
		count = 0;
	}

	list = devm_kzalloc(&priv->wdev->dev, struct_size(list, cd_fan, count), GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	for (idx = 0; idx < count; idx++) {
		/* Do not calculate array index using count, as it may be truncated. */
		list->cd_fan[idx] = (struct capdata_fan) {
			.id      = block->data[idx],
			.max_rpm = block->data[idx + block->nr],
			.min_rpm = block->data[idx + (2 * block->nr)],
		};
	}

	*listptr = list;
	return count;
}

/**
 * lwmi_cd_alloc() - Allocate a cd_list struct in drvdata
 * @priv: lenovo-wmi-capdata driver data.
 * @type: The type of capability data.
 *
 * Allocate a cd_list struct large enough to contain data from all WMI data
 * blocks provided by the interface.
 *
 * Return: 0 on success, or an error.
 */
static int lwmi_cd_alloc(struct lwmi_cd_priv *priv, enum lwmi_cd_type type)
{
	struct cd_list *list;
	size_t list_size;
	int count, ret;

	count = wmidev_instance_count(priv->wdev);

	switch (type) {
	case LENOVO_CAPABILITY_DATA_00:
		list_size = struct_size(list, cd00, count);
		break;
	case LENOVO_CAPABILITY_DATA_01:
		list_size = struct_size(list, cd01, count);
		break;
	case LENOVO_FAN_TEST_DATA:
		count = lwmi_cd_fan_list_alloc_cache(priv, &list);
		if (count < 0)
			return count;

		goto got_list;
	default:
		return -EINVAL;
	}

	list = devm_kzalloc(&priv->wdev->dev, list_size, GFP_KERNEL);
	if (!list)
		return -ENOMEM;

got_list:
	ret = devm_mutex_init(&priv->wdev->dev, &list->list_mutex);
	if (ret)
		return ret;

	list->type = type;
	list->count = count;
	priv->list = list;

	return 0;
}

/**
 * lwmi_cd_setup() - Cache all WMI data block information
 * @priv: lenovo-wmi-capdata driver data.
 * @type: The type of capability data.
 *
 * Allocate a cd_list struct large enough to contain data from all WMI data
 * blocks provided by the interface. Then loop through each data block and
 * cache the data.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_cd_setup(struct lwmi_cd_priv *priv, enum lwmi_cd_type type)
{
	int ret;

	ret = lwmi_cd_alloc(priv, type);
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
	const struct lwmi_cd_info *info = context;
	struct lwmi_cd_priv *priv;
	int ret;

	if (!info)
		return -EINVAL;

	priv = devm_kzalloc(&wdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->wdev = wdev;
	dev_set_drvdata(&wdev->dev, priv);

	ret = lwmi_cd_setup(priv, info->type);
	if (ret)
		goto out;

	switch (info->type) {
	case LENOVO_CAPABILITY_DATA_00:
		ret = component_add(&wdev->dev, &lwmi_cd_component_ops);
		goto out;
	case LENOVO_CAPABILITY_DATA_01:
		priv->acpi_nb.notifier_call = lwmi_cd01_notifier_call;

		ret = register_acpi_notifier(&priv->acpi_nb);
		if (ret)
			goto out;

		ret = devm_add_action_or_reset(&wdev->dev, lwmi_cd01_unregister,
					       &priv->acpi_nb);
		if (ret)
			goto out;

		ret = component_add(&wdev->dev, &lwmi_cd_component_ops);
		goto out;
	case LENOVO_FAN_TEST_DATA:
		goto out;
	default:
		return -EINVAL;
	}
out:
	if (ret) {
		dev_err(&wdev->dev, "failed to register %s: %d\n",
			info->name, ret);
	} else {
		dev_dbg(&wdev->dev, "registered %s with %u items\n",
			info->name, priv->list->count);
	}
	return ret;
}

static void lwmi_cd_remove(struct wmi_device *wdev)
{
	struct lwmi_cd_priv *priv = dev_get_drvdata(&wdev->dev);

	switch (priv->list->type) {
	case LENOVO_CAPABILITY_DATA_00:
	case LENOVO_CAPABILITY_DATA_01:
		component_del(&wdev->dev, &lwmi_cd_component_ops);
		break;
	case LENOVO_FAN_TEST_DATA:
		break;
	default:
		WARN_ON(1);
	}
}

#define LWMI_CD_WDEV_ID(_type)				\
	.guid_string = _type##_GUID,			\
	.context = &lwmi_cd_table[_type],

static const struct wmi_device_id lwmi_cd_id_table[] = {
	{ LWMI_CD_WDEV_ID(LENOVO_CAPABILITY_DATA_00) },
	{ LWMI_CD_WDEV_ID(LENOVO_CAPABILITY_DATA_01) },
	{ LWMI_CD_WDEV_ID(LENOVO_FAN_TEST_DATA) },
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

module_wmi_driver(lwmi_cd_driver);

MODULE_DEVICE_TABLE(wmi, lwmi_cd_id_table);
MODULE_AUTHOR("Derek J. Clark <derekjohn.clark@gmail.com>");
MODULE_AUTHOR("Rong Zhang <i@rong.moe>");
MODULE_DESCRIPTION("Lenovo Capability Data WMI Driver");
MODULE_LICENSE("GPL");
