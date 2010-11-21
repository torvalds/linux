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

#define BAT_ATTR(_name, _mode, _show, _store)	\
struct bat_attribute bat_attr_##_name = {	\
	.attr = {.name = __stringify(_name),	\
		 .mode = _mode },		\
	.show   = _show,			\
	.store  = _store,			\
};

static ssize_t show_aggr_ogms(struct kobject *kobj, struct attribute *attr,
			     char *buff)
{
	struct device *dev = to_dev(kobj->parent);
	struct bat_priv *bat_priv = netdev_priv(to_net_dev(dev));
	int aggr_status = atomic_read(&bat_priv->aggregated_ogms);

	return sprintf(buff, "%s\n",
		       aggr_status == 0 ? "disabled" : "enabled");
}

static ssize_t store_aggr_ogms(struct kobject *kobj, struct attribute *attr,
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

		bat_info(net_dev,
			 "Invalid parameter for 'aggregate OGM' setting"
			 "received: %s\n", buff);
		return -EINVAL;
	}

	if (atomic_read(&bat_priv->aggregated_ogms) == aggr_tmp)
		return count;

	bat_info(net_dev, "Changing aggregation from: %s to: %s\n",
		 atomic_read(&bat_priv->aggregated_ogms) == 1 ?
		 "enabled" : "disabled", aggr_tmp == 1 ? "enabled" :
		 "disabled");

	atomic_set(&bat_priv->aggregated_ogms, (unsigned)aggr_tmp);
	return count;
}

static ssize_t show_bond(struct kobject *kobj, struct attribute *attr,
			     char *buff)
{
	struct device *dev = to_dev(kobj->parent);
	struct bat_priv *bat_priv = netdev_priv(to_net_dev(dev));
	int bond_status = atomic_read(&bat_priv->bonding);

	return sprintf(buff, "%s\n",
		       bond_status == 0 ? "disabled" : "enabled");
}

static ssize_t store_bond(struct kobject *kobj, struct attribute *attr,
			  char *buff, size_t count)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	int bonding_enabled_tmp = -1;

	if (((count == 2) && (buff[0] == '1')) ||
	    (strncmp(buff, "enable", 6) == 0))
		bonding_enabled_tmp = 1;

	if (((count == 2) && (buff[0] == '0')) ||
	    (strncmp(buff, "disable", 7) == 0))
		bonding_enabled_tmp = 0;

	if (bonding_enabled_tmp < 0) {
		if (buff[count - 1] == '\n')
			buff[count - 1] = '\0';

		bat_err(net_dev,
			"Invalid parameter for 'bonding' setting received: "
			"%s\n", buff);
		return -EINVAL;
	}

	if (atomic_read(&bat_priv->bonding) == bonding_enabled_tmp)
		return count;

	bat_info(net_dev, "Changing bonding from: %s to: %s\n",
		 atomic_read(&bat_priv->bonding) == 1 ?
		 "enabled" : "disabled",
		 bonding_enabled_tmp == 1 ? "enabled" : "disabled");

	atomic_set(&bat_priv->bonding, (unsigned)bonding_enabled_tmp);
	return count;
}

static ssize_t show_frag(struct kobject *kobj, struct attribute *attr,
			     char *buff)
{
	struct device *dev = to_dev(kobj->parent);
	struct bat_priv *bat_priv = netdev_priv(to_net_dev(dev));
	int frag_status = atomic_read(&bat_priv->fragmentation);

	return sprintf(buff, "%s\n",
		       frag_status == 0 ? "disabled" : "enabled");
}

