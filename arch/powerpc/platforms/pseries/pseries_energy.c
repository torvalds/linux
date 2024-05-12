// SPDX-License-Identifier: GPL-2.0-only
/*
 * POWER platform energy management driver
 * Copyright (C) 2010 IBM Corporation
 *
 * This pseries platform device driver provides access to
 * platform energy management capabilities.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <asm/cputhreads.h>
#include <asm/page.h>
#include <asm/hvcall.h>
#include <asm/firmware.h>
#include <asm/prom.h>


#define MODULE_VERS "1.0"
#define MODULE_NAME "pseries_energy"

/* Driver flags */

static int sysfs_entries;

/* Helper routines */

/* Helper Routines to convert between drc_index to cpu numbers */

static u32 cpu_to_drc_index(int cpu)
{
	struct device_node *dn = NULL;
	struct property *info;
	int thread_index;
	int rc = 1;
	u32 ret = 0;

	dn = of_find_node_by_path("/cpus");
	if (dn == NULL)
		goto err;

	/* Convert logical cpu number to core number */
	thread_index = cpu_core_index_of_thread(cpu);

	info = of_find_property(dn, "ibm,drc-info", NULL);
	if (info) {
		struct of_drc_info drc;
		int j;
		u32 num_set_entries;
		const __be32 *value;

		value = of_prop_next_u32(info, NULL, &num_set_entries);
		if (!value)
			goto err_of_node_put;
		else
			value++;

		for (j = 0; j < num_set_entries; j++) {

			of_read_drc_info_cell(&info, &value, &drc);
			if (strncmp(drc.drc_type, "CPU", 3))
				goto err;

			if (thread_index < drc.last_drc_index)
				break;
		}

		ret = drc.drc_index_start + (thread_index * drc.sequential_inc);
	} else {
		u32 nr_drc_indexes, thread_drc_index;

		/*
		 * The first element of ibm,drc-indexes array is the
		 * number of drc_indexes returned in the list.  Hence
		 * thread_index+1 will get the drc_index corresponding
		 * to core number thread_index.
		 */
		rc = of_property_read_u32_index(dn, "ibm,drc-indexes",
						0, &nr_drc_indexes);
		if (rc)
			goto err_of_node_put;

		WARN_ON_ONCE(thread_index > nr_drc_indexes);
		rc = of_property_read_u32_index(dn, "ibm,drc-indexes",
						thread_index + 1,
						&thread_drc_index);
		if (rc)
			goto err_of_node_put;

		ret = thread_drc_index;
	}

	rc = 0;

err_of_node_put:
	of_node_put(dn);
err:
	if (rc)
		printk(KERN_WARNING "cpu_to_drc_index(%d) failed", cpu);
	return ret;
}

static int drc_index_to_cpu(u32 drc_index)
{
	struct device_node *dn = NULL;
	struct property *info;
	const int *indexes;
	int thread_index = 0, cpu = 0;
	int rc = 1;

	dn = of_find_node_by_path("/cpus");
	if (dn == NULL)
		goto err;
	info = of_find_property(dn, "ibm,drc-info", NULL);
	if (info) {
		struct of_drc_info drc;
		int j;
		u32 num_set_entries;
		const __be32 *value;

		value = of_prop_next_u32(info, NULL, &num_set_entries);
		if (!value)
			goto err_of_node_put;
		else
			value++;

		for (j = 0; j < num_set_entries; j++) {

			of_read_drc_info_cell(&info, &value, &drc);
			if (strncmp(drc.drc_type, "CPU", 3))
				goto err;

			if (drc_index > drc.last_drc_index) {
				cpu += drc.num_sequential_elems;
				continue;
			}
			cpu += ((drc_index - drc.drc_index_start) /
				drc.sequential_inc);

			thread_index = cpu_first_thread_of_core(cpu);
			rc = 0;
			break;
		}
	} else {
		unsigned long int i;

		indexes = of_get_property(dn, "ibm,drc-indexes", NULL);
		if (indexes == NULL)
			goto err_of_node_put;
		/*
		 * First element in the array is the number of drc_indexes
		 * returned.  Search through the list to find the matching
		 * drc_index and get the core number
		 */
		for (i = 0; i < indexes[0]; i++) {
			if (indexes[i + 1] == drc_index)
				break;
		}
		/* Convert core number to logical cpu number */
		thread_index = cpu_first_thread_of_core(i);
		rc = 0;
	}

err_of_node_put:
	of_node_put(dn);
err:
	if (rc)
		printk(KERN_WARNING "drc_index_to_cpu(%d) failed", drc_index);
	return thread_index;
}

/*
 * pseries hypervisor call H_BEST_ENERGY provides hints to OS on
 * preferred logical cpus to activate or deactivate for optimized
 * energy consumption.
 */

#define FLAGS_MODE1	0x004E200000080E01UL
#define FLAGS_MODE2	0x004E200000080401UL
#define FLAGS_ACTIVATE  0x100

