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

/* OPP tables with uninitialized required OPPs */
LIST_HEAD(lazy_opp_tables);

/* Lock to allow exclusive modification to the device and opp lists */
DEFINE_MUTEX(opp_table_lock);
/* Flag indicating that opp_tables list is being updated at the moment */
static bool opp_tables_busy;

static bool _find_opp_dev(const struct device *dev, struct opp_table *opp_table)
{
	struct opp_device *opp_dev;
	bool found = false;

	mutex_lock(&opp_table->lock);
	list_for_each_entry(opp_dev, &opp_table->dev_list, node)
		if (opp_dev->dev == dev) {
			found = true;
			break;
		}

	mutex_unlock(&opp_table->lock);
	return found;
}

static struct opp_table *_find_opp_table_unlocked(struct device *dev)
{
	struct opp_table *opp_table;

	list_for_each_entry(opp_table, &opp_tables, node) {
		if (_find_opp_dev(dev, opp_table)) {
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
 * dev_pm_opp_get_required_pstate() - Gets the required performance state
 *                                    corresponding to an available opp
 * @opp:	opp for which performance state has to be returned for
 * @index:	index of the required opp
 *
 * Return: performance state read from device tree corresponding to the
 * required opp, else return 0.
 */
unsigned int dev_pm_opp_get_required_pstate(struct dev_pm_opp *opp,
					    unsigned int index)
{
	if (IS_ERR_OR_NULL(opp) || !opp->available ||
	    index >= opp->opp_table->required_opp_count) {
		pr_err("%s: Invalid parameters\n", __func__);
		return 0;
	}

	/* required-opps not fully initialized yet */
	if (lazy_linking_pending(opp->opp_table))
		return 0;

	return opp->required_opps[index]->pstate;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_required_pstate);

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

/**
 * dev_pm_opp_find_level_ceil() - search for an rounded up level
 * @dev:		device for which we do this operation
 * @level:		level to search for
 *
 * Return: Searches for rounded up match in the opp table and returns pointer
 * to the  matching opp if found, else returns ERR_PTR in case of error and
 * should be handled using IS_ERR. Error return values can be:
 * EINVAL:	for bad pointer
 * ERANGE:	no match found for search
 * ENODEV:	if device not found in list of registered devices
 *
 * The callers are required to call dev_pm_opp_put() for the returned OPP after
 * use.
 */
struct dev_pm_opp *dev_pm_opp_find_level_ceil(struct device *dev,
					      unsigned int *level)
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
		if (temp_opp->available && temp_opp->level >= *level) {
			opp = temp_opp;
			*level = opp->level;

			/* Increment the reference count of OPP */
			dev_pm_opp_get(opp);
			break;
		}
	}

	mutex_unlock(&opp_table->lock);
	dev_pm_opp_put_opp_table(opp_table);

	return opp;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_find_level_ceil);

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

	/* We may reach here for devices which don't change frequency */
	if (IS_ERR(clk))
		return 0;

	ret = clk_set_rate(clk, freq);
	if (ret) {
		dev_err(dev, "%s: failed to set clock rate: %d\n", __func__,
			ret);
	}

	return ret;
}

static int _generic_set_opp_regulator(struct opp_table *opp_table,
				      struct device *dev,
				      struct dev_pm_opp *opp,
				      unsigned long freq,
				      int scaling_down)
{
	struct regulator *reg = opp_table->regulators[0];
	struct dev_pm_opp *old_opp = opp_table->current_opp;
	int ret;

	/* This function only supports single regulator per device */
	if (WARN_ON(opp_table->regulator_count > 1)) {
		dev_err(dev, "multiple regulators are not supported\n");
		return -EINVAL;
	}

	/* Scaling up? Scale voltage before frequency */
	if (!scaling_down) {
		ret = _set_opp_voltage(dev, reg, opp->supplies);
		if (ret)
			goto restore_voltage;
	}

	/* Change frequency */
	ret = _generic_set_opp_clk_only(dev, opp_table->clk, freq);
	if (ret)
		goto restore_voltage;

	/* Scaling down? Scale voltage after frequency */
	if (scaling_down) {
		ret = _set_opp_voltage(dev, reg, opp->supplies);
		if (ret)
			goto restore_freq;
	}

	/*
	 * Enable the regulator after setting its voltages, otherwise it breaks
	 * some boot-enabled regulators.
	 */
	if (unlikely(!opp_table->enabled)) {
		ret = regulator_enable(reg);
		if (ret < 0)
			dev_warn(dev, "Failed to enable regulator: %d", ret);
	}

	return 0;

restore_freq:
	if (_generic_set_opp_clk_only(dev, opp_table->clk, old_opp->rate))
		dev_err(dev, "%s: failed to restore old-freq (%lu Hz)\n",
			__func__, old_opp->rate);
restore_voltage:
	/* This shouldn't harm even if the voltages weren't updated earlier */
	_set_opp_voltage(dev, reg, old_opp->supplies);

	return ret;
}