static ssize_t store_frag(struct kobject *kobj, struct attribute *attr,
			  char *buff, size_t count)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	int frag_enabled_tmp = -1;

	if (((count == 2) && (buff[0] == '1')) ||
	    (strncmp(buff, "enable", 6) == 0))
		frag_enabled_tmp = 1;

	if (((count == 2) && (buff[0] == '0')) ||
	    (strncmp(buff, "disable", 7) == 0))
		frag_enabled_tmp = 0;

	if (frag_enabled_tmp < 0) {
		if (buff[count - 1] == '\n')
			buff[count - 1] = '\0';

		bat_err(net_dev,
			"Invalid parameter for 'fragmentation' setting on mesh"
			"received: %s\n", buff);
		return -EINVAL;
	}

	if (atomic_read(&bat_priv->fragmentation) == frag_enabled_tmp)
		return count;

	bat_info(net_dev, "Changing fragmentation from: %s to: %s\n",
		 atomic_read(&bat_priv->fragmentation) == 1 ?
		 "enabled" : "disabled",
		 frag_enabled_tmp == 1 ? "enabled" : "disabled");

	atomic_set(&bat_priv->fragmentation, (unsigned)frag_enabled_tmp);
	update_min_mtu(net_dev);
	return count;
}

static ssize_t show_vis_mode(struct kobject *kobj, struct attribute *attr,
			     char *buff)
{
	struct device *dev = to_dev(kobj->parent);
	struct bat_priv *bat_priv = netdev_priv(to_net_dev(dev));
	int vis_mode = atomic_read(&bat_priv->vis_mode);

	return sprintf(buff, "%s\n",
		       vis_mode == VIS_TYPE_CLIENT_UPDATE ?
							"client" : "server");
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
	    (strncmp(buff, "client", 6) == 0) ||
	    (strncmp(buff, "off", 3) == 0))
		vis_mode_tmp = VIS_TYPE_CLIENT_UPDATE;

	if (((count == 2) && (!ret) && (val == VIS_TYPE_SERVER_SYNC)) ||
	    (strncmp(buff, "server", 6) == 0))
		vis_mode_tmp = VIS_TYPE_SERVER_SYNC;

	if (vis_mode_tmp < 0) {
		if (buff[count - 1] == '\n')
			buff[count - 1] = '\0';

		bat_info(net_dev,
			 "Invalid parameter for 'vis mode' setting received: "
			 "%s\n", buff);
		return -EINVAL;
	}

	if (atomic_read(&bat_priv->vis_mode) == vis_mode_tmp)
		return count;

	bat_info(net_dev, "Changing vis mode from: %s to: %s\n",
		 atomic_read(&bat_priv->vis_mode) == VIS_TYPE_CLIENT_UPDATE ?
		 "client" : "server", vis_mode_tmp == VIS_TYPE_CLIENT_UPDATE ?
		 "client" : "server");

	atomic_set(&bat_priv->vis_mode, (unsigned)vis_mode_tmp);
	return count;
}

static ssize_t show_orig_interval(struct kobject *kobj, struct attribute *attr,
				 char *buff)
{
	struct device *dev = to_dev(kobj->parent);
	struct bat_priv *bat_priv = netdev_priv(to_net_dev(dev));

	return sprintf(buff, "%i\n",
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
		bat_info(net_dev, "Invalid parameter for 'orig_interval' "
			 "setting received: %s\n", buff);
		return -EINVAL;
	}

	if (orig_interval_tmp < JITTER * 2) {
		bat_info(net_dev, "New originator interval too small: %li "
			 "(min: %i)\n", orig_interval_tmp, JITTER * 2);
		return -EINVAL;
	}

	if (atomic_read(&bat_priv->orig_interval) == orig_interval_tmp)
		return count;

	bat_info(net_dev, "Changing originator interval from: %i to: %li\n",
		 atomic_read(&bat_priv->orig_interval),
		 orig_interval_tmp);

	atomic_set(&bat_priv->orig_interval, orig_interval_tmp);
	return count;
}

#ifdef CONFIG_BATMAN_ADV_DEBUG
static ssize_t show_log_level(struct kobject *kobj, struct attribute *attr,
			     char *buff)
{
	struct device *dev = to_dev(kobj->parent);
	struct bat_priv *bat_priv = netdev_priv(to_net_dev(dev));
	int log_level = atomic_read(&bat_priv->log_level);

	return sprintf(buff, "%d\n", log_level);
}

