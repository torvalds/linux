// SPDX-License-Identifier: GPL-2.0
/*
 * DPAA2 Ethernet Switch driver
 *
 * Copyright 2014-2016 Freescale Semiconductor Inc.
 * Copyright 2017-2018 NXP
 *
 */

#include <linux/module.h>

#include <linux/interrupt.h>
#include <linux/msi.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>

#include <linux/fsl/mc.h>

#include "ethsw.h"

static struct workqueue_struct *ethsw_owq;

/* Minimal supported DPSW version */
#define DPSW_MIN_VER_MAJOR		8
#define DPSW_MIN_VER_MINOR		0

#define DEFAULT_VLAN_ID			1

static int ethsw_add_vlan(struct ethsw_core *ethsw, u16 vid)
{
	int err;

	struct dpsw_vlan_cfg	vcfg = {
		.fdb_id = 0,
	};

	if (ethsw->vlans[vid]) {
		dev_err(ethsw->dev, "VLAN already configured\n");
		return -EEXIST;
	}

	err = dpsw_vlan_add(ethsw->mc_io, 0,
			    ethsw->dpsw_handle, vid, &vcfg);
	if (err) {
		dev_err(ethsw->dev, "dpsw_vlan_add err %d\n", err);
		return err;
	}
	ethsw->vlans[vid] = ETHSW_VLAN_MEMBER;

	return 0;
}

static int ethsw_port_set_pvid(struct ethsw_port_priv *port_priv, u16 pvid)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct net_device *netdev = port_priv->netdev;
	struct dpsw_tci_cfg tci_cfg = { 0 };
	bool is_oper;
	int err, ret;

	err = dpsw_if_get_tci(ethsw->mc_io, 0, ethsw->dpsw_handle,
			      port_priv->idx, &tci_cfg);
	if (err) {
		netdev_err(netdev, "dpsw_if_get_tci err %d\n", err);
		return err;
	}

	tci_cfg.vlan_id = pvid;

	/* Interface needs to be down to change PVID */
	is_oper = netif_oper_up(netdev);
	if (is_oper) {
		err = dpsw_if_disable(ethsw->mc_io, 0,
				      ethsw->dpsw_handle,
				      port_priv->idx);
		if (err) {
			netdev_err(netdev, "dpsw_if_disable err %d\n", err);
			return err;
		}
	}

	err = dpsw_if_set_tci(ethsw->mc_io, 0, ethsw->dpsw_handle,
			      port_priv->idx, &tci_cfg);
	if (err) {
		netdev_err(netdev, "dpsw_if_set_tci err %d\n", err);
		goto set_tci_error;
	}

	/* Delete previous PVID info and mark the new one */
	port_priv->vlans[port_priv->pvid] &= ~ETHSW_VLAN_PVID;
	port_priv->vlans[pvid] |= ETHSW_VLAN_PVID;
	port_priv->pvid = pvid;

set_tci_error:
	if (is_oper) {
		ret = dpsw_if_enable(ethsw->mc_io, 0,
				     ethsw->dpsw_handle,
				     port_priv->idx);
		if (ret) {
			netdev_err(netdev, "dpsw_if_enable err %d\n", ret);
			return ret;
		}
	}

	return err;
}

static int ethsw_port_add_vlan(struct ethsw_port_priv *port_priv,
			       u16 vid, u16 flags)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct net_device *netdev = port_priv->netdev;
	struct dpsw_vlan_if_cfg vcfg;
	int err;

	if (port_priv->vlans[vid]) {
		netdev_warn(netdev, "VLAN %d already configured\n", vid);
		return -EEXIST;
	}

	vcfg.num_ifs = 1;
	vcfg.if_id[0] = port_priv->idx;
	err = dpsw_vlan_add_if(ethsw->mc_io, 0, ethsw->dpsw_handle, vid, &vcfg);
	if (err) {
		netdev_err(netdev, "dpsw_vlan_add_if err %d\n", err);
		return err;
	}

	port_priv->vlans[vid] = ETHSW_VLAN_MEMBER;

	if (flags & BRIDGE_VLAN_INFO_UNTAGGED) {
		err = dpsw_vlan_add_if_untagged(ethsw->mc_io, 0,
						ethsw->dpsw_handle,
						vid, &vcfg);
		if (err) {
			netdev_err(netdev,
				   "dpsw_vlan_add_if_untagged err %d\n", err);
			return err;
		}
		port_priv->vlans[vid] |= ETHSW_VLAN_UNTAGGED;
	}

	if (flags & BRIDGE_VLAN_INFO_PVID) {
		err = ethsw_port_set_pvid(port_priv, vid);
		if (err)
			return err;
	}

	return 0;
}

static int ethsw_set_learning(struct ethsw_core *ethsw, u8 flag)
{
	enum dpsw_fdb_learning_mode learn_mode;
	int err;

	if (flag)
		learn_mode = DPSW_FDB_LEARNING_MODE_HW;
	else
		learn_mode = DPSW_FDB_LEARNING_MODE_DIS;

	err = dpsw_fdb_set_learning_mode(ethsw->mc_io, 0, ethsw->dpsw_handle, 0,
					 learn_mode);
	if (err) {
		dev_err(ethsw->dev, "dpsw_fdb_set_learning_mode err %d\n", err);
		return err;
	}
	ethsw->learning = !!flag;

	return 0;
}

