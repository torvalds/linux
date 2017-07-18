/*
 * drivers/net/ethernet/mellanox/mlxsw/reg.h
 * Copyright (c) 2015-2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015-2016 Ido Schimmel <idosch@mellanox.com>
 * Copyright (c) 2015 Elad Raz <eladr@mellanox.com>
 * Copyright (c) 2015-2017 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2016 Yotam Gigi <yotamg@mellanox.com>
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
	const char *name;
};

#define MLXSW_REG_DEFINE(_name, _id, _len)				\
static const struct mlxsw_reg_info mlxsw_reg_##_name = {		\
	.id = _id,							\
	.len = _len,							\
	.name = #_name,							\
}

#define MLXSW_REG(type) (&mlxsw_reg_##type)
#define MLXSW_REG_LEN(type) MLXSW_REG(type)->len
#define MLXSW_REG_ZERO(type, payload) memset(payload, 0, MLXSW_REG(type)->len)

/* SGCR - Switch General Configuration Register
 * --------------------------------------------
 * This register is used for configuration of the switch capabilities.
 */
#define MLXSW_REG_SGCR_ID 0x2000
#define MLXSW_REG_SGCR_LEN 0x10

MLXSW_REG_DEFINE(sgcr, MLXSW_REG_SGCR_ID, MLXSW_REG_SGCR_LEN);

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

MLXSW_REG_DEFINE(spad, MLXSW_REG_SPAD_ID, MLXSW_REG_SPAD_LEN);

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

MLXSW_REG_DEFINE(smid, MLXSW_REG_SMID_ID, MLXSW_REG_SMID_LEN);

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

MLXSW_REG_DEFINE(sspr, MLXSW_REG_SSPR_ID, MLXSW_REG_SSPR_LEN);

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

MLXSW_REG_DEFINE(sfdat, MLXSW_REG_SFDAT_ID, MLXSW_REG_SFDAT_LEN);

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

MLXSW_REG_DEFINE(sfd, MLXSW_REG_SFD_ID, MLXSW_REG_SFD_LEN);

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

MLXSW_REG_DEFINE(sfn, MLXSW_REG_SFN_ID, MLXSW_REG_SFN_LEN);

/* reg_sfn_swid
 * Switch partition ID.
 * Access: Index
 */
MLXSW_ITEM32(reg, sfn, swid, 0x00, 24, 8);

/* reg_sfn_end
 * Forces the current session to end.
 * Access: OP
 */
MLXSW_ITEM32(reg, sfn, end, 0x04, 20, 1);

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
	mlxsw_reg_sfn_end_set(payload, 1);
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

MLXSW_REG_DEFINE(spms, MLXSW_REG_SPMS_ID, MLXSW_REG_SPMS_LEN);

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

MLXSW_REG_DEFINE(spvid, MLXSW_REG_SPVID_ID, MLXSW_REG_SPVID_LEN);

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
#define MLXSW_REG_SPVM_REC_MAX_COUNT 255
#define MLXSW_REG_SPVM_LEN (MLXSW_REG_SPVM_BASE_LEN +	\
		    MLXSW_REG_SPVM_REC_LEN * MLXSW_REG_SPVM_REC_MAX_COUNT)

MLXSW_REG_DEFINE(spvm, MLXSW_REG_SPVM_ID, MLXSW_REG_SPVM_LEN);

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

MLXSW_REG_DEFINE(spaft, MLXSW_REG_SPAFT_ID, MLXSW_REG_SPAFT_LEN);

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

MLXSW_REG_DEFINE(sfgc, MLXSW_REG_SFGC_ID, MLXSW_REG_SFGC_LEN);

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
	MLXSW_REG_SFGC_TABLE_TYPE_FID_OFFSET = 3,
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

MLXSW_REG_DEFINE(sftr, MLXSW_REG_SFTR_ID, MLXSW_REG_SFTR_LEN);

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

MLXSW_REG_DEFINE(sfdf, MLXSW_REG_SFDF_ID, MLXSW_REG_SFDF_LEN);

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

MLXSW_REG_DEFINE(sldr, MLXSW_REG_SLDR_ID, MLXSW_REG_SLDR_LEN);

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

MLXSW_REG_DEFINE(slcr, MLXSW_REG_SLCR_ID, MLXSW_REG_SLCR_LEN);

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
	mlxsw_reg_slcr_type_set(payload, MLXSW_REG_SLCR_TYPE_CRC);
	mlxsw_reg_slcr_lag_hash_set(payload, lag_hash);
}

/* SLCOR - Switch LAG Collector Register
 * -------------------------------------
 * The Switch LAG Collector register controls the Local Port membership
 * in a LAG and enablement of the collector.
 */
#define MLXSW_REG_SLCOR_ID 0x2016
#define MLXSW_REG_SLCOR_LEN 0x10

MLXSW_REG_DEFINE(slcor, MLXSW_REG_SLCOR_ID, MLXSW_REG_SLCOR_LEN);

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

MLXSW_REG_DEFINE(spmlr, MLXSW_REG_SPMLR_ID, MLXSW_REG_SPMLR_LEN);

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

MLXSW_REG_DEFINE(svfa, MLXSW_REG_SVFA_ID, MLXSW_REG_SVFA_LEN);

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

MLXSW_REG_DEFINE(svpe, MLXSW_REG_SVPE_ID, MLXSW_REG_SVPE_LEN);

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

MLXSW_REG_DEFINE(sfmr, MLXSW_REG_SFMR_ID, MLXSW_REG_SFMR_LEN);

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
#define MLXSW_REG_SPVMLR_REC_MAX_COUNT 255
#define MLXSW_REG_SPVMLR_LEN (MLXSW_REG_SPVMLR_BASE_LEN + \
			      MLXSW_REG_SPVMLR_REC_LEN * \
			      MLXSW_REG_SPVMLR_REC_MAX_COUNT)

MLXSW_REG_DEFINE(spvmlr, MLXSW_REG_SPVMLR_ID, MLXSW_REG_SPVMLR_LEN);

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

/* PPBT - Policy-Engine Port Binding Table
 * ---------------------------------------
 * This register is used for configuration of the Port Binding Table.
 */
#define MLXSW_REG_PPBT_ID 0x3002
#define MLXSW_REG_PPBT_LEN 0x14

MLXSW_REG_DEFINE(ppbt, MLXSW_REG_PPBT_ID, MLXSW_REG_PPBT_LEN);

enum mlxsw_reg_pxbt_e {
	MLXSW_REG_PXBT_E_IACL,
	MLXSW_REG_PXBT_E_EACL,
};

/* reg_ppbt_e
 * Access: Index
 */
MLXSW_ITEM32(reg, ppbt, e, 0x00, 31, 1);

enum mlxsw_reg_pxbt_op {
	MLXSW_REG_PXBT_OP_BIND,
	MLXSW_REG_PXBT_OP_UNBIND,
};

/* reg_ppbt_op
 * Access: RW
 */
MLXSW_ITEM32(reg, ppbt, op, 0x00, 28, 3);

/* reg_ppbt_local_port
 * Local port. Not including CPU port.
 * Access: Index
 */
MLXSW_ITEM32(reg, ppbt, local_port, 0x00, 16, 8);

/* reg_ppbt_g
 * group - When set, the binding is of an ACL group. When cleared,
 * the binding is of an ACL.
 * Must be set to 1 for Spectrum.
 * Access: RW
 */
MLXSW_ITEM32(reg, ppbt, g, 0x10, 31, 1);

/* reg_ppbt_acl_info
 * ACL/ACL group identifier. If the g bit is set, this field should hold
 * the acl_group_id, else it should hold the acl_id.
 * Access: RW
 */
MLXSW_ITEM32(reg, ppbt, acl_info, 0x10, 0, 16);

static inline void mlxsw_reg_ppbt_pack(char *payload, enum mlxsw_reg_pxbt_e e,
				       enum mlxsw_reg_pxbt_op op,
				       u8 local_port, u16 acl_info)
{
	MLXSW_REG_ZERO(ppbt, payload);
	mlxsw_reg_ppbt_e_set(payload, e);
	mlxsw_reg_ppbt_op_set(payload, op);
	mlxsw_reg_ppbt_local_port_set(payload, local_port);
	mlxsw_reg_ppbt_g_set(payload, true);
	mlxsw_reg_ppbt_acl_info_set(payload, acl_info);
}

/* PACL - Policy-Engine ACL Register
 * ---------------------------------
 * This register is used for configuration of the ACL.
 */
#define MLXSW_REG_PACL_ID 0x3004
#define MLXSW_REG_PACL_LEN 0x70

MLXSW_REG_DEFINE(pacl, MLXSW_REG_PACL_ID, MLXSW_REG_PACL_LEN);

/* reg_pacl_v
 * Valid. Setting the v bit makes the ACL valid. It should not be cleared
 * while the ACL is bounded to either a port, VLAN or ACL rule.
 * Access: RW
 */
MLXSW_ITEM32(reg, pacl, v, 0x00, 24, 1);

/* reg_pacl_acl_id
 * An identifier representing the ACL (managed by software)
 * Range 0 .. cap_max_acl_regions - 1
 * Access: Index
 */
MLXSW_ITEM32(reg, pacl, acl_id, 0x08, 0, 16);

#define MLXSW_REG_PXXX_TCAM_REGION_INFO_LEN 16

/* reg_pacl_tcam_region_info
 * Opaque object that represents a TCAM region.
 * Obtained through PTAR register.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, pacl, tcam_region_info, 0x30,
	       MLXSW_REG_PXXX_TCAM_REGION_INFO_LEN);

static inline void mlxsw_reg_pacl_pack(char *payload, u16 acl_id,
				       bool valid, const char *tcam_region_info)
{
	MLXSW_REG_ZERO(pacl, payload);
	mlxsw_reg_pacl_acl_id_set(payload, acl_id);
	mlxsw_reg_pacl_v_set(payload, valid);
	mlxsw_reg_pacl_tcam_region_info_memcpy_to(payload, tcam_region_info);
}

/* PAGT - Policy-Engine ACL Group Table
 * ------------------------------------
 * This register is used for configuration of the ACL Group Table.
 */
#define MLXSW_REG_PAGT_ID 0x3005
#define MLXSW_REG_PAGT_BASE_LEN 0x30
#define MLXSW_REG_PAGT_ACL_LEN 4
#define MLXSW_REG_PAGT_ACL_MAX_NUM 16
#define MLXSW_REG_PAGT_LEN (MLXSW_REG_PAGT_BASE_LEN + \
		MLXSW_REG_PAGT_ACL_MAX_NUM * MLXSW_REG_PAGT_ACL_LEN)

MLXSW_REG_DEFINE(pagt, MLXSW_REG_PAGT_ID, MLXSW_REG_PAGT_LEN);

/* reg_pagt_size
 * Number of ACLs in the group.
 * Size 0 invalidates a group.
 * Range 0 .. cap_max_acl_group_size (hard coded to 16 for now)
 * Total number of ACLs in all groups must be lower or equal
 * to cap_max_acl_tot_groups
 * Note: a group which is binded must not be invalidated
 * Access: Index
 */
MLXSW_ITEM32(reg, pagt, size, 0x00, 0, 8);

/* reg_pagt_acl_group_id
 * An identifier (numbered from 0..cap_max_acl_groups-1) representing
 * the ACL Group identifier (managed by software).
 * Access: Index
 */
MLXSW_ITEM32(reg, pagt, acl_group_id, 0x08, 0, 16);

/* reg_pagt_acl_id
 * ACL identifier
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, pagt, acl_id, 0x30, 0, 16, 0x04, 0x00, false);

static inline void mlxsw_reg_pagt_pack(char *payload, u16 acl_group_id)
{
	MLXSW_REG_ZERO(pagt, payload);
	mlxsw_reg_pagt_acl_group_id_set(payload, acl_group_id);
}

static inline void mlxsw_reg_pagt_acl_id_pack(char *payload, int index,
					      u16 acl_id)
{
	u8 size = mlxsw_reg_pagt_size_get(payload);

	if (index >= size)
		mlxsw_reg_pagt_size_set(payload, index + 1);
	mlxsw_reg_pagt_acl_id_set(payload, index, acl_id);
}

/* PTAR - Policy-Engine TCAM Allocation Register
 * ---------------------------------------------
 * This register is used for allocation of regions in the TCAM.
 * Note: Query method is not supported on this register.
 */
#define MLXSW_REG_PTAR_ID 0x3006
#define MLXSW_REG_PTAR_BASE_LEN 0x20
#define MLXSW_REG_PTAR_KEY_ID_LEN 1
#define MLXSW_REG_PTAR_KEY_ID_MAX_NUM 16
#define MLXSW_REG_PTAR_LEN (MLXSW_REG_PTAR_BASE_LEN + \
		MLXSW_REG_PTAR_KEY_ID_MAX_NUM * MLXSW_REG_PTAR_KEY_ID_LEN)

MLXSW_REG_DEFINE(ptar, MLXSW_REG_PTAR_ID, MLXSW_REG_PTAR_LEN);

enum mlxsw_reg_ptar_op {
	/* allocate a TCAM region */
	MLXSW_REG_PTAR_OP_ALLOC,
	/* resize a TCAM region */
	MLXSW_REG_PTAR_OP_RESIZE,
	/* deallocate TCAM region */
	MLXSW_REG_PTAR_OP_FREE,
	/* test allocation */
	MLXSW_REG_PTAR_OP_TEST,
};

/* reg_ptar_op
 * Access: OP
 */
MLXSW_ITEM32(reg, ptar, op, 0x00, 28, 4);

/* reg_ptar_action_set_type
 * Type of action set to be used on this region.
 * For Spectrum, this is always type 2 - "flexible"
 * Access: WO
 */
MLXSW_ITEM32(reg, ptar, action_set_type, 0x00, 16, 8);

/* reg_ptar_key_type
 * TCAM key type for the region.
 * For Spectrum, this is always type 0x50 - "FLEX_KEY"
 * Access: WO
 */
MLXSW_ITEM32(reg, ptar, key_type, 0x00, 0, 8);

/* reg_ptar_region_size
 * TCAM region size. When allocating/resizing this is the requested size,
 * the response is the actual size. Note that actual size may be
 * larger than requested.
 * Allowed range 1 .. cap_max_rules-1
 * Reserved during op deallocate.
 * Access: WO
 */
MLXSW_ITEM32(reg, ptar, region_size, 0x04, 0, 16);

/* reg_ptar_region_id
 * Region identifier
 * Range 0 .. cap_max_regions-1
 * Access: Index
 */