static ssize_t store_log_level(struct kobject *kobj, struct attribute *attr,
			      char *buff, size_t count)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	unsigned long log_level_tmp;
	int ret;

	ret = strict_strtoul(buff, 10, &log_level_tmp);
	if (ret) {
		bat_info(net_dev, "Invalid parameter for 'log_level' "
			 "setting received: %s\n", buff);
		return -EINVAL;
	}

	if (log_level_tmp > 3) {
		bat_info(net_dev, "New log level too big: %li "
			 "(max: %i)\n", log_level_tmp, 3);
		return -EINVAL;
	}

	if (atomic_read(&bat_priv->log_level) == log_level_tmp)
		return count;

	bat_info(net_dev, "Changing log level from: %i to: %li\n",
		 atomic_read(&bat_priv->log_level),
		 log_level_tmp);

	atomic_set(&bat_priv->log_level, (unsigned)log_level_tmp);
	return count;
}
#endif

static BAT_ATTR(aggregated_ogms, S_IRUGO | S_IWUSR,
		show_aggr_ogms, store_aggr_ogms);
static BAT_ATTR(bonding, S_IRUGO | S_IWUSR, show_bond, store_bond);
static BAT_ATTR(fragmentation, S_IRUGO | S_IWUSR, show_frag, store_frag);
static BAT_ATTR(vis_mode, S_IRUGO | S_IWUSR, show_vis_mode, store_vis_mode);
static BAT_ATTR(orig_interval, S_IRUGO | S_IWUSR,
		show_orig_interval, store_orig_interval);
#ifdef CONFIG_BATMAN_ADV_DEBUG
static BAT_ATTR(log_level, S_IRUGO | S_IWUSR, show_log_level, store_log_level);
#endif

static struct bat_attribute *mesh_attrs[] = {
	&bat_attr_aggregated_ogms,
	&bat_attr_bonding,
	&bat_attr_fragmentation,
	&bat_attr_vis_mode,
	&bat_attr_orig_interval,
#ifdef CONFIG_BATMAN_ADV_DEBUG
	&bat_attr_log_level,
#endif
	NULL,
};

int sysfs_add_meshif(struct net_device *dev)
{
	struct kobject *batif_kobject = &dev->dev.kobj;
	struct bat_priv *bat_priv = netdev_priv(dev);
	struct bat_attribute **bat_attr;
	int err;

	bat_priv->mesh_obj = kobject_create_and_add(SYSFS_IF_MESH_SUBDIR,
						    batif_kobject);
	if (!bat_priv->mesh_obj) {
		bat_err(dev, "Can't add sysfs directory: %s/%s\n", dev->name,
			SYSFS_IF_MESH_SUBDIR);
		goto out;
	}

	for (bat_attr = mesh_attrs; *bat_attr; ++bat_attr) {
		err = sysfs_create_file(bat_priv->mesh_obj,
					&((*bat_attr)->attr));
		if (err) {
			bat_err(dev, "Can't add sysfs file: %s/%s/%s\n",
				dev->name, SYSFS_IF_MESH_SUBDIR,
				((*bat_attr)->attr).name);
			goto rem_attr;
		}
	}

	return 0;

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
	ssize_t length;

	if (!batman_if)
		return 0;

	length = sprintf(buff, "%s\n", batman_if->if_status == IF_NOT_IN_USE ?
			 "none" : batman_if->soft_iface->name);

	kref_put(&batman_if->refcount, hardif_free_ref);

	return length;
}

