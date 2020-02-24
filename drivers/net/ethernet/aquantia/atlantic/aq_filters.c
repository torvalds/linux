// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2014-2019 aQuantia Corporation. */

/* File aq_filters.c: RX filters related functions. */

#include "aq_filters.h"

static bool __must_check
aq_rule_is_approve(struct ethtool_rx_flow_spec *fsp)
{
	if (fsp->flow_type & FLOW_MAC_EXT)
		return false;

	switch (fsp->flow_type & ~FLOW_EXT) {
	case ETHER_FLOW:
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
	case SCTP_V6_FLOW:
	case IPV4_FLOW:
	case IPV6_FLOW:
		return true;
	case IP_USER_FLOW:
		switch (fsp->h_u.usr_ip4_spec.proto) {
		case IPPROTO_TCP:
		case IPPROTO_UDP:
		case IPPROTO_SCTP:
		case IPPROTO_IP:
			return true;
		default:
			return false;
			}
	case IPV6_USER_FLOW:
		switch (fsp->h_u.usr_ip6_spec.l4_proto) {
		case IPPROTO_TCP:
		case IPPROTO_UDP:
		case IPPROTO_SCTP:
		case IPPROTO_IP:
			return true;
		default:
			return false;
			}
	default:
		return false;
	}

	return false;
}

static bool __must_check
aq_match_filter(struct ethtool_rx_flow_spec *fsp1,
		struct ethtool_rx_flow_spec *fsp2)
{
	if (fsp1->flow_type != fsp2->flow_type ||
	    memcmp(&fsp1->h_u, &fsp2->h_u, sizeof(fsp2->h_u)) ||
	    memcmp(&fsp1->h_ext, &fsp2->h_ext, sizeof(fsp2->h_ext)) ||
	    memcmp(&fsp1->m_u, &fsp2->m_u, sizeof(fsp2->m_u)) ||
	    memcmp(&fsp1->m_ext, &fsp2->m_ext, sizeof(fsp2->m_ext)))
		return false;

	return true;
}

static bool __must_check
aq_rule_already_exists(struct aq_nic_s *aq_nic,
		       struct ethtool_rx_flow_spec *fsp)
{
	struct aq_rx_filter *rule;
	struct hlist_node *aq_node2;
	struct aq_hw_rx_fltrs_s *rx_fltrs = aq_get_hw_rx_fltrs(aq_nic);

	hlist_for_each_entry_safe(rule, aq_node2,
				  &rx_fltrs->filter_list, aq_node) {
		if (rule->aq_fsp.location == fsp->location)
			continue;
		if (aq_match_filter(&rule->aq_fsp, fsp)) {
			netdev_err(aq_nic->ndev,
				   "ethtool: This filter is already set\n");
			return true;
		}
	}

	return false;
}

static int aq_check_approve_fl3l4(struct aq_nic_s *aq_nic,
				  struct aq_hw_rx_fltrs_s *rx_fltrs,
				  struct ethtool_rx_flow_spec *fsp)
{
	u32 last_location = AQ_RX_LAST_LOC_FL3L4 -
			    aq_nic->aq_hw_rx_fltrs.fl3l4.reserved_count;

	if (fsp->location < AQ_RX_FIRST_LOC_FL3L4 ||
	    fsp->location > last_location) {
		netdev_err(aq_nic->ndev,
			   "ethtool: location must be in range [%d, %d]",
			   AQ_RX_FIRST_LOC_FL3L4, last_location);
		return -EINVAL;
	}
	if (rx_fltrs->fl3l4.is_ipv6 && rx_fltrs->fl3l4.active_ipv4) {
		rx_fltrs->fl3l4.is_ipv6 = false;
		netdev_err(aq_nic->ndev,
			   "ethtool: mixing ipv4 and ipv6 is not allowed");
		return -EINVAL;
	} else if (!rx_fltrs->fl3l4.is_ipv6 && rx_fltrs->fl3l4.active_ipv6) {
		rx_fltrs->fl3l4.is_ipv6 = true;
		netdev_err(aq_nic->ndev,
			   "ethtool: mixing ipv4 and ipv6 is not allowed");
		return -EINVAL;
	} else if (rx_fltrs->fl3l4.is_ipv6		      &&
		   fsp->location != AQ_RX_FIRST_LOC_FL3L4 + 4 &&
		   fsp->location != AQ_RX_FIRST_LOC_FL3L4) {
		netdev_err(aq_nic->ndev,
			   "ethtool: The specified location for ipv6 must be %d or %d",
			   AQ_RX_FIRST_LOC_FL3L4, AQ_RX_FIRST_LOC_FL3L4 + 4);
		return -EINVAL;
	}