MLXSW_ITEM32(reg, ptar, region_id, 0x08, 0, 16);

/* reg_ptar_tcam_region_info
 * Opaque object that represents the TCAM region.
 * Returned when allocating a region.
 * Provided by software for ACL generation and region deallocation and resize.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, ptar, tcam_region_info, 0x10,
	       MLXSW_REG_PXXX_TCAM_REGION_INFO_LEN);

/* reg_ptar_flexible_key_id
 * Identifier of the Flexible Key.
 * Only valid if key_type == "FLEX_KEY"
 * The key size will be rounded up to one of the following values:
 * 9B, 18B, 36B, 54B.
 * This field is reserved for in resize operation.
 * Access: WO
 */
MLXSW_ITEM8_INDEXED(reg, ptar, flexible_key_id, 0x20, 0, 8,
		    MLXSW_REG_PTAR_KEY_ID_LEN, 0x00, false);

static inline void mlxsw_reg_ptar_pack(char *payload, enum mlxsw_reg_ptar_op op,
				       u16 region_size, u16 region_id,
				       const char *tcam_region_info)
{
	MLXSW_REG_ZERO(ptar, payload);
	mlxsw_reg_ptar_op_set(payload, op);
	mlxsw_reg_ptar_action_set_type_set(payload, 2); /* "flexible" */
	mlxsw_reg_ptar_key_type_set(payload, 0x50); /* "FLEX_KEY" */
	mlxsw_reg_ptar_region_size_set(payload, region_size);
	mlxsw_reg_ptar_region_id_set(payload, region_id);
	mlxsw_reg_ptar_tcam_region_info_memcpy_to(payload, tcam_region_info);
}

static inline void mlxsw_reg_ptar_key_id_pack(char *payload, int index,
					      u16 key_id)
{
	mlxsw_reg_ptar_flexible_key_id_set(payload, index, key_id);
}

static inline void mlxsw_reg_ptar_unpack(char *payload, char *tcam_region_info)
{
	mlxsw_reg_ptar_tcam_region_info_memcpy_from(payload, tcam_region_info);
}

/* PPBS - Policy-Engine Policy Based Switching Register
 * ----------------------------------------------------
 * This register retrieves and sets Policy Based Switching Table entries.
 */
#define MLXSW_REG_PPBS_ID 0x300C
#define MLXSW_REG_PPBS_LEN 0x14

MLXSW_REG_DEFINE(ppbs, MLXSW_REG_PPBS_ID, MLXSW_REG_PPBS_LEN);

/* reg_ppbs_pbs_ptr
 * Index into the PBS table.
 * For Spectrum, the index points to the KVD Linear.
 * Access: Index
 */
MLXSW_ITEM32(reg, ppbs, pbs_ptr, 0x08, 0, 24);

/* reg_ppbs_system_port
 * Unique port identifier for the final destination of the packet.
 * Access: RW
 */
MLXSW_ITEM32(reg, ppbs, system_port, 0x10, 0, 16);

static inline void mlxsw_reg_ppbs_pack(char *payload, u32 pbs_ptr,
				       u16 system_port)
{
	MLXSW_REG_ZERO(ppbs, payload);
	mlxsw_reg_ppbs_pbs_ptr_set(payload, pbs_ptr);
	mlxsw_reg_ppbs_system_port_set(payload, system_port);
}

/* PRCR - Policy-Engine Rules Copy Register
 * ----------------------------------------
 * This register is used for accessing rules within a TCAM region.
 */
#define MLXSW_REG_PRCR_ID 0x300D
#define MLXSW_REG_PRCR_LEN 0x40

MLXSW_REG_DEFINE(prcr, MLXSW_REG_PRCR_ID, MLXSW_REG_PRCR_LEN);

enum mlxsw_reg_prcr_op {
	/* Move rules. Moves the rules from "tcam_region_info" starting
	 * at offset "offset" to "dest_tcam_region_info"
	 * at offset "dest_offset."
	 */
	MLXSW_REG_PRCR_OP_MOVE,
	/* Copy rules. Copies the rules from "tcam_region_info" starting
	 * at offset "offset" to "dest_tcam_region_info"
	 * at offset "dest_offset."
	 */
	MLXSW_REG_PRCR_OP_COPY,
};

/* reg_prcr_op
 * Access: OP
 */
MLXSW_ITEM32(reg, prcr, op, 0x00, 28, 4);

/* reg_prcr_offset
 * Offset within the source region to copy/move from.
 * Access: Index
 */
MLXSW_ITEM32(reg, prcr, offset, 0x00, 0, 16);

/* reg_prcr_size
 * The number of rules to copy/move.
 * Access: WO
 */
MLXSW_ITEM32(reg, prcr, size, 0x04, 0, 16);

/* reg_prcr_tcam_region_info
 * Opaque object that represents the source TCAM region.
 * Access: Index
 */
MLXSW_ITEM_BUF(reg, prcr, tcam_region_info, 0x10,
	       MLXSW_REG_PXXX_TCAM_REGION_INFO_LEN);

/* reg_prcr_dest_offset
 * Offset within the source region to copy/move to.
 * Access: Index
 */
MLXSW_ITEM32(reg, prcr, dest_offset, 0x20, 0, 16);

/* reg_prcr_dest_tcam_region_info
 * Opaque object that represents the destination TCAM region.
 * Access: Index
 */
MLXSW_ITEM_BUF(reg, prcr, dest_tcam_region_info, 0x30,
	       MLXSW_REG_PXXX_TCAM_REGION_INFO_LEN);

static inline void mlxsw_reg_prcr_pack(char *payload, enum mlxsw_reg_prcr_op op,
				       const char *src_tcam_region_info,
				       u16 src_offset,
				       const char *dest_tcam_region_info,
				       u16 dest_offset, u16 size)
{
	MLXSW_REG_ZERO(prcr, payload);
	mlxsw_reg_prcr_op_set(payload, op);
	mlxsw_reg_prcr_offset_set(payload, src_offset);
	mlxsw_reg_prcr_size_set(payload, size);
	mlxsw_reg_prcr_tcam_region_info_memcpy_to(payload,
						  src_tcam_region_info);
	mlxsw_reg_prcr_dest_offset_set(payload, dest_offset);
	mlxsw_reg_prcr_dest_tcam_region_info_memcpy_to(payload,
						       dest_tcam_region_info);
}

/* PEFA - Policy-Engine Extended Flexible Action Register
 * ------------------------------------------------------
 * This register is used for accessing an extended flexible action entry
 * in the central KVD Linear Database.
 */
#define MLXSW_REG_PEFA_ID 0x300F
#define MLXSW_REG_PEFA_LEN 0xB0

MLXSW_REG_DEFINE(pefa, MLXSW_REG_PEFA_ID, MLXSW_REG_PEFA_LEN);

/* reg_pefa_index
 * Index in the KVD Linear Centralized Database.
 * Access: Index
 */
MLXSW_ITEM32(reg, pefa, index, 0x00, 0, 24);

#define MLXSW_REG_PXXX_FLEX_ACTION_SET_LEN 0xA8

/* reg_pefa_flex_action_set
 * Action-set to perform when rule is matched.
 * Must be zero padded if action set is shorter.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, pefa, flex_action_set, 0x08,
	       MLXSW_REG_PXXX_FLEX_ACTION_SET_LEN);

static inline void mlxsw_reg_pefa_pack(char *payload, u32 index,
				       const char *flex_action_set)
{
	MLXSW_REG_ZERO(pefa, payload);
	mlxsw_reg_pefa_index_set(payload, index);
	mlxsw_reg_pefa_flex_action_set_memcpy_to(payload, flex_action_set);
}

/* PTCE-V2 - Policy-Engine TCAM Entry Register Version 2
 * -----------------------------------------------------
 * This register is used for accessing rules within a TCAM region.
 * It is a new version of PTCE in order to support wider key,
 * mask and action within a TCAM region. This register is not supported
 * by SwitchX and SwitchX-2.
 */
#define MLXSW_REG_PTCE2_ID 0x3017
#define MLXSW_REG_PTCE2_LEN 0x1D8

MLXSW_REG_DEFINE(ptce2, MLXSW_REG_PTCE2_ID, MLXSW_REG_PTCE2_LEN);

/* reg_ptce2_v
 * Valid.
 * Access: RW
 */
MLXSW_ITEM32(reg, ptce2, v, 0x00, 31, 1);

/* reg_ptce2_a
 * Activity. Set if a packet lookup has hit on the specific entry.
 * To clear the "a" bit, use "clear activity" op or "clear on read" op.
 * Access: RO
 */
MLXSW_ITEM32(reg, ptce2, a, 0x00, 30, 1);

enum mlxsw_reg_ptce2_op {
	/* Read operation. */
	MLXSW_REG_PTCE2_OP_QUERY_READ = 0,
	/* clear on read operation. Used to read entry
	 * and clear Activity bit.
	 */
	MLXSW_REG_PTCE2_OP_QUERY_CLEAR_ON_READ = 1,
	/* Write operation. Used to write a new entry to the table.
	 * All R/W fields are relevant for new entry. Activity bit is set
	 * for new entries - Note write with v = 0 will delete the entry.
	 */
	MLXSW_REG_PTCE2_OP_WRITE_WRITE = 0,
	/* Update action. Only action set will be updated. */
	MLXSW_REG_PTCE2_OP_WRITE_UPDATE = 1,
	/* Clear activity. A bit is cleared for the entry. */
	MLXSW_REG_PTCE2_OP_WRITE_CLEAR_ACTIVITY = 2,
};

/* reg_ptce2_op
 * Access: OP
 */
MLXSW_ITEM32(reg, ptce2, op, 0x00, 20, 3);

/* reg_ptce2_offset
 * Access: Index
 */
MLXSW_ITEM32(reg, ptce2, offset, 0x00, 0, 16);

/* reg_ptce2_tcam_region_info
 * Opaque object that represents the TCAM region.
 * Access: Index
 */
MLXSW_ITEM_BUF(reg, ptce2, tcam_region_info, 0x10,
	       MLXSW_REG_PXXX_TCAM_REGION_INFO_LEN);

#define MLXSW_REG_PTCE2_FLEX_KEY_BLOCKS_LEN 96

/* reg_ptce2_flex_key_blocks
 * ACL Key.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, ptce2, flex_key_blocks, 0x20,
	       MLXSW_REG_PTCE2_FLEX_KEY_BLOCKS_LEN);

/* reg_ptce2_mask
 * mask- in the same size as key. A bit that is set directs the TCAM
 * to compare the corresponding bit in key. A bit that is clear directs
 * the TCAM to ignore the corresponding bit in key.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, ptce2, mask, 0x80,
	       MLXSW_REG_PTCE2_FLEX_KEY_BLOCKS_LEN);

/* reg_ptce2_flex_action_set
 * ACL action set.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, ptce2, flex_action_set, 0xE0,
	       MLXSW_REG_PXXX_FLEX_ACTION_SET_LEN);

static inline void mlxsw_reg_ptce2_pack(char *payload, bool valid,
					enum mlxsw_reg_ptce2_op op,
					const char *tcam_region_info,
					u16 offset)
{
	MLXSW_REG_ZERO(ptce2, payload);
	mlxsw_reg_ptce2_v_set(payload, valid);
	mlxsw_reg_ptce2_op_set(payload, op);
	mlxsw_reg_ptce2_offset_set(payload, offset);
	mlxsw_reg_ptce2_tcam_region_info_memcpy_to(payload, tcam_region_info);
}

/* QPCR - QoS Policer Configuration Register
 * -----------------------------------------
 * The QPCR register is used to create policers - that limit
 * the rate of bytes or packets via some trap group.
 */
#define MLXSW_REG_QPCR_ID 0x4004
#define MLXSW_REG_QPCR_LEN 0x28

MLXSW_REG_DEFINE(qpcr, MLXSW_REG_QPCR_ID, MLXSW_REG_QPCR_LEN);

enum mlxsw_reg_qpcr_g {
	MLXSW_REG_QPCR_G_GLOBAL = 2,
	MLXSW_REG_QPCR_G_STORM_CONTROL = 3,
};

/* reg_qpcr_g
 * The policer type.
 * Access: Index
 */
MLXSW_ITEM32(reg, qpcr, g, 0x00, 14, 2);

/* reg_qpcr_pid
 * Policer ID.
 * Access: Index
 */
MLXSW_ITEM32(reg, qpcr, pid, 0x00, 0, 14);

/* reg_qpcr_color_aware
 * Is the policer aware of colors.
 * Must be 0 (unaware) for cpu port.
 * Access: RW for unbounded policer. RO for bounded policer.
 */
MLXSW_ITEM32(reg, qpcr, color_aware, 0x04, 15, 1);

/* reg_qpcr_bytes
 * Is policer limit is for bytes per sec or packets per sec.
 * 0 - packets
 * 1 - bytes
 * Access: RW for unbounded policer. RO for bounded policer.
 */
MLXSW_ITEM32(reg, qpcr, bytes, 0x04, 14, 1);

enum mlxsw_reg_qpcr_ir_units {
	MLXSW_REG_QPCR_IR_UNITS_M,
	MLXSW_REG_QPCR_IR_UNITS_K,
};

/* reg_qpcr_ir_units
 * Policer's units for cir and eir fields (for bytes limits only)
 * 1 - 10^3
 * 0 - 10^6
 * Access: OP
 */
MLXSW_ITEM32(reg, qpcr, ir_units, 0x04, 12, 1);

enum mlxsw_reg_qpcr_rate_type {
	MLXSW_REG_QPCR_RATE_TYPE_SINGLE = 1,
	MLXSW_REG_QPCR_RATE_TYPE_DOUBLE = 2,
};

/* reg_qpcr_rate_type
 * Policer can have one limit (single rate) or 2 limits with specific operation
 * for packets that exceed the lower rate but not the upper one.
 * (For cpu port must be single rate)
 * Access: RW for unbounded policer. RO for bounded policer.
 */
MLXSW_ITEM32(reg, qpcr, rate_type, 0x04, 8, 2);

/* reg_qpc_cbs
 * Policer's committed burst size.
 * The policer is working with time slices of 50 nano sec. By default every
 * slice is granted the proportionate share of the committed rate. If we want to
 * allow a slice to exceed that share (while still keeping the rate per sec) we
 * can allow burst. The burst size is between the default proportionate share
 * (and no lower than 8) to 32Gb. (Even though giving a number higher than the
 * committed rate will result in exceeding the rate). The burst size must be a
 * log of 2 and will be determined by 2^cbs.
 * Access: RW
 */
