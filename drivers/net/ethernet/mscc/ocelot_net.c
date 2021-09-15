// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot Switch driver
 *
 * This contains glue logic between the switchdev driver operations and the
 * mscc_ocelot_switch_lib.
 *
 * Copyright (c) 2017, 2019 Microsemi Corporation
 * Copyright 2020-2021 NXP Semiconductors
 */

#include <linux/if_bridge.h>
#include <linux/of_net.h>
#include <linux/phy/phy.h>
#include <net/pkt_cls.h>
#include "ocelot.h"
#include "ocelot_vcap.h"

#define OCELOT_MAC_QUIRKS	OCELOT_QUIRK_QSGMII_PORTS_MUST_BE_UP

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

static struct devlink_port *ocelot_get_devlink_port(struct net_device *dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	return &ocelot->devlink_ports[port];
}

int ocelot_setup_tc_cls_flower(struct ocelot_port_private *priv,
			       struct flow_cls_offload *f,
			       bool ingress)
{
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

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

static int ocelot_setup_tc_cls_matchall(struct ocelot_port_private *priv,
					struct tc_cls_matchall_offload *f,
					bool ingress)
{
	struct netlink_ext_ack *extack = f->common.extack;
	struct ocelot *ocelot = priv->port.ocelot;
	struct ocelot_policer pol = { 0 };
	struct flow_action_entry *action;
	int port = priv->chip_port;
	int err;

	if (!ingress) {
		NL_SET_ERR_MSG_MOD(extack, "Only ingress is supported");
		return -EOPNOTSUPP;
	}

	switch (f->command) {
	case TC_CLSMATCHALL_REPLACE:
		if (!flow_offload_has_one_action(&f->rule->action)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Only one action is supported");
			return -EOPNOTSUPP;
		}

		if (priv->tc.block_shared) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Rate limit is not supported on shared blocks");
			return -EOPNOTSUPP;
		}

		action = &f->rule->action.entries[0];

		if (action->id != FLOW_ACTION_POLICE) {
			NL_SET_ERR_MSG_MOD(extack, "Unsupported action");
			return -EOPNOTSUPP;
		}

		if (priv->tc.police_id && priv->tc.police_id != f->cookie) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Only one policer per port is supported");
			return -EEXIST;
		}

		if (action->police.rate_pkt_ps) {
			NL_SET_ERR_MSG_MOD(extack,
					   "QoS offload not support packets per second");
			return -EOPNOTSUPP;
		}

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
	case TC_CLSMATCHALL_DESTROY:
		if (priv->tc.police_id != f->cookie)
			return -ENOENT;

		err = ocelot_port_policer_del(ocelot, port);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Could not delete policer");
			return err;
		}
		priv->tc.police_id = 0;
		priv->tc.offload_cnt--;
		return 0;
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
	int port = priv->chip_port;
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
	int port = priv->chip_port;
	int ret;

	/* 8021q removes VID 0 on module unload for all interfaces
	 * with VLAN filtering feature. We need to keep it to receive
	 * untagged traffic.
	 */
	if (vid == 0)
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
	int port = priv->chip_port;
	u32 rew_op = 0;

	if (!ocelot_can_inject(ocelot, 0))
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

	ocelot_port_inject_frame(ocelot, port, 0, rew_op, skb);

	kfree_skb(skb);

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
	w.forget.vid = ocelot_port->pvid_vlan.vid;
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
	w.learn.vid = ocelot_port->pvid_vlan.vid;
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
			  ocelot_port->pvid_vlan.vid, ENTRYTYPE_LOCKED);
	/* Then forget the previous one. */
	ocelot_mact_forget(ocelot, dev->dev_addr, ocelot_port->pvid_vlan.vid);

	ether_addr_copy(dev->dev_addr, addr->sa_data);
	return 0;
}