	return 0;
}

static int __must_check
aq_check_approve_fl2(struct aq_nic_s *aq_nic,
		     struct aq_hw_rx_fltrs_s *rx_fltrs,
		     struct ethtool_rx_flow_spec *fsp)
{
	u32 last_location = AQ_RX_LAST_LOC_FETHERT -
			    aq_nic->aq_hw_rx_fltrs.fet_reserved_count;

	if (fsp->location < AQ_RX_FIRST_LOC_FETHERT ||
	    fsp->location > last_location) {
		netdev_err(aq_nic->ndev,
			   "ethtool: location must be in range [%d, %d]",
			   AQ_RX_FIRST_LOC_FETHERT,
			   last_location);
		return -EINVAL;
	}

	if (be16_to_cpu(fsp->m_ext.vlan_tci) == VLAN_PRIO_MASK &&
	    fsp->m_u.ether_spec.h_proto == 0U) {
		netdev_err(aq_nic->ndev,
			   "ethtool: proto (ether_type) parameter must be specified");
		return -EINVAL;
	}

	return 0;
}

static int __must_check
aq_check_approve_fvlan(struct aq_nic_s *aq_nic,
		       struct aq_hw_rx_fltrs_s *rx_fltrs,
		       struct ethtool_rx_flow_spec *fsp)
{
	if (fsp->location < AQ_RX_FIRST_LOC_FVLANID ||
	    fsp->location > AQ_RX_LAST_LOC_FVLANID) {
		netdev_err(aq_nic->ndev,
			   "ethtool: location must be in range [%d, %d]",
			   AQ_RX_FIRST_LOC_FVLANID,
			   AQ_RX_LAST_LOC_FVLANID);
		return -EINVAL;
	}

	if ((aq_nic->ndev->features & NETIF_F_HW_VLAN_CTAG_FILTER) &&
	    (!test_bit(be16_to_cpu(fsp->h_ext.vlan_tci) & VLAN_VID_MASK,
		       aq_nic->active_vlans))) {
		netdev_err(aq_nic->ndev,
			   "ethtool: unknown vlan-id specified");
		return -EINVAL;
	}

	if (fsp->ring_cookie > aq_nic->aq_nic_cfg.num_rss_queues) {
		netdev_err(aq_nic->ndev,
			   "ethtool: queue number must be in range [0, %d]",
			   aq_nic->aq_nic_cfg.num_rss_queues - 1);
		return -EINVAL;
	}
	return 0;
}

static int __must_check
aq_check_filter(struct aq_nic_s *aq_nic,
		struct ethtool_rx_flow_spec *fsp)
{
	int err = 0;
	struct aq_hw_rx_fltrs_s *rx_fltrs = aq_get_hw_rx_fltrs(aq_nic);

	if (fsp->flow_type & FLOW_EXT) {
		if (be16_to_cpu(fsp->m_ext.vlan_tci) == VLAN_VID_MASK) {
			err = aq_check_approve_fvlan(aq_nic, rx_fltrs, fsp);
		} else if (be16_to_cpu(fsp->m_ext.vlan_tci) == VLAN_PRIO_MASK) {
			err = aq_check_approve_fl2(aq_nic, rx_fltrs, fsp);
		} else {
			netdev_err(aq_nic->ndev,
				   "ethtool: invalid vlan mask 0x%x specified",
				   be16_to_cpu(fsp->m_ext.vlan_tci));
			err = -EINVAL;
		}
	} else {
		switch (fsp->flow_type & ~FLOW_EXT) {
		case ETHER_FLOW:
			err = aq_check_approve_fl2(aq_nic, rx_fltrs, fsp);
			break;
		case TCP_V4_FLOW:
		case UDP_V4_FLOW:
		case SCTP_V4_FLOW:
		case IPV4_FLOW:
		case IP_USER_FLOW:
			rx_fltrs->fl3l4.is_ipv6 = false;
			err = aq_check_approve_fl3l4(aq_nic, rx_fltrs, fsp);
			break;
		case TCP_V6_FLOW:
		case UDP_V6_FLOW:
		case SCTP_V6_FLOW:
		case IPV6_FLOW:
		case IPV6_USER_FLOW:
			rx_fltrs->fl3l4.is_ipv6 = true;
			err = aq_check_approve_fl3l4(aq_nic, rx_fltrs, fsp);
			break;
		default:
			netdev_err(aq_nic->ndev,
				   "ethtool: unknown flow-type specified");
			err = -EINVAL;
		}
	}

