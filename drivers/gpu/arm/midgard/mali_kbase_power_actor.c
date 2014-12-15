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



#include <mali_kbase.h>
#include <mali_kbase_config_defaults.h>

#include <linux/devfreq_cooling.h>
#include <linux/power_actor.h>
#include <linux/thermal.h>

#include "mali_kbase_power_actor.h"


static u32 mali_pa_get_req_power(struct power_actor *actor, struct thermal_zone_device *zone)
{
	struct mali_power_actor *mali_actor = actor->data;
	struct kbase_device *kbdev = mali_actor->kbdev;
	unsigned long total_time, busy_time;
	unsigned long power, temperature;
	struct dev_pm_opp *opp;
	unsigned long voltage;
	unsigned long freq;

	kbase_pm_get_dvfs_utilisation(kbdev, &total_time, &busy_time);

	freq = clk_get_rate(kbdev->clock);

	rcu_read_lock();
	opp = dev_pm_opp_find_freq_floor(kbdev->dev, &freq);
	if (IS_ERR_OR_NULL(opp)) {
		rcu_read_unlock();
		return 0;
	}

	voltage = dev_pm_opp_get_voltage(opp) / 1000; /* mV */
	rcu_read_unlock();

	power = mali_actor->ops->get_dynamic_power(freq, voltage);
	power = (power * busy_time) / total_time;

	temperature = zone->temperature;

	/* Assume all cores are always powered */
	power += mali_actor->ops->get_static_power(voltage, temperature);

	dev_dbg(kbdev->dev, "get req power = %lu\n", power);

	return (u32)power;
}

static u32 mali_pa_get_max_power(struct power_actor *actor, struct thermal_zone_device *zone)
{
	struct mali_power_actor *mali_actor = actor->data;
	struct kbase_device *kbdev = mali_actor->kbdev;
	struct dev_pm_opp *opp;
	unsigned long voltage, temperature;
	unsigned long freq = ULONG_MAX;
	u32 power;

	rcu_read_lock();
	opp = dev_pm_opp_find_freq_floor(kbdev->dev, &freq);
	if (IS_ERR_OR_NULL(opp)) {
		rcu_read_unlock();
		dev_err(kbdev->dev, "Failed to get OPP for max freq\n");
		return 0;
	}
	voltage = dev_pm_opp_get_voltage(opp) / 1000; /* mV */
	rcu_read_unlock();

	temperature = zone->temperature;

	power = mali_actor->ops->get_static_power(voltage, temperature)
			+ mali_actor->ops->get_dynamic_power(freq, voltage);

	dev_dbg(kbdev->dev, "get max power = %u\n", power);

	return power;
}

static int mali_pa_set_power(struct power_actor *actor, struct thermal_zone_device *zone, u32 power)
{
	struct mali_power_actor *mali_actor = actor->data;
	struct kbase_device *kbdev = mali_actor->kbdev;
	struct thermal_cooling_device *cdev;
	unsigned long total_time, busy_time;
	unsigned long freq, state;
	unsigned long static_power, normalized_power;
	unsigned long voltage, temperature;
	struct dev_pm_opp *opp;
	int err, i;

	dev_dbg(kbdev->dev, "Setting max power %u\n", power);

	kbase_pm_get_dvfs_utilisation(kbdev, &total_time, &busy_time);

	freq = clk_get_rate(kbdev->clock);

	rcu_read_lock();
	opp = dev_pm_opp_find_freq_exact(kbdev->dev, freq, true);
	if (IS_ERR_OR_NULL(opp)) {
		rcu_read_unlock();
		return -ENOENT;
	}
	voltage = dev_pm_opp_get_voltage(opp) / 1000; /* mV */
	rcu_read_unlock();

	temperature = zone->temperature;

	static_power = mali_actor->ops->get_static_power(voltage, temperature);

	if (power < static_power) {
		normalized_power = 0;
	} else {
		unsigned long dyn_power = power - static_power;

		if (!busy_time)
			normalized_power = dyn_power;
		else
			normalized_power = (dyn_power * total_time) / busy_time;
	}

	/* Find target frequency. Use the lowest OPP if allocated power is too
	 * low. */
	freq = mali_actor->dyn_table[0].freq;
	for (i = 1; i < mali_actor->dyn_table_count; i++) {
		if (mali_actor->dyn_table[i].power > normalized_power)
			break;
		else
			freq = mali_actor->dyn_table[i].freq;
	}

	state = devfreq_cooling_get_level(kbdev->devfreq, freq);
	if (state == THERMAL_CSTATE_INVALID) {
		dev_err(kbdev->dev,
			"Failed to lookup cooling level for freq %ld\n", freq);
		return -EINVAL;
	}

	cdev = kbdev->devfreq_cooling->cdev;
	err = cdev->ops->set_cur_state(cdev, state);

	dev_dbg(kbdev->dev,
		"Max power set to %u using frequency %ld (cooling level %ld) (%d)\n",
		power, freq, state, err);

	return err;
}

