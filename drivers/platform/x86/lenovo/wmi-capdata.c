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
#include <linux/compiler.h>
#include <linux/component.h>
#include <linux/container_of.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/gfp_types.h>
#include <linux/limits.h>
#include <linux/lockdep.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mutex_types.h>
#include <linux/notifier.h>
#include <linux/overflow.h>
#include <linux/seq_file.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/wmi.h>

#include <asm/barrier.h>

#include "wmi-capdata.h"
#include "wmi-helpers.h"

#define LENOVO_CAPABILITY_DATA_00_GUID "362A3AFE-3D96-4665-8530-96DAD5BB300E"
#define LENOVO_CAPABILITY_DATA_01_GUID "7A8F5407-CB67-4D6E-B547-39B3BE018154"
#define LENOVO_FAN_TEST_DATA_GUID "B642801B-3D21-45DE-90AE-6E86F164FB21"

#define ACPI_AC_NOTIFY_STATUS 0x80

#define LWMI_FEATURE_ID_FAN_TEST 0x05

#define LWMI_ATTR_ID_FAN_TEST                                      \
	lwmi_attr_id(LWMI_DEVICE_ID_FAN, LWMI_FEATURE_ID_FAN_TEST, \
		     LWMI_GZ_THERMAL_MODE_NONE, LWMI_TYPE_ID_NONE)

enum lwmi_cd_type {
	LENOVO_CAPABILITY_DATA_00,
	LENOVO_CAPABILITY_DATA_01,
	LENOVO_FAN_TEST_DATA,
	CD_TYPE_NONE = -1,
};

static const struct wmi_device_id lwmi_cd_id_table[];

/* Save some .rodata space by reusing the GUID strings from the id table. */
#define LWMI_CD_TABLE_ITEM(_type)					\
	[_type] = {							\
		.name = #_type,						\
		.guid_string = lwmi_cd_id_table[_type].guid_string,	\
		.type = _type,						\
	}

static const struct lwmi_cd_info {
	const char *name;
	const char *guid_string;
	enum lwmi_cd_type type;
} lwmi_cd_table[] = {
	LWMI_CD_TABLE_ITEM(LENOVO_CAPABILITY_DATA_00),
	LWMI_CD_TABLE_ITEM(LENOVO_CAPABILITY_DATA_01),
	LWMI_CD_TABLE_ITEM(LENOVO_FAN_TEST_DATA),
};

struct lwmi_cd_priv {
	struct notifier_block acpi_nb; /* ACPI events */
	const struct lwmi_cd_info *info;
	struct wmi_device *wdev;
	struct cd_list *list;
	struct dentry *debugfs_dir;
	bool initialized;

	/*
	 * A capdata device may be a component master of another capdata device.
	 * E.g., lenovo-wmi-other <-> capdata00 <-> capdata_fan
	 *       |- master            |- component
	 *                            |- sub-master
	 *                                          |- sub-component
	 */
	struct lwmi_cd_sub_master_priv {
		struct device *master_dev;
		cd_list_cb_t master_cb;
		struct cd_list *sub_component_list; /* ERR_PTR(-ENODEV): stubbed sub-component. */
		bool registered;                    /* Has the sub-master been registered? */
	} *sub_master;                              /* NULL: no sub-component. */
};

struct cd_list {
	struct mutex list_mutex; /* list R/W mutex */
	u8 count;

	union {
		DECLARE_FLEX_ARRAY(struct capdata00, cd00);
		DECLARE_FLEX_ARRAY(struct capdata01, cd01);
		DECLARE_FLEX_ARRAY(struct capdata_fan, cd_fan);
	};
};

static struct wmi_driver lwmi_cd_driver;

/* ======== Device components ======== */

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
	return priv->info->type == *(enum lwmi_cd_type *)type;
}

/**
 * lwmi_cd_match_add_all() - Add all match rule for the master driver.
 * @master: Pointer to the master device.
 * @matchptr: Pointer to the returned component_match pointer.
 *
 * Adds all component matches to the list stored in @matchptr for the @master
 * device. @matchptr must be initialized to NULL.
 *
 * Missing components are not added to the match list, or else the master waits
 * for them forever. For some components, their existence can be determined by
 * querying other components, so they are handled as sub-components and thus not
 * matched here. For components with no existence declaration, wmidev_exists()
 * is used to determine their existence.
 *
 * If all components are missing, ERR_PTR(-ENXIO) is returned through @matchptr.
 */
