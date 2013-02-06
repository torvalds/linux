/*
 * samsung_battery.c
 *
 * Copyright (C) 2011 Samsung Electronics
 * SangYoung Son <hello.son@samsung.com>
 *
 * based on sec_battery.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/reboot.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/android_alarm.h>
#include <linux/regulator/machine.h>
#include <linux/battery/samsung_battery.h>
#include <mach/regs-pmu.h>
#include "battery-factory.h"
#ifdef CONFIG_BATTERY_MAX77693_CHARGER
#include <linux/mfd/max77693-private.h>
#endif
#if defined(CONFIG_S3C_ADC)
#include <plat/adc.h>
#endif
#if defined(CONFIG_STMPE811_ADC)
#include <linux/stmpe811-adc.h>
#endif

static char *supply_list[] = {
	"battery",
};


#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
static void battery_error_control(struct battery_info *info);
#endif

/* Get LP charging mode state */
unsigned int lpcharge;
static int battery_get_lpm_state(char *str)
{
	get_option(&str, &lpcharge);
	pr_info("%s: Low power charging mode: %d\n", __func__, lpcharge);

	return lpcharge;
}
__setup("lpcharge=", battery_get_lpm_state);
#if defined(CONFIG_RTC_ALARM_BOOT)
EXPORT_SYMBOL(lpcharge);
#endif

/* Cable type from charger or adc */
static int battery_get_cable(struct battery_info *info)
{
	union power_supply_propval value;
	int cable_type = 0;
#if defined(EXTENDED_ONLINE_TYPE)
	int online_val;
#endif
	pr_debug("%s\n", __func__);

	mutex_lock(&info->ops_lock);

	switch (info->pdata->cb_det_src) {
	case CABLE_DET_CHARGER:
		info->psy_charger->get_property(info->psy_charger,
				POWER_SUPPLY_PROP_ONLINE, &value);

#if defined(EXTENDED_ONLINE_TYPE)
		/* | 31-24: RSVD | 23-16: MAIN TYPE |
			15-8: SUB TYPE | 7-0: POWER TYPE | */
		online_val = value.intval;
		online_val &= ~(ONLINE_TYPE_RSVD_MASK);
		cable_type = ((online_val & ONLINE_TYPE_MAIN_MASK) >>
						ONLINE_TYPE_MAIN_SHIFT);
		info->cable_sub_type = ((online_val & ONLINE_TYPE_SUB_MASK) >>
						ONLINE_TYPE_SUB_SHIFT);
		info->cable_pwr_type = ((online_val & ONLINE_TYPE_PWR_MASK) >>
						ONLINE_TYPE_PWR_SHIFT);
		pr_info("%s: main(%d), sub(%d), pwr(%d)\n", __func__,
						cable_type,
						info->cable_sub_type,
						info->cable_pwr_type);
#else
		cable_type = value.intval;
#endif
		break;
	default:
		pr_err("%s: not support src(%d)\n", __func__,
				info->pdata->cb_det_src);
		cable_type = POWER_SUPPLY_TYPE_BATTERY;
		break;
	}

	mutex_unlock(&info->ops_lock);

	return cable_type;
}

/* Temperature from fuelgauge or adc */
static int battery_get_temper(struct battery_info *info)
{
	union power_supply_propval value;
	int cnt, adc, adc_max, adc_min, adc_total;
	int temper = 300;
	int retry_cnt;
	pr_debug("%s\n", __func__);

	mutex_lock(&info->ops_lock);

	switch (info->pdata->temper_src) {
	case TEMPER_FUELGAUGE:
		info->psy_fuelgauge->get_property(info->psy_fuelgauge,
					  POWER_SUPPLY_PROP_TEMP, &value);
		temper = value.intval;
		break;
	case TEMPER_AP_ADC:
#if defined(CONFIG_S3C_ADC)
		adc = adc_max = adc_min = adc_total = 0;
		for (cnt = 0; cnt < CNT_ADC_SAMPLE; cnt++) {
			retry_cnt = 0;
			do {
				adc = s3c_adc_read(info->adc_client,
							info->pdata->temper_ch);
				if (adc < 0) {
					pr_info("%s: adc read(%d), retry(%d)",
						__func__, adc, retry_cnt++);
					msleep(ADC_ERR_DELAY);
				}
			} while (((adc < 0) && (retry_cnt <= ADC_ERR_CNT)));

			if (cnt != 0) {
				adc_max = MAX(adc, adc_max);
				adc_min = MIN(adc, adc_min);
			} else {
				adc_max = adc_min = adc;
			}

			adc_total += adc;
			pr_debug("%s: adc(%d), total(%d), max(%d), min(%d), "
					"avg(%d), cnt(%d)\n", __func__,
					adc, adc_total, adc_max, adc_min,
					adc_total / (cnt + 1),  cnt + 1);
		}

		info->battery_temper_adc =
			(adc_total - adc_max - adc_min) / (CNT_ADC_SAMPLE - 2);

		if (info->battery_temper_adc < 0) {
			pr_info("%s: adc read error(%d), temper set as 30.0",
					__func__, info->battery_temper_adc);
			temper = 300;
		} else {
			temper = info->pdata->covert_adc(
					info->battery_temper_adc,
					info->pdata->temper_ch);
		}
#endif
		break;
	case TEMPER_EXT_ADC:
#if defined(CONFIG_STMPE811_ADC)
		temper = stmpe811_get_adc_value(info->pdata->temper_ch);
#endif
		break;
	case TEMPER_UNKNOWN:
	default:
		pr_info("%s: invalid temper src(%d)\n", __func__,
					info->pdata->temper_src);
		temper = 300;
		break;
	}

	pr_debug("%s: temper(%d), source(%d)\n", __func__,
			temper, info->pdata->temper_src);

	mutex_unlock(&info->ops_lock);
	return temper;
}

#define ADC_REG_NAME	"vcc_adc_1.8v"
static int battery_set_adc_power(struct battery_info *info, bool en)
{
	struct regulator *regulator;
	int is_en;
	int ret = 0;
	pr_debug("%s\n", __func__);

	regulator = regulator_get(NULL, ADC_REG_NAME);
	if (IS_ERR(regulator))
		return -ENODEV;

	is_en = regulator_is_enabled(regulator);

	if (is_en != en)
		pr_info("%s: %s: is_en(%d), en(%d)\n", __func__,
					ADC_REG_NAME, is_en, en);

	if (!is_en && en)
		ret = regulator_enable(regulator);
	else if (is_en && !en)
		ret = regulator_force_disable(regulator);

	info->adc_pwr_st = en;

	regulator_put(regulator);

	return ret;
}

static int battery_get_vf(struct battery_info *info)
{
	union power_supply_propval value;
	int present = 0;
	int adc;
	pr_debug("%s\n", __func__);

	if (info->factory_mode) {
		pr_debug("%s: No need to check battery in factory mode\n",
			__func__);
		return 1;
	}

	mutex_lock(&info->ops_lock);

	switch (info->pdata->vf_det_src) {
	case VF_DET_ADC:
#if defined(CONFIG_S3C_ADC)
		if (info->pdata->vf_det_src == VF_DET_ADC)
			battery_set_adc_power(info, 1);
		adc = s3c_adc_read(info->adc_client, info->pdata->vf_det_ch);
		if (info->pdata->vf_det_src == VF_DET_ADC)
			battery_set_adc_power(info, 0);
#else
		adc = 350;	/* temporary value */
#endif
		info->battery_vf_adc = adc;
		present = INRANGE(adc, info->pdata->vf_det_th_l,
					info->pdata->vf_det_th_h);
		if (!present)
			pr_info("%s: adc(%d), out of range(%d ~ %d)\n",
						__func__, adc,
						info->pdata->vf_det_th_l,
						info->pdata->vf_det_th_h);
		break;
	case VF_DET_CHARGER:
		info->psy_charger->get_property(info->psy_charger,
					POWER_SUPPLY_PROP_PRESENT, &value);
		present = value.intval;
		break;
	case VF_DET_GPIO:
		present = !gpio_get_value(info->batdet_gpio);
		break;
	default:
		pr_err("%s: not support src(%d)\n", __func__,
					info->pdata->vf_det_src);
		present = 1;	/* always detected */
		break;
	}

	pr_debug("%s: present(%d)\n", __func__, present);

	mutex_unlock(&info->ops_lock);
	return present;
}

/* judge power off or not by current_avg */
static int battery_get_curr_avg(struct battery_info *info)
{
	int curr_avg;
	pr_debug("%s\n", __func__);

	/* if 0% && under min voltage && low power charging, power off */
	if ((info->battery_soc <= PWROFF_SOC) &&
		(info->battery_vcell < info->pdata->voltage_min) &&
		(info->battery_v_diff < 0) &&
		(info->input_current < info->pdata->chg_curr_ta)) {
		pr_info("%s: soc(%d), vol(%d < %d), diff(%d), in_curr(%d)\n",
					__func__, info->battery_soc,
					(info->battery_vcell / 1000),
					(info->pdata->voltage_min / 1000),
					info->battery_v_diff,
					info->input_current);
		curr_avg = -1;
	} else {
		curr_avg = info->input_current;
	}

	return curr_avg;
}

/* Get info from power supply at realtime */
int battery_get_info(struct battery_info *info,
		     enum power_supply_property property)
{
	union power_supply_propval value;
	value.intval = 0;

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	/* do nothing */
#else
	if (info->battery_error_test) {
		pr_info("%s: in test mode(%d), do not update\n", __func__,
			info->battery_error_test);
		return -EPERM;
	}
#endif

	switch (property) {
	/* Update from charger */
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
	case POWER_SUPPLY_PROP_HEALTH:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		info->psy_charger->get_property(info->psy_charger,
						property, &value);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		value.intval = battery_get_vf(info);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		value.intval = battery_get_cable(info);
		break;
	/* Update from fuelgauge */
	case POWER_SUPPLY_PROP_CAPACITY:	/* Only Adjusted SOC */
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:	/* Only VCELL */
		info->psy_fuelgauge->get_property(info->psy_fuelgauge,
						  property, &value);
		break;
	/* Update current_avg */
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		value.intval = battery_get_curr_avg(info);
		break;
	/* Update from fuelgauge or adc */
	case POWER_SUPPLY_PROP_TEMP:
		value.intval = battery_get_temper(info);
		break;
	default:
		break;
	}

