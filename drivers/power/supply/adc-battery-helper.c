// SPDX-License-Identifier: GPL-2.0+
/*
 * Helper for batteries with accurate current and voltage measurement, but
 * without temperature measurement or without a "resistance-temp-table".
 *
 * Some fuel-gauges are not full-featured autonomous fuel-gauges.
 * These fuel-gauges offer accurate current and voltage measurements but
 * their coulomb-counters are intended to work together with an always on
 * micro-controller monitoring the fuel-gauge.
 *
 * This adc-battery-helper code offers open-circuit-voltage (ocv) and through
 * that capacity estimation for devices where such limited functionality
 * fuel-gauges are exposed directly to Linux.
 *
 * This helper requires the hw to provide accurate battery current_now and
 * voltage_now measurement and this helper the provides the following properties
 * based on top of those readings:
 *
 *	POWER_SUPPLY_PROP_STATUS
 *	POWER_SUPPLY_PROP_VOLTAGE_OCV
 *	POWER_SUPPLY_PROP_VOLTAGE_NOW
 *	POWER_SUPPLY_PROP_CURRENT_NOW
 *	POWER_SUPPLY_PROP_CAPACITY
 *
 * As well as optional the following properties assuming an always present
 * system-scope battery, allowing direct use of adc_battery_helper_get_prop()
 * in this common case:
 *	POWER_SUPPLY_PROP_PRESENT
 *	POWER_SUPPLY_PROP_SCOPE
 *
 * Using this helper is as simple as:
 *
 * 1. Embed a struct adc_battery_helper this MUST be the first member of
 *    the battery driver's data struct.
 * 2. Use adc_battery_helper_props[] or add the above properties to
 *    the list of properties in power_supply_desc
 * 3. Call adc_battery_helper_init() after registering the power_supply and
 *    before returning from the probe() function
 * 4. Use adc_battery_helper_get_prop() as the power-supply's get_property()
 *    method, or call it for the above properties.
 * 5. Use adc_battery_helper_external_power_changed() as the power-supply's
 *    external_power_changed() method or call it from that method.
 * 6. Use adc_battery_helper_[suspend|resume]() as suspend-resume methods or
 *    call them from the driver's suspend-resume methods.
 *
 * The provided get_voltage_and_current_now() method will be called by this
 * helper at adc_battery_helper_init() time and later.
 *
 * Copyright (c) 2021-2025 Hans de Goede <hansg@kernel.org>
 */

#include <linux/cleanup.h>
#include <linux/devm-helpers.h>
#include <linux/gpio/consumer.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>

#include "adc-battery-helper.h"

#define MOV_AVG_WINDOW_SIZE			ADC_BAT_HELPER_MOV_AVG_WINDOW_SIZE
#define INIT_POLL_TIME				(5 * HZ)
#define POLL_TIME				(30 * HZ)
#define SETTLE_TIME				(1 * HZ)

#define INIT_POLL_COUNT				30

#define CURR_HYST_UA				65000

#define LOW_BAT_UV				3700000
#define FULL_BAT_HYST_UV			38000

#define AMBIENT_TEMP_CELSIUS			25

static int adc_battery_helper_get_status(struct adc_battery_helper *help)
{
	int full_uv =
		help->psy->battery_info->constant_charge_voltage_max_uv - FULL_BAT_HYST_UV;

	if (help->curr_ua > CURR_HYST_UA)
		return POWER_SUPPLY_STATUS_CHARGING;

	if (help->curr_ua < -CURR_HYST_UA)
		return POWER_SUPPLY_STATUS_DISCHARGING;

	if (help->supplied) {
		bool full;

		if (help->charge_finished)
			full = gpiod_get_value_cansleep(help->charge_finished);
		else
			full = help->ocv_avg_uv > full_uv;

		if (full)
			return POWER_SUPPLY_STATUS_FULL;
	}

	return POWER_SUPPLY_STATUS_NOT_CHARGING;
}

