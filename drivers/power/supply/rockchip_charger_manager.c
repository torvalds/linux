// SPDX-License-Identifier: GPL-2.0
/*
 * Regulator driver for Rockchip RK806
 *
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 *
 * Author: Xu Shengfei <xsf@rock-chips.com>
 *
 * This driver enables to monitor battery health and control charger
 * during suspend-to-mem.
 * Charger manager depends on other devices. Register this later than
 * the depending devices.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/of.h>
#include <linux/thermal.h>
#include <../drivers/extcon/extcon.h>
#include <linux/power_supply.h>
#include <linux/extcon.h>
#include <linux/alarmtimer.h>

static int dbg_enable;

module_param_named(dbg_level, dbg_enable, int, 0644);

#define CM_DBG(args...) \
	do { \
		if (dbg_enable) { \
			pr_info(args); \
		} \
	} while (0)

/*
 * Regard CM_JIFFIES_SMALL jiffies is small enough to ignore for
 * delayed works so that we can run delayed works with CM_JIFFIES_SMALL
 * without any delays.
 */
#define CM_JIFFIES_SMALL	(2)

/* If y is valid (> 0) and smaller than x, do x = y */
#define CM_MIN_VALID(x, y)	(x = (((y > 0) && ((x) > (y))) ? (y) : (x)))

/*
 * Regard CM_RTC_SMALL (sec) is small enough to ignore error in invoking
 * rtc alarm. It should be 2 or larger
 */
#define CM_RTC_SMALL			(2)
#define PM_WORK_RUN_INTERVAL		300
#define TAPER_TIMEOUT			(5000 / PM_WORK_RUN_INTERVAL)
#define IBUS_CHANGE_TIMEOUT		(500 / PM_WORK_RUN_INTERVAL)

#define FC_VOLTAGE_STEP		(20 * 1000)
#define FC_CURRENT_STEP		(50 * 1000)

#define USB_SDP_INPUT_CURRENT		(500 * 1000)
#define USB_DCP_INPUT_CURRENT		(1000 * 1000)
#define USB_CDP_INPUT_CURRENT		(1000 * 1000)
#define USB_TYPE_C_INPUT_CURRENT	(1000 * 1000)
#define USB_PPS_INPUT_CURRENT		(1000 * 1000)

enum polling_modes {
	CM_POLL_DISABLE = 0,
	CM_POLL_ALWAYS,
	CM_POLL_EXTERNAL_POWER_ONLY,
	CM_POLL_CHARGING_ONLY,
};

enum cm_batt_temp {
	CM_BATT_OK = 0,
	CM_BATT_OVERHEAT,
	CM_BATT_COLD,
};

enum cm_pps_state {
	PPS_PM_STATE_ENTRY,
	PPS_PM_STATE_FC_ENTRY,
	PPS_PM_STATE_FC_ENTRY_1,
	PPS_PM_STATE_FC_ENTRY_2,
	PPS_PM_STATE_FC_TUNE,
	PPS_PM_STATE_FC_EXIT,
};

struct fastcharge_config {
	/*bat volt loop limit*/
	int vbat_lmt;
	int ibat_lmt;
	int vbat_now;
	int ibat_now;
	int pcb_resistance;

	int vbus_lmt;
	int ibus_lmt;

	int vbus_dcdc_lmt;
	int ibus_dcdc_lmt;

	int vbus_cp_lmt;
	int ibus_cp_lmt;
	int vbus_cp;
	int ibus_cp;

	int adapter_volt_max_lmt;
	int adapter_curr_max_lmt;
	int adaper_power_lmt;
	int adaper_power_init_flag;
	int adapter_volt_required;
	int adapter_curr_required;
	int cable_resistance;

	int min_vbat_for_cp;
	int fc_taper_current;
	int fc_steps;
	/* disable switching charger during flash charge*/
	bool fc_disable_sw;
};

struct fuel_gauge_info {
	int vbat_now;
	int ibat_now;
	int bat_res;
	int bat_temperature;
};

struct chargepump_info {
	bool charge_enabled;
	bool batt_pres;
	bool vbus_pres;

	/* alarm/fault status */
	bool bat_ovp_fault;
	bool bat_ocp_fault;
	bool bus_ovp_fault;
	bool bus_ocp_fault;

	bool bat_ovp_alarm;
	bool bat_ocp_alarm;
	bool bus_ovp_alarm;
	bool bus_ocp_alarm;

	bool bat_ucp_alarm;

	bool bat_therm_alarm;
	bool bus_therm_alarm;
	bool die_therm_alarm;

	bool bat_therm_fault;
	bool bus_therm_fault;
	bool die_therm_fault;

	bool therm_shutdown_flag;
	bool therm_shutdown_stat;

	bool vbat_reg;
	bool ibat_reg;

	int vout_volt;
	int vbat_volt;
	int vbus_volt;
	int vbus_max;
	int ibat_curr;
	int ibus_curr;
	int ibus_max;

	int vbus_error_low;
	int vbus_error_high;

	int bat_temp;
	int bus_temp;
	int die_temp;

	int cp_alarm_status;
	int cp_fault_status;

	int request_voltage;
	int request_current;
};

/**
 * struct charger_desc
 * @psy_name: the name of power-supply-class for charger manager
 * @polling_mode:
 *	Determine which polling mode will be used
 * @polling_interval_ms: interval in millisecond at which
 *	charger manager will monitor battery health
 * @psy_charger_stat: the names of power-supply for chargers
 * @psy_fuel_gauge: the name of power-supply for fuel gauge
 * @temp_min : Minimum battery temperature for charging.
 * @temp_max : Maximum battery temperature for charging.
 * @temp_diff : Temperature difference to restart charging.
 * @measure_battery_temp:
 *	true: measure battery temperature
 *	false: measure ambient temperature
 * @charging_max_duration_ms: Maximum possible duration for charging
 *	If whole charging duration exceed 'charging_max_duration_ms',
 *	cm stop charging.
 * @discharging_max_duration_ms:
 *	Maximum possible duration for discharging with charger cable
 *	after full-batt. If discharging duration exceed 'discharging
 *	max_duration_ms', cm start charging.
 */
struct charger_desc {
	const char *psy_name;

	enum polling_modes polling_mode;
	unsigned int polling_interval_ms;
	unsigned int polling_force_enable;

	struct power_supply_battery_info info;

	const char *psy_charger_stat;
	const char *psy_charger_pump_stat;
	const char *psy_fuel_gauge;
	const char *thermal_zone;

	struct power_supply *tcpm_psy;
	struct extcon_dev *extcon_dev;

	int temp_min;
	int temp_max;
	int temp_diff;

	bool measure_battery_temp;

	u32 charging_max_duration_ms;
	u32 discharging_max_duration_ms;
};