	return err;
}

static bool __must_check
aq_rule_is_not_support(struct aq_nic_s *aq_nic,
		       struct ethtool_rx_flow_spec *fsp)
{
	bool rule_is_not_support = false;

	if (!(aq_nic->ndev->features & NETIF_F_NTUPLE)) {
		netdev_err(aq_nic->ndev,
			   "ethtool: Please, to enable the RX flow control:\n"
			   "ethtool -K %s ntuple on\n", aq_nic->ndev->name);
		rule_is_not_support = true;
	} else if (!aq_rule_is_approve(fsp)) {
		netdev_err(aq_nic->ndev,
			   "ethtool: The specified flow type is not supported\n");
		rule_is_not_support = true;
	} else if ((fsp->flow_type & ~FLOW_EXT) != ETHER_FLOW &&
		   (fsp->h_u.tcp_ip4_spec.tos ||
		    fsp->h_u.tcp_ip6_spec.tclass)) {
		netdev_err(aq_nic->ndev,
			   "ethtool: The specified tos tclass are not supported\n");
		rule_is_not_support = true;
	} else if (fsp->flow_type & FLOW_MAC_EXT) {
		netdev_err(aq_nic->ndev,
			   "ethtool: MAC_EXT is not supported");
		rule_is_not_support = true;
	}

	return rule_is_not_support;
}

static bool __must_check
aq_rule_is_not_correct(struct aq_nic_s *aq_nic,
		       struct ethtool_rx_flow_spec *fsp)
{
	bool rule_is_not_correct = false;

	if (!aq_nic) {
		rule_is_not_correct = true;
	} else if (fsp->location > AQ_RX_MAX_RXNFC_LOC) {
		netdev_err(aq_nic->ndev,
			   "ethtool: The specified number %u rule is invalid\n",
			   fsp->location);
		rule_is_not_correct = true;
	} else if (aq_check_filter(aq_nic, fsp)) {
		rule_is_not_correct = true;
	} else if (fsp->ring_cookie != RX_CLS_FLOW_DISC) {
		if (fsp->ring_cookie >= aq_nic->aq_nic_cfg.num_rss_queues) {
			netdev_err(aq_nic->ndev,
				   "ethtool: The specified action is invalid.\n"
				   "Maximum allowable value action is %u.\n",
				   aq_nic->aq_nic_cfg.num_rss_queues - 1);
			rule_is_not_correct = true;
		}
	}

	return rule_is_not_correct;
}

static int __must_check
aq_check_rule(struct aq_nic_s *aq_nic,
	      struct ethtool_rx_flow_spec *fsp)
{
	int err = 0;

	if (aq_rule_is_not_correct(aq_nic, fsp))
		err = -EINVAL;
	else if (aq_rule_is_not_support(aq_nic, fsp))
		err = -EOPNOTSUPP;
	else if (aq_rule_already_exists(aq_nic, fsp))
		err = -EEXIST;

	return err;
}

static void aq_set_data_fl2(struct aq_nic_s *aq_nic,
			    struct aq_rx_filter *aq_rx_fltr,
			    struct aq_rx_filter_l2 *data, bool add)
{
	const struct ethtool_rx_flow_spec *fsp = &aq_rx_fltr->aq_fsp;

	memset(data, 0, sizeof(*data));

	data->location = fsp->location - AQ_RX_FIRST_LOC_FETHERT;

	if (fsp->ring_cookie != RX_CLS_FLOW_DISC)
		data->queue = fsp->ring_cookie;
	else
		data->queue = -1;

	data->ethertype = be16_to_cpu(fsp->h_u.ether_spec.h_proto);
	data->user_priority_en = be16_to_cpu(fsp->m_ext.vlan_tci)
				 == VLAN_PRIO_MASK;
	data->user_priority = (be16_to_cpu(fsp->h_ext.vlan_tci)
			       & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
}

static int aq_add_del_fether(struct aq_nic_s *aq_nic,
			     struct aq_rx_filter *aq_rx_fltr, bool add)
{
	struct aq_rx_filter_l2 data;
	struct aq_hw_s *aq_hw = aq_nic->aq_hw;
	const struct aq_hw_ops *aq_hw_ops = aq_nic->aq_hw_ops;

