/*
 * Support for dynamic reconfiguration for PCI, Memory, and CPU
 * Hotplug and Dynamic Logical Partitioning on RPA platforms.
 *
 * Copyright (C) 2009 Nathan Fontenot
 * Copyright (C) 2009 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include "offline_states.h"

#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/uaccess.h>
#include <asm/rtas.h>
#include <asm/pSeries_reconfig.h>

struct cc_workarea {
	u32	drc_index;
	u32	zero;
	u32	name_offset;
	u32	prop_length;
	u32	prop_offset;
};

void dlpar_free_cc_property(struct property *prop)
{
	kfree(prop->name);
	kfree(prop->value);
	kfree(prop);
}

static struct property *dlpar_parse_cc_property(struct cc_workarea *ccwa)
{
	struct property *prop;
	char *name;
	char *value;

	prop = kzalloc(sizeof(*prop), GFP_KERNEL);
	if (!prop)
		return NULL;

	name = (char *)ccwa + ccwa->name_offset;
	prop->name = kstrdup(name, GFP_KERNEL);

	prop->length = ccwa->prop_length;
	value = (char *)ccwa + ccwa->prop_offset;
	prop->value = kmemdup(value, prop->length, GFP_KERNEL);
	if (!prop->value) {
		dlpar_free_cc_property(prop);
		return NULL;
	}

	return prop;
}

static struct device_node *dlpar_parse_cc_node(struct cc_workarea *ccwa)
{
	struct device_node *dn;
	char *name;

	dn = kzalloc(sizeof(*dn), GFP_KERNEL);
	if (!dn)
		return NULL;

	/* The configure connector reported name does not contain a
	 * preceeding '/', so we allocate a buffer large enough to
	 * prepend this to the full_name.
	 */
	name = (char *)ccwa + ccwa->name_offset;
	dn->full_name = kasprintf(GFP_KERNEL, "/%s", name);
	if (!dn->full_name) {
		kfree(dn);
		return NULL;
	}

	return dn;
}

static void dlpar_free_one_cc_node(struct device_node *dn)
{
	struct property *prop;

	while (dn->properties) {
		prop = dn->properties;
		dn->properties = prop->next;
		dlpar_free_cc_property(prop);
	}

	kfree(dn->full_name);
	kfree(dn);
}

void dlpar_free_cc_nodes(struct device_node *dn)
{
	if (dn->child)
		dlpar_free_cc_nodes(dn->child);

	if (dn->sibling)
		dlpar_free_cc_nodes(dn->sibling);

	dlpar_free_one_cc_node(dn);
}

#define NEXT_SIBLING    1
#define NEXT_CHILD      2
#define NEXT_PROPERTY   3
#define PREV_PARENT     4
#define MORE_MEMORY     5
#define CALL_AGAIN	-2
#define ERR_CFG_USE     -9003

struct device_node *dlpar_configure_connector(u32 drc_index)
{
	struct device_node *dn;
	struct device_node *first_dn = NULL;
	struct device_node *last_dn = NULL;
	struct property *property;
	struct property *last_property = NULL;
	struct cc_workarea *ccwa;
	char *data_buf;
	int cc_token;
	int rc = -1;

	cc_token = rtas_token("ibm,configure-connector");
	if (cc_token == RTAS_UNKNOWN_SERVICE)
		return NULL;

	data_buf = kzalloc(RTAS_DATA_BUF_SIZE, GFP_KERNEL);
	if (!data_buf)
		return NULL;

	ccwa = (struct cc_workarea *)&data_buf[0];
	ccwa->drc_index = drc_index;
	ccwa->zero = 0;

	do {
		/* Since we release the rtas_data_buf lock between configure
		 * connector calls we want to re-populate the rtas_data_buffer
		 * with the contents of the previous call.
		 */
		spin_lock(&rtas_data_buf_lock);

		memcpy(rtas_data_buf, data_buf, RTAS_DATA_BUF_SIZE);
		rc = rtas_call(cc_token, 2, 1, NULL, rtas_data_buf, NULL);
		memcpy(data_buf, rtas_data_buf, RTAS_DATA_BUF_SIZE);

		spin_unlock(&rtas_data_buf_lock);

		switch (rc) {
		case NEXT_SIBLING:
			dn = dlpar_parse_cc_node(ccwa);
			if (!dn)
				goto cc_error;

			dn->parent = last_dn->parent;
			last_dn->sibling = dn;
			last_dn = dn;
			break;

		case NEXT_CHILD:
			dn = dlpar_parse_cc_node(ccwa);
			if (!dn)
				goto cc_error;

			if (!first_dn)
				first_dn = dn;
			else {
				dn->parent = last_dn;
				if (last_dn)
					last_dn->child = dn;
			}

			last_dn = dn;
			break;

		case NEXT_PROPERTY:
			property = dlpar_parse_cc_property(ccwa);
			if (!property)
				goto cc_error;

			if (!last_dn->properties)
				last_dn->properties = property;
			else
				last_property->next = property;

			last_property = property;
			break;

		case PREV_PARENT:
			last_dn = last_dn->parent;
			break;

		case CALL_AGAIN:
			break;

		case MORE_MEMORY:
		case ERR_CFG_USE:
		default:
			printk(KERN_ERR "Unexpected Error (%d) "
			       "returned from configure-connector\n", rc);
			goto cc_error;
		}
	} while (rc);

cc_error:
	kfree(data_buf);

