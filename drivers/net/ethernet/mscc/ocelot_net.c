// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot Switch driver
 *
 * This contains glue logic between the switchdev driver operations and the
 * mscc_ocelot_switch_lib.
 *
 * Copyright (c) 2017, 2019 Microsemi Corporation
 * Copyright 2020-2021 NXP
 */

#include <linux/dsa/ocelot.h>
#include <linux/if_bridge.h>
#include <linux/of_net.h>
#include <linux/phy/phy.h>
#include <net/pkt_cls.h>
#include "ocelot.h"
#include "ocelot_police.h"
#include "ocelot_vcap.h"
#include "ocelot_fdma.h"

#define OCELOT_MAC_QUIRKS	OCELOT_QUIRK_QSGMII_PORTS_MUST_BE_UP

struct ocelot_dump_ctx {
	struct net_device *dev;
	struct sk_buff *skb;
	struct netlink_callback *cb;
	int idx;
};

static bool ocelot_netdevice_dev_check(const struct net_device *dev);

static struct ocelot *devlink_port_to_ocelot(struct devlink_port *dlp)
{
	return devlink_priv(dlp->devlink);
}

static int devlink_port_to_port(struct devlink_port *dlp)
{
	struct ocelot *ocelot = devlink_port_to_ocelot(dlp);

	return dlp - ocelot->devlink_ports;
}

static int ocelot_devlink_sb_pool_get(struct devlink *dl,
				      unsigned int sb_index, u16 pool_index,
				      struct devlink_sb_pool_info *pool_info)
{
	struct ocelot *ocelot = devlink_priv(dl);

	return ocelot_sb_pool_get(ocelot, sb_index, pool_index, pool_info);
}

static int ocelot_devlink_sb_pool_set(struct devlink *dl, unsigned int sb_index,
				      u16 pool_index, u32 size,
				      enum devlink_sb_threshold_type threshold_type,
				      struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = devlink_priv(dl);

	return ocelot_sb_pool_set(ocelot, sb_index, pool_index, size,
				  threshold_type, extack);
}

static int ocelot_devlink_sb_port_pool_get(struct devlink_port *dlp,
					   unsigned int sb_index, u16 pool_index,
					   u32 *p_threshold)
{
	struct ocelot *ocelot = devlink_port_to_ocelot(dlp);
	int port = devlink_port_to_port(dlp);

	return ocelot_sb_port_pool_get(ocelot, port, sb_index, pool_index,
				       p_threshold);
}

static int ocelot_devlink_sb_port_pool_set(struct devlink_port *dlp,
					   unsigned int sb_index, u16 pool_index,
					   u32 threshold,
					   struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = devlink_port_to_ocelot(dlp);
	int port = devlink_port_to_port(dlp);

	return ocelot_sb_port_pool_set(ocelot, port, sb_index, pool_index,
				       threshold, extack);
}

static int
ocelot_devlink_sb_tc_pool_bind_get(struct devlink_port *dlp,
				   unsigned int sb_index, u16 tc_index,
				   enum devlink_sb_pool_type pool_type,
				   u16 *p_pool_index, u32 *p_threshold)
{
	struct ocelot *ocelot = devlink_port_to_ocelot(dlp);
	int port = devlink_port_to_port(dlp);

	return ocelot_sb_tc_pool_bind_get(ocelot, port, sb_index, tc_index,
					  pool_type, p_pool_index,
					  p_threshold);
}

static int
ocelot_devlink_sb_tc_pool_bind_set(struct devlink_port *dlp,
				   unsigned int sb_index, u16 tc_index,
				   enum devlink_sb_pool_type pool_type,
				   u16 pool_index, u32 threshold,
				   struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = devlink_port_to_ocelot(dlp);
	int port = devlink_port_to_port(dlp);

	return ocelot_sb_tc_pool_bind_set(ocelot, port, sb_index, tc_index,
					  pool_type, pool_index, threshold,
					  extack);
}

static int ocelot_devlink_sb_occ_snapshot(struct devlink *dl,
					  unsigned int sb_index)
{
	struct ocelot *ocelot = devlink_priv(dl);

	return ocelot_sb_occ_snapshot(ocelot, sb_index);
}

static int ocelot_devlink_sb_occ_max_clear(struct devlink *dl,
					   unsigned int sb_index)
{
	struct ocelot *ocelot = devlink_priv(dl);

	return ocelot_sb_occ_max_clear(ocelot, sb_index);
}

static int ocelot_devlink_sb_occ_port_pool_get(struct devlink_port *dlp,
					       unsigned int sb_index,
					       u16 pool_index, u32 *p_cur,
					       u32 *p_max)
{
	struct ocelot *ocelot = devlink_port_to_ocelot(dlp);
	int port = devlink_port_to_port(dlp);

	return ocelot_sb_occ_port_pool_get(ocelot, port, sb_index, pool_index,
					   p_cur, p_max);
}

static int
ocelot_devlink_sb_occ_tc_port_bind_get(struct devlink_port *dlp,
				       unsigned int sb_index, u16 tc_index,
				       enum devlink_sb_pool_type pool_type,
				       u32 *p_cur, u32 *p_max)
{
	struct ocelot *ocelot = devlink_port_to_ocelot(dlp);
	int port = devlink_port_to_port(dlp);

	return ocelot_sb_occ_tc_port_bind_get(ocelot, port, sb_index,
					      tc_index, pool_type,
					      p_cur, p_max);
}

const struct devlink_ops ocelot_devlink_ops = {
	.sb_pool_get			= ocelot_devlink_sb_pool_get,
	.sb_pool_set			= ocelot_devlink_sb_pool_set,
	.sb_port_pool_get		= ocelot_devlink_sb_port_pool_get,
	.sb_port_pool_set		= ocelot_devlink_sb_port_pool_set,
	.sb_tc_pool_bind_get		= ocelot_devlink_sb_tc_pool_bind_get,
	.sb_tc_pool_bind_set		= ocelot_devlink_sb_tc_pool_bind_set,
	.sb_occ_snapshot		= ocelot_devlink_sb_occ_snapshot,
	.sb_occ_max_clear		= ocelot_devlink_sb_occ_max_clear,
	.sb_occ_port_pool_get		= ocelot_devlink_sb_occ_port_pool_get,
	.sb_occ_tc_port_bind_get	= ocelot_devlink_sb_occ_tc_port_bind_get,
};

int ocelot_port_devlink_init(struct ocelot *ocelot, int port,
			     enum devlink_port_flavour flavour)
{
	struct devlink_port *dlp = &ocelot->devlink_ports[port];
	int id_len = sizeof(ocelot->base_mac);
	struct devlink *dl = ocelot->devlink;
	struct devlink_port_attrs attrs = {};

