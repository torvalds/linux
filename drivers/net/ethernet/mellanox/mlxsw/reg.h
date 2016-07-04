/*
 * drivers/net/ethernet/mellanox/mlxsw/reg.h
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015-2016 Ido Schimmel <idosch@mellanox.com>
 * Copyright (c) 2015 Elad Raz <eladr@mellanox.com>
 * Copyright (c) 2015-2016 Jiri Pirko <jiri@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MLXSW_REG_H
#define _MLXSW_REG_H

#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/if_vlan.h>

#include "item.h"
#include "port.h"

struct mlxsw_reg_info {
	u16 id;
	u16 len; /* In u8 */
};

#define MLXSW_REG(type) (&mlxsw_reg_##type)
#define MLXSW_REG_LEN(type) MLXSW_REG(type)->len
#define MLXSW_REG_ZERO(type, payload) memset(payload, 0, MLXSW_REG(type)->len)

/* SGCR - Switch General Configuration Register
 * --------------------------------------------
 * This register is used for configuration of the switch capabilities.
 */
#define MLXSW_REG_SGCR_ID 0x2000
#define MLXSW_REG_SGCR_LEN 0x10

static const struct mlxsw_reg_info mlxsw_reg_sgcr = {
	.id = MLXSW_REG_SGCR_ID,
	.len = MLXSW_REG_SGCR_LEN,
};

/* reg_sgcr_llb
 * Link Local Broadcast (Default=0)
 * When set, all Link Local packets (224.0.0.X) will be treated as broadcast
 * packets and ignore the IGMP snooping entries.
 * Access: RW
 */
MLXSW_ITEM32(reg, sgcr, llb, 0x04, 0, 1);

static inline void mlxsw_reg_sgcr_pack(char *payload, bool llb)
{
	MLXSW_REG_ZERO(sgcr, payload);
	mlxsw_reg_sgcr_llb_set(payload, !!llb);
}

/* SPAD - Switch Physical Address Register
 * ---------------------------------------
 * The SPAD register configures the switch physical MAC address.
 */
#define MLXSW_REG_SPAD_ID 0x2002
#define MLXSW_REG_SPAD_LEN 0x10

static const struct mlxsw_reg_info mlxsw_reg_spad = {
	.id = MLXSW_REG_SPAD_ID,
	.len = MLXSW_REG_SPAD_LEN,
};

/* reg_spad_base_mac
 * Base MAC address for the switch partitions.
 * Per switch partition MAC address is equal to:
 * base_mac + swid
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, spad, base_mac, 0x02, 6);

/* SMID - Switch Multicast ID
 * --------------------------
 * The MID record maps from a MID (Multicast ID), which is a unique identifier
 * of the multicast group within the stacking domain, into a list of local
 * ports into which the packet is replicated.
 */
#define MLXSW_REG_SMID_ID 0x2007
#define MLXSW_REG_SMID_LEN 0x240

static const struct mlxsw_reg_info mlxsw_reg_smid = {
	.id = MLXSW_REG_SMID_ID,
	.len = MLXSW_REG_SMID_LEN,
};

/* reg_smid_swid
 * Switch partition ID.
 * Access: Index
 */
MLXSW_ITEM32(reg, smid, swid, 0x00, 24, 8);

/* reg_smid_mid
 * Multicast identifier - global identifier that represents the multicast group
 * across all devices.
 * Access: Index
 */
MLXSW_ITEM32(reg, smid, mid, 0x00, 0, 16);

/* reg_smid_port
 * Local port memebership (1 bit per port).
 * Access: RW
 */
MLXSW_ITEM_BIT_ARRAY(reg, smid, port, 0x20, 0x20, 1);

/* reg_smid_port_mask
 * Local port mask (1 bit per port).
 * Access: W
 */
MLXSW_ITEM_BIT_ARRAY(reg, smid, port_mask, 0x220, 0x20, 1);

static inline void mlxsw_reg_smid_pack(char *payload, u16 mid,
				       u8 port, bool set)
{
	MLXSW_REG_ZERO(smid, payload);
	mlxsw_reg_smid_swid_set(payload, 0);
	mlxsw_reg_smid_mid_set(payload, mid);
	mlxsw_reg_smid_port_set(payload, port, set);
	mlxsw_reg_smid_port_mask_set(payload, port, 1);
}

/* SSPR - Switch System Port Record Register
 * -----------------------------------------
 * Configures the system port to local port mapping.
 */
#define MLXSW_REG_SSPR_ID 0x2008
#define MLXSW_REG_SSPR_LEN 0x8

static const struct mlxsw_reg_info mlxsw_reg_sspr = {
	.id = MLXSW_REG_SSPR_ID,
	.len = MLXSW_REG_SSPR_LEN,
};

/* reg_sspr_m
 * Master - if set, then the record describes the master system port.
 * This is needed in case a local port is mapped into several system ports
 * (for multipathing). That number will be reported as the source system
 * port when packets are forwarded to the CPU. Only one master port is allowed
 * per local port.
 *
 * Note: Must be set for Spectrum.
 * Access: RW
 */
MLXSW_ITEM32(reg, sspr, m, 0x00, 31, 1);

/* reg_sspr_local_port
 * Local port number.
 *
 * Access: RW
 */
MLXSW_ITEM32(reg, sspr, local_port, 0x00, 16, 8);

/* reg_sspr_sub_port
 * Virtual port within the physical port.
 * Should be set to 0 when virtual ports are not enabled on the port.
 *
 * Access: RW
 */
MLXSW_ITEM32(reg, sspr, sub_port, 0x00, 8, 8);

/* reg_sspr_system_port
 * Unique identifier within the stacking domain that represents all the ports
 * that are available in the system (external ports).
 *
 * Currently, only single-ASIC configurations are supported, so we default to
 * 1:1 mapping between system ports and local ports.
 * Access: Index
 */
MLXSW_ITEM32(reg, sspr, system_port, 0x04, 0, 16);

static inline void mlxsw_reg_sspr_pack(char *payload, u8 local_port)
{
	MLXSW_REG_ZERO(sspr, payload);
	mlxsw_reg_sspr_m_set(payload, 1);
	mlxsw_reg_sspr_local_port_set(payload, local_port);
	mlxsw_reg_sspr_sub_port_set(payload, 0);
	mlxsw_reg_sspr_system_port_set(payload, local_port);
}

/* SFDAT - Switch Filtering Database Aging Time
 * --------------------------------------------
 * Controls the Switch aging time. Aging time is able to be set per Switch
 * Partition.
 */
#define MLXSW_REG_SFDAT_ID 0x2009
#define MLXSW_REG_SFDAT_LEN 0x8

static const struct mlxsw_reg_info mlxsw_reg_sfdat = {
	.id = MLXSW_REG_SFDAT_ID,
	.len = MLXSW_REG_SFDAT_LEN,
};

/* reg_sfdat_swid
 * Switch partition ID.
 * Access: Index
 */
MLXSW_ITEM32(reg, sfdat, swid, 0x00, 24, 8);

/* reg_sfdat_age_time
 * Aging time in seconds
 * Min - 10 seconds
 * Max - 1,000,000 seconds
 * Default is 300 seconds.
 * Access: RW
 */
MLXSW_ITEM32(reg, sfdat, age_time, 0x04, 0, 20);

static inline void mlxsw_reg_sfdat_pack(char *payload, u32 age_time)
{
	MLXSW_REG_ZERO(sfdat, payload);
	mlxsw_reg_sfdat_swid_set(payload, 0);
	mlxsw_reg_sfdat_age_time_set(payload, age_time);
}

/* SFD - Switch Filtering Database
 * -------------------------------
 * The following register defines the access to the filtering database.
 * The register supports querying, adding, removing and modifying the database.
 * The access is optimized for bulk updates in which case more than one
 * FDB record is present in the same command.
 */
#define MLXSW_REG_SFD_ID 0x200A
#define MLXSW_REG_SFD_BASE_LEN 0x10 /* base length, without records */
#define MLXSW_REG_SFD_REC_LEN 0x10 /* record length */
#define MLXSW_REG_SFD_REC_MAX_COUNT 64
#define MLXSW_REG_SFD_LEN (MLXSW_REG_SFD_BASE_LEN +	\
			   MLXSW_REG_SFD_REC_LEN * MLXSW_REG_SFD_REC_MAX_COUNT)

static const struct mlxsw_reg_info mlxsw_reg_sfd = {
	.id = MLXSW_REG_SFD_ID,
	.len = MLXSW_REG_SFD_LEN,
};

/* reg_sfd_swid
 * Switch partition ID for queries. Reserved on Write.
 * Access: Index
 */
MLXSW_ITEM32(reg, sfd, swid, 0x00, 24, 8);

enum mlxsw_reg_sfd_op {
	/* Dump entire FDB a (process according to record_locator) */
	MLXSW_REG_SFD_OP_QUERY_DUMP = 0,
	/* Query records by {MAC, VID/FID} value */
	MLXSW_REG_SFD_OP_QUERY_QUERY = 1,
	/* Query and clear activity. Query records by {MAC, VID/FID} value */
	MLXSW_REG_SFD_OP_QUERY_QUERY_AND_CLEAR_ACTIVITY = 2,
	/* Test. Response indicates if each of the records could be
	 * added to the FDB.
	 */
	MLXSW_REG_SFD_OP_WRITE_TEST = 0,
	/* Add/modify. Aged-out records cannot be added. This command removes
	 * the learning notification of the {MAC, VID/FID}. Response includes
	 * the entries that were added to the FDB.
	 */
	MLXSW_REG_SFD_OP_WRITE_EDIT = 1,
	/* Remove record by {MAC, VID/FID}. This command also removes
	 * the learning notification and aged-out notifications
	 * of the {MAC, VID/FID}. The response provides current (pre-removal)
	 * entries as non-aged-out.
	 */
	MLXSW_REG_SFD_OP_WRITE_REMOVE = 2,
	/* Remove learned notification by {MAC, VID/FID}. The response provides
	 * the removed learning notification.
	 */
	MLXSW_REG_SFD_OP_WRITE_REMOVE_NOTIFICATION = 2,
};

/* reg_sfd_op
 * Operation.
 * Access: OP
 */
MLXSW_ITEM32(reg, sfd, op, 0x04, 30, 2);

/* reg_sfd_record_locator
 * Used for querying the FDB. Use record_locator=0 to initiate the
 * query. When a record is returned, a new record_locator is
 * returned to be used in the subsequent query.
 * Reserved for database update.
 * Access: Index
 */
MLXSW_ITEM32(reg, sfd, record_locator, 0x04, 0, 30);

/* reg_sfd_num_rec
 * Request: Number of records to read/add/modify/remove
 * Response: Number of records read/added/replaced/removed
 * See above description for more details.
 * Ranges 0..64
 * Access: RW
 */
MLXSW_ITEM32(reg, sfd, num_rec, 0x08, 0, 8);

static inline void mlxsw_reg_sfd_pack(char *payload, enum mlxsw_reg_sfd_op op,
				      u32 record_locator)
{
	MLXSW_REG_ZERO(sfd, payload);
	mlxsw_reg_sfd_op_set(payload, op);
	mlxsw_reg_sfd_record_locator_set(payload, record_locator);
}

/* reg_sfd_rec_swid
 * Switch partition ID.
 * Access: Index
 */
MLXSW_ITEM32_INDEXED(reg, sfd, rec_swid, MLXSW_REG_SFD_BASE_LEN, 24, 8,
		     MLXSW_REG_SFD_REC_LEN, 0x00, false);

enum mlxsw_reg_sfd_rec_type {
	MLXSW_REG_SFD_REC_TYPE_UNICAST = 0x0,
	MLXSW_REG_SFD_REC_TYPE_UNICAST_LAG = 0x1,
	MLXSW_REG_SFD_REC_TYPE_MULTICAST = 0x2,
};

/* reg_sfd_rec_type
 * FDB record type.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sfd, rec_type, MLXSW_REG_SFD_BASE_LEN, 20, 4,
		     MLXSW_REG_SFD_REC_LEN, 0x00, false);

enum mlxsw_reg_sfd_rec_policy {
	/* Replacement disabled, aging disabled. */
	MLXSW_REG_SFD_REC_POLICY_STATIC_ENTRY = 0,
	/* (mlag remote): Replacement enabled, aging disabled,
	 * learning notification enabled on this port.
	 */
	MLXSW_REG_SFD_REC_POLICY_DYNAMIC_ENTRY_MLAG = 1,
	/* (ingress device): Replacement enabled, aging enabled. */
	MLXSW_REG_SFD_REC_POLICY_DYNAMIC_ENTRY_INGRESS = 3,
};

/* reg_sfd_rec_policy
 * Policy.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sfd, rec_policy, MLXSW_REG_SFD_BASE_LEN, 18, 2,
		     MLXSW_REG_SFD_REC_LEN, 0x00, false);

/* reg_sfd_rec_a
 * Activity. Set for new static entries. Set for static entries if a frame SMAC
 * lookup hits on the entry.
 * To clear the a bit, use "query and clear activity" op.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, sfd, rec_a, MLXSW_REG_SFD_BASE_LEN, 16, 1,
		     MLXSW_REG_SFD_REC_LEN, 0x00, false);

/* reg_sfd_rec_mac
 * MAC address.
 * Access: Index
 */
MLXSW_ITEM_BUF_INDEXED(reg, sfd, rec_mac, MLXSW_REG_SFD_BASE_LEN, 6,
		       MLXSW_REG_SFD_REC_LEN, 0x02);

enum mlxsw_reg_sfd_rec_action {
	/* forward */
	MLXSW_REG_SFD_REC_ACTION_NOP = 0,
	/* forward and trap, trap_id is FDB_TRAP */
	MLXSW_REG_SFD_REC_ACTION_MIRROR_TO_CPU = 1,
	/* trap and do not forward, trap_id is FDB_TRAP */
	MLXSW_REG_SFD_REC_ACTION_TRAP = 2,
	/* forward to IP router */
	MLXSW_REG_SFD_REC_ACTION_FORWARD_IP_ROUTER = 3,
	MLXSW_REG_SFD_REC_ACTION_DISCARD_ERROR = 15,
};

/* reg_sfd_rec_action
 * Action to apply on the packet.
 * Note: Dynamic entries can only be configured with NOP action.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sfd, rec_action, MLXSW_REG_SFD_BASE_LEN, 28, 4,
		     MLXSW_REG_SFD_REC_LEN, 0x0C, false);

/* reg_sfd_uc_sub_port
 * VEPA channel on local port.
 * Valid only if local port is a non-stacking port. Must be 0 if multichannel
 * VEPA is not enabled.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_sub_port, MLXSW_REG_SFD_BASE_LEN, 16, 8,
		     MLXSW_REG_SFD_REC_LEN, 0x08, false);

/* reg_sfd_uc_fid_vid
 * Filtering ID or VLAN ID
 * For SwitchX and SwitchX-2:
 * - Dynamic entries (policy 2,3) use FID
 * - Static entries (policy 0) use VID
 * - When independent learning is configured, VID=FID
 * For Spectrum: use FID for both Dynamic and Static entries.
 * VID should not be used.
 * Access: Index
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_fid_vid, MLXSW_REG_SFD_BASE_LEN, 0, 16,
		     MLXSW_REG_SFD_REC_LEN, 0x08, false);

/* reg_sfd_uc_system_port
 * Unique port identifier for the final destination of the packet.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_system_port, MLXSW_REG_SFD_BASE_LEN, 0, 16,
		     MLXSW_REG_SFD_REC_LEN, 0x0C, false);

static inline void mlxsw_reg_sfd_rec_pack(char *payload, int rec_index,
					  enum mlxsw_reg_sfd_rec_type rec_type,
					  const char *mac,
					  enum mlxsw_reg_sfd_rec_action action)
{
	u8 num_rec = mlxsw_reg_sfd_num_rec_get(payload);

	if (rec_index >= num_rec)
		mlxsw_reg_sfd_num_rec_set(payload, rec_index + 1);
	mlxsw_reg_sfd_rec_swid_set(payload, rec_index, 0);
	mlxsw_reg_sfd_rec_type_set(payload, rec_index, rec_type);
	mlxsw_reg_sfd_rec_mac_memcpy_to(payload, rec_index, mac);
	mlxsw_reg_sfd_rec_action_set(payload, rec_index, action);
}

static inline void mlxsw_reg_sfd_uc_pack(char *payload, int rec_index,
					 enum mlxsw_reg_sfd_rec_policy policy,
					 const char *mac, u16 fid_vid,
					 enum mlxsw_reg_sfd_rec_action action,
					 u8 local_port)
{
	mlxsw_reg_sfd_rec_pack(payload, rec_index,
			       MLXSW_REG_SFD_REC_TYPE_UNICAST, mac, action);
	mlxsw_reg_sfd_rec_policy_set(payload, rec_index, policy);
	mlxsw_reg_sfd_uc_sub_port_set(payload, rec_index, 0);
	mlxsw_reg_sfd_uc_fid_vid_set(payload, rec_index, fid_vid);
	mlxsw_reg_sfd_uc_system_port_set(payload, rec_index, local_port);
}

static inline void mlxsw_reg_sfd_uc_unpack(char *payload, int rec_index,
					   char *mac, u16 *p_fid_vid,
					   u8 *p_local_port)
{
	mlxsw_reg_sfd_rec_mac_memcpy_from(payload, rec_index, mac);
	*p_fid_vid = mlxsw_reg_sfd_uc_fid_vid_get(payload, rec_index);
	*p_local_port = mlxsw_reg_sfd_uc_system_port_get(payload, rec_index);
}

/* reg_sfd_uc_lag_sub_port
 * LAG sub port.
 * Must be 0 if multichannel VEPA is not enabled.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_lag_sub_port, MLXSW_REG_SFD_BASE_LEN, 16, 8,
		     MLXSW_REG_SFD_REC_LEN, 0x08, false);

/* reg_sfd_uc_lag_fid_vid
 * Filtering ID or VLAN ID
 * For SwitchX and SwitchX-2:
 * - Dynamic entries (policy 2,3) use FID
 * - Static entries (policy 0) use VID
 * - When independent learning is configured, VID=FID
 * For Spectrum: use FID for both Dynamic and Static entries.
 * VID should not be used.
 * Access: Index
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_lag_fid_vid, MLXSW_REG_SFD_BASE_LEN, 0, 16,
		     MLXSW_REG_SFD_REC_LEN, 0x08, false);

/* reg_sfd_uc_lag_lag_vid
 * Indicates VID in case of vFIDs. Reserved for FIDs.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_lag_lag_vid, MLXSW_REG_SFD_BASE_LEN, 16, 12,
		     MLXSW_REG_SFD_REC_LEN, 0x0C, false);

/* reg_sfd_uc_lag_lag_id
 * LAG Identifier - pointer into the LAG descriptor table.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_lag_lag_id, MLXSW_REG_SFD_BASE_LEN, 0, 10,
		     MLXSW_REG_SFD_REC_LEN, 0x0C, false);

static inline void
mlxsw_reg_sfd_uc_lag_pack(char *payload, int rec_index,
			  enum mlxsw_reg_sfd_rec_policy policy,
			  const char *mac, u16 fid_vid,
			  enum mlxsw_reg_sfd_rec_action action, u16 lag_vid,
			  u16 lag_id)
{
	mlxsw_reg_sfd_rec_pack(payload, rec_index,
			       MLXSW_REG_SFD_REC_TYPE_UNICAST_LAG,
			       mac, action);
	mlxsw_reg_sfd_rec_policy_set(payload, rec_index, policy);
	mlxsw_reg_sfd_uc_lag_sub_port_set(payload, rec_index, 0);
	mlxsw_reg_sfd_uc_lag_fid_vid_set(payload, rec_index, fid_vid);
	mlxsw_reg_sfd_uc_lag_lag_vid_set(payload, rec_index, lag_vid);
	mlxsw_reg_sfd_uc_lag_lag_id_set(payload, rec_index, lag_id);
}

static inline void mlxsw_reg_sfd_uc_lag_unpack(char *payload, int rec_index,
					       char *mac, u16 *p_vid,
					       u16 *p_lag_id)
{
	mlxsw_reg_sfd_rec_mac_memcpy_from(payload, rec_index, mac);
	*p_vid = mlxsw_reg_sfd_uc_lag_fid_vid_get(payload, rec_index);
	*p_lag_id = mlxsw_reg_sfd_uc_lag_lag_id_get(payload, rec_index);
}

/* reg_sfd_mc_pgi
 *
 * Multicast port group index - index into the port group table.
 * Value 0x1FFF indicates the pgi should point to the MID entry.
 * For Spectrum this value must be set to 0x1FFF
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sfd, mc_pgi, MLXSW_REG_SFD_BASE_LEN, 16, 13,
		     MLXSW_REG_SFD_REC_LEN, 0x08, false);

/* reg_sfd_mc_fid_vid
 *
 * Filtering ID or VLAN ID
 * Access: Index
 */
MLXSW_ITEM32_INDEXED(reg, sfd, mc_fid_vid, MLXSW_REG_SFD_BASE_LEN, 0, 16,
		     MLXSW_REG_SFD_REC_LEN, 0x08, false);

