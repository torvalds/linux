/*
 * nvec_power: power supply driver for a NVIDIA compliant embedded controller
 *
 * Copyright (C) 2011 The AC100 Kernel Team <ac100@lists.launchpad.net>
 *
 * Authors:  Ilya Petrov <ilya.muromec@gmail.com>
 *           Marc Dietrich <marvin24@gmx.de>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#include "nvec.h"

#define GET_SYSTEM_STATUS 0x00

struct nvec_power {
	struct notifier_block notifier;
	struct delayed_work poller;
	struct nvec_chip *nvec;
	int on;
	int bat_present;
	int bat_status;
	int bat_voltage_now;
	int bat_current_now;
	int bat_current_avg;
	int time_remain;
	int charge_full_design;
	int charge_last_full;
	int critical_capacity;
	int capacity_remain;
	int bat_temperature;
	int bat_cap;
	int bat_type_enum;
	char bat_manu[30];
	char bat_model[30];
	char bat_type[30];
};

enum {
	SLOT_STATUS,
	VOLTAGE,
	TIME_REMAINING,
	CURRENT,
	AVERAGE_CURRENT,
	AVERAGING_TIME_INTERVAL,
	CAPACITY_REMAINING,
	LAST_FULL_CHARGE_CAPACITY,
	DESIGN_CAPACITY,
	CRITICAL_CAPACITY,
	TEMPERATURE,
	MANUFACTURER,
	MODEL,
	TYPE,
};

enum {
	AC,
	BAT,
};

struct bat_response {
	u8 event_type;
	u8 length;
	u8 sub_type;
	u8 status;
	/* payload */
	union {
		char plc[30];
		u16 plu;
		s16 pls;
	};
};

static struct power_supply nvec_bat_psy;
static struct power_supply nvec_psy;

static int nvec_power_notifier(struct notifier_block *nb,
			       unsigned long event_type, void *data)
{
	struct nvec_power *power =
	    container_of(nb, struct nvec_power, notifier);
	struct bat_response *res = (struct bat_response *)data;

	if (event_type != NVEC_SYS)
		return NOTIFY_DONE;

	if (res->sub_type == 0) {
		if (power->on != res->plu) {
			power->on = res->plu;
			power_supply_changed(&nvec_psy);
		}
		return NOTIFY_STOP;
	}
	return NOTIFY_OK;
}

static const int bat_init[] = {
	LAST_FULL_CHARGE_CAPACITY, DESIGN_CAPACITY, CRITICAL_CAPACITY,
	MANUFACTURER, MODEL, TYPE,
};

static void get_bat_mfg_data(struct nvec_power *power)
{
	int i;
	char buf[] = { NVEC_BAT, SLOT_STATUS };

	for (i = 0; i < ARRAY_SIZE(bat_init); i++) {
		buf[1] = bat_init[i];
		nvec_write_async(power->nvec, buf, 2);
	}
}

static int nvec_power_bat_notifier(struct notifier_block *nb,
				   unsigned long event_type, void *data)
{
	struct nvec_power *power =
	    container_of(nb, struct nvec_power, notifier);
	struct bat_response *res = (struct bat_response *)data;
	int status_changed = 0;

	if (event_type != NVEC_BAT)
		return NOTIFY_DONE;

	switch (res->sub_type) {
	case SLOT_STATUS:
		if (res->plc[0] & 1) {
			if (power->bat_present == 0) {
				status_changed = 1;
				get_bat_mfg_data(power);
			}

			power->bat_present = 1;

			switch ((res->plc[0] >> 1) & 3) {
			case 0:
				power->bat_status =
				    POWER_SUPPLY_STATUS_NOT_CHARGING;
				break;
			case 1:
				power->bat_status =
				    POWER_SUPPLY_STATUS_CHARGING;
				break;
			case 2:
				power->bat_status =
				    POWER_SUPPLY_STATUS_DISCHARGING;
				break;
			default:
				power->bat_status = POWER_SUPPLY_STATUS_UNKNOWN;
			}
		} else {
			if (power->bat_present == 1)
				status_changed = 1;

			power->bat_present = 0;
			power->bat_status = POWER_SUPPLY_STATUS_UNKNOWN;
		}
		power->bat_cap = res->plc[1];
		if (status_changed)
			power_supply_changed(&nvec_bat_psy);
		break;
	case VOLTAGE:
		power->bat_voltage_now = res->plu * 1000;
		break;
	case TIME_REMAINING:
		power->time_remain = res->plu * 3600;
		break;
	case CURRENT:
		power->bat_current_now = res->pls * 1000;
		break;
	case AVERAGE_CURRENT:
		power->bat_current_avg = res->pls * 1000;
		break;
	case CAPACITY_REMAINING:
		power->capacity_remain = res->plu * 1000;
		break;
	case LAST_FULL_CHARGE_CAPACITY:
		power->charge_last_full = res->plu * 1000;
		break;
	case DESIGN_CAPACITY:
		power->charge_full_design = res->plu * 1000;
		break;
	case CRITICAL_CAPACITY:
		power->critical_capacity = res->plu * 1000;
		break;
	case TEMPERATURE:
		power->bat_temperature = res->plu - 2732;
		break;
	case MANUFACTURER:
		memcpy(power->bat_manu, &res->plc, res->length - 2);
		power->bat_model[res->length - 2] = '\0';
		break;
	case MODEL:
		memcpy(power->bat_model, &res->plc, res->length - 2);
		power->bat_model[res->length - 2] = '\0';
		break;
	case TYPE:
		memcpy(power->bat_type, &res->plc, res->length - 2);
		power->bat_type[res->length - 2] = '\0';
		/* this differs a little from the spec
		   fill in more if you find some */
		if (!strncmp(power->bat_type, "Li", 30))
			power->bat_type_enum = POWER_SUPPLY_TECHNOLOGY_LION;
		else
			power->bat_type_enum = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
		break;
	default:
		return NOTIFY_STOP;
	}

	return NOTIFY_STOP;
}

