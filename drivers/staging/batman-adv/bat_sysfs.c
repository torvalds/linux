/*
 * Copyright (C) 2010 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include "main.h"
#include "bat_sysfs.h"
#include "translation-table.h"
#include "originator.h"
#include "hard-interface.h"

#define to_dev(obj)     container_of(obj, struct device, kobj)

struct bat_attribute {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct attribute *attr,
			 char *buf, size_t count);
};

#define BAT_ATTR(_name, _mode, _show, _store)	\
struct bat_attribute bat_attr_##_name = {	\
	.attr = {.name = __stringify(_name),	\
		 .mode = _mode },		\
	.show   = _show,			\
	.store  = _store,			\
};

#define BAT_BIN_ATTR(_name, _mode, _read, _write)	\
struct bin_attribute bat_attr_##_name = {		\
	.attr = { .name = __stringify(_name),		\
		  .mode = _mode, },			\
	.read = _read,					\
	.write = _write,				\
};

static ssize_t show_aggr_ogm(struct kobject *kobj, struct attribute *attr,
			     char *buff)
{
	struct device *dev = to_dev(kobj->parent);
	struct bat_priv *bat_priv = netdev_priv(to_net_dev(dev));
	int aggr_status = atomic_read(&bat_priv->aggregation_enabled);

	return sprintf(buff, "status: %s\ncommands: enable, disable, 0, 1 \n",
		       aggr_status == 0 ? "disabled" : "enabled");
}

static ssize_t store_aggr_ogm(struct kobject *kobj, struct attribute *attr,
			      char *buff, size_t count)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	int aggr_tmp = -1;

	if (((count == 2) && (buff[0] == '1')) ||
	    (strncmp(buff, "enable", 6) == 0))
		aggr_tmp = 1;

	if (((count == 2) && (buff[0] == '0')) ||
	    (strncmp(buff, "disable", 7) == 0))
		aggr_tmp = 0;

	if (aggr_tmp < 0) {
		if (buff[count - 1] == '\n')
			buff[count - 1] = '\0';

		printk(KERN_INFO "batman-adv:Invalid parameter for 'aggregate OGM' setting on mesh %s received: %s\n",
		       net_dev->name, buff);
		return -EINVAL;
	}

	if (atomic_read(&bat_priv->aggregation_enabled) == aggr_tmp)
		return count;

	printk(KERN_INFO "batman-adv:Changing aggregation from: %s to: %s on mesh: %s\n",
	       atomic_read(&bat_priv->aggregation_enabled) == 1 ?
	       "enabled" : "disabled", aggr_tmp == 1 ? "enabled" : "disabled",
	       net_dev->name);

	atomic_set(&bat_priv->aggregation_enabled, (unsigned)aggr_tmp);
	return count;
}

static BAT_ATTR(aggregate_ogm, S_IRUGO | S_IWUSR,
		show_aggr_ogm, store_aggr_ogm);

static struct bat_attribute *mesh_attrs[] = {
	&bat_attr_aggregate_ogm,
	NULL,
};

static ssize_t transtable_local_read(struct kobject *kobj,
			       struct bin_attribute *bin_attr,
			       char *buff, loff_t off, size_t count)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);

	rcu_read_lock();
	if (list_empty(&if_list)) {
		rcu_read_unlock();

		if (off == 0)
			return sprintf(buff,
				       "BATMAN mesh %s disabled - please specify interfaces to enable it\n",
				       net_dev->name);

		return 0;
	}
	rcu_read_unlock();

	return hna_local_fill_buffer_text(net_dev, buff, count, off);
}

static ssize_t transtable_global_read(struct kobject *kobj,
			       struct bin_attribute *bin_attr,
			       char *buff, loff_t off, size_t count)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);

	rcu_read_lock();
	if (list_empty(&if_list)) {
		rcu_read_unlock();

		if (off == 0)
			return sprintf(buff,
				       "BATMAN mesh %s disabled - please specify interfaces to enable it\n",
				       net_dev->name);

		return 0;
	}
	rcu_read_unlock();

	return hna_global_fill_buffer_text(net_dev, buff, count, off);
}

static ssize_t originators_read(struct kobject *kobj,
			       struct bin_attribute *bin_attr,
			       char *buff, loff_t off, size_t count)
{
	/* FIXME: orig table should exist per batif */
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);

	rcu_read_lock();
	if (list_empty(&if_list)) {
		rcu_read_unlock();

		if (off == 0)
			return sprintf(buff,
				       "BATMAN mesh %s disabled - please specify interfaces to enable it\n",
				       net_dev->name);

		return 0;
	}

	if (((struct batman_if *)if_list.next)->if_active != IF_ACTIVE) {
		rcu_read_unlock();

		if (off == 0)
			return sprintf(buff,
				       "BATMAN mesh %s disabled - primary interface not active\n",
				       net_dev->name);

		return 0;
	}
	rcu_read_unlock();

	return orig_fill_buffer_text(buff, count, off);
}

