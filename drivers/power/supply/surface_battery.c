// SPDX-License-Identifier: GPL-2.0+
/*
 * Battery driver for 7th-generation Microsoft Surface devices via Surface
 * System Aggregator Module (SSAM).
 *
 * Copyright (C) 2019-2021 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <asm/unaligned.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <linux/surface_aggregator/device.h>


/* -- SAM interface. -------------------------------------------------------- */

enum sam_event_cid_bat {
	SAM_EVENT_CID_BAT_BIX         = 0x15,
	SAM_EVENT_CID_BAT_BST         = 0x16,
	SAM_EVENT_CID_BAT_ADP         = 0x17,
	SAM_EVENT_CID_BAT_PROT        = 0x18,
	SAM_EVENT_CID_BAT_DPTF        = 0x53,
};

enum sam_battery_sta {
	SAM_BATTERY_STA_OK            = 0x0f,
	SAM_BATTERY_STA_PRESENT	      = 0x10,
};

enum sam_battery_state {
	SAM_BATTERY_STATE_DISCHARGING = BIT(0),
	SAM_BATTERY_STATE_CHARGING    = BIT(1),
	SAM_BATTERY_STATE_CRITICAL    = BIT(2),
};

enum sam_battery_power_unit {
	SAM_BATTERY_POWER_UNIT_mW     = 0,
	SAM_BATTERY_POWER_UNIT_mA     = 1,
};

/* Equivalent to data returned in ACPI _BIX method, revision 0. */
struct spwr_bix {
	u8  revision;
	__le32 power_unit;
	__le32 design_cap;
	__le32 last_full_charge_cap;
	__le32 technology;
	__le32 design_voltage;
	__le32 design_cap_warn;
	__le32 design_cap_low;
	__le32 cycle_count;
	__le32 measurement_accuracy;
	__le32 max_sampling_time;
	__le32 min_sampling_time;
	__le32 max_avg_interval;
	__le32 min_avg_interval;
	__le32 bat_cap_granularity_1;
	__le32 bat_cap_granularity_2;
	__u8 model[21];
	__u8 serial[11];
	__u8 type[5];
	__u8 oem_info[21];
} __packed;

static_assert(sizeof(struct spwr_bix) == 119);

/* Equivalent to data returned in ACPI _BST method. */
struct spwr_bst {
	__le32 state;
	__le32 present_rate;
	__le32 remaining_cap;
	__le32 present_voltage;
} __packed;

static_assert(sizeof(struct spwr_bst) == 16);

#define SPWR_BIX_REVISION		0
#define SPWR_BATTERY_VALUE_UNKNOWN	0xffffffff

/* Get battery status (_STA) */
SSAM_DEFINE_SYNC_REQUEST_CL_R(ssam_bat_get_sta, __le32, {
	.target_category = SSAM_SSH_TC_BAT,
	.command_id      = 0x01,
});

/* Get battery static information (_BIX). */
SSAM_DEFINE_SYNC_REQUEST_CL_R(ssam_bat_get_bix, struct spwr_bix, {
	.target_category = SSAM_SSH_TC_BAT,
	.command_id      = 0x02,
});

/* Get battery dynamic information (_BST). */
SSAM_DEFINE_SYNC_REQUEST_CL_R(ssam_bat_get_bst, struct spwr_bst, {
	.target_category = SSAM_SSH_TC_BAT,
	.command_id      = 0x03,
});

/* Set battery trip point (_BTP). */
SSAM_DEFINE_SYNC_REQUEST_CL_W(ssam_bat_set_btp, __le32, {
	.target_category = SSAM_SSH_TC_BAT,
	.command_id      = 0x04,
});


/* -- Device structures. ---------------------------------------------------- */

struct spwr_psy_properties {
	const char *name;
	struct ssam_event_registry registry;
};

struct spwr_battery_device {
	struct ssam_device *sdev;

	char name[32];
	struct power_supply *psy;
	struct power_supply_desc psy_desc;

	struct delayed_work update_work;

	struct ssam_event_notifier notif;

	struct mutex lock;  /* Guards access to state data below. */
	unsigned long timestamp;