	memset(dlp, 0, sizeof(*dlp));
	memcpy(attrs.switch_id.id, &ocelot->base_mac, id_len);
	attrs.switch_id.id_len = id_len;
	attrs.phys.port_number = port;
	attrs.flavour = flavour;

	devlink_port_attrs_set(dlp, &attrs);

	return devlink_port_register(dl, dlp, port);
}

void ocelot_port_devlink_teardown(struct ocelot *ocelot, int port)
{
	struct devlink_port *dlp = &ocelot->devlink_ports[port];

	devlink_port_unregister(dlp);
}

int ocelot_setup_tc_cls_flower(struct ocelot_port_private *priv,
			       struct flow_cls_offload *f,
			       bool ingress)
{
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;

	if (!ingress)
		return -EOPNOTSUPP;

	switch (f->command) {
	case FLOW_CLS_REPLACE:
		return ocelot_cls_flower_replace(ocelot, port, f, ingress);
	case FLOW_CLS_DESTROY:
		return ocelot_cls_flower_destroy(ocelot, port, f, ingress);
	case FLOW_CLS_STATS:
		return ocelot_cls_flower_stats(ocelot, port, f, ingress);
	default:
		return -EOPNOTSUPP;
	}
}

static int ocelot_setup_tc_cls_matchall_police(struct ocelot_port_private *priv,
					       struct tc_cls_matchall_offload *f,
					       bool ingress,
					       struct netlink_ext_ack *extack)
{
	struct flow_action_entry *action = &f->rule->action.entries[0];
	struct ocelot *ocelot = priv->port.ocelot;
	struct ocelot_policer pol = { 0 };
	int port = priv->port.index;
	int err;

	if (!ingress) {
		NL_SET_ERR_MSG_MOD(extack, "Only ingress is supported");
		return -EOPNOTSUPP;
	}

	if (priv->tc.police_id && priv->tc.police_id != f->cookie) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only one policer per port is supported");
		return -EEXIST;
	}

	err = ocelot_policer_validate(&f->rule->action, action, extack);
	if (err)
		return err;

	pol.rate = (u32)div_u64(action->police.rate_bytes_ps, 1000) * 8;
	pol.burst = action->police.burst;

	err = ocelot_port_policer_add(ocelot, port, &pol);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Could not add policer");
		return err;
	}

	priv->tc.police_id = f->cookie;
	priv->tc.offload_cnt++;

	return 0;
}

static int ocelot_setup_tc_cls_matchall_mirred(struct ocelot_port_private *priv,
					       struct tc_cls_matchall_offload *f,
					       bool ingress,
					       struct netlink_ext_ack *extack)
{
	struct flow_action *action = &f->rule->action;
	struct ocelot *ocelot = priv->port.ocelot;
	struct ocelot_port_private *other_priv;
	const struct flow_action_entry *a;
	int err;

	if (f->common.protocol != htons(ETH_P_ALL))
		return -EOPNOTSUPP;

	if (!flow_action_basic_hw_stats_check(action, extack))
		return -EOPNOTSUPP;

	a = &action->entries[0];
	if (!a->dev)
		return -EINVAL;

	if (!ocelot_netdevice_dev_check(a->dev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Destination not an ocelot port");
		return -EOPNOTSUPP;
	}

	other_priv = netdev_priv(a->dev);

	err = ocelot_port_mirror_add(ocelot, priv->port.index,
				     other_priv->port.index, ingress, extack);
	if (err)
		return err;

	if (ingress)
		priv->tc.ingress_mirred_id = f->cookie;
	else
		priv->tc.egress_mirred_id = f->cookie;
	priv->tc.offload_cnt++;

	return 0;
}

static int ocelot_del_tc_cls_matchall_police(struct ocelot_port_private *priv,
					     struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;
	int err;

	err = ocelot_port_policer_del(ocelot, port);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Could not delete policer");
		return err;
	}

	priv->tc.police_id = 0;
	priv->tc.offload_cnt--;

	return 0;
}

static int ocelot_del_tc_cls_matchall_mirred(struct ocelot_port_private *priv,
					     bool ingress,
					     struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;

	ocelot_port_mirror_del(ocelot, port, ingress);

	if (ingress)
		priv->tc.ingress_mirred_id = 0;
	else
		priv->tc.egress_mirred_id = 0;
	priv->tc.offload_cnt--;

	return 0;
}

static int ocelot_setup_tc_cls_matchall(struct ocelot_port_private *priv,
					struct tc_cls_matchall_offload *f,
					bool ingress)
{
	struct netlink_ext_ack *extack = f->common.extack;
	struct flow_action_entry *action;

	switch (f->command) {
	case TC_CLSMATCHALL_REPLACE:
		if (!flow_offload_has_one_action(&f->rule->action)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Only one action is supported");
			return -EOPNOTSUPP;
		}

		if (priv->tc.block_shared) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Matchall offloads not supported on shared blocks");
			return -EOPNOTSUPP;
		}

		action = &f->rule->action.entries[0];

		switch (action->id) {
		case FLOW_ACTION_POLICE:
			return ocelot_setup_tc_cls_matchall_police(priv, f,
								   ingress,
								   extack);
			break;
		case FLOW_ACTION_MIRRED:
			return ocelot_setup_tc_cls_matchall_mirred(priv, f,
								   ingress,
								   extack);
		default:
			NL_SET_ERR_MSG_MOD(extack, "Unsupported action");
			return -EOPNOTSUPP;
		}

		break;
	case TC_CLSMATCHALL_DESTROY:
		action = &f->rule->action.entries[0];

		if (f->cookie == priv->tc.police_id)
			return ocelot_del_tc_cls_matchall_police(priv, extack);
		else if (f->cookie == priv->tc.ingress_mirred_id ||
			 f->cookie == priv->tc.egress_mirred_id)
			return ocelot_del_tc_cls_matchall_mirred(priv, ingress,
								 extack);
		else
			return -ENOENT;

		break;
	case TC_CLSMATCHALL_STATS:
	default:
		return -EOPNOTSUPP;
	}
}