	return value.intval;
}

/* Update all values for battery */
void battery_update_info(struct battery_info *info)
{
	union power_supply_propval value;
	int temper;

	/* Update from Charger */
	if (info->slate_mode)
		info->cable_type = POWER_SUPPLY_TYPE_BATTERY;
	else
		info->cable_type = battery_get_cable(info);

	info->psy_charger->get_property(info->psy_charger,
					POWER_SUPPLY_PROP_STATUS, &value);
	info->charge_real_state = info->charge_virt_state = value.intval;

	info->psy_charger->get_property(info->psy_charger,
					POWER_SUPPLY_PROP_CHARGE_TYPE, &value);
	info->charge_type = value.intval;

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)\
	|| defined(CONFIG_MACH_M0_CMCC)
	/* temperature error is higher priority */
	if (!info->temper_state) {
		info->psy_charger->get_property(info->psy_charger,
					POWER_SUPPLY_PROP_HEALTH, &value);
		info->battery_health = value.intval;
	}
#else
	info->psy_charger->get_property(info->psy_charger,
					POWER_SUPPLY_PROP_HEALTH, &value);
	info->battery_health = value.intval;
#endif

	info->battery_present = battery_get_vf(info);

	info->psy_charger->get_property(info->psy_charger,
					POWER_SUPPLY_PROP_CURRENT_NOW, &value);
	info->charge_current = value.intval;

	info->psy_charger->get_property(info->psy_charger,
					POWER_SUPPLY_PROP_CURRENT_MAX, &value);
	info->input_current = value.intval;

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	if (info->cable_type == POWER_SUPPLY_TYPE_BATTERY) {
		info->battery_health = POWER_SUPPLY_HEALTH_GOOD;
		info->battery_present = 1;
	}
#endif

	/* Fuelgauge power off state */
	if ((info->cable_type != POWER_SUPPLY_TYPE_BATTERY) &&
	    (info->battery_present == 0) && (info->monitor_count)) {
		pr_info("%s: abnormal fuelgauge power state\n", __func__);
		goto update_finish;
	}

	/* Update from Fuelgauge */
	value.intval = SOC_TYPE_ADJUSTED;
	info->psy_fuelgauge->get_property(info->psy_fuelgauge,
					  POWER_SUPPLY_PROP_CAPACITY, &value);

	if ((info->cable_type == POWER_SUPPLY_TYPE_BATTERY) &&
		(info->battery_soc < value.intval) && (info->monitor_count))
		pr_info("%s: new soc(%d) is bigger than prev soc(%d)"
					" in discharging state\n", __func__,
					value.intval, info->battery_soc);
	else
		info->battery_soc = value.intval;

	value.intval = SOC_TYPE_RAW;
	info->psy_fuelgauge->get_property(info->psy_fuelgauge,
					  POWER_SUPPLY_PROP_CAPACITY, &value);
	info->battery_r_s_delta = value.intval - info->battery_raw_soc;
	info->battery_raw_soc = value.intval;

	value.intval = SOC_TYPE_FULL;
	info->psy_fuelgauge->get_property(info->psy_fuelgauge,
					  POWER_SUPPLY_PROP_CAPACITY, &value);
	info->battery_full_soc = value.intval;

	value.intval = VOLTAGE_TYPE_VCELL;
	info->psy_fuelgauge->get_property(info->psy_fuelgauge,
					  POWER_SUPPLY_PROP_VOLTAGE_NOW,
					  &value);
	info->battery_vcell = value.intval;

	value.intval = VOLTAGE_TYPE_VFOCV;
	info->psy_fuelgauge->get_property(info->psy_fuelgauge,
					  POWER_SUPPLY_PROP_VOLTAGE_NOW,
					  &value);
	info->battery_vfocv = value.intval;
	info->battery_v_diff = info->battery_vcell - info->battery_vfocv;

	temper = battery_get_temper(info);
	info->battery_t_delta = temper - info->battery_temper;
	info->battery_temper = temper;

	/* update current_avg later */
	info->charge_current_avg = battery_get_curr_avg(info);

update_finish:
	switch (info->battery_error_test) {
	case 0:
		pr_debug("%s: error test: not test modde\n", __func__);
#if defined(CONFIG_TARGET_LOCALE_KOR)
		info->errortest_stopcharging = false;
#endif
		break;
	case 1:
		pr_info("%s: error test: full charged\n", __func__);
		info->charge_real_state = POWER_SUPPLY_STATUS_FULL;
		info->battery_vcell = info->pdata->voltage_max;
		break;
	case 2:
		pr_info("%s: error test: freezed\n", __func__);
		info->battery_temper = info->pdata->freeze_stop_temp - 10;
		break;
	case 3:
		pr_info("%s: error test: overheated\n", __func__);
		info->battery_temper = info->pdata->overheat_stop_temp + 10;
		break;
	case 4:
		pr_info("%s: error test: ovp\n", __func__);
		break;
	case 5:
		pr_info("%s: error test: vf error\n", __func__);
		info->battery_present = 0;
		break;
#if defined(CONFIG_TARGET_LOCALE_KOR)
	case 6:
		info->errortest_stopcharging = true;
		break;
#endif
	default:
		pr_info("%s: error test: unknown state\n", __func__);
		break;
	}

	pr_debug("%s: state(%d), type(%d), "
		 "health(%d), present(%d), "
		 "cable(%d), curr(%d), "
		 "soc(%d), raw(%d), "
		 "vol(%d), ocv(%d), tmp(%d)\n", __func__,
		 info->charge_real_state, info->charge_type,
		 info->battery_health, info->battery_present,
		 info->cable_type, info->charge_current,
		 info->battery_soc, info->battery_raw_soc,
		 info->battery_vcell, info->battery_vfocv,
		 info->battery_temper);
}

/* Control charger and fuelgauge */
void battery_control_info(struct battery_info *info,
			  enum power_supply_property property, int intval)
{
	union power_supply_propval value;

	value.intval = intval;

	switch (property) {
	/* Control to charger */
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
#if defined(CONFIG_CHARGER_MAX8922_U1)
		info->psy_sub_charger->set_property(info->psy_sub_charger,
						property, &value);
#else
		info->psy_charger->set_property(info->psy_charger,
						property, &value);
#endif
		break;

	/* Control to fuelgauge */
	case POWER_SUPPLY_PROP_CAPACITY:
		info->psy_fuelgauge->set_property(info->psy_fuelgauge,
						  property, &value);
		break;
	default:
		break;
	}
}

static void battery_event_alarm(struct alarm *alarm)
{
	struct battery_info *info = container_of(alarm, struct battery_info,
								 event_alarm);
	pr_info("%s: exit event state, %ds gone\n", __func__,
				info->pdata->event_time);

	/* clear event state */
	info->event_state = EVENT_STATE_CLEAR;

	wake_lock(&info->monitor_wake_lock);
	schedule_work(&info->monitor_work);
}

void battery_event_control(struct battery_info *info)
{
	int event_num;
	ktime_t interval, next, slack;
	/* sync with event_type in samsung_battery.h */
	char *event_type_name[] = { "WCDMA CALL", "GSM CALL", "CALL",
					"VIDEO", "MUSIC", "BROWSER",
					"HOTSPOT", "CAMERA", "DATA CALL",
					"GPS", "LTE", "WIFI",
					"USE", "UNKNOWN"
	};

	pr_debug("%s\n", __func__);

	if (info->event_type) {
		pr_info("%s: in event state(%d), type(0x%04x)\n", __func__,
					info->event_state, info->event_type);

		for (event_num = 0; event_num < EVENT_TYPE_MAX; event_num++) {
			if (info->event_type & (1 << event_num))
				pr_info("%s: %d: %s\n", __func__, event_num,
						event_type_name[event_num]);
		}

		if (info->event_state == EVENT_STATE_SET) {
			pr_info("%s: event already set, event(%d, 0x%04x)\n",
				__func__, info->event_state, info->event_type);
		} else if (info->event_state == EVENT_STATE_IN_TIMER) {
			pr_info("%s: cancel event timer\n", __func__);

			alarm_cancel(&info->event_alarm);

			info->event_state = EVENT_STATE_SET;

			wake_lock(&info->monitor_wake_lock);
			schedule_work(&info->monitor_work);
		} else {
			pr_info("%s: enter event state(%d, 0x%04x)\n",
				__func__, info->event_state, info->event_type);

			info->event_state = EVENT_STATE_SET;

			wake_lock(&info->monitor_wake_lock);
			schedule_work(&info->monitor_work);
		}
	} else {
		pr_info("%s: clear event type(0x%04x), wait %ds\n", __func__,
				info->event_type, info->pdata->event_time);

		if (info->event_state == EVENT_STATE_SET) {
			pr_info("%s: start event timer\n", __func__);
			info->last_poll = alarm_get_elapsed_realtime();

			interval = ktime_set(info->pdata->event_time, 0);
			next = ktime_add(info->last_poll, interval);
			slack = ktime_set(20, 0);

			alarm_start_range(&info->event_alarm, next,
						ktime_add(next, slack));

			info->event_state = EVENT_STATE_IN_TIMER;
		} else {
			pr_info("%s: event already clear, event(%d, 0x%04x)\n",
				__func__, info->event_state, info->event_type);
		}
	}
}

static void battery_notify_full_state(struct battery_info *info)
{
	union power_supply_propval value;
	pr_debug("%s: r(%d), f(%d), rs(%d), fs(%d), s(%d)\n", __func__,
			info->recharge_phase, info->full_charged_state,
			info->battery_raw_soc, info->battery_full_soc,
						info->battery_soc);

	if ((info->recharge_phase && info->full_charged_state) ||
		((info->charge_real_state != POWER_SUPPLY_STATUS_DISCHARGING) &&
		(info->battery_raw_soc > info->battery_full_soc) &&
		(info->battery_soc == 100))) {
		/* notify full state to fuel guage */
		value.intval = POWER_SUPPLY_STATUS_FULL;
		info->psy_fuelgauge->set_property(info->psy_fuelgauge,
			POWER_SUPPLY_PROP_STATUS, &value);
	}
}