static BAT_BIN_ATTR(transtable_local, S_IRUGO, transtable_local_read, NULL);
static BAT_BIN_ATTR(transtable_global, S_IRUGO, transtable_global_read, NULL);
static BAT_BIN_ATTR(originators, S_IRUGO, originators_read, NULL);

static struct bin_attribute *mesh_bin_attrs[] = {
	&bat_attr_transtable_local,
	&bat_attr_transtable_global,
	&bat_attr_originators,
	NULL,
};

int sysfs_add_meshif(struct net_device *dev)
{
	struct kobject *batif_kobject = &dev->dev.kobj;
	struct bat_priv *bat_priv = netdev_priv(dev);
	struct bat_attribute **bat_attr;
	struct bin_attribute **bin_attr;
	int err;

	/* FIXME: should be done in the general mesh setup
		  routine as soon as we have it */
	atomic_set(&bat_priv->aggregation_enabled, 1);

	bat_priv->mesh_obj = kobject_create_and_add(SYSFS_IF_MESH_SUBDIR,
						    batif_kobject);
	if (!bat_priv->mesh_obj) {
		printk(KERN_ERR "batman-adv:Can't add sysfs directory: %s/%s\n",
		       dev->name, SYSFS_IF_MESH_SUBDIR);
		goto out;
	}

	for (bat_attr = mesh_attrs; *bat_attr; ++bat_attr) {
		err = sysfs_create_file(bat_priv->mesh_obj,
					&((*bat_attr)->attr));
		if (err) {
			printk(KERN_ERR "batman-adv:Can't add sysfs file: %s/%s/%s\n",
			       dev->name, SYSFS_IF_MESH_SUBDIR,
			       ((*bat_attr)->attr).name);
			goto rem_attr;
		}
	}

	for (bin_attr = mesh_bin_attrs; *bin_attr; ++bin_attr) {
		err = sysfs_create_bin_file(bat_priv->mesh_obj, (*bin_attr));
		if (err) {
			printk(KERN_ERR "batman-adv:Can't add sysfs file: %s/%s/%s\n",
			       dev->name, SYSFS_IF_MESH_SUBDIR,
			       ((*bin_attr)->attr).name);
			goto rem_bin_attr;
		}
	}

	return 0;

rem_bin_attr:
	for (bin_attr = mesh_bin_attrs; *bin_attr; ++bin_attr)
		sysfs_remove_bin_file(bat_priv->mesh_obj, (*bin_attr));
rem_attr:
	for (bat_attr = mesh_attrs; *bat_attr; ++bat_attr)
		sysfs_remove_file(bat_priv->mesh_obj, &((*bat_attr)->attr));

	kobject_put(bat_priv->mesh_obj);
	bat_priv->mesh_obj = NULL;
out:
	return -ENOMEM;
}

void sysfs_del_meshif(struct net_device *dev)
{
	struct bat_priv *bat_priv = netdev_priv(dev);
	struct bat_attribute **bat_attr;
	struct bin_attribute **bin_attr;

	for (bin_attr = mesh_bin_attrs; *bin_attr; ++bin_attr)
		sysfs_remove_bin_file(bat_priv->mesh_obj, (*bin_attr));

	for (bat_attr = mesh_attrs; *bat_attr; ++bat_attr)
		sysfs_remove_file(bat_priv->mesh_obj, &((*bat_attr)->attr));

	kobject_put(bat_priv->mesh_obj);
	bat_priv->mesh_obj = NULL;
}
