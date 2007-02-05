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
 *
 * $Id: ipoib_vlan.c 1349 2004-12-16 21:09:43Z roland $
 */

#include <linux/module.h>

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/seq_file.h>

#include <asm/uaccess.h>

#include "ipoib.h"

static ssize_t show_parent(struct device *d, struct device_attribute *attr,
			   char *buf)
{
	struct net_device *dev = to_net_dev(d);
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	return sprintf(buf, "%s\n", priv->parent->name);
}
static DEVICE_ATTR(parent, S_IRUGO, show_parent, NULL);

int ipoib_vlan_add(struct net_device *pdev, unsigned short pkey)
{
	struct ipoib_dev_priv *ppriv, *priv;
	char intf_name[IFNAMSIZ];
	int result;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	ppriv = netdev_priv(pdev);

	mutex_lock(&ppriv->vlan_mutex);

	/*
	 * First ensure this isn't a duplicate. We check the parent device and
	 * then all of the child interfaces to make sure the Pkey doesn't match.
	 */
	if (ppriv->pkey == pkey) {
		result = -ENOTUNIQ;
		goto err;
	}

	list_for_each_entry(priv, &ppriv->child_intfs, list) {
		if (priv->pkey == pkey) {
			result = -ENOTUNIQ;
			goto err;
		}
	}

	snprintf(intf_name, sizeof intf_name, "%s.%04x",
		 ppriv->dev->name, pkey);
	priv = ipoib_intf_alloc(intf_name);
	if (!priv) {
		result = -ENOMEM;
		goto err;
	}

	set_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags);

	priv->pkey = pkey;

	memcpy(priv->dev->dev_addr, ppriv->dev->dev_addr, INFINIBAND_ALEN);
	priv->dev->broadcast[8] = pkey >> 8;
	priv->dev->broadcast[9] = pkey & 0xff;

	result = ipoib_dev_init(priv->dev, ppriv->ca, ppriv->port);
	if (result < 0) {
		ipoib_warn(ppriv, "failed to initialize subinterface: "
			   "device %s, port %d",
			   ppriv->ca->name, ppriv->port);
		goto device_init_failed;
	}

	result = register_netdev(priv->dev);
	if (result) {
		ipoib_warn(priv, "failed to initialize; error %i", result);
		goto register_failed;
	}

	priv->parent = ppriv->dev;

	ipoib_create_debug_files(priv->dev);

	if (ipoib_cm_add_mode_attr(priv->dev))
		goto sysfs_failed;
	if (ipoib_add_pkey_attr(priv->dev))
		goto sysfs_failed;

	if (device_create_file(&priv->dev->dev, &dev_attr_parent))
		goto sysfs_failed;

	list_add_tail(&priv->list, &ppriv->child_intfs);

	mutex_unlock(&ppriv->vlan_mutex);

	return 0;

sysfs_failed:
	ipoib_delete_debug_files(priv->dev);
	unregister_netdev(priv->dev);

register_failed:
	ipoib_dev_cleanup(priv->dev);

device_init_failed:
	free_netdev(priv->dev);

err:
	mutex_unlock(&ppriv->vlan_mutex);
	return result;
}

int ipoib_vlan_delete(struct net_device *pdev, unsigned short pkey)
{
	struct ipoib_dev_priv *ppriv, *priv, *tpriv;
	int ret = -ENOENT;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	ppriv = netdev_priv(pdev);

	mutex_lock(&ppriv->vlan_mutex);
	list_for_each_entry_safe(priv, tpriv, &ppriv->child_intfs, list) {
		if (priv->pkey == pkey) {
			unregister_netdev(priv->dev);
			ipoib_dev_cleanup(priv->dev);
			list_del(&priv->list);
			free_netdev(priv->dev);

			ret = 0;
			break;
		}
	}
	mutex_unlock(&ppriv->vlan_mutex);

	return ret;
}