static void battery_monitor_alarm(struct alarm *alarm)
{
	struct battery_info *info = container_of(alarm, struct battery_info,
								 monitor_alarm);
	pr_debug("%s\n", __func__);

	wake_lock(&info->monitor_wake_lock);
	schedule_work(&info->monitor_work);
}

static void battery_monitor_interval(struct battery_info *info)
{
	ktime_t interval, next, slack;
	unsigned long flags;
	pr_debug("%s\n", __func__);

	local_irq_save(flags);

	info->last_poll = alarm_get_elapsed_realtime();

	switch (info->monitor_mode) {
	case MONITOR_CHNG:
		info->monitor_interval = info->pdata->chng_interval;
		break;
	case MONITOR_CHNG_SUSP:
		info->monitor_interval = info->pdata->chng_susp_interval;
		break;
	case MONITOR_NORM:
		info->monitor_interval = info->pdata->norm_interval;
		break;
	case MONITOR_NORM_SUSP:
		info->monitor_interval = info->pdata->norm_susp_interval;
		break;
	case MONITOR_EMER_LV1:
		info->monitor_interval = info->pdata->emer_lv1_interval;
		break;
	case MONITOR_EMER_LV2:
		info->monitor_interval = info->pdata->emer_lv2_interval;
		break;
	default:
		info->monitor_interval = info->pdata->norm_interval;
		break;
	}

	/* 5 times after boot, apply no interval (1 sec) */
	if (info->monitor_count < 5)
		info->monitor_interval = 1;

	/* apply monitor interval weight */
	if (info->monitor_weight != 100) {
		pr_info("%s: apply weight(%d), %d -> %d\n", __func__,
			info->monitor_weight, info->monitor_interval,
			(info->monitor_interval * info->monitor_weight / 100));
		info->monitor_interval *= info->monitor_weight;
		info->monitor_interval /= 100;
	}

	pr_debug("%s: monitor mode(%d), interval(%d)\n", __func__,
		info->monitor_mode, info->monitor_interval);

	interval = ktime_set(info->monitor_interval, 0);
	next = ktime_add(info->last_poll, interval);
	slack = ktime_set(20, 0);

	alarm_start_range(&info->monitor_alarm, next, ktime_add(next, slack));

	local_irq_restore(flags);
}

static bool battery_recharge_cond(struct battery_info *info)
{
	pr_debug("%s\n", __func__);

	if (info->charge_real_state == POWER_SUPPLY_STATUS_CHARGING) {
		pr_debug("%s: r_state chargng, cs(%d)\n", __func__,
					info->charge_real_state);
		return false;
	}

	if (info->battery_vcell < info->pdata->recharge_voltage) {
		pr_info("%s: recharge state, vcell(%d ? %d)\n", __func__,
			info->battery_vcell, info->pdata->recharge_voltage);
		return true;
	} else
		pr_debug("%s: not recharge state, vcell(%d ? %d)\n", __func__,
			info->battery_vcell, info->pdata->recharge_voltage);

	return false;
}

static bool battery_abstimer_cond(struct battery_info *info)
{
	unsigned int abstimer_duration;
	ktime_t ktime;
	struct timespec current_time;
	pr_debug("%s\n", __func__);

	/* always update time for info data */
	ktime = alarm_get_elapsed_realtime();
	info->current_time = current_time = ktime_to_timespec(ktime);

	if ((info->cable_type == POWER_SUPPLY_TYPE_USB) ||
		(info->full_charged_state != STATUS_NOT_FULL) ||
		(info->charge_start_time == 0)) {
		pr_debug("%s: not abstimer state, cb(%d), f(%d), t(%d)\n",
						__func__, info->cable_type,
						info->full_charged_state,
						info->charge_start_time);
		info->abstimer_state = false;
		return false;
	}

	if (info->recharge_phase) {
		abstimer_duration = info->pdata->abstimer_recharge_duration;
	} else {
		if (info->cable_type == POWER_SUPPLY_TYPE_WIRELESS)
			abstimer_duration =
				info->pdata->abstimer_charge_duration_wpc;
		else
			abstimer_duration =
				info->pdata->abstimer_charge_duration;
	}

	if ((current_time.tv_sec - info->charge_start_time) >
	    abstimer_duration) {
		pr_info("%s: abstimer state, t(%d - %d ?? %d)\n", __func__,
			(int)current_time.tv_sec, info->charge_start_time,
							abstimer_duration);
		info->abstimer_state = true;
		info->abstimer_active = (int)current_time.tv_sec;
	} else {
		pr_debug("%s: not abstimer state, t(%d - %d ?? %d)\n", __func__,
			(int)current_time.tv_sec, info->charge_start_time,
							abstimer_duration);
		info->abstimer_state = false;
	}

	return info->abstimer_state;
}

static bool battery_fullcharged_cond(struct battery_info *info)
{
	int f_cond_soc;
	int f_cond_vcell;
	int full_state;
	pr_debug("%s\n", __func__);

	/* max voltage - RECHG_DROP_VALUE: recharge voltage */
	f_cond_vcell = info->pdata->voltage_max - RECHG_DROP_VALUE;
	/* max soc - 5% */
	f_cond_soc = 95;

	pr_debug("%s: cs(%d ? %d), v(%d ? %d), s(%d ? %d)\n", __func__,
			info->charge_real_state, POWER_SUPPLY_STATUS_FULL,
			info->battery_vcell, f_cond_vcell,
			info->battery_soc, f_cond_soc);

	if (info->charge_real_state == POWER_SUPPLY_STATUS_FULL) {
		if ((info->battery_vcell > f_cond_vcell) &&
		    (info->battery_soc > f_cond_soc)) {
			pr_info("%s: real full charged, v(%d), s(%d)\n",
					__func__, info->battery_vcell,
						info->battery_soc);
#if defined(USE_2STEP_TERM)
			full_state = battery_get_info(info,
					POWER_SUPPLY_PROP_CHARGE_FULL);
			if (!full_state) {
				if (info->full_charged_state != STATUS_1ST_FULL)
					pr_info("%s: 1st full by current\n",
								__func__);

				info->full_charged_state = STATUS_1ST_FULL;

				return false;
			} else {
				if (info->full_charged_state != STATUS_2ND_FULL)
					pr_info("%s: 2nd full by timer\n",
								__func__);

				info->full_charged_state = STATUS_2ND_FULL;

				return true;
			}
#else
			info->full_charged_state = STATUS_1ST_FULL;
			return true;
#endif
		} else {
			pr_info("%s: not real full charged, v(%d), s(%d)\n",
					__func__, info->battery_vcell,
						info->battery_soc);

			/* restart charger */
			battery_control_info(info,
					     POWER_SUPPLY_PROP_STATUS,
					     DISABLE);
			msleep(100);
			battery_control_info(info,
					     POWER_SUPPLY_PROP_STATUS,
					     ENABLE);

			info->charge_real_state = info->charge_virt_state =
						battery_get_info(info,
						POWER_SUPPLY_PROP_STATUS);
			return false;
		}
	} else if (info->full_charged_state != STATUS_NOT_FULL) {
		pr_debug("%s: already full charged, v(%d), s(%d)\n", __func__,
				info->battery_vcell, info->battery_soc);
	} else {
		pr_debug("%s: not full charged, v(%d), s(%d)\n", __func__,
				info->battery_vcell, info->battery_soc);
		info->full_charged_state = STATUS_NOT_FULL;
	}

	return false;
}

static bool battery_vf_cond(struct battery_info *info)
{
	pr_debug("%s\n", __func__);

#if defined(CONFIG_MACH_P11) || defined(CONFIG_MACH_P10)
	/* FIXME: fix P11 build error temporarily */
#else
	/* jig detect by MUIC */
	if (is_jig_attached == JIG_ON) {
		pr_info("%s: JIG ON, do not check\n", __func__);
		info->vf_state = false;
		return false;
	}
#endif

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	if (info->cable_type == POWER_SUPPLY_TYPE_BATTERY) {
		info->vf_state = false;
		return false;
	}
#endif

	/* real time state */
	info->battery_present =
		battery_get_info(info, POWER_SUPPLY_PROP_PRESENT);

	if (info->battery_present == 0) {
		pr_info("%s: battery(%d) is not detected\n", __func__,
						info->battery_present);
		info->vf_state = true;
	} else {
		pr_debug("%s: battery(%d) is detected\n", __func__,
						info->battery_present);
		info->vf_state = false;
	}

	return info->vf_state;
}

static bool battery_health_cond(struct battery_info *info)
{
	pr_debug("%s\n", __func__);

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	if (info->cable_type == POWER_SUPPLY_TYPE_BATTERY) {
		info->health_state = false;
		return false;
	}
#endif

	/* temperature error is higher priority */
	if (info->temper_state == true) {
		pr_info("%s: temper stop state, do not check\n", __func__);
		return false;
	}

	/* real time state */
	info->battery_health =
		battery_get_info(info, POWER_SUPPLY_PROP_HEALTH);

	if (info->battery_health == POWER_SUPPLY_HEALTH_UNSPEC_FAILURE) {
		pr_info("%s: battery unspec(%d)\n", __func__,
					info->battery_health);
		info->health_state = true;
	} else if (info->battery_health == POWER_SUPPLY_HEALTH_DEAD) {
		pr_info("%s: battery dead(%d)\n", __func__,
					info->battery_health);
		info->health_state = true;
	} else if (info->battery_health == POWER_SUPPLY_HEALTH_OVERVOLTAGE) {
		pr_info("%s: battery overvoltage(%d)\n", __func__,
					info->battery_health);
		info->health_state = true;
	} else if (info->battery_health == POWER_SUPPLY_HEALTH_UNDERVOLTAGE) {
		pr_info("%s: battery undervoltage(%d)\n", __func__,
					info->battery_health);
		info->health_state = true;
	} else {
		pr_debug("%s: battery good(%d)\n", __func__,
					info->battery_health);
		info->health_state = false;

	}

	return info->health_state;
}

