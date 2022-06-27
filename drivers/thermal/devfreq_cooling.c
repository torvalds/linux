// SPDX-License-Identifier: GPL-2.0
/*
 * devfreq_cooling: Thermal cooling device implementation for devices using
 *                  devfreq
 *
 * Copyright (C) 2014-2015 ARM Limited
 *
 * TODO:
 *    - If OPPs are added or removed after devfreq cooling has
 *      registered, the devfreq cooling won't react to it.
 */

#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>
#include <linux/energy_model.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/thermal.h>
#include <linux/units.h>

#include <trace/events/thermal.h>

#define SCALE_ERROR_MITIGATION	100

/**
 * struct devfreq_cooling_device - Devfreq cooling device
 *		devfreq_cooling_device registered.
 * @cdev:	Pointer to associated thermal cooling device.
 * @devfreq:	Pointer to associated devfreq device.
 * @cooling_state:	Current cooling state.
 * @freq_table:	Pointer to a table with the frequencies sorted in descending
 *		order.  You can index the table by cooling device state
 * @max_state:	It is the last index, that is, one less than the number of the
 *		OPPs
 * @power_ops:	Pointer to devfreq_cooling_power, a more precised model.
 * @res_util:	Resource utilization scaling factor for the power.
 *		It is multiplied by 100 to minimize the error. It is used
 *		for estimation of the power budget instead of using
 *		'utilization' (which is	'busy_time' / 'total_time').
 *		The 'res_util' range is from 100 to power * 100	for the
 *		corresponding 'state'.
 * @capped_state:	index to cooling state with in dynamic power budget
 * @req_max_freq:	PM QoS request for limiting the maximum frequency
 *			of the devfreq device.
 * @em_pd:		Energy Model for the associated Devfreq device
 */
struct devfreq_cooling_device {
	struct thermal_cooling_device *cdev;
	struct devfreq *devfreq;
	unsigned long cooling_state;
	u32 *freq_table;
	size_t max_state;
	struct devfreq_cooling_power *power_ops;
	u32 res_util;
	int capped_state;
	struct dev_pm_qos_request req_max_freq;
	struct em_perf_domain *em_pd;
};

static int devfreq_cooling_get_max_state(struct thermal_cooling_device *cdev,
					 unsigned long *state)
{
	struct devfreq_cooling_device *dfc = cdev->devdata;

	*state = dfc->max_state;

	return 0;
}

static int devfreq_cooling_get_cur_state(struct thermal_cooling_device *cdev,
					 unsigned long *state)
{
	struct devfreq_cooling_device *dfc = cdev->devdata;

	*state = dfc->cooling_state;

	return 0;
}

static int devfreq_cooling_set_cur_state(struct thermal_cooling_device *cdev,
					 unsigned long state)
{
	struct devfreq_cooling_device *dfc = cdev->devdata;
	struct devfreq *df = dfc->devfreq;
	struct device *dev = df->dev.parent;
	unsigned long freq;
	int perf_idx;

	if (state == dfc->cooling_state)
		return 0;

	dev_dbg(dev, "Setting cooling state %lu\n", state);

	if (state > dfc->max_state)
		return -EINVAL;

	if (dfc->em_pd) {
		perf_idx = dfc->max_state - state;
		freq = dfc->em_pd->table[perf_idx].frequency * 1000;
	} else {
		freq = dfc->freq_table[state];
	}

	dev_pm_qos_update_request(&dfc->req_max_freq,
				  DIV_ROUND_UP(freq, HZ_PER_KHZ));

	dfc->cooling_state = state;

	return 0;
}

/**
 * get_perf_idx() - get the performance index corresponding to a frequency
 * @em_pd:	Pointer to device's Energy Model
 * @freq:	frequency in kHz
 *
 * Return: the performance index associated with the @freq, or
 * -EINVAL if it wasn't found.
 */
static int get_perf_idx(struct em_perf_domain *em_pd, unsigned long freq)
{
	int i;

	for (i = 0; i < em_pd->nr_perf_states; i++) {
		if (em_pd->table[i].frequency == freq)
			return i;
	}

	return -EINVAL;
}

