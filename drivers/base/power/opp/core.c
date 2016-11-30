/*
 * Generic OPP Interface
 *
 * Copyright (C) 2009-2010 Texas Instruments Incorporated.
 *	Nishanth Menon
 *	Romit Dasgupta
 *	Kevin Hilman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/export.h>
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

#define opp_rcu_lockdep_assert()					\
do {									\
	RCU_LOCKDEP_WARN(!rcu_read_lock_held() &&			\
			 !lockdep_is_held(&opp_table_lock),		\
			 "Missing rcu_read_lock() or "			\
			 "opp_table_lock protection");			\
} while (0)

static struct opp_device *_find_opp_dev(const struct device *dev,
					struct opp_table *opp_table)
{
	struct opp_device *opp_dev;

	list_for_each_entry(opp_dev, &opp_table->dev_list, node)
		if (opp_dev->dev == dev)
			return opp_dev;

	return NULL;
}

/**
 * _find_opp_table() - find opp_table struct using device pointer
 * @dev:	device pointer used to lookup OPP table
 *
 * Search OPP table for one containing matching device. Does a RCU reader
 * operation to grab the pointer needed.
 *
 * Return: pointer to 'struct opp_table' if found, otherwise -ENODEV or
 * -EINVAL based on type of error.
 *
 * Locking: For readers, this function must be called under rcu_read_lock().
 * opp_table is a RCU protected pointer, which means that opp_table is valid
 * as long as we are under RCU lock.
 *
 * For Writers, this function must be called with opp_table_lock held.
 */
struct opp_table *_find_opp_table(struct device *dev)
{
	struct opp_table *opp_table;

	opp_rcu_lockdep_assert();

	if (IS_ERR_OR_NULL(dev)) {
		pr_err("%s: Invalid parameters\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	list_for_each_entry_rcu(opp_table, &opp_tables, node)
		if (_find_opp_dev(dev, opp_table))
			return opp_table;

	return ERR_PTR(-ENODEV);
}

/**
 * dev_pm_opp_get_voltage() - Gets the voltage corresponding to an opp
 * @opp:	opp for which voltage has to be returned for
 *
 * Return: voltage in micro volt corresponding to the opp, else
 * return 0
 *
 * Locking: This function must be called under rcu_read_lock(). opp is a rcu
 * protected pointer. This means that opp which could have been fetched by
 * opp_find_freq_{exact,ceil,floor} functions is valid as long as we are
 * under RCU lock. The pointer returned by the opp_find_freq family must be
 * used in the same section as the usage of this function with the pointer
 * prior to unlocking with rcu_read_unlock() to maintain the integrity of the
 * pointer.
 */
unsigned long dev_pm_opp_get_voltage(struct dev_pm_opp *opp)
{
	struct dev_pm_opp *tmp_opp;
	unsigned long v = 0;

	opp_rcu_lockdep_assert();

	tmp_opp = rcu_dereference(opp);
	if (IS_ERR_OR_NULL(tmp_opp))
		pr_err("%s: Invalid parameters\n", __func__);
	else
		v = tmp_opp->u_volt;

	return v;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_voltage);

/**
 * dev_pm_opp_get_freq() - Gets the frequency corresponding to an available opp
 * @opp:	opp for which frequency has to be returned for
 *
 * Return: frequency in hertz corresponding to the opp, else
 * return 0
 *
 * Locking: This function must be called under rcu_read_lock(). opp is a rcu
 * protected pointer. This means that opp which could have been fetched by
 * opp_find_freq_{exact,ceil,floor} functions is valid as long as we are
 * under RCU lock. The pointer returned by the opp_find_freq family must be
 * used in the same section as the usage of this function with the pointer
 * prior to unlocking with rcu_read_unlock() to maintain the integrity of the
 * pointer.
 */
unsigned long dev_pm_opp_get_freq(struct dev_pm_opp *opp)
{
	struct dev_pm_opp *tmp_opp;
	unsigned long f = 0;

	opp_rcu_lockdep_assert();

	tmp_opp = rcu_dereference(opp);
	if (IS_ERR_OR_NULL(tmp_opp) || !tmp_opp->available)
		pr_err("%s: Invalid parameters\n", __func__);
	else
		f = tmp_opp->rate;

	return f;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_freq);

/**
 * dev_pm_opp_is_turbo() - Returns if opp is turbo OPP or not
 * @opp: opp for which turbo mode is being verified
 *
 * Turbo OPPs are not for normal use, and can be enabled (under certain
 * conditions) for short duration of times to finish high throughput work
 * quickly. Running on them for longer times may overheat the chip.
 *
 * Return: true if opp is turbo opp, else false.
 *
 * Locking: This function must be called under rcu_read_lock(). opp is a rcu
 * protected pointer. This means that opp which could have been fetched by
 * opp_find_freq_{exact,ceil,floor} functions is valid as long as we are
 * under RCU lock. The pointer returned by the opp_find_freq family must be
 * used in the same section as the usage of this function with the pointer
 * prior to unlocking with rcu_read_unlock() to maintain the integrity of the
 * pointer.
 */
bool dev_pm_opp_is_turbo(struct dev_pm_opp *opp)
{
	struct dev_pm_opp *tmp_opp;

	opp_rcu_lockdep_assert();

	tmp_opp = rcu_dereference(opp);
	if (IS_ERR_OR_NULL(tmp_opp) || !tmp_opp->available) {
		pr_err("%s: Invalid parameters\n", __func__);
		return false;
	}

	return tmp_opp->turbo;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_is_turbo);

/**
 * dev_pm_opp_get_max_clock_latency() - Get max clock latency in nanoseconds
 * @dev:	device for which we do this operation
 *
 * Return: This function returns the max clock latency in nanoseconds.
 *
 * Locking: This function takes rcu_read_lock().
 */
unsigned long dev_pm_opp_get_max_clock_latency(struct device *dev)
{
	struct opp_table *opp_table;
	unsigned long clock_latency_ns;

	rcu_read_lock();

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		clock_latency_ns = 0;
	else
		clock_latency_ns = opp_table->clock_latency_ns_max;

	rcu_read_unlock();
	return clock_latency_ns;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_max_clock_latency);

/**
 * dev_pm_opp_get_max_volt_latency() - Get max voltage latency in nanoseconds
 * @dev: device for which we do this operation
 *
 * Return: This function returns the max voltage latency in nanoseconds.
 *
 * Locking: This function takes rcu_read_lock().
 */
unsigned long dev_pm_opp_get_max_volt_latency(struct device *dev)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *opp;
	struct regulator *reg;
	unsigned long latency_ns = 0;
	unsigned long min_uV = ~0, max_uV = 0;
	int ret;

	rcu_read_lock();

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		rcu_read_unlock();
		return 0;
	}