	__le32 sta;
	struct spwr_bix bix;
	struct spwr_bst bst;
	u32 alarm;
};


/* -- Module parameters. ---------------------------------------------------- */

static unsigned int cache_time = 1000;
module_param(cache_time, uint, 0644);
MODULE_PARM_DESC(cache_time, "battery state caching time in milliseconds [default: 1000]");


/* -- State management. ----------------------------------------------------- */

/*
 * Delay for battery update quirk. See spwr_external_power_changed() below
 * for more details.
 */
#define SPWR_AC_BAT_UPDATE_DELAY	msecs_to_jiffies(5000)

static bool spwr_battery_present(struct spwr_battery_device *bat)
{
	lockdep_assert_held(&bat->lock);

	return le32_to_cpu(bat->sta) & SAM_BATTERY_STA_PRESENT;
}

static int spwr_battery_load_sta(struct spwr_battery_device *bat)
{
	lockdep_assert_held(&bat->lock);

	return ssam_retry(ssam_bat_get_sta, bat->sdev, &bat->sta);
}

static int spwr_battery_load_bix(struct spwr_battery_device *bat)
{
	int status;

	lockdep_assert_held(&bat->lock);

	if (!spwr_battery_present(bat))
		return 0;

	status = ssam_retry(ssam_bat_get_bix, bat->sdev, &bat->bix);

	/* Enforce NULL terminated strings in case anything goes wrong... */
	bat->bix.model[ARRAY_SIZE(bat->bix.model) - 1] = 0;
	bat->bix.serial[ARRAY_SIZE(bat->bix.serial) - 1] = 0;
	bat->bix.type[ARRAY_SIZE(bat->bix.type) - 1] = 0;
	bat->bix.oem_info[ARRAY_SIZE(bat->bix.oem_info) - 1] = 0;

	return status;
}

static int spwr_battery_load_bst(struct spwr_battery_device *bat)
{
	lockdep_assert_held(&bat->lock);

	if (!spwr_battery_present(bat))
		return 0;

	return ssam_retry(ssam_bat_get_bst, bat->sdev, &bat->bst);
}

static int spwr_battery_set_alarm_unlocked(struct spwr_battery_device *bat, u32 value)
{
	__le32 value_le = cpu_to_le32(value);

	lockdep_assert_held(&bat->lock);

	bat->alarm = value;
	return ssam_retry(ssam_bat_set_btp, bat->sdev, &value_le);
}

static int spwr_battery_update_bst_unlocked(struct spwr_battery_device *bat, bool cached)
{
	unsigned long cache_deadline = bat->timestamp + msecs_to_jiffies(cache_time);
	int status;

	lockdep_assert_held(&bat->lock);

	if (cached && bat->timestamp && time_is_after_jiffies(cache_deadline))
		return 0;

	status = spwr_battery_load_sta(bat);
	if (status)
		return status;

	status = spwr_battery_load_bst(bat);
	if (status)
		return status;

	bat->timestamp = jiffies;
	return 0;
}

static int spwr_battery_update_bst(struct spwr_battery_device *bat, bool cached)
{
	int status;

	mutex_lock(&bat->lock);
	status = spwr_battery_update_bst_unlocked(bat, cached);
	mutex_unlock(&bat->lock);

	return status;
}

static int spwr_battery_update_bix_unlocked(struct spwr_battery_device *bat)
{
	int status;

	lockdep_assert_held(&bat->lock);

	status = spwr_battery_load_sta(bat);
	if (status)
		return status;

	status = spwr_battery_load_bix(bat);
	if (status)
		return status;

	status = spwr_battery_load_bst(bat);
	if (status)
		return status;

	if (bat->bix.revision != SPWR_BIX_REVISION)
		dev_warn(&bat->sdev->dev, "unsupported battery revision: %u\n", bat->bix.revision);

	bat->timestamp = jiffies;
	return 0;
}

static u32 sprw_battery_get_full_cap_safe(struct spwr_battery_device *bat)
{
	u32 full_cap = get_unaligned_le32(&bat->bix.last_full_charge_cap);

	lockdep_assert_held(&bat->lock);

	if (full_cap == 0 || full_cap == SPWR_BATTERY_VALUE_UNKNOWN)
		full_cap = get_unaligned_le32(&bat->bix.design_cap);

	return full_cap;
}