static unsigned long get_voltage(struct devfreq *df, unsigned long freq)
{
	struct device *dev = df->dev.parent;
	unsigned long voltage;
	struct dev_pm_opp *opp;

	opp = dev_pm_opp_find_freq_exact(dev, freq, true);
	if (PTR_ERR(opp) == -ERANGE)
		opp = dev_pm_opp_find_freq_exact(dev, freq, false);

	if (IS_ERR(opp)) {
		dev_err_ratelimited(dev, "Failed to find OPP for frequency %lu: %ld\n",
				    freq, PTR_ERR(opp));
		return 0;
	}

	voltage = dev_pm_opp_get_voltage(opp) / 1000; /* mV */
	dev_pm_opp_put(opp);

	if (voltage == 0) {
		dev_err_ratelimited(dev,
				    "Failed to get voltage for frequency %lu\n",
				    freq);
	}

	return voltage;
}

static void _normalize_load(struct devfreq_dev_status *status)
{
	if (status->total_time > 0xfffff) {
		status->total_time >>= 10;
		status->busy_time >>= 10;
	}

	status->busy_time <<= 10;
	status->busy_time /= status->total_time ? : 1;

	status->busy_time = status->busy_time ? : 1;
	status->total_time = 1024;
}

static int devfreq_cooling_get_requested_power(struct thermal_cooling_device *cdev,
					       u32 *power)
{
	struct devfreq_cooling_device *dfc = cdev->devdata;
	struct devfreq *df = dfc->devfreq;
	struct devfreq_dev_status status;
	unsigned long state;
	unsigned long freq;
	unsigned long voltage;
	int res, perf_idx;

	mutex_lock(&df->lock);
	status = df->last_status;
	mutex_unlock(&df->lock);

	freq = status.current_frequency;

	if (dfc->power_ops && dfc->power_ops->get_real_power) {
		voltage = get_voltage(df, freq);
		if (voltage == 0) {
			res = -EINVAL;
			goto fail;
		}

		res = dfc->power_ops->get_real_power(df, power, freq, voltage);
		if (!res) {
			state = dfc->capped_state;
			dfc->res_util = dfc->em_pd->table[state].power;
			dfc->res_util *= SCALE_ERROR_MITIGATION;

			if (*power > 1)
				dfc->res_util /= *power;
		} else {
			goto fail;
		}
	} else {
		/* Energy Model frequencies are in kHz */
		perf_idx = get_perf_idx(dfc->em_pd, freq / 1000);
		if (perf_idx < 0) {
			res = -EAGAIN;
			goto fail;
		}

		_normalize_load(&status);

		/* Scale power for utilization */
		*power = dfc->em_pd->table[perf_idx].power;
		*power *= status.busy_time;
		*power >>= 10;
	}

	trace_thermal_power_devfreq_get_power(cdev, &status, freq, *power);

	return 0;
fail:
	/* It is safe to set max in this case */
	dfc->res_util = SCALE_ERROR_MITIGATION;
	return res;
}

static int devfreq_cooling_state2power(struct thermal_cooling_device *cdev,
				       unsigned long state, u32 *power)
{
	struct devfreq_cooling_device *dfc = cdev->devdata;
	int perf_idx;

	if (state > dfc->max_state)
		return -EINVAL;

	perf_idx = dfc->max_state - state;
	*power = dfc->em_pd->table[perf_idx].power;

	return 0;
}

static int devfreq_cooling_power2state(struct thermal_cooling_device *cdev,
				       u32 power, unsigned long *state)
{
	struct devfreq_cooling_device *dfc = cdev->devdata;
	struct devfreq *df = dfc->devfreq;
	struct devfreq_dev_status status;
	unsigned long freq;
	s32 est_power;
	int i;

	mutex_lock(&df->lock);
	status = df->last_status;
	mutex_unlock(&df->lock);

	freq = status.current_frequency;

	if (dfc->power_ops && dfc->power_ops->get_real_power) {
		/* Scale for resource utilization */
		est_power = power * dfc->res_util;
		est_power /= SCALE_ERROR_MITIGATION;
	} else {
		/* Scale dynamic power for utilization */
		_normalize_load(&status);
		est_power = power << 10;
		est_power /= status.busy_time;
	}

	/*
	 * Find the first cooling state that is within the power
	 * budget. The EM power table is sorted ascending.
	 */
	for (i = dfc->max_state; i > 0; i--)
		if (est_power >= dfc->em_pd->table[i].power)
			break;

	*state = dfc->max_state - i;
	dfc->capped_state = *state;

	trace_thermal_power_devfreq_limit(cdev, freq, *state, power);
	return 0;
}

