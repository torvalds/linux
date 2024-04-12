// SPDX-License-Identifier: GPL-2.0
/*
 * A power allocator to manage temperature
 *
 * Copyright (C) 2014 ARM Ltd.
 *
 */

#define pr_fmt(fmt) "Power allocator: " fmt

#include <linux/slab.h>
#include <linux/thermal.h>

#define CREATE_TRACE_POINTS
#include "thermal_trace_ipa.h"

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
 * struct power_actor - internal power information for power actor
 * @req_power:		requested power value (not weighted)
 * @max_power:		max allocatable power for this actor
 * @granted_power:	granted power for this actor
 * @extra_actor_power:	extra power that this actor can receive
 * @weighted_req_power:	weighted requested power as input to IPA
 */
struct power_actor {
	u32 req_power;
	u32 max_power;
	u32 granted_power;
	u32 extra_actor_power;
	u32 weighted_req_power;
};

/**
 * struct power_allocator_params - parameters for the power allocator governor
 * @allocated_tzp:	whether we have allocated tzp for this thermal zone and
 *			it needs to be freed on unbind
 * @err_integral:	accumulated error in the PID controller.
 * @prev_err:	error in the previous iteration of the PID controller.
 *		Used to calculate the derivative term.
 * @sustainable_power:	Sustainable power (heat) that this thermal zone can
 *			dissipate
 * @trip_switch_on:	first passive trip point of the thermal zone.  The
 *			governor switches on when this trip point is crossed.
 *			If the thermal zone only has one passive trip point,
 *			@trip_switch_on should be NULL.
 * @trip_max:		last passive trip point of the thermal zone. The
 *			temperature we are controlling for.
 * @total_weight:	Sum of all thermal instances weights
 * @num_actors:		number of cooling devices supporting IPA callbacks
 * @buffer_size:	internal buffer size, to avoid runtime re-calculation
 * @power:		buffer for all power actors internal power information
 */
struct power_allocator_params {
	bool allocated_tzp;
	s64 err_integral;
	s32 prev_err;
	u32 sustainable_power;
	const struct thermal_trip *trip_switch_on;
	const struct thermal_trip *trip_max;
	int total_weight;
	unsigned int num_actors;
	unsigned int buffer_size;
	struct power_actor *power;
};

static bool power_actor_is_valid(struct power_allocator_params *params,
				 struct thermal_instance *instance)
{
	return (instance->trip == params->trip_max &&
		 cdev_is_power_actor(instance->cdev));
}

/**
 * estimate_sustainable_power() - Estimate the sustainable power of a thermal zone
 * @tz: thermal zone we are operating in
 *
 * For thermal zones that don't provide a sustainable_power in their
 * thermal_zone_params, estimate one.  Calculate it using the minimum
 * power of all the cooling devices as that gives a valid value that
 * can give some degree of functionality.  For optimal performance of
 * this governor, provide a sustainable_power in the thermal zone's
 * thermal_zone_params.
 */
static u32 estimate_sustainable_power(struct thermal_zone_device *tz)
{
	struct power_allocator_params *params = tz->governor_data;
	struct thermal_cooling_device *cdev;
	struct thermal_instance *instance;
	u32 sustainable_power = 0;
	u32 min_power;

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if (!power_actor_is_valid(params, instance))
			continue;

		cdev = instance->cdev;
		if (cdev->ops->state2power(cdev, instance->upper, &min_power))
			continue;

		sustainable_power += min_power;
	}

	return sustainable_power;
}

/**
 * estimate_pid_constants() - Estimate the constants for the PID controller
 * @tz:		thermal zone for which to estimate the constants
 * @sustainable_power:	sustainable power for the thermal zone
 * @trip_switch_on:	trip point for the switch on temperature
 * @control_temp:	target temperature for the power allocator governor
 *
 * This function is used to update the estimation of the PID
 * controller constants in struct thermal_zone_parameters.
 */
