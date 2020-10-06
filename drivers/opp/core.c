// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic OPP Interface
 *
 * Copyright (C) 2009-2010 Texas Instruments Incorporated.
 *	Nishanth Menon
 *	Romit Dasgupta
 *	Kevin Hilman
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/pm_domain.h>
#include <linux/regulator/consumer.h>

#include "opp.h"

/*
 * The root of the list of all opp-tables. All opp_table structures branch off
 * from here, with each opp_table containing the list of opps it supports in
 * various states of availability.
 */
LIST_HEAD(opp_tables);
/* Lock to allow exclusive modification to the device and opp lists */
DEFINE_MUTEX(opp_table_lock);

static struct opp_device *_find_opp_dev(const struct device *dev,
					struct opp_table *opp_table)
{
	struct opp_device *opp_dev;

	list_for_each_entry(opp_dev, &opp_table->dev_list, node)
		if (opp_dev->dev == dev)
			return opp_dev;

	return NULL;
}

static struct opp_table *_find_opp_table_unlocked(struct device *dev)
{
	struct opp_table *opp_table;
	bool found;

	list_for_each_entry(opp_table, &opp_tables, node) {
		mutex_lock(&opp_table->lock);
		found = !!_find_opp_dev(dev, opp_table);
		mutex_unlock(&opp_table->lock);

		if (found) {
			_get_opp_table_kref(opp_table);

			return opp_table;
		}
	}

	return ERR_PTR(-ENODEV);
}

/**
 * _find_opp_table() - find opp_table struct using device pointer
 * @dev:	device pointer used to lookup OPP table
 *
 * Search OPP table for one containing matching device.
 *
 * Return: pointer to 'struct opp_table' if found, otherwise -ENODEV or
 * -EINVAL based on type of error.
 *
 * The callers must call dev_pm_opp_put_opp_table() after the table is used.
 */
struct opp_table *_find_opp_table(struct device *dev)
{
	struct opp_table *opp_table;

	if (IS_ERR_OR_NULL(dev)) {
		pr_err("%s: Invalid parameters\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	mutex_lock(&opp_table_lock);
	opp_table = _find_opp_table_unlocked(dev);
	mutex_unlock(&opp_table_lock);

	return opp_table;
}

/**
 * dev_pm_opp_get_voltage() - Gets the voltage corresponding to an opp
 * @opp:	opp for which voltage has to be returned for
 *
 * Return: voltage in micro volt corresponding to the opp, else
 * return 0
 *
 * This is useful only for devices with single power supply.
 */
unsigned long dev_pm_opp_get_voltage(struct dev_pm_opp *opp)
{
	if (IS_ERR_OR_NULL(opp)) {
		pr_err("%s: Invalid parameters\n", __func__);
		return 0;
	}

	return opp->supplies[0].u_volt;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_voltage);

/**
 * dev_pm_opp_get_freq() - Gets the frequency corresponding to an available opp
 * @opp:	opp for which frequency has to be returned for
 *
 * Return: frequency in hertz corresponding to the opp, else
 * return 0
 */
unsigned long dev_pm_opp_get_freq(struct dev_pm_opp *opp)
{
	if (IS_ERR_OR_NULL(opp)) {
		pr_err("%s: Invalid parameters\n", __func__);
		return 0;
	}

	return opp->rate;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_freq);

/**
 * dev_pm_opp_get_level() - Gets the level corresponding to an available opp
 * @opp:	opp for which level value has to be returned for
 *
 * Return: level read from device tree corresponding to the opp, else
 * return 0.
 */
unsigned int dev_pm_opp_get_level(struct dev_pm_opp *opp)
{
	if (IS_ERR_OR_NULL(opp) || !opp->available) {
		pr_err("%s: Invalid parameters\n", __func__);
		return 0;
	}

	return opp->level;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_level);

/**
 * dev_pm_opp_is_turbo() - Returns if opp is turbo OPP or not
 * @opp: opp for which turbo mode is being verified
 *
 * Turbo OPPs are not for normal use, and can be enabled (under certain
 * conditions) for short duration of times to finish high throughput work
 * quickly. Running on them for longer times may overheat the chip.
 *
 * Return: true if opp is turbo opp, else false.
 */
bool dev_pm_opp_is_turbo(struct dev_pm_opp *opp)
{
	if (IS_ERR_OR_NULL(opp) || !opp->available) {
		pr_err("%s: Invalid parameters\n", __func__);
		return false;
	}

	return opp->turbo;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_is_turbo);

/**
 * dev_pm_opp_get_max_clock_latency() - Get max clock latency in nanoseconds
 * @dev:	device for which we do this operation
 *
 * Return: This function returns the max clock latency in nanoseconds.
 */
unsigned long dev_pm_opp_get_max_clock_latency(struct device *dev)
{
	struct opp_table *opp_table;
	unsigned long clock_latency_ns;

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		return 0;

	clock_latency_ns = opp_table->clock_latency_ns_max;

	dev_pm_opp_put_opp_table(opp_table);

	return clock_latency_ns;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_max_clock_latency);

/**
 * dev_pm_opp_get_max_volt_latency() - Get max voltage latency in nanoseconds
 * @dev: device for which we do this operation
 *
 * Return: This function returns the max voltage latency in nanoseconds.
 */
unsigned long dev_pm_opp_get_max_volt_latency(struct device *dev)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *opp;
	struct regulator *reg;
	unsigned long latency_ns = 0;
	int ret, i, count;
	struct {
		unsigned long min;
		unsigned long max;
	} *uV;

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		return 0;

	/* Regulator may not be required for the device */
	if (!opp_table->regulators)
		goto put_opp_table;

	count = opp_table->regulator_count;

	uV = kmalloc_array(count, sizeof(*uV), GFP_KERNEL);
	if (!uV)
		goto put_opp_table;

	mutex_lock(&opp_table->lock);

	for (i = 0; i < count; i++) {
		uV[i].min = ~0;
		uV[i].max = 0;

		list_for_each_entry(opp, &opp_table->opp_list, node) {
			if (!opp->available)
				continue;

			if (opp->supplies[i].u_volt_min < uV[i].min)
				uV[i].min = opp->supplies[i].u_volt_min;
			if (opp->supplies[i].u_volt_max > uV[i].max)
				uV[i].max = opp->supplies[i].u_volt_max;
		}
	}

	mutex_unlock(&opp_table->lock);

	/*
	 * The caller needs to ensure that opp_table (and hence the regulator)
	 * isn't freed, while we are executing this routine.
	 */
	for (i = 0; i < count; i++) {
		reg = opp_table->regulators[i];
		ret = regulator_set_voltage_time(reg, uV[i].min, uV[i].max);
		if (ret > 0)
			latency_ns += ret * 1000;
	}

	kfree(uV);
put_opp_table:
	dev_pm_opp_put_opp_table(opp_table);

	return latency_ns;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_max_volt_latency);

/**
 * dev_pm_opp_get_max_transition_latency() - Get max transition latency in
 *					     nanoseconds
 * @dev: device for which we do this operation
 *
 * Return: This function returns the max transition latency, in nanoseconds, to
 * switch from one OPP to other.
 */
unsigned long dev_pm_opp_get_max_transition_latency(struct device *dev)
{
	return dev_pm_opp_get_max_volt_latency(dev) +
		dev_pm_opp_get_max_clock_latency(dev);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_max_transition_latency);

/**
 * dev_pm_opp_get_suspend_opp_freq() - Get frequency of suspend opp in Hz
 * @dev:	device for which we do this operation
 *
 * Return: This function returns the frequency of the OPP marked as suspend_opp
 * if one is available, else returns 0;
 */
unsigned long dev_pm_opp_get_suspend_opp_freq(struct device *dev)
{
	struct opp_table *opp_table;
	unsigned long freq = 0;

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		return 0;

	if (opp_table->suspend_opp && opp_table->suspend_opp->available)
		freq = dev_pm_opp_get_freq(opp_table->suspend_opp);

	dev_pm_opp_put_opp_table(opp_table);

	return freq;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_suspend_opp_freq);

int _get_opp_count(struct opp_table *opp_table)
{
	struct dev_pm_opp *opp;
	int count = 0;

	mutex_lock(&opp_table->lock);

	list_for_each_entry(opp, &opp_table->opp_list, node) {
		if (opp->available)
			count++;
	}

	mutex_unlock(&opp_table->lock);

	return count;
}

/**
 * dev_pm_opp_get_opp_count() - Get number of opps available in the opp table
 * @dev:	device for which we do this operation
 *
 * Return: This function returns the number of available opps if there are any,
 * else returns 0 if none or the corresponding error value.
 */
int dev_pm_opp_get_opp_count(struct device *dev)
{
	struct opp_table *opp_table;
	int count;

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		count = PTR_ERR(opp_table);
		dev_dbg(dev, "%s: OPP table not found (%d)\n",
			__func__, count);
		return count;
	}

	count = _get_opp_count(opp_table);
	dev_pm_opp_put_opp_table(opp_table);

	return count;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_opp_count);

