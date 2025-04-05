// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2012
 * Copyright (c) 2012 Sony Mobile Communications AB
 *
 * Charging algorithm driver for AB8500
 *
 * Authors:
 *	Johan Palsson <johan.palsson@stericsson.com>
 *	Karl Komierowski <karl.komierowski@stericsson.com>
 *	Arun R Murthy <arun.murthy@stericsson.com>
 *	Author: Imre Sunyi <imre.sunyi@sonymobile.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/component.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/of.h>
#include <linux/mfd/core.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/notifier.h>

#include "ab8500-bm.h"
#include "ab8500-chargalg.h"

/* Watchdog kick interval */
#define CHG_WD_INTERVAL			(6 * HZ)

/* End-of-charge criteria counter */
#define EOC_COND_CNT			10

/* One hour expressed in seconds */
#define ONE_HOUR_IN_SECONDS            3600

/* Five minutes expressed in seconds */
#define FIVE_MINUTES_IN_SECONDS        300

/*
 * This is the battery capacity limit that will trigger a new
 * full charging cycle in the case where maintenance charging
 * has been disabled
 */
#define AB8500_RECHARGE_CAP		95

enum ab8500_chargers {
	NO_CHG,
	AC_CHG,
	USB_CHG,
};

struct ab8500_chargalg_charger_info {
	enum ab8500_chargers conn_chg;
	enum ab8500_chargers prev_conn_chg;
	enum ab8500_chargers online_chg;
	enum ab8500_chargers prev_online_chg;
	enum ab8500_chargers charger_type;
	bool usb_chg_ok;
	bool ac_chg_ok;
	int usb_volt_uv;
	int usb_curr_ua;
	int ac_volt_uv;
	int ac_curr_ua;
	int usb_vset_uv;
	int usb_iset_ua;
	int ac_vset_uv;
	int ac_iset_ua;
};

struct ab8500_chargalg_battery_data {
	int temp;
	int volt_uv;
	int avg_curr_ua;
	int inst_curr_ua;
	int percent;
};

enum ab8500_chargalg_states {
	STATE_HANDHELD_INIT,
	STATE_HANDHELD,
	STATE_CHG_NOT_OK_INIT,
	STATE_CHG_NOT_OK,
	STATE_HW_TEMP_PROTECT_INIT,
	STATE_HW_TEMP_PROTECT,
	STATE_NORMAL_INIT,
	STATE_NORMAL,
	STATE_WAIT_FOR_RECHARGE_INIT,
	STATE_WAIT_FOR_RECHARGE,
	STATE_MAINTENANCE_A_INIT,
	STATE_MAINTENANCE_A,
	STATE_MAINTENANCE_B_INIT,
	STATE_MAINTENANCE_B,
	STATE_TEMP_UNDEROVER_INIT,
	STATE_TEMP_UNDEROVER,
	STATE_TEMP_LOWHIGH_INIT,
	STATE_TEMP_LOWHIGH,
	STATE_OVV_PROTECT_INIT,
	STATE_OVV_PROTECT,
	STATE_SAFETY_TIMER_EXPIRED_INIT,
	STATE_SAFETY_TIMER_EXPIRED,
	STATE_BATT_REMOVED_INIT,
	STATE_BATT_REMOVED,
	STATE_WD_EXPIRED_INIT,
	STATE_WD_EXPIRED,
};

static const char * const states[] = {
	"HANDHELD_INIT",
	"HANDHELD",
	"CHG_NOT_OK_INIT",
	"CHG_NOT_OK",
	"HW_TEMP_PROTECT_INIT",
	"HW_TEMP_PROTECT",
	"NORMAL_INIT",
	"NORMAL",
	"WAIT_FOR_RECHARGE_INIT",
	"WAIT_FOR_RECHARGE",
	"MAINTENANCE_A_INIT",
	"MAINTENANCE_A",
	"MAINTENANCE_B_INIT",
	"MAINTENANCE_B",
	"TEMP_UNDEROVER_INIT",
	"TEMP_UNDEROVER",
	"TEMP_LOWHIGH_INIT",
	"TEMP_LOWHIGH",
	"OVV_PROTECT_INIT",
	"OVV_PROTECT",
	"SAFETY_TIMER_EXPIRED_INIT",
	"SAFETY_TIMER_EXPIRED",
	"BATT_REMOVED_INIT",
	"BATT_REMOVED",
	"WD_EXPIRED_INIT",
	"WD_EXPIRED",
};

struct ab8500_chargalg_events {
	bool batt_unknown;
	bool mainextchnotok;
	bool batt_ovv;
	bool batt_rem;
	bool btemp_underover;
	bool btemp_low;
	bool btemp_high;
	bool main_thermal_prot;
	bool usb_thermal_prot;
	bool main_ovv;
	bool vbus_ovv;
	bool usbchargernotok;
	bool safety_timer_expired;
	bool maintenance_timer_expired;
	bool ac_wd_expired;
	bool usb_wd_expired;
	bool ac_cv_active;
	bool usb_cv_active;
	bool vbus_collapsed;
};

/**
 * struct ab8500_charge_curr_maximization - Charger maximization parameters
 * @original_iset_ua:	the non optimized/maximised charger current
 * @current_iset_ua:	the charging current used at this moment
 * @condition_cnt:	number of iterations needed before a new charger current
			is set
 * @max_current_ua:	maximum charger current
 * @wait_cnt:		to avoid too fast current step down in case of charger
 *			voltage collapse, we insert this delay between step
 *			down
 * @level:		tells in how many steps the charging current has been
			increased
 */
struct ab8500_charge_curr_maximization {
	int original_iset_ua;
	int current_iset_ua;
	int condition_cnt;
	int max_current_ua;
	int wait_cnt;
	u8 level;
};

enum maxim_ret {
	MAXIM_RET_NOACTION,
	MAXIM_RET_CHANGE,
	MAXIM_RET_IBAT_TOO_HIGH,
};

/**
 * struct ab8500_chargalg - ab8500 Charging algorithm device information
 * @dev:		pointer to the structure device
 * @charge_status:	battery operating status
 * @eoc_cnt:		counter used to determine end-of_charge
 * @maintenance_chg:	indicate if maintenance charge is active
 * @t_hyst_norm		temperature hysteresis when the temperature has been
 *			over or under normal limits
 * @t_hyst_lowhigh	temperature hysteresis when the temperature has been
 *			over or under the high or low limits
 * @charge_state:	current state of the charging algorithm
 * @ccm			charging current maximization parameters
 * @chg_info:		information about connected charger types
 * @batt_data:		data of the battery
 * @bm:           	Platform specific battery management information
 * @parent:		pointer to the struct ab8500
 * @chargalg_psy:	structure that holds the battery properties exposed by
 *			the charging algorithm
 * @events:		structure for information about events triggered
 * @chargalg_wq:		work queue for running the charging algorithm
 * @chargalg_periodic_work:	work to run the charging algorithm periodically
 * @chargalg_wd_work:		work to kick the charger watchdog periodically
 * @chargalg_work:		work to run the charging algorithm instantly
 * @safety_timer:		charging safety timer
 * @maintenance_timer:		maintenance charging timer
 * @chargalg_kobject:		structure of type kobject
 */
struct ab8500_chargalg {
	struct device *dev;
	int charge_status;
	int eoc_cnt;
	bool maintenance_chg;
	int t_hyst_norm;
	int t_hyst_lowhigh;
	enum ab8500_chargalg_states charge_state;
	struct ab8500_charge_curr_maximization ccm;
	struct ab8500_chargalg_charger_info chg_info;
	struct ab8500_chargalg_battery_data batt_data;
	struct ab8500 *parent;
	struct ab8500_bm_data *bm;
	struct power_supply *chargalg_psy;
	struct ux500_charger *ac_chg;
	struct ux500_charger *usb_chg;
	struct ab8500_chargalg_events events;
	struct workqueue_struct *chargalg_wq;
	struct delayed_work chargalg_periodic_work;
	struct delayed_work chargalg_wd_work;
	struct work_struct chargalg_work;
	struct hrtimer safety_timer;
	struct hrtimer maintenance_timer;
	struct kobject chargalg_kobject;
};