static int ocelot_setup_tc_block_cb(enum tc_setup_type type,
				    void *type_data,
				    void *cb_priv, bool ingress)
{
	struct ocelot_port_private *priv = cb_priv;

	if (!tc_cls_can_offload_and_chain0(priv->dev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSMATCHALL:
		return ocelot_setup_tc_cls_matchall(priv, type_data, ingress);
	case TC_SETUP_CLSFLOWER:
		return ocelot_setup_tc_cls_flower(priv, type_data, ingress);
	default:
		return -EOPNOTSUPP;
	}
}

static int ocelot_setup_tc_block_cb_ig(enum tc_setup_type type,
				       void *type_data,
				       void *cb_priv)
{
	return ocelot_setup_tc_block_cb(type, type_data,
					cb_priv, true);
}

static int ocelot_setup_tc_block_cb_eg(enum tc_setup_type type,
				       void *type_data,
				       void *cb_priv)
{
	return ocelot_setup_tc_block_cb(type, type_data,
					cb_priv, false);
}

static LIST_HEAD(ocelot_block_cb_list);

static int ocelot_setup_tc_block(struct ocelot_port_private *priv,
				 struct flow_block_offload *f)
{
	struct flow_block_cb *block_cb;
	flow_setup_cb_t *cb;

	if (f->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS) {
		cb = ocelot_setup_tc_block_cb_ig;
		priv->tc.block_shared = f->block_shared;
	} else if (f->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS) {
		cb = ocelot_setup_tc_block_cb_eg;
	} else {
		return -EOPNOTSUPP;
	}

	f->driver_block_list = &ocelot_block_cb_list;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		if (flow_block_cb_is_busy(cb, priv, &ocelot_block_cb_list))
			return -EBUSY;

		block_cb = flow_block_cb_alloc(cb, priv, priv, NULL);
		if (IS_ERR(block_cb))
			return PTR_ERR(block_cb);

		flow_block_cb_add(block_cb, f);
		list_add_tail(&block_cb->driver_list, f->driver_block_list);
		return 0;
	case FLOW_BLOCK_UNBIND:
		block_cb = flow_block_cb_lookup(f->block, cb, priv);
		if (!block_cb)
			return -ENOENT;

		flow_block_cb_remove(block_cb, f);
		list_del(&block_cb->driver_list);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int ocelot_setup_tc(struct net_device *dev, enum tc_setup_type type,
			   void *type_data)
{
	struct ocelot_port_private *priv = netdev_priv(dev);

	switch (type) {
	case TC_SETUP_BLOCK:
		return ocelot_setup_tc_block(priv, type_data);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int ocelot_vlan_vid_add(struct net_device *dev, u16 vid, bool pvid,
			       bool untagged)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->port.index;
	int ret;

	ret = ocelot_vlan_add(ocelot, port, vid, pvid, untagged);
	if (ret)
		return ret;

	/* Add the port MAC address to with the right VLAN information */
	ocelot_mact_learn(ocelot, PGID_CPU, dev->dev_addr, vid,
			  ENTRYTYPE_LOCKED);

	return 0;
}

static int ocelot_vlan_vid_del(struct net_device *dev, u16 vid)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;
	int ret;

	/* 8021q removes VID 0 on module unload for all interfaces
	 * with VLAN filtering feature. We need to keep it to receive
	 * untagged traffic.
	 */
	if (vid == OCELOT_STANDALONE_PVID)
		return 0;

	ret = ocelot_vlan_del(ocelot, port, vid);
	if (ret)
		return ret;

	/* Del the port MAC address to with the right VLAN information */
	ocelot_mact_forget(ocelot, dev->dev_addr, vid);

	return 0;
}

static int ocelot_port_open(struct net_device *dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);

	phylink_start(priv->phylink);

	return 0;
}

static int ocelot_port_stop(struct net_device *dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);

	phylink_stop(priv->phylink);

	return 0;
}

static netdev_tx_t ocelot_port_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->port.index;
	u32 rew_op = 0;

	if (!static_branch_unlikely(&ocelot_fdma_enabled) &&
	    !ocelot_can_inject(ocelot, 0))
		return NETDEV_TX_BUSY;

	/* Check if timestamping is needed */
	if (ocelot->ptp && (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)) {
		struct sk_buff *clone = NULL;

		if (ocelot_port_txtstamp_request(ocelot, port, skb, &clone)) {
			kfree_skb(skb);
			return NETDEV_TX_OK;
		}

		if (clone)
			OCELOT_SKB_CB(skb)->clone = clone;

		rew_op = ocelot_ptp_rew_op(skb);
	}

	if (static_branch_unlikely(&ocelot_fdma_enabled)) {
		ocelot_fdma_inject_frame(ocelot, port, rew_op, skb, dev);
	} else {
		ocelot_port_inject_frame(ocelot, port, 0, rew_op, skb);

		consume_skb(skb);
	}

	return NETDEV_TX_OK;
}

enum ocelot_action_type {
	OCELOT_MACT_LEARN,
	OCELOT_MACT_FORGET,
};

struct ocelot_mact_work_ctx {
	struct work_struct work;
	struct ocelot *ocelot;
	enum ocelot_action_type type;
	union {
		/* OCELOT_MACT_LEARN */
		struct {
			unsigned char addr[ETH_ALEN];
			u16 vid;
			enum macaccess_entry_type entry_type;
			int pgid;
		} learn;
		/* OCELOT_MACT_FORGET */
		struct {
			unsigned char addr[ETH_ALEN];
			u16 vid;
		} forget;
	};
};

#define ocelot_work_to_ctx(x) \
	container_of((x), struct ocelot_mact_work_ctx, work)

static void ocelot_mact_work(struct work_struct *work)
{
	struct ocelot_mact_work_ctx *w = ocelot_work_to_ctx(work);
	struct ocelot *ocelot = w->ocelot;

	switch (w->type) {
	case OCELOT_MACT_LEARN:
		ocelot_mact_learn(ocelot, w->learn.pgid, w->learn.addr,
				  w->learn.vid, w->learn.entry_type);
		break;
	case OCELOT_MACT_FORGET:
		ocelot_mact_forget(ocelot, w->forget.addr, w->forget.vid);
		break;
	default:
		break;
	}

	kfree(w);
}

static int ocelot_enqueue_mact_action(struct ocelot *ocelot,
				      const struct ocelot_mact_work_ctx *ctx)
{
	struct ocelot_mact_work_ctx *w = kmemdup(ctx, sizeof(*w), GFP_ATOMIC);

	if (!w)
		return -ENOMEM;

	w->ocelot = ocelot;
	INIT_WORK(&w->work, ocelot_mact_work);
	queue_work(ocelot->owq, &w->work);

	return 0;
}

static int ocelot_mc_unsync(struct net_device *dev, const unsigned char *addr)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	struct ocelot_mact_work_ctx w;

	ether_addr_copy(w.forget.addr, addr);
	w.forget.vid = OCELOT_STANDALONE_PVID;
	w.type = OCELOT_MACT_FORGET;

	return ocelot_enqueue_mact_action(ocelot, &w);
}

static int ocelot_mc_sync(struct net_device *dev, const unsigned char *addr)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	struct ocelot_mact_work_ctx w;

	ether_addr_copy(w.learn.addr, addr);
	w.learn.vid = OCELOT_STANDALONE_PVID;
	w.learn.pgid = PGID_CPU;
	w.learn.entry_type = ENTRYTYPE_LOCKED;
	w.type = OCELOT_MACT_LEARN;

	return ocelot_enqueue_mact_action(ocelot, &w);
}

