// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017, 2019 Microsemi Corporation
 */

#include <linux/if_bridge.h>
#include "ocelot.h"
#include "ocelot_vcap.h"

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

static void ocelot_port_adjust_link(struct net_device *dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	ocelot_adjust_link(ocelot, port, dev->phydev);
}

static int ocelot_vlan_vid_prepare(struct net_device *dev, u16 vid, bool pvid,
				   bool untagged)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->chip_port;

	return ocelot_vlan_prepare(ocelot, port, vid, pvid, untagged);
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
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->chip_port;
	int err;

	if (priv->serdes) {
		err = phy_set_mode_ext(priv->serdes, PHY_MODE_ETHERNET,
				       ocelot_port->phy_mode);
		if (err) {
			netdev_err(dev, "Could not set mode of SerDes\n");
			return err;
		}
	}

	err = phy_connect_direct(dev, priv->phy, &ocelot_port_adjust_link,
				 ocelot_port->phy_mode);
	if (err) {
		netdev_err(dev, "Could not attach to PHY\n");
		return err;
	}

	dev->phydev = priv->phy;

	phy_attached_info(priv->phy);
	phy_start(priv->phy);

	ocelot_port_enable(ocelot, port, priv->phy);

	return 0;
}

static int ocelot_port_stop(struct net_device *dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	phy_disconnect(priv->phy);

	dev->phydev = NULL;

	ocelot_port_disable(ocelot, port);

	return 0;
}

/* Generate the IFH for frame injection
 *
 * The IFH is a 128bit-value
 * bit 127: bypass the analyzer processing
 * bit 56-67: destination mask
 * bit 28-29: pop_cnt: 3 disables all rewriting of the frame
 * bit 20-27: cpu extraction queue mask
 * bit 16: tag type 0: C-tag, 1: S-tag
 * bit 0-11: VID
 */
static int ocelot_gen_ifh(u32 *ifh, struct frame_info *info)
{
	ifh[0] = IFH_INJ_BYPASS | ((0x1ff & info->rew_op) << 21);
	ifh[1] = (0xf00 & info->port) >> 8;
	ifh[2] = (0xff & info->port) << 24;
	ifh[3] = (info->tag_type << 16) | info->vid;

	return 0;
}

static int ocelot_port_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	u32 val, ifh[OCELOT_TAG_LEN / 4];
	struct frame_info info = {};
	u8 grp = 0; /* Send everything on CPU group 0 */
	unsigned int i, count, last;
	int port = priv->chip_port;

	val = ocelot_read(ocelot, QS_INJ_STATUS);
	if (!(val & QS_INJ_STATUS_FIFO_RDY(BIT(grp))) ||
	    (val & QS_INJ_STATUS_WMARK_REACHED(BIT(grp))))
		return NETDEV_TX_BUSY;

	ocelot_write_rix(ocelot, QS_INJ_CTRL_GAP_SIZE(1) |
			 QS_INJ_CTRL_SOF, QS_INJ_CTRL, grp);

	info.port = BIT(port);
	info.tag_type = IFH_TAG_TYPE_C;
	info.vid = skb_vlan_tag_get(skb);

	/* Check if timestamping is needed */
	if (ocelot->ptp && (shinfo->tx_flags & SKBTX_HW_TSTAMP)) {
		info.rew_op = ocelot_port->ptp_cmd;

		if (ocelot_port->ptp_cmd == IFH_REW_OP_TWO_STEP_PTP) {
			struct sk_buff *clone;

			clone = skb_clone_sk(skb);
			if (!clone) {
				kfree_skb(skb);
				return NETDEV_TX_OK;
			}

			ocelot_port_add_txtstamp_skb(ocelot, port, clone);

			info.rew_op |= clone->cb[0] << 3;
		}
	}

	if (ocelot->ptp && shinfo->tx_flags & SKBTX_HW_TSTAMP) {
		info.rew_op = ocelot_port->ptp_cmd;
		if (ocelot_port->ptp_cmd == IFH_REW_OP_TWO_STEP_PTP)
			info.rew_op |= skb->cb[0] << 3;
	}

	ocelot_gen_ifh(ifh, &info);

	for (i = 0; i < OCELOT_TAG_LEN / 4; i++)
		ocelot_write_rix(ocelot, (__force u32)cpu_to_be32(ifh[i]),
				 QS_INJ_WR, grp);

	count = (skb->len + 3) / 4;
	last = skb->len % 4;
	for (i = 0; i < count; i++)
		ocelot_write_rix(ocelot, ((u32 *)skb->data)[i], QS_INJ_WR, grp);

	/* Add padding */
	while (i < (OCELOT_BUFFER_CELL_SZ / 4)) {
		ocelot_write_rix(ocelot, 0, QS_INJ_WR, grp);
		i++;
	}

	/* Indicate EOF and valid bytes in last word */
	ocelot_write_rix(ocelot, QS_INJ_CTRL_GAP_SIZE(1) |
			 QS_INJ_CTRL_VLD_BYTES(skb->len < OCELOT_BUFFER_CELL_SZ ? 0 : last) |
			 QS_INJ_CTRL_EOF,
			 QS_INJ_CTRL, grp);

	/* Add dummy CRC */
	ocelot_write_rix(ocelot, 0, QS_INJ_WR, grp);
	skb_tx_timestamp(skb);

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

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
	};

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

