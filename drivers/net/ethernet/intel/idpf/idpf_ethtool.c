// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"
#include "idpf_ptp.h"
#include "idpf_virtchnl.h"

/**
 * idpf_get_rxnfc - command to get RX flow classification rules
 * @netdev: network interface device structure
 * @cmd: ethtool rxnfc command
 * @rule_locs: pointer to store rule locations
 *
 * Returns Success if the command is supported.
 */
static int idpf_get_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd,
			  u32 *rule_locs)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport_user_config_data *user_config;
	struct idpf_fsteer_fltr *f;
	struct idpf_vport *vport;
	unsigned int cnt = 0;
	int err = 0;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);
	user_config = &np->adapter->vport_config[np->vport_idx]->user_config;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = vport->num_rxq;
		break;
	case ETHTOOL_GRXCLSRLCNT:
		cmd->rule_cnt = user_config->num_fsteer_fltrs;
		cmd->data = idpf_fsteer_max_rules(vport);
		break;
	case ETHTOOL_GRXCLSRULE:
		err = -EINVAL;
		list_for_each_entry(f, &user_config->flow_steer_list, list)
			if (f->loc == cmd->fs.location) {
				cmd->fs.ring_cookie = f->q_index;
				err = 0;
				break;
			}
		break;
	case ETHTOOL_GRXCLSRLALL:
		cmd->data = idpf_fsteer_max_rules(vport);
		list_for_each_entry(f, &user_config->flow_steer_list, list) {
			if (cnt == cmd->rule_cnt) {
				err = -EMSGSIZE;
				break;
			}
			rule_locs[cnt] = f->loc;
			cnt++;
		}
		if (!err)
			cmd->rule_cnt = user_config->num_fsteer_fltrs;
		break;
	default:
		break;
	}

	idpf_vport_ctrl_unlock(netdev);

	return err;
}

static void idpf_fsteer_fill_ipv4(struct virtchnl2_proto_hdrs *hdrs,
				  struct ethtool_rx_flow_spec *fsp)
{
	struct iphdr *iph;

	hdrs->proto_hdr[0].hdr_type = cpu_to_le32(VIRTCHNL2_PROTO_HDR_IPV4);

	iph = (struct iphdr *)hdrs->proto_hdr[0].buffer_spec;
	iph->saddr = fsp->h_u.tcp_ip4_spec.ip4src;
	iph->daddr = fsp->h_u.tcp_ip4_spec.ip4dst;

	iph = (struct iphdr *)hdrs->proto_hdr[0].buffer_mask;
	iph->saddr = fsp->m_u.tcp_ip4_spec.ip4src;
	iph->daddr = fsp->m_u.tcp_ip4_spec.ip4dst;
}

static void idpf_fsteer_fill_udp(struct virtchnl2_proto_hdrs *hdrs,
				 struct ethtool_rx_flow_spec *fsp,
				 bool v4)
{
	struct udphdr *udph, *udpm;

	hdrs->proto_hdr[1].hdr_type = cpu_to_le32(VIRTCHNL2_PROTO_HDR_UDP);

	udph = (struct udphdr *)hdrs->proto_hdr[1].buffer_spec;
	udpm = (struct udphdr *)hdrs->proto_hdr[1].buffer_mask;

	if (v4) {
		udph->source = fsp->h_u.udp_ip4_spec.psrc;
		udph->dest = fsp->h_u.udp_ip4_spec.pdst;
		udpm->source = fsp->m_u.udp_ip4_spec.psrc;
		udpm->dest = fsp->m_u.udp_ip4_spec.pdst;
	} else {
		udph->source = fsp->h_u.udp_ip6_spec.psrc;
		udph->dest = fsp->h_u.udp_ip6_spec.pdst;
		udpm->source = fsp->m_u.udp_ip6_spec.psrc;
		udpm->dest = fsp->m_u.udp_ip6_spec.pdst;
	}
}

static void idpf_fsteer_fill_tcp(struct virtchnl2_proto_hdrs *hdrs,
				 struct ethtool_rx_flow_spec *fsp,
				 bool v4)
{
	struct tcphdr *tcph, *tcpm;

	hdrs->proto_hdr[1].hdr_type = cpu_to_le32(VIRTCHNL2_PROTO_HDR_TCP);

	tcph = (struct tcphdr *)hdrs->proto_hdr[1].buffer_spec;
	tcpm = (struct tcphdr *)hdrs->proto_hdr[1].buffer_mask;

	if (v4) {
		tcph->source = fsp->h_u.tcp_ip4_spec.psrc;
		tcph->dest = fsp->h_u.tcp_ip4_spec.pdst;
		tcpm->source = fsp->m_u.tcp_ip4_spec.psrc;
		tcpm->dest = fsp->m_u.tcp_ip4_spec.pdst;
	} else {
		tcph->source = fsp->h_u.tcp_ip6_spec.psrc;
		tcph->dest = fsp->h_u.tcp_ip6_spec.pdst;
		tcpm->source = fsp->m_u.tcp_ip6_spec.psrc;
		tcpm->dest = fsp->m_u.tcp_ip6_spec.pdst;
	}
}

/**
 * idpf_add_flow_steer - add a Flow Steering filter
 * @netdev: network interface device structure
 * @cmd: command to add Flow Steering filter
 *
 * Return: 0 on success and negative values for failure
 */
static int idpf_add_flow_steer(struct net_device *netdev,
			       struct ethtool_rxnfc *cmd)
{
	struct idpf_fsteer_fltr *fltr, *parent = NULL, *f;
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport_user_config_data *user_config;
	struct ethtool_rx_flow_spec *fsp = &cmd->fs;
	struct virtchnl2_flow_rule_add_del *rule;
	struct idpf_vport_config *vport_config;
	struct virtchnl2_rule_action_set *acts;
	struct virtchnl2_flow_rule_info *info;
	struct virtchnl2_proto_hdrs *hdrs;
	struct idpf_vport *vport;
	u32 flow_type, q_index;
	u16 num_rxq;
	int err;

	vport = idpf_netdev_to_vport(netdev);
	vport_config = vport->adapter->vport_config[np->vport_idx];
	user_config = &vport_config->user_config;
	num_rxq = user_config->num_req_rx_qs;

	flow_type = fsp->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT | FLOW_RSS);
	if (flow_type != fsp->flow_type)
		return -EINVAL;

	if (!idpf_sideband_action_ena(vport, fsp) ||
	    !idpf_sideband_flow_type_ena(vport, flow_type))
		return -EOPNOTSUPP;

	if (user_config->num_fsteer_fltrs > idpf_fsteer_max_rules(vport))
		return -ENOSPC;

	q_index = fsp->ring_cookie;
	if (q_index >= num_rxq)
		return -EINVAL;

	rule = kzalloc(struct_size(rule, rule_info, 1), GFP_KERNEL);
	if (!rule)
		return -ENOMEM;

	rule->vport_id = cpu_to_le32(vport->vport_id);
	rule->count = cpu_to_le32(1);
	info = &rule->rule_info[0];
	info->rule_id = cpu_to_le32(fsp->location);

	hdrs = &info->rule_cfg.proto_hdrs;
	hdrs->tunnel_level = 0;
	hdrs->count = cpu_to_le32(2);

	acts = &info->rule_cfg.action_set;
	acts->count = cpu_to_le32(1);
	acts->actions[0].action_type = cpu_to_le32(VIRTCHNL2_ACTION_QUEUE);
	acts->actions[0].act_conf.q_id = cpu_to_le32(q_index);

	switch (flow_type) {
	case UDP_V4_FLOW:
		idpf_fsteer_fill_ipv4(hdrs, fsp);
		idpf_fsteer_fill_udp(hdrs, fsp, true);
		break;
	case TCP_V4_FLOW:
		idpf_fsteer_fill_ipv4(hdrs, fsp);
		idpf_fsteer_fill_tcp(hdrs, fsp, true);
		break;
	default:
		err = -EINVAL;
		goto out;
	}

	err = idpf_add_del_fsteer_filters(vport->adapter, rule,
					  VIRTCHNL2_OP_ADD_FLOW_RULE);
	if (err)
		goto out;

	if (info->status != cpu_to_le32(VIRTCHNL2_FLOW_RULE_SUCCESS)) {
		err = -EIO;
		goto out;
	}

	fltr = kzalloc(sizeof(*fltr), GFP_KERNEL);
	if (!fltr) {
		err = -ENOMEM;
		goto out;
	}

	fltr->loc = fsp->location;
	fltr->q_index = q_index;
	list_for_each_entry(f, &user_config->flow_steer_list, list) {
		if (f->loc >= fltr->loc)
			break;
		parent = f;
	}

	parent ? list_add(&fltr->list, &parent->list) :
		 list_add(&fltr->list, &user_config->flow_steer_list);

	user_config->num_fsteer_fltrs++;