static void ocelot_set_rx_mode(struct net_device *dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	u32 val;
	int i;

	/* This doesn't handle promiscuous mode because the bridge core is
	 * setting IFF_PROMISC on all slave interfaces and all frames would be
	 * forwarded to the CPU port.
	 */
	val = GENMASK(ocelot->num_phys_ports - 1, 0);
	for_each_nonreserved_multicast_dest_pgid(ocelot, i)
		ocelot_write_rix(ocelot, val, ANA_PGID_PGID, i);

	__dev_mc_sync(dev, ocelot_mc_sync, ocelot_mc_unsync);
}

static int ocelot_port_set_mac_address(struct net_device *dev, void *p)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	const struct sockaddr *addr = p;

	/* Learn the new net device MAC address in the mac table. */
	ocelot_mact_learn(ocelot, PGID_CPU, addr->sa_data,
			  OCELOT_STANDALONE_PVID, ENTRYTYPE_LOCKED);
	/* Then forget the previous one. */
	ocelot_mact_forget(ocelot, dev->dev_addr, OCELOT_STANDALONE_PVID);

	eth_hw_addr_set(dev, addr->sa_data);
	return 0;
}

static void ocelot_get_stats64(struct net_device *dev,
			       struct rtnl_link_stats64 *stats)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;

	return ocelot_port_get_stats64(ocelot, port, stats);
}

static int ocelot_port_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
			       struct net_device *dev,
			       const unsigned char *addr,
			       u16 vid, u16 flags, bool *notified,
			       struct netlink_ext_ack *extack)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->port.index;

	return ocelot_fdb_add(ocelot, port, addr, vid, ocelot_port->bridge);
}

static int ocelot_port_fdb_del(struct ndmsg *ndm, struct nlattr *tb[],
			       struct net_device *dev,
			       const unsigned char *addr, u16 vid,
			       bool *notified, struct netlink_ext_ack *extack)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->port.index;

	return ocelot_fdb_del(ocelot, port, addr, vid, ocelot_port->bridge);
}

static int ocelot_port_fdb_do_dump(const unsigned char *addr, u16 vid,
				   bool is_static, void *data)
{
	struct ocelot_dump_ctx *dump = data;
	struct ndo_fdb_dump_context *ctx = (void *)dump->cb->ctx;
	u32 portid = NETLINK_CB(dump->cb->skb).portid;
	u32 seq = dump->cb->nlh->nlmsg_seq;
	struct nlmsghdr *nlh;
	struct ndmsg *ndm;

	if (dump->idx < ctx->fdb_idx)
		goto skip;

	nlh = nlmsg_put(dump->skb, portid, seq, RTM_NEWNEIGH,
			sizeof(*ndm), NLM_F_MULTI);
	if (!nlh)
		return -EMSGSIZE;

	ndm = nlmsg_data(nlh);
	ndm->ndm_family  = AF_BRIDGE;
	ndm->ndm_pad1    = 0;
	ndm->ndm_pad2    = 0;
	ndm->ndm_flags   = NTF_SELF;
	ndm->ndm_type    = 0;
	ndm->ndm_ifindex = dump->dev->ifindex;
	ndm->ndm_state   = is_static ? NUD_NOARP : NUD_REACHABLE;

	if (nla_put(dump->skb, NDA_LLADDR, ETH_ALEN, addr))
		goto nla_put_failure;

	if (vid && nla_put_u16(dump->skb, NDA_VLAN, vid))
		goto nla_put_failure;

	nlmsg_end(dump->skb, nlh);

skip:
	dump->idx++;
	return 0;

nla_put_failure:
	nlmsg_cancel(dump->skb, nlh);
	return -EMSGSIZE;
}

static int ocelot_port_fdb_dump(struct sk_buff *skb,
				struct netlink_callback *cb,
				struct net_device *dev,
				struct net_device *filter_dev, int *idx)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	struct ocelot_dump_ctx dump = {
		.dev = dev,
		.skb = skb,
		.cb = cb,
		.idx = *idx,
	};
	int port = priv->port.index;
	int ret;

	ret = ocelot_fdb_dump(ocelot, port, ocelot_port_fdb_do_dump, &dump);

	*idx = dump.idx;

	return ret;
}

static int ocelot_vlan_rx_add_vid(struct net_device *dev, __be16 proto,
				  u16 vid)
{
	return ocelot_vlan_vid_add(dev, vid, false, false);
}

static int ocelot_vlan_rx_kill_vid(struct net_device *dev, __be16 proto,
				   u16 vid)
{
	return ocelot_vlan_vid_del(dev, vid);
}

static void ocelot_vlan_mode(struct ocelot *ocelot, int port,
			     netdev_features_t features)
{
	u32 val;

	/* Filtering */
	val = ocelot_read(ocelot, ANA_VLANMASK);
	if (features & NETIF_F_HW_VLAN_CTAG_FILTER)
		val |= BIT(port);
	else
		val &= ~BIT(port);
	ocelot_write(ocelot, val, ANA_VLANMASK);
}

static int ocelot_set_features(struct net_device *dev,
			       netdev_features_t features)
{
	netdev_features_t changed = dev->features ^ features;
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;

	if ((dev->features & NETIF_F_HW_TC) > (features & NETIF_F_HW_TC) &&
	    priv->tc.offload_cnt) {
		netdev_err(dev,
			   "Cannot disable HW TC offload while offloads active\n");
		return -EBUSY;
	}

	if (changed & NETIF_F_HW_VLAN_CTAG_FILTER)
		ocelot_vlan_mode(ocelot, port, features);

	return 0;
}

static int ocelot_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;

	/* If the attached PHY device isn't capable of timestamping operations,
	 * use our own (when possible).
	 */
	if (!phy_has_hwtstamp(dev->phydev) && ocelot->ptp) {
		switch (cmd) {
		case SIOCSHWTSTAMP:
			return ocelot_hwstamp_set(ocelot, port, ifr);
		case SIOCGHWTSTAMP:
			return ocelot_hwstamp_get(ocelot, port, ifr);
		}
	}

	return phy_mii_ioctl(dev->phydev, ifr, cmd);
}

static int ocelot_change_mtu(struct net_device *dev, int new_mtu)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;

	ocelot_port_set_maxlen(ocelot, priv->port.index, new_mtu);
	WRITE_ONCE(dev->mtu, new_mtu);

	return 0;
}

static const struct net_device_ops ocelot_port_netdev_ops = {
	.ndo_open			= ocelot_port_open,
	.ndo_stop			= ocelot_port_stop,
	.ndo_start_xmit			= ocelot_port_xmit,
	.ndo_change_mtu			= ocelot_change_mtu,
	.ndo_set_rx_mode		= ocelot_set_rx_mode,
	.ndo_set_mac_address		= ocelot_port_set_mac_address,
	.ndo_get_stats64		= ocelot_get_stats64,
	.ndo_fdb_add			= ocelot_port_fdb_add,
	.ndo_fdb_del			= ocelot_port_fdb_del,
	.ndo_fdb_dump			= ocelot_port_fdb_dump,
	.ndo_vlan_rx_add_vid		= ocelot_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid		= ocelot_vlan_rx_kill_vid,
	.ndo_set_features		= ocelot_set_features,
	.ndo_setup_tc			= ocelot_setup_tc,
	.ndo_eth_ioctl			= ocelot_ioctl,
};

