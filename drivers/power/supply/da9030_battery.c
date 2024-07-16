// SPDX-License-Identifier: GPL-2.0-only
/*
 * Battery charger driver for Dialog Semiconductor DA9030
 *
 * Copyright (C) 2008 Compulab, Ltd.
 * 	Mike Rapoport <mike@compulab.co.il>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/mfd/da903x.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/notifier.h>

#define DA9030_FAULT_LOG		0x0a
#define DA9030_FAULT_LOG_OVER_TEMP	(1 << 7)
#define DA9030_FAULT_LOG_VBAT_OVER	(1 << 4)

#define DA9030_CHARGE_CONTROL		0x28
#define DA9030_CHRG_CHARGER_ENABLE	(1 << 7)

#define DA9030_ADC_MAN_CONTROL		0x30
#define DA9030_ADC_TBATREF_ENABLE	(1 << 5)
#define DA9030_ADC_LDO_INT_ENABLE	(1 << 4)

#define DA9030_ADC_AUTO_CONTROL		0x31
#define DA9030_ADC_TBAT_ENABLE		(1 << 5)
#define DA9030_ADC_VBAT_IN_TXON		(1 << 4)
#define DA9030_ADC_VCH_ENABLE		(1 << 3)
#define DA9030_ADC_ICH_ENABLE		(1 << 2)
#define DA9030_ADC_VBAT_ENABLE		(1 << 1)
#define DA9030_ADC_AUTO_SLEEP_ENABLE	(1 << 0)

#define DA9030_VBATMON		0x32
#define DA9030_VBATMONTXON	0x33
#define DA9030_TBATHIGHP	0x34
#define DA9030_TBATHIGHN	0x35
#define DA9030_TBATLOW		0x36

#define DA9030_VBAT_RES		0x41
#define DA9030_VBATMIN_RES	0x42
#define DA9030_VBATMINTXON_RES	0x43
#define DA9030_ICHMAX_RES	0x44
#define DA9030_ICHMIN_RES	0x45
#define DA9030_ICHAVERAGE_RES	0x46
#define DA9030_VCHMAX_RES	0x47
#define DA9030_VCHMIN_RES	0x48
#define DA9030_TBAT_RES		0x49

struct da9030_adc_res {
	uint8_t vbat_res;
	uint8_t vbatmin_res;
	uint8_t vbatmintxon;
	uint8_t ichmax_res;
	uint8_t ichmin_res;
	uint8_t ichaverage_res;
	uint8_t vchmax_res;
	uint8_t vchmin_res;
	uint8_t tbat_res;
	uint8_t adc_in4_res;
	uint8_t adc_in5_res;
};

struct da9030_battery_thresholds {
	int tbat_low;
	int tbat_high;
	int tbat_restart;

	int vbat_low;
	int vbat_crit;
	int vbat_charge_start;
	int vbat_charge_stop;
	int vbat_charge_restart;

	int vcharge_min;
	int vcharge_max;
};

struct da9030_charger {
	struct power_supply *psy;
	struct power_supply_desc psy_desc;

	struct device *master;

	struct da9030_adc_res adc;
	struct delayed_work work;
	unsigned int interval;

	struct power_supply_info *battery_info;

	struct da9030_battery_thresholds thresholds;

	unsigned int charge_milliamp;
	unsigned int charge_millivolt;

	/* charger status */
	bool chdet;
	uint8_t fault;
	int mA;
	int mV;
	bool is_on;

	struct notifier_block nb;

	/* platform callbacks for battery low and critical events */
	void (*battery_low)(void);
	void (*battery_critical)(void);

	struct dentry *debug_file;
};

static inline int da9030_reg_to_mV(int reg)
{
	return ((reg * 2650) >> 8) + 2650;
}

static inline int da9030_millivolt_to_reg(int mV)
{
	return ((mV - 2650) << 8) / 2650;
}

static inline int da9030_reg_to_mA(int reg)
{
	return ((reg * 24000) >> 8) / 15;
}