out:
	kfree(rule);
	return err;
}

/**
 * idpf_del_flow_steer - delete a Flow Steering filter
 * @netdev: network interface device structure
 * @cmd: command to add Flow Steering filter
 *
 * Return: 0 on success and negative values for failure
 */
static int idpf_del_flow_steer(struct net_device *netdev,
			       struct ethtool_rxnfc *cmd)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport_user_config_data *user_config;
	struct ethtool_rx_flow_spec *fsp = &cmd->fs;
	struct virtchnl2_flow_rule_add_del *rule;
	struct idpf_vport_config *vport_config;
	struct virtchnl2_flow_rule_info *info;
	struct idpf_fsteer_fltr *f, *iter;
	struct idpf_vport *vport;
	int err;

	vport = idpf_netdev_to_vport(netdev);
	vport_config = vport->adapter->vport_config[np->vport_idx];
	user_config = &vport_config->user_config;

	if (!idpf_sideband_action_ena(vport, fsp))
		return -EOPNOTSUPP;

	rule = kzalloc(struct_size(rule, rule_info, 1), GFP_KERNEL);
	if (!rule)
		return -ENOMEM;

	rule->vport_id = cpu_to_le32(vport->vport_id);
	rule->count = cpu_to_le32(1);
	info = &rule->rule_info[0];
	info->rule_id = cpu_to_le32(fsp->location);

	err = idpf_add_del_fsteer_filters(vport->adapter, rule,
					  VIRTCHNL2_OP_DEL_FLOW_RULE);
	if (err)
		goto out;

	if (info->status != cpu_to_le32(VIRTCHNL2_FLOW_RULE_SUCCESS)) {
		err = -EIO;
		goto out;
	}

	list_for_each_entry_safe(f, iter,
				 &user_config->flow_steer_list, list) {
		if (f->loc == fsp->location) {
			list_del(&f->list);
			kfree(f);
			user_config->num_fsteer_fltrs--;
			goto out;
		}
	}
	err = -EINVAL;

out:
	kfree(rule);
	return err;
}

static int idpf_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd)
{
	int ret = -EOPNOTSUPP;

	idpf_vport_ctrl_lock(netdev);
	switch (cmd->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		ret = idpf_add_flow_steer(netdev, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		ret = idpf_del_flow_steer(netdev, cmd);
		break;
	default:
		break;
	}

	idpf_vport_ctrl_unlock(netdev);
	return ret;
}

/**
 * idpf_get_rxfh_key_size - get the RSS hash key size
 * @netdev: network interface device structure
 *
 * Returns the key size on success, error value on failure.
 */
static u32 idpf_get_rxfh_key_size(struct net_device *netdev)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport_user_config_data *user_config;

	if (!idpf_is_cap_ena_all(np->adapter, IDPF_RSS_CAPS, IDPF_CAP_RSS))
		return 0;

	user_config = &np->adapter->vport_config[np->vport_idx]->user_config;

	return user_config->rss_data.rss_key_size;
}

/**
 * idpf_get_rxfh_indir_size - get the rx flow hash indirection table size
 * @netdev: network interface device structure
 *
 * Returns the table size on success, error value on failure.
 */
static u32 idpf_get_rxfh_indir_size(struct net_device *netdev)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport_user_config_data *user_config;

	if (!idpf_is_cap_ena_all(np->adapter, IDPF_RSS_CAPS, IDPF_CAP_RSS))
		return 0;

	user_config = &np->adapter->vport_config[np->vport_idx]->user_config;

	return user_config->rss_data.rss_lut_size;
}

/**
 * idpf_get_rxfh - get the rx flow hash indirection table
 * @netdev: network interface device structure
 * @rxfh: pointer to param struct (indir, key, hfunc)
 *
 * Reads the indirection table directly from the hardware. Always returns 0.
 */
static int idpf_get_rxfh(struct net_device *netdev,
			 struct ethtool_rxfh_param *rxfh)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_rss_data *rss_data;
	struct idpf_adapter *adapter;
	int err = 0;
	u16 i;

	idpf_vport_ctrl_lock(netdev);

	adapter = np->adapter;

	if (!idpf_is_cap_ena_all(adapter, IDPF_RSS_CAPS, IDPF_CAP_RSS)) {
		err = -EOPNOTSUPP;
		goto unlock_mutex;
	}

	rss_data = &adapter->vport_config[np->vport_idx]->user_config.rss_data;
	if (np->state != __IDPF_VPORT_UP)
		goto unlock_mutex;

	rxfh->hfunc = ETH_RSS_HASH_TOP;

	if (rxfh->key)
		memcpy(rxfh->key, rss_data->rss_key, rss_data->rss_key_size);

	if (rxfh->indir) {
		for (i = 0; i < rss_data->rss_lut_size; i++)
			rxfh->indir[i] = rss_data->rss_lut[i];
	}

unlock_mutex:
	idpf_vport_ctrl_unlock(netdev);

	return err;
}

/**
 * idpf_set_rxfh - set the rx flow hash indirection table
 * @netdev: network interface device structure
 * @rxfh: pointer to param struct (indir, key, hfunc)
 * @extack: extended ACK from the Netlink message
 *
 * Returns -EINVAL if the table specifies an invalid queue id, otherwise
 * returns 0 after programming the table.
 */
static int idpf_set_rxfh(struct net_device *netdev,
			 struct ethtool_rxfh_param *rxfh,
			 struct netlink_ext_ack *extack)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_rss_data *rss_data;
	struct idpf_adapter *adapter;
	struct idpf_vport *vport;
	int err = 0;
	u16 lut;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);

	adapter = vport->adapter;

	if (!idpf_is_cap_ena_all(adapter, IDPF_RSS_CAPS, IDPF_CAP_RSS)) {
		err = -EOPNOTSUPP;
		goto unlock_mutex;
	}

	rss_data = &adapter->vport_config[vport->idx]->user_config.rss_data;
	if (np->state != __IDPF_VPORT_UP)
		goto unlock_mutex;

	if (rxfh->hfunc != ETH_RSS_HASH_NO_CHANGE &&
	    rxfh->hfunc != ETH_RSS_HASH_TOP) {
		err = -EOPNOTSUPP;
		goto unlock_mutex;
	}

	if (rxfh->key)
		memcpy(rss_data->rss_key, rxfh->key, rss_data->rss_key_size);

	if (rxfh->indir) {
		for (lut = 0; lut < rss_data->rss_lut_size; lut++)
			rss_data->rss_lut[lut] = rxfh->indir[lut];
	}

	err = idpf_config_rss(vport);

unlock_mutex:
	idpf_vport_ctrl_unlock(netdev);

	return err;
}

/**
 * idpf_get_channels: get the number of channels supported by the device
 * @netdev: network interface device structure
 * @ch: channel information structure
 *
 * Report maximum of TX and RX. Report one extra channel to match our MailBox
 * Queue.
 */
