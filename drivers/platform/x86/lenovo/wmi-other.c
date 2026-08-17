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
#include <linux/platform_profile.h>
#include <linux/power_supply.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include <acpi/battery.h>

#include "wmi-capdata.h"
#include "wmi-events.h"
#include "wmi-helpers.h"
#include "../firmware_attributes_class.h"

#define LENOVO_OTHER_MODE_GUID "DC2A8805-3A8C-41BA-A6F7-092E0089CD3B"

enum lwmi_feature_id_cpu {
	LWMI_FEATURE_ID_CPU_SPPT =	0x01,
	LWMI_FEATURE_ID_CPU_SPL =	0x02,
	LWMI_FEATURE_ID_CPU_FPPT =	0x03,
	LWMI_FEATURE_ID_CPU_TEMP =	0x04,
	LWMI_FEATURE_ID_CPU_APU =	0x05,
	LWMI_FEATURE_ID_CPU_CL =	0x06,
	LWMI_FEATURE_ID_CPU_TAU =	0x07,
	LWMI_FEATURE_ID_CPU_IPL =	0x09,
};

enum lwmi_feature_id_gpu {
	LWMI_FEATURE_ID_GPU_NV_PPAB =		0x01,
	LWMI_FEATURE_ID_GPU_NV_CTGP =		0x02,
	LWMI_FEATURE_ID_GPU_TEMP =		0x03,
	LWMI_FEATURE_ID_GPU_AC_OFFSET =		0x04,
	LWMI_FEATURE_ID_DGPU_BOOST_CLK =	0x06,
	LWMI_FEATURE_ID_DGPU_EN =		0x07,
	LWMI_FEATURE_ID_GPU_MODE =		0x08,
	LWMI_FEATURE_ID_DGPU_DIDVID =		0x09,
	LWMI_FEATURE_ID_GPU_NV_BPL =		0x0a,
	LWMI_FEATURE_ID_GPU_NV_CPU_BOOST =	0x0b,
};

enum lwmi_feature_id_psu {
	LWMI_FEATURE_ID_PSU_CHARGE_TYPES =	0x01,
	LWMI_FEATURE_ID_PSU_CHARGE_BEHAVIOUR =	0x02,
};

#define LWMI_FEATURE_ID_FAN_RPM 0x03

#define LWMI_TYPE_ID_CROSSLOAD	0x01
#define LWMI_TYPE_ID_PSU_AC	0x01

#define LWMI_FEATURE_VALUE_GET 17
#define LWMI_FEATURE_VALUE_SET 18

#define LWMI_FAN_ID_BASE 1
#define LWMI_FAN_NR 4
#define LWMI_FAN_ID(x) ((x) + LWMI_FAN_ID_BASE)

#define LWMI_FAN_DIV 100

#define LWMI_CHARGE_BEHAVIOR_DISCHARGE	0x00
#define LWMI_CHARGE_BEHAVIOR_AUTO	0x01
#define LWMI_CHARGE_TYPE_STANDARD	0x00
#define LWMI_CHARGE_TYPE_LONGLIFE	0x01

#define LWMI_ATTR_ID_FAN_RPM(x)                                   \
	lwmi_attr_id(LWMI_DEVICE_ID_FAN, LWMI_FEATURE_ID_FAN_RPM, \
		     LWMI_GZ_THERMAL_MODE_NONE, LWMI_FAN_ID(x))

#define LWMI_ATTR_ID_PSU(feat, type)			\
	lwmi_attr_id(LWMI_DEVICE_ID_PSU, feat,		\
		     LWMI_GZ_THERMAL_MODE_NONE, type)

#define LWMI_OM_SYSFS_NAME "lenovo-wmi-other"
#define LWMI_OM_HWMON_NAME "lenovo_wmi_other"

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
	struct wmi_device *wdev;
	int ida_id;

	struct lwmi_fan_info fan_info[LWMI_FAN_NR];

	struct {
		bool capdata00_collected : 1;
		bool capdata_fan_collected : 1;
	} fan_flags;

	enum power_supply_charge_behaviour charge_behaviour;
	const struct power_supply_ext *battery_ext;
	struct acpi_battery_hook battery_hook;
	bool bh_registered;
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