/* Main battery properties */
static enum power_supply_property ab8500_chargalg_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
};

/**
 * ab8500_chargalg_safety_timer_expired() - Expiration of the safety timer
 * @timer:     pointer to the hrtimer structure
 *
 * This function gets called when the safety timer for the charger
 * expires
 */
static enum hrtimer_restart
ab8500_chargalg_safety_timer_expired(struct hrtimer *timer)
{
	struct ab8500_chargalg *di = container_of(timer, struct ab8500_chargalg,
						  safety_timer);
	dev_err(di->dev, "Safety timer expired\n");
	di->events.safety_timer_expired = true;

	/* Trigger execution of the algorithm instantly */
	queue_work(di->chargalg_wq, &di->chargalg_work);

	return HRTIMER_NORESTART;
}

/**
 * ab8500_chargalg_maintenance_timer_expired() - Expiration of
 * the maintenance timer
 * @timer:     pointer to the timer structure
 *
 * This function gets called when the maintenance timer
 * expires
 */
static enum hrtimer_restart
ab8500_chargalg_maintenance_timer_expired(struct hrtimer *timer)
{

	struct ab8500_chargalg *di = container_of(timer, struct ab8500_chargalg,
						  maintenance_timer);

	dev_dbg(di->dev, "Maintenance timer expired\n");
	di->events.maintenance_timer_expired = true;

	/* Trigger execution of the algorithm instantly */
	queue_work(di->chargalg_wq, &di->chargalg_work);

	return HRTIMER_NORESTART;
}

/**
 * ab8500_chargalg_state_to() - Change charge state
 * @di:		pointer to the ab8500_chargalg structure
 *
 * This function gets called when a charge state change should occur
 */
static void ab8500_chargalg_state_to(struct ab8500_chargalg *di,
	enum ab8500_chargalg_states state)
{
	dev_dbg(di->dev,
		"State changed: %s (From state: [%d] %s =to=> [%d] %s )\n",
		di->charge_state == state ? "NO" : "YES",
		di->charge_state,
		states[di->charge_state],
		state,
		states[state]);

	di->charge_state = state;
}

static int ab8500_chargalg_check_charger_enable(struct ab8500_chargalg *di)
{
	struct power_supply_battery_info *bi = di->bm->bi;

	switch (di->charge_state) {
	case STATE_NORMAL:
	case STATE_MAINTENANCE_A:
	case STATE_MAINTENANCE_B:
		break;
	default:
		return 0;
	}

	if (di->chg_info.charger_type & USB_CHG) {
		return di->usb_chg->ops.check_enable(di->usb_chg,
			bi->constant_charge_voltage_max_uv,
			bi->constant_charge_current_max_ua);
	} else if (di->chg_info.charger_type & AC_CHG) {
		return di->ac_chg->ops.check_enable(di->ac_chg,
			bi->constant_charge_voltage_max_uv,
			bi->constant_charge_current_max_ua);
	}
	return 0;
}

/**
 * ab8500_chargalg_check_charger_connection() - Check charger connection change
 * @di:		pointer to the ab8500_chargalg structure
 *
 * This function will check if there is a change in the charger connection
 * and change charge state accordingly. AC has precedence over USB.
 */
static int ab8500_chargalg_check_charger_connection(struct ab8500_chargalg *di)
{
	if (di->chg_info.conn_chg != di->chg_info.prev_conn_chg) {
		/* Charger state changed since last update */
		if (di->chg_info.conn_chg & AC_CHG) {
			dev_info(di->dev, "Charging source is AC\n");
			if (di->chg_info.charger_type != AC_CHG) {
				di->chg_info.charger_type = AC_CHG;
				ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
			}
		} else if (di->chg_info.conn_chg & USB_CHG) {
			dev_info(di->dev, "Charging source is USB\n");
			di->chg_info.charger_type = USB_CHG;
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
		} else {
			dev_dbg(di->dev, "Charging source is OFF\n");
			di->chg_info.charger_type = NO_CHG;
			ab8500_chargalg_state_to(di, STATE_HANDHELD_INIT);
		}
		di->chg_info.prev_conn_chg = di->chg_info.conn_chg;
	}
	return di->chg_info.conn_chg;
}

/**
 * ab8500_chargalg_start_safety_timer() - Start charging safety timer
 * @di:		pointer to the ab8500_chargalg structure
 *
 * The safety timer is used to avoid overcharging of old or bad batteries.
 * There are different timers for AC and USB
 */
static void ab8500_chargalg_start_safety_timer(struct ab8500_chargalg *di)
{
	/* Charger-dependent expiration time in hours*/
	int timer_expiration = 0;

	switch (di->chg_info.charger_type) {
	case AC_CHG:
		timer_expiration = di->bm->main_safety_tmr_h;
		break;

	case USB_CHG:
		timer_expiration = di->bm->usb_safety_tmr_h;
		break;

	default:
		dev_err(di->dev, "Unknown charger to charge from\n");
		break;
	}

	di->events.safety_timer_expired = false;
	hrtimer_set_expires_range(&di->safety_timer,
		ktime_set(timer_expiration * ONE_HOUR_IN_SECONDS, 0),
		ktime_set(FIVE_MINUTES_IN_SECONDS, 0));
	hrtimer_start_expires(&di->safety_timer, HRTIMER_MODE_REL);
}

/**
 * ab8500_chargalg_stop_safety_timer() - Stop charging safety timer
 * @di:		pointer to the ab8500_chargalg structure
 *
 * The safety timer is stopped whenever the NORMAL state is exited
 */
static void ab8500_chargalg_stop_safety_timer(struct ab8500_chargalg *di)
{
	if (hrtimer_try_to_cancel(&di->safety_timer) >= 0)
		di->events.safety_timer_expired = false;
}

/**
 * ab8500_chargalg_start_maintenance_timer() - Start charging maintenance timer
 * @di:		pointer to the ab8500_chargalg structure
 * @duration:	duration of the maintenance timer in minutes
 *
 * The maintenance timer is used to maintain the charge in the battery once
 * the battery is considered full. These timers are chosen to match the
 * discharge curve of the battery
 */
static void ab8500_chargalg_start_maintenance_timer(struct ab8500_chargalg *di,
	int duration)
{
	/* Set a timer in minutes with a 30 second range */
	hrtimer_set_expires_range(&di->maintenance_timer,
		ktime_set(duration * 60, 0),
		ktime_set(30, 0));
	di->events.maintenance_timer_expired = false;
	hrtimer_start_expires(&di->maintenance_timer, HRTIMER_MODE_REL);
}

/**
 * ab8500_chargalg_stop_maintenance_timer() - Stop maintenance timer
 * @di:		pointer to the ab8500_chargalg structure
 *
 * The maintenance timer is stopped whenever maintenance ends or when another
 * state is entered
 */
static void ab8500_chargalg_stop_maintenance_timer(struct ab8500_chargalg *di)
{
	if (hrtimer_try_to_cancel(&di->maintenance_timer) >= 0)
		di->events.maintenance_timer_expired = false;
}

/**
 * ab8500_chargalg_kick_watchdog() - Kick charger watchdog
 * @di:		pointer to the ab8500_chargalg structure
 *
 * The charger watchdog have to be kicked periodically whenever the charger is
 * on, else the ABB will reset the system
 */
