/*
 * Copyright (c) 2012 Mellanox Technologies.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*#include "core_priv.h"*/
#include "mlx4_ib.h"
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/stat.h>

#include <rdma/ib_mad.h>
/*show_admin_alias_guid returns the administratively assigned value of that GUID.
 * Values returned in buf parameter string:
 *	0			- requests opensm to assign a value.
 *	ffffffffffffffff	- delete this entry.
 *	other			- value assigned by administrator.
 */
static ssize_t show_admin_alias_guid(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	int record_num;/*0-15*/
	int guid_index_in_rec; /*0 - 7*/
	struct mlx4_ib_iov_sysfs_attr *mlx4_ib_iov_dentry =
		container_of(attr, struct mlx4_ib_iov_sysfs_attr, dentry);
	struct mlx4_ib_iov_port *port = mlx4_ib_iov_dentry->ctx;
	struct mlx4_ib_dev *mdev = port->dev;

	record_num = mlx4_ib_iov_dentry->entry_num / 8 ;
	guid_index_in_rec = mlx4_ib_iov_dentry->entry_num % 8 ;

	return sprintf(buf, "%llx\n",
		       be64_to_cpu(*(__be64 *)&mdev->sriov.alias_guid.
				   ports_guid[port->num - 1].
				   all_rec_per_port[record_num].
				   all_recs[8 * guid_index_in_rec]));
}

/* store_admin_alias_guid stores the (new) administratively assigned value of that GUID.
 * Values in buf parameter string:
 *	0			- requests opensm to assign a value.
 *	0xffffffffffffffff	- delete this entry.
 *	other			- guid value assigned by the administrator.
 */
static ssize_t store_admin_alias_guid(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int record_num;/*0-15*/
	int guid_index_in_rec; /*0 - 7*/
	struct mlx4_ib_iov_sysfs_attr *mlx4_ib_iov_dentry =
		container_of(attr, struct mlx4_ib_iov_sysfs_attr, dentry);
	struct mlx4_ib_iov_port *port = mlx4_ib_iov_dentry->ctx;
	struct mlx4_ib_dev *mdev = port->dev;
	u64 sysadmin_ag_val;

	record_num = mlx4_ib_iov_dentry->entry_num / 8;
	guid_index_in_rec = mlx4_ib_iov_dentry->entry_num % 8;
	if (0 == record_num && 0 == guid_index_in_rec) {
		pr_err("GUID 0 block 0 is RO\n");
		return count;
	}
	sscanf(buf, "%llx", &sysadmin_ag_val);
	*(__be64 *)&mdev->sriov.alias_guid.ports_guid[port->num - 1].
		all_rec_per_port[record_num].
		all_recs[GUID_REC_SIZE * guid_index_in_rec] =
			cpu_to_be64(sysadmin_ag_val);

	/* Change the state to be pending for update */
	mdev->sriov.alias_guid.ports_guid[port->num - 1].all_rec_per_port[record_num].status
		= MLX4_GUID_INFO_STATUS_IDLE ;

	mdev->sriov.alias_guid.ports_guid[port->num - 1].all_rec_per_port[record_num].method
		= MLX4_GUID_INFO_RECORD_SET;

	switch (sysadmin_ag_val) {
	case MLX4_GUID_FOR_DELETE_VAL:
		mdev->sriov.alias_guid.ports_guid[port->num - 1].all_rec_per_port[record_num].method
			= MLX4_GUID_INFO_RECORD_DELETE;
		mdev->sriov.alias_guid.ports_guid[port->num - 1].all_rec_per_port[record_num].ownership
			= MLX4_GUID_SYSADMIN_ASSIGN;
		break;
	/* The sysadmin requests the SM to re-assign */
	case MLX4_NOT_SET_GUID:
		mdev->sriov.alias_guid.ports_guid[port->num - 1].all_rec_per_port[record_num].ownership
			= MLX4_GUID_DRIVER_ASSIGN;
		break;
	/* The sysadmin requests a specific value.*/
	default:
		mdev->sriov.alias_guid.ports_guid[port->num - 1].all_rec_per_port[record_num].ownership
			= MLX4_GUID_SYSADMIN_ASSIGN;
		break;
	}

	/* set the record index */
	mdev->sriov.alias_guid.ports_guid[port->num - 1].all_rec_per_port[record_num].guid_indexes
		= mlx4_ib_get_aguid_comp_mask_from_ix(guid_index_in_rec);

	mlx4_ib_init_alias_guid_work(mdev, port->num - 1);

	return count;
}

