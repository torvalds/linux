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
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	return sprintf(buf, "%s\n", priv->parent->name);
}
static DEVICE_ATTR(parent, S_IRUGO, show_parent, NULL);

/*
 * NOTE: If this function fails then the priv->dev will remain valid, however
 * priv can have been freed and must not be touched by caller in the error
 * case.
 *
 * If (ndev->reg_state == NETREG_UNINITIALIZED) then it is up to the caller to
 * free the net_device (just as rtnl_newlink does) otherwise the net_device
 * will be freed when the rtnl is unlocked.
 */
int __ipoib_vlan_add(struct ipoib_dev_priv *ppriv, struct ipoib_dev_priv *priv,
		     u16 pkey, int type)
{
	struct net_device *ndev = priv->dev;
	int result;

	ASSERT_RTNL();

	priv->parent = ppriv->dev;
	priv->pkey = pkey;
	priv->child_type = type;

	/* We do not need to touch priv if register_netdevice fails */
	ndev->priv_destructor = ipoib_intf_free;

	result = register_netdevice(ndev);
	if (result) {
		ipoib_warn(priv, "failed to initialize; error %i", result);

		/*
		 * register_netdevice sometimes calls priv_destructor,
		 * sometimes not. Make sure it was done.
		 */
		if (ndev->priv_destructor)
			ndev->priv_destructor(ndev);
		return result;
	}

	/* RTNL childs don't need proprietary sysfs entries */
	if (type == IPOIB_LEGACY_CHILD) {
		if (ipoib_cm_add_mode_attr(ndev))
			goto sysfs_failed;
		if (ipoib_add_pkey_attr(ndev))
			goto sysfs_failed;
		if (ipoib_add_umcast_attr(ndev))
			goto sysfs_failed;

		if (device_create_file(&ndev->dev, &dev_attr_parent))
			goto sysfs_failed;
	}

	list_add_tail(&priv->list, &ppriv->child_intfs);

	return 0;

sysfs_failed:
	unregister_netdevice(priv->dev);
	return -ENOMEM;
}

int ipoib_vlan_add(struct net_device *pdev, unsigned short pkey)
{
	struct ipoib_dev_priv *ppriv, *priv;
	char intf_name[IFNAMSIZ];
	struct net_device *ndev;
	struct ipoib_dev_priv *tpriv;
	int result;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	ppriv = ipoib_priv(pdev);

	snprintf(intf_name, sizeof(intf_name), "%s.%04x",
		 ppriv->dev->name, pkey);

	if (!mutex_trylock(&ppriv->sysfs_mutex))
		return restart_syscall();

	if (!rtnl_trylock()) {
		mutex_unlock(&ppriv->sysfs_mutex);
		return restart_syscall();
	}

	if (pdev->reg_state != NETREG_REGISTERED) {
		rtnl_unlock();
		mutex_unlock(&ppriv->sysfs_mutex);
		return -EPERM;
	}

	if (!down_write_trylock(&ppriv->vlan_rwsem)) {
		rtnl_unlock();
		mutex_unlock(&ppriv->sysfs_mutex);
		return restart_syscall();
	}

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

	priv = ipoib_intf_alloc(ppriv->ca, ppriv->port, intf_name);
	if (!priv) {
		result = -ENOMEM;
		goto out;
	}
	ndev = priv->dev;

	result = __ipoib_vlan_add(ppriv, priv, pkey, IPOIB_LEGACY_CHILD);

	if (result && ndev->reg_state == NETREG_UNINITIALIZED)
		free_netdev(ndev);

out:
	up_write(&ppriv->vlan_rwsem);
	rtnl_unlock();
	mutex_unlock(&ppriv->sysfs_mutex);

	return result;
}

int ipoib_vlan_delete(struct net_device *pdev, unsigned short pkey)
{
	struct ipoib_dev_priv *ppriv, *priv, *tpriv;
	struct net_device *dev = NULL;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	ppriv = ipoib_priv(pdev);

	if (!mutex_trylock(&ppriv->sysfs_mutex))
		return restart_syscall();

	if (!rtnl_trylock()) {
		mutex_unlock(&ppriv->sysfs_mutex);
		return restart_syscall();
	}

	if (pdev->reg_state != NETREG_REGISTERED) {
		rtnl_unlock();
		mutex_unlock(&ppriv->sysfs_mutex);
		return -EPERM;
	}

	if (!down_write_trylock(&ppriv->vlan_rwsem)) {
		rtnl_unlock();
		mutex_unlock(&ppriv->sysfs_mutex);
		return restart_syscall();
	}

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
	mutex_unlock(&ppriv->sysfs_mutex);

	return (dev) ? 0 : -ENODEV;
}
