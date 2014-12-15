/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */




#ifndef _KBASE_POWER_ACTOR_H_
#define _KBASE_POWER_ACTOR_H_

#include <mali_kbase.h>

#include <linux/pm_opp.h>

/** struct mali_pa_model_ops - Function pointer for power model
 *
 * @get_static_power: Pointer to a function that returns the estimated static
 *                    power usage in mW, based on the input voltage in mV and
 *                    temperature in millidegrees Celsius.
 * @get_dynamic_power: Pointer to a function that returns the estimated dynamic power
 *                     usage in mW, based on the input voltage in mV and
 *                     frequency in Hz.
 */
struct mali_pa_model_ops {
	unsigned long (*get_static_power)(unsigned long voltage,
			unsigned long temperature);
	unsigned long (*get_dynamic_power)(unsigned long freq,
			unsigned long voltage);
};

struct mali_pa_power_table {
	unsigned long freq;
	unsigned long power;
};

struct mali_power_actor {
	struct kbase_device *kbdev;
	struct mali_pa_model_ops *ops;
	struct mali_pa_power_table *dyn_table;
	int dyn_table_count;
};

int mali_pa_init(struct kbase_device *kbdev);
void mali_pa_term(struct kbase_device *kbdev);


#endif