struct net_device *ocelot_port_to_netdev(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct ocelot_port_private *priv;

	if (!ocelot_port)
		return NULL;

	priv = container_of(ocelot_port, struct ocelot_port_private, port);

	return priv->dev;
}

/* Checks if the net_device instance given to us originates from our driver */
static bool ocelot_netdevice_dev_check(const struct net_device *dev)
{
	return dev->netdev_ops == &ocelot_port_netdev_ops;
}

int ocelot_netdev_to_port(struct net_device *dev)
{
	struct ocelot_port_private *priv;

	if (!dev || !ocelot_netdevice_dev_check(dev))
		return -EINVAL;

	priv = netdev_priv(dev);

	return priv->port.index;
}

static void ocelot_port_get_strings(struct net_device *netdev, u32 sset,
				    u8 *data)
{
	struct ocelot_port_private *priv = netdev_priv(netdev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;

	ocelot_get_strings(ocelot, port, sset, data);
}

static void ocelot_port_get_ethtool_stats(struct net_device *dev,
					  struct ethtool_stats *stats,
					  u64 *data)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;

	ocelot_get_ethtool_stats(ocelot, port, data);
}

static int ocelot_port_get_sset_count(struct net_device *dev, int sset)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;

	return ocelot_get_sset_count(ocelot, port, sset);
}

static int ocelot_port_get_ts_info(struct net_device *dev,
				   struct kernel_ethtool_ts_info *info)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;

	if (!ocelot->ptp)
		return ethtool_op_get_ts_info(dev, info);

	return ocelot_get_ts_info(ocelot, port, info);
}

static const struct ethtool_ops ocelot_ethtool_ops = {
	.get_strings		= ocelot_port_get_strings,
	.get_ethtool_stats	= ocelot_port_get_ethtool_stats,
	.get_sset_count		= ocelot_port_get_sset_count,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.get_ts_info		= ocelot_port_get_ts_info,
};

static void ocelot_port_attr_stp_state_set(struct ocelot *ocelot, int port,
					   u8 state)
{
	ocelot_bridge_stp_state_set(ocelot, port, state);
}

static void ocelot_port_attr_ageing_set(struct ocelot *ocelot, int port,
					unsigned long ageing_clock_t)
{
	unsigned long ageing_jiffies = clock_t_to_jiffies(ageing_clock_t);
	u32 ageing_time = jiffies_to_msecs(ageing_jiffies);

	ocelot_set_ageing_time(ocelot, ageing_time);
}

static void ocelot_port_attr_mc_set(struct ocelot *ocelot, int port, bool mc)
{
	u32 cpu_fwd_mcast = ANA_PORT_CPU_FWD_CFG_CPU_IGMP_REDIR_ENA |
			    ANA_PORT_CPU_FWD_CFG_CPU_MLD_REDIR_ENA |
			    ANA_PORT_CPU_FWD_CFG_CPU_IPMC_CTRL_COPY_ENA;
	u32 val = 0;

	if (mc)
		val = cpu_fwd_mcast;

	ocelot_rmw_gix(ocelot, val, cpu_fwd_mcast,
		       ANA_PORT_CPU_FWD_CFG, port);
}

static int ocelot_port_attr_set(struct net_device *dev, const void *ctx,
				const struct switchdev_attr *attr,
				struct netlink_ext_ack *extack)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;
	int err = 0;

	if (ctx && ctx != priv)
		return 0;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		ocelot_port_attr_stp_state_set(ocelot, port, attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		ocelot_port_attr_ageing_set(ocelot, port, attr->u.ageing_time);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		ocelot_port_vlan_filtering(ocelot, port, attr->u.vlan_filtering,
					   extack);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_MC_DISABLED:
		ocelot_port_attr_mc_set(ocelot, port, !attr->u.mc_disabled);
		break;
	case SWITCHDEV_ATTR_ID_PORT_PRE_BRIDGE_FLAGS:
		err = ocelot_port_pre_bridge_flags(ocelot, port,
						   attr->u.brport_flags);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		ocelot_port_bridge_flags(ocelot, port, attr->u.brport_flags);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int ocelot_vlan_vid_prepare(struct net_device *dev, u16 vid, bool pvid,
				   bool untagged, struct netlink_ext_ack *extack)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->port.index;

	return ocelot_vlan_prepare(ocelot, port, vid, pvid, untagged, extack);
}

static int ocelot_port_obj_add_vlan(struct net_device *dev,
				    const struct switchdev_obj_port_vlan *vlan,
				    struct netlink_ext_ack *extack)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	int ret;

	ret = ocelot_vlan_vid_prepare(dev, vlan->vid, pvid, untagged, extack);
	if (ret)
		return ret;

	return ocelot_vlan_vid_add(dev, vlan->vid, pvid, untagged);
}

static int ocelot_port_obj_add_mdb(struct net_device *dev,
				   const struct switchdev_obj_port_mdb *mdb)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->port.index;

	return ocelot_port_mdb_add(ocelot, port, mdb, ocelot_port->bridge);
}

static int ocelot_port_obj_del_mdb(struct net_device *dev,
				   const struct switchdev_obj_port_mdb *mdb)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->port.index;

	return ocelot_port_mdb_del(ocelot, port, mdb, ocelot_port->bridge);
}

static int ocelot_port_obj_mrp_add(struct net_device *dev,
				   const struct switchdev_obj_mrp *mrp)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->port.index;

	return ocelot_mrp_add(ocelot, port, mrp);
}

static int ocelot_port_obj_mrp_del(struct net_device *dev,
				   const struct switchdev_obj_mrp *mrp)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->port.index;

	return ocelot_mrp_del(ocelot, port, mrp);
}

static int
ocelot_port_obj_mrp_add_ring_role(struct net_device *dev,
				  const struct switchdev_obj_ring_role_mrp *mrp)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->port.index;

	return ocelot_mrp_add_ring_role(ocelot, port, mrp);
}

static int
ocelot_port_obj_mrp_del_ring_role(struct net_device *dev,
				  const struct switchdev_obj_ring_role_mrp *mrp)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->port.index;

	return ocelot_mrp_del_ring_role(ocelot, port, mrp);
}