static int ab8500_chargalg_kick_watchdog(struct ab8500_chargalg *di)
{
	/* Check if charger exists and kick watchdog if charging */
	if (di->ac_chg && di->ac_chg->ops.kick_wd &&
	    di->chg_info.online_chg & AC_CHG) {
		return di->ac_chg->ops.kick_wd(di->ac_chg);
	} else if (di->usb_chg && di->usb_chg->ops.kick_wd &&
			di->chg_info.online_chg & USB_CHG)
		return di->usb_chg->ops.kick_wd(di->usb_chg);

	return -ENXIO;
}

/**
 * ab8500_chargalg_ac_en() - Turn on/off the AC charger
 * @di:		pointer to the ab8500_chargalg structure
 * @enable:	charger on/off
 * @vset_uv:	requested charger output voltage in microvolt
 * @iset_ua:	requested charger output current in microampere
 *
 * The AC charger will be turned on/off with the requested charge voltage and
 * current
 */
static int ab8500_chargalg_ac_en(struct ab8500_chargalg *di, int enable,
	int vset_uv, int iset_ua)
{
	if (!di->ac_chg || !di->ac_chg->ops.enable)
		return -ENXIO;

	/* Select maximum of what both the charger and the battery supports */
	if (di->ac_chg->max_out_volt_uv)
		vset_uv = min(vset_uv, di->ac_chg->max_out_volt_uv);
	if (di->ac_chg->max_out_curr_ua)
		iset_ua = min(iset_ua, di->ac_chg->max_out_curr_ua);

	di->chg_info.ac_iset_ua = iset_ua;
	di->chg_info.ac_vset_uv = vset_uv;

	return di->ac_chg->ops.enable(di->ac_chg, enable, vset_uv, iset_ua);
}

/**
 * ab8500_chargalg_usb_en() - Turn on/off the USB charger
 * @di:		pointer to the ab8500_chargalg structure
 * @enable:	charger on/off
 * @vset_uv:	requested charger output voltage in microvolt
 * @iset_ua:	requested charger output current in microampere
 *
 * The USB charger will be turned on/off with the requested charge voltage and
 * current
 */
static int ab8500_chargalg_usb_en(struct ab8500_chargalg *di, int enable,
	int vset_uv, int iset_ua)
{
	if (!di->usb_chg || !di->usb_chg->ops.enable)
		return -ENXIO;

	/* Select maximum of what both the charger and the battery supports */
	if (di->usb_chg->max_out_volt_uv)
		vset_uv = min(vset_uv, di->usb_chg->max_out_volt_uv);
	if (di->usb_chg->max_out_curr_ua)
		iset_ua = min(iset_ua, di->usb_chg->max_out_curr_ua);

	di->chg_info.usb_iset_ua = iset_ua;
	di->chg_info.usb_vset_uv = vset_uv;

	return di->usb_chg->ops.enable(di->usb_chg, enable, vset_uv, iset_ua);
}

/**
 * ab8500_chargalg_update_chg_curr() - Update charger current
 * @di:		pointer to the ab8500_chargalg structure
 * @iset_ua:	requested charger output current in microampere
 *
 * The charger output current will be updated for the charger
 * that is currently in use
 */
static int ab8500_chargalg_update_chg_curr(struct ab8500_chargalg *di,
		int iset_ua)
{
	/* Check if charger exists and update current if charging */
	if (di->ac_chg && di->ac_chg->ops.update_curr &&
			di->chg_info.charger_type & AC_CHG) {
		/*
		 * Select maximum of what both the charger
		 * and the battery supports
		 */
		if (di->ac_chg->max_out_curr_ua)
			iset_ua = min(iset_ua, di->ac_chg->max_out_curr_ua);

		di->chg_info.ac_iset_ua = iset_ua;

		return di->ac_chg->ops.update_curr(di->ac_chg, iset_ua);
	} else if (di->usb_chg && di->usb_chg->ops.update_curr &&
			di->chg_info.charger_type & USB_CHG) {
		/*
		 * Select maximum of what both the charger
		 * and the battery supports
		 */
		if (di->usb_chg->max_out_curr_ua)
			iset_ua = min(iset_ua, di->usb_chg->max_out_curr_ua);

		di->chg_info.usb_iset_ua = iset_ua;

		return di->usb_chg->ops.update_curr(di->usb_chg, iset_ua);
	}

	return -ENXIO;
}

/**
 * ab8500_chargalg_stop_charging() - Stop charging
 * @di:		pointer to the ab8500_chargalg structure
 *
 * This function is called from any state where charging should be stopped.
 * All charging is disabled and all status parameters and timers are changed
 * accordingly
 */
static void ab8500_chargalg_stop_charging(struct ab8500_chargalg *di)
{
	ab8500_chargalg_ac_en(di, false, 0, 0);
	ab8500_chargalg_usb_en(di, false, 0, 0);
	ab8500_chargalg_stop_safety_timer(di);
	ab8500_chargalg_stop_maintenance_timer(di);
	di->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	di->maintenance_chg = false;
	cancel_delayed_work(&di->chargalg_wd_work);
	power_supply_changed(di->chargalg_psy);
}

/**
 * ab8500_chargalg_hold_charging() - Pauses charging
 * @di:		pointer to the ab8500_chargalg structure
 *
 * This function is called in the case where maintenance charging has been
 * disabled and instead a battery voltage mode is entered to check when the
 * battery voltage has reached a certain recharge voltage
 */
static void ab8500_chargalg_hold_charging(struct ab8500_chargalg *di)
{
	ab8500_chargalg_ac_en(di, false, 0, 0);
	ab8500_chargalg_usb_en(di, false, 0, 0);
	ab8500_chargalg_stop_safety_timer(di);
	ab8500_chargalg_stop_maintenance_timer(di);
	di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
	di->maintenance_chg = false;
	cancel_delayed_work(&di->chargalg_wd_work);
	power_supply_changed(di->chargalg_psy);
}

/**
 * ab8500_chargalg_start_charging() - Start the charger
 * @di:		pointer to the ab8500_chargalg structure
 * @vset_uv:	requested charger output voltage in microvolt
 * @iset_ua:	requested charger output current in microampere
 *
 * A charger will be enabled depending on the requested charger type that was
 * detected previously.
 */
static void ab8500_chargalg_start_charging(struct ab8500_chargalg *di,
	int vset_uv, int iset_ua)
{
	switch (di->chg_info.charger_type) {
	case AC_CHG:
		dev_dbg(di->dev,
			"AC parameters: Vset %d, Ich %d\n", vset_uv, iset_ua);
		ab8500_chargalg_usb_en(di, false, 0, 0);
		ab8500_chargalg_ac_en(di, true, vset_uv, iset_ua);
		break;

	case USB_CHG:
		dev_dbg(di->dev,
			"USB parameters: Vset %d, Ich %d\n", vset_uv, iset_ua);
		ab8500_chargalg_ac_en(di, false, 0, 0);
		ab8500_chargalg_usb_en(di, true, vset_uv, iset_ua);
		break;

	default:
		dev_err(di->dev, "Unknown charger to charge from\n");
		break;
	}
}

/**
 * ab8500_chargalg_check_temp() - Check battery temperature ranges
 * @di:		pointer to the ab8500_chargalg structure
 *
 * The battery temperature is checked against the predefined limits and the
 * charge state is changed accordingly
 */