static void ocelot_get_stats64(struct net_device *dev,
			       struct rtnl_link_stats64 *stats)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	/* Configure the port to read the stats from */
	ocelot_write(ocelot, SYS_STAT_CFG_STAT_VIEW(port),
		     SYS_STAT_CFG);

	/* Get Rx stats */
	stats->rx_bytes = ocelot_read(ocelot, SYS_COUNT_RX_OCTETS);
	stats->rx_packets = ocelot_read(ocelot, SYS_COUNT_RX_SHORTS) +
			    ocelot_read(ocelot, SYS_COUNT_RX_FRAGMENTS) +
			    ocelot_read(ocelot, SYS_COUNT_RX_JABBERS) +
			    ocelot_read(ocelot, SYS_COUNT_RX_LONGS) +
			    ocelot_read(ocelot, SYS_COUNT_RX_64) +
			    ocelot_read(ocelot, SYS_COUNT_RX_65_127) +
			    ocelot_read(ocelot, SYS_COUNT_RX_128_255) +
			    ocelot_read(ocelot, SYS_COUNT_RX_256_1023) +
			    ocelot_read(ocelot, SYS_COUNT_RX_1024_1526) +
			    ocelot_read(ocelot, SYS_COUNT_RX_1527_MAX);
	stats->multicast = ocelot_read(ocelot, SYS_COUNT_RX_MULTICAST);
	stats->rx_dropped = dev->stats.rx_dropped;

	/* Get Tx stats */
	stats->tx_bytes = ocelot_read(ocelot, SYS_COUNT_TX_OCTETS);
	stats->tx_packets = ocelot_read(ocelot, SYS_COUNT_TX_64) +
			    ocelot_read(ocelot, SYS_COUNT_TX_65_127) +
			    ocelot_read(ocelot, SYS_COUNT_TX_128_511) +
			    ocelot_read(ocelot, SYS_COUNT_TX_512_1023) +
			    ocelot_read(ocelot, SYS_COUNT_TX_1024_1526) +
			    ocelot_read(ocelot, SYS_COUNT_TX_1527_MAX);
	stats->tx_dropped = ocelot_read(ocelot, SYS_COUNT_TX_DROPS) +
			    ocelot_read(ocelot, SYS_COUNT_TX_AGING);
	stats->collisions = ocelot_read(ocelot, SYS_COUNT_TX_COLLISION);
}

static int ocelot_port_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
			       struct net_device *dev,
			       const unsigned char *addr,
			       u16 vid, u16 flags,
			       struct netlink_ext_ack *extack)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	return ocelot_fdb_add(ocelot, port, addr, vid);
}

static int ocelot_port_fdb_del(struct ndmsg *ndm, struct nlattr *tb[],
			       struct net_device *dev,
			       const unsigned char *addr, u16 vid)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	return ocelot_fdb_del(ocelot, port, addr, vid);
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
	int port = priv->chip_port;
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
	int port = priv->chip_port;

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
	int port = priv->chip_port;

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

static const struct net_device_ops ocelot_port_netdev_ops = {
	.ndo_open			= ocelot_port_open,
	.ndo_stop			= ocelot_port_stop,
	.ndo_start_xmit			= ocelot_port_xmit,
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
	.ndo_get_devlink_port		= ocelot_get_devlink_port,
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

	return priv->chip_port;
}

static void ocelot_port_get_strings(struct net_device *netdev, u32 sset,
				    u8 *data)
{
	struct ocelot_port_private *priv = netdev_priv(netdev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	ocelot_get_strings(ocelot, port, sset, data);
}

static void ocelot_port_get_ethtool_stats(struct net_device *dev,
					  struct ethtool_stats *stats,
					  u64 *data)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	ocelot_get_ethtool_stats(ocelot, port, data);
}

static int ocelot_port_get_sset_count(struct net_device *dev, int sset)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	return ocelot_get_sset_count(ocelot, port, sset);
}

static int ocelot_port_get_ts_info(struct net_device *dev,
				   struct ethtool_ts_info *info)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

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
	int port = priv->chip_port;
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
	int port = priv->chip_port;

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
	int port = priv->chip_port;

	return ocelot_port_mdb_add(ocelot, port, mdb);
}

