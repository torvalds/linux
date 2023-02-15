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

#include <net/devlink.h>
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

struct mae_mport_desc {
	u32 mport_id;
	u32 flags;
	u32 caller_flags; /* enum mae_mport_desc_caller_flags */
	u32 mport_type; /* MAE_MPORT_DESC_MPORT_TYPE_* */
	union {
		u32 port_idx; /* for mport_type == NET_PORT */
		u32 alias_mport_id; /* for mport_type == ALIAS */
		struct { /* for mport_type == VNIC */
			u32 vnic_client_type; /* MAE_MPORT_DESC_VNIC_CLIENT_TYPE_* */
			u32 interface_idx;
			u16 pf_idx;
			u16 vf_idx;
		};
	};
	struct rhash_head linkage;
	struct devlink_port dl_port;
};

int efx_mae_enumerate_mports(struct efx_nic *efx);
struct mae_mport_desc *efx_mae_get_mport(struct efx_nic *efx, u32 mport_id);
void efx_mae_put_mport(struct efx_nic *efx, struct mae_mport_desc *desc);

/**
 * struct efx_mae - MAE information
 *
 * @efx: The associated NIC
 * @mports_ht: m-port descriptions from MC_CMD_MAE_MPORT_READ_JOURNAL
 */
struct efx_mae {
	struct efx_nic *efx;
	struct rhashtable mports_ht;
};

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

int efx_init_mae(struct efx_nic *efx);
void efx_fini_mae(struct efx_nic *efx);
void efx_mae_remove_mport(void *desc, void *arg);
int efx_mae_fw_lookup_mport(struct efx_nic *efx, u32 selector, u32 *id);
int efx_mae_lookup_mport(struct efx_nic *efx, u32 vf, u32 *id);
#endif /* EF100_MAE_H */