MLXSW_ITEM32(reg, qpcr, cbs, 0x08, 24, 6);

/* reg_qpcr_cir
 * Policer's committed rate.
 * The rate used for sungle rate, the lower rate for double rate.
 * For bytes limits, the rate will be this value * the unit from ir_units.
 * (Resolution error is up to 1%).
 * Access: RW
 */
MLXSW_ITEM32(reg, qpcr, cir, 0x0C, 0, 32);

/* reg_qpcr_eir
 * Policer's exceed rate.
 * The higher rate for double rate, reserved for single rate.
 * Lower rate for double rate policer.
 * For bytes limits, the rate will be this value * the unit from ir_units.
 * (Resolution error is up to 1%).
 * Access: RW
 */
MLXSW_ITEM32(reg, qpcr, eir, 0x10, 0, 32);

#define MLXSW_REG_QPCR_DOUBLE_RATE_ACTION 2

/* reg_qpcr_exceed_action.
 * What to do with packets between the 2 limits for double rate.
 * Access: RW for unbounded policer. RO for bounded policer.
 */
MLXSW_ITEM32(reg, qpcr, exceed_action, 0x14, 0, 4);

enum mlxsw_reg_qpcr_action {
	/* Discard */
	MLXSW_REG_QPCR_ACTION_DISCARD = 1,
	/* Forward and set color to red.
	 * If the packet is intended to cpu port, it will be dropped.
	 */
	MLXSW_REG_QPCR_ACTION_FORWARD = 2,
};

/* reg_qpcr_violate_action
 * What to do with packets that cross the cir limit (for single rate) or the eir
 * limit (for double rate).
 * Access: RW for unbounded policer. RO for bounded policer.
 */
MLXSW_ITEM32(reg, qpcr, violate_action, 0x18, 0, 4);

static inline void mlxsw_reg_qpcr_pack(char *payload, u16 pid,
				       enum mlxsw_reg_qpcr_ir_units ir_units,
				       bool bytes, u32 cir, u16 cbs)
{
	MLXSW_REG_ZERO(qpcr, payload);
	mlxsw_reg_qpcr_pid_set(payload, pid);
	mlxsw_reg_qpcr_g_set(payload, MLXSW_REG_QPCR_G_GLOBAL);
	mlxsw_reg_qpcr_rate_type_set(payload, MLXSW_REG_QPCR_RATE_TYPE_SINGLE);
	mlxsw_reg_qpcr_violate_action_set(payload,
					  MLXSW_REG_QPCR_ACTION_DISCARD);
	mlxsw_reg_qpcr_cir_set(payload, cir);
	mlxsw_reg_qpcr_ir_units_set(payload, ir_units);
	mlxsw_reg_qpcr_bytes_set(payload, bytes);
	mlxsw_reg_qpcr_cbs_set(payload, cbs);
}

/* QTCT - QoS Switch Traffic Class Table
 * -------------------------------------
 * Configures the mapping between the packet switch priority and the
 * traffic class on the transmit port.
 */
#define MLXSW_REG_QTCT_ID 0x400A
#define MLXSW_REG_QTCT_LEN 0x08

MLXSW_REG_DEFINE(qtct, MLXSW_REG_QTCT_ID, MLXSW_REG_QTCT_LEN);

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

MLXSW_REG_DEFINE(qeec, MLXSW_REG_QEEC_ID, MLXSW_REG_QEEC_LEN);

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

MLXSW_REG_DEFINE(pmlp, MLXSW_REG_PMLP_ID, MLXSW_REG_PMLP_LEN);

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

MLXSW_REG_DEFINE(pmtu, MLXSW_REG_PMTU_ID, MLXSW_REG_PMTU_LEN);

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

MLXSW_REG_DEFINE(ptys, MLXSW_REG_PTYS_ID, MLXSW_REG_PTYS_LEN);

/* reg_ptys_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, ptys, local_port, 0x00, 16, 8);

#define MLXSW_REG_PTYS_PROTO_MASK_IB	BIT(0)
#define MLXSW_REG_PTYS_PROTO_MASK_ETH	BIT(2)

/* reg_ptys_proto_mask
 * Protocol mask. Indicates which protocol is used.
 * 0 - Infiniband.
 * 1 - Fibre Channel.
 * 2 - Ethernet.
 * Access: Index
 */
MLXSW_ITEM32(reg, ptys, proto_mask, 0x00, 0, 3);

enum {
	MLXSW_REG_PTYS_AN_STATUS_NA,
	MLXSW_REG_PTYS_AN_STATUS_OK,
	MLXSW_REG_PTYS_AN_STATUS_FAIL,
};

/* reg_ptys_an_status
 * Autonegotiation status.
 * Access: RO
 */
MLXSW_ITEM32(reg, ptys, an_status, 0x04, 28, 4);

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
#define MLXSW_REG_PTYS_ETH_SPEED_50GBASE_SR2		BIT(18)
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

/* reg_ptys_ib_link_width_cap
 * IB port supported widths.
 * Access: RO
 */
MLXSW_ITEM32(reg, ptys, ib_link_width_cap, 0x10, 16, 16);

#define MLXSW_REG_PTYS_IB_SPEED_SDR	BIT(0)
#define MLXSW_REG_PTYS_IB_SPEED_DDR	BIT(1)
#define MLXSW_REG_PTYS_IB_SPEED_QDR	BIT(2)
#define MLXSW_REG_PTYS_IB_SPEED_FDR10	BIT(3)
#define MLXSW_REG_PTYS_IB_SPEED_FDR	BIT(4)
#define MLXSW_REG_PTYS_IB_SPEED_EDR	BIT(5)

/* reg_ptys_ib_proto_cap
 * IB port supported speeds and protocols.
 * Access: RO
 */
MLXSW_ITEM32(reg, ptys, ib_proto_cap, 0x10, 0, 16);

/* reg_ptys_eth_proto_admin
 * Speed and protocol to set port to.
 * Access: RW
 */
MLXSW_ITEM32(reg, ptys, eth_proto_admin, 0x18, 0, 32);

/* reg_ptys_ib_link_width_admin
 * IB width to set port to.
 * Access: RW
 */
MLXSW_ITEM32(reg, ptys, ib_link_width_admin, 0x1C, 16, 16);

/* reg_ptys_ib_proto_admin
 * IB speeds and protocols to set port to.
 * Access: RW
 */
MLXSW_ITEM32(reg, ptys, ib_proto_admin, 0x1C, 0, 16);

/* reg_ptys_eth_proto_oper
 * The current speed and protocol configured for the port.
 * Access: RO
 */
MLXSW_ITEM32(reg, ptys, eth_proto_oper, 0x24, 0, 32);

/* reg_ptys_ib_link_width_oper
 * The current IB width to set port to.
 * Access: RO
 */
MLXSW_ITEM32(reg, ptys, ib_link_width_oper, 0x28, 16, 16);

/* reg_ptys_ib_proto_oper
 * The current IB speed and protocol.
 * Access: RO
 */
MLXSW_ITEM32(reg, ptys, ib_proto_oper, 0x28, 0, 16);

/* reg_ptys_eth_proto_lp_advertise
 * The protocols that were advertised by the link partner during
 * autonegotiation.
 * Access: RO
 */
MLXSW_ITEM32(reg, ptys, eth_proto_lp_advertise, 0x30, 0, 32);

static inline void mlxsw_reg_ptys_eth_pack(char *payload, u8 local_port,
					   u32 proto_admin)
{
	MLXSW_REG_ZERO(ptys, payload);
	mlxsw_reg_ptys_local_port_set(payload, local_port);
	mlxsw_reg_ptys_proto_mask_set(payload, MLXSW_REG_PTYS_PROTO_MASK_ETH);
	mlxsw_reg_ptys_eth_proto_admin_set(payload, proto_admin);
}