/**
 * struct charger_manager
 * @entry: entry for list
 * @dev: device pointer
 * @desc: instance of charger_desc
 * @fuel_gauge: power_supply for fuel gauge
 * @charger_stat: array of power_supply for chargers
 * @tzd_batt : thermal zone device for battery
 * @charger_enabled: the state of charger
 * @emergency_stop:
 *	When setting true, stop charging
 * @status_save_ext_pwr_inserted:
 *	saved status of external power before entering suspend-to-RAM
 * @status_save_batt:
 *	saved status of battery before entering suspend-to-RAM
 * @charging_start_time: saved start time of enabling charging
 * @charging_end_time: saved end time of disabling charging
 * @battery_status: Current battery status
 */
struct charger_manager {
	struct list_head entry;
	struct device *dev;
	struct charger_desc *desc;

	struct workqueue_struct *cm_wq; /* init at driver add */
	struct delayed_work cm_monitor_work; /* init at driver add */

	/* The state of charger cable */
	bool attached;
	bool charger_enabled;

	enum cm_pps_state state;
	int emergency_stop;

	/* The rockchip-charger-manager use Extcon framework */
	struct work_struct wq;
	struct notifier_block nb;

	struct chargepump_info cp;
	struct fuel_gauge_info fg_info;
	struct fastcharge_config *fc_config;

	/* About in-suspend (suspend-again) monitoring */
	int fc2_taper_timer;

	unsigned long next_polling;
	u64 charging_start_time;
	u64 charging_end_time;

	int battery_status;
};

enum {
	PM_ALGO_RET_OK,
	PM_ALGO_RET_THERM_FAULT,
	PM_ALGO_RET_OTHER_FAULT,
	PM_ALGO_RET_CHG_DISABLED,
	PM_ALGO_RET_TAPER_DONE,
};

enum data_source {
	CM_BATTERY_PRESENT,
	CM_NO_BATTERY,
	CM_FUEL_GAUGE,
	CM_CHARGER_STAT,
};

static const unsigned char *pm_debug_str[] = {
	"PPS_PM_STATE_ENTRY",
	"PPS_PM_STATE_FC_ENTRY",
	"PPS_PM_STATE_FC_ENTRY_1",
	"PPS_PM_STATE_FC_ENTRY_2",
	"PPS_PM_STATE_FC_TUNE",
	"PPS_PM_STATE_FC_EXIT",
};

static struct {
	const char *name;
	u64 extcon_type;
} extcon_mapping[] = {
	/* Current textual representations */
	{ "USB", EXTCON_USB },
	{ "SDP", EXTCON_CHG_USB_SDP },
	{ "DCP", EXTCON_CHG_USB_DCP },
	{ "CDP", EXTCON_CHG_USB_CDP },
};

struct power_supply_battery_info bat_default_info = {
	.precharge_voltage_max_uv = 3600 * 1000, /* microVolts */
	.charge_term_current_ua = 100 * 1000, /* microAmps */
	.charge_restart_voltage_uv = 4100 * 1000, /* microVolts */
	.constant_charge_current_max_ua = 1000 * 1000, /* microAmps */
	.constant_charge_voltage_max_uv = 4200 * 1000, /* microVolts */
	.factory_internal_resistance_uohm = 100, /* microOhms */
};

static struct fastcharge_config fc_config_parameter = {
	.vbat_lmt = 4400 * 1000,
	.ibat_lmt = 8000 * 1000,
	.ibus_dcdc_lmt = 1000 * 1000,
	.vbus_cp_lmt = 9500 * 1000,
	.ibus_cp_lmt = 4000 * 1000,
	.fc_taper_current = 2000 * 1000,/* disable cp */
	.fc_steps = 1, /* 20mV step */
	.adaper_power_lmt = 45000 * 1000,/* mV * mA */
	.min_vbat_for_cp = 3600,
	.fc_disable_sw = true,
};

/**
 * is_ext_pwr_online - See if an external power source is attached to charge
 * @cm: the Charger Manager representing the battery.
 *
 * Returns true if at least one of the chargers of the battery has an external
 * power source attached to charge the battery regardless of whether it is
 * actually charging or not.
 */
static bool is_ext_pwr_online(struct charger_manager *cm)
{
	union power_supply_propval val;
	struct power_supply *psy;
	bool online = false;
	int ret;

	/* If at least one of them has one, it's yes. */
	psy = power_supply_get_by_name(cm->desc->psy_charger_stat);
	if (!psy) {
		dev_err(cm->dev, "Cannot find power supply \"%s\"\n",
			cm->desc->psy_charger_stat);
		return online;
	}

	ret = power_supply_get_property(psy,
					POWER_SUPPLY_PROP_ONLINE,
					&val);
	power_supply_put(psy);
	if (ret == 0 && val.intval)
		online = true;

	return online;
}

static int set_sw_charger_input_limit_current(struct charger_manager *cm,
						int input_current_ua)
{
	union power_supply_propval val;
	struct power_supply *psy;

	/* If at least one of them has one, it's yes. */
	psy = power_supply_get_by_name(cm->desc->psy_charger_stat);
	if (!psy) {
		dev_err(cm->dev, "Cannot find power supply \"%s\"\n",
			cm->desc->psy_charger_stat);
		return 0;
	}

	val.intval = input_current_ua;
	power_supply_set_property(psy,
					POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
					&val);

	val.intval = 0x01;
	power_supply_set_property(psy,
					POWER_SUPPLY_PROP_ONLINE,
					&val);

	power_supply_put(psy);
	cm->fc_config->ibus_dcdc_lmt = input_current_ua;

	return 0;
}

static int get_sw_charger_input_limit_current(struct charger_manager *cm)
{
	union power_supply_propval val;
	struct power_supply *psy;
	int ret;

	/* If at least one of them has one, it's yes. */
	psy = power_supply_get_by_name(cm->desc->psy_charger_stat);
	if (!psy) {
		dev_err(cm->dev, "Cannot find power supply \"%s\"\n",
			cm->desc->psy_charger_stat);
		return 0;
	}

	ret = power_supply_get_property(psy,
					POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
					&val);

	if (ret)
		dev_err(cm->dev, "Cannot find POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT\n");

	power_supply_put(psy);

	return val.intval;
}

/**
 * get_cp_charge_current_max - Get the voltage level of the battery
 * @cm: the Charger Manager representing the battery.
 * @uV: the voltage level returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int set_cp_enable(struct charger_manager *cm)
{
	union power_supply_propval val;
	struct power_supply *cp_psy;

	cp_psy = power_supply_get_by_name(cm->desc->psy_charger_pump_stat);
	if (!cp_psy) {
		dev_err(cm->dev, "Cannot find power supply \"%s\"\n",
		cm->desc->psy_charger_pump_stat);
		return -ENODEV;
	}

	val.intval = 1;
	power_supply_set_property(cp_psy,
					POWER_SUPPLY_PROP_CP_CHARGING_ENABLED,
					&val);

	power_supply_put(cp_psy);

	cm->cp.charge_enabled = 1;

	return 0;
}

static int set_cp_disable(struct charger_manager *cm)
{
	union power_supply_propval val;
	struct power_supply *cp_psy;

	cp_psy = power_supply_get_by_name(cm->desc->psy_charger_pump_stat);
	if (!cp_psy) {
		dev_err(cm->dev, "Cannot find power supply \"%s\"\n",
		cm->desc->psy_charger_pump_stat);
		return -ENODEV;
	}

	val.intval = 1;
	power_supply_set_property(cp_psy,
					POWER_SUPPLY_PROP_CP_CHARGING_ENABLED,
					&val);


	cm->cp.charge_enabled = 0;
	return 0;
}

/**
 * is_polling_required - Return true if need to continue polling for this CM.
 * @cm: the Charger Manager representing the battery.
 */