static void estimate_pid_constants(struct thermal_zone_device *tz,
				   u32 sustainable_power,
				   const struct thermal_trip *trip_switch_on,
				   int control_temp)
{
	u32 temperature_threshold = control_temp;
	s32 k_i;

	if (trip_switch_on)
		temperature_threshold -= trip_switch_on->temperature;

	/*
	 * estimate_pid_constants() tries to find appropriate default
	 * values for thermal zones that don't provide them. If a
	 * system integrator has configured a thermal zone with two
	 * passive trip points at the same temperature, that person
	 * hasn't put any effort to set up the thermal zone properly
	 * so just give up.
	 */
	if (!temperature_threshold)
		return;

	tz->tzp->k_po = int_to_frac(sustainable_power) /
		temperature_threshold;

	tz->tzp->k_pu = int_to_frac(2 * sustainable_power) /
		temperature_threshold;

	k_i = tz->tzp->k_pu / 10;
	tz->tzp->k_i = k_i > 0 ? k_i : 1;

	/*
	 * The default for k_d and integral_cutoff is 0, so we can
	 * leave them as they are.
	 */
}

/**
 * get_sustainable_power() - Get the right sustainable power
 * @tz:		thermal zone for which to estimate the constants
 * @params:	parameters for the power allocator governor
 * @control_temp:	target temperature for the power allocator governor
 *
 * This function is used for getting the proper sustainable power value based
 * on variables which might be updated by the user sysfs interface. If that
 * happen the new value is going to be estimated and updated. It is also used
 * after thermal zone binding, where the initial values where set to 0.
 */
static u32 get_sustainable_power(struct thermal_zone_device *tz,
				 struct power_allocator_params *params,
				 int control_temp)
{
	u32 sustainable_power;

	if (!tz->tzp->sustainable_power)
		sustainable_power = estimate_sustainable_power(tz);
	else
		sustainable_power = tz->tzp->sustainable_power;

	/* Check if it's init value 0 or there was update via sysfs */
	if (sustainable_power != params->sustainable_power) {
		estimate_pid_constants(tz, sustainable_power,
				       params->trip_switch_on, control_temp);

		/* Do the estimation only once and make available in sysfs */
		tz->tzp->sustainable_power = sustainable_power;
		params->sustainable_power = sustainable_power;
	}

	return sustainable_power;
}

/**
 * pid_controller() - PID controller
 * @tz:	thermal zone we are operating in
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
			  int control_temp,
			  u32 max_allocatable_power)
{
	struct power_allocator_params *params = tz->governor_data;
	s64 p, i, d, power_range;
	s32 err, max_power_frac;
	u32 sustainable_power;

	max_power_frac = int_to_frac(max_allocatable_power);

	sustainable_power = get_sustainable_power(tz, params, control_temp);

	err = control_temp - tz->temperature;
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

		if (abs(i_next) < max_power_frac) {
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
	d = div_frac(d, jiffies_to_msecs(tz->passive_delay_jiffies));
	params->prev_err = err;

	power_range = p + i + d;

	/* feed-forward the known sustainable dissipatable power */
	power_range = sustainable_power + frac_to_int(power_range);

	power_range = clamp(power_range, (s64)0, (s64)max_allocatable_power);

	trace_thermal_power_allocator_pid(tz, frac_to_int(err),
					  frac_to_int(params->err_integral),
					  frac_to_int(p), frac_to_int(i),
					  frac_to_int(d), power_range);

	return power_range;
}

/**
 * power_actor_set_power() - limit the maximum power a cooling device consumes
 * @cdev:	pointer to &thermal_cooling_device
 * @instance:	thermal instance to update
 * @power:	the power in milliwatts
 *
 * Set the cooling device to consume at most @power milliwatts. The limit is
 * expected to be a cap at the maximum power consumption.
 *
 * Return: 0 on success, -EINVAL if the cooling device does not
 * implement the power actor API or -E* for other failures.
 */
static int
power_actor_set_power(struct thermal_cooling_device *cdev,
		      struct thermal_instance *instance, u32 power)
{
	unsigned long state;
	int ret;

	ret = cdev->ops->power2state(cdev, power, &state);
	if (ret)
		return ret;

	instance->target = clamp_val(state, instance->lower, instance->upper);
	mutex_lock(&cdev->lock);
	__thermal_cdev_update(cdev);
	mutex_unlock(&cdev->lock);