	reg = opp_table->regulator;
	if (IS_ERR(reg)) {
		/* Regulator may not be required for device */
		rcu_read_unlock();
		return 0;
	}

	list_for_each_entry_rcu(opp, &opp_table->opp_list, node) {
		if (!opp->available)
			continue;

		if (opp->u_volt_min < min_uV)
			min_uV = opp->u_volt_min;
		if (opp->u_volt_max > max_uV)
			max_uV = opp->u_volt_max;
	}

	rcu_read_unlock();

	/*
	 * The caller needs to ensure that opp_table (and hence the regulator)
	 * isn't freed, while we are executing this routine.
	 */
	ret = regulator_set_voltage_time(reg, min_uV, max_uV);
	if (ret > 0)
		latency_ns = ret * 1000;

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
 *
 * Locking: This function takes rcu_read_lock().
 */
unsigned long dev_pm_opp_get_max_transition_latency(struct device *dev)
{
	return dev_pm_opp_get_max_volt_latency(dev) +
		dev_pm_opp_get_max_clock_latency(dev);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_max_transition_latency);

/**
 * dev_pm_opp_get_suspend_opp() - Get suspend opp
 * @dev:	device for which we do this operation
 *
 * Return: This function returns pointer to the suspend opp if it is
 * defined and available, otherwise it returns NULL.
 *
 * Locking: This function must be called under rcu_read_lock(). opp is a rcu
 * protected pointer. The reason for the same is that the opp pointer which is
 * returned will remain valid for use with opp_get_{voltage, freq} only while
 * under the locked area. The pointer returned must be used prior to unlocking
 * with rcu_read_unlock() to maintain the integrity of the pointer.
 */
struct dev_pm_opp *dev_pm_opp_get_suspend_opp(struct device *dev)
{
	struct opp_table *opp_table;

	opp_rcu_lockdep_assert();

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table) || !opp_table->suspend_opp ||
	    !opp_table->suspend_opp->available)
		return NULL;

	return opp_table->suspend_opp;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_suspend_opp);

/**
 * dev_pm_opp_get_opp_count() - Get number of opps available in the opp table
 * @dev:	device for which we do this operation
 *
 * Return: This function returns the number of available opps if there are any,
 * else returns 0 if none or the corresponding error value.
 *
 * Locking: This function takes rcu_read_lock().
 */
int dev_pm_opp_get_opp_count(struct device *dev)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *temp_opp;
	int count = 0;

	rcu_read_lock();

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		count = PTR_ERR(opp_table);
		dev_err(dev, "%s: OPP table not found (%d)\n",
			__func__, count);
		goto out_unlock;
	}

	list_for_each_entry_rcu(temp_opp, &opp_table->opp_list, node) {
		if (temp_opp->available)
			count++;
	}

out_unlock:
	rcu_read_unlock();
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
 * Locking: This function must be called under rcu_read_lock(). opp is a rcu
 * protected pointer. The reason for the same is that the opp pointer which is
 * returned will remain valid for use with opp_get_{voltage, freq} only while
 * under the locked area. The pointer returned must be used prior to unlocking
 * with rcu_read_unlock() to maintain the integrity of the pointer.
 */
struct dev_pm_opp *dev_pm_opp_find_freq_exact(struct device *dev,
					      unsigned long freq,
					      bool available)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *temp_opp, *opp = ERR_PTR(-ERANGE);

	opp_rcu_lockdep_assert();

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		int r = PTR_ERR(opp_table);

		dev_err(dev, "%s: OPP table not found (%d)\n", __func__, r);
		return ERR_PTR(r);
	}

	list_for_each_entry_rcu(temp_opp, &opp_table->opp_list, node) {
		if (temp_opp->available == available &&
				temp_opp->rate == freq) {
			opp = temp_opp;
			break;
		}
	}

	return opp;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_find_freq_exact);

