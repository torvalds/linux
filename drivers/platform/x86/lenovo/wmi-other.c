// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Lenovo Other Mode WMI interface driver.
 *
 * This driver uses the fw_attributes class to expose the various WMI functions
 * provided by the "Other Mode" WMI interface. This enables CPU and GPU power
 * limit as well as various other attributes for devices that fall under the
 * "Gaming Series" of Lenovo laptop devices. Each attribute exposed by the
 * "Other Mode" interface has a corresponding Capability Data struct that
 * allows the driver to probe details about the attribute such as if it is
 * supported by the hardware, the default_value, max_value, min_value, and step
 * increment.
 *
 * These attributes typically don't fit anywhere else in the sysfs and are set
 * in Windows using one of Lenovo's multiple user applications.
 *
 * Additionally, this driver also exports tunable fan speed RPM to HWMON.
 * Min/max RPM are also provided for reference.
 *
 * Copyright (C) 2025 Derek J. Clark <derekjohn.clark@gmail.com>
 *   - fw_attributes
 *   - binding to Capability Data 01
 *
 * Copyright (C) 2025 Rong Zhang <i@rong.moe>
 *   - HWMON
 *   - binding to Capability Data 00 and Fan
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/component.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/gfp_types.h>
#include <linux/hwmon.h>
#include <linux/idr.h>
#include <linux/kdev_t.h>
#include <linux/kobject.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_profile.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include "wmi-capdata.h"
#include "wmi-events.h"
#include "wmi-gamezone.h"
#include "wmi-helpers.h"
#include "wmi-other.h"
#include "../firmware_attributes_class.h"

#define LENOVO_OTHER_MODE_GUID "DC2A8805-3A8C-41BA-A6F7-092E0089CD3B"

#define LWMI_DEVICE_ID_CPU 0x01

#define LWMI_FEATURE_ID_CPU_SPPT 0x01
#define LWMI_FEATURE_ID_CPU_SPL 0x02
#define LWMI_FEATURE_ID_CPU_FPPT 0x03

#define LWMI_FEATURE_ID_FAN_RPM 0x03

#define LWMI_TYPE_ID_NONE 0x00

#define LWMI_FEATURE_VALUE_GET 17
#define LWMI_FEATURE_VALUE_SET 18

#define LWMI_FAN_ID_BASE 1
#define LWMI_FAN_NR 4
#define LWMI_FAN_ID(x) ((x) + LWMI_FAN_ID_BASE)

#define LWMI_ATTR_ID_FAN_RPM(x)						\
	(FIELD_PREP(LWMI_ATTR_DEV_ID_MASK, LWMI_DEVICE_ID_FAN) |	\
	 FIELD_PREP(LWMI_ATTR_FEAT_ID_MASK, LWMI_FEATURE_ID_FAN_RPM) |	\
	 FIELD_PREP(LWMI_ATTR_TYPE_ID_MASK, LWMI_FAN_ID(x)))

#define LWMI_FAN_DIV 100

#define LWMI_OM_FW_ATTR_BASE_PATH "lenovo-wmi-other"
#define LWMI_OM_HWMON_NAME "lenovo_wmi_other"

static BLOCKING_NOTIFIER_HEAD(om_chain_head);
static DEFINE_IDA(lwmi_om_ida);

enum attribute_property {
	DEFAULT_VAL,
	MAX_VAL,
	MIN_VAL,
	STEP_VAL,
	SUPPORTED,
};

struct lwmi_fan_info {
	u32 supported;
	u32 last_target;
	long min_rpm;
	long max_rpm;
};

struct lwmi_om_priv {
	struct component_master_ops *ops;

	/* only valid after capdata bind */
	struct cd_list *cd00_list;
	struct cd_list *cd01_list;

	struct device *hwmon_dev;
	struct device *fw_attr_dev;
	struct kset *fw_attr_kset;
	struct notifier_block nb;
	struct wmi_device *wdev;
	int ida_id;

	struct lwmi_fan_info fan_info[LWMI_FAN_NR];

	struct {
		bool capdata00_collected : 1;
		bool capdata_fan_collected : 1;
	} fan_flags;
};

/*
 * Visibility of fan channels:
 *
 * +-------------------+---------+------------------+-----------------------+------------+
 * |                   | default | +expose_all_fans | +relax_fan_constraint | +both      |
 * +-------------------+---------+------------------+-----------------------+------------+
 * | canonical         | RW      | RW               | RW+relaxed            | RW+relaxed |
 * +-------------------+---------+------------------+-----------------------+------------+
 * | -capdata_fan[idx] | N       | RO               | N                     | RW+relaxed |
 * +-------------------+---------+------------------+-----------------------+------------+
 *
 * Note:
 * 1. LWMI_ATTR_ID_FAN_RPM[idx].supported is always checked before exposing a channel.
 * 2. -capdata_fan implies -capdata_fan[idx].
 */