static int ethsw_port_set_flood(struct ethsw_port_priv *port_priv, u8 flag)
{
	int err;

	err = dpsw_if_set_flooding(port_priv->ethsw_data->mc_io, 0,
				   port_priv->ethsw_data->dpsw_handle,
				   port_priv->idx, flag);
	if (err) {
		netdev_err(port_priv->netdev,
			   "dpsw_fdb_set_learning_mode err %d\n", err);
		return err;
	}
	port_priv->flood = !!flag;

	return 0;
}

static int ethsw_port_set_stp_state(struct ethsw_port_priv *port_priv, u8 state)
{
	struct dpsw_stp_cfg stp_cfg = {
		.vlan_id = DEFAULT_VLAN_ID,
		.state = state,
	};
	int err;

	if (!netif_oper_up(port_priv->netdev) || state == port_priv->stp_state)
		return 0;	/* Nothing to do */

	err = dpsw_if_set_stp(port_priv->ethsw_data->mc_io, 0,
			      port_priv->ethsw_data->dpsw_handle,
			      port_priv->idx, &stp_cfg);
	if (err) {
		netdev_err(port_priv->netdev,
			   "dpsw_if_set_stp err %d\n", err);
		return err;
	}

	port_priv->stp_state = state;

	return 0;
}

static int ethsw_dellink_switch(struct ethsw_core *ethsw, u16 vid)
{
	struct ethsw_port_priv *ppriv_local = NULL;
	int i, err;

	if (!ethsw->vlans[vid])
		return -ENOENT;

	err = dpsw_vlan_remove(ethsw->mc_io, 0, ethsw->dpsw_handle, vid);
	if (err) {
		dev_err(ethsw->dev, "dpsw_vlan_remove err %d\n", err);
		return err;
	}
	ethsw->vlans[vid] = 0;

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++) {
		ppriv_local = ethsw->ports[i];
		ppriv_local->vlans[vid] = 0;
	}

	return 0;
}

static int ethsw_port_fdb_add_uc(struct ethsw_port_priv *port_priv,
				 const unsigned char *addr)
{
	struct dpsw_fdb_unicast_cfg entry = {0};
	int err;

	entry.if_egress = port_priv->idx;
	entry.type = DPSW_FDB_ENTRY_STATIC;
	ether_addr_copy(entry.mac_addr, addr);

	err = dpsw_fdb_add_unicast(port_priv->ethsw_data->mc_io, 0,
				   port_priv->ethsw_data->dpsw_handle,
				   0, &entry);
	if (err)
		netdev_err(port_priv->netdev,
			   "dpsw_fdb_add_unicast err %d\n", err);
	return err;
}

static int ethsw_port_fdb_del_uc(struct ethsw_port_priv *port_priv,
				 const unsigned char *addr)
{
	struct dpsw_fdb_unicast_cfg entry = {0};
	int err;

	entry.if_egress = port_priv->idx;
	entry.type = DPSW_FDB_ENTRY_STATIC;
	ether_addr_copy(entry.mac_addr, addr);

	err = dpsw_fdb_remove_unicast(port_priv->ethsw_data->mc_io, 0,
				      port_priv->ethsw_data->dpsw_handle,
				      0, &entry);
	/* Silently discard error for calling multiple times the del command */
	if (err && err != -ENXIO)
		netdev_err(port_priv->netdev,
			   "dpsw_fdb_remove_unicast err %d\n", err);
	return err;
}

static int ethsw_port_fdb_add_mc(struct ethsw_port_priv *port_priv,
				 const unsigned char *addr)
{
	struct dpsw_fdb_multicast_cfg entry = {0};
	int err;

	ether_addr_copy(entry.mac_addr, addr);
	entry.type = DPSW_FDB_ENTRY_STATIC;
	entry.num_ifs = 1;
	entry.if_id[0] = port_priv->idx;

	err = dpsw_fdb_add_multicast(port_priv->ethsw_data->mc_io, 0,
				     port_priv->ethsw_data->dpsw_handle,
				     0, &entry);
	/* Silently discard error for calling multiple times the add command */
	if (err && err != -ENXIO)
		netdev_err(port_priv->netdev, "dpsw_fdb_add_multicast err %d\n",
			   err);
	return err;
}

static int ethsw_port_fdb_del_mc(struct ethsw_port_priv *port_priv,
				 const unsigned char *addr)
{
	struct dpsw_fdb_multicast_cfg entry = {0};
	int err;

	ether_addr_copy(entry.mac_addr, addr);
	entry.type = DPSW_FDB_ENTRY_STATIC;
	entry.num_ifs = 1;
	entry.if_id[0] = port_priv->idx;

	err = dpsw_fdb_remove_multicast(port_priv->ethsw_data->mc_io, 0,
					port_priv->ethsw_data->dpsw_handle,
					0, &entry);
	/* Silently discard error for calling multiple times the del command */
	if (err && err != -ENAVAIL)
		netdev_err(port_priv->netdev,
			   "dpsw_fdb_remove_multicast err %d\n", err);
	return err;
}