static inline void mlxsw_reg_ptys_eth_unpack(char *payload,
					     u32 *p_eth_proto_cap,
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

static inline void mlxsw_reg_ptys_ib_pack(char *payload, u8 local_port,
					  u16 proto_admin, u16 link_width)
{
	MLXSW_REG_ZERO(ptys, payload);
	mlxsw_reg_ptys_local_port_set(payload, local_port);
	mlxsw_reg_ptys_proto_mask_set(payload, MLXSW_REG_PTYS_PROTO_MASK_IB);
	mlxsw_reg_ptys_ib_proto_admin_set(payload, proto_admin);
	mlxsw_reg_ptys_ib_link_width_admin_set(payload, link_width);
}

static inline void mlxsw_reg_ptys_ib_unpack(char *payload, u16 *p_ib_proto_cap,
					    u16 *p_ib_link_width_cap,
					    u16 *p_ib_proto_oper,
					    u16 *p_ib_link_width_oper)
{
	if (p_ib_proto_cap)
		*p_ib_proto_cap = mlxsw_reg_ptys_ib_proto_cap_get(payload);
	if (p_ib_link_width_cap)
		*p_ib_link_width_cap =
			mlxsw_reg_ptys_ib_link_width_cap_get(payload);
	if (p_ib_proto_oper)
		*p_ib_proto_oper = mlxsw_reg_ptys_ib_proto_oper_get(payload);
	if (p_ib_link_width_oper)
		*p_ib_link_width_oper =
			mlxsw_reg_ptys_ib_link_width_oper_get(payload);
}

/* PPAD - Port Physical Address Register
 * -------------------------------------
 * The PPAD register configures the per port physical MAC address.
 */
#define MLXSW_REG_PPAD_ID 0x5005
#define MLXSW_REG_PPAD_LEN 0x10

MLXSW_REG_DEFINE(ppad, MLXSW_REG_PPAD_ID, MLXSW_REG_PPAD_LEN);

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

MLXSW_REG_DEFINE(paos, MLXSW_REG_PAOS_ID, MLXSW_REG_PAOS_LEN);

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

MLXSW_REG_DEFINE(pfcc, MLXSW_REG_PFCC_ID, MLXSW_REG_PFCC_LEN);

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

MLXSW_REG_DEFINE(ppcnt, MLXSW_REG_PPCNT_ID, MLXSW_REG_PPCNT_LEN);

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
	MLXSW_REG_PPCNT_TC_CNT = 0x11,
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

/* Ethernet Per Traffic Group Counters */

/* reg_ppcnt_tc_transmit_queue
 * Contains the transmit queue depth in cells of traffic class
 * selected by prio_tc and the port selected by local_port.
 * The field cannot be cleared.
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, tc_transmit_queue, 0x08 + 0x00, 0, 64);

/* reg_ppcnt_tc_no_buffer_discard_uc
 * The number of unicast packets dropped due to lack of shared
 * buffer resources.
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, tc_no_buffer_discard_uc, 0x08 + 0x08, 0, 64);

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

/* PLIB - Port Local to InfiniBand Port
 * ------------------------------------
 * The PLIB register performs mapping from Local Port into InfiniBand Port.
 */
#define MLXSW_REG_PLIB_ID 0x500A
#define MLXSW_REG_PLIB_LEN 0x10

MLXSW_REG_DEFINE(plib, MLXSW_REG_PLIB_ID, MLXSW_REG_PLIB_LEN);

/* reg_plib_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32(reg, plib, local_port, 0x00, 16, 8);

/* reg_plib_ib_port
 * InfiniBand port remapping for local_port.
 * Access: RW
 */
MLXSW_ITEM32(reg, plib, ib_port, 0x00, 0, 8);

/* PPTB - Port Prio To Buffer Register
 * -----------------------------------
 * Configures the switch priority to buffer table.
 */
#define MLXSW_REG_PPTB_ID 0x500B
#define MLXSW_REG_PPTB_LEN 0x10

MLXSW_REG_DEFINE(pptb, MLXSW_REG_PPTB_ID, MLXSW_REG_PPTB_LEN);

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

/* reg_pptb_prio_to_buff_msb
 * Mapping of switch priority <i+8> to one of the allocated receive port
 * buffers.
 * Access: RW
 */
MLXSW_ITEM_BIT_ARRAY(reg, pptb, prio_to_buff_msb, 0x0C, 0x04, 4);

#define MLXSW_REG_PPTB_ALL_PRIO 0xFF

static inline void mlxsw_reg_pptb_pack(char *payload, u8 local_port)
{
	MLXSW_REG_ZERO(pptb, payload);
	mlxsw_reg_pptb_mm_set(payload, MLXSW_REG_PPTB_MM_UM);
	mlxsw_reg_pptb_local_port_set(payload, local_port);
	mlxsw_reg_pptb_pm_set(payload, MLXSW_REG_PPTB_ALL_PRIO);
	mlxsw_reg_pptb_pm_msb_set(payload, MLXSW_REG_PPTB_ALL_PRIO);
}

static inline void mlxsw_reg_pptb_prio_to_buff_pack(char *payload, u8 prio,
						    u8 buff)
{
	mlxsw_reg_pptb_prio_to_buff_set(payload, prio, buff);
	mlxsw_reg_pptb_prio_to_buff_msb_set(payload, prio, buff);
}

/* PBMC - Port Buffer Management Control Register
 * ----------------------------------------------
 * The PBMC register configures and retrieves the port packet buffer
 * allocation for different Prios, and the Pause threshold management.
 */
#define MLXSW_REG_PBMC_ID 0x500C
#define MLXSW_REG_PBMC_LEN 0x6C

MLXSW_REG_DEFINE(pbmc, MLXSW_REG_PBMC_ID, MLXSW_REG_PBMC_LEN);

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

MLXSW_REG_DEFINE(pspa, MLXSW_REG_PSPA_ID, MLXSW_REG_PSPA_LEN);

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
#define MLXSW_REG_HTGT_LEN 0x20

MLXSW_REG_DEFINE(htgt, MLXSW_REG_HTGT_ID, MLXSW_REG_HTGT_LEN);

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
	MLXSW_REG_HTGT_TRAP_GROUP_SX2_RX,
	MLXSW_REG_HTGT_TRAP_GROUP_SX2_CTRL,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_STP,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_LACP,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_LLDP,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_IGMP,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_BGP_IPV4,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_OSPF,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_ARP,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_ARP_MISS,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_ROUTER_EXP,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_REMOTE_ROUTE,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_IP2ME,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_DHCP,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_EVENT,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_IPV6_MLD,
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

#define MLXSW_REG_HTGT_INVALID_POLICER 0xff

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

#define MLXSW_REG_HTGT_DEFAULT_PRIORITY 0

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

#define MLXSW_REG_HTGT_DEFAULT_TC 7

/* reg_htgt_local_path_cpu_tclass
 * CPU ingress traffic class for the trap group.
 * Access: RW
 */
MLXSW_ITEM32(reg, htgt, local_path_cpu_tclass, 0x10, 16, 6);

enum mlxsw_reg_htgt_local_path_rdq {
	MLXSW_REG_HTGT_LOCAL_PATH_RDQ_SX2_CTRL = 0x13,
	MLXSW_REG_HTGT_LOCAL_PATH_RDQ_SX2_RX = 0x14,
	MLXSW_REG_HTGT_LOCAL_PATH_RDQ_SX2_EMAD = 0x15,
	MLXSW_REG_HTGT_LOCAL_PATH_RDQ_SIB_EMAD = 0x15,
};
/* reg_htgt_local_path_rdq
 * Receive descriptor queue (RDQ) to use for the trap group.
 * Access: RW
 */
MLXSW_ITEM32(reg, htgt, local_path_rdq, 0x10, 0, 6);

static inline void mlxsw_reg_htgt_pack(char *payload, u8 group, u8 policer_id,
				       u8 priority, u8 tc)
{
	MLXSW_REG_ZERO(htgt, payload);

	if (policer_id == MLXSW_REG_HTGT_INVALID_POLICER) {
		mlxsw_reg_htgt_pide_set(payload,
					MLXSW_REG_HTGT_POLICER_DISABLE);
	} else {
		mlxsw_reg_htgt_pide_set(payload,
					MLXSW_REG_HTGT_POLICER_ENABLE);
		mlxsw_reg_htgt_pid_set(payload, policer_id);
	}

	mlxsw_reg_htgt_type_set(payload, MLXSW_REG_HTGT_PATH_TYPE_LOCAL);
	mlxsw_reg_htgt_trap_group_set(payload, group);
	mlxsw_reg_htgt_mirror_action_set(payload, MLXSW_REG_HTGT_TRAP_TO_CPU);
	mlxsw_reg_htgt_mirroring_agent_set(payload, 0);
	mlxsw_reg_htgt_priority_set(payload, priority);
	mlxsw_reg_htgt_local_path_cpu_tclass_set(payload, tc);
	mlxsw_reg_htgt_local_path_rdq_set(payload, group);
}

/* HPKT - Host Packet Trap
 * -----------------------
 * Configures trap IDs inside trap groups.
 */
#define MLXSW_REG_HPKT_ID 0x7003
#define MLXSW_REG_HPKT_LEN 0x10

MLXSW_REG_DEFINE(hpkt, MLXSW_REG_HPKT_ID, MLXSW_REG_HPKT_LEN);

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
 * Ignored by SwitchX-2.
 * 0 - Keep factory defaults.
 * 1 - Do not use control buffer for this trap ID.
 * 2 - Use control buffer for this trap ID.
 * Access: RW
 */
MLXSW_ITEM32(reg, hpkt, ctrl, 0x04, 16, 2);

static inline void mlxsw_reg_hpkt_pack(char *payload, u8 action, u16 trap_id,
				       enum mlxsw_reg_htgt_trap_group trap_group,
				       bool is_ctrl)
{
	MLXSW_REG_ZERO(hpkt, payload);
	mlxsw_reg_hpkt_ack_set(payload, MLXSW_REG_HPKT_ACK_NOT_REQUIRED);
	mlxsw_reg_hpkt_action_set(payload, action);
	mlxsw_reg_hpkt_trap_group_set(payload, trap_group);
	mlxsw_reg_hpkt_trap_id_set(payload, trap_id);
	mlxsw_reg_hpkt_ctrl_set(payload, is_ctrl ?
				MLXSW_REG_HPKT_CTRL_PACKET_USE_BUFFER :
				MLXSW_REG_HPKT_CTRL_PACKET_NO_BUFFER);
}

/* RGCR - Router General Configuration Register
 * --------------------------------------------
 * The register is used for setting up the router configuration.
 */
#define MLXSW_REG_RGCR_ID 0x8001
#define MLXSW_REG_RGCR_LEN 0x28

MLXSW_REG_DEFINE(rgcr, MLXSW_REG_RGCR_ID, MLXSW_REG_RGCR_LEN);

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

static inline void mlxsw_reg_rgcr_pack(char *payload, bool ipv4_en,
				       bool ipv6_en)
{
	MLXSW_REG_ZERO(rgcr, payload);
	mlxsw_reg_rgcr_ipv4_en_set(payload, ipv4_en);
	mlxsw_reg_rgcr_ipv6_en_set(payload, ipv6_en);
}

/* RITR - Router Interface Table Register
 * --------------------------------------
 * The register is used to configure the router interface table.
 */
#define MLXSW_REG_RITR_ID 0x8002
#define MLXSW_REG_RITR_LEN 0x40

MLXSW_REG_DEFINE(ritr, MLXSW_REG_RITR_ID, MLXSW_REG_RITR_LEN);

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

/* reg_ritr_lb_en
 * Loop-back filter enable for unicast packets.
 * If the flag is set then loop-back filter for unicast packets is
 * implemented on the RIF. Multicast packets are always subject to
 * loop-back filtering.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, lb_en, 0x04, 24, 1);

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

/* Shared between ingress/egress */
enum mlxsw_reg_ritr_counter_set_type {
	/* No Count. */
	MLXSW_REG_RITR_COUNTER_SET_TYPE_NO_COUNT = 0x0,
	/* Basic. Used for router interfaces, counting the following:
	 *	- Error and Discard counters.
	 *	- Unicast, Multicast and Broadcast counters. Sharing the
	 *	  same set of counters for the different type of traffic
	 *	  (IPv4, IPv6 and mpls).
	 */
	MLXSW_REG_RITR_COUNTER_SET_TYPE_BASIC = 0x9,
};

/* reg_ritr_ingress_counter_index
 * Counter Index for flow counter.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, ingress_counter_index, 0x38, 0, 24);

/* reg_ritr_ingress_counter_set_type
 * Igress Counter Set Type for router interface counter.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, ingress_counter_set_type, 0x38, 24, 8);

/* reg_ritr_egress_counter_index
 * Counter Index for flow counter.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, egress_counter_index, 0x3C, 0, 24);

/* reg_ritr_egress_counter_set_type
 * Egress Counter Set Type for router interface counter.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, egress_counter_set_type, 0x3C, 24, 8);

static inline void mlxsw_reg_ritr_counter_pack(char *payload, u32 index,
					       bool enable, bool egress)
{
	enum mlxsw_reg_ritr_counter_set_type set_type;

	if (enable)
		set_type = MLXSW_REG_RITR_COUNTER_SET_TYPE_BASIC;
	else
		set_type = MLXSW_REG_RITR_COUNTER_SET_TYPE_NO_COUNT;
	mlxsw_reg_ritr_egress_counter_set_type_set(payload, set_type);

	if (egress)
		mlxsw_reg_ritr_egress_counter_index_set(payload, index);
	else
		mlxsw_reg_ritr_ingress_counter_index_set(payload, index);
}

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
				       u16 rif, u16 vr_id, u16 mtu,
				       const char *mac)
{
	bool op = enable ? MLXSW_REG_RITR_RIF_CREATE : MLXSW_REG_RITR_RIF_DEL;

	MLXSW_REG_ZERO(ritr, payload);
	mlxsw_reg_ritr_enable_set(payload, enable);
	mlxsw_reg_ritr_ipv4_set(payload, 1);
	mlxsw_reg_ritr_type_set(payload, type);
	mlxsw_reg_ritr_op_set(payload, op);
	mlxsw_reg_ritr_rif_set(payload, rif);
	mlxsw_reg_ritr_ipv4_fe_set(payload, 1);
	mlxsw_reg_ritr_lb_en_set(payload, 1);
	mlxsw_reg_ritr_virtual_router_set(payload, vr_id);
	mlxsw_reg_ritr_mtu_set(payload, mtu);
	mlxsw_reg_ritr_if_mac_memcpy_to(payload, mac);
}

/* RATR - Router Adjacency Table Register
 * --------------------------------------
 * The RATR register is used to configure the Router Adjacency (next-hop)
 * Table.
 */
#define MLXSW_REG_RATR_ID 0x8008
#define MLXSW_REG_RATR_LEN 0x2C

MLXSW_REG_DEFINE(ratr, MLXSW_REG_RATR_ID, MLXSW_REG_RATR_LEN);

enum mlxsw_reg_ratr_op {
	/* Read */
	MLXSW_REG_RATR_OP_QUERY_READ = 0,
	/* Read and clear activity */
	MLXSW_REG_RATR_OP_QUERY_READ_CLEAR = 2,
	/* Write Adjacency entry */
	MLXSW_REG_RATR_OP_WRITE_WRITE_ENTRY = 1,
	/* Write Adjacency entry only if the activity is cleared.
	 * The write may not succeed if the activity is set. There is not
	 * direct feedback if the write has succeeded or not, however
	 * the get will reveal the actual entry (SW can compare the get
	 * response to the set command).
	 */
	MLXSW_REG_RATR_OP_WRITE_WRITE_ENTRY_ON_ACTIVITY = 3,
};

/* reg_ratr_op
 * Note that Write operation may also be used for updating
 * counter_set_type and counter_index. In this case all other
 * fields must not be updated.
 * Access: OP
 */
MLXSW_ITEM32(reg, ratr, op, 0x00, 28, 4);

/* reg_ratr_v
 * Valid bit. Indicates if the adjacency entry is valid.
 * Note: the device may need some time before reusing an invalidated
 * entry. During this time the entry can not be reused. It is
 * recommended to use another entry before reusing an invalidated
 * entry (e.g. software can put it at the end of the list for
 * reusing). Trying to access an invalidated entry not yet cleared
 * by the device results with failure indicating "Try Again" status.
 * When valid is '0' then egress_router_interface,trap_action,
 * adjacency_parameters and counters are reserved
 * Access: RW
 */
MLXSW_ITEM32(reg, ratr, v, 0x00, 24, 1);

/* reg_ratr_a
 * Activity. Set for new entries. Set if a packet lookup has hit on
 * the specific entry. To clear the a bit, use "clear activity".
 * Access: RO
 */
MLXSW_ITEM32(reg, ratr, a, 0x00, 16, 1);

/* reg_ratr_adjacency_index_low
 * Bits 15:0 of index into the adjacency table.
 * For SwitchX and SwitchX-2, the adjacency table is linear and
 * used for adjacency entries only.
 * For Spectrum, the index is to the KVD linear.
 * Access: Index
 */
MLXSW_ITEM32(reg, ratr, adjacency_index_low, 0x04, 0, 16);

/* reg_ratr_egress_router_interface
 * Range is 0 .. cap_max_router_interfaces - 1
 * Access: RW
 */
MLXSW_ITEM32(reg, ratr, egress_router_interface, 0x08, 0, 16);

enum mlxsw_reg_ratr_trap_action {
	MLXSW_REG_RATR_TRAP_ACTION_NOP,
	MLXSW_REG_RATR_TRAP_ACTION_TRAP,
	MLXSW_REG_RATR_TRAP_ACTION_MIRROR_TO_CPU,
	MLXSW_REG_RATR_TRAP_ACTION_MIRROR,
	MLXSW_REG_RATR_TRAP_ACTION_DISCARD_ERRORS,
};

/* reg_ratr_trap_action
 * see mlxsw_reg_ratr_trap_action
 * Access: RW
 */
MLXSW_ITEM32(reg, ratr, trap_action, 0x0C, 28, 4);

enum mlxsw_reg_ratr_trap_id {
	MLXSW_REG_RATR_TRAP_ID_RTR_EGRESS0 = 0,
	MLXSW_REG_RATR_TRAP_ID_RTR_EGRESS1 = 1,
};

/* reg_ratr_adjacency_index_high
 * Bits 23:16 of the adjacency_index.
 * Access: Index
 */
MLXSW_ITEM32(reg, ratr, adjacency_index_high, 0x0C, 16, 8);

/* reg_ratr_trap_id
 * Trap ID to be reported to CPU.
 * Trap-ID is RTR_EGRESS0 or RTR_EGRESS1.
 * For trap_action of NOP, MIRROR and DISCARD_ERROR
 * Access: RW
 */
MLXSW_ITEM32(reg, ratr, trap_id, 0x0C, 0, 8);

/* reg_ratr_eth_destination_mac
 * MAC address of the destination next-hop.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, ratr, eth_destination_mac, 0x12, 6);

static inline void
mlxsw_reg_ratr_pack(char *payload,
		    enum mlxsw_reg_ratr_op op, bool valid,
		    u32 adjacency_index, u16 egress_rif)
{
	MLXSW_REG_ZERO(ratr, payload);
	mlxsw_reg_ratr_op_set(payload, op);
	mlxsw_reg_ratr_v_set(payload, valid);
	mlxsw_reg_ratr_adjacency_index_low_set(payload, adjacency_index);
	mlxsw_reg_ratr_adjacency_index_high_set(payload, adjacency_index >> 16);
	mlxsw_reg_ratr_egress_router_interface_set(payload, egress_rif);
}

static inline void mlxsw_reg_ratr_eth_entry_pack(char *payload,
						 const char *dest_mac)
{
	mlxsw_reg_ratr_eth_destination_mac_memcpy_to(payload, dest_mac);
}

/* RICNT - Router Interface Counter Register
 * -----------------------------------------
 * The RICNT register retrieves per port performance counters
 */
#define MLXSW_REG_RICNT_ID 0x800B
#define MLXSW_REG_RICNT_LEN 0x100

MLXSW_REG_DEFINE(ricnt, MLXSW_REG_RICNT_ID, MLXSW_REG_RICNT_LEN);

/* reg_ricnt_counter_index
 * Counter index
 * Access: RW
 */
MLXSW_ITEM32(reg, ricnt, counter_index, 0x04, 0, 24);

enum mlxsw_reg_ricnt_counter_set_type {
	/* No Count. */
	MLXSW_REG_RICNT_COUNTER_SET_TYPE_NO_COUNT = 0x00,
	/* Basic. Used for router interfaces, counting the following:
	 *	- Error and Discard counters.
	 *	- Unicast, Multicast and Broadcast counters. Sharing the
	 *	  same set of counters for the different type of traffic
	 *	  (IPv4, IPv6 and mpls).
	 */
	MLXSW_REG_RICNT_COUNTER_SET_TYPE_BASIC = 0x09,
};

/* reg_ricnt_counter_set_type
 * Counter Set Type for router interface counter
 * Access: RW
 */
MLXSW_ITEM32(reg, ricnt, counter_set_type, 0x04, 24, 8);

enum mlxsw_reg_ricnt_opcode {
	/* Nop. Supported only for read access*/
	MLXSW_REG_RICNT_OPCODE_NOP = 0x00,
	/* Clear. Setting the clr bit will reset the counter value for
	 * all counters of the specified Router Interface.
	 */
	MLXSW_REG_RICNT_OPCODE_CLEAR = 0x08,
};

/* reg_ricnt_opcode
 * Opcode
 * Access: RW
 */
MLXSW_ITEM32(reg, ricnt, op, 0x00, 28, 4);

/* reg_ricnt_good_unicast_packets
 * good unicast packets.
 * Access: RW
 */
MLXSW_ITEM64(reg, ricnt, good_unicast_packets, 0x08, 0, 64);

/* reg_ricnt_good_multicast_packets
 * good multicast packets.
 * Access: RW
 */
MLXSW_ITEM64(reg, ricnt, good_multicast_packets, 0x10, 0, 64);

/* reg_ricnt_good_broadcast_packets
 * good broadcast packets
 * Access: RW
 */
MLXSW_ITEM64(reg, ricnt, good_broadcast_packets, 0x18, 0, 64);

/* reg_ricnt_good_unicast_bytes
 * A count of L3 data and padding octets not including L2 headers
 * for good unicast frames.
 * Access: RW
 */
MLXSW_ITEM64(reg, ricnt, good_unicast_bytes, 0x20, 0, 64);

/* reg_ricnt_good_multicast_bytes
 * A count of L3 data and padding octets not including L2 headers
 * for good multicast frames.
 * Access: RW
 */
MLXSW_ITEM64(reg, ricnt, good_multicast_bytes, 0x28, 0, 64);

/* reg_ritr_good_broadcast_bytes
 * A count of L3 data and padding octets not including L2 headers
 * for good broadcast frames.
 * Access: RW
 */
MLXSW_ITEM64(reg, ricnt, good_broadcast_bytes, 0x30, 0, 64);

/* reg_ricnt_error_packets
 * A count of errored frames that do not pass the router checks.
 * Access: RW
 */
MLXSW_ITEM64(reg, ricnt, error_packets, 0x38, 0, 64);

/* reg_ricnt_discrad_packets
 * A count of non-errored frames that do not pass the router checks.
 * Access: RW
 */
MLXSW_ITEM64(reg, ricnt, discard_packets, 0x40, 0, 64);

/* reg_ricnt_error_bytes
 * A count of L3 data and padding octets not including L2 headers
 * for errored frames.
 * Access: RW
 */
MLXSW_ITEM64(reg, ricnt, error_bytes, 0x48, 0, 64);

/* reg_ricnt_discard_bytes
 * A count of L3 data and padding octets not including L2 headers
 * for non-errored frames that do not pass the router checks.
 * Access: RW
 */
MLXSW_ITEM64(reg, ricnt, discard_bytes, 0x50, 0, 64);

static inline void mlxsw_reg_ricnt_pack(char *payload, u32 index,
					enum mlxsw_reg_ricnt_opcode op)
{
	MLXSW_REG_ZERO(ricnt, payload);
	mlxsw_reg_ricnt_op_set(payload, op);
	mlxsw_reg_ricnt_counter_index_set(payload, index);
	mlxsw_reg_ricnt_counter_set_type_set(payload,
					     MLXSW_REG_RICNT_COUNTER_SET_TYPE_BASIC);
}

/* RALTA - Router Algorithmic LPM Tree Allocation Register
 * -------------------------------------------------------
 * RALTA is used to allocate the LPM trees of the SHSPM method.
 */
#define MLXSW_REG_RALTA_ID 0x8010
#define MLXSW_REG_RALTA_LEN 0x04

MLXSW_REG_DEFINE(ralta, MLXSW_REG_RALTA_ID, MLXSW_REG_RALTA_LEN);

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

MLXSW_REG_DEFINE(ralst, MLXSW_REG_RALST_ID, MLXSW_REG_RALST_LEN);

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

MLXSW_REG_DEFINE(raltb, MLXSW_REG_RALTB_ID, MLXSW_REG_RALTB_LEN);

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

/* RALUE - Router Algorithmic LPM Unicast Entry Register
 * -----------------------------------------------------
 * RALUE is used to configure and query LPM entries that serve
 * the Unicast protocols.
 */
#define MLXSW_REG_RALUE_ID 0x8013
#define MLXSW_REG_RALUE_LEN 0x38

MLXSW_REG_DEFINE(ralue, MLXSW_REG_RALUE_ID, MLXSW_REG_RALUE_LEN);

/* reg_ralue_protocol
 * Protocol.
 * Access: Index
 */
MLXSW_ITEM32(reg, ralue, protocol, 0x00, 24, 4);

enum mlxsw_reg_ralue_op {
	/* Read operation. If entry doesn't exist, the operation fails. */
	MLXSW_REG_RALUE_OP_QUERY_READ = 0,
	/* Clear on read operation. Used to read entry and
	 * clear Activity bit.
	 */
	MLXSW_REG_RALUE_OP_QUERY_CLEAR = 1,
	/* Write operation. Used to write a new entry to the table. All RW
	 * fields are written for new entry. Activity bit is set
	 * for new entries.
	 */
	MLXSW_REG_RALUE_OP_WRITE_WRITE = 0,
	/* Update operation. Used to update an existing route entry and
	 * only update the RW fields that are detailed in the field
	 * op_u_mask. If entry doesn't exist, the operation fails.
	 */
	MLXSW_REG_RALUE_OP_WRITE_UPDATE = 1,
	/* Clear activity. The Activity bit (the field a) is cleared
	 * for the entry.
	 */
	MLXSW_REG_RALUE_OP_WRITE_CLEAR = 2,
	/* Delete operation. Used to delete an existing entry. If entry
	 * doesn't exist, the operation fails.
	 */
	MLXSW_REG_RALUE_OP_WRITE_DELETE = 3,
};

/* reg_ralue_op
 * Operation.
 * Access: OP
 */
MLXSW_ITEM32(reg, ralue, op, 0x00, 20, 3);

/* reg_ralue_a
 * Activity. Set for new entries. Set if a packet lookup has hit on the
 * specific entry, only if the entry is a route. To clear the a bit, use
 * "clear activity" op.
 * Enabled by activity_dis in RGCR
 * Access: RO
 */
MLXSW_ITEM32(reg, ralue, a, 0x00, 16, 1);

/* reg_ralue_virtual_router
 * Virtual Router ID
 * Range is 0..cap_max_virtual_routers-1
 * Access: Index
 */
MLXSW_ITEM32(reg, ralue, virtual_router, 0x04, 16, 16);

#define MLXSW_REG_RALUE_OP_U_MASK_ENTRY_TYPE	BIT(0)
#define MLXSW_REG_RALUE_OP_U_MASK_BMP_LEN	BIT(1)
#define MLXSW_REG_RALUE_OP_U_MASK_ACTION	BIT(2)

/* reg_ralue_op_u_mask
 * opcode update mask.
 * On read operation, this field is reserved.
 * This field is valid for update opcode, otherwise - reserved.
 * This field is a bitmask of the fields that should be updated.
 * Access: WO
 */
MLXSW_ITEM32(reg, ralue, op_u_mask, 0x04, 8, 3);

/* reg_ralue_prefix_len
 * Number of bits in the prefix of the LPM route.
 * Note that for IPv6 prefixes, if prefix_len>64 the entry consumes
 * two entries in the physical HW table.
 * Access: Index
 */
MLXSW_ITEM32(reg, ralue, prefix_len, 0x08, 0, 8);

/* reg_ralue_dip*
 * The prefix of the route or of the marker that the object of the LPM
 * is compared with. The most significant bits of the dip are the prefix.
 * The list significant bits must be '0' if the prefix_len is smaller
 * than 128 for IPv6 or smaller than 32 for IPv4.
 * IPv4 address uses bits dip[31:0] and bits dip[127:32] are reserved.
 * Access: Index
 */
MLXSW_ITEM32(reg, ralue, dip4, 0x18, 0, 32);

enum mlxsw_reg_ralue_entry_type {
	MLXSW_REG_RALUE_ENTRY_TYPE_MARKER_ENTRY = 1,
	MLXSW_REG_RALUE_ENTRY_TYPE_ROUTE_ENTRY = 2,
	MLXSW_REG_RALUE_ENTRY_TYPE_MARKER_AND_ROUTE_ENTRY = 3,
};

/* reg_ralue_entry_type
 * Entry type.
 * Note - for Marker entries, the action_type and action fields are reserved.
 * Access: RW
 */
MLXSW_ITEM32(reg, ralue, entry_type, 0x1C, 30, 2);

/* reg_ralue_bmp_len
 * The best match prefix length in the case that there is no match for
 * longer prefixes.
 * If (entry_type != MARKER_ENTRY), bmp_len must be equal to prefix_len
 * Note for any update operation with entry_type modification this
 * field must be set.
 * Access: RW
 */
MLXSW_ITEM32(reg, ralue, bmp_len, 0x1C, 16, 8);

enum mlxsw_reg_ralue_action_type {
	MLXSW_REG_RALUE_ACTION_TYPE_REMOTE,
	MLXSW_REG_RALUE_ACTION_TYPE_LOCAL,
	MLXSW_REG_RALUE_ACTION_TYPE_IP2ME,
};

/* reg_ralue_action_type
 * Action Type
 * Indicates how the IP address is connected.
 * It can be connected to a local subnet through local_erif or can be
 * on a remote subnet connected through a next-hop router,
 * or transmitted to the CPU.
 * Reserved when entry_type = MARKER_ENTRY
 * Access: RW
 */
MLXSW_ITEM32(reg, ralue, action_type, 0x1C, 0, 2);

enum mlxsw_reg_ralue_trap_action {
	MLXSW_REG_RALUE_TRAP_ACTION_NOP,
	MLXSW_REG_RALUE_TRAP_ACTION_TRAP,
	MLXSW_REG_RALUE_TRAP_ACTION_MIRROR_TO_CPU,
	MLXSW_REG_RALUE_TRAP_ACTION_MIRROR,
	MLXSW_REG_RALUE_TRAP_ACTION_DISCARD_ERROR,
};

/* reg_ralue_trap_action
 * Trap action.
 * For IP2ME action, only NOP and MIRROR are possible.
 * Access: RW
 */
MLXSW_ITEM32(reg, ralue, trap_action, 0x20, 28, 4);

/* reg_ralue_trap_id
 * Trap ID to be reported to CPU.
 * Trap ID is RTR_INGRESS0 or RTR_INGRESS1.
 * For trap_action of NOP, MIRROR and DISCARD_ERROR, trap_id is reserved.
 * Access: RW
 */
MLXSW_ITEM32(reg, ralue, trap_id, 0x20, 0, 9);

/* reg_ralue_adjacency_index
 * Points to the first entry of the group-based ECMP.
 * Only relevant in case of REMOTE action.
 * Access: RW
 */
MLXSW_ITEM32(reg, ralue, adjacency_index, 0x24, 0, 24);

/* reg_ralue_ecmp_size
 * Amount of sequential entries starting
 * from the adjacency_index (the number of ECMPs).
 * The valid range is 1-64, 512, 1024, 2048 and 4096.
 * Reserved when trap_action is TRAP or DISCARD_ERROR.
 * Only relevant in case of REMOTE action.
 * Access: RW
 */
MLXSW_ITEM32(reg, ralue, ecmp_size, 0x28, 0, 13);

/* reg_ralue_local_erif
 * Egress Router Interface.
 * Only relevant in case of LOCAL action.
 * Access: RW
 */
MLXSW_ITEM32(reg, ralue, local_erif, 0x24, 0, 16);

/* reg_ralue_v
 * Valid bit for the tunnel_ptr field.
 * If valid = 0 then trap to CPU as IP2ME trap ID.
 * If valid = 1 and the packet format allows NVE or IPinIP tunnel
 * decapsulation then tunnel decapsulation is done.
 * If valid = 1 and packet format does not allow NVE or IPinIP tunnel
 * decapsulation then trap as IP2ME trap ID.
 * Only relevant in case of IP2ME action.
 * Access: RW
 */
MLXSW_ITEM32(reg, ralue, v, 0x24, 31, 1);

/* reg_ralue_tunnel_ptr
 * Tunnel Pointer for NVE or IPinIP tunnel decapsulation.
 * For Spectrum, pointer to KVD Linear.
 * Only relevant in case of IP2ME action.
 * Access: RW
 */
MLXSW_ITEM32(reg, ralue, tunnel_ptr, 0x24, 0, 24);

static inline void mlxsw_reg_ralue_pack(char *payload,
					enum mlxsw_reg_ralxx_protocol protocol,
					enum mlxsw_reg_ralue_op op,
					u16 virtual_router, u8 prefix_len)
{
	MLXSW_REG_ZERO(ralue, payload);
	mlxsw_reg_ralue_protocol_set(payload, protocol);
	mlxsw_reg_ralue_op_set(payload, op);
	mlxsw_reg_ralue_virtual_router_set(payload, virtual_router);
	mlxsw_reg_ralue_prefix_len_set(payload, prefix_len);
	mlxsw_reg_ralue_entry_type_set(payload,
				       MLXSW_REG_RALUE_ENTRY_TYPE_ROUTE_ENTRY);
	mlxsw_reg_ralue_bmp_len_set(payload, prefix_len);
}

static inline void mlxsw_reg_ralue_pack4(char *payload,
					 enum mlxsw_reg_ralxx_protocol protocol,
					 enum mlxsw_reg_ralue_op op,
					 u16 virtual_router, u8 prefix_len,
					 u32 dip)
{
	mlxsw_reg_ralue_pack(payload, protocol, op, virtual_router, prefix_len);
	mlxsw_reg_ralue_dip4_set(payload, dip);
}

static inline void
mlxsw_reg_ralue_act_remote_pack(char *payload,
				enum mlxsw_reg_ralue_trap_action trap_action,
				u16 trap_id, u32 adjacency_index, u16 ecmp_size)
{
	mlxsw_reg_ralue_action_type_set(payload,
					MLXSW_REG_RALUE_ACTION_TYPE_REMOTE);
	mlxsw_reg_ralue_trap_action_set(payload, trap_action);
	mlxsw_reg_ralue_trap_id_set(payload, trap_id);
	mlxsw_reg_ralue_adjacency_index_set(payload, adjacency_index);
	mlxsw_reg_ralue_ecmp_size_set(payload, ecmp_size);
}

static inline void
mlxsw_reg_ralue_act_local_pack(char *payload,
			       enum mlxsw_reg_ralue_trap_action trap_action,
			       u16 trap_id, u16 local_erif)
{
	mlxsw_reg_ralue_action_type_set(payload,
					MLXSW_REG_RALUE_ACTION_TYPE_LOCAL);
	mlxsw_reg_ralue_trap_action_set(payload, trap_action);
	mlxsw_reg_ralue_trap_id_set(payload, trap_id);
	mlxsw_reg_ralue_local_erif_set(payload, local_erif);
}

static inline void
mlxsw_reg_ralue_act_ip2me_pack(char *payload)
{
	mlxsw_reg_ralue_action_type_set(payload,
					MLXSW_REG_RALUE_ACTION_TYPE_IP2ME);
}

/* RAUHT - Router Algorithmic LPM Unicast Host Table Register
 * ----------------------------------------------------------
 * The RAUHT register is used to configure and query the Unicast Host table in
 * devices that implement the Algorithmic LPM.
 */
#define MLXSW_REG_RAUHT_ID 0x8014
#define MLXSW_REG_RAUHT_LEN 0x74

MLXSW_REG_DEFINE(rauht, MLXSW_REG_RAUHT_ID, MLXSW_REG_RAUHT_LEN);

enum mlxsw_reg_rauht_type {
	MLXSW_REG_RAUHT_TYPE_IPV4,
	MLXSW_REG_RAUHT_TYPE_IPV6,
};

/* reg_rauht_type
 * Access: Index
 */
MLXSW_ITEM32(reg, rauht, type, 0x00, 24, 2);

enum mlxsw_reg_rauht_op {
	MLXSW_REG_RAUHT_OP_QUERY_READ = 0,
	/* Read operation */
	MLXSW_REG_RAUHT_OP_QUERY_CLEAR_ON_READ = 1,
	/* Clear on read operation. Used to read entry and clear
	 * activity bit.
	 */
	MLXSW_REG_RAUHT_OP_WRITE_ADD = 0,
	/* Add. Used to write a new entry to the table. All R/W fields are
	 * relevant for new entry. Activity bit is set for new entries.
	 */
	MLXSW_REG_RAUHT_OP_WRITE_UPDATE = 1,
	/* Update action. Used to update an existing route entry and
	 * only update the following fields:
	 * trap_action, trap_id, mac, counter_set_type, counter_index
	 */
	MLXSW_REG_RAUHT_OP_WRITE_CLEAR_ACTIVITY = 2,
	/* Clear activity. A bit is cleared for the entry. */
	MLXSW_REG_RAUHT_OP_WRITE_DELETE = 3,
	/* Delete entry */
	MLXSW_REG_RAUHT_OP_WRITE_DELETE_ALL = 4,
	/* Delete all host entries on a RIF. In this command, dip
	 * field is reserved.
	 */
};

/* reg_rauht_op
 * Access: OP
 */
MLXSW_ITEM32(reg, rauht, op, 0x00, 20, 3);

/* reg_rauht_a
 * Activity. Set for new entries. Set if a packet lookup has hit on
 * the specific entry.
 * To clear the a bit, use "clear activity" op.
 * Enabled by activity_dis in RGCR
 * Access: RO
 */
MLXSW_ITEM32(reg, rauht, a, 0x00, 16, 1);

/* reg_rauht_rif
 * Router Interface
 * Access: Index
 */
MLXSW_ITEM32(reg, rauht, rif, 0x00, 0, 16);

/* reg_rauht_dip*
 * Destination address.
 * Access: Index
 */
MLXSW_ITEM32(reg, rauht, dip4, 0x1C, 0x0, 32);

enum mlxsw_reg_rauht_trap_action {
	MLXSW_REG_RAUHT_TRAP_ACTION_NOP,
	MLXSW_REG_RAUHT_TRAP_ACTION_TRAP,
	MLXSW_REG_RAUHT_TRAP_ACTION_MIRROR_TO_CPU,
	MLXSW_REG_RAUHT_TRAP_ACTION_MIRROR,
	MLXSW_REG_RAUHT_TRAP_ACTION_DISCARD_ERRORS,
};

/* reg_rauht_trap_action
 * Access: RW
 */
MLXSW_ITEM32(reg, rauht, trap_action, 0x60, 28, 4);

enum mlxsw_reg_rauht_trap_id {
	MLXSW_REG_RAUHT_TRAP_ID_RTR_EGRESS0,
	MLXSW_REG_RAUHT_TRAP_ID_RTR_EGRESS1,
};

/* reg_rauht_trap_id
 * Trap ID to be reported to CPU.
 * Trap-ID is RTR_EGRESS0 or RTR_EGRESS1.
 * For trap_action of NOP, MIRROR and DISCARD_ERROR,
 * trap_id is reserved.
 * Access: RW
 */
MLXSW_ITEM32(reg, rauht, trap_id, 0x60, 0, 9);

/* reg_rauht_counter_set_type
 * Counter set type for flow counters
 * Access: RW
 */
MLXSW_ITEM32(reg, rauht, counter_set_type, 0x68, 24, 8);

/* reg_rauht_counter_index
 * Counter index for flow counters
 * Access: RW
 */
MLXSW_ITEM32(reg, rauht, counter_index, 0x68, 0, 24);

/* reg_rauht_mac
 * MAC address.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, rauht, mac, 0x6E, 6);

static inline void mlxsw_reg_rauht_pack(char *payload,
					enum mlxsw_reg_rauht_op op, u16 rif,
					const char *mac)
{
	MLXSW_REG_ZERO(rauht, payload);
	mlxsw_reg_rauht_op_set(payload, op);
	mlxsw_reg_rauht_rif_set(payload, rif);
	mlxsw_reg_rauht_mac_memcpy_to(payload, mac);
}

static inline void mlxsw_reg_rauht_pack4(char *payload,
					 enum mlxsw_reg_rauht_op op, u16 rif,
					 const char *mac, u32 dip)
{
	mlxsw_reg_rauht_pack(payload, op, rif, mac);
	mlxsw_reg_rauht_dip4_set(payload, dip);
}

/* RALEU - Router Algorithmic LPM ECMP Update Register
 * ---------------------------------------------------
 * The register enables updating the ECMP section in the action for multiple
 * LPM Unicast entries in a single operation. The update is executed to
 * all entries of a {virtual router, protocol} tuple using the same ECMP group.
 */
#define MLXSW_REG_RALEU_ID 0x8015
#define MLXSW_REG_RALEU_LEN 0x28

MLXSW_REG_DEFINE(raleu, MLXSW_REG_RALEU_ID, MLXSW_REG_RALEU_LEN);

/* reg_raleu_protocol
 * Protocol.
 * Access: Index
 */
MLXSW_ITEM32(reg, raleu, protocol, 0x00, 24, 4);

/* reg_raleu_virtual_router
 * Virtual Router ID
 * Range is 0..cap_max_virtual_routers-1
 * Access: Index
 */
MLXSW_ITEM32(reg, raleu, virtual_router, 0x00, 0, 16);

/* reg_raleu_adjacency_index
 * Adjacency Index used for matching on the existing entries.
 * Access: Index
 */
MLXSW_ITEM32(reg, raleu, adjacency_index, 0x10, 0, 24);

/* reg_raleu_ecmp_size
 * ECMP Size used for matching on the existing entries.
 * Access: Index
 */
MLXSW_ITEM32(reg, raleu, ecmp_size, 0x14, 0, 13);

/* reg_raleu_new_adjacency_index
 * New Adjacency Index.
 * Access: WO
 */
MLXSW_ITEM32(reg, raleu, new_adjacency_index, 0x20, 0, 24);

/* reg_raleu_new_ecmp_size
 * New ECMP Size.
 * Access: WO
 */
MLXSW_ITEM32(reg, raleu, new_ecmp_size, 0x24, 0, 13);

static inline void mlxsw_reg_raleu_pack(char *payload,
					enum mlxsw_reg_ralxx_protocol protocol,
					u16 virtual_router,
					u32 adjacency_index, u16 ecmp_size,
					u32 new_adjacency_index,
					u16 new_ecmp_size)
{
	MLXSW_REG_ZERO(raleu, payload);
	mlxsw_reg_raleu_protocol_set(payload, protocol);
	mlxsw_reg_raleu_virtual_router_set(payload, virtual_router);
	mlxsw_reg_raleu_adjacency_index_set(payload, adjacency_index);
	mlxsw_reg_raleu_ecmp_size_set(payload, ecmp_size);
	mlxsw_reg_raleu_new_adjacency_index_set(payload, new_adjacency_index);
	mlxsw_reg_raleu_new_ecmp_size_set(payload, new_ecmp_size);
}

/* RAUHTD - Router Algorithmic LPM Unicast Host Table Dump Register
 * ----------------------------------------------------------------
 * The RAUHTD register allows dumping entries from the Router Unicast Host
 * Table. For a given session an entry is dumped no more than one time. The
 * first RAUHTD access after reset is a new session. A session ends when the
 * num_rec response is smaller than num_rec request or for IPv4 when the
 * num_entries is smaller than 4. The clear activity affect the current session
 * or the last session if a new session has not started.
 */
#define MLXSW_REG_RAUHTD_ID 0x8018
#define MLXSW_REG_RAUHTD_BASE_LEN 0x20
#define MLXSW_REG_RAUHTD_REC_LEN 0x20
#define MLXSW_REG_RAUHTD_REC_MAX_NUM 32
#define MLXSW_REG_RAUHTD_LEN (MLXSW_REG_RAUHTD_BASE_LEN + \
		MLXSW_REG_RAUHTD_REC_MAX_NUM * MLXSW_REG_RAUHTD_REC_LEN)