static bool battery_temper_cond(struct battery_info *info)
{
	int ovh_stop, ovh_recover;
	int frz_stop, frz_recover;
	pr_debug("%s\n", __func__);

	/* update overheat temperature threshold */
	if ((info->pdata->ctia_spec == true) && (info->lpm_state)) {
		ovh_stop = info->pdata->lpm_overheat_stop_temp;
		ovh_recover = info->pdata->lpm_overheat_recovery_temp;
		frz_stop = info->pdata->lpm_freeze_stop_temp;
		frz_recover = info->pdata->lpm_freeze_recovery_temp;
	} else if ((info->pdata->ctia_spec == true) &&
			(info->event_state != EVENT_STATE_CLEAR)) {
		ovh_stop = info->pdata->event_overheat_stop_temp;
		ovh_recover = info->pdata->event_overheat_recovery_temp;
		frz_stop = info->pdata->event_freeze_stop_temp;
		frz_recover = info->pdata->event_freeze_recovery_temp;
		pr_info("%s: ovh(%d/%d), frz(%d/%d), lpm(%d), "
			"event(%d, 0x%04x)\n", __func__,
			ovh_stop, ovh_recover, frz_stop, frz_recover,
			info->lpm_state, info->event_state, info->event_type);
	} else {
		ovh_stop = info->pdata->overheat_stop_temp;
		ovh_recover = info->pdata->overheat_recovery_temp;
		frz_stop = info->pdata->freeze_stop_temp;
		frz_recover = info->pdata->freeze_recovery_temp;
	}

#if defined(CONFIG_MACH_T0_USA_SPR)
	/* unver rev0.7, do not stop charging by tempereture */
	if (system_rev < 7) {
		ovh_stop = info->battery_temper + 1;
		frz_stop = info->battery_temper - 1;
	}
#endif

	if (info->temper_state == false) {
		if (info->charge_real_state != POWER_SUPPLY_STATUS_CHARGING) {
			pr_debug("%s: r_state !charging, cs(%d)\n",
					__func__, info->charge_real_state);
			return false;
		}

		pr_debug("%s: check charging stop temper "
			 "cond: %d ?? %d ~ %d\n", __func__,
			 info->battery_temper, frz_stop, ovh_stop);

		if (info->battery_temper >= ovh_stop) {
			pr_info("%s: stop by overheated, t(%d ? %d)\n",
				__func__, info->battery_temper, ovh_stop);
			info->overheated_state = true;
		} else if (info->battery_temper <= frz_stop) {
			pr_info("%s: stop by freezed, t(%d ? %d)\n",
				__func__, info->battery_temper, frz_stop);
			info->freezed_state = true;
		} else
			pr_debug("%s: normal charging, t(%d)\n", __func__,
							info->battery_temper);
	} else {
		pr_debug("%s: check charging recovery temper "
			 "cond: %d ?? %d ~ %d\n", __func__,
			 info->battery_temper, frz_recover, ovh_recover);

		if ((info->overheated_state == true) &&
		    (info->battery_temper <= ovh_recover)) {
			pr_info("%s: recovery from overheated, t(%d ? %d)\n",
				__func__, info->battery_temper, ovh_recover);
			info->overheated_state = false;
		} else if ((info->freezed_state == true) &&
			   (info->battery_temper >= frz_recover)) {
			pr_info("%s: recovery from freezed, t(%d ? %d)\n",
				__func__, info->battery_temper, frz_recover);
			info->freezed_state = false;
		} else
			pr_info("%s: charge stopped, t(%d)\n", __func__,
							info->battery_temper);
	}

	if (info->overheated_state == true) {
		info->battery_health = POWER_SUPPLY_HEALTH_OVERHEAT;
		info->freezed_state = false;
		info->temper_state = true;
	} else if (info->freezed_state == true) {
		info->battery_health = POWER_SUPPLY_HEALTH_COLD;
		info->overheated_state = false;
		info->temper_state = true;
	} else {
#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
		info->battery_health = POWER_SUPPLY_HEALTH_GOOD;
#endif
		info->overheated_state = false;
		info->freezed_state = false;
		info->temper_state = false;
	}

	return info->temper_state;
}

static void battery_charge_control(struct battery_info *info,
				unsigned int chg_curr, unsigned int in_curr)
{
	int charge_state;
	ktime_t ktime;
	struct timespec current_time;
	pr_debug("%s, chg(%d), in(%d)\n", __func__, chg_curr, in_curr);

	mutex_lock(&info->ops_lock);

	ktime = alarm_get_elapsed_realtime();
	current_time = ktime_to_timespec(ktime);

	if ((info->cable_type != POWER_SUPPLY_TYPE_BATTERY) &&
		(chg_curr > 0) && (info->siop_state == true)) {

		switch (info->siop_lv) {
		case SIOP_LV1:
			info->siop_charge_current =
				info->pdata->chg_curr_siop_lv1;
			break;
		case SIOP_LV2:
			info->siop_charge_current =
				info->pdata->chg_curr_siop_lv2;
			break;
		case SIOP_LV3:
			info->siop_charge_current =
				info->pdata->chg_curr_siop_lv3;
			break;
		default:
			info->siop_charge_current =
				info->pdata->chg_curr_usb;
			break;
		}

		chg_curr = MIN(chg_curr, info->siop_charge_current);
		pr_info("%s: siop state, level(%d), cc(%d)\n",
				__func__, info->siop_lv, chg_curr);
	}

	if (in_curr == KEEP_CURR)
		goto charge_current_con;

	/* input current limit */
	in_curr = MIN(in_curr, info->pdata->in_curr_limit);

	/* check charge input before and after */
	if (info->input_current == ((in_curr / 20) * 20)) {
		/*
		 * (current / 20) is converted value
		 * for register setting.
		 * (register current * 20) is actual value
		 * for input current
		 */
		pr_debug("%s: same input current: %dmA\n", __func__,  in_curr);
	} else {
		battery_control_info(info,
				     POWER_SUPPLY_PROP_CURRENT_MAX,
				     in_curr);
		info->input_current =
			battery_get_info(info, POWER_SUPPLY_PROP_CURRENT_MAX);

		pr_debug("%s: update input current: %dmA\n", __func__, in_curr);
	}

charge_current_con:
	if (chg_curr == KEEP_CURR)
		goto charge_state_con;

	/* check charge current before and after */
	if (info->charge_current == ((chg_curr * 3 / 100) * 333 / 10)) {
		/*
		 * (current * 3 / 100) is converted value
		 * for register setting.
		 * (register current * 333 / 10) is actual value
		 * for charge current
		 */
		pr_debug("%s: same charge current: %dmA\n",
					__func__, chg_curr);
	} else {
		battery_control_info(info,
				     POWER_SUPPLY_PROP_CURRENT_NOW,
				     chg_curr);
		info->charge_current =
			battery_get_info(info, POWER_SUPPLY_PROP_CURRENT_NOW);

		pr_debug("%s: update charge current: %dmA\n",
					__func__, chg_curr);
	}

charge_state_con:
	/* control charger control only, buck is on by default */
	if ((chg_curr != 0) && (info->charge_start_time == 0)) {
		battery_control_info(info, POWER_SUPPLY_PROP_STATUS, ENABLE);

		info->charge_start_time = current_time.tv_sec;
		pr_info("%s: charge enabled, current as %d/%dmA @%d\n",
			__func__, info->charge_current, info->input_current,
			info->charge_start_time);

		charge_state = battery_get_info(info, POWER_SUPPLY_PROP_STATUS);

		if (charge_state != POWER_SUPPLY_STATUS_CHARGING) {
			pr_info("%s: force set v_state as charging\n",
								__func__);
			info->charge_real_state = charge_state;
			info->charge_virt_state = POWER_SUPPLY_STATUS_CHARGING;
			info->ambiguous_state = true;
		} else {
			info->charge_real_state = info->charge_virt_state =
								charge_state;
			info->ambiguous_state = false;
		}
	} else if ((chg_curr == 0) && (info->charge_start_time != 0)) {
		battery_control_info(info, POWER_SUPPLY_PROP_STATUS, DISABLE);

		pr_info("%s: charge disabled, current as %d/%dmA @%d\n",
			__func__, info->charge_current, info->input_current,
			(int)current_time.tv_sec);

		info->charge_start_time = 0;

		charge_state = battery_get_info(info, POWER_SUPPLY_PROP_STATUS);

		if (charge_state != POWER_SUPPLY_STATUS_DISCHARGING) {
			pr_info("%s: force set v_state as discharging\n",
								__func__);
			info->charge_real_state = charge_state;
			info->charge_virt_state =
						POWER_SUPPLY_STATUS_DISCHARGING;
			info->ambiguous_state = true;
		} else {
			info->charge_real_state = info->charge_virt_state =
								charge_state;
			info->ambiguous_state = false;
		}
	} else {
		pr_debug("%s: same charge state(%s), current as %d/%dmA @%d\n",
			__func__, ((chg_curr != 0) ? "enabled" : "disabled"),
				info->charge_current, info->input_current,
				info->charge_start_time);

		/* release ambiguous state */
		if ((info->ambiguous_state == true) &&
			(info->charge_real_state == info->charge_virt_state)) {
			pr_debug("%s: release ambiguous state, s(%d)\n",
					__func__, info->charge_real_state);
			info->ambiguous_state = false;
		}
	}

	mutex_unlock(&info->ops_lock);
}

/* charge state for UI(icon) */
static void battery_indicator_icon(struct battery_info *info)
{
	if (info->cable_type != POWER_SUPPLY_TYPE_BATTERY) {
		if (info->full_charged_state != STATUS_NOT_FULL) {
			info->charge_virt_state =
				POWER_SUPPLY_STATUS_FULL;
			info->battery_soc = 100;
		} else if (info->abstimer_active) {
			if (info->battery_soc == 100)
				info->charge_virt_state =
					POWER_SUPPLY_STATUS_FULL;
			else
				info->charge_virt_state =
					POWER_SUPPLY_STATUS_CHARGING;
		} else if (info->recharge_phase == true) {
			info->charge_virt_state =
				POWER_SUPPLY_STATUS_CHARGING;
		}

		if (info->temper_state == true) {
			info->charge_virt_state =
				POWER_SUPPLY_STATUS_NOT_CHARGING;
		}

		if (info->vf_state == true) {
			info->charge_virt_state =
				POWER_SUPPLY_STATUS_NOT_CHARGING;
			info->battery_health =
				POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		}

		if (info->health_state == true) {
			/* ovp is not 'NOT_CHARGING' */
			if (info->battery_health ==
						POWER_SUPPLY_HEALTH_OVERVOLTAGE)
				info->charge_virt_state =
					POWER_SUPPLY_STATUS_DISCHARGING;
			else
				info->charge_virt_state =
					POWER_SUPPLY_STATUS_NOT_CHARGING;
		}
	}
}