static ssize_t store_mesh_iface(struct kobject *kobj, struct attribute *attr,
				char *buff, size_t count)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);
	struct batman_if *batman_if = get_batman_if_by_netdev(net_dev);
	int status_tmp = -1;
	int ret;

	if (!batman_if)
		return count;

	if (buff[count - 1] == '\n')
		buff[count - 1] = '\0';

	if (strlen(buff) >= IFNAMSIZ) {
		pr_err("Invalid parameter for 'mesh_iface' setting received: "
		       "interface name too long '%s'\n", buff);
		kref_put(&batman_if->refcount, hardif_free_ref);
		return -EINVAL;
	}

	if (strncmp(buff, "none", 4) == 0)
		status_tmp = IF_NOT_IN_USE;
	else
		status_tmp = IF_I_WANT_YOU;

	if ((batman_if->if_status == status_tmp) || ((batman_if->soft_iface) &&
	    (strncmp(batman_if->soft_iface->name, buff, IFNAMSIZ) == 0))) {
		kref_put(&batman_if->refcount, hardif_free_ref);
		return count;
	}

	if (status_tmp == IF_NOT_IN_USE) {
		rtnl_lock();
		hardif_disable_interface(batman_if);
		rtnl_unlock();
		kref_put(&batman_if->refcount, hardif_free_ref);
		return count;
	}

	/* if the interface already is in use */
	if (batman_if->if_status != IF_NOT_IN_USE) {
		rtnl_lock();
		hardif_disable_interface(batman_if);
		rtnl_unlock();
	}

	ret = hardif_enable_interface(batman_if, buff);
	kref_put(&batman_if->refcount, hardif_free_ref);

	return ret;
}

static ssize_t show_iface_status(struct kobject *kobj, struct attribute *attr,
				 char *buff)
{
	struct device *dev = to_dev(kobj->parent);
	struct net_device *net_dev = to_net_dev(dev);
	struct batman_if *batman_if = get_batman_if_by_netdev(net_dev);
	ssize_t length;

	if (!batman_if)
		return 0;

	switch (batman_if->if_status) {
	case IF_TO_BE_REMOVED:
		length = sprintf(buff, "disabling\n");
		break;
	case IF_INACTIVE:
		length = sprintf(buff, "inactive\n");
		break;
	case IF_ACTIVE:
		length = sprintf(buff, "active\n");
		break;
	case IF_TO_BE_ACTIVATED:
		length = sprintf(buff, "enabling\n");
		break;
	case IF_NOT_IN_USE:
	default:
		length = sprintf(buff, "not in use\n");
		break;
	}

	kref_put(&batman_if->refcount, hardif_free_ref);

	return length;
}

static BAT_ATTR(mesh_iface, S_IRUGO | S_IWUSR,
		show_mesh_iface, store_mesh_iface);
static BAT_ATTR(iface_status, S_IRUGO, show_iface_status, NULL);

static struct bat_attribute *batman_attrs[] = {
	&bat_attr_mesh_iface,
	&bat_attr_iface_status,
	NULL,
};

int sysfs_add_hardif(struct kobject **hardif_obj, struct net_device *dev)
{
	struct kobject *hardif_kobject = &dev->dev.kobj;
	struct bat_attribute **bat_attr;
	int err;

	*hardif_obj = kobject_create_and_add(SYSFS_IF_BAT_SUBDIR,
						    hardif_kobject);

	if (!*hardif_obj) {
		bat_err(dev, "Can't add sysfs directory: %s/%s\n", dev->name,
			SYSFS_IF_BAT_SUBDIR);
		goto out;
	}

	for (bat_attr = batman_attrs; *bat_attr; ++bat_attr) {
		err = sysfs_create_file(*hardif_obj, &((*bat_attr)->attr));
		if (err) {
			bat_err(dev, "Can't add sysfs file: %s/%s/%s\n",
				dev->name, SYSFS_IF_BAT_SUBDIR,
				((*bat_attr)->attr).name);
			goto rem_attr;
		}
	}

	return 0;

rem_attr:
	for (bat_attr = batman_attrs; *bat_attr; ++bat_attr)
		sysfs_remove_file(*hardif_obj, &((*bat_attr)->attr));
out:
	return -ENOMEM;
}

void sysfs_del_hardif(struct kobject **hardif_obj)
{
	kobject_put(*hardif_obj);
	*hardif_obj = NULL;
}
