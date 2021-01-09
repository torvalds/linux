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

/* Minimal supported DPSW version */
#define DPSW_MIN_VER_MAJOR		8
#define DPSW_MIN_VER_MINOR		1

#define DEFAULT_VLAN_ID			1

static int dpaa2_switch_add_vlan(struct ethsw_core *ethsw, u16 vid)
{
	int err;

	struct dpsw_vlan_cfg	vcfg = {
		.fdb_id = 0,
	};

	err = dpsw_vlan_add(ethsw->mc_io, 0,
			    ethsw->dpsw_handle, vid, &vcfg);
	if (err) {
		dev_err(ethsw->dev, "dpsw_vlan_add err %d\n", err);
		return err;
	}
	ethsw->vlans[vid] = ETHSW_VLAN_MEMBER;

	return 0;
}

static bool dpaa2_switch_port_is_up(struct ethsw_port_priv *port_priv)
{
	struct net_device *netdev = port_priv->netdev;
	struct dpsw_link_state state;
	int err;

	err = dpsw_if_get_link_state(port_priv->ethsw_data->mc_io, 0,
				     port_priv->ethsw_data->dpsw_handle,
				     port_priv->idx, &state);
	if (err) {
		netdev_err(netdev, "dpsw_if_get_link_state() err %d\n", err);
		return true;
	}

	WARN_ONCE(state.up > 1, "Garbage read into link_state");

	return state.up ? true : false;
}