static ssize_t show_port_gid(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct mlx4_ib_iov_sysfs_attr *mlx4_ib_iov_dentry =
		container_of(attr, struct mlx4_ib_iov_sysfs_attr, dentry);
	struct mlx4_ib_iov_port *port = mlx4_ib_iov_dentry->ctx;
	struct mlx4_ib_dev *mdev = port->dev;
	union ib_gid gid;
	ssize_t ret;

	ret = __mlx4_ib_query_gid(&mdev->ib_dev, port->num,
				  mlx4_ib_iov_dentry->entry_num, &gid, 1);
	if (ret)
		return ret;
	ret = sprintf(buf, "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
		      be16_to_cpu(((__be16 *) gid.raw)[0]),
		      be16_to_cpu(((__be16 *) gid.raw)[1]),
		      be16_to_cpu(((__be16 *) gid.raw)[2]),
		      be16_to_cpu(((__be16 *) gid.raw)[3]),
		      be16_to_cpu(((__be16 *) gid.raw)[4]),
		      be16_to_cpu(((__be16 *) gid.raw)[5]),
		      be16_to_cpu(((__be16 *) gid.raw)[6]),
		      be16_to_cpu(((__be16 *) gid.raw)[7]));
	return ret;
}

static ssize_t show_phys_port_pkey(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct mlx4_ib_iov_sysfs_attr *mlx4_ib_iov_dentry =
		container_of(attr, struct mlx4_ib_iov_sysfs_attr, dentry);
	struct mlx4_ib_iov_port *port = mlx4_ib_iov_dentry->ctx;
	struct mlx4_ib_dev *mdev = port->dev;
	u16 pkey;
	ssize_t ret;

	ret = __mlx4_ib_query_pkey(&mdev->ib_dev, port->num,
				   mlx4_ib_iov_dentry->entry_num, &pkey, 1);
	if (ret)
		return ret;

	return sprintf(buf, "0x%04x\n", pkey);
}

#define DENTRY_REMOVE(_dentry)						\
do {									\
	sysfs_remove_file((_dentry)->kobj, &(_dentry)->dentry.attr);	\
} while (0);

static int create_sysfs_entry(void *_ctx, struct mlx4_ib_iov_sysfs_attr *_dentry,
			      char *_name, struct kobject *_kobj,
			      ssize_t (*show)(struct device *dev,
					      struct device_attribute *attr,
					      char *buf),
			      ssize_t (*store)(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t count)
			      )
{
	int ret = 0;
	struct mlx4_ib_iov_sysfs_attr *vdentry = _dentry;

	vdentry->ctx = _ctx;
	vdentry->dentry.show = show;
	vdentry->dentry.store = store;
	sysfs_attr_init(&vdentry->dentry.attr);
	vdentry->dentry.attr.name = vdentry->name;
	vdentry->dentry.attr.mode = 0;
	vdentry->kobj = _kobj;
	snprintf(vdentry->name, 15, "%s", _name);

	if (vdentry->dentry.store)
		vdentry->dentry.attr.mode |= S_IWUSR;

	if (vdentry->dentry.show)
		vdentry->dentry.attr.mode |= S_IRUGO;

	ret = sysfs_create_file(vdentry->kobj, &vdentry->dentry.attr);
	if (ret) {
		pr_err("failed to create %s\n", vdentry->dentry.attr.name);
		vdentry->ctx = NULL;
		return ret;
	}

	return ret;
}

int add_sysfs_port_mcg_attr(struct mlx4_ib_dev *device, int port_num,
		struct attribute *attr)
{
	struct mlx4_ib_iov_port *port = &device->iov_ports[port_num - 1];
	int ret;

	ret = sysfs_create_file(port->mcgs_parent, attr);
	if (ret)
		pr_err("failed to create %s\n", attr->name);

	return ret;
}

void del_sysfs_port_mcg_attr(struct mlx4_ib_dev *device, int port_num,
		struct attribute *attr)
{
	struct mlx4_ib_iov_port *port = &device->iov_ports[port_num - 1];

	sysfs_remove_file(port->mcgs_parent, attr);
}

