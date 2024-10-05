// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024, Linaro Ltd
 * Authors:
 *    Bjorn Andersson
 *    Dmitry Baryshkov
 */
#include <linux/auxiliary_bus.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/power_supply.h>
#include <linux/platform_data/lenovo-yoga-c630.h>

struct yoga_c630_psy {
	struct yoga_c630_ec *ec;
	struct device *dev;
	struct fwnode_handle *fwnode;
	struct notifier_block nb;

	/* guards all battery properties and registration of power supplies */
	struct mutex lock;

	struct power_supply *adp_psy;
	struct power_supply *bat_psy;

	unsigned long last_status_update;

	bool adapter_online;

	bool unit_mA;

	bool bat_present;
	unsigned int bat_status;
	unsigned int design_capacity;
	unsigned int design_voltage;
	unsigned int full_charge_capacity;

	unsigned int capacity_now;
	unsigned int voltage_now;

	int current_now;
	int rate_now;
};

#define LENOVO_EC_CACHE_TIME		(10 * HZ)

#define LENOVO_EC_ADPT_STATUS		0xa3
#define LENOVO_EC_ADPT_STATUS_PRESENT		BIT(7)
#define LENOVO_EC_BAT_ATTRIBUTES	0xc0
#define LENOVO_EC_BAT_ATTRIBUTES_UNIT_IS_MA	BIT(1)
#define LENOVO_EC_BAT_STATUS		0xc1
#define LENOVO_EC_BAT_STATUS_DISCHARGING	BIT(0)
#define LENOVO_EC_BAT_STATUS_CHARGING		BIT(1)
#define LENOVO_EC_BAT_REMAIN_CAPACITY	0xc2
#define LENOVO_EC_BAT_VOLTAGE		0xc6
#define LENOVO_EC_BAT_DESIGN_VOLTAGE	0xc8
#define LENOVO_EC_BAT_DESIGN_CAPACITY	0xca
#define LENOVO_EC_BAT_FULL_CAPACITY	0xcc
#define LENOVO_EC_BAT_CURRENT		0xd2
#define LENOVO_EC_BAT_FULL_FACTORY	0xd6
#define LENOVO_EC_BAT_PRESENT		0xda
#define LENOVO_EC_BAT_PRESENT_IS_PRESENT	BIT(0)
#define LENOVO_EC_BAT_FULL_REGISTER	0xdb
#define LENOVO_EC_BAT_FULL_REGISTER_IS_FACTORY	BIT(0)

static int yoga_c630_psy_update_bat_info(struct yoga_c630_psy *ecbat)
{
	struct yoga_c630_ec *ec = ecbat->ec;
	int val;

	lockdep_assert_held(&ecbat->lock);

	val = yoga_c630_ec_read8(ec, LENOVO_EC_BAT_PRESENT);
	if (val < 0)
		return val;
	ecbat->bat_present = !!(val & LENOVO_EC_BAT_PRESENT_IS_PRESENT);
	if (!ecbat->bat_present)
		return val;

	val = yoga_c630_ec_read8(ec, LENOVO_EC_BAT_ATTRIBUTES);
	if (val < 0)
		return val;
	ecbat->unit_mA = val & LENOVO_EC_BAT_ATTRIBUTES_UNIT_IS_MA;

	val = yoga_c630_ec_read16(ec, LENOVO_EC_BAT_DESIGN_CAPACITY);
	if (val < 0)
		return val;
	ecbat->design_capacity = val * 1000;

	/*
	 * DSDT has delays after most of EC reads in these methods.
	 * Having no documentation for the EC we have to follow and sleep here.
	 */
	msleep(50);

	val = yoga_c630_ec_read16(ec, LENOVO_EC_BAT_DESIGN_VOLTAGE);
	if (val < 0)
		return val;
	ecbat->design_voltage = val;

	msleep(50);

	val = yoga_c630_ec_read8(ec, LENOVO_EC_BAT_FULL_REGISTER);
	if (val < 0)
		return val;
	val = yoga_c630_ec_read16(ec,
				  val & LENOVO_EC_BAT_FULL_REGISTER_IS_FACTORY ?
				  LENOVO_EC_BAT_FULL_FACTORY :
				  LENOVO_EC_BAT_FULL_CAPACITY);
	if (val < 0)
		return val;

	ecbat->full_charge_capacity = val * 1000;

	if (!ecbat->unit_mA) {
		ecbat->design_capacity *= 10;
		ecbat->full_charge_capacity *= 10;
	}

	return 0;
}