/* Visibility of power supply extensions */
static bool force_load_psy_ext;
module_param(force_load_psy_ext, bool, 0444);
MODULE_PARM_DESC(force_load_psy_ext,
	"This option will skip checking if the ideapad_laptop driver will conflict "
	"with adding an extension to set the battery charge behavior and battery charge "
	"control end threshold. It will also skip checking if the BIOS reports that "
	"those features are fully supported. It is recommended to blacklist the ideapad "
	"driver before using this option.");

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
	struct wmi_method_args_32 args = {};
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
			else
				raw = val;

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

/* ======== Power Supply Extension (component: lenovo-wmi-capdata 00) ======== */

/**
 * lwmi_psy_ext_get_prop() - Get a power_supply_ext property
 * @ps: The battery that was extended
 * @ext: The extension
 * @ext_data: Pointer to the lwmi_om_priv drvdata
 * @prop: The property to read
 * @val: The value to return
 *
 * Reads the given value from the power_supply_ext property
 *
 * Return: 0 on success, or an error
 */
static int lwmi_psy_ext_get_prop(struct power_supply *ps,
				 const struct power_supply_ext *ext,
				 void *ext_data,
				 enum power_supply_property prop,
				 union power_supply_propval *val)
{
	struct lwmi_om_priv *priv = ext_data;
	struct wmi_method_args_32 args = {};
	u32 retval;
	int ret;

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		/* Reading from BIOS reads the wrong bit. Use cached value */
		val->intval = priv->charge_behaviour;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_TYPES:
		args.arg0 = LWMI_ATTR_ID_PSU(LWMI_FEATURE_ID_PSU_CHARGE_TYPES,
					     LWMI_TYPE_ID_PSU_AC);
		break;
	default:
		return -EINVAL;
	}

	ret = lwmi_dev_evaluate_int(priv->wdev, 0x0, LWMI_FEATURE_VALUE_GET,
				    (u8 *)&args, sizeof(args),
				    &retval);
	if (ret)
		return ret;

	dev_dbg(&priv->wdev->dev, "Got return value %#x for property %#x\n", retval, prop);

	switch (retval) {
	case LWMI_CHARGE_TYPE_LONGLIFE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_LONGLIFE;
		break;
	case LWMI_CHARGE_TYPE_STANDARD:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_STANDARD;
		break;
	default:
		dev_err(&priv->wdev->dev, "Got invalid charge types value: %#x\n", retval);
		return -EINVAL;
	}

	return 0;
}

/**
 * lwmi_psy_ext_set_prop() - Set a power_supply_ext property
 * @ps: The battery that was extended
 * @ext: The extension
 * @ext_data: Pointer to the lwmi_om_priv drvdata
 * @prop: The property to write
 * @val: The value to write
 *
 * Writes the given value to the power_supply_ext property
 *
 * Return: 0 on success, or an error
 */
