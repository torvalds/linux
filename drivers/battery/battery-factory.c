/*
 * battery-factory.c - factory mode for battery driver
 *
 * Copyright (C) 2011 Samsung Electronics
 * SangYoung Son <hello.son@samsung.com>
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

#include "battery-factory.h"

/* prototype */
static ssize_t factory_show_property(struct device *dev,
				     struct device_attribute *attr, char *buf);

static ssize_t factory_store_property(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count);

static ssize_t ctia_show_property(struct device *dev,
				     struct device_attribute *attr, char *buf);

static ssize_t ctia_store_property(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count);


#define FACTORY_ATTR(_name)			\
{						\
	.attr = { .name = #_name,		\
		  .mode = S_IRUGO | S_IWUSR | S_IWGRP,	\
		},					\
	.show = factory_show_property,		\
	.store = factory_store_property,		\
}

static struct device_attribute factory_attrs[] = {
	FACTORY_ATTR(batt_reset_soc),
	FACTORY_ATTR(batt_read_raw_soc),
	FACTORY_ATTR(batt_read_adj_soc),
	FACTORY_ATTR(batt_type),
	FACTORY_ATTR(batt_temp_adc),
	FACTORY_ATTR(batt_temp_aver),
	FACTORY_ATTR(batt_temp_adc_aver),
	FACTORY_ATTR(batt_vol_aver),
	FACTORY_ATTR(batt_vfocv),
	FACTORY_ATTR(batt_lp_charging),
	FACTORY_ATTR(batt_charging_source),
	FACTORY_ATTR(test_mode),
	FACTORY_ATTR(batt_error_test),
	FACTORY_ATTR(siop_activated),
	FACTORY_ATTR(siop_level),
	FACTORY_ATTR(wc_status),
	FACTORY_ATTR(wpc_pin_state),
	FACTORY_ATTR(factory_mode),
	FACTORY_ATTR(update),
	FACTORY_ATTR(batt_slate_mode),
	FACTORY_ATTR(batt_vf_adc),

	/* not use */
	FACTORY_ATTR(batt_vol_adc),
	FACTORY_ATTR(batt_vol_adc_cal),
	FACTORY_ATTR(batt_vol_adc_aver),
	FACTORY_ATTR(batt_temp_adc_cal),
	FACTORY_ATTR(auth_battery),

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	FACTORY_ATTR(batt_temp_adc_spec),
	FACTORY_ATTR(batt_sysrev),
#endif
};

enum {
	BATT_RESET_SOC = 0,
	BATT_READ_RAW_SOC,
	BATT_READ_ADJ_SOC,
	BATT_TYPE,
	BATT_TEMP_ADC,
	BATT_TEMP_AVER,
	BATT_TEMP_ADC_AVER,
	BATT_VOL_AVER,
	BATT_VFOCV,
	BATT_LP_CHARGING,
	BATT_CHARGING_SOURCE,
	TEST_MODE,
	BATT_ERROR_TEST,
	SIOP_ACTIVATED,
	SIOP_LEVEL,
	WC_STATUS,
	WPC_PIN_STATE,
	FACTORY_MODE,
	UPDATE,
	BATT_SLATE_MODE,
	BATT_VF_ADC,

	/* not use */
	BATT_VOL_ADC,
	BATT_VOL_ADC_CAL,
	BATT_VOL_ADC_AVER,
	BATT_TEMP_ADC_CAL,
	AUTH_BATTERY,

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	BATT_TEMP_ADC_SPEC,
	BATT_SYSREV,
#endif
};