static void port_get_stats(struct net_device *netdev,
			   struct rtnl_link_stats64 *stats)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	u64 tmp;
	int err;

	err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
				  port_priv->ethsw_data->dpsw_handle,
				  port_priv->idx,
				  DPSW_CNT_ING_FRAME, &stats->rx_packets);
	if (err)
		goto error;

	err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
				  port_priv->ethsw_data->dpsw_handle,
				  port_priv->idx,
				  DPSW_CNT_EGR_FRAME, &stats->tx_packets);
	if (err)
		goto error;

	err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
				  port_priv->ethsw_data->dpsw_handle,
				  port_priv->idx,
				  DPSW_CNT_ING_BYTE, &stats->rx_bytes);
	if (err)
		goto error;

	err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
				  port_priv->ethsw_data->dpsw_handle,
				  port_priv->idx,
				  DPSW_CNT_EGR_BYTE, &stats->tx_bytes);
	if (err)
		goto error;

	err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
				  port_priv->ethsw_data->dpsw_handle,
				  port_priv->idx,
				  DPSW_CNT_ING_FRAME_DISCARD,
				  &stats->rx_dropped);
	if (err)
		goto error;

	err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
				  port_priv->ethsw_data->dpsw_handle,
				  port_priv->idx,
				  DPSW_CNT_ING_FLTR_FRAME,
				  &tmp);
	if (err)
		goto error;
	stats->rx_dropped += tmp;

	err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
				  port_priv->ethsw_data->dpsw_handle,
				  port_priv->idx,
				  DPSW_CNT_EGR_FRAME_DISCARD,
				  &stats->tx_dropped);
	if (err)
		goto error;

	return;

error:
	netdev_err(netdev, "dpsw_if_get_counter err %d\n", err);
}

static bool port_has_offload_stats(const struct net_device *netdev,
				   int attr_id)
{
	return (attr_id == IFLA_OFFLOAD_XSTATS_CPU_HIT);
}

static int port_get_offload_stats(int attr_id,
				  const struct net_device *netdev,
				  void *sp)
{
	switch (attr_id) {
	case IFLA_OFFLOAD_XSTATS_CPU_HIT:
		port_get_stats((struct net_device *)netdev, sp);
		return 0;
	}

	return -EINVAL;
}

static int port_change_mtu(struct net_device *netdev, int mtu)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	err = dpsw_if_set_max_frame_length(port_priv->ethsw_data->mc_io,
					   0,
					   port_priv->ethsw_data->dpsw_handle,
					   port_priv->idx,
					   (u16)ETHSW_L2_MAX_FRM(mtu));
	if (err) {
		netdev_err(netdev,
			   "dpsw_if_set_max_frame_length() err %d\n", err);
		return err;
	}

	netdev->mtu = mtu;
	return 0;
}

static int port_carrier_state_sync(struct net_device *netdev)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct dpsw_link_state state;
	int err;

	err = dpsw_if_get_link_state(port_priv->ethsw_data->mc_io, 0,
				     port_priv->ethsw_data->dpsw_handle,
				     port_priv->idx, &state);
	if (err) {
		netdev_err(netdev, "dpsw_if_get_link_state() err %d\n", err);
		return err;
	}

	WARN_ONCE(state.up > 1, "Garbage read into link_state");

	if (state.up != port_priv->link_state) {
		if (state.up)
			netif_carrier_on(netdev);
		else
			netif_carrier_off(netdev);
		port_priv->link_state = state.up;
	}
	return 0;
}

static int port_open(struct net_device *netdev)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	/* No need to allow Tx as control interface is disabled */
	netif_tx_stop_all_queues(netdev);

	err = dpsw_if_enable(port_priv->ethsw_data->mc_io, 0,
			     port_priv->ethsw_data->dpsw_handle,
			     port_priv->idx);
	if (err) {
		netdev_err(netdev, "dpsw_if_enable err %d\n", err);
		return err;
	}

	/* sync carrier state */
	err = port_carrier_state_sync(netdev);
	if (err) {
		netdev_err(netdev,
			   "port_carrier_state_sync err %d\n", err);
		goto err_carrier_sync;
	}

	return 0;

err_carrier_sync:
	dpsw_if_disable(port_priv->ethsw_data->mc_io, 0,
			port_priv->ethsw_data->dpsw_handle,
			port_priv->idx);
	return err;
}

static int port_stop(struct net_device *netdev)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	err = dpsw_if_disable(port_priv->ethsw_data->mc_io, 0,
			      port_priv->ethsw_data->dpsw_handle,
			      port_priv->idx);
	if (err) {
		netdev_err(netdev, "dpsw_if_disable err %d\n", err);
		return err;
	}

	return 0;
}

static netdev_tx_t port_dropframe(struct sk_buff *skb,
				  struct net_device *netdev)
{
	/* we don't support I/O for now, drop the frame */
	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static const struct net_device_ops ethsw_port_ops = {
	.ndo_open		= port_open,
	.ndo_stop		= port_stop,

	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_change_mtu		= port_change_mtu,
	.ndo_has_offload_stats	= port_has_offload_stats,
	.ndo_get_offload_stats	= port_get_offload_stats,

	.ndo_start_xmit		= port_dropframe,
};

static void ethsw_links_state_update(struct ethsw_core *ethsw)
{
	int i;

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++)
		port_carrier_state_sync(ethsw->ports[i]->netdev);
}

static irqreturn_t ethsw_irq0_handler_thread(int irq_num, void *arg)
{
	struct device *dev = (struct device *)arg;
	struct ethsw_core *ethsw = dev_get_drvdata(dev);

	/* Mask the events and the if_id reserved bits to be cleared on read */
	u32 status = DPSW_IRQ_EVENT_LINK_CHANGED | 0xFFFF0000;
	int err;

	err = dpsw_get_irq_status(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  DPSW_IRQ_INDEX_IF, &status);
	if (err) {
		dev_err(dev, "Can't get irq status (err %d)", err);

		err = dpsw_clear_irq_status(ethsw->mc_io, 0, ethsw->dpsw_handle,
					    DPSW_IRQ_INDEX_IF, 0xFFFFFFFF);
		if (err)
			dev_err(dev, "Can't clear irq status (err %d)", err);
		goto out;
	}

	if (status & DPSW_IRQ_EVENT_LINK_CHANGED)
		ethsw_links_state_update(ethsw);

out:
	return IRQ_HANDLED;
}

