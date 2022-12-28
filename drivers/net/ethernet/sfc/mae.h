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

#ifndef EF100_MAE_H
#define EF100_MAE_H
/* MCDI interface for the ef100 Match-Action Engine */

#include "net_driver.h"
#include "tc.h"
#include "mcdi_pcol.h" /* needed for various MC_CMD_MAE_*_NULL defines */

int efx_mae_allocate_mport(struct efx_nic *efx, u32 *id, u32 *label);
int efx_mae_free_mport(struct efx_nic *efx, u32 id);

void efx_mae_mport_wire(struct efx_nic *efx, u32 *out);
void efx_mae_mport_uplink(struct efx_nic *efx, u32 *out);
void efx_mae_mport_vf(struct efx_nic *efx, u32 vf_id, u32 *out);
void efx_mae_mport_mport(struct efx_nic *efx, u32 mport_id, u32 *out);

int efx_mae_lookup_mport(struct efx_nic *efx, u32 selector, u32 *id);

int efx_mae_start_counters(struct efx_nic *efx, struct efx_rx_queue *rx_queue);
int efx_mae_stop_counters(struct efx_nic *efx, struct efx_rx_queue *rx_queue);
void efx_mae_counters_grant_credits(struct work_struct *work);

#define MAE_NUM_FIELDS	(MAE_FIELD_ENC_VNET_ID + 1)

struct mae_caps {
	u32 match_field_count;
	u32 action_prios;
	u8 action_rule_fields[MAE_NUM_FIELDS];
};

int efx_mae_get_caps(struct efx_nic *efx, struct mae_caps *caps);

int efx_mae_match_check_caps(struct efx_nic *efx,
			     const struct efx_tc_match_fields *mask,
			     struct netlink_ext_ack *extack);

int efx_mae_allocate_counter(struct efx_nic *efx, struct efx_tc_counter *cnt);
int efx_mae_free_counter(struct efx_nic *efx, struct efx_tc_counter *cnt);

int efx_mae_alloc_action_set(struct efx_nic *efx, struct efx_tc_action_set *act);
int efx_mae_free_action_set(struct efx_nic *efx, u32 fw_id);

int efx_mae_alloc_action_set_list(struct efx_nic *efx,
				  struct efx_tc_action_set_list *acts);
int efx_mae_free_action_set_list(struct efx_nic *efx,
				 struct efx_tc_action_set_list *acts);

int efx_mae_insert_rule(struct efx_nic *efx, const struct efx_tc_match *match,
			u32 prio, u32 acts_id, u32 *id);
int efx_mae_delete_rule(struct efx_nic *efx, u32 id);

#endif /* EF100_MAE_H */