static void idpf_get_channels(struct net_device *netdev,
			      struct ethtool_channels *ch)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport_config *vport_config;
	u16 num_txq, num_rxq;
	u16 combined;

	vport_config = np->adapter->vport_config[np->vport_idx];

	num_txq = vport_config->user_config.num_req_tx_qs;
	num_rxq = vport_config->user_config.num_req_rx_qs;

	combined = min(num_txq, num_rxq);

	/* Report maximum channels */
	ch->max_combined = min_t(u16, vport_config->max_q.max_txq,
				 vport_config->max_q.max_rxq);
	ch->max_rx = vport_config->max_q.max_rxq;
	ch->max_tx = vport_config->max_q.max_txq;

	ch->max_other = IDPF_MAX_MBXQ;
	ch->other_count = IDPF_MAX_MBXQ;

	ch->combined_count = combined;
	ch->rx_count = num_rxq - combined;
	ch->tx_count = num_txq - combined;
}

/**
 * idpf_set_channels: set the new channel count
 * @netdev: network interface device structure
 * @ch: channel information structure
 *
 * Negotiate a new number of channels with CP. Returns 0 on success, negative
 * on failure.
 */
static int idpf_set_channels(struct net_device *netdev,
			     struct ethtool_channels *ch)
{
	struct idpf_vport_config *vport_config;
	unsigned int num_req_tx_q;
	unsigned int num_req_rx_q;
	struct idpf_vport *vport;
	u16 num_txq, num_rxq;
	struct device *dev;
	int err = 0;
	u16 idx;

	if (ch->rx_count && ch->tx_count) {
		netdev_err(netdev, "Dedicated RX or TX channels cannot be used simultaneously\n");
		return -EINVAL;
	}

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);

	idx = vport->idx;
	vport_config = vport->adapter->vport_config[idx];

	num_txq = vport_config->user_config.num_req_tx_qs;
	num_rxq = vport_config->user_config.num_req_rx_qs;

	num_req_tx_q = ch->combined_count + ch->tx_count;
	num_req_rx_q = ch->combined_count + ch->rx_count;

	dev = &vport->adapter->pdev->dev;
	/* It's possible to specify number of queues that exceeds max.
	 * Stack checks max combined_count and max [tx|rx]_count but not the
	 * max combined_count + [tx|rx]_count. These checks should catch that.
	 */
	if (num_req_tx_q > vport_config->max_q.max_txq) {
		dev_info(dev, "Maximum TX queues is %d\n",
			 vport_config->max_q.max_txq);
		err = -EINVAL;
		goto unlock_mutex;
	}
	if (num_req_rx_q > vport_config->max_q.max_rxq) {
		dev_info(dev, "Maximum RX queues is %d\n",
			 vport_config->max_q.max_rxq);
		err = -EINVAL;
		goto unlock_mutex;
	}

	if (num_req_tx_q == num_txq && num_req_rx_q == num_rxq)
		goto unlock_mutex;

	vport_config->user_config.num_req_tx_qs = num_req_tx_q;
	vport_config->user_config.num_req_rx_qs = num_req_rx_q;

	err = idpf_initiate_soft_reset(vport, IDPF_SR_Q_CHANGE);
	if (err) {
		/* roll back queue change */
		vport_config->user_config.num_req_tx_qs = num_txq;
		vport_config->user_config.num_req_rx_qs = num_rxq;
	}

unlock_mutex:
	idpf_vport_ctrl_unlock(netdev);

	return err;
}

/**
 * idpf_get_ringparam - Get ring parameters
 * @netdev: network interface device structure
 * @ring: ethtool ringparam structure
 * @kring: unused
 * @ext_ack: unused
 *
 * Returns current ring parameters. TX and RX rings are reported separately,
 * but the number of rings is not reported.
 */
static void idpf_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kring,
			       struct netlink_ext_ack *ext_ack)
{
	struct idpf_vport *vport;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);

	ring->rx_max_pending = IDPF_MAX_RXQ_DESC;
	ring->tx_max_pending = IDPF_MAX_TXQ_DESC;
	ring->rx_pending = vport->rxq_desc_count;
	ring->tx_pending = vport->txq_desc_count;

	kring->tcp_data_split = idpf_vport_get_hsplit(vport);

	idpf_vport_ctrl_unlock(netdev);
}

/**
 * idpf_set_ringparam - Set ring parameters
 * @netdev: network interface device structure
 * @ring: ethtool ringparam structure
 * @kring: unused
 * @ext_ack: unused
 *
 * Sets ring parameters. TX and RX rings are controlled separately, but the
 * number of rings is not specified, so all rings get the same settings.
 */
static int idpf_set_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring,
			      struct kernel_ethtool_ringparam *kring,
			      struct netlink_ext_ack *ext_ack)
{
	struct idpf_vport_user_config_data *config_data;
	u32 new_rx_count, new_tx_count;
	struct idpf_vport *vport;
	int i, err = 0;
	u16 idx;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);

	idx = vport->idx;

	if (ring->tx_pending < IDPF_MIN_TXQ_DESC) {
		netdev_err(netdev, "Descriptors requested (Tx: %u) is less than min supported (%u)\n",
			   ring->tx_pending,
			   IDPF_MIN_TXQ_DESC);
		err = -EINVAL;
		goto unlock_mutex;
	}

	if (ring->rx_pending < IDPF_MIN_RXQ_DESC) {
		netdev_err(netdev, "Descriptors requested (Rx: %u) is less than min supported (%u)\n",
			   ring->rx_pending,
			   IDPF_MIN_RXQ_DESC);
		err = -EINVAL;
		goto unlock_mutex;
	}

	new_rx_count = ALIGN(ring->rx_pending, IDPF_REQ_RXQ_DESC_MULTIPLE);
	if (new_rx_count != ring->rx_pending)
		netdev_info(netdev, "Requested Rx descriptor count rounded up to %u\n",
			    new_rx_count);

	new_tx_count = ALIGN(ring->tx_pending, IDPF_REQ_DESC_MULTIPLE);
	if (new_tx_count != ring->tx_pending)
		netdev_info(netdev, "Requested Tx descriptor count rounded up to %u\n",
			    new_tx_count);

	if (new_tx_count == vport->txq_desc_count &&
	    new_rx_count == vport->rxq_desc_count &&
	    kring->tcp_data_split == idpf_vport_get_hsplit(vport))
		goto unlock_mutex;

	if (!idpf_vport_set_hsplit(vport, kring->tcp_data_split)) {
		NL_SET_ERR_MSG_MOD(ext_ack,
				   "setting TCP data split is not supported");
		err = -EOPNOTSUPP;

		goto unlock_mutex;
	}

	config_data = &vport->adapter->vport_config[idx]->user_config;
	config_data->num_req_txq_desc = new_tx_count;
	config_data->num_req_rxq_desc = new_rx_count;

	/* Since we adjusted the RX completion queue count, the RX buffer queue
	 * descriptor count needs to be adjusted as well
	 */
	for (i = 0; i < vport->num_bufqs_per_qgrp; i++)
		vport->bufq_desc_count[i] =
			IDPF_RX_BUFQ_DESC_COUNT(new_rx_count,
						vport->num_bufqs_per_qgrp);

	err = idpf_initiate_soft_reset(vport, IDPF_SR_Q_DESC_CHANGE);

unlock_mutex:
	idpf_vport_ctrl_unlock(netdev);

	return err;
}