	return 0;
}

/**
 * divvy_up_power() - divvy the allocated power between the actors
 * @power:		buffer for all power actors internal power information
 * @num_actors:		number of power actors in this thermal zone
 * @total_req_power:	sum of all weighted requested power for all actors
 * @power_range:	total allocated power
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
 */
static void divvy_up_power(struct power_actor *power, int num_actors,
			   u32 total_req_power, u32 power_range)
{
	u32 capped_extra_power = 0;
	u32 extra_power = 0;
	int i;

	/*
	 * Prevent division by 0 if none of the actors request power.
	 */
	if (!total_req_power)
		total_req_power = 1;

	for (i = 0; i < num_actors; i++) {
		struct power_actor *pa = &power[i];
		u64 req_range = (u64)pa->req_power * power_range;

		pa->granted_power = DIV_ROUND_CLOSEST_ULL(req_range,
							  total_req_power);

		if (pa->granted_power > pa->max_power) {
			extra_power += pa->granted_power - pa->max_power;
			pa->granted_power = pa->max_power;
		}

		pa->extra_actor_power = pa->max_power - pa->granted_power;
		capped_extra_power += pa->extra_actor_power;
	}

	if (!extra_power || !capped_extra_power)
		return;

	/*
	 * Re-divvy the reclaimed extra among actors based on
	 * how far they are from the max
	 */
	extra_power = min(extra_power, capped_extra_power);

	for (i = 0; i < num_actors; i++) {
		struct power_actor *pa = &power[i];
		u64 extra_range = pa->extra_actor_power;

		extra_range *= extra_power;
		pa->granted_power += DIV_ROUND_CLOSEST_ULL(extra_range,
						capped_extra_power);
	}
}

static int allocate_power(struct thermal_zone_device *tz, int control_temp)
{
	struct power_allocator_params *params = tz->governor_data;
	unsigned int num_actors = params->num_actors;
	struct power_actor *power = params->power;
	struct thermal_cooling_device *cdev;
	struct thermal_instance *instance;
	u32 total_weighted_req_power = 0;
	u32 max_allocatable_power = 0;
	u32 total_granted_power = 0;
	u32 total_req_power = 0;
	u32 power_range, weight;
	int i = 0, ret;

	if (!num_actors)
		return -ENODEV;

	/* Clean all buffers for new power estimations */
	memset(power, 0, params->buffer_size);

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		struct power_actor *pa = &power[i];

		if (!power_actor_is_valid(params, instance))
			continue;

		cdev = instance->cdev;

		ret = cdev->ops->get_requested_power(cdev, &pa->req_power);
		if (ret)
			continue;

		if (!params->total_weight)
			weight = 1 << FRAC_BITS;
		else
			weight = instance->weight;

		pa->weighted_req_power = frac_to_int(weight * pa->req_power);

		ret = cdev->ops->state2power(cdev, instance->lower,
					     &pa->max_power);
		if (ret)
			continue;

		total_req_power += pa->req_power;
		max_allocatable_power += pa->max_power;
		total_weighted_req_power += pa->weighted_req_power;

		i++;
	}

	power_range = pid_controller(tz, control_temp, max_allocatable_power);

	divvy_up_power(power, num_actors, total_weighted_req_power,
		       power_range);

	i = 0;
	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		struct power_actor *pa = &power[i];

		if (!power_actor_is_valid(params, instance))
			continue;

		power_actor_set_power(instance->cdev, instance,
				      pa->granted_power);
		total_granted_power += pa->granted_power;

		trace_thermal_power_actor(tz, i, pa->req_power,
					  pa->granted_power);
		i++;
	}

	trace_thermal_power_allocator(tz, total_req_power, total_granted_power,
				      num_actors, power_range,
				      max_allocatable_power, tz->temperature,
				      control_temp - tz->temperature);

	return 0;
}