/**
 * dev_pm_opp_find_freq_exact() - search for an exact frequency
 * @dev:		device for which we do this operation
 * @freq:		frequency to search for
 * @available:		true/false - match for available opp
 *
 * Return: Searches for exact match in the opp table and returns pointer to the
 * matching opp if found, else returns ERR_PTR in case of error and should
 * be handled using IS_ERR. Error return values can be:
 * EINVAL:	for bad pointer
 * ERANGE:	no match found for search
 * ENODEV:	if device not found in list of registered devices
 *
 * Note: available is a modifier for the search. if available=true, then the
 * match is for exact matching frequency and is available in the stored OPP
 * table. if false, the match is for exact frequency which is not available.
 *
 * This provides a mechanism to enable an opp which is not available currently
 * or the opposite as well.
 *
 * The callers are required to call dev_pm_opp_put() for the returned OPP after
 * use.
 */
struct dev_pm_opp *dev_pm_opp_find_freq_exact(struct device *dev,
					      unsigned long freq,
					      bool available)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *temp_opp, *opp = ERR_PTR(-ERANGE);

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		int r = PTR_ERR(opp_table);

		dev_err(dev, "%s: OPP table not found (%d)\n", __func__, r);
		return ERR_PTR(r);
	}

	mutex_lock(&opp_table->lock);

	list_for_each_entry(temp_opp, &opp_table->opp_list, node) {
		if (temp_opp->available == available &&
				temp_opp->rate == freq) {
			opp = temp_opp;

			/* Increment the reference count of OPP */
			dev_pm_opp_get(opp);
			break;
		}
	}

	mutex_unlock(&opp_table->lock);
	dev_pm_opp_put_opp_table(opp_table);

	return opp;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_find_freq_exact);

/**
 * dev_pm_opp_find_level_exact() - search for an exact level
 * @dev:		device for which we do this operation
 * @level:		level to search for
 *
 * Return: Searches for exact match in the opp table and returns pointer to the
 * matching opp if found, else returns ERR_PTR in case of error and should
 * be handled using IS_ERR. Error return values can be:
 * EINVAL:	for bad pointer
 * ERANGE:	no match found for search
 * ENODEV:	if device not found in list of registered devices
 *
 * The callers are required to call dev_pm_opp_put() for the returned OPP after
 * use.
 */
struct dev_pm_opp *dev_pm_opp_find_level_exact(struct device *dev,
					       unsigned int level)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *temp_opp, *opp = ERR_PTR(-ERANGE);

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		int r = PTR_ERR(opp_table);

		dev_err(dev, "%s: OPP table not found (%d)\n", __func__, r);
		return ERR_PTR(r);
	}

	mutex_lock(&opp_table->lock);

	list_for_each_entry(temp_opp, &opp_table->opp_list, node) {
		if (temp_opp->level == level) {
			opp = temp_opp;

			/* Increment the reference count of OPP */
			dev_pm_opp_get(opp);
			break;
		}
	}

	mutex_unlock(&opp_table->lock);
	dev_pm_opp_put_opp_table(opp_table);

	return opp;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_find_level_exact);

static noinline struct dev_pm_opp *_find_freq_ceil(struct opp_table *opp_table,
						   unsigned long *freq)
{
	struct dev_pm_opp *temp_opp, *opp = ERR_PTR(-ERANGE);

	mutex_lock(&opp_table->lock);

	list_for_each_entry(temp_opp, &opp_table->opp_list, node) {
		if (temp_opp->available && temp_opp->rate >= *freq) {
			opp = temp_opp;
			*freq = opp->rate;

			/* Increment the reference count of OPP */
			dev_pm_opp_get(opp);
			break;
		}
	}

	mutex_unlock(&opp_table->lock);

	return opp;
}

/**
 * dev_pm_opp_find_freq_ceil() - Search for an rounded ceil freq
 * @dev:	device for which we do this operation
 * @freq:	Start frequency
 *
 * Search for the matching ceil *available* OPP from a starting freq
 * for a device.
 *
 * Return: matching *opp and refreshes *freq accordingly, else returns
 * ERR_PTR in case of error and should be handled using IS_ERR. Error return
 * values can be:
 * EINVAL:	for bad pointer
 * ERANGE:	no match found for search
 * ENODEV:	if device not found in list of registered devices
 *
 * The callers are required to call dev_pm_opp_put() for the returned OPP after
 * use.
 */
struct dev_pm_opp *dev_pm_opp_find_freq_ceil(struct device *dev,
					     unsigned long *freq)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *opp;

	if (!dev || !freq) {
		dev_err(dev, "%s: Invalid argument freq=%p\n", __func__, freq);
		return ERR_PTR(-EINVAL);
	}

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		return ERR_CAST(opp_table);

	opp = _find_freq_ceil(opp_table, freq);

	dev_pm_opp_put_opp_table(opp_table);

	return opp;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_find_freq_ceil);

/**
 * dev_pm_opp_find_freq_floor() - Search for a rounded floor freq
 * @dev:	device for which we do this operation
 * @freq:	Start frequency
 *
 * Search for the matching floor *available* OPP from a starting freq
 * for a device.
 *
 * Return: matching *opp and refreshes *freq accordingly, else returns
 * ERR_PTR in case of error and should be handled using IS_ERR. Error return
 * values can be:
 * EINVAL:	for bad pointer
 * ERANGE:	no match found for search
 * ENODEV:	if device not found in list of registered devices
 *
 * The callers are required to call dev_pm_opp_put() for the returned OPP after
 * use.
 */
struct dev_pm_opp *dev_pm_opp_find_freq_floor(struct device *dev,
					      unsigned long *freq)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *temp_opp, *opp = ERR_PTR(-ERANGE);

	if (!dev || !freq) {
		dev_err(dev, "%s: Invalid argument freq=%p\n", __func__, freq);
		return ERR_PTR(-EINVAL);
	}

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		return ERR_CAST(opp_table);

	mutex_lock(&opp_table->lock);

	list_for_each_entry(temp_opp, &opp_table->opp_list, node) {
		if (temp_opp->available) {
			/* go to the next node, before choosing prev */
			if (temp_opp->rate > *freq)
				break;
			else
				opp = temp_opp;
		}
	}

	/* Increment the reference count of OPP */
	if (!IS_ERR(opp))
		dev_pm_opp_get(opp);
	mutex_unlock(&opp_table->lock);
	dev_pm_opp_put_opp_table(opp_table);

	if (!IS_ERR(opp))
		*freq = opp->rate;

	return opp;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_find_freq_floor);

/**
 * dev_pm_opp_find_freq_ceil_by_volt() - Find OPP with highest frequency for
 *					 target voltage.
 * @dev:	Device for which we do this operation.
 * @u_volt:	Target voltage.
 *
 * Search for OPP with highest (ceil) frequency and has voltage <= u_volt.
 *
 * Return: matching *opp, else returns ERR_PTR in case of error which should be
 * handled using IS_ERR.
 *
 * Error return values can be:
 * EINVAL:	bad parameters
 *
 * The callers are required to call dev_pm_opp_put() for the returned OPP after
 * use.
 */
struct dev_pm_opp *dev_pm_opp_find_freq_ceil_by_volt(struct device *dev,
						     unsigned long u_volt)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *temp_opp, *opp = ERR_PTR(-ERANGE);

	if (!dev || !u_volt) {
		dev_err(dev, "%s: Invalid argument volt=%lu\n", __func__,
			u_volt);
		return ERR_PTR(-EINVAL);
	}

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		return ERR_CAST(opp_table);

	mutex_lock(&opp_table->lock);

	list_for_each_entry(temp_opp, &opp_table->opp_list, node) {
		if (temp_opp->available) {
			if (temp_opp->supplies[0].u_volt > u_volt)
				break;
			opp = temp_opp;
		}
	}

	/* Increment the reference count of OPP */
	if (!IS_ERR(opp))
		dev_pm_opp_get(opp);

	mutex_unlock(&opp_table->lock);
	dev_pm_opp_put_opp_table(opp_table);

	return opp;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_find_freq_ceil_by_volt);

static int _set_opp_voltage(struct device *dev, struct regulator *reg,
			    struct dev_pm_opp_supply *supply)
{
	int ret;

	/* Regulator not available for device */
	if (IS_ERR(reg)) {
		dev_dbg(dev, "%s: regulator not available: %ld\n", __func__,
			PTR_ERR(reg));
		return 0;
	}

	dev_dbg(dev, "%s: voltages (mV): %lu %lu %lu\n", __func__,
		supply->u_volt_min, supply->u_volt, supply->u_volt_max);

	ret = regulator_set_voltage_triplet(reg, supply->u_volt_min,
					    supply->u_volt, supply->u_volt_max);
	if (ret)
		dev_err(dev, "%s: failed to set voltage (%lu %lu %lu mV): %d\n",
			__func__, supply->u_volt_min, supply->u_volt,
			supply->u_volt_max, ret);

	return ret;
}

static inline int _generic_set_opp_clk_only(struct device *dev, struct clk *clk,
					    unsigned long freq)
{
	int ret;

	ret = clk_set_rate(clk, freq);
	if (ret) {
		dev_err(dev, "%s: failed to set clock rate: %d\n", __func__,
			ret);
	}

	return ret;
}