static int ocelot_port_obj_del_mdb(struct net_device *dev,
				   const struct switchdev_obj_port_mdb *mdb)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->chip_port;

	return ocelot_port_mdb_del(ocelot, port, mdb);
}

static int ocelot_port_obj_mrp_add(struct net_device *dev,
				   const struct switchdev_obj_mrp *mrp)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->chip_port;

	return ocelot_mrp_add(ocelot, port, mrp);
}

static int ocelot_port_obj_mrp_del(struct net_device *dev,
				   const struct switchdev_obj_mrp *mrp)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->chip_port;

	return ocelot_mrp_del(ocelot, port, mrp);
}

static int
ocelot_port_obj_mrp_add_ring_role(struct net_device *dev,
				  const struct switchdev_obj_ring_role_mrp *mrp)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->chip_port;

	return ocelot_mrp_add_ring_role(ocelot, port, mrp);
}

static int
ocelot_port_obj_mrp_del_ring_role(struct net_device *dev,
				  const struct switchdev_obj_ring_role_mrp *mrp)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->chip_port;

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

static int ocelot_netdevice_bridge_join(struct net_device *dev,
					struct net_device *brport_dev,
					struct net_device *bridge,
					struct netlink_ext_ack *extack)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->chip_port;
	int err;

	ocelot_port_bridge_join(ocelot, port, bridge);

	err = switchdev_bridge_port_offload(brport_dev, dev, priv,
					    &ocelot_netdevice_nb,
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
					&ocelot_netdevice_nb,
					&ocelot_switchdev_blocking_nb);
err_switchdev_offload:
	ocelot_port_bridge_leave(ocelot, port, bridge);
	return err;
}

static void ocelot_netdevice_pre_bridge_leave(struct net_device *dev,
					      struct net_device *brport_dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);

	switchdev_bridge_port_unoffload(brport_dev, priv,
					&ocelot_netdevice_nb,
					&ocelot_switchdev_blocking_nb);
}

static int ocelot_netdevice_bridge_leave(struct net_device *dev,
					 struct net_device *brport_dev,
					 struct net_device *bridge)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->chip_port;
	int err;

	err = ocelot_switchdev_unsync(ocelot, port);
	if (err)
		return err;

	ocelot_port_bridge_leave(ocelot, port, bridge);

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
	int port = priv->chip_port;
	int err;

	err = ocelot_port_lag_join(ocelot, port, bond, info);
	if (err == -EOPNOTSUPP) {
		NL_SET_ERR_MSG_MOD(extack, "Offloading not supported");
		return 0;
	}

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
	int port = priv->chip_port;

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
	int port = priv->chip_port;

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

static void vsc7514_phylink_validate(struct phylink_config *config,
				     unsigned long *supported,
				     struct phylink_link_state *state)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct ocelot_port_private *priv = netdev_priv(ndev);
	struct ocelot_port *ocelot_port = &priv->port;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = {};

	if (state->interface != PHY_INTERFACE_MODE_NA &&
	    state->interface != ocelot_port->phy_mode) {
		bitmap_zero(supported, __ETHTOOL_LINK_MODE_MASK_NBITS);
		return;
	}

	phylink_set_port_modes(mask);

	phylink_set(mask, Pause);
	phylink_set(mask, Autoneg);
	phylink_set(mask, Asym_Pause);
	phylink_set(mask, 10baseT_Half);
	phylink_set(mask, 10baseT_Full);
	phylink_set(mask, 100baseT_Half);
	phylink_set(mask, 100baseT_Full);
	phylink_set(mask, 1000baseT_Half);
	phylink_set(mask, 1000baseT_Full);
	phylink_set(mask, 1000baseX_Full);
	phylink_set(mask, 2500baseT_Full);
	phylink_set(mask, 2500baseX_Full);

	bitmap_and(supported, supported, mask, __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static void vsc7514_phylink_mac_config(struct phylink_config *config,
				       unsigned int link_an_mode,
				       const struct phylink_link_state *state)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct ocelot_port_private *priv = netdev_priv(ndev);
	struct ocelot_port *ocelot_port = &priv->port;

	/* Disable HDX fast control */
	ocelot_port_writel(ocelot_port, DEV_PORT_MISC_HDX_FAST_DIS,
			   DEV_PORT_MISC);

	/* SGMII only for now */
	ocelot_port_writel(ocelot_port, PCS1G_MODE_CFG_SGMII_MODE_ENA,
			   PCS1G_MODE_CFG);
	ocelot_port_writel(ocelot_port, PCS1G_SD_CFG_SD_SEL, PCS1G_SD_CFG);

	/* Enable PCS */
	ocelot_port_writel(ocelot_port, PCS1G_CFG_PCS_ENA, PCS1G_CFG);

	/* No aneg on SGMII */
	ocelot_port_writel(ocelot_port, 0, PCS1G_ANEG_CFG);

	/* No loopback */
	ocelot_port_writel(ocelot_port, 0, PCS1G_LB_CFG);
}