static ssize_t factory_show_property(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct battery_info *info = dev_get_drvdata(dev->parent);
	int i;
	int cnt, dat, d_max, d_min, d_total;
	int val;
	const ptrdiff_t off = attr - factory_attrs;
	pr_debug("%s: %s\n", __func__, factory_attrs[off].attr.name);

	i = 0;
	val = 0;
	switch (off) {
	case BATT_READ_RAW_SOC:
		battery_update_info(info);
		val = info->battery_raw_soc;
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case BATT_READ_ADJ_SOC:
		val = info->battery_soc =
			battery_get_info(info, POWER_SUPPLY_PROP_CAPACITY);
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case BATT_TYPE:
		i += scnprintf(buf + i, PAGE_SIZE - i, "SDI_SDI\n");
		break;
	case BATT_TEMP_ADC:
		battery_get_info(info, POWER_SUPPLY_PROP_TEMP);
		val = info->battery_temper_adc;
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case BATT_TEMP_AVER:
		val = 0;
		for (cnt = 0; cnt < CNT_TEMPER_AVG; cnt++) {
			msleep(100);
			battery_get_info(info, POWER_SUPPLY_PROP_TEMP);
			val += info->battery_temper_adc;
			info->battery_temper_adc_avg = val / (cnt + 1);
		}
#ifdef CONFIG_S3C_ADC
		info->battery_temper_avg = info->pdata->covert_adc(
						info->battery_temper_adc_avg,
						info->pdata->temper_ch);
#else
		info->battery_temper_avg = info->battery_temper;
#endif
		val = info->battery_temper_avg;
		pr_info("%s: temper avg(%d)\n", __func__, val);
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case BATT_TEMP_ADC_AVER:
		val = info->battery_temper_adc_avg;
		pr_info("%s: temper adc avg(%d)\n", __func__, val);
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case BATT_VOL_AVER:	/* not use POWER_SUPPLY_PROP_VOLTAGE_AVG */
		val = dat = d_max = d_min = d_total = 0;
		for (cnt = 0; cnt < CNT_VOLTAGE_AVG; cnt++) {
			msleep(200);
			dat = battery_get_info(info,
				POWER_SUPPLY_PROP_VOLTAGE_NOW);

			if (cnt != 0) {
				d_max = max(dat, d_max);
				d_min = min(dat, d_min);
			} else
				d_max = d_min = dat;

			d_total += dat;
		}
		val = (d_total - d_max - d_min) / (CNT_VOLTAGE_AVG - 2);
		pr_info("%s: voltage avg(%d)\n", __func__, val);
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case BATT_VFOCV:
		battery_update_info(info);
		val = info->battery_vfocv;
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case BATT_LP_CHARGING:
		val = info->lpm_state;
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case BATT_CHARGING_SOURCE:
		val = info->cable_type =
			battery_get_info(info, POWER_SUPPLY_PROP_ONLINE);
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case TEST_MODE:
		val = info->battery_test_mode;
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case BATT_ERROR_TEST:
		i += scnprintf(buf + i, PAGE_SIZE - i,
			"(%d): 0: normal, 1: full charged, 2: freezed, 3: overheated, 4: ovp, 5: vf\n",
			info->battery_error_test);
		break;
	case SIOP_ACTIVATED:
		val = info->siop_state;
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case SIOP_LEVEL:
		val = info->siop_lv;
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case WC_STATUS:
	case WPC_PIN_STATE:
#ifdef CONFIG_BATTERY_WPC_CHARGER
		val = !gpio_get_value(GPIO_WPC_INT);
#else
		val = false;
#endif
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case FACTORY_MODE:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
						info->factory_mode);
		break;
	case BATT_SLATE_MODE:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
						info->slate_mode);
		break;
	case BATT_VF_ADC:
		battery_get_info(info, POWER_SUPPLY_PROP_PRESENT);
		val = info->battery_vf_adc;
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case BATT_VOL_ADC:
	case BATT_VOL_ADC_CAL:
	case BATT_VOL_ADC_AVER:
	case BATT_TEMP_ADC_CAL:
	case AUTH_BATTERY:
		i += scnprintf(buf + i, PAGE_SIZE - i, "N/A\n");
		break;
#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	case BATT_SYSREV:
		val = system_rev;
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);
		break;
	case BATT_TEMP_ADC_SPEC:
		i += scnprintf(buf + i, PAGE_SIZE - i,
			"(HIGH: %d / %d,   LOW: %d / %d)\n",
			info->pdata->overheat_stop_temp,
			info->pdata->overheat_recovery_temp,
			info->pdata->freeze_stop_temp,
			info->pdata->freeze_recovery_temp);
		break;
#endif
	default:
		i = -EINVAL;
	}

	return i;
}

