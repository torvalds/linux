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
#include "vis.h"

#define to_dev(obj)     container_of(obj, struct device, kobj)

struct bat_attribute {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct attribute *attr,
			 char *buf, size_t count);
};

struct hardif_attribute {
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

#define HARDIF_ATTR(_name, _mode, _show, _store)	\
struct hardif_attribute hardif_attr_##_name = {		\
	.attr = {.name = __stringify(_name),		\
		 .mode = _mode },			\
	.show   = _show,				\
	.store  = _store,				\
};

static ssize_t show_aggr_ogm(struct kobject *kobj, struct attribute *attr,
			     char *buff)
{
	struct device *dev = to_dev(kobj->parent);
	struct bat_priv *bat_priv = netdev_priv(to_net_dev(dev));
	int aggr_status = atomic_read(&bat_priv->aggregation_enabled);

	return sprintf(buff, "status: %s\ncommands: enable, disable, 0, 1\n",
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

static ssize_t show_vis_mode(struct kobject *kobj, struct attribute *attr,
			     char *buff)
{
	struct device *dev = to_dev(kobj->parent);
	struct bat_priv *bat_priv = netdev_priv(to_net_dev(dev));
	int vis_mode = atomic_read(&bat_priv->vis_mode);

	return sprintf(buff, "status: %s\ncommands: client, server, %d, %d\n",
		       vis_mode == VIS_TYPE_CLIENT_UPDATE ?
							"client" : "server",
		       VIS_TYPE_SERVER_SYNC, VIS_TYPE_CLIENT_UPDATE);
}

static ssize_t store_vis_mode(struct kobject *kobj, struct attribute *attr,
			      char *buff, size_t count)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	unsigned long val;
	int ret, vis_mode_tmp = -1;

	ret = strict_strtoul(buff, 10, &val);

	if (((count == 2) && (!ret) && (val == VIS_TYPE_CLIENT_UPDATE)) ||
	    (strncmp(buff, "client", 6) == 0))
		vis_mode_tmp = VIS_TYPE_CLIENT_UPDATE;

	if (((count == 2) && (!ret) && (val == VIS_TYPE_SERVER_SYNC)) ||
	    (strncmp(buff, "server", 6) == 0))
		vis_mode_tmp = VIS_TYPE_SERVER_SYNC;

	if (vis_mode_tmp < 0) {
		if (buff[count - 1] == '\n')
			buff[count - 1] = '\0';

		printk(KERN_INFO "batman-adv:Invalid parameter for 'vis mode' setting on mesh %s received: %s\n",
		       net_dev->name, buff);
		return -EINVAL;
	}

	if (atomic_read(&bat_priv->vis_mode) == vis_mode_tmp)
		return count;

	printk(KERN_INFO "batman-adv:Changing vis mode from: %s to: %s on mesh: %s\n",
	       atomic_read(&bat_priv->vis_mode) == VIS_TYPE_CLIENT_UPDATE ?
	       "client" : "server", vis_mode_tmp == VIS_TYPE_CLIENT_UPDATE ?
	       "client" : "server", net_dev->name);

	atomic_set(&bat_priv->vis_mode, (unsigned)vis_mode_tmp);
	return count;
}

static ssize_t show_orig_interval(struct kobject *kobj, struct attribute *attr,
				 char *buff)
{
	struct device *dev = to_dev(kobj->parent);
	struct bat_priv *bat_priv = netdev_priv(to_net_dev(dev));

	return sprintf(buff, "status: %i\n",
		       atomic_read(&bat_priv->orig_interval));
}

static ssize_t store_orig_interval(struct kobject *kobj, struct attribute *attr,
				  char *buff, size_t count)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	unsigned long orig_interval_tmp;
	int ret;

	ret = strict_strtoul(buff, 10, &orig_interval_tmp);
	if (ret) {
		printk(KERN_INFO "batman-adv:Invalid parameter for 'orig_interval' setting on mesh %s received: %s\n",
		       net_dev->name, buff);
		return -EINVAL;
	}

	if (orig_interval_tmp <= JITTER * 2) {
		printk(KERN_INFO "batman-adv:New originator interval too small: %li (min: %i)\n",
		       orig_interval_tmp, JITTER * 2);
		return -EINVAL;
	}

	if (atomic_read(&bat_priv->orig_interval) == orig_interval_tmp)
		return count;

	printk(KERN_INFO "batman-adv:Changing originator interval from: %i to: %li on mesh: %s\n",
	       atomic_read(&bat_priv->orig_interval),
	       orig_interval_tmp, net_dev->name);

	atomic_set(&bat_priv->orig_interval, orig_interval_tmp);
	return count;
}

static BAT_ATTR(aggregate_ogm, S_IRUGO | S_IWUSR,
		show_aggr_ogm, store_aggr_ogm);
static BAT_ATTR(vis_mode, S_IRUGO | S_IWUSR, show_vis_mode, store_vis_mode);
static BAT_ATTR(orig_interval, S_IRUGO | S_IWUSR,
		show_orig_interval, store_orig_interval);

static struct bat_attribute *mesh_attrs[] = {
	&bat_attr_aggregate_ogm,
	&bat_attr_vis_mode,
	&bat_attr_orig_interval,
	NULL,
};

static ssize_t transtable_local_read(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buff, loff_t off, size_t count)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);

	return hna_local_fill_buffer_text(net_dev, buff, count, off);
}