/* reg_sfd_mc_mid
 *
 * Multicast identifier - global identifier that represents the multicast
 * group across all devices.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sfd, mc_mid, MLXSW_REG_SFD_BASE_LEN, 0, 16,
		     MLXSW_REG_SFD_REC_LEN, 0x0C, false);

static inline void
mlxsw_reg_sfd_mc_pack(char *payload, int rec_index,
		      const char *mac, u16 fid_vid,
		      enum mlxsw_reg_sfd_rec_action action, u16 mid)
{
	mlxsw_reg_sfd_rec_pack(payload, rec_index,
			       MLXSW_REG_SFD_REC_TYPE_MULTICAST, mac, action);
	mlxsw_reg_sfd_mc_pgi_set(payload, rec_index, 0x1FFF);
	mlxsw_reg_sfd_mc_fid_vid_set(payload, rec_index, fid_vid);
	mlxsw_reg_sfd_mc_mid_set(payload, rec_index, mid);
}

/* SFN - Switch FDB Notification Register
 * -------------------------------------------
 * The switch provides notifications on newly learned FDB entries and
 * aged out entries. The notifications can be polled by software.
 */
#define MLXSW_REG_SFN_ID 0x200B
#define MLXSW_REG_SFN_BASE_LEN 0x10 /* base length, without records */
#define MLXSW_REG_SFN_REC_LEN 0x10 /* record length */
#define MLXSW_REG_SFN_REC_MAX_COUNT 64
#define MLXSW_REG_SFN_LEN (MLXSW_REG_SFN_BASE_LEN +	\
			   MLXSW_REG_SFN_REC_LEN * MLXSW_REG_SFN_REC_MAX_COUNT)

static const struct mlxsw_reg_info mlxsw_reg_sfn = {
	.id = MLXSW_REG_SFN_ID,
	.len = MLXSW_REG_SFN_LEN,
};

/* reg_sfn_swid
 * Switch partition ID.
 * Access: Index
 */
MLXSW_ITEM32(reg, sfn, swid, 0x00, 24, 8);

/* reg_sfn_num_rec
 * Request: Number of learned notifications and aged-out notification
 * records requested.
 * Response: Number of notification records returned (must be smaller
 * than or equal to the value requested)
 * Ranges 0..64
 * Access: OP
 */
MLXSW_ITEM32(reg, sfn, num_rec, 0x04, 0, 8);

static inline void mlxsw_reg_sfn_pack(char *payload)
{
	MLXSW_REG_ZERO(sfn, payload);
	mlxsw_reg_sfn_swid_set(payload, 0);
	mlxsw_reg_sfn_num_rec_set(payload, MLXSW_REG_SFN_REC_MAX_COUNT);
}

/* reg_sfn_rec_swid
 * Switch partition ID.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, sfn, rec_swid, MLXSW_REG_SFN_BASE_LEN, 24, 8,
		     MLXSW_REG_SFN_REC_LEN, 0x00, false);

enum mlxsw_reg_sfn_rec_type {
	/* MAC addresses learned on a regular port. */
	MLXSW_REG_SFN_REC_TYPE_LEARNED_MAC = 0x5,
	/* MAC addresses learned on a LAG port. */
	MLXSW_REG_SFN_REC_TYPE_LEARNED_MAC_LAG = 0x6,
	/* Aged-out MAC address on a regular port. */
	MLXSW_REG_SFN_REC_TYPE_AGED_OUT_MAC = 0x7,
	/* Aged-out MAC address on a LAG port. */
	MLXSW_REG_SFN_REC_TYPE_AGED_OUT_MAC_LAG = 0x8,
};

/* reg_sfn_rec_type
 * Notification record type.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, sfn, rec_type, MLXSW_REG_SFN_BASE_LEN, 20, 4,
		     MLXSW_REG_SFN_REC_LEN, 0x00, false);

/* reg_sfn_rec_mac
 * MAC address.
 * Access: RO
 */
MLXSW_ITEM_BUF_INDEXED(reg, sfn, rec_mac, MLXSW_REG_SFN_BASE_LEN, 6,
		       MLXSW_REG_SFN_REC_LEN, 0x02);

/* reg_sfn_mac_sub_port
 * VEPA channel on the local port.
 * 0 if multichannel VEPA is not enabled.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, sfn, mac_sub_port, MLXSW_REG_SFN_BASE_LEN, 16, 8,
		     MLXSW_REG_SFN_REC_LEN, 0x08, false);

/* reg_sfn_mac_fid
 * Filtering identifier.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, sfn, mac_fid, MLXSW_REG_SFN_BASE_LEN, 0, 16,
		     MLXSW_REG_SFN_REC_LEN, 0x08, false);

/* reg_sfn_mac_system_port
 * Unique port identifier for the final destination of the packet.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, sfn, mac_system_port, MLXSW_REG_SFN_BASE_LEN, 0, 16,
		     MLXSW_REG_SFN_REC_LEN, 0x0C, false);

static inline void mlxsw_reg_sfn_mac_unpack(char *payload, int rec_index,
					    char *mac, u16 *p_vid,
					    u8 *p_local_port)
{
	mlxsw_reg_sfn_rec_mac_memcpy_from(payload, rec_index, mac);
	*p_vid = mlxsw_reg_sfn_mac_fid_get(payload, rec_index);
	*p_local_port = mlxsw_reg_sfn_mac_system_port_get(payload, rec_index);
}

/* reg_sfn_mac_lag_lag_id
 * LAG ID (pointer into the LAG descriptor table).
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, sfn, mac_lag_lag_id, MLXSW_REG_SFN_BASE_LEN, 0, 10,
		     MLXSW_REG_SFN_REC_LEN, 0x0C, false);

static inline void mlxsw_reg_sfn_mac_lag_unpack(char *payload, int rec_index,
						char *mac, u16 *p_vid,
						u16 *p_lag_id)
{
	mlxsw_reg_sfn_rec_mac_memcpy_from(payload, rec_index, mac);
	*p_vid = mlxsw_reg_sfn_mac_fid_get(payload, rec_index);
	*p_lag_id = mlxsw_reg_sfn_mac_lag_lag_id_get(payload, rec_index);
}

/* SPMS - Switch Port MSTP/RSTP State Register
 * -------------------------------------------
 * Configures the spanning tree state of a physical port.
 */
#define MLXSW_REG_SPMS_ID 0x200D
#define MLXSW_REG_SPMS_LEN 0x404

static const struct mlxsw_reg_info mlxsw_reg_spms = {
	.id = MLXSW_REG_SPMS_ID,
	.len = MLXSW_REG_SPMS_LEN,
};

/* reg_spms_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, spms, local_port, 0x00, 16, 8);

enum mlxsw_reg_spms_state {
	MLXSW_REG_SPMS_STATE_NO_CHANGE,
	MLXSW_REG_SPMS_STATE_DISCARDING,
	MLXSW_REG_SPMS_STATE_LEARNING,
	MLXSW_REG_SPMS_STATE_FORWARDING,
};

/* reg_spms_state
 * Spanning tree state of each VLAN ID (VID) of the local port.
 * 0 - Do not change spanning tree state (used only when writing).
 * 1 - Discarding. No learning or forwarding to/from this port (default).
 * 2 - Learning. Port is learning, but not forwarding.
 * 3 - Forwarding. Port is learning and forwarding.
 * Access: RW
 */
MLXSW_ITEM_BIT_ARRAY(reg, spms, state, 0x04, 0x400, 2);

static inline void mlxsw_reg_spms_pack(char *payload, u8 local_port)
{
	MLXSW_REG_ZERO(spms, payload);
	mlxsw_reg_spms_local_port_set(payload, local_port);
}

static inline void mlxsw_reg_spms_vid_pack(char *payload, u16 vid,
					   enum mlxsw_reg_spms_state state)
{
	mlxsw_reg_spms_state_set(payload, vid, state);
}

/* SPVID - Switch Port VID
 * -----------------------
 * The switch port VID configures the default VID for a port.
 */
#define MLXSW_REG_SPVID_ID 0x200E
#define MLXSW_REG_SPVID_LEN 0x08

static const struct mlxsw_reg_info mlxsw_reg_spvid = {
	.id = MLXSW_REG_SPVID_ID,
	.len = MLXSW_REG_SPVID_LEN,
};

/* reg_spvid_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, spvid, local_port, 0x00, 16, 8);

/* reg_spvid_sub_port
 * Virtual port within the physical port.
 * Should be set to 0 when virtual ports are not enabled on the port.
 * Access: Index
 */
MLXSW_ITEM32(reg, spvid, sub_port, 0x00, 8, 8);

/* reg_spvid_pvid
 * Port default VID
 * Access: RW
 */
MLXSW_ITEM32(reg, spvid, pvid, 0x04, 0, 12);

static inline void mlxsw_reg_spvid_pack(char *payload, u8 local_port, u16 pvid)
{
	MLXSW_REG_ZERO(spvid, payload);
	mlxsw_reg_spvid_local_port_set(payload, local_port);
	mlxsw_reg_spvid_pvid_set(payload, pvid);
}

/* SPVM - Switch Port VLAN Membership
 * ----------------------------------
 * The Switch Port VLAN Membership register configures the VLAN membership
 * of a port in a VLAN denoted by VID. VLAN membership is managed per
 * virtual port. The register can be used to add and remove VID(s) from a port.
 */
#define MLXSW_REG_SPVM_ID 0x200F
#define MLXSW_REG_SPVM_BASE_LEN 0x04 /* base length, without records */
#define MLXSW_REG_SPVM_REC_LEN 0x04 /* record length */
#define MLXSW_REG_SPVM_REC_MAX_COUNT 256
#define MLXSW_REG_SPVM_LEN (MLXSW_REG_SPVM_BASE_LEN +	\
		    MLXSW_REG_SPVM_REC_LEN * MLXSW_REG_SPVM_REC_MAX_COUNT)

static const struct mlxsw_reg_info mlxsw_reg_spvm = {
	.id = MLXSW_REG_SPVM_ID,
	.len = MLXSW_REG_SPVM_LEN,
};

/* reg_spvm_pt
 * Priority tagged. If this bit is set, packets forwarded to the port with
 * untagged VLAN membership (u bit is set) will be tagged with priority tag
 * (VID=0)
 * Access: RW
 */
MLXSW_ITEM32(reg, spvm, pt, 0x00, 31, 1);

/* reg_spvm_pte
 * Priority Tagged Update Enable. On Write operations, if this bit is cleared,
 * the pt bit will NOT be updated. To update the pt bit, pte must be set.
 * Access: WO
 */
MLXSW_ITEM32(reg, spvm, pte, 0x00, 30, 1);

/* reg_spvm_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, spvm, local_port, 0x00, 16, 8);

/* reg_spvm_sub_port
 * Virtual port within the physical port.
 * Should be set to 0 when virtual ports are not enabled on the port.
 * Access: Index
 */
MLXSW_ITEM32(reg, spvm, sub_port, 0x00, 8, 8);

/* reg_spvm_num_rec
 * Number of records to update. Each record contains: i, e, u, vid.
 * Access: OP
 */
MLXSW_ITEM32(reg, spvm, num_rec, 0x00, 0, 8);

/* reg_spvm_rec_i
 * Ingress membership in VLAN ID.
 * Access: Index
 */
MLXSW_ITEM32_INDEXED(reg, spvm, rec_i,
		     MLXSW_REG_SPVM_BASE_LEN, 14, 1,
		     MLXSW_REG_SPVM_REC_LEN, 0, false);

/* reg_spvm_rec_e
 * Egress membership in VLAN ID.
 * Access: Index
 */
MLXSW_ITEM32_INDEXED(reg, spvm, rec_e,
		     MLXSW_REG_SPVM_BASE_LEN, 13, 1,
		     MLXSW_REG_SPVM_REC_LEN, 0, false);

/* reg_spvm_rec_u
 * Untagged - port is an untagged member - egress transmission uses untagged
 * frames on VID<n>
 * Access: Index
 */
MLXSW_ITEM32_INDEXED(reg, spvm, rec_u,
		     MLXSW_REG_SPVM_BASE_LEN, 12, 1,
		     MLXSW_REG_SPVM_REC_LEN, 0, false);

/* reg_spvm_rec_vid
 * Egress membership in VLAN ID.
 * Access: Index
 */
MLXSW_ITEM32_INDEXED(reg, spvm, rec_vid,
		     MLXSW_REG_SPVM_BASE_LEN, 0, 12,
		     MLXSW_REG_SPVM_REC_LEN, 0, false);

static inline void mlxsw_reg_spvm_pack(char *payload, u8 local_port,
				       u16 vid_begin, u16 vid_end,
				       bool is_member, bool untagged)
{
	int size = vid_end - vid_begin + 1;
	int i;

	MLXSW_REG_ZERO(spvm, payload);
	mlxsw_reg_spvm_local_port_set(payload, local_port);
	mlxsw_reg_spvm_num_rec_set(payload, size);

	for (i = 0; i < size; i++) {
		mlxsw_reg_spvm_rec_i_set(payload, i, is_member);
		mlxsw_reg_spvm_rec_e_set(payload, i, is_member);
		mlxsw_reg_spvm_rec_u_set(payload, i, untagged);
		mlxsw_reg_spvm_rec_vid_set(payload, i, vid_begin + i);
	}
}

/* SPAFT - Switch Port Acceptable Frame Types
 * ------------------------------------------
 * The Switch Port Acceptable Frame Types register configures the frame
 * admittance of the port.
 */
#define MLXSW_REG_SPAFT_ID 0x2010
#define MLXSW_REG_SPAFT_LEN 0x08

static const struct mlxsw_reg_info mlxsw_reg_spaft = {
	.id = MLXSW_REG_SPAFT_ID,
	.len = MLXSW_REG_SPAFT_LEN,
};

/* reg_spaft_local_port
 * Local port number.
 * Access: Index
 *
 * Note: CPU port is not supported (all tag types are allowed).
 */
MLXSW_ITEM32(reg, spaft, local_port, 0x00, 16, 8);

/* reg_spaft_sub_port
 * Virtual port within the physical port.
 * Should be set to 0 when virtual ports are not enabled on the port.
 * Access: RW
 */
MLXSW_ITEM32(reg, spaft, sub_port, 0x00, 8, 8);

/* reg_spaft_allow_untagged
 * When set, untagged frames on the ingress are allowed (default).
 * Access: RW
 */
MLXSW_ITEM32(reg, spaft, allow_untagged, 0x04, 31, 1);

/* reg_spaft_allow_prio_tagged
 * When set, priority tagged frames on the ingress are allowed (default).
 * Access: RW
 */
MLXSW_ITEM32(reg, spaft, allow_prio_tagged, 0x04, 30, 1);

/* reg_spaft_allow_tagged
 * When set, tagged frames on the ingress are allowed (default).
 * Access: RW
 */
MLXSW_ITEM32(reg, spaft, allow_tagged, 0x04, 29, 1);

static inline void mlxsw_reg_spaft_pack(char *payload, u8 local_port,
					bool allow_untagged)
{
	MLXSW_REG_ZERO(spaft, payload);
	mlxsw_reg_spaft_local_port_set(payload, local_port);
	mlxsw_reg_spaft_allow_untagged_set(payload, allow_untagged);
	mlxsw_reg_spaft_allow_prio_tagged_set(payload, true);
	mlxsw_reg_spaft_allow_tagged_set(payload, true);
}

/* SFGC - Switch Flooding Group Configuration
 * ------------------------------------------
 * The following register controls the association of flooding tables and MIDs
 * to packet types used for flooding.
 */
#define MLXSW_REG_SFGC_ID 0x2011
#define MLXSW_REG_SFGC_LEN 0x10

static const struct mlxsw_reg_info mlxsw_reg_sfgc = {
	.id = MLXSW_REG_SFGC_ID,
	.len = MLXSW_REG_SFGC_LEN,
};

enum mlxsw_reg_sfgc_type {
	MLXSW_REG_SFGC_TYPE_BROADCAST,
	MLXSW_REG_SFGC_TYPE_UNKNOWN_UNICAST,
	MLXSW_REG_SFGC_TYPE_UNREGISTERED_MULTICAST_IPV4,
	MLXSW_REG_SFGC_TYPE_UNREGISTERED_MULTICAST_IPV6,
	MLXSW_REG_SFGC_TYPE_RESERVED,
	MLXSW_REG_SFGC_TYPE_UNREGISTERED_MULTICAST_NON_IP,
	MLXSW_REG_SFGC_TYPE_IPV4_LINK_LOCAL,
	MLXSW_REG_SFGC_TYPE_IPV6_ALL_HOST,
	MLXSW_REG_SFGC_TYPE_MAX,
};

/* reg_sfgc_type
 * The traffic type to reach the flooding table.
 * Access: Index
 */
MLXSW_ITEM32(reg, sfgc, type, 0x00, 0, 4);

enum mlxsw_reg_sfgc_bridge_type {
	MLXSW_REG_SFGC_BRIDGE_TYPE_1Q_FID = 0,
	MLXSW_REG_SFGC_BRIDGE_TYPE_VFID = 1,
};

/* reg_sfgc_bridge_type
 * Access: Index
 *
 * Note: SwitchX-2 only supports 802.1Q mode.
 */
MLXSW_ITEM32(reg, sfgc, bridge_type, 0x04, 24, 3);

enum mlxsw_flood_table_type {
	MLXSW_REG_SFGC_TABLE_TYPE_VID = 1,
	MLXSW_REG_SFGC_TABLE_TYPE_SINGLE = 2,
	MLXSW_REG_SFGC_TABLE_TYPE_ANY = 0,
	MLXSW_REG_SFGC_TABLE_TYPE_FID_OFFEST = 3,
	MLXSW_REG_SFGC_TABLE_TYPE_FID = 4,
};

/* reg_sfgc_table_type
 * See mlxsw_flood_table_type
 * Access: RW
 *
 * Note: FID offset and FID types are not supported in SwitchX-2.
 */
MLXSW_ITEM32(reg, sfgc, table_type, 0x04, 16, 3);

/* reg_sfgc_flood_table
 * Flooding table index to associate with the specific type on the specific
 * switch partition.
 * Access: RW
 */
MLXSW_ITEM32(reg, sfgc, flood_table, 0x04, 0, 6);

/* reg_sfgc_mid
 * The multicast ID for the swid. Not supported for Spectrum
 * Access: RW
 */
MLXSW_ITEM32(reg, sfgc, mid, 0x08, 0, 16);

/* reg_sfgc_counter_set_type
 * Counter Set Type for flow counters.
 * Access: RW
 */
MLXSW_ITEM32(reg, sfgc, counter_set_type, 0x0C, 24, 8);

/* reg_sfgc_counter_index
 * Counter Index for flow counters.
 * Access: RW
 */
MLXSW_ITEM32(reg, sfgc, counter_index, 0x0C, 0, 24);

static inline void
mlxsw_reg_sfgc_pack(char *payload, enum mlxsw_reg_sfgc_type type,
		    enum mlxsw_reg_sfgc_bridge_type bridge_type,
		    enum mlxsw_flood_table_type table_type,
		    unsigned int flood_table)
{
	MLXSW_REG_ZERO(sfgc, payload);
	mlxsw_reg_sfgc_type_set(payload, type);
	mlxsw_reg_sfgc_bridge_type_set(payload, bridge_type);
	mlxsw_reg_sfgc_table_type_set(payload, table_type);
	mlxsw_reg_sfgc_flood_table_set(payload, flood_table);
	mlxsw_reg_sfgc_mid_set(payload, MLXSW_PORT_MID);
}

/* SFTR - Switch Flooding Table Register
 * -------------------------------------
 * The switch flooding table is used for flooding packet replication. The table
 * defines a bit mask of ports for packet replication.
 */
#define MLXSW_REG_SFTR_ID 0x2012
#define MLXSW_REG_SFTR_LEN 0x420

static const struct mlxsw_reg_info mlxsw_reg_sftr = {
	.id = MLXSW_REG_SFTR_ID,
	.len = MLXSW_REG_SFTR_LEN,
};

/* reg_sftr_swid
 * Switch partition ID with which to associate the port.
 * Access: Index
 */
MLXSW_ITEM32(reg, sftr, swid, 0x00, 24, 8);

/* reg_sftr_flood_table
 * Flooding table index to associate with the specific type on the specific
 * switch partition.
 * Access: Index
 */
MLXSW_ITEM32(reg, sftr, flood_table, 0x00, 16, 6);

/* reg_sftr_index
 * Index. Used as an index into the Flooding Table in case the table is
 * configured to use VID / FID or FID Offset.
 * Access: Index
 */
MLXSW_ITEM32(reg, sftr, index, 0x00, 0, 16);

/* reg_sftr_table_type
 * See mlxsw_flood_table_type
 * Access: RW
 */
MLXSW_ITEM32(reg, sftr, table_type, 0x04, 16, 3);

/* reg_sftr_range
 * Range of entries to update
 * Access: Index
 */
MLXSW_ITEM32(reg, sftr, range, 0x04, 0, 16);

/* reg_sftr_port
 * Local port membership (1 bit per port).
 * Access: RW
 */
MLXSW_ITEM_BIT_ARRAY(reg, sftr, port, 0x20, 0x20, 1);

/* reg_sftr_cpu_port_mask
 * CPU port mask (1 bit per port).
 * Access: W
 */
MLXSW_ITEM_BIT_ARRAY(reg, sftr, port_mask, 0x220, 0x20, 1);

