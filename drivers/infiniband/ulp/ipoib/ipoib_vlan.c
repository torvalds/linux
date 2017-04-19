/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
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

#include <linux/module.h>
#include <linux/sched/signal.h>

#include <linux/init.h>
#include <linux/seq_file.h>

#include <linux/uaccess.h>

#include "ipoib.h"

static ssize_t show_parent(struct device *d, struct device_attribute *attr,
			   char *buf)
{
	struct net_device *dev = to_net_dev(d);
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	return sprintf(buf, "%s\n", priv->parent->name);
}
static DEVICE_ATTR(parent, S_IRUGO, show_parent, NULL);

int __ipoib_vlan_add(struct ipoib_dev_priv *ppriv, struct ipoib_dev_priv *priv,
		     u16 pkey, int type)
{
	int result;

	priv->max_ib_mtu = ppriv->max_ib_mtu;
	/* MTU will be reset when mcast join happens */
	priv->dev->mtu   = IPOIB_UD_MTU(priv->max_ib_mtu);
	priv->mcast_mtu  = priv->admin_mtu = priv->dev->mtu;
	priv->parent = ppriv->dev;
	set_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags);

	ipoib_set_dev_features(priv, ppriv->ca);

	priv->pkey = pkey;

	memcpy(priv->dev->dev_addr, ppriv->dev->dev_addr, INFINIBAND_ALEN);
	memcpy(&priv->local_gid, &ppriv->local_gid, sizeof(priv->local_gid));
	set_bit(IPOIB_FLAG_DEV_ADDR_SET, &priv->flags);
	priv->dev->broadcast[8] = pkey >> 8;
	priv->dev->broadcast[9] = pkey & 0xff;

	result = ipoib_dev_init(priv->dev, ppriv->ca, ppriv->port);
	if (result < 0) {
		ipoib_warn(ppriv, "failed to initialize subinterface: "
			   "device %s, port %d",
			   ppriv->ca->name, ppriv->port);
		goto err;
	}

	result = register_netdevice(priv->dev);
	if (result) {
		ipoib_warn(priv, "failed to initialize; error %i", result);
		goto register_failed;
	}

	ipoib_create_debug_files(priv->dev);

	/* RTNL childs don't need proprietary sysfs entries */
	if (type == IPOIB_LEGACY_CHILD) {
		if (ipoib_cm_add_mode_attr(priv->dev))
			goto sysfs_failed;
		if (ipoib_add_pkey_attr(priv->dev))
			goto sysfs_failed;
		if (ipoib_add_umcast_attr(priv->dev))
			goto sysfs_failed;

		if (device_create_file(&priv->dev->dev, &dev_attr_parent))
			goto sysfs_failed;
	}

	priv->child_type  = type;
	list_add_tail(&priv->list, &ppriv->child_intfs);

	return 0;

sysfs_failed:
	result = -ENOMEM;
	ipoib_delete_debug_files(priv->dev);
	unregister_netdevice(priv->dev);

register_failed:
	ipoib_dev_cleanup(priv->dev);

err:
	return result;
}

int ipoib_vlan_add(struct net_device *pdev, unsigned short pkey)
{
	struct ipoib_dev_priv *ppriv, *priv;
	char intf_name[IFNAMSIZ];
	struct ipoib_dev_priv *tpriv;
	int result;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	ppriv = netdev_priv(pdev);

	if (test_bit(IPOIB_FLAG_GOING_DOWN, &ppriv->flags))
		return -EPERM;

	snprintf(intf_name, sizeof intf_name, "%s.%04x",
		 ppriv->dev->name, pkey);
	priv = ipoib_intf_alloc(intf_name);
	if (!priv)
		return -ENOMEM;

	if (!rtnl_trylock())
		return restart_syscall();

	down_write(&ppriv->vlan_rwsem);

	/*
	 * First ensure this isn't a duplicate. We check the parent device and
	 * then all of the legacy child interfaces to make sure the Pkey
	 * doesn't match.
	 */
	if (ppriv->pkey == pkey) {
		result = -ENOTUNIQ;
		goto out;
	}

	list_for_each_entry(tpriv, &ppriv->child_intfs, list) {
		if (tpriv->pkey == pkey &&
		    tpriv->child_type == IPOIB_LEGACY_CHILD) {
			result = -ENOTUNIQ;
			goto out;
		}
	}

	result = __ipoib_vlan_add(ppriv, priv, pkey, IPOIB_LEGACY_CHILD);

out:
	up_write(&ppriv->vlan_rwsem);

	rtnl_unlock();

	if (result)
		free_netdev(priv->dev);

	return result;
}

int ipoib_vlan_delete(struct net_device *pdev, unsigned short pkey)
{
	struct ipoib_dev_priv *ppriv, *priv, *tpriv;
	struct net_device *dev = NULL;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	ppriv = netdev_priv(pdev);

	if (test_bit(IPOIB_FLAG_GOING_DOWN, &ppriv->flags))
		return -EPERM;

	if (!rtnl_trylock())
		return restart_syscall();

	down_write(&ppriv->vlan_rwsem);
	list_for_each_entry_safe(priv, tpriv, &ppriv->child_intfs, list) {
		if (priv->pkey == pkey &&
		    priv->child_type == IPOIB_LEGACY_CHILD) {
			list_del(&priv->list);
			dev = priv->dev;
			break;
		}
	}
	up_write(&ppriv->vlan_rwsem);

	if (dev) {
		ipoib_dbg(ppriv, "delete child vlan %s\n", dev->name);
		unregister_netdevice(dev);
	}

	rtnl_unlock();

	if (dev) {
		free_netdev(dev);
		return 0;
	}

	return -ENODEV;
}