static int ethsw_setup_irqs(struct fsl_mc_device *sw_dev)
{
	struct device *dev = &sw_dev->dev;
	struct ethsw_core *ethsw = dev_get_drvdata(dev);
	u32 mask = DPSW_IRQ_EVENT_LINK_CHANGED;
	struct fsl_mc_device_irq *irq;
	int err;

	err = fsl_mc_allocate_irqs(sw_dev);
	if (err) {
		dev_err(dev, "MC irqs allocation failed\n");
		return err;
	}

	if (WARN_ON(sw_dev->obj_desc.irq_count != DPSW_IRQ_NUM)) {
		err = -EINVAL;
		goto free_irq;
	}

	err = dpsw_set_irq_enable(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  DPSW_IRQ_INDEX_IF, 0);
	if (err) {
		dev_err(dev, "dpsw_set_irq_enable err %d\n", err);
		goto free_irq;
	}

	irq = sw_dev->irqs[DPSW_IRQ_INDEX_IF];

	err = devm_request_threaded_irq(dev, irq->msi_desc->irq,
					NULL,
					ethsw_irq0_handler_thread,
					IRQF_NO_SUSPEND | IRQF_ONESHOT,
					dev_name(dev), dev);
	if (err) {
		dev_err(dev, "devm_request_threaded_irq(): %d", err);
		goto free_irq;
	}

	err = dpsw_set_irq_mask(ethsw->mc_io, 0, ethsw->dpsw_handle,
				DPSW_IRQ_INDEX_IF, mask);
	if (err) {
		dev_err(dev, "dpsw_set_irq_mask(): %d", err);
		goto free_devm_irq;
	}

	err = dpsw_set_irq_enable(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  DPSW_IRQ_INDEX_IF, 1);
	if (err) {
		dev_err(dev, "dpsw_set_irq_enable(): %d", err);
		goto free_devm_irq;
	}

	return 0;

free_devm_irq:
	devm_free_irq(dev, irq->msi_desc->irq, dev);
free_irq:
	fsl_mc_free_irqs(sw_dev);
	return err;
}

static void ethsw_teardown_irqs(struct fsl_mc_device *sw_dev)
{
	struct device *dev = &sw_dev->dev;
	struct ethsw_core *ethsw = dev_get_drvdata(dev);
	int err;

	err = dpsw_set_irq_enable(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  DPSW_IRQ_INDEX_IF, 0);
	if (err)
		dev_err(dev, "dpsw_set_irq_enable err %d\n", err);

	fsl_mc_free_irqs(sw_dev);
}

static int swdev_port_attr_get(struct net_device *netdev,
			       struct switchdev_attr *attr)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_PARENT_ID:
		attr->u.ppid.id_len = 1;
		attr->u.ppid.id[0] = port_priv->ethsw_data->dev_id;
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		attr->u.brport_flags =
			(port_priv->ethsw_data->learning ? BR_LEARNING : 0) |
			(port_priv->flood ? BR_FLOOD : 0);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS_SUPPORT:
		attr->u.brport_flags_support = BR_LEARNING | BR_FLOOD;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int port_attr_stp_state_set(struct net_device *netdev,
				   struct switchdev_trans *trans,
				   u8 state)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);

	if (switchdev_trans_ph_prepare(trans))
		return 0;

	return ethsw_port_set_stp_state(port_priv, state);
}

static int port_attr_br_flags_set(struct net_device *netdev,
				  struct switchdev_trans *trans,
				  unsigned long flags)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err = 0;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

	/* Learning is enabled per switch */
	err = ethsw_set_learning(port_priv->ethsw_data, flags & BR_LEARNING);
	if (err)
		goto exit;

	err = ethsw_port_set_flood(port_priv, flags & BR_FLOOD);

exit:
	return err;
}

static int swdev_port_attr_set(struct net_device *netdev,
			       const struct switchdev_attr *attr,
			       struct switchdev_trans *trans)
{
	int err = 0;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		err = port_attr_stp_state_set(netdev, trans,
					      attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		err = port_attr_br_flags_set(netdev, trans,
					     attr->u.brport_flags);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		/* VLANs are supported by default  */
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int port_vlans_add(struct net_device *netdev,
			  const struct switchdev_obj_port_vlan *vlan,
			  struct switchdev_trans *trans)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int vid, err;

	if (netif_is_bridge_master(vlan->obj.orig_dev))
		return -EOPNOTSUPP;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++) {
		if (!port_priv->ethsw_data->vlans[vid]) {
			/* this is a new VLAN */
			err = ethsw_add_vlan(port_priv->ethsw_data, vid);
			if (err)
				return err;

			port_priv->ethsw_data->vlans[vid] |= ETHSW_VLAN_GLOBAL;
		}
		err = ethsw_port_add_vlan(port_priv, vid, vlan->flags);
		if (err)
			break;
	}

	return err;
}

