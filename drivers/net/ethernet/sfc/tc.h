/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 * Copyright 2020-2022 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_TC_H
#define EFX_TC_H
#include "net_driver.h"

struct efx_tc_action_set {
	u16 deliver:1;
	u32 dest_mport;
	u32 fw_id; /* index of this entry in firmware actions table */
	struct list_head list;
};

struct efx_tc_match_fields {
	/* L1 */
	u32 ingress_port;
};

struct efx_tc_match {
	struct efx_tc_match_fields value;
	struct efx_tc_match_fields mask;
};

struct efx_tc_action_set_list {
	struct list_head list;
	u32 fw_id;
};

struct efx_tc_flow_rule {
	struct efx_tc_match match;
	struct efx_tc_action_set_list acts;
	u32 fw_id;
};

enum efx_tc_rule_prios {
	EFX_TC_PRIO_DFLT, /* Default switch rule; one of efx_tc_default_rules */
	EFX_TC_PRIO__NUM
};

/**
 * struct efx_tc_state - control plane data for TC offload
 *
 * @reps_mport_id: MAE port allocated for representor RX
 * @reps_filter_uc: VNIC filter for representor unicast RX (promisc)
 * @reps_filter_mc: VNIC filter for representor multicast RX (allmulti)
 * @reps_mport_vport_id: vport_id for representor RX filters
 * @dflt: Match-action rules for default switching; at priority
 *	%EFX_TC_PRIO_DFLT.  Named by *ingress* port
 * @dflt.pf: rule for traffic ingressing from PF (egresses to wire)
 * @dflt.wire: rule for traffic ingressing from wire (egresses to PF)
 */
struct efx_tc_state {
	u32 reps_mport_id, reps_mport_vport_id;
	s32 reps_filter_uc, reps_filter_mc;
	struct {
		struct efx_tc_flow_rule pf;
		struct efx_tc_flow_rule wire;
	} dflt;
};

struct efx_rep;

int efx_tc_configure_default_rule_rep(struct efx_rep *efv);
void efx_tc_deconfigure_default_rule(struct efx_nic *efx,
				     struct efx_tc_flow_rule *rule);

int efx_tc_insert_rep_filters(struct efx_nic *efx);
void efx_tc_remove_rep_filters(struct efx_nic *efx);

int efx_init_tc(struct efx_nic *efx);
void efx_fini_tc(struct efx_nic *efx);

int efx_init_struct_tc(struct efx_nic *efx);
void efx_fini_struct_tc(struct efx_nic *efx);

#endif /* EFX_TC_H */