static inline void mlxsw_reg_sftr_pack(char *payload,
				       unsigned int flood_table,
				       unsigned int index,
				       enum mlxsw_flood_table_type table_type,
				       unsigned int range, u8 port, bool set)
{
	MLXSW_REG_ZERO(sftr, payload);
	mlxsw_reg_sftr_swid_set(payload, 0);
	mlxsw_reg_sftr_flood_table_set(payload, flood_table);
	mlxsw_reg_sftr_index_set(payload, index);
	mlxsw_reg_sftr_table_type_set(payload, table_type);
	mlxsw_reg_sftr_range_set(payload, range);
	mlxsw_reg_sftr_port_set(payload, port, set);
	mlxsw_reg_sftr_port_mask_set(payload, port, 1);
}

/* SFDF - Switch Filtering DB Flush
 * --------------------------------
 * The switch filtering DB flush register is used to flush the FDB.
 * Note that FDB notifications are flushed as well.
 */
#define MLXSW_REG_SFDF_ID 0x2013
#define MLXSW_REG_SFDF_LEN 0x14

static const struct mlxsw_reg_info mlxsw_reg_sfdf = {
	.id = MLXSW_REG_SFDF_ID,
	.len = MLXSW_REG_SFDF_LEN,
};

/* reg_sfdf_swid
 * Switch partition ID.
 * Access: Index
 */
MLXSW_ITEM32(reg, sfdf, swid, 0x00, 24, 8);

enum mlxsw_reg_sfdf_flush_type {
	MLXSW_REG_SFDF_FLUSH_PER_SWID,
	MLXSW_REG_SFDF_FLUSH_PER_FID,
	MLXSW_REG_SFDF_FLUSH_PER_PORT,
	MLXSW_REG_SFDF_FLUSH_PER_PORT_AND_FID,
	MLXSW_REG_SFDF_FLUSH_PER_LAG,
	MLXSW_REG_SFDF_FLUSH_PER_LAG_AND_FID,
};

/* reg_sfdf_flush_type
 * Flush type.
 * 0 - All SWID dynamic entries are flushed.
 * 1 - All FID dynamic entries are flushed.
 * 2 - All dynamic entries pointing to port are flushed.
 * 3 - All FID dynamic entries pointing to port are flushed.
 * 4 - All dynamic entries pointing to LAG are flushed.
 * 5 - All FID dynamic entries pointing to LAG are flushed.
 * Access: RW
 */
MLXSW_ITEM32(reg, sfdf, flush_type, 0x04, 28, 4);

/* reg_sfdf_flush_static
 * Static.
 * 0 - Flush only dynamic entries.
 * 1 - Flush both dynamic and static entries.
 * Access: RW
 */
MLXSW_ITEM32(reg, sfdf, flush_static, 0x04, 24, 1);

static inline void mlxsw_reg_sfdf_pack(char *payload,
				       enum mlxsw_reg_sfdf_flush_type type)
{
	MLXSW_REG_ZERO(sfdf, payload);
	mlxsw_reg_sfdf_flush_type_set(payload, type);
	mlxsw_reg_sfdf_flush_static_set(payload, true);
}

/* reg_sfdf_fid
 * FID to flush.
 * Access: RW
 */
MLXSW_ITEM32(reg, sfdf, fid, 0x0C, 0, 16);

/* reg_sfdf_system_port
 * Port to flush.
 * Access: RW
 */
MLXSW_ITEM32(reg, sfdf, system_port, 0x0C, 0, 16);

/* reg_sfdf_port_fid_system_port
 * Port to flush, pointed to by FID.
 * Access: RW
 */
MLXSW_ITEM32(reg, sfdf, port_fid_system_port, 0x08, 0, 16);

/* reg_sfdf_lag_id
 * LAG ID to flush.
 * Access: RW
 */
MLXSW_ITEM32(reg, sfdf, lag_id, 0x0C, 0, 10);

/* reg_sfdf_lag_fid_lag_id
 * LAG ID to flush, pointed to by FID.
 * Access: RW
 */
MLXSW_ITEM32(reg, sfdf, lag_fid_lag_id, 0x08, 0, 10);

/* SLDR - Switch LAG Descriptor Register
 * -----------------------------------------
 * The switch LAG descriptor register is populated by LAG descriptors.
 * Each LAG descriptor is indexed by lag_id. The LAG ID runs from 0 to
 * max_lag-1.
 */
#define MLXSW_REG_SLDR_ID 0x2014
#define MLXSW_REG_SLDR_LEN 0x0C /* counting in only one port in list */

static const struct mlxsw_reg_info mlxsw_reg_sldr = {
	.id = MLXSW_REG_SLDR_ID,
	.len = MLXSW_REG_SLDR_LEN,
};

enum mlxsw_reg_sldr_op {
	/* Indicates a creation of a new LAG-ID, lag_id must be valid */
	MLXSW_REG_SLDR_OP_LAG_CREATE,
	MLXSW_REG_SLDR_OP_LAG_DESTROY,
	/* Ports that appear in the list have the Distributor enabled */
	MLXSW_REG_SLDR_OP_LAG_ADD_PORT_LIST,
	/* Removes ports from the disributor list */
	MLXSW_REG_SLDR_OP_LAG_REMOVE_PORT_LIST,
};

/* reg_sldr_op
 * Operation.
 * Access: RW
 */
MLXSW_ITEM32(reg, sldr, op, 0x00, 29, 3);

/* reg_sldr_lag_id
 * LAG identifier. The lag_id is the index into the LAG descriptor table.
 * Access: Index
 */
MLXSW_ITEM32(reg, sldr, lag_id, 0x00, 0, 10);

static inline void mlxsw_reg_sldr_lag_create_pack(char *payload, u8 lag_id)
{
	MLXSW_REG_ZERO(sldr, payload);
	mlxsw_reg_sldr_op_set(payload, MLXSW_REG_SLDR_OP_LAG_CREATE);
	mlxsw_reg_sldr_lag_id_set(payload, lag_id);
}

static inline void mlxsw_reg_sldr_lag_destroy_pack(char *payload, u8 lag_id)
{
	MLXSW_REG_ZERO(sldr, payload);
	mlxsw_reg_sldr_op_set(payload, MLXSW_REG_SLDR_OP_LAG_DESTROY);
	mlxsw_reg_sldr_lag_id_set(payload, lag_id);
}

/* reg_sldr_num_ports
 * The number of member ports of the LAG.
 * Reserved for Create / Destroy operations
 * For Add / Remove operations - indicates the number of ports in the list.
 * Access: RW
 */
MLXSW_ITEM32(reg, sldr, num_ports, 0x04, 24, 8);

/* reg_sldr_system_port
 * System port.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sldr, system_port, 0x08, 0, 16, 4, 0, false);

static inline void mlxsw_reg_sldr_lag_add_port_pack(char *payload, u8 lag_id,
						    u8 local_port)
{
	MLXSW_REG_ZERO(sldr, payload);
	mlxsw_reg_sldr_op_set(payload, MLXSW_REG_SLDR_OP_LAG_ADD_PORT_LIST);
	mlxsw_reg_sldr_lag_id_set(payload, lag_id);
	mlxsw_reg_sldr_num_ports_set(payload, 1);
	mlxsw_reg_sldr_system_port_set(payload, 0, local_port);
}

static inline void mlxsw_reg_sldr_lag_remove_port_pack(char *payload, u8 lag_id,
						       u8 local_port)
{
	MLXSW_REG_ZERO(sldr, payload);
	mlxsw_reg_sldr_op_set(payload, MLXSW_REG_SLDR_OP_LAG_REMOVE_PORT_LIST);
	mlxsw_reg_sldr_lag_id_set(payload, lag_id);
	mlxsw_reg_sldr_num_ports_set(payload, 1);
	mlxsw_reg_sldr_system_port_set(payload, 0, local_port);
}

/* SLCR - Switch LAG Configuration 2 Register
 * -------------------------------------------
 * The Switch LAG Configuration register is used for configuring the
 * LAG properties of the switch.
 */
#define MLXSW_REG_SLCR_ID 0x2015
#define MLXSW_REG_SLCR_LEN 0x10

static const struct mlxsw_reg_info mlxsw_reg_slcr = {
	.id = MLXSW_REG_SLCR_ID,
	.len = MLXSW_REG_SLCR_LEN,
};

enum mlxsw_reg_slcr_pp {
	/* Global Configuration (for all ports) */
	MLXSW_REG_SLCR_PP_GLOBAL,
	/* Per port configuration, based on local_port field */
	MLXSW_REG_SLCR_PP_PER_PORT,
};

/* reg_slcr_pp
 * Per Port Configuration
 * Note: Reading at Global mode results in reading port 1 configuration.
 * Access: Index
 */
MLXSW_ITEM32(reg, slcr, pp, 0x00, 24, 1);

/* reg_slcr_local_port
 * Local port number
 * Supported from CPU port
 * Not supported from router port
 * Reserved when pp = Global Configuration
 * Access: Index
 */
MLXSW_ITEM32(reg, slcr, local_port, 0x00, 16, 8);

enum mlxsw_reg_slcr_type {
	MLXSW_REG_SLCR_TYPE_CRC, /* default */
	MLXSW_REG_SLCR_TYPE_XOR,
	MLXSW_REG_SLCR_TYPE_RANDOM,
};

/* reg_slcr_type
 * Hash type
 * Access: RW
 */
MLXSW_ITEM32(reg, slcr, type, 0x00, 0, 4);

/* Ingress port */
#define MLXSW_REG_SLCR_LAG_HASH_IN_PORT		BIT(0)
/* SMAC - for IPv4 and IPv6 packets */
#define MLXSW_REG_SLCR_LAG_HASH_SMAC_IP		BIT(1)
/* SMAC - for non-IP packets */
#define MLXSW_REG_SLCR_LAG_HASH_SMAC_NONIP	BIT(2)
#define MLXSW_REG_SLCR_LAG_HASH_SMAC \
	(MLXSW_REG_SLCR_LAG_HASH_SMAC_IP | \
	 MLXSW_REG_SLCR_LAG_HASH_SMAC_NONIP)
/* DMAC - for IPv4 and IPv6 packets */
#define MLXSW_REG_SLCR_LAG_HASH_DMAC_IP		BIT(3)
/* DMAC - for non-IP packets */
#define MLXSW_REG_SLCR_LAG_HASH_DMAC_NONIP	BIT(4)
#define MLXSW_REG_SLCR_LAG_HASH_DMAC \
	(MLXSW_REG_SLCR_LAG_HASH_DMAC_IP | \
	 MLXSW_REG_SLCR_LAG_HASH_DMAC_NONIP)
/* Ethertype - for IPv4 and IPv6 packets */
#define MLXSW_REG_SLCR_LAG_HASH_ETHERTYPE_IP	BIT(5)
/* Ethertype - for non-IP packets */
#define MLXSW_REG_SLCR_LAG_HASH_ETHERTYPE_NONIP	BIT(6)
#define MLXSW_REG_SLCR_LAG_HASH_ETHERTYPE \
	(MLXSW_REG_SLCR_LAG_HASH_ETHERTYPE_IP | \
	 MLXSW_REG_SLCR_LAG_HASH_ETHERTYPE_NONIP)
/* VLAN ID - for IPv4 and IPv6 packets */
#define MLXSW_REG_SLCR_LAG_HASH_VLANID_IP	BIT(7)
/* VLAN ID - for non-IP packets */
#define MLXSW_REG_SLCR_LAG_HASH_VLANID_NONIP	BIT(8)
#define MLXSW_REG_SLCR_LAG_HASH_VLANID \
	(MLXSW_REG_SLCR_LAG_HASH_VLANID_IP | \
	 MLXSW_REG_SLCR_LAG_HASH_VLANID_NONIP)
/* Source IP address (can be IPv4 or IPv6) */
#define MLXSW_REG_SLCR_LAG_HASH_SIP		BIT(9)
/* Destination IP address (can be IPv4 or IPv6) */
#define MLXSW_REG_SLCR_LAG_HASH_DIP		BIT(10)
/* TCP/UDP source port */
#define MLXSW_REG_SLCR_LAG_HASH_SPORT		BIT(11)
/* TCP/UDP destination port*/
#define MLXSW_REG_SLCR_LAG_HASH_DPORT		BIT(12)
/* IPv4 Protocol/IPv6 Next Header */
#define MLXSW_REG_SLCR_LAG_HASH_IPPROTO		BIT(13)
/* IPv6 Flow label */
#define MLXSW_REG_SLCR_LAG_HASH_FLOWLABEL	BIT(14)
/* SID - FCoE source ID */
#define MLXSW_REG_SLCR_LAG_HASH_FCOE_SID	BIT(15)
/* DID - FCoE destination ID */
#define MLXSW_REG_SLCR_LAG_HASH_FCOE_DID	BIT(16)
/* OXID - FCoE originator exchange ID */
#define MLXSW_REG_SLCR_LAG_HASH_FCOE_OXID	BIT(17)
/* Destination QP number - for RoCE packets */
#define MLXSW_REG_SLCR_LAG_HASH_ROCE_DQP	BIT(19)

/* reg_slcr_lag_hash
 * LAG hashing configuration. This is a bitmask, in which each set
 * bit includes the corresponding item in the LAG hash calculation.
 * The default lag_hash contains SMAC, DMAC, VLANID and
 * Ethertype (for all packet types).
 * Access: RW
 */
MLXSW_ITEM32(reg, slcr, lag_hash, 0x04, 0, 20);

static inline void mlxsw_reg_slcr_pack(char *payload, u16 lag_hash)
{
	MLXSW_REG_ZERO(slcr, payload);
	mlxsw_reg_slcr_pp_set(payload, MLXSW_REG_SLCR_PP_GLOBAL);
	mlxsw_reg_slcr_type_set(payload, MLXSW_REG_SLCR_TYPE_XOR);
	mlxsw_reg_slcr_lag_hash_set(payload, lag_hash);
}

/* SLCOR - Switch LAG Collector Register
 * -------------------------------------
 * The Switch LAG Collector register controls the Local Port membership
 * in a LAG and enablement of the collector.
 */
#define MLXSW_REG_SLCOR_ID 0x2016
#define MLXSW_REG_SLCOR_LEN 0x10

static const struct mlxsw_reg_info mlxsw_reg_slcor = {
	.id = MLXSW_REG_SLCOR_ID,
	.len = MLXSW_REG_SLCOR_LEN,
};

enum mlxsw_reg_slcor_col {
	/* Port is added with collector disabled */
	MLXSW_REG_SLCOR_COL_LAG_ADD_PORT,
	MLXSW_REG_SLCOR_COL_LAG_COLLECTOR_ENABLED,
	MLXSW_REG_SLCOR_COL_LAG_COLLECTOR_DISABLED,
	MLXSW_REG_SLCOR_COL_LAG_REMOVE_PORT,
};

/* reg_slcor_col
 * Collector configuration
 * Access: RW
 */
MLXSW_ITEM32(reg, slcor, col, 0x00, 30, 2);

/* reg_slcor_local_port
 * Local port number
 * Not supported for CPU port
 * Access: Index
 */
MLXSW_ITEM32(reg, slcor, local_port, 0x00, 16, 8);

/* reg_slcor_lag_id
 * LAG Identifier. Index into the LAG descriptor table.
 * Access: Index
 */
MLXSW_ITEM32(reg, slcor, lag_id, 0x00, 0, 10);

/* reg_slcor_port_index
 * Port index in the LAG list. Only valid on Add Port to LAG col.
 * Valid range is from 0 to cap_max_lag_members-1
 * Access: RW
 */
MLXSW_ITEM32(reg, slcor, port_index, 0x04, 0, 10);

static inline void mlxsw_reg_slcor_pack(char *payload,
					u8 local_port, u16 lag_id,
					enum mlxsw_reg_slcor_col col)
{
	MLXSW_REG_ZERO(slcor, payload);
	mlxsw_reg_slcor_col_set(payload, col);
	mlxsw_reg_slcor_local_port_set(payload, local_port);
	mlxsw_reg_slcor_lag_id_set(payload, lag_id);
}

static inline void mlxsw_reg_slcor_port_add_pack(char *payload,
						 u8 local_port, u16 lag_id,
						 u8 port_index)
{
	mlxsw_reg_slcor_pack(payload, local_port, lag_id,
			     MLXSW_REG_SLCOR_COL_LAG_ADD_PORT);
	mlxsw_reg_slcor_port_index_set(payload, port_index);
}

static inline void mlxsw_reg_slcor_port_remove_pack(char *payload,
						    u8 local_port, u16 lag_id)
{
	mlxsw_reg_slcor_pack(payload, local_port, lag_id,
			     MLXSW_REG_SLCOR_COL_LAG_REMOVE_PORT);
}

static inline void mlxsw_reg_slcor_col_enable_pack(char *payload,
						   u8 local_port, u16 lag_id)
{
	mlxsw_reg_slcor_pack(payload, local_port, lag_id,
			     MLXSW_REG_SLCOR_COL_LAG_COLLECTOR_ENABLED);
}

static inline void mlxsw_reg_slcor_col_disable_pack(char *payload,
						    u8 local_port, u16 lag_id)
{
	mlxsw_reg_slcor_pack(payload, local_port, lag_id,
			     MLXSW_REG_SLCOR_COL_LAG_COLLECTOR_ENABLED);
}

/* SPMLR - Switch Port MAC Learning Register
 * -----------------------------------------
 * Controls the Switch MAC learning policy per port.
 */
#define MLXSW_REG_SPMLR_ID 0x2018
#define MLXSW_REG_SPMLR_LEN 0x8

static const struct mlxsw_reg_info mlxsw_reg_spmlr = {
	.id = MLXSW_REG_SPMLR_ID,
	.len = MLXSW_REG_SPMLR_LEN,
};

/* reg_spmlr_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, spmlr, local_port, 0x00, 16, 8);

/* reg_spmlr_sub_port
 * Virtual port within the physical port.
 * Should be set to 0 when virtual ports are not enabled on the port.
 * Access: Index
 */
MLXSW_ITEM32(reg, spmlr, sub_port, 0x00, 8, 8);

enum mlxsw_reg_spmlr_learn_mode {
	MLXSW_REG_SPMLR_LEARN_MODE_DISABLE = 0,
	MLXSW_REG_SPMLR_LEARN_MODE_ENABLE = 2,
	MLXSW_REG_SPMLR_LEARN_MODE_SEC = 3,
};

/* reg_spmlr_learn_mode
 * Learning mode on the port.
 * 0 - Learning disabled.
 * 2 - Learning enabled.
 * 3 - Security mode.
 *
 * In security mode the switch does not learn MACs on the port, but uses the
 * SMAC to see if it exists on another ingress port. If so, the packet is
 * classified as a bad packet and is discarded unless the software registers
 * to receive port security error packets usign HPKT.
 */
MLXSW_ITEM32(reg, spmlr, learn_mode, 0x04, 30, 2);

static inline void mlxsw_reg_spmlr_pack(char *payload, u8 local_port,
					enum mlxsw_reg_spmlr_learn_mode mode)
{
	MLXSW_REG_ZERO(spmlr, payload);
	mlxsw_reg_spmlr_local_port_set(payload, local_port);
	mlxsw_reg_spmlr_sub_port_set(payload, 0);
	mlxsw_reg_spmlr_learn_mode_set(payload, mode);
}

/* SVFA - Switch VID to FID Allocation Register
 * --------------------------------------------
 * Controls the VID to FID mapping and {Port, VID} to FID mapping for
 * virtualized ports.
 */
#define MLXSW_REG_SVFA_ID 0x201C
#define MLXSW_REG_SVFA_LEN 0x10

static const struct mlxsw_reg_info mlxsw_reg_svfa = {
	.id = MLXSW_REG_SVFA_ID,
	.len = MLXSW_REG_SVFA_LEN,
};

/* reg_svfa_swid
 * Switch partition ID.
 * Access: Index
 */
MLXSW_ITEM32(reg, svfa, swid, 0x00, 24, 8);

/* reg_svfa_local_port
 * Local port number.
 * Access: Index
 *
 * Note: Reserved for 802.1Q FIDs.
 */
MLXSW_ITEM32(reg, svfa, local_port, 0x00, 16, 8);

enum mlxsw_reg_svfa_mt {
	MLXSW_REG_SVFA_MT_VID_TO_FID,
	MLXSW_REG_SVFA_MT_PORT_VID_TO_FID,
};

/* reg_svfa_mapping_table
 * Mapping table:
 * 0 - VID to FID
 * 1 - {Port, VID} to FID
 * Access: Index
 *
 * Note: Reserved for SwitchX-2.
 */
MLXSW_ITEM32(reg, svfa, mapping_table, 0x00, 8, 3);

/* reg_svfa_v
 * Valid.
 * Valid if set.
 * Access: RW
 *
 * Note: Reserved for SwitchX-2.
 */
MLXSW_ITEM32(reg, svfa, v, 0x00, 0, 1);

/* reg_svfa_fid
 * Filtering ID.
 * Access: RW
 */
MLXSW_ITEM32(reg, svfa, fid, 0x04, 16, 16);

/* reg_svfa_vid
 * VLAN ID.
 * Access: Index
 */
MLXSW_ITEM32(reg, svfa, vid, 0x04, 0, 12);