static int dpaa2_switch_port_set_pvid(struct ethsw_port_priv *port_priv, u16 pvid)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct net_device *netdev = port_priv->netdev;
	struct dpsw_tci_cfg tci_cfg = { 0 };
	bool up;
	int err, ret;

	err = dpsw_if_get_tci(ethsw->mc_io, 0, ethsw->dpsw_handle,
			      port_priv->idx, &tci_cfg);
	if (err) {
		netdev_err(netdev, "dpsw_if_get_tci err %d\n", err);
		return err;
	}

	tci_cfg.vlan_id = pvid;

	/* Interface needs to be down to change PVID */
	up = dpaa2_switch_port_is_up(port_priv);
	if (up) {
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
	if (up) {
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

static int dpaa2_switch_port_add_vlan(struct ethsw_port_priv *port_priv,
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
		err = dpaa2_switch_port_set_pvid(port_priv, vid);
		if (err)
			return err;
	}

	return 0;
}

static int dpaa2_switch_set_learning(struct ethsw_core *ethsw, bool enable)
{
	enum dpsw_fdb_learning_mode learn_mode;
	int err;

	if (enable)
		learn_mode = DPSW_FDB_LEARNING_MODE_HW;
	else
		learn_mode = DPSW_FDB_LEARNING_MODE_DIS;

	err = dpsw_fdb_set_learning_mode(ethsw->mc_io, 0, ethsw->dpsw_handle, 0,
					 learn_mode);
	if (err) {
		dev_err(ethsw->dev, "dpsw_fdb_set_learning_mode err %d\n", err);
		return err;
	}
	ethsw->learning = enable;

	return 0;
}

static int dpaa2_switch_port_set_flood(struct ethsw_port_priv *port_priv, bool enable)
{
	int err;

	err = dpsw_if_set_flooding(port_priv->ethsw_data->mc_io, 0,
				   port_priv->ethsw_data->dpsw_handle,
				   port_priv->idx, enable);
	if (err) {
		netdev_err(port_priv->netdev,
			   "dpsw_if_set_flooding err %d\n", err);
		return err;
	}
	port_priv->flood = enable;

	return 0;
}

static int dpaa2_switch_port_set_stp_state(struct ethsw_port_priv *port_priv, u8 state)
{
	struct dpsw_stp_cfg stp_cfg = {
		.state = state,
	};
	int err;
	u16 vid;

	if (!netif_running(port_priv->netdev) || state == port_priv->stp_state)
		return 0;	/* Nothing to do */

	for (vid = 0; vid <= VLAN_VID_MASK; vid++) {
		if (port_priv->vlans[vid] & ETHSW_VLAN_MEMBER) {
			stp_cfg.vlan_id = vid;
			err = dpsw_if_set_stp(port_priv->ethsw_data->mc_io, 0,
					      port_priv->ethsw_data->dpsw_handle,
					      port_priv->idx, &stp_cfg);
			if (err) {
				netdev_err(port_priv->netdev,
					   "dpsw_if_set_stp err %d\n", err);
				return err;
			}
		}
	}

	port_priv->stp_state = state;

	return 0;
}

static int dpaa2_switch_dellink(struct ethsw_core *ethsw, u16 vid)
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

static int dpaa2_switch_port_fdb_add_uc(struct ethsw_port_priv *port_priv,
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

static int dpaa2_switch_port_fdb_del_uc(struct ethsw_port_priv *port_priv,
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

static int dpaa2_switch_port_fdb_add_mc(struct ethsw_port_priv *port_priv,
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

static int dpaa2_switch_port_fdb_del_mc(struct ethsw_port_priv *port_priv,
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

static int dpaa2_switch_port_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
				     struct net_device *dev, const unsigned char *addr,
				     u16 vid, u16 flags,
				     struct netlink_ext_ack *extack)
{
	if (is_unicast_ether_addr(addr))
		return dpaa2_switch_port_fdb_add_uc(netdev_priv(dev),
						    addr);
	else
		return dpaa2_switch_port_fdb_add_mc(netdev_priv(dev),
						    addr);
}

static int dpaa2_switch_port_fdb_del(struct ndmsg *ndm, struct nlattr *tb[],
				     struct net_device *dev,
				     const unsigned char *addr, u16 vid)
{
	if (is_unicast_ether_addr(addr))
		return dpaa2_switch_port_fdb_del_uc(netdev_priv(dev),
						    addr);
	else
		return dpaa2_switch_port_fdb_del_mc(netdev_priv(dev),
						    addr);
}

static void dpaa2_switch_port_get_stats(struct net_device *netdev,
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

static bool dpaa2_switch_port_has_offload_stats(const struct net_device *netdev,
						int attr_id)
{
	return (attr_id == IFLA_OFFLOAD_XSTATS_CPU_HIT);
}

static int dpaa2_switch_port_get_offload_stats(int attr_id,
					       const struct net_device *netdev,
					       void *sp)
{
	switch (attr_id) {
	case IFLA_OFFLOAD_XSTATS_CPU_HIT:
		dpaa2_switch_port_get_stats((struct net_device *)netdev, sp);
		return 0;
	}

	return -EINVAL;
}

static int dpaa2_switch_port_change_mtu(struct net_device *netdev, int mtu)
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

static int dpaa2_switch_port_carrier_state_sync(struct net_device *netdev)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct dpsw_link_state state;
	int err;

	/* Interrupts are received even though no one issued an 'ifconfig up'
	 * on the switch interface. Ignore these link state update interrupts
	 */
	if (!netif_running(netdev))
		return 0;

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

static int dpaa2_switch_port_open(struct net_device *netdev)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	/* No need to allow Tx as control interface is disabled */
	netif_tx_stop_all_queues(netdev);

	/* Explicitly set carrier off, otherwise
	 * netif_carrier_ok() will return true and cause 'ip link show'
	 * to report the LOWER_UP flag, even though the link
	 * notification wasn't even received.
	 */
	netif_carrier_off(netdev);

	err = dpsw_if_enable(port_priv->ethsw_data->mc_io, 0,
			     port_priv->ethsw_data->dpsw_handle,
			     port_priv->idx);
	if (err) {
		netdev_err(netdev, "dpsw_if_enable err %d\n", err);
		return err;
	}

	/* sync carrier state */
	err = dpaa2_switch_port_carrier_state_sync(netdev);
	if (err) {
		netdev_err(netdev,
			   "dpaa2_switch_port_carrier_state_sync err %d\n", err);
		goto err_carrier_sync;
	}

	return 0;

err_carrier_sync:
	dpsw_if_disable(port_priv->ethsw_data->mc_io, 0,
			port_priv->ethsw_data->dpsw_handle,
			port_priv->idx);
	return err;
}

static int dpaa2_switch_port_stop(struct net_device *netdev)
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

static netdev_tx_t dpaa2_switch_port_dropframe(struct sk_buff *skb,
					       struct net_device *netdev)
{
	/* we don't support I/O for now, drop the frame */
	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static int dpaa2_switch_port_parent_id(struct net_device *dev,
				       struct netdev_phys_item_id *ppid)
{
	struct ethsw_port_priv *port_priv = netdev_priv(dev);

	ppid->id_len = 1;
	ppid->id[0] = port_priv->ethsw_data->dev_id;

	return 0;
}

static int dpaa2_switch_port_get_phys_name(struct net_device *netdev, char *name,
					   size_t len)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	err = snprintf(name, len, "p%d", port_priv->idx);
	if (err >= len)
		return -EINVAL;

	return 0;
}

struct ethsw_dump_ctx {
	struct net_device *dev;
	struct sk_buff *skb;
	struct netlink_callback *cb;
	int idx;
};

static int dpaa2_switch_fdb_dump_nl(struct fdb_dump_entry *entry,
				    struct ethsw_dump_ctx *dump)
{
	int is_dynamic = entry->type & DPSW_FDB_ENTRY_DINAMIC;
	u32 portid = NETLINK_CB(dump->cb->skb).portid;
	u32 seq = dump->cb->nlh->nlmsg_seq;
	struct nlmsghdr *nlh;
	struct ndmsg *ndm;

	if (dump->idx < dump->cb->args[2])
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
	ndm->ndm_state   = is_dynamic ? NUD_REACHABLE : NUD_NOARP;

	if (nla_put(dump->skb, NDA_LLADDR, ETH_ALEN, entry->mac_addr))
		goto nla_put_failure;

	nlmsg_end(dump->skb, nlh);

skip:
	dump->idx++;
	return 0;

nla_put_failure:
	nlmsg_cancel(dump->skb, nlh);
	return -EMSGSIZE;
}

static int dpaa2_switch_port_fdb_valid_entry(struct fdb_dump_entry *entry,
					     struct ethsw_port_priv *port_priv)
{
	int idx = port_priv->idx;
	int valid;

	if (entry->type & DPSW_FDB_ENTRY_TYPE_UNICAST)
		valid = entry->if_info == port_priv->idx;
	else
		valid = entry->if_mask[idx / 8] & BIT(idx % 8);

	return valid;
}

static int dpaa2_switch_port_fdb_dump(struct sk_buff *skb, struct netlink_callback *cb,
				      struct net_device *net_dev,
				      struct net_device *filter_dev, int *idx)
{
	struct ethsw_port_priv *port_priv = netdev_priv(net_dev);
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct device *dev = net_dev->dev.parent;
	struct fdb_dump_entry *fdb_entries;
	struct fdb_dump_entry fdb_entry;
	struct ethsw_dump_ctx dump = {
		.dev = net_dev,
		.skb = skb,
		.cb = cb,
		.idx = *idx,
	};
	dma_addr_t fdb_dump_iova;
	u16 num_fdb_entries;
	u32 fdb_dump_size;
	int err = 0, i;
	u8 *dma_mem;

	fdb_dump_size = ethsw->sw_attr.max_fdb_entries * sizeof(fdb_entry);
	dma_mem = kzalloc(fdb_dump_size, GFP_KERNEL);
	if (!dma_mem)
		return -ENOMEM;

	fdb_dump_iova = dma_map_single(dev, dma_mem, fdb_dump_size,
				       DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, fdb_dump_iova)) {
		netdev_err(net_dev, "dma_map_single() failed\n");
		err = -ENOMEM;
		goto err_map;
	}

	err = dpsw_fdb_dump(ethsw->mc_io, 0, ethsw->dpsw_handle, 0,
			    fdb_dump_iova, fdb_dump_size, &num_fdb_entries);
	if (err) {
		netdev_err(net_dev, "dpsw_fdb_dump() = %d\n", err);
		goto err_dump;
	}

	dma_unmap_single(dev, fdb_dump_iova, fdb_dump_size, DMA_FROM_DEVICE);

	fdb_entries = (struct fdb_dump_entry *)dma_mem;
	for (i = 0; i < num_fdb_entries; i++) {
		fdb_entry = fdb_entries[i];

		if (!dpaa2_switch_port_fdb_valid_entry(&fdb_entry, port_priv))
			continue;

		err = dpaa2_switch_fdb_dump_nl(&fdb_entry, &dump);
		if (err)
			goto end;
	}

end:
	*idx = dump.idx;

	kfree(dma_mem);

	return 0;

err_dump:
	dma_unmap_single(dev, fdb_dump_iova, fdb_dump_size, DMA_TO_DEVICE);
err_map:
	kfree(dma_mem);
	return err;
}

static int dpaa2_switch_port_set_mac_addr(struct ethsw_port_priv *port_priv)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct net_device *net_dev = port_priv->netdev;
	struct device *dev = net_dev->dev.parent;
	u8 mac_addr[ETH_ALEN];
	int err;

	if (!(ethsw->features & ETHSW_FEATURE_MAC_ADDR))
		return 0;

	/* Get firmware address, if any */
	err = dpsw_if_get_port_mac_addr(ethsw->mc_io, 0, ethsw->dpsw_handle,
					port_priv->idx, mac_addr);
	if (err) {
		dev_err(dev, "dpsw_if_get_port_mac_addr() failed\n");
		return err;
	}

	/* First check if firmware has any address configured by bootloader */
	if (!is_zero_ether_addr(mac_addr)) {
		memcpy(net_dev->dev_addr, mac_addr, net_dev->addr_len);
	} else {
		/* No MAC address configured, fill in net_dev->dev_addr
		 * with a random one
		 */
		eth_hw_addr_random(net_dev);
		dev_dbg_once(dev, "device(s) have all-zero hwaddr, replaced with random\n");

		/* Override NET_ADDR_RANDOM set by eth_hw_addr_random(); for all
		 * practical purposes, this will be our "permanent" mac address,
		 * at least until the next reboot. This move will also permit
		 * register_netdevice() to properly fill up net_dev->perm_addr.
		 */
		net_dev->addr_assign_type = NET_ADDR_PERM;
	}

	return 0;
}

static const struct net_device_ops dpaa2_switch_port_ops = {
	.ndo_open		= dpaa2_switch_port_open,
	.ndo_stop		= dpaa2_switch_port_stop,

	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_get_stats64	= dpaa2_switch_port_get_stats,
	.ndo_change_mtu		= dpaa2_switch_port_change_mtu,
	.ndo_has_offload_stats	= dpaa2_switch_port_has_offload_stats,
	.ndo_get_offload_stats	= dpaa2_switch_port_get_offload_stats,
	.ndo_fdb_add		= dpaa2_switch_port_fdb_add,
	.ndo_fdb_del		= dpaa2_switch_port_fdb_del,
	.ndo_fdb_dump		= dpaa2_switch_port_fdb_dump,

	.ndo_start_xmit		= dpaa2_switch_port_dropframe,
	.ndo_get_port_parent_id	= dpaa2_switch_port_parent_id,
	.ndo_get_phys_port_name = dpaa2_switch_port_get_phys_name,
};

static bool dpaa2_switch_port_dev_check(const struct net_device *netdev,
					struct notifier_block *nb)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);

	if (netdev->netdev_ops == &dpaa2_switch_port_ops &&
	    (!nb || &port_priv->ethsw_data->port_nb == nb ||
	     &port_priv->ethsw_data->port_switchdev_nb == nb ||
	     &port_priv->ethsw_data->port_switchdevb_nb == nb))
		return true;

	return false;
}