static int add_port_entries(struct mlx4_ib_dev *device, int port_num)
{
	int i;
	char buff[10];
	struct mlx4_ib_iov_port *port = NULL;
	int ret = 0 ;
	struct ib_port_attr attr;

	/* get the physical gid and pkey table sizes.*/
	ret = __mlx4_ib_query_port(&device->ib_dev, port_num, &attr, 1);
	if (ret)
		goto err;

	port = &device->iov_ports[port_num - 1];
	port->dev = device;
	port->num = port_num;
	/* Directory structure:
	 * iov -
	 *   port num -
	 *	admin_guids
	 *	gids (operational)
	 *	mcg_table
	 */
	port->dentr_ar = kzalloc(sizeof (struct mlx4_ib_iov_sysfs_attr_ar),
				 GFP_KERNEL);
	if (!port->dentr_ar) {
		ret = -ENOMEM;
		goto err;
	}
	sprintf(buff, "%d", port_num);
	port->cur_port = kobject_create_and_add(buff,
				 kobject_get(device->ports_parent));
	if (!port->cur_port) {
		ret = -ENOMEM;
		goto kobj_create_err;
	}
	/* admin GUIDs */
	port->admin_alias_parent = kobject_create_and_add("admin_guids",
						  kobject_get(port->cur_port));
	if (!port->admin_alias_parent) {
		ret = -ENOMEM;
		goto err_admin_guids;
	}
	for (i = 0 ; i < attr.gid_tbl_len; i++) {
		sprintf(buff, "%d", i);
		port->dentr_ar->dentries[i].entry_num = i;
		ret = create_sysfs_entry(port, &port->dentr_ar->dentries[i],
					  buff, port->admin_alias_parent,
					  show_admin_alias_guid, store_admin_alias_guid);
		if (ret)
			goto err_admin_alias_parent;
	}

	/* gids subdirectory (operational gids) */
	port->gids_parent = kobject_create_and_add("gids",
						  kobject_get(port->cur_port));
	if (!port->gids_parent) {
		ret = -ENOMEM;
		goto err_gids;
	}

	for (i = 0 ; i < attr.gid_tbl_len; i++) {
		sprintf(buff, "%d", i);
		port->dentr_ar->dentries[attr.gid_tbl_len + i].entry_num = i;
		ret = create_sysfs_entry(port,
					 &port->dentr_ar->dentries[attr.gid_tbl_len + i],
					 buff,
					 port->gids_parent, show_port_gid, NULL);
		if (ret)
			goto err_gids_parent;
	}

	/* physical port pkey table */
	port->pkeys_parent =
		kobject_create_and_add("pkeys", kobject_get(port->cur_port));
	if (!port->pkeys_parent) {
		ret = -ENOMEM;
		goto err_pkeys;
	}

	for (i = 0 ; i < attr.pkey_tbl_len; i++) {
		sprintf(buff, "%d", i);
		port->dentr_ar->dentries[2 * attr.gid_tbl_len + i].entry_num = i;
		ret = create_sysfs_entry(port,
					 &port->dentr_ar->dentries[2 * attr.gid_tbl_len + i],
					 buff, port->pkeys_parent,
					 show_phys_port_pkey, NULL);
		if (ret)
			goto err_pkeys_parent;
	}

	/* MCGs table */
	port->mcgs_parent =
		kobject_create_and_add("mcgs", kobject_get(port->cur_port));
	if (!port->mcgs_parent) {
		ret = -ENOMEM;
		goto err_mcgs;
	}
	return 0;

err_mcgs:
	kobject_put(port->cur_port);

err_pkeys_parent:
	kobject_put(port->pkeys_parent);

err_pkeys:
	kobject_put(port->cur_port);

err_gids_parent:
	kobject_put(port->gids_parent);

err_gids:
	kobject_put(port->cur_port);

err_admin_alias_parent:
	kobject_put(port->admin_alias_parent);

err_admin_guids:
	kobject_put(port->cur_port);
	kobject_put(port->cur_port); /* once more for create_and_add buff */

kobj_create_err:
	kobject_put(device->ports_parent);
	kfree(port->dentr_ar);

err:
	pr_err("add_port_entries FAILED: for port:%d, error: %d\n",
	       port_num, ret);
	return ret;
}