static noinline struct dev_pm_opp *_find_freq_ceil(struct opp_table *opp_table,
						   unsigned long *freq)
{
	struct dev_pm_opp *temp_opp, *opp = ERR_PTR(-ERANGE);

	list_for_each_entry_rcu(temp_opp, &opp_table->opp_list, node) {
		if (temp_opp->available && temp_opp->rate >= *freq) {
			opp = temp_opp;
			*freq = opp->rate;
			break;
		}
	}

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
 * Locking: This function must be called under rcu_read_lock(). opp is a rcu
 * protected pointer. The reason for the same is that the opp pointer which is
 * returned will remain valid for use with opp_get_{voltage, freq} only while
 * under the locked area. The pointer returned must be used prior to unlocking
 * with rcu_read_unlock() to maintain the integrity of the pointer.
 */
struct dev_pm_opp *dev_pm_opp_find_freq_ceil(struct device *dev,
					     unsigned long *freq)
{
	struct opp_table *opp_table;

	opp_rcu_lockdep_assert();

	if (!dev || !freq) {
		dev_err(dev, "%s: Invalid argument freq=%p\n", __func__, freq);
		return ERR_PTR(-EINVAL);
	}

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		return ERR_CAST(opp_table);

	return _find_freq_ceil(opp_table, freq);
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
 * Locking: This function must be called under rcu_read_lock(). opp is a rcu
 * protected pointer. The reason for the same is that the opp pointer which is
 * returned will remain valid for use with opp_get_{voltage, freq} only while
 * under the locked area. The pointer returned must be used prior to unlocking
 * with rcu_read_unlock() to maintain the integrity of the pointer.
 */
struct dev_pm_opp *dev_pm_opp_find_freq_floor(struct device *dev,
					      unsigned long *freq)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *temp_opp, *opp = ERR_PTR(-ERANGE);

	opp_rcu_lockdep_assert();

	if (!dev || !freq) {
		dev_err(dev, "%s: Invalid argument freq=%p\n", __func__, freq);
		return ERR_PTR(-EINVAL);
	}

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		return ERR_CAST(opp_table);

	list_for_each_entry_rcu(temp_opp, &opp_table->opp_list, node) {
		if (temp_opp->available) {
			/* go to the next node, before choosing prev */
			if (temp_opp->rate > *freq)
				break;
			else
				opp = temp_opp;
		}
	}
	if (!IS_ERR(opp))
		*freq = opp->rate;

	return opp;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_find_freq_floor);

/*
 * The caller needs to ensure that opp_table (and hence the clk) isn't freed,
 * while clk returned here is used.
 */
static struct clk *_get_opp_clk(struct device *dev)
{
	struct opp_table *opp_table;
	struct clk *clk;

	rcu_read_lock();

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		dev_err(dev, "%s: device opp doesn't exist\n", __func__);
		clk = ERR_CAST(opp_table);
		goto unlock;
	}

	clk = opp_table->clk;
	if (IS_ERR(clk))
		dev_err(dev, "%s: No clock available for the device\n",
			__func__);

unlock:
	rcu_read_unlock();
	return clk;
}

static int _set_opp_voltage(struct device *dev, struct regulator *reg,
			    unsigned long u_volt, unsigned long u_volt_min,
			    unsigned long u_volt_max)
{
	int ret;

	/* Regulator not available for device */
	if (IS_ERR(reg)) {
		dev_dbg(dev, "%s: regulator not available: %ld\n", __func__,
			PTR_ERR(reg));
		return 0;
	}

	dev_dbg(dev, "%s: voltages (mV): %lu %lu %lu\n", __func__, u_volt_min,
		u_volt, u_volt_max);

	ret = regulator_set_voltage_triplet(reg, u_volt_min, u_volt,
					    u_volt_max);
	if (ret)
		dev_err(dev, "%s: failed to set voltage (%lu %lu %lu mV): %d\n",
			__func__, u_volt_min, u_volt, u_volt_max, ret);

	return ret;
}

/**
 * dev_pm_opp_set_rate() - Configure new OPP based on frequency
 * @dev:	 device for which we do this operation
 * @target_freq: frequency to achieve
 *
 * This configures the power-supplies and clock source to the levels specified
 * by the OPP corresponding to the target_freq.
 *
 * Locking: This function takes rcu_read_lock().
 */