static bool expose_all_fans;
module_param(expose_all_fans, bool, 0444);
MODULE_PARM_DESC(expose_all_fans,
	"This option skips some capability checks and solely relies on per-channel ones "
	"to expose fan attributes. Use with caution.");

static bool relax_fan_constraint;
module_param(relax_fan_constraint, bool, 0444);
MODULE_PARM_DESC(relax_fan_constraint,
	"Do not enforce fan RPM constraint (div/min/max) "
	"and enables fan tuning when such data is missing. "
	"Enabling this may results in HWMON attributes being out-of-sync, "
	"and setting a too low RPM stops the fan. Use with caution.");

/* ======== HWMON (component: lenovo-wmi-capdata 00 & fan) ======== */

/**
 * lwmi_om_fan_get_set() - Get or set fan RPM value of specified fan
 * @priv: Driver private data structure
 * @channel: Fan channel index (0-based)
 * @val: Pointer to value (input for set, output for get)
 * @set: True to set value, false to get value
 *
 * Communicates with WMI interface to either retrieve current fan RPM
 * or set target fan RPM.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_om_fan_get_set(struct lwmi_om_priv *priv, int channel, u32 *val, bool set)
{
	struct wmi_method_args_32 args;
	u32 method_id, retval;
	int err;

	method_id = set ? LWMI_FEATURE_VALUE_SET : LWMI_FEATURE_VALUE_GET;
	args.arg0 = LWMI_ATTR_ID_FAN_RPM(channel);
	args.arg1 = set ? *val : 0;

	err = lwmi_dev_evaluate_int(priv->wdev, 0x0, method_id,
				    (unsigned char *)&args, sizeof(args), &retval);
	if (err)
		return err;

	if (!set) {
		*val = retval;
		return 0;
	}

	/*
	 * It seems that 0 means "no error" and 1 means "done". Apparently
	 * different firmware teams have different thoughts on indicating
	 * success, so we accepts both.
	 */
	return (retval == 0 || retval == 1) ? 0 : -EIO;
}

/**
 * lwmi_om_hwmon_is_visible() - Determine visibility of HWMON attributes
 * @drvdata: Driver private data
 * @type: Sensor type
 * @attr: Attribute identifier
 * @channel: Channel index
 *
 * Determines whether an HWMON attribute should be visible in sysfs
 * based on hardware capabilities and current configuration.
 *
 * Return: permission mode, or 0 if invisible.
 */
static umode_t lwmi_om_hwmon_is_visible(const void *drvdata, enum hwmon_sensor_types type,
					u32 attr, int channel)
{
	struct lwmi_om_priv *priv = (struct lwmi_om_priv *)drvdata;
	bool visible = false;

	if (type == hwmon_fan) {
		if (!(priv->fan_info[channel].supported & LWMI_SUPP_VALID))
			return 0;

		switch (attr) {
		case hwmon_fan_target:
			if (!(priv->fan_info[channel].supported & LWMI_SUPP_SET))
				return 0;

			if (relax_fan_constraint ||
			    (priv->fan_info[channel].min_rpm >= 0 &&
			     priv->fan_info[channel].max_rpm >= 0))
				return 0644;

			/*
			 * Reaching here implies expose_all_fans is set.
			 * See lwmi_om_hwmon_add().
			 */
			dev_warn_once(&priv->wdev->dev,
				      "fan tuning disabled due to missing RPM constraint\n");
			return 0;
		case hwmon_fan_div:
		case hwmon_fan_input:
			visible = priv->fan_info[channel].supported & LWMI_SUPP_GET;
			break;
		case hwmon_fan_min:
			visible = priv->fan_info[channel].min_rpm >= 0;
			break;
		case hwmon_fan_max:
			visible = priv->fan_info[channel].max_rpm >= 0;
			break;
		}
	}

	return visible ? 0444 : 0;
}

/**
 * lwmi_om_hwmon_read() - Read HWMON sensor data
 * @dev: Device pointer
 * @type: Sensor type
 * @attr: Attribute identifier
 * @channel: Channel index
 * @val: Pointer to store value
 *
 * Reads current sensor values from hardware through WMI interface.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_om_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long *val)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(dev);
	u32 retval = 0;
	int err;

	if (type == hwmon_fan) {
		switch (attr) {
		/*
		 * The EC has an internal RPM divisor (i.e., the raw register value is
		 * RPM / fanY_div). For fanY_input, the WMI method reads the register
		 * value and returns raw * fanY_div. For fanY_target, the WMI method
		 * divides the written value by fanY_div before writing it to the EC.
		 *
		 * As a result, reading fanY_input always returns a multiple of fanY_div,
		 * while writing to fanY_target loses the remainder.
		 */
		case hwmon_fan_div:
			*val = LWMI_FAN_DIV;
			return 0;
		case hwmon_fan_input:
			err = lwmi_om_fan_get_set(priv, channel, &retval, false);
			if (err)
				return err;

			*val = retval;
			return 0;
		case hwmon_fan_target:
			*val = priv->fan_info[channel].last_target;
			return 0;
		case hwmon_fan_min:
			*val = priv->fan_info[channel].min_rpm;
			return 0;
		case hwmon_fan_max:
			*val = priv->fan_info[channel].max_rpm;
			return 0;
		}
	}

	return -EOPNOTSUPP;
}