static int _set_opp_bw(const struct opp_table *opp_table,
		       struct dev_pm_opp *opp, struct device *dev)
{
	u32 avg, peak;
	int i, ret;

	if (!opp_table->paths)
		return 0;

	for (i = 0; i < opp_table->path_count; i++) {
		if (!opp) {
			avg = 0;
			peak = 0;
		} else {
			avg = opp->bandwidth[i].avg;
			peak = opp->bandwidth[i].peak;
		}
		ret = icc_set_bw(opp_table->paths[i], avg, peak);
		if (ret) {
			dev_err(dev, "Failed to %s bandwidth[%d]: %d\n",
				opp ? "set" : "remove", i, ret);
			return ret;
		}
	}

	return 0;
}

static int _set_opp_custom(const struct opp_table *opp_table,
			   struct device *dev, struct dev_pm_opp *opp,
			   unsigned long freq)
{
	struct dev_pm_set_opp_data *data = opp_table->set_opp_data;
	struct dev_pm_opp *old_opp = opp_table->current_opp;
	int size;

	/*
	 * We support this only if dev_pm_opp_set_regulators() was called
	 * earlier.
	 */
	if (opp_table->sod_supplies) {
		size = sizeof(*old_opp->supplies) * opp_table->regulator_count;
		memcpy(data->old_opp.supplies, old_opp->supplies, size);
		memcpy(data->new_opp.supplies, opp->supplies, size);
		data->regulator_count = opp_table->regulator_count;
	} else {
		data->regulator_count = 0;
	}

	data->regulators = opp_table->regulators;
	data->clk = opp_table->clk;
	data->dev = dev;
	data->old_opp.rate = old_opp->rate;
	data->new_opp.rate = freq;

	return opp_table->set_opp(data);
}

static int _set_required_opp(struct device *dev, struct device *pd_dev,
			     struct dev_pm_opp *opp, int i)
{
	unsigned int pstate = likely(opp) ? opp->required_opps[i]->pstate : 0;
	int ret;

	if (!pd_dev)
		return 0;

	ret = dev_pm_genpd_set_performance_state(pd_dev, pstate);
	if (ret) {
		dev_err(dev, "Failed to set performance rate of %s: %d (%d)\n",
			dev_name(pd_dev), pstate, ret);
	}

	return ret;
}

/* This is only called for PM domain for now */
static int _set_required_opps(struct device *dev,
			      struct opp_table *opp_table,
			      struct dev_pm_opp *opp, bool up)
{
	struct opp_table **required_opp_tables = opp_table->required_opp_tables;
	struct device **genpd_virt_devs = opp_table->genpd_virt_devs;
	int i, ret = 0;

	if (!required_opp_tables)
		return 0;

	/* required-opps not fully initialized yet */
	if (lazy_linking_pending(opp_table))
		return -EBUSY;

	/*
	 * We only support genpd's OPPs in the "required-opps" for now, as we
	 * don't know much about other use cases. Error out if the required OPP
	 * doesn't belong to a genpd.
	 */
	if (unlikely(!required_opp_tables[0]->is_genpd)) {
		dev_err(dev, "required-opps don't belong to a genpd\n");
		return -ENOENT;
	}

	/* Single genpd case */
	if (!genpd_virt_devs)
		return _set_required_opp(dev, dev, opp, 0);

	/* Multiple genpd case */

	/*
	 * Acquire genpd_virt_dev_lock to make sure we don't use a genpd_dev
	 * after it is freed from another thread.
	 */
	mutex_lock(&opp_table->genpd_virt_dev_lock);

	/* Scaling up? Set required OPPs in normal order, else reverse */
	if (up) {
		for (i = 0; i < opp_table->required_opp_count; i++) {
			ret = _set_required_opp(dev, genpd_virt_devs[i], opp, i);
			if (ret)
				break;
		}
	} else {
		for (i = opp_table->required_opp_count - 1; i >= 0; i--) {
			ret = _set_required_opp(dev, genpd_virt_devs[i], opp, i);
			if (ret)
				break;
		}
	}

	mutex_unlock(&opp_table->genpd_virt_dev_lock);

	return ret;
}