/**
 * get_governor_trips() - get the two trip points that are key for this governor
 * @tz:	thermal zone to operate on
 * @params:	pointer to private data for this governor
 *
 * The power allocator governor works optimally with two trips points:
 * a "switch on" trip point and a "maximum desired temperature".  These
 * are defined as the first and last passive trip points.
 *
 * If there is only one trip point, then that's considered to be the
 * "maximum desired temperature" trip point and the governor is always
 * on.  If there are no passive or active trip points, then the
 * governor won't do anything.  In fact, its throttle function
 * won't be called at all.
 */
static void get_governor_trips(struct thermal_zone_device *tz,
			       struct power_allocator_params *params)
{
	const struct thermal_trip *first_passive = NULL;
	const struct thermal_trip *last_passive = NULL;
	const struct thermal_trip *last_active = NULL;
	const struct thermal_trip *trip;

	for_each_trip(tz, trip) {
		switch (trip->type) {
		case THERMAL_TRIP_PASSIVE:
			if (!first_passive) {
				first_passive = trip;
				break;
			}
			last_passive = trip;
			break;
		case THERMAL_TRIP_ACTIVE:
			last_active = trip;
			break;
		default:
			break;
		}
	}

	if (last_passive) {
		params->trip_switch_on = first_passive;
		params->trip_max = last_passive;
	} else if (first_passive) {
		params->trip_switch_on = NULL;
		params->trip_max = first_passive;
	} else {
		params->trip_switch_on = NULL;
		params->trip_max = last_active;
	}
}

static void reset_pid_controller(struct power_allocator_params *params)
{
	params->err_integral = 0;
	params->prev_err = 0;
}

static void allow_maximum_power(struct thermal_zone_device *tz, bool update)
{
	struct power_allocator_params *params = tz->governor_data;
	struct thermal_cooling_device *cdev;
	struct thermal_instance *instance;
	u32 req_power;

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if (!power_actor_is_valid(params, instance))
			continue;

		cdev = instance->cdev;

		instance->target = 0;
		mutex_lock(&cdev->lock);
		/*
		 * Call for updating the cooling devices local stats and avoid
		 * periods of dozen of seconds when those have not been
		 * maintained.
		 */
		cdev->ops->get_requested_power(cdev, &req_power);

		if (update)
			__thermal_cdev_update(cdev);

		mutex_unlock(&cdev->lock);
	}
}

/**
 * check_power_actors() - Check all cooling devices and warn when they are
 *			not power actors
 * @tz:		thermal zone to operate on
 * @params:	power allocator private data
 *
 * Check all cooling devices in the @tz and warn every time they are missing
 * power actor API. The warning should help to investigate the issue, which
 * could be e.g. lack of Energy Model for a given device.
 *
 * If all of the cooling devices currently attached to @tz implement the power
 * actor API, return the number of them (which may be 0, because some cooling
 * devices may be attached later). Otherwise, return -EINVAL.
 */
static int check_power_actors(struct thermal_zone_device *tz,
			      struct power_allocator_params *params)
{
	struct thermal_instance *instance;
	int ret = 0;

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if (instance->trip != params->trip_max)
			continue;

		if (!cdev_is_power_actor(instance->cdev)) {
			dev_warn(&tz->device, "power_allocator: %s is not a power actor\n",
				 instance->cdev->type);
			return -EINVAL;
		}
		ret++;
	}

	return ret;
}

static int allocate_actors_buffer(struct power_allocator_params *params,
				  int num_actors)
{
	int ret;

	kfree(params->power);

	/* There might be no cooling devices yet. */
	if (!num_actors) {
		ret = -EINVAL;
		goto clean_state;
	}

	params->power = kcalloc(num_actors, sizeof(struct power_actor),
				GFP_KERNEL);
	if (!params->power) {
		ret = -ENOMEM;
		goto clean_state;
	}

	params->num_actors = num_actors;
	params->buffer_size = num_actors * sizeof(struct power_actor);

	return 0;

clean_state:
	params->num_actors = 0;
	params->buffer_size = 0;
	params->power = NULL;
	return ret;
}

static void power_allocator_update_tz(struct thermal_zone_device *tz,
				      enum thermal_notify_event reason)
{
	struct power_allocator_params *params = tz->governor_data;
	struct thermal_instance *instance;
	int num_actors = 0;

