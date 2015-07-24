/*
 * A power allocator to manage temperature
 *
 * Copyright (C) 2014 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "Power allocator: " fmt

#include <linux/rculist.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#define CREATE_TRACE_POINTS
#include <trace/events/thermal_power_allocator.h>

#include "thermal_core.h"

#define FRAC_BITS 10
#define int_to_frac(x) ((x) << FRAC_BITS)
#define frac_to_int(x) ((x) >> FRAC_BITS)

/**
 * mul_frac() - multiply two fixed-point numbers
 * @x:	first multiplicand
 * @y:	second multiplicand
 *
 * Return: the result of multiplying two fixed-point numbers.  The
 * result is also a fixed-point number.
 */
static inline s64 mul_frac(s64 x, s64 y)
{
	return (x * y) >> FRAC_BITS;
}

/**
 * div_frac() - divide two fixed-point numbers
 * @x:	the dividend
 * @y:	the divisor
 *
 * Return: the result of dividing two fixed-point numbers.  The
 * result is also a fixed-point number.
 */
static inline s64 div_frac(s64 x, s64 y)
{
	return div_s64(x << FRAC_BITS, y);
}

/**
 * struct power_allocator_params - parameters for the power allocator governor
 * @err_integral:	accumulated error in the PID controller.
 * @prev_err:	error in the previous iteration of the PID controller.
 *		Used to calculate the derivative term.
 * @trip_switch_on:	first passive trip point of the thermal zone.  The
 *			governor switches on when this trip point is crossed.
 * @trip_max_desired_temperature:	last passive trip point of the thermal
 *					zone.  The temperature we are
 *					controlling for.
 */
struct power_allocator_params {
	s64 err_integral;
	s32 prev_err;
	int trip_switch_on;
	int trip_max_desired_temperature;
};

/**
 * pid_controller() - PID controller
 * @tz:	thermal zone we are operating in
 * @current_temp:	the current temperature in millicelsius
 * @control_temp:	the target temperature in millicelsius
 * @max_allocatable_power:	maximum allocatable power for this thermal zone
 *
 * This PID controller increases the available power budget so that the
 * temperature of the thermal zone gets as close as possible to
 * @control_temp and limits the power if it exceeds it.  k_po is the
 * proportional term when we are overshooting, k_pu is the
 * proportional term when we are undershooting.  integral_cutoff is a
 * threshold below which we stop accumulating the error.  The
 * accumulated error is only valid if the requested power will make
 * the system warmer.  If the system is mostly idle, there's no point
 * in accumulating positive error.
 *
 * Return: The power budget for the next period.
 */
static u32 pid_controller(struct thermal_zone_device *tz,
			  int current_temp,
			  int control_temp,
			  u32 max_allocatable_power)
{
	s64 p, i, d, power_range;
	s32 err, max_power_frac;
	struct power_allocator_params *params = tz->governor_data;

	max_power_frac = int_to_frac(max_allocatable_power);

	err = control_temp - current_temp;
	err = int_to_frac(err);

	/* Calculate the proportional term */
	p = mul_frac(err < 0 ? tz->tzp->k_po : tz->tzp->k_pu, err);

	/*
	 * Calculate the integral term
	 *
	 * if the error is less than cut off allow integration (but
	 * the integral is limited to max power)
	 */
	i = mul_frac(tz->tzp->k_i, params->err_integral);

	if (err < int_to_frac(tz->tzp->integral_cutoff)) {
		s64 i_next = i + mul_frac(tz->tzp->k_i, err);

		if (abs64(i_next) < max_power_frac) {
			i = i_next;
			params->err_integral += err;
		}
	}

	/*
	 * Calculate the derivative term
	 *
	 * We do err - prev_err, so with a positive k_d, a decreasing
	 * error (i.e. driving closer to the line) results in less
	 * power being applied, slowing down the controller)
	 */
	d = mul_frac(tz->tzp->k_d, err - params->prev_err);
	d = div_frac(d, tz->passive_delay);
	params->prev_err = err;

	power_range = p + i + d;

	/* feed-forward the known sustainable dissipatable power */
	power_range = tz->tzp->sustainable_power + frac_to_int(power_range);

	power_range = clamp(power_range, (s64)0, (s64)max_allocatable_power);

	trace_thermal_power_allocator_pid(tz, frac_to_int(err),
					  frac_to_int(params->err_integral),
					  frac_to_int(p), frac_to_int(i),
					  frac_to_int(d), power_range);

	return power_range;
}

