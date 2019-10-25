// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for Partition Mobility/Migration
 *
 * Copyright (C) 2010 Nathan Fontenot
 * Copyright (C) 2010 IBM Corporation
 */

#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/stat.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/stringify.h>

#include <asm/machdep.h>
#include <asm/rtas.h>
#include "pseries.h"
#include "../../kernel/cacheinfo.h"

static struct kobject *mobility_kobj;

struct update_props_workarea {
	__be32 phandle;
	__be32 state;
	__be64 reserved;
	__be32 nprops;
} __packed;

#define NODE_ACTION_MASK	0xff000000
#define NODE_COUNT_MASK		0x00ffffff

#define DELETE_DT_NODE	0x01000000
#define UPDATE_DT_NODE	0x02000000
#define ADD_DT_NODE	0x03000000

#define MIGRATION_SCOPE	(1)
#define PRRN_SCOPE -2

static int mobility_rtas_call(int token, char *buf, s32 scope)
{
	int rc;

	spin_lock(&rtas_data_buf_lock);

	memcpy(rtas_data_buf, buf, RTAS_DATA_BUF_SIZE);
	rc = rtas_call(token, 2, 1, NULL, rtas_data_buf, scope);
	memcpy(buf, rtas_data_buf, RTAS_DATA_BUF_SIZE);

	spin_unlock(&rtas_data_buf_lock);
	return rc;
}

static int delete_dt_node(__be32 phandle)
{
	struct device_node *dn;

	dn = of_find_node_by_phandle(be32_to_cpu(phandle));
	if (!dn)
		return -ENOENT;

	dlpar_detach_node(dn);
	of_node_put(dn);
	return 0;
}

static int update_dt_property(struct device_node *dn, struct property **prop,
			      const char *name, u32 vd, char *value)
{
	struct property *new_prop = *prop;
	int more = 0;

	/* A negative 'vd' value indicates that only part of the new property
	 * value is contained in the buffer and we need to call
	 * ibm,update-properties again to get the rest of the value.
	 *
	 * A negative value is also the two's compliment of the actual value.
	 */
	if (vd & 0x80000000) {
		vd = ~vd + 1;
		more = 1;
	}

	if (new_prop) {
		/* partial property fixup */
		char *new_data = kzalloc(new_prop->length + vd, GFP_KERNEL);
		if (!new_data)
			return -ENOMEM;

		memcpy(new_data, new_prop->value, new_prop->length);
		memcpy(new_data + new_prop->length, value, vd);

		kfree(new_prop->value);
		new_prop->value = new_data;
		new_prop->length += vd;
	} else {
		new_prop = kzalloc(sizeof(*new_prop), GFP_KERNEL);
		if (!new_prop)
			return -ENOMEM;

		new_prop->name = kstrdup(name, GFP_KERNEL);
		if (!new_prop->name) {
			kfree(new_prop);
			return -ENOMEM;
		}

		new_prop->length = vd;
		new_prop->value = kzalloc(new_prop->length, GFP_KERNEL);
		if (!new_prop->value) {
			kfree(new_prop->name);
			kfree(new_prop);
			return -ENOMEM;
		}

		memcpy(new_prop->value, value, vd);
		*prop = new_prop;
	}

	if (!more) {
		of_update_property(dn, new_prop);
		*prop = NULL;
	}

	return 0;
}

