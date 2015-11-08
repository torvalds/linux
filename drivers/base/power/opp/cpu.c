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
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/of.h>
#include <linux/slab.h>

#include "opp.h"

#ifdef CONFIG_CPU_FREQ

/**
 * dev_pm_opp_init_cpufreq_table() - create a cpufreq table for a device
 * @dev:	device for which we do this operation
 * @table:	Cpufreq table returned back to caller
 *
 * Generate a cpufreq table for a provided device- this assumes that the
 * opp list is already initialized and ready for usage.
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
 *
 * Locking: The internal device_opp and opp structures are RCU protected.
 * Since we just use the regular accessor functions to access the internal data
 * structures, we use RCU read lock inside this function. As a result, users of
 * this function DONOT need to use explicit locks for invoking.
 */
int dev_pm_opp_init_cpufreq_table(struct device *dev,
				  struct cpufreq_frequency_table **table)
{
	struct dev_pm_opp *opp;
	struct cpufreq_frequency_table *freq_table = NULL;
	int i, max_opps, ret = 0;
	unsigned long rate;

	rcu_read_lock();

	max_opps = dev_pm_opp_get_opp_count(dev);
	if (max_opps <= 0) {
		ret = max_opps ? max_opps : -ENODATA;
		goto out;
	}

	freq_table = kcalloc((max_opps + 1), sizeof(*freq_table), GFP_ATOMIC);
	if (!freq_table) {
		ret = -ENOMEM;
		goto out;
	}

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
	}

	freq_table[i].driver_data = i;
	freq_table[i].frequency = CPUFREQ_TABLE_END;

	*table = &freq_table[0];

out:
	rcu_read_unlock();
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

/* Required only for V1 bindings, as v2 can manage it from DT itself */
int dev_pm_opp_set_sharing_cpus(struct device *cpu_dev, cpumask_var_t cpumask)
{
	struct device_list_opp *list_dev;
	struct device_opp *dev_opp;
	struct device *dev;
	int cpu, ret = 0;

	rcu_read_lock();

	dev_opp = _find_device_opp(cpu_dev);
	if (IS_ERR(dev_opp)) {
		ret = -EINVAL;
		goto out_rcu_read_unlock;
	}

	for_each_cpu(cpu, cpumask) {
		if (cpu == cpu_dev->id)
			continue;

		dev = get_cpu_device(cpu);
		if (!dev) {
			dev_err(cpu_dev, "%s: failed to get cpu%d device\n",
				__func__, cpu);
			continue;
		}

		list_dev = _add_list_dev(dev, dev_opp);
		if (!list_dev) {
			dev_err(dev, "%s: failed to add list-dev for cpu%d device\n",
				__func__, cpu);
			continue;
		}
	}
out_rcu_read_unlock:
	rcu_read_unlock();

	return 0;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_sharing_cpus);

#ifdef CONFIG_OF
void dev_pm_opp_of_cpumask_remove_table(cpumask_var_t cpumask)
{
	struct device *cpu_dev;
	int cpu;

	WARN_ON(cpumask_empty(cpumask));

	for_each_cpu(cpu, cpumask) {
		cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev) {
			pr_err("%s: failed to get cpu%d device\n", __func__,
			       cpu);
			continue;
		}

		dev_pm_opp_of_remove_table(cpu_dev);
	}
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_cpumask_remove_table);

int dev_pm_opp_of_cpumask_add_table(cpumask_var_t cpumask)
{
	struct device *cpu_dev;
	int cpu, ret = 0;

	WARN_ON(cpumask_empty(cpumask));

	for_each_cpu(cpu, cpumask) {
		cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev) {
			pr_err("%s: failed to get cpu%d device\n", __func__,
			       cpu);
			continue;
		}

		ret = dev_pm_opp_of_add_table(cpu_dev);
		if (ret) {
			pr_err("%s: couldn't find opp table for cpu:%d, %d\n",
			       __func__, cpu, ret);

			/* Free all other OPPs */
			dev_pm_opp_of_cpumask_remove_table(cpumask);
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_cpumask_add_table);

/*
 * Works only for OPP v2 bindings.
 *
 * cpumask should be already set to mask of cpu_dev->id.
 * Returns -ENOENT if operating-points-v2 bindings aren't supported.
 */
int dev_pm_opp_of_get_sharing_cpus(struct device *cpu_dev, cpumask_var_t cpumask)
{
	struct device_node *np, *tmp_np;
	struct device *tcpu_dev;
	int cpu, ret = 0;

	/* Get OPP descriptor node */
	np = _of_get_opp_desc_node(cpu_dev);
	if (!np) {
		dev_dbg(cpu_dev, "%s: Couldn't find cpu_dev node.\n", __func__);
		return -ENOENT;
	}

	/* OPPs are shared ? */
	if (!of_property_read_bool(np, "opp-shared"))
		goto put_cpu_node;

	for_each_possible_cpu(cpu) {
		if (cpu == cpu_dev->id)
			continue;

		tcpu_dev = get_cpu_device(cpu);
		if (!tcpu_dev) {
			dev_err(cpu_dev, "%s: failed to get cpu%d device\n",
				__func__, cpu);
			ret = -ENODEV;
			goto put_cpu_node;
		}

		/* Get OPP descriptor node */
		tmp_np = _of_get_opp_desc_node(tcpu_dev);
		if (!tmp_np) {
			dev_err(tcpu_dev, "%s: Couldn't find tcpu_dev node.\n",
				__func__);
			ret = -ENOENT;
			goto put_cpu_node;
		}

		/* CPUs are sharing opp node */
		if (np == tmp_np)
			cpumask_set_cpu(cpu, cpumask);

		of_node_put(tmp_np);
	}

put_cpu_node:
	of_node_put(np);
	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_get_sharing_cpus);
#endif