static void dpaa2_switch_links_state_update(struct ethsw_core *ethsw)
{
	int i;

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++) {
		dpaa2_switch_port_carrier_state_sync(ethsw->ports[i]->netdev);
		dpaa2_switch_port_set_mac_addr(ethsw->ports[i]);
	}
}

static irqreturn_t dpaa2_switch_irq0_handler_thread(int irq_num, void *arg)
{
	struct device *dev = (struct device *)arg;
	struct ethsw_core *ethsw = dev_get_drvdata(dev);

	/* Mask the events and the if_id reserved bits to be cleared on read */
	u32 status = DPSW_IRQ_EVENT_LINK_CHANGED | 0xFFFF0000;
	int err;

	err = dpsw_get_irq_status(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  DPSW_IRQ_INDEX_IF, &status);
	if (err) {
		dev_err(dev, "Can't get irq status (err %d)\n", err);

		err = dpsw_clear_irq_status(ethsw->mc_io, 0, ethsw->dpsw_handle,
					    DPSW_IRQ_INDEX_IF, 0xFFFFFFFF);
		if (err)
			dev_err(dev, "Can't clear irq status (err %d)\n", err);
		goto out;
	}

	if (status & DPSW_IRQ_EVENT_LINK_CHANGED)
		dpaa2_switch_links_state_update(ethsw);

out:
	return IRQ_HANDLED;
}