static bool is_polling_required(struct charger_manager *cm)
{
	CM_DBG("cm->desc->polling_mode: %d\n", cm->desc->polling_mode);
	switch (cm->desc->polling_mode) {
	case CM_POLL_DISABLE:
		return false;
	case CM_POLL_ALWAYS:
		return true;
	case CM_POLL_EXTERNAL_POWER_ONLY:
		return is_ext_pwr_online(cm);
	case CM_POLL_CHARGING_ONLY:
		return cm->attached && cm->charger_enabled;
	default:
		dev_warn(cm->dev, "Incorrect polling_mode (%d)\n",
			 cm->desc->polling_mode);
	}
	return false;
}

static void cm_update_charge_pump_status(struct charger_manager *cm)
{
	union power_supply_propval val = {0,};
	struct power_supply *cp_psy;
	int ret;

	cp_psy = power_supply_get_by_name(cm->desc->psy_charger_pump_stat);
	if (!cp_psy) {
		dev_err(cm->dev, "Cannot find power supply:%s\n",
			cm->desc->psy_charger_pump_stat);
		return;
	}

	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_VOLTAGE_NOW,
					&val);
	if (!ret)
		cm->cp.vbat_volt = val.intval;

	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CURRENT_NOW,
					&val);
	if (!ret)
		cm->cp.ibat_curr = val.intval;

	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_VBUS,
					&val);
	if (!ret)
		cm->cp.vbus_volt = val.intval;

	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_IBUS,
					&val);
	if (!ret)
		cm->cp.ibus_curr = val.intval;

	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
					&val);
	if (!ret)
		cm->cp.ibus_max = val.intval;

	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
					&val);
	if (!ret)
		cm->cp.vbus_max = val.intval;

	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_VBUS_HERROR_STATUS,
					&val);
	if (!ret)
		cm->cp.vbus_error_high = val.intval;

	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_VBUS_LERROR_STATUS,
					&val);
	if (!ret)
		cm->cp.vbus_error_low = val.intval;

	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BAT_TEMPERATURE,
					&val);
	if (!ret)
		cm->cp.bat_temp = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BUS_TEMPERATURE,
					&val);
	if (!ret)
		cm->cp.bus_temp = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_DIE_TEMPERATURE,
					&val);
	if (!ret)
		cm->cp.die_temp = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BAT_OVP_ALARM,
					&val);
	if (!ret)
		cm->cp.bat_ovp_alarm = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BAT_OCP_ALARM,
					&val);
	if (!ret)
		cm->cp.bat_ocp_alarm = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BAT_UCP_ALARM,
					&val);
	if (!ret)
		cm->cp.bat_ucp_alarm = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BUS_OVP_ALARM,
					&val);
	if (!ret)
		cm->cp.bus_ovp_alarm = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BUS_OCP_ALARM,
					&val);
	if (!ret)
		cm->cp.bus_ocp_alarm = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BAT_THERM_ALARM,
					&val);
	if (!ret)
		cm->cp.bat_therm_alarm = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BUS_THERM_ALARM,
					&val);
	if (!ret)
		cm->cp.bus_therm_alarm = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_DIE_THERM_ALARM,
					&val);
	if (!ret)
		cm->cp.die_therm_alarm = val.intval;

	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BAT_OVP_FAULT,
					&val);
	if (!ret)
		cm->cp.bat_ovp_fault = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BAT_OCP_FAULT,
					&val);
	if (!ret)
		cm->cp.bat_ocp_fault = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BUS_OVP_FAULT,
					&val);
	if (!ret)
		cm->cp.bus_ovp_fault = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BUS_OCP_FAULT,
					&val);
	if (!ret)
		cm->cp.bus_ocp_fault = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BAT_THERM_FAULT,
					&val);
	if (!ret)
		cm->cp.bat_therm_fault = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_BUS_THERM_FAULT,
					&val);
	if (!ret)
		cm->cp.bus_therm_fault = val.intval;
	ret = power_supply_get_property(cp_psy,
					POWER_SUPPLY_PROP_CP_DIE_THERM_FAULT,
					&val);
	if (!ret)
		cm->cp.die_therm_fault = val.intval;

	if (cp_psy)
		power_supply_put(cp_psy);

	CM_DBG("cm->cp.cp_alarm_status:0x%x, cm->cp.cp_fault_status:0x%x\n",
	       cm->cp.cp_alarm_status, cm->cp.cp_fault_status);
}

static void cm_charge_pump_move_state(struct charger_manager *cm,
				      enum cm_pps_state state)
{
	CM_DBG("state change:%s -> %s\n",
		pm_debug_str[cm->state], pm_debug_str[state]);

	cm->state = state;
}