/**
 * struct idpf_stats - definition for an ethtool statistic
 * @stat_string: statistic name to display in ethtool -S output
 * @sizeof_stat: the sizeof() the stat, must be no greater than sizeof(u64)
 * @stat_offset: offsetof() the stat from a base pointer
 *
 * This structure defines a statistic to be added to the ethtool stats buffer.
 * It defines a statistic as offset from a common base pointer. Stats should
 * be defined in constant arrays using the IDPF_STAT macro, with every element
 * of the array using the same _type for calculating the sizeof_stat and
 * stat_offset.
 *
 * The @sizeof_stat is expected to be sizeof(u8), sizeof(u16), sizeof(u32) or
 * sizeof(u64). Other sizes are not expected and will produce a WARN_ONCE from
 * the idpf_add_ethtool_stat() helper function.
 *
 * The @stat_string is interpreted as a format string, allowing formatted
 * values to be inserted while looping over multiple structures for a given
 * statistics array. Thus, every statistic string in an array should have the
 * same type and number of format specifiers, to be formatted by variadic
 * arguments to the idpf_add_stat_string() helper function.
 */
struct idpf_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

/* Helper macro to define an idpf_stat structure with proper size and type.
 * Use this when defining constant statistics arrays. Note that @_type expects
 * only a type name and is used multiple times.
 */
#define IDPF_STAT(_type, _name, _stat) { \
	.stat_string = _name, \
	.sizeof_stat = sizeof_field(_type, _stat), \
	.stat_offset = offsetof(_type, _stat) \
}

/* Helper macros for defining some statistics related to queues */
#define IDPF_RX_QUEUE_STAT(_name, _stat) \
	IDPF_STAT(struct idpf_rx_queue, _name, _stat)
#define IDPF_TX_QUEUE_STAT(_name, _stat) \
	IDPF_STAT(struct idpf_tx_queue, _name, _stat)

/* Stats associated with a Tx queue */
static const struct idpf_stats idpf_gstrings_tx_queue_stats[] = {
	IDPF_TX_QUEUE_STAT("pkts", q_stats.packets),
	IDPF_TX_QUEUE_STAT("bytes", q_stats.bytes),
	IDPF_TX_QUEUE_STAT("lso_pkts", q_stats.lso_pkts),
};

/* Stats associated with an Rx queue */
static const struct idpf_stats idpf_gstrings_rx_queue_stats[] = {
	IDPF_RX_QUEUE_STAT("pkts", q_stats.packets),
	IDPF_RX_QUEUE_STAT("bytes", q_stats.bytes),
	IDPF_RX_QUEUE_STAT("rx_gro_hw_pkts", q_stats.rsc_pkts),
};

#define IDPF_TX_QUEUE_STATS_LEN		ARRAY_SIZE(idpf_gstrings_tx_queue_stats)
#define IDPF_RX_QUEUE_STATS_LEN		ARRAY_SIZE(idpf_gstrings_rx_queue_stats)

#define IDPF_PORT_STAT(_name, _stat) \
	IDPF_STAT(struct idpf_vport,  _name, _stat)

static const struct idpf_stats idpf_gstrings_port_stats[] = {
	IDPF_PORT_STAT("rx-csum_errors", port_stats.rx_hw_csum_err),
	IDPF_PORT_STAT("rx-hsplit", port_stats.rx_hsplit),
	IDPF_PORT_STAT("rx-hsplit_hbo", port_stats.rx_hsplit_hbo),
	IDPF_PORT_STAT("rx-bad_descs", port_stats.rx_bad_descs),
	IDPF_PORT_STAT("tx-skb_drops", port_stats.tx_drops),
	IDPF_PORT_STAT("tx-dma_map_errs", port_stats.tx_dma_map_errs),
	IDPF_PORT_STAT("tx-linearized_pkts", port_stats.tx_linearize),
	IDPF_PORT_STAT("tx-busy_events", port_stats.tx_busy),
	IDPF_PORT_STAT("rx-unicast_pkts", port_stats.vport_stats.rx_unicast),
	IDPF_PORT_STAT("rx-multicast_pkts", port_stats.vport_stats.rx_multicast),
	IDPF_PORT_STAT("rx-broadcast_pkts", port_stats.vport_stats.rx_broadcast),
	IDPF_PORT_STAT("rx-unknown_protocol", port_stats.vport_stats.rx_unknown_protocol),
	IDPF_PORT_STAT("tx-unicast_pkts", port_stats.vport_stats.tx_unicast),
	IDPF_PORT_STAT("tx-multicast_pkts", port_stats.vport_stats.tx_multicast),
	IDPF_PORT_STAT("tx-broadcast_pkts", port_stats.vport_stats.tx_broadcast),
};

#define IDPF_PORT_STATS_LEN ARRAY_SIZE(idpf_gstrings_port_stats)

/**
 * __idpf_add_qstat_strings - copy stat strings into ethtool buffer
 * @p: ethtool supplied buffer
 * @stats: stat definitions array
 * @size: size of the stats array
 * @type: stat type
 * @idx: stat index
 *
 * Format and copy the strings described by stats into the buffer pointed at
 * by p.
 */
static void __idpf_add_qstat_strings(u8 **p, const struct idpf_stats *stats,
				     const unsigned int size, const char *type,
				     unsigned int idx)
{
	unsigned int i;

	for (i = 0; i < size; i++)
		ethtool_sprintf(p, "%s_q-%u_%s",
				type, idx, stats[i].stat_string);
}

/**
 * idpf_add_qstat_strings - Copy queue stat strings into ethtool buffer
 * @p: ethtool supplied buffer
 * @stats: stat definitions array
 * @type: stat type
 * @idx: stat idx
 *
 * Format and copy the strings described by the const static stats value into
 * the buffer pointed at by p.
 *
 * The parameter @stats is evaluated twice, so parameters with side effects
 * should be avoided. Additionally, stats must be an array such that
 * ARRAY_SIZE can be called on it.
 */
#define idpf_add_qstat_strings(p, stats, type, idx) \
	__idpf_add_qstat_strings(p, stats, ARRAY_SIZE(stats), type, idx)

/**
 * idpf_add_stat_strings - Copy port stat strings into ethtool buffer
 * @p: ethtool buffer
 * @stats: struct to copy from
 * @size: size of stats array to copy from
 */
static void idpf_add_stat_strings(u8 **p, const struct idpf_stats *stats,
				  const unsigned int size)
{
	unsigned int i;

	for (i = 0; i < size; i++)
		ethtool_puts(p, stats[i].stat_string);
}

/**
 * idpf_get_stat_strings - Get stat strings
 * @netdev: network interface device structure
 * @data: buffer for string data
 *
 * Builds the statistics string table
 */
static void idpf_get_stat_strings(struct net_device *netdev, u8 *data)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport_config *vport_config;
	unsigned int i;

	idpf_add_stat_strings(&data, idpf_gstrings_port_stats,
			      IDPF_PORT_STATS_LEN);

	vport_config = np->adapter->vport_config[np->vport_idx];
	/* It's critical that we always report a constant number of strings and
	 * that the strings are reported in the same order regardless of how
	 * many queues are actually in use.
	 */
	for (i = 0; i < vport_config->max_q.max_txq; i++)
		idpf_add_qstat_strings(&data, idpf_gstrings_tx_queue_stats,
				       "tx", i);

	for (i = 0; i < vport_config->max_q.max_rxq; i++)
		idpf_add_qstat_strings(&data, idpf_gstrings_rx_queue_stats,
				       "rx", i);
}

/**
 * idpf_get_strings - Get string set
 * @netdev: network interface device structure
 * @sset: id of string set
 * @data: buffer for string data
 *
 * Builds string tables for various string sets
 */
static void idpf_get_strings(struct net_device *netdev, u32 sset, u8 *data)
{
	switch (sset) {
	case ETH_SS_STATS:
		idpf_get_stat_strings(netdev, data);
		break;
	default:
		break;
	}
}

/**
 * idpf_get_sset_count - Get length of string set
 * @netdev: network interface device structure
 * @sset: id of string set
 *
 * Reports size of various string tables.
 */