static void vsc7514_phylink_mac_link_down(struct phylink_config *config,
					  unsigned int link_an_mode,
					  phy_interface_t interface)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct ocelot_port_private *priv = netdev_priv(ndev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

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
	int port = priv->chip_port;

	ocelot_phylink_mac_link_up(ocelot, port, phydev, link_an_mode,
				   interface, speed, duplex,
				   tx_pause, rx_pause, OCELOT_MAC_QUIRKS);
}

static const struct phylink_mac_ops ocelot_phylink_ops = {
	.validate		= vsc7514_phylink_validate,
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

	/* Ensure clock signals and speed are set on all QSGMII links */
	if (phy_mode == PHY_INTERFACE_MODE_QSGMII)
		ocelot_port_rmwl(ocelot_port, 0,
				 DEV_CLOCK_CFG_MAC_TX_RST |
				 DEV_CLOCK_CFG_MAC_TX_RST,
				 DEV_CLOCK_CFG);

	ocelot_port->phy_mode = phy_mode;

	if (phy_mode != PHY_INTERFACE_MODE_INTERNAL) {
		struct phy *serdes = of_phy_get(portnp, NULL);

		if (IS_ERR(serdes)) {
			err = PTR_ERR(serdes);
			dev_err_probe(dev, err,
				      "missing SerDes phys for port %d\n",
				      port);
			return err;
		}

		err = phy_set_mode_ext(serdes, PHY_MODE_ETHERNET, phy_mode);
		of_phy_put(serdes);
		if (err) {
			dev_err(dev, "Could not SerDes mode on port %d: %pe\n",
				port, ERR_PTR(err));
			return err;
		}
	}

	priv = container_of(ocelot_port, struct ocelot_port_private, port);

	priv->phylink_config.dev = &priv->dev->dev;
	priv->phylink_config.type = PHYLINK_NETDEV;

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
	priv->chip_port = port;
	ocelot_port = &priv->port;
	ocelot_port->ocelot = ocelot;
	ocelot_port->target = target;
	ocelot->ports[port] = ocelot_port;

	dev->netdev_ops = &ocelot_port_netdev_ops;
	dev->ethtool_ops = &ocelot_ethtool_ops;

	dev->hw_features |= NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_RXFCS |
		NETIF_F_HW_TC;
	dev->features |= NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_TC;

	memcpy(dev->dev_addr, ocelot->base_mac, ETH_ALEN);
	dev->dev_addr[ETH_ALEN - 1] += port;
	ocelot_mact_learn(ocelot, PGID_CPU, dev->dev_addr,
			  ocelot_port->pvid_vlan.vid, ENTRYTYPE_LOCKED);

	ocelot_init_port(ocelot, port);

	err = ocelot_port_phylink_create(ocelot, port, portnp);
	if (err)
		goto out;

	err = register_netdev(dev);
	if (err) {
		dev_err(ocelot->dev, "register_netdev failed\n");
		goto out;
	}

	return 0;

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

	unregister_netdev(priv->dev);

	if (priv->phylink) {
		rtnl_lock();
		phylink_disconnect_phy(priv->phylink);
		rtnl_unlock();

		phylink_destroy(priv->phylink);
	}

	free_netdev(priv->dev);
}