static ssize_t factory_store_property(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct battery_info *info = dev_get_drvdata(dev->parent);
	int x;
	int ret;
	const ptrdiff_t off = attr - factory_attrs;
	pr_info("%s: %s\n", __func__, factory_attrs[off].attr.name);

	x = 0;
	ret = 0;
	switch (off) {
	case BATT_RESET_SOC:
		if (sscanf(buf, "%d\n", &x) == 1) {
			if (x == 1) {
				pr_info("%s: Reset SOC.\n", __func__);
				battery_control_info(info,
						POWER_SUPPLY_PROP_CAPACITY,
						1);
				info->battery_soc =
						battery_get_info(info,
						POWER_SUPPLY_PROP_CAPACITY);
			} else
				pr_info("%s: Not supported param.\n", __func__);
			ret = count;
		}
		break;
	case TEST_MODE:
		if (sscanf(buf, "%d\n", &x) == 1) {
			info->battery_test_mode = x;
			pr_info("%s: battery test mode: %d\n", __func__,
						info->battery_test_mode);
			ret = count;
		}
		break;
	case BATT_ERROR_TEST:
		if (sscanf(buf, "%d\n", &x) == 1) {
			info->battery_error_test = x;
			pr_info("%s: battery error test: %d\n", __func__,
						info->battery_error_test);
			ret = count;
		}
		break;
	case SIOP_ACTIVATED:
		if (sscanf(buf, "%d\n", &x) == 1) {
			info->siop_state = x;
			pr_info("%s: SIOP %s\n", __func__,
				(info->siop_state ?
				"activated" : "deactivated"));
			ret = count;
		}
		break;
	case SIOP_LEVEL:
		if (sscanf(buf, "%d\n", &x) == 1) {
			info->siop_lv = x;
			pr_info("%s: SIOP level %d\n", __func__, info->siop_lv);
			ret = count;
		}
		break;
	case FACTORY_MODE:
		if (sscanf(buf, "%d\n", &x) == 1) {
			if (x)
				info->factory_mode = true;
			else
				info->factory_mode = false;

			pr_info("%s: factory mode %s\n", __func__,
				(info->factory_mode ? "set" : "clear"));
			ret = count;
		}
		break;
	case UPDATE:
		pr_info("%s: battery update\n", __func__);
		ret = count;
		break;
	case BATT_SLATE_MODE:
		if (sscanf(buf, "%d\n", &x) == 1) {
			if (x)
				info->slate_mode = 1;
			else
				info->slate_mode = 0;

			pr_info("%s: slate_mode %s\n", __func__,
				(info->slate_mode ? "set" : "clear"));
			ret = count;
		}
		break;
	default:
		ret = -EINVAL;
	}

	schedule_work(&info->monitor_work);

	return ret;
}

#define CTIA_ATTR(_name)			\
{						\
	.attr = { .name = #_name,		\
		  .mode = S_IRUGO | S_IWUSR | S_IWGRP,	\
		},					\
	.show = ctia_show_property,		\
	.store = ctia_store_property,		\
}

/* CTIA */
static struct device_attribute ctia_attrs[] = {
	CTIA_ATTR(talk_wcdma),
	CTIA_ATTR(talk_gsm),
	CTIA_ATTR(call),
	CTIA_ATTR(video),
	CTIA_ATTR(music),
	CTIA_ATTR(browser),
	CTIA_ATTR(hotspot),
	CTIA_ATTR(camera),
	CTIA_ATTR(data_call),
	CTIA_ATTR(gps),
	CTIA_ATTR(lte),
	CTIA_ATTR(wifi),
	CTIA_ATTR(use),
};

static ssize_t ctia_show_property(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct battery_info *info = dev_get_drvdata(dev->parent);
	int i = 0;
	const ptrdiff_t off = attr - ctia_attrs;
	pr_info("%s: %s\n", __func__, ctia_attrs[off].attr.name);

	i += scnprintf(buf + i, PAGE_SIZE - i, "%d 0x%04x\n",
				info->event_state, info->event_type);

	return i;
}

static ssize_t ctia_store_property(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct battery_info *info = dev_get_drvdata(dev->parent);
	int x = 0;
	int ret = -EINVAL;
	const ptrdiff_t off = attr - ctia_attrs;
	pr_info("%s: %s\n", __func__, ctia_attrs[off].attr.name);

	if (sscanf(buf, "%d\n", &x) == 1) {
		if (x == 1) {
			info->event_type |= (1 << off);
			pr_info("%s: set case #%d, event(0x%04x)\n",
				__func__, off, info->event_type);
		} else if (x == 0) {
			info->event_type &= ~(1 << off);
			pr_info("%s: clear case #%d, event(0x%04x)\n",
				__func__, off, info->event_type);
		} else {
			pr_info("%s: invalid case #%d, event(0x%04x)\n",
				__func__, off, info->event_type);
		}
		ret = count;
	}

	battery_event_control(info);

	return ret;
}