static int _generic_set_opp_regulator(struct opp_table *opp_table,
				      struct device *dev,
				      unsigned long old_freq,
				      unsigned long freq,
				      struct dev_pm_opp_supply *old_supply,
				      struct dev_pm_opp_supply *new_supply)
{
	struct regulator *reg = opp_table->regulators[0];
	int ret;

	/* This function only supports single regulator per device */
	if (WARN_ON(opp_table->regulator_count > 1)) {
		dev_err(dev, "multiple regulators are not supported\n");
		return -EINVAL;
	}

	/* Scaling up? Scale voltage before frequency */
	if (freq >= old_freq) {
		ret = _set_opp_voltage(dev, reg, new_supply);
		if (ret)
			goto restore_voltage;
	}

	/* Change frequency */
	ret = _generic_set_opp_clk_only(dev, opp_table->clk, freq);
	if (ret)
		goto restore_voltage;

	/* Scaling down? Scale voltage after frequency */
	if (freq < old_freq) {
		ret = _set_opp_voltage(dev, reg, new_supply);
		if (ret)
			goto restore_freq;
	}

	/*
	 * Enable the regulator after setting its voltages, otherwise it breaks
	 * some boot-enabled regulators.
	 */
	if (unlikely(!opp_table->regulator_enabled)) {
		ret = regulator_enable(reg);
		if (ret < 0)
			dev_warn(dev, "Failed to enable regulator: %d", ret);
		else
			opp_table->regulator_enabled = true;
	}

	return 0;

restore_freq:
	if (_generic_set_opp_clk_only(dev, opp_table->clk, old_freq))
		dev_err(dev, "%s: failed to restore old-freq (%lu Hz)\n",
			__func__, old_freq);
restore_voltage:
	/* This shouldn't harm even if the voltages weren't updated earlier */
	if (old_supply)
		_set_opp_voltage(dev, reg, old_supply);

	return ret;
}

static int _set_opp_bw(const struct opp_table *opp_table,
		       struct dev_pm_opp *opp, struct device *dev, bool remove)
{
	u32 avg, peak;
	int i, ret;

	if (!opp_table->paths)
		return 0;

	for (i = 0; i < opp_table->path_count; i++) {
		if (remove) {
			avg = 0;
			peak = 0;
		} else {
			avg = opp->bandwidth[i].avg;
			peak = opp->bandwidth[i].peak;
		}
		ret = icc_set_bw(opp_table->paths[i], avg, peak);
		if (ret) {
			dev_err(dev, "Failed to %s bandwidth[%d]: %d\n",
				remove ? "remove" : "set", i, ret);
			return ret;
		}
	}

	return 0;
}

static int _set_opp_custom(const struct opp_table *opp_table,
			   struct device *dev, unsigned long old_freq,
			   unsigned long freq,
			   struct dev_pm_opp_supply *old_supply,
			   struct dev_pm_opp_supply *new_supply)
{
	struct dev_pm_set_opp_data *data;
	int size;

	data = opp_table->set_opp_data;
	data->regulators = opp_table->regulators;
	data->regulator_count = opp_table->regulator_count;
	data->clk = opp_table->clk;
	data->dev = dev;

	data->old_opp.rate = old_freq;
	size = sizeof(*old_supply) * opp_table->regulator_count;
	if (!old_supply)
		memset(data->old_opp.supplies, 0, size);
	else
		memcpy(data->old_opp.supplies, old_supply, size);

	data->new_opp.rate = freq;
	memcpy(data->new_opp.supplies, new_supply, size);

	return opp_table->set_opp(data);
}

/* This is only called for PM domain for now */
static int _set_required_opps(struct device *dev,
			      struct opp_table *opp_table,
			      struct dev_pm_opp *opp)
{
	struct opp_table **required_opp_tables = opp_table->required_opp_tables;
	struct device **genpd_virt_devs = opp_table->genpd_virt_devs;
	unsigned int pstate;
	int i, ret = 0;

	if (!required_opp_tables)
		return 0;

	/* Single genpd case */
	if (!genpd_virt_devs) {
		pstate = likely(opp) ? opp->required_opps[0]->pstate : 0;
		ret = dev_pm_genpd_set_performance_state(dev, pstate);
		if (ret) {
			dev_err(dev, "Failed to set performance state of %s: %d (%d)\n",
				dev_name(dev), pstate, ret);
		}
		return ret;
	}

	/* Multiple genpd case */

	/*
	 * Acquire genpd_virt_dev_lock to make sure we don't use a genpd_dev
	 * after it is freed from another thread.
	 */
	mutex_lock(&opp_table->genpd_virt_dev_lock);

	for (i = 0; i < opp_table->required_opp_count; i++) {
		pstate = likely(opp) ? opp->required_opps[i]->pstate : 0;

		if (!genpd_virt_devs[i])
			continue;

		ret = dev_pm_genpd_set_performance_state(genpd_virt_devs[i], pstate);
		if (ret) {
			dev_err(dev, "Failed to set performance rate of %s: %d (%d)\n",
				dev_name(genpd_virt_devs[i]), pstate, ret);
			break;
		}
	}
	mutex_unlock(&opp_table->genpd_virt_dev_lock);

	return ret;
}

/**
 * dev_pm_opp_set_bw() - sets bandwidth levels corresponding to an opp
 * @dev:	device for which we do this operation
 * @opp:	opp based on which the bandwidth levels are to be configured
 *
 * This configures the bandwidth to the levels specified by the OPP. However
 * if the OPP specified is NULL the bandwidth levels are cleared out.
 *
 * Return: 0 on success or a negative error value.
 */
int dev_pm_opp_set_bw(struct device *dev, struct dev_pm_opp *opp)
{
	struct opp_table *opp_table;
	int ret;

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		dev_err(dev, "%s: device opp table doesn't exist\n", __func__);
		return PTR_ERR(opp_table);
	}

	if (opp)
		ret = _set_opp_bw(opp_table, opp, dev, false);
	else
		ret = _set_opp_bw(opp_table, NULL, dev, true);

	dev_pm_opp_put_opp_table(opp_table);
	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_bw);

/**
 * dev_pm_opp_set_rate() - Configure new OPP based on frequency
 * @dev:	 device for which we do this operation
 * @target_freq: frequency to achieve
 *
 * This configures the power-supplies to the levels specified by the OPP
 * corresponding to the target_freq, and programs the clock to a value <=
 * target_freq, as rounded by clk_round_rate(). Device wanting to run at fmax
 * provided by the opp, should have already rounded to the target OPP's
 * frequency.
 */
int dev_pm_opp_set_rate(struct device *dev, unsigned long target_freq)
{
	struct opp_table *opp_table;
	unsigned long freq, old_freq, temp_freq;
	struct dev_pm_opp *old_opp, *opp;
	struct clk *clk;
	int ret;

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		dev_err(dev, "%s: device opp doesn't exist\n", __func__);
		return PTR_ERR(opp_table);
	}

	if (unlikely(!target_freq)) {
		/*
		 * Some drivers need to support cases where some platforms may
		 * have OPP table for the device, while others don't and
		 * opp_set_rate() just needs to behave like clk_set_rate().
		 */
		if (!_get_opp_count(opp_table)) {
			ret = 0;
			goto put_opp_table;
		}

		if (!opp_table->required_opp_tables && !opp_table->regulators &&
		    !opp_table->paths) {
			dev_err(dev, "target frequency can't be 0\n");
			ret = -EINVAL;
			goto put_opp_table;
		}

		ret = _set_opp_bw(opp_table, NULL, dev, true);
		if (ret)
			goto put_opp_table;

		if (opp_table->regulator_enabled) {
			regulator_disable(opp_table->regulators[0]);
			opp_table->regulator_enabled = false;
		}

		ret = _set_required_opps(dev, opp_table, NULL);
		goto put_opp_table;
	}

	clk = opp_table->clk;
	if (IS_ERR(clk)) {
		dev_err(dev, "%s: No clock available for the device\n",
			__func__);
		ret = PTR_ERR(clk);
		goto put_opp_table;
	}

	freq = clk_round_rate(clk, target_freq);
	if ((long)freq <= 0)
		freq = target_freq;

	old_freq = clk_get_rate(clk);

	/* Return early if nothing to do */
	if (old_freq == freq) {
		if (!opp_table->required_opp_tables && !opp_table->regulators &&
		    !opp_table->paths) {
			dev_dbg(dev, "%s: old/new frequencies (%lu Hz) are same, nothing to do\n",
				__func__, freq);
			ret = 0;
			goto put_opp_table;
		}
	}

	/*
	 * For IO devices which require an OPP on some platforms/SoCs
	 * while just needing to scale the clock on some others
	 * we look for empty OPP tables with just a clock handle and
	 * scale only the clk. This makes dev_pm_opp_set_rate()
	 * equivalent to a clk_set_rate()
	 */
	if (!_get_opp_count(opp_table)) {
		ret = _generic_set_opp_clk_only(dev, clk, freq);
		goto put_opp_table;
	}

	temp_freq = old_freq;
	old_opp = _find_freq_ceil(opp_table, &temp_freq);
	if (IS_ERR(old_opp)) {
		dev_err(dev, "%s: failed to find current OPP for freq %lu (%ld)\n",
			__func__, old_freq, PTR_ERR(old_opp));
	}

	temp_freq = freq;
	opp = _find_freq_ceil(opp_table, &temp_freq);
	if (IS_ERR(opp)) {
		ret = PTR_ERR(opp);
		dev_err(dev, "%s: failed to find OPP for freq %lu (%d)\n",
			__func__, freq, ret);
		goto put_old_opp;
	}

	dev_dbg(dev, "%s: switching OPP: %lu Hz --> %lu Hz\n", __func__,
		old_freq, freq);

	/* Scaling up? Configure required OPPs before frequency */
	if (freq >= old_freq) {
		ret = _set_required_opps(dev, opp_table, opp);
		if (ret)
			goto put_opp;
	}

	if (opp_table->set_opp) {
		ret = _set_opp_custom(opp_table, dev, old_freq, freq,
				      IS_ERR(old_opp) ? NULL : old_opp->supplies,
				      opp->supplies);
	} else if (opp_table->regulators) {
		ret = _generic_set_opp_regulator(opp_table, dev, old_freq, freq,
						 IS_ERR(old_opp) ? NULL : old_opp->supplies,
						 opp->supplies);
	} else {
		/* Only frequency scaling */
		ret = _generic_set_opp_clk_only(dev, clk, freq);
	}

	/* Scaling down? Configure required OPPs after frequency */
	if (!ret && freq < old_freq) {
		ret = _set_required_opps(dev, opp_table, opp);
		if (ret)
			dev_err(dev, "Failed to set required opps: %d\n", ret);
	}

	if (!ret)
		ret = _set_opp_bw(opp_table, opp, dev, false);

