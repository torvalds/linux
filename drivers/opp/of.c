/*
 * Generic OPP OF helpers
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

#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/export.h>

#include "opp.h"

static struct opp_table *_managed_opp(const struct device_node *np)
{
	struct opp_table *opp_table, *managed_table = NULL;

	mutex_lock(&opp_table_lock);

	list_for_each_entry(opp_table, &opp_tables, node) {
		if (opp_table->np == np) {
			/*
			 * Multiple devices can point to the same OPP table and
			 * so will have same node-pointer, np.
			 *
			 * But the OPPs will be considered as shared only if the
			 * OPP table contains a "opp-shared" property.
			 */
			if (opp_table->shared_opp == OPP_TABLE_ACCESS_SHARED) {
				_get_opp_table_kref(opp_table);
				managed_table = opp_table;
			}

			break;
		}
	}

	mutex_unlock(&opp_table_lock);

	return managed_table;
}

void _of_init_opp_table(struct opp_table *opp_table, struct device *dev)
{
	struct device_node *np;

	/*
	 * Only required for backward compatibility with v1 bindings, but isn't
	 * harmful for other cases. And so we do it unconditionally.
	 */
	np = of_node_get(dev->of_node);
	if (np) {
		u32 val;

		if (!of_property_read_u32(np, "clock-latency", &val))
			opp_table->clock_latency_ns_max = val;
		of_property_read_u32(np, "voltage-tolerance",
				     &opp_table->voltage_tolerance_v1);
		of_node_put(np);
	}
}

static bool _opp_is_supported(struct device *dev, struct opp_table *opp_table,
			      struct device_node *np)
{
	unsigned int count = opp_table->supported_hw_count;
	u32 version;
	int ret;

	if (!opp_table->supported_hw) {
		/*
		 * In the case that no supported_hw has been set by the
		 * platform but there is an opp-supported-hw value set for
		 * an OPP then the OPP should not be enabled as there is
		 * no way to see if the hardware supports it.
		 */
		if (of_find_property(np, "opp-supported-hw", NULL))
			return false;
		else
			return true;
	}

	while (count--) {
		ret = of_property_read_u32_index(np, "opp-supported-hw", count,
						 &version);
		if (ret) {
			dev_warn(dev, "%s: failed to read opp-supported-hw property at index %d: %d\n",
				 __func__, count, ret);
			return false;
		}

		/* Both of these are bitwise masks of the versions */
		if (!(version & opp_table->supported_hw[count]))
			return false;
	}

	return true;
}

static int opp_parse_supplies(struct dev_pm_opp *opp, struct device *dev,
			      struct opp_table *opp_table)
{
	u32 *microvolt, *microamp = NULL;
	int supplies, vcount, icount, ret, i, j;
	struct property *prop = NULL;
	char name[NAME_MAX];

	supplies = opp_table->regulator_count ? opp_table->regulator_count : 1;

	/* Search for "opp-microvolt-<name>" */
	if (opp_table->prop_name) {
		snprintf(name, sizeof(name), "opp-microvolt-%s",
			 opp_table->prop_name);
		prop = of_find_property(opp->np, name, NULL);
	}

	if (!prop) {
		/* Search for "opp-microvolt" */
		sprintf(name, "opp-microvolt");
		prop = of_find_property(opp->np, name, NULL);

		/* Missing property isn't a problem, but an invalid entry is */
		if (!prop) {
			if (!opp_table->regulator_count)
				return 0;

			dev_err(dev, "%s: opp-microvolt missing although OPP managing regulators\n",
				__func__);
			return -EINVAL;
		}
	}

	vcount = of_property_count_u32_elems(opp->np, name);
	if (vcount < 0) {
		dev_err(dev, "%s: Invalid %s property (%d)\n",
			__func__, name, vcount);
		return vcount;
	}

	/* There can be one or three elements per supply */
	if (vcount != supplies && vcount != supplies * 3) {
		dev_err(dev, "%s: Invalid number of elements in %s property (%d) with supplies (%d)\n",
			__func__, name, vcount, supplies);
		return -EINVAL;
	}

	microvolt = kmalloc_array(vcount, sizeof(*microvolt), GFP_KERNEL);
	if (!microvolt)
		return -ENOMEM;

	ret = of_property_read_u32_array(opp->np, name, microvolt, vcount);
	if (ret) {
		dev_err(dev, "%s: error parsing %s: %d\n", __func__, name, ret);
		ret = -EINVAL;
		goto free_microvolt;
	}