/**
 * divvy_up_power() - divvy the allocated power between the actors
 * @req_power:	each actor's requested power
 * @max_power:	each actor's maximum available power
 * @num_actors:	size of the @req_power, @max_power and @granted_power's array
 * @total_req_power: sum of @req_power
 * @power_range:	total allocated power
 * @granted_power:	output array: each actor's granted power
 * @extra_actor_power:	an appropriately sized array to be used in the
 *			function as temporary storage of the extra power given
 *			to the actors
 *
 * This function divides the total allocated power (@power_range)
 * fairly between the actors.  It first tries to give each actor a
 * share of the @power_range according to how much power it requested
 * compared to the rest of the actors.  For example, if only one actor
 * requests power, then it receives all the @power_range.  If
 * three actors each requests 1mW, each receives a third of the
 * @power_range.
 *
 * If any actor received more than their maximum power, then that
 * surplus is re-divvied among the actors based on how far they are
 * from their respective maximums.
 *
 * Granted power for each actor is written to @granted_power, which
 * should've been allocated by the calling function.
 */
static void divvy_up_power(u32 *req_power, u32 *max_power, int num_actors,
			   u32 total_req_power, u32 power_range,
			   u32 *granted_power, u32 *extra_actor_power)
{
	u32 extra_power, capped_extra_power;
	int i;

	/*
	 * Prevent division by 0 if none of the actors request power.
	 */
	if (!total_req_power)
		total_req_power = 1;

	capped_extra_power = 0;
	extra_power = 0;
	for (i = 0; i < num_actors; i++) {
		u64 req_range = req_power[i] * power_range;

		granted_power[i] = DIV_ROUND_CLOSEST_ULL(req_range,
							 total_req_power);

		if (granted_power[i] > max_power[i]) {
			extra_power += granted_power[i] - max_power[i];
			granted_power[i] = max_power[i];
		}

		extra_actor_power[i] = max_power[i] - granted_power[i];
		capped_extra_power += extra_actor_power[i];
	}

	if (!extra_power)
		return;

	/*
	 * Re-divvy the reclaimed extra among actors based on
	 * how far they are from the max
	 */
	extra_power = min(extra_power, capped_extra_power);
	if (capped_extra_power > 0)
		for (i = 0; i < num_actors; i++)
			granted_power[i] += (extra_actor_power[i] *
					extra_power) / capped_extra_power;
}

