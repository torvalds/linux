/*
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/errno.h>
#include <scsi/libfcoe.h>

#include "libfcoe.h"

MODULE_AUTHOR("Open-FCoE.org");
MODULE_DESCRIPTION("FIP discovery protocol and FCoE transport for FCoE HBAs");
MODULE_LICENSE("GPL v2");

static int fcoe_transport_create(const char *, struct kernel_param *);
static int fcoe_transport_destroy(const char *, struct kernel_param *);
static int fcoe_transport_show(char *buffer, const struct kernel_param *kp);
static struct fcoe_transport *fcoe_transport_lookup(struct net_device *device);
static struct fcoe_transport *fcoe_netdev_map_lookup(struct net_device *device);
static int fcoe_transport_enable(const char *, struct kernel_param *);
static int fcoe_transport_disable(const char *, struct kernel_param *);

static LIST_HEAD(fcoe_transports);
static LIST_HEAD(fcoe_netdevs);
static DEFINE_MUTEX(ft_mutex);

unsigned int libfcoe_debug_logging;
module_param_named(debug_logging, libfcoe_debug_logging, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug_logging, "a bit mask of logging levels");

module_param_call(show, NULL, fcoe_transport_show, NULL, S_IRUSR);
__MODULE_PARM_TYPE(show, "string");
MODULE_PARM_DESC(show, " Show attached FCoE transports");

module_param_call(create, fcoe_transport_create, NULL,
		  (void *)FIP_MODE_FABRIC, S_IWUSR);
__MODULE_PARM_TYPE(create, "string");
MODULE_PARM_DESC(create, " Creates fcoe instance on a ethernet interface");

module_param_call(create_vn2vn, fcoe_transport_create, NULL,
		  (void *)FIP_MODE_VN2VN, S_IWUSR);
__MODULE_PARM_TYPE(create_vn2vn, "string");
MODULE_PARM_DESC(create_vn2vn, " Creates a VN_node to VN_node FCoE instance "
		 "on an Ethernet interface");

module_param_call(destroy, fcoe_transport_destroy, NULL, NULL, S_IWUSR);
__MODULE_PARM_TYPE(destroy, "string");
MODULE_PARM_DESC(destroy, " Destroys fcoe instance on a ethernet interface");

module_param_call(enable, fcoe_transport_enable, NULL, NULL, S_IWUSR);
__MODULE_PARM_TYPE(enable, "string");
MODULE_PARM_DESC(enable, " Enables fcoe on a ethernet interface.");

module_param_call(disable, fcoe_transport_disable, NULL, NULL, S_IWUSR);
__MODULE_PARM_TYPE(disable, "string");
MODULE_PARM_DESC(disable, " Disables fcoe on a ethernet interface.");

/**
 * fcoe_transport_lookup - find an fcoe transport that matches a netdev
 * @netdev: The netdev to look for from all attached transports
 *
 * Returns : ptr to the fcoe transport that supports this netdev or NULL
 * if not found.
 *
 * The ft_mutex should be held when this is called
 */
static struct fcoe_transport *fcoe_transport_lookup(struct net_device *netdev)
{
	struct fcoe_transport *ft = NULL;

	list_for_each_entry(ft, &fcoe_transports, list)
		if (ft->match && ft->match(netdev))
			return ft;
	return NULL;
}

/**
 * fcoe_transport_attach - Attaches an FCoE transport
 * @ft: The fcoe transport to be attached
 *
 * Returns : 0 for success
 */
int fcoe_transport_attach(struct fcoe_transport *ft)
{
	int rc = 0;

	mutex_lock(&ft_mutex);
	if (ft->attached) {
		LIBFCOE_TRANSPORT_DBG("transport %s already attached\n",
				       ft->name);
		rc = -EEXIST;
		goto out_attach;
	}

	/* Add default transport to the tail */
	if (strcmp(ft->name, FCOE_TRANSPORT_DEFAULT))
		list_add(&ft->list, &fcoe_transports);
	else
		list_add_tail(&ft->list, &fcoe_transports);

	ft->attached = true;
	LIBFCOE_TRANSPORT_DBG("attaching transport %s\n", ft->name);

out_attach:
	mutex_unlock(&ft_mutex);
	return rc;
}
EXPORT_SYMBOL(fcoe_transport_attach);