static int ocelot_port_obj_add(struct net_device *dev, const void *ctx,
			       const struct switchdev_obj *obj,
			       struct netlink_ext_ack *extack)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	int ret = 0;

	if (ctx && ctx != priv)
		return 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		ret = ocelot_port_obj_add_vlan(dev,
					       SWITCHDEV_OBJ_PORT_VLAN(obj),
					       extack);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		ret = ocelot_port_obj_add_mdb(dev, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	case SWITCHDEV_OBJ_ID_MRP:
		ret = ocelot_port_obj_mrp_add(dev, SWITCHDEV_OBJ_MRP(obj));
		break;
	case SWITCHDEV_OBJ_ID_RING_ROLE_MRP:
		ret = ocelot_port_obj_mrp_add_ring_role(dev,
							SWITCHDEV_OBJ_RING_ROLE_MRP(obj));
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

static int ocelot_port_obj_del(struct net_device *dev, const void *ctx,
			       const struct switchdev_obj *obj)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	int ret = 0;

	if (ctx && ctx != priv)
		return 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		ret = ocelot_vlan_vid_del(dev,
					  SWITCHDEV_OBJ_PORT_VLAN(obj)->vid);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		ret = ocelot_port_obj_del_mdb(dev, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	case SWITCHDEV_OBJ_ID_MRP:
		ret = ocelot_port_obj_mrp_del(dev, SWITCHDEV_OBJ_MRP(obj));
		break;
	case SWITCHDEV_OBJ_ID_RING_ROLE_MRP:
		ret = ocelot_port_obj_mrp_del_ring_role(dev,
							SWITCHDEV_OBJ_RING_ROLE_MRP(obj));
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

static void ocelot_inherit_brport_flags(struct ocelot *ocelot, int port,
					struct net_device *brport_dev)
{
	struct switchdev_brport_flags flags = {0};
	int flag;

	flags.mask = BR_LEARNING | BR_FLOOD | BR_MCAST_FLOOD | BR_BCAST_FLOOD;

	for_each_set_bit(flag, &flags.mask, 32)
		if (br_port_flag_is_set(brport_dev, BIT(flag)))
			flags.val |= BIT(flag);

	ocelot_port_bridge_flags(ocelot, port, flags);
}

static void ocelot_clear_brport_flags(struct ocelot *ocelot, int port)
{
	struct switchdev_brport_flags flags;

	flags.mask = BR_LEARNING | BR_FLOOD | BR_MCAST_FLOOD | BR_BCAST_FLOOD;
	flags.val = flags.mask & ~BR_LEARNING;

	ocelot_port_bridge_flags(ocelot, port, flags);
}

static int ocelot_switchdev_sync(struct ocelot *ocelot, int port,
				 struct net_device *brport_dev,
				 struct net_device *bridge_dev,
				 struct netlink_ext_ack *extack)
{
	clock_t ageing_time;
	u8 stp_state;

	ocelot_inherit_brport_flags(ocelot, port, brport_dev);

	stp_state = br_port_get_stp_state(brport_dev);
	ocelot_bridge_stp_state_set(ocelot, port, stp_state);

	ageing_time = br_get_ageing_time(bridge_dev);
	ocelot_port_attr_ageing_set(ocelot, port, ageing_time);

	return ocelot_port_vlan_filtering(ocelot, port,
					  br_vlan_enabled(bridge_dev),
					  extack);
}

static int ocelot_switchdev_unsync(struct ocelot *ocelot, int port)
{
	int err;

	err = ocelot_port_vlan_filtering(ocelot, port, false, NULL);
	if (err)
		return err;

	ocelot_clear_brport_flags(ocelot, port);

	ocelot_bridge_stp_state_set(ocelot, port, BR_STATE_FORWARDING);

	return 0;
}

static int ocelot_bridge_num_get(struct ocelot *ocelot,
				 const struct net_device *bridge_dev)
{
	int bridge_num = ocelot_bridge_num_find(ocelot, bridge_dev);

	if (bridge_num < 0) {
		/* First port that offloads this bridge */
		bridge_num = find_first_zero_bit(&ocelot->bridges,
						 ocelot->num_phys_ports);

		set_bit(bridge_num, &ocelot->bridges);
	}

	return bridge_num;
}

static void ocelot_bridge_num_put(struct ocelot *ocelot,
				  const struct net_device *bridge_dev,
				  int bridge_num)
{
	/* Check if the bridge is still in use, otherwise it is time
	 * to clean it up so we can reuse this bridge_num later.
	 */
	if (!ocelot_bridge_num_find(ocelot, bridge_dev))
		clear_bit(bridge_num, &ocelot->bridges);
}

static int ocelot_netdevice_bridge_join(struct net_device *dev,
					struct net_device *brport_dev,
					struct net_device *bridge,
					struct netlink_ext_ack *extack)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->port.index;
	int bridge_num, err;

	bridge_num = ocelot_bridge_num_get(ocelot, bridge);

	err = ocelot_port_bridge_join(ocelot, port, bridge, bridge_num,
				      extack);
	if (err)
		goto err_join;

	err = switchdev_bridge_port_offload(brport_dev, dev, priv,
					    &ocelot_switchdev_nb,
					    &ocelot_switchdev_blocking_nb,
					    false, extack);
	if (err)
		goto err_switchdev_offload;

	err = ocelot_switchdev_sync(ocelot, port, brport_dev, bridge, extack);
	if (err)
		goto err_switchdev_sync;

	return 0;

err_switchdev_sync:
	switchdev_bridge_port_unoffload(brport_dev, priv,
					&ocelot_switchdev_nb,
					&ocelot_switchdev_blocking_nb);
err_switchdev_offload:
	ocelot_port_bridge_leave(ocelot, port, bridge);
err_join:
	ocelot_bridge_num_put(ocelot, bridge, bridge_num);
	return err;
}

static void ocelot_netdevice_pre_bridge_leave(struct net_device *dev,
					      struct net_device *brport_dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);

	switchdev_bridge_port_unoffload(brport_dev, priv,
					&ocelot_switchdev_nb,
					&ocelot_switchdev_blocking_nb);
}

static int ocelot_netdevice_bridge_leave(struct net_device *dev,
					 struct net_device *brport_dev,
					 struct net_device *bridge)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int bridge_num = ocelot_port->bridge_num;
	int port = priv->port.index;
	int err;

	err = ocelot_switchdev_unsync(ocelot, port);
	if (err)
		return err;

	ocelot_port_bridge_leave(ocelot, port, bridge);
	ocelot_bridge_num_put(ocelot, bridge, bridge_num);

	return 0;
}

static int ocelot_netdevice_lag_join(struct net_device *dev,
				     struct net_device *bond,
				     struct netdev_lag_upper_info *info,
				     struct netlink_ext_ack *extack)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	struct net_device *bridge_dev;
	int port = priv->port.index;
	int err;

	err = ocelot_port_lag_join(ocelot, port, bond, info, extack);
	if (err == -EOPNOTSUPP)
		/* Offloading not supported, fall back to software LAG */
		return 0;

	bridge_dev = netdev_master_upper_dev_get(bond);
	if (!bridge_dev || !netif_is_bridge_master(bridge_dev))
		return 0;

	err = ocelot_netdevice_bridge_join(dev, bond, bridge_dev, extack);
	if (err)
		goto err_bridge_join;

	return 0;