	switch (reason) {
	case THERMAL_TZ_BIND_CDEV:
	case THERMAL_TZ_UNBIND_CDEV:
		list_for_each_entry(instance, &tz->thermal_instances, tz_node)
			if (power_actor_is_valid(params, instance))
				num_actors++;

		if (num_actors == params->num_actors)
			return;

		allocate_actors_buffer(params, num_actors);
		break;
	case THERMAL_INSTANCE_WEIGHT_CHANGED:
		params->total_weight = 0;
		list_for_each_entry(instance, &tz->thermal_instances, tz_node)
			if (power_actor_is_valid(params, instance))
				params->total_weight += instance->weight;
		break;
	default:
		break;
	}
}

/**
 * power_allocator_bind() - bind the power_allocator governor to a thermal zone
 * @tz:	thermal zone to bind it to
 *
 * Initialize the PID controller parameters and bind it to the thermal
 * zone.
 *
 * Return: 0 on success, or -ENOMEM if we ran out of memory, or -EINVAL
 * when there are unsupported cooling devices in the @tz.
 */
static int power_allocator_bind(struct thermal_zone_device *tz)
{
	struct power_allocator_params *params;
	int ret;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	get_governor_trips(tz, params);
	if (!params->trip_max) {
		dev_warn(&tz->device, "power_allocator: missing trip_max\n");
		kfree(params);
		return -EINVAL;
	}

	ret = check_power_actors(tz, params);
	if (ret < 0) {
		dev_warn(&tz->device, "power_allocator: binding failed\n");
		kfree(params);
		return ret;
	}

	ret = allocate_actors_buffer(params, ret);
	if (ret) {
		dev_warn(&tz->device, "power_allocator: allocation failed\n");
		kfree(params);
		return ret;
	}

	if (!tz->tzp) {
		tz->tzp = kzalloc(sizeof(*tz->tzp), GFP_KERNEL);
		if (!tz->tzp) {
			ret = -ENOMEM;
			goto free_params;
		}

		params->allocated_tzp = true;
	}

	if (!tz->tzp->sustainable_power)
		dev_warn(&tz->device, "power_allocator: sustainable_power will be estimated\n");
	else
		params->sustainable_power = tz->tzp->sustainable_power;

	estimate_pid_constants(tz, tz->tzp->sustainable_power,
			       params->trip_switch_on,
			       params->trip_max->temperature);

	reset_pid_controller(params);

	tz->governor_data = params;

	return 0;

free_params:
	kfree(params->power);
	kfree(params);

	return ret;
}

static void power_allocator_unbind(struct thermal_zone_device *tz)
{
	struct power_allocator_params *params = tz->governor_data;

	dev_dbg(&tz->device, "Unbinding from thermal zone %d\n", tz->id);

	if (params->allocated_tzp) {
		kfree(tz->tzp);
		tz->tzp = NULL;
	}

	kfree(params->power);
	kfree(tz->governor_data);
	tz->governor_data = NULL;
}

static int power_allocator_throttle(struct thermal_zone_device *tz,
				    const struct thermal_trip *trip)
{
	struct power_allocator_params *params = tz->governor_data;
	bool update;

	lockdep_assert_held(&tz->lock);

	/*
	 * We get called for every trip point but we only need to do
	 * our calculations once
	 */
	if (trip != params->trip_max)
		return 0;

	trip = params->trip_switch_on;
	if (trip && tz->temperature < trip->temperature) {
		update = tz->passive;
		tz->passive = 0;
		reset_pid_controller(params);
		allow_maximum_power(tz, update);
		return 0;
	}

	tz->passive = 1;

	return allocate_power(tz, params->trip_max->temperature);
}

static struct thermal_governor thermal_gov_power_allocator = {
	.name		= "power_allocator",
	.bind_to_tz	= power_allocator_bind,
	.unbind_from_tz	= power_allocator_unbind,
	.throttle	= power_allocator_throttle,
	.update_tz	= power_allocator_update_tz,
};
THERMAL_GOVERNOR_DECLARE(thermal_gov_power_allocator);