put_opp:
	dev_pm_opp_put(opp);
put_old_opp:
	if (!IS_ERR(old_opp))
		dev_pm_opp_put(old_opp);
put_opp_table:
	dev_pm_opp_put_opp_table(opp_table);
	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_rate);

/* OPP-dev Helpers */
static void _remove_opp_dev(struct opp_device *opp_dev,
			    struct opp_table *opp_table)
{
	opp_debug_unregister(opp_dev, opp_table);
	list_del(&opp_dev->node);
	kfree(opp_dev);
}

static struct opp_device *_add_opp_dev_unlocked(const struct device *dev,
						struct opp_table *opp_table)
{
	struct opp_device *opp_dev;

	opp_dev = kzalloc(sizeof(*opp_dev), GFP_KERNEL);
	if (!opp_dev)
		return NULL;

	/* Initialize opp-dev */
	opp_dev->dev = dev;

	list_add(&opp_dev->node, &opp_table->dev_list);

	/* Create debugfs entries for the opp_table */
	opp_debug_register(opp_dev, opp_table);

	return opp_dev;
}

struct opp_device *_add_opp_dev(const struct device *dev,
				struct opp_table *opp_table)
{
	struct opp_device *opp_dev;

	mutex_lock(&opp_table->lock);
	opp_dev = _add_opp_dev_unlocked(dev, opp_table);
	mutex_unlock(&opp_table->lock);

	return opp_dev;
}

static struct opp_table *_allocate_opp_table(struct device *dev, int index)
{
	struct opp_table *opp_table;
	struct opp_device *opp_dev;
	int ret;

	/*
	 * Allocate a new OPP table. In the infrequent case where a new
	 * device is needed to be added, we pay this penalty.
	 */
	opp_table = kzalloc(sizeof(*opp_table), GFP_KERNEL);
	if (!opp_table)
		return ERR_PTR(-ENOMEM);

	mutex_init(&opp_table->lock);
	mutex_init(&opp_table->genpd_virt_dev_lock);
	INIT_LIST_HEAD(&opp_table->dev_list);

	/* Mark regulator count uninitialized */
	opp_table->regulator_count = -1;

	opp_dev = _add_opp_dev(dev, opp_table);
	if (!opp_dev) {
		ret = -ENOMEM;
		goto err;
	}

	_of_init_opp_table(opp_table, dev, index);

	/* Find clk for the device */
	opp_table->clk = clk_get(dev, NULL);
	if (IS_ERR(opp_table->clk)) {
		ret = PTR_ERR(opp_table->clk);
		if (ret == -EPROBE_DEFER)
			goto err;

		dev_dbg(dev, "%s: Couldn't find clock: %d\n", __func__, ret);
	}

	/* Find interconnect path(s) for the device */
	ret = dev_pm_opp_of_find_icc_paths(dev, opp_table);
	if (ret) {
		if (ret == -EPROBE_DEFER)
			goto err;

		dev_warn(dev, "%s: Error finding interconnect paths: %d\n",
			 __func__, ret);
	}

	BLOCKING_INIT_NOTIFIER_HEAD(&opp_table->head);
	INIT_LIST_HEAD(&opp_table->opp_list);
	kref_init(&opp_table->kref);

	/* Secure the device table modification */
	list_add(&opp_table->node, &opp_tables);
	return opp_table;

err:
	kfree(opp_table);
	return ERR_PTR(ret);
}

void _get_opp_table_kref(struct opp_table *opp_table)
{
	kref_get(&opp_table->kref);
}

static struct opp_table *_opp_get_opp_table(struct device *dev, int index)
{
	struct opp_table *opp_table;

	/* Hold our table modification lock here */
	mutex_lock(&opp_table_lock);

	opp_table = _find_opp_table_unlocked(dev);
	if (!IS_ERR(opp_table))
		goto unlock;

	opp_table = _managed_opp(dev, index);
	if (opp_table) {
		if (!_add_opp_dev_unlocked(dev, opp_table)) {
			dev_pm_opp_put_opp_table(opp_table);
			opp_table = ERR_PTR(-ENOMEM);
		}
		goto unlock;
	}

	opp_table = _allocate_opp_table(dev, index);

unlock:
	mutex_unlock(&opp_table_lock);

	return opp_table;
}

struct opp_table *dev_pm_opp_get_opp_table(struct device *dev)
{
	return _opp_get_opp_table(dev, 0);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_opp_table);

struct opp_table *dev_pm_opp_get_opp_table_indexed(struct device *dev,
						   int index)
{
	return _opp_get_opp_table(dev, index);
}

static void _opp_table_kref_release(struct kref *kref)
{
	struct opp_table *opp_table = container_of(kref, struct opp_table, kref);
	struct opp_device *opp_dev, *temp;
	int i;

	_of_clear_opp_table(opp_table);

	/* Release clk */
	if (!IS_ERR(opp_table->clk))
		clk_put(opp_table->clk);

	if (opp_table->paths) {
		for (i = 0; i < opp_table->path_count; i++)
			icc_put(opp_table->paths[i]);
		kfree(opp_table->paths);
	}

	WARN_ON(!list_empty(&opp_table->opp_list));

	list_for_each_entry_safe(opp_dev, temp, &opp_table->dev_list, node) {
		/*
		 * The OPP table is getting removed, drop the performance state
		 * constraints.
		 */
		if (opp_table->genpd_performance_state)
			dev_pm_genpd_set_performance_state((struct device *)(opp_dev->dev), 0);

		_remove_opp_dev(opp_dev, opp_table);
	}

	mutex_destroy(&opp_table->genpd_virt_dev_lock);
	mutex_destroy(&opp_table->lock);
	list_del(&opp_table->node);
	kfree(opp_table);

	mutex_unlock(&opp_table_lock);
}

void dev_pm_opp_put_opp_table(struct opp_table *opp_table)
{
	kref_put_mutex(&opp_table->kref, _opp_table_kref_release,
		       &opp_table_lock);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_put_opp_table);

void _opp_free(struct dev_pm_opp *opp)
{
	kfree(opp);
}

static void _opp_kref_release(struct dev_pm_opp *opp,
			      struct opp_table *opp_table)
{
	/*
	 * Notify the changes in the availability of the operable
	 * frequency/voltage list.
	 */
	blocking_notifier_call_chain(&opp_table->head, OPP_EVENT_REMOVE, opp);
	_of_opp_free_required_opps(opp_table, opp);
	opp_debug_remove_one(opp);
	list_del(&opp->node);
	kfree(opp);
}

static void _opp_kref_release_unlocked(struct kref *kref)
{
	struct dev_pm_opp *opp = container_of(kref, struct dev_pm_opp, kref);
	struct opp_table *opp_table = opp->opp_table;

	_opp_kref_release(opp, opp_table);
}

static void _opp_kref_release_locked(struct kref *kref)
{
	struct dev_pm_opp *opp = container_of(kref, struct dev_pm_opp, kref);
	struct opp_table *opp_table = opp->opp_table;

	_opp_kref_release(opp, opp_table);
	mutex_unlock(&opp_table->lock);
}

void dev_pm_opp_get(struct dev_pm_opp *opp)
{
	kref_get(&opp->kref);
}

void dev_pm_opp_put(struct dev_pm_opp *opp)
{
	kref_put_mutex(&opp->kref, _opp_kref_release_locked,
		       &opp->opp_table->lock);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_put);