static void get_name(struct mlx4_ib_dev *dev, char *name, int i, int max)
{
	char base_name[9];

	/* pci_name format is: bus:dev:func -> xxxx:yy:zz.n */
	strlcpy(name, pci_name(dev->dev->pdev), max);
	strncpy(base_name, name, 8); /*till xxxx:yy:*/
	base_name[8] = '\0';
	/* with no ARI only 3 last bits are used so when the fn is higher than 8
	 * need to add it to the dev num, so count in the last number will be
	 * modulo 8 */
	sprintf(name, "%s%.2d.%d", base_name, (i/8), (i%8));
}

struct mlx4_port {
	struct kobject         kobj;
	struct mlx4_ib_dev    *dev;
	struct attribute_group pkey_group;
	struct attribute_group gid_group;
	u8                     port_num;
	int		       slave;
};


static void mlx4_port_release(struct kobject *kobj)
{
	struct mlx4_port *p = container_of(kobj, struct mlx4_port, kobj);
	struct attribute *a;
	int i;

	for (i = 0; (a = p->pkey_group.attrs[i]); ++i)
		kfree(a);
	kfree(p->pkey_group.attrs);
	for (i = 0; (a = p->gid_group.attrs[i]); ++i)
		kfree(a);
	kfree(p->gid_group.attrs);
	kfree(p);
}

struct port_attribute {
	struct attribute attr;
	ssize_t (*show)(struct mlx4_port *, struct port_attribute *, char *buf);
	ssize_t (*store)(struct mlx4_port *, struct port_attribute *,
			 const char *buf, size_t count);
};

static ssize_t port_attr_show(struct kobject *kobj,
			      struct attribute *attr, char *buf)
{
	struct port_attribute *port_attr =
		container_of(attr, struct port_attribute, attr);
	struct mlx4_port *p = container_of(kobj, struct mlx4_port, kobj);

	if (!port_attr->show)
		return -EIO;
	return port_attr->show(p, port_attr, buf);
}

static ssize_t port_attr_store(struct kobject *kobj,
			       struct attribute *attr,
			       const char *buf, size_t size)
{
	struct port_attribute *port_attr =
		container_of(attr, struct port_attribute, attr);
	struct mlx4_port *p = container_of(kobj, struct mlx4_port, kobj);

	if (!port_attr->store)
		return -EIO;
	return port_attr->store(p, port_attr, buf, size);
}

static const struct sysfs_ops port_sysfs_ops = {
	.show = port_attr_show,
	.store = port_attr_store,
};

static struct kobj_type port_type = {
	.release    = mlx4_port_release,
	.sysfs_ops  = &port_sysfs_ops,
};

struct port_table_attribute {
	struct port_attribute	attr;
	char			name[8];
	int			index;
};

static ssize_t show_port_pkey(struct mlx4_port *p, struct port_attribute *attr,
			      char *buf)
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	ssize_t ret = -ENODEV;

	if (p->dev->pkeys.virt2phys_pkey[p->slave][p->port_num - 1][tab_attr->index] >=
	    (p->dev->dev->caps.pkey_table_len[p->port_num]))
		ret = sprintf(buf, "none\n");
	else
		ret = sprintf(buf, "%d\n",
			      p->dev->pkeys.virt2phys_pkey[p->slave]
			      [p->port_num - 1][tab_attr->index]);
	return ret;
}

static ssize_t store_port_pkey(struct mlx4_port *p, struct port_attribute *attr,
			       const char *buf, size_t count)
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	int idx;
	int err;

	/* do not allow remapping Dom0 virtual pkey table */
	if (p->slave == mlx4_master_func_num(p->dev->dev))
		return -EINVAL;

	if (!strncasecmp(buf, "no", 2))
		idx = p->dev->dev->phys_caps.pkey_phys_table_len[p->port_num] - 1;
	else if (sscanf(buf, "%i", &idx) != 1 ||
		 idx >= p->dev->dev->caps.pkey_table_len[p->port_num] ||
		 idx < 0)
		return -EINVAL;

	p->dev->pkeys.virt2phys_pkey[p->slave][p->port_num - 1]
				    [tab_attr->index] = idx;
	mlx4_sync_pkey_table(p->dev->dev, p->slave, p->port_num,
			     tab_attr->index, idx);
	err = mlx4_gen_pkey_eqe(p->dev->dev, p->slave, p->port_num);
	if (err) {
		pr_err("mlx4_gen_pkey_eqe failed for slave %d,"
		       " port %d, index %d\n", p->slave, p->port_num, idx);
		return err;
	}
	return count;
}