	aq_set_data_fl2(aq_nic, aq_rx_fltr, &data, add);

	if (unlikely(!aq_hw_ops->hw_filter_l2_set))
		return -EOPNOTSUPP;
	if (unlikely(!aq_hw_ops->hw_filter_l2_clear))
		return -EOPNOTSUPP;

	if (add)
		return aq_hw_ops->hw_filter_l2_set(aq_hw, &data);
	else
		return aq_hw_ops->hw_filter_l2_clear(aq_hw, &data);
}

static bool aq_fvlan_is_busy(struct aq_rx_filter_vlan *aq_vlans, int vlan)
{
	int i;

	for (i = 0; i < AQ_VLAN_MAX_FILTERS; ++i) {
		if (aq_vlans[i].enable &&
		    aq_vlans[i].queue != AQ_RX_QUEUE_NOT_ASSIGNED &&
		    aq_vlans[i].vlan_id == vlan) {
			return true;
		}
	}

	return false;
}

/* Function rebuilds array of vlan filters so that filters with assigned
 * queue have a precedence over just vlans on the interface.
 */
static void aq_fvlan_rebuild(struct aq_nic_s *aq_nic,
			     unsigned long *active_vlans,
			     struct aq_rx_filter_vlan *aq_vlans)
{
	bool vlan_busy = false;
	int vlan = -1;
	int i;

	for (i = 0; i < AQ_VLAN_MAX_FILTERS; ++i) {
		if (aq_vlans[i].enable &&
		    aq_vlans[i].queue != AQ_RX_QUEUE_NOT_ASSIGNED)
			continue;
		do {
			vlan = find_next_bit(active_vlans,
					     VLAN_N_VID,
					     vlan + 1);
			if (vlan == VLAN_N_VID) {
				aq_vlans[i].enable = 0U;
				aq_vlans[i].queue = AQ_RX_QUEUE_NOT_ASSIGNED;
				aq_vlans[i].vlan_id = 0;
				continue;
			}

			vlan_busy = aq_fvlan_is_busy(aq_vlans, vlan);
			if (!vlan_busy) {
				aq_vlans[i].enable = 1U;
				aq_vlans[i].queue = AQ_RX_QUEUE_NOT_ASSIGNED;
				aq_vlans[i].vlan_id = vlan;
			}
		} while (vlan_busy && vlan != VLAN_N_VID);
	}
}

static int aq_set_data_fvlan(struct aq_nic_s *aq_nic,
			     struct aq_rx_filter *aq_rx_fltr,
			     struct aq_rx_filter_vlan *aq_vlans, bool add)
{
	const struct ethtool_rx_flow_spec *fsp = &aq_rx_fltr->aq_fsp;
	int location = fsp->location - AQ_RX_FIRST_LOC_FVLANID;
	int i;

	memset(&aq_vlans[location], 0, sizeof(aq_vlans[location]));

	if (!add)
		return 0;

	/* remove vlan if it was in table without queue assignment */
	for (i = 0; i < AQ_VLAN_MAX_FILTERS; ++i) {
		if (aq_vlans[i].vlan_id ==
		   (be16_to_cpu(fsp->h_ext.vlan_tci) & VLAN_VID_MASK)) {
			aq_vlans[i].enable = false;
		}
	}

	aq_vlans[location].location = location;
	aq_vlans[location].vlan_id = be16_to_cpu(fsp->h_ext.vlan_tci)
				     & VLAN_VID_MASK;
	aq_vlans[location].queue = fsp->ring_cookie & 0x1FU;
	aq_vlans[location].enable = 1U;

	return 0;
}

int aq_del_fvlan_by_vlan(struct aq_nic_s *aq_nic, u16 vlan_id)
{
	struct aq_hw_rx_fltrs_s *rx_fltrs = aq_get_hw_rx_fltrs(aq_nic);
	struct aq_rx_filter *rule = NULL;
	struct hlist_node *aq_node2;

	hlist_for_each_entry_safe(rule, aq_node2,
				  &rx_fltrs->filter_list, aq_node) {
		if (be16_to_cpu(rule->aq_fsp.h_ext.vlan_tci) == vlan_id)
			break;
	}
	if (rule && rule->type == aq_rx_filter_vlan &&
	    be16_to_cpu(rule->aq_fsp.h_ext.vlan_tci) == vlan_id) {
		struct ethtool_rxnfc cmd;

		cmd.fs.location = rule->aq_fsp.location;
		return aq_del_rxnfc_rule(aq_nic, &cmd);
	}

	return -ENOENT;
}

static int aq_add_del_fvlan(struct aq_nic_s *aq_nic,
			    struct aq_rx_filter *aq_rx_fltr, bool add)
{
	const struct aq_hw_ops *aq_hw_ops = aq_nic->aq_hw_ops;