int dev_pm_opp_set_rate(struct device *dev, unsigned long target_freq)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *old_opp, *opp;
	struct regulator *reg;
	struct clk *clk;
	unsigned long freq, old_freq;
	unsigned long u_volt, u_volt_min, u_volt_max;
	int ret;

	if (unlikely(!target_freq)) {
		dev_err(dev, "%s: Invalid target frequency %lu\n", __func__,
			target_freq);
		return -EINVAL;
	}

	clk = _get_opp_clk(dev);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	freq = clk_round_rate(clk, target_freq);
	if ((long)freq <= 0)
		freq = target_freq;

	old_freq = clk_get_rate(clk);

	/* Return early if nothing to do */
	if (old_freq == freq) {
		dev_dbg(dev, "%s: old/new frequencies (%lu Hz) are same, nothing to do\n",
			__func__, freq);
		return 0;
	}

	rcu_read_lock();

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		dev_err(dev, "%s: device opp doesn't exist\n", __func__);
		rcu_read_unlock();
		return PTR_ERR(opp_table);
	}

	old_opp = _find_freq_ceil(opp_table, &old_freq);
	if (IS_ERR(old_opp)) {
		dev_err(dev, "%s: failed to find current OPP for freq %lu (%ld)\n",
			__func__, old_freq, PTR_ERR(old_opp));
	}

	opp = _find_freq_ceil(opp_table, &freq);
	if (IS_ERR(opp)) {
		ret = PTR_ERR(opp);
		dev_err(dev, "%s: failed to find OPP for freq %lu (%d)\n",
			__func__, freq, ret);
		rcu_read_unlock();
		return ret;
	}

	u_volt = opp->u_volt;
	u_volt_min = opp->u_volt_min;
	u_volt_max = opp->u_volt_max;

	reg = opp_table->regulator;

	rcu_read_unlock();

	/* Scaling up? Scale voltage before frequency */
	if (freq > old_freq) {
		ret = _set_opp_voltage(dev, reg, u_volt, u_volt_min,
				       u_volt_max);
		if (ret)
			goto restore_voltage;
	}

	/* Change frequency */

	dev_dbg(dev, "%s: switching OPP: %lu Hz --> %lu Hz\n",
		__func__, old_freq, freq);

	ret = clk_set_rate(clk, freq);
	if (ret) {
		dev_err(dev, "%s: failed to set clock rate: %d\n", __func__,
			ret);
		goto restore_voltage;
	}

	/* Scaling down? Scale voltage after frequency */
	if (freq < old_freq) {
		ret = _set_opp_voltage(dev, reg, u_volt, u_volt_min,
				       u_volt_max);
		if (ret)
			goto restore_freq;
	}

	return 0;

restore_freq:
	if (clk_set_rate(clk, old_freq))
		dev_err(dev, "%s: failed to restore old-freq (%lu Hz)\n",
			__func__, old_freq);
restore_voltage:
	/* This shouldn't harm even if the voltages weren't updated earlier */
	if (!IS_ERR(old_opp))
		_set_opp_voltage(dev, reg, old_opp->u_volt,
				 old_opp->u_volt_min, old_opp->u_volt_max);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_rate);

/* OPP-dev Helpers */
static void _kfree_opp_dev_rcu(struct rcu_head *head)
{
	struct opp_device *opp_dev;

	opp_dev = container_of(head, struct opp_device, rcu_head);
	kfree_rcu(opp_dev, rcu_head);
}

static void _remove_opp_dev(struct opp_device *opp_dev,
			    struct opp_table *opp_table)
{
	opp_debug_unregister(opp_dev, opp_table);
	list_del(&opp_dev->node);
	call_srcu(&opp_table->srcu_head.srcu, &opp_dev->rcu_head,
		  _kfree_opp_dev_rcu);
}

struct opp_device *_add_opp_dev(const struct device *dev,
				struct opp_table *opp_table)
{
	struct opp_device *opp_dev;
	int ret;

	opp_dev = kzalloc(sizeof(*opp_dev), GFP_KERNEL);
	if (!opp_dev)
		return NULL;

	/* Initialize opp-dev */
	opp_dev->dev = dev;
	list_add_rcu(&opp_dev->node, &opp_table->dev_list);

	/* Create debugfs entries for the opp_table */
	ret = opp_debug_register(opp_dev, opp_table);
	if (ret)
		dev_err(dev, "%s: Failed to register opp debugfs (%d)\n",
			__func__, ret);

	return opp_dev;
}

/**
 * _add_opp_table() - Find OPP table or allocate a new one
 * @dev:	device for which we do this operation
 *
 * It tries to find an existing table first, if it couldn't find one, it
 * allocates a new OPP table and returns that.
 *
 * Return: valid opp_table pointer if success, else NULL.
 */
static struct opp_table *_add_opp_table(struct device *dev)
{
	struct opp_table *opp_table;
	struct opp_device *opp_dev;
	int ret;

	/* Check for existing table for 'dev' first */
	opp_table = _find_opp_table(dev);
	if (!IS_ERR(opp_table))
		return opp_table;

	/*
	 * Allocate a new OPP table. In the infrequent case where a new
	 * device is needed to be added, we pay this penalty.
	 */
	opp_table = kzalloc(sizeof(*opp_table), GFP_KERNEL);
	if (!opp_table)
		return NULL;

	INIT_LIST_HEAD(&opp_table->dev_list);

	opp_dev = _add_opp_dev(dev, opp_table);
	if (!opp_dev) {
		kfree(opp_table);
		return NULL;
	}

	_of_init_opp_table(opp_table, dev);

	/* Set regulator to a non-NULL error value */
	opp_table->regulator = ERR_PTR(-ENXIO);

	/* Find clk for the device */
	opp_table->clk = clk_get(dev, NULL);
	if (IS_ERR(opp_table->clk)) {
		ret = PTR_ERR(opp_table->clk);
		if (ret != -EPROBE_DEFER)
			dev_dbg(dev, "%s: Couldn't find clock: %d\n", __func__,
				ret);
	}