/**
 * fcoe_transport_attach - Detaches an FCoE transport
 * @ft: The fcoe transport to be attached
 *
 * Returns : 0 for success
 */
int fcoe_transport_detach(struct fcoe_transport *ft)
{
	int rc = 0;

	mutex_lock(&ft_mutex);
	if (!ft->attached) {
		LIBFCOE_TRANSPORT_DBG("transport %s already detached\n",
			ft->name);
		rc = -ENODEV;
		goto out_attach;
	}

	list_del(&ft->list);
	ft->attached = false;
	LIBFCOE_TRANSPORT_DBG("detaching transport %s\n", ft->name);

out_attach:
	mutex_unlock(&ft_mutex);
	return rc;

}
EXPORT_SYMBOL(fcoe_transport_detach);

static int fcoe_transport_show(char *buffer, const struct kernel_param *kp)
{
	int i, j;
	struct fcoe_transport *ft = NULL;

	i = j = sprintf(buffer, "Attached FCoE transports:");
	mutex_lock(&ft_mutex);
	list_for_each_entry(ft, &fcoe_transports, list) {
		i += snprintf(&buffer[i], IFNAMSIZ, "%s ", ft->name);
		if (i >= PAGE_SIZE)
			break;
	}
	mutex_unlock(&ft_mutex);
	if (i == j)
		i += snprintf(&buffer[i], IFNAMSIZ, "none");
	return i;
}

static int __init fcoe_transport_init(void)
{
	return 0;
}

static int __exit fcoe_transport_exit(void)
{
	struct fcoe_transport *ft;

	mutex_lock(&ft_mutex);
	list_for_each_entry(ft, &fcoe_transports, list)
		printk(KERN_ERR "FCoE transport %s is still attached!\n",
		      ft->name);
	mutex_unlock(&ft_mutex);
	return 0;
}


static int fcoe_add_netdev_mapping(struct net_device *netdev,
					struct fcoe_transport *ft)
{
	struct fcoe_netdev_mapping *nm;

	nm = kmalloc(sizeof(*nm), GFP_KERNEL);
	if (!nm) {
		printk(KERN_ERR "Unable to allocate netdev_mapping");
		return -ENOMEM;
	}

	nm->netdev = netdev;
	nm->ft = ft;

	list_add(&nm->list, &fcoe_netdevs);
	return 0;
}


static void fcoe_del_netdev_mapping(struct net_device *netdev)
{
	struct fcoe_netdev_mapping *nm = NULL, *tmp;

	list_for_each_entry_safe(nm, tmp, &fcoe_netdevs, list) {
		if (nm->netdev == netdev) {
			list_del(&nm->list);
			kfree(nm);
			return;
		}
	}
}


/**
 * fcoe_netdev_map_lookup - find the fcoe transport that matches the netdev on which
 * it was created
 *
 * Returns : ptr to the fcoe transport that supports this netdev or NULL
 * if not found.
 *
 * The ft_mutex should be held when this is called
 */
static struct fcoe_transport *fcoe_netdev_map_lookup(struct net_device *netdev)
{
	struct fcoe_transport *ft = NULL;
	struct fcoe_netdev_mapping *nm;

	list_for_each_entry(nm, &fcoe_netdevs, list) {
		if (netdev == nm->netdev) {
			ft = nm->ft;
			return ft;
		}
	}

	return NULL;
}

/**
 * fcoe_if_to_netdev() - Parse a name buffer to get a net device
 * @buffer: The name of the net device
 *
 * Returns: NULL or a ptr to net_device
 */
static struct net_device *fcoe_if_to_netdev(const char *buffer)
{
	char *cp;
	char ifname[IFNAMSIZ + 2];

