/*
 * Copyright 2010 Benjamin Herrenschmidt, IBM Corp
 *                <benh@kernel.crashing.org>
 *     and        David Gibson, IBM Corporation.
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <asm/debug.h>
#include <asm/prom.h>
#include <asm/scom.h>

const struct scom_controller *scom_controller;
EXPORT_SYMBOL_GPL(scom_controller);

struct device_node *scom_find_parent(struct device_node *node)
{
	struct device_node *par, *tmp;
	const u32 *p;

	for (par = of_node_get(node); par;) {
		if (of_get_property(par, "scom-controller", NULL))
			break;
		p = of_get_property(par, "scom-parent", NULL);
		tmp = par;
		if (p == NULL)
			par = of_get_parent(par);
		else
			par = of_find_node_by_phandle(*p);
		of_node_put(tmp);
	}
	return par;
}
EXPORT_SYMBOL_GPL(scom_find_parent);

scom_map_t scom_map_device(struct device_node *dev, int index)
{
	struct device_node *parent;
	unsigned int cells, size;
	const u32 *prop;
	u64 reg, cnt;
	scom_map_t ret;

	parent = scom_find_parent(dev);

	if (parent == NULL)
		return 0;

	prop = of_get_property(parent, "#scom-cells", NULL);
	cells = prop ? *prop : 1;

	prop = of_get_property(dev, "scom-reg", &size);
	if (!prop)
		return 0;
	size >>= 2;

	if (index >= (size / (2*cells)))
		return 0;

	reg = of_read_number(&prop[index * cells * 2], cells);
	cnt = of_read_number(&prop[index * cells * 2 + cells], cells);

	ret = scom_map(parent, reg, cnt);
	of_node_put(parent);

	return ret;
}
EXPORT_SYMBOL_GPL(scom_map_device);

#ifdef CONFIG_SCOM_DEBUGFS
struct scom_debug_entry {
	struct device_node *dn;
	unsigned long addr;
	scom_map_t map;
	spinlock_t lock;
	char name[8];
	struct debugfs_blob_wrapper blob;
};

static int scom_addr_set(void *data, u64 val)
{
	struct scom_debug_entry *ent = data;

	ent->addr = 0;
	scom_unmap(ent->map);

	ent->map = scom_map(ent->dn, val, 1);
	if (scom_map_ok(ent->map))
		ent->addr = val;
	else
		return -EFAULT;

	return 0;
}

static int scom_addr_get(void *data, u64 *val)
{
	struct scom_debug_entry *ent = data;
	*val = ent->addr;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(scom_addr_fops, scom_addr_get, scom_addr_set,
			"0x%llx\n");

static int scom_val_set(void *data, u64 val)
{
	struct scom_debug_entry *ent = data;

	if (!scom_map_ok(ent->map))
		return -EFAULT;

	scom_write(ent->map, 0, val);

	return 0;
}

static int scom_val_get(void *data, u64 *val)
{
	struct scom_debug_entry *ent = data;

	if (!scom_map_ok(ent->map))
		return -EFAULT;

	*val = scom_read(ent->map, 0);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(scom_val_fops, scom_val_get, scom_val_set,
			"0x%llx\n");

static int scom_debug_init_one(struct dentry *root, struct device_node *dn,
			       int i)
{
	struct scom_debug_entry *ent;
	struct dentry *dir;

	ent = kzalloc(sizeof(*ent), GFP_KERNEL);
	if (!ent)
		return -ENOMEM;

	ent->dn = of_node_get(dn);
	ent->map = SCOM_MAP_INVALID;
	spin_lock_init(&ent->lock);
	snprintf(ent->name, 8, "scom%d", i);
	ent->blob.data = dn->full_name;
	ent->blob.size = strlen(dn->full_name);

	dir = debugfs_create_dir(ent->name, root);
	if (!dir) {
		of_node_put(dn);
		kfree(ent);
		return -1;
	}

	debugfs_create_file("addr", 0600, dir, ent, &scom_addr_fops);
	debugfs_create_file("value", 0600, dir, ent, &scom_val_fops);
	debugfs_create_blob("path", 0400, dir, &ent->blob);

	return 0;
}

static int scom_debug_init(void)
{
	struct device_node *dn;
	struct dentry *root;
	int i, rc;

	root = debugfs_create_dir("scom", powerpc_debugfs_root);
	if (!root)
		return -1;

	i = rc = 0;
	for_each_node_with_property(dn, "scom-controller")
		rc |= scom_debug_init_one(root, dn, i++);

	return rc;
}
device_initcall(scom_debug_init);
#endif /* CONFIG_SCOM_DEBUGFS */