	if (unlikely(!aq_hw_ops->hw_filter_vlan_set))
		return -EOPNOTSUPP;

	aq_set_data_fvlan(aq_nic,
			  aq_rx_fltr,
			  aq_nic->aq_hw_rx_fltrs.fl2.aq_vlans,
			  add);

	return aq_filters_vlans_update(aq_nic);
}

static int aq_set_data_fl3l4(struct aq_nic_s *aq_nic,
			     struct aq_rx_filter *aq_rx_fltr,
			     struct aq_rx_filter_l3l4 *data, bool add)
{
	struct aq_hw_rx_fltrs_s *rx_fltrs = aq_get_hw_rx_fltrs(aq_nic);
	const struct ethtool_rx_flow_spec *fsp = &aq_rx_fltr->aq_fsp;

	memset(data, 0, sizeof(*data));

	data->is_ipv6 = rx_fltrs->fl3l4.is_ipv6;
	data->location = HW_ATL_GET_REG_LOCATION_FL3L4(fsp->location);

	if (!add) {
		if (!data->is_ipv6)
			rx_fltrs->fl3l4.active_ipv4 &= ~BIT(data->location);
		else
			rx_fltrs->fl3l4.active_ipv6 &=
				~BIT((data->location) / 4);

		return 0;
	}

	data->cmd |= HW_ATL_RX_ENABLE_FLTR_L3L4;

	switch (fsp->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		data->cmd |= HW_ATL_RX_ENABLE_CMP_PROT_L4;
		break;
	case UDP_V4_FLOW:
	case UDP_V6_FLOW:
		data->cmd |= HW_ATL_RX_UDP;
		data->cmd |= HW_ATL_RX_ENABLE_CMP_PROT_L4;
		break;
	case SCTP_V4_FLOW:
	case SCTP_V6_FLOW:
		data->cmd |= HW_ATL_RX_SCTP;
		data->cmd |= HW_ATL_RX_ENABLE_CMP_PROT_L4;
		break;
	default:
		break;
	}

	if (!data->is_ipv6) {
		data->ip_src[0] =
			ntohl(fsp->h_u.tcp_ip4_spec.ip4src);
		data->ip_dst[0] =
			ntohl(fsp->h_u.tcp_ip4_spec.ip4dst);
		rx_fltrs->fl3l4.active_ipv4 |= BIT(data->location);
	} else {
		int i;

		rx_fltrs->fl3l4.active_ipv6 |= BIT((data->location) / 4);
		for (i = 0; i < HW_ATL_RX_CNT_REG_ADDR_IPV6; ++i) {
			data->ip_dst[i] =
				ntohl(fsp->h_u.tcp_ip6_spec.ip6dst[i]);
			data->ip_src[i] =
				ntohl(fsp->h_u.tcp_ip6_spec.ip6src[i]);
		}
		data->cmd |= HW_ATL_RX_ENABLE_L3_IPV6;
	}
	if (fsp->flow_type != IP_USER_FLOW &&
	    fsp->flow_type != IPV6_USER_FLOW) {
		if (!data->is_ipv6) {
			data->p_dst =
				ntohs(fsp->h_u.tcp_ip4_spec.pdst);
			data->p_src =
				ntohs(fsp->h_u.tcp_ip4_spec.psrc);
		} else {
			data->p_dst =
				ntohs(fsp->h_u.tcp_ip6_spec.pdst);
			data->p_src =
				ntohs(fsp->h_u.tcp_ip6_spec.psrc);
		}
	}
	if (data->ip_src[0] && !data->is_ipv6)
		data->cmd |= HW_ATL_RX_ENABLE_CMP_SRC_ADDR_L3;
	if (data->ip_dst[0] && !data->is_ipv6)
		data->cmd |= HW_ATL_RX_ENABLE_CMP_DEST_ADDR_L3;
	if (data->p_dst)
		data->cmd |= HW_ATL_RX_ENABLE_CMP_DEST_PORT_L4;
	if (data->p_src)
		data->cmd |= HW_ATL_RX_ENABLE_CMP_SRC_PORT_L4;
	if (fsp->ring_cookie != RX_CLS_FLOW_DISC) {
		data->cmd |= HW_ATL_RX_HOST << HW_ATL_RX_ACTION_FL3F4_SHIFT;
		data->cmd |= fsp->ring_cookie << HW_ATL_RX_QUEUE_FL3L4_SHIFT;
		data->cmd |= HW_ATL_RX_ENABLE_QUEUE_L3L4;
	} else {
		data->cmd |= HW_ATL_RX_DISCARD << HW_ATL_RX_ACTION_FL3F4_SHIFT;
	}