static struct thermal_cooling_device_ops devfreq_cooling_ops = {
	.get_max_state = devfreq_cooling_get_max_state,
	.get_cur_state = devfreq_cooling_get_cur_state,
	.set_cur_state = devfreq_cooling_set_cur_state,
};

/**
 * devfreq_cooling_gen_tables() - Generate frequency table.
 * @dfc:	Pointer to devfreq cooling device.
 * @num_opps:	Number of OPPs
 *
 * Generate frequency table which holds the frequencies in descending
 * order. That way its indexed by cooling device state. This is for
 * compatibility with drivers which do not register Energy Model.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int devfreq_cooling_gen_tables(struct devfreq_cooling_device *dfc,
				      int num_opps)
{
	struct devfreq *df = dfc->devfreq;
	struct device *dev = df->dev.parent;
	unsigned long freq;
	int i;

	dfc->freq_table = kcalloc(num_opps, sizeof(*dfc->freq_table),
			     GFP_KERNEL);
	if (!dfc->freq_table)
		return -ENOMEM;

	for (i = 0, freq = ULONG_MAX; i < num_opps; i++, freq--) {
		struct dev_pm_opp *opp;

		opp = dev_pm_opp_find_freq_floor(dev, &freq);
		if (IS_ERR(opp)) {
			kfree(dfc->freq_table);
			return PTR_ERR(opp);
		}

		dev_pm_opp_put(opp);
		dfc->freq_table[i] = freq;
	}

	return 0;
}

/**
 * of_devfreq_cooling_register_power() - Register devfreq cooling device,
 *                                      with OF and power information.
 * @np:	Pointer to OF device_node.
 * @df:	Pointer to devfreq device.
 * @dfc_power:	Pointer to devfreq_cooling_power.
 *
 * Register a devfreq cooling device.  The available OPPs must be
 * registered on the device.
 *
 * If @dfc_power is provided, the cooling device is registered with the
 * power extensions.  For the power extensions to work correctly,
 * devfreq should use the simple_ondemand governor, other governors
 * are not currently supported.
 */
struct thermal_cooling_device *
of_devfreq_cooling_register_power(struct device_node *np, struct devfreq *df,
				  struct devfreq_cooling_power *dfc_power)
{
	struct thermal_cooling_device *cdev;
	struct device *dev = df->dev.parent;
	struct devfreq_cooling_device *dfc;
	struct thermal_cooling_device_ops *ops;
	char *name;
	int err, num_opps;

	ops = kmemdup(&devfreq_cooling_ops, sizeof(*ops), GFP_KERNEL);
	if (!ops)
		return ERR_PTR(-ENOMEM);

	dfc = kzalloc(sizeof(*dfc), GFP_KERNEL);
	if (!dfc) {
		err = -ENOMEM;
		goto free_ops;
	}

	dfc->devfreq = df;

	dfc->em_pd = em_pd_get(dev);
	if (dfc->em_pd) {
		ops->get_requested_power =
			devfreq_cooling_get_requested_power;
		ops->state2power = devfreq_cooling_state2power;
		ops->power2state = devfreq_cooling_power2state;

		dfc->power_ops = dfc_power;

		num_opps = em_pd_nr_perf_states(dfc->em_pd);
	} else {
		/* Backward compatibility for drivers which do not use IPA */
		dev_dbg(dev, "missing EM for cooling device\n");

		num_opps = dev_pm_opp_get_opp_count(dev);

		err = devfreq_cooling_gen_tables(dfc, num_opps);
		if (err)
			goto free_dfc;
	}

	if (num_opps <= 0) {
		err = -EINVAL;
		goto free_dfc;
	}

	/* max_state is an index, not a counter */
	dfc->max_state = num_opps - 1;

	err = dev_pm_qos_add_request(dev, &dfc->req_max_freq,
				     DEV_PM_QOS_MAX_FREQUENCY,
				     PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);
	if (err < 0)
		goto free_table;

