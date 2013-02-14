/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Charging algorithm driver for abx500 variants
 *
 * License Terms: GNU General Public License v2
 * Authors:
 *	Johan Palsson <johan.palsson@stericsson.com>
 *	Karl Komierowski <karl.komierowski@stericsson.com>
 *	Arun R Murthy <arun.murthy@stericsson.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
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
#include <linux/mfd/abx500/ux500_chargalg.h>
#include <linux/mfd/abx500/ab8500-bm.h>
#include <linux/notifier.h>

/* Watchdog kick interval */
#define CHG_WD_INTERVAL			(6 * HZ)

/* End-of-charge criteria counter */
#define EOC_COND_CNT			10

/* Plus margin for the low battery threshold */
#define BAT_PLUS_MARGIN                (100)

#define to_abx500_chargalg_device_info(x) container_of((x), \
	struct abx500_chargalg, chargalg_psy);

enum abx500_chargers {
	NO_CHG,
	AC_CHG,
	USB_CHG,
};

struct abx500_chargalg_charger_info {
	enum abx500_chargers conn_chg;
	enum abx500_chargers prev_conn_chg;
	enum abx500_chargers online_chg;
	enum abx500_chargers prev_online_chg;
	enum abx500_chargers charger_type;
	bool usb_chg_ok;
	bool ac_chg_ok;
	int usb_volt;
	int usb_curr;
	int ac_volt;
	int ac_curr;
	int usb_vset;
	int usb_iset;
	int ac_vset;
	int ac_iset;
};

struct abx500_chargalg_suspension_status {
	bool suspended_change;
	bool ac_suspended;
	bool usb_suspended;
};

struct abx500_chargalg_battery_data {
	int temp;
	int volt;
	int avg_curr;
	int inst_curr;
	int percent;
};

enum abx500_chargalg_states {
	STATE_HANDHELD_INIT,
	STATE_HANDHELD,
	STATE_CHG_NOT_OK_INIT,
	STATE_CHG_NOT_OK,
	STATE_HW_TEMP_PROTECT_INIT,
	STATE_HW_TEMP_PROTECT,
	STATE_NORMAL_INIT,
	STATE_USB_PP_PRE_CHARGE,
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
	STATE_SUSPENDED_INIT,
	STATE_SUSPENDED,
	STATE_OVV_PROTECT_INIT,
	STATE_OVV_PROTECT,
	STATE_SAFETY_TIMER_EXPIRED_INIT,
	STATE_SAFETY_TIMER_EXPIRED,
	STATE_BATT_REMOVED_INIT,
	STATE_BATT_REMOVED,
	STATE_WD_EXPIRED_INIT,
	STATE_WD_EXPIRED,
};

static const char *states[] = {
	"HANDHELD_INIT",
	"HANDHELD",
	"CHG_NOT_OK_INIT",
	"CHG_NOT_OK",
	"HW_TEMP_PROTECT_INIT",
	"HW_TEMP_PROTECT",
	"NORMAL_INIT",
	"USB_PP_PRE_CHARGE",
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
	"SUSPENDED_INIT",
	"SUSPENDED",
	"OVV_PROTECT_INIT",
	"OVV_PROTECT",
	"SAFETY_TIMER_EXPIRED_INIT",
	"SAFETY_TIMER_EXPIRED",
	"BATT_REMOVED_INIT",
	"BATT_REMOVED",
	"WD_EXPIRED_INIT",
	"WD_EXPIRED",
};

struct abx500_chargalg_events {
	bool batt_unknown;
	bool mainextchnotok;
	bool batt_ovv;
	bool batt_rem;
	bool btemp_underover;
	bool btemp_lowhigh;
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
 * struct abx500_charge_curr_maximization - Charger maximization parameters
 * @original_iset:	the non optimized/maximised charger current
 * @current_iset:	the charging current used at this moment
 * @test_delta_i:	the delta between the current we want to charge and the
			current that is really going into the battery
 * @condition_cnt:	number of iterations needed before a new charger current
			is set
 * @max_current:	maximum charger current
 * @wait_cnt:		to avoid too fast current step down in case of charger
 *			voltage collapse, we insert this delay between step
 *			down
 * @level:		tells in how many steps the charging current has been
			increased
 */
struct abx500_charge_curr_maximization {
	int original_iset;
	int current_iset;
	int test_delta_i;
	int condition_cnt;
	int max_current;
	int wait_cnt;
	u8 level;
};

enum maxim_ret {
	MAXIM_RET_NOACTION,
	MAXIM_RET_CHANGE,
	MAXIM_RET_IBAT_TOO_HIGH,
};

/**
 * struct abx500_chargalg - abx500 Charging algorithm device information
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
 * @susp_status:	current charger suspension status
 * @bm:           	Platform specific battery management information
 * @parent:		pointer to the struct abx500
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
struct abx500_chargalg {
	struct device *dev;
	int charge_status;
	int eoc_cnt;
	bool maintenance_chg;
	int t_hyst_norm;
	int t_hyst_lowhigh;
	enum abx500_chargalg_states charge_state;
	struct abx500_charge_curr_maximization ccm;
	struct abx500_chargalg_charger_info chg_info;
	struct abx500_chargalg_battery_data batt_data;
	struct abx500_chargalg_suspension_status susp_status;
	struct ab8500 *parent;
	struct abx500_bm_data *bm;
	struct power_supply chargalg_psy;
	struct ux500_charger *ac_chg;
	struct ux500_charger *usb_chg;
	struct abx500_chargalg_events events;
	struct workqueue_struct *chargalg_wq;
	struct delayed_work chargalg_periodic_work;
	struct delayed_work chargalg_wd_work;
	struct work_struct chargalg_work;
	struct timer_list safety_timer;
	struct timer_list maintenance_timer;
	struct kobject chargalg_kobject;
};

/*External charger prepare notifier*/
BLOCKING_NOTIFIER_HEAD(charger_notifier_list);

/* Main battery properties */
static enum power_supply_property abx500_chargalg_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
};

/**
 * abx500_chargalg_safety_timer_expired() - Expiration of the safety timer
 * @data:	pointer to the abx500_chargalg structure
 *
 * This function gets called when the safety timer for the charger
 * expires
 */
static void abx500_chargalg_safety_timer_expired(unsigned long data)
{
	struct abx500_chargalg *di = (struct abx500_chargalg *) data;
	dev_err(di->dev, "Safety timer expired\n");
	di->events.safety_timer_expired = true;

	/* Trigger execution of the algorithm instantly */
	queue_work(di->chargalg_wq, &di->chargalg_work);
}

/**
 * abx500_chargalg_maintenance_timer_expired() - Expiration of
 * the maintenance timer
 * @i:		pointer to the abx500_chargalg structure
 *
 * This function gets called when the maintenence timer
 * expires
 */
static void abx500_chargalg_maintenance_timer_expired(unsigned long data)
{

	struct abx500_chargalg *di = (struct abx500_chargalg *) data;
	dev_dbg(di->dev, "Maintenance timer expired\n");
	di->events.maintenance_timer_expired = true;

	/* Trigger execution of the algorithm instantly */
	queue_work(di->chargalg_wq, &di->chargalg_work);
}