	if (buffer) {
		strlcpy(ifname, buffer, IFNAMSIZ);
		cp = ifname + strlen(ifname);
		while (--cp >= ifname && *cp == '\n')
			*cp = '\0';
		return dev_get_by_name(&init_net, ifname);
	}
	return NULL;
}

/**
 * fcoe_transport_create() - Create a fcoe interface
 * @buffer: The name of the Ethernet interface to create on
 * @kp:	    The associated kernel param
 *
 * Called from sysfs. This holds the ft_mutex while calling the
 * registered fcoe transport's create function.
 *
 * Returns: 0 for success
 */
static int fcoe_transport_create(const char *buffer, struct kernel_param *kp)
{
	int rc = -ENODEV;
	struct net_device *netdev = NULL;
	struct fcoe_transport *ft = NULL;
	enum fip_state fip_mode = (enum fip_state)(long)kp->arg;

	if (!mutex_trylock(&ft_mutex))
		return restart_syscall();

#ifdef CONFIG_LIBFCOE_MODULE
	/*
	 * Make sure the module has been initialized, and is not about to be
	 * removed.  Module parameter sysfs files are writable before the
	 * module_init function is called and after module_exit.
	 */
	if (THIS_MODULE->state != MODULE_STATE_LIVE)
		goto out_nodev;
#endif

	netdev = fcoe_if_to_netdev(buffer);
	if (!netdev) {
		LIBFCOE_TRANSPORT_DBG("Invalid device %s.\n", buffer);
		goto out_nodev;
	}

	ft = fcoe_netdev_map_lookup(netdev);
	if (ft) {
		LIBFCOE_TRANSPORT_DBG("transport %s already has existing "
				      "FCoE instance on %s.\n",
				      ft->name, netdev->name);
		rc = -EEXIST;
		goto out_putdev;
	}

	ft = fcoe_transport_lookup(netdev);
	if (!ft) {
		LIBFCOE_TRANSPORT_DBG("no FCoE transport found for %s.\n",
				      netdev->name);
		goto out_putdev;
	}

	rc = fcoe_add_netdev_mapping(netdev, ft);
	if (rc) {
		LIBFCOE_TRANSPORT_DBG("failed to add new netdev mapping "
				      "for FCoE transport %s for %s.\n",
				      ft->name, netdev->name);
		goto out_putdev;
	}

	/* pass to transport create */
	rc = ft->create ? ft->create(netdev, fip_mode) : -ENODEV;
	if (rc)
		fcoe_del_netdev_mapping(netdev);

	LIBFCOE_TRANSPORT_DBG("transport %s %s to create fcoe on %s.\n",
			      ft->name, (rc) ? "failed" : "succeeded",
			      netdev->name);

out_putdev:
	dev_put(netdev);
out_nodev:
	mutex_unlock(&ft_mutex);
	if (rc == -ERESTARTSYS)
		return restart_syscall();
	else
		return rc;
}

/**
 * fcoe_transport_destroy() - Destroy a FCoE interface
 * @buffer: The name of the Ethernet interface to be destroyed
 * @kp:	    The associated kernel parameter
 *
 * Called from sysfs. This holds the ft_mutex while calling the
 * registered fcoe transport's destroy function.
 *
 * Returns: 0 for success
 */