static int dpaa2_switch_setup_irqs(struct fsl_mc_device *sw_dev)
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
					dpaa2_switch_irq0_handler_thread,
					IRQF_NO_SUSPEND | IRQF_ONESHOT,
					dev_name(dev), dev);
	if (err) {
		dev_err(dev, "devm_request_threaded_irq(): %d\n", err);
		goto free_irq;
	}

	err = dpsw_set_irq_mask(ethsw->mc_io, 0, ethsw->dpsw_handle,
				DPSW_IRQ_INDEX_IF, mask);
	if (err) {
		dev_err(dev, "dpsw_set_irq_mask(): %d\n", err);
		goto free_devm_irq;
	}

	err = dpsw_set_irq_enable(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  DPSW_IRQ_INDEX_IF, 1);
	if (err) {
		dev_err(dev, "dpsw_set_irq_enable(): %d\n", err);
		goto free_devm_irq;
	}

	return 0;

free_devm_irq:
	devm_free_irq(dev, irq->msi_desc->irq, dev);
free_irq:
	fsl_mc_free_irqs(sw_dev);
	return err;
}

static void dpaa2_switch_teardown_irqs(struct fsl_mc_device *sw_dev)
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

static int dpaa2_switch_port_attr_stp_state_set(struct net_device *netdev,
						struct switchdev_trans *trans,
						u8 state)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);

	if (switchdev_trans_ph_prepare(trans))
		return 0;

	return dpaa2_switch_port_set_stp_state(port_priv, state);
}

static int dpaa2_switch_port_attr_br_flags_pre_set(struct net_device *netdev,
						   struct switchdev_trans *trans,
						   unsigned long flags)
{
	if (flags & ~(BR_LEARNING | BR_FLOOD))
		return -EINVAL;

	return 0;
}

static int dpaa2_switch_port_attr_br_flags_set(struct net_device *netdev,
					       struct switchdev_trans *trans,
					       unsigned long flags)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err = 0;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

	/* Learning is enabled per switch */
	err = dpaa2_switch_set_learning(port_priv->ethsw_data,
					!!(flags & BR_LEARNING));
	if (err)
		goto exit;

	err = dpaa2_switch_port_set_flood(port_priv, !!(flags & BR_FLOOD));

exit:
	return err;
}