	srcu_init_notifier_head(&opp_table->srcu_head);
	INIT_LIST_HEAD(&opp_table->opp_list);

	/* Secure the device table modification */
	list_add_rcu(&opp_table->node, &opp_tables);
	return opp_table;
}

/**
 * _kfree_device_rcu() - Free opp_table RCU handler
 * @head:	RCU head
 */
static void _kfree_device_rcu(struct rcu_head *head)
{
	struct opp_table *opp_table = container_of(head, struct opp_table,
						   rcu_head);

	kfree_rcu(opp_table, rcu_head);
}

/**
 * _remove_opp_table() - Removes a OPP table
 * @opp_table: OPP table to be removed.
 *
 * Removes/frees OPP table if it doesn't contain any OPPs.
 */
static void _remove_opp_table(struct opp_table *opp_table)
{
	struct opp_device *opp_dev;

	if (!list_empty(&opp_table->opp_list))
		return;

	if (opp_table->supported_hw)
		return;

	if (opp_table->prop_name)
		return;

	if (!IS_ERR(opp_table->regulator))
		return;

	/* Release clk */
	if (!IS_ERR(opp_table->clk))
		clk_put(opp_table->clk);

	opp_dev = list_first_entry(&opp_table->dev_list, struct opp_device,
				   node);

	_remove_opp_dev(opp_dev, opp_table);

	/* dev_list must be empty now */
	WARN_ON(!list_empty(&opp_table->dev_list));

	list_del_rcu(&opp_table->node);
	call_srcu(&opp_table->srcu_head.srcu, &opp_table->rcu_head,
		  _kfree_device_rcu);
}

/**
 * _kfree_opp_rcu() - Free OPP RCU handler
 * @head:	RCU head
 */
static void _kfree_opp_rcu(struct rcu_head *head)
{
	struct dev_pm_opp *opp = container_of(head, struct dev_pm_opp, rcu_head);

	kfree_rcu(opp, rcu_head);
}

/**
 * _opp_remove()  - Remove an OPP from a table definition
 * @opp_table:	points back to the opp_table struct this opp belongs to
 * @opp:	pointer to the OPP to remove
 * @notify:	OPP_EVENT_REMOVE notification should be sent or not
 *
 * This function removes an opp definition from the opp table.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * It is assumed that the caller holds required mutex for an RCU updater
 * strategy.
 */
void _opp_remove(struct opp_table *opp_table, struct dev_pm_opp *opp,
		 bool notify)
{
	/*
	 * Notify the changes in the availability of the operable
	 * frequency/voltage list.
	 */
	if (notify)
		srcu_notifier_call_chain(&opp_table->srcu_head,
					 OPP_EVENT_REMOVE, opp);
	opp_debug_remove_one(opp);
	list_del_rcu(&opp->node);
	call_srcu(&opp_table->srcu_head.srcu, &opp->rcu_head, _kfree_opp_rcu);

	_remove_opp_table(opp_table);
}

/**
 * dev_pm_opp_remove()  - Remove an OPP from OPP table
 * @dev:	device for which we do this operation
 * @freq:	OPP to remove with matching 'freq'
 *
 * This function removes an opp from the opp table.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
void dev_pm_opp_remove(struct device *dev, unsigned long freq)
{
	struct dev_pm_opp *opp;
	struct opp_table *opp_table;
	bool found = false;

	/* Hold our table modification lock here */
	mutex_lock(&opp_table_lock);

	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table))
		goto unlock;

	list_for_each_entry(opp, &opp_table->opp_list, node) {
		if (opp->rate == freq) {
			found = true;
			break;
		}
	}

	if (!found) {
		dev_warn(dev, "%s: Couldn't find OPP with freq: %lu\n",
			 __func__, freq);
		goto unlock;
	}

	_opp_remove(opp_table, opp, true);
unlock:
	mutex_unlock(&opp_table_lock);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_remove);

struct dev_pm_opp *_allocate_opp(struct device *dev,
				 struct opp_table **opp_table)
{
	struct dev_pm_opp *opp;

	/* allocate new OPP node */
	opp = kzalloc(sizeof(*opp), GFP_KERNEL);
	if (!opp)
		return NULL;

	INIT_LIST_HEAD(&opp->node);

	*opp_table = _add_opp_table(dev);
	if (!*opp_table) {
		kfree(opp);
		return NULL;
	}

	return opp;
}

static bool _opp_supported_by_regulators(struct dev_pm_opp *opp,
					 struct opp_table *opp_table)
{
	struct regulator *reg = opp_table->regulator;

	if (!IS_ERR(reg) &&
	    !regulator_is_supported_voltage(reg, opp->u_volt_min,
					    opp->u_volt_max)) {
		pr_warn("%s: OPP minuV: %lu maxuV: %lu, not supported by regulator\n",
			__func__, opp->u_volt_min, opp->u_volt_max);
		return false;
	}

	return true;
}

int _opp_add(struct device *dev, struct dev_pm_opp *new_opp,
	     struct opp_table *opp_table)
{
	struct dev_pm_opp *opp;
	struct list_head *head = &opp_table->opp_list;
	int ret;