#ifdef CONFIG_DEBUG_FS
static int bat_debug_show(struct seq_file *s, void *data)
{
	struct da9030_charger *charger = s->private;

	seq_printf(s, "charger is %s\n", charger->is_on ? "on" : "off");
	if (charger->chdet) {
		seq_printf(s, "iset = %dmA, vset = %dmV\n",
			   charger->mA, charger->mV);
	}

	seq_printf(s, "vbat_res = %d (%dmV)\n",
		   charger->adc.vbat_res,
		   da9030_reg_to_mV(charger->adc.vbat_res));
	seq_printf(s, "vbatmin_res = %d (%dmV)\n",
		   charger->adc.vbatmin_res,
		   da9030_reg_to_mV(charger->adc.vbatmin_res));
	seq_printf(s, "vbatmintxon = %d (%dmV)\n",
		   charger->adc.vbatmintxon,
		   da9030_reg_to_mV(charger->adc.vbatmintxon));
	seq_printf(s, "ichmax_res = %d (%dmA)\n",
		   charger->adc.ichmax_res,
		   da9030_reg_to_mV(charger->adc.ichmax_res));
	seq_printf(s, "ichmin_res = %d (%dmA)\n",
		   charger->adc.ichmin_res,
		   da9030_reg_to_mA(charger->adc.ichmin_res));
	seq_printf(s, "ichaverage_res = %d (%dmA)\n",
		   charger->adc.ichaverage_res,
		   da9030_reg_to_mA(charger->adc.ichaverage_res));
	seq_printf(s, "vchmax_res = %d (%dmV)\n",
		   charger->adc.vchmax_res,
		   da9030_reg_to_mA(charger->adc.vchmax_res));
	seq_printf(s, "vchmin_res = %d (%dmV)\n",
		   charger->adc.vchmin_res,
		   da9030_reg_to_mV(charger->adc.vchmin_res));

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(bat_debug);

static struct dentry *da9030_bat_create_debugfs(struct da9030_charger *charger)
{
	charger->debug_file = debugfs_create_file("charger", 0666, NULL,
						  charger, &bat_debug_fops);
	return charger->debug_file;
}

static void da9030_bat_remove_debugfs(struct da9030_charger *charger)
{
	debugfs_remove(charger->debug_file);
}
#else
static inline struct dentry *da9030_bat_create_debugfs(struct da9030_charger *charger)
{
	return NULL;
}
static inline void da9030_bat_remove_debugfs(struct da9030_charger *charger)
{
}
#endif

static inline void da9030_read_adc(struct da9030_charger *charger,
				   struct da9030_adc_res *adc)
{
	da903x_reads(charger->master, DA9030_VBAT_RES,
		     sizeof(*adc), (uint8_t *)adc);
}

static void da9030_charger_update_state(struct da9030_charger *charger)
{
	uint8_t val;

	da903x_read(charger->master, DA9030_CHARGE_CONTROL, &val);
	charger->is_on = (val & DA9030_CHRG_CHARGER_ENABLE) ? 1 : 0;
	charger->mA = ((val >> 3) & 0xf) * 100;
	charger->mV = (val & 0x7) * 50 + 4000;

	da9030_read_adc(charger, &charger->adc);
	da903x_read(charger->master, DA9030_FAULT_LOG, &charger->fault);
	charger->chdet = da903x_query_status(charger->master,
						     DA9030_STATUS_CHDET);
}

static void da9030_set_charge(struct da9030_charger *charger, int on)
{
	uint8_t val;

	if (on) {
		val = DA9030_CHRG_CHARGER_ENABLE;
		val |= (charger->charge_milliamp / 100) << 3;
		val |= (charger->charge_millivolt - 4000) / 50;
		charger->is_on = 1;
	} else {
		val = 0;
		charger->is_on = 0;
	}

	da903x_write(charger->master, DA9030_CHARGE_CONTROL, val);

	power_supply_changed(charger->psy);
}

static void da9030_charger_check_state(struct da9030_charger *charger)
{
	da9030_charger_update_state(charger);

	/* we wake or boot with external power on */
	if (!charger->is_on) {
		if ((charger->chdet) &&
		    (charger->adc.vbat_res <
		     charger->thresholds.vbat_charge_start)) {
			da9030_set_charge(charger, 1);
		}
	} else {
		/* Charger has been pulled out */
		if (!charger->chdet) {
			da9030_set_charge(charger, 0);
			return;
		}

		if (charger->adc.vbat_res >=
		    charger->thresholds.vbat_charge_stop) {
			da9030_set_charge(charger, 0);
			da903x_write(charger->master, DA9030_VBATMON,
				       charger->thresholds.vbat_charge_restart);
		} else if (charger->adc.vbat_res >
			   charger->thresholds.vbat_low) {
			/* we are charging and passed LOW_THRESH,
			   so upate DA9030 VBAT threshold
			 */
			da903x_write(charger->master, DA9030_VBATMON,
				     charger->thresholds.vbat_low);
		}
		if (charger->adc.vchmax_res > charger->thresholds.vcharge_max ||
		    charger->adc.vchmin_res < charger->thresholds.vcharge_min ||
		    /* Tempreture readings are negative */
		    charger->adc.tbat_res < charger->thresholds.tbat_high ||
		    charger->adc.tbat_res > charger->thresholds.tbat_low) {
			/* disable charger */
			da9030_set_charge(charger, 0);
		}
	}
}

static void da9030_charging_monitor(struct work_struct *work)
{
	struct da9030_charger *charger;

	charger = container_of(work, struct da9030_charger, work.work);

	da9030_charger_check_state(charger);

	/* reschedule for the next time */
	schedule_delayed_work(&charger->work, charger->interval);
}

static enum power_supply_property da9030_battery_props[] = {
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
};

static void da9030_battery_check_status(struct da9030_charger *charger,
				    union power_supply_propval *val)
{
	if (charger->chdet) {
		if (charger->is_on)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
	} else {
		val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
	}
}

static void da9030_battery_check_health(struct da9030_charger *charger,
				    union power_supply_propval *val)
{
	if (charger->fault & DA9030_FAULT_LOG_OVER_TEMP)
		val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (charger->fault & DA9030_FAULT_LOG_VBAT_OVER)
		val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	else
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
}

static int da9030_battery_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct da9030_charger *charger = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		da9030_battery_check_status(charger, val);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		da9030_battery_check_health(charger, val);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = charger->battery_info->technology;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = charger->battery_info->voltage_max_design;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = charger->battery_info->voltage_min_design;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = da9030_reg_to_mV(charger->adc.vbat_res) * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval =
			da9030_reg_to_mA(charger->adc.ichaverage_res) * 1000;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = charger->battery_info->name;
		break;
	default:
		break;
	}

	return 0;
}