static void cm_charge_limit_update(struct charger_manager *cm)
{
	struct fastcharge_config *fc_config;
	struct power_supply_battery_info info;
	union power_supply_propval val;
	int pd_adaper_volt_max;
	int pd_adaper_curr_max;
	int ibus_dcdc_lmt;

	info = cm->desc->info;
	fc_config = cm->fc_config;
	fc_config->vbat_lmt = info.constant_charge_voltage_max_uv;
	fc_config->ibat_lmt = info.constant_charge_current_max_ua;
	fc_config->vbat_now = cm->cp.vbat_volt;
	fc_config->ibat_now = cm->cp.ibat_curr;

	ibus_dcdc_lmt = get_sw_charger_input_limit_current(cm);
	if (ibus_dcdc_lmt != 0)
		fc_config->ibus_dcdc_lmt = ibus_dcdc_lmt;

	fc_config->vbus_cp_lmt = cm->cp.vbus_max;
	fc_config->ibus_cp_lmt = cm->cp.ibus_max;
	fc_config->vbus_cp = cm->cp.vbus_volt;
	fc_config->ibus_cp = cm->cp.ibus_curr;

	fc_config->ibus_lmt = info.constant_charge_current_max_ua >> 1;

	fc_config->adapter_curr_required = cm->cp.request_current;
	fc_config->adapter_volt_required = cm->cp.request_voltage;

	if (fc_config->ibus_cp > 1000 * 1000)
		fc_config->cable_resistance =
			(fc_config->adapter_volt_required - fc_config->vbus_cp) /
				((fc_config->ibus_cp + fc_config->ibus_dcdc_lmt) / 1000);

	power_supply_get_property(cm->desc->tcpm_psy,
				  POWER_SUPPLY_PROP_VOLTAGE_MAX,
				  &val);
	fc_config->adapter_volt_max_lmt = val.intval;
	pd_adaper_volt_max = val.intval;
	power_supply_get_property(cm->desc->tcpm_psy,
				  POWER_SUPPLY_PROP_CURRENT_MAX,
				  &val);
	fc_config->adapter_curr_max_lmt = val.intval;
	pd_adaper_curr_max = val.intval;

	if (!fc_config->adaper_power_init_flag) {
		fc_config->adaper_power_lmt = (pd_adaper_volt_max / 1000) * (pd_adaper_curr_max / 1000);
		fc_config->adaper_power_init_flag = 1;
	}

	CM_DBG("battery info:\n");
	CM_DBG("battery info:: charge-full-design-microamp-hours: %d\n",
	       info.charge_full_design_uah);
	CM_DBG("battery info:: factory_internal_resistance_uohm: %d\n",
	       info.factory_internal_resistance_uohm);
	CM_DBG("battery info:: charge_term_current_ua: %d\n",
	       info.charge_term_current_ua);
	CM_DBG("battery info:: constant_charge_voltage_max_uv: %d\n",
	       info.constant_charge_voltage_max_uv);
	CM_DBG("battery info:: constant_charge_current_max_ua: %d\n",
	       info.constant_charge_current_max_ua);
	CM_DBG("battery info:: precharge_current_ua: %d\n",
	       info.precharge_current_ua);
	CM_DBG("battery info:: precharge-upper-limit-microvolt: %d\n",
	       info.precharge_voltage_max_uv);

	CM_DBG("vbat_lmt: %d\n"
		"ibat_lmt: %d\n"
		"vbat_now: %d\n"
		"ibat_now: %d\n"
		"vbus_cp_lmt: %d\n"
		"ibus_cp_lmt: %d\n"
		"vbus_cp: %d\n"
		"ibus_cp: %d\n"
		"ibus_lmt: %d\n"
		"adapter_volt_max_lmt: %d\n"
		"adapter_curr_max_lmt: %d\n"
		"adapter_curr_required: %d\n"
		"adapter_volt_required: %d\n"
		"adaper_power_lmt: %d\n"
		"cable_resistance: %d\n",
		fc_config->vbat_lmt,
		fc_config->ibat_lmt,
		fc_config->vbat_now,
		fc_config->ibat_now,
		fc_config->vbus_cp_lmt,
		fc_config->ibus_cp_lmt,
		fc_config->vbus_cp,
		fc_config->ibus_cp,
		fc_config->ibus_lmt,
		fc_config->adapter_volt_max_lmt,
		fc_config->adapter_curr_max_lmt,
		fc_config->adapter_curr_required,
		fc_config->adapter_volt_required,
		fc_config->adaper_power_lmt,
		fc_config->cable_resistance);
}

static int cm_charge_pump_algo(struct charger_manager *cm)
{
	struct fastcharge_config *fc_config;
	int step_ibus_total = 0;
	int sw_ctrl_steps = 0;
	int hw_ctrl_steps = 1;
	int step_bat_reg = 0;
	int step_vbat = 0;
	int step_ibus = 0;
	int step_ibat = 0;
	int ibus_total = 0;
	int fc2_steps = 1;
	int step_ui = 0;
	int ibus_limit;
	int steps;

	fc_config = cm->fc_config;

	/* battery voltage loop*/
	if (fc_config->vbat_now > fc_config->vbat_lmt)
		step_vbat = -fc2_steps;
	else if (fc_config->vbat_now < fc_config->vbat_lmt - 10 * 1000)
		step_vbat = fc2_steps;

	/* battery charge current loop*/
	if (cm->cp.ibat_curr < fc_config->ibat_lmt)
		step_ibat = fc2_steps;
	else if (cm->cp.ibat_curr > fc_config->ibat_lmt + 100 * 1000)
		step_ibat = -fc2_steps;

	/* bus total current loop */
	ibus_limit = fc_config->adapter_curr_required; /* fc_config->ibus_lmt ; */
	ibus_total = fc_config->ibus_cp + fc_config->ibus_dcdc_lmt;/* CP + DCDC */
	if (ibus_total < ibus_limit - FC_CURRENT_STEP)
		step_ibus_total = fc2_steps;
	else if (ibus_total > ibus_limit)
		step_ibus_total = -fc2_steps;
	CM_DBG("ibus_limit: %d cm->cp.ibus_max: %d\n", ibus_limit, cm->cp.ibus_max);

	/* bus current loop*/
	/* charge pump bus current loop */
	if (fc_config->ibus_cp < fc_config->adapter_curr_required - fc_config->ibus_dcdc_lmt - 100 * 1000)
		step_ibus = fc2_steps;
	else if (fc_config->ibus_cp > fc_config->adapter_curr_required - fc_config->ibus_dcdc_lmt)
		step_ibus = -fc2_steps;

	if (cm->cp.ibus_curr > fc_config->ibus_cp_lmt)
		step_ibus = 0;

	/* the adaper voltage loop */
	if (cm->cp.request_voltage + FC_VOLTAGE_STEP < fc_config->adapter_volt_max_lmt)
		step_bat_reg = fc2_steps;

	/* the adapter power negotiation loop*/
	if ((fc_config->adapter_volt_required / 1000) * (fc_config->adapter_curr_required / 1000) <= fc_config->adaper_power_lmt)
		step_ui = fc2_steps;
	else
		cm->cp.request_current -= FC_CURRENT_STEP;

	cm->cp.request_current = min(fc_config->adapter_curr_max_lmt, cm->cp.request_current);

	sw_ctrl_steps = min(min(step_vbat, step_ibus), min(step_ibat, step_ibus_total));
	sw_ctrl_steps = min(sw_ctrl_steps, min(step_bat_reg, step_ui));

	/* hardware alarm loop */
	if (cm->cp.bat_ocp_alarm
		|| cm->cp.bus_ocp_alarm || cm->cp.bus_ovp_alarm)
		hw_ctrl_steps = -fc2_steps;
	else
		hw_ctrl_steps = fc2_steps;

	if (cm->cp.bat_therm_fault) {/* battery overheat, stop charge*/
		CM_DBG("bat_therm_fault:%d\n", cm->cp.bat_therm_fault);
		return PM_ALGO_RET_THERM_FAULT;
	} else if (cm->cp.bat_ocp_fault || cm->cp.bus_ocp_fault ||
			cm->cp.bat_ovp_fault || cm->cp.bus_ovp_fault) {
		CM_DBG("bat_ocp_fault:%d\n"
		       "bus_ocp_fault:%d\n"
		       "bat_ovp_fault:%d\n"
		       "bus_ovp_fault:%d\n",
		       cm->cp.bat_ocp_fault,
		       cm->cp.bus_ocp_fault,
		       cm->cp.bat_ovp_fault,
		       cm->cp.bus_ovp_fault);

		return PM_ALGO_RET_OTHER_FAULT;
	} else if (cm->cp.vbus_error_low || cm->cp.vbus_error_high) {
		/* go to switch, and try to ramp up*/
		pr_info("cp.charge_enabled:%d %d %d\n",
			cm->cp.charge_enabled, cm->cp.vbus_error_low, cm->cp.vbus_error_high);
		return PM_ALGO_RET_CHG_DISABLED;
	}

	/* charge pump taper charge */
	if ((fc_config->vbat_now > (fc_config->vbat_lmt - FC_VOLTAGE_STEP))
			&& (fc_config->ibat_now < fc_config->fc_taper_current)) {
		if (cm->fc2_taper_timer++ > TAPER_TIMEOUT) {
			CM_DBG("charge pump taper charging done\n");
			cm->fc2_taper_timer = 0;
			return PM_ALGO_RET_TAPER_DONE;
		}
	} else {
		cm->fc2_taper_timer = 0;
	}

	steps = min(sw_ctrl_steps, hw_ctrl_steps);
	cm->cp.request_voltage += steps * FC_VOLTAGE_STEP;

	CM_DBG(">>>>>>step_bat_reg: %d\n", step_bat_reg);
	CM_DBG(">>>>>>step_vbat: %d\n", step_vbat);
	CM_DBG(">>>>>>step_ibat: %d\n", step_ibat);
	CM_DBG(">>>>>>step_ibus: %d\n", step_ibus);
	CM_DBG(">>>>>>step_ibus_total: %d\n", step_ibus_total);
	CM_DBG(">>>>>>step_ui: %d\n", step_ui);
	CM_DBG(">>>>>>sw_ctrl_steps: %d\n", sw_ctrl_steps);
	CM_DBG(">>>>>>hw_ctrl_steps: %d\n", hw_ctrl_steps);
	CM_DBG(">>>>>>steps: %d\n", steps);
	CM_DBG(">>>>>>%d %d %d %d sw %d hw %d all %d\n",
	       step_bat_reg, step_vbat, step_ibat,
	       step_ibus, sw_ctrl_steps, hw_ctrl_steps, steps);

	return PM_ALGO_RET_OK;
}