	/*
	 * Insert new OPP in order of increasing frequency and discard if
	 * already present.
	 *
	 * Need to use &opp_table->opp_list in the condition part of the 'for'
	 * loop, don't replace it with head otherwise it will become an infinite
	 * loop.
	 */
	list_for_each_entry_rcu(opp, &opp_table->opp_list, node) {
		if (new_opp->rate > opp->rate) {
			head = &opp->node;
			continue;
		}

		if (new_opp->rate < opp->rate)
			break;

		/* Duplicate OPPs */
		dev_warn(dev, "%s: duplicate OPPs detected. Existing: freq: %lu, volt: %lu, enabled: %d. New: freq: %lu, volt: %lu, enabled: %d\n",
			 __func__, opp->rate, opp->u_volt, opp->available,
			 new_opp->rate, new_opp->u_volt, new_opp->available);

		return opp->available && new_opp->u_volt == opp->u_volt ?
			0 : -EEXIST;
	}

	new_opp->opp_table = opp_table;
	list_add_rcu(&new_opp->node, head);

	ret = opp_debug_create_one(new_opp, opp_table);
	if (ret)
		dev_err(dev, "%s: Failed to register opp to debugfs (%d)\n",
			__func__, ret);

	if (!_opp_supported_by_regulators(new_opp, opp_table)) {
		new_opp->available = false;
		dev_warn(dev, "%s: OPP not supported by regulators (%lu)\n",
			 __func__, new_opp->rate);
	}

	return 0;
}

/**
 * _opp_add_v1() - Allocate a OPP based on v1 bindings.
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
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 *
 * Return:
 * 0		On success OR
 *		Duplicate OPPs (both freq and volt are same) and opp->available
 * -EEXIST	Freq are same and volt are different OR
 *		Duplicate OPPs (both freq and volt are same) and !opp->available
 * -ENOMEM	Memory allocation failure
 */
int _opp_add_v1(struct device *dev, unsigned long freq, long u_volt,
		bool dynamic)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *new_opp;
	unsigned long tol;
	int ret;

	/* Hold our table modification lock here */
	mutex_lock(&opp_table_lock);

	new_opp = _allocate_opp(dev, &opp_table);
	if (!new_opp) {
		ret = -ENOMEM;
		goto unlock;
	}

	/* populate the opp table */
	new_opp->rate = freq;
	tol = u_volt * opp_table->voltage_tolerance_v1 / 100;
	new_opp->u_volt = u_volt;
	new_opp->u_volt_min = u_volt - tol;
	new_opp->u_volt_max = u_volt + tol;
	new_opp->available = true;
	new_opp->dynamic = dynamic;

	ret = _opp_add(dev, new_opp, opp_table);
	if (ret)
		goto free_opp;

	mutex_unlock(&opp_table_lock);

	/*
	 * Notify the changes in the availability of the operable
	 * frequency/voltage list.
	 */
	srcu_notifier_call_chain(&opp_table->srcu_head, OPP_EVENT_ADD, new_opp);
	return 0;

free_opp:
	_opp_remove(opp_table, new_opp, false);
unlock:
	mutex_unlock(&opp_table_lock);
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
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
int dev_pm_opp_set_supported_hw(struct device *dev, const u32 *versions,
				unsigned int count)
{
	struct opp_table *opp_table;
	int ret = 0;

	/* Hold our table modification lock here */
	mutex_lock(&opp_table_lock);

	opp_table = _add_opp_table(dev);
	if (!opp_table) {
		ret = -ENOMEM;
		goto unlock;
	}

	/* Make sure there are no concurrent readers while updating opp_table */
	WARN_ON(!list_empty(&opp_table->opp_list));

	/* Do we already have a version hierarchy associated with opp_table? */
	if (opp_table->supported_hw) {
		dev_err(dev, "%s: Already have supported hardware list\n",
			__func__);
		ret = -EBUSY;
		goto err;
	}

	opp_table->supported_hw = kmemdup(versions, count * sizeof(*versions),
					GFP_KERNEL);
	if (!opp_table->supported_hw) {
		ret = -ENOMEM;
		goto err;
	}

	opp_table->supported_hw_count = count;
	mutex_unlock(&opp_table_lock);
	return 0;

err:
	_remove_opp_table(opp_table);
unlock:
	mutex_unlock(&opp_table_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_supported_hw);

/**
 * dev_pm_opp_put_supported_hw() - Releases resources blocked for supported hw
 * @dev: Device for which supported-hw has to be put.
 *
 * This is required only for the V2 bindings, and is called for a matching
 * dev_pm_opp_set_supported_hw(). Until this is called, the opp_table structure
 * will not be freed.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
void dev_pm_opp_put_supported_hw(struct device *dev)
{
	struct opp_table *opp_table;

	/* Hold our table modification lock here */
	mutex_lock(&opp_table_lock);

	/* Check for existing table for 'dev' first */
	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		dev_err(dev, "Failed to find opp_table: %ld\n",
			PTR_ERR(opp_table));
		goto unlock;
	}

	/* Make sure there are no concurrent readers while updating opp_table */
	WARN_ON(!list_empty(&opp_table->opp_list));

	if (!opp_table->supported_hw) {
		dev_err(dev, "%s: Doesn't have supported hardware list\n",
			__func__);
		goto unlock;
	}

	kfree(opp_table->supported_hw);
	opp_table->supported_hw = NULL;
	opp_table->supported_hw_count = 0;

	/* Try freeing opp_table if this was the last blocking resource */
	_remove_opp_table(opp_table);