	return 0;
}

static int aq_set_fl3l4(struct aq_hw_s *aq_hw,
			const struct aq_hw_ops *aq_hw_ops,
			struct aq_rx_filter_l3l4 *data)
{
	if (unlikely(!aq_hw_ops->hw_filter_l3l4_set))
		return -EOPNOTSUPP;

	return aq_hw_ops->hw_filter_l3l4_set(aq_hw, data);
}

static int aq_add_del_fl3l4(struct aq_nic_s *aq_nic,
			    struct aq_rx_filter *aq_rx_fltr, bool add)
{
	const struct aq_hw_ops *aq_hw_ops = aq_nic->aq_hw_ops;
	struct aq_hw_s *aq_hw = aq_nic->aq_hw;
	struct aq_rx_filter_l3l4 data;

	if (unlikely(aq_rx_fltr->aq_fsp.location < AQ_RX_FIRST_LOC_FL3L4 ||
		     aq_rx_fltr->aq_fsp.location > AQ_RX_LAST_LOC_FL3L4  ||
		     aq_set_data_fl3l4(aq_nic, aq_rx_fltr, &data, add)))
		return -EINVAL;

	return aq_set_fl3l4(aq_hw, aq_hw_ops, &data);
}

static int aq_add_del_rule(struct aq_nic_s *aq_nic,
			   struct aq_rx_filter *aq_rx_fltr, bool add)
{
	int err = -EINVAL;

	if (aq_rx_fltr->aq_fsp.flow_type & FLOW_EXT) {
		if (be16_to_cpu(aq_rx_fltr->aq_fsp.m_ext.vlan_tci)
		    == VLAN_VID_MASK) {
			aq_rx_fltr->type = aq_rx_filter_vlan;
			err = aq_add_del_fvlan(aq_nic, aq_rx_fltr, add);
		} else if (be16_to_cpu(aq_rx_fltr->aq_fsp.m_ext.vlan_tci)
			== VLAN_PRIO_MASK) {
			aq_rx_fltr->type = aq_rx_filter_ethertype;
			err = aq_add_del_fether(aq_nic, aq_rx_fltr, add);
		}
	} else {
		switch (aq_rx_fltr->aq_fsp.flow_type & ~FLOW_EXT) {
		case ETHER_FLOW:
			aq_rx_fltr->type = aq_rx_filter_ethertype;
			err = aq_add_del_fether(aq_nic, aq_rx_fltr, add);
			break;
		case TCP_V4_FLOW:
		case UDP_V4_FLOW:
		case SCTP_V4_FLOW:
		case IP_USER_FLOW:
		case TCP_V6_FLOW:
		case UDP_V6_FLOW:
		case SCTP_V6_FLOW:
		case IPV6_USER_FLOW:
			aq_rx_fltr->type = aq_rx_filter_l3l4;
			err = aq_add_del_fl3l4(aq_nic, aq_rx_fltr, add);
			break;
		default:
			err = -EINVAL;
			break;
		}
	}

	return err;
}

static int aq_update_table_filters(struct aq_nic_s *aq_nic,
				   struct aq_rx_filter *aq_rx_fltr, u16 index,
				   struct ethtool_rxnfc *cmd)
{
	struct aq_hw_rx_fltrs_s *rx_fltrs = aq_get_hw_rx_fltrs(aq_nic);
	struct aq_rx_filter *rule = NULL, *parent = NULL;
	struct hlist_node *aq_node2;
	int err = -EINVAL;

	hlist_for_each_entry_safe(rule, aq_node2,
				  &rx_fltrs->filter_list, aq_node) {
		if (rule->aq_fsp.location >= index)
			break;
		parent = rule;
	}

	if (rule && rule->aq_fsp.location == index) {
		err = aq_add_del_rule(aq_nic, rule, false);
		hlist_del(&rule->aq_node);
		kfree(rule);
		--rx_fltrs->active_filters;
	}