	/* Search for "opp-microamp-<name>" */
	prop = NULL;
	if (opp_table->prop_name) {
		snprintf(name, sizeof(name), "opp-microamp-%s",
			 opp_table->prop_name);
		prop = of_find_property(opp->np, name, NULL);
	}

	if (!prop) {
		/* Search for "opp-microamp" */
		sprintf(name, "opp-microamp");
		prop = of_find_property(opp->np, name, NULL);
	}

	if (prop) {
		icount = of_property_count_u32_elems(opp->np, name);
		if (icount < 0) {
			dev_err(dev, "%s: Invalid %s property (%d)\n", __func__,
				name, icount);
			ret = icount;
			goto free_microvolt;
		}

		if (icount != supplies) {
			dev_err(dev, "%s: Invalid number of elements in %s property (%d) with supplies (%d)\n",
				__func__, name, icount, supplies);
			ret = -EINVAL;
			goto free_microvolt;
		}

		microamp = kmalloc_array(icount, sizeof(*microamp), GFP_KERNEL);
		if (!microamp) {
			ret = -EINVAL;
			goto free_microvolt;
		}

		ret = of_property_read_u32_array(opp->np, name, microamp,
						 icount);
		if (ret) {
			dev_err(dev, "%s: error parsing %s: %d\n", __func__,
				name, ret);
			ret = -EINVAL;
			goto free_microamp;
		}
	}

	for (i = 0, j = 0; i < supplies; i++) {
		opp->supplies[i].u_volt = microvolt[j++];

		if (vcount == supplies) {
			opp->supplies[i].u_volt_min = opp->supplies[i].u_volt;
			opp->supplies[i].u_volt_max = opp->supplies[i].u_volt;
		} else {
			opp->supplies[i].u_volt_min = microvolt[j++];
			opp->supplies[i].u_volt_max = microvolt[j++];
		}

		if (microamp)
			opp->supplies[i].u_amp = microamp[i];
	}

free_microamp:
	kfree(microamp);
free_microvolt:
	kfree(microvolt);

	return ret;
}

/**
 * dev_pm_opp_of_remove_table() - Free OPP table entries created from static DT
 *				  entries
 * @dev:	device pointer used to lookup OPP table.
 *
 * Free OPPs created using static entries present in DT.
 */
void dev_pm_opp_of_remove_table(struct device *dev)
{
	_dev_pm_opp_find_and_remove_table(dev, false);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_remove_table);

/* Returns opp descriptor node for a device node, caller must
 * do of_node_put() */
static struct device_node *_opp_of_get_opp_desc_node(struct device_node *np)
{
	/*
	 * There should be only ONE phandle present in "operating-points-v2"
	 * property.
	 */

	return of_parse_phandle(np, "operating-points-v2", 0);
}

/* Returns opp descriptor node for a device, caller must do of_node_put() */
struct device_node *dev_pm_opp_of_get_opp_desc_node(struct device *dev)
{
	return _opp_of_get_opp_desc_node(dev->of_node);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_get_opp_desc_node);

/**
 * _opp_add_static_v2() - Allocate static OPPs (As per 'v2' DT bindings)
 * @opp_table:	OPP table
 * @dev:	device for which we do this operation
 * @np:		device node
 *
 * This function adds an opp definition to the opp table and returns status. The
 * opp can be controlled using dev_pm_opp_enable/disable functions and may be
 * removed by dev_pm_opp_remove.
 *
 * Return:
 * 0		On success OR
 *		Duplicate OPPs (both freq and volt are same) and opp->available
 * -EEXIST	Freq are same and volt are different OR
 *		Duplicate OPPs (both freq and volt are same) and !opp->available
 * -ENOMEM	Memory allocation failure
 * -EINVAL	Failed parsing the OPP node
 */
static int _opp_add_static_v2(struct opp_table *opp_table, struct device *dev,
			      struct device_node *np)
{
	struct dev_pm_opp *new_opp;
	u64 rate;
	u32 val;
	int ret;

	new_opp = _opp_allocate(opp_table);
	if (!new_opp)
		return -ENOMEM;

	ret = of_property_read_u64(np, "opp-hz", &rate);
	if (ret < 0) {
		dev_err(dev, "%s: opp-hz not found\n", __func__);
		goto free_opp;
	}

	/* Check if the OPP supports hardware's hierarchy of versions or not */
	if (!_opp_is_supported(dev, opp_table, np)) {
		dev_dbg(dev, "OPP not supported by hardware: %llu\n", rate);
		goto free_opp;
	}