	err = -ENOMEM;
	name = kasprintf(GFP_KERNEL, "devfreq-%s", dev_name(dev));
	if (!name)
		goto remove_qos_req;

	cdev = thermal_of_cooling_device_register(np, name, dfc, ops);
	kfree(name);

	if (IS_ERR(cdev)) {
		err = PTR_ERR(cdev);
		dev_err(dev,
			"Failed to register devfreq cooling device (%d)\n",
			err);
		goto remove_qos_req;
	}

	dfc->cdev = cdev;

	return cdev;

remove_qos_req:
	dev_pm_qos_remove_request(&dfc->req_max_freq);
free_table:
	kfree(dfc->freq_table);
free_dfc:
	kfree(dfc);
free_ops:
	kfree(ops);

	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(of_devfreq_cooling_register_power);

/**
 * of_devfreq_cooling_register() - Register devfreq cooling device,
 *                                with OF information.
 * @np: Pointer to OF device_node.
 * @df: Pointer to devfreq device.
 */
struct thermal_cooling_device *
of_devfreq_cooling_register(struct device_node *np, struct devfreq *df)
{
	return of_devfreq_cooling_register_power(np, df, NULL);
}
EXPORT_SYMBOL_GPL(of_devfreq_cooling_register);

/**
 * devfreq_cooling_register() - Register devfreq cooling device.
 * @df: Pointer to devfreq device.
 */
struct thermal_cooling_device *devfreq_cooling_register(struct devfreq *df)
{
	return of_devfreq_cooling_register(NULL, df);
}
EXPORT_SYMBOL_GPL(devfreq_cooling_register);

/**
 * devfreq_cooling_em_register() - Register devfreq cooling device with
 *		power information and automatically register Energy Model (EM)
 * @df:		Pointer to devfreq device.
 * @dfc_power:	Pointer to devfreq_cooling_power.
 *
 * Register a devfreq cooling device and automatically register EM. The
 * available OPPs must be registered for the device.
 *
 * If @dfc_power is provided, the cooling device is registered with the
 * power extensions. It is using the simple Energy Model which requires
 * "dynamic-power-coefficient" a devicetree property. To not break drivers
 * which miss that DT property, the function won't bail out when the EM
 * registration failed. The cooling device will be registered if everything
 * else is OK.
 */
struct thermal_cooling_device *
devfreq_cooling_em_register(struct devfreq *df,
			    struct devfreq_cooling_power *dfc_power)
{
	struct thermal_cooling_device *cdev;
	struct device *dev;
	int ret;

	if (IS_ERR_OR_NULL(df))
		return ERR_PTR(-EINVAL);

	dev = df->dev.parent;

	ret = dev_pm_opp_of_register_em(dev, NULL);
	if (ret)
		dev_dbg(dev, "Unable to register EM for devfreq cooling device (%d)\n",
			ret);

	cdev = of_devfreq_cooling_register_power(dev->of_node, df, dfc_power);

	if (IS_ERR_OR_NULL(cdev))
		em_dev_unregister_perf_domain(dev);

	return cdev;
}
EXPORT_SYMBOL_GPL(devfreq_cooling_em_register);

/**
 * devfreq_cooling_unregister() - Unregister devfreq cooling device.
 * @cdev: Pointer to devfreq cooling device to unregister.
 *
 * Unregisters devfreq cooling device and related Energy Model if it was
 * present.
 */
void devfreq_cooling_unregister(struct thermal_cooling_device *cdev)
{
	struct devfreq_cooling_device *dfc;
	const struct thermal_cooling_device_ops *ops;
	struct device *dev;

	if (IS_ERR_OR_NULL(cdev))
		return;

	ops = cdev->ops;
	dfc = cdev->devdata;
	dev = dfc->devfreq->dev.parent;

	thermal_cooling_device_unregister(dfc->cdev);
	dev_pm_qos_remove_request(&dfc->req_max_freq);

	em_dev_unregister_perf_domain(dev);

	kfree(dfc->freq_table);
	kfree(dfc);
	kfree(ops);
}
EXPORT_SYMBOL_GPL(devfreq_cooling_unregister);