static int port_lookup_address(struct net_device *netdev, int is_uc,
			       const unsigned char *addr)
{
	struct netdev_hw_addr_list *list = (is_uc) ? &netdev->uc : &netdev->mc;
	struct netdev_hw_addr *ha;

	netif_addr_lock_bh(netdev);
	list_for_each_entry(ha, &list->list, list) {
		if (ether_addr_equal(ha->addr, addr)) {
			netif_addr_unlock_bh(netdev);
			return 1;
		}
	}
	netif_addr_unlock_bh(netdev);
	return 0;
}

static int port_mdb_add(struct net_device *netdev,
			const struct switchdev_obj_port_mdb *mdb,
			struct switchdev_trans *trans)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

	/* Check if address is already set on this port */
	if (port_lookup_address(netdev, 0, mdb->addr))
		return -EEXIST;

	err = ethsw_port_fdb_add_mc(port_priv, mdb->addr);
	if (err)
		return err;

	err = dev_mc_add(netdev, mdb->addr);
	if (err) {
		netdev_err(netdev, "dev_mc_add err %d\n", err);
		ethsw_port_fdb_del_mc(port_priv, mdb->addr);
	}

	return err;
}

static int swdev_port_obj_add(struct net_device *netdev,
			      const struct switchdev_obj *obj,
			      struct switchdev_trans *trans)
{
	int err;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = port_vlans_add(netdev,
				     SWITCHDEV_OBJ_PORT_VLAN(obj),
				     trans);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = port_mdb_add(netdev,
				   SWITCHDEV_OBJ_PORT_MDB(obj),
				   trans);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int ethsw_port_del_vlan(struct ethsw_port_priv *port_priv, u16 vid)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct net_device *netdev = port_priv->netdev;
	struct dpsw_vlan_if_cfg vcfg;
	int i, err;

	if (!port_priv->vlans[vid])
		return -ENOENT;

	if (port_priv->vlans[vid] & ETHSW_VLAN_PVID) {
		err = ethsw_port_set_pvid(port_priv, 0);
		if (err)
			return err;
	}

	vcfg.num_ifs = 1;
	vcfg.if_id[0] = port_priv->idx;
	if (port_priv->vlans[vid] & ETHSW_VLAN_UNTAGGED) {
		err = dpsw_vlan_remove_if_untagged(ethsw->mc_io, 0,
						   ethsw->dpsw_handle,
						   vid, &vcfg);
		if (err) {
			netdev_err(netdev,
				   "dpsw_vlan_remove_if_untagged err %d\n",
				   err);
		}
		port_priv->vlans[vid] &= ~ETHSW_VLAN_UNTAGGED;
	}

	if (port_priv->vlans[vid] & ETHSW_VLAN_MEMBER) {
		err = dpsw_vlan_remove_if(ethsw->mc_io, 0, ethsw->dpsw_handle,
					  vid, &vcfg);
		if (err) {
			netdev_err(netdev,
				   "dpsw_vlan_remove_if err %d\n", err);
			return err;
		}
		port_priv->vlans[vid] &= ~ETHSW_VLAN_MEMBER;

		/* Delete VLAN from switch if it is no longer configured on
		 * any port
		 */
		for (i = 0; i < ethsw->sw_attr.num_ifs; i++)
			if (ethsw->ports[i]->vlans[vid] & ETHSW_VLAN_MEMBER)
				return 0; /* Found a port member in VID */

		ethsw->vlans[vid] &= ~ETHSW_VLAN_GLOBAL;

		err = ethsw_dellink_switch(ethsw, vid);
		if (err)
			return err;
	}

	return 0;
}

static int port_vlans_del(struct net_device *netdev,
			  const struct switchdev_obj_port_vlan *vlan)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int vid, err;

	if (netif_is_bridge_master(vlan->obj.orig_dev))
		return -EOPNOTSUPP;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++) {
		err = ethsw_port_del_vlan(port_priv, vid);
		if (err)
			break;
	}

	return err;
}

static int port_mdb_del(struct net_device *netdev,
			const struct switchdev_obj_port_mdb *mdb)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	if (!port_lookup_address(netdev, 0, mdb->addr))
		return -ENOENT;

	err = ethsw_port_fdb_del_mc(port_priv, mdb->addr);
	if (err)
		return err;

	err = dev_mc_del(netdev, mdb->addr);
	if (err) {
		netdev_err(netdev, "dev_mc_del err %d\n", err);
		return err;
	}

	return err;
}

static int swdev_port_obj_del(struct net_device *netdev,
			      const struct switchdev_obj *obj)
{
	int err;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = port_vlans_del(netdev, SWITCHDEV_OBJ_PORT_VLAN(obj));
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = port_mdb_del(netdev, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
	return err;
}

static const struct switchdev_ops ethsw_port_switchdev_ops = {
	.switchdev_port_attr_get	= swdev_port_attr_get,
	.switchdev_port_attr_set	= swdev_port_attr_set,
	.switchdev_port_obj_add		= swdev_port_obj_add,
	.switchdev_port_obj_del		= swdev_port_obj_del,
};

/* For the moment, only flood setting needs to be updated */
static int port_bridge_join(struct net_device *netdev,
			    struct net_device *upper_dev)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	int i, err;

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++)
		if (ethsw->ports[i]->bridge_dev &&
		    (ethsw->ports[i]->bridge_dev != upper_dev)) {
			netdev_err(netdev,
				   "Another switch port is connected to %s\n",
				   ethsw->ports[i]->bridge_dev->name);
			return -EINVAL;
		}

	/* Enable flooding */
	err = ethsw_port_set_flood(port_priv, 1);
	if (!err)
		port_priv->bridge_dev = upper_dev;

	return err;
}