static struct power_actor_ops mali_pa_ops = {
	.get_req_power = mali_pa_get_req_power,
	.get_max_power = mali_pa_get_max_power,
	.set_power = mali_pa_set_power,
};

int mali_pa_init(struct kbase_device *kbdev)
{
	struct power_actor *actor;
	struct mali_power_actor *mali_actor;
	struct mali_pa_model_ops *callbacks;
	struct mali_pa_power_table *table;
	unsigned long freq;
	int i, num_opps;

	callbacks = (void *)kbasep_get_config_value(kbdev, kbdev->config_attributes,
				KBASE_CONFIG_ATTR_POWER_MODEL_CALLBACKS);
	if (!callbacks)
		return -ENODEV;

	mali_actor = kzalloc(sizeof(*mali_actor), GFP_KERNEL);
	if (!mali_actor)
		return -ENOMEM;

	mali_actor->ops = callbacks;
	mali_actor->kbdev = kbdev;

	rcu_read_lock();
	num_opps = dev_pm_opp_get_opp_count(kbdev->dev);
	rcu_read_unlock();

	table = kcalloc(num_opps, sizeof(table[0]), GFP_KERNEL);
	if (!table) {
		kfree(mali_actor);
		return -ENOMEM;
	}

	rcu_read_lock();
	for (i = 0, freq = 0; i < num_opps; i++, freq++) {
		unsigned long power_static, power_dyn, voltage;
		struct dev_pm_opp *opp;

		opp = dev_pm_opp_find_freq_ceil(kbdev->dev, &freq);
		if (IS_ERR(opp))
			break;

		voltage = dev_pm_opp_get_voltage(opp) / 1000; /* mV */

		table[i].freq = freq;

		power_dyn = callbacks->get_dynamic_power(freq, voltage);
		power_static = callbacks->get_static_power(voltage, 85000);

		dev_info(kbdev->dev, "Power table: %lu MHz @ %lu mV: %lu + %lu = %lu mW\n",
				freq / 1000000, voltage,
				power_dyn, power_static, power_dyn + power_static);

		table[i].power = power_dyn;
	}
	rcu_read_unlock();

	if (i != num_opps)
		dev_warn(kbdev->dev, "Power actor: Unable to enumerate all OPPs (%d != %d)\n",
				i, num_opps);

	mali_actor->dyn_table = table;
	mali_actor->dyn_table_count = i;

	/* Register power actor.
	 * Set default actor weight to 1 (8-bit fixed point). */
	actor = power_actor_register(1 * 256, &mali_pa_ops, mali_actor);
	if (IS_ERR_OR_NULL(actor)) {
		kfree(mali_actor->dyn_table);
		kfree(mali_actor);
		return PTR_ERR(actor);
	}

	kbdev->power_actor = actor;

	dev_info(kbdev->dev, "Initalized power actor\n");

	return 0;
}

void mali_pa_term(struct kbase_device *kbdev)
{
	struct mali_power_actor *mali_actor;

	if (kbdev->power_actor) {
		mali_actor = kbdev->power_actor->data;

		power_actor_unregister(kbdev->power_actor);
		kbdev->power_actor = NULL;

		kfree(mali_actor->dyn_table);
		kfree(mali_actor);
	}
}