static void da9030_battery_vbat_event(struct da9030_charger *charger)
{
	da9030_read_adc(charger, &charger->adc);

	if (charger->is_on)
		return;

	if (charger->adc.vbat_res < charger->thresholds.vbat_low) {
		/* set VBAT threshold for critical */
		da903x_write(charger->master, DA9030_VBATMON,
			     charger->thresholds.vbat_crit);
		if (charger->battery_low)
			charger->battery_low();
	} else if (charger->adc.vbat_res <
		   charger->thresholds.vbat_crit) {
		/* notify the system of battery critical */
		if (charger->battery_critical)
			charger->battery_critical();
	}
}

static int da9030_battery_event(struct notifier_block *nb, unsigned long event,
				void *data)
{
	struct da9030_charger *charger =
		container_of(nb, struct da9030_charger, nb);

	switch (event) {
	case DA9030_EVENT_CHDET:
		cancel_delayed_work_sync(&charger->work);
		schedule_work(&charger->work.work);
		break;
	case DA9030_EVENT_VBATMON:
		da9030_battery_vbat_event(charger);
		break;
	case DA9030_EVENT_CHIOVER:
	case DA9030_EVENT_TBAT:
		da9030_set_charge(charger, 0);
		break;
	}

	return 0;
}

static void da9030_battery_convert_thresholds(struct da9030_charger *charger,
					      struct da9030_battery_info *pdata)
{
	charger->thresholds.tbat_low = pdata->tbat_low;
	charger->thresholds.tbat_high = pdata->tbat_high;
	charger->thresholds.tbat_restart  = pdata->tbat_restart;

	charger->thresholds.vbat_low =
		da9030_millivolt_to_reg(pdata->vbat_low);
	charger->thresholds.vbat_crit =
		da9030_millivolt_to_reg(pdata->vbat_crit);
	charger->thresholds.vbat_charge_start =
		da9030_millivolt_to_reg(pdata->vbat_charge_start);
	charger->thresholds.vbat_charge_stop =
		da9030_millivolt_to_reg(pdata->vbat_charge_stop);
	charger->thresholds.vbat_charge_restart =
		da9030_millivolt_to_reg(pdata->vbat_charge_restart);

	charger->thresholds.vcharge_min =
		da9030_millivolt_to_reg(pdata->vcharge_min);
	charger->thresholds.vcharge_max =
		da9030_millivolt_to_reg(pdata->vcharge_max);
}

static void da9030_battery_setup_psy(struct da9030_charger *charger)
{
	struct power_supply_desc *psy_desc = &charger->psy_desc;
	struct power_supply_info *info = charger->battery_info;

	psy_desc->name = info->name;
	psy_desc->use_for_apm = info->use_for_apm;
	psy_desc->type = POWER_SUPPLY_TYPE_BATTERY;
	psy_desc->get_property = da9030_battery_get_property;

	psy_desc->properties = da9030_battery_props;
	psy_desc->num_properties = ARRAY_SIZE(da9030_battery_props);
};