#define MLXSW_REG_RAUHTD_IPV4_ENT_PER_REC 4

MLXSW_REG_DEFINE(rauhtd, MLXSW_REG_RAUHTD_ID, MLXSW_REG_RAUHTD_LEN);

#define MLXSW_REG_RAUHTD_FILTER_A BIT(0)
#define MLXSW_REG_RAUHTD_FILTER_RIF BIT(3)

/* reg_rauhtd_filter_fields
 * if a bit is '0' then the relevant field is ignored and dump is done
 * regardless of the field value
 * Bit0 - filter by activity: entry_a
 * Bit3 - filter by entry rip: entry_rif
 * Access: Index
 */
MLXSW_ITEM32(reg, rauhtd, filter_fields, 0x00, 0, 8);

enum mlxsw_reg_rauhtd_op {
	MLXSW_REG_RAUHTD_OP_DUMP,
	MLXSW_REG_RAUHTD_OP_DUMP_AND_CLEAR,
};

/* reg_rauhtd_op
 * Access: OP
 */
MLXSW_ITEM32(reg, rauhtd, op, 0x04, 24, 2);

/* reg_rauhtd_num_rec
 * At request: number of records requested
 * At response: number of records dumped
 * For IPv4, each record has 4 entries at request and up to 4 entries
 * at response
 * Range is 0..MLXSW_REG_RAUHTD_REC_MAX_NUM
 * Access: Index
 */
