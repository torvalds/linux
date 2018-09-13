/*
 * Generic OPP helper interface for CPU device
 *
 * Copyright (C) 2009-2014 Texas Instruments Incorporated.
 *	Nishanth Menon
 *	Romit Dasgupta
 *	Kevin Hilman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/slab.h>

#include "opp.h"

#ifdef CONFIG_CPU_FREQ

/**
 * dev_pm_opp_init_cpufreq_table() - create a cpufreq table for a device
 * @dev:	device for which we do this operation
 * @table:	Cpufreq table returned back to caller
 *
 * Generate a cpufreq table for a provided device- this assumes that the
 * opp table is already initialized and ready for usage.
 *
 * This function allocates required memory for the cpufreq table. It is
 * expected that the caller does the required maintenance such as freeing
 * the table as required.
 *
 * Returns -EINVAL for bad pointers, -ENODEV if the device is not found, -ENOMEM
 * if no memory available for the operation (table is not populated), returns 0
 * if successful and table is populated.
 *
 * WARNING: It is  important for the callers to ensure refreshing their copy of
 * the table if any of the mentioned functions have been invoked in the interim.
 */
int dev_pm_opp_init_cpufreq_table(struct device *dev,
				  struct cpufreq_frequency_table **table)
{
	struct dev_pm_opp *opp;
	struct cpufreq_frequency_table *freq_table = NULL;
	int i, max_opps, ret = 0;
	unsigned long rate;

	max_opps = dev_pm_opp_get_opp_count(dev);
	if (max_opps <= 0)
		return max_opps ? max_opps : -ENODATA;

	freq_table = kcalloc((max_opps + 1), sizeof(*freq_table), GFP_KERNEL);
	if (!freq_table)
		return -ENOMEM;

	for (i = 0, rate = 0; i < max_opps; i++, rate++) {
		/* find next rate */
		opp = dev_pm_opp_find_freq_ceil(dev, &rate);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			goto out;
		}
		freq_table[i].driver_data = i;
		freq_table[i].frequency = rate / 1000;

		/* Is Boost/turbo opp ? */
		if (dev_pm_opp_is_turbo(opp))
			freq_table[i].flags = CPUFREQ_BOOST_FREQ;

		dev_pm_opp_put(opp);
	}

	freq_table[i].driver_data = i;
	freq_table[i].frequency = CPUFREQ_TABLE_END;

	*table = &freq_table[0];

out:
	if (ret)
		kfree(freq_table);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_init_cpufreq_table);

/**
 * dev_pm_opp_free_cpufreq_table() - free the cpufreq table
 * @dev:	device for which we do this operation
 * @table:	table to free
 *
 * Free up the table allocated by dev_pm_opp_init_cpufreq_table
 */
void dev_pm_opp_free_cpufreq_table(struct device *dev,
				   struct cpufreq_frequency_table **table)
{
	if (!table)
		return;

	kfree(*table);
	*table = NULL;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_free_cpufreq_table);
#endif	/* CONFIG_CPU_FREQ */

void _dev_pm_opp_cpumask_remove_table(const struct cpumask *cpumask, bool of,
				      int last_cpu)
{
	struct device *cpu_dev;
	int cpu;

	WARN_ON(cpumask_empty(cpumask));

	for_each_cpu(cpu, cpumask) {
		if (cpu == last_cpu)
			break;

		cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev) {
			pr_err("%s: failed to get cpu%d device\n", __func__,
			       cpu);
			continue;
		}

		if (of)
			dev_pm_opp_of_remove_table(cpu_dev);
		else
			dev_pm_opp_remove_table(cpu_dev);
	}
}

/**
 * dev_pm_opp_cpumask_remove_table() - Removes OPP table for @cpumask
 * @cpumask:	cpumask for which OPP table needs to be removed
 *
 * This removes the OPP tables for CPUs present in the @cpumask.
 * This should be used to remove all the OPPs entries associated with
 * the cpus in @cpumask.
 */
void dev_pm_opp_cpumask_remove_table(const struct cpumask *cpumask)
{
	_dev_pm_opp_cpumask_remove_table(cpumask, false, -1);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_cpumask_remove_table);

/**
 * dev_pm_opp_set_sharing_cpus() - Mark OPP table as shared by few CPUs
 * @cpu_dev:	CPU device for which we do this operation
 * @cpumask:	cpumask of the CPUs which share the OPP table with @cpu_dev
 *
 * This marks OPP table of the @cpu_dev as shared by the CPUs present in
 * @cpumask.
 *
 * Returns -ENODEV if OPP table isn't already present.
 */
int dev_pm_opp_set_sharing_cpus(struct device *cpu_dev,
				const struct cpumask *cpumask)
{
	struct opp_device *opp_dev;
	struct opp_table *opp_table;
	struct device *dev;
	int cpu, ret = 0;

	opp_table = _find_opp_table(cpu_dev);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	for_each_cpu(cpu, cpumask) {
		if (cpu == cpu_dev->id)
			continue;

		dev = get_cpu_device(cpu);
		if (!dev) {
			dev_err(cpu_dev, "%s: failed to get cpu%d device\n",
				__func__, cpu);
			continue;
		}

		opp_dev = _add_opp_dev(dev, opp_table);
		if (!opp_dev) {
			dev_err(dev, "%s: failed to add opp-dev for cpu%d device\n",
				__func__, cpu);
			continue;
		}

		/* Mark opp-table as multiple CPUs are sharing it now */
		opp_table->shared_opp = OPP_TABLE_ACCESS_SHARED;
	}

	dev_pm_opp_put_opp_table(opp_table);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_sharing_cpus);

/**
 * dev_pm_opp_get_sharing_cpus() - Get cpumask of CPUs sharing OPPs with @cpu_dev
 * @cpu_dev:	CPU device for which we do this operation
 * @cpumask:	cpumask to update with information of sharing CPUs
 *
 * This updates the @cpumask with CPUs that are sharing OPPs with @cpu_dev.
 *
 * Returns -ENODEV if OPP table isn't already present and -EINVAL if the OPP
 * table's status is access-unknown.
 */
int dev_pm_opp_get_sharing_cpus(struct device *cpu_dev, struct cpumask *cpumask)
{
	struct opp_device *opp_dev;
	struct opp_table *opp_table;
	int ret = 0;

	opp_table = _find_opp_table(cpu_dev);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	if (opp_table->shared_opp == OPP_TABLE_ACCESS_UNKNOWN) {
		ret = -EINVAL;
		goto put_opp_table;
	}

	cpumask_clear(cpumask);

	if (opp_table->shared_opp == OPP_TABLE_ACCESS_SHARED) {
		list_for_each_entry(opp_dev, &opp_table->dev_list, node)
			cpumask_set_cpu(opp_dev->dev->id, cpumask);
	} else {
		cpumask_set_cpu(cpu_dev->id, cpumask);
	}

put_opp_table:
	dev_pm_opp_put_opp_table(opp_table);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_sharing_cpus);