static int yoga_c630_psy_maybe_update_bat_status(struct yoga_c630_psy *ecbat)
{
	struct yoga_c630_ec *ec = ecbat->ec;
	int current_mA;
	int val;

	guard(mutex)(&ecbat->lock);
	if (time_before(jiffies, ecbat->last_status_update + LENOVO_EC_CACHE_TIME))
		return 0;

	val = yoga_c630_ec_read8(ec, LENOVO_EC_BAT_STATUS);
	if (val < 0)
		return val;
	ecbat->bat_status = val;

	msleep(50);

	val = yoga_c630_ec_read16(ec, LENOVO_EC_BAT_REMAIN_CAPACITY);
	if (val < 0)
		return val;
	ecbat->capacity_now = val * 1000;

	msleep(50);

	val = yoga_c630_ec_read16(ec, LENOVO_EC_BAT_VOLTAGE);
	if (val < 0)
		return val;
	ecbat->voltage_now = val * 1000;

	msleep(50);

	val = yoga_c630_ec_read16(ec, LENOVO_EC_BAT_CURRENT);
	if (val < 0)
		return val;
	current_mA = sign_extend32(val, 15);
	ecbat->current_now = current_mA * 1000;
	ecbat->rate_now = current_mA * (ecbat->voltage_now / 1000);

	msleep(50);

	if (!ecbat->unit_mA)
		ecbat->capacity_now *= 10;

	ecbat->last_status_update = jiffies;

	return 0;
}

static int yoga_c630_psy_update_adapter_status(struct yoga_c630_psy *ecbat)
{
	struct yoga_c630_ec *ec = ecbat->ec;
	int val;

	guard(mutex)(&ecbat->lock);

	val = yoga_c630_ec_read8(ec, LENOVO_EC_ADPT_STATUS);
	if (val < 0)
		return val;

	ecbat->adapter_online = !!(val & LENOVO_EC_ADPT_STATUS_PRESENT);

	return 0;
}

static bool yoga_c630_psy_is_charged(struct yoga_c630_psy *ecbat)
{
	if (ecbat->bat_status != 0)
		return false;

	if (ecbat->full_charge_capacity <= ecbat->capacity_now)
		return true;

	if (ecbat->design_capacity <= ecbat->capacity_now)
		return true;

	return false;
}

static int yoga_c630_psy_bat_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct yoga_c630_psy *ecbat = power_supply_get_drvdata(psy);
	int rc = 0;

	if (!ecbat->bat_present && psp != POWER_SUPPLY_PROP_PRESENT)
		return -ENODEV;

	rc = yoga_c630_psy_maybe_update_bat_status(ecbat);
	if (rc)
		return rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (ecbat->bat_status & LENOVO_EC_BAT_STATUS_DISCHARGING)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (ecbat->bat_status & LENOVO_EC_BAT_STATUS_CHARGING)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (yoga_c630_psy_is_charged(ecbat))
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = ecbat->bat_present;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = ecbat->design_voltage;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		val->intval = ecbat->design_capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_ENERGY_FULL:
		val->intval = ecbat->full_charge_capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		val->intval = ecbat->capacity_now;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = ecbat->current_now;
		break;
	case POWER_SUPPLY_PROP_POWER_NOW:
		val->intval = ecbat->rate_now;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = ecbat->voltage_now;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = "PABAS0241231";
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Compal";
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static enum power_supply_property yoga_c630_psy_bat_mA_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SCOPE,
};

static enum power_supply_property yoga_c630_psy_bat_mWh_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SCOPE,
};

static const struct power_supply_desc yoga_c630_psy_bat_psy_desc_mA = {
	.name = "yoga-c630-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = yoga_c630_psy_bat_mA_properties,
	.num_properties = ARRAY_SIZE(yoga_c630_psy_bat_mA_properties),
	.get_property = yoga_c630_psy_bat_get_property,
};

static const struct power_supply_desc yoga_c630_psy_bat_psy_desc_mWh = {
	.name = "yoga-c630-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = yoga_c630_psy_bat_mWh_properties,
	.num_properties = ARRAY_SIZE(yoga_c630_psy_bat_mWh_properties),
	.get_property = yoga_c630_psy_bat_get_property,
};

static int yoga_c630_psy_adpt_get_property(struct power_supply *psy,
					  enum power_supply_property psp,
					  union power_supply_propval *val)
{
	struct yoga_c630_psy *ecbat = power_supply_get_drvdata(psy);
	int ret = 0;

	ret = yoga_c630_psy_update_adapter_status(ecbat);
	if (ret < 0)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = ecbat->adapter_online;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = POWER_SUPPLY_USB_TYPE_C;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static enum power_supply_property yoga_c630_psy_adpt_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
};

static const struct power_supply_desc yoga_c630_psy_adpt_psy_desc = {
	.name = "yoga-c630-adapter",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_C),
	.properties = yoga_c630_psy_adpt_properties,
	.num_properties = ARRAY_SIZE(yoga_c630_psy_adpt_properties),
	.get_property = yoga_c630_psy_adpt_get_property,
};