static void _find_current_opp(struct device *dev, struct opp_table *opp_table)
{
	struct dev_pm_opp *opp = ERR_PTR(-ENODEV);
	unsigned long freq;

	if (!IS_ERR(opp_table->clk)) {
		freq = clk_get_rate(opp_table->clk);
		opp = _find_freq_ceil(opp_table, &freq);
	}

	/*
	 * Unable to find the current OPP ? Pick the first from the list since
	 * it is in ascending order, otherwise rest of the code will need to
	 * make special checks to validate current_opp.
	 */
	if (IS_ERR(opp)) {
		mutex_lock(&opp_table->lock);
		opp = list_first_entry(&opp_table->opp_list, struct dev_pm_opp, node);
		dev_pm_opp_get(opp);
		mutex_unlock(&opp_table->lock);
	}

	opp_table->current_opp = opp;
}

static int _disable_opp_table(struct device *dev, struct opp_table *opp_table)
{
	int ret;

	if (!opp_table->enabled)
		return 0;

	/*
	 * Some drivers need to support cases where some platforms may
	 * have OPP table for the device, while others don't and
	 * opp_set_rate() just needs to behave like clk_set_rate().
	 */
	if (!_get_opp_count(opp_table))
		return 0;

	ret = _set_opp_bw(opp_table, NULL, dev);
	if (ret)
		return ret;

	if (opp_table->regulators)
		regulator_disable(opp_table->regulators[0]);

	ret = _set_required_opps(dev, opp_table, NULL, false);

	opp_table->enabled = false;
	return ret;
}

static int _set_opp(struct device *dev, struct opp_table *opp_table,
		    struct dev_pm_opp *opp, unsigned long freq)
{
	struct dev_pm_opp *old_opp;
	int scaling_down, ret;

	if (unlikely(!opp))
		return _disable_opp_table(dev, opp_table);

	/* Find the currently set OPP if we don't know already */
	if (unlikely(!opp_table->current_opp))
		_find_current_opp(dev, opp_table);

	old_opp = opp_table->current_opp;

	/* Return early if nothing to do */
	if (old_opp == opp && opp_table->current_rate == freq &&
	    opp_table->enabled) {
		dev_dbg(dev, "%s: OPPs are same, nothing to do\n", __func__);
		return 0;
	}

	dev_dbg(dev, "%s: switching OPP: Freq %lu -> %lu Hz, Level %u -> %u, Bw %u -> %u\n",
		__func__, opp_table->current_rate, freq, old_opp->level,
		opp->level, old_opp->bandwidth ? old_opp->bandwidth[0].peak : 0,
		opp->bandwidth ? opp->bandwidth[0].peak : 0);

	scaling_down = _opp_compare_key(old_opp, opp);
	if (scaling_down == -1)
		scaling_down = 0;

	/* Scaling up? Configure required OPPs before frequency */
	if (!scaling_down) {
		ret = _set_required_opps(dev, opp_table, opp, true);
		if (ret) {
			dev_err(dev, "Failed to set required opps: %d\n", ret);
			return ret;
		}

		ret = _set_opp_bw(opp_table, opp, dev);
		if (ret) {
			dev_err(dev, "Failed to set bw: %d\n", ret);
			return ret;
		}
	}

	if (opp_table->set_opp) {
		ret = _set_opp_custom(opp_table, dev, opp, freq);
	} else if (opp_table->regulators) {
		ret = _generic_set_opp_regulator(opp_table, dev, opp, freq,
						 scaling_down);
	} else {
		/* Only frequency scaling */
		ret = _generic_set_opp_clk_only(dev, opp_table->clk, freq);
	}

	if (ret)
		return ret;

	/* Scaling down? Configure required OPPs after frequency */
	if (scaling_down) {
		ret = _set_opp_bw(opp_table, opp, dev);
		if (ret) {
			dev_err(dev, "Failed to set bw: %d\n", ret);
			return ret;
		}

		ret = _set_required_opps(dev, opp_table, opp, false);
		if (ret) {
			dev_err(dev, "Failed to set required opps: %d\n", ret);
			return ret;
		}
	}

	opp_table->enabled = true;
	dev_pm_opp_put(old_opp);

	/* Make sure current_opp doesn't get freed */
	dev_pm_opp_get(opp);
	opp_table->current_opp = opp;
	opp_table->current_rate = freq;

	return ret;
}

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
	unsigned long freq = 0, temp_freq;
	struct dev_pm_opp *opp = NULL;
	int ret;

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		dev_err(dev, "%s: device's opp table doesn't exist\n", __func__);
		return PTR_ERR(opp_table);
	}

	if (target_freq) {
		/*
		 * For IO devices which require an OPP on some platforms/SoCs
		 * while just needing to scale the clock on some others
		 * we look for empty OPP tables with just a clock handle and
		 * scale only the clk. This makes dev_pm_opp_set_rate()
		 * equivalent to a clk_set_rate()
		 */
		if (!_get_opp_count(opp_table)) {
			ret = _generic_set_opp_clk_only(dev, opp_table->clk, target_freq);
			goto put_opp_table;
		}

		freq = clk_round_rate(opp_table->clk, target_freq);
		if ((long)freq <= 0)
			freq = target_freq;

		/*
		 * The clock driver may support finer resolution of the
		 * frequencies than the OPP table, don't update the frequency we
		 * pass to clk_set_rate() here.
		 */
		temp_freq = freq;
		opp = _find_freq_ceil(opp_table, &temp_freq);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			dev_err(dev, "%s: failed to find OPP for freq %lu (%d)\n",
				__func__, freq, ret);
			goto put_opp_table;
		}
	}

	ret = _set_opp(dev, opp_table, opp, freq);

	if (target_freq)
		dev_pm_opp_put(opp);