static int nvec_power_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct nvec_power *power = dev_get_drvdata(psy->dev->parent);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = power->on;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int nvec_battery_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	struct nvec_power *power = dev_get_drvdata(psy->dev->parent);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = power->bat_status;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = power->bat_cap;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = power->bat_present;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = power->bat_voltage_now;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = power->bat_current_now;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = power->bat_current_avg;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		val->intval = power->time_remain;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = power->charge_full_design;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = power->charge_last_full;
		break;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY:
		val->intval = power->critical_capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = power->capacity_remain;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = power->bat_temperature;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = power->bat_manu;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = power->bat_model;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = power->bat_type_enum;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static enum power_supply_property nvec_power_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property nvec_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
#ifdef EC_FULL_DIAG
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
#endif
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_EMPTY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_TECHNOLOGY,
};

static char *nvec_power_supplied_to[] = {
	"battery",
};

static struct power_supply nvec_bat_psy = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = nvec_battery_props,
	.num_properties = ARRAY_SIZE(nvec_battery_props),
	.get_property = nvec_battery_get_property,
};

static struct power_supply nvec_psy = {
	.name = "ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = nvec_power_props,
	.num_properties = ARRAY_SIZE(nvec_power_props),
	.get_property = nvec_power_get_property,
};

static int counter;
static int const bat_iter[] = {
	SLOT_STATUS, VOLTAGE, CURRENT, CAPACITY_REMAINING,
#ifdef EC_FULL_DIAG
	AVERAGE_CURRENT, TEMPERATURE, TIME_REMAINING,
#endif
};

static void nvec_power_poll(struct work_struct *work)
{
	char buf[] = { NVEC_SYS, GET_SYSTEM_STATUS };
	struct nvec_power *power = container_of(work, struct nvec_power,
						poller.work);

	if (counter >= ARRAY_SIZE(bat_iter))
		counter = 0;

/* AC status via sys req */
	nvec_write_async(power->nvec, buf, 2);
	msleep(100);

/* select a battery request function via round robin
   doing it all at once seems to overload the power supply */
	buf[0] = NVEC_BAT;
	buf[1] = bat_iter[counter++];
	nvec_write_async(power->nvec, buf, 2);

	schedule_delayed_work(to_delayed_work(work), msecs_to_jiffies(5000));
};

static int nvec_power_probe(struct platform_device *pdev)
{
	struct power_supply *psy;
	struct nvec_power *power;
	struct nvec_chip *nvec = dev_get_drvdata(pdev->dev.parent);
	struct power_supply_config psy_cfg = {};

	power = devm_kzalloc(&pdev->dev, sizeof(struct nvec_power), GFP_NOWAIT);
	if (power == NULL)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, power);
	power->nvec = nvec;

	switch (pdev->id) {
	case AC:
		psy = &nvec_psy;
		psy_cfg.supplied_to = nvec_power_supplied_to;
		psy_cfg.num_supplicants = ARRAY_SIZE(nvec_power_supplied_to);

		power->notifier.notifier_call = nvec_power_notifier;

		INIT_DELAYED_WORK(&power->poller, nvec_power_poll);
		schedule_delayed_work(&power->poller, msecs_to_jiffies(5000));
		break;
	case BAT:
		psy = &nvec_bat_psy;

		power->notifier.notifier_call = nvec_power_bat_notifier;
		break;
	default:
		return -ENODEV;
	}

	nvec_register_notifier(nvec, &power->notifier, NVEC_SYS);

	if (pdev->id == BAT)
		get_bat_mfg_data(power);

	return power_supply_register(&pdev->dev, psy, &psy_cfg);
}

static int nvec_power_remove(struct platform_device *pdev)
{
	struct nvec_power *power = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&power->poller);
	nvec_unregister_notifier(power->nvec, &power->notifier);
	switch (pdev->id) {
	case AC:
		power_supply_unregister(&nvec_psy);
		break;
	case BAT:
		power_supply_unregister(&nvec_bat_psy);
	}

	return 0;
}

static struct platform_driver nvec_power_driver = {
	.probe = nvec_power_probe,
	.remove = nvec_power_remove,
	.driver = {
		   .name = "nvec-power",
		   }
};

module_platform_driver(nvec_power_driver);

MODULE_AUTHOR("Ilya Petrov <ilya.muromec@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NVEC battery and AC driver");
MODULE_ALIAS("platform:nvec-power");