static ssize_t show_port_gid_idx(struct mlx4_port *p,
				 struct port_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", p->slave);
}

static struct attribute **
alloc_group_attrs(ssize_t (*show)(struct mlx4_port *,
				  struct port_attribute *, char *buf),
		  ssize_t (*store)(struct mlx4_port *, struct port_attribute *,
				   const char *buf, size_t count),
		  int len)
{
	struct attribute **tab_attr;
	struct port_table_attribute *element;
	int i;

	tab_attr = kcalloc(1 + len, sizeof (struct attribute *), GFP_KERNEL);
	if (!tab_attr)
		return NULL;

	for (i = 0; i < len; i++) {
		element = kzalloc(sizeof (struct port_table_attribute),
				  GFP_KERNEL);
		if (!element)
			goto err;
		if (snprintf(element->name, sizeof (element->name),
			     "%d", i) >= sizeof (element->name)) {
			kfree(element);
			goto err;
		}
		sysfs_attr_init(&element->attr.attr);
		element->attr.attr.name  = element->name;
		if (store) {
			element->attr.attr.mode  = S_IWUSR | S_IRUGO;
			element->attr.store	 = store;
		} else
			element->attr.attr.mode  = S_IRUGO;

		element->attr.show       = show;
		element->index		 = i;
		tab_attr[i] = &element->attr.attr;
	}
	return tab_attr;

err:
	while (--i >= 0)
		kfree(tab_attr[i]);
	kfree(tab_attr);
	return NULL;
}