static bool spwr_battery_is_full(struct spwr_battery_device *bat)
{
	u32 state = get_unaligned_le32(&bat->bst.state);
	u32 full_cap = sprw_battery_get_full_cap_safe(bat);
	u32 remaining_cap = get_unaligned_le32(&bat->bst.remaining_cap);

	lockdep_assert_held(&bat->lock);

	return full_cap != SPWR_BATTERY_VALUE_UNKNOWN && full_cap != 0 &&
		remaining_cap != SPWR_BATTERY_VALUE_UNKNOWN &&
		remaining_cap >= full_cap &&
		state == 0;
}

static int spwr_battery_recheck_full(struct spwr_battery_device *bat)
{
	bool present;
	u32 unit;
	int status;

	mutex_lock(&bat->lock);
	unit = get_unaligned_le32(&bat->bix.power_unit);
	present = spwr_battery_present(bat);

	status = spwr_battery_update_bix_unlocked(bat);
	if (status)
		goto out;

	/* If battery has been attached, (re-)initialize alarm. */
	if (!present && spwr_battery_present(bat)) {
		u32 cap_warn = get_unaligned_le32(&bat->bix.design_cap_warn);

		status = spwr_battery_set_alarm_unlocked(bat, cap_warn);
		if (status)
			goto out;
	}

	/*
	 * Warn if the unit has changed. This is something we genuinely don't
	 * expect to happen, so make this a big warning. If it does, we'll
	 * need to add support for it.
	 */
	WARN_ON(unit != get_unaligned_le32(&bat->bix.power_unit));

out:
	mutex_unlock(&bat->lock);

	if (!status)
		power_supply_changed(bat->psy);

	return status;
}

static int spwr_battery_recheck_status(struct spwr_battery_device *bat)
{
	int status;

	status = spwr_battery_update_bst(bat, false);
	if (!status)
		power_supply_changed(bat->psy);

	return status;
}

static u32 spwr_notify_bat(struct ssam_event_notifier *nf, const struct ssam_event *event)
{
	struct spwr_battery_device *bat = container_of(nf, struct spwr_battery_device, notif);
	int status;

	dev_dbg(&bat->sdev->dev, "power event (cid = %#04x, iid = %#04x, tid = %#04x)\n",
		event->command_id, event->instance_id, event->target_id);

	switch (event->command_id) {
	case SAM_EVENT_CID_BAT_BIX:
		status = spwr_battery_recheck_full(bat);
		break;

	case SAM_EVENT_CID_BAT_BST:
		status = spwr_battery_recheck_status(bat);
		break;

	case SAM_EVENT_CID_BAT_PROT:
		/*
		 * TODO: Implement support for battery protection status change
		 *       event.
		 */
		status = 0;
		break;

	case SAM_EVENT_CID_BAT_DPTF:
		/*
		 * TODO: Implement support for DPTF event.
		 */
		status = 0;
		break;

	default:
		return 0;
	}

	return ssam_notifier_from_errno(status) | SSAM_NOTIF_HANDLED;
}

static void spwr_battery_update_bst_workfn(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct spwr_battery_device *bat;
	int status;

	bat = container_of(dwork, struct spwr_battery_device, update_work);

	status = spwr_battery_update_bst(bat, false);
	if (status) {
		dev_err(&bat->sdev->dev, "failed to update battery state: %d\n", status);
		return;
	}

	power_supply_changed(bat->psy);
}

static void spwr_external_power_changed(struct power_supply *psy)
{
	struct spwr_battery_device *bat = power_supply_get_drvdata(psy);

	/*
	 * Handle battery update quirk: When the battery is fully charged (or
	 * charged up to the limit imposed by the UEFI battery limit) and the
	 * adapter is plugged in or removed, the EC does not send a separate
	 * event for the state (charging/discharging) change. Furthermore it
	 * may take some time until the state is updated on the battery.
	 * Schedule an update to solve this.
	 */

	schedule_delayed_work(&bat->update_work, SPWR_AC_BAT_UPDATE_DELAY);
}


/* -- Properties. ----------------------------------------------------------- */