/* reg_svfa_counter_set_type
 * Counter set type for flow counters.
 * Access: RW
 *
 * Note: Reserved for SwitchX-2.
 */
MLXSW_ITEM32(reg, svfa, counter_set_type, 0x08, 24, 8);

/* reg_svfa_counter_index
 * Counter index for flow counters.
 * Access: RW
 *
 * Note: Reserved for SwitchX-2.
 */
MLXSW_ITEM32(reg, svfa, counter_index, 0x08, 0, 24);

static inline void mlxsw_reg_svfa_pack(char *payload, u8 local_port,
				       enum mlxsw_reg_svfa_mt mt, bool valid,
				       u16 fid, u16 vid)
{
	MLXSW_REG_ZERO(svfa, payload);
	local_port = mt == MLXSW_REG_SVFA_MT_VID_TO_FID ? 0 : local_port;
	mlxsw_reg_svfa_swid_set(payload, 0);
	mlxsw_reg_svfa_local_port_set(payload, local_port);
	mlxsw_reg_svfa_mapping_table_set(payload, mt);
	mlxsw_reg_svfa_v_set(payload, valid);
	mlxsw_reg_svfa_fid_set(payload, fid);
	mlxsw_reg_svfa_vid_set(payload, vid);
}

/* SVPE - Switch Virtual-Port Enabling Register
 * --------------------------------------------
 * Enables port virtualization.
 */
#define MLXSW_REG_SVPE_ID 0x201E
#define MLXSW_REG_SVPE_LEN 0x4

static const struct mlxsw_reg_info mlxsw_reg_svpe = {
	.id = MLXSW_REG_SVPE_ID,
	.len = MLXSW_REG_SVPE_LEN,
};

/* reg_svpe_local_port
 * Local port number
 * Access: Index
 *
 * Note: CPU port is not supported (uses VLAN mode only).
 */
MLXSW_ITEM32(reg, svpe, local_port, 0x00, 16, 8);

/* reg_svpe_vp_en
 * Virtual port enable.
 * 0 - Disable, VLAN mode (VID to FID).
 * 1 - Enable, Virtual port mode ({Port, VID} to FID).
 * Access: RW
 */
MLXSW_ITEM32(reg, svpe, vp_en, 0x00, 8, 1);

static inline void mlxsw_reg_svpe_pack(char *payload, u8 local_port,
				       bool enable)
{
	MLXSW_REG_ZERO(svpe, payload);
	mlxsw_reg_svpe_local_port_set(payload, local_port);
	mlxsw_reg_svpe_vp_en_set(payload, enable);
}

/* SFMR - Switch FID Management Register
 * -------------------------------------
 * Creates and configures FIDs.
 */
#define MLXSW_REG_SFMR_ID 0x201F
#define MLXSW_REG_SFMR_LEN 0x18

static const struct mlxsw_reg_info mlxsw_reg_sfmr = {
	.id = MLXSW_REG_SFMR_ID,
	.len = MLXSW_REG_SFMR_LEN,
};

enum mlxsw_reg_sfmr_op {
	MLXSW_REG_SFMR_OP_CREATE_FID,
	MLXSW_REG_SFMR_OP_DESTROY_FID,
};

/* reg_sfmr_op
 * Operation.
 * 0 - Create or edit FID.
 * 1 - Destroy FID.
 * Access: WO
 */
MLXSW_ITEM32(reg, sfmr, op, 0x00, 24, 4);

/* reg_sfmr_fid
 * Filtering ID.
 * Access: Index
 */
MLXSW_ITEM32(reg, sfmr, fid, 0x00, 0, 16);

/* reg_sfmr_fid_offset
 * FID offset.
 * Used to point into the flooding table selected by SFGC register if
 * the table is of type FID-Offset. Otherwise, this field is reserved.
 * Access: RW
 */
MLXSW_ITEM32(reg, sfmr, fid_offset, 0x08, 0, 16);

/* reg_sfmr_vtfp
 * Valid Tunnel Flood Pointer.
 * If not set, then nve_tunnel_flood_ptr is reserved and considered NULL.
 * Access: RW
 *
 * Note: Reserved for 802.1Q FIDs.
 */
MLXSW_ITEM32(reg, sfmr, vtfp, 0x0C, 31, 1);

/* reg_sfmr_nve_tunnel_flood_ptr
 * Underlay Flooding and BC Pointer.
 * Used as a pointer to the first entry of the group based link lists of
 * flooding or BC entries (for NVE tunnels).
 * Access: RW
 */
MLXSW_ITEM32(reg, sfmr, nve_tunnel_flood_ptr, 0x0C, 0, 24);

/* reg_sfmr_vv
 * VNI Valid.
 * If not set, then vni is reserved.
 * Access: RW
 *
 * Note: Reserved for 802.1Q FIDs.
 */
MLXSW_ITEM32(reg, sfmr, vv, 0x10, 31, 1);

/* reg_sfmr_vni
 * Virtual Network Identifier.
 * Access: RW
 *
 * Note: A given VNI can only be assigned to one FID.
 */
MLXSW_ITEM32(reg, sfmr, vni, 0x10, 0, 24);

static inline void mlxsw_reg_sfmr_pack(char *payload,
				       enum mlxsw_reg_sfmr_op op, u16 fid,
				       u16 fid_offset)
{
	MLXSW_REG_ZERO(sfmr, payload);
	mlxsw_reg_sfmr_op_set(payload, op);
	mlxsw_reg_sfmr_fid_set(payload, fid);
	mlxsw_reg_sfmr_fid_offset_set(payload, fid_offset);
	mlxsw_reg_sfmr_vtfp_set(payload, false);
	mlxsw_reg_sfmr_vv_set(payload, false);
}

/* SPVMLR - Switch Port VLAN MAC Learning Register
 * -----------------------------------------------
 * Controls the switch MAC learning policy per {Port, VID}.
 */
#define MLXSW_REG_SPVMLR_ID 0x2020
#define MLXSW_REG_SPVMLR_BASE_LEN 0x04 /* base length, without records */
#define MLXSW_REG_SPVMLR_REC_LEN 0x04 /* record length */
#define MLXSW_REG_SPVMLR_REC_MAX_COUNT 256
#define MLXSW_REG_SPVMLR_LEN (MLXSW_REG_SPVMLR_BASE_LEN + \
			      MLXSW_REG_SPVMLR_REC_LEN * \
			      MLXSW_REG_SPVMLR_REC_MAX_COUNT)

static const struct mlxsw_reg_info mlxsw_reg_spvmlr = {
	.id = MLXSW_REG_SPVMLR_ID,
	.len = MLXSW_REG_SPVMLR_LEN,
};

/* reg_spvmlr_local_port
 * Local ingress port.
 * Access: Index
 *
 * Note: CPU port is not supported.
 */
MLXSW_ITEM32(reg, spvmlr, local_port, 0x00, 16, 8);

/* reg_spvmlr_num_rec
 * Number of records to update.
 * Access: OP
 */
MLXSW_ITEM32(reg, spvmlr, num_rec, 0x00, 0, 8);

/* reg_spvmlr_rec_learn_enable
 * 0 - Disable learning for {Port, VID}.
 * 1 - Enable learning for {Port, VID}.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, spvmlr, rec_learn_enable, MLXSW_REG_SPVMLR_BASE_LEN,
		     31, 1, MLXSW_REG_SPVMLR_REC_LEN, 0x00, false);

/* reg_spvmlr_rec_vid
 * VLAN ID to be added/removed from port or for querying.
 * Access: Index
 */
MLXSW_ITEM32_INDEXED(reg, spvmlr, rec_vid, MLXSW_REG_SPVMLR_BASE_LEN, 0, 12,
		     MLXSW_REG_SPVMLR_REC_LEN, 0x00, false);

static inline void mlxsw_reg_spvmlr_pack(char *payload, u8 local_port,
					 u16 vid_begin, u16 vid_end,
					 bool learn_enable)
{
	int num_rec = vid_end - vid_begin + 1;
	int i;

	WARN_ON(num_rec < 1 || num_rec > MLXSW_REG_SPVMLR_REC_MAX_COUNT);

	MLXSW_REG_ZERO(spvmlr, payload);
	mlxsw_reg_spvmlr_local_port_set(payload, local_port);
	mlxsw_reg_spvmlr_num_rec_set(payload, num_rec);

	for (i = 0; i < num_rec; i++) {
		mlxsw_reg_spvmlr_rec_learn_enable_set(payload, i, learn_enable);
		mlxsw_reg_spvmlr_rec_vid_set(payload, i, vid_begin + i);
	}
}

/* QTCT - QoS Switch Traffic Class Table
 * -------------------------------------
 * Configures the mapping between the packet switch priority and the
 * traffic class on the transmit port.
 */
#define MLXSW_REG_QTCT_ID 0x400A
#define MLXSW_REG_QTCT_LEN 0x08

static const struct mlxsw_reg_info mlxsw_reg_qtct = {
	.id = MLXSW_REG_QTCT_ID,
	.len = MLXSW_REG_QTCT_LEN,
};

/* reg_qtct_local_port
 * Local port number.
 * Access: Index
 *
 * Note: CPU port is not supported.
 */
MLXSW_ITEM32(reg, qtct, local_port, 0x00, 16, 8);

/* reg_qtct_sub_port
 * Virtual port within the physical port.
 * Should be set to 0 when virtual ports are not enabled on the port.
 * Access: Index
 */
MLXSW_ITEM32(reg, qtct, sub_port, 0x00, 8, 8);

/* reg_qtct_switch_prio
 * Switch priority.
 * Access: Index
 */
MLXSW_ITEM32(reg, qtct, switch_prio, 0x00, 0, 4);

/* reg_qtct_tclass
 * Traffic class.
 * Default values:
 * switch_prio 0 : tclass 1
 * switch_prio 1 : tclass 0
 * switch_prio i : tclass i, for i > 1
 * Access: RW
 */
MLXSW_ITEM32(reg, qtct, tclass, 0x04, 0, 4);

static inline void mlxsw_reg_qtct_pack(char *payload, u8 local_port,
				       u8 switch_prio, u8 tclass)
{
	MLXSW_REG_ZERO(qtct, payload);
	mlxsw_reg_qtct_local_port_set(payload, local_port);
	mlxsw_reg_qtct_switch_prio_set(payload, switch_prio);
	mlxsw_reg_qtct_tclass_set(payload, tclass);
}

/* QEEC - QoS ETS Element Configuration Register
 * ---------------------------------------------
 * Configures the ETS elements.
 */
#define MLXSW_REG_QEEC_ID 0x400D
#define MLXSW_REG_QEEC_LEN 0x1C

static const struct mlxsw_reg_info mlxsw_reg_qeec = {
	.id = MLXSW_REG_QEEC_ID,
	.len = MLXSW_REG_QEEC_LEN,
};

/* reg_qeec_local_port
 * Local port number.
 * Access: Index
 *
 * Note: CPU port is supported.
 */
MLXSW_ITEM32(reg, qeec, local_port, 0x00, 16, 8);

enum mlxsw_reg_qeec_hr {
	MLXSW_REG_QEEC_HIERARCY_PORT,
	MLXSW_REG_QEEC_HIERARCY_GROUP,
	MLXSW_REG_QEEC_HIERARCY_SUBGROUP,
	MLXSW_REG_QEEC_HIERARCY_TC,
};

/* reg_qeec_element_hierarchy
 * 0 - Port
 * 1 - Group
 * 2 - Subgroup
 * 3 - Traffic Class
 * Access: Index
 */
MLXSW_ITEM32(reg, qeec, element_hierarchy, 0x04, 16, 4);

/* reg_qeec_element_index
 * The index of the element in the hierarchy.
 * Access: Index
 */
MLXSW_ITEM32(reg, qeec, element_index, 0x04, 0, 8);

/* reg_qeec_next_element_index
 * The index of the next (lower) element in the hierarchy.
 * Access: RW
 *
 * Note: Reserved for element_hierarchy 0.
 */
MLXSW_ITEM32(reg, qeec, next_element_index, 0x08, 0, 8);

enum {
	MLXSW_REG_QEEC_BYTES_MODE,
	MLXSW_REG_QEEC_PACKETS_MODE,
};

/* reg_qeec_pb
 * Packets or bytes mode.
 * 0 - Bytes mode
 * 1 - Packets mode
 * Access: RW
 *
 * Note: Used for max shaper configuration. For Spectrum, packets mode
 * is supported only for traffic classes of CPU port.
 */
MLXSW_ITEM32(reg, qeec, pb, 0x0C, 28, 1);

/* reg_qeec_mase
 * Max shaper configuration enable. Enables configuration of the max
 * shaper on this ETS element.
 * 0 - Disable
 * 1 - Enable
 * Access: RW
 */
MLXSW_ITEM32(reg, qeec, mase, 0x10, 31, 1);

/* A large max rate will disable the max shaper. */
#define MLXSW_REG_QEEC_MAS_DIS	200000000	/* Kbps */

/* reg_qeec_max_shaper_rate
 * Max shaper information rate.
 * For CPU port, can only be configured for port hierarchy.
 * When in bytes mode, value is specified in units of 1000bps.
 * Access: RW
 */
MLXSW_ITEM32(reg, qeec, max_shaper_rate, 0x10, 0, 28);

/* reg_qeec_de
 * DWRR configuration enable. Enables configuration of the dwrr and
 * dwrr_weight.
 * 0 - Disable
 * 1 - Enable
 * Access: RW
 */
MLXSW_ITEM32(reg, qeec, de, 0x18, 31, 1);

/* reg_qeec_dwrr
 * Transmission selection algorithm to use on the link going down from
 * the ETS element.
 * 0 - Strict priority
 * 1 - DWRR
 * Access: RW
 */
MLXSW_ITEM32(reg, qeec, dwrr, 0x18, 15, 1);

/* reg_qeec_dwrr_weight
 * DWRR weight on the link going down from the ETS element. The
 * percentage of bandwidth guaranteed to an ETS element within
 * its hierarchy. The sum of all weights across all ETS elements
 * within one hierarchy should be equal to 100. Reserved when
 * transmission selection algorithm is strict priority.
 * Access: RW
 */
MLXSW_ITEM32(reg, qeec, dwrr_weight, 0x18, 0, 8);

static inline void mlxsw_reg_qeec_pack(char *payload, u8 local_port,
				       enum mlxsw_reg_qeec_hr hr, u8 index,
				       u8 next_index)
{
	MLXSW_REG_ZERO(qeec, payload);
	mlxsw_reg_qeec_local_port_set(payload, local_port);
	mlxsw_reg_qeec_element_hierarchy_set(payload, hr);
	mlxsw_reg_qeec_element_index_set(payload, index);
	mlxsw_reg_qeec_next_element_index_set(payload, next_index);
}

/* PMLP - Ports Module to Local Port Register
 * ------------------------------------------
 * Configures the assignment of modules to local ports.
 */
#define MLXSW_REG_PMLP_ID 0x5002
#define MLXSW_REG_PMLP_LEN 0x40

static const struct mlxsw_reg_info mlxsw_reg_pmlp = {
	.id = MLXSW_REG_PMLP_ID,
	.len = MLXSW_REG_PMLP_LEN,
};

/* reg_pmlp_rxtx
 * 0 - Tx value is used for both Tx and Rx.
 * 1 - Rx value is taken from a separte field.
 * Access: RW
 */
MLXSW_ITEM32(reg, pmlp, rxtx, 0x00, 31, 1);

/* reg_pmlp_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, pmlp, local_port, 0x00, 16, 8);

/* reg_pmlp_width
 * 0 - Unmap local port.
 * 1 - Lane 0 is used.
 * 2 - Lanes 0 and 1 are used.
 * 4 - Lanes 0, 1, 2 and 3 are used.
 * Access: RW
 */
MLXSW_ITEM32(reg, pmlp, width, 0x00, 0, 8);

/* reg_pmlp_module
 * Module number.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, pmlp, module, 0x04, 0, 8, 0x04, 0x00, false);

/* reg_pmlp_tx_lane
 * Tx Lane. When rxtx field is cleared, this field is used for Rx as well.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, pmlp, tx_lane, 0x04, 16, 2, 0x04, 0x00, false);

/* reg_pmlp_rx_lane
 * Rx Lane. When rxtx field is cleared, this field is ignored and Rx lane is
 * equal to Tx lane.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, pmlp, rx_lane, 0x04, 24, 2, 0x04, 0x00, false);

static inline void mlxsw_reg_pmlp_pack(char *payload, u8 local_port)
{
	MLXSW_REG_ZERO(pmlp, payload);
	mlxsw_reg_pmlp_local_port_set(payload, local_port);
}

/* PMTU - Port MTU Register
 * ------------------------
 * Configures and reports the port MTU.
 */
#define MLXSW_REG_PMTU_ID 0x5003
#define MLXSW_REG_PMTU_LEN 0x10

static const struct mlxsw_reg_info mlxsw_reg_pmtu = {
	.id = MLXSW_REG_PMTU_ID,
	.len = MLXSW_REG_PMTU_LEN,
};

/* reg_pmtu_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, pmtu, local_port, 0x00, 16, 8);

/* reg_pmtu_max_mtu
 * Maximum MTU.
 * When port type (e.g. Ethernet) is configured, the relevant MTU is
 * reported, otherwise the minimum between the max_mtu of the different
 * types is reported.
 * Access: RO
 */
MLXSW_ITEM32(reg, pmtu, max_mtu, 0x04, 16, 16);

/* reg_pmtu_admin_mtu
 * MTU value to set port to. Must be smaller or equal to max_mtu.
 * Note: If port type is Infiniband, then port must be disabled, when its
 * MTU is set.
 * Access: RW
 */
MLXSW_ITEM32(reg, pmtu, admin_mtu, 0x08, 16, 16);

/* reg_pmtu_oper_mtu
 * The actual MTU configured on the port. Packets exceeding this size
 * will be dropped.
 * Note: In Ethernet and FC oper_mtu == admin_mtu, however, in Infiniband
 * oper_mtu might be smaller than admin_mtu.
 * Access: RO
 */
MLXSW_ITEM32(reg, pmtu, oper_mtu, 0x0C, 16, 16);

static inline void mlxsw_reg_pmtu_pack(char *payload, u8 local_port,
				       u16 new_mtu)
{
	MLXSW_REG_ZERO(pmtu, payload);
	mlxsw_reg_pmtu_local_port_set(payload, local_port);
	mlxsw_reg_pmtu_max_mtu_set(payload, 0);
	mlxsw_reg_pmtu_admin_mtu_set(payload, new_mtu);
	mlxsw_reg_pmtu_oper_mtu_set(payload, 0);
}

/* PTYS - Port Type and Speed Register
 * -----------------------------------
 * Configures and reports the port speed type.
 *
 * Note: When set while the link is up, the changes will not take effect
 * until the port transitions from down to up state.
 */
#define MLXSW_REG_PTYS_ID 0x5004
#define MLXSW_REG_PTYS_LEN 0x40

static const struct mlxsw_reg_info mlxsw_reg_ptys = {
	.id = MLXSW_REG_PTYS_ID,
	.len = MLXSW_REG_PTYS_LEN,
};

/* reg_ptys_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, ptys, local_port, 0x00, 16, 8);

#define MLXSW_REG_PTYS_PROTO_MASK_ETH	BIT(2)

/* reg_ptys_proto_mask
 * Protocol mask. Indicates which protocol is used.
 * 0 - Infiniband.
 * 1 - Fibre Channel.
 * 2 - Ethernet.
 * Access: Index
 */
MLXSW_ITEM32(reg, ptys, proto_mask, 0x00, 0, 3);

#define MLXSW_REG_PTYS_ETH_SPEED_SGMII			BIT(0)
#define MLXSW_REG_PTYS_ETH_SPEED_1000BASE_KX		BIT(1)
#define MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CX4		BIT(2)
#define MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KX4		BIT(3)
#define MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KR		BIT(4)
#define MLXSW_REG_PTYS_ETH_SPEED_20GBASE_KR2		BIT(5)
#define MLXSW_REG_PTYS_ETH_SPEED_40GBASE_CR4		BIT(6)
#define MLXSW_REG_PTYS_ETH_SPEED_40GBASE_KR4		BIT(7)
#define MLXSW_REG_PTYS_ETH_SPEED_56GBASE_R4		BIT(8)
#define MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CR		BIT(12)
#define MLXSW_REG_PTYS_ETH_SPEED_10GBASE_SR		BIT(13)
#define MLXSW_REG_PTYS_ETH_SPEED_10GBASE_ER_LR		BIT(14)
#define MLXSW_REG_PTYS_ETH_SPEED_40GBASE_SR4		BIT(15)
#define MLXSW_REG_PTYS_ETH_SPEED_40GBASE_LR4_ER4	BIT(16)
#define MLXSW_REG_PTYS_ETH_SPEED_50GBASE_KR4		BIT(19)
#define MLXSW_REG_PTYS_ETH_SPEED_100GBASE_CR4		BIT(20)
#define MLXSW_REG_PTYS_ETH_SPEED_100GBASE_SR4		BIT(21)
#define MLXSW_REG_PTYS_ETH_SPEED_100GBASE_KR4		BIT(22)
#define MLXSW_REG_PTYS_ETH_SPEED_100GBASE_LR4_ER4	BIT(23)
#define MLXSW_REG_PTYS_ETH_SPEED_100BASE_TX		BIT(24)
#define MLXSW_REG_PTYS_ETH_SPEED_100BASE_T		BIT(25)
#define MLXSW_REG_PTYS_ETH_SPEED_10GBASE_T		BIT(26)
#define MLXSW_REG_PTYS_ETH_SPEED_25GBASE_CR		BIT(27)
#define MLXSW_REG_PTYS_ETH_SPEED_25GBASE_KR		BIT(28)
#define MLXSW_REG_PTYS_ETH_SPEED_25GBASE_SR		BIT(29)
#define MLXSW_REG_PTYS_ETH_SPEED_50GBASE_CR2		BIT(30)
#define MLXSW_REG_PTYS_ETH_SPEED_50GBASE_KR2		BIT(31)