static int lwmi_psy_ext_set_prop(struct power_supply *ps,
				 const struct power_supply_ext *ext,
				 void *ext_data,
				 enum power_supply_property prop,
				 const union power_supply_propval *val)
{
	struct lwmi_om_priv *priv = ext_data;
	struct wmi_method_args_32 args = {};

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		args.arg0 = LWMI_ATTR_ID_PSU(LWMI_FEATURE_ID_PSU_CHARGE_BEHAVIOUR,
					     LWMI_TYPE_ID_NONE);
		switch (val->intval) {
		case POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO:
			args.arg1 = LWMI_CHARGE_BEHAVIOR_AUTO;
			break;
		case POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE:
			args.arg1 = LWMI_CHARGE_BEHAVIOR_DISCHARGE;
			break;
		default:
			dev_err(&priv->wdev->dev, "Got invalid charge behavior value: %#x\n",
				val->intval);
			return -EINVAL;
		}
		priv->charge_behaviour = val->intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPES:
		args.arg0 = LWMI_ATTR_ID_PSU(LWMI_FEATURE_ID_PSU_CHARGE_TYPES,
					     LWMI_TYPE_ID_PSU_AC);
		switch (val->intval) {
		case POWER_SUPPLY_CHARGE_TYPE_LONGLIFE:
			args.arg1 = LWMI_CHARGE_TYPE_LONGLIFE;
			break;
		case POWER_SUPPLY_CHARGE_TYPE_STANDARD:
			args.arg1 = LWMI_CHARGE_TYPE_STANDARD;
			break;
		default:
			dev_err(&priv->wdev->dev, "Got invalid charge types value: %#x\n",
				val->intval);
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(&priv->wdev->dev, "Attempting to set %#010x for property %#x to %#x\n",
		args.arg0, prop, args.arg1);

	return lwmi_dev_evaluate_int(priv->wdev, 0x0, LWMI_FEATURE_VALUE_SET,
				     (u8 *)&args, sizeof(args), NULL);
}

/** lwmi_psy_prop_get_supported() - Gets the support level from capdata for a given property
 * @priv: Pointer to the lwmi_om_priv drvdata
 * @prop: The power supply property to be evaluated
 *
 * Return: bitmapped capability data support level
 */
static u32 lwmi_psy_prop_get_supported(struct lwmi_om_priv *priv, enum power_supply_property prop)
{
	struct capdata00 capdata;
	u32 attribute_id;
	int ret;

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		attribute_id = LWMI_ATTR_ID_PSU(LWMI_FEATURE_ID_PSU_CHARGE_BEHAVIOUR,
						LWMI_TYPE_ID_NONE);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPES:
		attribute_id = LWMI_ATTR_ID_PSU(LWMI_FEATURE_ID_PSU_CHARGE_TYPES,
						LWMI_TYPE_ID_PSU_AC);
		break;
	default:
		return 0;
	}

	ret = lwmi_cd00_get_data(priv->cd00_list, attribute_id, &capdata);
	if (ret)
		return 0;

	dev_dbg(&priv->wdev->dev, "Battery charge feature (%#010x) support level: %#x\n",
		attribute_id, capdata.supported);

	return capdata.supported;
}

/**
 * lwmi_psy_prop_is_supported() - Determine if the property is supported
 * @priv: Pointer to the lwmi_om_priv drvdata
 * @prop: The power supply property to be evaluated
 *
 * Checks capdata 00 to determine if the property is supported.
 *
 * Return: true if readable, or false
 */
static bool lwmi_psy_prop_is_supported(struct lwmi_om_priv *priv, enum power_supply_property prop)
{
	int ret;

	if (force_load_psy_ext)
		return true;

	ret = lwmi_psy_prop_get_supported(priv, prop);
	if (ret < 0)
		return false;

	return (ret & LWMI_SUPP_VALID) && (ret & LWMI_SUPP_GET);
}

/**
 * lwmi_psy_prop_is_writeable() - Determine if the property is writeable
 * @ps: The battery that was extended
 * @ext: The extension
 * @ext_data: Pointer the lwmi_om_priv drvdata
 * @prop: The property to check
 *
 * Checks capdata 00 to determine if the property is writable.
 *
 * Return: true if writable, or false
 */
static int lwmi_psy_prop_is_writeable(struct power_supply *ps,
				      const struct power_supply_ext *ext,
				      void *ext_data,
				      enum power_supply_property prop)
{
	struct lwmi_om_priv *priv = ext_data;
	int ret;

	if (force_load_psy_ext)
		return true;

	ret = lwmi_psy_prop_get_supported(priv, prop);
	if (ret < 0)
		return false;

	return !!(ret & LWMI_SUPP_SET);
}

#define DEFINE_LWMI_POWER_SUPPLY_EXTENSION(_name, _props, _behaviours, _types)	\
	static const struct power_supply_ext _name = {				\
		.name			= LWMI_OM_SYSFS_NAME,			\
		.properties		= _props,				\
		.num_properties		= ARRAY_SIZE(_props),			\
		.charge_behaviours	= _behaviours,				\
		.charge_types		= _types,				\
		.get_property		= lwmi_psy_ext_get_prop,		\
		.set_property		= lwmi_psy_ext_set_prop,		\
		.property_is_writeable	= lwmi_psy_prop_is_writeable,		\
	}

static const enum power_supply_property lwmi_psy_ext_props_all[] = {
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
	POWER_SUPPLY_PROP_CHARGE_TYPES,
};

static const enum power_supply_property lwmi_psy_ext_props_types[] = {
	POWER_SUPPLY_PROP_CHARGE_TYPES,
};

static const enum power_supply_property lwmi_psy_ext_props_behaviour[] = {
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
};

#define LWMI_CHARGE_BEHAVIOURS (BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) | \
				BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE))