static int idpf_get_sset_count(struct net_device *netdev, int sset)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport_config *vport_config;
	u16 max_txq, max_rxq;

	if (sset != ETH_SS_STATS)
		return -EINVAL;

	vport_config = np->adapter->vport_config[np->vport_idx];
	/* This size reported back here *must* be constant throughout the
	 * lifecycle of the netdevice, i.e. we must report the maximum length
	 * even for queues that don't technically exist.  This is due to the
	 * fact that this userspace API uses three separate ioctl calls to get
	 * stats data but has no way to communicate back to userspace when that
	 * size has changed, which can typically happen as a result of changing
	 * number of queues. If the number/order of stats change in the middle
	 * of this call chain it will lead to userspace crashing/accessing bad
	 * data through buffer under/overflow.
	 */
	max_txq = vport_config->max_q.max_txq;
	max_rxq = vport_config->max_q.max_rxq;

	return IDPF_PORT_STATS_LEN + (IDPF_TX_QUEUE_STATS_LEN * max_txq) +
	       (IDPF_RX_QUEUE_STATS_LEN * max_rxq);
}

/**
 * idpf_add_one_ethtool_stat - copy the stat into the supplied buffer
 * @data: location to store the stat value
 * @pstat: old stat pointer to copy from
 * @stat: the stat definition
 *
 * Copies the stat data defined by the pointer and stat structure pair into
 * the memory supplied as data. If the pointer is null, data will be zero'd.
 */
static void idpf_add_one_ethtool_stat(u64 *data, const void *pstat,
				      const struct idpf_stats *stat)
{
	char *p;

	if (!pstat) {
		/* Ensure that the ethtool data buffer is zero'd for any stats
		 * which don't have a valid pointer.
		 */
		*data = 0;
		return;
	}

	p = (char *)pstat + stat->stat_offset;
	switch (stat->sizeof_stat) {
	case sizeof(u64):
		*data = *((u64 *)p);
		break;
	case sizeof(u32):
		*data = *((u32 *)p);
		break;
	case sizeof(u16):
		*data = *((u16 *)p);
		break;
	case sizeof(u8):
		*data = *((u8 *)p);
		break;
	default:
		WARN_ONCE(1, "unexpected stat size for %s",
			  stat->stat_string);
		*data = 0;
	}
}

/**
 * idpf_add_queue_stats - copy queue statistics into supplied buffer
 * @data: ethtool stats buffer
 * @q: the queue to copy
 * @type: type of the queue
 *
 * Queue statistics must be copied while protected by u64_stats_fetch_begin,
 * so we can't directly use idpf_add_ethtool_stats. Assumes that queue stats
 * are defined in idpf_gstrings_queue_stats. If the queue pointer is null,
 * zero out the queue stat values and update the data pointer. Otherwise
 * safely copy the stats from the queue into the supplied buffer and update
 * the data pointer when finished.
 *
 * This function expects to be called while under rcu_read_lock().
 */
static void idpf_add_queue_stats(u64 **data, const void *q,
				 enum virtchnl2_queue_type type)
{
	const struct u64_stats_sync *stats_sync;
	const struct idpf_stats *stats;
	unsigned int start;
	unsigned int size;
	unsigned int i;

	if (type == VIRTCHNL2_QUEUE_TYPE_RX) {
		size = IDPF_RX_QUEUE_STATS_LEN;
		stats = idpf_gstrings_rx_queue_stats;
		stats_sync = &((const struct idpf_rx_queue *)q)->stats_sync;
	} else {
		size = IDPF_TX_QUEUE_STATS_LEN;
		stats = idpf_gstrings_tx_queue_stats;
		stats_sync = &((const struct idpf_tx_queue *)q)->stats_sync;
	}

	/* To avoid invalid statistics values, ensure that we keep retrying
	 * the copy until we get a consistent value according to
	 * u64_stats_fetch_retry.
	 */
	do {
		start = u64_stats_fetch_begin(stats_sync);
		for (i = 0; i < size; i++)
			idpf_add_one_ethtool_stat(&(*data)[i], q, &stats[i]);
	} while (u64_stats_fetch_retry(stats_sync, start));

	/* Once we successfully copy the stats in, update the data pointer */
	*data += size;
}

/**
 * idpf_add_empty_queue_stats - Add stats for a non-existent queue
 * @data: pointer to data buffer
 * @qtype: type of data queue
 *
 * We must report a constant length of stats back to userspace regardless of
 * how many queues are actually in use because stats collection happens over
 * three separate ioctls and there's no way to notify userspace the size
 * changed between those calls. This adds empty to data to the stats since we
 * don't have a real queue to refer to for this stats slot.
 */
static void idpf_add_empty_queue_stats(u64 **data, u16 qtype)
{
	unsigned int i;
	int stats_len;

	if (qtype == VIRTCHNL2_QUEUE_TYPE_RX)
		stats_len = IDPF_RX_QUEUE_STATS_LEN;
	else
		stats_len = IDPF_TX_QUEUE_STATS_LEN;

	for (i = 0; i < stats_len; i++)
		(*data)[i] = 0;
	*data += stats_len;
}

/**
 * idpf_add_port_stats - Copy port stats into ethtool buffer
 * @vport: virtual port struct
 * @data: ethtool buffer to copy into
 */
static void idpf_add_port_stats(struct idpf_vport *vport, u64 **data)
{
	unsigned int size = IDPF_PORT_STATS_LEN;
	unsigned int start;
	unsigned int i;

	do {
		start = u64_stats_fetch_begin(&vport->port_stats.stats_sync);
		for (i = 0; i < size; i++)
			idpf_add_one_ethtool_stat(&(*data)[i], vport,
						  &idpf_gstrings_port_stats[i]);
	} while (u64_stats_fetch_retry(&vport->port_stats.stats_sync, start));

	*data += size;
}

/**
 * idpf_collect_queue_stats - accumulate various per queue stats
 * into port level stats
 * @vport: pointer to vport struct
 **/
