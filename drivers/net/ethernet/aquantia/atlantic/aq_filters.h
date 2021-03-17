/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2014-2017 aQuantia Corporation. */

/* File aq_filters.h: RX filters related functions. */

#ifndef AQ_FILTERS_H
#define AQ_FILTERS_H

#include "aq_nic.h"

enum aq_rx_filter_type {
	aq_rx_filter_ethertype,
	aq_rx_filter_vlan,
	aq_rx_filter_l3l4
};

struct aq_rx_filter {
	struct hlist_node aq_node;
	enum aq_rx_filter_type type;
	struct ethtool_rx_flow_spec aq_fsp;
};

u16 aq_get_rxnfc_count_all_rules(struct aq_nic_s *aq_nic);
struct aq_hw_rx_fltrs_s *aq_get_hw_rx_fltrs(struct aq_nic_s *aq_nic);
int aq_add_rxnfc_rule(struct aq_nic_s *aq_nic, const struct ethtool_rxnfc *cmd);
int aq_del_rxnfc_rule(struct aq_nic_s *aq_nic, const struct ethtool_rxnfc *cmd);
int aq_get_rxnfc_rule(struct aq_nic_s *aq_nic, struct ethtool_rxnfc *cmd);
int aq_get_rxnfc_all_rules(struct aq_nic_s *aq_nic, struct ethtool_rxnfc *cmd,
			   u32 *rule_locs);
int aq_del_fvlan_by_vlan(struct aq_nic_s *aq_nic, u16 vlan_id);
int aq_clear_rxnfc_all_rules(struct aq_nic_s *aq_nic);
int aq_reapply_rxnfc_all_rules(struct aq_nic_s *aq_nic);
int aq_filters_vlans_update(struct aq_nic_s *aq_nic);
int aq_filters_vlan_offload_off(struct aq_nic_s *aq_nic);

#endif /* AQ_FILTERS_H */