	/*
	 * Rate is defined as an unsigned long in clk API, and so casting
	 * explicitly to its type. Must be fixed once rate is 64 bit
	 * guaranteed in clk API.
	 */
	new_opp->rate = (unsigned long)rate;
	new_opp->turbo = of_property_read_bool(np, "turbo-mode");

	new_opp->np = np;
	new_opp->dynamic = false;
	new_opp->available = true;

	if (!of_property_read_u32(np, "clock-latency-ns", &val))
		new_opp->clock_latency_ns = val;

	ret = opp_parse_supplies(new_opp, dev, opp_table);
	if (ret)
		goto free_opp;

	ret = _opp_add(dev, new_opp, opp_table);
	if (ret) {
		/* Don't return error for duplicate OPPs */
		if (ret == -EBUSY)
			ret = 0;
		goto free_opp;
	}

	/* OPP to select on device suspend */
	if (of_property_read_bool(np, "opp-suspend")) {
		if (opp_table->suspend_opp) {
			dev_warn(dev, "%s: Multiple suspend OPPs found (%lu %lu)\n",
				 __func__, opp_table->suspend_opp->rate,
				 new_opp->rate);
		} else {
			new_opp->suspend = true;
			opp_table->suspend_opp = new_opp;
		}
	}

	if (new_opp->clock_latency_ns > opp_table->clock_latency_ns_max)
		opp_table->clock_latency_ns_max = new_opp->clock_latency_ns;