static int cm_charge_pump_sm(struct charger_manager *cm)
{
	struct power_supply_battery_info info;
	struct fastcharge_config *fc_config;
	union power_supply_propval val;
	static int tune_vbus_retry;
	int vbus_volt_init_up;
	static bool recover;
	int adapter_volt_max;
	int bat_res;
	int ret;

	info = cm->desc->info;
	fc_config = cm->fc_config;
	switch (cm->state) {
	case PPS_PM_STATE_ENTRY:
		recover = false;
		if (cm->cp.vbat_volt < info.precharge_voltage_max_uv) {
			power_supply_get_property(cm->desc->tcpm_psy,
						  POWER_SUPPLY_PROP_CURRENT_MAX,
						  &val);
			set_sw_charger_input_limit_current(cm, val.intval);
			CM_DBG("batt_volt-%d, waiting... > %d\n",
			       cm->cp.vbat_volt, info.precharge_voltage_max_uv);
		} else if (cm->cp.vbat_volt > info.constant_charge_voltage_max_uv - 100 * 1000) {
			pr_info("batt_volt-%d is too high for cp, charging with switch charger(%duv)\n",
				cm->cp.vbat_volt, info.constant_charge_voltage_max_uv - 100 * 1000);
			cm_charge_pump_move_state(cm, PPS_PM_STATE_FC_EXIT);
		} else {
			pr_info("batt_volt-%d is ok, start flash charging\n",
					cm->cp.vbat_volt);
			cm->desc->polling_interval_ms = 300;

			val.intval = 2;
			power_supply_set_property(cm->desc->tcpm_psy,
							POWER_SUPPLY_PROP_ONLINE,
							&val);
			cm_charge_pump_move_state(cm, PPS_PM_STATE_FC_ENTRY);
		}
		break;
	case PPS_PM_STATE_FC_ENTRY:
		CM_DBG("PPS_PM_STATE_FC_ENTRY:\n");
		set_sw_charger_input_limit_current(cm, USB_PPS_INPUT_CURRENT);
		vbus_volt_init_up = info.constant_charge_current_max_ua / 1000 * info.factory_internal_resistance_uohm;

		cm->cp.request_voltage = cm->cp.vbat_volt * 2 + vbus_volt_init_up * 2;

		CM_DBG("cm->cp.request_voltage: %d,cm->cp.vbat_volt: %d, vbus_volt_init_up: %d\n",
			cm->cp.request_voltage, cm->cp.vbat_volt, vbus_volt_init_up);

		power_supply_get_property(cm->desc->tcpm_psy,
					  POWER_SUPPLY_PROP_CURRENT_MAX,
					  &val);
		CM_DBG("CURRENT_MAX:%d\n", val.intval);

		cm->cp.request_current = min(cm->cp.ibus_max + fc_config->ibus_dcdc_lmt, val.intval);

		adapter_volt_max = fc_config->adaper_power_lmt / (cm->cp.request_current / 1000);
		cm->cp.request_voltage = min(cm->cp.request_voltage, adapter_volt_max * 1000);
		val.intval = cm->cp.request_voltage;
		power_supply_set_property(cm->desc->tcpm_psy,
						POWER_SUPPLY_PROP_VOLTAGE_NOW,
						&val);

		val.intval = cm->cp.request_current;
		power_supply_set_property(cm->desc->tcpm_psy,
						POWER_SUPPLY_PROP_CURRENT_NOW,
						&val);

		CM_DBG("request_voltage:%d, request_current:%d\n",
		       cm->cp.request_voltage, cm->cp.request_current);

		if (!cm->cp.charge_enabled)
			set_cp_enable(cm);
		cm_charge_pump_move_state(cm, PPS_PM_STATE_FC_ENTRY_1);
		cm->desc->polling_interval_ms = 100;
		tune_vbus_retry = 0;

		break;
	case PPS_PM_STATE_FC_ENTRY_1:
		CM_DBG("PPS_PM_STATE_FC_ENTRY_1:\n");
		set_sw_charger_input_limit_current(cm, USB_PPS_INPUT_CURRENT);

		vbus_volt_init_up = info.constant_charge_current_max_ua / 1000 * info.factory_internal_resistance_uohm;

		if (cm->cp.vbat_volt < info.constant_charge_voltage_max_uv) {
			bat_res = info.constant_charge_current_max_ua / cm->cp.ibat_curr * info.factory_internal_resistance_uohm;
			vbus_volt_init_up *= ((bat_res - info.factory_internal_resistance_uohm) / info.factory_internal_resistance_uohm);
		}

		cm->cp.request_voltage += cm->cp.request_voltage - cm->cp.vbus_volt +
					vbus_volt_init_up * 2 +
					fc_config->cable_resistance * cm->cp.ibus_curr;
		adapter_volt_max = fc_config->adaper_power_lmt / (cm->cp.request_current / 1000);
		cm->cp.request_voltage = min(cm->cp.request_voltage, adapter_volt_max * 1000);

		val.intval  = cm->cp.request_voltage;
		power_supply_set_property(cm->desc->tcpm_psy,
						POWER_SUPPLY_PROP_VOLTAGE_NOW,
						&val);

		val.intval = cm->cp.request_current;
		power_supply_set_property(cm->desc->tcpm_psy,
						POWER_SUPPLY_PROP_CURRENT_NOW,
						&val);
		cm->desc->polling_interval_ms = 100;
		CM_DBG("request_voltage:%d, request_current:%d\n",
		       cm->cp.request_voltage, cm->cp.request_current);
		cm_charge_pump_move_state(cm, PPS_PM_STATE_FC_ENTRY_2);
		break;
	case PPS_PM_STATE_FC_ENTRY_2:
		CM_DBG("PPS_PM_STATE_FC_ENTRY_2:\n");
		set_sw_charger_input_limit_current(cm, USB_PPS_INPUT_CURRENT);
		if (cm->cp.vbus_error_low) {
			tune_vbus_retry++;
			cm->cp.request_voltage += FC_VOLTAGE_STEP;
			adapter_volt_max =
				fc_config->adaper_power_lmt / (cm->cp.request_current / 1000);
			cm->cp.request_voltage =
				min(cm->cp.request_voltage, adapter_volt_max * 1000);

			val.intval  = cm->cp.request_voltage;
			power_supply_set_property(cm->desc->tcpm_psy,
							POWER_SUPPLY_PROP_VOLTAGE_NOW,
							&val);

			CM_DBG("\trequest_voltage:%d, request_current:%d\n",
			       cm->cp.request_voltage, cm->cp.request_current);
		} else if (cm->cp.vbus_error_high) {
			tune_vbus_retry++;
			cm->cp.request_voltage -= FC_VOLTAGE_STEP;
			val.intval  = cm->cp.request_voltage;
			power_supply_set_property(cm->desc->tcpm_psy,
							POWER_SUPPLY_PROP_VOLTAGE_NOW,
							&val);
			CM_DBG("request_voltage:%d, request_current:%d\n",
					cm->cp.request_voltage, cm->cp.request_current);
		} else {
			CM_DBG("adapter volt tune ok, retry %d times\n", tune_vbus_retry);
			cm->fc2_taper_timer = 0;
			cm_charge_pump_move_state(cm, PPS_PM_STATE_FC_TUNE);
			break;
		}
		if (tune_vbus_retry > 25) {
			CM_DBG("Failed to tune adapter volt into valid range.\n");
			cm_charge_pump_move_state(cm, PPS_PM_STATE_FC_EXIT);
		}
		cm->desc->polling_interval_ms = 100;
		break;

	case PPS_PM_STATE_FC_TUNE:
		CM_DBG("PPS_PM_STATE_FC_TUNE:\n");
		set_sw_charger_input_limit_current(cm, USB_PPS_INPUT_CURRENT);
		ret = cm_charge_pump_algo(cm);
		if (ret == PM_ALGO_RET_THERM_FAULT) {
			CM_DBG("Move to stop charging\n");
			cm_charge_pump_move_state(cm, PPS_PM_STATE_FC_EXIT);
		} else if (ret == PM_ALGO_RET_OTHER_FAULT || ret == PM_ALGO_RET_TAPER_DONE) {
			CM_DBG("Move to switch charging\n");
			cm_charge_pump_move_state(cm, PPS_PM_STATE_FC_EXIT);
		} else if (ret == PM_ALGO_RET_CHG_DISABLED) {
			CM_DBG("Move to switch charging, will try to recover flash charging\n");
			recover = true;
			cm_charge_pump_move_state(cm, PPS_PM_STATE_FC_EXIT);
		} else {
			if (!cm->cp.charge_enabled)
				set_cp_enable(cm);
			cm->cp.request_voltage = min(cm->cp.request_voltage, cm->cp.vbus_max);

			val.intval = cm->cp.request_voltage;
			CM_DBG("cm->cp.request_voltage: %d\n", cm->cp.request_voltage);
			power_supply_set_property(cm->desc->tcpm_psy,
							POWER_SUPPLY_PROP_VOLTAGE_NOW,
							&val);
			val.intval = cm->cp.request_current;
			power_supply_set_property(cm->desc->tcpm_psy,
							POWER_SUPPLY_PROP_CURRENT_NOW,
							&val);
			cm->desc->polling_interval_ms = 1000;
			cm_charge_pump_move_state(cm, PPS_PM_STATE_FC_TUNE);
		}
		break;
	case PPS_PM_STATE_FC_EXIT:
		CM_DBG("PPS_PM_STATE_FC_EXIT:\n");
		if (cm->cp.charge_enabled)
			set_cp_disable(cm);
		val.intval = 1;
		power_supply_set_property(cm->desc->tcpm_psy,
						POWER_SUPPLY_PROP_ONLINE,
						&val);

		set_sw_charger_input_limit_current(cm, val.intval);

		if (recover)
			cm_charge_pump_move_state(cm, PPS_PM_STATE_ENTRY);
		break;

	}
	return 0;
}