static void idpf_collect_queue_stats(struct idpf_vport *vport)
{
	struct idpf_port_stats *pstats = &vport->port_stats;
	int i, j;

	/* zero out port stats since they're actually tracked in per
	 * queue stats; this is only for reporting
	 */
	u64_stats_update_begin(&pstats->stats_sync);
	u64_stats_set(&pstats->rx_hw_csum_err, 0);
	u64_stats_set(&pstats->rx_hsplit, 0);
	u64_stats_set(&pstats->rx_hsplit_hbo, 0);
	u64_stats_set(&pstats->rx_bad_descs, 0);
	u64_stats_set(&pstats->tx_linearize, 0);
	u64_stats_set(&pstats->tx_busy, 0);
	u64_stats_set(&pstats->tx_drops, 0);
	u64_stats_set(&pstats->tx_dma_map_errs, 0);
	u64_stats_update_end(&pstats->stats_sync);

	for (i = 0; i < vport->num_rxq_grp; i++) {
		struct idpf_rxq_group *rxq_grp = &vport->rxq_grps[i];
		u16 num_rxq;

		if (idpf_is_queue_model_split(vport->rxq_model))
			num_rxq = rxq_grp->splitq.num_rxq_sets;
		else
			num_rxq = rxq_grp->singleq.num_rxq;

		for (j = 0; j < num_rxq; j++) {
			u64 hw_csum_err, hsplit, hsplit_hbo, bad_descs;
			struct idpf_rx_queue_stats *stats;
			struct idpf_rx_queue *rxq;
			unsigned int start;

			if (idpf_is_queue_model_split(vport->rxq_model))
				rxq = &rxq_grp->splitq.rxq_sets[j]->rxq;
			else
				rxq = rxq_grp->singleq.rxqs[j];

			if (!rxq)
				continue;

			do {
				start = u64_stats_fetch_begin(&rxq->stats_sync);

				stats = &rxq->q_stats;
				hw_csum_err = u64_stats_read(&stats->hw_csum_err);
				hsplit = u64_stats_read(&stats->hsplit_pkts);
				hsplit_hbo = u64_stats_read(&stats->hsplit_buf_ovf);
				bad_descs = u64_stats_read(&stats->bad_descs);
			} while (u64_stats_fetch_retry(&rxq->stats_sync, start));

			u64_stats_update_begin(&pstats->stats_sync);
			u64_stats_add(&pstats->rx_hw_csum_err, hw_csum_err);
			u64_stats_add(&pstats->rx_hsplit, hsplit);
			u64_stats_add(&pstats->rx_hsplit_hbo, hsplit_hbo);
			u64_stats_add(&pstats->rx_bad_descs, bad_descs);
			u64_stats_update_end(&pstats->stats_sync);
		}
	}

	for (i = 0; i < vport->num_txq_grp; i++) {
		struct idpf_txq_group *txq_grp = &vport->txq_grps[i];

		for (j = 0; j < txq_grp->num_txq; j++) {
			u64 linearize, qbusy, skb_drops, dma_map_errs;
			struct idpf_tx_queue *txq = txq_grp->txqs[j];
			struct idpf_tx_queue_stats *stats;
			unsigned int start;

			if (!txq)
				continue;

			do {
				start = u64_stats_fetch_begin(&txq->stats_sync);

				stats = &txq->q_stats;
				linearize = u64_stats_read(&stats->linearize);
				qbusy = u64_stats_read(&stats->q_busy);
				skb_drops = u64_stats_read(&stats->skb_drops);
				dma_map_errs = u64_stats_read(&stats->dma_map_errs);
			} while (u64_stats_fetch_retry(&txq->stats_sync, start));

			u64_stats_update_begin(&pstats->stats_sync);
			u64_stats_add(&pstats->tx_linearize, linearize);
			u64_stats_add(&pstats->tx_busy, qbusy);
			u64_stats_add(&pstats->tx_drops, skb_drops);
			u64_stats_add(&pstats->tx_dma_map_errs, dma_map_errs);
			u64_stats_update_end(&pstats->stats_sync);
		}
	}
}

/**
 * idpf_get_ethtool_stats - report device statistics
 * @netdev: network interface device structure
 * @stats: ethtool statistics structure
 * @data: pointer to data buffer
 *
 * All statistics are added to the data buffer as an array of u64.
 */
static void idpf_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats __always_unused *stats,
				   u64 *data)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport_config *vport_config;
	struct idpf_vport *vport;
	unsigned int total = 0;
	unsigned int i, j;
	bool is_splitq;
	u16 qtype;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);

	if (np->state != __IDPF_VPORT_UP) {
		idpf_vport_ctrl_unlock(netdev);

		return;
	}

	rcu_read_lock();

	idpf_collect_queue_stats(vport);
	idpf_add_port_stats(vport, &data);

	for (i = 0; i < vport->num_txq_grp; i++) {
		struct idpf_txq_group *txq_grp = &vport->txq_grps[i];

		qtype = VIRTCHNL2_QUEUE_TYPE_TX;

		for (j = 0; j < txq_grp->num_txq; j++, total++) {
			struct idpf_tx_queue *txq = txq_grp->txqs[j];

			if (!txq)
				idpf_add_empty_queue_stats(&data, qtype);
			else
				idpf_add_queue_stats(&data, txq, qtype);
		}
	}

	vport_config = vport->adapter->vport_config[vport->idx];
	/* It is critical we provide a constant number of stats back to
	 * userspace regardless of how many queues are actually in use because
	 * there is no way to inform userspace the size has changed between
	 * ioctl calls. This will fill in any missing stats with zero.
	 */
	for (; total < vport_config->max_q.max_txq; total++)
		idpf_add_empty_queue_stats(&data, VIRTCHNL2_QUEUE_TYPE_TX);
	total = 0;

	is_splitq = idpf_is_queue_model_split(vport->rxq_model);

	for (i = 0; i < vport->num_rxq_grp; i++) {
		struct idpf_rxq_group *rxq_grp = &vport->rxq_grps[i];
		u16 num_rxq;

		qtype = VIRTCHNL2_QUEUE_TYPE_RX;

		if (is_splitq)
			num_rxq = rxq_grp->splitq.num_rxq_sets;
		else
			num_rxq = rxq_grp->singleq.num_rxq;

		for (j = 0; j < num_rxq; j++, total++) {
			struct idpf_rx_queue *rxq;

			if (is_splitq)
				rxq = &rxq_grp->splitq.rxq_sets[j]->rxq;
			else
				rxq = rxq_grp->singleq.rxqs[j];
			if (!rxq)
				idpf_add_empty_queue_stats(&data, qtype);
			else
				idpf_add_queue_stats(&data, rxq, qtype);
		}
	}

	for (; total < vport_config->max_q.max_rxq; total++)
		idpf_add_empty_queue_stats(&data, VIRTCHNL2_QUEUE_TYPE_RX);

	rcu_read_unlock();

	idpf_vport_ctrl_unlock(netdev);
}

/**
 * idpf_find_rxq_vec - find rxq vector from q index
 * @vport: virtual port associated to queue
 * @q_num: q index used to find queue
 *
 * returns pointer to rx vector
 */
struct idpf_q_vector *idpf_find_rxq_vec(const struct idpf_vport *vport,
					u32 q_num)
{
	int q_grp, q_idx;

	if (!idpf_is_queue_model_split(vport->rxq_model))
		return vport->rxq_grps->singleq.rxqs[q_num]->q_vector;

	q_grp = q_num / IDPF_DFLT_SPLITQ_RXQ_PER_GROUP;
	q_idx = q_num % IDPF_DFLT_SPLITQ_RXQ_PER_GROUP;

	return vport->rxq_grps[q_grp].splitq.rxq_sets[q_idx]->rxq.q_vector;
}

/**
 * idpf_find_txq_vec - find txq vector from q index
 * @vport: virtual port associated to queue
 * @q_num: q index used to find queue
 *
 * returns pointer to tx vector
 */
struct idpf_q_vector *idpf_find_txq_vec(const struct idpf_vport *vport,
					u32 q_num)
{
	int q_grp;

	if (!idpf_is_queue_model_split(vport->txq_model))
		return vport->txqs[q_num]->q_vector;

	q_grp = q_num / IDPF_DFLT_SPLITQ_TXQ_PER_GROUP;

	return vport->txq_grps[q_grp].complq->q_vector;
}

/**
 * __idpf_get_q_coalesce - get ITR values for specific queue
 * @ec: ethtool structure to fill with driver's coalesce settings
 * @q_vector: queue vector corresponding to this queue
 * @type: queue type
 */
static void __idpf_get_q_coalesce(struct ethtool_coalesce *ec,
				  const struct idpf_q_vector *q_vector,
				  enum virtchnl2_queue_type type)
{
	if (type == VIRTCHNL2_QUEUE_TYPE_RX) {
		ec->use_adaptive_rx_coalesce =
				IDPF_ITR_IS_DYNAMIC(q_vector->rx_intr_mode);
		ec->rx_coalesce_usecs = q_vector->rx_itr_value;
	} else {
		ec->use_adaptive_tx_coalesce =
				IDPF_ITR_IS_DYNAMIC(q_vector->tx_intr_mode);
		ec->tx_coalesce_usecs = q_vector->tx_itr_value;
	}
}

/**
 * idpf_get_q_coalesce - get ITR values for specific queue
 * @netdev: pointer to the netdev associated with this query
 * @ec: coalesce settings to program the device with
 * @q_num: update ITR/INTRL (coalesce) settings for this queue number/index
 *
 * Return 0 on success, and negative on failure
 */