static void dev_pm_opp_put_unlocked(struct dev_pm_opp *opp)
{
	kref_put(&opp->kref, _opp_kref_release_unlocked);
}

/**
 * dev_pm_opp_remove()  - Remove an OPP from OPP table
 * @dev:	device for which we do this operation
 * @freq:	OPP to remove with matching 'freq'
 *
 * This function removes an opp from the opp table.
 */
void dev_pm_opp_remove(struct device *dev, unsigned long freq)
{
	struct dev_pm_opp *opp;
	struct opp_table *opp_table;
	bool found = false;

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		return;

	mutex_lock(&opp_table->lock);

	list_for_each_entry(opp, &opp_table->opp_list, node) {
		if (opp->rate == freq) {
			found = true;
			break;
		}
	}

	mutex_unlock(&opp_table->lock);

	if (found) {
		dev_pm_opp_put(opp);

		/* Drop the reference taken by dev_pm_opp_add() */
		dev_pm_opp_put_opp_table(opp_table);
	} else {
		dev_warn(dev, "%s: Couldn't find OPP with freq: %lu\n",
			 __func__, freq);
	}

	/* Drop the reference taken by _find_opp_table() */
	dev_pm_opp_put_opp_table(opp_table);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_remove);

bool _opp_remove_all_static(struct opp_table *opp_table)
{
	struct dev_pm_opp *opp, *tmp;
	bool ret = true;

	mutex_lock(&opp_table->lock);

	if (!opp_table->parsed_static_opps) {
		ret = false;
		goto unlock;
	}

	if (--opp_table->parsed_static_opps)
		goto unlock;

	list_for_each_entry_safe(opp, tmp, &opp_table->opp_list, node) {
		if (!opp->dynamic)
			dev_pm_opp_put_unlocked(opp);
	}

unlock:
	mutex_unlock(&opp_table->lock);

	return ret;
}

/**
 * dev_pm_opp_remove_all_dynamic() - Remove all dynamically created OPPs
 * @dev:	device for which we do this operation
 *
 * This function removes all dynamically created OPPs from the opp table.
 */
void dev_pm_opp_remove_all_dynamic(struct device *dev)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *opp, *temp;
	int count = 0;

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		return;

	mutex_lock(&opp_table->lock);
	list_for_each_entry_safe(opp, temp, &opp_table->opp_list, node) {
		if (opp->dynamic) {
			dev_pm_opp_put_unlocked(opp);
			count++;
		}
	}
	mutex_unlock(&opp_table->lock);

	/* Drop the references taken by dev_pm_opp_add() */
	while (count--)
		dev_pm_opp_put_opp_table(opp_table);

	/* Drop the reference taken by _find_opp_table() */
	dev_pm_opp_put_opp_table(opp_table);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_remove_all_dynamic);

struct dev_pm_opp *_opp_allocate(struct opp_table *table)
{
	struct dev_pm_opp *opp;
	int supply_count, supply_size, icc_size;

	/* Allocate space for at least one supply */
	supply_count = table->regulator_count > 0 ? table->regulator_count : 1;
	supply_size = sizeof(*opp->supplies) * supply_count;
	icc_size = sizeof(*opp->bandwidth) * table->path_count;

	/* allocate new OPP node and supplies structures */
	opp = kzalloc(sizeof(*opp) + supply_size + icc_size, GFP_KERNEL);

	if (!opp)
		return NULL;

	/* Put the supplies at the end of the OPP structure as an empty array */
	opp->supplies = (struct dev_pm_opp_supply *)(opp + 1);
	if (icc_size)
		opp->bandwidth = (struct dev_pm_opp_icc_bw *)(opp->supplies + supply_count);
	INIT_LIST_HEAD(&opp->node);

	return opp;
}

static bool _opp_supported_by_regulators(struct dev_pm_opp *opp,
					 struct opp_table *opp_table)
{
	struct regulator *reg;
	int i;

	if (!opp_table->regulators)
		return true;

	for (i = 0; i < opp_table->regulator_count; i++) {
		reg = opp_table->regulators[i];

		if (!regulator_is_supported_voltage(reg,
					opp->supplies[i].u_volt_min,
					opp->supplies[i].u_volt_max)) {
			pr_warn("%s: OPP minuV: %lu maxuV: %lu, not supported by regulator\n",
				__func__, opp->supplies[i].u_volt_min,
				opp->supplies[i].u_volt_max);
			return false;
		}
	}

	return true;
}

int _opp_compare_key(struct dev_pm_opp *opp1, struct dev_pm_opp *opp2)
{
	if (opp1->rate != opp2->rate)
		return opp1->rate < opp2->rate ? -1 : 1;
	if (opp1->bandwidth && opp2->bandwidth &&
	    opp1->bandwidth[0].peak != opp2->bandwidth[0].peak)
		return opp1->bandwidth[0].peak < opp2->bandwidth[0].peak ? -1 : 1;
	if (opp1->level != opp2->level)
		return opp1->level < opp2->level ? -1 : 1;
	return 0;
}

static int _opp_is_duplicate(struct device *dev, struct dev_pm_opp *new_opp,
			     struct opp_table *opp_table,
			     struct list_head **head)
{
	struct dev_pm_opp *opp;
	int opp_cmp;

	/*
	 * Insert new OPP in order of increasing frequency and discard if
	 * already present.
	 *
	 * Need to use &opp_table->opp_list in the condition part of the 'for'
	 * loop, don't replace it with head otherwise it will become an infinite
	 * loop.
	 */
	list_for_each_entry(opp, &opp_table->opp_list, node) {
		opp_cmp = _opp_compare_key(new_opp, opp);
		if (opp_cmp > 0) {
			*head = &opp->node;
			continue;
		}

		if (opp_cmp < 0)
			return 0;

		/* Duplicate OPPs */
		dev_warn(dev, "%s: duplicate OPPs detected. Existing: freq: %lu, volt: %lu, enabled: %d. New: freq: %lu, volt: %lu, enabled: %d\n",
			 __func__, opp->rate, opp->supplies[0].u_volt,
			 opp->available, new_opp->rate,
			 new_opp->supplies[0].u_volt, new_opp->available);

		/* Should we compare voltages for all regulators here ? */
		return opp->available &&
		       new_opp->supplies[0].u_volt == opp->supplies[0].u_volt ? -EBUSY : -EEXIST;
	}

	return 0;
}

/*
 * Returns:
 * 0: On success. And appropriate error message for duplicate OPPs.
 * -EBUSY: For OPP with same freq/volt and is available. The callers of
 *  _opp_add() must return 0 if they receive -EBUSY from it. This is to make
 *  sure we don't print error messages unnecessarily if different parts of
 *  kernel try to initialize the OPP table.
 * -EEXIST: For OPP with same freq but different volt or is unavailable. This
 *  should be considered an error by the callers of _opp_add().
 */
int _opp_add(struct device *dev, struct dev_pm_opp *new_opp,
	     struct opp_table *opp_table, bool rate_not_available)
{
	struct list_head *head;
	int ret;

	mutex_lock(&opp_table->lock);
	head = &opp_table->opp_list;

	if (likely(!rate_not_available)) {
		ret = _opp_is_duplicate(dev, new_opp, opp_table, &head);
		if (ret) {
			mutex_unlock(&opp_table->lock);
			return ret;
		}
	}

	list_add(&new_opp->node, head);
	mutex_unlock(&opp_table->lock);

	new_opp->opp_table = opp_table;
	kref_init(&new_opp->kref);

	opp_debug_create_one(new_opp, opp_table);

	if (!_opp_supported_by_regulators(new_opp, opp_table)) {
		new_opp->available = false;
		dev_warn(dev, "%s: OPP not supported by regulators (%lu)\n",
			 __func__, new_opp->rate);
	}

	return 0;
}

/**
 * _opp_add_v1() - Allocate a OPP based on v1 bindings.
 * @opp_table:	OPP table
 * @dev:	device for which we do this operation
 * @freq:	Frequency in Hz for this OPP
 * @u_volt:	Voltage in uVolts for this OPP
 * @dynamic:	Dynamically added OPPs.
 *
 * This function adds an opp definition to the opp table and returns status.
 * The opp is made available by default and it can be controlled using
 * dev_pm_opp_enable/disable functions and may be removed by dev_pm_opp_remove.
 *
 * NOTE: "dynamic" parameter impacts OPPs added by the dev_pm_opp_of_add_table
 * and freed by dev_pm_opp_of_remove_table.
 *
 * Return:
 * 0		On success OR
 *		Duplicate OPPs (both freq and volt are same) and opp->available
 * -EEXIST	Freq are same and volt are different OR
 *		Duplicate OPPs (both freq and volt are same) and !opp->available
 * -ENOMEM	Memory allocation failure
 */