static void adc_battery_helper_work(struct work_struct *work)
{
	struct adc_battery_helper *help = container_of(work, struct adc_battery_helper,
						       work.work);
	int i, curr_diff_ua, volt_diff_uv, res_mohm, ret, win_size;
	struct device *dev = help->psy->dev.parent;
	int volt_uv, prev_volt_uv = help->volt_uv;
	int curr_ua, prev_curr_ua = help->curr_ua;
	bool prev_supplied = help->supplied;
	int prev_status = help->status;

	guard(mutex)(&help->lock);

	ret = help->get_voltage_and_current_now(help->psy, &volt_uv, &curr_ua);
	if (ret)
		goto out;

	help->volt_uv = volt_uv;
	help->curr_ua = curr_ua;

	help->ocv_uv[help->ocv_avg_index] =
		help->volt_uv - help->curr_ua * help->intern_res_avg_mohm / 1000;
	dev_dbg(dev, "volt-now: %d, curr-now: %d, volt-ocv: %d\n",
		help->volt_uv, help->curr_ua, help->ocv_uv[help->ocv_avg_index]);
	help->ocv_avg_index = (help->ocv_avg_index + 1) % MOV_AVG_WINDOW_SIZE;
	help->poll_count++;

	help->ocv_avg_uv = 0;
	win_size = min(help->poll_count, MOV_AVG_WINDOW_SIZE);
	for (i = 0; i < win_size; i++)
		help->ocv_avg_uv += help->ocv_uv[i];
	help->ocv_avg_uv /= win_size;

	help->supplied = power_supply_am_i_supplied(help->psy);
	help->status = adc_battery_helper_get_status(help);
	if (help->status == POWER_SUPPLY_STATUS_FULL)
		help->capacity = 100;
	else
		help->capacity = power_supply_batinfo_ocv2cap(help->psy->battery_info,
							      help->ocv_avg_uv,
							      AMBIENT_TEMP_CELSIUS);

	/*
	 * Skip internal resistance calc on charger [un]plug and
	 * when the battery is almost empty (voltage low).
	 */
	if (help->supplied != prev_supplied ||
	    help->volt_uv < LOW_BAT_UV ||
	    help->poll_count < 2)
		goto out;

	/*
	 * Assuming that the OCV voltage does not change significantly
	 * between 2 polls, then we can calculate the internal resistance
	 * on a significant current change by attributing all voltage
	 * change between the 2 readings to the internal resistance.
	 */
	curr_diff_ua = abs(help->curr_ua - prev_curr_ua);
	if (curr_diff_ua < CURR_HYST_UA)
		goto out;

	volt_diff_uv = abs(help->volt_uv - prev_volt_uv);
	res_mohm = volt_diff_uv * 1000 / curr_diff_ua;

	if ((res_mohm < (help->intern_res_avg_mohm * 2 / 3)) ||
	    (res_mohm > (help->intern_res_avg_mohm * 4 / 3))) {
		dev_dbg(dev, "Ignoring outlier internal resistance %d mOhm\n", res_mohm);
		goto out;
	}

	dev_dbg(dev, "Internal resistance %d mOhm\n", res_mohm);

	help->intern_res_mohm[help->intern_res_avg_index] = res_mohm;
	help->intern_res_avg_index = (help->intern_res_avg_index + 1) % MOV_AVG_WINDOW_SIZE;
	help->intern_res_poll_count++;

	help->intern_res_avg_mohm = 0;
	win_size = min(help->intern_res_poll_count, MOV_AVG_WINDOW_SIZE);
	for (i = 0; i < win_size; i++)
		help->intern_res_avg_mohm += help->intern_res_mohm[i];
	help->intern_res_avg_mohm /= win_size;

out:
	queue_delayed_work(system_percpu_wq, &help->work,
			   (help->poll_count <= INIT_POLL_COUNT) ?
					INIT_POLL_TIME : POLL_TIME);

	if (help->status != prev_status)
		power_supply_changed(help->psy);
}