static int idpf_get_q_coalesce(struct net_device *netdev,
			       struct ethtool_coalesce *ec,
			       u32 q_num)
{
	const struct idpf_netdev_priv *np = netdev_priv(netdev);
	const struct idpf_vport *vport;
	int err = 0;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);

	if (np->state != __IDPF_VPORT_UP)
		goto unlock_mutex;

	if (q_num >= vport->num_rxq && q_num >= vport->num_txq) {
		err = -EINVAL;
		goto unlock_mutex;
	}

	if (q_num < vport->num_rxq)
		__idpf_get_q_coalesce(ec, idpf_find_rxq_vec(vport, q_num),
				      VIRTCHNL2_QUEUE_TYPE_RX);

	if (q_num < vport->num_txq)
		__idpf_get_q_coalesce(ec, idpf_find_txq_vec(vport, q_num),
				      VIRTCHNL2_QUEUE_TYPE_TX);

unlock_mutex:
	idpf_vport_ctrl_unlock(netdev);

	return err;
}

/**
 * idpf_get_coalesce - get ITR values as requested by user
 * @netdev: pointer to the netdev associated with this query
 * @ec: coalesce settings to be filled
 * @kec: unused
 * @extack: unused
 *
 * Return 0 on success, and negative on failure
 */
static int idpf_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec,
			     struct kernel_ethtool_coalesce *kec,
			     struct netlink_ext_ack *extack)
{
	/* Return coalesce based on queue number zero */
	return idpf_get_q_coalesce(netdev, ec, 0);
}

/**
 * idpf_get_per_q_coalesce - get ITR values as requested by user
 * @netdev: pointer to the netdev associated with this query
 * @q_num: queue for which the itr values has to retrieved
 * @ec: coalesce settings to be filled
 *
 * Return 0 on success, and negative on failure
 */

static int idpf_get_per_q_coalesce(struct net_device *netdev, u32 q_num,
				   struct ethtool_coalesce *ec)
{
	return idpf_get_q_coalesce(netdev, ec, q_num);
}

/**
 * __idpf_set_q_coalesce - set ITR values for specific queue
 * @ec: ethtool structure from user to update ITR settings
 * @q_coal: per queue coalesce settings
 * @qv: queue vector for which itr values has to be set
 * @is_rxq: is queue type rx
 *
 * Returns 0 on success, negative otherwise.
 */
static int __idpf_set_q_coalesce(const struct ethtool_coalesce *ec,
				 struct idpf_q_coalesce *q_coal,
				 struct idpf_q_vector *qv, bool is_rxq)
{
	u32 use_adaptive_coalesce, coalesce_usecs;
	bool is_dim_ena = false;
	u16 itr_val;

	if (is_rxq) {
		is_dim_ena = IDPF_ITR_IS_DYNAMIC(qv->rx_intr_mode);
		use_adaptive_coalesce = ec->use_adaptive_rx_coalesce;
		coalesce_usecs = ec->rx_coalesce_usecs;
		itr_val = qv->rx_itr_value;
	} else {
		is_dim_ena = IDPF_ITR_IS_DYNAMIC(qv->tx_intr_mode);
		use_adaptive_coalesce = ec->use_adaptive_tx_coalesce;
		coalesce_usecs = ec->tx_coalesce_usecs;
		itr_val = qv->tx_itr_value;
	}
	if (coalesce_usecs != itr_val && use_adaptive_coalesce) {
		netdev_err(qv->vport->netdev, "Cannot set coalesce usecs if adaptive enabled\n");

		return -EINVAL;
	}

	if (is_dim_ena && use_adaptive_coalesce)
		return 0;

	if (coalesce_usecs > IDPF_ITR_MAX) {
		netdev_err(qv->vport->netdev,
			   "Invalid value, %d-usecs range is 0-%d\n",
			   coalesce_usecs, IDPF_ITR_MAX);

		return -EINVAL;
	}

	if (coalesce_usecs % 2) {
		coalesce_usecs--;
		netdev_info(qv->vport->netdev,
			    "HW only supports even ITR values, ITR rounded to %d\n",
			    coalesce_usecs);
	}

	if (is_rxq) {
		qv->rx_itr_value = coalesce_usecs;
		q_coal->rx_coalesce_usecs = coalesce_usecs;
		if (use_adaptive_coalesce) {
			qv->rx_intr_mode = IDPF_ITR_DYNAMIC;
			q_coal->rx_intr_mode = IDPF_ITR_DYNAMIC;
		} else {
			qv->rx_intr_mode = !IDPF_ITR_DYNAMIC;
			q_coal->rx_intr_mode = !IDPF_ITR_DYNAMIC;
			idpf_vport_intr_write_itr(qv, coalesce_usecs, false);
		}
	} else {
		qv->tx_itr_value = coalesce_usecs;
		q_coal->tx_coalesce_usecs = coalesce_usecs;
		if (use_adaptive_coalesce) {
			qv->tx_intr_mode = IDPF_ITR_DYNAMIC;
			q_coal->tx_intr_mode = IDPF_ITR_DYNAMIC;
		} else {
			qv->tx_intr_mode = !IDPF_ITR_DYNAMIC;
			q_coal->tx_intr_mode = !IDPF_ITR_DYNAMIC;
			idpf_vport_intr_write_itr(qv, coalesce_usecs, true);
		}
	}

	/* Update of static/dynamic itr will be taken care when interrupt is
	 * fired
	 */
	return 0;
}

/**
 * idpf_set_q_coalesce - set ITR values for specific queue
 * @vport: vport associated to the queue that need updating
 * @q_coal: per queue coalesce settings
 * @ec: coalesce settings to program the device with
 * @q_num: update ITR/INTRL (coalesce) settings for this queue number/index
 * @is_rxq: is queue type rx
 *
 * Return 0 on success, and negative on failure
 */
static int idpf_set_q_coalesce(const struct idpf_vport *vport,
			       struct idpf_q_coalesce *q_coal,
			       const struct ethtool_coalesce *ec,
			       int q_num, bool is_rxq)
{
	struct idpf_q_vector *qv;

	qv = is_rxq ? idpf_find_rxq_vec(vport, q_num) :
		      idpf_find_txq_vec(vport, q_num);

	if (qv && __idpf_set_q_coalesce(ec, q_coal, qv, is_rxq))
		return -EINVAL;

	return 0;
}

/**
 * idpf_set_coalesce - set ITR values as requested by user
 * @netdev: pointer to the netdev associated with this query
 * @ec: coalesce settings to program the device with
 * @kec: unused
 * @extack: unused
 *
 * Return 0 on success, and negative on failure
 */
static int idpf_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec,
			     struct kernel_ethtool_coalesce *kec,
			     struct netlink_ext_ack *extack)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport_user_config_data *user_config;
	struct idpf_q_coalesce *q_coal;
	struct idpf_vport *vport;
	int i, err = 0;

	user_config = &np->adapter->vport_config[np->vport_idx]->user_config;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);

	if (np->state != __IDPF_VPORT_UP)
		goto unlock_mutex;

	for (i = 0; i < vport->num_txq; i++) {
		q_coal = &user_config->q_coalesce[i];
		err = idpf_set_q_coalesce(vport, q_coal, ec, i, false);
		if (err)
			goto unlock_mutex;
	}

	for (i = 0; i < vport->num_rxq; i++) {
		q_coal = &user_config->q_coalesce[i];
		err = idpf_set_q_coalesce(vport, q_coal, ec, i, true);
		if (err)
			goto unlock_mutex;
	}

unlock_mutex:
	idpf_vport_ctrl_unlock(netdev);

	return err;
}

/**
 * idpf_set_per_q_coalesce - set ITR values as requested by user
 * @netdev: pointer to the netdev associated with this query
 * @q_num: queue for which the itr values has to be set
 * @ec: coalesce settings to program the device with
 *
 * Return 0 on success, and negative on failure
 */
