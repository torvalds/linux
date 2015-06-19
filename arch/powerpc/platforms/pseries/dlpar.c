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

#define pr_fmt(fmt)	"dlpar: " fmt

#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/of.h>
#include "offline_states.h"
#include "pseries.h"

#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/uaccess.h>
#include <asm/rtas.h>

struct cc_workarea {
	__be32	drc_index;
	__be32	zero;
	__be32	name_offset;
	__be32	prop_length;
	__be32	prop_offset;
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

	name = (char *)ccwa + be32_to_cpu(ccwa->name_offset);
	prop->name = kstrdup(name, GFP_KERNEL);

	prop->length = be32_to_cpu(ccwa->prop_length);
	value = (char *)ccwa + be32_to_cpu(ccwa->prop_offset);
	prop->value = kmemdup(value, prop->length, GFP_KERNEL);
	if (!prop->value) {
		dlpar_free_cc_property(prop);
		return NULL;
	}

	return prop;
}

static struct device_node *dlpar_parse_cc_node(struct cc_workarea *ccwa,
					       const char *path)
{
	struct device_node *dn;
	char *name;

	/* If parent node path is "/" advance path to NULL terminator to
	 * prevent double leading slashs in full_name.
	 */
	if (!path[1])
		path++;

	dn = kzalloc(sizeof(*dn), GFP_KERNEL);
	if (!dn)
		return NULL;

	name = (char *)ccwa + be32_to_cpu(ccwa->name_offset);
	dn->full_name = kasprintf(GFP_KERNEL, "%s/%s", path, name);
	if (!dn->full_name) {
		kfree(dn);
		return NULL;
	}

	of_node_set_flag(dn, OF_DYNAMIC);
	of_node_init(dn);

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

#define COMPLETE	0
#define NEXT_SIBLING    1
#define NEXT_CHILD      2
#define NEXT_PROPERTY   3
#define PREV_PARENT     4
#define MORE_MEMORY     5
#define CALL_AGAIN	-2
#define ERR_CFG_USE     -9003

struct device_node *dlpar_configure_connector(__be32 drc_index,
					      struct device_node *parent)
{
	struct device_node *dn;
	struct device_node *first_dn = NULL;
	struct device_node *last_dn = NULL;
	struct property *property;
	struct property *last_property = NULL;
	struct cc_workarea *ccwa;
	char *data_buf;
	const char *parent_path = parent->full_name;
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
		case COMPLETE:
			break;

		case NEXT_SIBLING:
			dn = dlpar_parse_cc_node(ccwa, parent_path);
			if (!dn)
				goto cc_error;

			dn->parent = last_dn->parent;
			last_dn->sibling = dn;
			last_dn = dn;
			break;

		case NEXT_CHILD:
			if (first_dn)
				parent_path = last_dn->full_name;

			dn = dlpar_parse_cc_node(ccwa, parent_path);
			if (!dn)
				goto cc_error;

			if (!first_dn) {
				dn->parent = parent;
				first_dn = dn;
			} else {
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
			parent_path = last_dn->parent->full_name;
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
	int rc;

	dn->parent = derive_parent(dn->full_name);
	if (!dn->parent)
		return -ENOMEM;

	rc = of_attach_node(dn);
	if (rc) {
		printk(KERN_ERR "Failed to add device node %s\n",
		       dn->full_name);
		return rc;
	}

	of_node_put(dn->parent);
	return 0;
}

int dlpar_detach_node(struct device_node *dn)
{
	struct device_node *child;
	int rc;

	child = of_get_next_child(dn, NULL);
	while (child) {
		dlpar_detach_node(child);
		child = of_get_next_child(dn, child);
	}

	rc = of_detach_node(dn);
	if (rc)
		return rc;

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
	const __be32 *intserv;
	u32 thread;

	intserv = of_get_property(dn, "ibm,ppc-interrupt-server#s", &len);
	if (!intserv)
		return -EINVAL;

	nthreads = len / sizeof(u32);

	cpu_maps_update_begin();
	for (i = 0; i < nthreads; i++) {
		thread = be32_to_cpu(intserv[i]);
		for_each_present_cpu(cpu) {
			if (get_hard_smp_processor_id(cpu) != thread)
				continue;
			BUG_ON(get_cpu_current_state(cpu)
					!= CPU_STATE_OFFLINE);
			cpu_maps_update_done();
			rc = device_online(get_cpu_device(cpu));
			if (rc)
				goto out;
			cpu_maps_update_begin();

			break;
		}
		if (cpu == num_possible_cpus())
			printk(KERN_WARNING "Could not find cpu to online "
			       "with physical id 0x%x\n", thread);
	}
	cpu_maps_update_done();

out:
	return rc;

}

static ssize_t dlpar_cpu_probe(const char *buf, size_t count)
{
	struct device_node *dn, *parent;
	u32 drc_index;
	int rc;

	rc = kstrtou32(buf, 0, &drc_index);
	if (rc)
		return -EINVAL;

	parent = of_find_node_by_path("/cpus");
	if (!parent)
		return -ENODEV;

	dn = dlpar_configure_connector(cpu_to_be32(drc_index), parent);
	if (!dn)
		return -EINVAL;

	of_node_put(parent);

	rc = dlpar_acquire_drc(drc_index);
	if (rc) {
		dlpar_free_cc_nodes(dn);
		return -EINVAL;
	}

	rc = dlpar_attach_node(dn);
	if (rc) {
		dlpar_release_drc(drc_index);
		dlpar_free_cc_nodes(dn);
		return rc;
	}

	rc = dlpar_online_cpu(dn);
	if (rc)
		return rc;

	return count;
}

static int dlpar_offline_cpu(struct device_node *dn)
{
	int rc = 0;
	unsigned int cpu;
	int len, nthreads, i;
	const __be32 *intserv;
	u32 thread;

	intserv = of_get_property(dn, "ibm,ppc-interrupt-server#s", &len);
	if (!intserv)
		return -EINVAL;

	nthreads = len / sizeof(u32);

	cpu_maps_update_begin();
	for (i = 0; i < nthreads; i++) {
		thread = be32_to_cpu(intserv[i]);
		for_each_present_cpu(cpu) {
			if (get_hard_smp_processor_id(cpu) != thread)
				continue;

			if (get_cpu_current_state(cpu) == CPU_STATE_OFFLINE)
				break;

			if (get_cpu_current_state(cpu) == CPU_STATE_ONLINE) {
				set_preferred_offline_state(cpu, CPU_STATE_OFFLINE);
				cpu_maps_update_done();
				rc = device_offline(get_cpu_device(cpu));
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
			BUG_ON(plpar_hcall_norets(H_PROD, thread)
								!= H_SUCCESS);
			__cpu_die(cpu);
			break;
		}
		if (cpu == num_possible_cpus())
			printk(KERN_WARNING "Could not find cpu to offline "
			       "with physical id 0x%x\n", thread);
	}
	cpu_maps_update_done();

out:
	return rc;

}

static ssize_t dlpar_cpu_release(const char *buf, size_t count)
{
	struct device_node *dn;
	u32 drc_index;
	int rc;

	dn = of_find_node_by_path(buf);
	if (!dn)
		return -EINVAL;

	rc = of_property_read_u32(dn, "ibm,my-drc-index", &drc_index);
	if (rc) {
		of_node_put(dn);
		return -EINVAL;
	}

	rc = dlpar_offline_cpu(dn);
	if (rc) {
		of_node_put(dn);
		return -EINVAL;
	}

	rc = dlpar_release_drc(drc_index);
	if (rc) {
		of_node_put(dn);
		return rc;
	}

	rc = dlpar_detach_node(dn);
	if (rc) {
		dlpar_acquire_drc(drc_index);
		return rc;
	}

	of_node_put(dn);

	return count;
}

#endif /* CONFIG_ARCH_CPU_PROBE_RELEASE */

static int handle_dlpar_errorlog(struct pseries_hp_errorlog *hp_elog)
{
	int rc;

	/* pseries error logs are in BE format, convert to cpu type */
	switch (hp_elog->id_type) {
	case PSERIES_HP_ELOG_ID_DRC_COUNT:
		hp_elog->_drc_u.drc_count =
					be32_to_cpu(hp_elog->_drc_u.drc_count);
		break;
	case PSERIES_HP_ELOG_ID_DRC_INDEX:
		hp_elog->_drc_u.drc_index =
					be32_to_cpu(hp_elog->_drc_u.drc_index);
	}

	switch (hp_elog->resource) {
	case PSERIES_HP_ELOG_RESOURCE_MEM:
		rc = dlpar_memory(hp_elog);
		break;
	default:
		pr_warn_ratelimited("Invalid resource (%d) specified\n",
				    hp_elog->resource);
		rc = -EINVAL;
	}

	return rc;
}

static ssize_t dlpar_store(struct class *class, struct class_attribute *attr,
			   const char *buf, size_t count)
{
	struct pseries_hp_errorlog *hp_elog;
	const char *arg;
	int rc;

	hp_elog = kzalloc(sizeof(*hp_elog), GFP_KERNEL);
	if (!hp_elog) {
		rc = -ENOMEM;
		goto dlpar_store_out;
	}

	/* Parse out the request from the user, this will be in the form
	 * <resource> <action> <id_type> <id>
	 */
	arg = buf;
	if (!strncmp(arg, "memory", 6)) {
		hp_elog->resource = PSERIES_HP_ELOG_RESOURCE_MEM;
		arg += strlen("memory ");
	} else {
		pr_err("Invalid resource specified: \"%s\"\n", buf);
		rc = -EINVAL;
		goto dlpar_store_out;
	}

	if (!strncmp(arg, "add", 3)) {
		hp_elog->action = PSERIES_HP_ELOG_ACTION_ADD;
		arg += strlen("add ");
	} else if (!strncmp(arg, "remove", 6)) {
		hp_elog->action = PSERIES_HP_ELOG_ACTION_REMOVE;
		arg += strlen("remove ");
	} else {
		pr_err("Invalid action specified: \"%s\"\n", buf);
		rc = -EINVAL;
		goto dlpar_store_out;
	}

	if (!strncmp(arg, "index", 5)) {
		u32 index;

		hp_elog->id_type = PSERIES_HP_ELOG_ID_DRC_INDEX;
		arg += strlen("index ");
		if (kstrtou32(arg, 0, &index)) {
			rc = -EINVAL;
			pr_err("Invalid drc_index specified: \"%s\"\n", buf);
			goto dlpar_store_out;
		}

		hp_elog->_drc_u.drc_index = cpu_to_be32(index);
	} else if (!strncmp(arg, "count", 5)) {
		u32 count;

		hp_elog->id_type = PSERIES_HP_ELOG_ID_DRC_COUNT;
		arg += strlen("count ");
		if (kstrtou32(arg, 0, &count)) {
			rc = -EINVAL;
			pr_err("Invalid count specified: \"%s\"\n", buf);
			goto dlpar_store_out;
		}

		hp_elog->_drc_u.drc_count = cpu_to_be32(count);
	} else {
		pr_err("Invalid id_type specified: \"%s\"\n", buf);
		rc = -EINVAL;
		goto dlpar_store_out;
	}

	rc = handle_dlpar_errorlog(hp_elog);

dlpar_store_out:
	kfree(hp_elog);
	return rc ? rc : count;
}

static CLASS_ATTR(dlpar, S_IWUSR, NULL, dlpar_store);

static int __init pseries_dlpar_init(void)
{
	int rc;

#ifdef CONFIG_ARCH_CPU_PROBE_RELEASE
	ppc_md.cpu_probe = dlpar_cpu_probe;
	ppc_md.cpu_release = dlpar_cpu_release;
#endif /* CONFIG_ARCH_CPU_PROBE_RELEASE */

	rc = sysfs_create_file(kernel_kobj, &class_attr_dlpar.attr);

	return rc;
}
machine_device_initcall(pseries, pseries_dlpar_init);