int _opp_add_v1(struct opp_table *opp_table, struct device *dev,
		unsigned long freq, long u_volt, bool dynamic)
{
	struct dev_pm_opp *new_opp;
	unsigned long tol;
	int ret;

	new_opp = _opp_allocate(opp_table);
	if (!new_opp)
		return -ENOMEM;

	/* populate the opp table */
	new_opp->rate = freq;
	tol = u_volt * opp_table->voltage_tolerance_v1 / 100;
	new_opp->supplies[0].u_volt = u_volt;
	new_opp->supplies[0].u_volt_min = u_volt - tol;
	new_opp->supplies[0].u_volt_max = u_volt + tol;
	new_opp->available = true;
	new_opp->dynamic = dynamic;

	ret = _opp_add(dev, new_opp, opp_table, false);
	if (ret) {
		/* Don't return error for duplicate OPPs */
		if (ret == -EBUSY)
			ret = 0;
		goto free_opp;
	}

	/*
	 * Notify the changes in the availability of the operable
	 * frequency/voltage list.
	 */
	blocking_notifier_call_chain(&opp_table->head, OPP_EVENT_ADD, new_opp);
	return 0;

free_opp:
	_opp_free(new_opp);

	return ret;
}

/**
 * dev_pm_opp_set_supported_hw() - Set supported platforms
 * @dev: Device for which supported-hw has to be set.
 * @versions: Array of hierarchy of versions to match.
 * @count: Number of elements in the array.
 *
 * This is required only for the V2 bindings, and it enables a platform to
 * specify the hierarchy of versions it supports. OPP layer will then enable
 * OPPs, which are available for those versions, based on its 'opp-supported-hw'
 * property.
 */
struct opp_table *dev_pm_opp_set_supported_hw(struct device *dev,
			const u32 *versions, unsigned int count)
{
	struct opp_table *opp_table;

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (IS_ERR(opp_table))
		return opp_table;

	/* Make sure there are no concurrent readers while updating opp_table */
	WARN_ON(!list_empty(&opp_table->opp_list));

	/* Another CPU that shares the OPP table has set the property ? */
	if (opp_table->supported_hw)
		return opp_table;

	opp_table->supported_hw = kmemdup(versions, count * sizeof(*versions),
					GFP_KERNEL);
	if (!opp_table->supported_hw) {
		dev_pm_opp_put_opp_table(opp_table);
		return ERR_PTR(-ENOMEM);
	}

	opp_table->supported_hw_count = count;

	return opp_table;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_supported_hw);

/**
 * dev_pm_opp_put_supported_hw() - Releases resources blocked for supported hw
 * @opp_table: OPP table returned by dev_pm_opp_set_supported_hw().
 *
 * This is required only for the V2 bindings, and is called for a matching
 * dev_pm_opp_set_supported_hw(). Until this is called, the opp_table structure
 * will not be freed.
 */
void dev_pm_opp_put_supported_hw(struct opp_table *opp_table)
{
	/* Make sure there are no concurrent readers while updating opp_table */
	WARN_ON(!list_empty(&opp_table->opp_list));

	kfree(opp_table->supported_hw);
	opp_table->supported_hw = NULL;
	opp_table->supported_hw_count = 0;

	dev_pm_opp_put_opp_table(opp_table);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_put_supported_hw);

/**
 * dev_pm_opp_set_prop_name() - Set prop-extn name
 * @dev: Device for which the prop-name has to be set.
 * @name: name to postfix to properties.
 *
 * This is required only for the V2 bindings, and it enables a platform to
 * specify the extn to be used for certain property names. The properties to
 * which the extension will apply are opp-microvolt and opp-microamp. OPP core
 * should postfix the property name with -<name> while looking for them.
 */
struct opp_table *dev_pm_opp_set_prop_name(struct device *dev, const char *name)
{
	struct opp_table *opp_table;

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (IS_ERR(opp_table))
		return opp_table;

	/* Make sure there are no concurrent readers while updating opp_table */
	WARN_ON(!list_empty(&opp_table->opp_list));

	/* Another CPU that shares the OPP table has set the property ? */
	if (opp_table->prop_name)
		return opp_table;

	opp_table->prop_name = kstrdup(name, GFP_KERNEL);
	if (!opp_table->prop_name) {
		dev_pm_opp_put_opp_table(opp_table);
		return ERR_PTR(-ENOMEM);
	}

	return opp_table;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_prop_name);

/**
 * dev_pm_opp_put_prop_name() - Releases resources blocked for prop-name
 * @opp_table: OPP table returned by dev_pm_opp_set_prop_name().
 *
 * This is required only for the V2 bindings, and is called for a matching
 * dev_pm_opp_set_prop_name(). Until this is called, the opp_table structure
 * will not be freed.
 */
void dev_pm_opp_put_prop_name(struct opp_table *opp_table)
{
	/* Make sure there are no concurrent readers while updating opp_table */
	WARN_ON(!list_empty(&opp_table->opp_list));

	kfree(opp_table->prop_name);
	opp_table->prop_name = NULL;

	dev_pm_opp_put_opp_table(opp_table);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_put_prop_name);

static int _allocate_set_opp_data(struct opp_table *opp_table)
{
	struct dev_pm_set_opp_data *data;
	int len, count = opp_table->regulator_count;

	if (WARN_ON(!opp_table->regulators))
		return -EINVAL;

	/* space for set_opp_data */
	len = sizeof(*data);

	/* space for old_opp.supplies and new_opp.supplies */
	len += 2 * sizeof(struct dev_pm_opp_supply) * count;

	data = kzalloc(len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->old_opp.supplies = (void *)(data + 1);
	data->new_opp.supplies = data->old_opp.supplies + count;

	opp_table->set_opp_data = data;

	return 0;
}

static void _free_set_opp_data(struct opp_table *opp_table)
{
	kfree(opp_table->set_opp_data);
	opp_table->set_opp_data = NULL;
}

/**
 * dev_pm_opp_set_regulators() - Set regulator names for the device
 * @dev: Device for which regulator name is being set.
 * @names: Array of pointers to the names of the regulator.
 * @count: Number of regulators.
 *
 * In order to support OPP switching, OPP layer needs to know the name of the
 * device's regulators, as the core would be required to switch voltages as
 * well.
 *
 * This must be called before any OPPs are initialized for the device.
 */
struct opp_table *dev_pm_opp_set_regulators(struct device *dev,
					    const char * const names[],
					    unsigned int count)
{
	struct opp_table *opp_table;
	struct regulator *reg;
	int ret, i;

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (IS_ERR(opp_table))
		return opp_table;

	/* This should be called before OPPs are initialized */
	if (WARN_ON(!list_empty(&opp_table->opp_list))) {
		ret = -EBUSY;
		goto err;
	}

	/* Another CPU that shares the OPP table has set the regulators ? */
	if (opp_table->regulators)
		return opp_table;

	opp_table->regulators = kmalloc_array(count,
					      sizeof(*opp_table->regulators),
					      GFP_KERNEL);
	if (!opp_table->regulators) {
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0; i < count; i++) {
		reg = regulator_get_optional(dev, names[i]);
		if (IS_ERR(reg)) {
			ret = PTR_ERR(reg);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "%s: no regulator (%s) found: %d\n",
					__func__, names[i], ret);
			goto free_regulators;
		}

		opp_table->regulators[i] = reg;
	}

	opp_table->regulator_count = count;

	/* Allocate block only once to pass to set_opp() routines */
	ret = _allocate_set_opp_data(opp_table);
	if (ret)
		goto free_regulators;

	return opp_table;

free_regulators:
	while (i != 0)
		regulator_put(opp_table->regulators[--i]);

	kfree(opp_table->regulators);
	opp_table->regulators = NULL;
	opp_table->regulator_count = -1;
err:
	dev_pm_opp_put_opp_table(opp_table);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_regulators);

/**
 * dev_pm_opp_put_regulators() - Releases resources blocked for regulator
 * @opp_table: OPP table returned from dev_pm_opp_set_regulators().
 */
void dev_pm_opp_put_regulators(struct opp_table *opp_table)
{
	int i;

	if (!opp_table->regulators)
		goto put_opp_table;

	/* Make sure there are no concurrent readers while updating opp_table */
	WARN_ON(!list_empty(&opp_table->opp_list));

	if (opp_table->regulator_enabled) {
		for (i = opp_table->regulator_count - 1; i >= 0; i--)
			regulator_disable(opp_table->regulators[i]);

		opp_table->regulator_enabled = false;
	}

	for (i = opp_table->regulator_count - 1; i >= 0; i--)
		regulator_put(opp_table->regulators[i]);

	_free_set_opp_data(opp_table);

	kfree(opp_table->regulators);
	opp_table->regulators = NULL;
	opp_table->regulator_count = -1;

put_opp_table:
	dev_pm_opp_put_opp_table(opp_table);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_put_regulators);

/**
 * dev_pm_opp_set_clkname() - Set clk name for the device
 * @dev: Device for which clk name is being set.
 * @name: Clk name.
 *
 * In order to support OPP switching, OPP layer needs to get pointer to the
 * clock for the device. Simple cases work fine without using this routine (i.e.
 * by passing connection-id as NULL), but for a device with multiple clocks
 * available, the OPP core needs to know the exact name of the clk to use.
 *
 * This must be called before any OPPs are initialized for the device.
 */
struct opp_table *dev_pm_opp_set_clkname(struct device *dev, const char *name)
{
	struct opp_table *opp_table;
	int ret;

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (IS_ERR(opp_table))
		return opp_table;