void lwmi_cd_match_add_all(struct device *master, struct component_match **matchptr)
{
	bool matched = false;
	int ret, i;

	if (WARN_ON(*matchptr))
		return;

	for (i = 0; i < ARRAY_SIZE(lwmi_cd_table); i++) {
		guid_t guid;

		/* Skip sub-components. */
		if (lwmi_cd_table[i].type == LENOVO_FAN_TEST_DATA)
			continue;

		ret = guid_parse(lwmi_cd_table[i].guid_string, &guid);
		if (ret < 0) {
			*matchptr = ERR_PTR(ret);
			return;
		}

		if (!wmidev_exists(&guid))
			continue;

		component_match_add(master, matchptr, lwmi_cd_match,
				    (void *)&lwmi_cd_table[i].type);
		if (IS_ERR(*matchptr))
			return;

		matched = true;
	}

	if (!matched)
		*matchptr = ERR_PTR(-ENXIO);
}
EXPORT_SYMBOL_NS_GPL(lwmi_cd_match_add_all, "LENOVO_WMI_CAPDATA");

/**
 * lwmi_cd_call_master_cb() - Call the master callback for the sub-component.
 * @priv: Pointer to the capability data private data.
 *
 * Call the master callback and pass the sub-component list to it if the
 * dependency chain (master <-> sub-master <-> sub-component) is complete.
 */
static void lwmi_cd_call_master_cb(struct lwmi_cd_priv *priv)
{
	struct cd_list *sub_component_list = priv->sub_master->sub_component_list;

	/*
	 * Call the callback only if the dependency chain is ready:
	 * - Binding between master and sub-master: fills master_dev and master_cb
	 * - Binding between sub-master and sub-component: fills sub_component_list
	 *
	 * If a binding has been unbound before the other binding is bound, the
	 * corresponding members filled by the former are guaranteed to be cleared.
	 *
	 * This function is only called in bind callbacks, and the component
	 * framework guarantees bind/unbind callbacks may never execute
	 * simultaneously, which implies that it's impossible to have a race
	 * condition.
	 *
	 * Hence, this check is sufficient to ensure that the callback is called
	 * at most once and with the correct state, without relying on a specific
	 * sequence of binding establishment.
	 */
	if (!sub_component_list ||
	    !priv->sub_master->master_dev ||
	    !priv->sub_master->master_cb)
		return;

	if (PTR_ERR(sub_component_list) == -ENODEV)
		sub_component_list = NULL;
	else if (WARN_ON(IS_ERR(sub_component_list)))
		return;

	priv->sub_master->master_cb(priv->sub_master->master_dev,
				    sub_component_list);

	/*
	 * Userspace may unbind a device from its driver and bind it again
	 * through sysfs. Let's call this operation "reprobe" to distinguish it
	 * from component "rebind".
	 *
	 * When reprobing capdata00/01 or the master device, the master device
	 * is unbound from us with appropriate cleanup before we bind to it and
	 * call master_cb. Everything is fine in this case.
	 *
	 * When reprobing capdata_fan, the master device has never been unbound
	 * from us (hence no cleanup is done)[1], but we call master_cb the
	 * second time. To solve this issue, we clear master_cb and master_dev
	 * so we won't call master_cb twice while a binding is still complete.
	 *
	 * Note that we can't clear sub_component_list, otherwise reprobing
	 * capdata01 or the master device causes master_cb to be never called
	 * after we rebind to the master device.
	 *
	 * [1]: The master device does not need capdata_fan in run time, so
	 * losing capdata_fan will not break the binding to the master device.
	 */
	priv->sub_master->master_cb = NULL;
	priv->sub_master->master_dev = NULL;
}

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
 * If cd_dev is a sub-master, try to call the master callback.
 *
 * Return: 0
 */
static int lwmi_cd_component_bind(struct device *cd_dev,
				  struct device *om_dev, void *data)
{
	struct lwmi_cd_priv *priv = dev_get_drvdata(cd_dev);
	struct lwmi_cd_binder *binder = data;