	if (unlikely(!aq_rx_fltr))
		return err;

	INIT_HLIST_NODE(&aq_rx_fltr->aq_node);

	if (parent)
		hlist_add_behind(&aq_rx_fltr->aq_node, &parent->aq_node);
	else
		hlist_add_head(&aq_rx_fltr->aq_node, &rx_fltrs->filter_list);

	++rx_fltrs->active_filters;

	return 0;
}

u16 aq_get_rxnfc_count_all_rules(struct aq_nic_s *aq_nic)
{
	struct aq_hw_rx_fltrs_s *rx_fltrs = aq_get_hw_rx_fltrs(aq_nic);

	return rx_fltrs->active_filters;
}

struct aq_hw_rx_fltrs_s *aq_get_hw_rx_fltrs(struct aq_nic_s *aq_nic)
{
	return &aq_nic->aq_hw_rx_fltrs;
}

int aq_add_rxnfc_rule(struct aq_nic_s *aq_nic, const struct ethtool_rxnfc *cmd)
{
	struct aq_hw_rx_fltrs_s *rx_fltrs = aq_get_hw_rx_fltrs(aq_nic);
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct aq_rx_filter *aq_rx_fltr;
	int err = 0;

	err = aq_check_rule(aq_nic, fsp);
	if (err)
		goto err_exit;

	aq_rx_fltr = kzalloc(sizeof(*aq_rx_fltr), GFP_KERNEL);
	if (unlikely(!aq_rx_fltr)) {
		err = -ENOMEM;
		goto err_exit;
	}

	memcpy(&aq_rx_fltr->aq_fsp, fsp, sizeof(*fsp));

	err = aq_update_table_filters(aq_nic, aq_rx_fltr, fsp->location, NULL);
	if (unlikely(err))
		goto err_free;

	err = aq_add_del_rule(aq_nic, aq_rx_fltr, true);
	if (unlikely(err)) {
		hlist_del(&aq_rx_fltr->aq_node);
		--rx_fltrs->active_filters;
		goto err_free;
	}

	return 0;

err_free:
	kfree(aq_rx_fltr);
err_exit:
	return err;
}

int aq_del_rxnfc_rule(struct aq_nic_s *aq_nic, const struct ethtool_rxnfc *cmd)
{
	struct aq_hw_rx_fltrs_s *rx_fltrs = aq_get_hw_rx_fltrs(aq_nic);
	struct aq_rx_filter *rule = NULL;
	struct hlist_node *aq_node2;
	int err = -EINVAL;

	hlist_for_each_entry_safe(rule, aq_node2,
				  &rx_fltrs->filter_list, aq_node) {
		if (rule->aq_fsp.location == cmd->fs.location)
			break;
	}

	if (rule && rule->aq_fsp.location == cmd->fs.location) {
		err = aq_add_del_rule(aq_nic, rule, false);
		hlist_del(&rule->aq_node);
		kfree(rule);
		--rx_fltrs->active_filters;
	}
	return err;
}

int aq_get_rxnfc_rule(struct aq_nic_s *aq_nic, struct ethtool_rxnfc *cmd)
{
	struct aq_hw_rx_fltrs_s *rx_fltrs = aq_get_hw_rx_fltrs(aq_nic);
	struct ethtool_rx_flow_spec *fsp =
			(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct aq_rx_filter *rule = NULL;
	struct hlist_node *aq_node2;

	hlist_for_each_entry_safe(rule, aq_node2,
				  &rx_fltrs->filter_list, aq_node)
		if (fsp->location <= rule->aq_fsp.location)
			break;

	if (unlikely(!rule || fsp->location != rule->aq_fsp.location))
		return -EINVAL;

	memcpy(fsp, &rule->aq_fsp, sizeof(*fsp));

	return 0;
}

int aq_get_rxnfc_all_rules(struct aq_nic_s *aq_nic, struct ethtool_rxnfc *cmd,
			   u32 *rule_locs)
{
	struct aq_hw_rx_fltrs_s *rx_fltrs = aq_get_hw_rx_fltrs(aq_nic);
	struct hlist_node *aq_node2;
	struct aq_rx_filter *rule;
	int count = 0;

	cmd->data = aq_get_rxnfc_count_all_rules(aq_nic);

	hlist_for_each_entry_safe(rule, aq_node2,
				  &rx_fltrs->filter_list, aq_node) {
		if (unlikely(count == cmd->rule_cnt))
			return -EMSGSIZE;

		rule_locs[count++] = rule->aq_fsp.location;
	}

	cmd->rule_cnt = count;

	return 0;
}

int aq_clear_rxnfc_all_rules(struct aq_nic_s *aq_nic)
{
	struct aq_hw_rx_fltrs_s *rx_fltrs = aq_get_hw_rx_fltrs(aq_nic);
	struct hlist_node *aq_node2;
	struct aq_rx_filter *rule;
	int err = 0;

	hlist_for_each_entry_safe(rule, aq_node2,
				  &rx_fltrs->filter_list, aq_node) {
		err = aq_add_del_rule(aq_nic, rule, false);
		if (err)
			goto err_exit;
		hlist_del(&rule->aq_node);
		kfree(rule);
		--rx_fltrs->active_filters;
	}

err_exit:
	return err;
}

int aq_reapply_rxnfc_all_rules(struct aq_nic_s *aq_nic)
{
	struct aq_hw_rx_fltrs_s *rx_fltrs = aq_get_hw_rx_fltrs(aq_nic);
	struct hlist_node *aq_node2;
	struct aq_rx_filter *rule;
	int err = 0;

	hlist_for_each_entry_safe(rule, aq_node2,
				  &rx_fltrs->filter_list, aq_node) {
		err = aq_add_del_rule(aq_nic, rule, true);
		if (err)
			goto err_exit;
	}

err_exit:
	return err;
}

int aq_filters_vlans_update(struct aq_nic_s *aq_nic)
{
	const struct aq_hw_ops *aq_hw_ops = aq_nic->aq_hw_ops;
	struct aq_hw_s *aq_hw = aq_nic->aq_hw;
	int hweight = 0;
	int err = 0;
	int i;

	if (unlikely(!aq_hw_ops->hw_filter_vlan_set))
		return -EOPNOTSUPP;
	if (unlikely(!aq_hw_ops->hw_filter_vlan_ctrl))
		return -EOPNOTSUPP;

	aq_fvlan_rebuild(aq_nic, aq_nic->active_vlans,
			 aq_nic->aq_hw_rx_fltrs.fl2.aq_vlans);

	if (aq_nic->ndev->features & NETIF_F_HW_VLAN_CTAG_FILTER) {
		for (i = 0; i < BITS_TO_LONGS(VLAN_N_VID); i++)
			hweight += hweight_long(aq_nic->active_vlans[i]);

		err = aq_hw_ops->hw_filter_vlan_ctrl(aq_hw, false);
		if (err)
			return err;
	}

	err = aq_hw_ops->hw_filter_vlan_set(aq_hw,
					    aq_nic->aq_hw_rx_fltrs.fl2.aq_vlans
					   );
	if (err)
		return err;

	if (aq_nic->ndev->features & NETIF_F_HW_VLAN_CTAG_FILTER) {
		if (hweight <= AQ_VLAN_MAX_FILTERS && hweight > 0) {
			err = aq_hw_ops->hw_filter_vlan_ctrl(aq_hw,
				!(aq_nic->packet_filter & IFF_PROMISC));
			aq_nic->aq_nic_cfg.is_vlan_force_promisc = false;
		} else {
		/* otherwise left in promiscue mode */
			aq_nic->aq_nic_cfg.is_vlan_force_promisc = true;
		}
	}

	return err;
}

int aq_filters_vlan_offload_off(struct aq_nic_s *aq_nic)
{
	const struct aq_hw_ops *aq_hw_ops = aq_nic->aq_hw_ops;
	struct aq_hw_s *aq_hw = aq_nic->aq_hw;
	int err = 0;

	memset(aq_nic->active_vlans, 0, sizeof(aq_nic->active_vlans));
	aq_fvlan_rebuild(aq_nic, aq_nic->active_vlans,
			 aq_nic->aq_hw_rx_fltrs.fl2.aq_vlans);

	if (unlikely(!aq_hw_ops->hw_filter_vlan_set))
		return -EOPNOTSUPP;
	if (unlikely(!aq_hw_ops->hw_filter_vlan_ctrl))
		return -EOPNOTSUPP;

	aq_nic->aq_nic_cfg.is_vlan_force_promisc = true;
	err = aq_hw_ops->hw_filter_vlan_ctrl(aq_hw, false);
	if (err)
		return err;
	err = aq_hw_ops->hw_filter_vlan_set(aq_hw,
					    aq_nic->aq_hw_rx_fltrs.fl2.aq_vlans
					   );
	return err;
}