	if (rc) {
		if (first_dn)
			dlpar_free_cc_nodes(first_dn);

		return NULL;
	}

	return first_dn;
}

static struct device_node *derive_parent(const char *path)
{
	struct device_node *parent;
	char *last_slash;

	last_slash = strrchr(path, '/');
	if (last_slash == path) {
		parent = of_find_node_by_path("/");
	} else {
		char *parent_path;
		int parent_path_len = last_slash - path + 1;
		parent_path = kmalloc(parent_path_len, GFP_KERNEL);
		if (!parent_path)
			return NULL;

		strlcpy(parent_path, path, parent_path_len);
		parent = of_find_node_by_path(parent_path);
		kfree(parent_path);
	}

	return parent;
}

int dlpar_attach_node(struct device_node *dn)
{
#ifdef CONFIG_PROC_DEVICETREE
	struct proc_dir_entry *ent;
#endif
	int rc;

	of_node_set_flag(dn, OF_DYNAMIC);
	kref_init(&dn->kref);
	dn->parent = derive_parent(dn->full_name);
	if (!dn->parent)
		return -ENOMEM;

	rc = blocking_notifier_call_chain(&pSeries_reconfig_chain,
					  PSERIES_RECONFIG_ADD, dn);
	if (rc == NOTIFY_BAD) {
		printk(KERN_ERR "Failed to add device node %s\n",
		       dn->full_name);
		return -ENOMEM; /* For now, safe to assume kmalloc failure */
	}

	of_attach_node(dn);

#ifdef CONFIG_PROC_DEVICETREE
	ent = proc_mkdir(strrchr(dn->full_name, '/') + 1, dn->parent->pde);
	if (ent)
		proc_device_tree_add_node(dn, ent);
#endif

	of_node_put(dn->parent);
	return 0;
}

int dlpar_detach_node(struct device_node *dn)
{
#ifdef CONFIG_PROC_DEVICETREE
	struct device_node *parent = dn->parent;
	struct property *prop = dn->properties;

	while (prop) {
		remove_proc_entry(prop->name, dn->pde);
		prop = prop->next;
	}

	if (dn->pde)
		remove_proc_entry(dn->pde->name, parent->pde);
#endif

	blocking_notifier_call_chain(&pSeries_reconfig_chain,
			    PSERIES_RECONFIG_REMOVE, dn);
	of_detach_node(dn);
	of_node_put(dn); /* Must decrement the refcount */

	return 0;
}

#define DR_ENTITY_SENSE		9003
#define DR_ENTITY_PRESENT	1
#define DR_ENTITY_UNUSABLE	2
#define ALLOCATION_STATE	9003
#define ALLOC_UNUSABLE		0
#define ALLOC_USABLE		1
#define ISOLATION_STATE		9001
#define ISOLATE			0
#define UNISOLATE		1

int dlpar_acquire_drc(u32 drc_index)
{
	int dr_status, rc;

	rc = rtas_call(rtas_token("get-sensor-state"), 2, 2, &dr_status,
		       DR_ENTITY_SENSE, drc_index);
	if (rc || dr_status != DR_ENTITY_UNUSABLE)
		return -1;

	rc = rtas_set_indicator(ALLOCATION_STATE, drc_index, ALLOC_USABLE);
	if (rc)
		return rc;

	rc = rtas_set_indicator(ISOLATION_STATE, drc_index, UNISOLATE);
	if (rc) {
		rtas_set_indicator(ALLOCATION_STATE, drc_index, ALLOC_UNUSABLE);
		return rc;
	}

	return 0;
}

int dlpar_release_drc(u32 drc_index)
{
	int dr_status, rc;

	rc = rtas_call(rtas_token("get-sensor-state"), 2, 2, &dr_status,
		       DR_ENTITY_SENSE, drc_index);
	if (rc || dr_status != DR_ENTITY_PRESENT)
		return -1;

	rc = rtas_set_indicator(ISOLATION_STATE, drc_index, ISOLATE);
	if (rc)
		return rc;

	rc = rtas_set_indicator(ALLOCATION_STATE, drc_index, ALLOC_UNUSABLE);
	if (rc) {
		rtas_set_indicator(ISOLATION_STATE, drc_index, UNISOLATE);
		return rc;
	}

	return 0;
}

#ifdef CONFIG_ARCH_CPU_PROBE_RELEASE