/**
 * lwmi_om_hwmon_write() - Write HWMON sensor data
 * @dev: Device pointer
 * @type: Sensor type
 * @attr: Attribute identifier
 * @channel: Channel index
 * @val: Value to write
 *
 * Writes configuration values to hardware through WMI interface.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_om_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
			       u32 attr, int channel, long val)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(dev);
	u32 raw, min_rpm, max_rpm;
	int err;

	if (type == hwmon_fan) {
		switch (attr) {
		case hwmon_fan_target:
			if (relax_fan_constraint) {
				min_rpm = 1;
				max_rpm = U16_MAX;
			} else {
				min_rpm = priv->fan_info[channel].min_rpm;
				max_rpm = priv->fan_info[channel].max_rpm;
			}

			/* 0 means "auto". */
			if (val != 0 && (val < min_rpm || val > max_rpm))
				return -EINVAL;

			/*
			 * The effective fanY_target is always a multiple of fanY_div
			 * due to the EC's internal RPM divisor (see lwmi_om_hwmon_read).
			 *
			 * Round down the written value to the nearest multiple of fanY_div
			 * to prevent mismatch between the effective value and last_target.
			 *
			 * For relax_fan_constraint, skip this conversion as setting a
			 * sub-fanY_div value is necessary to completely stop the fan on
			 * some devices.
			 */
			if (!relax_fan_constraint)
				raw = val / LWMI_FAN_DIV * LWMI_FAN_DIV;

			err = lwmi_om_fan_get_set(priv, channel, &raw, true);
			if (err)
				return err;

			priv->fan_info[channel].last_target = raw;
			return 0;
		}
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_channel_info * const lwmi_om_hwmon_info[] = {
	/* Must match LWMI_FAN_NR. */
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_DIV |
			   HWMON_F_MIN | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_DIV |
			   HWMON_F_MIN | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_DIV |
			   HWMON_F_MIN | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_DIV |
			   HWMON_F_MIN | HWMON_F_MAX),
	NULL
};

static const struct hwmon_ops lwmi_om_hwmon_ops = {
	.is_visible = lwmi_om_hwmon_is_visible,
	.read = lwmi_om_hwmon_read,
	.write = lwmi_om_hwmon_write,
};

static const struct hwmon_chip_info lwmi_om_hwmon_chip_info = {
	.ops = &lwmi_om_hwmon_ops,
	.info = lwmi_om_hwmon_info,
};

/**
 * lwmi_om_hwmon_add() - Register HWMON device if all info is collected
 * @priv: Driver private data
 */
static void lwmi_om_hwmon_add(struct lwmi_om_priv *priv)
{
	int i, valid;

	if (WARN_ON(priv->hwmon_dev))
		return;

	if (!priv->fan_flags.capdata00_collected || !priv->fan_flags.capdata_fan_collected) {
		dev_dbg(&priv->wdev->dev, "HWMON registration pending (00: %d, fan: %d)\n",
			priv->fan_flags.capdata00_collected,
			priv->fan_flags.capdata_fan_collected);
		return;
	}

	if (expose_all_fans)
		dev_warn(&priv->wdev->dev, "all fans exposed. Use with caution\n");

	if (relax_fan_constraint)
		dev_warn(&priv->wdev->dev, "fan RPM constraint relaxed. Use with caution\n");

	valid = 0;
	for (i = 0; i < LWMI_FAN_NR; i++) {
		if (!(priv->fan_info[i].supported & LWMI_SUPP_VALID))
			continue;

		valid++;

		if (!expose_all_fans &&
		    (priv->fan_info[i].min_rpm < 0 || priv->fan_info[i].max_rpm < 0)) {
			dev_dbg(&priv->wdev->dev, "missing RPM constraint for fan%d, hiding\n",
				LWMI_FAN_ID(i));
			priv->fan_info[i].supported = 0;
			valid--;
		}
	}

	if (valid == 0) {
		dev_warn(&priv->wdev->dev,
			 "fan reporting/tuning is unsupported on this device\n");
		return;
	}

	priv->hwmon_dev = hwmon_device_register_with_info(&priv->wdev->dev,
							  LWMI_OM_HWMON_NAME, priv,
							  &lwmi_om_hwmon_chip_info,
							  NULL);
	if (IS_ERR(priv->hwmon_dev)) {
		dev_warn(&priv->wdev->dev, "failed to register HWMON device: %ld\n",
			 PTR_ERR(priv->hwmon_dev));
		priv->hwmon_dev = NULL;
		return;
	}

	dev_dbg(&priv->wdev->dev, "registered HWMON device\n");
}

/**
 * lwmi_om_hwmon_remove() - Unregister HWMON device
 * @priv: Driver private data
 *
 * Unregisters the HWMON device if applicable.
 */