static int da9030_battery_charger_init(struct da9030_charger *charger)
{
	char v[5];
	int ret;

	v[0] = v[1] = charger->thresholds.vbat_low;
	v[2] = charger->thresholds.tbat_high;
	v[3] = charger->thresholds.tbat_restart;
	v[4] = charger->thresholds.tbat_low;

	ret = da903x_writes(charger->master, DA9030_VBATMON, 5, v);
	if (ret)
		return ret;

	/*
	 * Enable reference voltage supply for ADC from the LDO_INTERNAL
	 * regulator. Must be set before ADC measurements can be made.
	 */
	ret = da903x_write(charger->master, DA9030_ADC_MAN_CONTROL,
			   DA9030_ADC_LDO_INT_ENABLE |
			   DA9030_ADC_TBATREF_ENABLE);
	if (ret)
		return ret;

	/* enable auto ADC measuremnts */
	return da903x_write(charger->master, DA9030_ADC_AUTO_CONTROL,
			    DA9030_ADC_TBAT_ENABLE | DA9030_ADC_VBAT_IN_TXON |
			    DA9030_ADC_VCH_ENABLE | DA9030_ADC_ICH_ENABLE |
			    DA9030_ADC_VBAT_ENABLE |
			    DA9030_ADC_AUTO_SLEEP_ENABLE);
}

static int da9030_battery_probe(struct platform_device *pdev)
{
	struct da9030_charger *charger;
	struct power_supply_config psy_cfg = {};
	struct da9030_battery_info *pdata = pdev->dev.platform_data;
	int ret;

	if (pdata == NULL)
		return -EINVAL;

	if (pdata->charge_milliamp >= 1500 ||
	    pdata->charge_millivolt < 4000 ||
	    pdata->charge_millivolt > 4350)
		return -EINVAL;

	charger = devm_kzalloc(&pdev->dev, sizeof(*charger), GFP_KERNEL);
	if (charger == NULL)
		return -ENOMEM;

	charger->master = pdev->dev.parent;

	/* 10 seconds between monitor runs unless platform defines other
	   interval */
	charger->interval = msecs_to_jiffies(
		(pdata->batmon_interval ? : 10) * 1000);

	charger->charge_milliamp = pdata->charge_milliamp;
	charger->charge_millivolt = pdata->charge_millivolt;
	charger->battery_info = pdata->battery_info;
	charger->battery_low = pdata->battery_low;
	charger->battery_critical = pdata->battery_critical;

	da9030_battery_convert_thresholds(charger, pdata);

	ret = da9030_battery_charger_init(charger);
	if (ret)
		goto err_charger_init;

	INIT_DELAYED_WORK(&charger->work, da9030_charging_monitor);
	schedule_delayed_work(&charger->work, charger->interval);

	charger->nb.notifier_call = da9030_battery_event;
	ret = da903x_register_notifier(charger->master, &charger->nb,
				       DA9030_EVENT_CHDET |
				       DA9030_EVENT_VBATMON |
				       DA9030_EVENT_CHIOVER |
				       DA9030_EVENT_TBAT);
	if (ret)
		goto err_notifier;

	da9030_battery_setup_psy(charger);
	psy_cfg.drv_data = charger;
	charger->psy = power_supply_register(&pdev->dev, &charger->psy_desc,
					     &psy_cfg);
	if (IS_ERR(charger->psy)) {
		ret = PTR_ERR(charger->psy);
		goto err_ps_register;
	}

	charger->debug_file = da9030_bat_create_debugfs(charger);
	platform_set_drvdata(pdev, charger);
	return 0;

err_ps_register:
	da903x_unregister_notifier(charger->master, &charger->nb,
				   DA9030_EVENT_CHDET | DA9030_EVENT_VBATMON |
				   DA9030_EVENT_CHIOVER | DA9030_EVENT_TBAT);
err_notifier:
	cancel_delayed_work(&charger->work);

err_charger_init:
	return ret;
}

static int da9030_battery_remove(struct platform_device *dev)
{
	struct da9030_charger *charger = platform_get_drvdata(dev);

	da9030_bat_remove_debugfs(charger);

	da903x_unregister_notifier(charger->master, &charger->nb,
				   DA9030_EVENT_CHDET | DA9030_EVENT_VBATMON |
				   DA9030_EVENT_CHIOVER | DA9030_EVENT_TBAT);
	cancel_delayed_work_sync(&charger->work);
	da9030_set_charge(charger, 0);
	power_supply_unregister(charger->psy);

	return 0;
}

static struct platform_driver da903x_battery_driver = {
	.driver	= {
		.name	= "da903x-battery",
	},
	.probe = da9030_battery_probe,
	.remove = da9030_battery_remove,
};

module_platform_driver(da903x_battery_driver);

MODULE_DESCRIPTION("DA9030 battery charger driver");
MODULE_AUTHOR("Mike Rapoport, CompuLab");
MODULE_LICENSE("GPL");