put_opp_table:
	dev_pm_opp_put_opp_table(opp_table);
	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_rate);

/**
 * dev_pm_opp_set_opp() - Configure device for OPP
 * @dev: device for which we do this operation
 * @opp: OPP to set to
 *
 * This configures the device based on the properties of the OPP passed to this
 * routine.
 *
 * Return: 0 on success, a negative error number otherwise.
 */
int dev_pm_opp_set_opp(struct device *dev, struct dev_pm_opp *opp)
{
	struct opp_table *opp_table;
	int ret;

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		dev_err(dev, "%s: device opp doesn't exist\n", __func__);
		return PTR_ERR(opp_table);
	}

	ret = _set_opp(dev, opp_table, opp, opp ? opp->rate : 0);
	dev_pm_opp_put_opp_table(opp_table);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_opp);

/* OPP-dev Helpers */
static void _remove_opp_dev(struct opp_device *opp_dev,
			    struct opp_table *opp_table)
{
	opp_debug_unregister(opp_dev, opp_table);
	list_del(&opp_dev->node);
	kfree(opp_dev);
}

struct opp_device *_add_opp_dev(const struct device *dev,
				struct opp_table *opp_table)
{
	struct opp_device *opp_dev;

	opp_dev = kzalloc(sizeof(*opp_dev), GFP_KERNEL);
	if (!opp_dev)
		return NULL;

	/* Initialize opp-dev */
	opp_dev->dev = dev;

	mutex_lock(&opp_table->lock);
	list_add(&opp_dev->node, &opp_table->dev_list);
	mutex_unlock(&opp_table->lock);

	/* Create debugfs entries for the opp_table */
	opp_debug_register(opp_dev, opp_table);

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
	INIT_LIST_HEAD(&opp_table->lazy);

	/* Mark regulator count uninitialized */
	opp_table->regulator_count = -1;

	opp_dev = _add_opp_dev(dev, opp_table);
	if (!opp_dev) {
		ret = -ENOMEM;
		goto err;
	}

	_of_init_opp_table(opp_table, dev, index);

	/* Find interconnect path(s) for the device */
	ret = dev_pm_opp_of_find_icc_paths(dev, opp_table);
	if (ret) {
		if (ret == -EPROBE_DEFER)
			goto remove_opp_dev;

		dev_warn(dev, "%s: Error finding interconnect paths: %d\n",
			 __func__, ret);
	}

	BLOCKING_INIT_NOTIFIER_HEAD(&opp_table->head);
	INIT_LIST_HEAD(&opp_table->opp_list);
	kref_init(&opp_table->kref);

	return opp_table;

remove_opp_dev:
	_remove_opp_dev(opp_dev, opp_table);
err:
	kfree(opp_table);
	return ERR_PTR(ret);
}

void _get_opp_table_kref(struct opp_table *opp_table)
{
	kref_get(&opp_table->kref);
}

static struct opp_table *_update_opp_table_clk(struct device *dev,
					       struct opp_table *opp_table,
					       bool getclk)
{
	int ret;

	/*
	 * Return early if we don't need to get clk or we have already tried it
	 * earlier.
	 */
	if (!getclk || IS_ERR(opp_table) || opp_table->clk)
		return opp_table;

	/* Find clk for the device */
	opp_table->clk = clk_get(dev, NULL);

	ret = PTR_ERR_OR_ZERO(opp_table->clk);
	if (!ret)
		return opp_table;

	if (ret == -ENOENT) {
		dev_dbg(dev, "%s: Couldn't find clock: %d\n", __func__, ret);
		return opp_table;
	}

	dev_pm_opp_put_opp_table(opp_table);
	dev_err_probe(dev, ret, "Couldn't find clock\n");

	return ERR_PTR(ret);
}

/*
 * We need to make sure that the OPP table for a device doesn't get added twice,
 * if this routine gets called in parallel with the same device pointer.
 *
 * The simplest way to enforce that is to perform everything (find existing
 * table and if not found, create a new one) under the opp_table_lock, so only
 * one creator gets access to the same. But that expands the critical section
 * under the lock and may end up causing circular dependencies with frameworks
 * like debugfs, interconnect or clock framework as they may be direct or
 * indirect users of OPP core.
 *
 * And for that reason we have to go for a bit tricky implementation here, which
 * uses the opp_tables_busy flag to indicate if another creator is in the middle
 * of adding an OPP table and others should wait for it to finish.
 */