MLXSW_ITEM32(reg, rauhtd, num_rec, 0x04, 0, 8);

/* reg_rauhtd_entry_a
 * Dump only if activity has value of entry_a
 * Reserved if filter_fields bit0 is '0'
 * Access: Index
 */
MLXSW_ITEM32(reg, rauhtd, entry_a, 0x08, 16, 1);

enum mlxsw_reg_rauhtd_type {
	MLXSW_REG_RAUHTD_TYPE_IPV4,
	MLXSW_REG_RAUHTD_TYPE_IPV6,
};

/* reg_rauhtd_type
 * Dump only if record type is:
 * 0 - IPv4
 * 1 - IPv6
 * Access: Index
 */
MLXSW_ITEM32(reg, rauhtd, type, 0x08, 0, 4);

/* reg_rauhtd_entry_rif
 * Dump only if RIF has value of entry_rif
 * Reserved if filter_fields bit3 is '0'
 * Access: Index
 */
MLXSW_ITEM32(reg, rauhtd, entry_rif, 0x0C, 0, 16);

static inline void mlxsw_reg_rauhtd_pack(char *payload,
					 enum mlxsw_reg_rauhtd_type type)
{
	MLXSW_REG_ZERO(rauhtd, payload);
	mlxsw_reg_rauhtd_filter_fields_set(payload, MLXSW_REG_RAUHTD_FILTER_A);
	mlxsw_reg_rauhtd_op_set(payload, MLXSW_REG_RAUHTD_OP_DUMP_AND_CLEAR);
	mlxsw_reg_rauhtd_num_rec_set(payload, MLXSW_REG_RAUHTD_REC_MAX_NUM);
	mlxsw_reg_rauhtd_entry_a_set(payload, 1);
	mlxsw_reg_rauhtd_type_set(payload, type);
}

/* reg_rauhtd_ipv4_rec_num_entries
 * Number of valid entries in this record:
 * 0 - 1 valid entry
 * 1 - 2 valid entries
 * 2 - 3 valid entries
 * 3 - 4 valid entries
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, rauhtd, ipv4_rec_num_entries,
		     MLXSW_REG_RAUHTD_BASE_LEN, 28, 2,
		     MLXSW_REG_RAUHTD_REC_LEN, 0x00, false);

/* reg_rauhtd_rec_type
 * Record type.
 * 0 - IPv4
 * 1 - IPv6
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, rauhtd, rec_type, MLXSW_REG_RAUHTD_BASE_LEN, 24, 2,
		     MLXSW_REG_RAUHTD_REC_LEN, 0x00, false);

#define MLXSW_REG_RAUHTD_IPV4_ENT_LEN 0x8

/* reg_rauhtd_ipv4_ent_a
 * Activity. Set for new entries. Set if a packet lookup has hit on the
 * specific entry.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, rauhtd, ipv4_ent_a, MLXSW_REG_RAUHTD_BASE_LEN, 16, 1,
		     MLXSW_REG_RAUHTD_IPV4_ENT_LEN, 0x00, false);

/* reg_rauhtd_ipv4_ent_rif
 * Router interface.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, rauhtd, ipv4_ent_rif, MLXSW_REG_RAUHTD_BASE_LEN, 0,
		     16, MLXSW_REG_RAUHTD_IPV4_ENT_LEN, 0x00, false);

/* reg_rauhtd_ipv4_ent_dip
 * Destination IPv4 address.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, rauhtd, ipv4_ent_dip, MLXSW_REG_RAUHTD_BASE_LEN, 0,
		     32, MLXSW_REG_RAUHTD_IPV4_ENT_LEN, 0x04, false);

static inline void mlxsw_reg_rauhtd_ent_ipv4_unpack(char *payload,
						    int ent_index, u16 *p_rif,
						    u32 *p_dip)
{
	*p_rif = mlxsw_reg_rauhtd_ipv4_ent_rif_get(payload, ent_index);
	*p_dip = mlxsw_reg_rauhtd_ipv4_ent_dip_get(payload, ent_index);
}

/* MFCR - Management Fan Control Register
 * --------------------------------------
 * This register controls the settings of the Fan Speed PWM mechanism.
 */