static int yoga_c630_psy_register_bat_psy(struct yoga_c630_psy *ecbat)
{
	struct power_supply_config bat_cfg = {};

	bat_cfg.drv_data = ecbat;
	bat_cfg.fwnode = ecbat->fwnode;
	bat_cfg.no_wakeup_source = true;
	ecbat->bat_psy = power_supply_register(ecbat->dev,
					       ecbat->unit_mA ?
					       &yoga_c630_psy_bat_psy_desc_mA :
					       &yoga_c630_psy_bat_psy_desc_mWh,
					       &bat_cfg);
	if (IS_ERR(ecbat->bat_psy)) {
		dev_err(ecbat->dev, "failed to register battery supply\n");
		return PTR_ERR(ecbat->bat_psy);
	}

	return 0;
}

static void yoga_c630_ec_refresh_bat_info(struct yoga_c630_psy *ecbat)
{
	bool current_unit;

	guard(mutex)(&ecbat->lock);

	current_unit = ecbat->unit_mA;

	yoga_c630_psy_update_bat_info(ecbat);

	if (current_unit != ecbat->unit_mA) {
		power_supply_unregister(ecbat->bat_psy);
		yoga_c630_psy_register_bat_psy(ecbat);
	}
}

static int yoga_c630_psy_notify(struct notifier_block *nb,
				unsigned long action, void *data)
{
	struct yoga_c630_psy *ecbat = container_of(nb, struct yoga_c630_psy, nb);

	switch (action) {
	case LENOVO_EC_EVENT_BAT_INFO:
		yoga_c630_ec_refresh_bat_info(ecbat);
		break;
	case LENOVO_EC_EVENT_BAT_ADPT_STATUS:
		power_supply_changed(ecbat->adp_psy);
		fallthrough;
	case LENOVO_EC_EVENT_BAT_STATUS:
		power_supply_changed(ecbat->bat_psy);
		break;
	}

	return NOTIFY_OK;
}

static int yoga_c630_psy_probe(struct auxiliary_device *adev,
				   const struct auxiliary_device_id *id)
{
	struct yoga_c630_ec *ec = adev->dev.platform_data;
	struct power_supply_config adp_cfg = {};
	struct device *dev = &adev->dev;
	struct yoga_c630_psy *ecbat;
	int ret;

	ecbat = devm_kzalloc(&adev->dev, sizeof(*ecbat), GFP_KERNEL);
	if (!ecbat)
		return -ENOMEM;

	ecbat->ec = ec;
	ecbat->dev = dev;
	mutex_init(&ecbat->lock);
	ecbat->fwnode = adev->dev.parent->fwnode;
	ecbat->nb.notifier_call = yoga_c630_psy_notify;

	auxiliary_set_drvdata(adev, ecbat);

	adp_cfg.drv_data = ecbat;
	adp_cfg.fwnode = ecbat->fwnode;
	adp_cfg.supplied_to = (char **)&yoga_c630_psy_bat_psy_desc_mA.name;
	adp_cfg.num_supplicants = 1;
	adp_cfg.no_wakeup_source = true;
	ecbat->adp_psy = devm_power_supply_register(dev, &yoga_c630_psy_adpt_psy_desc, &adp_cfg);
	if (IS_ERR(ecbat->adp_psy)) {
		dev_err(dev, "failed to register AC adapter supply\n");
		return PTR_ERR(ecbat->adp_psy);
	}

	scoped_guard(mutex, &ecbat->lock) {
		ret = yoga_c630_psy_update_bat_info(ecbat);
		if (ret)
			goto err_unreg_bat;

		ret = yoga_c630_psy_register_bat_psy(ecbat);
		if (ret)
			goto err_unreg_bat;
	}

	ret = yoga_c630_ec_register_notify(ecbat->ec, &ecbat->nb);
	if (ret)
		goto err_unreg_bat;

	return 0;

err_unreg_bat:
	power_supply_unregister(ecbat->bat_psy);
	return ret;
}

static void yoga_c630_psy_remove(struct auxiliary_device *adev)
{
	struct yoga_c630_psy *ecbat = auxiliary_get_drvdata(adev);

	yoga_c630_ec_unregister_notify(ecbat->ec, &ecbat->nb);
	power_supply_unregister(ecbat->bat_psy);
}

static const struct auxiliary_device_id yoga_c630_psy_id_table[] = {
	{ .name = YOGA_C630_MOD_NAME "." YOGA_C630_DEV_PSY, },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, yoga_c630_psy_id_table);

static struct auxiliary_driver yoga_c630_psy_driver = {
	.name = YOGA_C630_DEV_PSY,
	.id_table = yoga_c630_psy_id_table,
	.probe = yoga_c630_psy_probe,
	.remove = yoga_c630_psy_remove,
};

module_auxiliary_driver(yoga_c630_psy_driver);

MODULE_DESCRIPTION("Lenovo Yoga C630 psy");
MODULE_LICENSE("GPL");