static int ocelot_port_get_phys_port_name(struct net_device *dev,
					  char *buf, size_t len)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	int port = priv->chip_port;
	int ret;

	ret = snprintf(buf, len, "p%d", port);
	if (ret >= len)
		return -EINVAL;

	return 0;
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

static int ocelot_get_port_parent_id(struct net_device *dev,
				     struct netdev_phys_item_id *ppid)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;

	ppid->id_len = sizeof(ocelot->base_mac);
	memcpy(&ppid->id, &ocelot->base_mac, ppid->id_len);

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
	.ndo_get_phys_port_name		= ocelot_port_get_phys_port_name,
	.ndo_set_mac_address		= ocelot_port_set_mac_address,
	.ndo_get_stats64		= ocelot_get_stats64,
	.ndo_fdb_add			= ocelot_port_fdb_add,
	.ndo_fdb_del			= ocelot_port_fdb_del,
	.ndo_fdb_dump			= ocelot_port_fdb_dump,
	.ndo_vlan_rx_add_vid		= ocelot_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid		= ocelot_vlan_rx_kill_vid,
	.ndo_set_features		= ocelot_set_features,
	.ndo_get_port_parent_id		= ocelot_get_port_parent_id,
	.ndo_setup_tc			= ocelot_setup_tc,
	.ndo_do_ioctl			= ocelot_ioctl,
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
					   struct switchdev_trans *trans,
					   u8 state)
{
	if (switchdev_trans_ph_prepare(trans))
		return;

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

static int ocelot_port_attr_set(struct net_device *dev,
				const struct switchdev_attr *attr,
				struct switchdev_trans *trans)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;
	int err = 0;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		ocelot_port_attr_stp_state_set(ocelot, port, trans,
					       attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		ocelot_port_attr_ageing_set(ocelot, port, attr->u.ageing_time);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		ocelot_port_vlan_filtering(ocelot, port,
					   attr->u.vlan_filtering, trans);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_MC_DISABLED:
		ocelot_port_attr_mc_set(ocelot, port, !attr->u.mc_disabled);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int ocelot_port_obj_add_vlan(struct net_device *dev,
				    const struct switchdev_obj_port_vlan *vlan)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	int ret;

	ret = ocelot_vlan_vid_prepare(dev, vlan->vid, pvid, untagged);
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

static int ocelot_port_obj_add(struct net_device *dev,
			       const struct switchdev_obj *obj,
			       struct netlink_ext_ack *extack)
{
	int ret = 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		ret = ocelot_port_obj_add_vlan(dev,
					       SWITCHDEV_OBJ_PORT_VLAN(obj));
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		ret = ocelot_port_obj_add_mdb(dev, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

static int ocelot_port_obj_del(struct net_device *dev,
			       const struct switchdev_obj *obj)
{
	int ret = 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		ret = ocelot_vlan_vid_del(dev,
					  SWITCHDEV_OBJ_PORT_VLAN(obj)->vid);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		ret = ocelot_port_obj_del_mdb(dev, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

static int ocelot_netdevice_port_event(struct net_device *dev,
				       unsigned long event,
				       struct netdev_notifier_changeupper_info *info)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->chip_port;
	int err = 0;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		if (netif_is_bridge_master(info->upper_dev)) {
			if (info->linking) {
				err = ocelot_port_bridge_join(ocelot, port,
							      info->upper_dev);
			} else {
				err = ocelot_port_bridge_leave(ocelot, port,
							       info->upper_dev);
			}
		}
		if (netif_is_lag_master(info->upper_dev)) {
			if (info->linking)
				err = ocelot_port_lag_join(ocelot, port,
							   info->upper_dev);
			else
				ocelot_port_lag_leave(ocelot, port,
						      info->upper_dev);
		}
		break;
	default:
		break;
	}

	return err;
}

static int ocelot_netdevice_event(struct notifier_block *unused,
				  unsigned long event, void *ptr)
{
	struct netdev_notifier_changeupper_info *info = ptr;
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	int ret = 0;

	if (!ocelot_netdevice_dev_check(dev))
		return 0;

	if (event == NETDEV_PRECHANGEUPPER &&
	    netif_is_lag_master(info->upper_dev)) {
		struct netdev_lag_upper_info *lag_upper_info = info->upper_info;
		struct netlink_ext_ack *extack;

		if (lag_upper_info &&
		    lag_upper_info->tx_type != NETDEV_LAG_TX_TYPE_HASH) {
			extack = netdev_notifier_info_to_extack(&info->info);
			NL_SET_ERR_MSG_MOD(extack, "LAG device using unsupported Tx type");

			ret = -EINVAL;
			goto notify;
		}
	}

	if (netif_is_lag_master(dev)) {
		struct net_device *slave;
		struct list_head *iter;

		netdev_for_each_lower_dev(dev, slave, iter) {
			ret = ocelot_netdevice_port_event(slave, event, info);
			if (ret)
				goto notify;
		}
	} else {
		ret = ocelot_netdevice_port_event(dev, event, info);
	}

notify:
	return notifier_from_errno(ret);
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

int ocelot_probe_port(struct ocelot *ocelot, int port, struct regmap *target,
		      struct phy_device *phy)
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
	priv->phy = phy;
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

	err = register_netdev(dev);
	if (err) {
		dev_err(ocelot->dev, "register_netdev failed\n");
		free_netdev(dev);
	}

	return err;
}