	/* This should be called before OPPs are initialized */
	if (WARN_ON(!list_empty(&opp_table->opp_list))) {
		ret = -EBUSY;
		goto err;
	}

	/* Already have default clk set, free it */
	if (!IS_ERR(opp_table->clk))
		clk_put(opp_table->clk);

	/* Find clk for the device */
	opp_table->clk = clk_get(dev, name);
	if (IS_ERR(opp_table->clk)) {
		ret = PTR_ERR(opp_table->clk);
		if (ret != -EPROBE_DEFER) {
			dev_err(dev, "%s: Couldn't find clock: %d\n", __func__,
				ret);
		}
		goto err;
	}

	return opp_table;

err:
	dev_pm_opp_put_opp_table(opp_table);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_clkname);

/**
 * dev_pm_opp_put_clkname() - Releases resources blocked for clk.
 * @opp_table: OPP table returned from dev_pm_opp_set_clkname().
 */
void dev_pm_opp_put_clkname(struct opp_table *opp_table)
{
	/* Make sure there are no concurrent readers while updating opp_table */
	WARN_ON(!list_empty(&opp_table->opp_list));

	clk_put(opp_table->clk);
	opp_table->clk = ERR_PTR(-EINVAL);

	dev_pm_opp_put_opp_table(opp_table);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_put_clkname);

/**
 * dev_pm_opp_register_set_opp_helper() - Register custom set OPP helper
 * @dev: Device for which the helper is getting registered.
 * @set_opp: Custom set OPP helper.
 *
 * This is useful to support complex platforms (like platforms with multiple
 * regulators per device), instead of the generic OPP set rate helper.
 *
 * This must be called before any OPPs are initialized for the device.
 */
struct opp_table *dev_pm_opp_register_set_opp_helper(struct device *dev,
			int (*set_opp)(struct dev_pm_set_opp_data *data))
{
	struct opp_table *opp_table;

	if (!set_opp)
		return ERR_PTR(-EINVAL);

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (!IS_ERR(opp_table))
		return opp_table;

	/* This should be called before OPPs are initialized */
	if (WARN_ON(!list_empty(&opp_table->opp_list))) {
		dev_pm_opp_put_opp_table(opp_table);
		return ERR_PTR(-EBUSY);
	}

	/* Another CPU that shares the OPP table has set the helper ? */
	if (!opp_table->set_opp)
		opp_table->set_opp = set_opp;

	return opp_table;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_register_set_opp_helper);

/**
 * dev_pm_opp_unregister_set_opp_helper() - Releases resources blocked for
 *					   set_opp helper
 * @opp_table: OPP table returned from dev_pm_opp_register_set_opp_helper().
 *
 * Release resources blocked for platform specific set_opp helper.
 */
void dev_pm_opp_unregister_set_opp_helper(struct opp_table *opp_table)
{
	/* Make sure there are no concurrent readers while updating opp_table */
	WARN_ON(!list_empty(&opp_table->opp_list));

	opp_table->set_opp = NULL;
	dev_pm_opp_put_opp_table(opp_table);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_unregister_set_opp_helper);

static void _opp_detach_genpd(struct opp_table *opp_table)
{
	int index;

	for (index = 0; index < opp_table->required_opp_count; index++) {
		if (!opp_table->genpd_virt_devs[index])
			continue;

		dev_pm_domain_detach(opp_table->genpd_virt_devs[index], false);
		opp_table->genpd_virt_devs[index] = NULL;
	}

	kfree(opp_table->genpd_virt_devs);
	opp_table->genpd_virt_devs = NULL;
}

/**
 * dev_pm_opp_attach_genpd - Attach genpd(s) for the device and save virtual device pointer
 * @dev: Consumer device for which the genpd is getting attached.
 * @names: Null terminated array of pointers containing names of genpd to attach.
 * @virt_devs: Pointer to return the array of virtual devices.
 *
 * Multiple generic power domains for a device are supported with the help of
 * virtual genpd devices, which are created for each consumer device - genpd
 * pair. These are the device structures which are attached to the power domain
 * and are required by the OPP core to set the performance state of the genpd.
 * The same API also works for the case where single genpd is available and so
 * we don't need to support that separately.
 *
 * This helper will normally be called by the consumer driver of the device
 * "dev", as only that has details of the genpd names.
 *
 * This helper needs to be called once with a list of all genpd to attach.
 * Otherwise the original device structure will be used instead by the OPP core.
 *
 * The order of entries in the names array must match the order in which
 * "required-opps" are added in DT.
 */
struct opp_table *dev_pm_opp_attach_genpd(struct device *dev,
		const char **names, struct device ***virt_devs)
{
	struct opp_table *opp_table;
	struct device *virt_dev;
	int index = 0, ret = -EINVAL;
	const char **name = names;

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (IS_ERR(opp_table))
		return opp_table;

	/*
	 * If the genpd's OPP table isn't already initialized, parsing of the
	 * required-opps fail for dev. We should retry this after genpd's OPP
	 * table is added.
	 */
	if (!opp_table->required_opp_count) {
		ret = -EPROBE_DEFER;
		goto put_table;
	}

	mutex_lock(&opp_table->genpd_virt_dev_lock);

	opp_table->genpd_virt_devs = kcalloc(opp_table->required_opp_count,
					     sizeof(*opp_table->genpd_virt_devs),
					     GFP_KERNEL);
	if (!opp_table->genpd_virt_devs)
		goto unlock;

	while (*name) {
		if (index >= opp_table->required_opp_count) {
			dev_err(dev, "Index can't be greater than required-opp-count - 1, %s (%d : %d)\n",
				*name, opp_table->required_opp_count, index);
			goto err;
		}

		if (opp_table->genpd_virt_devs[index]) {
			dev_err(dev, "Genpd virtual device already set %s\n",
				*name);
			goto err;
		}

		virt_dev = dev_pm_domain_attach_by_name(dev, *name);
		if (IS_ERR(virt_dev)) {
			ret = PTR_ERR(virt_dev);
			dev_err(dev, "Couldn't attach to pm_domain: %d\n", ret);
			goto err;
		}

		opp_table->genpd_virt_devs[index] = virt_dev;
		index++;
		name++;
	}

	if (virt_devs)
		*virt_devs = opp_table->genpd_virt_devs;
	mutex_unlock(&opp_table->genpd_virt_dev_lock);

	return opp_table;

err:
	_opp_detach_genpd(opp_table);
unlock:
	mutex_unlock(&opp_table->genpd_virt_dev_lock);

put_table:
	dev_pm_opp_put_opp_table(opp_table);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_attach_genpd);

/**
 * dev_pm_opp_detach_genpd() - Detach genpd(s) from the device.
 * @opp_table: OPP table returned by dev_pm_opp_attach_genpd().
 *
 * This detaches the genpd(s), resets the virtual device pointers, and puts the
 * OPP table.
 */
void dev_pm_opp_detach_genpd(struct opp_table *opp_table)
{
	/*
	 * Acquire genpd_virt_dev_lock to make sure virt_dev isn't getting
	 * used in parallel.
	 */
	mutex_lock(&opp_table->genpd_virt_dev_lock);
	_opp_detach_genpd(opp_table);
	mutex_unlock(&opp_table->genpd_virt_dev_lock);

	dev_pm_opp_put_opp_table(opp_table);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_detach_genpd);

/**
 * dev_pm_opp_xlate_performance_state() - Find required OPP's pstate for src_table.
 * @src_table: OPP table which has dst_table as one of its required OPP table.
 * @dst_table: Required OPP table of the src_table.
 * @pstate: Current performance state of the src_table.
 *
 * This Returns pstate of the OPP (present in @dst_table) pointed out by the
 * "required-opps" property of the OPP (present in @src_table) which has
 * performance state set to @pstate.
 *
 * Return: Zero or positive performance state on success, otherwise negative
 * value on errors.
 */
int dev_pm_opp_xlate_performance_state(struct opp_table *src_table,
				       struct opp_table *dst_table,
				       unsigned int pstate)
{
	struct dev_pm_opp *opp;
	int dest_pstate = -EINVAL;
	int i;

	if (!pstate)
		return 0;

	/*
	 * Normally the src_table will have the "required_opps" property set to
	 * point to one of the OPPs in the dst_table, but in some cases the
	 * genpd and its master have one to one mapping of performance states
	 * and so none of them have the "required-opps" property set. Return the
	 * pstate of the src_table as it is in such cases.
	 */
	if (!src_table->required_opp_count)
		return pstate;

	for (i = 0; i < src_table->required_opp_count; i++) {
		if (src_table->required_opp_tables[i]->np == dst_table->np)
			break;
	}

	if (unlikely(i == src_table->required_opp_count)) {
		pr_err("%s: Couldn't find matching OPP table (%p: %p)\n",
		       __func__, src_table, dst_table);
		return -EINVAL;
	}

	mutex_lock(&src_table->lock);

	list_for_each_entry(opp, &src_table->opp_list, node) {
		if (opp->pstate == pstate) {
			dest_pstate = opp->required_opps[i]->pstate;
			goto unlock;
		}
	}

	pr_err("%s: Couldn't find matching OPP (%p: %p)\n", __func__, src_table,
	       dst_table);

unlock:
	mutex_unlock(&src_table->lock);

	return dest_pstate;
}

