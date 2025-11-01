// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2023 Beijing WangXun Technology Co., Ltd. */

#include <linux/pci.h>
#include <linux/phylink.h>
#include <linux/netdevice.h>

#include "../libwx/wx_ethtool.h"
#include "../libwx/wx_type.h"
#include "../libwx/wx_lib.h"
#include "txgbe_type.h"
#include "txgbe_fdir.h"
#include "txgbe_ethtool.h"

int txgbe_get_link_ksettings(struct net_device *netdev,
			     struct ethtool_link_ksettings *cmd)
{
	struct wx *wx = netdev_priv(netdev);
	struct txgbe *txgbe = wx->priv;
	int err;

	if (wx->mac.type == wx_mac_aml40)
		return -EOPNOTSUPP;

	err = wx_get_link_ksettings(netdev, cmd);
	if (err)
		return err;

	if (wx->mac.type == wx_mac_sp)
		return 0;

	cmd->base.port = txgbe->link_port;
	cmd->base.autoneg = AUTONEG_DISABLE;
	linkmode_copy(cmd->link_modes.supported, txgbe->sfp_support);
	linkmode_copy(cmd->link_modes.advertising, txgbe->advertising);

	return 0;
}

static int txgbe_set_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
{
	struct wx *wx = netdev_priv(netdev);
	u32 new_rx_count, new_tx_count;
	struct wx_ring *temp_ring;
	int i, err = 0;

	new_tx_count = clamp_t(u32, ring->tx_pending, WX_MIN_TXD, WX_MAX_TXD);
	new_tx_count = ALIGN(new_tx_count, WX_REQ_TX_DESCRIPTOR_MULTIPLE);

	new_rx_count = clamp_t(u32, ring->rx_pending, WX_MIN_RXD, WX_MAX_RXD);
	new_rx_count = ALIGN(new_rx_count, WX_REQ_RX_DESCRIPTOR_MULTIPLE);

	if (new_tx_count == wx->tx_ring_count &&
	    new_rx_count == wx->rx_ring_count)
		return 0;

	err = wx_set_state_reset(wx);
	if (err)
		return err;

	if (!netif_running(wx->netdev)) {
		for (i = 0; i < wx->num_tx_queues; i++)
			wx->tx_ring[i]->count = new_tx_count;
		for (i = 0; i < wx->num_rx_queues; i++)
			wx->rx_ring[i]->count = new_rx_count;
		wx->tx_ring_count = new_tx_count;
		wx->rx_ring_count = new_rx_count;

		goto clear_reset;
	}

	/* allocate temporary buffer to store rings in */
	i = max_t(int, wx->num_tx_queues, wx->num_rx_queues);
	temp_ring = kvmalloc_array(i, sizeof(struct wx_ring), GFP_KERNEL);
	if (!temp_ring) {
		err = -ENOMEM;
		goto clear_reset;
	}

	txgbe_down(wx);

	wx_set_ring(wx, new_tx_count, new_rx_count, temp_ring);
	kvfree(temp_ring);

	txgbe_up(wx);

clear_reset:
	clear_bit(WX_STATE_RESETTING, wx->state);
	return err;
}

static int txgbe_set_channels(struct net_device *dev,
			      struct ethtool_channels *ch)
{
	int err;

	err = wx_set_channels(dev, ch);
	if (err < 0)
		return err;

	/* use setup TC to update any traffic class queue mapping */
	return txgbe_setup_tc(dev, netdev_get_num_tc(dev));
}

