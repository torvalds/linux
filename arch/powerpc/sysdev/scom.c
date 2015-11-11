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
#include <asm/uaccess.h>

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
	const __be32 *prop, *sprop;
	u64 reg, cnt;
	scom_map_t ret;

	parent = scom_find_parent(dev);

	if (parent == NULL)
		return 0;

	/*
	 * We support "scom-reg" properties for adding scom registers
	 * to a random device-tree node with an explicit scom-parent
	 *
	 * We also support the simple "reg" property if the device is
	 * a direct child of a scom controller.
	 *
	 * In case both exist, "scom-reg" takes precedence.
	 */
	prop = of_get_property(dev, "scom-reg", &size);
	sprop = of_get_property(parent, "#scom-cells", NULL);
	if (!prop && parent == dev->parent) {
		prop = of_get_property(dev, "reg", &size);
		sprop = of_get_property(parent, "#address-cells", NULL);
	}
	if (!prop)
		return NULL;
	cells = sprop ? be32_to_cpup(sprop) : 1;
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
	struct debugfs_blob_wrapper path;
	char name[16];
};

static ssize_t scom_debug_read(struct file *filp, char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	struct scom_debug_entry *ent = filp->private_data;
	u64 __user *ubuf64 = (u64 __user *)ubuf;
	loff_t off = *ppos;
	ssize_t done = 0; 
	u64 reg, reg_cnt, val;
	scom_map_t map;
	int rc;

	if (off < 0 || (off & 7) || (count & 7))
		return -EINVAL;
	reg = off >> 3;
	reg_cnt = count >> 3;

	map = scom_map(ent->dn, reg, reg_cnt);
	if (!scom_map_ok(map))
		return -ENXIO;

	for (reg = 0; reg < reg_cnt; reg++) {
		rc = scom_read(map, reg, &val);
		if (!rc)
			rc = put_user(val, ubuf64);
		if (rc) {
			if (!done)
				done = rc;
			break;
		}
		ubuf64++;
		*ppos += 8;
		done += 8;
	}
	scom_unmap(map);
	return done;
}

static ssize_t scom_debug_write(struct file* filp, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct scom_debug_entry *ent = filp->private_data;
	u64 __user *ubuf64 = (u64 __user *)ubuf;
	loff_t off = *ppos;
	ssize_t done = 0; 
	u64 reg, reg_cnt, val;
	scom_map_t map;
	int rc;

	if (off < 0 || (off & 7) || (count & 7))
		return -EINVAL;
	reg = off >> 3;
	reg_cnt = count >> 3;

	map = scom_map(ent->dn, reg, reg_cnt);
	if (!scom_map_ok(map))
		return -ENXIO;

	for (reg = 0; reg < reg_cnt; reg++) {
		rc = get_user(val, ubuf64);
		if (!rc)
			rc = scom_write(map, reg,  val);
		if (rc) {
			if (!done)
				done = rc;
			break;
		}
		ubuf64++;
		done += 8;
	}
	scom_unmap(map);
	return done;
}

static const struct file_operations scom_debug_fops = {
	.read =		scom_debug_read,
	.write =	scom_debug_write,
	.open =		simple_open,
	.llseek =	default_llseek,
};

static int scom_debug_init_one(struct dentry *root, struct device_node *dn,
			       int i)
{
	struct scom_debug_entry *ent;
	struct dentry *dir;

	ent = kzalloc(sizeof(*ent), GFP_KERNEL);
	if (!ent)
		return -ENOMEM;

	ent->dn = of_node_get(dn);
	snprintf(ent->name, 16, "%08x", i);
	ent->path.data = (void*) dn->full_name;
	ent->path.size = strlen(dn->full_name);

	dir = debugfs_create_dir(ent->name, root);
	if (!dir) {
		of_node_put(dn);
		kfree(ent);
		return -1;
	}

	debugfs_create_blob("devspec", 0400, dir, &ent->path);
	debugfs_create_file("access", 0600, dir, ent, &scom_debug_fops);

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
	for_each_node_with_property(dn, "scom-controller") {
		int id = of_get_ibm_chip_id(dn);
		if (id == -1)
			id = i;
		rc |= scom_debug_init_one(root, dn, id);
		i++;
	}

	return rc;
}
device_initcall(scom_debug_init);
#endif /* CONFIG_SCOM_DEBUGFS */