	pr_debug("%s: turbo:%d rate:%lu uv:%lu uvmin:%lu uvmax:%lu latency:%lu\n",
		 __func__, new_opp->turbo, new_opp->rate,
		 new_opp->supplies[0].u_volt, new_opp->supplies[0].u_volt_min,
		 new_opp->supplies[0].u_volt_max, new_opp->clock_latency_ns);

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

/* Initializes OPP tables based on new bindings */
static int _of_add_opp_table_v2(struct device *dev, struct device_node *opp_np)
{
	struct device_node *np;
	struct opp_table *opp_table;
	int ret = 0, count = 0;

	opp_table = _managed_opp(opp_np);
	if (opp_table) {
		/* OPPs are already managed */
		if (!_add_opp_dev(dev, opp_table))
			ret = -ENOMEM;
		goto put_opp_table;
	}

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (!opp_table)
		return -ENOMEM;

	/* We have opp-table node now, iterate over it and add OPPs */
	for_each_available_child_of_node(opp_np, np) {
		count++;

		ret = _opp_add_static_v2(opp_table, dev, np);
		if (ret) {
			dev_err(dev, "%s: Failed to add OPP, %d\n", __func__,
				ret);
			_dev_pm_opp_remove_table(opp_table, dev, false);
			of_node_put(np);
			goto put_opp_table;
		}
	}

	/* There should be one of more OPP defined */
	if (WARN_ON(!count)) {
		ret = -ENOENT;
		goto put_opp_table;
	}

	opp_table->np = opp_np;
	if (of_property_read_bool(opp_np, "opp-shared"))
		opp_table->shared_opp = OPP_TABLE_ACCESS_SHARED;
	else
		opp_table->shared_opp = OPP_TABLE_ACCESS_EXCLUSIVE;

put_opp_table:
	dev_pm_opp_put_opp_table(opp_table);

	return ret;
}

/* Initializes OPP tables based on old-deprecated bindings */
static int _of_add_opp_table_v1(struct device *dev)
{
	struct opp_table *opp_table;
	const struct property *prop;
	const __be32 *val;
	int nr, ret = 0;

	prop = of_find_property(dev->of_node, "operating-points", NULL);
	if (!prop)
		return -ENODEV;
	if (!prop->value)
		return -ENODATA;

	/*
	 * Each OPP is a set of tuples consisting of frequency and
	 * voltage like <freq-kHz vol-uV>.
	 */
	nr = prop->length / sizeof(u32);
	if (nr % 2) {
		dev_err(dev, "%s: Invalid OPP table\n", __func__);
		return -EINVAL;
	}

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (!opp_table)
		return -ENOMEM;

	val = prop->value;
	while (nr) {
		unsigned long freq = be32_to_cpup(val++) * 1000;
		unsigned long volt = be32_to_cpup(val++);

		ret = _opp_add_v1(opp_table, dev, freq, volt, false);
		if (ret) {
			dev_err(dev, "%s: Failed to add OPP %ld (%d)\n",
				__func__, freq, ret);
			_dev_pm_opp_remove_table(opp_table, dev, false);
			break;
		}
		nr -= 2;
	}

	dev_pm_opp_put_opp_table(opp_table);
	return ret;
}

/**
 * dev_pm_opp_of_add_table() - Initialize opp table from device tree
 * @dev:	device pointer used to lookup OPP table.
 *
 * Register the initial OPP table with the OPP library for given device.
 *
 * Return:
 * 0		On success OR
 *		Duplicate OPPs (both freq and volt are same) and opp->available
 * -EEXIST	Freq are same and volt are different OR
 *		Duplicate OPPs (both freq and volt are same) and !opp->available
 * -ENOMEM	Memory allocation failure
 * -ENODEV	when 'operating-points' property is not found or is invalid data
 *		in device node.
 * -ENODATA	when empty 'operating-points' property is found
 * -EINVAL	when invalid entries are found in opp-v2 table
 */
int dev_pm_opp_of_add_table(struct device *dev)
{
	struct device_node *opp_np;
	int ret;

	/*
	 * OPPs have two version of bindings now. The older one is deprecated,
	 * try for the new binding first.
	 */
	opp_np = dev_pm_opp_of_get_opp_desc_node(dev);
	if (!opp_np) {
		/*
		 * Try old-deprecated bindings for backward compatibility with
		 * older dtbs.
		 */
		return _of_add_opp_table_v1(dev);
	}

	ret = _of_add_opp_table_v2(dev, opp_np);
	of_node_put(opp_np);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_add_table);

/* CPU device specific helpers */

/**
 * dev_pm_opp_of_cpumask_remove_table() - Removes OPP table for @cpumask
 * @cpumask:	cpumask for which OPP table needs to be removed
 *
 * This removes the OPP tables for CPUs present in the @cpumask.
 * This should be used only to remove static entries created from DT.
 */
void dev_pm_opp_of_cpumask_remove_table(const struct cpumask *cpumask)
{
	_dev_pm_opp_cpumask_remove_table(cpumask, true);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_cpumask_remove_table);

/**
 * dev_pm_opp_of_cpumask_add_table() - Adds OPP table for @cpumask
 * @cpumask:	cpumask for which OPP table needs to be added.
 *
 * This adds the OPP tables for CPUs present in the @cpumask.
 */
int dev_pm_opp_of_cpumask_add_table(const struct cpumask *cpumask)
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
			/*
			 * OPP may get registered dynamically, don't print error
			 * message here.
			 */
			pr_debug("%s: couldn't find opp table for cpu:%d, %d\n",
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
 * Returns -ENOENT if operating-points-v2 bindings aren't supported.
 */
/**
 * dev_pm_opp_of_get_sharing_cpus() - Get cpumask of CPUs sharing OPPs with
 *				      @cpu_dev using operating-points-v2
 *				      bindings.
 *
 * @cpu_dev:	CPU device for which we do this operation
 * @cpumask:	cpumask to update with information of sharing CPUs
 *
 * This updates the @cpumask with CPUs that are sharing OPPs with @cpu_dev.
 *
 * Returns -ENOENT if operating-points-v2 isn't present for @cpu_dev.
 */
int dev_pm_opp_of_get_sharing_cpus(struct device *cpu_dev,
				   struct cpumask *cpumask)
{
	struct device_node *np, *tmp_np, *cpu_np;
	int cpu, ret = 0;

	/* Get OPP descriptor node */
	np = dev_pm_opp_of_get_opp_desc_node(cpu_dev);
	if (!np) {
		dev_dbg(cpu_dev, "%s: Couldn't find opp node.\n", __func__);
		return -ENOENT;
	}

	cpumask_set_cpu(cpu_dev->id, cpumask);

	/* OPPs are shared ? */
	if (!of_property_read_bool(np, "opp-shared"))
		goto put_cpu_node;

	for_each_possible_cpu(cpu) {
		if (cpu == cpu_dev->id)
			continue;

		cpu_np = of_cpu_device_node_get(cpu);
		if (!cpu_np) {
			dev_err(cpu_dev, "%s: failed to get cpu%d node\n",
				__func__, cpu);
			ret = -ENOENT;
			goto put_cpu_node;
		}

		/* Get OPP descriptor node */
		tmp_np = _opp_of_get_opp_desc_node(cpu_np);
		of_node_put(cpu_np);
		if (!tmp_np) {
			pr_err("%pOF: Couldn't find opp node\n", cpu_np);
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