static void lwmi_om_hwmon_remove(struct lwmi_om_priv *priv)
{
	if (!priv->hwmon_dev)
		return;

	hwmon_device_unregister(priv->hwmon_dev);
	priv->hwmon_dev = NULL;
}

/**
 * lwmi_om_fan_info_init() - Initialzie fan info
 * @priv: Driver private data
 *
 * lwmi_om_fan_info_collect_cd00() and lwmi_om_fan_info_collect_cd_fan() may be
 * called in an arbitrary order. Hence, initializion must be done before.
 */
static void lwmi_om_fan_info_init(struct lwmi_om_priv *priv)
{
	int i;

	for (i = 0; i < LWMI_FAN_NR; i++) {
		priv->fan_info[i] = (struct lwmi_fan_info) {
			.supported = 0,
			/*
			 * Assume 0 on probe as the EC resets all fans to auto mode on (re)boot.
			 *
			 * Note that S0ix (s2idle) preserves the RPM target, so we don't need
			 * suspend/resume callbacks. This behavior has not been tested on S3-
			 * capable devices, but I doubt if such devices even have this interface.
			 */
			.last_target = 0,
			.min_rpm = -ENODATA,
			.max_rpm = -ENODATA,
		};
	}

	priv->fan_flags.capdata00_collected = false;
	priv->fan_flags.capdata_fan_collected = false;
}

/**
 * lwmi_om_fan_info_collect_cd00() - Collect fan info from capdata 00
 * @priv: Driver private data
 */
static void lwmi_om_fan_info_collect_cd00(struct lwmi_om_priv *priv)
{
	struct capdata00 capdata00;
	int i, err;

	dev_dbg(&priv->wdev->dev, "Collecting fan info from capdata00\n");

	for (i = 0; i < LWMI_FAN_NR; i++) {
		err = lwmi_cd00_get_data(priv->cd00_list, LWMI_ATTR_ID_FAN_RPM(i), &capdata00);
		priv->fan_info[i].supported = err ? 0 : capdata00.supported;
	}

	priv->fan_flags.capdata00_collected = true;
	lwmi_om_hwmon_add(priv);
}

/**
 * lwmi_om_fan_info_collect_cd_fan() - Collect fan info from capdata fan
 * @dev: Pointer to the lenovo-wmi-other device
 * @cd_fan_list: Pointer to the capdata fan list
 */
static void lwmi_om_fan_info_collect_cd_fan(struct device *dev, struct cd_list *cd_fan_list)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(dev);
	struct capdata_fan capdata_fan;
	int i, err;

	dev_dbg(dev, "Collecting fan info from capdata_fan\n");

	if (!cd_fan_list)
		goto out;

	for (i = 0; i < LWMI_FAN_NR; i++) {
		err = lwmi_cd_fan_get_data(cd_fan_list, LWMI_FAN_ID(i), &capdata_fan);
		if (err)
			continue;

		priv->fan_info[i].min_rpm = capdata_fan.min_rpm;
		priv->fan_info[i].max_rpm = capdata_fan.max_rpm;
	}

out:
	priv->fan_flags.capdata_fan_collected = true;
	lwmi_om_hwmon_add(priv);
}

/* ======== fw_attributes (component: lenovo-wmi-capdata 01) ======== */

struct tunable_attr_01 {
	struct capdata01 *capdata;
	struct device *dev;
	u32 feature_id;
	u32 device_id;
	u32 type_id;
};

static struct tunable_attr_01 ppt_pl1_spl = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_SPL,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 ppt_pl2_sppt = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_SPPT,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 ppt_pl3_fppt = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_FPPT,
	.type_id = LWMI_TYPE_ID_NONE,
};

struct capdata01_attr_group {
	const struct attribute_group *attr_group;
	struct tunable_attr_01 *tunable_attr;
};

/**
 * lwmi_om_register_notifier() - Add a notifier to the blocking notifier chain
 * @nb: The notifier_block struct to register
 *
 * Call blocking_notifier_chain_register to register the notifier block to the
 * lenovo-wmi-other driver notifier chain.
 *
 * Return: 0 on success, %-EEXIST on error.
 */
int lwmi_om_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&om_chain_head, nb);
}
EXPORT_SYMBOL_NS_GPL(lwmi_om_register_notifier, "LENOVO_WMI_OTHER");

/**
 * lwmi_om_unregister_notifier() - Remove a notifier from the blocking notifier
 * chain.
 * @nb: The notifier_block struct to register
 *
 * Call blocking_notifier_chain_unregister to unregister the notifier block from the
 * lenovo-wmi-other driver notifier chain.
 *
 * Return: 0 on success, %-ENOENT on error.
 */
int lwmi_om_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&om_chain_head, nb);
}
EXPORT_SYMBOL_NS_GPL(lwmi_om_unregister_notifier, "LENOVO_WMI_OTHER");