/**
 * _cm_monitor - Monitor the temperature and return true for exceptions.
 * @cm: the Charger Manager representing the battery.
 *
 * Returns true if there is an event to notify for the battery.
 * (True if the status of "emergency_stop" changes)
 */
static void _cm_monitor(struct charger_manager *cm)
{
	cm_update_charge_pump_status(cm);
	cm_charge_limit_update(cm);
	cm_charge_pump_sm(cm);
}

/**
 * _setup_polling - Setup the next instance of polling.
 * @work: work_struct of the function _setup_polling.
 */
static void _setup_polling(struct charger_manager *cm)
{
	unsigned long polling_jiffy = ULONG_MAX; /* ULONG_MAX: no polling */
	unsigned long min = ULONG_MAX;
	bool keep_polling = false;
	unsigned long _next_polling;

	if ((is_polling_required(cm) && cm->desc->polling_interval_ms) ||
	    (cm->attached && cm->charger_enabled)) {
		CM_DBG("cm->attached: %d\n", cm->attached);
		keep_polling = true;

		if (min > cm->desc->polling_interval_ms)
			min = cm->desc->polling_interval_ms;
	}

	CM_DBG("keep_polling: %d\n", keep_polling);
	polling_jiffy = msecs_to_jiffies(min);
	if (polling_jiffy <= CM_JIFFIES_SMALL)
		polling_jiffy = CM_JIFFIES_SMALL + 1;

	if (!keep_polling)
		polling_jiffy = ULONG_MAX;

	if (polling_jiffy == ULONG_MAX)
		goto out;

	WARN(cm->cm_wq == NULL, "rockchip-charger-manager: workqueue not initialized\n");

	/*
	 * Use mod_delayed_work() iff the next polling interval should
	 * occur before the currently scheduled one.  If @cm_monitor_work
	 * isn't active, the end result is the same, so no need to worry
	 * about stale @next_polling.
	 */
	_next_polling = jiffies + polling_jiffy;

	if (time_before(_next_polling, cm->next_polling)) {
		queue_delayed_work(cm->cm_wq, &cm->cm_monitor_work, polling_jiffy);
		cm->next_polling = _next_polling;
	} else {
		if (queue_delayed_work(cm->cm_wq, &cm->cm_monitor_work, polling_jiffy))
			cm->next_polling = _next_polling;
	}
out:
	return;
}