static void ab8500_chargalg_check_temp(struct ab8500_chargalg *di)
{
	struct power_supply_battery_info *bi = di->bm->bi;

	if (di->batt_data.temp > (bi->temp_alert_min + di->t_hyst_norm) &&
		di->batt_data.temp < (bi->temp_alert_max - di->t_hyst_norm)) {
		/* Temp OK! */
		di->events.btemp_underover = false;
		di->events.btemp_low = false;
		di->events.btemp_high = false;
		di->t_hyst_norm = 0;
		di->t_hyst_lowhigh = 0;
	} else {
		if ((di->batt_data.temp >= bi->temp_alert_max) &&
		    (di->batt_data.temp < (bi->temp_max - di->t_hyst_lowhigh))) {
			/* Alert zone for high temperature */
			di->events.btemp_underover = false;
			di->events.btemp_high = true;
			di->t_hyst_norm = di->bm->temp_hysteresis;
			di->t_hyst_lowhigh = 0;
		} else if ((di->batt_data.temp > (bi->temp_min + di->t_hyst_lowhigh)) &&
			   (di->batt_data.temp <= bi->temp_alert_min)) {
			/* Alert zone for low temperature */
			di->events.btemp_underover = false;
			di->events.btemp_low = true;
			di->t_hyst_norm = di->bm->temp_hysteresis;
			di->t_hyst_lowhigh = 0;
		} else if (di->batt_data.temp <= bi->temp_min ||
			di->batt_data.temp >= bi->temp_max) {
			/* TEMP major!!!!! */
			di->events.btemp_underover = true;
			di->events.btemp_low = false;
			di->events.btemp_high = false;
			di->t_hyst_norm = 0;
			di->t_hyst_lowhigh = di->bm->temp_hysteresis;
		} else {
			/* Within hysteresis */
			dev_dbg(di->dev, "Within hysteresis limit temp: %d "
				"hyst_lowhigh %d, hyst normal %d\n",
				di->batt_data.temp, di->t_hyst_lowhigh,
				di->t_hyst_norm);
		}
	}
}

/**
 * ab8500_chargalg_check_charger_voltage() - Check charger voltage
 * @di:		pointer to the ab8500_chargalg structure
 *
 * Charger voltage is checked against maximum limit
 */
static void ab8500_chargalg_check_charger_voltage(struct ab8500_chargalg *di)
{
	if (di->chg_info.usb_volt_uv > di->bm->chg_params->usb_volt_max_uv)
		di->chg_info.usb_chg_ok = false;
	else
		di->chg_info.usb_chg_ok = true;

	if (di->chg_info.ac_volt_uv > di->bm->chg_params->ac_volt_max_uv)
		di->chg_info.ac_chg_ok = false;
	else
		di->chg_info.ac_chg_ok = true;

}

/**
 * ab8500_chargalg_end_of_charge() - Check if end-of-charge criteria is fulfilled
 * @di:		pointer to the ab8500_chargalg structure
 *
 * End-of-charge criteria is fulfilled when the battery voltage is above a
 * certain limit and the battery current is below a certain limit for a
 * predefined number of consecutive seconds. If true, the battery is full
 */
static void ab8500_chargalg_end_of_charge(struct ab8500_chargalg *di)
{
	if (di->charge_status == POWER_SUPPLY_STATUS_CHARGING &&
		di->charge_state == STATE_NORMAL &&
		!di->maintenance_chg && (di->batt_data.volt_uv >=
		di->bm->bi->voltage_max_design_uv ||
		di->events.usb_cv_active || di->events.ac_cv_active) &&
		di->batt_data.avg_curr_ua <
		di->bm->bi->charge_term_current_ua &&
		di->batt_data.avg_curr_ua > 0) {
		if (++di->eoc_cnt >= EOC_COND_CNT) {
			di->eoc_cnt = 0;
			di->charge_status = POWER_SUPPLY_STATUS_FULL;
			di->maintenance_chg = true;
			dev_dbg(di->dev, "EOC reached!\n");
			power_supply_changed(di->chargalg_psy);
		} else {
			dev_dbg(di->dev,
				" EOC limit reached for the %d"
				" time, out of %d before EOC\n",
				di->eoc_cnt,
				EOC_COND_CNT);
		}
	} else {
		di->eoc_cnt = 0;
	}
}

static void init_maxim_chg_curr(struct ab8500_chargalg *di)
{
	struct power_supply_battery_info *bi = di->bm->bi;

	di->ccm.original_iset_ua = bi->constant_charge_current_max_ua;
	di->ccm.current_iset_ua = bi->constant_charge_current_max_ua;
	di->ccm.max_current_ua = di->bm->maxi->chg_curr_ua;
	di->ccm.condition_cnt = di->bm->maxi->wait_cycles;
	di->ccm.level = 0;
}

/**
 * ab8500_chargalg_chg_curr_maxim - increases the charger current to
 *			compensate for the system load
 * @di		pointer to the ab8500_chargalg structure
 *
 * This maximization function is used to raise the charger current to get the
 * battery current as close to the optimal value as possible. The battery
 * current during charging is affected by the system load
 */
static enum maxim_ret ab8500_chargalg_chg_curr_maxim(struct ab8500_chargalg *di)
{

	if (!di->bm->maxi->ena_maxi)
		return MAXIM_RET_NOACTION;

	if (di->events.vbus_collapsed) {
		dev_dbg(di->dev, "Charger voltage has collapsed %d\n",
				di->ccm.wait_cnt);
		if (di->ccm.wait_cnt == 0) {
			dev_dbg(di->dev, "lowering current\n");
			di->ccm.wait_cnt++;
			di->ccm.condition_cnt = di->bm->maxi->wait_cycles;
			di->ccm.max_current_ua = di->ccm.current_iset_ua;
			di->ccm.current_iset_ua = di->ccm.max_current_ua;
			di->ccm.level--;
			return MAXIM_RET_CHANGE;
		} else {
			dev_dbg(di->dev, "waiting\n");
			/* Let's go in here twice before lowering curr again */
			di->ccm.wait_cnt = (di->ccm.wait_cnt + 1) % 3;
			return MAXIM_RET_NOACTION;
		}
	}

	di->ccm.wait_cnt = 0;

	if (di->batt_data.inst_curr_ua > di->ccm.original_iset_ua) {
		dev_dbg(di->dev, " Maximization Ibat (%duA) too high"
			" (limit %duA) (current iset: %duA)!\n",
			di->batt_data.inst_curr_ua, di->ccm.original_iset_ua,
			di->ccm.current_iset_ua);

		if (di->ccm.current_iset_ua == di->ccm.original_iset_ua)
			return MAXIM_RET_NOACTION;

		di->ccm.condition_cnt = di->bm->maxi->wait_cycles;
		di->ccm.current_iset_ua = di->ccm.original_iset_ua;
		di->ccm.level = 0;

		return MAXIM_RET_IBAT_TOO_HIGH;
	}

	di->ccm.condition_cnt = di->bm->maxi->wait_cycles;
	return MAXIM_RET_NOACTION;
}

static void handle_maxim_chg_curr(struct ab8500_chargalg *di)
{
	struct power_supply_battery_info *bi = di->bm->bi;
	enum maxim_ret ret;
	int result;

	ret = ab8500_chargalg_chg_curr_maxim(di);
	switch (ret) {
	case MAXIM_RET_CHANGE:
		result = ab8500_chargalg_update_chg_curr(di,
			di->ccm.current_iset_ua);
		if (result)
			dev_err(di->dev, "failed to set chg curr\n");
		break;
	case MAXIM_RET_IBAT_TOO_HIGH:
		result = ab8500_chargalg_update_chg_curr(di,
			bi->constant_charge_current_max_ua);
		if (result)
			dev_err(di->dev, "failed to set chg curr\n");
		break;

	case MAXIM_RET_NOACTION:
	default:
		/* Do nothing..*/
		break;
	}
}