/**
 * abx500_chargalg_state_to() - Change charge state
 * @di:		pointer to the abx500_chargalg structure
 *
 * This function gets called when a charge state change should occur
 */
static void abx500_chargalg_state_to(struct abx500_chargalg *di,
	enum abx500_chargalg_states state)
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

static int abx500_chargalg_check_charger_enable(struct abx500_chargalg *di)
{
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
                         di->bm->bat_type[di->bm->batt_id].normal_vol_lvl,
                         di->bm->bat_type[di->bm->batt_id].normal_cur_lvl);
	} else if ((di->chg_info.charger_type & AC_CHG) &&
		   !(di->ac_chg->external)) {
		return di->ac_chg->ops.check_enable(di->ac_chg,
                         di->bm->bat_type[di->bm->batt_id].normal_vol_lvl,
                         di->bm->bat_type[di->bm->batt_id].normal_cur_lvl);
	}
	return 0;
}

/**
 * abx500_chargalg_check_charger_connection() - Check charger connection change
 * @di:		pointer to the abx500_chargalg structure
 *
 * This function will check if there is a change in the charger connection
 * and change charge state accordingly. AC has precedence over USB.
 */
static int abx500_chargalg_check_charger_connection(struct abx500_chargalg *di)
{
	if (di->chg_info.conn_chg != di->chg_info.prev_conn_chg ||
		di->susp_status.suspended_change) {
		/*
		 * Charger state changed or suspension
		 * has changed since last update
		 */
		if ((di->chg_info.conn_chg & AC_CHG) &&
			!di->susp_status.ac_suspended) {
			dev_dbg(di->dev, "Charging source is AC\n");
			if (di->chg_info.charger_type != AC_CHG) {
				di->chg_info.charger_type = AC_CHG;
				abx500_chargalg_state_to(di, STATE_NORMAL_INIT);
			}
		} else if ((di->chg_info.conn_chg & USB_CHG) &&
			!di->susp_status.usb_suspended) {
			dev_dbg(di->dev, "Charging source is USB\n");
			di->chg_info.charger_type = USB_CHG;
			abx500_chargalg_state_to(di, STATE_NORMAL_INIT);
		} else if (di->chg_info.conn_chg &&
			(di->susp_status.ac_suspended ||
			di->susp_status.usb_suspended)) {
			dev_dbg(di->dev, "Charging is suspended\n");
			di->chg_info.charger_type = NO_CHG;
			abx500_chargalg_state_to(di, STATE_SUSPENDED_INIT);
		} else {
			dev_dbg(di->dev, "Charging source is OFF\n");
			di->chg_info.charger_type = NO_CHG;
			abx500_chargalg_state_to(di, STATE_HANDHELD_INIT);
		}
		di->chg_info.prev_conn_chg = di->chg_info.conn_chg;
		di->susp_status.suspended_change = false;
	}
	return di->chg_info.conn_chg;
}

/**
 * abx500_chargalg_start_safety_timer() - Start charging safety timer
 * @di:		pointer to the abx500_chargalg structure
 *
 * The safety timer is used to avoid overcharging of old or bad batteries.
 * There are different timers for AC and USB
 */
static void abx500_chargalg_start_safety_timer(struct abx500_chargalg *di)
{
	unsigned long timer_expiration = 0;

	switch (di->chg_info.charger_type) {
	case AC_CHG:
		timer_expiration =
		round_jiffies(jiffies +
			(di->bm->main_safety_tmr_h * 3600 * HZ));
		break;

	case USB_CHG:
		timer_expiration =
		round_jiffies(jiffies +
			(di->bm->usb_safety_tmr_h * 3600 * HZ));
		break;

	default:
		dev_err(di->dev, "Unknown charger to charge from\n");
		break;
	}

	di->events.safety_timer_expired = false;
	di->safety_timer.expires = timer_expiration;
	if (!timer_pending(&di->safety_timer))
		add_timer(&di->safety_timer);
	else
		mod_timer(&di->safety_timer, timer_expiration);
}

/**
 * abx500_chargalg_stop_safety_timer() - Stop charging safety timer
 * @di:		pointer to the abx500_chargalg structure
 *
 * The safety timer is stopped whenever the NORMAL state is exited
 */
static void abx500_chargalg_stop_safety_timer(struct abx500_chargalg *di)
{
	di->events.safety_timer_expired = false;
	del_timer(&di->safety_timer);
}

/**
 * abx500_chargalg_start_maintenance_timer() - Start charging maintenance timer
 * @di:		pointer to the abx500_chargalg structure
 * @duration:	duration of ther maintenance timer in hours
 *
 * The maintenance timer is used to maintain the charge in the battery once
 * the battery is considered full. These timers are chosen to match the
 * discharge curve of the battery
 */
static void abx500_chargalg_start_maintenance_timer(struct abx500_chargalg *di,
	int duration)
{
	unsigned long timer_expiration;

	/* Convert from hours to jiffies */
	timer_expiration = round_jiffies(jiffies + (duration * 3600 * HZ));

	di->events.maintenance_timer_expired = false;
	di->maintenance_timer.expires = timer_expiration;
	if (!timer_pending(&di->maintenance_timer))
		add_timer(&di->maintenance_timer);
	else
		mod_timer(&di->maintenance_timer, timer_expiration);
}

/**
 * abx500_chargalg_stop_maintenance_timer() - Stop maintenance timer
 * @di:		pointer to the abx500_chargalg structure
 *
 * The maintenance timer is stopped whenever maintenance ends or when another
 * state is entered
 */
static void abx500_chargalg_stop_maintenance_timer(struct abx500_chargalg *di)
{
	di->events.maintenance_timer_expired = false;
	del_timer(&di->maintenance_timer);
}

/**
 * abx500_chargalg_kick_watchdog() - Kick charger watchdog
 * @di:		pointer to the abx500_chargalg structure
 *
 * The charger watchdog have to be kicked periodically whenever the charger is
 * on, else the ABB will reset the system
 */
static int abx500_chargalg_kick_watchdog(struct abx500_chargalg *di)
{
	/* Check if charger exists and kick watchdog if charging */
	if (di->ac_chg && di->ac_chg->ops.kick_wd &&
	    di->chg_info.online_chg & AC_CHG) {
		/*
		 * If AB charger watchdog expired, pm2xxx charging
		 * gets disabled. To be safe, kick both AB charger watchdog
		 * and pm2xxx watchdog.
		 */
		if (di->ac_chg->external &&
		    di->usb_chg && di->usb_chg->ops.kick_wd)
			di->usb_chg->ops.kick_wd(di->usb_chg);

		return di->ac_chg->ops.kick_wd(di->ac_chg);
	}
	else if (di->usb_chg && di->usb_chg->ops.kick_wd &&
			di->chg_info.online_chg & USB_CHG)
		return di->usb_chg->ops.kick_wd(di->usb_chg);

	return -ENXIO;
}

/**
 * abx500_chargalg_ac_en() - Turn on/off the AC charger
 * @di:		pointer to the abx500_chargalg structure
 * @enable:	charger on/off
 * @vset:	requested charger output voltage
 * @iset:	requested charger output current
 *
 * The AC charger will be turned on/off with the requested charge voltage and
 * current
 */