static ssize_t get_best_energy_list(char *page, int activate)
{
	int rc, cnt, i, cpu;
	unsigned long retbuf[PLPAR_HCALL9_BUFSIZE];
	unsigned long flags = 0;
	u32 *buf_page;
	char *s = page;

	buf_page = (u32 *) get_zeroed_page(GFP_KERNEL);
	if (!buf_page)
		return -ENOMEM;

	flags = FLAGS_MODE1;
	if (activate)
		flags |= FLAGS_ACTIVATE;

	rc = plpar_hcall9(H_BEST_ENERGY, retbuf, flags, 0, __pa(buf_page),
				0, 0, 0, 0, 0, 0);
	if (rc != H_SUCCESS) {
		free_page((unsigned long) buf_page);
		return -EINVAL;
	}

	cnt = retbuf[0];
	for (i = 0; i < cnt; i++) {
		cpu = drc_index_to_cpu(buf_page[2*i+1]);
		if ((cpu_online(cpu) && !activate) ||
		    (!cpu_online(cpu) && activate))
			s += sprintf(s, "%d,", cpu);
	}
	if (s > page) { /* Something to show */
		s--; /* Suppress last comma */
		s += sprintf(s, "\n");
	}

	free_page((unsigned long) buf_page);
	return s-page;
}

static ssize_t get_best_energy_data(struct device *dev,
					char *page, int activate)
{
	int rc;
	unsigned long retbuf[PLPAR_HCALL9_BUFSIZE];
	unsigned long flags = 0;

	flags = FLAGS_MODE2;
	if (activate)
		flags |= FLAGS_ACTIVATE;

	rc = plpar_hcall9(H_BEST_ENERGY, retbuf, flags,
				cpu_to_drc_index(dev->id),
				0, 0, 0, 0, 0, 0, 0);

	if (rc != H_SUCCESS)
		return -EINVAL;

	return sprintf(page, "%lu\n", retbuf[1] >> 32);
}

/* Wrapper functions */

static ssize_t cpu_activate_hint_list_show(struct device *dev,
			struct device_attribute *attr, char *page)
{
	return get_best_energy_list(page, 1);
}

static ssize_t cpu_deactivate_hint_list_show(struct device *dev,
			struct device_attribute *attr, char *page)
{
	return get_best_energy_list(page, 0);
}

static ssize_t percpu_activate_hint_show(struct device *dev,
			struct device_attribute *attr, char *page)
{
	return get_best_energy_data(dev, page, 1);
}

static ssize_t percpu_deactivate_hint_show(struct device *dev,
			struct device_attribute *attr, char *page)
{
	return get_best_energy_data(dev, page, 0);
}

/*
 * Create sysfs interface:
 * /sys/devices/system/cpu/pseries_activate_hint_list
 * /sys/devices/system/cpu/pseries_deactivate_hint_list
 *	Comma separated list of cpus to activate or deactivate
 * /sys/devices/system/cpu/cpuN/pseries_activate_hint
 * /sys/devices/system/cpu/cpuN/pseries_deactivate_hint
 *	Per-cpu value of the hint
 */

static struct device_attribute attr_cpu_activate_hint_list =
		__ATTR(pseries_activate_hint_list, 0444,
		cpu_activate_hint_list_show, NULL);

static struct device_attribute attr_cpu_deactivate_hint_list =
		__ATTR(pseries_deactivate_hint_list, 0444,
		cpu_deactivate_hint_list_show, NULL);

static struct device_attribute attr_percpu_activate_hint =
		__ATTR(pseries_activate_hint, 0444,
		percpu_activate_hint_show, NULL);

static struct device_attribute attr_percpu_deactivate_hint =
		__ATTR(pseries_deactivate_hint, 0444,
		percpu_deactivate_hint_show, NULL);

static int __init pseries_energy_init(void)
{
	int cpu, err;
	struct device *cpu_dev, *dev_root;

	if (!firmware_has_feature(FW_FEATURE_BEST_ENERGY))
		return 0; /* H_BEST_ENERGY hcall not supported */

	/* Create the sysfs files */
	dev_root = bus_get_dev_root(&cpu_subsys);
	if (dev_root) {
		err = device_create_file(dev_root, &attr_cpu_activate_hint_list);
		if (!err)
			err = device_create_file(dev_root, &attr_cpu_deactivate_hint_list);
		put_device(dev_root);
		if (err)
			return err;
	}

	for_each_possible_cpu(cpu) {
		cpu_dev = get_cpu_device(cpu);
		err = device_create_file(cpu_dev,
				&attr_percpu_activate_hint);
		if (err)
			break;
		err = device_create_file(cpu_dev,
				&attr_percpu_deactivate_hint);
		if (err)
			break;
	}

	if (err)
		return err;

	sysfs_entries = 1; /* Removed entries on cleanup */
	return 0;

}

static void __exit pseries_energy_cleanup(void)
{
	int cpu;
	struct device *cpu_dev, *dev_root;

	if (!sysfs_entries)
		return;

	/* Remove the sysfs files */
	dev_root = bus_get_dev_root(&cpu_subsys);
	if (dev_root) {
		device_remove_file(dev_root, &attr_cpu_activate_hint_list);
		device_remove_file(dev_root, &attr_cpu_deactivate_hint_list);
		put_device(dev_root);
	}

	for_each_possible_cpu(cpu) {
		cpu_dev = get_cpu_device(cpu);
		sysfs_remove_file(&cpu_dev->kobj,
				&attr_percpu_activate_hint.attr);
		sysfs_remove_file(&cpu_dev->kobj,
				&attr_percpu_deactivate_hint.attr);
	}
}

module_init(pseries_energy_init);
module_exit(pseries_energy_cleanup);
MODULE_DESCRIPTION("Driver for pSeries platform energy management");
MODULE_AUTHOR("Vaidyanathan Srinivasan");
MODULE_LICENSE("GPL");