/**
 * devm_lwmi_om_unregister_notifier() - Remove a notifier from the blocking
 * notifier chain.
 * @data: Void pointer to the notifier_block struct to register.
 *
 * Call lwmi_om_unregister_notifier to unregister the notifier block from the
 * lenovo-wmi-other driver notifier chain.
 *
 * Return: 0 on success, %-ENOENT on error.
 */
static void devm_lwmi_om_unregister_notifier(void *data)
{
	struct notifier_block *nb = data;

	lwmi_om_unregister_notifier(nb);
}

/**
 * devm_lwmi_om_register_notifier() - Add a notifier to the blocking notifier
 * chain.
 * @dev: The parent device of the notifier_block struct.
 * @nb: The notifier_block struct to register
 *
 * Call lwmi_om_register_notifier to register the notifier block to the
 * lenovo-wmi-other driver notifier chain. Then add devm_lwmi_om_unregister_notifier
 * as a device managed action to automatically unregister the notifier block
 * upon parent device removal.
 *
 * Return: 0 on success, or an error code.
 */
int devm_lwmi_om_register_notifier(struct device *dev,
				   struct notifier_block *nb)
{
	int ret;

	ret = lwmi_om_register_notifier(nb);
	if (ret < 0)
		return ret;

	return devm_add_action_or_reset(dev, devm_lwmi_om_unregister_notifier,
					nb);
}
EXPORT_SYMBOL_NS_GPL(devm_lwmi_om_register_notifier, "LENOVO_WMI_OTHER");

/**
 * lwmi_om_notifier_call() - Call functions for the notifier call chain.
 * @mode: Pointer to a thermal mode enum to retrieve the data from.
 *
 * Call blocking_notifier_call_chain to retrieve the thermal mode from the
 * lenovo-wmi-gamezone driver.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_om_notifier_call(enum thermal_mode *mode)
{
	int ret;

	ret = blocking_notifier_call_chain(&om_chain_head,
					   LWMI_GZ_GET_THERMAL_MODE, &mode);
	if ((ret & ~NOTIFY_STOP_MASK) != NOTIFY_OK)
		return -EINVAL;

	return 0;
}

/* Attribute Methods */

/**
 * int_type_show() - Emit the data type for an integer attribute
 * @kobj: Pointer to the driver object.
 * @kattr: Pointer to the attribute calling this function.
 * @buf: The buffer to write to.
 *
 * Return: Number of characters written to buf.
 */
static ssize_t int_type_show(struct kobject *kobj, struct kobj_attribute *kattr,
			     char *buf)
{
	return sysfs_emit(buf, "integer\n");
}

/**
 * attr_capdata01_show() - Get the value of the specified attribute property
 *
 * @kobj: Pointer to the driver object.
 * @kattr: Pointer to the attribute calling this function.
 * @buf: The buffer to write to.
 * @tunable_attr: The attribute to be read.
 * @prop: The property of this attribute to be read.
 *
 * Retrieves the given property from the capability data 01 struct for the
 * specified attribute's "custom" thermal mode. This function is intended
 * to be generic so it can be called from any integer attributes "_show"
 * function.
 *
 * If the WMI is success the sysfs attribute is notified.
 *
 * Return: Either number of characters written to buf, or an error code.
 */
static ssize_t attr_capdata01_show(struct kobject *kobj,
				   struct kobj_attribute *kattr, char *buf,
				   struct tunable_attr_01 *tunable_attr,
				   enum attribute_property prop)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(tunable_attr->dev);
	struct capdata01 capdata;
	u32 attribute_id;
	int value, ret;

	attribute_id =
		FIELD_PREP(LWMI_ATTR_DEV_ID_MASK, tunable_attr->device_id) |
		FIELD_PREP(LWMI_ATTR_FEAT_ID_MASK, tunable_attr->feature_id) |
		FIELD_PREP(LWMI_ATTR_MODE_ID_MASK,
			   LWMI_GZ_THERMAL_MODE_CUSTOM) |
		FIELD_PREP(LWMI_ATTR_TYPE_ID_MASK, tunable_attr->type_id);

	ret = lwmi_cd01_get_data(priv->cd01_list, attribute_id, &capdata);
	if (ret)
		return ret;

	switch (prop) {
	case DEFAULT_VAL:
		value = capdata.default_value;
		break;
	case MAX_VAL:
		value = capdata.max_value;
		break;
	case MIN_VAL:
		value = capdata.min_value;
		break;
	case STEP_VAL:
		value = capdata.step;
		break;
	default:
		return -EINVAL;
	}

	return sysfs_emit(buf, "%d\n", value);
}

/**
 * attr_current_value_store() - Set the current value of the given attribute
 * @kobj: Pointer to the driver object.
 * @kattr: Pointer to the attribute calling this function.
 * @buf: The buffer to read from, this is parsed to `int` type.
 * @count: Required by sysfs attribute macros, pass in from the callee attr.
 * @tunable_attr: The attribute to be stored.
 *
 * Sets the value of the given attribute when operating under the "custom"
 * smartfan profile. The current smartfan profile is retrieved from the
 * lenovo-wmi-gamezone driver and error is returned if the result is not
 * "custom". This function is intended to be generic so it can be called from
 * any integer attribute's "_store" function. The integer to be sent to the WMI
 * method is range checked and an error code is returned if out of range.
 *
 * If the value is valid and WMI is success, then the sysfs attribute is
 * notified.
 *
 * Return: Either count, or an error code.
 */
