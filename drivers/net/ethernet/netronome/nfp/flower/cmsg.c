/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
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

#include <linux/bitfield.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <net/dst_metadata.h>

#include "main.h"
#include "../nfp_net.h"
#include "../nfp_net_repr.h"
#include "./cmsg.h"

static struct nfp_flower_cmsg_hdr *
nfp_flower_cmsg_get_hdr(struct sk_buff *skb)
{
	return (struct nfp_flower_cmsg_hdr *)skb->data;
}

struct sk_buff *
nfp_flower_cmsg_alloc(struct nfp_app *app, unsigned int size,
		      enum nfp_flower_cmsg_type_port type, gfp_t flag)
{
	struct nfp_flower_cmsg_hdr *ch;
	struct sk_buff *skb;

	size += NFP_FLOWER_CMSG_HLEN;

	skb = nfp_app_ctrl_msg_alloc(app, size, flag);
	if (!skb)
		return NULL;

	ch = nfp_flower_cmsg_get_hdr(skb);
	ch->pad = 0;
	ch->version = NFP_FLOWER_CMSG_VER1;
	ch->type = type;
	skb_put(skb, size);

	return skb;
}

struct sk_buff *
nfp_flower_cmsg_mac_repr_start(struct nfp_app *app, unsigned int num_ports)
{
	struct nfp_flower_cmsg_mac_repr *msg;
	struct sk_buff *skb;
	unsigned int size;

	size = sizeof(*msg) + num_ports * sizeof(msg->ports[0]);
	skb = nfp_flower_cmsg_alloc(app, size, NFP_FLOWER_CMSG_TYPE_MAC_REPR,
				    GFP_KERNEL);
	if (!skb)
		return NULL;

	msg = nfp_flower_cmsg_get_data(skb);
	memset(msg->reserved, 0, sizeof(msg->reserved));
	msg->num_ports = num_ports;

	return skb;
}

void
nfp_flower_cmsg_mac_repr_add(struct sk_buff *skb, unsigned int idx,
			     unsigned int nbi, unsigned int nbi_port,
			     unsigned int phys_port)
{
	struct nfp_flower_cmsg_mac_repr *msg;

	msg = nfp_flower_cmsg_get_data(skb);
	msg->ports[idx].idx = idx;
	msg->ports[idx].info = nbi & NFP_FLOWER_CMSG_MAC_REPR_NBI;
	msg->ports[idx].nbi_port = nbi_port;
	msg->ports[idx].phys_port = phys_port;
}