/* reg_ptys_eth_proto_cap
 * Ethernet port supported speeds and protocols.
 * Access: RO
 */
MLXSW_ITEM32(reg, ptys, eth_proto_cap, 0x0C, 0, 32);

/* reg_ptys_eth_proto_admin
 * Speed and protocol to set port to.
 * Access: RW
 */
MLXSW_ITEM32(reg, ptys, eth_proto_admin, 0x18, 0, 32);

/* reg_ptys_eth_proto_oper
 * The current speed and protocol configured for the port.
 * Access: RO
 */
MLXSW_ITEM32(reg, ptys, eth_proto_oper, 0x24, 0, 32);

static inline void mlxsw_reg_ptys_pack(char *payload, u8 local_port,
				       u32 proto_admin)
{
	MLXSW_REG_ZERO(ptys, payload);
	mlxsw_reg_ptys_local_port_set(payload, local_port);
	mlxsw_reg_ptys_proto_mask_set(payload, MLXSW_REG_PTYS_PROTO_MASK_ETH);
	mlxsw_reg_ptys_eth_proto_admin_set(payload, proto_admin);
}

static inline void mlxsw_reg_ptys_unpack(char *payload, u32 *p_eth_proto_cap,
					 u32 *p_eth_proto_adm,
					 u32 *p_eth_proto_oper)
{
	if (p_eth_proto_cap)
		*p_eth_proto_cap = mlxsw_reg_ptys_eth_proto_cap_get(payload);
	if (p_eth_proto_adm)
		*p_eth_proto_adm = mlxsw_reg_ptys_eth_proto_admin_get(payload);
	if (p_eth_proto_oper)
		*p_eth_proto_oper = mlxsw_reg_ptys_eth_proto_oper_get(payload);
}

/* PPAD - Port Physical Address Register
 * -------------------------------------
 * The PPAD register configures the per port physical MAC address.
 */
#define MLXSW_REG_PPAD_ID 0x5005
#define MLXSW_REG_PPAD_LEN 0x10

static const struct mlxsw_reg_info mlxsw_reg_ppad = {
	.id = MLXSW_REG_PPAD_ID,
	.len = MLXSW_REG_PPAD_LEN,
};

/* reg_ppad_single_base_mac
 * 0: base_mac, local port should be 0 and mac[7:0] is
 * reserved. HW will set incremental
 * 1: single_mac - mac of the local_port
 * Access: RW
 */
MLXSW_ITEM32(reg, ppad, single_base_mac, 0x00, 28, 1);

/* reg_ppad_local_port
 * port number, if single_base_mac = 0 then local_port is reserved
 * Access: RW
 */
MLXSW_ITEM32(reg, ppad, local_port, 0x00, 16, 8);

/* reg_ppad_mac
 * If single_base_mac = 0 - base MAC address, mac[7:0] is reserved.
 * If single_base_mac = 1 - the per port MAC address
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, ppad, mac, 0x02, 6);

static inline void mlxsw_reg_ppad_pack(char *payload, bool single_base_mac,
				       u8 local_port)
{
	MLXSW_REG_ZERO(ppad, payload);
	mlxsw_reg_ppad_single_base_mac_set(payload, !!single_base_mac);
	mlxsw_reg_ppad_local_port_set(payload, local_port);
}

/* PAOS - Ports Administrative and Operational Status Register
 * -----------------------------------------------------------
 * Configures and retrieves per port administrative and operational status.
 */
#define MLXSW_REG_PAOS_ID 0x5006
#define MLXSW_REG_PAOS_LEN 0x10

static const struct mlxsw_reg_info mlxsw_reg_paos = {
	.id = MLXSW_REG_PAOS_ID,
	.len = MLXSW_REG_PAOS_LEN,
};

/* reg_paos_swid
 * Switch partition ID with which to associate the port.
 * Note: while external ports uses unique local port numbers (and thus swid is
 * redundant), router ports use the same local port number where swid is the
 * only indication for the relevant port.
 * Access: Index
 */
MLXSW_ITEM32(reg, paos, swid, 0x00, 24, 8);

/* reg_paos_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, paos, local_port, 0x00, 16, 8);

/* reg_paos_admin_status
 * Port administrative state (the desired state of the port):
 * 1 - Up.
 * 2 - Down.
 * 3 - Up once. This means that in case of link failure, the port won't go
 *     into polling mode, but will wait to be re-enabled by software.
 * 4 - Disabled by system. Can only be set by hardware.
 * Access: RW
 */
MLXSW_ITEM32(reg, paos, admin_status, 0x00, 8, 4);

/* reg_paos_oper_status
 * Port operational state (the current state):
 * 1 - Up.
 * 2 - Down.
 * 3 - Down by port failure. This means that the device will not let the
 *     port up again until explicitly specified by software.
 * Access: RO
 */
MLXSW_ITEM32(reg, paos, oper_status, 0x00, 0, 4);

/* reg_paos_ase
 * Admin state update enabled.
 * Access: WO
 */
MLXSW_ITEM32(reg, paos, ase, 0x04, 31, 1);

/* reg_paos_ee
 * Event update enable. If this bit is set, event generation will be
 * updated based on the e field.
 * Access: WO
 */
MLXSW_ITEM32(reg, paos, ee, 0x04, 30, 1);

/* reg_paos_e
 * Event generation on operational state change:
 * 0 - Do not generate event.
 * 1 - Generate Event.
 * 2 - Generate Single Event.
 * Access: RW
 */
MLXSW_ITEM32(reg, paos, e, 0x04, 0, 2);

static inline void mlxsw_reg_paos_pack(char *payload, u8 local_port,
				       enum mlxsw_port_admin_status status)
{
	MLXSW_REG_ZERO(paos, payload);
	mlxsw_reg_paos_swid_set(payload, 0);
	mlxsw_reg_paos_local_port_set(payload, local_port);
	mlxsw_reg_paos_admin_status_set(payload, status);
	mlxsw_reg_paos_oper_status_set(payload, 0);
	mlxsw_reg_paos_ase_set(payload, 1);
	mlxsw_reg_paos_ee_set(payload, 1);
	mlxsw_reg_paos_e_set(payload, 1);
}

/* PFCC - Ports Flow Control Configuration Register
 * ------------------------------------------------
 * Configures and retrieves the per port flow control configuration.
 */
#define MLXSW_REG_PFCC_ID 0x5007
#define MLXSW_REG_PFCC_LEN 0x20

static const struct mlxsw_reg_info mlxsw_reg_pfcc = {
	.id = MLXSW_REG_PFCC_ID,
	.len = MLXSW_REG_PFCC_LEN,
};

/* reg_pfcc_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, pfcc, local_port, 0x00, 16, 8);

/* reg_pfcc_pnat
 * Port number access type. Determines the way local_port is interpreted:
 * 0 - Local port number.
 * 1 - IB / label port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, pfcc, pnat, 0x00, 14, 2);

/* reg_pfcc_shl_cap
 * Send to higher layers capabilities:
 * 0 - No capability of sending Pause and PFC frames to higher layers.
 * 1 - Device has capability of sending Pause and PFC frames to higher
 *     layers.
 * Access: RO
 */
MLXSW_ITEM32(reg, pfcc, shl_cap, 0x00, 1, 1);

/* reg_pfcc_shl_opr
 * Send to higher layers operation:
 * 0 - Pause and PFC frames are handled by the port (default).
 * 1 - Pause and PFC frames are handled by the port and also sent to
 *     higher layers. Only valid if shl_cap = 1.
 * Access: RW
 */
MLXSW_ITEM32(reg, pfcc, shl_opr, 0x00, 0, 1);

/* reg_pfcc_ppan
 * Pause policy auto negotiation.
 * 0 - Disabled. Generate / ignore Pause frames based on pptx / pprtx.
 * 1 - Enabled. When auto-negotiation is performed, set the Pause policy
 *     based on the auto-negotiation resolution.
 * Access: RW
 *
 * Note: The auto-negotiation advertisement is set according to pptx and
 * pprtx. When PFC is set on Tx / Rx, ppan must be set to 0.
 */
MLXSW_ITEM32(reg, pfcc, ppan, 0x04, 28, 4);

/* reg_pfcc_prio_mask_tx
 * Bit per priority indicating if Tx flow control policy should be
 * updated based on bit pfctx.
 * Access: WO
 */
MLXSW_ITEM32(reg, pfcc, prio_mask_tx, 0x04, 16, 8);

/* reg_pfcc_prio_mask_rx
 * Bit per priority indicating if Rx flow control policy should be
 * updated based on bit pfcrx.
 * Access: WO
 */
MLXSW_ITEM32(reg, pfcc, prio_mask_rx, 0x04, 0, 8);

/* reg_pfcc_pptx
 * Admin Pause policy on Tx.
 * 0 - Never generate Pause frames (default).
 * 1 - Generate Pause frames according to Rx buffer threshold.
 * Access: RW
 */
MLXSW_ITEM32(reg, pfcc, pptx, 0x08, 31, 1);

/* reg_pfcc_aptx
 * Active (operational) Pause policy on Tx.
 * 0 - Never generate Pause frames.
 * 1 - Generate Pause frames according to Rx buffer threshold.
 * Access: RO
 */
MLXSW_ITEM32(reg, pfcc, aptx, 0x08, 30, 1);

/* reg_pfcc_pfctx
 * Priority based flow control policy on Tx[7:0]. Per-priority bit mask:
 * 0 - Never generate priority Pause frames on the specified priority
 *     (default).
 * 1 - Generate priority Pause frames according to Rx buffer threshold on
 *     the specified priority.
 * Access: RW
 *
 * Note: pfctx and pptx must be mutually exclusive.
 */
MLXSW_ITEM32(reg, pfcc, pfctx, 0x08, 16, 8);

/* reg_pfcc_pprx
 * Admin Pause policy on Rx.
 * 0 - Ignore received Pause frames (default).
 * 1 - Respect received Pause frames.
 * Access: RW
 */
MLXSW_ITEM32(reg, pfcc, pprx, 0x0C, 31, 1);

/* reg_pfcc_aprx
 * Active (operational) Pause policy on Rx.
 * 0 - Ignore received Pause frames.
 * 1 - Respect received Pause frames.
 * Access: RO
 */
MLXSW_ITEM32(reg, pfcc, aprx, 0x0C, 30, 1);

/* reg_pfcc_pfcrx
 * Priority based flow control policy on Rx[7:0]. Per-priority bit mask:
 * 0 - Ignore incoming priority Pause frames on the specified priority
 *     (default).
 * 1 - Respect incoming priority Pause frames on the specified priority.
 * Access: RW
 */
MLXSW_ITEM32(reg, pfcc, pfcrx, 0x0C, 16, 8);

#define MLXSW_REG_PFCC_ALL_PRIO 0xFF

static inline void mlxsw_reg_pfcc_prio_pack(char *payload, u8 pfc_en)
{
	mlxsw_reg_pfcc_prio_mask_tx_set(payload, MLXSW_REG_PFCC_ALL_PRIO);
	mlxsw_reg_pfcc_prio_mask_rx_set(payload, MLXSW_REG_PFCC_ALL_PRIO);
	mlxsw_reg_pfcc_pfctx_set(payload, pfc_en);
	mlxsw_reg_pfcc_pfcrx_set(payload, pfc_en);
}

static inline void mlxsw_reg_pfcc_pack(char *payload, u8 local_port)
{
	MLXSW_REG_ZERO(pfcc, payload);
	mlxsw_reg_pfcc_local_port_set(payload, local_port);
}

/* PPCNT - Ports Performance Counters Register
 * -------------------------------------------
 * The PPCNT register retrieves per port performance counters.
 */
#define MLXSW_REG_PPCNT_ID 0x5008
#define MLXSW_REG_PPCNT_LEN 0x100

static const struct mlxsw_reg_info mlxsw_reg_ppcnt = {
	.id = MLXSW_REG_PPCNT_ID,
	.len = MLXSW_REG_PPCNT_LEN,
};

/* reg_ppcnt_swid
 * For HCA: must be always 0.
 * Switch partition ID to associate port with.
 * Switch partitions are numbered from 0 to 7 inclusively.
 * Switch partition 254 indicates stacking ports.
 * Switch partition 255 indicates all switch partitions.
 * Only valid on Set() operation with local_port=255.
 * Access: Index
 */
MLXSW_ITEM32(reg, ppcnt, swid, 0x00, 24, 8);

/* reg_ppcnt_local_port
 * Local port number.
 * 255 indicates all ports on the device, and is only allowed
 * for Set() operation.
 * Access: Index
 */
MLXSW_ITEM32(reg, ppcnt, local_port, 0x00, 16, 8);

/* reg_ppcnt_pnat
 * Port number access type:
 * 0 - Local port number
 * 1 - IB port number
 * Access: Index
 */
MLXSW_ITEM32(reg, ppcnt, pnat, 0x00, 14, 2);

enum mlxsw_reg_ppcnt_grp {
	MLXSW_REG_PPCNT_IEEE_8023_CNT = 0x0,
	MLXSW_REG_PPCNT_PRIO_CNT = 0x10,
};

/* reg_ppcnt_grp
 * Performance counter group.
 * Group 63 indicates all groups. Only valid on Set() operation with
 * clr bit set.
 * 0x0: IEEE 802.3 Counters
 * 0x1: RFC 2863 Counters
 * 0x2: RFC 2819 Counters
 * 0x3: RFC 3635 Counters
 * 0x5: Ethernet Extended Counters
 * 0x8: Link Level Retransmission Counters
 * 0x10: Per Priority Counters
 * 0x11: Per Traffic Class Counters
 * 0x12: Physical Layer Counters
 * Access: Index
 */
MLXSW_ITEM32(reg, ppcnt, grp, 0x00, 0, 6);

/* reg_ppcnt_clr
 * Clear counters. Setting the clr bit will reset the counter value
 * for all counters in the counter group. This bit can be set
 * for both Set() and Get() operation.
 * Access: OP
 */
MLXSW_ITEM32(reg, ppcnt, clr, 0x04, 31, 1);

/* reg_ppcnt_prio_tc
 * Priority for counter set that support per priority, valid values: 0-7.
 * Traffic class for counter set that support per traffic class,
 * valid values: 0- cap_max_tclass-1 .
 * For HCA: cap_max_tclass is always 8.
 * Otherwise must be 0.
 * Access: Index
 */
MLXSW_ITEM32(reg, ppcnt, prio_tc, 0x04, 0, 5);

/* Ethernet IEEE 802.3 Counter Group */

/* reg_ppcnt_a_frames_transmitted_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_frames_transmitted_ok,
	     0x08 + 0x00, 0, 64);

/* reg_ppcnt_a_frames_received_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_frames_received_ok,
	     0x08 + 0x08, 0, 64);

/* reg_ppcnt_a_frame_check_sequence_errors
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_frame_check_sequence_errors,
	     0x08 + 0x10, 0, 64);

/* reg_ppcnt_a_alignment_errors
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_alignment_errors,
	     0x08 + 0x18, 0, 64);

/* reg_ppcnt_a_octets_transmitted_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_octets_transmitted_ok,
	     0x08 + 0x20, 0, 64);

/* reg_ppcnt_a_octets_received_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_octets_received_ok,
	     0x08 + 0x28, 0, 64);

/* reg_ppcnt_a_multicast_frames_xmitted_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_multicast_frames_xmitted_ok,
	     0x08 + 0x30, 0, 64);

/* reg_ppcnt_a_broadcast_frames_xmitted_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_broadcast_frames_xmitted_ok,
	     0x08 + 0x38, 0, 64);

/* reg_ppcnt_a_multicast_frames_received_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_multicast_frames_received_ok,
	     0x08 + 0x40, 0, 64);

/* reg_ppcnt_a_broadcast_frames_received_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_broadcast_frames_received_ok,
	     0x08 + 0x48, 0, 64);

/* reg_ppcnt_a_in_range_length_errors
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_in_range_length_errors,
	     0x08 + 0x50, 0, 64);

/* reg_ppcnt_a_out_of_range_length_field
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_out_of_range_length_field,
	     0x08 + 0x58, 0, 64);

/* reg_ppcnt_a_frame_too_long_errors
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_frame_too_long_errors,
	     0x08 + 0x60, 0, 64);

/* reg_ppcnt_a_symbol_error_during_carrier
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_symbol_error_during_carrier,
	     0x08 + 0x68, 0, 64);

/* reg_ppcnt_a_mac_control_frames_transmitted
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_mac_control_frames_transmitted,
	     0x08 + 0x70, 0, 64);

/* reg_ppcnt_a_mac_control_frames_received
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_mac_control_frames_received,
	     0x08 + 0x78, 0, 64);

/* reg_ppcnt_a_unsupported_opcodes_received
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_unsupported_opcodes_received,
	     0x08 + 0x80, 0, 64);

/* reg_ppcnt_a_pause_mac_ctrl_frames_received
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_pause_mac_ctrl_frames_received,
	     0x08 + 0x88, 0, 64);

/* reg_ppcnt_a_pause_mac_ctrl_frames_transmitted
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_pause_mac_ctrl_frames_transmitted,
	     0x08 + 0x90, 0, 64);

/* Ethernet Per Priority Group Counters */

/* reg_ppcnt_rx_octets
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, rx_octets, 0x08 + 0x00, 0, 64);

/* reg_ppcnt_rx_frames
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, rx_frames, 0x08 + 0x20, 0, 64);

/* reg_ppcnt_tx_octets
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, tx_octets, 0x08 + 0x28, 0, 64);

/* reg_ppcnt_tx_frames
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, tx_frames, 0x08 + 0x48, 0, 64);

/* reg_ppcnt_rx_pause
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, rx_pause, 0x08 + 0x50, 0, 64);

/* reg_ppcnt_rx_pause_duration
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, rx_pause_duration, 0x08 + 0x58, 0, 64);

/* reg_ppcnt_tx_pause
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, tx_pause, 0x08 + 0x60, 0, 64);

/* reg_ppcnt_tx_pause_duration
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, tx_pause_duration, 0x08 + 0x68, 0, 64);

/* reg_ppcnt_rx_pause_transition
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, tx_pause_transition, 0x08 + 0x70, 0, 64);

static inline void mlxsw_reg_ppcnt_pack(char *payload, u8 local_port,
					enum mlxsw_reg_ppcnt_grp grp,
					u8 prio_tc)
{
	MLXSW_REG_ZERO(ppcnt, payload);
	mlxsw_reg_ppcnt_swid_set(payload, 0);
	mlxsw_reg_ppcnt_local_port_set(payload, local_port);
	mlxsw_reg_ppcnt_pnat_set(payload, 0);
	mlxsw_reg_ppcnt_grp_set(payload, grp);
	mlxsw_reg_ppcnt_clr_set(payload, 0);
	mlxsw_reg_ppcnt_prio_tc_set(payload, prio_tc);
}

/* PPTB - Port Prio To Buffer Register
 * -----------------------------------
 * Configures the switch priority to buffer table.
 */
#define MLXSW_REG_PPTB_ID 0x500B
#define MLXSW_REG_PPTB_LEN 0x0C

static const struct mlxsw_reg_info mlxsw_reg_pptb = {
	.id = MLXSW_REG_PPTB_ID,
	.len = MLXSW_REG_PPTB_LEN,
};

enum {
	MLXSW_REG_PPTB_MM_UM,
	MLXSW_REG_PPTB_MM_UNICAST,
	MLXSW_REG_PPTB_MM_MULTICAST,
};

/* reg_pptb_mm
 * Mapping mode.
 * 0 - Map both unicast and multicast packets to the same buffer.
 * 1 - Map only unicast packets.
 * 2 - Map only multicast packets.
 * Access: Index
 *
 * Note: SwitchX-2 only supports the first option.
 */
MLXSW_ITEM32(reg, pptb, mm, 0x00, 28, 2);

/* reg_pptb_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, pptb, local_port, 0x00, 16, 8);

/* reg_pptb_um
 * Enables the update of the untagged_buf field.
 * Access: RW
 */
MLXSW_ITEM32(reg, pptb, um, 0x00, 8, 1);

/* reg_pptb_pm
 * Enables the update of the prio_to_buff field.
 * Bit <i> is a flag for updating the mapping for switch priority <i>.
 * Access: RW
 */
MLXSW_ITEM32(reg, pptb, pm, 0x00, 0, 8);

/* reg_pptb_prio_to_buff
 * Mapping of switch priority <i> to one of the allocated receive port
 * buffers.
 * Access: RW
 */
MLXSW_ITEM_BIT_ARRAY(reg, pptb, prio_to_buff, 0x04, 0x04, 4);