static ssize_t attr_current_value_store(struct kobject *kobj,
					struct kobj_attribute *kattr,
					const char *buf, size_t count,
					struct tunable_attr_01 *tunable_attr)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(tunable_attr->dev);
	struct wmi_method_args_32 args;
	struct capdata01 capdata;
	enum thermal_mode mode;
	u32 attribute_id;
	u32 value;
	int ret;

	ret = lwmi_om_notifier_call(&mode);
	if (ret)
		return ret;

	if (mode != LWMI_GZ_THERMAL_MODE_CUSTOM)
		return -EBUSY;

	attribute_id =
		FIELD_PREP(LWMI_ATTR_DEV_ID_MASK, tunable_attr->device_id) |
		FIELD_PREP(LWMI_ATTR_FEAT_ID_MASK, tunable_attr->feature_id) |
		FIELD_PREP(LWMI_ATTR_MODE_ID_MASK, mode) |
		FIELD_PREP(LWMI_ATTR_TYPE_ID_MASK, tunable_attr->type_id);

	ret = lwmi_cd01_get_data(priv->cd01_list, attribute_id, &capdata);
	if (ret)
		return ret;

	ret = kstrtouint(buf, 10, &value);
	if (ret)
		return ret;

	if (value < capdata.min_value || value > capdata.max_value)
		return -EINVAL;

	args.arg0 = attribute_id;
	args.arg1 = value;

	ret = lwmi_dev_evaluate_int(priv->wdev, 0x0, LWMI_FEATURE_VALUE_SET,
				    (unsigned char *)&args, sizeof(args), NULL);
	if (ret)
		return ret;

	return count;
};

/**
 * attr_current_value_show() - Get the current value of the given attribute
 * @kobj: Pointer to the driver object.
 * @kattr: Pointer to the attribute calling this function.
 * @buf: The buffer to write to.
 * @tunable_attr: The attribute to be read.
 *
 * Retrieves the value of the given attribute for the current smartfan profile.
 * The current smartfan profile is retrieved from the lenovo-wmi-gamezone driver.
 * This function is intended to be generic so it can be called from any integer
 * attribute's "_show" function.
 *
 * If the WMI is success the sysfs attribute is notified.
 *
 * Return: Either number of characters written to buf, or an error code.
 */
static ssize_t attr_current_value_show(struct kobject *kobj,
				       struct kobj_attribute *kattr, char *buf,
				       struct tunable_attr_01 *tunable_attr)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(tunable_attr->dev);
	struct wmi_method_args_32 args;
	enum thermal_mode mode;
	u32 attribute_id;
	int retval;
	int ret;

	ret = lwmi_om_notifier_call(&mode);
	if (ret)
		return ret;

	attribute_id =
		FIELD_PREP(LWMI_ATTR_DEV_ID_MASK, tunable_attr->device_id) |
		FIELD_PREP(LWMI_ATTR_FEAT_ID_MASK, tunable_attr->feature_id) |
		FIELD_PREP(LWMI_ATTR_MODE_ID_MASK, mode) |
		FIELD_PREP(LWMI_ATTR_TYPE_ID_MASK, tunable_attr->type_id);

	args.arg0 = attribute_id;

	ret = lwmi_dev_evaluate_int(priv->wdev, 0x0, LWMI_FEATURE_VALUE_GET,
				    (unsigned char *)&args, sizeof(args),
				    &retval);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", retval);
}

/* Lenovo WMI Other Mode Attribute macros */
#define __LWMI_ATTR_RO(_func, _name)                                  \
	{                                                             \
		.attr = { .name = __stringify(_name), .mode = 0444 }, \
		.show = _func##_##_name##_show,                       \
	}

#define __LWMI_ATTR_RO_AS(_name, _show)                               \
	{                                                             \
		.attr = { .name = __stringify(_name), .mode = 0444 }, \
		.show = _show,                                        \
	}

#define __LWMI_ATTR_RW(_func, _name) \
	__ATTR(_name, 0644, _func##_##_name##_show, _func##_##_name##_store)

/* Shows a formatted static variable */
#define __LWMI_ATTR_SHOW_FMT(_prop, _attrname, _fmt, _val)                     \
	static ssize_t _attrname##_##_prop##_show(                             \
		struct kobject *kobj, struct kobj_attribute *kattr, char *buf) \
	{                                                                      \
		return sysfs_emit(buf, _fmt, _val);                            \
	}                                                                      \
	static struct kobj_attribute attr_##_attrname##_##_prop =              \
		__LWMI_ATTR_RO(_attrname, _prop)