static int dpaa2_switch_port_attr_set(struct net_device *netdev,
				      const struct switchdev_attr *attr,
				      struct switchdev_trans *trans)
{
	int err = 0;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		err = dpaa2_switch_port_attr_stp_state_set(netdev, trans,
							   attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_PORT_PRE_BRIDGE_FLAGS:
		err = dpaa2_switch_port_attr_br_flags_pre_set(netdev, trans,
							      attr->u.brport_flags);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		err = dpaa2_switch_port_attr_br_flags_set(netdev, trans,
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

static int dpaa2_switch_port_vlans_add(struct net_device *netdev,
				       const struct switchdev_obj_port_vlan *vlan,
				       struct switchdev_trans *trans)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct dpsw_attr *attr = &ethsw->sw_attr;
	int err = 0;

	if (switchdev_trans_ph_prepare(trans)) {
		/* Make sure that the VLAN is not already configured
		 * on the switch port
		 */
		if (port_priv->vlans[vlan->vid] & ETHSW_VLAN_MEMBER)
			return -EEXIST;

		/* Check if there is space for a new VLAN */
		err = dpsw_get_attributes(ethsw->mc_io, 0, ethsw->dpsw_handle,
					  &ethsw->sw_attr);
		if (err) {
			netdev_err(netdev, "dpsw_get_attributes err %d\n", err);
			return err;
		}
		if (attr->max_vlans - attr->num_vlans < 1)
			return -ENOSPC;

		return 0;
	}

	if (!port_priv->ethsw_data->vlans[vlan->vid]) {
		/* this is a new VLAN */
		err = dpaa2_switch_add_vlan(port_priv->ethsw_data, vlan->vid);
		if (err)
			return err;

		port_priv->ethsw_data->vlans[vlan->vid] |= ETHSW_VLAN_GLOBAL;
	}

	return dpaa2_switch_port_add_vlan(port_priv, vlan->vid, vlan->flags);
}

static int dpaa2_switch_port_lookup_address(struct net_device *netdev, int is_uc,
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

static int dpaa2_switch_port_mdb_add(struct net_device *netdev,
				     const struct switchdev_obj_port_mdb *mdb,
				     struct switchdev_trans *trans)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

	/* Check if address is already set on this port */
	if (dpaa2_switch_port_lookup_address(netdev, 0, mdb->addr))
		return -EEXIST;

	err = dpaa2_switch_port_fdb_add_mc(port_priv, mdb->addr);
	if (err)
		return err;

	err = dev_mc_add(netdev, mdb->addr);
	if (err) {
		netdev_err(netdev, "dev_mc_add err %d\n", err);
		dpaa2_switch_port_fdb_del_mc(port_priv, mdb->addr);
	}

	return err;
}

static int dpaa2_switch_port_obj_add(struct net_device *netdev,
				     const struct switchdev_obj *obj,
				     struct switchdev_trans *trans)
{
	int err;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = dpaa2_switch_port_vlans_add(netdev,
						  SWITCHDEV_OBJ_PORT_VLAN(obj),
						  trans);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = dpaa2_switch_port_mdb_add(netdev,
						SWITCHDEV_OBJ_PORT_MDB(obj),
						trans);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int dpaa2_switch_port_del_vlan(struct ethsw_port_priv *port_priv, u16 vid)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct net_device *netdev = port_priv->netdev;
	struct dpsw_vlan_if_cfg vcfg;
	int i, err;

	if (!port_priv->vlans[vid])
		return -ENOENT;

	if (port_priv->vlans[vid] & ETHSW_VLAN_PVID) {
		err = dpaa2_switch_port_set_pvid(port_priv, 0);
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

		err = dpaa2_switch_dellink(ethsw, vid);
		if (err)
			return err;
	}

	return 0;
}

static int dpaa2_switch_port_vlans_del(struct net_device *netdev,
				       const struct switchdev_obj_port_vlan *vlan)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);

	if (netif_is_bridge_master(vlan->obj.orig_dev))
		return -EOPNOTSUPP;

	return dpaa2_switch_port_del_vlan(port_priv, vlan->vid);
}

static int dpaa2_switch_port_mdb_del(struct net_device *netdev,
				     const struct switchdev_obj_port_mdb *mdb)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	if (!dpaa2_switch_port_lookup_address(netdev, 0, mdb->addr))
		return -ENOENT;

	err = dpaa2_switch_port_fdb_del_mc(port_priv, mdb->addr);
	if (err)
		return err;

	err = dev_mc_del(netdev, mdb->addr);
	if (err) {
		netdev_err(netdev, "dev_mc_del err %d\n", err);
		return err;
	}

	return err;
}

static int dpaa2_switch_port_obj_del(struct net_device *netdev,
				     const struct switchdev_obj *obj)
{
	int err;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = dpaa2_switch_port_vlans_del(netdev, SWITCHDEV_OBJ_PORT_VLAN(obj));
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = dpaa2_switch_port_mdb_del(netdev, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
	return err;
}

static int dpaa2_switch_port_attr_set_event(struct net_device *netdev,
					    struct switchdev_notifier_port_attr_info
					    *port_attr_info)
{
	int err;

	err = dpaa2_switch_port_attr_set(netdev, port_attr_info->attr,
					 port_attr_info->trans);

	port_attr_info->handled = true;
	return notifier_from_errno(err);
}

/* For the moment, only flood setting needs to be updated */
static int dpaa2_switch_port_bridge_join(struct net_device *netdev,
					 struct net_device *upper_dev)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct ethsw_port_priv *other_port_priv;
	struct net_device *other_dev;
	struct list_head *iter;
	int i, err;

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++)
		if (ethsw->ports[i]->bridge_dev &&
		    (ethsw->ports[i]->bridge_dev != upper_dev)) {
			netdev_err(netdev,
				   "Only one bridge supported per DPSW object!\n");
			return -EINVAL;
		}

	netdev_for_each_lower_dev(upper_dev, other_dev, iter) {
		if (!dpaa2_switch_port_dev_check(other_dev, NULL))
			continue;

		other_port_priv = netdev_priv(other_dev);
		if (other_port_priv->ethsw_data != port_priv->ethsw_data) {
			netdev_err(netdev,
				   "Interface from a different DPSW is in the bridge already!\n");
			return -EINVAL;
		}
	}

	/* Enable flooding */
	err = dpaa2_switch_port_set_flood(port_priv, 1);
	if (!err)
		port_priv->bridge_dev = upper_dev;

	return err;
}