/* reg_pptb_pm_msb
 * Enables the update of the prio_to_buff field.
 * Bit <i> is a flag for updating the mapping for switch priority <i+8>.
 * Access: RW
 */
MLXSW_ITEM32(reg, pptb, pm_msb, 0x08, 24, 8);

/* reg_pptb_untagged_buff
 * Mapping of untagged frames to one of the allocated receive port buffers.
 * Access: RW
 *
 * Note: In SwitchX-2 this field must be mapped to buffer 8. Reserved for
 * Spectrum, as it maps untagged packets based on the default switch priority.
 */
MLXSW_ITEM32(reg, pptb, untagged_buff, 0x08, 0, 4);

#define MLXSW_REG_PPTB_ALL_PRIO 0xFF

static inline void mlxsw_reg_pptb_pack(char *payload, u8 local_port)
{
	MLXSW_REG_ZERO(pptb, payload);
	mlxsw_reg_pptb_mm_set(payload, MLXSW_REG_PPTB_MM_UM);
	mlxsw_reg_pptb_local_port_set(payload, local_port);
	mlxsw_reg_pptb_pm_set(payload, MLXSW_REG_PPTB_ALL_PRIO);
}

/* PBMC - Port Buffer Management Control Register
 * ----------------------------------------------
 * The PBMC register configures and retrieves the port packet buffer
 * allocation for different Prios, and the Pause threshold management.
 */
#define MLXSW_REG_PBMC_ID 0x500C
#define MLXSW_REG_PBMC_LEN 0x6C

static const struct mlxsw_reg_info mlxsw_reg_pbmc = {
	.id = MLXSW_REG_PBMC_ID,
	.len = MLXSW_REG_PBMC_LEN,
};

/* reg_pbmc_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, pbmc, local_port, 0x00, 16, 8);

/* reg_pbmc_xoff_timer_value
 * When device generates a pause frame, it uses this value as the pause
 * timer (time for the peer port to pause in quota-512 bit time).
 * Access: RW
 */
MLXSW_ITEM32(reg, pbmc, xoff_timer_value, 0x04, 16, 16);

/* reg_pbmc_xoff_refresh
 * The time before a new pause frame should be sent to refresh the pause RW
 * state. Using the same units as xoff_timer_value above (in quota-512 bit
 * time).
 * Access: RW
 */
MLXSW_ITEM32(reg, pbmc, xoff_refresh, 0x04, 0, 16);

#define MLXSW_REG_PBMC_PORT_SHARED_BUF_IDX 11

/* reg_pbmc_buf_lossy
 * The field indicates if the buffer is lossy.
 * 0 - Lossless
 * 1 - Lossy
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, pbmc, buf_lossy, 0x0C, 25, 1, 0x08, 0x00, false);

/* reg_pbmc_buf_epsb
 * Eligible for Port Shared buffer.
 * If epsb is set, packets assigned to buffer are allowed to insert the port
 * shared buffer.
 * When buf_lossy is MLXSW_REG_PBMC_LOSSY_LOSSY this field is reserved.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, pbmc, buf_epsb, 0x0C, 24, 1, 0x08, 0x00, false);

/* reg_pbmc_buf_size
 * The part of the packet buffer array is allocated for the specific buffer.
 * Units are represented in cells.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, pbmc, buf_size, 0x0C, 0, 16, 0x08, 0x00, false);

/* reg_pbmc_buf_xoff_threshold
 * Once the amount of data in the buffer goes above this value, device
 * starts sending PFC frames for all priorities associated with the
 * buffer. Units are represented in cells. Reserved in case of lossy
 * buffer.
 * Access: RW
 *
 * Note: In Spectrum, reserved for buffer[9].
 */
MLXSW_ITEM32_INDEXED(reg, pbmc, buf_xoff_threshold, 0x0C, 16, 16,
		     0x08, 0x04, false);

/* reg_pbmc_buf_xon_threshold
 * When the amount of data in the buffer goes below this value, device
 * stops sending PFC frames for the priorities associated with the
 * buffer. Units are represented in cells. Reserved in case of lossy
 * buffer.
 * Access: RW
 *
 * Note: In Spectrum, reserved for buffer[9].
 */
MLXSW_ITEM32_INDEXED(reg, pbmc, buf_xon_threshold, 0x0C, 0, 16,
		     0x08, 0x04, false);

static inline void mlxsw_reg_pbmc_pack(char *payload, u8 local_port,
				       u16 xoff_timer_value, u16 xoff_refresh)
{
	MLXSW_REG_ZERO(pbmc, payload);
	mlxsw_reg_pbmc_local_port_set(payload, local_port);
	mlxsw_reg_pbmc_xoff_timer_value_set(payload, xoff_timer_value);
	mlxsw_reg_pbmc_xoff_refresh_set(payload, xoff_refresh);
}

static inline void mlxsw_reg_pbmc_lossy_buffer_pack(char *payload,
						    int buf_index,
						    u16 size)
{
	mlxsw_reg_pbmc_buf_lossy_set(payload, buf_index, 1);
	mlxsw_reg_pbmc_buf_epsb_set(payload, buf_index, 0);
	mlxsw_reg_pbmc_buf_size_set(payload, buf_index, size);
}

static inline void mlxsw_reg_pbmc_lossless_buffer_pack(char *payload,
						       int buf_index, u16 size,
						       u16 threshold)
{
	mlxsw_reg_pbmc_buf_lossy_set(payload, buf_index, 0);
	mlxsw_reg_pbmc_buf_epsb_set(payload, buf_index, 0);
	mlxsw_reg_pbmc_buf_size_set(payload, buf_index, size);
	mlxsw_reg_pbmc_buf_xoff_threshold_set(payload, buf_index, threshold);
	mlxsw_reg_pbmc_buf_xon_threshold_set(payload, buf_index, threshold);
}

/* PSPA - Port Switch Partition Allocation
 * ---------------------------------------
 * Controls the association of a port with a switch partition and enables
 * configuring ports as stacking ports.
 */
#define MLXSW_REG_PSPA_ID 0x500D
#define MLXSW_REG_PSPA_LEN 0x8

static const struct mlxsw_reg_info mlxsw_reg_pspa = {
	.id = MLXSW_REG_PSPA_ID,
	.len = MLXSW_REG_PSPA_LEN,
};

/* reg_pspa_swid
 * Switch partition ID.
 * Access: RW
 */
MLXSW_ITEM32(reg, pspa, swid, 0x00, 24, 8);

/* reg_pspa_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, pspa, local_port, 0x00, 16, 8);

/* reg_pspa_sub_port
 * Virtual port within the local port. Set to 0 when virtual ports are
 * disabled on the local port.
 * Access: Index
 */
MLXSW_ITEM32(reg, pspa, sub_port, 0x00, 8, 8);

static inline void mlxsw_reg_pspa_pack(char *payload, u8 swid, u8 local_port)
{
	MLXSW_REG_ZERO(pspa, payload);
	mlxsw_reg_pspa_swid_set(payload, swid);
	mlxsw_reg_pspa_local_port_set(payload, local_port);
	mlxsw_reg_pspa_sub_port_set(payload, 0);
}

/* HTGT - Host Trap Group Table
 * ----------------------------
 * Configures the properties for forwarding to CPU.
 */
#define MLXSW_REG_HTGT_ID 0x7002
#define MLXSW_REG_HTGT_LEN 0x100

static const struct mlxsw_reg_info mlxsw_reg_htgt = {
	.id = MLXSW_REG_HTGT_ID,
	.len = MLXSW_REG_HTGT_LEN,
};

/* reg_htgt_swid
 * Switch partition ID.
 * Access: Index
 */
MLXSW_ITEM32(reg, htgt, swid, 0x00, 24, 8);

#define MLXSW_REG_HTGT_PATH_TYPE_LOCAL 0x0	/* For locally attached CPU */

/* reg_htgt_type
 * CPU path type.
 * Access: RW
 */
MLXSW_ITEM32(reg, htgt, type, 0x00, 8, 4);

enum mlxsw_reg_htgt_trap_group {
	MLXSW_REG_HTGT_TRAP_GROUP_EMAD,
	MLXSW_REG_HTGT_TRAP_GROUP_RX,
	MLXSW_REG_HTGT_TRAP_GROUP_CTRL,
};

/* reg_htgt_trap_group
 * Trap group number. User defined number specifying which trap groups
 * should be forwarded to the CPU. The mapping between trap IDs and trap
 * groups is configured using HPKT register.
 * Access: Index
 */
MLXSW_ITEM32(reg, htgt, trap_group, 0x00, 0, 8);

enum {
	MLXSW_REG_HTGT_POLICER_DISABLE,
	MLXSW_REG_HTGT_POLICER_ENABLE,
};

/* reg_htgt_pide
 * Enable policer ID specified using 'pid' field.
 * Access: RW
 */
MLXSW_ITEM32(reg, htgt, pide, 0x04, 15, 1);

/* reg_htgt_pid
 * Policer ID for the trap group.
 * Access: RW
 */
MLXSW_ITEM32(reg, htgt, pid, 0x04, 0, 8);

#define MLXSW_REG_HTGT_TRAP_TO_CPU 0x0

/* reg_htgt_mirror_action
 * Mirror action to use.
 * 0 - Trap to CPU.
 * 1 - Trap to CPU and mirror to a mirroring agent.
 * 2 - Mirror to a mirroring agent and do not trap to CPU.
 * Access: RW
 *
 * Note: Mirroring to a mirroring agent is only supported in Spectrum.
 */
MLXSW_ITEM32(reg, htgt, mirror_action, 0x08, 8, 2);

/* reg_htgt_mirroring_agent
 * Mirroring agent.
 * Access: RW
 */
MLXSW_ITEM32(reg, htgt, mirroring_agent, 0x08, 0, 3);

/* reg_htgt_priority
 * Trap group priority.
 * In case a packet matches multiple classification rules, the packet will
 * only be trapped once, based on the trap ID associated with the group (via
 * register HPKT) with the highest priority.
 * Supported values are 0-7, with 7 represnting the highest priority.
 * Access: RW
 *
 * Note: In SwitchX-2 this field is ignored and the priority value is replaced
 * by the 'trap_group' field.
 */
MLXSW_ITEM32(reg, htgt, priority, 0x0C, 0, 4);

/* reg_htgt_local_path_cpu_tclass
 * CPU ingress traffic class for the trap group.
 * Access: RW
 */
MLXSW_ITEM32(reg, htgt, local_path_cpu_tclass, 0x10, 16, 6);

#define MLXSW_REG_HTGT_LOCAL_PATH_RDQ_EMAD	0x15
#define MLXSW_REG_HTGT_LOCAL_PATH_RDQ_RX	0x14
#define MLXSW_REG_HTGT_LOCAL_PATH_RDQ_CTRL	0x13

/* reg_htgt_local_path_rdq
 * Receive descriptor queue (RDQ) to use for the trap group.
 * Access: RW
 */
MLXSW_ITEM32(reg, htgt, local_path_rdq, 0x10, 0, 6);

static inline void mlxsw_reg_htgt_pack(char *payload,
				       enum mlxsw_reg_htgt_trap_group group)
{
	u8 swid, rdq;

	MLXSW_REG_ZERO(htgt, payload);
	switch (group) {
	case MLXSW_REG_HTGT_TRAP_GROUP_EMAD:
		swid = MLXSW_PORT_SWID_ALL_SWIDS;
		rdq = MLXSW_REG_HTGT_LOCAL_PATH_RDQ_EMAD;
		break;
	case MLXSW_REG_HTGT_TRAP_GROUP_RX:
		swid = 0;
		rdq = MLXSW_REG_HTGT_LOCAL_PATH_RDQ_RX;
		break;
	case MLXSW_REG_HTGT_TRAP_GROUP_CTRL:
		swid = 0;
		rdq = MLXSW_REG_HTGT_LOCAL_PATH_RDQ_CTRL;
		break;
	}
	mlxsw_reg_htgt_swid_set(payload, swid);
	mlxsw_reg_htgt_type_set(payload, MLXSW_REG_HTGT_PATH_TYPE_LOCAL);
	mlxsw_reg_htgt_trap_group_set(payload, group);
	mlxsw_reg_htgt_pide_set(payload, MLXSW_REG_HTGT_POLICER_DISABLE);
	mlxsw_reg_htgt_pid_set(payload, 0);
	mlxsw_reg_htgt_mirror_action_set(payload, MLXSW_REG_HTGT_TRAP_TO_CPU);
	mlxsw_reg_htgt_mirroring_agent_set(payload, 0);
	mlxsw_reg_htgt_priority_set(payload, 0);
	mlxsw_reg_htgt_local_path_cpu_tclass_set(payload, 7);
	mlxsw_reg_htgt_local_path_rdq_set(payload, rdq);
}

/* HPKT - Host Packet Trap
 * -----------------------
 * Configures trap IDs inside trap groups.
 */
#define MLXSW_REG_HPKT_ID 0x7003
#define MLXSW_REG_HPKT_LEN 0x10

static const struct mlxsw_reg_info mlxsw_reg_hpkt = {
	.id = MLXSW_REG_HPKT_ID,
	.len = MLXSW_REG_HPKT_LEN,
};

enum {
	MLXSW_REG_HPKT_ACK_NOT_REQUIRED,
	MLXSW_REG_HPKT_ACK_REQUIRED,
};

/* reg_hpkt_ack
 * Require acknowledgements from the host for events.
 * If set, then the device will wait for the event it sent to be acknowledged
 * by the host. This option is only relevant for event trap IDs.
 * Access: RW
 *
 * Note: Currently not supported by firmware.
 */
MLXSW_ITEM32(reg, hpkt, ack, 0x00, 24, 1);

enum mlxsw_reg_hpkt_action {
	MLXSW_REG_HPKT_ACTION_FORWARD,
	MLXSW_REG_HPKT_ACTION_TRAP_TO_CPU,
	MLXSW_REG_HPKT_ACTION_MIRROR_TO_CPU,
	MLXSW_REG_HPKT_ACTION_DISCARD,
	MLXSW_REG_HPKT_ACTION_SOFT_DISCARD,
	MLXSW_REG_HPKT_ACTION_TRAP_AND_SOFT_DISCARD,
};

/* reg_hpkt_action
 * Action to perform on packet when trapped.
 * 0 - No action. Forward to CPU based on switching rules.
 * 1 - Trap to CPU (CPU receives sole copy).
 * 2 - Mirror to CPU (CPU receives a replica of the packet).
 * 3 - Discard.
 * 4 - Soft discard (allow other traps to act on the packet).
 * 5 - Trap and soft discard (allow other traps to overwrite this trap).
 * Access: RW
 *
 * Note: Must be set to 0 (forward) for event trap IDs, as they are already
 * addressed to the CPU.
 */
MLXSW_ITEM32(reg, hpkt, action, 0x00, 20, 3);

/* reg_hpkt_trap_group
 * Trap group to associate the trap with.
 * Access: RW
 */
MLXSW_ITEM32(reg, hpkt, trap_group, 0x00, 12, 6);

/* reg_hpkt_trap_id
 * Trap ID.
 * Access: Index
 *
 * Note: A trap ID can only be associated with a single trap group. The device
 * will associate the trap ID with the last trap group configured.
 */
MLXSW_ITEM32(reg, hpkt, trap_id, 0x00, 0, 9);

enum {
	MLXSW_REG_HPKT_CTRL_PACKET_DEFAULT,
	MLXSW_REG_HPKT_CTRL_PACKET_NO_BUFFER,
	MLXSW_REG_HPKT_CTRL_PACKET_USE_BUFFER,
};

/* reg_hpkt_ctrl
 * Configure dedicated buffer resources for control packets.
 * 0 - Keep factory defaults.
 * 1 - Do not use control buffer for this trap ID.
 * 2 - Use control buffer for this trap ID.
 * Access: RW
 */
MLXSW_ITEM32(reg, hpkt, ctrl, 0x04, 16, 2);

static inline void mlxsw_reg_hpkt_pack(char *payload, u8 action, u16 trap_id)
{
	enum mlxsw_reg_htgt_trap_group trap_group;

	MLXSW_REG_ZERO(hpkt, payload);
	mlxsw_reg_hpkt_ack_set(payload, MLXSW_REG_HPKT_ACK_NOT_REQUIRED);
	mlxsw_reg_hpkt_action_set(payload, action);
	switch (trap_id) {
	case MLXSW_TRAP_ID_ETHEMAD:
	case MLXSW_TRAP_ID_PUDE:
		trap_group = MLXSW_REG_HTGT_TRAP_GROUP_EMAD;
		break;
	default:
		trap_group = MLXSW_REG_HTGT_TRAP_GROUP_RX;
		break;
	}
	mlxsw_reg_hpkt_trap_group_set(payload, trap_group);
	mlxsw_reg_hpkt_trap_id_set(payload, trap_id);
	mlxsw_reg_hpkt_ctrl_set(payload, MLXSW_REG_HPKT_CTRL_PACKET_DEFAULT);
}

/* RGCR - Router General Configuration Register
 * --------------------------------------------
 * The register is used for setting up the router configuration.
 */
#define MLXSW_REG_RGCR_ID 0x8001
#define MLXSW_REG_RGCR_LEN 0x28

static const struct mlxsw_reg_info mlxsw_reg_rgcr = {
	.id = MLXSW_REG_RGCR_ID,
	.len = MLXSW_REG_RGCR_LEN,
};

/* reg_rgcr_ipv4_en
 * IPv4 router enable.
 * Access: RW
 */
MLXSW_ITEM32(reg, rgcr, ipv4_en, 0x00, 31, 1);

/* reg_rgcr_ipv6_en
 * IPv6 router enable.
 * Access: RW
 */
MLXSW_ITEM32(reg, rgcr, ipv6_en, 0x00, 30, 1);

/* reg_rgcr_max_router_interfaces
 * Defines the maximum number of active router interfaces for all virtual
 * routers.
 * Access: RW
 */
MLXSW_ITEM32(reg, rgcr, max_router_interfaces, 0x10, 0, 16);

/* reg_rgcr_usp
 * Update switch priority and packet color.
 * 0 - Preserve the value of Switch Priority and packet color.
 * 1 - Recalculate the value of Switch Priority and packet color.
 * Access: RW
 *
 * Note: Not supported by SwitchX and SwitchX-2.
 */
MLXSW_ITEM32(reg, rgcr, usp, 0x18, 20, 1);

/* reg_rgcr_pcp_rw
 * Indicates how to handle the pcp_rewrite_en value:
 * 0 - Preserve the value of pcp_rewrite_en.
 * 2 - Disable PCP rewrite.
 * 3 - Enable PCP rewrite.
 * Access: RW
 *
 * Note: Not supported by SwitchX and SwitchX-2.
 */
MLXSW_ITEM32(reg, rgcr, pcp_rw, 0x18, 16, 2);

/* reg_rgcr_activity_dis
 * Activity disable:
 * 0 - Activity will be set when an entry is hit (default).
 * 1 - Activity will not be set when an entry is hit.
 *
 * Bit 0 - Disable activity bit in Router Algorithmic LPM Unicast Entry
 * (RALUE).
 * Bit 1 - Disable activity bit in Router Algorithmic LPM Unicast Host
 * Entry (RAUHT).
 * Bits 2:7 are reserved.
 * Access: RW
 *
 * Note: Not supported by SwitchX, SwitchX-2 and Switch-IB.
 */
MLXSW_ITEM32(reg, rgcr, activity_dis, 0x20, 0, 8);

static inline void mlxsw_reg_rgcr_pack(char *payload, bool ipv4_en)
{
	MLXSW_REG_ZERO(rgcr, payload);
	mlxsw_reg_rgcr_ipv4_en_set(payload, ipv4_en);
}

/* RITR - Router Interface Table Register
 * --------------------------------------
 * The register is used to configure the router interface table.
 */
#define MLXSW_REG_RITR_ID 0x8002
#define MLXSW_REG_RITR_LEN 0x40

static const struct mlxsw_reg_info mlxsw_reg_ritr = {
	.id = MLXSW_REG_RITR_ID,
	.len = MLXSW_REG_RITR_LEN,
};

/* reg_ritr_enable
 * Enables routing on the router interface.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, enable, 0x00, 31, 1);

/* reg_ritr_ipv4
 * IPv4 routing enable. Enables routing of IPv4 traffic on the router
 * interface.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, ipv4, 0x00, 29, 1);

/* reg_ritr_ipv6
 * IPv6 routing enable. Enables routing of IPv6 traffic on the router
 * interface.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, ipv6, 0x00, 28, 1);

enum mlxsw_reg_ritr_if_type {
	MLXSW_REG_RITR_VLAN_IF,
	MLXSW_REG_RITR_FID_IF,
	MLXSW_REG_RITR_SP_IF,
};

/* reg_ritr_type
 * Router interface type.
 * 0 - VLAN interface.
 * 1 - FID interface.
 * 2 - Sub-port interface.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, type, 0x00, 23, 3);

enum {
	MLXSW_REG_RITR_RIF_CREATE,
	MLXSW_REG_RITR_RIF_DEL,
};

/* reg_ritr_op
 * Opcode:
 * 0 - Create or edit RIF.
 * 1 - Delete RIF.
 * Reserved for SwitchX-2. For Spectrum, editing of interface properties
 * is not supported. An interface must be deleted and re-created in order
 * to update properties.
 * Access: WO
 */
