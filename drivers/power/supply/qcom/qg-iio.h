/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QG_IIO_H
#define __QG_IIO_H

#include <linux/iio/iio.h>
#include <dt-bindings/iio/qti_power_supply_iio.h>

struct qg_iio_channels {
	const char *datasheet_name;
	int channel_num;
	enum iio_chan_type type;
	long info_mask;
};

#define QG_IIO_CHAN(_name, _num, _type, _mask)		\
	{						\
		.datasheet_name = _name,		\
		.channel_num = _num,			\
		.type = _type,				\
		.info_mask = _mask,			\
	},

#define QG_CHAN_VOLT(_name, _num)			\
	QG_IIO_CHAN(_name, _num, IIO_VOLTAGE,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define QG_CHAN_CUR(_name, _num)			\
	QG_IIO_CHAN(_name, _num, IIO_CURRENT,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define QG_CHAN_RES(_name, _num)			\
	QG_IIO_CHAN(_name, _num, IIO_RESISTANCE,	\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define QG_CHAN_TEMP(_name, _num)			\
	QG_IIO_CHAN(_name, _num, IIO_TEMP,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define QG_CHAN_POW(_name, _num)			\
	QG_IIO_CHAN(_name, _num, IIO_POWER,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define QG_CHAN_ENERGY(_name, _num)			\
	QG_IIO_CHAN(_name, _num, IIO_ENERGY,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define QG_CHAN_INDEX(_name, _num)			\
	QG_IIO_CHAN(_name, _num, IIO_INDEX,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define QG_CHAN_ACT(_name, _num)			\
	QG_IIO_CHAN(_name, _num, IIO_ACTIVITY,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define QG_CHAN_TSTAMP(_name, _num)			\
	QG_IIO_CHAN(_name, _num, IIO_TIMESTAMP,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define QG_CHAN_COUNT(_name, _num)			\
	QG_IIO_CHAN(_name, _num, IIO_COUNT,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

static const struct qg_iio_channels qg_iio_psy_channels[] = {
	QG_CHAN_ENERGY("capacity", PSY_IIO_CAPACITY)
	QG_CHAN_ENERGY("capacity_raw", PSY_IIO_CAPACITY_RAW)
	QG_CHAN_ENERGY("real_capacity", PSY_IIO_REAL_CAPACITY)
	QG_CHAN_TEMP("temp", PSY_IIO_TEMP)
	QG_CHAN_VOLT("voltage_now", PSY_IIO_VOLTAGE_NOW)
	QG_CHAN_VOLT("voltage_ocv", PSY_IIO_VOLTAGE_OCV)
	QG_CHAN_CUR("current_now", PSY_IIO_CURRENT_NOW)
	QG_CHAN_ENERGY("charge_counter", PSY_IIO_CHARGE_COUNTER)
	QG_CHAN_RES("resistance", PSY_IIO_RESISTANCE)
	QG_CHAN_RES("resistance_id", PSY_IIO_RESISTANCE_ID)
	QG_CHAN_ACT("soc_reporting_ready", PSY_IIO_SOC_REPORTING_READY)
	QG_CHAN_RES("resistance_capacitive", PSY_IIO_RESISTANCE_CAPACITIVE)
	QG_CHAN_INDEX("debug_battery", PSY_IIO_DEBUG_BATTERY)
	QG_CHAN_VOLT("voltage_min", PSY_IIO_VOLTAGE_MIN)
	QG_CHAN_VOLT("voltage_max", PSY_IIO_VOLTAGE_MAX)
	QG_CHAN_CUR("batt_full_current", PSY_IIO_BATT_FULL_CURRENT)
	QG_CHAN_INDEX("batt_profile_version", PSY_IIO_BATT_PROFILE_VERSION)
	QG_CHAN_COUNT("cycle_count", PSY_IIO_CYCLE_COUNT)
	QG_CHAN_ENERGY("charge_full", PSY_IIO_CHARGE_FULL)
	QG_CHAN_ENERGY("charge_full_design", PSY_IIO_CHARGE_FULL_DESIGN)
	QG_CHAN_TSTAMP("time_to_full_avg", PSY_IIO_TIME_TO_FULL_AVG)
	QG_CHAN_TSTAMP("time_to_full_now", PSY_IIO_TIME_TO_FULL_NOW)
	QG_CHAN_TSTAMP("time_to_empty_avg", PSY_IIO_TIME_TO_EMPTY_AVG)
	QG_CHAN_RES("esr_actual", PSY_IIO_ESR_ACTUAL)
	QG_CHAN_RES("esr_nominal", PSY_IIO_ESR_NOMINAL)
	QG_CHAN_INDEX("soh", PSY_IIO_SOH)
	QG_CHAN_INDEX("clear_soh", PSY_IIO_CLEAR_SOH)
	QG_CHAN_ENERGY("cc_soc", PSY_IIO_CC_SOC)
	QG_CHAN_ACT("fg_reset", PSY_IIO_FG_RESET)
	QG_CHAN_VOLT("voltage_avg", PSY_IIO_VOLTAGE_AVG)
	QG_CHAN_CUR("current_avg", PSY_IIO_CURRENT_AVG)
	QG_CHAN_POW("power_avg", PSY_IIO_POWER_AVG)
	QG_CHAN_POW("power_now", PSY_IIO_POWER_NOW)
	QG_CHAN_ACT("scale_mode_en", PSY_IIO_SCALE_MODE_EN)
	QG_CHAN_INDEX("batt_age_level", PSY_IIO_BATT_AGE_LEVEL)
	QG_CHAN_ACT("fg_type", PSY_IIO_FG_TYPE)
};

enum qg_ext_iio_channels {
	INPUT_CURRENT_LIMITED = 0,
	RECHARGE_SOC,
	FORCE_RECHARGE,
	CHARGE_DONE,
	PARALLEL_CHARGING_ENABLED,
	CP_CHARGING_ENABLED,
};

static const char * const qg_ext_iio_chan_name[] = {
	[INPUT_CURRENT_LIMITED]	= "input_current_limited",
	[RECHARGE_SOC]			= "recharge_soc",
	[FORCE_RECHARGE]		= "force_recharge",
	[CHARGE_DONE]			= "charge_done",
	[PARALLEL_CHARGING_ENABLED]	= "parallel_charging_enabled",
	[CP_CHARGING_ENABLED]		= "cp_charging_enabled",
};

#endif