static int dpaa2_switch_port_bridge_leave(struct net_device *netdev)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	/* Disable flooding */
	err = dpaa2_switch_port_set_flood(port_priv, 0);
	if (!err)
		port_priv->bridge_dev = NULL;

	return err;
}

static int dpaa2_switch_port_netdevice_event(struct notifier_block *nb,
					     unsigned long event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_changeupper_info *info = ptr;
	struct net_device *upper_dev;
	int err = 0;

	if (!dpaa2_switch_port_dev_check(netdev, nb))
		return NOTIFY_DONE;

	/* Handle just upper dev link/unlink for the moment */
	if (event == NETDEV_CHANGEUPPER) {
		upper_dev = info->upper_dev;
		if (netif_is_bridge_master(upper_dev)) {
			if (info->linking)
				err = dpaa2_switch_port_bridge_join(netdev, upper_dev);
			else
				err = dpaa2_switch_port_bridge_leave(netdev);
		}
	}

	return notifier_from_errno(err);
}

struct ethsw_switchdev_event_work {
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct net_device *dev;
	unsigned long event;
};

static void dpaa2_switch_event_work(struct work_struct *work)
{
	struct ethsw_switchdev_event_work *switchdev_work =
		container_of(work, struct ethsw_switchdev_event_work, work);
	struct net_device *dev = switchdev_work->dev;
	struct switchdev_notifier_fdb_info *fdb_info;
	int err;

	rtnl_lock();
	fdb_info = &switchdev_work->fdb_info;

	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		if (!fdb_info->added_by_user)
			break;
		if (is_unicast_ether_addr(fdb_info->addr))
			err = dpaa2_switch_port_fdb_add_uc(netdev_priv(dev),
							   fdb_info->addr);
		else
			err = dpaa2_switch_port_fdb_add_mc(netdev_priv(dev),
							   fdb_info->addr);
		if (err)
			break;
		fdb_info->offloaded = true;
		call_switchdev_notifiers(SWITCHDEV_FDB_OFFLOADED, dev,
					 &fdb_info->info, NULL);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		if (!fdb_info->added_by_user)
			break;
		if (is_unicast_ether_addr(fdb_info->addr))
			dpaa2_switch_port_fdb_del_uc(netdev_priv(dev), fdb_info->addr);
		else
			dpaa2_switch_port_fdb_del_mc(netdev_priv(dev), fdb_info->addr);
		break;
	}

	rtnl_unlock();
	kfree(switchdev_work->fdb_info.addr);
	kfree(switchdev_work);
	dev_put(dev);
}

/* Called under rcu_read_lock() */
static int dpaa2_switch_port_event(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	struct ethsw_port_priv *port_priv = netdev_priv(dev);
	struct ethsw_switchdev_event_work *switchdev_work;
	struct switchdev_notifier_fdb_info *fdb_info = ptr;
	struct ethsw_core *ethsw = port_priv->ethsw_data;

	if (!dpaa2_switch_port_dev_check(dev, nb))
		return NOTIFY_DONE;

	if (event == SWITCHDEV_PORT_ATTR_SET)
		return dpaa2_switch_port_attr_set_event(dev, ptr);

	switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);
	if (!switchdev_work)
		return NOTIFY_BAD;

	INIT_WORK(&switchdev_work->work, dpaa2_switch_event_work);
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
		kfree(switchdev_work);
		return NOTIFY_DONE;
	}

	queue_work(ethsw->workqueue, &switchdev_work->work);

	return NOTIFY_DONE;

err_addr_alloc:
	kfree(switchdev_work);
	return NOTIFY_BAD;
}

static int dpaa2_switch_port_obj_event(unsigned long event,
				       struct net_device *netdev,
				       struct switchdev_notifier_port_obj_info *port_obj_info)
{
	int err = -EOPNOTSUPP;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
		err = dpaa2_switch_port_obj_add(netdev, port_obj_info->obj,
						port_obj_info->trans);
		break;
	case SWITCHDEV_PORT_OBJ_DEL:
		err = dpaa2_switch_port_obj_del(netdev, port_obj_info->obj);
		break;
	}

	port_obj_info->handled = true;
	return notifier_from_errno(err);
}