/* charge state for LED */
static void battery_indicator_led(struct battery_info *info)
{

	if (info->charge_virt_state ==
			POWER_SUPPLY_STATUS_CHARGING) {
		if (info->led_state != BATT_LED_CHARGING) {
			/* TODO: for kernel LED control: CHARGING */
			info->led_state = BATT_LED_CHARGING;
		}
	} else if (info->charge_virt_state ==
			POWER_SUPPLY_STATUS_NOT_CHARGING) {
		if (info->led_state != BATT_LED_NOT_CHARGING) {
			/* TODO: for kernel LED control: NOT CHARGING */
			info->led_state = BATT_LED_NOT_CHARGING;
		}
	} else if (info->charge_virt_state ==
			POWER_SUPPLY_STATUS_FULL) {
		if (info->led_state != BATT_LED_FULL) {
			/* TODO: for kernel LED control: FULL */
			info->led_state = BATT_LED_FULL;
		}
	} else {
		if (info->led_state != BATT_LED_DISCHARGING) {
			/* TODO: for kernel LED control: DISCHARGING */
			info->led_state = BATT_LED_DISCHARGING;
		}
	}
}

/* dynamic battery polling interval */
static void battery_interval_calulation(struct battery_info *info)
{
	pr_debug("%s\n", __func__);

	/* init monitor interval weight */
	info->monitor_weight = 100;

	/* ambiguous state */
	if (info->ambiguous_state == true) {
		pr_info("%s: ambiguous state\n", __func__);
		info->monitor_mode = MONITOR_EMER_LV2;
		wake_lock(&info->emer_wake_lock);
		return;
	} else {
		pr_debug("%s: not ambiguous state\n", __func__);
		wake_unlock(&info->emer_wake_lock);
	}

	/* prevent critical low raw soc factor */
	if (info->battery_raw_soc < 100) {
		pr_info("%s: soc(%d) too low state\n", __func__,
						info->battery_raw_soc);
		info->monitor_mode = MONITOR_EMER_LV2;
		wake_lock(&info->emer_wake_lock);
		return;
	} else {
		pr_debug("%s: soc(%d) not too low state\n", __func__,
						info->battery_raw_soc);
		wake_unlock(&info->emer_wake_lock);
	}

	/* prevent critical low voltage factor */
	if ((info->battery_vcell <
				(info->pdata->voltage_min - PWROFF_MARGIN)) ||
		(info->battery_vfocv < info->pdata->voltage_min)) {
		pr_info("%s: voltage(%d) too low state\n", __func__,
						info->battery_vcell);
		info->monitor_mode = MONITOR_EMER_LV2;
		wake_lock(&info->emer_wake_lock);
		return;
	} else {
		pr_debug("%s: voltage(%d) not too low state\n", __func__,
						info->battery_vcell);
		wake_unlock(&info->emer_wake_lock);
	}

	/* charge state factor */
	if (info->charge_virt_state ==
				POWER_SUPPLY_STATUS_CHARGING) {
		pr_debug("%s: v_state charging\n", __func__);
		info->monitor_mode = MONITOR_CHNG;
		wake_unlock(&info->emer_wake_lock);
#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
		if ((info->prev_cable_type == POWER_SUPPLY_TYPE_BATTERY &&
			info->cable_type != POWER_SUPPLY_TYPE_BATTERY) &&
			(info->battery_temper >=
				 info->pdata->overheat_stop_temp ||
			info->battery_temper <=
				info->pdata->freeze_stop_temp)) {
			pr_info("%s : re-check temper condition\n", __func__);
			info->monitor_mode = MONITOR_EMER_LV2;
		}
#endif
	} else if (info->charge_virt_state ==
				POWER_SUPPLY_STATUS_NOT_CHARGING) {
		pr_debug("%s: emergency(not charging) state\n", __func__);
		info->monitor_mode = MONITOR_EMER_LV2;
		wake_lock(&info->emer_wake_lock);
		return;
	} else {
		pr_debug("%s: normal state\n", __func__);
		info->monitor_mode = MONITOR_NORM;
		wake_unlock(&info->emer_wake_lock);
	}

	/*
	 * in LPM state, set default weight as 200%
	 */
	 if (info->lpm_state == true)
		info->monitor_weight *= 2;

	/* 5 times after boot, apply no interval (1 sec) */
	 if (info->monitor_count < 5) {
		pr_info("%s: now in booting, set 1s\n", __func__);
		info->monitor_mode = MONITOR_EMER_LV1; /* dummy value */
		return;
	 }

	/*
	 * prevent low voltage phase
	 * default, vcell is lower than min_voltage + 50mV, -30%
	 */
	if (info->battery_vcell < (info->pdata->voltage_min + 50000)) {
		info->monitor_mode = MONITOR_EMER_LV1;
		info->monitor_weight -= 30;
		pr_info("%s: low v(%d), weight(%d)\n", __func__,
				info->battery_vcell, info->monitor_weight);
	}

	/*
	 * prevent high current state(both charging and discharging
	 * default, (v diff = vcell - vfocv)
	 * charging, v_diff is higher than 250mV, (%dmV / 1000)%
	 * discharging, v_diff is higher than 100mV, (%dmV / 1000)%
	 * both, v_diff is lower than 10mV, +20%
	 */
	if ((info->battery_v_diff > 250000) ||
		(info->battery_v_diff < -100000)) {
		info->monitor_weight += (info->battery_v_diff / 10000);
		pr_info("%s: v diff(%d), weight(%d)\n", __func__,
			ABS(info->battery_v_diff), info->monitor_weight);
	} else if ((ABS(info->battery_v_diff)) < 50000) {
		info->monitor_weight += 20;
		pr_info("%s: v diff(%d), weight(%d)\n", __func__,
			ABS(info->battery_v_diff), info->monitor_weight);
	}

	/*
	 * prevent raw soc changable phase
	 * default, raw soc X.YY%, YY% is in under 0.1% or upper 0.9%, -10%
	 */
	if ((((info->battery_raw_soc % 100) < 10) &&
					(info->battery_v_diff < 0)) ||
		(((info->battery_raw_soc % 100) > 90) &&
					(info->battery_v_diff > 0))) {
		info->monitor_weight -= 10;
		pr_info("%s: raw soc(%d), weight(%d)\n", __func__,
			info->battery_raw_soc, info->monitor_weight);
	}

	/*
	 * prevent high slope raw soc change
	 * default, raw soc delta is higher than 0.5%, -(raw soc delta / 5)%
	 */
	if (ABS(info->battery_r_s_delta) > 50) {
		info->monitor_weight -= (ABS(info->battery_r_s_delta)) / 5;
		pr_info("%s: raw soc delta(%d), weight(%d)\n", __func__,
			ABS(info->battery_r_s_delta), info->monitor_weight);
	}

	/*
	 * prevent high/low temper phase
	 * default, temper is in (overheat temp - 5'C) or (freeze temp + 5'C)
	 */
	if ((info->battery_temper > (info->pdata->overheat_stop_temp - 50)) ||
		(info->battery_temper < (info->pdata->freeze_stop_temp + 50))) {
		info->monitor_weight -= 20;
		pr_info("%s: temper(%d ? %d - %d), weight(%d)\n", __func__,
					info->battery_temper,
					(info->pdata->overheat_stop_temp - 50),
					(info->pdata->freeze_stop_temp + 50),
					info->monitor_weight);
	}

	/*
	 * prevent high slope temper change
	 * default, temper delta is higher than 2.00'C
	 */
	if (ABS(info->battery_t_delta) > 20) {
		info->monitor_weight -= 20;
		pr_info("%s: temper delta(%d), weight(%d)\n", __func__,
			ABS(info->battery_t_delta), info->monitor_weight);
	}

	/* prevent too low or too high weight, 10 ~ 150% */
	info->monitor_weight = MIN(MAX(info->monitor_weight, 10), 150);

	if (info->monitor_weight != 100)
		pr_info("%s: weight(%d)\n", __func__, info->monitor_weight);
}