static int dlpar_online_cpu(struct device_node *dn)
{
	int rc = 0;
	unsigned int cpu;
	int len, nthreads, i;
	const u32 *intserv;

	intserv = of_get_property(dn, "ibm,ppc-interrupt-server#s", &len);
	if (!intserv)
		return -EINVAL;

	nthreads = len / sizeof(u32);

	cpu_maps_update_begin();
	for (i = 0; i < nthreads; i++) {
		for_each_present_cpu(cpu) {
			if (get_hard_smp_processor_id(cpu) != intserv[i])
				continue;
			BUG_ON(get_cpu_current_state(cpu)
					!= CPU_STATE_OFFLINE);
			cpu_maps_update_done();
			rc = cpu_up(cpu);
			if (rc)
				goto out;
			cpu_maps_update_begin();

			break;
		}
		if (cpu == num_possible_cpus())
			printk(KERN_WARNING "Could not find cpu to online "
			       "with physical id 0x%x\n", intserv[i]);
	}
	cpu_maps_update_done();

out:
	return rc;

}

static ssize_t dlpar_cpu_probe(const char *buf, size_t count)
{
	struct device_node *dn;
	unsigned long drc_index;
	char *cpu_name;
	int rc;

	cpu_hotplug_driver_lock();
	rc = strict_strtoul(buf, 0, &drc_index);
	if (rc) {
		rc = -EINVAL;
		goto out;
	}

	dn = dlpar_configure_connector(drc_index);
	if (!dn) {
		rc = -EINVAL;
		goto out;
	}

	/* configure-connector reports cpus as living in the base
	 * directory of the device tree.  CPUs actually live in the
	 * cpus directory so we need to fixup the full_name.
	 */
	cpu_name = kasprintf(GFP_KERNEL, "/cpus%s", dn->full_name);
	if (!cpu_name) {
		dlpar_free_cc_nodes(dn);
		rc = -ENOMEM;
		goto out;
	}

	kfree(dn->full_name);
	dn->full_name = cpu_name;

	rc = dlpar_acquire_drc(drc_index);
	if (rc) {
		dlpar_free_cc_nodes(dn);
		rc = -EINVAL;
		goto out;
	}

	rc = dlpar_attach_node(dn);
	if (rc) {
		dlpar_release_drc(drc_index);
		dlpar_free_cc_nodes(dn);
		goto out;
	}

	rc = dlpar_online_cpu(dn);
out:
	cpu_hotplug_driver_unlock();

	return rc ? rc : count;
}

static int dlpar_offline_cpu(struct device_node *dn)
{
	int rc = 0;
	unsigned int cpu;
	int len, nthreads, i;
	const u32 *intserv;

	intserv = of_get_property(dn, "ibm,ppc-interrupt-server#s", &len);
	if (!intserv)
		return -EINVAL;

	nthreads = len / sizeof(u32);

	cpu_maps_update_begin();
	for (i = 0; i < nthreads; i++) {
		for_each_present_cpu(cpu) {
			if (get_hard_smp_processor_id(cpu) != intserv[i])
				continue;

			if (get_cpu_current_state(cpu) == CPU_STATE_OFFLINE)
				break;

			if (get_cpu_current_state(cpu) == CPU_STATE_ONLINE) {
				set_preferred_offline_state(cpu, CPU_STATE_OFFLINE);
				cpu_maps_update_done();
				rc = cpu_down(cpu);
				if (rc)
					goto out;
				cpu_maps_update_begin();
				break;

			}

			/*
			 * The cpu is in CPU_STATE_INACTIVE.
			 * Upgrade it's state to CPU_STATE_OFFLINE.
			 */
			set_preferred_offline_state(cpu, CPU_STATE_OFFLINE);
			BUG_ON(plpar_hcall_norets(H_PROD, intserv[i])
								!= H_SUCCESS);
			__cpu_die(cpu);
			break;
		}
		if (cpu == num_possible_cpus())
			printk(KERN_WARNING "Could not find cpu to offline "
			       "with physical id 0x%x\n", intserv[i]);
	}
	cpu_maps_update_done();

out:
	return rc;

}

static ssize_t dlpar_cpu_release(const char *buf, size_t count)
{
	struct device_node *dn;
	const u32 *drc_index;
	int rc;

	dn = of_find_node_by_path(buf);
	if (!dn)
		return -EINVAL;

	drc_index = of_get_property(dn, "ibm,my-drc-index", NULL);
	if (!drc_index) {
		of_node_put(dn);
		return -EINVAL;
	}

	cpu_hotplug_driver_lock();
	rc = dlpar_offline_cpu(dn);
	if (rc) {
		of_node_put(dn);
		rc = -EINVAL;
		goto out;
	}

	rc = dlpar_release_drc(*drc_index);
	if (rc) {
		of_node_put(dn);
		goto out;
	}

	rc = dlpar_detach_node(dn);
	if (rc) {
		dlpar_acquire_drc(*drc_index);
		goto out;
	}

	of_node_put(dn);
out:
	cpu_hotplug_driver_unlock();
	return rc ? rc : count;
}

static int __init pseries_dlpar_init(void)
{
	ppc_md.cpu_probe = dlpar_cpu_probe;
	ppc_md.cpu_release = dlpar_cpu_release;

	return 0;
}
machine_device_initcall(pseries, pseries_dlpar_init);

#endif /* CONFIG_ARCH_CPU_PROBE_RELEASE */