/* Attribute current value read/write */
#define __LWMI_TUNABLE_CURRENT_VALUE_CAP01(_attrname)                          \
	static ssize_t _attrname##_current_value_store(                        \
		struct kobject *kobj, struct kobj_attribute *kattr,            \
		const char *buf, size_t count)                                 \
	{                                                                      \
		return attr_current_value_store(kobj, kattr, buf, count,       \
						&_attrname);                   \
	}                                                                      \
	static ssize_t _attrname##_current_value_show(                         \
		struct kobject *kobj, struct kobj_attribute *kattr, char *buf) \
	{                                                                      \
		return attr_current_value_show(kobj, kattr, buf, &_attrname);  \
	}                                                                      \
	static struct kobj_attribute attr_##_attrname##_current_value =        \
		__LWMI_ATTR_RW(_attrname, current_value)

/* Attribute property read only */
#define __LWMI_TUNABLE_RO_CAP01(_prop, _attrname, _prop_type)                  \
	static ssize_t _attrname##_##_prop##_show(                             \
		struct kobject *kobj, struct kobj_attribute *kattr, char *buf) \
	{                                                                      \
		return attr_capdata01_show(kobj, kattr, buf, &_attrname,       \
					   _prop_type);                        \
	}                                                                      \
	static struct kobj_attribute attr_##_attrname##_##_prop =              \
		__LWMI_ATTR_RO(_attrname, _prop)

#define LWMI_ATTR_GROUP_TUNABLE_CAP01(_attrname, _fsname, _dispname)      \
	__LWMI_TUNABLE_CURRENT_VALUE_CAP01(_attrname);                    \
	__LWMI_TUNABLE_RO_CAP01(default_value, _attrname, DEFAULT_VAL);   \
	__LWMI_ATTR_SHOW_FMT(display_name, _attrname, "%s\n", _dispname); \
	__LWMI_TUNABLE_RO_CAP01(max_value, _attrname, MAX_VAL);           \
	__LWMI_TUNABLE_RO_CAP01(min_value, _attrname, MIN_VAL);           \
	__LWMI_TUNABLE_RO_CAP01(scalar_increment, _attrname, STEP_VAL);   \
	static struct kobj_attribute attr_##_attrname##_type =            \
		__LWMI_ATTR_RO_AS(type, int_type_show);                   \
	static struct attribute *_attrname##_attrs[] = {                  \
		&attr_##_attrname##_current_value.attr,                   \
		&attr_##_attrname##_default_value.attr,                   \
		&attr_##_attrname##_display_name.attr,                    \
		&attr_##_attrname##_max_value.attr,                       \
		&attr_##_attrname##_min_value.attr,                       \
		&attr_##_attrname##_scalar_increment.attr,                \
		&attr_##_attrname##_type.attr,                            \
		NULL,                                                     \
	};                                                                \
	static const struct attribute_group _attrname##_attr_group = {    \
		.name = _fsname, .attrs = _attrname##_attrs               \
	}

LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl1_spl, "ppt_pl1_spl",
			      "Set the CPU sustained power limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl2_sppt, "ppt_pl2_sppt",
			      "Set the CPU slow package power tracking limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl3_fppt, "ppt_pl3_fppt",
			      "Set the CPU fast package power tracking limit");

static struct capdata01_attr_group cd01_attr_groups[] = {
	{ &ppt_pl1_spl_attr_group, &ppt_pl1_spl },
	{ &ppt_pl2_sppt_attr_group, &ppt_pl2_sppt },
	{ &ppt_pl3_fppt_attr_group, &ppt_pl3_fppt },
	{},
};

/**
 * lwmi_om_fw_attr_add() - Register all firmware_attributes_class members
 * @priv: The Other Mode driver data.
 *
 * Return: Either 0, or an error code.
 */
static int lwmi_om_fw_attr_add(struct lwmi_om_priv *priv)
{
	unsigned int i;
	int err;

	priv->ida_id = ida_alloc(&lwmi_om_ida, GFP_KERNEL);
	if (priv->ida_id < 0)
		return priv->ida_id;

	priv->fw_attr_dev = device_create(&firmware_attributes_class, NULL,
					  MKDEV(0, 0), NULL, "%s-%u",
					  LWMI_OM_FW_ATTR_BASE_PATH,
					  priv->ida_id);
	if (IS_ERR(priv->fw_attr_dev)) {
		err = PTR_ERR(priv->fw_attr_dev);
		goto err_free_ida;
	}

	priv->fw_attr_kset = kset_create_and_add("attributes", NULL,
						 &priv->fw_attr_dev->kobj);
	if (!priv->fw_attr_kset) {
		err = -ENOMEM;
		goto err_destroy_classdev;
	}

	for (i = 0; i < ARRAY_SIZE(cd01_attr_groups) - 1; i++) {
		err = sysfs_create_group(&priv->fw_attr_kset->kobj,
					 cd01_attr_groups[i].attr_group);
		if (err)
			goto err_remove_groups;

		cd01_attr_groups[i].tunable_attr->dev = &priv->wdev->dev;
	}
	return 0;

err_remove_groups:
	while (i--)
		sysfs_remove_group(&priv->fw_attr_kset->kobj,
				   cd01_attr_groups[i].attr_group);

	kset_unregister(priv->fw_attr_kset);

err_destroy_classdev:
	device_unregister(priv->fw_attr_dev);

err_free_ida:
	ida_free(&lwmi_om_ida, priv->ida_id);
	return err;
}