static int allocate_power(struct thermal_zone_device *tz,
			  int current_temp,
			  int control_temp)
{
	struct thermal_instance *instance;
	struct power_allocator_params *params = tz->governor_data;
	u32 *req_power, *max_power, *granted_power, *extra_actor_power;
	u32 total_req_power, max_allocatable_power;
	u32 total_granted_power, power_range;
	int i, num_actors, total_weight, ret = 0;
	int trip_max_desired_temperature = params->trip_max_desired_temperature;

	mutex_lock(&tz->lock);

	num_actors = 0;
	total_weight = 0;
	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if ((instance->trip == trip_max_desired_temperature) &&
		    cdev_is_power_actor(instance->cdev)) {
			num_actors++;
			total_weight += instance->weight;
		}
	}

	/*
	 * We need to allocate three arrays of the same size:
	 * req_power, max_power and granted_power.  They are going to
	 * be needed until this function returns.  Allocate them all
	 * in one go to simplify the allocation and deallocation
	 * logic.
	 */
	BUILD_BUG_ON(sizeof(*req_power) != sizeof(*max_power));
	BUILD_BUG_ON(sizeof(*req_power) != sizeof(*granted_power));
	BUILD_BUG_ON(sizeof(*req_power) != sizeof(*extra_actor_power));
	req_power = devm_kcalloc(&tz->device, num_actors * 4,
				 sizeof(*req_power), GFP_KERNEL);
	if (!req_power) {
		ret = -ENOMEM;
		goto unlock;
	}

	max_power = &req_power[num_actors];
	granted_power = &req_power[2 * num_actors];
	extra_actor_power = &req_power[3 * num_actors];

	i = 0;
	total_req_power = 0;
	max_allocatable_power = 0;

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		int weight;
		struct thermal_cooling_device *cdev = instance->cdev;

		if (instance->trip != trip_max_desired_temperature)
			continue;

		if (!cdev_is_power_actor(cdev))
			continue;

		if (cdev->ops->get_requested_power(cdev, tz, &req_power[i]))
			continue;

		if (!total_weight)
			weight = 1 << FRAC_BITS;
		else
			weight = instance->weight;

		req_power[i] = frac_to_int(weight * req_power[i]);

		if (power_actor_get_max_power(cdev, tz, &max_power[i]))
			continue;

		total_req_power += req_power[i];
		max_allocatable_power += max_power[i];

		i++;
	}

	power_range = pid_controller(tz, current_temp, control_temp,
				     max_allocatable_power);

	divvy_up_power(req_power, max_power, num_actors, total_req_power,
		       power_range, granted_power, extra_actor_power);

	total_granted_power = 0;
	i = 0;
	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if (instance->trip != trip_max_desired_temperature)
			continue;

		if (!cdev_is_power_actor(instance->cdev))
			continue;

		power_actor_set_power(instance->cdev, instance,
				      granted_power[i]);
		total_granted_power += granted_power[i];

		i++;
	}

	trace_thermal_power_allocator(tz, req_power, total_req_power,
				      granted_power, total_granted_power,
				      num_actors, power_range,
				      max_allocatable_power, current_temp,
				      control_temp - current_temp);

	devm_kfree(&tz->device, req_power);
unlock:
	mutex_unlock(&tz->lock);

	return ret;
}

static int get_governor_trips(struct thermal_zone_device *tz,
			      struct power_allocator_params *params)
{
	int i, ret, last_passive;
	bool found_first_passive;

	found_first_passive = false;
	last_passive = -1;
	ret = -EINVAL;

	for (i = 0; i < tz->trips; i++) {
		enum thermal_trip_type type;

		ret = tz->ops->get_trip_type(tz, i, &type);
		if (ret)
			return ret;

		if (!found_first_passive) {
			if (type == THERMAL_TRIP_PASSIVE) {
				params->trip_switch_on = i;
				found_first_passive = true;
			}
		} else if (type == THERMAL_TRIP_PASSIVE) {
			last_passive = i;
		} else {
			break;
		}
	}