int nfp_flower_cmsg_portmod(struct nfp_repr *repr, bool carrier_ok,
			    unsigned int mtu, bool mtu_only)
{
	struct nfp_flower_cmsg_portmod *msg;
	struct sk_buff *skb;

	skb = nfp_flower_cmsg_alloc(repr->app, sizeof(*msg),
				    NFP_FLOWER_CMSG_TYPE_PORT_MOD, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	msg = nfp_flower_cmsg_get_data(skb);
	msg->portnum = cpu_to_be32(repr->dst->u.port_info.port_id);
	msg->reserved = 0;
	msg->info = carrier_ok;

	if (mtu_only)
		msg->info |= NFP_FLOWER_CMSG_PORTMOD_MTU_CHANGE_ONLY;

	msg->mtu = cpu_to_be16(mtu);

	nfp_ctrl_tx(repr->app->ctrl, skb);

	return 0;
}

int nfp_flower_cmsg_portreify(struct nfp_repr *repr, bool exists)
{
	struct nfp_flower_cmsg_portreify *msg;
	struct sk_buff *skb;

	skb = nfp_flower_cmsg_alloc(repr->app, sizeof(*msg),
				    NFP_FLOWER_CMSG_TYPE_PORT_REIFY,
				    GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	msg = nfp_flower_cmsg_get_data(skb);
	msg->portnum = cpu_to_be32(repr->dst->u.port_info.port_id);
	msg->reserved = 0;
	msg->info = cpu_to_be16(exists);

	nfp_ctrl_tx(repr->app->ctrl, skb);

	return 0;
}

static bool
nfp_flower_process_mtu_ack(struct nfp_app *app, struct sk_buff *skb)
{
	struct nfp_flower_priv *app_priv = app->priv;
	struct nfp_flower_cmsg_portmod *msg;

	msg = nfp_flower_cmsg_get_data(skb);

	if (!(msg->info & NFP_FLOWER_CMSG_PORTMOD_MTU_CHANGE_ONLY))
		return false;

	spin_lock_bh(&app_priv->mtu_conf.lock);
	if (!app_priv->mtu_conf.requested_val ||
	    app_priv->mtu_conf.portnum != be32_to_cpu(msg->portnum) ||
	    be16_to_cpu(msg->mtu) != app_priv->mtu_conf.requested_val) {
		/* Not an ack for requested MTU change. */
		spin_unlock_bh(&app_priv->mtu_conf.lock);
		return false;
	}

	app_priv->mtu_conf.ack = true;
	app_priv->mtu_conf.requested_val = 0;
	wake_up(&app_priv->mtu_conf.wait_q);
	spin_unlock_bh(&app_priv->mtu_conf.lock);

	return true;
}

static void
nfp_flower_cmsg_portmod_rx(struct nfp_app *app, struct sk_buff *skb)
{
	struct nfp_flower_cmsg_portmod *msg;
	struct net_device *netdev;
	bool link;

	msg = nfp_flower_cmsg_get_data(skb);
	link = msg->info & NFP_FLOWER_CMSG_PORTMOD_INFO_LINK;

	rtnl_lock();
	rcu_read_lock();
	netdev = nfp_app_repr_get(app, be32_to_cpu(msg->portnum));
	rcu_read_unlock();
	if (!netdev) {
		nfp_flower_cmsg_warn(app, "ctrl msg for unknown port 0x%08x\n",
				     be32_to_cpu(msg->portnum));
		rtnl_unlock();
		return;
	}

	if (link) {
		u16 mtu = be16_to_cpu(msg->mtu);

		netif_carrier_on(netdev);

		/* An MTU of 0 from the firmware should be ignored */
		if (mtu)
			dev_set_mtu(netdev, mtu);
	} else {
		netif_carrier_off(netdev);
	}
	rtnl_unlock();
}

static void
nfp_flower_cmsg_portreify_rx(struct nfp_app *app, struct sk_buff *skb)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_flower_cmsg_portreify *msg;
	bool exists;

	msg = nfp_flower_cmsg_get_data(skb);

	rcu_read_lock();
	exists = !!nfp_app_repr_get(app, be32_to_cpu(msg->portnum));
	rcu_read_unlock();
	if (!exists) {
		nfp_flower_cmsg_warn(app, "ctrl msg for unknown port 0x%08x\n",
				     be32_to_cpu(msg->portnum));
		return;
	}

	atomic_inc(&priv->reify_replies);
	wake_up_interruptible(&priv->reify_wait_queue);
}

static void
nfp_flower_cmsg_process_one_rx(struct nfp_app *app, struct sk_buff *skb)
{
	struct nfp_flower_cmsg_hdr *cmsg_hdr;
	enum nfp_flower_cmsg_type_port type;

	cmsg_hdr = nfp_flower_cmsg_get_hdr(skb);

	type = cmsg_hdr->type;
	switch (type) {
	case NFP_FLOWER_CMSG_TYPE_PORT_REIFY:
		nfp_flower_cmsg_portreify_rx(app, skb);
		break;
	case NFP_FLOWER_CMSG_TYPE_PORT_MOD:
		nfp_flower_cmsg_portmod_rx(app, skb);
		break;
	case NFP_FLOWER_CMSG_TYPE_NO_NEIGH:
		nfp_tunnel_request_route(app, skb);
		break;
	case NFP_FLOWER_CMSG_TYPE_ACTIVE_TUNS:
		nfp_tunnel_keep_alive(app, skb);
		break;
	default:
		nfp_flower_cmsg_warn(app, "Cannot handle invalid repr control type %u\n",
				     type);
		goto out;
	}

	dev_consume_skb_any(skb);
	return;
out:
	dev_kfree_skb_any(skb);
}

void nfp_flower_cmsg_process_rx(struct work_struct *work)
{
	struct nfp_flower_priv *priv;
	struct sk_buff *skb;

	priv = container_of(work, struct nfp_flower_priv, cmsg_work);

	while ((skb = skb_dequeue(&priv->cmsg_skbs)))
		nfp_flower_cmsg_process_one_rx(priv->app, skb);
}

void nfp_flower_cmsg_rx(struct nfp_app *app, struct sk_buff *skb)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_flower_cmsg_hdr *cmsg_hdr;

	cmsg_hdr = nfp_flower_cmsg_get_hdr(skb);

	if (unlikely(cmsg_hdr->version != NFP_FLOWER_CMSG_VER1)) {
		nfp_flower_cmsg_warn(app, "Cannot handle repr control version %u\n",
				     cmsg_hdr->version);
		dev_kfree_skb_any(skb);
		return;
	}

	if (cmsg_hdr->type == NFP_FLOWER_CMSG_TYPE_FLOW_STATS) {
		/* We need to deal with stats updates from HW asap */
		nfp_flower_rx_flow_stats(app, skb);
		dev_consume_skb_any(skb);
	} else if (cmsg_hdr->type == NFP_FLOWER_CMSG_TYPE_PORT_MOD &&
		   nfp_flower_process_mtu_ack(app, skb)) {
		/* Handle MTU acks outside wq to prevent RTNL conflict. */
		dev_consume_skb_any(skb);
	} else if (cmsg_hdr->type == NFP_FLOWER_CMSG_TYPE_TUN_NEIGH) {
		/* Acks from the NFP that the route is added - ignore. */
		dev_consume_skb_any(skb);
	} else {
		skb_queue_tail(&priv->cmsg_skbs, skb);
		schedule_work(&priv->cmsg_work);
	}
}