static int dpaa2_switch_port_blocking_event(struct notifier_block *nb,
					    unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);

	if (!dpaa2_switch_port_dev_check(dev, nb))
		return NOTIFY_DONE;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
	case SWITCHDEV_PORT_OBJ_DEL:
		return dpaa2_switch_port_obj_event(event, dev, ptr);
	case SWITCHDEV_PORT_ATTR_SET:
		return dpaa2_switch_port_attr_set_event(dev, ptr);
	}

	return NOTIFY_DONE;
}

static int dpaa2_switch_register_notifier(struct device *dev)
{
	struct ethsw_core *ethsw = dev_get_drvdata(dev);
	int err;

	ethsw->port_nb.notifier_call = dpaa2_switch_port_netdevice_event;
	err = register_netdevice_notifier(&ethsw->port_nb);
	if (err) {
		dev_err(dev, "Failed to register netdev notifier\n");
		return err;
	}

	ethsw->port_switchdev_nb.notifier_call = dpaa2_switch_port_event;
	err = register_switchdev_notifier(&ethsw->port_switchdev_nb);
	if (err) {
		dev_err(dev, "Failed to register switchdev notifier\n");
		goto err_switchdev_nb;
	}

	ethsw->port_switchdevb_nb.notifier_call = dpaa2_switch_port_blocking_event;
	err = register_switchdev_blocking_notifier(&ethsw->port_switchdevb_nb);
	if (err) {
		dev_err(dev, "Failed to register switchdev blocking notifier\n");
		goto err_switchdev_blocking_nb;
	}

	return 0;

err_switchdev_blocking_nb:
	unregister_switchdev_notifier(&ethsw->port_switchdev_nb);
err_switchdev_nb:
	unregister_netdevice_notifier(&ethsw->port_nb);
	return err;
}

static void dpaa2_switch_detect_features(struct ethsw_core *ethsw)
{
	ethsw->features = 0;

	if (ethsw->major > 8 || (ethsw->major == 8 && ethsw->minor >= 6))
		ethsw->features |= ETHSW_FEATURE_MAC_ADDR;
}

static int dpaa2_switch_init(struct fsl_mc_device *sw_dev)
{
	struct device *dev = &sw_dev->dev;
	struct ethsw_core *ethsw = dev_get_drvdata(dev);
	struct dpsw_stp_cfg stp_cfg;
	int err;
	u16 i;

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
				   &ethsw->major,
				   &ethsw->minor);
	if (err) {
		dev_err(dev, "dpsw_get_api_version err %d\n", err);
		goto err_close;
	}

	/* Minimum supported DPSW version check */
	if (ethsw->major < DPSW_MIN_VER_MAJOR ||
	    (ethsw->major == DPSW_MIN_VER_MAJOR &&
	     ethsw->minor < DPSW_MIN_VER_MINOR)) {
		dev_err(dev, "DPSW version %d:%d not supported. Use %d.%d or greater.\n",
			ethsw->major,
			ethsw->minor,
			DPSW_MIN_VER_MAJOR, DPSW_MIN_VER_MINOR);
		err = -ENOTSUPP;
		goto err_close;
	}

	dpaa2_switch_detect_features(ethsw);

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

	ethsw->workqueue = alloc_ordered_workqueue("%s_%d_ordered",
						   WQ_MEM_RECLAIM, "ethsw",
						   ethsw->sw_attr.id);
	if (!ethsw->workqueue) {
		err = -ENOMEM;
		goto err_close;
	}

	err = dpaa2_switch_register_notifier(dev);
	if (err)
		goto err_destroy_ordered_workqueue;

	return 0;

err_destroy_ordered_workqueue:
	destroy_workqueue(ethsw->workqueue);

err_close:
	dpsw_close(ethsw->mc_io, 0, ethsw->dpsw_handle);
	return err;
}

static int dpaa2_switch_port_init(struct ethsw_port_priv *port_priv, u16 port)
{
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

	err = dpaa2_switch_port_set_pvid(port_priv, 0);
	if (err)
		return err;

	err = dpsw_vlan_remove_if(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  DEFAULT_VLAN_ID, &vcfg);
	if (err)
		netdev_err(netdev, "dpsw_vlan_remove_if err %d\n", err);

	return err;
}

static void dpaa2_switch_unregister_notifier(struct device *dev)
{
	struct ethsw_core *ethsw = dev_get_drvdata(dev);
	struct notifier_block *nb;
	int err;

	nb = &ethsw->port_switchdevb_nb;
	err = unregister_switchdev_blocking_notifier(nb);
	if (err)
		dev_err(dev,
			"Failed to unregister switchdev blocking notifier (%d)\n",
			err);

	err = unregister_switchdev_notifier(&ethsw->port_switchdev_nb);
	if (err)
		dev_err(dev,
			"Failed to unregister switchdev notifier (%d)\n", err);

	err = unregister_netdevice_notifier(&ethsw->port_nb);
	if (err)
		dev_err(dev,
			"Failed to unregister netdev notifier (%d)\n", err);
}