/**
 * cm_monitor_poller - The Monitor / Poller.
 * @work: work_struct of the function cm_monitor_poller
 *
 * During non-suspended state, cm_monitor_poller is used to
 * poll and monitor the batteries.
 */
static void cm_monitor_poller(struct work_struct *work)
{
	struct charger_manager *cm = container_of(work,
						  struct charger_manager,
						  cm_monitor_work.work);

	_cm_monitor(cm);
	_setup_polling(cm);
}

/**
 * charger_extcon_work - enable/disable charger according to
 * the state of charger cable
 *
 * @work: work_struct of the function charger_extcon_work.
 */
static void charger_extcon_work(struct work_struct *work)
{
	struct charger_manager *cm = container_of(work,
						 struct charger_manager,
						 wq);

	cancel_delayed_work(&cm->cm_monitor_work);
	queue_delayed_work(cm->cm_wq, &cm->cm_monitor_work, 0);
}

/**
 * charger_extcon_notifier - receive the state of charger cable
 * when registered cable is attached or detached.
 *
 * @self: the notifier block of the charger_extcon_notifier.
 * @event: the cable state.
 * @ptr: the data pointer of notifier block.
 */

static int charger_extcon_notifier(struct notifier_block *self,
				   unsigned long event,
				   void *ptr)
{
	struct charger_manager *cm =
		container_of(self, struct charger_manager, nb);
	struct charger_desc *desc = cm->desc;
	struct fastcharge_config *fc_config;
	union power_supply_propval val;

	/*
	 * The newly state of charger cable.
	 * If cable is attached, cable->attached is true.
	 */
	cm->attached = event;
	fc_config = cm->fc_config;

	CM_DBG("%s, %d\n", __func__, cm->attached);
	if (event) {
		power_supply_get_property(desc->tcpm_psy,
					  POWER_SUPPLY_PROP_USB_TYPE,
					  &val);
		switch (val.intval) {
		case POWER_SUPPLY_USB_TYPE_PD:
			CM_DBG("POWER_SUPPLY_USB_TYPE_PD\n");
			power_supply_get_property(cm->desc->tcpm_psy,
						  POWER_SUPPLY_PROP_VOLTAGE_MAX,
						  &val);
			fc_config->adapter_volt_max_lmt = val.intval;
			power_supply_get_property(cm->desc->tcpm_psy,
						  POWER_SUPPLY_PROP_CURRENT_MAX,
						  &val);
			fc_config->adapter_curr_max_lmt = val.intval;

			CM_DBG("adapter_volt_max_lmt:%d\n", fc_config->adapter_volt_max_lmt);
			CM_DBG("adapter_curr_max_lmt:%d\n", fc_config->adapter_curr_max_lmt);
			set_sw_charger_input_limit_current(cm, fc_config->adapter_curr_max_lmt);
			CM_DBG("USB-TYPE: POWER_SUPPLY_USB_TYPE_PD\n");
		break;

		case POWER_SUPPLY_USB_TYPE_C:
			CM_DBG("POWER_SUPPLY_USB_TYPE_C\n");
			fc_config->adaper_power_init_flag = 0;
			set_sw_charger_input_limit_current(cm, USB_TYPE_C_INPUT_CURRENT);
			CM_DBG("USB-TYPE: POWER_SUPPLY_USB_TYPE_C\n");
		break;

		case POWER_SUPPLY_USB_TYPE_PD_PPS:
			CM_DBG("POWER_SUPPLY_USB_TYPE_PD_PPS\n");
			cm->charger_enabled = 1;
			set_sw_charger_input_limit_current(cm, USB_PPS_INPUT_CURRENT);
			cm_charge_pump_move_state(cm, PPS_PM_STATE_ENTRY);
			/*
			 * Setup work for controlling charger(regulator)
			 * according to charger cable.
			 */
			pm_stay_awake(cm->dev);

			queue_work(cm->cm_wq, &cm->wq);
		break;

		default:
			if (extcon_get_state(desc->extcon_dev, EXTCON_CHG_USB_SDP) > 0) {
				CM_DBG("EXTCON_CHG_USB_SDP\n");
				set_sw_charger_input_limit_current(cm, USB_SDP_INPUT_CURRENT);
			} else {
				if (extcon_get_state(desc->extcon_dev, EXTCON_CHG_USB_DCP) > 0) {
					CM_DBG("EXTCON_CHG_USB_DCP\n");
					set_sw_charger_input_limit_current(cm, USB_DCP_INPUT_CURRENT);
				}
				if (extcon_get_state(desc->extcon_dev, EXTCON_CHG_USB_CDP) > 0) {
					CM_DBG("EXTCON_CHG_USB_CDP\n");
					set_sw_charger_input_limit_current(cm, USB_CDP_INPUT_CURRENT);
				}
			}
		break;
		}
	} else {
		set_sw_charger_input_limit_current(cm, USB_SDP_INPUT_CURRENT);
		cm_charge_pump_move_state(cm, PPS_PM_STATE_ENTRY);
		if (cm->cp.charge_enabled)
			set_cp_disable(cm);
		if (cm->charger_enabled) {
			pm_relax(cm->dev);
			cm->charger_enabled = 0;
		}
	}

	return NOTIFY_DONE;
}

/**
 * charger_extcon_init - register external connector to use it
 * as the charger cable
 *
 * @cm: the Charger Manager representing the battery.
 * @cable: the Charger cable representing the external connector.
 */
static int charger_extcon_init(struct charger_manager *cm)
{
	struct charger_desc *desc = cm->desc;
	u64 extcon_type = EXTCON_NONE;
	unsigned long event;
	int ret, i;

	/*
	 * Charger manager use Extcon framework to identify
	 * the charger cable among various external connector
	 * cable (e.g., TA, USB, MHL, Dock).
	 */
	INIT_WORK(&cm->wq, charger_extcon_work);
	cm->nb.notifier_call = charger_extcon_notifier;

	if (IS_ERR_OR_NULL(desc->extcon_dev)) {
		pr_err("Cannot find extcon_dev\n");
		if (desc->extcon_dev == NULL)
			return -EPROBE_DEFER;
		else
			return PTR_ERR(desc->extcon_dev);
	}

	for (i = 0; i < ARRAY_SIZE(extcon_mapping); i++) {
		extcon_type = extcon_mapping[i].extcon_type;
		if (extcon_type == EXTCON_NONE) {
			pr_err("Cannot find cable for type %s", extcon_mapping[i].name);
			return -EINVAL;
		}

		ret = devm_extcon_register_notifier(cm->dev,
						    desc->extcon_dev,
						    extcon_type,
						    &cm->nb);
		if (ret < 0) {
			pr_err("Cannot register extcon_dev for %s\n", extcon_mapping[i].name);
			return ret;
		}

		event = extcon_get_state(desc->extcon_dev, extcon_type);
		charger_extcon_notifier(&cm->nb, event, NULL);
		CM_DBG("%s: %s\n", desc->extcon_dev->name, extcon_mapping[i].name);
	}

	return 0;
}