#define MLXSW_REG_MFCR_ID 0x9001
#define MLXSW_REG_MFCR_LEN 0x08

MLXSW_REG_DEFINE(mfcr, MLXSW_REG_MFCR_ID, MLXSW_REG_MFCR_LEN);

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
MLXSW_ITEM32(reg, mfcr, pwm_frequency, 0x00, 0, 7);

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

MLXSW_REG_DEFINE(mfsc, MLXSW_REG_MFSC_ID, MLXSW_REG_MFSC_LEN);

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

MLXSW_REG_DEFINE(mfsm, MLXSW_REG_MFSM_ID, MLXSW_REG_MFSM_LEN);

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

/* MFSL - Management Fan Speed Limit Register
 * ------------------------------------------
 * The Fan Speed Limit register is used to configure the fan speed
 * event / interrupt notification mechanism. Fan speed threshold are
 * defined for both under-speed and over-speed.
 */
#define MLXSW_REG_MFSL_ID 0x9004
#define MLXSW_REG_MFSL_LEN 0x0C

MLXSW_REG_DEFINE(mfsl, MLXSW_REG_MFSL_ID, MLXSW_REG_MFSL_LEN);

/* reg_mfsl_tacho
 * Fan tachometer index.
 * Access: Index
 */
MLXSW_ITEM32(reg, mfsl, tacho, 0x00, 24, 4);

/* reg_mfsl_tach_min
 * Tachometer minimum value (minimum RPM).
 * Access: RW
 */
MLXSW_ITEM32(reg, mfsl, tach_min, 0x04, 0, 16);

/* reg_mfsl_tach_max
 * Tachometer maximum value (maximum RPM).
 * Access: RW
 */
MLXSW_ITEM32(reg, mfsl, tach_max, 0x08, 0, 16);

static inline void mlxsw_reg_mfsl_pack(char *payload, u8 tacho,
				       u16 tach_min, u16 tach_max)
{
	MLXSW_REG_ZERO(mfsl, payload);
	mlxsw_reg_mfsl_tacho_set(payload, tacho);
	mlxsw_reg_mfsl_tach_min_set(payload, tach_min);
	mlxsw_reg_mfsl_tach_max_set(payload, tach_max);
}

static inline void mlxsw_reg_mfsl_unpack(char *payload, u8 tacho,
					 u16 *p_tach_min, u16 *p_tach_max)
{
	if (p_tach_min)
		*p_tach_min = mlxsw_reg_mfsl_tach_min_get(payload);

	if (p_tach_max)
		*p_tach_max = mlxsw_reg_mfsl_tach_max_get(payload);
}

/* MTCAP - Management Temperature Capabilities
 * -------------------------------------------
 * This register exposes the capabilities of the device and
 * system temperature sensing.
 */
#define MLXSW_REG_MTCAP_ID 0x9009
#define MLXSW_REG_MTCAP_LEN 0x08

MLXSW_REG_DEFINE(mtcap, MLXSW_REG_MTCAP_ID, MLXSW_REG_MTCAP_LEN);

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

MLXSW_REG_DEFINE(mtmp, MLXSW_REG_MTMP_ID, MLXSW_REG_MTMP_LEN);

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

/* MCIA - Management Cable Info Access
 * -----------------------------------
 * MCIA register is used to access the SFP+ and QSFP connector's EPROM.
 */

#define MLXSW_REG_MCIA_ID 0x9014
#define MLXSW_REG_MCIA_LEN 0x40

MLXSW_REG_DEFINE(mcia, MLXSW_REG_MCIA_ID, MLXSW_REG_MCIA_LEN);

/* reg_mcia_l
 * Lock bit. Setting this bit will lock the access to the specific
 * cable. Used for updating a full page in a cable EPROM. Any access
 * other then subsequence writes will fail while the port is locked.
 * Access: RW
 */
MLXSW_ITEM32(reg, mcia, l, 0x00, 31, 1);

/* reg_mcia_module
 * Module number.
 * Access: Index
 */
MLXSW_ITEM32(reg, mcia, module, 0x00, 16, 8);

/* reg_mcia_status
 * Module status.
 * Access: RO
 */
MLXSW_ITEM32(reg, mcia, status, 0x00, 0, 8);

/* reg_mcia_i2c_device_address
 * I2C device address.
 * Access: RW
 */
MLXSW_ITEM32(reg, mcia, i2c_device_address, 0x04, 24, 8);

/* reg_mcia_page_number
 * Page number.
 * Access: RW
 */
MLXSW_ITEM32(reg, mcia, page_number, 0x04, 16, 8);

/* reg_mcia_device_address
 * Device address.
 * Access: RW
 */
MLXSW_ITEM32(reg, mcia, device_address, 0x04, 0, 16);

/* reg_mcia_size
 * Number of bytes to read/write (up to 48 bytes).
 * Access: RW
 */
MLXSW_ITEM32(reg, mcia, size, 0x08, 0, 16);

#define MLXSW_SP_REG_MCIA_EEPROM_SIZE 48

/* reg_mcia_eeprom
 * Bytes to read/write.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, mcia, eeprom, 0x10, MLXSW_SP_REG_MCIA_EEPROM_SIZE);

static inline void mlxsw_reg_mcia_pack(char *payload, u8 module, u8 lock,
				       u8 page_number, u16 device_addr,
				       u8 size, u8 i2c_device_addr)
{
	MLXSW_REG_ZERO(mcia, payload);
	mlxsw_reg_mcia_module_set(payload, module);
	mlxsw_reg_mcia_l_set(payload, lock);
	mlxsw_reg_mcia_page_number_set(payload, page_number);
	mlxsw_reg_mcia_device_address_set(payload, device_addr);
	mlxsw_reg_mcia_size_set(payload, size);
	mlxsw_reg_mcia_i2c_device_address_set(payload, i2c_device_addr);
}

/* MPAT - Monitoring Port Analyzer Table
 * -------------------------------------
 * MPAT Register is used to query and configure the Switch PortAnalyzer Table.
 * For an enabled analyzer, all fields except e (enable) cannot be modified.
 */
#define MLXSW_REG_MPAT_ID 0x901A
#define MLXSW_REG_MPAT_LEN 0x78

MLXSW_REG_DEFINE(mpat, MLXSW_REG_MPAT_ID, MLXSW_REG_MPAT_LEN);

/* reg_mpat_pa_id
 * Port Analyzer ID.
 * Access: Index
 */
MLXSW_ITEM32(reg, mpat, pa_id, 0x00, 28, 4);

/* reg_mpat_system_port
 * A unique port identifier for the final destination of the packet.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, system_port, 0x00, 0, 16);

/* reg_mpat_e
 * Enable. Indicating the Port Analyzer is enabled.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, e, 0x04, 31, 1);

/* reg_mpat_qos
 * Quality Of Service Mode.
 * 0: CONFIGURED - QoS parameters (Switch Priority, and encapsulation
 * PCP, DEI, DSCP or VL) are configured.
 * 1: MAINTAIN - QoS parameters (Switch Priority, Color) are the
 * same as in the original packet that has triggered the mirroring. For
 * SPAN also the pcp,dei are maintained.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, qos, 0x04, 26, 1);

/* reg_mpat_be
 * Best effort mode. Indicates mirroring traffic should not cause packet
 * drop or back pressure, but will discard the mirrored packets. Mirrored
 * packets will be forwarded on a best effort manner.
 * 0: Do not discard mirrored packets
 * 1: Discard mirrored packets if causing congestion
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, be, 0x04, 25, 1);

static inline void mlxsw_reg_mpat_pack(char *payload, u8 pa_id,
				       u16 system_port, bool e)
{
	MLXSW_REG_ZERO(mpat, payload);
	mlxsw_reg_mpat_pa_id_set(payload, pa_id);
	mlxsw_reg_mpat_system_port_set(payload, system_port);
	mlxsw_reg_mpat_e_set(payload, e);
	mlxsw_reg_mpat_qos_set(payload, 1);
	mlxsw_reg_mpat_be_set(payload, 1);
}

/* MPAR - Monitoring Port Analyzer Register
 * ----------------------------------------
 * MPAR register is used to query and configure the port analyzer port mirroring
 * properties.
 */
#define MLXSW_REG_MPAR_ID 0x901B
#define MLXSW_REG_MPAR_LEN 0x08

MLXSW_REG_DEFINE(mpar, MLXSW_REG_MPAR_ID, MLXSW_REG_MPAR_LEN);

/* reg_mpar_local_port
 * The local port to mirror the packets from.
 * Access: Index
 */
MLXSW_ITEM32(reg, mpar, local_port, 0x00, 16, 8);

enum mlxsw_reg_mpar_i_e {
	MLXSW_REG_MPAR_TYPE_EGRESS,
	MLXSW_REG_MPAR_TYPE_INGRESS,
};

/* reg_mpar_i_e
 * Ingress/Egress
 * Access: Index
 */
MLXSW_ITEM32(reg, mpar, i_e, 0x00, 0, 4);

/* reg_mpar_enable
 * Enable mirroring
 * By default, port mirroring is disabled for all ports.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpar, enable, 0x04, 31, 1);

/* reg_mpar_pa_id
 * Port Analyzer ID.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpar, pa_id, 0x04, 0, 4);

static inline void mlxsw_reg_mpar_pack(char *payload, u8 local_port,
				       enum mlxsw_reg_mpar_i_e i_e,
				       bool enable, u8 pa_id)
{
	MLXSW_REG_ZERO(mpar, payload);
	mlxsw_reg_mpar_local_port_set(payload, local_port);
	mlxsw_reg_mpar_enable_set(payload, enable);
	mlxsw_reg_mpar_i_e_set(payload, i_e);
	mlxsw_reg_mpar_pa_id_set(payload, pa_id);
}

/* MLCR - Management LED Control Register
 * --------------------------------------
 * Controls the system LEDs.
 */
#define MLXSW_REG_MLCR_ID 0x902B
#define MLXSW_REG_MLCR_LEN 0x0C

MLXSW_REG_DEFINE(mlcr, MLXSW_REG_MLCR_ID, MLXSW_REG_MLCR_LEN);

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

/* MCQI - Management Component Query Information
 * ---------------------------------------------
 * This register allows querying information about firmware components.
 */
#define MLXSW_REG_MCQI_ID 0x9061
#define MLXSW_REG_MCQI_BASE_LEN 0x18
#define MLXSW_REG_MCQI_CAP_LEN 0x14
#define MLXSW_REG_MCQI_LEN (MLXSW_REG_MCQI_BASE_LEN + MLXSW_REG_MCQI_CAP_LEN)

MLXSW_REG_DEFINE(mcqi, MLXSW_REG_MCQI_ID, MLXSW_REG_MCQI_LEN);

/* reg_mcqi_component_index
 * Index of the accessed component.
 * Access: Index
 */
MLXSW_ITEM32(reg, mcqi, component_index, 0x00, 0, 16);

enum mlxfw_reg_mcqi_info_type {
	MLXSW_REG_MCQI_INFO_TYPE_CAPABILITIES,
};

/* reg_mcqi_info_type
 * Component properties set.
 * Access: RW
 */
MLXSW_ITEM32(reg, mcqi, info_type, 0x08, 0, 5);

/* reg_mcqi_offset
 * The requested/returned data offset from the section start, given in bytes.
 * Must be DWORD aligned.
 * Access: RW
 */
MLXSW_ITEM32(reg, mcqi, offset, 0x10, 0, 32);

/* reg_mcqi_data_size
 * The requested/returned data size, given in bytes. If data_size is not DWORD
 * aligned, the last bytes are zero padded.
 * Access: RW
 */
MLXSW_ITEM32(reg, mcqi, data_size, 0x14, 0, 16);

/* reg_mcqi_cap_max_component_size
 * Maximum size for this component, given in bytes.
 * Access: RO
 */
MLXSW_ITEM32(reg, mcqi, cap_max_component_size, 0x20, 0, 32);

/* reg_mcqi_cap_log_mcda_word_size
 * Log 2 of the access word size in bytes. Read and write access must be aligned
 * to the word size. Write access must be done for an integer number of words.
 * Access: RO
 */
MLXSW_ITEM32(reg, mcqi, cap_log_mcda_word_size, 0x24, 28, 4);

/* reg_mcqi_cap_mcda_max_write_size
 * Maximal write size for MCDA register
 * Access: RO
 */
MLXSW_ITEM32(reg, mcqi, cap_mcda_max_write_size, 0x24, 0, 16);

static inline void mlxsw_reg_mcqi_pack(char *payload, u16 component_index)
{
	MLXSW_REG_ZERO(mcqi, payload);
	mlxsw_reg_mcqi_component_index_set(payload, component_index);
	mlxsw_reg_mcqi_info_type_set(payload,
				     MLXSW_REG_MCQI_INFO_TYPE_CAPABILITIES);
	mlxsw_reg_mcqi_offset_set(payload, 0);
	mlxsw_reg_mcqi_data_size_set(payload, MLXSW_REG_MCQI_CAP_LEN);
}

static inline void mlxsw_reg_mcqi_unpack(char *payload,
					 u32 *p_cap_max_component_size,
					 u8 *p_cap_log_mcda_word_size,
					 u16 *p_cap_mcda_max_write_size)
{
	*p_cap_max_component_size =
		mlxsw_reg_mcqi_cap_max_component_size_get(payload);
	*p_cap_log_mcda_word_size =
		mlxsw_reg_mcqi_cap_log_mcda_word_size_get(payload);
	*p_cap_mcda_max_write_size =
		mlxsw_reg_mcqi_cap_mcda_max_write_size_get(payload);
}

/* MCC - Management Component Control
 * ----------------------------------
 * Controls the firmware component and updates the FSM.
 */
#define MLXSW_REG_MCC_ID 0x9062
#define MLXSW_REG_MCC_LEN 0x1C

MLXSW_REG_DEFINE(mcc, MLXSW_REG_MCC_ID, MLXSW_REG_MCC_LEN);