static int update_dt_node(__be32 phandle, s32 scope)
{
	struct update_props_workarea *upwa;
	struct device_node *dn;
	struct property *prop = NULL;
	int i, rc, rtas_rc;
	char *prop_data;
	char *rtas_buf;
	int update_properties_token;
	u32 nprops;
	u32 vd;

	update_properties_token = rtas_token("ibm,update-properties");
	if (update_properties_token == RTAS_UNKNOWN_SERVICE)
		return -EINVAL;

	rtas_buf = kzalloc(RTAS_DATA_BUF_SIZE, GFP_KERNEL);
	if (!rtas_buf)
		return -ENOMEM;

	dn = of_find_node_by_phandle(be32_to_cpu(phandle));
	if (!dn) {
		kfree(rtas_buf);
		return -ENOENT;
	}

	upwa = (struct update_props_workarea *)&rtas_buf[0];
	upwa->phandle = phandle;

	do {
		rtas_rc = mobility_rtas_call(update_properties_token, rtas_buf,
					scope);
		if (rtas_rc < 0)
			break;

		prop_data = rtas_buf + sizeof(*upwa);
		nprops = be32_to_cpu(upwa->nprops);

		/* On the first call to ibm,update-properties for a node the
		 * the first property value descriptor contains an empty
		 * property name, the property value length encoded as u32,
		 * and the property value is the node path being updated.
		 */
		if (*prop_data == 0) {
			prop_data++;
			vd = be32_to_cpu(*(__be32 *)prop_data);
			prop_data += vd + sizeof(vd);
			nprops--;
		}

		for (i = 0; i < nprops; i++) {
			char *prop_name;

			prop_name = prop_data;
			prop_data += strlen(prop_name) + 1;
			vd = be32_to_cpu(*(__be32 *)prop_data);
			prop_data += sizeof(vd);

			switch (vd) {
			case 0x00000000:
				/* name only property, nothing to do */
				break;

			case 0x80000000:
				of_remove_property(dn, of_find_property(dn,
							prop_name, NULL));
				prop = NULL;
				break;

			default:
				rc = update_dt_property(dn, &prop, prop_name,
							vd, prop_data);
				if (rc) {
					printk(KERN_ERR "Could not update %s"
					       " property\n", prop_name);
				}

				prop_data += vd;
			}

			cond_resched();
		}

		cond_resched();
	} while (rtas_rc == 1);

	of_node_put(dn);
	kfree(rtas_buf);
	return 0;
}

static int add_dt_node(__be32 parent_phandle, __be32 drc_index)
{
	struct device_node *dn;
	struct device_node *parent_dn;
	int rc;

	parent_dn = of_find_node_by_phandle(be32_to_cpu(parent_phandle));
	if (!parent_dn)
		return -ENOENT;

	dn = dlpar_configure_connector(drc_index, parent_dn);
	if (!dn) {
		of_node_put(parent_dn);
		return -ENOENT;
	}

	rc = dlpar_attach_node(dn, parent_dn);
	if (rc)
		dlpar_free_cc_nodes(dn);

	of_node_put(parent_dn);
	return rc;
}

static void prrn_update_node(__be32 phandle)
{
	struct pseries_hp_errorlog hp_elog;
	struct device_node *dn;

	/*
	 * If a node is found from a the given phandle, the phandle does not
	 * represent the drc index of an LMB and we can ignore.
	 */
	dn = of_find_node_by_phandle(be32_to_cpu(phandle));
	if (dn) {
		of_node_put(dn);
		return;
	}

	hp_elog.resource = PSERIES_HP_ELOG_RESOURCE_MEM;
	hp_elog.action = PSERIES_HP_ELOG_ACTION_READD;
	hp_elog.id_type = PSERIES_HP_ELOG_ID_DRC_INDEX;
	hp_elog._drc_u.drc_index = phandle;

	handle_dlpar_errorlog(&hp_elog);
}