static const enum power_supply_property spwr_battery_props_chg[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

static const enum power_supply_property spwr_battery_props_eng[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

static int spwr_battery_prop_status(struct spwr_battery_device *bat)
{
	u32 state = get_unaligned_le32(&bat->bst.state);
	u32 present_rate = get_unaligned_le32(&bat->bst.present_rate);

	lockdep_assert_held(&bat->lock);

	if (state & SAM_BATTERY_STATE_DISCHARGING)
		return POWER_SUPPLY_STATUS_DISCHARGING;

	if (state & SAM_BATTERY_STATE_CHARGING)
		return POWER_SUPPLY_STATUS_CHARGING;

	if (spwr_battery_is_full(bat))
		return POWER_SUPPLY_STATUS_FULL;

	if (present_rate == 0)
		return POWER_SUPPLY_STATUS_NOT_CHARGING;

	return POWER_SUPPLY_STATUS_UNKNOWN;
}

static int spwr_battery_prop_technology(struct spwr_battery_device *bat)
{
	lockdep_assert_held(&bat->lock);

	if (!strcasecmp("NiCd", bat->bix.type))
		return POWER_SUPPLY_TECHNOLOGY_NiCd;

	if (!strcasecmp("NiMH", bat->bix.type))
		return POWER_SUPPLY_TECHNOLOGY_NiMH;

	if (!strcasecmp("LION", bat->bix.type))
		return POWER_SUPPLY_TECHNOLOGY_LION;

	if (!strncasecmp("LI-ION", bat->bix.type, 6))
		return POWER_SUPPLY_TECHNOLOGY_LION;

	if (!strcasecmp("LiP", bat->bix.type))
		return POWER_SUPPLY_TECHNOLOGY_LIPO;

	return POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
}

static int spwr_battery_prop_capacity(struct spwr_battery_device *bat)
{
	u32 full_cap = sprw_battery_get_full_cap_safe(bat);
	u32 remaining_cap = get_unaligned_le32(&bat->bst.remaining_cap);

	lockdep_assert_held(&bat->lock);

	if (full_cap == 0 || full_cap == SPWR_BATTERY_VALUE_UNKNOWN)
		return -ENODATA;

	if (remaining_cap == SPWR_BATTERY_VALUE_UNKNOWN)
		return -ENODATA;

	return remaining_cap * 100 / full_cap;
}

static int spwr_battery_prop_capacity_level(struct spwr_battery_device *bat)
{
	u32 state = get_unaligned_le32(&bat->bst.state);
	u32 remaining_cap = get_unaligned_le32(&bat->bst.remaining_cap);

	lockdep_assert_held(&bat->lock);

	if (state & SAM_BATTERY_STATE_CRITICAL)
		return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;

	if (spwr_battery_is_full(bat))
		return POWER_SUPPLY_CAPACITY_LEVEL_FULL;

	if (remaining_cap <= bat->alarm)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;

	return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
}

static int spwr_battery_get_property(struct power_supply *psy, enum power_supply_property psp,
				     union power_supply_propval *val)
{
	struct spwr_battery_device *bat = power_supply_get_drvdata(psy);
	u32 value;
	int status;

	mutex_lock(&bat->lock);

	status = spwr_battery_update_bst_unlocked(bat, true);
	if (status)
		goto out;

	/* Abort if battery is not present. */
	if (!spwr_battery_present(bat) && psp != POWER_SUPPLY_PROP_PRESENT) {
		status = -ENODEV;
		goto out;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = spwr_battery_prop_status(bat);
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = spwr_battery_present(bat);
		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = spwr_battery_prop_technology(bat);
		break;

	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		value = get_unaligned_le32(&bat->bix.cycle_count);
		if (value != SPWR_BATTERY_VALUE_UNKNOWN)
			val->intval = value;
		else
			status = -ENODATA;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		value = get_unaligned_le32(&bat->bix.design_voltage);
		if (value != SPWR_BATTERY_VALUE_UNKNOWN)
			val->intval = value * 1000;
		else
			status = -ENODATA;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		value = get_unaligned_le32(&bat->bst.present_voltage);
		if (value != SPWR_BATTERY_VALUE_UNKNOWN)
			val->intval = value * 1000;
		else
			status = -ENODATA;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_POWER_NOW:
		value = get_unaligned_le32(&bat->bst.present_rate);
		if (value != SPWR_BATTERY_VALUE_UNKNOWN)
			val->intval = value * 1000;
		else
			status = -ENODATA;
		break;

	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		value = get_unaligned_le32(&bat->bix.design_cap);
		if (value != SPWR_BATTERY_VALUE_UNKNOWN)
			val->intval = value * 1000;
		else
			status = -ENODATA;
		break;

	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_ENERGY_FULL:
		value = get_unaligned_le32(&bat->bix.last_full_charge_cap);
		if (value != SPWR_BATTERY_VALUE_UNKNOWN)
			val->intval = value * 1000;
		else
			status = -ENODATA;
		break;

	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		value = get_unaligned_le32(&bat->bst.remaining_cap);
		if (value != SPWR_BATTERY_VALUE_UNKNOWN)
			val->intval = value * 1000;
		else
			status = -ENODATA;
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = spwr_battery_prop_capacity(bat);
		break;

	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = spwr_battery_prop_capacity_level(bat);
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = bat->bix.model;
		break;

	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = bat->bix.oem_info;
		break;

	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = bat->bix.serial;
		break;

	default:
		status = -EINVAL;
		break;
	}

out:
	mutex_unlock(&bat->lock);
	return status;
}


/* -- Alarm attribute. ------------------------------------------------------ */

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct spwr_battery_device *bat = power_supply_get_drvdata(psy);
	int status;

	mutex_lock(&bat->lock);
	status = sysfs_emit(buf, "%d\n", bat->alarm * 1000);
	mutex_unlock(&bat->lock);

	return status;
}

static ssize_t alarm_store(struct device *dev, struct device_attribute *attr, const char *buf,
			   size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct spwr_battery_device *bat = power_supply_get_drvdata(psy);
	unsigned long value;
	int status;

	status = kstrtoul(buf, 0, &value);
	if (status)
		return status;

	mutex_lock(&bat->lock);

	if (!spwr_battery_present(bat)) {
		mutex_unlock(&bat->lock);
		return -ENODEV;
	}

	status = spwr_battery_set_alarm_unlocked(bat, value / 1000);
	if (status) {
		mutex_unlock(&bat->lock);
		return status;
	}

	mutex_unlock(&bat->lock);
	return count;
}

static DEVICE_ATTR_RW(alarm);

static struct attribute *spwr_battery_attrs[] = {
	&dev_attr_alarm.attr,
	NULL,
};
ATTRIBUTE_GROUPS(spwr_battery);


/* -- Device setup. --------------------------------------------------------- */

static void spwr_battery_init(struct spwr_battery_device *bat, struct ssam_device *sdev,
			      struct ssam_event_registry registry, const char *name)
{
	mutex_init(&bat->lock);
	strncpy(bat->name, name, ARRAY_SIZE(bat->name) - 1);

	bat->sdev = sdev;

	bat->notif.base.priority = 1;
	bat->notif.base.fn = spwr_notify_bat;
	bat->notif.event.reg = registry;
	bat->notif.event.id.target_category = sdev->uid.category;
	bat->notif.event.id.instance = 0;
	bat->notif.event.mask = SSAM_EVENT_MASK_STRICT;
	bat->notif.event.flags = SSAM_EVENT_SEQUENCED;

	bat->psy_desc.name = bat->name;
	bat->psy_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	bat->psy_desc.get_property = spwr_battery_get_property;

	INIT_DELAYED_WORK(&bat->update_work, spwr_battery_update_bst_workfn);
}

static int spwr_battery_register(struct spwr_battery_device *bat)
{
	struct power_supply_config psy_cfg = {};
	__le32 sta;
	int status;

	/* Make sure the device is there and functioning properly. */
	status = ssam_retry(ssam_bat_get_sta, bat->sdev, &sta);
	if (status)
		return status;

	if ((le32_to_cpu(sta) & SAM_BATTERY_STA_OK) != SAM_BATTERY_STA_OK)
		return -ENODEV;

	/* Satisfy lockdep although we are in an exclusive context here. */
	mutex_lock(&bat->lock);

	status = spwr_battery_update_bix_unlocked(bat);
	if (status) {
		mutex_unlock(&bat->lock);
		return status;
	}

	if (spwr_battery_present(bat)) {
		u32 cap_warn = get_unaligned_le32(&bat->bix.design_cap_warn);

		status = spwr_battery_set_alarm_unlocked(bat, cap_warn);
		if (status) {
			mutex_unlock(&bat->lock);
			return status;
		}
	}

	mutex_unlock(&bat->lock);

	bat->psy_desc.external_power_changed = spwr_external_power_changed;

	switch (get_unaligned_le32(&bat->bix.power_unit)) {
	case SAM_BATTERY_POWER_UNIT_mW:
		bat->psy_desc.properties = spwr_battery_props_eng;
		bat->psy_desc.num_properties = ARRAY_SIZE(spwr_battery_props_eng);
		break;

	case SAM_BATTERY_POWER_UNIT_mA:
		bat->psy_desc.properties = spwr_battery_props_chg;
		bat->psy_desc.num_properties = ARRAY_SIZE(spwr_battery_props_chg);
		break;

	default:
		dev_err(&bat->sdev->dev, "unsupported battery power unit: %u\n",
			get_unaligned_le32(&bat->bix.power_unit));
		return -EINVAL;
	}

	psy_cfg.drv_data = bat;
	psy_cfg.attr_grp = spwr_battery_groups;

	bat->psy = devm_power_supply_register(&bat->sdev->dev, &bat->psy_desc, &psy_cfg);
	if (IS_ERR(bat->psy))
		return PTR_ERR(bat->psy);

	return ssam_notifier_register(bat->sdev->ctrl, &bat->notif);
}


/* -- Driver setup. --------------------------------------------------------- */

static int __maybe_unused surface_battery_resume(struct device *dev)
{
	return spwr_battery_recheck_full(dev_get_drvdata(dev));
}
static SIMPLE_DEV_PM_OPS(surface_battery_pm_ops, NULL, surface_battery_resume);

static int surface_battery_probe(struct ssam_device *sdev)
{
	const struct spwr_psy_properties *p;
	struct spwr_battery_device *bat;

	p = ssam_device_get_match_data(sdev);
	if (!p)
		return -ENODEV;

	bat = devm_kzalloc(&sdev->dev, sizeof(*bat), GFP_KERNEL);
	if (!bat)
		return -ENOMEM;

	spwr_battery_init(bat, sdev, p->registry, p->name);
	ssam_device_set_drvdata(sdev, bat);

	return spwr_battery_register(bat);
}

static void surface_battery_remove(struct ssam_device *sdev)
{
	struct spwr_battery_device *bat = ssam_device_get_drvdata(sdev);

	ssam_notifier_unregister(sdev->ctrl, &bat->notif);
	cancel_delayed_work_sync(&bat->update_work);
}

static const struct spwr_psy_properties spwr_psy_props_bat1 = {
	.name = "BAT1",
	.registry = SSAM_EVENT_REGISTRY_SAM,
};

static const struct spwr_psy_properties spwr_psy_props_bat2_sb3 = {
	.name = "BAT2",
	.registry = SSAM_EVENT_REGISTRY_KIP,
};

static const struct ssam_device_id surface_battery_match[] = {
	{ SSAM_SDEV(BAT, 0x01, 0x01, 0x00), (unsigned long)&spwr_psy_props_bat1     },
	{ SSAM_SDEV(BAT, 0x02, 0x01, 0x00), (unsigned long)&spwr_psy_props_bat2_sb3 },
	{ },
};
MODULE_DEVICE_TABLE(ssam, surface_battery_match);

static struct ssam_device_driver surface_battery_driver = {
	.probe = surface_battery_probe,
	.remove = surface_battery_remove,
	.match_table = surface_battery_match,
	.driver = {
		.name = "surface_battery",
		.pm = &surface_battery_pm_ops,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_ssam_device_driver(surface_battery_driver);

MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_DESCRIPTION("Battery driver for Surface System Aggregator Module");
MODULE_LICENSE("GPL");