struct opp_table *_add_opp_table_indexed(struct device *dev, int index,
					 bool getclk)
{
	struct opp_table *opp_table;

again:
	mutex_lock(&opp_table_lock);

	opp_table = _find_opp_table_unlocked(dev);
	if (!IS_ERR(opp_table))
		goto unlock;

	/*
	 * The opp_tables list or an OPP table's dev_list is getting updated by
	 * another user, wait for it to finish.
	 */
	if (unlikely(opp_tables_busy)) {
		mutex_unlock(&opp_table_lock);
		cpu_relax();
		goto again;
	}

	opp_tables_busy = true;
	opp_table = _managed_opp(dev, index);

	/* Drop the lock to reduce the size of critical section */
	mutex_unlock(&opp_table_lock);

	if (opp_table) {
		if (!_add_opp_dev(dev, opp_table)) {
			dev_pm_opp_put_opp_table(opp_table);
			opp_table = ERR_PTR(-ENOMEM);
		}

		mutex_lock(&opp_table_lock);
	} else {
		opp_table = _allocate_opp_table(dev, index);

		mutex_lock(&opp_table_lock);
		if (!IS_ERR(opp_table))
			list_add(&opp_table->node, &opp_tables);
	}

	opp_tables_busy = false;

unlock:
	mutex_unlock(&opp_table_lock);

	return _update_opp_table_clk(dev, opp_table, getclk);
}

static struct opp_table *_add_opp_table(struct device *dev, bool getclk)
{
	return _add_opp_table_indexed(dev, 0, getclk);
}

struct opp_table *dev_pm_opp_get_opp_table(struct device *dev)
{
	return _find_opp_table(dev);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_opp_table);