static int ab8500_chargalg_get_ext_psy_data(struct power_supply *ext, void *data)
{
	struct power_supply *psy;
	const char **supplicants = (const char **)ext->supplied_to;
	struct ab8500_chargalg *di;
	union power_supply_propval ret;
	int j;
	bool capacity_updated = false;

	psy = (struct power_supply *)data;
	di = power_supply_get_drvdata(psy);
	/* For all psy where the driver name appears in any supplied_to */
	j = match_string(supplicants, ext->num_supplicants, psy->desc->name);
	if (j < 0)
		return 0;

	/*
	 *  If external is not registering 'POWER_SUPPLY_PROP_CAPACITY' to its
	 * property because of handling that sysfs entry on its own, this is
	 * the place to get the battery capacity.
	 */
	if (!power_supply_get_property(ext, POWER_SUPPLY_PROP_CAPACITY, &ret)) {
		di->batt_data.percent = ret.intval;
		capacity_updated = true;
	}

	/* Go through all properties for the psy */
	for (j = 0; j < ext->desc->num_properties; j++) {
		enum power_supply_property prop;
		prop = ext->desc->properties[j];

		/*
		 * Initialize chargers if not already done.
		 * The ab8500_charger*/
		if (!di->ac_chg &&
			ext->desc->type == POWER_SUPPLY_TYPE_MAINS)
			di->ac_chg = psy_to_ux500_charger(ext);
		else if (!di->usb_chg &&
			ext->desc->type == POWER_SUPPLY_TYPE_USB)
			di->usb_chg = psy_to_ux500_charger(ext);

		if (power_supply_get_property(ext, prop, &ret))
			continue;
		switch (prop) {
		case POWER_SUPPLY_PROP_PRESENT:
			switch (ext->desc->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				/* Battery present */
				if (ret.intval)
					di->events.batt_rem = false;
				/* Battery removed */
				else
					di->events.batt_rem = true;
				break;
			case POWER_SUPPLY_TYPE_MAINS:
				/* AC disconnected */
				if (!ret.intval &&
					(di->chg_info.conn_chg & AC_CHG)) {
					di->chg_info.prev_conn_chg =
						di->chg_info.conn_chg;
					di->chg_info.conn_chg &= ~AC_CHG;
				}
				/* AC connected */
				else if (ret.intval &&
					!(di->chg_info.conn_chg & AC_CHG)) {
					di->chg_info.prev_conn_chg =
						di->chg_info.conn_chg;
					di->chg_info.conn_chg |= AC_CHG;
				}
				break;
			case POWER_SUPPLY_TYPE_USB:
				/* USB disconnected */
				if (!ret.intval &&
					(di->chg_info.conn_chg & USB_CHG)) {
					di->chg_info.prev_conn_chg =
						di->chg_info.conn_chg;
					di->chg_info.conn_chg &= ~USB_CHG;
				}
				/* USB connected */
				else if (ret.intval &&
					!(di->chg_info.conn_chg & USB_CHG)) {
					di->chg_info.prev_conn_chg =
						di->chg_info.conn_chg;
					di->chg_info.conn_chg |= USB_CHG;
				}
				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_ONLINE:
			switch (ext->desc->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				break;
			case POWER_SUPPLY_TYPE_MAINS:
				/* AC offline */
				if (!ret.intval &&
					(di->chg_info.online_chg & AC_CHG)) {
					di->chg_info.prev_online_chg =
						di->chg_info.online_chg;
					di->chg_info.online_chg &= ~AC_CHG;
				}
				/* AC online */
				else if (ret.intval &&
					!(di->chg_info.online_chg & AC_CHG)) {
					di->chg_info.prev_online_chg =
						di->chg_info.online_chg;
					di->chg_info.online_chg |= AC_CHG;
					queue_delayed_work(di->chargalg_wq,
						&di->chargalg_wd_work, 0);
				}
				break;
			case POWER_SUPPLY_TYPE_USB:
				/* USB offline */
				if (!ret.intval &&
					(di->chg_info.online_chg & USB_CHG)) {
					di->chg_info.prev_online_chg =
						di->chg_info.online_chg;
					di->chg_info.online_chg &= ~USB_CHG;
				}
				/* USB online */
				else if (ret.intval &&
					!(di->chg_info.online_chg & USB_CHG)) {
					di->chg_info.prev_online_chg =
						di->chg_info.online_chg;
					di->chg_info.online_chg |= USB_CHG;
					queue_delayed_work(di->chargalg_wq,
						&di->chargalg_wd_work, 0);
				}
				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_HEALTH:
			switch (ext->desc->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				break;
			case POWER_SUPPLY_TYPE_MAINS:
				switch (ret.intval) {
				case POWER_SUPPLY_HEALTH_UNSPEC_FAILURE:
					di->events.mainextchnotok = true;
					di->events.main_thermal_prot = false;
					di->events.main_ovv = false;
					di->events.ac_wd_expired = false;
					break;
				case POWER_SUPPLY_HEALTH_DEAD:
					di->events.ac_wd_expired = true;
					di->events.mainextchnotok = false;
					di->events.main_ovv = false;
					di->events.main_thermal_prot = false;
					break;
				case POWER_SUPPLY_HEALTH_COLD:
				case POWER_SUPPLY_HEALTH_OVERHEAT:
					di->events.main_thermal_prot = true;
					di->events.mainextchnotok = false;
					di->events.main_ovv = false;
					di->events.ac_wd_expired = false;
					break;
				case POWER_SUPPLY_HEALTH_OVERVOLTAGE:
					di->events.main_ovv = true;
					di->events.mainextchnotok = false;
					di->events.main_thermal_prot = false;
					di->events.ac_wd_expired = false;
					break;
				case POWER_SUPPLY_HEALTH_GOOD:
					di->events.main_thermal_prot = false;
					di->events.mainextchnotok = false;
					di->events.main_ovv = false;
					di->events.ac_wd_expired = false;
					break;
				default:
					break;
				}
				break;

			case POWER_SUPPLY_TYPE_USB:
				switch (ret.intval) {
				case POWER_SUPPLY_HEALTH_UNSPEC_FAILURE:
					di->events.usbchargernotok = true;
					di->events.usb_thermal_prot = false;
					di->events.vbus_ovv = false;
					di->events.usb_wd_expired = false;
					break;
				case POWER_SUPPLY_HEALTH_DEAD:
					di->events.usb_wd_expired = true;
					di->events.usbchargernotok = false;
					di->events.usb_thermal_prot = false;
					di->events.vbus_ovv = false;
					break;
				case POWER_SUPPLY_HEALTH_COLD:
				case POWER_SUPPLY_HEALTH_OVERHEAT:
					di->events.usb_thermal_prot = true;
					di->events.usbchargernotok = false;
					di->events.vbus_ovv = false;
					di->events.usb_wd_expired = false;
					break;
				case POWER_SUPPLY_HEALTH_OVERVOLTAGE:
					di->events.vbus_ovv = true;
					di->events.usbchargernotok = false;
					di->events.usb_thermal_prot = false;
					di->events.usb_wd_expired = false;
					break;
				case POWER_SUPPLY_HEALTH_GOOD:
					di->events.usbchargernotok = false;
					di->events.usb_thermal_prot = false;
					di->events.vbus_ovv = false;
					di->events.usb_wd_expired = false;
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			switch (ext->desc->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				di->batt_data.volt_uv = ret.intval;
				break;
			case POWER_SUPPLY_TYPE_MAINS:
				di->chg_info.ac_volt_uv = ret.intval;
				break;
			case POWER_SUPPLY_TYPE_USB:
				di->chg_info.usb_volt_uv = ret.intval;
				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_VOLTAGE_AVG:
			switch (ext->desc->type) {
			case POWER_SUPPLY_TYPE_MAINS:
				/* AVG is used to indicate when we are
				 * in CV mode */
				if (ret.intval)
					di->events.ac_cv_active = true;
				else
					di->events.ac_cv_active = false;

				break;
			case POWER_SUPPLY_TYPE_USB:
				/* AVG is used to indicate when we are
				 * in CV mode */
				if (ret.intval)
					di->events.usb_cv_active = true;
				else
					di->events.usb_cv_active = false;

				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_TECHNOLOGY:
			switch (ext->desc->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				if (ret.intval)
					di->events.batt_unknown = false;
				else
					di->events.batt_unknown = true;

				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_TEMP:
			di->batt_data.temp = ret.intval / 10;
			break;

		case POWER_SUPPLY_PROP_CURRENT_NOW:
			switch (ext->desc->type) {
			case POWER_SUPPLY_TYPE_MAINS:
				di->chg_info.ac_curr_ua = ret.intval;
				break;
			case POWER_SUPPLY_TYPE_USB:
				di->chg_info.usb_curr_ua = ret.intval;
				break;
			case POWER_SUPPLY_TYPE_BATTERY:
				di->batt_data.inst_curr_ua = ret.intval;
				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_CURRENT_AVG:
			switch (ext->desc->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				di->batt_data.avg_curr_ua = ret.intval;
				break;
			case POWER_SUPPLY_TYPE_USB:
				if (ret.intval)
					di->events.vbus_collapsed = true;
				else
					di->events.vbus_collapsed = false;
				break;
			default:
				break;
			}
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			if (!capacity_updated)
				di->batt_data.percent = ret.intval;
			break;
		default:
			break;
		}
	}
	return 0;
}

/**
 * ab8500_chargalg_external_power_changed() - callback for power supply changes
 * @psy:       pointer to the structure power_supply
 *
 * This function is the entry point of the pointer external_power_changed
 * of the structure power_supply.
 * This function gets executed when there is a change in any external power
 * supply that this driver needs to be notified of.
 */
static void ab8500_chargalg_external_power_changed(struct power_supply *psy)
{
	struct ab8500_chargalg *di = power_supply_get_drvdata(psy);

	/*
	 * Trigger execution of the algorithm instantly and read
	 * all power_supply properties there instead
	 */
	if (di->chargalg_wq)
		queue_work(di->chargalg_wq, &di->chargalg_work);
}

/**
 * ab8500_chargalg_time_to_restart() - time to restart CC/CV charging?
 * @di: charging algorithm state
 *
 * This checks if the voltage or capacity of the battery has fallen so
 * low that we need to restart the CC/CV charge cycle.
 */
static bool ab8500_chargalg_time_to_restart(struct ab8500_chargalg *di)
{
	struct power_supply_battery_info *bi = di->bm->bi;

	/* Sanity check - these need to have some reasonable values */
	if (!di->batt_data.volt_uv || !di->batt_data.percent)
		return false;

	/* Some batteries tell us at which voltage we should restart charging */
	if (bi->charge_restart_voltage_uv > 0) {
		if (di->batt_data.volt_uv <= bi->charge_restart_voltage_uv)
			return true;
		/* Else we restart as we reach a certain capacity */
	} else {
		if (di->batt_data.percent <= AB8500_RECHARGE_CAP)
			return true;
	}

	return false;
}

/**
 * ab8500_chargalg_algorithm() - Main function for the algorithm
 * @di:		pointer to the ab8500_chargalg structure
 *
 * This is the main control function for the charging algorithm.
 * It is called periodically or when something happens that will
 * trigger a state change
 */
static void ab8500_chargalg_algorithm(struct ab8500_chargalg *di)
{
	const struct power_supply_maintenance_charge_table *mt;
	struct power_supply_battery_info *bi = di->bm->bi;
	int charger_status;
	int ret;

	/* Collect data from all power_supply class devices */
	power_supply_for_each_psy(di->chargalg_psy, ab8500_chargalg_get_ext_psy_data);

	ab8500_chargalg_end_of_charge(di);
	ab8500_chargalg_check_temp(di);
	ab8500_chargalg_check_charger_voltage(di);

	charger_status = ab8500_chargalg_check_charger_connection(di);

	if (is_ab8500(di->parent)) {
		ret = ab8500_chargalg_check_charger_enable(di);
		if (ret < 0)
			dev_err(di->dev, "Checking charger is enabled error"
					": Returned Value %d\n", ret);
	}

	/*
	 * First check if we have a charger connected.
	 * Also we don't allow charging of unknown batteries if configured
	 * this way
	 */
	if (!charger_status ||
		(di->events.batt_unknown && !di->bm->chg_unknown_bat)) {
		if (di->charge_state != STATE_HANDHELD) {
			di->events.safety_timer_expired = false;
			ab8500_chargalg_state_to(di, STATE_HANDHELD_INIT);
		}
	}

	/* Safety timer expiration */
	else if (di->events.safety_timer_expired) {
		if (di->charge_state != STATE_SAFETY_TIMER_EXPIRED)
			ab8500_chargalg_state_to(di,
				STATE_SAFETY_TIMER_EXPIRED_INIT);
	}
	/*
	 * Check if any interrupts has occurred
	 * that will prevent us from charging
	 */

	/* Battery removed */
	else if (di->events.batt_rem) {
		if (di->charge_state != STATE_BATT_REMOVED)
			ab8500_chargalg_state_to(di, STATE_BATT_REMOVED_INIT);
	}
	/* Main or USB charger not ok. */
	else if (di->events.mainextchnotok || di->events.usbchargernotok) {
		/*
		 * If vbus_collapsed is set, we have to lower the charger
		 * current, which is done in the normal state below
		 */
		if (di->charge_state != STATE_CHG_NOT_OK &&
				!di->events.vbus_collapsed)
			ab8500_chargalg_state_to(di, STATE_CHG_NOT_OK_INIT);
	}
	/* VBUS, Main or VBAT OVV. */
	else if (di->events.vbus_ovv ||
			di->events.main_ovv ||
			di->events.batt_ovv ||
			!di->chg_info.usb_chg_ok ||
			!di->chg_info.ac_chg_ok) {
		if (di->charge_state != STATE_OVV_PROTECT)
			ab8500_chargalg_state_to(di, STATE_OVV_PROTECT_INIT);
	}
	/* USB Thermal, stop charging */
	else if (di->events.main_thermal_prot ||
		di->events.usb_thermal_prot) {
		if (di->charge_state != STATE_HW_TEMP_PROTECT)
			ab8500_chargalg_state_to(di,
				STATE_HW_TEMP_PROTECT_INIT);
	}
	/* Battery temp over/under */
	else if (di->events.btemp_underover) {
		if (di->charge_state != STATE_TEMP_UNDEROVER)
			ab8500_chargalg_state_to(di,
				STATE_TEMP_UNDEROVER_INIT);
	}
	/* Watchdog expired */
	else if (di->events.ac_wd_expired ||
		di->events.usb_wd_expired) {
		if (di->charge_state != STATE_WD_EXPIRED)
			ab8500_chargalg_state_to(di, STATE_WD_EXPIRED_INIT);
	}
	/* Battery temp high/low */
	else if (di->events.btemp_low || di->events.btemp_high) {
		if (di->charge_state != STATE_TEMP_LOWHIGH)
			ab8500_chargalg_state_to(di, STATE_TEMP_LOWHIGH_INIT);
	}

	dev_dbg(di->dev,
		"[CHARGALG] Vb %d Ib_avg %d Ib_inst %d Tb %d Cap %d Maint %d "
		"State %s Active_chg %d Chg_status %d AC %d USB %d "
		"AC_online %d USB_online %d AC_CV %d USB_CV %d AC_I %d "
		"USB_I %d AC_Vset %d AC_Iset %d USB_Vset %d USB_Iset %d\n",
		di->batt_data.volt_uv,
		di->batt_data.avg_curr_ua,
		di->batt_data.inst_curr_ua,
		di->batt_data.temp,
		di->batt_data.percent,
		di->maintenance_chg,
		states[di->charge_state],
		di->chg_info.charger_type,
		di->charge_status,
		di->chg_info.conn_chg & AC_CHG,
		di->chg_info.conn_chg & USB_CHG,
		di->chg_info.online_chg & AC_CHG,
		di->chg_info.online_chg & USB_CHG,
		di->events.ac_cv_active,
		di->events.usb_cv_active,
		di->chg_info.ac_curr_ua,
		di->chg_info.usb_curr_ua,
		di->chg_info.ac_vset_uv,
		di->chg_info.ac_iset_ua,
		di->chg_info.usb_vset_uv,
		di->chg_info.usb_iset_ua);

	switch (di->charge_state) {
	case STATE_HANDHELD_INIT:
		ab8500_chargalg_stop_charging(di);
		di->charge_status = POWER_SUPPLY_STATUS_DISCHARGING;
		ab8500_chargalg_state_to(di, STATE_HANDHELD);
		fallthrough;

	case STATE_HANDHELD:
		break;

	case STATE_BATT_REMOVED_INIT:
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_BATT_REMOVED);
		fallthrough;

	case STATE_BATT_REMOVED:
		if (!di->events.batt_rem)
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_HW_TEMP_PROTECT_INIT:
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_HW_TEMP_PROTECT);
		fallthrough;

	case STATE_HW_TEMP_PROTECT:
		if (!di->events.main_thermal_prot &&
				!di->events.usb_thermal_prot)
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_OVV_PROTECT_INIT:
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_OVV_PROTECT);
		fallthrough;

	case STATE_OVV_PROTECT:
		if (!di->events.vbus_ovv &&
				!di->events.main_ovv &&
				!di->events.batt_ovv &&
				di->chg_info.usb_chg_ok &&
				di->chg_info.ac_chg_ok)
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_CHG_NOT_OK_INIT:
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_CHG_NOT_OK);
		fallthrough;

	case STATE_CHG_NOT_OK:
		if (!di->events.mainextchnotok &&
				!di->events.usbchargernotok)
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_SAFETY_TIMER_EXPIRED_INIT:
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_SAFETY_TIMER_EXPIRED);
		fallthrough;

	case STATE_SAFETY_TIMER_EXPIRED:
		/* We exit this state when charger is removed */
		break;

	case STATE_NORMAL_INIT:
		if (bi->constant_charge_current_max_ua == 0)
			/* "charging" with 0 uA */
			ab8500_chargalg_stop_charging(di);
		else {
			ab8500_chargalg_start_charging(di,
				bi->constant_charge_voltage_max_uv,
				bi->constant_charge_current_max_ua);
		}

		ab8500_chargalg_state_to(di, STATE_NORMAL);
		ab8500_chargalg_start_safety_timer(di);
		ab8500_chargalg_stop_maintenance_timer(di);
		init_maxim_chg_curr(di);
		di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
		di->eoc_cnt = 0;
		di->maintenance_chg = false;
		power_supply_changed(di->chargalg_psy);

		break;

	case STATE_NORMAL:
		handle_maxim_chg_curr(di);
		if (di->charge_status == POWER_SUPPLY_STATUS_FULL &&
			di->maintenance_chg) {
			/*
			 * The battery is fully charged, check if we support
			 * maintenance charging else go back to waiting for
			 * the recharge voltage limit.
			 */
			if (!power_supply_supports_maintenance_charging(bi))
				ab8500_chargalg_state_to(di,
					STATE_WAIT_FOR_RECHARGE_INIT);
			else
				ab8500_chargalg_state_to(di,
					STATE_MAINTENANCE_A_INIT);
		}
		break;

	/* This state will be used when the maintenance state is disabled */
	case STATE_WAIT_FOR_RECHARGE_INIT:
		ab8500_chargalg_hold_charging(di);
		ab8500_chargalg_state_to(di, STATE_WAIT_FOR_RECHARGE);
		fallthrough;

	case STATE_WAIT_FOR_RECHARGE:
		if (ab8500_chargalg_time_to_restart(di))
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_MAINTENANCE_A_INIT:
		mt = power_supply_get_maintenance_charging_setting(bi, 0);
		if (!mt) {
			/* No maintenance A state, go back to normal */
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
			power_supply_changed(di->chargalg_psy);
			break;
		}
		ab8500_chargalg_stop_safety_timer(di);
		ab8500_chargalg_start_maintenance_timer(di,
			mt->charge_safety_timer_minutes);
		ab8500_chargalg_start_charging(di,
			mt->charge_voltage_max_uv,
			mt->charge_current_max_ua);
		ab8500_chargalg_state_to(di, STATE_MAINTENANCE_A);
		power_supply_changed(di->chargalg_psy);
		fallthrough;

	case STATE_MAINTENANCE_A:
		if (di->events.maintenance_timer_expired) {
			ab8500_chargalg_stop_maintenance_timer(di);
			ab8500_chargalg_state_to(di, STATE_MAINTENANCE_B_INIT);
		}
		/*
		 * This happens if the voltage drops too quickly during
		 * maintenance charging, especially in older batteries.
		 */
		if (ab8500_chargalg_time_to_restart(di)) {
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
			dev_info(di->dev, "restarted charging from maintenance state A - battery getting old?\n");
		}
		break;

	case STATE_MAINTENANCE_B_INIT:
		mt = power_supply_get_maintenance_charging_setting(bi, 1);
		if (!mt) {
			/* No maintenance B state, go back to normal */
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
			power_supply_changed(di->chargalg_psy);
			break;
		}
		ab8500_chargalg_start_maintenance_timer(di,
			mt->charge_safety_timer_minutes);
		ab8500_chargalg_start_charging(di,
			mt->charge_voltage_max_uv,
			mt->charge_current_max_ua);
		ab8500_chargalg_state_to(di, STATE_MAINTENANCE_B);
		power_supply_changed(di->chargalg_psy);
		fallthrough;

	case STATE_MAINTENANCE_B:
		if (di->events.maintenance_timer_expired) {
			ab8500_chargalg_stop_maintenance_timer(di);
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
		}
		/*
		 * This happens if the voltage drops too quickly during
		 * maintenance charging, especially in older batteries.
		 */
		if (ab8500_chargalg_time_to_restart(di)) {
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
			dev_info(di->dev, "restarted charging from maintenance state B - battery getting old?\n");
		}
		break;

	case STATE_TEMP_LOWHIGH_INIT:
		if (di->events.btemp_low) {
			ab8500_chargalg_start_charging(di,
				       bi->alert_low_temp_charge_voltage_uv,
				       bi->alert_low_temp_charge_current_ua);
		} else if (di->events.btemp_high) {
			ab8500_chargalg_start_charging(di,
				       bi->alert_high_temp_charge_voltage_uv,
				       bi->alert_high_temp_charge_current_ua);
		} else {
			dev_err(di->dev, "neither low or high temp event occurred\n");
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
			break;
		}
		ab8500_chargalg_stop_maintenance_timer(di);
		di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
		ab8500_chargalg_state_to(di, STATE_TEMP_LOWHIGH);
		power_supply_changed(di->chargalg_psy);
		fallthrough;

	case STATE_TEMP_LOWHIGH:
		if (!di->events.btemp_low && !di->events.btemp_high)
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_WD_EXPIRED_INIT:
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_WD_EXPIRED);
		fallthrough;

	case STATE_WD_EXPIRED:
		if (!di->events.ac_wd_expired &&
				!di->events.usb_wd_expired)
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_TEMP_UNDEROVER_INIT:
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_TEMP_UNDEROVER);
		fallthrough;

	case STATE_TEMP_UNDEROVER:
		if (!di->events.btemp_underover)
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;
	}

	/* Start charging directly if the new state is a charge state */
	if (di->charge_state == STATE_NORMAL_INIT ||
			di->charge_state == STATE_MAINTENANCE_A_INIT ||
			di->charge_state == STATE_MAINTENANCE_B_INIT)
		queue_work(di->chargalg_wq, &di->chargalg_work);
}

/**
 * ab8500_chargalg_periodic_work() - Periodic work for the algorithm
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for the charging algorithm
 */
static void ab8500_chargalg_periodic_work(struct work_struct *work)
{
	struct ab8500_chargalg *di = container_of(work,
		struct ab8500_chargalg, chargalg_periodic_work.work);

	ab8500_chargalg_algorithm(di);

	/*
	 * If a charger is connected then the battery has to be monitored
	 * frequently, else the work can be delayed.
	 */
	if (di->chg_info.conn_chg)
		queue_delayed_work(di->chargalg_wq,
			&di->chargalg_periodic_work,
			di->bm->interval_charging * HZ);
	else
		queue_delayed_work(di->chargalg_wq,
			&di->chargalg_periodic_work,
			di->bm->interval_not_charging * HZ);
}

/**
 * ab8500_chargalg_wd_work() - periodic work to kick the charger watchdog
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for kicking the charger watchdog
 */
static void ab8500_chargalg_wd_work(struct work_struct *work)
{
	int ret;
	struct ab8500_chargalg *di = container_of(work,
		struct ab8500_chargalg, chargalg_wd_work.work);

	ret = ab8500_chargalg_kick_watchdog(di);
	if (ret < 0)
		dev_err(di->dev, "failed to kick watchdog\n");

	queue_delayed_work(di->chargalg_wq,
		&di->chargalg_wd_work, CHG_WD_INTERVAL);
}

/**
 * ab8500_chargalg_work() - Work to run the charging algorithm instantly
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for calling the charging algorithm
 */
static void ab8500_chargalg_work(struct work_struct *work)
{
	struct ab8500_chargalg *di = container_of(work,
		struct ab8500_chargalg, chargalg_work);

	ab8500_chargalg_algorithm(di);
}

/**
 * ab8500_chargalg_get_property() - get the chargalg properties
 * @psy:	pointer to the power_supply structure
 * @psp:	pointer to the power_supply_property structure
 * @val:	pointer to the power_supply_propval union
 *
 * This function gets called when an application tries to get the
 * chargalg properties by reading the sysfs files.
 * status:     charging/discharging/full/unknown
 * health:     health of the battery
 * Returns error code in case of failure else 0 on success
 */
static int ab8500_chargalg_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct ab8500_chargalg *di = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = di->charge_status;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if (di->events.batt_ovv) {
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		} else if (di->events.btemp_underover) {
			if (di->batt_data.temp <= di->bm->bi->temp_min)
				val->intval = POWER_SUPPLY_HEALTH_COLD;
			else
				val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		} else if (di->charge_state == STATE_SAFETY_TIMER_EXPIRED ||
			   di->charge_state == STATE_SAFETY_TIMER_EXPIRED_INIT) {
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		} else {
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int __maybe_unused ab8500_chargalg_resume(struct device *dev)
{
	struct ab8500_chargalg *di = dev_get_drvdata(dev);

	/* Kick charger watchdog if charging (any charger online) */
	if (di->chg_info.online_chg)
		queue_delayed_work(di->chargalg_wq, &di->chargalg_wd_work, 0);

	/*
	 * Run the charging algorithm directly to be sure we don't
	 * do it too seldom
	 */
	queue_delayed_work(di->chargalg_wq, &di->chargalg_periodic_work, 0);

	return 0;
}

static int __maybe_unused ab8500_chargalg_suspend(struct device *dev)
{
	struct ab8500_chargalg *di = dev_get_drvdata(dev);

	if (di->chg_info.online_chg)
		cancel_delayed_work_sync(&di->chargalg_wd_work);

	cancel_delayed_work_sync(&di->chargalg_periodic_work);

	return 0;
}

static char *supply_interface[] = {
	"ab8500_fg",
};

static const struct power_supply_desc ab8500_chargalg_desc = {
	.name			= "ab8500_chargalg",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= ab8500_chargalg_props,
	.num_properties		= ARRAY_SIZE(ab8500_chargalg_props),
	.get_property		= ab8500_chargalg_get_property,
	.external_power_changed	= ab8500_chargalg_external_power_changed,
};

static int ab8500_chargalg_bind(struct device *dev, struct device *master,
				void *data)
{
	struct ab8500_chargalg *di = dev_get_drvdata(dev);

	/* Create a work queue for the chargalg */
	di->chargalg_wq = alloc_ordered_workqueue("ab8500_chargalg_wq",
						  WQ_MEM_RECLAIM);
	if (di->chargalg_wq == NULL) {
		dev_err(di->dev, "failed to create work queue\n");
		return -ENOMEM;
	}

	/* Run the charging algorithm */
	queue_delayed_work(di->chargalg_wq, &di->chargalg_periodic_work, 0);

	return 0;
}

static void ab8500_chargalg_unbind(struct device *dev, struct device *master,
				   void *data)
{
	struct ab8500_chargalg *di = dev_get_drvdata(dev);

	/* Stop all timers and work */
	hrtimer_cancel(&di->safety_timer);
	hrtimer_cancel(&di->maintenance_timer);

	cancel_delayed_work_sync(&di->chargalg_periodic_work);
	cancel_delayed_work_sync(&di->chargalg_wd_work);
	cancel_work_sync(&di->chargalg_work);

	/* Delete the work queue */
	destroy_workqueue(di->chargalg_wq);
}

static const struct component_ops ab8500_chargalg_component_ops = {
	.bind = ab8500_chargalg_bind,
	.unbind = ab8500_chargalg_unbind,
};

static int ab8500_chargalg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct power_supply_config psy_cfg = {};
	struct ab8500_chargalg *di;

	di = devm_kzalloc(dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->bm = &ab8500_bm_data;

	/* get device struct and parent */
	di->dev = dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);

	psy_cfg.supplied_to = supply_interface;
	psy_cfg.num_supplicants = ARRAY_SIZE(supply_interface);
	psy_cfg.drv_data = di;

	/* Initilialize safety timer */
	hrtimer_init(&di->safety_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	di->safety_timer.function = ab8500_chargalg_safety_timer_expired;

	/* Initilialize maintenance timer */
	hrtimer_init(&di->maintenance_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	di->maintenance_timer.function =
		ab8500_chargalg_maintenance_timer_expired;

	/* Init work for chargalg */
	INIT_DEFERRABLE_WORK(&di->chargalg_periodic_work,
		ab8500_chargalg_periodic_work);
	INIT_DEFERRABLE_WORK(&di->chargalg_wd_work,
		ab8500_chargalg_wd_work);

	/* Init work for chargalg */
	INIT_WORK(&di->chargalg_work, ab8500_chargalg_work);

	/* To detect charger at startup */
	di->chg_info.prev_conn_chg = -1;

	/* Register chargalg power supply class */
	di->chargalg_psy = devm_power_supply_register(di->dev,
						 &ab8500_chargalg_desc,
						 &psy_cfg);
	if (IS_ERR(di->chargalg_psy)) {
		dev_err(di->dev, "failed to register chargalg psy\n");
		return PTR_ERR(di->chargalg_psy);
	}

	platform_set_drvdata(pdev, di);

	dev_info(di->dev, "probe success\n");
	return component_add(dev, &ab8500_chargalg_component_ops);
}

static void ab8500_chargalg_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &ab8500_chargalg_component_ops);
}

static SIMPLE_DEV_PM_OPS(ab8500_chargalg_pm_ops, ab8500_chargalg_suspend, ab8500_chargalg_resume);

static const struct of_device_id ab8500_chargalg_match[] = {
	{ .compatible = "stericsson,ab8500-chargalg", },
	{ },
};

struct platform_driver ab8500_chargalg_driver = {
	.probe = ab8500_chargalg_probe,
	.remove = ab8500_chargalg_remove,
	.driver = {
		.name = "ab8500_chargalg",
		.of_match_table = ab8500_chargalg_match,
		.pm = &ab8500_chargalg_pm_ops,
	},
};
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Johan Palsson, Karl Komierowski");
MODULE_ALIAS("platform:ab8500-chargalg");
MODULE_DESCRIPTION("ab8500 battery charging algorithm");