static int port_bridge_leave(struct net_device *netdev)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	/* Disable flooding */
	err = ethsw_port_set_flood(port_priv, 0);
	if (!err)
		port_priv->bridge_dev = NULL;

	return err;
}

static int port_netdevice_event(struct notifier_block *unused,
				unsigned long event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_changeupper_info *info = ptr;
	struct net_device *upper_dev;
	int err = 0;

	if (netdev->netdev_ops != &ethsw_port_ops)
		return NOTIFY_DONE;

	/* Handle just upper dev link/unlink for the moment */
	if (event == NETDEV_CHANGEUPPER) {
		upper_dev = info->upper_dev;
		if (netif_is_bridge_master(upper_dev)) {
			if (info->linking)
				err = port_bridge_join(netdev, upper_dev);
			else
				err = port_bridge_leave(netdev);
		}
	}

	return notifier_from_errno(err);
}

static struct notifier_block port_nb __read_mostly = {
	.notifier_call = port_netdevice_event,
};

struct ethsw_switchdev_event_work {
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct net_device *dev;
	unsigned long event;
};

static void ethsw_switchdev_event_work(struct work_struct *work)
{
	struct ethsw_switchdev_event_work *switchdev_work =
		container_of(work, struct ethsw_switchdev_event_work, work);
	struct net_device *dev = switchdev_work->dev;
	struct switchdev_notifier_fdb_info *fdb_info;
	struct ethsw_port_priv *port_priv;

	rtnl_lock();
	port_priv = netdev_priv(dev);
	fdb_info = &switchdev_work->fdb_info;

	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		if (is_unicast_ether_addr(fdb_info->addr))
			ethsw_port_fdb_add_uc(netdev_priv(dev), fdb_info->addr);
		else
			ethsw_port_fdb_add_mc(netdev_priv(dev), fdb_info->addr);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		if (is_unicast_ether_addr(fdb_info->addr))
			ethsw_port_fdb_del_uc(netdev_priv(dev), fdb_info->addr);
		else
			ethsw_port_fdb_del_mc(netdev_priv(dev), fdb_info->addr);
		break;
	}

	rtnl_unlock();
	kfree(switchdev_work->fdb_info.addr);
	kfree(switchdev_work);
	dev_put(dev);
}

/* Called under rcu_read_lock() */
static int port_switchdev_event(struct notifier_block *unused,
				unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	struct ethsw_switchdev_event_work *switchdev_work;
	struct switchdev_notifier_fdb_info *fdb_info = ptr;

	switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);
	if (!switchdev_work)
		return NOTIFY_BAD;

	INIT_WORK(&switchdev_work->work, ethsw_switchdev_event_work);
	switchdev_work->dev = dev;
	switchdev_work->event = event;

	switch (event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		memcpy(&switchdev_work->fdb_info, ptr,
		       sizeof(switchdev_work->fdb_info));
		switchdev_work->fdb_info.addr = kzalloc(ETH_ALEN, GFP_ATOMIC);
		if (!switchdev_work->fdb_info.addr)
			goto err_addr_alloc;

		ether_addr_copy((u8 *)switchdev_work->fdb_info.addr,
				fdb_info->addr);

		/* Take a reference on the device to avoid being freed. */
		dev_hold(dev);
		break;
	default:
		return NOTIFY_DONE;
	}

	queue_work(ethsw_owq, &switchdev_work->work);

	return NOTIFY_DONE;

err_addr_alloc:
	kfree(switchdev_work);
	return NOTIFY_BAD;
}

static struct notifier_block port_switchdev_nb = {
	.notifier_call = port_switchdev_event,
};

static int ethsw_register_notifier(struct device *dev)
{
	int err;

	err = register_netdevice_notifier(&port_nb);
	if (err) {
		dev_err(dev, "Failed to register netdev notifier\n");
		return err;
	}

	err = register_switchdev_notifier(&port_switchdev_nb);
	if (err) {
		dev_err(dev, "Failed to register switchdev notifier\n");
		goto err_switchdev_nb;
	}

	return 0;

err_switchdev_nb:
	unregister_netdevice_notifier(&port_nb);
	return err;
}

static int ethsw_open(struct ethsw_core *ethsw)
{
	struct ethsw_port_priv *port_priv = NULL;
	int i, err;

	err = dpsw_enable(ethsw->mc_io, 0, ethsw->dpsw_handle);
	if (err) {
		dev_err(ethsw->dev, "dpsw_enable err %d\n", err);
		return err;
	}

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++) {
		port_priv = ethsw->ports[i];
		err = dev_open(port_priv->netdev);
		if (err) {
			netdev_err(port_priv->netdev, "dev_open err %d\n", err);
			return err;
		}
	}

	return 0;
}

static int ethsw_stop(struct ethsw_core *ethsw)
{
	struct ethsw_port_priv *port_priv = NULL;
	int i, err;

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++) {
		port_priv = ethsw->ports[i];
		dev_close(port_priv->netdev);
	}

	err = dpsw_disable(ethsw->mc_io, 0, ethsw->dpsw_handle);
	if (err) {
		dev_err(ethsw->dev, "dpsw_disable err %d\n", err);
		return err;
	}

	return 0;
}