static void _opp_table_kref_release(struct kref *kref)
{
	struct opp_table *opp_table = container_of(kref, struct opp_table, kref);
	struct opp_device *opp_dev, *temp;
	int i;

	/* Drop the lock as soon as we can */
	list_del(&opp_table->node);
	mutex_unlock(&opp_table_lock);

	if (opp_table->current_opp)
		dev_pm_opp_put(opp_table->current_opp);

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
	kfree(opp_table);
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

static void _opp_kref_release(struct kref *kref)
{
	struct dev_pm_opp *opp = container_of(kref, struct dev_pm_opp, kref);
	struct opp_table *opp_table = opp->opp_table;

	list_del(&opp->node);
	mutex_unlock(&opp_table->lock);

	/*
	 * Notify the changes in the availability of the operable
	 * frequency/voltage list.
	 */
	blocking_notifier_call_chain(&opp_table->head, OPP_EVENT_REMOVE, opp);
	_of_opp_free_required_opps(opp_table, opp);
	opp_debug_remove_one(opp);
	kfree(opp);
}

void dev_pm_opp_get(struct dev_pm_opp *opp)
{
	kref_get(&opp->kref);
}

void dev_pm_opp_put(struct dev_pm_opp *opp)
{
	kref_put_mutex(&opp->kref, _opp_kref_release, &opp->opp_table->lock);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_put);

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

static struct dev_pm_opp *_opp_get_next(struct opp_table *opp_table,
					bool dynamic)
{
	struct dev_pm_opp *opp = NULL, *temp;

	mutex_lock(&opp_table->lock);
	list_for_each_entry(temp, &opp_table->opp_list, node) {
		/*
		 * Refcount must be dropped only once for each OPP by OPP core,
		 * do that with help of "removed" flag.
		 */
		if (!temp->removed && dynamic == temp->dynamic) {
			opp = temp;
			break;
		}
	}

	mutex_unlock(&opp_table->lock);
	return opp;
}

/*
 * Can't call dev_pm_opp_put() from under the lock as debugfs removal needs to
 * happen lock less to avoid circular dependency issues. This routine must be
 * called without the opp_table->lock held.
 */
static void _opp_remove_all(struct opp_table *opp_table, bool dynamic)
{
	struct dev_pm_opp *opp;

	while ((opp = _opp_get_next(opp_table, dynamic))) {
		opp->removed = true;
		dev_pm_opp_put(opp);

		/* Drop the references taken by dev_pm_opp_add() */
		if (dynamic)
			dev_pm_opp_put_opp_table(opp_table);
	}
}

bool _opp_remove_all_static(struct opp_table *opp_table)
{
	mutex_lock(&opp_table->lock);

	if (!opp_table->parsed_static_opps) {
		mutex_unlock(&opp_table->lock);
		return false;
	}

	if (--opp_table->parsed_static_opps) {
		mutex_unlock(&opp_table->lock);
		return true;
	}

	mutex_unlock(&opp_table->lock);

	_opp_remove_all(opp_table, false);
	return true;
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

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		return;

	_opp_remove_all(opp_table, true);

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

void _required_opps_available(struct dev_pm_opp *opp, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (opp->required_opps[i]->available)
			continue;

		opp->available = false;
		pr_warn("%s: OPP not supported by required OPP %pOF (%lu)\n",
			 __func__, opp->required_opps[i]->np, opp->rate);
		return;
	}
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

	ret = _opp_is_duplicate(dev, new_opp, opp_table, &head);
	if (ret) {
		mutex_unlock(&opp_table->lock);
		return ret;
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

	/* required-opps not fully initialized yet */
	if (lazy_linking_pending(opp_table))
		return 0;

	_required_opps_available(new_opp, opp_table->required_opp_count);

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

	opp_table = _add_opp_table(dev, false);
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
	if (unlikely(!opp_table))
		return;

	kfree(opp_table->supported_hw);
	opp_table->supported_hw = NULL;
	opp_table->supported_hw_count = 0;

	dev_pm_opp_put_opp_table(opp_table);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_put_supported_hw);

static void devm_pm_opp_supported_hw_release(void *data)
{
	dev_pm_opp_put_supported_hw(data);
}

/**
 * devm_pm_opp_set_supported_hw() - Set supported platforms
 * @dev: Device for which supported-hw has to be set.
 * @versions: Array of hierarchy of versions to match.
 * @count: Number of elements in the array.
 *
 * This is a resource-managed variant of dev_pm_opp_set_supported_hw().
 *
 * Return: 0 on success and errorno otherwise.
 */
int devm_pm_opp_set_supported_hw(struct device *dev, const u32 *versions,
				 unsigned int count)
{
	struct opp_table *opp_table;

	opp_table = dev_pm_opp_set_supported_hw(dev, versions, count);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	return devm_add_action_or_reset(dev, devm_pm_opp_supported_hw_release,
					opp_table);
}
EXPORT_SYMBOL_GPL(devm_pm_opp_set_supported_hw);

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

	opp_table = _add_opp_table(dev, false);
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
	if (unlikely(!opp_table))
		return;

	kfree(opp_table->prop_name);
	opp_table->prop_name = NULL;

	dev_pm_opp_put_opp_table(opp_table);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_put_prop_name);

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
	struct dev_pm_opp_supply *supplies;
	struct opp_table *opp_table;
	struct regulator *reg;
	int ret, i;

	opp_table = _add_opp_table(dev, false);
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

	supplies = kmalloc_array(count * 2, sizeof(*supplies), GFP_KERNEL);
	if (!supplies) {
		ret = -ENOMEM;
		goto free_regulators;
	}

	mutex_lock(&opp_table->lock);
	opp_table->sod_supplies = supplies;
	if (opp_table->set_opp_data) {
		opp_table->set_opp_data->old_opp.supplies = supplies;
		opp_table->set_opp_data->new_opp.supplies = supplies + count;
	}
	mutex_unlock(&opp_table->lock);

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

	if (unlikely(!opp_table))
		return;

	if (!opp_table->regulators)
		goto put_opp_table;

	if (opp_table->enabled) {
		for (i = opp_table->regulator_count - 1; i >= 0; i--)
			regulator_disable(opp_table->regulators[i]);
	}

	for (i = opp_table->regulator_count - 1; i >= 0; i--)
		regulator_put(opp_table->regulators[i]);

	mutex_lock(&opp_table->lock);
	if (opp_table->set_opp_data) {
		opp_table->set_opp_data->old_opp.supplies = NULL;
		opp_table->set_opp_data->new_opp.supplies = NULL;
	}

	kfree(opp_table->sod_supplies);
	opp_table->sod_supplies = NULL;
	mutex_unlock(&opp_table->lock);

	kfree(opp_table->regulators);
	opp_table->regulators = NULL;
	opp_table->regulator_count = -1;

put_opp_table:
	dev_pm_opp_put_opp_table(opp_table);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_put_regulators);

static void devm_pm_opp_regulators_release(void *data)
{
	dev_pm_opp_put_regulators(data);
}

/**
 * devm_pm_opp_set_regulators() - Set regulator names for the device
 * @dev: Device for which regulator name is being set.
 * @names: Array of pointers to the names of the regulator.
 * @count: Number of regulators.
 *
 * This is a resource-managed variant of dev_pm_opp_set_regulators().
 *
 * Return: 0 on success and errorno otherwise.
 */