static void battery_monitor_work(struct work_struct *work)
{
	struct battery_info *info = container_of(work, struct battery_info,
						 monitor_work);
	int muic_cb_typ;
	pr_debug("%s\n", __func__);

	mutex_lock(&info->mon_lock);

	if (info->battery_test_mode) {
		pr_info("%s: now in test mode, not updated\n", __func__);
		goto monitor_finish;
	}

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	/* first, check cable-type */
	info->cable_type = battery_get_cable(info);
#endif

	/* If battery is not connected, clear flag for charge scenario */
	if ((battery_vf_cond(info) == true) ||
		(battery_health_cond(info) == true)) {
		pr_info("%s: battery error\n", __func__);
		info->overheated_state = false;
		info->freezed_state = false;
		info->temper_state = false;
		info->full_charged_state = STATUS_NOT_FULL;
		info->abstimer_state = false;
		info->abstimer_active = false;
		info->recharge_phase = false;

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
		pr_info("%s: not support standever...\n", __func__);
		battery_error_control(info);
#else
		if (info->pdata->battery_standever == true) {
			pr_info("%s: support standever\n", __func__);
			schedule_work(&info->error_work);
		} else {
			pr_info("%s: not support standever\n", __func__);
			battery_charge_control(info, OFF_CURR, OFF_CURR);
		}
#endif
	}

	/* Check battery state from charger and fuelgauge */
	battery_update_info(info);

	/* adc ldo , vf irq control */
	if (info->pdata->vf_det_src == VF_DET_GPIO) {
		if (info->cable_type == POWER_SUPPLY_TYPE_BATTERY) {
			if (info->batdet_irq_st) {
				disable_irq(info->batdet_irq);
				info->batdet_irq_st = false;
			}
			if (info->adc_pwr_st)
				battery_set_adc_power(info, 0);
		} else {
			if (!info->adc_pwr_st)
				battery_set_adc_power(info, 1);
			if (!info->batdet_irq_st) {
				enable_irq(info->batdet_irq);
				info->batdet_irq_st = true;
			}
		}
	}

	/* if battery is missed state, do not check charge scenario */
	if (info->battery_present == 0)
		goto monitor_finish;

	/* If charger is not connected, do not check charge scenario */
	if (info->cable_type == POWER_SUPPLY_TYPE_BATTERY)
		goto charge_ok;

	/* Below is charger is connected state */
#if defined(CONFIG_TARGET_LOCALE_KOR)
	if (info->errortest_stopcharging) {
		pr_info("%s: charge stopped by error_test mode\n", __func__);
		battery_charge_control(info, OFF_CURR, OFF_CURR);
		goto monitor_finish;
	}
#endif

	if (battery_temper_cond(info) == true) {
		pr_info("%s: charge stopped by temperature\n", __func__);
		battery_charge_control(info, OFF_CURR, OFF_CURR);
		goto monitor_finish;
	}

	if (battery_fullcharged_cond(info) == true) {
		pr_info("%s: full charged state\n", __func__);
		battery_charge_control(info, OFF_CURR, KEEP_CURR);
		info->recharge_phase = true;
		goto monitor_finish;
	}

	if (battery_abstimer_cond(info) == true) {
		pr_info("%s: abstimer state\n", __func__);
		battery_charge_control(info, OFF_CURR, OFF_CURR);
		info->recharge_phase = true;
		goto monitor_finish;
	}

	if (info->recharge_phase == true) {
		if (battery_recharge_cond(info) == true) {
			pr_info("%s: recharge condition\n", __func__);
			goto charge_ok;
		} else {
			pr_debug("%s: not recharge\n", __func__);
			goto monitor_finish;
		}
	}

charge_ok:
#if defined(CONFIG_MACH_GC1)
	pr_err("%s: Updated Cable State(%d)\n", __func__, info->cable_type);
#endif
	switch (info->cable_type) {
	case POWER_SUPPLY_TYPE_BATTERY:
		if (!info->pdata->suspend_chging)
			wake_unlock(&info->charge_wake_lock);
		battery_charge_control(info, OFF_CURR, OFF_CURR);

		/* clear charge scenario state */
		info->overheated_state = false;
		info->freezed_state = false;
		info->temper_state = false;
		info->full_charged_state = STATUS_NOT_FULL;
		info->abstimer_state = false;
		info->abstimer_active = false;
		info->recharge_phase = false;
		break;
	case POWER_SUPPLY_TYPE_MAINS:
		if (!info->pdata->suspend_chging)
			wake_lock(&info->charge_wake_lock);
		battery_charge_control(info, info->pdata->chg_curr_ta,
						info->pdata->in_curr_limit);
		break;
	case POWER_SUPPLY_TYPE_USB:
		if (!info->pdata->suspend_chging)
			wake_lock(&info->charge_wake_lock);
		battery_charge_control(info, info->pdata->chg_curr_usb,
						info->pdata->chg_curr_usb);
		break;
	case POWER_SUPPLY_TYPE_USB_CDP:
		if (!info->pdata->suspend_chging)
			wake_lock(&info->charge_wake_lock);
		battery_charge_control(info, info->pdata->chg_curr_cdp,
						info->pdata->chg_curr_cdp);
		break;
	case POWER_SUPPLY_TYPE_DOCK:
		if (!info->pdata->suspend_chging)
			wake_lock(&info->charge_wake_lock);
		muic_cb_typ = max77693_muic_get_charging_type();
		switch (muic_cb_typ) {
		case CABLE_TYPE_AUDIODOCK_MUIC:
			pr_info("%s: audio dock, %d\n",
					__func__, DOCK_TYPE_AUDIO_CURR);
			battery_charge_control(info,
						DOCK_TYPE_AUDIO_CURR,
						DOCK_TYPE_AUDIO_CURR);
			break;
		case CABLE_TYPE_SMARTDOCK_TA_MUIC:
			if (info->cable_sub_type == ONLINE_SUB_TYPE_SMART_OTG) {
				pr_info("%s: smart dock ta & host, %d\n",
					__func__, DOCK_TYPE_SMART_OTG_CURR);
				battery_charge_control(info,
						DOCK_TYPE_SMART_OTG_CURR,
						DOCK_TYPE_SMART_OTG_CURR);
			} else {
				pr_info("%s: smart dock ta & no host, %d\n",
					__func__, DOCK_TYPE_SMART_NOTG_CURR);
				battery_charge_control(info,
						DOCK_TYPE_SMART_NOTG_CURR,
						DOCK_TYPE_SMART_NOTG_CURR);
			}
			break;
		case CABLE_TYPE_SMARTDOCK_USB_MUIC:
			pr_info("%s: smart dock usb(low), %d\n",
					__func__, DOCK_TYPE_LOW_CURR);
			battery_charge_control(info,
						DOCK_TYPE_LOW_CURR,
						DOCK_TYPE_LOW_CURR);
			break;
		default:
			pr_info("%s: general dock, %d\n",
					__func__, info->pdata->chg_curr_dock);
		battery_charge_control(info,
			info->pdata->chg_curr_dock,
			info->pdata->chg_curr_dock);
			break;
		}
		break;
	case POWER_SUPPLY_TYPE_WIRELESS:
		if (!info->pdata->suspend_chging)
			wake_lock(&info->charge_wake_lock);
		battery_charge_control(info, info->pdata->chg_curr_wpc,
						info->pdata->chg_curr_wpc);
		break;
	default:
		break;
	}

monitor_finish:
	/* icon indicator */
	battery_indicator_icon(info);

	/* nofify full state to fuelgauge */
	battery_notify_full_state(info);

	/* dynamic battery polling interval */
	battery_interval_calulation(info);

	/* prevent suspend before starting the alarm */
	battery_monitor_interval(info);

	/* led indictor */
	if (info->pdata->led_indicator == true)
		battery_indicator_led(info);

	power_supply_changed(&info->psy_bat);

	pr_info("[%d] bat: s(%d, %d), v(%d, %d), "
		"t(%d.%d), "
		"cs(%d, %d), cb(%d), cr(%d, %d)",
		++info->monitor_count,
		info->battery_soc,
		info->battery_r_s_delta,
		info->battery_vcell / 1000,
		info->battery_v_diff / 1000,
		info->battery_temper / 10, info->battery_temper % 10,
		info->charge_real_state,
		info->charge_virt_state,
		info->cable_type,
		info->charge_current,
		info->input_current);

	if (info->battery_present == 0)
		pr_cont(", b(%d)", info->battery_present);
	if (info->battery_health != POWER_SUPPLY_HEALTH_GOOD)
		pr_cont(", h(%d)", info->battery_health);
	if (info->abstimer_state == 1)
		pr_cont(", a(%d)", info->abstimer_state);
	if (info->abstimer_active)
		pr_cont(", aa(%d)", info->abstimer_active);
	if (info->full_charged_state != STATUS_NOT_FULL)
		pr_cont(", f(%d)", info->full_charged_state);
	if (info->recharge_phase == 1)
		pr_cont(", r(%d)", info->recharge_phase);
	if (info->charge_start_time != 0)
		pr_cont(", t(%d)", ((int)info->current_time.tv_sec -
						info->charge_start_time));
	if (info->event_state != EVENT_STATE_CLEAR)
		pr_cont(", e(%d, 0x%04x)", info->event_state, info->event_type);
	if (info->siop_state)
		pr_cont(", op(%d, %d)", info->siop_state, info->siop_lv);

	pr_cont("\n");

	/* check current_avg */
	if (info->charge_current_avg < 0)
		pr_info("%s: charging but discharging, power off\n", __func__);

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	/* prevent suspend for ui-update */
	if (info->prev_cable_type != info->cable_type ||
		info->prev_battery_health != info->battery_health ||
		info->prev_charge_virt_state != info->charge_virt_state ||
		info->prev_battery_soc != info->battery_soc) {
		/* TBD : timeout value */
		pr_info("%s : update wakelock (%d)\n", __func__, 3 * HZ);
		wake_lock_timeout(&info->update_wake_lock, 3 * HZ);
	}

	info->prev_cable_type = info->cable_type;
	info->prev_battery_health = info->battery_health;
	info->prev_charge_virt_state = info->charge_virt_state;
	info->prev_battery_soc = info->battery_soc;
#endif

	/* if cable is detached in lpm, guarantee some secs for playlpm */
	if ((info->lpm_state == true) &&
		(info->cable_type == POWER_SUPPLY_TYPE_BATTERY)) {
		pr_info("%s: lpm with battery, maybe power off\n", __func__);
		wake_lock_timeout(&info->monitor_wake_lock, 10 * HZ);
	} else
		wake_lock_timeout(&info->monitor_wake_lock, HZ);

	mutex_unlock(&info->mon_lock);

	return;
}

static void battery_error_work(struct work_struct *work)
{
	struct battery_info *info = container_of(work, struct battery_info,
						 error_work);
	int err_cnt;
	int old_vcell, new_vcell, vcell_diff;
	pr_info("%s\n", __func__);

	mutex_lock(&info->err_lock);

	if ((info->vf_state == true) || (info->health_state == true)) {
		pr_info("%s: battery error state\n", __func__);
		old_vcell = info->battery_vcell;
		new_vcell = 0;
		for (err_cnt = 1; err_cnt <= VF_CHECK_COUNT; err_cnt++) {
#if defined(CONFIG_MACH_P11) || defined(CONFIG_MACH_P10)
			/* FIXME: fix P11 build error temporarily */
#else
			if (is_jig_attached == JIG_ON) {
				pr_info("%s: JIG detected, return\n", __func__);
				mutex_unlock(&info->err_lock);
				return;
			}
#endif
			info->battery_present =
				battery_get_info(info,
					POWER_SUPPLY_PROP_PRESENT);
			if (info->battery_present == 0) {
				pr_info("%s: battery still error(%d)\n",
						__func__, err_cnt);
				msleep(VF_CHECK_DELAY);
			} else {
				pr_info("%s: battery detect ok, "
						"check soc\n", __func__);
				new_vcell = battery_get_info(info,
					POWER_SUPPLY_PROP_VOLTAGE_NOW);
				vcell_diff = abs(old_vcell - new_vcell);
				pr_info("%s: check vcell: %d -> %d, diff: %d\n",
						__func__, info->battery_vcell,
							new_vcell, vcell_diff);
				if (vcell_diff > RESET_SOC_DIFF_TH) {
					pr_info("%s: reset soc\n", __func__);
					battery_control_info(info,
						POWER_SUPPLY_PROP_CAPACITY, 1);
				} else
					pr_info("%s: keep soc\n", __func__);
				break;
			}

			if (err_cnt == VF_CHECK_COUNT) {
				pr_info("%s: battery error, power off\n",
								__func__);
				battery_charge_control(info, OFF_CURR,
								OFF_CURR);
			}
		}
	} else
		pr_info("%s: unexpected error\n", __func__);

	mutex_unlock(&info->err_lock);
	return;
}

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
static void battery_error_control(struct battery_info *info)
{
	pr_info("%s\n", __func__);

	if (info->vf_state == true) {
		pr_info("%s: battery vf error state\n", __func__);

		/* check again */
		info->battery_present =	battery_get_info(info,
					POWER_SUPPLY_PROP_PRESENT);

		if (info->battery_present == 0) {
			pr_info("%s: battery vf error, "
				"disable charging and off the system path!\n",
				__func__);
			battery_charge_control(info, OFF_CURR, OFF_CURR);
			info->charge_virt_state =
				POWER_SUPPLY_STATUS_NOT_CHARGING;
		}
	} else if (info->health_state == true) {
		if (info->battery_health ==
				POWER_SUPPLY_HEALTH_OVERVOLTAGE)
			pr_info("%s: vbus ovp state!", __func__);
		else if (info->battery_health ==
				POWER_SUPPLY_HEALTH_UNSPEC_FAILURE)
			pr_info("%s: uspec state from charger", __func__);
	}

	return;
}
#endif