void battery_create_attrs(struct device *dev)
{
	struct battery_info *info = dev_get_drvdata(dev->parent);
	int i, rc;
	pr_info("%s\n", __func__);

	for (i = 0; i < ARRAY_SIZE(factory_attrs); i++) {
		rc = device_create_file(dev, &factory_attrs[i]);
		pr_debug("%s: factory attr: %s\n", __func__,
				factory_attrs[i].attr.name);
		if (rc)
			goto create_factory_attrs_failed;
	}
	pr_info("%s: factory attrs created\n", __func__);

	if (!info->pdata->ctia_spec) {
		pr_info("%s: not support CTIA spec\n", __func__);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(ctia_attrs); i++) {
		rc = device_create_file(dev, &ctia_attrs[i]);
		pr_debug("%s: CTIA attr: %s\n", __func__,
				ctia_attrs[i].attr.name);
		if (rc)
			goto create_ctia_attrs_failed;
	}
	pr_info("%s: CTIA attrs created\n", __func__);

	return;

create_factory_attrs_failed:
	pr_info("%s: factory attrs created failed\n", __func__);
	while (i--)
		device_remove_file(dev, &factory_attrs[i]);
	return;

create_ctia_attrs_failed:
	pr_info("%s: CTIA attrs created failed\n", __func__);
	while (i--)
		device_remove_file(dev, &ctia_attrs[i]);
	return;
}

#if defined(CONFIG_TARGET_LOCALE_KOR)
int battery_info_proc(char *buf, char **start,
			off_t offset, int count, int *eof, void *data)
{
	struct battery_info *info = data;
	struct timespec cur_time;
	ktime_t ktime;
	int len = 0;
	/* Guess we need no more than 100 bytes. */
	int size = 100;

	ktime = alarm_get_elapsed_realtime();
	cur_time = ktime_to_timespec(ktime);

	len = snprintf(buf, size,
		"%lu\t%u\t%u\t%u\t%u\t%d\t%u\t%d\t%d\t%u\t"
		"%u\t%u\t%u\t%u\t%u\t%u\t%d\t%u\t%u\n",
		cur_time.tv_sec,
		info->battery_raw_soc,
		info->battery_soc,
		info->battery_vcell / 1000,
		info->battery_vfocv / 1000,
		info->battery_full_soc,
		info->battery_present,
		info->battery_temper,
		info->battery_temper_adc,
		info->battery_health,
		info->charge_real_state,
		info->charge_virt_state,
		info->cable_type,
		info->charge_current,
		info->full_charged_state,
		info->recharge_phase,
		info->abstimer_state,
		info->monitor_interval,
		info->charge_start_time);
	return len;
}
#elif defined(CONFIG_MACH_M0_CTC)
int battery_info_proc(char *buf, char **start,
			off_t offset, int count, int *eof, void *data)
{
	struct battery_info *info = data;
	struct timespec cur_time;
	ktime_t ktime;
	int len = 0;
	/* Guess we need no more than 100 bytes. */
	int size = 100;

	ktime = alarm_get_elapsed_realtime();
	cur_time = ktime_to_timespec(ktime);

	len = snprintf(buf, size,
		"%lu\t%u\t%u\t%u\t%u\t%u\t%d\t%d\t%u\t"
		"%u\t%u\t%u\t%u\t%u\t%u\t%d\t%u\t%u\n",
		cur_time.tv_sec,
		info->battery_raw_soc,
		info->battery_soc,
		info->battery_vcell / 1000,
		info->battery_vfocv / 1000,
		info->battery_present,
		info->battery_temper,
		info->battery_temper_adc,
		info->battery_health,
		info->charge_real_state,
		info->charge_virt_state,
		info->cable_type,
		info->charge_current,
		info->full_charged_state,
		info->recharge_phase,
		info->abstimer_state,
		info->monitor_interval,
		info->charge_start_time);
	return len;
}
#endif