MLXSW_ITEM32(reg, ritr, op, 0x00, 20, 2);

/* reg_ritr_rif
 * Router interface index. A pointer to the Router Interface Table.
 * Access: Index
 */
MLXSW_ITEM32(reg, ritr, rif, 0x00, 0, 16);

/* reg_ritr_ipv4_fe
 * IPv4 Forwarding Enable.
 * Enables routing of IPv4 traffic on the router interface. When disabled,
 * forwarding is blocked but local traffic (traps and IP2ME) will be enabled.
 * Not supported in SwitchX-2.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, ipv4_fe, 0x04, 29, 1);

/* reg_ritr_ipv6_fe
 * IPv6 Forwarding Enable.
 * Enables routing of IPv6 traffic on the router interface. When disabled,
 * forwarding is blocked but local traffic (traps and IP2ME) will be enabled.
 * Not supported in SwitchX-2.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, ipv6_fe, 0x04, 28, 1);

/* reg_ritr_virtual_router
 * Virtual router ID associated with the router interface.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, virtual_router, 0x04, 0, 16);

/* reg_ritr_mtu
 * Router interface MTU.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, mtu, 0x34, 0, 16);

/* reg_ritr_if_swid
 * Switch partition ID.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, if_swid, 0x08, 24, 8);

/* reg_ritr_if_mac
 * Router interface MAC address.
 * In Spectrum, all MAC addresses must have the same 38 MSBits.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, ritr, if_mac, 0x12, 6);

/* VLAN Interface */

/* reg_ritr_vlan_if_vid
 * VLAN ID.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, vlan_if_vid, 0x08, 0, 12);

/* FID Interface */

/* reg_ritr_fid_if_fid
 * Filtering ID. Used to connect a bridge to the router. Only FIDs from
 * the vFID range are supported.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, fid_if_fid, 0x08, 0, 16);

static inline void mlxsw_reg_ritr_fid_set(char *payload,
					  enum mlxsw_reg_ritr_if_type rif_type,
					  u16 fid)
{
	if (rif_type == MLXSW_REG_RITR_FID_IF)
		mlxsw_reg_ritr_fid_if_fid_set(payload, fid);
	else
		mlxsw_reg_ritr_vlan_if_vid_set(payload, fid);
}

/* Sub-port Interface */

/* reg_ritr_sp_if_lag
 * LAG indication. When this bit is set the system_port field holds the
 * LAG identifier.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, sp_if_lag, 0x08, 24, 1);

/* reg_ritr_sp_system_port
 * Port unique indentifier. When lag bit is set, this field holds the
 * lag_id in bits 0:9.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, sp_if_system_port, 0x08, 0, 16);

/* reg_ritr_sp_if_vid
 * VLAN ID.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, sp_if_vid, 0x18, 0, 12);

static inline void mlxsw_reg_ritr_rif_pack(char *payload, u16 rif)
{
	MLXSW_REG_ZERO(ritr, payload);
	mlxsw_reg_ritr_rif_set(payload, rif);
}

static inline void mlxsw_reg_ritr_sp_if_pack(char *payload, bool lag,
					     u16 system_port, u16 vid)
{
	mlxsw_reg_ritr_sp_if_lag_set(payload, lag);
	mlxsw_reg_ritr_sp_if_system_port_set(payload, system_port);
	mlxsw_reg_ritr_sp_if_vid_set(payload, vid);
}

static inline void mlxsw_reg_ritr_pack(char *payload, bool enable,
				       enum mlxsw_reg_ritr_if_type type,
				       u16 rif, u16 mtu, const char *mac)
{
	bool op = enable ? MLXSW_REG_RITR_RIF_CREATE : MLXSW_REG_RITR_RIF_DEL;

	MLXSW_REG_ZERO(ritr, payload);
	mlxsw_reg_ritr_enable_set(payload, enable);
	mlxsw_reg_ritr_ipv4_set(payload, 1);
	mlxsw_reg_ritr_type_set(payload, type);
	mlxsw_reg_ritr_op_set(payload, op);
	mlxsw_reg_ritr_rif_set(payload, rif);
	mlxsw_reg_ritr_ipv4_fe_set(payload, 1);
	mlxsw_reg_ritr_mtu_set(payload, mtu);
	mlxsw_reg_ritr_if_mac_memcpy_to(payload, mac);
}

/* RALTA - Router Algorithmic LPM Tree Allocation Register
 * -------------------------------------------------------
 * RALTA is used to allocate the LPM trees of the SHSPM method.
 */
#define MLXSW_REG_RALTA_ID 0x8010
#define MLXSW_REG_RALTA_LEN 0x04

static const struct mlxsw_reg_info mlxsw_reg_ralta = {
	.id = MLXSW_REG_RALTA_ID,
	.len = MLXSW_REG_RALTA_LEN,
};

/* reg_ralta_op
 * opcode (valid for Write, must be 0 on Read)
 * 0 - allocate a tree
 * 1 - deallocate a tree
 * Access: OP
 */
MLXSW_ITEM32(reg, ralta, op, 0x00, 28, 2);

enum mlxsw_reg_ralxx_protocol {
	MLXSW_REG_RALXX_PROTOCOL_IPV4,
	MLXSW_REG_RALXX_PROTOCOL_IPV6,
};

/* reg_ralta_protocol
 * Protocol.
 * Deallocation opcode: Reserved.
 * Access: RW
 */
MLXSW_ITEM32(reg, ralta, protocol, 0x00, 24, 4);

/* reg_ralta_tree_id
 * An identifier (numbered from 1..cap_shspm_max_trees-1) representing
 * the tree identifier (managed by software).
 * Note that tree_id 0 is allocated for a default-route tree.
 * Access: Index
 */
MLXSW_ITEM32(reg, ralta, tree_id, 0x00, 0, 8);

static inline void mlxsw_reg_ralta_pack(char *payload, bool alloc,
					enum mlxsw_reg_ralxx_protocol protocol,
					u8 tree_id)
{
	MLXSW_REG_ZERO(ralta, payload);
	mlxsw_reg_ralta_op_set(payload, !alloc);
	mlxsw_reg_ralta_protocol_set(payload, protocol);
	mlxsw_reg_ralta_tree_id_set(payload, tree_id);
}

/* RALST - Router Algorithmic LPM Structure Tree Register
 * ------------------------------------------------------
 * RALST is used to set and query the structure of an LPM tree.
 * The structure of the tree must be sorted as a sorted binary tree, while
 * each node is a bin that is tagged as the length of the prefixes the lookup
 * will refer to. Therefore, bin X refers to a set of entries with prefixes
 * of X bits to match with the destination address. The bin 0 indicates
 * the default action, when there is no match of any prefix.
 */
#define MLXSW_REG_RALST_ID 0x8011
#define MLXSW_REG_RALST_LEN 0x104

static const struct mlxsw_reg_info mlxsw_reg_ralst = {
	.id = MLXSW_REG_RALST_ID,
	.len = MLXSW_REG_RALST_LEN,
};

/* reg_ralst_root_bin
 * The bin number of the root bin.
 * 0<root_bin=<(length of IP address)
 * For a default-route tree configure 0xff
 * Access: RW
 */
MLXSW_ITEM32(reg, ralst, root_bin, 0x00, 16, 8);

/* reg_ralst_tree_id
 * Tree identifier numbered from 1..(cap_shspm_max_trees-1).
 * Access: Index
 */
MLXSW_ITEM32(reg, ralst, tree_id, 0x00, 0, 8);

#define MLXSW_REG_RALST_BIN_NO_CHILD 0xff
#define MLXSW_REG_RALST_BIN_OFFSET 0x04
#define MLXSW_REG_RALST_BIN_COUNT 128

/* reg_ralst_left_child_bin
 * Holding the children of the bin according to the stored tree's structure.
 * For trees composed of less than 4 blocks, the bins in excess are reserved.
 * Note that tree_id 0 is allocated for a default-route tree, bins are 0xff
 * Access: RW
 */
MLXSW_ITEM16_INDEXED(reg, ralst, left_child_bin, 0x04, 8, 8, 0x02, 0x00, false);

/* reg_ralst_right_child_bin
 * Holding the children of the bin according to the stored tree's structure.
 * For trees composed of less than 4 blocks, the bins in excess are reserved.
 * Note that tree_id 0 is allocated for a default-route tree, bins are 0xff
 * Access: RW
 */
MLXSW_ITEM16_INDEXED(reg, ralst, right_child_bin, 0x04, 0, 8, 0x02, 0x00,
		     false);

static inline void mlxsw_reg_ralst_pack(char *payload, u8 root_bin, u8 tree_id)
{
	MLXSW_REG_ZERO(ralst, payload);

	/* Initialize all bins to have no left or right child */
	memset(payload + MLXSW_REG_RALST_BIN_OFFSET,
	       MLXSW_REG_RALST_BIN_NO_CHILD, MLXSW_REG_RALST_BIN_COUNT * 2);

	mlxsw_reg_ralst_root_bin_set(payload, root_bin);
	mlxsw_reg_ralst_tree_id_set(payload, tree_id);
}

static inline void mlxsw_reg_ralst_bin_pack(char *payload, u8 bin_number,
					    u8 left_child_bin,
					    u8 right_child_bin)
{
	int bin_index = bin_number - 1;

	mlxsw_reg_ralst_left_child_bin_set(payload, bin_index, left_child_bin);
	mlxsw_reg_ralst_right_child_bin_set(payload, bin_index,
					    right_child_bin);
}

/* RALTB - Router Algorithmic LPM Tree Binding Register
 * ----------------------------------------------------
 * RALTB is used to bind virtual router and protocol to an allocated LPM tree.
 */
#define MLXSW_REG_RALTB_ID 0x8012
#define MLXSW_REG_RALTB_LEN 0x04

static const struct mlxsw_reg_info mlxsw_reg_raltb = {
	.id = MLXSW_REG_RALTB_ID,
	.len = MLXSW_REG_RALTB_LEN,
};

/* reg_raltb_virtual_router
 * Virtual Router ID
 * Range is 0..cap_max_virtual_routers-1
 * Access: Index
 */
MLXSW_ITEM32(reg, raltb, virtual_router, 0x00, 16, 16);

/* reg_raltb_protocol
 * Protocol.
 * Access: Index
 */
MLXSW_ITEM32(reg, raltb, protocol, 0x00, 12, 4);

/* reg_raltb_tree_id
 * Tree to be used for the {virtual_router, protocol}
 * Tree identifier numbered from 1..(cap_shspm_max_trees-1).
 * By default, all Unicast IPv4 and IPv6 are bound to tree_id 0.
 * Access: RW
 */
MLXSW_ITEM32(reg, raltb, tree_id, 0x00, 0, 8);

static inline void mlxsw_reg_raltb_pack(char *payload, u16 virtual_router,
					enum mlxsw_reg_ralxx_protocol protocol,
					u8 tree_id)
{
	MLXSW_REG_ZERO(raltb, payload);
	mlxsw_reg_raltb_virtual_router_set(payload, virtual_router);
	mlxsw_reg_raltb_protocol_set(payload, protocol);
	mlxsw_reg_raltb_tree_id_set(payload, tree_id);
}

/* MFCR - Management Fan Control Register
 * --------------------------------------
 * This register controls the settings of the Fan Speed PWM mechanism.
 */
#define MLXSW_REG_MFCR_ID 0x9001
#define MLXSW_REG_MFCR_LEN 0x08

static const struct mlxsw_reg_info mlxsw_reg_mfcr = {
	.id = MLXSW_REG_MFCR_ID,
	.len = MLXSW_REG_MFCR_LEN,
};

enum mlxsw_reg_mfcr_pwm_frequency {
	MLXSW_REG_MFCR_PWM_FEQ_11HZ = 0x00,
	MLXSW_REG_MFCR_PWM_FEQ_14_7HZ = 0x01,
	MLXSW_REG_MFCR_PWM_FEQ_22_1HZ = 0x02,
	MLXSW_REG_MFCR_PWM_FEQ_1_4KHZ = 0x40,
	MLXSW_REG_MFCR_PWM_FEQ_5KHZ = 0x41,
	MLXSW_REG_MFCR_PWM_FEQ_20KHZ = 0x42,
	MLXSW_REG_MFCR_PWM_FEQ_22_5KHZ = 0x43,
	MLXSW_REG_MFCR_PWM_FEQ_25KHZ = 0x44,
};

/* reg_mfcr_pwm_frequency
 * Controls the frequency of the PWM signal.
 * Access: RW
 */
MLXSW_ITEM32(reg, mfcr, pwm_frequency, 0x00, 0, 6);

#define MLXSW_MFCR_TACHOS_MAX 10

/* reg_mfcr_tacho_active
 * Indicates which of the tachometer is active (bit per tachometer).
 * Access: RO
 */
MLXSW_ITEM32(reg, mfcr, tacho_active, 0x04, 16, MLXSW_MFCR_TACHOS_MAX);

#define MLXSW_MFCR_PWMS_MAX 5

/* reg_mfcr_pwm_active
 * Indicates which of the PWM control is active (bit per PWM).
 * Access: RO
 */
MLXSW_ITEM32(reg, mfcr, pwm_active, 0x04, 0, MLXSW_MFCR_PWMS_MAX);

static inline void
mlxsw_reg_mfcr_pack(char *payload,
		    enum mlxsw_reg_mfcr_pwm_frequency pwm_frequency)
{
	MLXSW_REG_ZERO(mfcr, payload);
	mlxsw_reg_mfcr_pwm_frequency_set(payload, pwm_frequency);
}

static inline void
mlxsw_reg_mfcr_unpack(char *payload,
		      enum mlxsw_reg_mfcr_pwm_frequency *p_pwm_frequency,
		      u16 *p_tacho_active, u8 *p_pwm_active)
{
	*p_pwm_frequency = mlxsw_reg_mfcr_pwm_frequency_get(payload);
	*p_tacho_active = mlxsw_reg_mfcr_tacho_active_get(payload);
	*p_pwm_active = mlxsw_reg_mfcr_pwm_active_get(payload);
}

/* MFSC - Management Fan Speed Control Register
 * --------------------------------------------
 * This register controls the settings of the Fan Speed PWM mechanism.
 */
#define MLXSW_REG_MFSC_ID 0x9002
#define MLXSW_REG_MFSC_LEN 0x08

static const struct mlxsw_reg_info mlxsw_reg_mfsc = {
	.id = MLXSW_REG_MFSC_ID,
	.len = MLXSW_REG_MFSC_LEN,
};

/* reg_mfsc_pwm
 * Fan pwm to control / monitor.
 * Access: Index
 */
MLXSW_ITEM32(reg, mfsc, pwm, 0x00, 24, 3);

/* reg_mfsc_pwm_duty_cycle
 * Controls the duty cycle of the PWM. Value range from 0..255 to
 * represent duty cycle of 0%...100%.
 * Access: RW
 */
MLXSW_ITEM32(reg, mfsc, pwm_duty_cycle, 0x04, 0, 8);

static inline void mlxsw_reg_mfsc_pack(char *payload, u8 pwm,
				       u8 pwm_duty_cycle)
{
	MLXSW_REG_ZERO(mfsc, payload);
	mlxsw_reg_mfsc_pwm_set(payload, pwm);
	mlxsw_reg_mfsc_pwm_duty_cycle_set(payload, pwm_duty_cycle);
}

/* MFSM - Management Fan Speed Measurement
 * ---------------------------------------
 * This register controls the settings of the Tacho measurements and
 * enables reading the Tachometer measurements.
 */
#define MLXSW_REG_MFSM_ID 0x9003
#define MLXSW_REG_MFSM_LEN 0x08

static const struct mlxsw_reg_info mlxsw_reg_mfsm = {
	.id = MLXSW_REG_MFSM_ID,
	.len = MLXSW_REG_MFSM_LEN,
};

/* reg_mfsm_tacho
 * Fan tachometer index.
 * Access: Index
 */
MLXSW_ITEM32(reg, mfsm, tacho, 0x00, 24, 4);

/* reg_mfsm_rpm
 * Fan speed (round per minute).
 * Access: RO
 */
MLXSW_ITEM32(reg, mfsm, rpm, 0x04, 0, 16);

static inline void mlxsw_reg_mfsm_pack(char *payload, u8 tacho)
{
	MLXSW_REG_ZERO(mfsm, payload);
	mlxsw_reg_mfsm_tacho_set(payload, tacho);
}

/* MTCAP - Management Temperature Capabilities
 * -------------------------------------------
 * This register exposes the capabilities of the device and
 * system temperature sensing.
 */
#define MLXSW_REG_MTCAP_ID 0x9009
#define MLXSW_REG_MTCAP_LEN 0x08

static const struct mlxsw_reg_info mlxsw_reg_mtcap = {
	.id = MLXSW_REG_MTCAP_ID,
	.len = MLXSW_REG_MTCAP_LEN,
};

/* reg_mtcap_sensor_count
 * Number of sensors supported by the device.
 * This includes the QSFP module sensors (if exists in the QSFP module).
 * Access: RO
 */
MLXSW_ITEM32(reg, mtcap, sensor_count, 0x00, 0, 7);

/* MTMP - Management Temperature
 * -----------------------------
 * This register controls the settings of the temperature measurements
 * and enables reading the temperature measurements. Note that temperature
 * is in 0.125 degrees Celsius.
 */
#define MLXSW_REG_MTMP_ID 0x900A
#define MLXSW_REG_MTMP_LEN 0x20

static const struct mlxsw_reg_info mlxsw_reg_mtmp = {
	.id = MLXSW_REG_MTMP_ID,
	.len = MLXSW_REG_MTMP_LEN,
};

/* reg_mtmp_sensor_index
 * Sensors index to access.
 * 64-127 of sensor_index are mapped to the SFP+/QSFP modules sequentially
 * (module 0 is mapped to sensor_index 64).
 * Access: Index
 */
MLXSW_ITEM32(reg, mtmp, sensor_index, 0x00, 0, 7);

/* Convert to milli degrees Celsius */
#define MLXSW_REG_MTMP_TEMP_TO_MC(val) (val * 125)

/* reg_mtmp_temperature
 * Temperature reading from the sensor. Reading is in 0.125 Celsius
 * degrees units.
 * Access: RO
 */
MLXSW_ITEM32(reg, mtmp, temperature, 0x04, 0, 16);

/* reg_mtmp_mte
 * Max Temperature Enable - enables measuring the max temperature on a sensor.
 * Access: RW
 */
MLXSW_ITEM32(reg, mtmp, mte, 0x08, 31, 1);

/* reg_mtmp_mtr
 * Max Temperature Reset - clears the value of the max temperature register.
 * Access: WO
 */
MLXSW_ITEM32(reg, mtmp, mtr, 0x08, 30, 1);

/* reg_mtmp_max_temperature
 * The highest measured temperature from the sensor.
 * When the bit mte is cleared, the field max_temperature is reserved.
 * Access: RO
 */
MLXSW_ITEM32(reg, mtmp, max_temperature, 0x08, 0, 16);

#define MLXSW_REG_MTMP_SENSOR_NAME_SIZE 8

/* reg_mtmp_sensor_name
 * Sensor Name
 * Access: RO
 */
MLXSW_ITEM_BUF(reg, mtmp, sensor_name, 0x18, MLXSW_REG_MTMP_SENSOR_NAME_SIZE);

static inline void mlxsw_reg_mtmp_pack(char *payload, u8 sensor_index,
				       bool max_temp_enable,
				       bool max_temp_reset)
{
	MLXSW_REG_ZERO(mtmp, payload);
	mlxsw_reg_mtmp_sensor_index_set(payload, sensor_index);
	mlxsw_reg_mtmp_mte_set(payload, max_temp_enable);
	mlxsw_reg_mtmp_mtr_set(payload, max_temp_reset);
}

static inline void mlxsw_reg_mtmp_unpack(char *payload, unsigned int *p_temp,
					 unsigned int *p_max_temp,
					 char *sensor_name)
{
	u16 temp;

	if (p_temp) {
		temp = mlxsw_reg_mtmp_temperature_get(payload);
		*p_temp = MLXSW_REG_MTMP_TEMP_TO_MC(temp);
	}
	if (p_max_temp) {
		temp = mlxsw_reg_mtmp_max_temperature_get(payload);
		*p_max_temp = MLXSW_REG_MTMP_TEMP_TO_MC(temp);
	}
	if (sensor_name)
		mlxsw_reg_mtmp_sensor_name_memcpy_from(payload, sensor_name);
}

/* MLCR - Management LED Control Register
 * --------------------------------------
 * Controls the system LEDs.
 */
#define MLXSW_REG_MLCR_ID 0x902B
#define MLXSW_REG_MLCR_LEN 0x0C

static const struct mlxsw_reg_info mlxsw_reg_mlcr = {
	.id = MLXSW_REG_MLCR_ID,
	.len = MLXSW_REG_MLCR_LEN,
};

/* reg_mlcr_local_port
 * Local port number.
 * Access: RW
 */
MLXSW_ITEM32(reg, mlcr, local_port, 0x00, 16, 8);

#define MLXSW_REG_MLCR_DURATION_MAX 0xFFFF

/* reg_mlcr_beacon_duration
 * Duration of the beacon to be active, in seconds.
 * 0x0 - Will turn off the beacon.
 * 0xFFFF - Will turn on the beacon until explicitly turned off.
 * Access: RW
 */