#define LWMI_CHARGE_TYPES (BIT(POWER_SUPPLY_CHARGE_TYPE_STANDARD) | \
			   BIT(POWER_SUPPLY_CHARGE_TYPE_LONGLIFE))

DEFINE_LWMI_POWER_SUPPLY_EXTENSION(lwmi_psy_ext_all, lwmi_psy_ext_props_all,
				   LWMI_CHARGE_BEHAVIOURS, LWMI_CHARGE_TYPES);
DEFINE_LWMI_POWER_SUPPLY_EXTENSION(lwmi_psy_ext_types, lwmi_psy_ext_props_types,
				   0, LWMI_CHARGE_TYPES);
DEFINE_LWMI_POWER_SUPPLY_EXTENSION(lwmi_psy_ext_behaviour, lwmi_psy_ext_props_behaviour,
				   LWMI_CHARGE_BEHAVIOURS, 0);

#define LWMI_PSY_PROP_BEHAVIOUR BIT(0)
#define LWMI_PSY_PROP_TYPES BIT(1)

static const struct power_supply_ext *lwmi_psy_exts[] = {
	[LWMI_PSY_PROP_BEHAVIOUR] =				&lwmi_psy_ext_behaviour,
	[LWMI_PSY_PROP_TYPES] =					&lwmi_psy_ext_types,
	[LWMI_PSY_PROP_BEHAVIOUR | LWMI_PSY_PROP_TYPES] =	&lwmi_psy_ext_all,
};

/**
 * lwmi_add_battery() - Connect the power_supply_ext
 * @battery: The battery to extend
 * @hook: The driver hook used to extend the battery
 *
 * Return: 0 on success, or an error.
 */
static int lwmi_add_battery(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct lwmi_om_priv *priv = container_of(hook, struct lwmi_om_priv, battery_hook);

	return power_supply_register_extension(battery, priv->battery_ext, &priv->wdev->dev, priv);
}

/**
 * lwmi_remove_battery() - Disconnect the power_supply_ext
 * @battery: The battery that was extended
 * @hook: The driver hook used to extend the battery
 *
 * Return: 0 on success, or an error.
 */
static int lwmi_remove_battery(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct lwmi_om_priv *priv = container_of(hook, struct lwmi_om_priv, battery_hook);

	power_supply_unregister_extension(battery, priv->battery_ext);
	return 0;
}

/**
 * lwmi_acpi_match() - Attempts to return the ideapad acpi handle
 * @handle: The ACPI handle that manages battery charging
 * @lvl: Unused
 * @context: Void pointer to the acpi_handle object to return
 * @retval: Unused
 *
 * Checks if the ideapad_laptop driver is going to manage charge_type first,
 * then if not, hooks the battery to our WMI methods.
 *
 * Return: AE_CTRL_TERMINATE if found, AE_OK if not found.
 */
static acpi_status lwmi_acpi_match(acpi_handle handle, u32 lvl,
				   void *context, void **retval)
{
	acpi_handle *ahand = context;

	if (!handle)
		return AE_OK;

	*ahand = handle;

	return AE_CTRL_TERMINATE;
}

/**
 * lwmi_om_psy_ext_init() - Hooks power supply extension to device battery
 * @priv: Pointer to the lwmi_om_priv drvdata.
 *
 * Checks if the ideapad_laptop driver is going to manage charge attributes first,
 * then if not, hooks the battery to our WMI methods if they are supported.
 */
static void lwmi_om_psy_ext_init(struct lwmi_om_priv *priv)
{
	static const char * const ideapad_hid = "VPC2004";
	acpi_handle handle = NULL;
	unsigned int props = 0;
	int ret;

	if (lwmi_psy_prop_is_supported(priv, POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR))
		props |= LWMI_PSY_PROP_BEHAVIOUR;
	if (lwmi_psy_prop_is_supported(priv, POWER_SUPPLY_PROP_CHARGE_TYPES))
		props |= LWMI_PSY_PROP_TYPES;
	if (!props)
		return;
	if (force_load_psy_ext)
		goto load_psy_ext;

	/* Deconflict ideapad_laptop driver */
	ret = acpi_get_devices(ideapad_hid, lwmi_acpi_match, &handle, NULL);
	if (ret)
		return;

	if (handle && acpi_has_method(handle, "GBMD") && acpi_has_method(handle, "SBMC")) {
		dev_dbg(&priv->wdev->dev, "ideapad_laptop driver manages battery for device\n");
		return;
	}

load_psy_ext:
	/* Add battery hooks */
	priv->battery_ext = lwmi_psy_exts[props];
	priv->battery_hook.add_battery = lwmi_add_battery;
	priv->battery_hook.remove_battery = lwmi_remove_battery;
	priv->battery_hook.name = "Lenovo WMI Other Battery Extension";
	priv->bh_registered = true;

	battery_hook_register(&priv->battery_hook);
}

