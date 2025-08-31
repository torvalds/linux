/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Helper for batteries with accurate current and voltage measurement, but
 * without temperature measurement or without a "resistance-temp-table".
 * Copyright (c) 2021-2025 Hans de Goede <hansg@kernel.org>
 */

#include <linux/mutex.h>
#include <linux/workqueue.h>

#define ADC_BAT_HELPER_MOV_AVG_WINDOW_SIZE		8

struct power_supply;
struct gpio_desc;

/*
 * The adc battery helper code needs voltage- and current-now to be sampled as
 * close to each other (in sample-time) as possible. A single getter function is
 * used to allow the battery driver to handle this in the best way possible.
 */
typedef int (*adc_battery_helper_get_func)(struct power_supply *psy, int *volt, int *curr);

struct adc_battery_helper {
	struct power_supply *psy;
	struct gpio_desc *charge_finished;
	struct delayed_work work;
	struct mutex lock;
	adc_battery_helper_get_func get_voltage_and_current_now;
	int ocv_uv[ADC_BAT_HELPER_MOV_AVG_WINDOW_SIZE];		/* micro-volt */
	int intern_res_mohm[ADC_BAT_HELPER_MOV_AVG_WINDOW_SIZE]; /* milli-ohm */
	int poll_count;
	int ocv_avg_index;
	int ocv_avg_uv;						/* micro-volt */
	int intern_res_poll_count;
	int intern_res_avg_index;
	int intern_res_avg_mohm;				/* milli-ohm */
	int volt_uv;						/* micro-volt */
	int curr_ua;						/* micro-ampere */
	int capacity;						/* percent */
	int status;
	bool supplied;
};

extern const enum power_supply_property adc_battery_helper_properties[];
/* Must be const cannot be an external. Asserted in adc-battery-helper.c */
#define ADC_HELPER_NUM_PROPERTIES 7

int adc_battery_helper_init(struct adc_battery_helper *help, struct power_supply *psy,
			    adc_battery_helper_get_func get_voltage_and_current_now,
			    struct gpio_desc *charge_finished_gpio);
/*
 * The below functions can be directly used as power-supply / suspend-resume
 * callbacks. They cast the power_supply_get_drvdata() / dev_get_drvdata() data
 * directly to struct adc_battery_helper. Therefor struct adc_battery_helper
 * MUST be the first member of the battery driver's data struct.
 */
int adc_battery_helper_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val);
void adc_battery_helper_external_power_changed(struct power_supply *psy);
int adc_battery_helper_suspend(struct device *dev);
int adc_battery_helper_resume(struct device *dev);