int pseries_devicetree_update(s32 scope)
{
	char *rtas_buf;
	__be32 *data;
	int update_nodes_token;
	int rc;

	update_nodes_token = rtas_token("ibm,update-nodes");
	if (update_nodes_token == RTAS_UNKNOWN_SERVICE)
		return -EINVAL;

	rtas_buf = kzalloc(RTAS_DATA_BUF_SIZE, GFP_KERNEL);
	if (!rtas_buf)
		return -ENOMEM;

	do {
		rc = mobility_rtas_call(update_nodes_token, rtas_buf, scope);
		if (rc && rc != 1)
			break;

		data = (__be32 *)rtas_buf + 4;
		while (be32_to_cpu(*data) & NODE_ACTION_MASK) {
			int i;
			u32 action = be32_to_cpu(*data) & NODE_ACTION_MASK;
			u32 node_count = be32_to_cpu(*data) & NODE_COUNT_MASK;

			data++;

			for (i = 0; i < node_count; i++) {
				__be32 phandle = *data++;
				__be32 drc_index;

				switch (action) {
				case DELETE_DT_NODE:
					delete_dt_node(phandle);
					break;
				case UPDATE_DT_NODE:
					update_dt_node(phandle, scope);

					if (scope == PRRN_SCOPE)
						prrn_update_node(phandle);

					break;
				case ADD_DT_NODE:
					drc_index = *data++;
					add_dt_node(phandle, drc_index);
					break;
				}

				cond_resched();
			}
		}

		cond_resched();
	} while (rc == 1);

	kfree(rtas_buf);
	return rc;
}

void post_mobility_fixup(void)
{
	int rc;
	int activate_fw_token;

	activate_fw_token = rtas_token("ibm,activate-firmware");
	if (activate_fw_token == RTAS_UNKNOWN_SERVICE) {
		printk(KERN_ERR "Could not make post-mobility "
		       "activate-fw call.\n");
		return;
	}

	do {
		rc = rtas_call(activate_fw_token, 0, 1, NULL);
	} while (rtas_busy_delay(rc));

	if (rc)
		printk(KERN_ERR "Post-mobility activate-fw failed: %d\n", rc);

	/*
	 * We don't want CPUs to go online/offline while the device
	 * tree is being updated.
	 */
	cpus_read_lock();

	/*
	 * It's common for the destination firmware to replace cache
	 * nodes.  Release all of the cacheinfo hierarchy's references
	 * before updating the device tree.
	 */
	cacheinfo_teardown();

	rc = pseries_devicetree_update(MIGRATION_SCOPE);
	if (rc)
		printk(KERN_ERR "Post-mobility device tree update "
			"failed: %d\n", rc);

	cacheinfo_rebuild();

	cpus_read_unlock();

	/* Possibly switch to a new RFI flush type */
	pseries_setup_rfi_flush();

	return;
}

static ssize_t migration_store(struct class *class,
			       struct class_attribute *attr, const char *buf,
			       size_t count)
{
	u64 streamid;
	int rc;

	rc = kstrtou64(buf, 0, &streamid);
	if (rc)
		return rc;

	stop_topology_update();

	do {
		rc = rtas_ibm_suspend_me(streamid);
		if (rc == -EAGAIN)
			ssleep(1);
	} while (rc == -EAGAIN);

	if (rc)
		return rc;

	post_mobility_fixup();

	start_topology_update();

	return count;
}

/*
 * Used by drmgr to determine the kernel behavior of the migration interface.
 *
 * Version 1: Performs all PAPR requirements for migration including
 *	firmware activation and device tree update.
 */
#define MIGRATION_API_VERSION	1

static CLASS_ATTR_WO(migration);
static CLASS_ATTR_STRING(api_version, 0444, __stringify(MIGRATION_API_VERSION));

static int __init mobility_sysfs_init(void)
{
	int rc;

	mobility_kobj = kobject_create_and_add("mobility", kernel_kobj);
	if (!mobility_kobj)
		return -ENOMEM;

	rc = sysfs_create_file(mobility_kobj, &class_attr_migration.attr);
	if (rc)
		pr_err("mobility: unable to create migration sysfs file (%d)\n", rc);

	rc = sysfs_create_file(mobility_kobj, &class_attr_api_version.attr.attr);
	if (rc)
		pr_err("mobility: unable to create api_version sysfs file (%d)\n", rc);

	return 0;
}
machine_device_initcall(pseries, mobility_sysfs_init);