/**
 * lwmi_om_psy_remove() - Unregister battery hook
 * @priv: Driver private data
 *
 * Unregisters the battery hook if applicable.
 */
static void lwmi_om_psy_remove(struct lwmi_om_priv *priv)
{
	if (!priv->bh_registered)
		return;

	battery_hook_unregister(&priv->battery_hook);
	priv->bh_registered = false;
}

/* ======== fw_attributes (component: lenovo-wmi-capdata 01) ======== */

struct tunable_attr_01 {
	struct device *dev;
	u8 feature_id;
	u8 device_id;
	u8 type_id;
	u8 cd_mode_id; /* mode arg for searching capdata */
	u8 cv_mode_id; /* mode arg for set/get current_value */
};

/**
 * tunable_attr_01_id() - Formats a tunable_attr_01 to a capdata attribute ID
 * @attr: The tunable_attr_01 to format.
 * @mode: The u8 corresponding to the wmi-gamezone mode for set/get.
 *
 * Return: encoded capability data attribute ID.
 */
static u32 tunable_attr_01_id(struct tunable_attr_01 *attr, u8 mode)
{
	return lwmi_attr_id(attr->device_id, attr->feature_id, mode, attr->type_id);
}

static struct tunable_attr_01 ppt_pl1_spl = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_SPL,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 ppt_pl1_spl_cl = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_SPL,
	.type_id = LWMI_TYPE_ID_CROSSLOAD,
};

static struct tunable_attr_01 ppt_pl2_sppt = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_SPPT,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 ppt_pl2_sppt_cl = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_SPPT,
	.type_id = LWMI_TYPE_ID_CROSSLOAD,
};

static struct tunable_attr_01 ppt_pl3_fppt = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_FPPT,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 ppt_pl3_fppt_cl = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_FPPT,
	.type_id = LWMI_TYPE_ID_CROSSLOAD,
};

static struct tunable_attr_01 cpu_temp = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_TEMP,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 ppt_pl1_apu_spl = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_APU,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 ppt_cpu_cl = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_CL,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 ppt_pl1_tau = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_TAU,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 ppt_pl4_ipl = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_IPL,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 ppt_pl4_ipl_cl = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_IPL,
	.type_id = LWMI_TYPE_ID_CROSSLOAD,
};