unlock:
	mutex_unlock(&opp_table_lock);
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
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
int dev_pm_opp_set_prop_name(struct device *dev, const char *name)
{
	struct opp_table *opp_table;
	int ret = 0;

	/* Hold our table modification lock here */
	mutex_lock(&opp_table_lock);

	opp_table = _add_opp_table(dev);
	if (!opp_table) {
		ret = -ENOMEM;
		goto unlock;
	}

	/* Make sure there are no concurrent readers while updating opp_table */
	WARN_ON(!list_empty(&opp_table->opp_list));

	/* Do we already have a prop-name associated with opp_table? */
	if (opp_table->prop_name) {
		dev_err(dev, "%s: Already have prop-name %s\n", __func__,
			opp_table->prop_name);
		ret = -EBUSY;
		goto err;
	}

	opp_table->prop_name = kstrdup(name, GFP_KERNEL);
	if (!opp_table->prop_name) {
		ret = -ENOMEM;
		goto err;
	}

	mutex_unlock(&opp_table_lock);
	return 0;

err:
	_remove_opp_table(opp_table);
unlock:
	mutex_unlock(&opp_table_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_prop_name);

/**
 * dev_pm_opp_put_prop_name() - Releases resources blocked for prop-name
 * @dev: Device for which the prop-name has to be put.
 *
 * This is required only for the V2 bindings, and is called for a matching
 * dev_pm_opp_set_prop_name(). Until this is called, the opp_table structure
 * will not be freed.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
void dev_pm_opp_put_prop_name(struct device *dev)
{
	struct opp_table *opp_table;

	/* Hold our table modification lock here */
	mutex_lock(&opp_table_lock);

	/* Check for existing table for 'dev' first */
	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		dev_err(dev, "Failed to find opp_table: %ld\n",
			PTR_ERR(opp_table));
		goto unlock;
	}

	/* Make sure there are no concurrent readers while updating opp_table */
	WARN_ON(!list_empty(&opp_table->opp_list));

	if (!opp_table->prop_name) {
		dev_err(dev, "%s: Doesn't have a prop-name\n", __func__);
		goto unlock;
	}

	kfree(opp_table->prop_name);
	opp_table->prop_name = NULL;

	/* Try freeing opp_table if this was the last blocking resource */
	_remove_opp_table(opp_table);

unlock:
	mutex_unlock(&opp_table_lock);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_put_prop_name);

/**
 * dev_pm_opp_set_regulator() - Set regulator name for the device
 * @dev: Device for which regulator name is being set.
 * @name: Name of the regulator.
 *
 * In order to support OPP switching, OPP layer needs to know the name of the
 * device's regulator, as the core would be required to switch voltages as well.
 *
 * This must be called before any OPPs are initialized for the device.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
struct opp_table *dev_pm_opp_set_regulator(struct device *dev, const char *name)
{
	struct opp_table *opp_table;
	struct regulator *reg;
	int ret;

	mutex_lock(&opp_table_lock);

	opp_table = _add_opp_table(dev);
	if (!opp_table) {
		ret = -ENOMEM;
		goto unlock;
	}

	/* This should be called before OPPs are initialized */
	if (WARN_ON(!list_empty(&opp_table->opp_list))) {
		ret = -EBUSY;
		goto err;
	}

	/* Already have a regulator set */
	if (WARN_ON(!IS_ERR(opp_table->regulator))) {
		ret = -EBUSY;
		goto err;
	}
	/* Allocate the regulator */
	reg = regulator_get_optional(dev, name);
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "%s: no regulator (%s) found: %d\n",
				__func__, name, ret);
		goto err;
	}

	opp_table->regulator = reg;

	mutex_unlock(&opp_table_lock);
	return opp_table;

err:
	_remove_opp_table(opp_table);
unlock:
	mutex_unlock(&opp_table_lock);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_regulator);

/**
 * dev_pm_opp_put_regulator() - Releases resources blocked for regulator
 * @opp_table: OPP table returned from dev_pm_opp_set_regulator().
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
void dev_pm_opp_put_regulator(struct opp_table *opp_table)
{
	mutex_lock(&opp_table_lock);

	if (IS_ERR(opp_table->regulator)) {
		pr_err("%s: Doesn't have regulator set\n", __func__);
		goto unlock;
	}

	/* Make sure there are no concurrent readers while updating opp_table */
	WARN_ON(!list_empty(&opp_table->opp_list));

	regulator_put(opp_table->regulator);
	opp_table->regulator = ERR_PTR(-ENXIO);

	/* Try freeing opp_table if this was the last blocking resource */
	_remove_opp_table(opp_table);

unlock:
	mutex_unlock(&opp_table_lock);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_put_regulator);

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
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
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
	return _opp_add_v1(dev, freq, u_volt, true);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_add);