static ssize_t transtable_global_read(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buff, loff_t off, size_t count)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);

	return hna_global_fill_buffer_text(net_dev, buff, count, off);
}

static ssize_t originators_read(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buff, loff_t off, size_t count)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);

	return orig_fill_buffer_text(net_dev, buff, count, off);
}

static ssize_t vis_data_read(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buff, loff_t off, size_t count)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);

	return vis_fill_buffer_text(net_dev, buff, count, off);
}

static BAT_BIN_ATTR(transtable_local, S_IRUGO, transtable_local_read, NULL);
static BAT_BIN_ATTR(transtable_global, S_IRUGO, transtable_global_read, NULL);
static BAT_BIN_ATTR(originators, S_IRUGO, originators_read, NULL);
static BAT_BIN_ATTR(vis_data, S_IRUGO, vis_data_read, NULL);

static struct bin_attribute *mesh_bin_attrs[] = {
	&bat_attr_transtable_local,
	&bat_attr_transtable_global,
	&bat_attr_originators,
	&bat_attr_vis_data,
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
	atomic_set(&bat_priv->vis_mode, VIS_TYPE_CLIENT_UPDATE);
	atomic_set(&bat_priv->orig_interval, 1000);
	bat_priv->primary_if = NULL;
	bat_priv->num_ifaces = 0;

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

static ssize_t show_mesh_iface(struct kobject *kobj, struct attribute *attr,
			       char *buff)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);
	struct batman_if *batman_if = get_batman_if_by_netdev(net_dev);

	if (!batman_if)
		return 0;

	return sprintf(buff, "status: %s\ncommands: none, bat0\n",
		       batman_if->if_status == IF_NOT_IN_USE ?
							"none" : "bat0");
}

static ssize_t store_mesh_iface(struct kobject *kobj, struct attribute *attr,
				char *buff, size_t count)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);
	struct batman_if *batman_if = get_batman_if_by_netdev(net_dev);
	int status_tmp = -1;

	if (!batman_if)
		return count;

	if (strncmp(buff, "none", 4) == 0)
		status_tmp = IF_NOT_IN_USE;

	if (strncmp(buff, "bat0", 4) == 0)
		status_tmp = IF_I_WANT_YOU;

	if (status_tmp < 0) {
		if (buff[count - 1] == '\n')
			buff[count - 1] = '\0';

		printk(KERN_ERR "batman-adv:Invalid parameter for 'mesh_iface' setting received: %s\n",
		       buff);
		return -EINVAL;
	}

	if ((batman_if->if_status == status_tmp) ||
	    ((status_tmp == IF_I_WANT_YOU) &&
	     (batman_if->if_status != IF_NOT_IN_USE)))
		return count;

	if (status_tmp == IF_I_WANT_YOU)
		status_tmp = hardif_enable_interface(batman_if);
	else
		hardif_disable_interface(batman_if);

	return (status_tmp < 0 ? status_tmp : count);
}

static ssize_t show_iface_status(struct kobject *kobj, struct attribute *attr,
				 char *buff)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);
	struct batman_if *batman_if = get_batman_if_by_netdev(net_dev);

	if (!batman_if)
		return 0;

	switch (batman_if->if_status) {
	case IF_TO_BE_REMOVED:
		return sprintf(buff, "disabling\n");
	case IF_INACTIVE:
		return sprintf(buff, "inactive\n");
	case IF_ACTIVE:
		return sprintf(buff, "active\n");
	case IF_TO_BE_ACTIVATED:
		return sprintf(buff, "enabling\n");
	case IF_NOT_IN_USE:
	default:
		return sprintf(buff, "not in use\n");
	}
}

static HARDIF_ATTR(mesh_iface, S_IRUGO | S_IWUSR,
		   show_mesh_iface, store_mesh_iface);
static HARDIF_ATTR(iface_status, S_IRUGO, show_iface_status, NULL);

static struct hardif_attribute *batman_attrs[] = {
	&hardif_attr_mesh_iface,
	&hardif_attr_iface_status,
	NULL,
};

int sysfs_add_hardif(struct kobject **hardif_obj, struct net_device *dev)
{
	struct kobject *hardif_kobject = &dev->dev.kobj;
	struct hardif_attribute **hardif_attr;
	int err;

	*hardif_obj = kobject_create_and_add(SYSFS_IF_BAT_SUBDIR,
						    hardif_kobject);

	if (!*hardif_obj) {
		printk(KERN_ERR "batman-adv:Can't add sysfs directory: %s/%s\n",
		       dev->name, SYSFS_IF_BAT_SUBDIR);
		goto out;
	}

	for (hardif_attr = batman_attrs; *hardif_attr; ++hardif_attr) {
		err = sysfs_create_file(*hardif_obj, &((*hardif_attr)->attr));
		if (err) {
			printk(KERN_ERR "batman-adv:Can't add sysfs file: %s/%s/%s\n",
			       dev->name, SYSFS_IF_BAT_SUBDIR,
			       ((*hardif_attr)->attr).name);
			goto rem_attr;
		}
	}

	return 0;

rem_attr:
	for (hardif_attr = batman_attrs; *hardif_attr; ++hardif_attr)
		sysfs_remove_file(*hardif_obj, &((*hardif_attr)->attr));
out:
	return -ENOMEM;
}

void sysfs_del_hardif(struct kobject **hardif_obj)
{
	kobject_put(*hardif_obj);
	*hardif_obj = NULL;
}