enum mlxsw_reg_mcc_instruction {
	MLXSW_REG_MCC_INSTRUCTION_LOCK_UPDATE_HANDLE = 0x01,
	MLXSW_REG_MCC_INSTRUCTION_RELEASE_UPDATE_HANDLE = 0x02,
	MLXSW_REG_MCC_INSTRUCTION_UPDATE_COMPONENT = 0x03,
	MLXSW_REG_MCC_INSTRUCTION_VERIFY_COMPONENT = 0x04,
	MLXSW_REG_MCC_INSTRUCTION_ACTIVATE = 0x06,
	MLXSW_REG_MCC_INSTRUCTION_CANCEL = 0x08,
};

/* reg_mcc_instruction
 * Command to be executed by the FSM.
 * Applicable for write operation only.
 * Access: RW
 */
MLXSW_ITEM32(reg, mcc, instruction, 0x00, 0, 8);

/* reg_mcc_component_index
 * Index of the accessed component. Applicable only for commands that
 * refer to components. Otherwise, this field is reserved.
 * Access: Index
 */
MLXSW_ITEM32(reg, mcc, component_index, 0x04, 0, 16);

/* reg_mcc_update_handle
 * Token representing the current flow executed by the FSM.
 * Access: WO
 */
MLXSW_ITEM32(reg, mcc, update_handle, 0x08, 0, 24);

/* reg_mcc_error_code
 * Indicates the successful completion of the instruction, or the reason it
 * failed
 * Access: RO
 */
MLXSW_ITEM32(reg, mcc, error_code, 0x0C, 8, 8);

/* reg_mcc_control_state
 * Current FSM state
 * Access: RO
 */
MLXSW_ITEM32(reg, mcc, control_state, 0x0C, 0, 4);

/* reg_mcc_component_size
 * Component size in bytes. Valid for UPDATE_COMPONENT instruction. Specifying
 * the size may shorten the update time. Value 0x0 means that size is
 * unspecified.
 * Access: WO
 */
MLXSW_ITEM32(reg, mcc, component_size, 0x10, 0, 32);

static inline void mlxsw_reg_mcc_pack(char *payload,
				      enum mlxsw_reg_mcc_instruction instr,
				      u16 component_index, u32 update_handle,
				      u32 component_size)
{
	MLXSW_REG_ZERO(mcc, payload);
	mlxsw_reg_mcc_instruction_set(payload, instr);
	mlxsw_reg_mcc_component_index_set(payload, component_index);
	mlxsw_reg_mcc_update_handle_set(payload, update_handle);
	mlxsw_reg_mcc_component_size_set(payload, component_size);
}

static inline void mlxsw_reg_mcc_unpack(char *payload, u32 *p_update_handle,
					u8 *p_error_code, u8 *p_control_state)
{
	if (p_update_handle)
		*p_update_handle = mlxsw_reg_mcc_update_handle_get(payload);
	if (p_error_code)
		*p_error_code = mlxsw_reg_mcc_error_code_get(payload);
	if (p_control_state)
		*p_control_state = mlxsw_reg_mcc_control_state_get(payload);
}

/* MCDA - Management Component Data Access
 * ---------------------------------------
 * This register allows reading and writing a firmware component.
 */
#define MLXSW_REG_MCDA_ID 0x9063
#define MLXSW_REG_MCDA_BASE_LEN 0x10
#define MLXSW_REG_MCDA_MAX_DATA_LEN 0x80
#define MLXSW_REG_MCDA_LEN \
		(MLXSW_REG_MCDA_BASE_LEN + MLXSW_REG_MCDA_MAX_DATA_LEN)

MLXSW_REG_DEFINE(mcda, MLXSW_REG_MCDA_ID, MLXSW_REG_MCDA_LEN);

/* reg_mcda_update_handle
 * Token representing the current flow executed by the FSM.
 * Access: RW
 */
MLXSW_ITEM32(reg, mcda, update_handle, 0x00, 0, 24);

/* reg_mcda_offset
 * Offset of accessed address relative to component start. Accesses must be in
 * accordance to log_mcda_word_size in MCQI reg.
 * Access: RW
 */
MLXSW_ITEM32(reg, mcda, offset, 0x04, 0, 32);

/* reg_mcda_size
 * Size of the data accessed, given in bytes.
 * Access: RW
 */
MLXSW_ITEM32(reg, mcda, size, 0x08, 0, 16);

/* reg_mcda_data
 * Data block accessed.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, mcda, data, 0x10, 0, 32, 4, 0, false);

static inline void mlxsw_reg_mcda_pack(char *payload, u32 update_handle,
				       u32 offset, u16 size, u8 *data)
{
	int i;

	MLXSW_REG_ZERO(mcda, payload);
	mlxsw_reg_mcda_update_handle_set(payload, update_handle);
	mlxsw_reg_mcda_offset_set(payload, offset);
	mlxsw_reg_mcda_size_set(payload, size);

	for (i = 0; i < size / 4; i++)
		mlxsw_reg_mcda_data_set(payload, i, *(u32 *) &data[i * 4]);
}

/* MPSC - Monitoring Packet Sampling Configuration Register
 * --------------------------------------------------------
 * MPSC Register is used to configure the Packet Sampling mechanism.
 */
#define MLXSW_REG_MPSC_ID 0x9080
#define MLXSW_REG_MPSC_LEN 0x1C

MLXSW_REG_DEFINE(mpsc, MLXSW_REG_MPSC_ID, MLXSW_REG_MPSC_LEN);

/* reg_mpsc_local_port
 * Local port number
 * Not supported for CPU port
 * Access: Index
 */
MLXSW_ITEM32(reg, mpsc, local_port, 0x00, 16, 8);

/* reg_mpsc_e
 * Enable sampling on port local_port
 * Access: RW
 */
MLXSW_ITEM32(reg, mpsc, e, 0x04, 30, 1);

#define MLXSW_REG_MPSC_RATE_MAX 3500000000UL

/* reg_mpsc_rate
 * Sampling rate = 1 out of rate packets (with randomization around
 * the point). Valid values are: 1 to MLXSW_REG_MPSC_RATE_MAX
 * Access: RW
 */
MLXSW_ITEM32(reg, mpsc, rate, 0x08, 0, 32);

static inline void mlxsw_reg_mpsc_pack(char *payload, u8 local_port, bool e,
				       u32 rate)
{
	MLXSW_REG_ZERO(mpsc, payload);
	mlxsw_reg_mpsc_local_port_set(payload, local_port);
	mlxsw_reg_mpsc_e_set(payload, e);
	mlxsw_reg_mpsc_rate_set(payload, rate);
}

/* MGPC - Monitoring General Purpose Counter Set Register
 * The MGPC register retrieves and sets the General Purpose Counter Set.
 */
#define MLXSW_REG_MGPC_ID 0x9081
#define MLXSW_REG_MGPC_LEN 0x18

MLXSW_REG_DEFINE(mgpc, MLXSW_REG_MGPC_ID, MLXSW_REG_MGPC_LEN);

enum mlxsw_reg_mgpc_counter_set_type {
	/* No count */
	MLXSW_REG_MGPC_COUNTER_SET_TYPE_NO_COUT = 0x00,
	/* Count packets and bytes */
	MLXSW_REG_MGPC_COUNTER_SET_TYPE_PACKETS_BYTES = 0x03,
	/* Count only packets */
	MLXSW_REG_MGPC_COUNTER_SET_TYPE_PACKETS = 0x05,
};

/* reg_mgpc_counter_set_type
 * Counter set type.
 * Access: OP
 */
MLXSW_ITEM32(reg, mgpc, counter_set_type, 0x00, 24, 8);

/* reg_mgpc_counter_index
 * Counter index.
 * Access: Index
 */
MLXSW_ITEM32(reg, mgpc, counter_index, 0x00, 0, 24);

enum mlxsw_reg_mgpc_opcode {
	/* Nop */
	MLXSW_REG_MGPC_OPCODE_NOP = 0x00,
	/* Clear counters */
	MLXSW_REG_MGPC_OPCODE_CLEAR = 0x08,
};

/* reg_mgpc_opcode
 * Opcode.
 * Access: OP
 */
MLXSW_ITEM32(reg, mgpc, opcode, 0x04, 28, 4);

/* reg_mgpc_byte_counter
 * Byte counter value.
 * Access: RW
 */
MLXSW_ITEM64(reg, mgpc, byte_counter, 0x08, 0, 64);

/* reg_mgpc_packet_counter
 * Packet counter value.
 * Access: RW
 */
MLXSW_ITEM64(reg, mgpc, packet_counter, 0x10, 0, 64);

static inline void mlxsw_reg_mgpc_pack(char *payload, u32 counter_index,
				       enum mlxsw_reg_mgpc_opcode opcode,
				       enum mlxsw_reg_mgpc_counter_set_type set_type)
{
	MLXSW_REG_ZERO(mgpc, payload);
	mlxsw_reg_mgpc_counter_index_set(payload, counter_index);
	mlxsw_reg_mgpc_counter_set_type_set(payload, set_type);
	mlxsw_reg_mgpc_opcode_set(payload, opcode);
}

/* SBPR - Shared Buffer Pools Register
 * -----------------------------------
 * The SBPR configures and retrieves the shared buffer pools and configuration.
 */
#define MLXSW_REG_SBPR_ID 0xB001
#define MLXSW_REG_SBPR_LEN 0x14

MLXSW_REG_DEFINE(sbpr, MLXSW_REG_SBPR_ID, MLXSW_REG_SBPR_LEN);

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

MLXSW_REG_DEFINE(sbcm, MLXSW_REG_SBCM_ID, MLXSW_REG_SBCM_LEN);

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

MLXSW_REG_DEFINE(sbpm, MLXSW_REG_SBPM_ID, MLXSW_REG_SBPM_LEN);

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

MLXSW_REG_DEFINE(sbmm, MLXSW_REG_SBMM_ID, MLXSW_REG_SBMM_LEN);

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

MLXSW_REG_DEFINE(sbsr, MLXSW_REG_SBSR_ID, MLXSW_REG_SBSR_LEN);

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

/* SBIB - Shared Buffer Internal Buffer Register
 * ---------------------------------------------
 * The SBIB register configures per port buffers for internal use. The internal
 * buffers consume memory on the port buffers (note that the port buffers are
 * used also by PBMC).
 *
 * For Spectrum this is used for egress mirroring.
 */
#define MLXSW_REG_SBIB_ID 0xB006
#define MLXSW_REG_SBIB_LEN 0x10

MLXSW_REG_DEFINE(sbib, MLXSW_REG_SBIB_ID, MLXSW_REG_SBIB_LEN);

/* reg_sbib_local_port
 * Local port number
 * Not supported for CPU port and router port
 * Access: Index
 */
MLXSW_ITEM32(reg, sbib, local_port, 0x00, 16, 8);

/* reg_sbib_buff_size
 * Units represented in cells
 * Allowed range is 0 to (cap_max_headroom_size - 1)
 * Default is 0
 * Access: RW
 */
MLXSW_ITEM32(reg, sbib, buff_size, 0x08, 0, 24);

static inline void mlxsw_reg_sbib_pack(char *payload, u8 local_port,
				       u32 buff_size)
{
	MLXSW_REG_ZERO(sbib, payload);
	mlxsw_reg_sbib_local_port_set(payload, local_port);
	mlxsw_reg_sbib_buff_size_set(payload, buff_size);
}

static const struct mlxsw_reg_info *mlxsw_reg_infos[] = {
	MLXSW_REG(sgcr),
	MLXSW_REG(spad),
	MLXSW_REG(smid),
	MLXSW_REG(sspr),
	MLXSW_REG(sfdat),
	MLXSW_REG(sfd),
	MLXSW_REG(sfn),
	MLXSW_REG(spms),
	MLXSW_REG(spvid),
	MLXSW_REG(spvm),
	MLXSW_REG(spaft),
	MLXSW_REG(sfgc),
	MLXSW_REG(sftr),
	MLXSW_REG(sfdf),
	MLXSW_REG(sldr),
	MLXSW_REG(slcr),
	MLXSW_REG(slcor),
	MLXSW_REG(spmlr),
	MLXSW_REG(svfa),
	MLXSW_REG(svpe),
	MLXSW_REG(sfmr),
	MLXSW_REG(spvmlr),
	MLXSW_REG(ppbt),
	MLXSW_REG(pacl),
	MLXSW_REG(pagt),
	MLXSW_REG(ptar),
	MLXSW_REG(ppbs),
	MLXSW_REG(prcr),
	MLXSW_REG(pefa),
	MLXSW_REG(ptce2),
	MLXSW_REG(qpcr),
	MLXSW_REG(qtct),
	MLXSW_REG(qeec),
	MLXSW_REG(pmlp),
	MLXSW_REG(pmtu),
	MLXSW_REG(ptys),
	MLXSW_REG(ppad),
	MLXSW_REG(paos),
	MLXSW_REG(pfcc),
	MLXSW_REG(ppcnt),
	MLXSW_REG(plib),
	MLXSW_REG(pptb),
	MLXSW_REG(pbmc),
	MLXSW_REG(pspa),
	MLXSW_REG(htgt),
	MLXSW_REG(hpkt),
	MLXSW_REG(rgcr),
	MLXSW_REG(ritr),
	MLXSW_REG(ratr),
	MLXSW_REG(ricnt),
	MLXSW_REG(ralta),
	MLXSW_REG(ralst),
	MLXSW_REG(raltb),
	MLXSW_REG(ralue),
	MLXSW_REG(rauht),
	MLXSW_REG(raleu),
	MLXSW_REG(rauhtd),
	MLXSW_REG(mfcr),
	MLXSW_REG(mfsc),
	MLXSW_REG(mfsm),
	MLXSW_REG(mfsl),
	MLXSW_REG(mtcap),
	MLXSW_REG(mtmp),
	MLXSW_REG(mcia),
	MLXSW_REG(mpat),
	MLXSW_REG(mpar),
	MLXSW_REG(mlcr),
	MLXSW_REG(mpsc),
	MLXSW_REG(mcqi),
	MLXSW_REG(mcc),
	MLXSW_REG(mcda),
	MLXSW_REG(mgpc),
	MLXSW_REG(sbpr),
	MLXSW_REG(sbcm),
	MLXSW_REG(sbpm),
	MLXSW_REG(sbmm),
	MLXSW_REG(sbsr),
	MLXSW_REG(sbib),
};

static inline const char *mlxsw_reg_id_str(u16 reg_id)
{
	const struct mlxsw_reg_info *reg_info;
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxsw_reg_infos); i++) {
		reg_info = mlxsw_reg_infos[i];
		if (reg_info->id == reg_id)
			return reg_info->name;
	}
	return "*UNKNOWN*";
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
