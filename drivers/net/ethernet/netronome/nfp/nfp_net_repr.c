/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
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

#include <linux/etherdevice.h>
#include <linux/lockdep.h>
#include <net/dst_metadata.h>

#include "nfpcore/nfp_cpp.h"
#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_net_repr.h"
#include "nfp_port.h"

static void nfp_repr_clean(struct nfp_repr *repr)
{
	unregister_netdev(repr->netdev);
	dst_release((struct dst_entry *)repr->dst);
	nfp_port_free(repr->port);
}

static struct lock_class_key nfp_repr_netdev_xmit_lock_key;
static struct lock_class_key nfp_repr_netdev_addr_lock_key;

static void nfp_repr_set_lockdep_class_one(struct net_device *dev,
					   struct netdev_queue *txq,
					   void *_unused)
{
	lockdep_set_class(&txq->_xmit_lock, &nfp_repr_netdev_xmit_lock_key);
}

static void nfp_repr_set_lockdep_class(struct net_device *dev)
{
	lockdep_set_class(&dev->addr_list_lock, &nfp_repr_netdev_addr_lock_key);
	netdev_for_each_tx_queue(dev, nfp_repr_set_lockdep_class_one, NULL);
}

int nfp_repr_init(struct nfp_app *app, struct net_device *netdev,
		  const struct net_device_ops *netdev_ops, u32 cmsg_port_id,
		  struct nfp_port *port, struct net_device *pf_netdev)
{
	struct nfp_repr *repr = netdev_priv(netdev);
	int err;

	nfp_repr_set_lockdep_class(netdev);

	repr->port = port;
	repr->dst = metadata_dst_alloc(0, METADATA_HW_PORT_MUX, GFP_KERNEL);
	if (!repr->dst)
		return -ENOMEM;
	repr->dst->u.port_info.port_id = cmsg_port_id;
	repr->dst->u.port_info.lower_dev = pf_netdev;

	netdev->netdev_ops = netdev_ops;

	err = register_netdev(netdev);
	if (err)
		goto err_clean;

	return 0;

err_clean:
	dst_release((struct dst_entry *)repr->dst);
	return err;
}

struct net_device *nfp_repr_alloc(struct nfp_app *app)
{
	struct net_device *netdev;
	struct nfp_repr *repr;

	netdev = alloc_etherdev(sizeof(*repr));
	if (!netdev)
		return NULL;

	repr = netdev_priv(netdev);
	repr->netdev = netdev;
	repr->app = app;

	return netdev;
}

static void nfp_repr_clean_and_free(struct nfp_repr *repr)
{
	nfp_info(repr->app->cpp, "Destroying Representor(%s)\n",
		 repr->netdev->name);
	nfp_repr_clean(repr);
	free_netdev(repr->netdev);
}

void nfp_reprs_clean_and_free(struct nfp_reprs *reprs)
{
	unsigned int i;

	for (i = 0; i < reprs->num_reprs; i++)
		if (reprs->reprs[i])
			nfp_repr_clean_and_free(netdev_priv(reprs->reprs[i]));

	kfree(reprs);
}

void
nfp_reprs_clean_and_free_by_type(struct nfp_app *app,
				 enum nfp_repr_type type)
{
	struct nfp_reprs *reprs;

	reprs = nfp_app_reprs_set(app, type, NULL);
	if (!reprs)
		return;

	synchronize_rcu();
	nfp_reprs_clean_and_free(reprs);
}

struct nfp_reprs *nfp_reprs_alloc(unsigned int num_reprs)
{
	struct nfp_reprs *reprs;

	reprs = kzalloc(sizeof(*reprs) +
			num_reprs * sizeof(struct net_device *), GFP_KERNEL);
	if (!reprs)
		return NULL;
	reprs->num_reprs = num_reprs;

	return reprs;
}