/**
 * dev_pm_opp_add()  - Add an OPP table from a table definitions
 * @dev:	device for which we do this operation
 * @freq:	Frequency in Hz for this OPP
 * @u_volt:	Voltage in uVolts for this OPP
 *
 * This function adds an opp definition to the opp table and returns status.
 * The opp is made available by default and it can be controlled using
 * dev_pm_opp_enable/disable functions.
 *
 * Return:
 * 0		On success OR
 *		Duplicate OPPs (both freq and volt are same) and opp->available
 * -EEXIST	Freq are same and volt are different OR
 *		Duplicate OPPs (both freq and volt are same) and !opp->available
 * -ENOMEM	Memory allocation failure
 */
int dev_pm_opp_add(struct device *dev, unsigned long freq, unsigned long u_volt)
{
	struct opp_table *opp_table;
	int ret;

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	/* Fix regulator count for dynamic OPPs */
	opp_table->regulator_count = 1;

	ret = _opp_add_v1(opp_table, dev, freq, u_volt, true);
	if (ret)
		dev_pm_opp_put_opp_table(opp_table);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_add);

/**
 * _opp_set_availability() - helper to set the availability of an opp
 * @dev:		device for which we do this operation
 * @freq:		OPP frequency to modify availability
 * @availability_req:	availability status requested for this opp
 *
 * Set the availability of an OPP, opp_{enable,disable} share a common logic
 * which is isolated here.
 *
 * Return: -EINVAL for bad pointers, -ENOMEM if no memory available for the
 * copy operation, returns 0 if no modification was done OR modification was
 * successful.
 */
static int _opp_set_availability(struct device *dev, unsigned long freq,
				 bool availability_req)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *tmp_opp, *opp = ERR_PTR(-ENODEV);
	int r = 0;

	/* Find the opp_table */
	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		r = PTR_ERR(opp_table);
		dev_warn(dev, "%s: Device OPP not found (%d)\n", __func__, r);
		return r;
	}

	mutex_lock(&opp_table->lock);

	/* Do we have the frequency? */
	list_for_each_entry(tmp_opp, &opp_table->opp_list, node) {
		if (tmp_opp->rate == freq) {
			opp = tmp_opp;
			break;
		}
	}

	if (IS_ERR(opp)) {
		r = PTR_ERR(opp);
		goto unlock;
	}

	/* Is update really needed? */
	if (opp->available == availability_req)
		goto unlock;

	opp->available = availability_req;

	dev_pm_opp_get(opp);
	mutex_unlock(&opp_table->lock);

	/* Notify the change of the OPP availability */
	if (availability_req)
		blocking_notifier_call_chain(&opp_table->head, OPP_EVENT_ENABLE,
					     opp);
	else
		blocking_notifier_call_chain(&opp_table->head,
					     OPP_EVENT_DISABLE, opp);

	dev_pm_opp_put(opp);
	goto put_table;

unlock:
	mutex_unlock(&opp_table->lock);
put_table:
	dev_pm_opp_put_opp_table(opp_table);
	return r;
}

/**
 * dev_pm_opp_adjust_voltage() - helper to change the voltage of an OPP
 * @dev:		device for which we do this operation
 * @freq:		OPP frequency to adjust voltage of
 * @u_volt:		new OPP target voltage
 * @u_volt_min:		new OPP min voltage
 * @u_volt_max:		new OPP max voltage
 *
 * Return: -EINVAL for bad pointers, -ENOMEM if no memory available for the
 * copy operation, returns 0 if no modifcation was done OR modification was
 * successful.
 */
int dev_pm_opp_adjust_voltage(struct device *dev, unsigned long freq,
			      unsigned long u_volt, unsigned long u_volt_min,
			      unsigned long u_volt_max)

{
	struct opp_table *opp_table;
	struct dev_pm_opp *tmp_opp, *opp = ERR_PTR(-ENODEV);
	int r = 0;

	/* Find the opp_table */
	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		r = PTR_ERR(opp_table);
		dev_warn(dev, "%s: Device OPP not found (%d)\n", __func__, r);
		return r;
	}

	mutex_lock(&opp_table->lock);

	/* Do we have the frequency? */
	list_for_each_entry(tmp_opp, &opp_table->opp_list, node) {
		if (tmp_opp->rate == freq) {
			opp = tmp_opp;
			break;
		}
	}

	if (IS_ERR(opp)) {
		r = PTR_ERR(opp);
		goto adjust_unlock;
	}

	/* Is update really needed? */
	if (opp->supplies->u_volt == u_volt)
		goto adjust_unlock;

	opp->supplies->u_volt = u_volt;
	opp->supplies->u_volt_min = u_volt_min;
	opp->supplies->u_volt_max = u_volt_max;

	dev_pm_opp_get(opp);
	mutex_unlock(&opp_table->lock);

	/* Notify the voltage change of the OPP */
	blocking_notifier_call_chain(&opp_table->head, OPP_EVENT_ADJUST_VOLTAGE,
				     opp);

	dev_pm_opp_put(opp);
	goto adjust_put_table;

adjust_unlock:
	mutex_unlock(&opp_table->lock);
adjust_put_table:
	dev_pm_opp_put_opp_table(opp_table);
	return r;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_adjust_voltage);

/**
 * dev_pm_opp_enable() - Enable a specific OPP
 * @dev:	device for which we do this operation
 * @freq:	OPP frequency to enable
 *
 * Enables a provided opp. If the operation is valid, this returns 0, else the
 * corresponding error value. It is meant to be used for users an OPP available
 * after being temporarily made unavailable with dev_pm_opp_disable.
 *
 * Return: -EINVAL for bad pointers, -ENOMEM if no memory available for the
 * copy operation, returns 0 if no modification was done OR modification was
 * successful.
 */
int dev_pm_opp_enable(struct device *dev, unsigned long freq)
{
	return _opp_set_availability(dev, freq, true);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_enable);

/**
 * dev_pm_opp_disable() - Disable a specific OPP
 * @dev:	device for which we do this operation
 * @freq:	OPP frequency to disable
 *
 * Disables a provided opp. If the operation is valid, this returns
 * 0, else the corresponding error value. It is meant to be a temporary
 * control by users to make this OPP not available until the circumstances are
 * right to make it available again (with a call to dev_pm_opp_enable).
 *
 * Return: -EINVAL for bad pointers, -ENOMEM if no memory available for the
 * copy operation, returns 0 if no modification was done OR modification was
 * successful.
 */
int dev_pm_opp_disable(struct device *dev, unsigned long freq)
{
	return _opp_set_availability(dev, freq, false);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_disable);

/**
 * dev_pm_opp_register_notifier() - Register OPP notifier for the device
 * @dev:	Device for which notifier needs to be registered
 * @nb:		Notifier block to be registered
 *
 * Return: 0 on success or a negative error value.
 */
int dev_pm_opp_register_notifier(struct device *dev, struct notifier_block *nb)
{
	struct opp_table *opp_table;
	int ret;

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	ret = blocking_notifier_chain_register(&opp_table->head, nb);

	dev_pm_opp_put_opp_table(opp_table);

	return ret;
}
EXPORT_SYMBOL(dev_pm_opp_register_notifier);

/**
 * dev_pm_opp_unregister_notifier() - Unregister OPP notifier for the device
 * @dev:	Device for which notifier needs to be unregistered
 * @nb:		Notifier block to be unregistered
 *
 * Return: 0 on success or a negative error value.
 */
int dev_pm_opp_unregister_notifier(struct device *dev,
				   struct notifier_block *nb)
{
	struct opp_table *opp_table;
	int ret;

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	ret = blocking_notifier_chain_unregister(&opp_table->head, nb);

	dev_pm_opp_put_opp_table(opp_table);

	return ret;
}
EXPORT_SYMBOL(dev_pm_opp_unregister_notifier);

void _dev_pm_opp_find_and_remove_table(struct device *dev)
{
	struct opp_table *opp_table;

	/* Check for existing table for 'dev' */
	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		int error = PTR_ERR(opp_table);

		if (error != -ENODEV)
			WARN(1, "%s: opp_table: %d\n",
			     IS_ERR_OR_NULL(dev) ?
					"Invalid device" : dev_name(dev),
			     error);
		return;
	}

	/*
	 * Drop the extra reference only if the OPP table was successfully added
	 * with dev_pm_opp_of_add_table() earlier.
	 **/
	if (_opp_remove_all_static(opp_table))
		dev_pm_opp_put_opp_table(opp_table);

	/* Drop reference taken by _find_opp_table() */
	dev_pm_opp_put_opp_table(opp_table);
}

/**
 * dev_pm_opp_remove_table() - Free all OPPs associated with the device
 * @dev:	device pointer used to lookup OPP table.
 *
 * Free both OPPs created using static entries present in DT and the
 * dynamically added entries.
 */
void dev_pm_opp_remove_table(struct device *dev)
{
	_dev_pm_opp_find_and_remove_table(dev);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_remove_table);