static int ethsw_init(struct fsl_mc_device *sw_dev)
{
	struct device *dev = &sw_dev->dev;
	struct ethsw_core *ethsw = dev_get_drvdata(dev);
	u16 version_major, version_minor, i;
	struct dpsw_stp_cfg stp_cfg;
	int err;

	ethsw->dev_id = sw_dev->obj_desc.id;

	err = dpsw_open(ethsw->mc_io, 0, ethsw->dev_id, &ethsw->dpsw_handle);
	if (err) {
		dev_err(dev, "dpsw_open err %d\n", err);
		return err;
	}

	err = dpsw_get_attributes(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  &ethsw->sw_attr);
	if (err) {
		dev_err(dev, "dpsw_get_attributes err %d\n", err);
		goto err_close;
	}

	err = dpsw_get_api_version(ethsw->mc_io, 0,
				   &version_major,
				   &version_minor);
	if (err) {
		dev_err(dev, "dpsw_get_api_version err %d\n", err);
		goto err_close;
	}

	/* Minimum supported DPSW version check */
	if (version_major < DPSW_MIN_VER_MAJOR ||
	    (version_major == DPSW_MIN_VER_MAJOR &&
	     version_minor < DPSW_MIN_VER_MINOR)) {
		dev_err(dev, "DPSW version %d:%d not supported. Use %d.%d or greater.\n",
			version_major,
			version_minor,
			DPSW_MIN_VER_MAJOR, DPSW_MIN_VER_MINOR);
		err = -ENOTSUPP;
		goto err_close;
	}

	err = dpsw_reset(ethsw->mc_io, 0, ethsw->dpsw_handle);
	if (err) {
		dev_err(dev, "dpsw_reset err %d\n", err);
		goto err_close;
	}

	err = dpsw_fdb_set_learning_mode(ethsw->mc_io, 0, ethsw->dpsw_handle, 0,
					 DPSW_FDB_LEARNING_MODE_HW);
	if (err) {
		dev_err(dev, "dpsw_fdb_set_learning_mode err %d\n", err);
		goto err_close;
	}

	stp_cfg.vlan_id = DEFAULT_VLAN_ID;
	stp_cfg.state = DPSW_STP_STATE_FORWARDING;

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++) {
		err = dpsw_if_set_stp(ethsw->mc_io, 0, ethsw->dpsw_handle, i,
				      &stp_cfg);
		if (err) {
			dev_err(dev, "dpsw_if_set_stp err %d for port %d\n",
				err, i);
			goto err_close;
		}

		err = dpsw_if_set_broadcast(ethsw->mc_io, 0,
					    ethsw->dpsw_handle, i, 1);
		if (err) {
			dev_err(dev,
				"dpsw_if_set_broadcast err %d for port %d\n",
				err, i);
			goto err_close;
		}
	}

	ethsw_owq = alloc_ordered_workqueue("%s_ordered", WQ_MEM_RECLAIM,
					    "ethsw");
	if (!ethsw_owq) {
		err = -ENOMEM;
		goto err_close;
	}

	err = ethsw_register_notifier(dev);
	if (err)
		goto err_destroy_ordered_workqueue;

	return 0;

err_destroy_ordered_workqueue:
	destroy_workqueue(ethsw_owq);

err_close:
	dpsw_close(ethsw->mc_io, 0, ethsw->dpsw_handle);
	return err;
}

static int ethsw_port_init(struct ethsw_port_priv *port_priv, u16 port)
{
	const char def_mcast[ETH_ALEN] = {0x01, 0x00, 0x5e, 0x00, 0x00, 0x01};
	struct net_device *netdev = port_priv->netdev;
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct dpsw_vlan_if_cfg vcfg;
	int err;

	/* Switch starts with all ports configured to VLAN 1. Need to
	 * remove this setting to allow configuration at bridge join
	 */
	vcfg.num_ifs = 1;
	vcfg.if_id[0] = port_priv->idx;

	err = dpsw_vlan_remove_if_untagged(ethsw->mc_io, 0, ethsw->dpsw_handle,
					   DEFAULT_VLAN_ID, &vcfg);
	if (err) {
		netdev_err(netdev, "dpsw_vlan_remove_if_untagged err %d\n",
			   err);
		return err;
	}

	err = ethsw_port_set_pvid(port_priv, 0);
	if (err)
		return err;

	err = dpsw_vlan_remove_if(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  DEFAULT_VLAN_ID, &vcfg);
	if (err) {
		netdev_err(netdev, "dpsw_vlan_remove_if err %d\n", err);
		return err;
	}

	err = ethsw_port_fdb_add_mc(port_priv, def_mcast);

	return err;
}

static void ethsw_unregister_notifier(struct device *dev)
{
	int err;

	err = unregister_switchdev_notifier(&port_switchdev_nb);
	if (err)
		dev_err(dev,
			"Failed to unregister switchdev notifier (%d)\n", err);

	err = unregister_netdevice_notifier(&port_nb);
	if (err)
		dev_err(dev,
			"Failed to unregister netdev notifier (%d)\n", err);
}

static void ethsw_takedown(struct fsl_mc_device *sw_dev)
{
	struct device *dev = &sw_dev->dev;
	struct ethsw_core *ethsw = dev_get_drvdata(dev);
	int err;

	ethsw_unregister_notifier(dev);

	err = dpsw_close(ethsw->mc_io, 0, ethsw->dpsw_handle);
	if (err)
		dev_warn(dev, "dpsw_close err %d\n", err);
}