static int abx500_chargalg_ac_en(struct abx500_chargalg *di, int enable,
	int vset, int iset)
{
	static int abx500_chargalg_ex_ac_enable_toggle;

	if (!di->ac_chg || !di->ac_chg->ops.enable)
		return -ENXIO;

	/* Select maximum of what both the charger and the battery supports */
	if (di->ac_chg->max_out_volt)
		vset = min(vset, di->ac_chg->max_out_volt);
	if (di->ac_chg->max_out_curr)
		iset = min(iset, di->ac_chg->max_out_curr);

	di->chg_info.ac_iset = iset;
	di->chg_info.ac_vset = vset;

	/* Enable external charger */
	if (enable && di->ac_chg->external &&
	    !abx500_chargalg_ex_ac_enable_toggle) {
		blocking_notifier_call_chain(&charger_notifier_list,
					     0, di->dev);
		abx500_chargalg_ex_ac_enable_toggle++;
	}

	return di->ac_chg->ops.enable(di->ac_chg, enable, vset, iset);
}

/**
 * abx500_chargalg_usb_en() - Turn on/off the USB charger
 * @di:		pointer to the abx500_chargalg structure
 * @enable:	charger on/off
 * @vset:	requested charger output voltage
 * @iset:	requested charger output current
 *
 * The USB charger will be turned on/off with the requested charge voltage and
 * current
 */
static int abx500_chargalg_usb_en(struct abx500_chargalg *di, int enable,
	int vset, int iset)
{
	if (!di->usb_chg || !di->usb_chg->ops.enable)
		return -ENXIO;

	/* Select maximum of what both the charger and the battery supports */
	if (di->usb_chg->max_out_volt)
		vset = min(vset, di->usb_chg->max_out_volt);
	if (di->usb_chg->max_out_curr)
		iset = min(iset, di->usb_chg->max_out_curr);

	di->chg_info.usb_iset = iset;
	di->chg_info.usb_vset = vset;

	return di->usb_chg->ops.enable(di->usb_chg, enable, vset, iset);
}

 /**
 * ab8540_chargalg_usb_pp_en() - Enable/ disable USB power path
 * @di:                pointer to the abx500_chargalg structure
 * @enable:    power path enable/disable
 *
 * The USB power path will be enable/ disable
 */
static int ab8540_chargalg_usb_pp_en(struct abx500_chargalg *di, bool enable)
{
	if (!di->usb_chg || !di->usb_chg->ops.pp_enable)
		return -ENXIO;

	return di->usb_chg->ops.pp_enable(di->usb_chg, enable);
}

/**
 * ab8540_chargalg_usb_pre_chg_en() - Enable/ disable USB pre-charge
 * @di:                pointer to the abx500_chargalg structure
 * @enable:    USB pre-charge enable/disable
 *
 * The USB USB pre-charge will be enable/ disable
 */
static int ab8540_chargalg_usb_pre_chg_en(struct abx500_chargalg *di,
					  bool enable)
{
	if (!di->usb_chg || !di->usb_chg->ops.pre_chg_enable)
		return -ENXIO;

	return di->usb_chg->ops.pre_chg_enable(di->usb_chg, enable);
}

/**
 * abx500_chargalg_update_chg_curr() - Update charger current
 * @di:		pointer to the abx500_chargalg structure
 * @iset:	requested charger output current
 *
 * The charger output current will be updated for the charger
 * that is currently in use
 */
static int abx500_chargalg_update_chg_curr(struct abx500_chargalg *di,
		int iset)
{
	/* Check if charger exists and update current if charging */
	if (di->ac_chg && di->ac_chg->ops.update_curr &&
			di->chg_info.charger_type & AC_CHG) {
		/*
		 * Select maximum of what both the charger
		 * and the battery supports
		 */
		if (di->ac_chg->max_out_curr)
			iset = min(iset, di->ac_chg->max_out_curr);

		di->chg_info.ac_iset = iset;

		return di->ac_chg->ops.update_curr(di->ac_chg, iset);
	} else if (di->usb_chg && di->usb_chg->ops.update_curr &&
			di->chg_info.charger_type & USB_CHG) {
		/*
		 * Select maximum of what both the charger
		 * and the battery supports
		 */
		if (di->usb_chg->max_out_curr)
			iset = min(iset, di->usb_chg->max_out_curr);

		di->chg_info.usb_iset = iset;

		return di->usb_chg->ops.update_curr(di->usb_chg, iset);
	}

	return -ENXIO;
}

/**
 * abx500_chargalg_stop_charging() - Stop charging
 * @di:		pointer to the abx500_chargalg structure
 *
 * This function is called from any state where charging should be stopped.
 * All charging is disabled and all status parameters and timers are changed
 * accordingly
 */
static void abx500_chargalg_stop_charging(struct abx500_chargalg *di)
{
	abx500_chargalg_ac_en(di, false, 0, 0);
	abx500_chargalg_usb_en(di, false, 0, 0);
	abx500_chargalg_stop_safety_timer(di);
	abx500_chargalg_stop_maintenance_timer(di);
	di->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	di->maintenance_chg = false;
	cancel_delayed_work(&di->chargalg_wd_work);
	power_supply_changed(&di->chargalg_psy);
}

/**
 * abx500_chargalg_hold_charging() - Pauses charging
 * @di:		pointer to the abx500_chargalg structure
 *
 * This function is called in the case where maintenance charging has been
 * disabled and instead a battery voltage mode is entered to check when the
 * battery voltage has reached a certain recharge voltage
 */
static void abx500_chargalg_hold_charging(struct abx500_chargalg *di)
{
	abx500_chargalg_ac_en(di, false, 0, 0);
	abx500_chargalg_usb_en(di, false, 0, 0);
	abx500_chargalg_stop_safety_timer(di);
	abx500_chargalg_stop_maintenance_timer(di);
	di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
	di->maintenance_chg = false;
	cancel_delayed_work(&di->chargalg_wd_work);
	power_supply_changed(&di->chargalg_psy);
}

/**
 * abx500_chargalg_start_charging() - Start the charger
 * @di:		pointer to the abx500_chargalg structure
 * @vset:	requested charger output voltage
 * @iset:	requested charger output current
 *
 * A charger will be enabled depending on the requested charger type that was
 * detected previously.
 */
static void abx500_chargalg_start_charging(struct abx500_chargalg *di,
	int vset, int iset)
{
	bool start_chargalg_wd = true;

	switch (di->chg_info.charger_type) {
	case AC_CHG:
		dev_dbg(di->dev,
			"AC parameters: Vset %d, Ich %d\n", vset, iset);
		abx500_chargalg_usb_en(di, false, 0, 0);
		abx500_chargalg_ac_en(di, true, vset, iset);
		break;

	case USB_CHG:
		dev_dbg(di->dev,
			"USB parameters: Vset %d, Ich %d\n", vset, iset);
		abx500_chargalg_ac_en(di, false, 0, 0);
		abx500_chargalg_usb_en(di, true, vset, iset);
		break;

	default:
		dev_err(di->dev, "Unknown charger to charge from\n");
		start_chargalg_wd = false;
		break;
	}

	if (start_chargalg_wd && !delayed_work_pending(&di->chargalg_wd_work))
		queue_delayed_work(di->chargalg_wq, &di->chargalg_wd_work, 0);
}