int devm_pm_opp_set_regulators(struct device *dev,
			       const char * const names[],
			       unsigned int count)
{
	struct opp_table *opp_table;

	opp_table = dev_pm_opp_set_regulators(dev, names, count);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	return devm_add_action_or_reset(dev, devm_pm_opp_regulators_release,
					opp_table);
}
EXPORT_SYMBOL_GPL(devm_pm_opp_set_regulators);

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

	opp_table = _add_opp_table(dev, false);
	if (IS_ERR(opp_table))
		return opp_table;

	/* This should be called before OPPs are initialized */
	if (WARN_ON(!list_empty(&opp_table->opp_list))) {
		ret = -EBUSY;
		goto err;
	}

	/* clk shouldn't be initialized at this point */
	if (WARN_ON(opp_table->clk)) {
		ret = -EBUSY;
		goto err;
	}

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
	if (unlikely(!opp_table))
		return;

	clk_put(opp_table->clk);
	opp_table->clk = ERR_PTR(-EINVAL);

	dev_pm_opp_put_opp_table(opp_table);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_put_clkname);

static void devm_pm_opp_clkname_release(void *data)
{
	dev_pm_opp_put_clkname(data);
}

/**
 * devm_pm_opp_set_clkname() - Set clk name for the device
 * @dev: Device for which clk name is being set.
 * @name: Clk name.
 *
 * This is a resource-managed variant of dev_pm_opp_set_clkname().
 *
 * Return: 0 on success and errorno otherwise.
 */
int devm_pm_opp_set_clkname(struct device *dev, const char *name)
{
	struct opp_table *opp_table;

	opp_table = dev_pm_opp_set_clkname(dev, name);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	return devm_add_action_or_reset(dev, devm_pm_opp_clkname_release,
					opp_table);
}
EXPORT_SYMBOL_GPL(devm_pm_opp_set_clkname);

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
	struct dev_pm_set_opp_data *data;
	struct opp_table *opp_table;

	if (!set_opp)
		return ERR_PTR(-EINVAL);

	opp_table = _add_opp_table(dev, false);
	if (IS_ERR(opp_table))
		return opp_table;

	/* This should be called before OPPs are initialized */
	if (WARN_ON(!list_empty(&opp_table->opp_list))) {
		dev_pm_opp_put_opp_table(opp_table);
		return ERR_PTR(-EBUSY);
	}

	/* Another CPU that shares the OPP table has set the helper ? */
	if (opp_table->set_opp)
		return opp_table;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&opp_table->lock);
	opp_table->set_opp_data = data;
	if (opp_table->sod_supplies) {
		data->old_opp.supplies = opp_table->sod_supplies;
		data->new_opp.supplies = opp_table->sod_supplies +
					 opp_table->regulator_count;
	}
	mutex_unlock(&opp_table->lock);

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
	if (unlikely(!opp_table))
		return;

	opp_table->set_opp = NULL;

	mutex_lock(&opp_table->lock);
	kfree(opp_table->set_opp_data);
	opp_table->set_opp_data = NULL;
	mutex_unlock(&opp_table->lock);

	dev_pm_opp_put_opp_table(opp_table);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_unregister_set_opp_helper);

static void devm_pm_opp_unregister_set_opp_helper(void *data)
{
	dev_pm_opp_unregister_set_opp_helper(data);
}

/**
 * devm_pm_opp_register_set_opp_helper() - Register custom set OPP helper
 * @dev: Device for which the helper is getting registered.
 * @set_opp: Custom set OPP helper.
 *
 * This is a resource-managed version of dev_pm_opp_register_set_opp_helper().
 *
 * Return: 0 on success and errorno otherwise.
 */
int devm_pm_opp_register_set_opp_helper(struct device *dev,
					int (*set_opp)(struct dev_pm_set_opp_data *data))
{
	struct opp_table *opp_table;

	opp_table = dev_pm_opp_register_set_opp_helper(dev, set_opp);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	return devm_add_action_or_reset(dev, devm_pm_opp_unregister_set_opp_helper,
					opp_table);
}
EXPORT_SYMBOL_GPL(devm_pm_opp_register_set_opp_helper);