	switch (priv->info->type) {
	case LENOVO_CAPABILITY_DATA_00:
		binder->cd00_list = priv->list;

		/* Missing sub-master info implies missing sub-component. */
		if (!priv->sub_master) {
			binder->cd_fan_list_cb(om_dev, NULL);
			break;
		}

		priv->sub_master->master_dev = om_dev;
		priv->sub_master->master_cb = binder->cd_fan_list_cb;
		lwmi_cd_call_master_cb(priv);

		break;
	case LENOVO_CAPABILITY_DATA_01:
		binder->cd01_list = priv->list;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * lwmi_cd_component_unbind() - Unbind component to master device.
 * @cd_dev: Pointer to the lenovo-wmi-capdata driver parent device.
 * @om_dev: Pointer to the lenovo-wmi-other driver parent device.
 * @data: Unused.
 *
 * If cd_dev is a sub-master, clear the collected data from the master device to
 * prevent the binding establishment between the sub-master and the sub-
 * component (if it's about to happen) from calling the master callback.
 */
static void lwmi_cd_component_unbind(struct device *cd_dev,
				     struct device *om_dev, void *data)
{
	struct lwmi_cd_priv *priv = dev_get_drvdata(cd_dev);

	if (priv->sub_master) {
		priv->sub_master->master_dev = NULL;
		priv->sub_master->master_cb = NULL;
	}
}

static const struct component_ops lwmi_cd_component_ops = {
	.bind = lwmi_cd_component_bind,
	.unbind = lwmi_cd_component_unbind,
};

/**
 * lwmi_cd_sub_master_bind() - Bind sub-component of sub-master device
 * @dev: The sub-master capdata basic device.
 *
 * Call component_bind_all to bind the sub-component device to the sub-master
 * device. On success, collect the pointer (or ERR_PTR(-ENODEV) if it's stubbed)
 * to the sub-component list and try to call the master callback.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_cd_sub_master_bind(struct device *dev)
{
	struct lwmi_cd_priv *priv = dev_get_drvdata(dev);
	struct cd_list *sub_component_list;
	int ret;

	ret = component_bind_all(dev, &sub_component_list);
	if (ret)
		return ret;

	priv->sub_master->sub_component_list = sub_component_list ?: ERR_PTR(-ENODEV);
	lwmi_cd_call_master_cb(priv);

	return 0;
}

/**
 * lwmi_cd_sub_master_unbind() - Unbind sub-component of sub-master device
 * @dev: The sub-master capdata basic device
 *
 * Clear the collected pointer to the sub-component list to prevent the binding
 * establishment between the sub-master and the sub-component (if it's about to
 * happen) from calling the master callback. Then, call component_unbind_all to
 * unbind the sub-component device from the sub-master device.
 */
static void lwmi_cd_sub_master_unbind(struct device *dev)
{
	struct lwmi_cd_priv *priv = dev_get_drvdata(dev);

	priv->sub_master->sub_component_list = NULL;

	component_unbind_all(dev, NULL);
}

static const struct component_master_ops lwmi_cd_sub_master_ops = {
	.bind = lwmi_cd_sub_master_bind,
	.unbind = lwmi_cd_sub_master_unbind,
};

/**
 * lwmi_cd_sub_master_add() - Register a sub-master with its sub-component
 * @priv: Pointer to the sub-master capdata device private data.
 * @sub_component_type: Type of the sub-component.
 *
 * Match the sub-component type and register the current capdata device as a
 * sub-master. If the given sub-component type is CD_TYPE_NONE, mark the sub-
 * component as non-existent without registering sub-master.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_cd_sub_master_add(struct lwmi_cd_priv *priv,
				  enum lwmi_cd_type sub_component_type)
{
	struct component_match *master_match = NULL;
	int ret;

	/*
	 * If the sub-master has no sub-component, there is no need to allocate
	 * or register the sub-master. The master callback will be called with
	 * NULL on master <-> sub-master bind to inform the master about the
	 * absence of sub-component.
	 */
	if (sub_component_type == CD_TYPE_NONE)
		return 0;

	priv->sub_master = devm_kzalloc(&priv->wdev->dev, sizeof(*priv->sub_master), GFP_KERNEL);
	if (!priv->sub_master)
		return -ENOMEM;

	/*
	 * lwmi_cd_match() needs a pointer to enum lwmi_cd_type, but on-stack
	 * data cannot be used here. Steal one from lwmi_cd_table.
	 */
	component_match_add(&priv->wdev->dev, &master_match, lwmi_cd_match,
			    (void *)&lwmi_cd_table[sub_component_type].type);
	if (IS_ERR(master_match))
		return PTR_ERR(master_match);

	ret = component_master_add_with_match(&priv->wdev->dev, &lwmi_cd_sub_master_ops,
					      master_match);
	if (ret)
		return ret;

	priv->sub_master->registered = true;
	return 0;
}

/**
 * lwmi_cd_sub_master_del() - Unregister a sub-master if it's registered
 * @priv: Pointer to the sub-master capdata device private data.
 */
static void lwmi_cd_sub_master_del(struct lwmi_cd_priv *priv)
{
	if (!priv->sub_master || !priv->sub_master->registered)
		return;

	component_master_del(&priv->wdev->dev, &lwmi_cd_sub_master_ops);
	priv->sub_master->registered = false;
}

/**
 * lwmi_cd_sub_component_bind() - Bind sub-component to sub-master device.
 * @sc_dev: Pointer to the sub-component capdata parent device.
 * @sm_dev: Pointer to the sub-master capdata parent device.
 * @data: Pointer used to return the capability data list pointer.
 *
 * On sub-master's bind, provide a pointer to the local capdata list.
 * This is used by the sub-master to call the master callback.
 *
 * Return: 0
 */
static int lwmi_cd_sub_component_bind(struct device *sc_dev,
				      struct device *sm_dev, void *data)
{
	struct lwmi_cd_priv *priv = dev_get_drvdata(sc_dev);
	struct cd_list **listp = data;

	*listp = priv->list;

	return 0;
}

static const struct component_ops lwmi_cd_sub_component_ops = {
	.bind = lwmi_cd_sub_component_bind,
};

/*
 * lwmi_cd*_get_data - Get the data of the specified attribute
 * @list: The lenovo-wmi-capdata pointer to its cd_list struct.
 * @attribute_id: The capdata attribute ID (non-zero) to be found.
 * @output: Pointer to a capdata* struct to return the data.
 *
 * Retrieves the capability data struct pointer for the given
 * attribute.
 *
 * Return: 0 on success, or -EINVAL.
 */
#define DEF_LWMI_CDXX_GET_DATA(_cdxx, _output_t)						\
	int lwmi_##_cdxx##_get_data(struct cd_list *list, u32 attribute_id, _output_t *output)	\
	{											\
		u8 idx;										\
												\
		if (WARN_ON(!list || !attribute_id))						\
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

DEF_LWMI_CDXX_GET_DATA(cd00, struct capdata00);
EXPORT_SYMBOL_NS_GPL(lwmi_cd00_get_data, "LENOVO_WMI_CAPDATA");

DEF_LWMI_CDXX_GET_DATA(cd01, struct capdata01);
EXPORT_SYMBOL_NS_GPL(lwmi_cd01_get_data, "LENOVO_WMI_CAPDATA");

DEF_LWMI_CDXX_GET_DATA(cd_fan, struct capdata_fan);
EXPORT_SYMBOL_NS_GPL(lwmi_cd_fan_get_data, "LENOVO_WMI_CAPDATA");

/* ======== debugfs ======== */

/**
 * lwmi_cd00_show() - Dump capdata00
 * @s: Pointer to seq_file where the capdata00 is dumped.
 * @cd00: Pointer to a capdata00 struct to be dumped.
 */
static void lwmi_cd00_show(struct seq_file *s, struct capdata00 *cd00)
{
	u8 dev = FIELD_GET(LWMI_ATTR_DEV_ID_MASK, cd00->id);
	u8 feat = FIELD_GET(LWMI_ATTR_FEAT_ID_MASK, cd00->id);
	u8 mode = FIELD_GET(LWMI_ATTR_MODE_ID_MASK, cd00->id);
	u8 type = FIELD_GET(LWMI_ATTR_TYPE_ID_MASK, cd00->id);
	bool extra = cd00->supported & ~(LWMI_SUPP_GET | LWMI_SUPP_SET | LWMI_SUPP_VALID);
	bool get = cd00->supported & LWMI_SUPP_GET;
	bool set = cd00->supported & LWMI_SUPP_SET;
	bool valid = cd00->supported & LWMI_SUPP_VALID;

	seq_printf(s, "  id:             0x%08x [dev: %2u, feat: %2u, mode: %2u, type: %2u]\n",
		   cd00->id, dev, feat, mode, type);

	seq_printf(s, "  supported:      0x%08x [%c%c%c%c]\n", cd00->supported,
		   extra ? '+' : ' ',
		   get   ? 'R' : ' ',
		   set   ? 'W' : ' ',
		   valid ? 'V' : ' ');

	seq_printf(s, "  default_value:  %u\n", cd00->default_value);
}

/**
 * lwmi_cd01_show() - Dump capdata01
 * @s: Pointer to seq_file where the capdata01 is dumped.
 * @cd01: Pointer to a capdata01 struct to be dumped.
 */
static void lwmi_cd01_show(struct seq_file *s, struct capdata01 *cd01)
{
	/* capdata01 is an extension to capdata00. */
	lwmi_cd00_show(s, &cd01->cd00);

	seq_printf(s, "  step:           %u\n", cd01->step);
	seq_printf(s, "  min_value:      %u\n", cd01->min_value);
	seq_printf(s, "  max_value:      %u\n", cd01->max_value);
}

/**
 * lwmi_cd_fan_show() - Dump capdata_fan
 * @s: Pointer to seq_file where the capdata_fan is dumped.
 * @cd_fan: Pointer to a capdata_fan struct to be dumped.
 */
static void lwmi_cd_fan_show(struct seq_file *s, struct capdata_fan *cd_fan)
{
	seq_printf(s, "  id:             %u\n", cd_fan->id);
	seq_printf(s, "  min_rpm:        %u\n", cd_fan->min_rpm);
	seq_printf(s, "  max_rpm:        %u\n", cd_fan->max_rpm);
}

/**
 * lwmi_cd_debugfs_show() - Dump capability data to debugfs
 * @s: Pointer to seq_file where the capability data is dumped.
 * @data: unused.
 *
 * Return: 0
 */
static int lwmi_cd_debugfs_show(struct seq_file *s, void *data)
{
	struct lwmi_cd_priv *priv = s->private;
	u8 idx;

	guard(mutex)(&priv->list->list_mutex);

	for (idx = 0; idx < priv->list->count; idx++) {
		seq_printf(s, "%s[%u]:\n", priv->info->name, idx);

		if (priv->info->type == LENOVO_CAPABILITY_DATA_00)
			lwmi_cd00_show(s, &priv->list->cd00[idx]);
		else if (priv->info->type == LENOVO_CAPABILITY_DATA_01)
			lwmi_cd01_show(s, &priv->list->cd01[idx]);
		else if (priv->info->type == LENOVO_FAN_TEST_DATA)
			lwmi_cd_fan_show(s, &priv->list->cd_fan[idx]);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(lwmi_cd_debugfs);

/**
 * lwmi_cd_debugfs_add() - Create debugfs directory and files for a device
 * @priv: lenovo-wmi-capdata driver data.
 */
static void lwmi_cd_debugfs_add(struct lwmi_cd_priv *priv)
{
	if (!priv->list)
		return;

	priv->debugfs_dir = lwmi_debugfs_create_dir(priv->wdev);

	debugfs_create_file("capdata", 0444, priv->debugfs_dir, priv, &lwmi_cd_debugfs_fops);
}

/**
 * lwmi_cd_debugfs_remove() - Remove debugfs directory for a device
 * @priv: lenovo-wmi-capdata driver data.
 */
static void lwmi_cd_debugfs_remove(struct lwmi_cd_priv *priv)
{
	/* Debugfs can handle NULL dir, no need to check. */
	debugfs_remove_recursive(priv->debugfs_dir);
	priv->debugfs_dir = NULL;
}

/* ======== WMI interface ======== */

/**
 * lwmi_cd_stub() - Stub the device by not providing any capability data
 * @priv: lenovo-wmi-capdata driver data.
 * @err: The occurred error.
 *
 * The Other Mode driver binds to both Capability Data 00 and 01. If either 00
 * or 01 fails to probe, the Other Mode device will fail to provide fw-attr or
 * hwmon/power_supply_ext devices for the other functional capdata device.
 *
 * Therefore, errors must be non-fatal in order not to break the assumptions
 * made by the device component framework, so that the Other Mode device can
 * provide whatever is functional.
 *
 * After stubbing the device, a NULL list will be passed to the Other Mode
 * device on bind.
 *
 * Return: 0 if the @err is suppressed, otherwise its propagated as is.
 */
static int lwmi_cd_stub(struct lwmi_cd_priv *priv, int err)
{
	/*
	 * No barrier is required in any possible code paths:
	 * - called from lwmi_cd_cache(): a proper barrier has been employed
	 * - called from lwmi_cd_fan_list_alloc_cache(): early setup does not
	 *   require synchronization, and priv->initialized must be false
	 */
	bool initialized = READ_ONCE(priv->initialized);

	dev_warn(&priv->wdev->dev, "%s %s (%u items) due to error: %d\n",
		 initialized ? "clearing" : "stubbing", priv->info->name,
		 priv->list ? priv->list->count : 0, err);

	/* Simply print the warning message. */
	if (!priv->list)
		return initialized ? err : 0;

	/*
	 * Stub the device on initialization errors.
	 *
	 * Note that if a transient error occurs during initialization, there is
	 * nothing we can do to re-enable the functionalities of the Other Mode
	 * driver even if a successful runtime notifier call making the relevant
	 * capdata emerge, as registering functionalities without the protection
	 * of the component framework is unsafe. Therefore, registering the
	 * notifier for a stubbed device is meaningless, and we don't need to
	 * distinguish transient and pernament errors during initialization.
	 *
	 * For runtime errors (see below), the registered functionalities
	 * (fw-attr) become temporarily unavailable due to the unavailablilty of
	 * the relevant capdata, and will recover after a successful runtime
	 * notifier call.
	 */
	if (!initialized) {
		devm_kfree(&priv->wdev->dev, priv->list);
		priv->list = NULL;

		return 0;
	}

	/*
	 * Runtime errors are transient. Simply clear all cached data and
	 * propagate the error.
	 *
	 * Since a valid attribute id is never 0 (the firmware also untilize the
	 * fact to stub some capabilities according to platform metadata),
	 * clearing the cached data effectively makes all attributes temporarily
	 * unavailable until the next notifier call.
	 */

	lockdep_assert_held(&priv->list->list_mutex);

	switch (priv->info->type) {
	case LENOVO_CAPABILITY_DATA_01:
		memset(priv->list->cd01, 0,
		       flex_array_size(priv->list, cd01, priv->list->count));
		break;
	default:
		unreachable();
	}

	return err;
}

/**
 * __lwmi_cd_cache() - Cache all WMI data block information locklessly
 * @priv: lenovo-wmi-capdata driver data.
 *
 * Loop through each WMI data block and cache the data locklessly.
 *
 * Return: 0 on success, or an error.
 */
static int __lwmi_cd_cache(struct lwmi_cd_priv *priv)
{
	bool got_data = false;
	size_t size;
	int idx;
	void *p;

	switch (priv->info->type) {
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

	for (idx = 0; idx < priv->list->count; idx++, p += size) {
		struct wmi_buffer wbuf;
		int ret;

		ret = wmidev_query_block(priv->wdev, idx, &wbuf, size);
		if (ret == -ENODATA) /* The block is too short, probably stubbed. */
			continue;
		if (ret)
			return lwmi_cd_stub(priv, ret);

		/* Capdata 01 is an extension to capdata 00. */
		struct capdata00 *capdata __free(kfree) = wbuf.data;

		memcpy(p, capdata, size);

		/*
		 * A valid attribute always has a non-zero id. IOW, a zero id
		 * implies a stubbed item (or the query method being stubbed).
		 *
		 * The stubbed item is still copied to the cache slot anyway, in
		 * case the firmware dynamically stubs the capdata item after
		 * switching Game Zone mode. In this manner, the previously
		 * exposed capdata can be void.
		 */
		if (capdata->id)
			got_data = true;
	}

	/*
	 * Some devices may stub the query method because it doesn't support the
	 * relevant interfaces at all. For example, most ThinkBook devices
	 * doesn't support Game Zone or Other Mode thermal tuning, so capdata 01
	 * is useless and stubbed.
	 *
	 * Stub the device and release needless resources.
	 */
	if (!got_data)
		return lwmi_cd_stub(priv, -ENODATA);

	return 0;
}

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
	if (!priv->list)
		return 0;

	/* Synchronize mutex initialization. Paired with lwmi_cd_finalize(). */
	if (!smp_load_acquire(&priv->initialized))
		return __lwmi_cd_cache(priv);

	switch (priv->info->type) {
	case LENOVO_CAPABILITY_DATA_01:
		break;
	default:
		return -EINVAL;
	}

	guard(mutex)(&priv->list->list_mutex);
	return __lwmi_cd_cache(priv);
}

struct lwmi_cd_fan_block {
	u32 nr;
	u32 data[]; /* id[nr], max_rpm[nr], min_rpm[nr] */
} __packed;

/**
 * lwmi_cd_fan_list_alloc_cache() - Alloc and cache Fan Test Data list
 * @priv: lenovo-wmi-capdata driver data.
 *
 * Return: 0 on success, or an error.
 */
static int lwmi_cd_fan_list_alloc_cache(struct lwmi_cd_priv *priv)
{
	struct wmi_buffer wbuf;
	bool got_data = false;
	struct cd_list *list;
	int ret, idx;
	u32 count;

	ret = wmidev_query_block(priv->wdev, 0, &wbuf, sizeof(struct lwmi_cd_fan_block));
	if (ret == -ENODATA) /* The block is too short, probably stubbed. */
		return 0;
	if (ret)
		return lwmi_cd_stub(priv, ret); /* Print the warning message. */

	struct lwmi_cd_fan_block *block __free(kfree) = wbuf.data;

	count = block->nr;

	if (count > U8_MAX) {
		dev_warn(&priv->wdev->dev,
			 "too many fans reported: %u > %u, ignoring\n", count, U8_MAX);
		count = 0;
	} else if (wbuf.length < struct_size(block, data, count * 3)) {
		dev_warn(&priv->wdev->dev,
			 "incomplete fan test data block: %zu < %zu (%u fans), ignoring\n",
			 wbuf.length, struct_size(block, data, count * 3), count);
		count = 0;
	}

	if (!count)
		return 0;

	list = devm_kzalloc(&priv->wdev->dev, struct_size(list, cd_fan, count), GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	list->count = count;
	priv->list = list;

	for (idx = 0; idx < count; idx++) {
		if (!block->data[idx]) /* Ignore stub. */
			continue;

		list->cd_fan[idx] = (struct capdata_fan) {
			.id      = block->data[idx],
			.max_rpm = block->data[idx + count],
			.min_rpm = block->data[idx + (2 * count)],
		};

		got_data = true;
	}

	if (!got_data)
		return lwmi_cd_stub(priv, -ENODATA);

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
	int count;

	count = wmidev_instance_count(priv->wdev);
	if (!count)
		return 0;

	switch (priv->info->type) {
	case LENOVO_CAPABILITY_DATA_00:
		list_size = struct_size(list, cd00, count);
		break;
	case LENOVO_CAPABILITY_DATA_01:
		list_size = struct_size(list, cd01, count);
		break;
	case LENOVO_FAN_TEST_DATA:
		return lwmi_cd_fan_list_alloc_cache(priv);
	default:
		return -EINVAL;
	}

	list = devm_kzalloc(&priv->wdev->dev, list_size, GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	list->count = count;
	priv->list = list;

	return 0;
}

/**
 * lwmi_cd_finalize() - Finalize the capability data initialization
 * @priv: lenovo-wmi-capdata driver data.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_cd_finalize(struct lwmi_cd_priv *priv)
{
	int ret;

	if (priv->list) {
		ret = devm_mutex_init(&priv->wdev->dev, &priv->list->list_mutex);
		if (ret)
			return ret;
	}

	/* Synchronize mutex initialization. Paired with lwmi_cd_cache(). */
	smp_store_release(&priv->initialized, 1);
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

	ret = lwmi_cd_cache(priv);
	if (ret)
		return ret;

	return lwmi_cd_finalize(priv);
}

/**
 * lwmi_cd01_notifier_call() - Call method for cd01 notifier.
 * @nb: The cd01 ACPI notifier_block.
 * @action: Unused.
 * @data: The ACPI event.
 *
 * Refresh cached capdata on AC status change.
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
		if (ret) {
			dev_warn(&priv->wdev->dev,
				 "failed to refresh capdata on AC change: %d\n", ret);

			/*
			 * Our error is non-fatal for others, and we still want
			 * the other event consumers to process it.
			 */
			return NOTIFY_DONE;
		}

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

	priv->info = info;
	priv->wdev = wdev;
	dev_set_drvdata(&wdev->dev, priv);

	ret = lwmi_cd_setup(priv);
	if (ret)
		goto out;

	switch (info->type) {
	case LENOVO_CAPABILITY_DATA_00: {
		enum lwmi_cd_type sub_component_type = LENOVO_FAN_TEST_DATA;
		struct capdata00 capdata00;

		ret = priv->list ? lwmi_cd00_get_data(priv->list, LWMI_ATTR_ID_FAN_TEST,
						      &capdata00) : -ENODATA;
		if (ret || !(capdata00.supported & LWMI_SUPP_VALID)) {
			dev_dbg(&wdev->dev, "capdata00 declares no fan test support\n");
			sub_component_type = CD_TYPE_NONE;
		}

		/* Sub-master (capdata00) <-> sub-component (capdata_fan) */
		ret = lwmi_cd_sub_master_add(priv, sub_component_type);
		if (ret)
			goto out;

		/* Master (lenovo-wmi-other) <-> sub-master (capdata00) */
		ret = component_add(&wdev->dev, &lwmi_cd_component_ops);
		if (ret)
			lwmi_cd_sub_master_del(priv);

		goto out;
	}
	case LENOVO_CAPABILITY_DATA_01:
		priv->acpi_nb.notifier_call = lwmi_cd01_notifier_call;

		if (priv->list) {
			ret = register_acpi_notifier(&priv->acpi_nb);
			if (ret)
				goto out;

			ret = devm_add_action_or_reset(&wdev->dev, lwmi_cd01_unregister,
						       &priv->acpi_nb);
			if (ret)
				goto out;
		}

		ret = component_add(&wdev->dev, &lwmi_cd_component_ops);
		goto out;
	case LENOVO_FAN_TEST_DATA:
		ret = component_add(&wdev->dev, &lwmi_cd_sub_component_ops);
		goto out;
	default:
		return -EINVAL;
	}
out:
	if (ret) {
		dev_err(&wdev->dev, "failed to register %s: %d\n",
			info->name, ret);
	} else {
		lwmi_cd_debugfs_add(priv);

		dev_dbg(&wdev->dev, "registered %s with %u items\n",
			info->name, priv->list ? priv->list->count : 0);
	}
	return ret;
}

static void lwmi_cd_remove(struct wmi_device *wdev)
{
	struct lwmi_cd_priv *priv = dev_get_drvdata(&wdev->dev);

	lwmi_cd_debugfs_remove(priv);

	switch (priv->info->type) {
	case LENOVO_CAPABILITY_DATA_00:
		lwmi_cd_sub_master_del(priv);
		fallthrough;
	case LENOVO_CAPABILITY_DATA_01:
		component_del(&wdev->dev, &lwmi_cd_component_ops);
		break;
	case LENOVO_FAN_TEST_DATA:
		component_del(&wdev->dev, &lwmi_cd_sub_component_ops);
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

MODULE_IMPORT_NS("LENOVO_WMI_HELPERS");
MODULE_DEVICE_TABLE(wmi, lwmi_cd_id_table);
MODULE_AUTHOR("Derek J. Clark <derekjohn.clark@gmail.com>");
MODULE_AUTHOR("Rong Zhang <i@rong.moe>");
MODULE_DESCRIPTION("Lenovo Capability Data WMI Driver");
MODULE_LICENSE("GPL");