static int txgbe_get_ethtool_fdir_entry(struct txgbe *txgbe,
					struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	union txgbe_atr_input *mask = &txgbe->fdir_mask;
	struct txgbe_fdir_filter *rule = NULL;
	struct hlist_node *node;

	/* report total rule count */
	cmd->data = (1024 << TXGBE_FDIR_PBALLOC_64K) - 2;

	hlist_for_each_entry_safe(rule, node, &txgbe->fdir_filter_list,
				  fdir_node) {
		if (fsp->location <= rule->sw_idx)
			break;
	}

	if (!rule || fsp->location != rule->sw_idx)
		return -EINVAL;

	/* set flow type field */
	switch (rule->filter.formatted.flow_type) {
	case TXGBE_ATR_FLOW_TYPE_TCPV4:
		fsp->flow_type = TCP_V4_FLOW;
		break;
	case TXGBE_ATR_FLOW_TYPE_UDPV4:
		fsp->flow_type = UDP_V4_FLOW;
		break;
	case TXGBE_ATR_FLOW_TYPE_SCTPV4:
		fsp->flow_type = SCTP_V4_FLOW;
		break;
	case TXGBE_ATR_FLOW_TYPE_IPV4:
		fsp->flow_type = IP_USER_FLOW;
		fsp->h_u.usr_ip4_spec.ip_ver = ETH_RX_NFC_IP4;
		fsp->h_u.usr_ip4_spec.proto = 0;
		fsp->m_u.usr_ip4_spec.proto = 0;
		break;
	default:
		return -EINVAL;
	}

	fsp->h_u.tcp_ip4_spec.psrc = rule->filter.formatted.src_port;
	fsp->m_u.tcp_ip4_spec.psrc = mask->formatted.src_port;
	fsp->h_u.tcp_ip4_spec.pdst = rule->filter.formatted.dst_port;
	fsp->m_u.tcp_ip4_spec.pdst = mask->formatted.dst_port;
	fsp->h_u.tcp_ip4_spec.ip4src = rule->filter.formatted.src_ip[0];
	fsp->m_u.tcp_ip4_spec.ip4src = mask->formatted.src_ip[0];
	fsp->h_u.tcp_ip4_spec.ip4dst = rule->filter.formatted.dst_ip[0];
	fsp->m_u.tcp_ip4_spec.ip4dst = mask->formatted.dst_ip[0];
	fsp->h_ext.vlan_etype = rule->filter.formatted.flex_bytes;
	fsp->m_ext.vlan_etype = mask->formatted.flex_bytes;
	fsp->h_ext.data[1] = htonl(rule->filter.formatted.vm_pool);
	fsp->m_ext.data[1] = htonl(mask->formatted.vm_pool);
	fsp->flow_type |= FLOW_EXT;

	/* record action */
	if (rule->action == TXGBE_RDB_FDIR_DROP_QUEUE)
		fsp->ring_cookie = RX_CLS_FLOW_DISC;
	else
		fsp->ring_cookie = rule->action;

	return 0;
}

static int txgbe_get_ethtool_fdir_all(struct txgbe *txgbe,
				      struct ethtool_rxnfc *cmd,
				      u32 *rule_locs)
{
	struct txgbe_fdir_filter *rule;
	struct hlist_node *node;
	int cnt = 0;

	/* report total rule count */
	cmd->data = (1024 << TXGBE_FDIR_PBALLOC_64K) - 2;

	hlist_for_each_entry_safe(rule, node, &txgbe->fdir_filter_list,
				  fdir_node) {
		if (cnt == cmd->rule_cnt)
			return -EMSGSIZE;
		rule_locs[cnt] = rule->sw_idx;
		cnt++;
	}

	cmd->rule_cnt = cnt;

	return 0;
}

static int txgbe_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd,
			   u32 *rule_locs)
{
	struct wx *wx = netdev_priv(dev);
	struct txgbe *txgbe = wx->priv;
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = wx->num_rx_queues;
		ret = 0;
		break;
	case ETHTOOL_GRXCLSRLCNT:
		cmd->rule_cnt = txgbe->fdir_filter_count;
		ret = 0;
		break;
	case ETHTOOL_GRXCLSRULE:
		ret = txgbe_get_ethtool_fdir_entry(txgbe, cmd);
		break;
	case ETHTOOL_GRXCLSRLALL:
		ret = txgbe_get_ethtool_fdir_all(txgbe, cmd, (u32 *)rule_locs);
		break;
	default:
		break;
	}

	return ret;
}