static int ethsw_remove(struct fsl_mc_device *sw_dev)
{
	struct ethsw_port_priv *port_priv;
	struct ethsw_core *ethsw;
	struct device *dev;
	int i;

	dev = &sw_dev->dev;
	ethsw = dev_get_drvdata(dev);

	ethsw_teardown_irqs(sw_dev);

	destroy_workqueue(ethsw_owq);

	rtnl_lock();
	ethsw_stop(ethsw);
	rtnl_unlock();

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++) {
		port_priv = ethsw->ports[i];
		unregister_netdev(port_priv->netdev);
		free_netdev(port_priv->netdev);
	}
	kfree(ethsw->ports);

	ethsw_takedown(sw_dev);
	fsl_mc_portal_free(ethsw->mc_io);

	kfree(ethsw);

	dev_set_drvdata(dev, NULL);

	return 0;
}

static int ethsw_probe_port(struct ethsw_core *ethsw, u16 port_idx)
{
	struct ethsw_port_priv *port_priv;
	struct device *dev = ethsw->dev;
	struct net_device *port_netdev;
	int err;

	port_netdev = alloc_etherdev(sizeof(struct ethsw_port_priv));
	if (!port_netdev) {
		dev_err(dev, "alloc_etherdev error\n");
		return -ENOMEM;
	}

	port_priv = netdev_priv(port_netdev);
	port_priv->netdev = port_netdev;
	port_priv->ethsw_data = ethsw;

	port_priv->idx = port_idx;
	port_priv->stp_state = BR_STATE_FORWARDING;

	/* Flooding is implicitly enabled */
	port_priv->flood = true;

	SET_NETDEV_DEV(port_netdev, dev);
	port_netdev->netdev_ops = &ethsw_port_ops;
	port_netdev->ethtool_ops = &ethsw_port_ethtool_ops;
	port_netdev->switchdev_ops = &ethsw_port_switchdev_ops;

	/* Set MTU limits */
	port_netdev->min_mtu = ETH_MIN_MTU;
	port_netdev->max_mtu = ETHSW_MAX_FRAME_LENGTH;

	err = register_netdev(port_netdev);
	if (err < 0) {
		dev_err(dev, "register_netdev error %d\n", err);
		free_netdev(port_netdev);
		return err;
	}

	ethsw->ports[port_idx] = port_priv;

	return ethsw_port_init(port_priv, port_idx);
}

static int ethsw_probe(struct fsl_mc_device *sw_dev)
{
	struct device *dev = &sw_dev->dev;
	struct ethsw_core *ethsw;
	int i, err;

	/* Allocate switch core*/
	ethsw = kzalloc(sizeof(*ethsw), GFP_KERNEL);

	if (!ethsw)
		return -ENOMEM;

	ethsw->dev = dev;
	dev_set_drvdata(dev, ethsw);

	err = fsl_mc_portal_allocate(sw_dev, 0, &ethsw->mc_io);
	if (err) {
		if (err == -ENXIO)
			err = -EPROBE_DEFER;
		else
			dev_err(dev, "fsl_mc_portal_allocate err %d\n", err);
		goto err_free_drvdata;
	}

	err = ethsw_init(sw_dev);
	if (err)
		goto err_free_cmdport;

	/* DEFAULT_VLAN_ID is implicitly configured on the switch */
	ethsw->vlans[DEFAULT_VLAN_ID] = ETHSW_VLAN_MEMBER;

	/* Learning is implicitly enabled */
	ethsw->learning = true;

	ethsw->ports = kcalloc(ethsw->sw_attr.num_ifs, sizeof(*ethsw->ports),
			       GFP_KERNEL);
	if (!(ethsw->ports)) {
		err = -ENOMEM;
		goto err_takedown;
	}

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++) {
		err = ethsw_probe_port(ethsw, i);
		if (err)
			goto err_free_ports;
	}

	/* Switch starts up enabled */
	rtnl_lock();
	err = ethsw_open(ethsw);
	rtnl_unlock();
	if (err)
		goto err_free_ports;

	/* Setup IRQs */
	err = ethsw_setup_irqs(sw_dev);
	if (err)
		goto err_stop;

	dev_info(dev, "probed %d port switch\n", ethsw->sw_attr.num_ifs);
	return 0;

err_stop:
	rtnl_lock();
	ethsw_stop(ethsw);
	rtnl_unlock();

err_free_ports:
	/* Cleanup registered ports only */
	for (i--; i >= 0; i--) {
		unregister_netdev(ethsw->ports[i]->netdev);
		free_netdev(ethsw->ports[i]->netdev);
	}
	kfree(ethsw->ports);

err_takedown:
	ethsw_takedown(sw_dev);

err_free_cmdport:
	fsl_mc_portal_free(ethsw->mc_io);

err_free_drvdata:
	kfree(ethsw);
	dev_set_drvdata(dev, NULL);

	return err;
}

static const struct fsl_mc_device_id ethsw_match_id_table[] = {
	{
		.vendor = FSL_MC_VENDOR_FREESCALE,
		.obj_type = "dpsw",
	},
	{ .vendor = 0x0 }
};
MODULE_DEVICE_TABLE(fslmc, ethsw_match_id_table);

static struct fsl_mc_driver eth_sw_drv = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
	},
	.probe = ethsw_probe,
	.remove = ethsw_remove,
	.match_id_table = ethsw_match_id_table
};

module_fsl_mc_driver(eth_sw_drv);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DPAA2 Ethernet Switch Driver");