err_bridge_join:
	ocelot_port_lag_leave(ocelot, port, bond);
	return err;
}

static void ocelot_netdevice_pre_lag_leave(struct net_device *dev,
					   struct net_device *bond)
{
	struct net_device *bridge_dev;

	bridge_dev = netdev_master_upper_dev_get(bond);
	if (!bridge_dev || !netif_is_bridge_master(bridge_dev))
		return;

	ocelot_netdevice_pre_bridge_leave(dev, bond);
}

static int ocelot_netdevice_lag_leave(struct net_device *dev,
				      struct net_device *bond)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	struct net_device *bridge_dev;
	int port = priv->port.index;

	ocelot_port_lag_leave(ocelot, port, bond);

	bridge_dev = netdev_master_upper_dev_get(bond);
	if (!bridge_dev || !netif_is_bridge_master(bridge_dev))
		return 0;

	return ocelot_netdevice_bridge_leave(dev, bond, bridge_dev);
}

static int ocelot_netdevice_changeupper(struct net_device *dev,
					struct net_device *brport_dev,
					struct netdev_notifier_changeupper_info *info)
{
	struct netlink_ext_ack *extack;
	int err = 0;

	extack = netdev_notifier_info_to_extack(&info->info);

	if (netif_is_bridge_master(info->upper_dev)) {
		if (info->linking)
			err = ocelot_netdevice_bridge_join(dev, brport_dev,
							   info->upper_dev,
							   extack);
		else
			err = ocelot_netdevice_bridge_leave(dev, brport_dev,
							    info->upper_dev);
	}
	if (netif_is_lag_master(info->upper_dev)) {
		if (info->linking)
			err = ocelot_netdevice_lag_join(dev, info->upper_dev,
							info->upper_info, extack);
		else
			ocelot_netdevice_lag_leave(dev, info->upper_dev);
	}

	return notifier_from_errno(err);
}

/* Treat CHANGEUPPER events on an offloaded LAG as individual CHANGEUPPER
 * events for the lower physical ports of the LAG.
 * If the LAG upper isn't offloaded, ignore its CHANGEUPPER events.
 * In case the LAG joined a bridge, notify that we are offloading it and can do
 * forwarding in hardware towards it.
 */
static int
ocelot_netdevice_lag_changeupper(struct net_device *dev,
				 struct netdev_notifier_changeupper_info *info)
{
	struct net_device *lower;
	struct list_head *iter;
	int err = NOTIFY_DONE;

	netdev_for_each_lower_dev(dev, lower, iter) {
		struct ocelot_port_private *priv = netdev_priv(lower);
		struct ocelot_port *ocelot_port = &priv->port;

		if (ocelot_port->bond != dev)
			return NOTIFY_OK;

		err = ocelot_netdevice_changeupper(lower, dev, info);
		if (err)
			return notifier_from_errno(err);
	}

	return NOTIFY_DONE;
}

static int
ocelot_netdevice_prechangeupper(struct net_device *dev,
				struct net_device *brport_dev,
				struct netdev_notifier_changeupper_info *info)
{
	if (netif_is_bridge_master(info->upper_dev) && !info->linking)
		ocelot_netdevice_pre_bridge_leave(dev, brport_dev);

	if (netif_is_lag_master(info->upper_dev) && !info->linking)
		ocelot_netdevice_pre_lag_leave(dev, info->upper_dev);

	return NOTIFY_DONE;
}

static int
ocelot_netdevice_lag_prechangeupper(struct net_device *dev,
				    struct netdev_notifier_changeupper_info *info)
{
	struct net_device *lower;
	struct list_head *iter;
	int err = NOTIFY_DONE;

	netdev_for_each_lower_dev(dev, lower, iter) {
		struct ocelot_port_private *priv = netdev_priv(lower);
		struct ocelot_port *ocelot_port = &priv->port;

		if (ocelot_port->bond != dev)
			return NOTIFY_OK;

		err = ocelot_netdevice_prechangeupper(dev, lower, info);
		if (err)
			return err;
	}

	return NOTIFY_DONE;
}

static int
ocelot_netdevice_changelowerstate(struct net_device *dev,
				  struct netdev_lag_lower_state_info *info)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	bool is_active = info->link_up && info->tx_enabled;
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->port.index;

	if (!ocelot_port->bond)
		return NOTIFY_DONE;

	if (ocelot_port->lag_tx_active == is_active)
		return NOTIFY_DONE;

	ocelot_port_lag_change(ocelot, port, is_active);

	return NOTIFY_OK;
}

static int ocelot_netdevice_event(struct notifier_block *unused,
				  unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_PRECHANGEUPPER: {
		struct netdev_notifier_changeupper_info *info = ptr;

		if (ocelot_netdevice_dev_check(dev))
			return ocelot_netdevice_prechangeupper(dev, dev, info);

		if (netif_is_lag_master(dev))
			return ocelot_netdevice_lag_prechangeupper(dev, info);

		break;
	}
	case NETDEV_CHANGEUPPER: {
		struct netdev_notifier_changeupper_info *info = ptr;

		if (ocelot_netdevice_dev_check(dev))
			return ocelot_netdevice_changeupper(dev, dev, info);

		if (netif_is_lag_master(dev))
			return ocelot_netdevice_lag_changeupper(dev, info);

		break;
	}
	case NETDEV_CHANGELOWERSTATE: {
		struct netdev_notifier_changelowerstate_info *info = ptr;

		if (!ocelot_netdevice_dev_check(dev))
			break;

		return ocelot_netdevice_changelowerstate(dev,
							 info->lower_state_info);
	}
	default:
		break;
	}

	return NOTIFY_DONE;
}

struct notifier_block ocelot_netdevice_nb __read_mostly = {
	.notifier_call = ocelot_netdevice_event,
};

static int ocelot_switchdev_event(struct notifier_block *unused,
				  unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     ocelot_netdevice_dev_check,
						     ocelot_port_attr_set);
		return notifier_from_errno(err);
	}

	return NOTIFY_DONE;
}

struct notifier_block ocelot_switchdev_nb __read_mostly = {
	.notifier_call = ocelot_switchdev_event,
};

static int ocelot_switchdev_blocking_event(struct notifier_block *unused,
					   unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
		/* Blocking events. */
	case SWITCHDEV_PORT_OBJ_ADD:
		err = switchdev_handle_port_obj_add(dev, ptr,
						    ocelot_netdevice_dev_check,
						    ocelot_port_obj_add);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_OBJ_DEL:
		err = switchdev_handle_port_obj_del(dev, ptr,
						    ocelot_netdevice_dev_check,
						    ocelot_port_obj_del);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     ocelot_netdevice_dev_check,
						     ocelot_port_attr_set);
		return notifier_from_errno(err);
	}

	return NOTIFY_DONE;
}