/**
 * _opp_set_availability() - helper to set the availability of an opp
 * @dev:		device for which we do this operation
 * @freq:		OPP frequency to modify availability
 * @availability_req:	availability status requested for this opp
 *
 * Set the availability of an OPP with an RCU operation, opp_{enable,disable}
 * share a common logic which is isolated here.
 *
 * Return: -EINVAL for bad pointers, -ENOMEM if no memory available for the
 * copy operation, returns 0 if no modification was done OR modification was
 * successful.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks to
 * keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex locking or synchronize_rcu() blocking calls cannot be used.
 */
static int _opp_set_availability(struct device *dev, unsigned long freq,
				 bool availability_req)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *new_opp, *tmp_opp, *opp = ERR_PTR(-ENODEV);
	int r = 0;

	/* keep the node allocated */
	new_opp = kmalloc(sizeof(*new_opp), GFP_KERNEL);
	if (!new_opp)
		return -ENOMEM;

	mutex_lock(&opp_table_lock);

	/* Find the opp_table */
	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		r = PTR_ERR(opp_table);
		dev_warn(dev, "%s: Device OPP not found (%d)\n", __func__, r);
		goto unlock;
	}

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
	/* copy the old data over */
	*new_opp = *opp;

	/* plug in new node */
	new_opp->available = availability_req;

	list_replace_rcu(&opp->node, &new_opp->node);
	mutex_unlock(&opp_table_lock);
	call_srcu(&opp_table->srcu_head.srcu, &opp->rcu_head, _kfree_opp_rcu);

	/* Notify the change of the OPP availability */
	if (availability_req)
		srcu_notifier_call_chain(&opp_table->srcu_head,
					 OPP_EVENT_ENABLE, new_opp);
	else
		srcu_notifier_call_chain(&opp_table->srcu_head,
					 OPP_EVENT_DISABLE, new_opp);

	return 0;

unlock:
	mutex_unlock(&opp_table_lock);
	kfree(new_opp);
	return r;
}

/**
 * dev_pm_opp_enable() - Enable a specific OPP
 * @dev:	device for which we do this operation
 * @freq:	OPP frequency to enable
 *
 * Enables a provided opp. If the operation is valid, this returns 0, else the
 * corresponding error value. It is meant to be used for users an OPP available
 * after being temporarily made unavailable with dev_pm_opp_disable.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function indirectly uses RCU and mutex locks to keep the
 * integrity of the internal data structures. Callers should ensure that
 * this function is *NOT* called under RCU protection or in contexts where
 * mutex locking or synchronize_rcu() blocking calls cannot be used.
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
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function indirectly uses RCU and mutex locks to keep the
 * integrity of the internal data structures. Callers should ensure that
 * this function is *NOT* called under RCU protection or in contexts where
 * mutex locking or synchronize_rcu() blocking calls cannot be used.
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
 * dev_pm_opp_get_notifier() - find notifier_head of the device with opp
 * @dev:	device pointer used to lookup OPP table.
 *
 * Return: pointer to  notifier head if found, otherwise -ENODEV or
 * -EINVAL based on type of error casted as pointer. value must be checked
 *  with IS_ERR to determine valid pointer or error result.
 *
 * Locking: This function must be called under rcu_read_lock(). opp_table is a
 * RCU protected pointer. The reason for the same is that the opp pointer which
 * is returned will remain valid for use with opp_get_{voltage, freq} only while
 * under the locked area. The pointer returned must be used prior to unlocking
 * with rcu_read_unlock() to maintain the integrity of the pointer.
 */
struct srcu_notifier_head *dev_pm_opp_get_notifier(struct device *dev)
{
	struct opp_table *opp_table = _find_opp_table(dev);

	if (IS_ERR(opp_table))
		return ERR_CAST(opp_table); /* matching type */

	return &opp_table->srcu_head;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_notifier);

/*
 * Free OPPs either created using static entries present in DT or even the
 * dynamically added entries based on remove_all param.
 */
void _dev_pm_opp_remove_table(struct device *dev, bool remove_all)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *opp, *tmp;

	/* Hold our table modification lock here */
	mutex_lock(&opp_table_lock);

	/* Check for existing table for 'dev' */
	opp_table = _find_opp_table(dev);
	if (IS_ERR(opp_table)) {
		int error = PTR_ERR(opp_table);

		if (error != -ENODEV)
			WARN(1, "%s: opp_table: %d\n",
			     IS_ERR_OR_NULL(dev) ?
					"Invalid device" : dev_name(dev),
			     error);
		goto unlock;
	}

	/* Find if opp_table manages a single device */
	if (list_is_singular(&opp_table->dev_list)) {
		/* Free static OPPs */
		list_for_each_entry_safe(opp, tmp, &opp_table->opp_list, node) {
			if (remove_all || !opp->dynamic)
				_opp_remove(opp_table, opp, true);
		}
	} else {
		_remove_opp_dev(_find_opp_dev(dev, opp_table), opp_table);
	}

unlock:
	mutex_unlock(&opp_table_lock);
}

/**
 * dev_pm_opp_remove_table() - Free all OPPs associated with the device
 * @dev:	device pointer used to lookup OPP table.
 *
 * Free both OPPs created using static entries present in DT and the
 * dynamically added entries.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function indirectly uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
void dev_pm_opp_remove_table(struct device *dev)
{
	_dev_pm_opp_remove_table(dev, true);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_remove_table);
