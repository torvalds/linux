/*
 * Support for Partition Mobility/Migration
 *
 * Copyright (C) 2010 Nathan Fontenot
 * Copyright (C) 2010 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/smp.h>
#include <linux/stat.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <asm/machdep.h>
#include <asm/rtas.h>
#include "pseries.h"

static struct kobject *mobility_kobj;

struct update_props_workarea {
	u32 phandle;
	u32 state;
	u64 reserved;
	u32 nprops;
} __packed;

#define NODE_ACTION_MASK	0xff000000
#define NODE_COUNT_MASK		0x00ffffff

#define DELETE_DT_NODE	0x01000000
#define UPDATE_DT_NODE	0x02000000
#define ADD_DT_NODE	0x03000000

#define MIGRATION_SCOPE	(1)

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

static int delete_dt_node(u32 phandle)
{
	struct device_node *dn;

	dn = of_find_node_by_phandle(phandle);
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

static int update_dt_node(u32 phandle, s32 scope)
{
	struct update_props_workarea *upwa;
	struct device_node *dn;
	struct property *prop = NULL;
	int i, rc, rtas_rc;
	char *prop_data;
	char *rtas_buf;
	int update_properties_token;
	u32 vd;

	update_properties_token = rtas_token("ibm,update-properties");
	if (update_properties_token == RTAS_UNKNOWN_SERVICE)
		return -EINVAL;

	rtas_buf = kzalloc(RTAS_DATA_BUF_SIZE, GFP_KERNEL);
	if (!rtas_buf)
		return -ENOMEM;

	dn = of_find_node_by_phandle(phandle);
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

		/* On the first call to ibm,update-properties for a node the
		 * the first property value descriptor contains an empty
		 * property name, the property value length encoded as u32,
		 * and the property value is the node path being updated.
		 */
		if (*prop_data == 0) {
			prop_data++;
			vd = *(u32 *)prop_data;
			prop_data += vd + sizeof(vd);
			upwa->nprops--;
		}

		for (i = 0; i < upwa->nprops; i++) {
			char *prop_name;

			prop_name = prop_data;
			prop_data += strlen(prop_name) + 1;
			vd = *(u32 *)prop_data;
			prop_data += sizeof(vd);

			switch (vd) {
			case 0x00000000:
				/* name only property, nothing to do */
				break;

			case 0x80000000:
				prop = of_find_property(dn, prop_name, NULL);
				of_remove_property(dn, prop);
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
		}
	} while (rtas_rc == 1);

	of_node_put(dn);
	kfree(rtas_buf);
	return 0;
}

static int add_dt_node(u32 parent_phandle, u32 drc_index)
{
	struct device_node *dn;
	struct device_node *parent_dn;
	int rc;

	parent_dn = of_find_node_by_phandle(parent_phandle);
	if (!parent_dn)
		return -ENOENT;

	dn = dlpar_configure_connector(drc_index, parent_dn);
	if (!dn)
		return -ENOENT;

	rc = dlpar_attach_node(dn);
	if (rc)
		dlpar_free_cc_nodes(dn);

	of_node_put(parent_dn);
	return rc;
}

int pseries_devicetree_update(s32 scope)
{
	char *rtas_buf;
	u32 *data;
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

		data = (u32 *)rtas_buf + 4;
		while (*data & NODE_ACTION_MASK) {
			int i;
			u32 action = *data & NODE_ACTION_MASK;
			int node_count = *data & NODE_COUNT_MASK;

			data++;

			for (i = 0; i < node_count; i++) {
				u32 phandle = *data++;
				u32 drc_index;

				switch (action) {
				case DELETE_DT_NODE:
					delete_dt_node(phandle);
					break;
				case UPDATE_DT_NODE:
					update_dt_node(phandle, scope);
					break;
				case ADD_DT_NODE:
					drc_index = *data++;
					add_dt_node(phandle, drc_index);
					break;
				}
			}
		}
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

	rc = pseries_devicetree_update(MIGRATION_SCOPE);
	if (rc)
		printk(KERN_ERR "Post-mobility device tree update "
			"failed: %d\n", rc);

	return;
}

static ssize_t migrate_store(struct class *class, struct class_attribute *attr,
			     const char *buf, size_t count)
{
	struct rtas_args args;
	u64 streamid;
	int rc;

	rc = strict_strtoull(buf, 0, &streamid);
	if (rc)
		return rc;

	memset(&args, 0, sizeof(args));
	args.token = rtas_token("ibm,suspend-me");
	args.nargs = 2;
	args.nret = 1;

	args.args[0] = streamid >> 32 ;
	args.args[1] = streamid & 0xffffffff;
	args.rets = &args.args[args.nargs];

	do {
		args.rets[0] = 0;
		rc = rtas_ibm_suspend_me(&args);
		if (!rc && args.rets[0] == RTAS_NOT_SUSPENDABLE)
			ssleep(1);
	} while (!rc && args.rets[0] == RTAS_NOT_SUSPENDABLE);

	if (rc)
		return rc;
	else if (args.rets[0])
		return args.rets[0];

	post_mobility_fixup();
	return count;
}

static CLASS_ATTR(migration, S_IWUSR, NULL, migrate_store);

static int __init mobility_sysfs_init(void)
{
	int rc;

	mobility_kobj = kobject_create_and_add("mobility", kernel_kobj);
	if (!mobility_kobj)
		return -ENOMEM;

	rc = sysfs_create_file(mobility_kobj, &class_attr_migration.attr);

	return rc;
}
machine_device_initcall(pseries, mobility_sysfs_init);