	if (last_passive != -1) {
		params->trip_max_desired_temperature = last_passive;
		ret = 0;
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static void reset_pid_controller(struct power_allocator_params *params)
{
	params->err_integral = 0;
	params->prev_err = 0;
}

static void allow_maximum_power(struct thermal_zone_device *tz)
{
	struct thermal_instance *instance;
	struct power_allocator_params *params = tz->governor_data;

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if ((instance->trip != params->trip_max_desired_temperature) ||
		    (!cdev_is_power_actor(instance->cdev)))
			continue;

		instance->target = 0;
		instance->cdev->updated = false;
		thermal_cdev_update(instance->cdev);
	}
}

/**
 * power_allocator_bind() - bind the power_allocator governor to a thermal zone
 * @tz:	thermal zone to bind it to
 *
 * Check that the thermal zone is valid for this governor, that is, it
 * has two thermal trips.  If so, initialize the PID controller
 * parameters and bind it to the thermal zone.
 *
 * Return: 0 on success, -EINVAL if the trips were invalid or -ENOMEM
 * if we ran out of memory.
 */
static int power_allocator_bind(struct thermal_zone_device *tz)
{
	int ret;
	struct power_allocator_params *params;
	int switch_on_temp, control_temp;
	u32 temperature_threshold;

	if (!tz->tzp || !tz->tzp->sustainable_power) {
		dev_err(&tz->device,
			"power_allocator: missing sustainable_power\n");
		return -EINVAL;
	}

	params = devm_kzalloc(&tz->device, sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	ret = get_governor_trips(tz, params);
	if (ret) {
		dev_err(&tz->device,
			"thermal zone %s has wrong trip setup for power allocator\n",
			tz->type);
		goto free;
	}

	ret = tz->ops->get_trip_temp(tz, params->trip_switch_on,
				     &switch_on_temp);
	if (ret)
		goto free;

	ret = tz->ops->get_trip_temp(tz, params->trip_max_desired_temperature,
				     &control_temp);
	if (ret)
		goto free;

	temperature_threshold = control_temp - switch_on_temp;

	tz->tzp->k_po = tz->tzp->k_po ?:
		int_to_frac(tz->tzp->sustainable_power) / temperature_threshold;
	tz->tzp->k_pu = tz->tzp->k_pu ?:
		int_to_frac(2 * tz->tzp->sustainable_power) /
		temperature_threshold;
	tz->tzp->k_i = tz->tzp->k_i ?: int_to_frac(10) / 1000;
	/*
	 * The default for k_d and integral_cutoff is 0, so we can
	 * leave them as they are.
	 */

	reset_pid_controller(params);

	tz->governor_data = params;

	return 0;

free:
	devm_kfree(&tz->device, params);
	return ret;
}

static void power_allocator_unbind(struct thermal_zone_device *tz)
{
	dev_dbg(&tz->device, "Unbinding from thermal zone %d\n", tz->id);
	devm_kfree(&tz->device, tz->governor_data);
	tz->governor_data = NULL;
}

static int power_allocator_throttle(struct thermal_zone_device *tz, int trip)
{
	int ret;
	int switch_on_temp, control_temp, current_temp;
	struct power_allocator_params *params = tz->governor_data;

	/*
	 * We get called for every trip point but we only need to do
	 * our calculations once
	 */
	if (trip != params->trip_max_desired_temperature)
		return 0;

	ret = thermal_zone_get_temp(tz, &current_temp);
	if (ret) {
		dev_warn(&tz->device, "Failed to get temperature: %d\n", ret);
		return ret;
	}

	ret = tz->ops->get_trip_temp(tz, params->trip_switch_on,
				     &switch_on_temp);
	if (ret) {
		dev_warn(&tz->device,
			 "Failed to get switch on temperature: %d\n", ret);
		return ret;
	}

	if (current_temp < switch_on_temp) {
		tz->passive = 0;
		reset_pid_controller(params);
		allow_maximum_power(tz);
		return 0;
	}

	tz->passive = 1;

	ret = tz->ops->get_trip_temp(tz, params->trip_max_desired_temperature,
				&control_temp);
	if (ret) {
		dev_warn(&tz->device,
			 "Failed to get the maximum desired temperature: %d\n",
			 ret);
		return ret;
	}

	return allocate_power(tz, current_temp, control_temp);
}

static struct thermal_governor thermal_gov_power_allocator = {
	.name		= "power_allocator",
	.bind_to_tz	= power_allocator_bind,
	.unbind_from_tz	= power_allocator_unbind,
	.throttle	= power_allocator_throttle,
};

int thermal_gov_power_allocator_register(void)
{
	return thermal_register_governor(&thermal_gov_power_allocator);
}

void thermal_gov_power_allocator_unregister(void)
{
	thermal_unregister_governor(&thermal_gov_power_allocator);
}