static struct tunable_attr_01 gpu_nv_ppab = {
	.device_id = LWMI_DEVICE_ID_GPU,
	.feature_id = LWMI_FEATURE_ID_GPU_NV_PPAB,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 gpu_nv_ctgp = {
	.device_id = LWMI_DEVICE_ID_GPU,
	.feature_id = LWMI_FEATURE_ID_GPU_NV_CTGP,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 gpu_temp = {
	.device_id = LWMI_DEVICE_ID_GPU,
	.feature_id = LWMI_FEATURE_ID_GPU_TEMP,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 gpu_nv_ac_offset = {
	.device_id = LWMI_DEVICE_ID_GPU,
	.feature_id = LWMI_FEATURE_ID_GPU_AC_OFFSET,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 dgpu_boost_clk = {
	.device_id = LWMI_DEVICE_ID_GPU,
	.feature_id = LWMI_FEATURE_ID_DGPU_BOOST_CLK,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 dgpu_enable = {
	.device_id = LWMI_DEVICE_ID_GPU,
	.feature_id = LWMI_FEATURE_ID_DGPU_EN,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 gpu_mode = {
	.device_id = LWMI_DEVICE_ID_GPU,
	.feature_id = LWMI_FEATURE_ID_GPU_MODE,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 dgpu_didvid = {
	.device_id = LWMI_DEVICE_ID_GPU,
	.feature_id = LWMI_FEATURE_ID_DGPU_DIDVID,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 gpu_nv_bpl = {
	.device_id = LWMI_DEVICE_ID_GPU,
	.feature_id = LWMI_FEATURE_ID_GPU_NV_BPL,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 gpu_nv_cpu_boost = {
	.device_id = LWMI_DEVICE_ID_GPU,
	.feature_id = LWMI_FEATURE_ID_GPU_NV_CPU_BOOST,
	.type_id = LWMI_TYPE_ID_NONE,
};

struct capdata01_attr_group {
	const struct attribute_group *attr_group;
	struct tunable_attr_01 *tunable_attr;
};

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

	attribute_id = tunable_attr_01_id(tunable_attr, tunable_attr->cd_mode_id);

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
	struct wmi_method_args_32 args = {};
	struct capdata01 capdata;
	enum thermal_mode mode;
	u32 value;
	int ret;

	ret = lwmi_tm_notifier_call(&mode);
	if (ret)
		return ret;

	if (mode != LWMI_GZ_THERMAL_MODE_CUSTOM)
		return -EBUSY;

	args.arg0 = tunable_attr_01_id(tunable_attr, tunable_attr->cd_mode_id);

	ret = lwmi_cd01_get_data(priv->cd01_list, args.arg0, &capdata);
	if (ret)
		return ret;

	ret = kstrtouint(buf, 10, &value);
	if (ret)
		return ret;

	if (value < capdata.min_value || value > capdata.max_value)
		return -EINVAL;

	args.arg0 = tunable_attr_01_id(tunable_attr, tunable_attr->cv_mode_id);
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
	struct wmi_method_args_32 args = {};
	enum thermal_mode mode;
	int retval;
	int ret;

	ret = lwmi_tm_notifier_call(&mode);
	if (ret)
		return ret;

	/* If "no-mode" is the supported mode, ensure we never send current mode */
	if (tunable_attr->cv_mode_id == LWMI_GZ_THERMAL_MODE_NONE)
		mode = tunable_attr->cv_mode_id;

	args.arg0 = tunable_attr_01_id(tunable_attr, mode);

	ret = lwmi_dev_evaluate_int(priv->wdev, 0x0, LWMI_FEATURE_VALUE_GET,
				    (unsigned char *)&args, sizeof(args),
				    &retval);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", retval);
}

/**
 * lwmi_attr_01_is_supported() - Determine if the given attribute is supported.
 * @tunable_attr: The attribute to verify.
 *
 * For an attribute to be supported it must have a functional get/set method,
 * as well as associated capability data stored in the capdata01 table.
 *
 * First check if the attribute has a corresponding data table under custom mode
 * (0xff), then under no mode (0x00). If either of those passes, check if the
 * supported field of the capdata struct is > 0. If it is supported, store the
 * successful mode in the cd_mode_id field of tunable_attr.
 *
 * If the attribute capdata shows it is supported, attempt to determine the mode
 * for the current value property get/set methods using a similar pattern to the
 * capdata table check. If the value returned by either mode is 0 or an error,
 * assume that mode is not supported. Otherwise, store the successful mode in the
 * cv_mode_id field of tunable_attr.
 *
 * If any of the above checks fail then the attribute is not fully supported.
 *
 * Return: true if capdata and set/get modes are found, otherwise false.
 */
static bool lwmi_attr_01_is_supported(struct tunable_attr_01 *tunable_attr)
{
	u8 modes[2] = { LWMI_GZ_THERMAL_MODE_CUSTOM, LWMI_GZ_THERMAL_MODE_NONE };
	struct lwmi_om_priv *priv = dev_get_drvdata(tunable_attr->dev);
	struct wmi_method_args_32 args = {};
	bool cd_mode_found = false;
	bool cv_mode_found = false;
	struct capdata01 capdata;
	int retval, ret, i;

	/* Determine tunable_attr->cd_mode_id */
	for (i = 0; i < ARRAY_SIZE(modes); i++) {
		args.arg0 = tunable_attr_01_id(tunable_attr, modes[i]);

		ret = lwmi_cd01_get_data(priv->cd01_list, args.arg0, &capdata);
		if (ret || !capdata.supported)
			continue;

		tunable_attr->cd_mode_id = modes[i];
		cd_mode_found = true;
		break;
	}

	if (!cd_mode_found)
		return cd_mode_found;

	dev_dbg(tunable_attr->dev,
		"cd_mode_id: %#010x\n", args.arg0);

	/* Determine tunable_attr->cv_mode_id, returns 1 if supported */
	for (i = 0; i < ARRAY_SIZE(modes); i++) {
		args.arg0 = tunable_attr_01_id(tunable_attr, modes[i]);

		ret = lwmi_dev_evaluate_int(priv->wdev, 0x0, LWMI_FEATURE_VALUE_GET,
					    (u8 *)&args, sizeof(args),
					    &retval);
		if (ret || !retval)
			continue;

		tunable_attr->cv_mode_id = modes[i];
		cv_mode_found = true;
		break;
	}

	if (!cv_mode_found)
		return cv_mode_found;

	dev_dbg(tunable_attr->dev, "cv_mode_id: %#010x, attribute support level: %#010x\n",
		args.arg0, capdata.supported);

	return capdata.supported > 0;
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

/* CPU tunable attributes */
LWMI_ATTR_GROUP_TUNABLE_CAP01(cpu_temp, "cpu_temp",
			      "Set the CPU thermal load limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_cpu_cl, "ppt_cpu_cl",
			      "Set the CPU cross loading power limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl1_apu_spl, "ppt_pl1_apu_spl",
			      "Set the APU sustained power limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl1_spl, "ppt_pl1_spl",
			      "Set the CPU sustained power limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl1_spl_cl, "ppt_pl1_spl_cl",
			      "Set the CPU cross loading sustained power limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl2_sppt, "ppt_pl2_sppt",
			      "Set the CPU slow package power tracking limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl2_sppt_cl, "ppt_pl2_sppt_cl",
			      "Set the CPU cross loading slow package power tracking limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl3_fppt, "ppt_pl3_fppt",
			      "Set the CPU fast package power tracking limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl3_fppt_cl, "ppt_pl3_fppt_cl",
			      "Set the CPU cross loading fast package power tracking limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl1_tau, "ppt_pl1_tau",
			      "Set the CPU sustained power limit exceed duration");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl4_ipl, "ppt_pl4_ipl",
			      "Set the CPU instantaneous power limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl4_ipl_cl, "ppt_pl4_ipl_cl",
			      "Set the CPU cross loading instantaneous power limit");

/* GPU tunable attributes */
LWMI_ATTR_GROUP_TUNABLE_CAP01(dgpu_boost_clk, "dgpu_boost_clk",
			      "Set the dedicated GPU boost clock");
LWMI_ATTR_GROUP_TUNABLE_CAP01(dgpu_didvid, "dgpu_didvid",
			      "Get the GPU device identifier and vendor identifier");
LWMI_ATTR_GROUP_TUNABLE_CAP01(dgpu_enable, "dgpu_enable",
			      "Set the dedicated Nvidia GPU enabled status");
LWMI_ATTR_GROUP_TUNABLE_CAP01(gpu_mode, "gpu_mode",
			      "Set the GPU mode by power limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(gpu_nv_ac_offset, "gpu_nv_ac_offset",
			      "Set the Nvidia GPU AC total processing power baseline offset");
LWMI_ATTR_GROUP_TUNABLE_CAP01(gpu_nv_bpl, "gpu_nv_bpl",
			      "Set the Nvidia GPU base power limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(gpu_nv_cpu_boost, "gpu_nv_cpu_boost",
			      "Set the Nvidia GPU to CPU dynamic boost limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(gpu_nv_ctgp, "gpu_nv_ctgp",
			      "Set the GPU configurable total graphics power");
LWMI_ATTR_GROUP_TUNABLE_CAP01(gpu_nv_ppab, "gpu_nv_ppab",
			      "Set the Nvidia GPU power performance aware boost limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(gpu_temp, "gpu_temp",
			      "Set the GPU thermal load limit");

static struct capdata01_attr_group cd01_attr_groups[] = {
	{ &cpu_temp_attr_group, &cpu_temp },
	{ &dgpu_boost_clk_attr_group, &dgpu_boost_clk },
	{ &dgpu_didvid_attr_group, &dgpu_didvid },
	{ &dgpu_enable_attr_group, &dgpu_enable },
	{ &gpu_mode_attr_group, &gpu_mode },
	{ &gpu_nv_ac_offset_attr_group, &gpu_nv_ac_offset },
	{ &gpu_nv_bpl_attr_group, &gpu_nv_bpl },
	{ &gpu_nv_cpu_boost_attr_group, &gpu_nv_cpu_boost },
	{ &gpu_nv_ctgp_attr_group, &gpu_nv_ctgp },
	{ &gpu_nv_ppab_attr_group, &gpu_nv_ppab },
	{ &gpu_temp_attr_group, &gpu_temp },
	{ &ppt_cpu_cl_attr_group, &ppt_cpu_cl },
	{ &ppt_pl1_apu_spl_attr_group, &ppt_pl1_apu_spl },
	{ &ppt_pl1_spl_attr_group, &ppt_pl1_spl },
	{ &ppt_pl1_spl_cl_attr_group, &ppt_pl1_spl_cl },
	{ &ppt_pl1_tau_attr_group, &ppt_pl1_tau },
	{ &ppt_pl2_sppt_attr_group, &ppt_pl2_sppt },
	{ &ppt_pl2_sppt_cl_attr_group, &ppt_pl2_sppt_cl },
	{ &ppt_pl3_fppt_attr_group, &ppt_pl3_fppt },
	{ &ppt_pl3_fppt_cl_attr_group, &ppt_pl3_fppt_cl },
	{ &ppt_pl4_ipl_attr_group, &ppt_pl4_ipl },
	{ &ppt_pl4_ipl_cl_attr_group, &ppt_pl4_ipl_cl },
	{},
};

/**
 * lwmi_om_fw_attr_add() - Register all firmware_attributes_class members
 * @priv: The Other Mode driver data.
 */
static void lwmi_om_fw_attr_add(struct lwmi_om_priv *priv)
{
	unsigned int i;
	int err;

	err = ida_alloc(&lwmi_om_ida, GFP_KERNEL);
	if (err < 0)
		goto err_no_ida;

	priv->ida_id = err;

	priv->fw_attr_dev = device_create(&firmware_attributes_class, NULL,
					  MKDEV(0, 0), NULL, "%s-%u",
					  LWMI_OM_SYSFS_NAME, priv->ida_id);
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
		cd01_attr_groups[i].tunable_attr->dev = &priv->wdev->dev;
		if (!lwmi_attr_01_is_supported(cd01_attr_groups[i].tunable_attr))
			continue;

		err = sysfs_create_group(&priv->fw_attr_kset->kobj,
					 cd01_attr_groups[i].attr_group);
		if (err)
			goto err_remove_groups;
	}
	return;

err_remove_groups:
	while (i--)
		sysfs_remove_group(&priv->fw_attr_kset->kobj,
				   cd01_attr_groups[i].attr_group);

	kset_unregister(priv->fw_attr_kset);

err_destroy_classdev:
	device_unregister(priv->fw_attr_dev);

err_free_ida:
	ida_free(&lwmi_om_ida, priv->ida_id);

err_no_ida:
	priv->ida_id = -EIDRM;

	dev_warn(&priv->wdev->dev,
		 "failed to register firmware-attributes device: %d\n", err);
}

/**
 * lwmi_om_fw_attr_remove() - Unregister all capability data attribute groups
 * @priv: the lenovo-wmi-other driver data.
 */
static void lwmi_om_fw_attr_remove(struct lwmi_om_priv *priv)
{
	if (priv->ida_id < 0)
		return;

	for (unsigned int i = 0; i < ARRAY_SIZE(cd01_attr_groups) - 1; i++)
		sysfs_remove_group(&priv->fw_attr_kset->kobj,
				   cd01_attr_groups[i].attr_group);

	kset_unregister(priv->fw_attr_kset);
	device_unregister(priv->fw_attr_dev);
	ida_free(&lwmi_om_ida, priv->ida_id);
	priv->ida_id = -EIDRM;
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
	if (!priv->cd00_list || !priv->cd01_list) {
		component_unbind_all(dev, NULL);

		return -ENODEV;
	}

	lwmi_om_fan_info_collect_cd00(priv);
	lwmi_om_psy_ext_init(priv);

	lwmi_om_fw_attr_add(priv);

	return 0;
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

	lwmi_om_psy_remove(priv);

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
	component_master_del(&wdev->dev, &lwmi_om_master_ops);
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