static void dpaa2_switch_takedown(struct fsl_mc_device *sw_dev)
{
	struct device *dev = &sw_dev->dev;
	struct ethsw_core *ethsw = dev_get_drvdata(dev);
	int err;

	dpaa2_switch_unregister_notifier(dev);

	err = dpsw_close(ethsw->mc_io, 0, ethsw->dpsw_handle);
	if (err)
		dev_warn(dev, "dpsw_close err %d\n", err);
}

static int dpaa2_switch_remove(struct fsl_mc_device *sw_dev)
{
	struct ethsw_port_priv *port_priv;
	struct ethsw_core *ethsw;
	struct device *dev;
	int i;

	dev = &sw_dev->dev;
	ethsw = dev_get_drvdata(dev);

	dpaa2_switch_teardown_irqs(sw_dev);

	dpsw_disable(ethsw->mc_io, 0, ethsw->dpsw_handle);

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++) {
		port_priv = ethsw->ports[i];
		unregister_netdev(port_priv->netdev);
		free_netdev(port_priv->netdev);
	}
	kfree(ethsw->ports);

	dpaa2_switch_takedown(sw_dev);

	destroy_workqueue(ethsw->workqueue);

	fsl_mc_portal_free(ethsw->mc_io);

	kfree(ethsw);

	dev_set_drvdata(dev, NULL);

	return 0;
}

static int dpaa2_switch_probe_port(struct ethsw_core *ethsw,
				   u16 port_idx)
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
	port_netdev->netdev_ops = &dpaa2_switch_port_ops;
	port_netdev->ethtool_ops = &dpaa2_switch_port_ethtool_ops;

	/* Set MTU limits */
	port_netdev->min_mtu = ETH_MIN_MTU;
	port_netdev->max_mtu = ETHSW_MAX_FRAME_LENGTH;

	err = dpaa2_switch_port_init(port_priv, port_idx);
	if (err)
		goto err_port_probe;

	err = dpaa2_switch_port_set_mac_addr(port_priv);
	if (err)
		goto err_port_probe;

	err = register_netdev(port_netdev);
	if (err < 0) {
		dev_err(dev, "register_netdev error %d\n", err);
		goto err_port_probe;
	}

	ethsw->ports[port_idx] = port_priv;

	return 0;

err_port_probe:
	free_netdev(port_netdev);

	return err;
}

static int dpaa2_switch_probe(struct fsl_mc_device *sw_dev)
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

	err = fsl_mc_portal_allocate(sw_dev, FSL_MC_IO_ATOMIC_CONTEXT_PORTAL,
				     &ethsw->mc_io);
	if (err) {
		if (err == -ENXIO)
			err = -EPROBE_DEFER;
		else
			dev_err(dev, "fsl_mc_portal_allocate err %d\n", err);
		goto err_free_drvdata;
	}

	err = dpaa2_switch_init(sw_dev);
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
		err = dpaa2_switch_probe_port(ethsw, i);
		if (err)
			goto err_free_ports;
	}

	err = dpsw_enable(ethsw->mc_io, 0, ethsw->dpsw_handle);
	if (err) {
		dev_err(ethsw->dev, "dpsw_enable err %d\n", err);
		goto err_free_ports;
	}

	/* Make sure the switch ports are disabled at probe time */
	for (i = 0; i < ethsw->sw_attr.num_ifs; i++)
		dpsw_if_disable(ethsw->mc_io, 0, ethsw->dpsw_handle, i);

	/* Setup IRQs */
	err = dpaa2_switch_setup_irqs(sw_dev);
	if (err)
		goto err_stop;

	dev_info(dev, "probed %d port switch\n", ethsw->sw_attr.num_ifs);
	return 0;

err_stop:
	dpsw_disable(ethsw->mc_io, 0, ethsw->dpsw_handle);

err_free_ports:
	/* Cleanup registered ports only */
	for (i--; i >= 0; i--) {
		unregister_netdev(ethsw->ports[i]->netdev);
		free_netdev(ethsw->ports[i]->netdev);
	}
	kfree(ethsw->ports);

err_takedown:
	dpaa2_switch_takedown(sw_dev);

err_free_cmdport:
	fsl_mc_portal_free(ethsw->mc_io);

err_free_drvdata:
	kfree(ethsw);
	dev_set_drvdata(dev, NULL);

	return err;
}

static const struct fsl_mc_device_id dpaa2_switch_match_id_table[] = {
	{
		.vendor = FSL_MC_VENDOR_FREESCALE,
		.obj_type = "dpsw",
	},
	{ .vendor = 0x0 }
};
MODULE_DEVICE_TABLE(fslmc, dpaa2_switch_match_id_table);

static struct fsl_mc_driver dpaa2_switch_drv = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
	},
	.probe = dpaa2_switch_probe,
	.remove = dpaa2_switch_remove,
	.match_id_table = dpaa2_switch_match_id_table
};

module_fsl_mc_driver(dpaa2_switch_drv);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DPAA2 Ethernet Switch Driver");