/**
 * lwmi_om_fw_attr_remove() - Unregister all capability data attribute groups
 * @priv: the lenovo-wmi-other driver data.
 */
static void lwmi_om_fw_attr_remove(struct lwmi_om_priv *priv)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(cd01_attr_groups) - 1; i++)
		sysfs_remove_group(&priv->fw_attr_kset->kobj,
				   cd01_attr_groups[i].attr_group);

	kset_unregister(priv->fw_attr_kset);
	device_unregister(priv->fw_attr_dev);
}

/* ======== Self (master: lenovo-wmi-other) ======== */

/**
 * lwmi_om_master_bind() - Bind all components of the other mode driver
 * @dev: The lenovo-wmi-other driver basic device.
 *
 * Call component_bind_all to bind the lenovo-wmi-capdata devices to the
 * lenovo-wmi-other master driver, with a callback to collect fan info from
 * capdata_fan. On success, assign the capability data list pointers to the
 * driver data struct for later access. These pointers are only valid while the
 * capdata interfaces exist. Finally, collect fan info from capdata00 and
 * register all firmware attribute groups. Note that the HWMON device is
 * registered only if all fan info is collected. Hence, it is not registered
 * here. See lwmi_om_fan_info_collect_cd00() and
 * lwmi_om_fan_info_collect_cd_fan().
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_om_master_bind(struct device *dev)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(dev);
	struct lwmi_cd_binder binder = {
		.cd_fan_list_cb = lwmi_om_fan_info_collect_cd_fan,
	};
	int ret;

	lwmi_om_fan_info_init(priv);

	ret = component_bind_all(dev, &binder);
	if (ret)
		return ret;

	priv->cd00_list = binder.cd00_list;
	priv->cd01_list = binder.cd01_list;
	if (!priv->cd00_list || !priv->cd01_list)
		return -ENODEV;

	lwmi_om_fan_info_collect_cd00(priv);

	return lwmi_om_fw_attr_add(priv);
}

/**
 * lwmi_om_master_unbind() - Unbind all components of the other mode driver
 * @dev: The lenovo-wmi-other driver basic device
 *
 * Unregister all firmware attribute groups and the HWMON device. Then call
 * component_unbind_all to unbind lenovo-wmi-capdata devices from the
 * lenovo-wmi-other master driver.
 */
static void lwmi_om_master_unbind(struct device *dev)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(dev);

	lwmi_om_fw_attr_remove(priv);

	lwmi_om_hwmon_remove(priv);

	component_unbind_all(dev, NULL);
}

static const struct component_master_ops lwmi_om_master_ops = {
	.bind = lwmi_om_master_bind,
	.unbind = lwmi_om_master_unbind,
};

static int lwmi_other_probe(struct wmi_device *wdev, const void *context)
{
	struct component_match *master_match = NULL;
	struct lwmi_om_priv *priv;

	priv = devm_kzalloc(&wdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Sentinel for on-demand ida_free(). */
	priv->ida_id = -EIDRM;

	priv->wdev = wdev;
	dev_set_drvdata(&wdev->dev, priv);

	lwmi_cd_match_add_all(&wdev->dev, &master_match);
	if (IS_ERR(master_match))
		return PTR_ERR(master_match);

	return component_master_add_with_match(&wdev->dev, &lwmi_om_master_ops,
					       master_match);
}

static void lwmi_other_remove(struct wmi_device *wdev)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(&wdev->dev);

	component_master_del(&wdev->dev, &lwmi_om_master_ops);

	/* No IDA to free if the driver is never bound to its components. */
	if (priv->ida_id >= 0)
		ida_free(&lwmi_om_ida, priv->ida_id);
}

static const struct wmi_device_id lwmi_other_id_table[] = {
	{ LENOVO_OTHER_MODE_GUID, NULL },
	{}
};

static struct wmi_driver lwmi_other_driver = {
	.driver = {
		.name = "lenovo_wmi_other",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = lwmi_other_id_table,
	.probe = lwmi_other_probe,
	.remove = lwmi_other_remove,
	.no_singleton = true,
};

module_wmi_driver(lwmi_other_driver);

MODULE_IMPORT_NS("LENOVO_WMI_CAPDATA");
MODULE_IMPORT_NS("LENOVO_WMI_HELPERS");
MODULE_DEVICE_TABLE(wmi, lwmi_other_id_table);
MODULE_AUTHOR("Derek J. Clark <derekjohn.clark@gmail.com>");
MODULE_AUTHOR("Rong Zhang <i@rong.moe>");
MODULE_DESCRIPTION("Lenovo Other Mode WMI Driver");
MODULE_LICENSE("GPL");