static const struct of_device_id charger_manager_match[] = {
	{
		.compatible = "rockchip-charger-manager",
	},
	{},
};
MODULE_DEVICE_TABLE(of, charger_manager_match);

static struct charger_desc *of_cm_parse_desc(struct device *dev)
{
	struct device_node *np = dev->of_node;
	u32 poll_mode = CM_POLL_DISABLE;
	struct charger_desc *desc;

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return ERR_PTR(-ENOMEM);

	of_property_read_u32(np, "cm-poll-mode", &poll_mode);
	desc->polling_mode = poll_mode;
	of_property_read_u32(np, "cm-poll-interval", &desc->polling_interval_ms);

	of_property_read_string(np, "cm-chargers", &desc->psy_charger_stat);
	of_property_read_string(np, "cm-charge-pump", &desc->psy_charger_pump_stat);
	of_property_read_string(np, "cm-fuel-gauge", &desc->psy_fuel_gauge);

	CM_DBG("cm-chargers: %s\n", desc->psy_charger_stat);
	CM_DBG("cm-charge-pumps: %s\n", desc->psy_charger_pump_stat);
	CM_DBG("cm-fuel-gauge: %s\n", desc->psy_fuel_gauge);

	desc->tcpm_psy = power_supply_get_by_phandle(np, "cm-chargers-phandle");
	if (IS_ERR_OR_NULL(desc->tcpm_psy)) {
		CM_DBG("cm-chargers-phandle is error\n");
		return ERR_PTR(-ENOMEM);
	}

	CM_DBG("tcpm_psy name : %s\n", desc->tcpm_psy->desc->name);

	desc->extcon_dev = extcon_get_edev_by_phandle(dev, 0);
	if (IS_ERR_OR_NULL(desc->extcon_dev)) {
		CM_DBG("CM: get extcon_edev error\n");
		return ERR_PTR(-ENOMEM);
	}
	return desc;
}

static inline struct charger_desc *cm_get_drv_data(struct platform_device *pdev)
{
	if (pdev->dev.of_node)
		return of_cm_parse_desc(&pdev->dev);
	return dev_get_platdata(&pdev->dev);
}

static int charger_manager_probe(struct platform_device *pdev)
{
	struct charger_desc *desc = cm_get_drv_data(pdev);
	struct power_supply_battery_info info;
	struct power_supply charger_psy;
	struct charger_manager *cm;
	int ret;

	if (IS_ERR(desc)) {
		dev_err(&pdev->dev, "No platform data (desc) found\n");
		return PTR_ERR(desc);
	}

	cm = devm_kzalloc(&pdev->dev, sizeof(*cm), GFP_KERNEL);
	if (!cm)
		return -ENOMEM;

	/* Basic Values. Unspecified are Null or 0 */
	cm->dev = &pdev->dev;
	cm->desc = desc;

	if (!desc->psy_charger_stat) {
		dev_err(&pdev->dev, "No power supply defined\n");
		return -EINVAL;
	}

	if (!desc->psy_fuel_gauge) {
		dev_err(&pdev->dev, "No fuel gauge power supply defined\n");
		return -EINVAL;
	}

	if (!desc->psy_charger_pump_stat)
		dev_err(&pdev->dev, "Cannot find charge pump power supply\n");

	if (cm->desc->polling_mode != CM_POLL_DISABLE &&
	    (desc->polling_interval_ms == 0 ||
	     msecs_to_jiffies(desc->polling_interval_ms) <= CM_JIFFIES_SMALL)) {
		dev_err(&pdev->dev, "polling_interval_ms is too small\n");
		return -EINVAL;
	}

	platform_set_drvdata(pdev, cm);
	charger_psy.of_node = cm->dev->of_node;
	ret  = power_supply_get_battery_info(&charger_psy, &(desc->info));
	if (ret) {
		info = bat_default_info;
		desc->info = bat_default_info;
		dev_err(&pdev->dev, "failed to get battery information\n");
	} else
		info = desc->info;

	cm->fc_config = &fc_config_parameter;

	CM_DBG("battery info:\n");
	CM_DBG("INFO: charge-full-design-microamp-hours: %d\n",
	       info.charge_full_design_uah);
	CM_DBG("INFO:factory_internal_resistance_uohm: %d\n",
	       info.factory_internal_resistance_uohm);
	CM_DBG("charge_term_current_ua: %d\n",
	       info.charge_term_current_ua);
	CM_DBG("constant_charge_voltage_max_uv: %d\n",
	       info.constant_charge_voltage_max_uv);
	CM_DBG("constant_charge_current_max_ua: %d\n",
	       info.constant_charge_current_max_ua);
	CM_DBG("precharge_current_ua: %d\n",
	       info.precharge_current_ua);
	CM_DBG("precharge-upper-limit-microvolt: %d\n",
	       info.precharge_voltage_max_uv);

	cm->cm_wq = alloc_ordered_workqueue("%s",
					    WQ_MEM_RECLAIM | WQ_FREEZABLE,
					    "cm-charger-wq");
	if (unlikely(!cm->cm_wq)) {
		dev_err(&pdev->dev, "failed to alloc ordered charger manager wq\n");
		return -ENOMEM;
	}
	INIT_DELAYED_WORK(&cm->cm_monitor_work,
			  cm_monitor_poller);

	/* Register extcon device for charger cable */
	ret = charger_extcon_init(cm);
	if (ret < 0) {
		dev_err(&pdev->dev, "Cannot initialize extcon device\n");
		goto err_reg_extcon;
	}

	/*
	 * Charger-manager is capable of waking up the system
	 * from sleep when event is happened through
	 * cm_notify_event()
	 */
	device_init_wakeup(&pdev->dev, true);
	device_set_wakeup_capable(&pdev->dev, false);

	return 0;
err_reg_extcon:

	return ret;
}

static int charger_manager_remove(struct platform_device *pdev)
{
	struct charger_manager *cm = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&cm->cm_monitor_work);
	return 0;
}

static const struct platform_device_id charger_manager_id[] = {
	{ "fast-charger-manager", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, charger_manager_id);

static struct platform_driver charger_manager_driver = {
	.driver = {
		.name = "fast-charger-manager",
		.of_match_table = charger_manager_match,
	},
	.probe = charger_manager_probe,
	.remove = charger_manager_remove,
	.id_table = charger_manager_id,
};

static int __init charger_manager_init(void)
{
	return platform_driver_register(&charger_manager_driver);
}
late_initcall(charger_manager_init);

static void __exit charger_manager_cleanup(void)
{
	platform_driver_unregister(&charger_manager_driver);
}
module_exit(charger_manager_cleanup);

MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_DESCRIPTION("Charger Manager");
MODULE_LICENSE("GPL");