struct notifier_block ocelot_switchdev_blocking_nb __read_mostly = {
	.notifier_call = ocelot_switchdev_blocking_event,
};

static void vsc7514_phylink_mac_config(struct phylink_config *config,
				       unsigned int link_an_mode,
				       const struct phylink_link_state *state)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct ocelot_port_private *priv = netdev_priv(ndev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;

	ocelot_phylink_mac_config(ocelot, port, link_an_mode, state);
}

static void vsc7514_phylink_mac_link_down(struct phylink_config *config,
					  unsigned int link_an_mode,
					  phy_interface_t interface)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct ocelot_port_private *priv = netdev_priv(ndev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;

	ocelot_phylink_mac_link_down(ocelot, port, link_an_mode, interface,
				     OCELOT_MAC_QUIRKS);
}

static void vsc7514_phylink_mac_link_up(struct phylink_config *config,
					struct phy_device *phydev,
					unsigned int link_an_mode,
					phy_interface_t interface,
					int speed, int duplex,
					bool tx_pause, bool rx_pause)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct ocelot_port_private *priv = netdev_priv(ndev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->port.index;

	ocelot_phylink_mac_link_up(ocelot, port, phydev, link_an_mode,
				   interface, speed, duplex,
				   tx_pause, rx_pause, OCELOT_MAC_QUIRKS);
}

static const struct phylink_mac_ops ocelot_phylink_ops = {
	.mac_config		= vsc7514_phylink_mac_config,
	.mac_link_down		= vsc7514_phylink_mac_link_down,
	.mac_link_up		= vsc7514_phylink_mac_link_up,
};

static int ocelot_port_phylink_create(struct ocelot *ocelot, int port,
				      struct device_node *portnp)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct ocelot_port_private *priv;
	struct device *dev = ocelot->dev;
	phy_interface_t phy_mode;
	struct phylink *phylink;
	int err;

	of_get_phy_mode(portnp, &phy_mode);
	/* DT bindings of internal PHY ports are broken and don't
	 * specify a phy-mode
	 */
	if (phy_mode == PHY_INTERFACE_MODE_NA)
		phy_mode = PHY_INTERFACE_MODE_INTERNAL;

	if (phy_mode != PHY_INTERFACE_MODE_SGMII &&
	    phy_mode != PHY_INTERFACE_MODE_QSGMII &&
	    phy_mode != PHY_INTERFACE_MODE_INTERNAL) {
		dev_err(dev, "unsupported phy mode %s for port %d\n",
			phy_modes(phy_mode), port);
		return -EINVAL;
	}

	ocelot_port->phy_mode = phy_mode;

	err = ocelot_port_configure_serdes(ocelot, port, portnp);
	if (err)
		return err;

	priv = container_of(ocelot_port, struct ocelot_port_private, port);

	priv->phylink_config.dev = &priv->dev->dev;
	priv->phylink_config.type = PHYLINK_NETDEV;
	priv->phylink_config.mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
		MAC_10 | MAC_100 | MAC_1000FD | MAC_2500FD;

	__set_bit(ocelot_port->phy_mode,
		  priv->phylink_config.supported_interfaces);

	phylink = phylink_create(&priv->phylink_config,
				 of_fwnode_handle(portnp),
				 phy_mode, &ocelot_phylink_ops);
	if (IS_ERR(phylink)) {
		err = PTR_ERR(phylink);
		dev_err(dev, "Could not create phylink (%pe)\n", phylink);
		return err;
	}

	priv->phylink = phylink;

	err = phylink_of_phy_connect(phylink, portnp, 0);
	if (err) {
		dev_err(dev, "Could not connect to PHY: %pe\n", ERR_PTR(err));
		phylink_destroy(phylink);
		priv->phylink = NULL;
		return err;
	}

	return 0;
}

int ocelot_probe_port(struct ocelot *ocelot, int port, struct regmap *target,
		      struct device_node *portnp)
{
	struct ocelot_port_private *priv;
	struct ocelot_port *ocelot_port;
	struct net_device *dev;
	int err;

	dev = alloc_etherdev(sizeof(struct ocelot_port_private));
	if (!dev)
		return -ENOMEM;
	SET_NETDEV_DEV(dev, ocelot->dev);
	priv = netdev_priv(dev);
	priv->dev = dev;
	ocelot_port = &priv->port;
	ocelot_port->ocelot = ocelot;
	ocelot_port->index = port;
	ocelot_port->target = target;
	ocelot->ports[port] = ocelot_port;

	dev->netdev_ops = &ocelot_port_netdev_ops;
	dev->ethtool_ops = &ocelot_ethtool_ops;
	dev->max_mtu = OCELOT_JUMBO_MTU;

	dev->hw_features |= NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_RXFCS |
		NETIF_F_HW_TC;
	dev->features |= NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_TC;

	err = of_get_ethdev_address(portnp, dev);
	if (err)
		eth_hw_addr_gen(dev, ocelot->base_mac, port);

	ocelot_mact_learn(ocelot, PGID_CPU, dev->dev_addr,
			  OCELOT_STANDALONE_PVID, ENTRYTYPE_LOCKED);

	ocelot_init_port(ocelot, port);

	err = ocelot_port_phylink_create(ocelot, port, portnp);
	if (err)
		goto out;

	if (ocelot->fdma)
		ocelot_fdma_netdev_init(ocelot, dev);

	SET_NETDEV_DEVLINK_PORT(dev, &ocelot->devlink_ports[port]);
	err = register_netdev(dev);
	if (err) {
		dev_err(ocelot->dev, "register_netdev failed\n");
		goto out_fdma_deinit;
	}

	return 0;

out_fdma_deinit:
	if (ocelot->fdma)
		ocelot_fdma_netdev_deinit(ocelot, dev);
out:
	ocelot->ports[port] = NULL;
	free_netdev(dev);

	return err;
}

void ocelot_release_port(struct ocelot_port *ocelot_port)
{
	struct ocelot_port_private *priv = container_of(ocelot_port,
						struct ocelot_port_private,
						port);
	struct ocelot *ocelot = ocelot_port->ocelot;
	struct ocelot_fdma *fdma = ocelot->fdma;

	unregister_netdev(priv->dev);

	if (fdma)
		ocelot_fdma_netdev_deinit(ocelot, priv->dev);

	if (priv->phylink) {
		rtnl_lock();
		phylink_disconnect_phy(priv->phylink);
		rtnl_unlock();

		phylink_destroy(priv->phylink);
	}

	free_netdev(priv->dev);
}
