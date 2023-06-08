/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2023, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_TC_ENCAP_ACTIONS_H
#define EFX_TC_ENCAP_ACTIONS_H
#include "net_driver.h"

#include <linux/refcount.h>
#include <net/tc_act/tc_tunnel_key.h>

/* This limit is arbitrary; current hardware (SN1022) handles encap headers
 * of up to 126 bytes, but that limit is not enshrined in the MCDI protocol.
 */
#define EFX_TC_MAX_ENCAP_HDR	126
struct efx_tc_encap_action {
	enum efx_encap_type type;
	struct ip_tunnel_key key; /* 52 bytes */
	u32 dest_mport; /* is copied into struct efx_tc_action_set */
	u8 encap_hdr_len;
	u8 encap_hdr[EFX_TC_MAX_ENCAP_HDR];
	struct rhash_head linkage; /* efx->tc_encap_ht */
	refcount_t ref;
	u32 fw_id; /* index of this entry in firmware encap table */
};

/* create/uncreate/teardown hashtables */
int efx_tc_init_encap_actions(struct efx_nic *efx);
void efx_tc_destroy_encap_actions(struct efx_nic *efx);
void efx_tc_fini_encap_actions(struct efx_nic *efx);

struct efx_tc_flow_rule;
bool efx_tc_check_ready(struct efx_nic *efx, struct efx_tc_flow_rule *rule);

struct efx_tc_encap_action *efx_tc_flower_create_encap_md(
			struct efx_nic *efx, const struct ip_tunnel_info *info,
			struct net_device *egdev, struct netlink_ext_ack *extack);
void efx_tc_flower_release_encap_md(struct efx_nic *efx,
				    struct efx_tc_encap_action *encap);

#endif /* EFX_TC_ENCAP_ACTIONS_H */