MLXSW_ITEM32(reg, mlcr, beacon_duration, 0x04, 0, 16);

/* reg_mlcr_beacon_remain
 * Remaining duration of the beacon, in seconds.
 * 0xFFFF indicates an infinite amount of time.
 * Access: RO
 */
MLXSW_ITEM32(reg, mlcr, beacon_remain, 0x08, 0, 16);

static inline void mlxsw_reg_mlcr_pack(char *payload, u8 local_port,
				       bool active)
{
	MLXSW_REG_ZERO(mlcr, payload);
	mlxsw_reg_mlcr_local_port_set(payload, local_port);
	mlxsw_reg_mlcr_beacon_duration_set(payload, active ?
					   MLXSW_REG_MLCR_DURATION_MAX : 0);
}

/* SBPR - Shared Buffer Pools Register
 * -----------------------------------
 * The SBPR configures and retrieves the shared buffer pools and configuration.
 */
#define MLXSW_REG_SBPR_ID 0xB001
#define MLXSW_REG_SBPR_LEN 0x14

static const struct mlxsw_reg_info mlxsw_reg_sbpr = {
	.id = MLXSW_REG_SBPR_ID,
	.len = MLXSW_REG_SBPR_LEN,
};

/* shared direstion enum for SBPR, SBCM, SBPM */
enum mlxsw_reg_sbxx_dir {
	MLXSW_REG_SBXX_DIR_INGRESS,
	MLXSW_REG_SBXX_DIR_EGRESS,
};

/* reg_sbpr_dir
 * Direction.
 * Access: Index
 */
MLXSW_ITEM32(reg, sbpr, dir, 0x00, 24, 2);

/* reg_sbpr_pool
 * Pool index.
 * Access: Index
 */
MLXSW_ITEM32(reg, sbpr, pool, 0x00, 0, 4);

/* reg_sbpr_size
 * Pool size in buffer cells.
 * Access: RW
 */
MLXSW_ITEM32(reg, sbpr, size, 0x04, 0, 24);

enum mlxsw_reg_sbpr_mode {
	MLXSW_REG_SBPR_MODE_STATIC,
	MLXSW_REG_SBPR_MODE_DYNAMIC,
};

/* reg_sbpr_mode
 * Pool quota calculation mode.
 * Access: RW
 */
MLXSW_ITEM32(reg, sbpr, mode, 0x08, 0, 4);

static inline void mlxsw_reg_sbpr_pack(char *payload, u8 pool,
				       enum mlxsw_reg_sbxx_dir dir,
				       enum mlxsw_reg_sbpr_mode mode, u32 size)
{
	MLXSW_REG_ZERO(sbpr, payload);
	mlxsw_reg_sbpr_pool_set(payload, pool);
	mlxsw_reg_sbpr_dir_set(payload, dir);
	mlxsw_reg_sbpr_mode_set(payload, mode);
	mlxsw_reg_sbpr_size_set(payload, size);
}

/* SBCM - Shared Buffer Class Management Register
 * ----------------------------------------------
 * The SBCM register configures and retrieves the shared buffer allocation
 * and configuration according to Port-PG, including the binding to pool
 * and definition of the associated quota.
 */
#define MLXSW_REG_SBCM_ID 0xB002
#define MLXSW_REG_SBCM_LEN 0x28

static const struct mlxsw_reg_info mlxsw_reg_sbcm = {
	.id = MLXSW_REG_SBCM_ID,
	.len = MLXSW_REG_SBCM_LEN,
};

/* reg_sbcm_local_port
 * Local port number.
 * For Ingress: excludes CPU port and Router port
 * For Egress: excludes IP Router
 * Access: Index
 */
MLXSW_ITEM32(reg, sbcm, local_port, 0x00, 16, 8);

/* reg_sbcm_pg_buff
 * PG buffer - Port PG (dir=ingress) / traffic class (dir=egress)
 * For PG buffer: range is 0..cap_max_pg_buffers - 1
 * For traffic class: range is 0..cap_max_tclass - 1
 * Note that when traffic class is in MC aware mode then the traffic
 * classes which are MC aware cannot be configured.
 * Access: Index
 */
MLXSW_ITEM32(reg, sbcm, pg_buff, 0x00, 8, 6);

/* reg_sbcm_dir
 * Direction.
 * Access: Index
 */
MLXSW_ITEM32(reg, sbcm, dir, 0x00, 0, 2);

/* reg_sbcm_min_buff
 * Minimum buffer size for the limiter, in cells.
 * Access: RW
 */
MLXSW_ITEM32(reg, sbcm, min_buff, 0x18, 0, 24);

/* shared max_buff limits for dynamic threshold for SBCM, SBPM */
#define MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN 1
#define MLXSW_REG_SBXX_DYN_MAX_BUFF_MAX 14

/* reg_sbcm_max_buff
 * When the pool associated to the port-pg/tclass is configured to
 * static, Maximum buffer size for the limiter configured in cells.
 * When the pool associated to the port-pg/tclass is configured to
 * dynamic, the max_buff holds the "alpha" parameter, supporting
 * the following values:
 * 0: 0
 * i: (1/128)*2^(i-1), for i=1..14
 * 0xFF: Infinity
 * Access: RW
 */
MLXSW_ITEM32(reg, sbcm, max_buff, 0x1C, 0, 24);

/* reg_sbcm_pool
 * Association of the port-priority to a pool.
 * Access: RW
 */
MLXSW_ITEM32(reg, sbcm, pool, 0x24, 0, 4);

static inline void mlxsw_reg_sbcm_pack(char *payload, u8 local_port, u8 pg_buff,
				       enum mlxsw_reg_sbxx_dir dir,
				       u32 min_buff, u32 max_buff, u8 pool)
{
	MLXSW_REG_ZERO(sbcm, payload);
	mlxsw_reg_sbcm_local_port_set(payload, local_port);
	mlxsw_reg_sbcm_pg_buff_set(payload, pg_buff);
	mlxsw_reg_sbcm_dir_set(payload, dir);
	mlxsw_reg_sbcm_min_buff_set(payload, min_buff);
	mlxsw_reg_sbcm_max_buff_set(payload, max_buff);
	mlxsw_reg_sbcm_pool_set(payload, pool);
}

/* SBPM - Shared Buffer Port Management Register
 * ---------------------------------------------
 * The SBPM register configures and retrieves the shared buffer allocation
 * and configuration according to Port-Pool, including the definition
 * of the associated quota.
 */
#define MLXSW_REG_SBPM_ID 0xB003
#define MLXSW_REG_SBPM_LEN 0x28

static const struct mlxsw_reg_info mlxsw_reg_sbpm = {
	.id = MLXSW_REG_SBPM_ID,
	.len = MLXSW_REG_SBPM_LEN,
};

/* reg_sbpm_local_port
 * Local port number.
 * For Ingress: excludes CPU port and Router port
 * For Egress: excludes IP Router
 * Access: Index
 */
MLXSW_ITEM32(reg, sbpm, local_port, 0x00, 16, 8);

/* reg_sbpm_pool
 * The pool associated to quota counting on the local_port.
 * Access: Index
 */
MLXSW_ITEM32(reg, sbpm, pool, 0x00, 8, 4);

/* reg_sbpm_dir
 * Direction.
 * Access: Index
 */
MLXSW_ITEM32(reg, sbpm, dir, 0x00, 0, 2);

/* reg_sbpm_buff_occupancy
 * Current buffer occupancy in cells.
 * Access: RO
 */
MLXSW_ITEM32(reg, sbpm, buff_occupancy, 0x10, 0, 24);

/* reg_sbpm_clr
 * Clear Max Buffer Occupancy
 * When this bit is set, max_buff_occupancy field is cleared (and a
 * new max value is tracked from the time the clear was performed).
 * Access: OP
 */
MLXSW_ITEM32(reg, sbpm, clr, 0x14, 31, 1);

/* reg_sbpm_max_buff_occupancy
 * Maximum value of buffer occupancy in cells monitored. Cleared by
 * writing to the clr field.
 * Access: RO
 */
MLXSW_ITEM32(reg, sbpm, max_buff_occupancy, 0x14, 0, 24);

/* reg_sbpm_min_buff
 * Minimum buffer size for the limiter, in cells.
 * Access: RW
 */
MLXSW_ITEM32(reg, sbpm, min_buff, 0x18, 0, 24);

/* reg_sbpm_max_buff
 * When the pool associated to the port-pg/tclass is configured to
 * static, Maximum buffer size for the limiter configured in cells.
 * When the pool associated to the port-pg/tclass is configured to
 * dynamic, the max_buff holds the "alpha" parameter, supporting
 * the following values:
 * 0: 0
 * i: (1/128)*2^(i-1), for i=1..14
 * 0xFF: Infinity
 * Access: RW
 */
MLXSW_ITEM32(reg, sbpm, max_buff, 0x1C, 0, 24);

static inline void mlxsw_reg_sbpm_pack(char *payload, u8 local_port, u8 pool,
				       enum mlxsw_reg_sbxx_dir dir, bool clr,
				       u32 min_buff, u32 max_buff)
{
	MLXSW_REG_ZERO(sbpm, payload);
	mlxsw_reg_sbpm_local_port_set(payload, local_port);
	mlxsw_reg_sbpm_pool_set(payload, pool);
	mlxsw_reg_sbpm_dir_set(payload, dir);
	mlxsw_reg_sbpm_clr_set(payload, clr);
	mlxsw_reg_sbpm_min_buff_set(payload, min_buff);
	mlxsw_reg_sbpm_max_buff_set(payload, max_buff);
}

static inline void mlxsw_reg_sbpm_unpack(char *payload, u32 *p_buff_occupancy,
					 u32 *p_max_buff_occupancy)
{
	*p_buff_occupancy = mlxsw_reg_sbpm_buff_occupancy_get(payload);
	*p_max_buff_occupancy = mlxsw_reg_sbpm_max_buff_occupancy_get(payload);
}

/* SBMM - Shared Buffer Multicast Management Register
 * --------------------------------------------------
 * The SBMM register configures and retrieves the shared buffer allocation
 * and configuration for MC packets according to Switch-Priority, including
 * the binding to pool and definition of the associated quota.
 */
#define MLXSW_REG_SBMM_ID 0xB004
#define MLXSW_REG_SBMM_LEN 0x28

static const struct mlxsw_reg_info mlxsw_reg_sbmm = {
	.id = MLXSW_REG_SBMM_ID,
	.len = MLXSW_REG_SBMM_LEN,
};

/* reg_sbmm_prio
 * Switch Priority.
 * Access: Index
 */
MLXSW_ITEM32(reg, sbmm, prio, 0x00, 8, 4);

/* reg_sbmm_min_buff
 * Minimum buffer size for the limiter, in cells.
 * Access: RW
 */
MLXSW_ITEM32(reg, sbmm, min_buff, 0x18, 0, 24);

/* reg_sbmm_max_buff
 * When the pool associated to the port-pg/tclass is configured to
 * static, Maximum buffer size for the limiter configured in cells.
 * When the pool associated to the port-pg/tclass is configured to
 * dynamic, the max_buff holds the "alpha" parameter, supporting
 * the following values:
 * 0: 0
 * i: (1/128)*2^(i-1), for i=1..14
 * 0xFF: Infinity
 * Access: RW
 */
MLXSW_ITEM32(reg, sbmm, max_buff, 0x1C, 0, 24);

/* reg_sbmm_pool
 * Association of the port-priority to a pool.
 * Access: RW
 */
MLXSW_ITEM32(reg, sbmm, pool, 0x24, 0, 4);

static inline void mlxsw_reg_sbmm_pack(char *payload, u8 prio, u32 min_buff,
				       u32 max_buff, u8 pool)
{
	MLXSW_REG_ZERO(sbmm, payload);
	mlxsw_reg_sbmm_prio_set(payload, prio);
	mlxsw_reg_sbmm_min_buff_set(payload, min_buff);
	mlxsw_reg_sbmm_max_buff_set(payload, max_buff);
	mlxsw_reg_sbmm_pool_set(payload, pool);
}

/* SBSR - Shared Buffer Status Register
 * ------------------------------------
 * The SBSR register retrieves the shared buffer occupancy according to
 * Port-Pool. Note that this register enables reading a large amount of data.
 * It is the user's responsibility to limit the amount of data to ensure the
 * response can match the maximum transfer unit. In case the response exceeds
 * the maximum transport unit, it will be truncated with no special notice.
 */
#define MLXSW_REG_SBSR_ID 0xB005
#define MLXSW_REG_SBSR_BASE_LEN 0x5C /* base length, without records */
#define MLXSW_REG_SBSR_REC_LEN 0x8 /* record length */
#define MLXSW_REG_SBSR_REC_MAX_COUNT 120
#define MLXSW_REG_SBSR_LEN (MLXSW_REG_SBSR_BASE_LEN +	\
			    MLXSW_REG_SBSR_REC_LEN *	\
			    MLXSW_REG_SBSR_REC_MAX_COUNT)

static const struct mlxsw_reg_info mlxsw_reg_sbsr = {
	.id = MLXSW_REG_SBSR_ID,
	.len = MLXSW_REG_SBSR_LEN,
};

/* reg_sbsr_clr
 * Clear Max Buffer Occupancy. When this bit is set, the max_buff_occupancy
 * field is cleared (and a new max value is tracked from the time the clear
 * was performed).
 * Access: OP
 */
MLXSW_ITEM32(reg, sbsr, clr, 0x00, 31, 1);

/* reg_sbsr_ingress_port_mask
 * Bit vector for all ingress network ports.
 * Indicates which of the ports (for which the relevant bit is set)
 * are affected by the set operation. Configuration of any other port
 * does not change.
 * Access: Index
 */
MLXSW_ITEM_BIT_ARRAY(reg, sbsr, ingress_port_mask, 0x10, 0x20, 1);

/* reg_sbsr_pg_buff_mask
 * Bit vector for all switch priority groups.
 * Indicates which of the priorities (for which the relevant bit is set)
 * are affected by the set operation. Configuration of any other priority
 * does not change.
 * Range is 0..cap_max_pg_buffers - 1
 * Access: Index
 */
MLXSW_ITEM_BIT_ARRAY(reg, sbsr, pg_buff_mask, 0x30, 0x4, 1);

/* reg_sbsr_egress_port_mask
 * Bit vector for all egress network ports.
 * Indicates which of the ports (for which the relevant bit is set)
 * are affected by the set operation. Configuration of any other port
 * does not change.
 * Access: Index
 */
MLXSW_ITEM_BIT_ARRAY(reg, sbsr, egress_port_mask, 0x34, 0x20, 1);

/* reg_sbsr_tclass_mask
 * Bit vector for all traffic classes.
 * Indicates which of the traffic classes (for which the relevant bit is
 * set) are affected by the set operation. Configuration of any other
 * traffic class does not change.
 * Range is 0..cap_max_tclass - 1
 * Access: Index
 */
MLXSW_ITEM_BIT_ARRAY(reg, sbsr, tclass_mask, 0x54, 0x8, 1);

static inline void mlxsw_reg_sbsr_pack(char *payload, bool clr)
{
	MLXSW_REG_ZERO(sbsr, payload);
	mlxsw_reg_sbsr_clr_set(payload, clr);
}

/* reg_sbsr_rec_buff_occupancy
 * Current buffer occupancy in cells.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, sbsr, rec_buff_occupancy, MLXSW_REG_SBSR_BASE_LEN,
		     0, 24, MLXSW_REG_SBSR_REC_LEN, 0x00, false);

/* reg_sbsr_rec_max_buff_occupancy
 * Maximum value of buffer occupancy in cells monitored. Cleared by
 * writing to the clr field.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, sbsr, rec_max_buff_occupancy, MLXSW_REG_SBSR_BASE_LEN,
		     0, 24, MLXSW_REG_SBSR_REC_LEN, 0x04, false);

static inline void mlxsw_reg_sbsr_rec_unpack(char *payload, int rec_index,
					     u32 *p_buff_occupancy,
					     u32 *p_max_buff_occupancy)
{
	*p_buff_occupancy =
		mlxsw_reg_sbsr_rec_buff_occupancy_get(payload, rec_index);
	*p_max_buff_occupancy =
		mlxsw_reg_sbsr_rec_max_buff_occupancy_get(payload, rec_index);
}

static inline const char *mlxsw_reg_id_str(u16 reg_id)
{
	switch (reg_id) {
	case MLXSW_REG_SGCR_ID:
		return "SGCR";
	case MLXSW_REG_SPAD_ID:
		return "SPAD";
	case MLXSW_REG_SMID_ID:
		return "SMID";
	case MLXSW_REG_SSPR_ID:
		return "SSPR";
	case MLXSW_REG_SFDAT_ID:
		return "SFDAT";
	case MLXSW_REG_SFD_ID:
		return "SFD";
	case MLXSW_REG_SFN_ID:
		return "SFN";
	case MLXSW_REG_SPMS_ID:
		return "SPMS";
	case MLXSW_REG_SPVID_ID:
		return "SPVID";
	case MLXSW_REG_SPVM_ID:
		return "SPVM";
	case MLXSW_REG_SPAFT_ID:
		return "SPAFT";
	case MLXSW_REG_SFGC_ID:
		return "SFGC";
	case MLXSW_REG_SFTR_ID:
		return "SFTR";
	case MLXSW_REG_SFDF_ID:
		return "SFDF";
	case MLXSW_REG_SLDR_ID:
		return "SLDR";
	case MLXSW_REG_SLCR_ID:
		return "SLCR";
	case MLXSW_REG_SLCOR_ID:
		return "SLCOR";
	case MLXSW_REG_SPMLR_ID:
		return "SPMLR";
	case MLXSW_REG_SVFA_ID:
		return "SVFA";
	case MLXSW_REG_SVPE_ID:
		return "SVPE";
	case MLXSW_REG_SFMR_ID:
		return "SFMR";
	case MLXSW_REG_SPVMLR_ID:
		return "SPVMLR";
	case MLXSW_REG_QTCT_ID:
		return "QTCT";
	case MLXSW_REG_QEEC_ID:
		return "QEEC";
	case MLXSW_REG_PMLP_ID:
		return "PMLP";
	case MLXSW_REG_PMTU_ID:
		return "PMTU";
	case MLXSW_REG_PTYS_ID:
		return "PTYS";
	case MLXSW_REG_PPAD_ID:
		return "PPAD";
	case MLXSW_REG_PAOS_ID:
		return "PAOS";
	case MLXSW_REG_PFCC_ID:
		return "PFCC";
	case MLXSW_REG_PPCNT_ID:
		return "PPCNT";
	case MLXSW_REG_PPTB_ID:
		return "PPTB";
	case MLXSW_REG_PBMC_ID:
		return "PBMC";
	case MLXSW_REG_PSPA_ID:
		return "PSPA";
	case MLXSW_REG_HTGT_ID:
		return "HTGT";
	case MLXSW_REG_HPKT_ID:
		return "HPKT";
	case MLXSW_REG_RGCR_ID:
		return "RGCR";
	case MLXSW_REG_RITR_ID:
		return "RITR";
	case MLXSW_REG_RALTA_ID:
		return "RALTA";
	case MLXSW_REG_RALST_ID:
		return "RALST";
	case MLXSW_REG_RALTB_ID:
		return "RALTB";
	case MLXSW_REG_MFCR_ID:
		return "MFCR";
	case MLXSW_REG_MFSC_ID:
		return "MFSC";
	case MLXSW_REG_MFSM_ID:
		return "MFSM";
	case MLXSW_REG_MTCAP_ID:
		return "MTCAP";
	case MLXSW_REG_MTMP_ID:
		return "MTMP";
	case MLXSW_REG_MLCR_ID:
		return "MLCR";
	case MLXSW_REG_SBPR_ID:
		return "SBPR";
	case MLXSW_REG_SBCM_ID:
		return "SBCM";
	case MLXSW_REG_SBPM_ID:
		return "SBPM";
	case MLXSW_REG_SBMM_ID:
		return "SBMM";
	case MLXSW_REG_SBSR_ID:
		return "SBSR";
	default:
		return "*UNKNOWN*";
	}
}

/* PUDE - Port Up / Down Event
 * ---------------------------
 * Reports the operational state change of a port.
 */
#define MLXSW_REG_PUDE_LEN 0x10

/* reg_pude_swid
 * Switch partition ID with which to associate the port.
 * Access: Index
 */
MLXSW_ITEM32(reg, pude, swid, 0x00, 24, 8);

/* reg_pude_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, pude, local_port, 0x00, 16, 8);

/* reg_pude_admin_status
 * Port administrative state (the desired state).
 * 1 - Up.
 * 2 - Down.
 * 3 - Up once. This means that in case of link failure, the port won't go
 *     into polling mode, but will wait to be re-enabled by software.
 * 4 - Disabled by system. Can only be set by hardware.
 * Access: RO
 */
MLXSW_ITEM32(reg, pude, admin_status, 0x00, 8, 4);

/* reg_pude_oper_status
 * Port operatioanl state.
 * 1 - Up.
 * 2 - Down.
 * 3 - Down by port failure. This means that the device will not let the
 *     port up again until explicitly specified by software.
 * Access: RO
 */
MLXSW_ITEM32(reg, pude, oper_status, 0x00, 0, 4);

#endif