static int idpf_set_per_q_coalesce(struct net_device *netdev, u32 q_num,
				   struct ethtool_coalesce *ec)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport_user_config_data *user_config;
	struct idpf_q_coalesce *q_coal;
	struct idpf_vport *vport;
	int err;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);
	user_config = &np->adapter->vport_config[np->vport_idx]->user_config;
	q_coal = &user_config->q_coalesce[q_num];

	err = idpf_set_q_coalesce(vport, q_coal, ec, q_num, false);
	if (err) {
		idpf_vport_ctrl_unlock(netdev);

		return err;
	}

	err = idpf_set_q_coalesce(vport, q_coal, ec, q_num, true);

	idpf_vport_ctrl_unlock(netdev);

	return err;
}

/**
 * idpf_get_msglevel - Get debug message level
 * @netdev: network interface device structure
 *
 * Returns current debug message level.
 */
static u32 idpf_get_msglevel(struct net_device *netdev)
{
	struct idpf_adapter *adapter = idpf_netdev_to_adapter(netdev);

	return adapter->msg_enable;
}

/**
 * idpf_set_msglevel - Set debug message level
 * @netdev: network interface device structure
 * @data: message level
 *
 * Set current debug message level. Higher values cause the driver to
 * be noisier.
 */
static void idpf_set_msglevel(struct net_device *netdev, u32 data)
{
	struct idpf_adapter *adapter = idpf_netdev_to_adapter(netdev);

	adapter->msg_enable = data;
}

/**
 * idpf_get_link_ksettings - Get Link Speed and Duplex settings
 * @netdev: network interface device structure
 * @cmd: ethtool command
 *
 * Reports speed/duplex settings.
 **/
static int idpf_get_link_ksettings(struct net_device *netdev,
				   struct ethtool_link_ksettings *cmd)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);

	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	cmd->base.autoneg = AUTONEG_DISABLE;
	cmd->base.port = PORT_NONE;
	if (netif_carrier_ok(netdev)) {
		cmd->base.duplex = DUPLEX_FULL;
		cmd->base.speed = np->link_speed_mbps;
	} else {
		cmd->base.duplex = DUPLEX_UNKNOWN;
		cmd->base.speed = SPEED_UNKNOWN;
	}

	return 0;
}

/**
 * idpf_get_timestamp_filters - Get the supported timestamping mode
 * @vport: Virtual port structure
 * @info: ethtool timestamping info structure
 *
 * Get the Tx/Rx timestamp filters.
 */
static void idpf_get_timestamp_filters(const struct idpf_vport *vport,
				       struct kernel_ethtool_ts_info *info)
{
	info->so_timestamping = SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	info->tx_types = BIT(HWTSTAMP_TX_OFF);
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) | BIT(HWTSTAMP_FILTER_ALL);

	if (!vport->tx_tstamp_caps ||
	    vport->adapter->ptp->tx_tstamp_access == IDPF_PTP_NONE)
		return;

	info->so_timestamping |= SOF_TIMESTAMPING_TX_SOFTWARE |
				 SOF_TIMESTAMPING_TX_HARDWARE;

	info->tx_types |= BIT(HWTSTAMP_TX_ON);
}

/**
 * idpf_get_ts_info - Get device PHC association
 * @netdev: network interface device structure
 * @info: ethtool timestamping info structure
 *
 * Return: 0 on success, -errno otherwise.
 */
static int idpf_get_ts_info(struct net_device *netdev,
			    struct kernel_ethtool_ts_info *info)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport *vport;
	int err = 0;

	if (!mutex_trylock(&np->adapter->vport_ctrl_lock))
		return -EBUSY;

	vport = idpf_netdev_to_vport(netdev);

	if (!vport->adapter->ptp) {
		err = -EOPNOTSUPP;
		goto unlock;
	}

	if (idpf_is_cap_ena(vport->adapter, IDPF_OTHER_CAPS, VIRTCHNL2_CAP_PTP) &&
	    vport->adapter->ptp->clock) {
		info->phc_index = ptp_clock_index(vport->adapter->ptp->clock);
		idpf_get_timestamp_filters(vport, info);
	} else {
		pci_dbg(vport->adapter->pdev, "PTP clock not detected\n");
		err = ethtool_op_get_ts_info(netdev, info);
	}

unlock:
	mutex_unlock(&np->adapter->vport_ctrl_lock);

	return err;
}

/**
 * idpf_get_ts_stats - Collect HW tstamping statistics
 * @netdev: network interface device structure
 * @ts_stats: HW timestamping stats structure
 *
 * Collect HW timestamping statistics including successfully timestamped
 * packets, discarded due to illegal values, flushed during releasing PTP and
 * skipped due to lack of the free index.
 */
static void idpf_get_ts_stats(struct net_device *netdev,
			      struct ethtool_ts_stats *ts_stats)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport *vport;
	unsigned int start;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);
	do {
		start = u64_stats_fetch_begin(&vport->tstamp_stats.stats_sync);
		ts_stats->pkts = u64_stats_read(&vport->tstamp_stats.packets);
		ts_stats->lost = u64_stats_read(&vport->tstamp_stats.flushed);
		ts_stats->err = u64_stats_read(&vport->tstamp_stats.discarded);
	} while (u64_stats_fetch_retry(&vport->tstamp_stats.stats_sync, start));

	if (np->state != __IDPF_VPORT_UP)
		goto exit;

	for (u16 i = 0; i < vport->num_txq_grp; i++) {
		struct idpf_txq_group *txq_grp = &vport->txq_grps[i];

		for (u16 j = 0; j < txq_grp->num_txq; j++) {
			struct idpf_tx_queue *txq = txq_grp->txqs[j];
			struct idpf_tx_queue_stats *stats;
			u64 ts;

			if (!txq)
				continue;

			stats = &txq->q_stats;
			do {
				start = u64_stats_fetch_begin(&txq->stats_sync);

				ts = u64_stats_read(&stats->tstamp_skipped);
			} while (u64_stats_fetch_retry(&txq->stats_sync,
						       start));

			ts_stats->lost += ts;
		}
	}

exit:
	idpf_vport_ctrl_unlock(netdev);
}

static const struct ethtool_ops idpf_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_USE_ADAPTIVE,
	.supported_ring_params	= ETHTOOL_RING_USE_TCP_DATA_SPLIT,
	.get_msglevel		= idpf_get_msglevel,
	.set_msglevel		= idpf_set_msglevel,
	.get_link		= ethtool_op_get_link,
	.get_coalesce		= idpf_get_coalesce,
	.set_coalesce		= idpf_set_coalesce,
	.get_per_queue_coalesce = idpf_get_per_q_coalesce,
	.set_per_queue_coalesce = idpf_set_per_q_coalesce,
	.get_ethtool_stats	= idpf_get_ethtool_stats,
	.get_strings		= idpf_get_strings,
	.get_sset_count		= idpf_get_sset_count,
	.get_channels		= idpf_get_channels,
	.get_rxnfc		= idpf_get_rxnfc,
	.set_rxnfc		= idpf_set_rxnfc,
	.get_rxfh_key_size	= idpf_get_rxfh_key_size,
	.get_rxfh_indir_size	= idpf_get_rxfh_indir_size,
	.get_rxfh		= idpf_get_rxfh,
	.set_rxfh		= idpf_set_rxfh,
	.set_channels		= idpf_set_channels,
	.get_ringparam		= idpf_get_ringparam,
	.set_ringparam		= idpf_set_ringparam,
	.get_link_ksettings	= idpf_get_link_ksettings,
	.get_ts_info		= idpf_get_ts_info,
	.get_ts_stats		= idpf_get_ts_stats,
};

/**
 * idpf_set_ethtool_ops - Initialize ethtool ops struct
 * @netdev: network interface device structure
 *
 * Sets ethtool ops struct in our netdev so that ethtool can call
 * our functions.
 */
void idpf_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &idpf_ethtool_ops;
}