static int fcoe_transport_destroy(const char *buffer, struct kernel_param *kp)
{
	int rc = -ENODEV;
	struct net_device *netdev = NULL;
	struct fcoe_transport *ft = NULL;

	if (!mutex_trylock(&ft_mutex))
		return restart_syscall();

#ifdef CONFIG_LIBFCOE_MODULE
	/*
	 * Make sure the module has been initialized, and is not about to be
	 * removed.  Module parameter sysfs files are writable before the
	 * module_init function is called and after module_exit.
	 */
	if (THIS_MODULE->state != MODULE_STATE_LIVE)
		goto out_nodev;
#endif

	netdev = fcoe_if_to_netdev(buffer);
	if (!netdev) {
		LIBFCOE_TRANSPORT_DBG("invalid device %s.\n", buffer);
		goto out_nodev;
	}

	ft = fcoe_netdev_map_lookup(netdev);
	if (!ft) {
		LIBFCOE_TRANSPORT_DBG("no FCoE transport found for %s.\n",
				      netdev->name);
		goto out_putdev;
	}

	/* pass to transport destroy */
	rc = ft->destroy ? ft->destroy(netdev) : -ENODEV;
	fcoe_del_netdev_mapping(netdev);
	LIBFCOE_TRANSPORT_DBG("transport %s %s to destroy fcoe on %s.\n",
			      ft->name, (rc) ? "failed" : "succeeded",
			      netdev->name);

out_putdev:
	dev_put(netdev);
out_nodev:
	mutex_unlock(&ft_mutex);

	if (rc == -ERESTARTSYS)
		return restart_syscall();
	else
		return rc;
}

/**
 * fcoe_transport_disable() - Disables a FCoE interface
 * @buffer: The name of the Ethernet interface to be disabled
 * @kp:	    The associated kernel parameter
 *
 * Called from sysfs.
 *
 * Returns: 0 for success
 */
static int fcoe_transport_disable(const char *buffer, struct kernel_param *kp)
{
	int rc = -ENODEV;
	struct net_device *netdev = NULL;
	struct fcoe_transport *ft = NULL;

	if (!mutex_trylock(&ft_mutex))
		return restart_syscall();

#ifdef CONFIG_LIBFCOE_MODULE
	/*
	 * Make sure the module has been initialized, and is not about to be
	 * removed.  Module parameter sysfs files are writable before the
	 * module_init function is called and after module_exit.
	 */
	if (THIS_MODULE->state != MODULE_STATE_LIVE)
		goto out_nodev;
#endif

	netdev = fcoe_if_to_netdev(buffer);
	if (!netdev)
		goto out_nodev;

	ft = fcoe_netdev_map_lookup(netdev);
	if (!ft)
		goto out_putdev;

	rc = ft->disable ? ft->disable(netdev) : -ENODEV;

out_putdev:
	dev_put(netdev);
out_nodev:
	mutex_unlock(&ft_mutex);

	if (rc == -ERESTARTSYS)
		return restart_syscall();
	else
		return rc;
}

/**
 * fcoe_transport_enable() - Enables a FCoE interface
 * @buffer: The name of the Ethernet interface to be enabled
 * @kp:     The associated kernel parameter
 *
 * Called from sysfs.
 *
 * Returns: 0 for success
 */
static int fcoe_transport_enable(const char *buffer, struct kernel_param *kp)
{
	int rc = -ENODEV;
	struct net_device *netdev = NULL;
	struct fcoe_transport *ft = NULL;

	if (!mutex_trylock(&ft_mutex))
		return restart_syscall();

#ifdef CONFIG_LIBFCOE_MODULE
	/*
	 * Make sure the module has been initialized, and is not about to be
	 * removed.  Module parameter sysfs files are writable before the
	 * module_init function is called and after module_exit.
	 */
	if (THIS_MODULE->state != MODULE_STATE_LIVE)
		goto out_nodev;
#endif

	netdev = fcoe_if_to_netdev(buffer);
	if (!netdev)
		goto out_nodev;

	ft = fcoe_netdev_map_lookup(netdev);
	if (!ft)
		goto out_putdev;

	rc = ft->enable ? ft->enable(netdev) : -ENODEV;

out_putdev:
	dev_put(netdev);
out_nodev:
	mutex_unlock(&ft_mutex);
	if (rc == -ERESTARTSYS)
		return restart_syscall();
	else
		return rc;
}

/**
 * libfcoe_init() - Initialization routine for libfcoe.ko
 */
static int __init libfcoe_init(void)
{
	fcoe_transport_init();

	return 0;
}
module_init(libfcoe_init);

/**
 * libfcoe_exit() - Tear down libfcoe.ko
 */
static void __exit libfcoe_exit(void)
{
	fcoe_transport_exit();
}
module_exit(libfcoe_exit);