/**
 * abx500_chargalg_check_temp() - Check battery temperature ranges
 * @di:		pointer to the abx500_chargalg structure
 *
 * The battery temperature is checked against the predefined limits and the
 * charge state is changed accordingly
 */
static void abx500_chargalg_check_temp(struct abx500_chargalg *di)
{
	if (di->batt_data.temp > (di->bm->temp_low + di->t_hyst_norm) &&
		di->batt_data.temp < (di->bm->temp_high - di->t_hyst_norm)) {
		/* Temp OK! */
		di->events.btemp_underover = false;
		di->events.btemp_lowhigh = false;
		di->t_hyst_norm = 0;
		di->t_hyst_lowhigh = 0;
	} else {
		if (((di->batt_data.temp >= di->bm->temp_high) &&
			(di->batt_data.temp <
				(di->bm->temp_over - di->t_hyst_lowhigh))) ||
			((di->batt_data.temp >
				(di->bm->temp_under + di->t_hyst_lowhigh)) &&
			(di->batt_data.temp <= di->bm->temp_low))) {
			/* TEMP minor!!!!! */
			di->events.btemp_underover = false;
			di->events.btemp_lowhigh = true;
			di->t_hyst_norm = di->bm->temp_hysteresis;
			di->t_hyst_lowhigh = 0;
		} else if (di->batt_data.temp <= di->bm->temp_under ||
			di->batt_data.temp >= di->bm->temp_over) {
			/* TEMP major!!!!! */
			di->events.btemp_underover = true;
			di->events.btemp_lowhigh = false;
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
 * abx500_chargalg_check_charger_voltage() - Check charger voltage
 * @di:		pointer to the abx500_chargalg structure
 *
 * Charger voltage is checked against maximum limit
 */
static void abx500_chargalg_check_charger_voltage(struct abx500_chargalg *di)
{
	if (di->chg_info.usb_volt > di->bm->chg_params->usb_volt_max)
		di->chg_info.usb_chg_ok = false;
	else
		di->chg_info.usb_chg_ok = true;

	if (di->chg_info.ac_volt > di->bm->chg_params->ac_volt_max)
		di->chg_info.ac_chg_ok = false;
	else
		di->chg_info.ac_chg_ok = true;

}

/**
 * abx500_chargalg_end_of_charge() - Check if end-of-charge criteria is fulfilled
 * @di:		pointer to the abx500_chargalg structure
 *
 * End-of-charge criteria is fulfilled when the battery voltage is above a
 * certain limit and the battery current is below a certain limit for a
 * predefined number of consecutive seconds. If true, the battery is full
 */
static void abx500_chargalg_end_of_charge(struct abx500_chargalg *di)
{
	if (di->charge_status == POWER_SUPPLY_STATUS_CHARGING &&
		di->charge_state == STATE_NORMAL &&
		!di->maintenance_chg && (di->batt_data.volt >=
		di->bm->bat_type[di->bm->batt_id].termination_vol ||
		di->events.usb_cv_active || di->events.ac_cv_active) &&
		di->batt_data.avg_curr <
		di->bm->bat_type[di->bm->batt_id].termination_curr &&
		di->batt_data.avg_curr > 0) {
		if (++di->eoc_cnt >= EOC_COND_CNT) {
			di->eoc_cnt = 0;
			if ((di->chg_info.charger_type & USB_CHG) &&
			   (di->usb_chg->power_path))
				ab8540_chargalg_usb_pp_en(di, true);
			di->charge_status = POWER_SUPPLY_STATUS_FULL;
			di->maintenance_chg = true;
			dev_dbg(di->dev, "EOC reached!\n");
			power_supply_changed(&di->chargalg_psy);
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

static void init_maxim_chg_curr(struct abx500_chargalg *di)
{
	di->ccm.original_iset =
		di->bm->bat_type[di->bm->batt_id].normal_cur_lvl;
	di->ccm.current_iset =
		di->bm->bat_type[di->bm->batt_id].normal_cur_lvl;
	di->ccm.test_delta_i = di->bm->maxi->charger_curr_step;
	di->ccm.max_current = di->bm->maxi->chg_curr;
	di->ccm.condition_cnt = di->bm->maxi->wait_cycles;
	di->ccm.level = 0;
}

/**
 * abx500_chargalg_chg_curr_maxim - increases the charger current to
 *			compensate for the system load
 * @di		pointer to the abx500_chargalg structure
 *
 * This maximization function is used to raise the charger current to get the
 * battery current as close to the optimal value as possible. The battery
 * current during charging is affected by the system load
 */
static enum maxim_ret abx500_chargalg_chg_curr_maxim(struct abx500_chargalg *di)
{
	int delta_i;

	if (!di->bm->maxi->ena_maxi)
		return MAXIM_RET_NOACTION;

	delta_i = di->ccm.original_iset - di->batt_data.inst_curr;

	if (di->events.vbus_collapsed) {
		dev_dbg(di->dev, "Charger voltage has collapsed %d\n",
				di->ccm.wait_cnt);
		if (di->ccm.wait_cnt == 0) {
			dev_dbg(di->dev, "lowering current\n");
			di->ccm.wait_cnt++;
			di->ccm.condition_cnt = di->bm->maxi->wait_cycles;
			di->ccm.max_current =
				di->ccm.current_iset - di->ccm.test_delta_i;
			di->ccm.current_iset = di->ccm.max_current;
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

	if ((di->batt_data.inst_curr > di->ccm.original_iset)) {
		dev_dbg(di->dev, " Maximization Ibat (%dmA) too high"
			" (limit %dmA) (current iset: %dmA)!\n",
			di->batt_data.inst_curr, di->ccm.original_iset,
			di->ccm.current_iset);

		if (di->ccm.current_iset == di->ccm.original_iset)
			return MAXIM_RET_NOACTION;

		di->ccm.condition_cnt = di->bm->maxi->wait_cycles;
		di->ccm.current_iset = di->ccm.original_iset;
		di->ccm.level = 0;

		return MAXIM_RET_IBAT_TOO_HIGH;
	}

	if (delta_i > di->ccm.test_delta_i &&
		(di->ccm.current_iset + di->ccm.test_delta_i) <
		di->ccm.max_current) {
		if (di->ccm.condition_cnt-- == 0) {
			/* Increse the iset with cco.test_delta_i */
			di->ccm.condition_cnt = di->bm->maxi->wait_cycles;
			di->ccm.current_iset += di->ccm.test_delta_i;
			di->ccm.level++;
			dev_dbg(di->dev, " Maximization needed, increase"
				" with %d mA to %dmA (Optimal ibat: %d)"
				" Level %d\n",
				di->ccm.test_delta_i,
				di->ccm.current_iset,
				di->ccm.original_iset,
				di->ccm.level);
			return MAXIM_RET_CHANGE;
		} else {
			return MAXIM_RET_NOACTION;
		}
	}  else {
		di->ccm.condition_cnt = di->bm->maxi->wait_cycles;
		return MAXIM_RET_NOACTION;
	}
}

static void handle_maxim_chg_curr(struct abx500_chargalg *di)
{
	enum maxim_ret ret;
	int result;

	ret = abx500_chargalg_chg_curr_maxim(di);
	switch (ret) {
	case MAXIM_RET_CHANGE:
		result = abx500_chargalg_update_chg_curr(di,
			di->ccm.current_iset);
		if (result)
			dev_err(di->dev, "failed to set chg curr\n");
		break;
	case MAXIM_RET_IBAT_TOO_HIGH:
		result = abx500_chargalg_update_chg_curr(di,
			di->bm->bat_type[di->bm->batt_id].normal_cur_lvl);
		if (result)
			dev_err(di->dev, "failed to set chg curr\n");
		break;

	case MAXIM_RET_NOACTION:
	default:
		/* Do nothing..*/
		break;
	}
}

static int abx500_chargalg_get_ext_psy_data(struct device *dev, void *data)
{
	struct power_supply *psy;
	struct power_supply *ext;
	struct abx500_chargalg *di;
	union power_supply_propval ret;
	int i, j;
	bool psy_found = false;
	bool capacity_updated = false;

	psy = (struct power_supply *)data;
	ext = dev_get_drvdata(dev);
	di = to_abx500_chargalg_device_info(psy);
	/* For all psy where the driver name appears in any supplied_to */
	for (i = 0; i < ext->num_supplicants; i++) {
		if (!strcmp(ext->supplied_to[i], psy->name))
			psy_found = true;
	}
	if (!psy_found)
		return 0;

	/*
	 *  If external is not registering 'POWER_SUPPLY_PROP_CAPACITY' to its
	 * property because of handling that sysfs entry on its own, this is
	 * the place to get the battery capacity.
	 */
	if (!ext->get_property(ext, POWER_SUPPLY_PROP_CAPACITY, &ret)) {
		di->batt_data.percent = ret.intval;
		capacity_updated = true;
	}

	/* Go through all properties for the psy */
	for (j = 0; j < ext->num_properties; j++) {
		enum power_supply_property prop;
		prop = ext->properties[j];

		/* Initialize chargers if not already done */
		if (!di->ac_chg &&
			ext->type == POWER_SUPPLY_TYPE_MAINS)
			di->ac_chg = psy_to_ux500_charger(ext);
		else if (!di->usb_chg &&
			ext->type == POWER_SUPPLY_TYPE_USB)
			di->usb_chg = psy_to_ux500_charger(ext);

		if (ext->get_property(ext, prop, &ret))
			continue;
		switch (prop) {
		case POWER_SUPPLY_PROP_PRESENT:
			switch (ext->type) {
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
			switch (ext->type) {
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
			switch (ext->type) {
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
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				di->batt_data.volt = ret.intval / 1000;
				break;
			case POWER_SUPPLY_TYPE_MAINS:
				di->chg_info.ac_volt = ret.intval / 1000;
				break;
			case POWER_SUPPLY_TYPE_USB:
				di->chg_info.usb_volt = ret.intval / 1000;
				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_VOLTAGE_AVG:
			switch (ext->type) {
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
			switch (ext->type) {
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
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_MAINS:
					di->chg_info.ac_curr =
						ret.intval / 1000;
					break;
			case POWER_SUPPLY_TYPE_USB:
					di->chg_info.usb_curr =
						ret.intval / 1000;
				break;
			case POWER_SUPPLY_TYPE_BATTERY:
				di->batt_data.inst_curr = ret.intval / 1000;
				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_CURRENT_AVG:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				di->batt_data.avg_curr = ret.intval / 1000;
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
 * abx500_chargalg_external_power_changed() - callback for power supply changes
 * @psy:       pointer to the structure power_supply
 *
 * This function is the entry point of the pointer external_power_changed
 * of the structure power_supply.
 * This function gets executed when there is a change in any external power
 * supply that this driver needs to be notified of.
 */
static void abx500_chargalg_external_power_changed(struct power_supply *psy)
{
	struct abx500_chargalg *di = to_abx500_chargalg_device_info(psy);

	/*
	 * Trigger execution of the algorithm instantly and read
	 * all power_supply properties there instead
	 */
	queue_work(di->chargalg_wq, &di->chargalg_work);
}

/**
 * abx500_chargalg_algorithm() - Main function for the algorithm
 * @di:		pointer to the abx500_chargalg structure
 *
 * This is the main control function for the charging algorithm.
 * It is called periodically or when something happens that will
 * trigger a state change
 */
static void abx500_chargalg_algorithm(struct abx500_chargalg *di)
{
	int charger_status;
	int ret;

	/* Collect data from all power_supply class devices */
	class_for_each_device(power_supply_class, NULL,
		&di->chargalg_psy, abx500_chargalg_get_ext_psy_data);

	abx500_chargalg_end_of_charge(di);
	abx500_chargalg_check_temp(di);
	abx500_chargalg_check_charger_voltage(di);

	charger_status = abx500_chargalg_check_charger_connection(di);

	if (is_ab8500(di->parent)) {
		ret = abx500_chargalg_check_charger_enable(di);
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
			abx500_chargalg_state_to(di, STATE_HANDHELD_INIT);
		}
	}

	/* If suspended, we should not continue checking the flags */
	else if (di->charge_state == STATE_SUSPENDED_INIT ||
		di->charge_state == STATE_SUSPENDED) {
		/* We don't do anything here, just don,t continue */
	}

	/* Safety timer expiration */
	else if (di->events.safety_timer_expired) {
		if (di->charge_state != STATE_SAFETY_TIMER_EXPIRED)
			abx500_chargalg_state_to(di,
				STATE_SAFETY_TIMER_EXPIRED_INIT);
	}
	/*
	 * Check if any interrupts has occured
	 * that will prevent us from charging
	 */

	/* Battery removed */
	else if (di->events.batt_rem) {
		if (di->charge_state != STATE_BATT_REMOVED)
			abx500_chargalg_state_to(di, STATE_BATT_REMOVED_INIT);
	}
	/* Main or USB charger not ok. */
	else if (di->events.mainextchnotok || di->events.usbchargernotok) {
		/*
		 * If vbus_collapsed is set, we have to lower the charger
		 * current, which is done in the normal state below
		 */
		if (di->charge_state != STATE_CHG_NOT_OK &&
				!di->events.vbus_collapsed)
			abx500_chargalg_state_to(di, STATE_CHG_NOT_OK_INIT);
	}
	/* VBUS, Main or VBAT OVV. */
	else if (di->events.vbus_ovv ||
			di->events.main_ovv ||
			di->events.batt_ovv ||
			!di->chg_info.usb_chg_ok ||
			!di->chg_info.ac_chg_ok) {
		if (di->charge_state != STATE_OVV_PROTECT)
			abx500_chargalg_state_to(di, STATE_OVV_PROTECT_INIT);
	}
	/* USB Thermal, stop charging */
	else if (di->events.main_thermal_prot ||
		di->events.usb_thermal_prot) {
		if (di->charge_state != STATE_HW_TEMP_PROTECT)
			abx500_chargalg_state_to(di,
				STATE_HW_TEMP_PROTECT_INIT);
	}
	/* Battery temp over/under */
	else if (di->events.btemp_underover) {
		if (di->charge_state != STATE_TEMP_UNDEROVER)
			abx500_chargalg_state_to(di,
				STATE_TEMP_UNDEROVER_INIT);
	}
	/* Watchdog expired */
	else if (di->events.ac_wd_expired ||
		di->events.usb_wd_expired) {
		if (di->charge_state != STATE_WD_EXPIRED)
			abx500_chargalg_state_to(di, STATE_WD_EXPIRED_INIT);
	}
	/* Battery temp high/low */
	else if (di->events.btemp_lowhigh) {
		if (di->charge_state != STATE_TEMP_LOWHIGH)
			abx500_chargalg_state_to(di, STATE_TEMP_LOWHIGH_INIT);
	}

	dev_dbg(di->dev,
		"[CHARGALG] Vb %d Ib_avg %d Ib_inst %d Tb %d Cap %d Maint %d "
		"State %s Active_chg %d Chg_status %d AC %d USB %d "
		"AC_online %d USB_online %d AC_CV %d USB_CV %d AC_I %d "
		"USB_I %d AC_Vset %d AC_Iset %d USB_Vset %d USB_Iset %d\n",
		di->batt_data.volt,
		di->batt_data.avg_curr,
		di->batt_data.inst_curr,
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
		di->chg_info.ac_curr,
		di->chg_info.usb_curr,
		di->chg_info.ac_vset,
		di->chg_info.ac_iset,
		di->chg_info.usb_vset,
		di->chg_info.usb_iset);

	switch (di->charge_state) {
	case STATE_HANDHELD_INIT:
		abx500_chargalg_stop_charging(di);
		di->charge_status = POWER_SUPPLY_STATUS_DISCHARGING;
		abx500_chargalg_state_to(di, STATE_HANDHELD);
		/* Intentional fallthrough */

	case STATE_HANDHELD:
		break;

	case STATE_SUSPENDED_INIT:
		if (di->susp_status.ac_suspended)
			abx500_chargalg_ac_en(di, false, 0, 0);
		if (di->susp_status.usb_suspended)
			abx500_chargalg_usb_en(di, false, 0, 0);
		abx500_chargalg_stop_safety_timer(di);
		abx500_chargalg_stop_maintenance_timer(di);
		di->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		di->maintenance_chg = false;
		abx500_chargalg_state_to(di, STATE_SUSPENDED);
		power_supply_changed(&di->chargalg_psy);
		/* Intentional fallthrough */

	case STATE_SUSPENDED:
		/* CHARGING is suspended */
		break;

	case STATE_BATT_REMOVED_INIT:
		abx500_chargalg_stop_charging(di);
		abx500_chargalg_state_to(di, STATE_BATT_REMOVED);
		/* Intentional fallthrough */

	case STATE_BATT_REMOVED:
		if (!di->events.batt_rem)
			abx500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_HW_TEMP_PROTECT_INIT:
		abx500_chargalg_stop_charging(di);
		abx500_chargalg_state_to(di, STATE_HW_TEMP_PROTECT);
		/* Intentional fallthrough */

	case STATE_HW_TEMP_PROTECT:
		if (!di->events.main_thermal_prot &&
				!di->events.usb_thermal_prot)
			abx500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_OVV_PROTECT_INIT:
		abx500_chargalg_stop_charging(di);
		abx500_chargalg_state_to(di, STATE_OVV_PROTECT);
		/* Intentional fallthrough */

	case STATE_OVV_PROTECT:
		if (!di->events.vbus_ovv &&
				!di->events.main_ovv &&
				!di->events.batt_ovv &&
				di->chg_info.usb_chg_ok &&
				di->chg_info.ac_chg_ok)
			abx500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_CHG_NOT_OK_INIT:
		abx500_chargalg_stop_charging(di);
		abx500_chargalg_state_to(di, STATE_CHG_NOT_OK);
		/* Intentional fallthrough */

	case STATE_CHG_NOT_OK:
		if (!di->events.mainextchnotok &&
				!di->events.usbchargernotok)
			abx500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_SAFETY_TIMER_EXPIRED_INIT:
		abx500_chargalg_stop_charging(di);
		abx500_chargalg_state_to(di, STATE_SAFETY_TIMER_EXPIRED);
		/* Intentional fallthrough */

	case STATE_SAFETY_TIMER_EXPIRED:
		/* We exit this state when charger is removed */
		break;

	case STATE_NORMAL_INIT:
		if ((di->chg_info.charger_type & USB_CHG) &&
				di->usb_chg->power_path) {
			if (di->batt_data.volt >
			    (di->bm->fg_params->lowbat_threshold +
			     BAT_PLUS_MARGIN)) {
				ab8540_chargalg_usb_pre_chg_en(di, false);
				ab8540_chargalg_usb_pp_en(di, false);
			} else {
				ab8540_chargalg_usb_pp_en(di, true);
				ab8540_chargalg_usb_pre_chg_en(di, true);
				abx500_chargalg_state_to(di,
					STATE_USB_PP_PRE_CHARGE);
				break;
			}
		}

		abx500_chargalg_start_charging(di,
			di->bm->bat_type[di->bm->batt_id].normal_vol_lvl,
			di->bm->bat_type[di->bm->batt_id].normal_cur_lvl);
		abx500_chargalg_state_to(di, STATE_NORMAL);
		abx500_chargalg_start_safety_timer(di);
		abx500_chargalg_stop_maintenance_timer(di);
		init_maxim_chg_curr(di);
		di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
		di->eoc_cnt = 0;
		di->maintenance_chg = false;
		power_supply_changed(&di->chargalg_psy);

		break;

	case STATE_USB_PP_PRE_CHARGE:
		if (di->batt_data.volt >
			(di->bm->fg_params->lowbat_threshold +
			BAT_PLUS_MARGIN))
			abx500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_NORMAL:
		handle_maxim_chg_curr(di);
		if (di->charge_status == POWER_SUPPLY_STATUS_FULL &&
			di->maintenance_chg) {
			if (di->bm->no_maintenance)
				abx500_chargalg_state_to(di,
					STATE_WAIT_FOR_RECHARGE_INIT);
			else
				abx500_chargalg_state_to(di,
					STATE_MAINTENANCE_A_INIT);
		}
		break;

	/* This state will be used when the maintenance state is disabled */
	case STATE_WAIT_FOR_RECHARGE_INIT:
		abx500_chargalg_hold_charging(di);
		abx500_chargalg_state_to(di, STATE_WAIT_FOR_RECHARGE);
		/* Intentional fallthrough */

	case STATE_WAIT_FOR_RECHARGE:
		if (di->batt_data.percent <=
		    di->bm->bat_type[di->bm->batt_id].
		    recharge_cap)
			abx500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_MAINTENANCE_A_INIT:
		abx500_chargalg_stop_safety_timer(di);
		abx500_chargalg_start_maintenance_timer(di,
			di->bm->bat_type[
				di->bm->batt_id].maint_a_chg_timer_h);
		abx500_chargalg_start_charging(di,
			di->bm->bat_type[
				di->bm->batt_id].maint_a_vol_lvl,
			di->bm->bat_type[
				di->bm->batt_id].maint_a_cur_lvl);
		abx500_chargalg_state_to(di, STATE_MAINTENANCE_A);
		power_supply_changed(&di->chargalg_psy);
		/* Intentional fallthrough*/

	case STATE_MAINTENANCE_A:
		if (di->events.maintenance_timer_expired) {
			abx500_chargalg_stop_maintenance_timer(di);
			abx500_chargalg_state_to(di, STATE_MAINTENANCE_B_INIT);
		}
		break;

	case STATE_MAINTENANCE_B_INIT:
		abx500_chargalg_start_maintenance_timer(di,
			di->bm->bat_type[
				di->bm->batt_id].maint_b_chg_timer_h);
		abx500_chargalg_start_charging(di,
			di->bm->bat_type[
				di->bm->batt_id].maint_b_vol_lvl,
			di->bm->bat_type[
				di->bm->batt_id].maint_b_cur_lvl);
		abx500_chargalg_state_to(di, STATE_MAINTENANCE_B);
		power_supply_changed(&di->chargalg_psy);
		/* Intentional fallthrough*/

	case STATE_MAINTENANCE_B:
		if (di->events.maintenance_timer_expired) {
			abx500_chargalg_stop_maintenance_timer(di);
			abx500_chargalg_state_to(di, STATE_NORMAL_INIT);
		}
		break;

	case STATE_TEMP_LOWHIGH_INIT:
		abx500_chargalg_start_charging(di,
			di->bm->bat_type[
				di->bm->batt_id].low_high_vol_lvl,
			di->bm->bat_type[
				di->bm->batt_id].low_high_cur_lvl);
		abx500_chargalg_stop_maintenance_timer(di);
		di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
		abx500_chargalg_state_to(di, STATE_TEMP_LOWHIGH);
		power_supply_changed(&di->chargalg_psy);
		/* Intentional fallthrough */

	case STATE_TEMP_LOWHIGH:
		if (!di->events.btemp_lowhigh)
			abx500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_WD_EXPIRED_INIT:
		abx500_chargalg_stop_charging(di);
		abx500_chargalg_state_to(di, STATE_WD_EXPIRED);
		/* Intentional fallthrough */

	case STATE_WD_EXPIRED:
		if (!di->events.ac_wd_expired &&
				!di->events.usb_wd_expired)
			abx500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;

	case STATE_TEMP_UNDEROVER_INIT:
		abx500_chargalg_stop_charging(di);
		abx500_chargalg_state_to(di, STATE_TEMP_UNDEROVER);
		/* Intentional fallthrough */

	case STATE_TEMP_UNDEROVER:
		if (!di->events.btemp_underover)
			abx500_chargalg_state_to(di, STATE_NORMAL_INIT);
		break;
	}

	/* Start charging directly if the new state is a charge state */
	if (di->charge_state == STATE_NORMAL_INIT ||
			di->charge_state == STATE_MAINTENANCE_A_INIT ||
			di->charge_state == STATE_MAINTENANCE_B_INIT)
		queue_work(di->chargalg_wq, &di->chargalg_work);
}

/**
 * abx500_chargalg_periodic_work() - Periodic work for the algorithm
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for the charging algorithm
 */
static void abx500_chargalg_periodic_work(struct work_struct *work)
{
	struct abx500_chargalg *di = container_of(work,
		struct abx500_chargalg, chargalg_periodic_work.work);

	abx500_chargalg_algorithm(di);

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
 * abx500_chargalg_wd_work() - periodic work to kick the charger watchdog
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for kicking the charger watchdog
 */
static void abx500_chargalg_wd_work(struct work_struct *work)
{
	int ret;
	struct abx500_chargalg *di = container_of(work,
		struct abx500_chargalg, chargalg_wd_work.work);

	dev_dbg(di->dev, "abx500_chargalg_wd_work\n");

	ret = abx500_chargalg_kick_watchdog(di);
	if (ret < 0)
		dev_err(di->dev, "failed to kick watchdog\n");

	queue_delayed_work(di->chargalg_wq,
		&di->chargalg_wd_work, CHG_WD_INTERVAL);
}

/**
 * abx500_chargalg_work() - Work to run the charging algorithm instantly
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for calling the charging algorithm
 */
static void abx500_chargalg_work(struct work_struct *work)
{
	struct abx500_chargalg *di = container_of(work,
		struct abx500_chargalg, chargalg_work);

	abx500_chargalg_algorithm(di);
}

/**
 * abx500_chargalg_get_property() - get the chargalg properties
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
static int abx500_chargalg_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct abx500_chargalg *di;

	di = to_abx500_chargalg_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = di->charge_status;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if (di->events.batt_ovv) {
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		} else if (di->events.btemp_underover) {
			if (di->batt_data.temp <= di->bm->temp_under)
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

/* Exposure to the sysfs interface */

/**
 * abx500_chargalg_sysfs_show() - sysfs show operations
 * @kobj:      pointer to the struct kobject
 * @attr:      pointer to the struct attribute
 * @buf:       buffer that holds the parameter to send to userspace
 *
 * Returns a buffer to be displayed in user space
 */
static ssize_t abx500_chargalg_sysfs_show(struct kobject *kobj,
					  struct attribute *attr, char *buf)
{
	struct abx500_chargalg *di = container_of(kobj,
               struct abx500_chargalg, chargalg_kobject);

	return sprintf(buf, "%d\n",
		       di->susp_status.ac_suspended &&
		       di->susp_status.usb_suspended);
}

/**
 * abx500_chargalg_sysfs_charger() - sysfs store operations
 * @kobj:      pointer to the struct kobject
 * @attr:      pointer to the struct attribute
 * @buf:       buffer that holds the parameter passed from userspace
 * @length:    length of the parameter passed
 *
 * Returns length of the buffer(input taken from user space) on success
 * else error code on failure
 * The operation to be performed on passing the parameters from the user space.
 */
static ssize_t abx500_chargalg_sysfs_charger(struct kobject *kobj,
	struct attribute *attr, const char *buf, size_t length)
{
	struct abx500_chargalg *di = container_of(kobj,
		struct abx500_chargalg, chargalg_kobject);
	long int param;
	int ac_usb;
	int ret;
	char entry = *attr->name;

	switch (entry) {
	case 'c':
		ret = strict_strtol(buf, 10, &param);
		if (ret < 0)
			return ret;

		ac_usb = param;
		switch (ac_usb) {
		case 0:
			/* Disable charging */
			di->susp_status.ac_suspended = true;
			di->susp_status.usb_suspended = true;
			di->susp_status.suspended_change = true;
			/* Trigger a state change */
			queue_work(di->chargalg_wq,
				&di->chargalg_work);
			break;
		case 1:
			/* Enable AC Charging */
			di->susp_status.ac_suspended = false;
			di->susp_status.suspended_change = true;
			/* Trigger a state change */
			queue_work(di->chargalg_wq,
				&di->chargalg_work);
			break;
		case 2:
			/* Enable USB charging */
			di->susp_status.usb_suspended = false;
			di->susp_status.suspended_change = true;
			/* Trigger a state change */
			queue_work(di->chargalg_wq,
				&di->chargalg_work);
			break;
		default:
			dev_info(di->dev, "Wrong input\n"
				"Enter 0. Disable AC/USB Charging\n"
				"1. Enable AC charging\n"
				"2. Enable USB Charging\n");
		};
		break;
	};
	return strlen(buf);
}

static struct attribute abx500_chargalg_en_charger = \
{
	.name = "chargalg",
	.mode = S_IRUGO | S_IWUSR,
};

static struct attribute *abx500_chargalg_chg[] = {
	&abx500_chargalg_en_charger,
	NULL
};

static const struct sysfs_ops abx500_chargalg_sysfs_ops = {
	.show = abx500_chargalg_sysfs_show,
	.store = abx500_chargalg_sysfs_charger,
};

static struct kobj_type abx500_chargalg_ktype = {
	.sysfs_ops = &abx500_chargalg_sysfs_ops,
	.default_attrs = abx500_chargalg_chg,
};

/**
 * abx500_chargalg_sysfs_exit() - de-init of sysfs entry
 * @di:                pointer to the struct abx500_chargalg
 *
 * This function removes the entry in sysfs.
 */
static void abx500_chargalg_sysfs_exit(struct abx500_chargalg *di)
{
	kobject_del(&di->chargalg_kobject);
}

/**
 * abx500_chargalg_sysfs_init() - init of sysfs entry
 * @di:                pointer to the struct abx500_chargalg
 *
 * This function adds an entry in sysfs.
 * Returns error code in case of failure else 0(on success)
 */
static int abx500_chargalg_sysfs_init(struct abx500_chargalg *di)
{
	int ret = 0;

	ret = kobject_init_and_add(&di->chargalg_kobject,
		&abx500_chargalg_ktype,
		NULL, "abx500_chargalg");
	if (ret < 0)
		dev_err(di->dev, "failed to create sysfs entry\n");

	return ret;
}
/* Exposure to the sysfs interface <<END>> */

#if defined(CONFIG_PM)
static int abx500_chargalg_resume(struct platform_device *pdev)
{
	struct abx500_chargalg *di = platform_get_drvdata(pdev);

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

static int abx500_chargalg_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct abx500_chargalg *di = platform_get_drvdata(pdev);

	if (di->chg_info.online_chg)
		cancel_delayed_work_sync(&di->chargalg_wd_work);

	cancel_delayed_work_sync(&di->chargalg_periodic_work);

	return 0;
}
#else
#define abx500_chargalg_suspend      NULL
#define abx500_chargalg_resume       NULL
#endif

static int abx500_chargalg_remove(struct platform_device *pdev)
{
	struct abx500_chargalg *di = platform_get_drvdata(pdev);

	/* sysfs interface to enable/disbale charging from user space */
	abx500_chargalg_sysfs_exit(di);

	/* Delete the work queue */
	destroy_workqueue(di->chargalg_wq);

	flush_scheduled_work();
	power_supply_unregister(&di->chargalg_psy);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static char *supply_interface[] = {
	"ab8500_fg",
};

static int abx500_chargalg_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct abx500_bm_data *plat = pdev->dev.platform_data;
	struct abx500_chargalg *di;
	int ret = 0;

	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&pdev->dev, "%s no mem for ab8500_chargalg\n", __func__);
		return -ENOMEM;
	}

	if (!plat) {
		dev_err(&pdev->dev, "no battery management data supplied\n");
		return -EINVAL;
	}
	di->bm = plat;

	if (np) {
		ret = ab8500_bm_of_probe(&pdev->dev, np, di->bm);
		if (ret) {
			dev_err(&pdev->dev, "failed to get battery information\n");
			return ret;
		}
	}

	/* get device struct and parent */
	di->dev = &pdev->dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);

	/* chargalg supply */
	di->chargalg_psy.name = "abx500_chargalg";
	di->chargalg_psy.type = POWER_SUPPLY_TYPE_BATTERY;
	di->chargalg_psy.properties = abx500_chargalg_props;
	di->chargalg_psy.num_properties = ARRAY_SIZE(abx500_chargalg_props);
	di->chargalg_psy.get_property = abx500_chargalg_get_property;
	di->chargalg_psy.supplied_to = supply_interface;
	di->chargalg_psy.num_supplicants = ARRAY_SIZE(supply_interface),
	di->chargalg_psy.external_power_changed =
		abx500_chargalg_external_power_changed;

	/* Initilialize safety timer */
	init_timer(&di->safety_timer);
	di->safety_timer.function = abx500_chargalg_safety_timer_expired;
	di->safety_timer.data = (unsigned long) di;

	/* Initilialize maintenance timer */
	init_timer(&di->maintenance_timer);
	di->maintenance_timer.function =
		abx500_chargalg_maintenance_timer_expired;
	di->maintenance_timer.data = (unsigned long) di;

	/* Create a work queue for the chargalg */
	di->chargalg_wq =
		create_singlethread_workqueue("abx500_chargalg_wq");
	if (di->chargalg_wq == NULL) {
		dev_err(di->dev, "failed to create work queue\n");
		return -ENOMEM;
	}

	/* Init work for chargalg */
	INIT_DEFERRABLE_WORK(&di->chargalg_periodic_work,
		abx500_chargalg_periodic_work);
	INIT_DEFERRABLE_WORK(&di->chargalg_wd_work,
		abx500_chargalg_wd_work);

	/* Init work for chargalg */
	INIT_WORK(&di->chargalg_work, abx500_chargalg_work);

	/* To detect charger at startup */
	di->chg_info.prev_conn_chg = -1;

	/* Register chargalg power supply class */
	ret = power_supply_register(di->dev, &di->chargalg_psy);
	if (ret) {
		dev_err(di->dev, "failed to register chargalg psy\n");
		goto free_chargalg_wq;
	}

	platform_set_drvdata(pdev, di);

	/* sysfs interface to enable/disable charging from user space */
	ret = abx500_chargalg_sysfs_init(di);
	if (ret) {
		dev_err(di->dev, "failed to create sysfs entry\n");
		goto free_psy;
	}

	/* Run the charging algorithm */
	queue_delayed_work(di->chargalg_wq, &di->chargalg_periodic_work, 0);

	dev_info(di->dev, "probe success\n");
	return ret;

free_psy:
	power_supply_unregister(&di->chargalg_psy);
free_chargalg_wq:
	destroy_workqueue(di->chargalg_wq);
	return ret;
}

static const struct of_device_id ab8500_chargalg_match[] = {
	{ .compatible = "stericsson,ab8500-chargalg", },
	{ },
};

static struct platform_driver abx500_chargalg_driver = {
	.probe = abx500_chargalg_probe,
	.remove = abx500_chargalg_remove,
	.suspend = abx500_chargalg_suspend,
	.resume = abx500_chargalg_resume,
	.driver = {
		.name = "ab8500-chargalg",
		.owner = THIS_MODULE,
		.of_match_table = ab8500_chargalg_match,
	},
};

static int __init abx500_chargalg_init(void)
{
	return platform_driver_register(&abx500_chargalg_driver);
}

static void __exit abx500_chargalg_exit(void)
{
	platform_driver_unregister(&abx500_chargalg_driver);
}

module_init(abx500_chargalg_init);
module_exit(abx500_chargalg_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Johan Palsson, Karl Komierowski");
MODULE_ALIAS("platform:abx500-chargalg");
MODULE_DESCRIPTION("abx500 battery charging algorithm");