static int add_port(struct mlx4_ib_dev *dev, int port_num, int slave)
{
	struct mlx4_port *p;
	int i;
	int ret;

	p = kzalloc(sizeof *p, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->dev = dev;
	p->port_num = port_num;
	p->slave = slave;

	ret = kobject_init_and_add(&p->kobj, &port_type,
				   kobject_get(dev->dev_ports_parent[slave]),
				   "%d", port_num);
	if (ret)
		goto err_alloc;

	p->pkey_group.name  = "pkey_idx";
	p->pkey_group.attrs =
		alloc_group_attrs(show_port_pkey, store_port_pkey,
				  dev->dev->caps.pkey_table_len[port_num]);
	if (!p->pkey_group.attrs)
		goto err_alloc;

	ret = sysfs_create_group(&p->kobj, &p->pkey_group);
	if (ret)
		goto err_free_pkey;

	p->gid_group.name  = "gid_idx";
	p->gid_group.attrs = alloc_group_attrs(show_port_gid_idx, NULL, 1);
	if (!p->gid_group.attrs)
		goto err_free_pkey;

	ret = sysfs_create_group(&p->kobj, &p->gid_group);
	if (ret)
		goto err_free_gid;

	list_add_tail(&p->kobj.entry, &dev->pkeys.pkey_port_list[slave]);
	return 0;

err_free_gid:
	kfree(p->gid_group.attrs[0]);
	kfree(p->gid_group.attrs);

err_free_pkey:
	for (i = 0; i < dev->dev->caps.pkey_table_len[port_num]; ++i)
		kfree(p->pkey_group.attrs[i]);
	kfree(p->pkey_group.attrs);

err_alloc:
	kobject_put(dev->dev_ports_parent[slave]);
	kfree(p);
	return ret;
}

static int register_one_pkey_tree(struct mlx4_ib_dev *dev, int slave)
{
	char name[32];
	int err;
	int port;
	struct kobject *p, *t;
	struct mlx4_port *mport;

	get_name(dev, name, slave, sizeof name);

	dev->pkeys.device_parent[slave] =
		kobject_create_and_add(name, kobject_get(dev->iov_parent));

	if (!dev->pkeys.device_parent[slave]) {
		err = -ENOMEM;
		goto fail_dev;
	}

	INIT_LIST_HEAD(&dev->pkeys.pkey_port_list[slave]);

	dev->dev_ports_parent[slave] =
		kobject_create_and_add("ports",
				       kobject_get(dev->pkeys.device_parent[slave]));

	if (!dev->dev_ports_parent[slave]) {
		err = -ENOMEM;
		goto err_ports;
	}

	for (port = 1; port <= dev->dev->caps.num_ports; ++port) {
		err = add_port(dev, port, slave);
		if (err)
			goto err_add;
	}
	return 0;

err_add:
	list_for_each_entry_safe(p, t,
				 &dev->pkeys.pkey_port_list[slave],
				 entry) {
		list_del(&p->entry);
		mport = container_of(p, struct mlx4_port, kobj);
		sysfs_remove_group(p, &mport->pkey_group);
		sysfs_remove_group(p, &mport->gid_group);
		kobject_put(p);
	}
	kobject_put(dev->dev_ports_parent[slave]);

err_ports:
	kobject_put(dev->pkeys.device_parent[slave]);
	/* extra put for the device_parent create_and_add */
	kobject_put(dev->pkeys.device_parent[slave]);

fail_dev:
	kobject_put(dev->iov_parent);
	return err;
}

static int register_pkey_tree(struct mlx4_ib_dev *device)
{
	int i;

	if (!mlx4_is_master(device->dev))
		return 0;

	for (i = 0; i <= device->dev->num_vfs; ++i)
		register_one_pkey_tree(device, i);

	return 0;
}

static void unregister_pkey_tree(struct mlx4_ib_dev *device)
{
	int slave;
	struct kobject *p, *t;
	struct mlx4_port *port;

	if (!mlx4_is_master(device->dev))
		return;

	for (slave = device->dev->num_vfs; slave >= 0; --slave) {
		list_for_each_entry_safe(p, t,
					 &device->pkeys.pkey_port_list[slave],
					 entry) {
			list_del(&p->entry);
			port = container_of(p, struct mlx4_port, kobj);
			sysfs_remove_group(p, &port->pkey_group);
			sysfs_remove_group(p, &port->gid_group);
			kobject_put(p);
			kobject_put(device->dev_ports_parent[slave]);
		}
		kobject_put(device->dev_ports_parent[slave]);
		kobject_put(device->pkeys.device_parent[slave]);
		kobject_put(device->pkeys.device_parent[slave]);
		kobject_put(device->iov_parent);
	}
}

int mlx4_ib_device_register_sysfs(struct mlx4_ib_dev *dev)
{
	int i;
	int ret = 0;

	if (!mlx4_is_master(dev->dev))
		return 0;

	dev->iov_parent =
		kobject_create_and_add("iov",
				       kobject_get(dev->ib_dev.ports_parent->parent));
	if (!dev->iov_parent) {
		ret = -ENOMEM;
		goto err;
	}
	dev->ports_parent =
		kobject_create_and_add("ports",
				       kobject_get(dev->iov_parent));
	if (!dev->ports_parent) {
		ret = -ENOMEM;
		goto err_ports;
	}

	for (i = 1; i <= dev->ib_dev.phys_port_cnt; ++i) {
		ret = add_port_entries(dev, i);
		if (ret)
			goto err_add_entries;
	}

	ret = register_pkey_tree(dev);
	if (ret)
		goto err_add_entries;
	return 0;

err_add_entries:
	kobject_put(dev->ports_parent);

err_ports:
	kobject_put(dev->iov_parent);
err:
	kobject_put(dev->ib_dev.ports_parent->parent);
	pr_err("mlx4_ib_device_register_sysfs error (%d)\n", ret);
	return ret;
}

static void unregister_alias_guid_tree(struct mlx4_ib_dev *device)
{
	struct mlx4_ib_iov_port *p;
	int i;

	if (!mlx4_is_master(device->dev))
		return;

	for (i = 0; i < device->dev->caps.num_ports; i++) {
		p = &device->iov_ports[i];
		kobject_put(p->admin_alias_parent);
		kobject_put(p->gids_parent);
		kobject_put(p->pkeys_parent);
		kobject_put(p->mcgs_parent);
		kobject_put(p->cur_port);
		kobject_put(p->cur_port);
		kobject_put(p->cur_port);
		kobject_put(p->cur_port);
		kobject_put(p->cur_port);
		kobject_put(p->dev->ports_parent);
		kfree(p->dentr_ar);
	}
}

void mlx4_ib_device_unregister_sysfs(struct mlx4_ib_dev *device)
{
	unregister_alias_guid_tree(device);
	unregister_pkey_tree(device);
	kobject_put(device->ports_parent);
	kobject_put(device->iov_parent);
	kobject_put(device->iov_parent);
	kobject_put(device->ib_dev.ports_parent->parent);
}