/* Support property from battery */
static enum power_supply_property samsung_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
#ifdef CONFIG_SLP
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
#endif
	POWER_SUPPLY_PROP_CAPACITY,
#ifdef CONFIG_SLP
	POWER_SUPPLY_PROP_CAPACITY_RAW,
#endif
	POWER_SUPPLY_PROP_TEMP,
};

/* Support property from usb, ac */
static enum power_supply_property samsung_power_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int samsung_battery_get_property(struct power_supply *ps,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct battery_info *info = container_of(ps, struct battery_info,
						 psy_bat);

	/* re-update indicator icon */
	battery_indicator_icon(info);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = info->charge_virt_state;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = info->charge_type;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = info->battery_health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = info->battery_present;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = info->cable_type;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = info->battery_vcell;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = info->input_current;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = info->charge_current;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = info->charge_current_avg;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = info->battery_soc;
		break;
#ifdef CONFIG_SLP
	case POWER_SUPPLY_PROP_CAPACITY_RAW:
		val->intval = info->battery_raw_soc;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		if (info->full_charged_state)
			val->intval = true;
		else
			val->intval = false;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		if (info->charge_real_state == POWER_SUPPLY_STATUS_CHARGING)
			val->intval = true;
		else
			val->intval = false;
		break;
#endif
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = info->battery_temper;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = info->pdata->voltage_max;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = info->pdata->voltage_min;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int samsung_battery_set_property(struct power_supply *ps,
					enum power_supply_property psp,
					const union power_supply_propval *val)
{
	struct battery_info *info = container_of(ps, struct battery_info,
						 psy_bat);

	if (info->is_suspended) {
		pr_info("%s: now in suspend\n", __func__);
		return 0;
	}

	if (info->battery_test_mode) {
		pr_info("%s: set test value: psp(%d), val(%d)\n",
					__func__, psp, val->intval);
		switch (psp) {
		case POWER_SUPPLY_PROP_STATUS:
			info->charge_virt_state = val->intval;
			break;
		case POWER_SUPPLY_PROP_CHARGE_TYPE:
			info->charge_type = val->intval;
			break;
		case POWER_SUPPLY_PROP_HEALTH:
			info->battery_health = val->intval;
			break;
		case POWER_SUPPLY_PROP_PRESENT:
			info->battery_present = val->intval;
			break;
		case POWER_SUPPLY_PROP_ONLINE:
			info->cable_type = val->intval;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			info->battery_vcell = val->intval;
			break;
		case POWER_SUPPLY_PROP_CURRENT_MAX:
			info->input_current = val->intval;
			break;
		case POWER_SUPPLY_PROP_CURRENT_NOW:
			info->charge_current = val->intval;
			break;
		case POWER_SUPPLY_PROP_CURRENT_AVG:
			info->charge_current_avg = val->intval;
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			info->battery_soc = val->intval;
			break;
		case POWER_SUPPLY_PROP_TEMP:
			info->battery_temper = val->intval;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
			info->pdata->voltage_max = val->intval;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
			info->pdata->voltage_min = val->intval;
			break;
		default:
			return -EINVAL;
		}
	} else {
		switch (psp) {
		case POWER_SUPPLY_PROP_STATUS:
			break;
		default:
			return -EINVAL;
		}
	}

	cancel_work_sync(&info->monitor_work);
	wake_lock(&info->monitor_wake_lock);
	schedule_work(&info->monitor_work);

	return 0;
}

static int samsung_usb_get_property(struct power_supply *ps,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	struct battery_info *info = container_of(ps, struct battery_info,
						 psy_usb);

	if (psp != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;

	/* Set enable=1 only if the USB charger is connected */
	val->intval = ((info->charge_virt_state !=
				POWER_SUPPLY_STATUS_DISCHARGING) &&
			((info->cable_type == POWER_SUPPLY_TYPE_USB) ||
			(info->cable_type == POWER_SUPPLY_TYPE_USB_CDP)));

	return 0;
}

static int samsung_ac_get_property(struct power_supply *ps,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct battery_info *info = container_of(ps, struct battery_info,
						 psy_ac);

	if (psp != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;

	/* Set enable=1 only if the AC charger is connected */
	val->intval = ((info->charge_virt_state !=
				POWER_SUPPLY_STATUS_DISCHARGING) &&
			((info->cable_type == POWER_SUPPLY_TYPE_MAINS) ||
			(info->cable_type == POWER_SUPPLY_TYPE_MISC) ||
			(info->cable_type == POWER_SUPPLY_TYPE_DOCK) ||
			(info->cable_type == POWER_SUPPLY_TYPE_WIRELESS)));

	return 0;
}

static irqreturn_t battery_isr(int irq, void *data)
{
	struct battery_info *info = data;
	int bat_gpio;
	pr_info("%s: irq(%d)\n", __func__, irq);

	bat_gpio = gpio_get_value(info->batdet_gpio);
	pr_info("%s: battery present gpio(%d)\n", __func__, bat_gpio);

	cancel_work_sync(&info->monitor_work);
	wake_lock(&info->monitor_wake_lock);
	schedule_work(&info->monitor_work);

	return IRQ_HANDLED;
}

static __devinit int samsung_battery_probe(struct platform_device *pdev)
{
	struct battery_info *info;
	struct samsung_battery_platform_data *pdata = pdev->dev.platform_data;
	int ret = -ENODEV;
	char *temper_src_name[] = { "fuelgauge", "ap adc",
					"ext adc", "unknown"
	};
	char *vf_src_name[] = { "adc", "charger irq", "gpio", "unknown"
	};
	pr_info("%s: battery init\n", __func__);

	if (!pdata) {
		pr_err("%s: no platform data\n", __func__);
		return -ENODEV;
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	platform_set_drvdata(pdev, info);

	info->dev = &pdev->dev;
	info->pdata = pdata;

	/* Check charger name and fuelgauge name. */
	if (!info->pdata->charger_name || !info->pdata->fuelgauge_name) {
		pr_err("%s: no charger or fuel gauge name\n", __func__);
		goto err_psy_get;
	}
	info->charger_name = info->pdata->charger_name;
	info->fuelgauge_name = info->pdata->fuelgauge_name;
#if defined(CONFIG_CHARGER_MAX8922_U1)
	if (system_rev >= 2)
		info->sub_charger_name = info->pdata->sub_charger_name;
#endif
	pr_info("%s: Charger name: %s\n", __func__, info->charger_name);
	pr_info("%s: Fuelgauge name: %s\n", __func__, info->fuelgauge_name);
#if defined(CONFIG_CHARGER_MAX8922_U1)
	if (system_rev >= 2)
		pr_info("%s: SubCharger name: %s\n", __func__,
			info->sub_charger_name);
#endif

	info->psy_charger = power_supply_get_by_name(info->charger_name);
	info->psy_fuelgauge = power_supply_get_by_name(info->fuelgauge_name);
#if defined(CONFIG_CHARGER_MAX8922_U1)
	if (system_rev >= 2)
		info->psy_sub_charger =
		    power_supply_get_by_name(info->sub_charger_name);
#endif
	if (!info->psy_charger || !info->psy_fuelgauge) {
		pr_err("%s: fail to get power supply\n", __func__);
		goto err_psy_get;
	}

#if defined(CONFIG_MACH_M0)
	/* WORKAROUND: set battery pdata in driver */
	if (system_rev == 3) {
		info->pdata->temper_src = TEMPER_EXT_ADC;
		info->pdata->temper_ch = 7;
	}
#endif
	pr_info("%s: Temperature source: %s\n", __func__,
		temper_src_name[info->pdata->temper_src]);


	/* not supported H/W rev for VF ADC */
#if defined(CONFIG_MACH_T0) && defined(CONFIG_TARGET_LOCALE_USA)
	if (system_rev < 7)
		info->pdata->vf_det_src = VF_DET_CHARGER;
#endif
	pr_info("%s: VF detect source: %s\n", __func__,
		vf_src_name[info->pdata->vf_det_src]);

	/* recalculate recharge voltage, it depends on max voltage value */
	info->pdata->recharge_voltage = info->pdata->voltage_max -
							RECHG_DROP_VALUE;
	pr_info("%s: Recharge voltage: %d\n", __func__,
				info->pdata->recharge_voltage);

	if (info->pdata->ctia_spec == true) {
		pr_info("%s: applied CTIA spec, event time(%ds)\n",
				__func__, info->pdata->event_time);
	} else
		pr_info("%s: not applied CTIA spec\n", __func__);

#if defined(CONFIG_S3C_ADC)
	/* adc register */
	info->adc_client = s3c_adc_register(pdev, NULL, NULL, 0);

	if (IS_ERR(info->adc_client)) {
		pr_err("%s: fail to register adc\n", __func__);
		goto err_adc_reg;
	}
#endif

	/* init battery info */
	info->charge_real_state = battery_get_info(info,
				POWER_SUPPLY_PROP_STATUS);
	if ((info->charge_real_state == POWER_SUPPLY_STATUS_CHARGING) ||
		(info->charge_real_state == POWER_SUPPLY_STATUS_FULL)) {
		pr_info("%s: boot with charging, s(%d)\n", __func__,
						info->charge_real_state);
		info->charge_start_time = 1;
		battery_set_adc_power(info, 1);
	} else {
		pr_info("%s: boot without charging, s(%d)\n", __func__,
						info->charge_real_state);
		info->charge_start_time = 0;
	}
	info->full_charged_state = STATUS_NOT_FULL;
	info->abstimer_state = false;
	info->abstimer_active = false;
	info->recharge_phase = false;
	info->siop_charge_current = info->pdata->chg_curr_usb;
	info->monitor_mode = MONITOR_NORM;
	info->led_state = BATT_LED_DISCHARGING;
	info->monitor_count = 0;
	info->slate_mode = 0;

	/* LPM charging state */
	info->lpm_state = lpcharge;

	mutex_init(&info->mon_lock);
	mutex_init(&info->ops_lock);
	mutex_init(&info->err_lock);

	wake_lock_init(&info->monitor_wake_lock, WAKE_LOCK_SUSPEND,
		       "battery-monitor");
	wake_lock_init(&info->emer_wake_lock, WAKE_LOCK_SUSPEND,
		       "battery-emergency");
	if (!info->pdata->suspend_chging)
		wake_lock_init(&info->charge_wake_lock,
			       WAKE_LOCK_SUSPEND, "battery-charging");
#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	wake_lock_init(&info->update_wake_lock, WAKE_LOCK_SUSPEND,
		       "battery-update");
#endif

	/* Init wq for battery */
	INIT_WORK(&info->error_work, battery_error_work);
	INIT_WORK(&info->monitor_work, battery_monitor_work);

	/* Init Power supply class */
	info->psy_bat.name = "battery";
	info->psy_bat.type = POWER_SUPPLY_TYPE_BATTERY;
	info->psy_bat.properties = samsung_battery_props;
	info->psy_bat.num_properties = ARRAY_SIZE(samsung_battery_props);
	info->psy_bat.get_property = samsung_battery_get_property;
	info->psy_bat.set_property = samsung_battery_set_property;

	info->psy_usb.name = "usb";
	info->psy_usb.type = POWER_SUPPLY_TYPE_USB;
	info->psy_usb.supplied_to = supply_list;
	info->psy_usb.num_supplicants = ARRAY_SIZE(supply_list);
	info->psy_usb.properties = samsung_power_props;
	info->psy_usb.num_properties = ARRAY_SIZE(samsung_power_props);
	info->psy_usb.get_property = samsung_usb_get_property;

	info->psy_ac.name = "ac";
	info->psy_ac.type = POWER_SUPPLY_TYPE_MAINS;
	info->psy_ac.supplied_to = supply_list;
	info->psy_ac.num_supplicants = ARRAY_SIZE(supply_list);
	info->psy_ac.properties = samsung_power_props;
	info->psy_ac.num_properties = ARRAY_SIZE(samsung_power_props);
	info->psy_ac.get_property = samsung_ac_get_property;

	ret = power_supply_register(&pdev->dev, &info->psy_bat);
	if (ret) {
		pr_err("%s: failed to register psy_bat\n", __func__);
		goto err_psy_reg_bat;
	}

	ret = power_supply_register(&pdev->dev, &info->psy_usb);
	if (ret) {
		pr_err("%s: failed to register psy_usb\n", __func__);
		goto err_psy_reg_usb;
	}

	ret = power_supply_register(&pdev->dev, &info->psy_ac);
	if (ret) {
		pr_err("%s: failed to register psy_ac\n", __func__);
		goto err_psy_reg_ac;
	}

	/* battery present irq */
	if (!info->pdata->batt_present_gpio) {
		pr_info("%s: do not support battery gpio detect\n", __func__);
		goto gpio_bat_det_finish;
	} else
		pr_info("%s: support battery gpio detection\n", __func__);

	info->batdet_gpio = info->pdata->batt_present_gpio;
	info->batdet_irq = gpio_to_irq(info->batdet_gpio);
	ret = gpio_request(info->batdet_gpio, "battery_present_n");
	if (ret) {
		pr_err("%s: failed requesting gpio %d\n", __func__,
						info->batdet_gpio);
		goto err_irq;
	}
	gpio_direction_input(info->batdet_gpio);
	gpio_free(info->batdet_gpio);

	ret = request_threaded_irq(info->batdet_irq, NULL,
				battery_isr,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"batdet-irq", info);
	if (ret) {
		pr_err("%s: fail to request batdet irq: %d: %d\n",
				__func__, info->batdet_irq, ret);
		goto err_irq;
	}

	ret = enable_irq_wake(info->batdet_irq);
	if (ret) {
		pr_err("%s: failed enable irq wake %d\n", __func__,
						info->batdet_irq);
		goto err_enable_irq;
	}

	info->batdet_irq_st = true;
gpio_bat_det_finish:

	/* Using android alarm for gauging instead of workqueue */
	info->last_poll = alarm_get_elapsed_realtime();
	alarm_init(&info->monitor_alarm,
			ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP,
			battery_monitor_alarm);

	if (info->pdata->ctia_spec == true)
		alarm_init(&info->event_alarm,
				ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP,
				battery_event_alarm);

	/* update battery init status */
	schedule_work(&info->monitor_work);

	/* Create samsung detail attributes */
	battery_create_attrs(info->psy_bat.dev);

#if defined(CONFIG_TARGET_LOCALE_KOR)
	info->entry = create_proc_entry("batt_info_proc", S_IRUGO, NULL);
	if (!info->entry)
		pr_err("%s: failed to create proc_entry\n", __func__);
	else {
		info->entry->read_proc = battery_info_proc;
		info->entry->data = (struct battery_info *)info;
	}
#endif

	pr_info("%s: probe complete\n", __func__);

	return 0;

err_enable_irq:
	free_irq(info->batdet_irq, info);
err_irq:
	power_supply_unregister(&info->psy_ac);
err_psy_reg_ac:
	power_supply_unregister(&info->psy_usb);
err_psy_reg_usb:
	power_supply_unregister(&info->psy_bat);
err_psy_reg_bat:
	s3c_adc_release(info->adc_client);
	wake_lock_destroy(&info->monitor_wake_lock);
	wake_lock_destroy(&info->emer_wake_lock);
#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	wake_lock_destroy(&info->update_wake_lock);
#endif
	mutex_destroy(&info->mon_lock);
	mutex_destroy(&info->ops_lock);
	mutex_destroy(&info->err_lock);
	if (!info->pdata->suspend_chging)
		wake_lock_destroy(&info->charge_wake_lock);

err_adc_reg:
err_psy_get:
	kfree(info);

	return ret;
}

static int __devexit samsung_battery_remove(struct platform_device *pdev)
{
	struct battery_info *info = platform_get_drvdata(pdev);

	remove_proc_entry("battery_info_proc", NULL);

	alarm_cancel(&info->monitor_alarm);
	if (info->pdata->ctia_spec == true)
		alarm_cancel(&info->event_alarm);

	cancel_work_sync(&info->error_work);
	cancel_work_sync(&info->monitor_work);

	power_supply_unregister(&info->psy_bat);
	power_supply_unregister(&info->psy_usb);
	power_supply_unregister(&info->psy_ac);

	wake_lock_destroy(&info->monitor_wake_lock);
	wake_lock_destroy(&info->emer_wake_lock);
#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	wake_lock_destroy(&info->update_wake_lock);
#endif
	if (!info->pdata->suspend_chging)
		wake_lock_destroy(&info->charge_wake_lock);

	mutex_destroy(&info->mon_lock);
	mutex_destroy(&info->ops_lock);
	mutex_destroy(&info->err_lock);

	kfree(info);

	return 0;
}

#ifdef CONFIG_PM
static int samsung_battery_prepare(struct device *dev)
{
	struct battery_info *info = dev_get_drvdata(dev);
	pr_info("%s\n", __func__);

	if ((info->monitor_mode != MONITOR_EMER_LV1) &&
		(info->monitor_mode != MONITOR_EMER_LV2)) {
		if ((info->charge_real_state ==
					POWER_SUPPLY_STATUS_CHARGING) ||
			(info->charge_virt_state ==
					POWER_SUPPLY_STATUS_CHARGING))
			info->monitor_mode = MONITOR_CHNG_SUSP;
		else
			info->monitor_mode = MONITOR_NORM_SUSP;
	}

	battery_monitor_interval(info);

	return 0;
}

static void samsung_battery_complete(struct device *dev)
{
	struct battery_info *info = dev_get_drvdata(dev);
	pr_info("%s\n", __func__);

	info->monitor_mode = MONITOR_NORM;

	battery_monitor_interval(info);
}

static int samsung_battery_suspend(struct device *dev)
{
	struct battery_info *info = dev_get_drvdata(dev);
	pr_info("%s\n", __func__);

	info->is_suspended = true;

	cancel_work_sync(&info->monitor_work);

	return 0;
}

static int samsung_battery_resume(struct device *dev)
{
	struct battery_info *info = dev_get_drvdata(dev);
	pr_info("%s\n", __func__);

	schedule_work(&info->monitor_work);

	info->is_suspended = false;

	return 0;
}

static const struct dev_pm_ops samsung_battery_pm_ops = {
	.prepare = samsung_battery_prepare,
	.complete = samsung_battery_complete,
	.suspend = samsung_battery_suspend,
	.resume = samsung_battery_resume,
};
#endif

static struct platform_driver samsung_battery_driver = {
	.driver = {
		.name = "samsung-battery",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &samsung_battery_pm_ops,
#endif
	},
	.probe = samsung_battery_probe,
	.remove = __devexit_p(samsung_battery_remove),
};

static int __init samsung_battery_init(void)
{
	return platform_driver_register(&samsung_battery_driver);
}

static void __exit samsung_battery_exit(void)
{
	platform_driver_unregister(&samsung_battery_driver);
}

late_initcall(samsung_battery_init);
module_exit(samsung_battery_exit);

MODULE_AUTHOR("SangYoung Son <hello.son@samsung.com>");
MODULE_DESCRIPTION("SAMSUNG battery driver");
MODULE_LICENSE("GPL");
