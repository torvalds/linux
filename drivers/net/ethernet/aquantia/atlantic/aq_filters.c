// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (C) 2014-2017 aQuantia Corporation. */

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

static int __must_check
aq_check_filter(struct aq_nic_s *aq_nic,
		struct ethtool_rx_flow_spec *fsp)
{
	int err = 0;

	if (fsp->flow_type & FLOW_EXT) {
		err = -EOPNOTSUPP;
	} else {
		switch (fsp->flow_type & ~FLOW_EXT) {
		case ETHER_FLOW:
			err = -EOPNOTSUPP;
			break;
		case TCP_V4_FLOW:
		case UDP_V4_FLOW:
		case SCTP_V4_FLOW:
		case IPV4_FLOW:
		case IP_USER_FLOW:
			err = -EOPNOTSUPP;
			break;
		case TCP_V6_FLOW:
		case UDP_V6_FLOW:
		case SCTP_V6_FLOW:
		case IPV6_FLOW:
		case IPV6_USER_FLOW:
			err = -EOPNOTSUPP;
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

static int aq_add_del_rule(struct aq_nic_s *aq_nic,
			   struct aq_rx_filter *aq_rx_fltr, bool add)
{
	int err = -EINVAL;

	if (aq_rx_fltr->aq_fsp.flow_type & FLOW_EXT) {
		err = -EOPNOTSUPP;
	} else {
		switch (aq_rx_fltr->aq_fsp.flow_type & ~FLOW_EXT) {
		case ETHER_FLOW:
			err = -EOPNOTSUPP;
			break;
		case TCP_V4_FLOW:
		case UDP_V4_FLOW:
		case SCTP_V4_FLOW:
		case IP_USER_FLOW:
		case TCP_V6_FLOW:
		case UDP_V6_FLOW:
		case SCTP_V6_FLOW:
		case IPV6_USER_FLOW:
			err = -EOPNOTSUPP;
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