const enum power_supply_property adc_battery_helper_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_SCOPE,
};
EXPORT_SYMBOL_GPL(adc_battery_helper_properties);

static_assert(ARRAY_SIZE(adc_battery_helper_properties) ==
	      ADC_HELPER_NUM_PROPERTIES);

int adc_battery_helper_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	struct adc_battery_helper *help = power_supply_get_drvdata(psy);
	int dummy, ret = 0;

	/*
	 * Avoid racing with adc_battery_helper_work() while it is updating
	 * variables and avoid calling get_voltage_and_current_now() reentrantly.
	 */
	guard(mutex)(&help->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = help->status;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = help->get_voltage_and_current_now(psy, &val->intval, &dummy);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		val->intval = help->ocv_avg_uv;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = help->get_voltage_and_current_now(psy, &dummy, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = help->capacity;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(adc_battery_helper_get_property);

void adc_battery_helper_external_power_changed(struct power_supply *psy)
{
	struct adc_battery_helper *help = power_supply_get_drvdata(psy);

	dev_dbg(help->psy->dev.parent, "external power changed\n");
	mod_delayed_work(system_percpu_wq, &help->work, SETTLE_TIME);
}
EXPORT_SYMBOL_GPL(adc_battery_helper_external_power_changed);

static void adc_battery_helper_start_work(struct adc_battery_helper *help)
{
	help->poll_count = 0;
	help->ocv_avg_index = 0;

	queue_delayed_work(system_percpu_wq, &help->work, 0);
	flush_delayed_work(&help->work);
}

int adc_battery_helper_init(struct adc_battery_helper *help, struct power_supply *psy,
			    adc_battery_helper_get_func get_voltage_and_current_now,
			    struct gpio_desc *charge_finished_gpio)
{
	struct device *dev = psy->dev.parent;
	int ret;

	help->psy = psy;
	help->get_voltage_and_current_now = get_voltage_and_current_now;
	help->charge_finished = charge_finished_gpio;

	ret = devm_mutex_init(dev, &help->lock);
	if (ret)
		return ret;

	ret = devm_delayed_work_autocancel(dev, &help->work, adc_battery_helper_work);
	if (ret)
		return ret;

	if (!help->psy->battery_info ||
	    help->psy->battery_info->factory_internal_resistance_uohm == -EINVAL ||
	    help->psy->battery_info->constant_charge_voltage_max_uv == -EINVAL ||
	    !psy->battery_info->ocv_table[0]) {
		dev_err(dev, "error required properties are missing\n");
		return -ENODEV;
	}

	/* Use provided internal resistance as start point (in milli-ohm) */
	help->intern_res_avg_mohm =
		help->psy->battery_info->factory_internal_resistance_uohm / 1000;
	/* Also add it to the internal resistance moving average window */
	help->intern_res_mohm[0] = help->intern_res_avg_mohm;
	help->intern_res_avg_index = 1;
	help->intern_res_poll_count = 1;

	adc_battery_helper_start_work(help);
	return 0;
}
EXPORT_SYMBOL_GPL(adc_battery_helper_init);

int adc_battery_helper_suspend(struct device *dev)
{
	struct adc_battery_helper *help = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&help->work);
	return 0;
}
EXPORT_SYMBOL_GPL(adc_battery_helper_suspend);

int adc_battery_helper_resume(struct device *dev)
{
	struct adc_battery_helper *help = dev_get_drvdata(dev);

	adc_battery_helper_start_work(help);
	return 0;
}
EXPORT_SYMBOL_GPL(adc_battery_helper_resume);

MODULE_AUTHOR("Hans de Goede <hansg@kernel.org>");
MODULE_DESCRIPTION("ADC battery capacity estimation helper");
MODULE_LICENSE("GPL");