static void _opp_detach_genpd(struct opp_table *opp_table)
{
	int index;

	if (!opp_table->genpd_virt_devs)
		return;

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

	opp_table = _add_opp_table(dev, false);
	if (IS_ERR(opp_table))
		return opp_table;

	if (opp_table->genpd_virt_devs)
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

		virt_dev = dev_pm_domain_attach_by_name(dev, *name);
		if (IS_ERR_OR_NULL(virt_dev)) {
			ret = PTR_ERR(virt_dev) ? : -ENODEV;
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
	if (unlikely(!opp_table))
		return;

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

static void devm_pm_opp_detach_genpd(void *data)
{
	dev_pm_opp_detach_genpd(data);
}

/**
 * devm_pm_opp_attach_genpd - Attach genpd(s) for the device and save virtual
 *			      device pointer
 * @dev: Consumer device for which the genpd is getting attached.
 * @names: Null terminated array of pointers containing names of genpd to attach.
 * @virt_devs: Pointer to return the array of virtual devices.
 *
 * This is a resource-managed version of dev_pm_opp_attach_genpd().
 *
 * Return: 0 on success and errorno otherwise.
 */
int devm_pm_opp_attach_genpd(struct device *dev, const char **names,
			     struct device ***virt_devs)
{
	struct opp_table *opp_table;

	opp_table = dev_pm_opp_attach_genpd(dev, names, virt_devs);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	return devm_add_action_or_reset(dev, devm_pm_opp_detach_genpd,
					opp_table);
}
EXPORT_SYMBOL_GPL(devm_pm_opp_attach_genpd);

/**
 * dev_pm_opp_xlate_required_opp() - Find required OPP for @src_table OPP.
 * @src_table: OPP table which has @dst_table as one of its required OPP table.
 * @dst_table: Required OPP table of the @src_table.
 * @src_opp: OPP from the @src_table.
 *
 * This function returns the OPP (present in @dst_table) pointed out by the
 * "required-opps" property of the @src_opp (present in @src_table).
 *
 * The callers are required to call dev_pm_opp_put() for the returned OPP after
 * use.
 *
 * Return: pointer to 'struct dev_pm_opp' on success and errorno otherwise.
 */
struct dev_pm_opp *dev_pm_opp_xlate_required_opp(struct opp_table *src_table,
						 struct opp_table *dst_table,
						 struct dev_pm_opp *src_opp)
{
	struct dev_pm_opp *opp, *dest_opp = ERR_PTR(-ENODEV);
	int i;

	if (!src_table || !dst_table || !src_opp ||
	    !src_table->required_opp_tables)
		return ERR_PTR(-EINVAL);

	/* required-opps not fully initialized yet */
	if (lazy_linking_pending(src_table))
		return ERR_PTR(-EBUSY);

	for (i = 0; i < src_table->required_opp_count; i++) {
		if (src_table->required_opp_tables[i] == dst_table) {
			mutex_lock(&src_table->lock);

			list_for_each_entry(opp, &src_table->opp_list, node) {
				if (opp == src_opp) {
					dest_opp = opp->required_opps[i];
					dev_pm_opp_get(dest_opp);
					break;
				}
			}

			mutex_unlock(&src_table->lock);
			break;
		}
	}

	if (IS_ERR(dest_opp)) {
		pr_err("%s: Couldn't find matching OPP (%p: %p)\n", __func__,
		       src_table, dst_table);
	}

	return dest_opp;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_xlate_required_opp);

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

	/*
	 * Normally the src_table will have the "required_opps" property set to
	 * point to one of the OPPs in the dst_table, but in some cases the
	 * genpd and its master have one to one mapping of performance states
	 * and so none of them have the "required-opps" property set. Return the
	 * pstate of the src_table as it is in such cases.
	 */
	if (!src_table || !src_table->required_opp_count)
		return pstate;

	/* required-opps not fully initialized yet */
	if (lazy_linking_pending(src_table))
		return -EBUSY;

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

	opp_table = _add_opp_table(dev, true);
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

/**
 * dev_pm_opp_remove_table() - Free all OPPs associated with the device
 * @dev:	device pointer used to lookup OPP table.
 *
 * Free both OPPs created using static entries present in DT and the
 * dynamically added entries.
 */
void dev_pm_opp_remove_table(struct device *dev)
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
EXPORT_SYMBOL_GPL(dev_pm_opp_remove_table);

/**
 * dev_pm_opp_sync_regulators() - Sync state of voltage regulators
 * @dev:	device for which we do this operation
 *
 * Sync voltage state of the OPP table regulators.
 *
 * Return: 0 on success or a negative error value.
 */
int dev_pm_opp_sync_regulators(struct device *dev)
{
	struct opp_table *opp_table;
	struct regulator *reg;
	int i, ret = 0;

	/* Device may not have OPP table */
	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		return 0;

	/* Regulator may not be required for the device */
	if (unlikely(!opp_table->regulators))
		goto put_table;

	/* Nothing to sync if voltage wasn't changed */
	if (!opp_table->enabled)
		goto put_table;

	for (i = 0; i < opp_table->regulator_count; i++) {
		reg = opp_table->regulators[i];
		ret = regulator_sync_voltage(reg);
		if (ret)
			break;
	}
put_table:
	/* Drop reference taken by _find_opp_table() */
	dev_pm_opp_put_opp_table(opp_table);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_sync_regulators);