static int txgbe_flowspec_to_flow_type(struct ethtool_rx_flow_spec *fsp,
				       u8 *flow_type)
{
	switch (fsp->flow_type & ~FLOW_EXT) {
	case TCP_V4_FLOW:
		*flow_type = TXGBE_ATR_FLOW_TYPE_TCPV4;
		break;
	case UDP_V4_FLOW:
		*flow_type = TXGBE_ATR_FLOW_TYPE_UDPV4;
		break;
	case SCTP_V4_FLOW:
		*flow_type = TXGBE_ATR_FLOW_TYPE_SCTPV4;
		break;
	case IP_USER_FLOW:
		switch (fsp->h_u.usr_ip4_spec.proto) {
		case IPPROTO_TCP:
			*flow_type = TXGBE_ATR_FLOW_TYPE_TCPV4;
			break;
		case IPPROTO_UDP:
			*flow_type = TXGBE_ATR_FLOW_TYPE_UDPV4;
			break;
		case IPPROTO_SCTP:
			*flow_type = TXGBE_ATR_FLOW_TYPE_SCTPV4;
			break;
		case 0:
			if (!fsp->m_u.usr_ip4_spec.proto) {
				*flow_type = TXGBE_ATR_FLOW_TYPE_IPV4;
				break;
			}
			fallthrough;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static bool txgbe_match_ethtool_fdir_entry(struct txgbe *txgbe,
					   struct txgbe_fdir_filter *input)
{
	struct txgbe_fdir_filter *rule = NULL;
	struct hlist_node *node2;

	hlist_for_each_entry_safe(rule, node2, &txgbe->fdir_filter_list,
				  fdir_node) {
		if (rule->filter.formatted.bkt_hash ==
		    input->filter.formatted.bkt_hash &&
		    rule->action == input->action) {
			wx_dbg(txgbe->wx, "FDIR entry already exist\n");
			return true;
		}
	}
	return false;
}

static int txgbe_update_ethtool_fdir_entry(struct txgbe *txgbe,
					   struct txgbe_fdir_filter *input,
					   u16 sw_idx)
{
	struct hlist_node *node = NULL, *parent = NULL;
	struct txgbe_fdir_filter *rule;
	struct wx *wx = txgbe->wx;
	bool deleted = false;
	int err;

	hlist_for_each_entry_safe(rule, node, &txgbe->fdir_filter_list,
				  fdir_node) {
		/* hash found, or no matching entry */
		if (rule->sw_idx >= sw_idx)
			break;
		parent = node;
	}

	/* if there is an old rule occupying our place remove it */
	if (rule && rule->sw_idx == sw_idx) {
		/* hardware filters are only configured when interface is up,
		 * and we should not issue filter commands while the interface
		 * is down
		 */
		if (netif_running(wx->netdev) &&
		    (!input || rule->filter.formatted.bkt_hash !=
		     input->filter.formatted.bkt_hash)) {
			err = txgbe_fdir_erase_perfect_filter(wx,
							      &rule->filter,
							      sw_idx);
			if (err)
				return -EINVAL;
		}

		hlist_del(&rule->fdir_node);
		kfree(rule);
		txgbe->fdir_filter_count--;
		deleted = true;
	}

	/* If we weren't given an input, then this was a request to delete a
	 * filter. We should return -EINVAL if the filter wasn't found, but
	 * return 0 if the rule was successfully deleted.
	 */
	if (!input)
		return deleted ? 0 : -EINVAL;

	/* initialize node and set software index */
	INIT_HLIST_NODE(&input->fdir_node);

	/* add filter to the list */
	if (parent)
		hlist_add_behind(&input->fdir_node, parent);
	else
		hlist_add_head(&input->fdir_node,
			       &txgbe->fdir_filter_list);

	/* update counts */
	txgbe->fdir_filter_count++;

	return 0;
}

static int txgbe_add_ethtool_fdir_entry(struct txgbe *txgbe,
					struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct txgbe_fdir_filter *input;
	union txgbe_atr_input mask;
	struct wx *wx = txgbe->wx;
	int err = -EINVAL;
	u16 ptype = 0;
	u8 queue;

	if (!(test_bit(WX_FLAG_FDIR_PERFECT, wx->flags)))
		return -EOPNOTSUPP;

	/* ring_cookie is a masked into a set of queues and txgbe pools or
	 * we use drop index
	 */
	if (fsp->ring_cookie == RX_CLS_FLOW_DISC) {
		queue = TXGBE_RDB_FDIR_DROP_QUEUE;
	} else {
		u32 ring = ethtool_get_flow_spec_ring(fsp->ring_cookie);
		u8 vf = ethtool_get_flow_spec_ring_vf(fsp->ring_cookie);

		if (!vf && ring >= wx->num_rx_queues)
			return -EINVAL;
		else if (vf && (vf > wx->num_vfs ||
				ring >= wx->num_rx_queues_per_pool))
			return -EINVAL;

		/* Map the ring onto the absolute queue index */
		if (!vf)
			queue = wx->rx_ring[ring]->reg_idx;
		else
			queue = ((vf - 1) * wx->num_rx_queues_per_pool) + ring;
	}

	/* Don't allow indexes to exist outside of available space */
	if (fsp->location >= ((1024 << TXGBE_FDIR_PBALLOC_64K) - 2)) {
		wx_err(wx, "Location out of range\n");
		return -EINVAL;
	}

	input = kzalloc(sizeof(*input), GFP_ATOMIC);
	if (!input)
		return -ENOMEM;

	memset(&mask, 0, sizeof(union txgbe_atr_input));

	/* set SW index */
	input->sw_idx = fsp->location;

	/* record flow type */
	if (txgbe_flowspec_to_flow_type(fsp,
					&input->filter.formatted.flow_type)) {
		wx_err(wx, "Unrecognized flow type\n");
		goto err_out;
	}

	mask.formatted.flow_type = TXGBE_ATR_L4TYPE_IPV6_MASK |
				   TXGBE_ATR_L4TYPE_MASK;

	if (input->filter.formatted.flow_type == TXGBE_ATR_FLOW_TYPE_IPV4)
		mask.formatted.flow_type &= TXGBE_ATR_L4TYPE_IPV6_MASK;

	/* Copy input into formatted structures */
	input->filter.formatted.src_ip[0] = fsp->h_u.tcp_ip4_spec.ip4src;
	mask.formatted.src_ip[0] = fsp->m_u.tcp_ip4_spec.ip4src;
	input->filter.formatted.dst_ip[0] = fsp->h_u.tcp_ip4_spec.ip4dst;
	mask.formatted.dst_ip[0] = fsp->m_u.tcp_ip4_spec.ip4dst;
	input->filter.formatted.src_port = fsp->h_u.tcp_ip4_spec.psrc;
	mask.formatted.src_port = fsp->m_u.tcp_ip4_spec.psrc;
	input->filter.formatted.dst_port = fsp->h_u.tcp_ip4_spec.pdst;
	mask.formatted.dst_port = fsp->m_u.tcp_ip4_spec.pdst;

	if (fsp->flow_type & FLOW_EXT) {
		input->filter.formatted.vm_pool =
				(unsigned char)ntohl(fsp->h_ext.data[1]);
		mask.formatted.vm_pool =
				(unsigned char)ntohl(fsp->m_ext.data[1]);
		input->filter.formatted.flex_bytes =
						fsp->h_ext.vlan_etype;
		mask.formatted.flex_bytes = fsp->m_ext.vlan_etype;
	}

	switch (input->filter.formatted.flow_type) {
	case TXGBE_ATR_FLOW_TYPE_TCPV4:
		ptype = WX_PTYPE_L2_IPV4_TCP;
		break;
	case TXGBE_ATR_FLOW_TYPE_UDPV4:
		ptype = WX_PTYPE_L2_IPV4_UDP;
		break;
	case TXGBE_ATR_FLOW_TYPE_SCTPV4:
		ptype = WX_PTYPE_L2_IPV4_SCTP;
		break;
	case TXGBE_ATR_FLOW_TYPE_IPV4:
		ptype = WX_PTYPE_L2_IPV4;
		break;
	default:
		break;
	}

	input->filter.formatted.vlan_id = htons(ptype);
	if (mask.formatted.flow_type & TXGBE_ATR_L4TYPE_MASK)
		mask.formatted.vlan_id = htons(0xFFFF);
	else
		mask.formatted.vlan_id = htons(0xFFF8);

	/* determine if we need to drop or route the packet */
	if (fsp->ring_cookie == RX_CLS_FLOW_DISC)
		input->action = TXGBE_RDB_FDIR_DROP_QUEUE;
	else
		input->action = fsp->ring_cookie;

	spin_lock(&txgbe->fdir_perfect_lock);

	if (hlist_empty(&txgbe->fdir_filter_list)) {
		/* save mask and program input mask into HW */
		memcpy(&txgbe->fdir_mask, &mask, sizeof(mask));
		err = txgbe_fdir_set_input_mask(wx, &mask);
		if (err)
			goto err_unlock;
	} else if (memcmp(&txgbe->fdir_mask, &mask, sizeof(mask))) {
		wx_err(wx, "Hardware only supports one mask per port. To change the mask you must first delete all the rules.\n");
		goto err_unlock;
	}

	/* apply mask and compute/store hash */
	txgbe_atr_compute_perfect_hash(&input->filter, &mask);

	/* check if new entry does not exist on filter list */
	if (txgbe_match_ethtool_fdir_entry(txgbe, input))
		goto err_unlock;

	/* only program filters to hardware if the net device is running, as
	 * we store the filters in the Rx buffer which is not allocated when
	 * the device is down
	 */
	if (netif_running(wx->netdev)) {
		err = txgbe_fdir_write_perfect_filter(wx, &input->filter,
						      input->sw_idx, queue);
		if (err)
			goto err_unlock;
	}

	txgbe_update_ethtool_fdir_entry(txgbe, input, input->sw_idx);

	spin_unlock(&txgbe->fdir_perfect_lock);

	return 0;
err_unlock:
	spin_unlock(&txgbe->fdir_perfect_lock);
err_out:
	kfree(input);
	return err;
}

static int txgbe_del_ethtool_fdir_entry(struct txgbe *txgbe,
					struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	int err = 0;

	spin_lock(&txgbe->fdir_perfect_lock);
	err = txgbe_update_ethtool_fdir_entry(txgbe, NULL, fsp->location);
	spin_unlock(&txgbe->fdir_perfect_lock);

	return err;
}

static int txgbe_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	struct wx *wx = netdev_priv(dev);
	struct txgbe *txgbe = wx->priv;
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		ret = txgbe_add_ethtool_fdir_entry(txgbe, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		ret = txgbe_del_ethtool_fdir_entry(txgbe, cmd);
		break;
	default:
		break;
	}

	return ret;
}

static const struct ethtool_ops txgbe_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_TX_MAX_FRAMES_IRQ |
				     ETHTOOL_COALESCE_USE_ADAPTIVE,
	.get_drvinfo		= wx_get_drvinfo,
	.nway_reset		= wx_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= txgbe_get_link_ksettings,
	.set_link_ksettings	= wx_set_link_ksettings,
	.get_sset_count		= wx_get_sset_count,
	.get_strings		= wx_get_strings,
	.get_ethtool_stats	= wx_get_ethtool_stats,
	.get_eth_mac_stats	= wx_get_mac_stats,
	.get_pause_stats	= wx_get_pause_stats,
	.get_pauseparam		= wx_get_pauseparam,
	.set_pauseparam		= wx_set_pauseparam,
	.get_ringparam		= wx_get_ringparam,
	.set_ringparam		= txgbe_set_ringparam,
	.get_coalesce		= wx_get_coalesce,
	.set_coalesce		= wx_set_coalesce,
	.get_channels		= wx_get_channels,
	.set_channels		= txgbe_set_channels,
	.get_rxnfc		= txgbe_get_rxnfc,
	.set_rxnfc		= txgbe_set_rxnfc,
	.get_rxfh_fields	= wx_get_rxfh_fields,
	.set_rxfh_fields	= wx_set_rxfh_fields,
	.get_rxfh_indir_size	= wx_rss_indir_size,
	.get_rxfh_key_size	= wx_get_rxfh_key_size,
	.get_rxfh		= wx_get_rxfh,
	.set_rxfh		= wx_set_rxfh,
	.get_msglevel		= wx_get_msglevel,
	.set_msglevel		= wx_set_msglevel,
	.get_ts_info		= wx_get_ts_info,
	.get_ts_stats		= wx_get_ptp_stats,
};

void txgbe_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &txgbe_ethtool_ops;
}
