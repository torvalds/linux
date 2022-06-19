/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_REG_H
#define _MLXSW_REG_H

#include <linux/kernel.h>
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
MLXSW_ITEM32_LP(reg, sspr, 0x00, 16, 0x00, 12);

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

static inline void mlxsw_reg_sspr_pack(char *payload, u16 local_port)
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
	MLXSW_REG_SFD_REC_TYPE_UNICAST_TUNNEL = 0xC,
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

/* reg_sfd_uc_set_vid
 * Set VID.
 * 0 - Do not update VID.
 * 1 - Set VID.
 * For Spectrum-2 when set_vid=0 and smpe_valid=1, the smpe will modify the vid.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used.
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_set_vid, MLXSW_REG_SFD_BASE_LEN, 31, 1,
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

/* reg_sfd_uc_vid
 * New VID when set_vid=1.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used and when set_vid=0.
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_vid, MLXSW_REG_SFD_BASE_LEN, 16, 12,
		     MLXSW_REG_SFD_REC_LEN, 0x0C, false);

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
					 u16 local_port)
{
	mlxsw_reg_sfd_rec_pack(payload, rec_index,
			       MLXSW_REG_SFD_REC_TYPE_UNICAST, mac, action);
	mlxsw_reg_sfd_rec_policy_set(payload, rec_index, policy);
	mlxsw_reg_sfd_uc_sub_port_set(payload, rec_index, 0);
	mlxsw_reg_sfd_uc_fid_vid_set(payload, rec_index, fid_vid);
	mlxsw_reg_sfd_uc_system_port_set(payload, rec_index, local_port);
}

/* reg_sfd_uc_lag_sub_port
 * LAG sub port.
 * Must be 0 if multichannel VEPA is not enabled.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_lag_sub_port, MLXSW_REG_SFD_BASE_LEN, 16, 8,
		     MLXSW_REG_SFD_REC_LEN, 0x08, false);

/* reg_sfd_uc_lag_set_vid
 * Set VID.
 * 0 - Do not update VID.
 * 1 - Set VID.
 * For Spectrum-2 when set_vid=0 and smpe_valid=1, the smpe will modify the vid.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used.
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_lag_set_vid, MLXSW_REG_SFD_BASE_LEN, 31, 1,
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
 * New vlan ID.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used and set_vid=0.
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

/* reg_sfd_uc_tunnel_uip_msb
 * When protocol is IPv4, the most significant byte of the underlay IPv4
 * destination IP.
 * When protocol is IPv6, reserved.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_tunnel_uip_msb, MLXSW_REG_SFD_BASE_LEN, 24,
		     8, MLXSW_REG_SFD_REC_LEN, 0x08, false);

/* reg_sfd_uc_tunnel_fid
 * Filtering ID.
 * Access: Index
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_tunnel_fid, MLXSW_REG_SFD_BASE_LEN, 0, 16,
		     MLXSW_REG_SFD_REC_LEN, 0x08, false);

enum mlxsw_reg_sfd_uc_tunnel_protocol {
	MLXSW_REG_SFD_UC_TUNNEL_PROTOCOL_IPV4,
	MLXSW_REG_SFD_UC_TUNNEL_PROTOCOL_IPV6,
};

/* reg_sfd_uc_tunnel_protocol
 * IP protocol.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_tunnel_protocol, MLXSW_REG_SFD_BASE_LEN, 27,
		     1, MLXSW_REG_SFD_REC_LEN, 0x0C, false);

/* reg_sfd_uc_tunnel_uip_lsb
 * When protocol is IPv4, the least significant bytes of the underlay
 * IPv4 destination IP.
 * When protocol is IPv6, pointer to the underlay IPv6 destination IP
 * which is configured by RIPS.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, sfd, uc_tunnel_uip_lsb, MLXSW_REG_SFD_BASE_LEN, 0,
		     24, MLXSW_REG_SFD_REC_LEN, 0x0C, false);

static inline void
mlxsw_reg_sfd_uc_tunnel_pack(char *payload, int rec_index,
			     enum mlxsw_reg_sfd_rec_policy policy,
			     const char *mac, u16 fid,
			     enum mlxsw_reg_sfd_rec_action action,
			     enum mlxsw_reg_sfd_uc_tunnel_protocol proto)
{
	mlxsw_reg_sfd_rec_pack(payload, rec_index,
			       MLXSW_REG_SFD_REC_TYPE_UNICAST_TUNNEL, mac,
			       action);
	mlxsw_reg_sfd_rec_policy_set(payload, rec_index, policy);
	mlxsw_reg_sfd_uc_tunnel_fid_set(payload, rec_index, fid);
	mlxsw_reg_sfd_uc_tunnel_protocol_set(payload, rec_index, proto);
}

static inline void
mlxsw_reg_sfd_uc_tunnel_pack4(char *payload, int rec_index,
			      enum mlxsw_reg_sfd_rec_policy policy,
			      const char *mac, u16 fid,
			      enum mlxsw_reg_sfd_rec_action action, u32 uip)
{
	mlxsw_reg_sfd_uc_tunnel_uip_msb_set(payload, rec_index, uip >> 24);
	mlxsw_reg_sfd_uc_tunnel_uip_lsb_set(payload, rec_index, uip);
	mlxsw_reg_sfd_uc_tunnel_pack(payload, rec_index, policy, mac, fid,
				     action,
				     MLXSW_REG_SFD_UC_TUNNEL_PROTOCOL_IPV4);
}

static inline void
mlxsw_reg_sfd_uc_tunnel_pack6(char *payload, int rec_index, const char *mac,
			      u16 fid, enum mlxsw_reg_sfd_rec_action action,
			      u32 uip_ptr)
{
	mlxsw_reg_sfd_uc_tunnel_uip_lsb_set(payload, rec_index, uip_ptr);
	/* Only static policy is supported for IPv6 unicast tunnel entry. */
	mlxsw_reg_sfd_uc_tunnel_pack(payload, rec_index,
				     MLXSW_REG_SFD_REC_POLICY_STATIC_ENTRY,
				     mac, fid, action,
				     MLXSW_REG_SFD_UC_TUNNEL_PROTOCOL_IPV6);
}

enum mlxsw_reg_tunnel_port {
	MLXSW_REG_TUNNEL_PORT_NVE,
	MLXSW_REG_TUNNEL_PORT_VPLS,
	MLXSW_REG_TUNNEL_PORT_FLEX_TUNNEL0,
	MLXSW_REG_TUNNEL_PORT_FLEX_TUNNEL1,
};

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
	mlxsw_reg_sfn_end_set(payload, 0);
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
	/* Learned unicast tunnel record. */
	MLXSW_REG_SFN_REC_TYPE_LEARNED_UNICAST_TUNNEL = 0xD,
	/* Aged-out unicast tunnel record. */
	MLXSW_REG_SFN_REC_TYPE_AGED_OUT_UNICAST_TUNNEL = 0xE,
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
					    u16 *p_local_port)
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

/* reg_sfn_uc_tunnel_uip_msb
 * When protocol is IPv4, the most significant byte of the underlay IPv4
 * address of the remote VTEP.
 * When protocol is IPv6, reserved.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, sfn, uc_tunnel_uip_msb, MLXSW_REG_SFN_BASE_LEN, 24,
		     8, MLXSW_REG_SFN_REC_LEN, 0x08, false);

enum mlxsw_reg_sfn_uc_tunnel_protocol {
	MLXSW_REG_SFN_UC_TUNNEL_PROTOCOL_IPV4,
	MLXSW_REG_SFN_UC_TUNNEL_PROTOCOL_IPV6,
};

/* reg_sfn_uc_tunnel_protocol
 * IP protocol.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, sfn, uc_tunnel_protocol, MLXSW_REG_SFN_BASE_LEN, 27,
		     1, MLXSW_REG_SFN_REC_LEN, 0x0C, false);

/* reg_sfn_uc_tunnel_uip_lsb
 * When protocol is IPv4, the least significant bytes of the underlay
 * IPv4 address of the remote VTEP.
 * When protocol is IPv6, ipv6_id to be queried from TNIPSD.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, sfn, uc_tunnel_uip_lsb, MLXSW_REG_SFN_BASE_LEN, 0,
		     24, MLXSW_REG_SFN_REC_LEN, 0x0C, false);

/* reg_sfn_uc_tunnel_port
 * Tunnel port.
 * Reserved on Spectrum.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, sfn, tunnel_port, MLXSW_REG_SFN_BASE_LEN, 0, 4,
		     MLXSW_REG_SFN_REC_LEN, 0x10, false);

static inline void
mlxsw_reg_sfn_uc_tunnel_unpack(char *payload, int rec_index, char *mac,
			       u16 *p_fid, u32 *p_uip,
			       enum mlxsw_reg_sfn_uc_tunnel_protocol *p_proto)
{
	u32 uip_msb, uip_lsb;

	mlxsw_reg_sfn_rec_mac_memcpy_from(payload, rec_index, mac);
	*p_fid = mlxsw_reg_sfn_mac_fid_get(payload, rec_index);
	uip_msb = mlxsw_reg_sfn_uc_tunnel_uip_msb_get(payload, rec_index);
	uip_lsb = mlxsw_reg_sfn_uc_tunnel_uip_lsb_get(payload, rec_index);
	*p_uip = uip_msb << 24 | uip_lsb;
	*p_proto = mlxsw_reg_sfn_uc_tunnel_protocol_get(payload, rec_index);
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
MLXSW_ITEM32_LP(reg, spms, 0x00, 16, 0x00, 12);

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

static inline void mlxsw_reg_spms_pack(char *payload, u16 local_port)
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

/* reg_spvid_tport
 * Port is tunnel port.
 * Reserved when SwitchX/-2 or Spectrum-1.
 * Access: Index
 */
MLXSW_ITEM32(reg, spvid, tport, 0x00, 24, 1);

/* reg_spvid_local_port
 * When tport = 0: Local port number. Not supported for CPU port.
 * When tport = 1: Tunnel port.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, spvid, 0x00, 16, 0x00, 12);

/* reg_spvid_sub_port
 * Virtual port within the physical port.
 * Should be set to 0 when virtual ports are not enabled on the port.
 * Access: Index
 */
MLXSW_ITEM32(reg, spvid, sub_port, 0x00, 8, 8);

/* reg_spvid_egr_et_set
 * When VLAN is pushed at ingress (for untagged packets or for
 * QinQ push mode) then the EtherType is decided at the egress port.
 * Reserved when Spectrum-1.
 * Access: RW
 */
MLXSW_ITEM32(reg, spvid, egr_et_set, 0x04, 24, 1);

/* reg_spvid_et_vlan
 * EtherType used for when VLAN is pushed at ingress (for untagged
 * packets or for QinQ push mode).
 * 0: ether_type0 - (default)
 * 1: ether_type1
 * 2: ether_type2 - Reserved when Spectrum-1, supported by Spectrum-2
 * Ethertype IDs are configured by SVER.
 * Reserved when egr_et_set = 1.
 * Access: RW
 */
MLXSW_ITEM32(reg, spvid, et_vlan, 0x04, 16, 2);

/* reg_spvid_pvid
 * Port default VID
 * Access: RW
 */
MLXSW_ITEM32(reg, spvid, pvid, 0x04, 0, 12);

static inline void mlxsw_reg_spvid_pack(char *payload, u16 local_port, u16 pvid,
					u8 et_vlan)
{
	MLXSW_REG_ZERO(spvid, payload);
	mlxsw_reg_spvid_local_port_set(payload, local_port);
	mlxsw_reg_spvid_pvid_set(payload, pvid);
	mlxsw_reg_spvid_et_vlan_set(payload, et_vlan);
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
MLXSW_ITEM32_LP(reg, spvm, 0x00, 16, 0x00, 12);

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

static inline void mlxsw_reg_spvm_pack(char *payload, u16 local_port,
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
MLXSW_ITEM32_LP(reg, spaft, 0x00, 16, 0x00, 12);

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

static inline void mlxsw_reg_spaft_pack(char *payload, u16 local_port,
					bool allow_untagged)
{
	MLXSW_REG_ZERO(spaft, payload);
	mlxsw_reg_spaft_local_port_set(payload, local_port);
	mlxsw_reg_spaft_allow_untagged_set(payload, allow_untagged);
	mlxsw_reg_spaft_allow_prio_tagged_set(payload, allow_untagged);
	mlxsw_reg_spaft_allow_tagged_set(payload, true);
}

/* SFGC - Switch Flooding Group Configuration
 * ------------------------------------------
 * The following register controls the association of flooding tables and MIDs
 * to packet types used for flooding.
 */
#define MLXSW_REG_SFGC_ID 0x2011
#define MLXSW_REG_SFGC_LEN 0x14

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

/* reg_sfgc_mid_base
 * MID Base.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used.
 */
MLXSW_ITEM32(reg, sfgc, mid_base, 0x10, 0, 16);

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
	MLXSW_REG_SFDF_FLUSH_PER_NVE,
	MLXSW_REG_SFDF_FLUSH_PER_NVE_AND_FID,
};

/* reg_sfdf_flush_type
 * Flush type.
 * 0 - All SWID dynamic entries are flushed.
 * 1 - All FID dynamic entries are flushed.
 * 2 - All dynamic entries pointing to port are flushed.
 * 3 - All FID dynamic entries pointing to port are flushed.
 * 4 - All dynamic entries pointing to LAG are flushed.
 * 5 - All FID dynamic entries pointing to LAG are flushed.
 * 6 - All entries of type "Unicast Tunnel" or "Multicast Tunnel" are
 *     flushed.
 * 7 - All entries of type "Unicast Tunnel" or "Multicast Tunnel" are
 *     flushed, per FID.
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
						    u16 local_port)
{
	MLXSW_REG_ZERO(sldr, payload);
	mlxsw_reg_sldr_op_set(payload, MLXSW_REG_SLDR_OP_LAG_ADD_PORT_LIST);
	mlxsw_reg_sldr_lag_id_set(payload, lag_id);
	mlxsw_reg_sldr_num_ports_set(payload, 1);
	mlxsw_reg_sldr_system_port_set(payload, 0, local_port);
}

static inline void mlxsw_reg_sldr_lag_remove_port_pack(char *payload, u8 lag_id,
						       u16 local_port)
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
MLXSW_ITEM32_LP(reg, slcr, 0x00, 16, 0x00, 12);

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

/* reg_slcr_seed
 * LAG seed value. The seed is the same for all ports.
 * Access: RW
 */
MLXSW_ITEM32(reg, slcr, seed, 0x08, 0, 32);

static inline void mlxsw_reg_slcr_pack(char *payload, u16 lag_hash, u32 seed)
{
	MLXSW_REG_ZERO(slcr, payload);
	mlxsw_reg_slcr_pp_set(payload, MLXSW_REG_SLCR_PP_GLOBAL);
	mlxsw_reg_slcr_type_set(payload, MLXSW_REG_SLCR_TYPE_CRC);
	mlxsw_reg_slcr_lag_hash_set(payload, lag_hash);
	mlxsw_reg_slcr_seed_set(payload, seed);
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
MLXSW_ITEM32_LP(reg, slcor, 0x00, 16, 0x00, 12);

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
					u16 local_port, u16 lag_id,
					enum mlxsw_reg_slcor_col col)
{
	MLXSW_REG_ZERO(slcor, payload);
	mlxsw_reg_slcor_col_set(payload, col);
	mlxsw_reg_slcor_local_port_set(payload, local_port);
	mlxsw_reg_slcor_lag_id_set(payload, lag_id);
}

static inline void mlxsw_reg_slcor_port_add_pack(char *payload,
						 u16 local_port, u16 lag_id,
						 u8 port_index)
{
	mlxsw_reg_slcor_pack(payload, local_port, lag_id,
			     MLXSW_REG_SLCOR_COL_LAG_ADD_PORT);
	mlxsw_reg_slcor_port_index_set(payload, port_index);
}

static inline void mlxsw_reg_slcor_port_remove_pack(char *payload,
						    u16 local_port, u16 lag_id)
{
	mlxsw_reg_slcor_pack(payload, local_port, lag_id,
			     MLXSW_REG_SLCOR_COL_LAG_REMOVE_PORT);
}

static inline void mlxsw_reg_slcor_col_enable_pack(char *payload,
						   u16 local_port, u16 lag_id)
{
	mlxsw_reg_slcor_pack(payload, local_port, lag_id,
			     MLXSW_REG_SLCOR_COL_LAG_COLLECTOR_ENABLED);
}

static inline void mlxsw_reg_slcor_col_disable_pack(char *payload,
						    u16 local_port, u16 lag_id)
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
MLXSW_ITEM32_LP(reg, spmlr, 0x00, 16, 0x00, 12);

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

static inline void mlxsw_reg_spmlr_pack(char *payload, u16 local_port,
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
#define MLXSW_REG_SVFA_LEN 0x18

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
MLXSW_ITEM32_LP(reg, svfa, 0x00, 16, 0x00, 12);

enum mlxsw_reg_svfa_mt {
	MLXSW_REG_SVFA_MT_VID_TO_FID,
	MLXSW_REG_SVFA_MT_PORT_VID_TO_FID,
	MLXSW_REG_SVFA_MT_VNI_TO_FID,
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

/* reg_svfa_vni
 * Virtual Network Identifier.
 * Access: Index
 *
 * Note: Reserved when mapping_table is not 2 (VNI mapping table).
 */
MLXSW_ITEM32(reg, svfa, vni, 0x10, 0, 24);

/* reg_svfa_irif_v
 * Ingress RIF valid.
 * 0 - Ingress RIF is not valid, no ingress RIF assigned.
 * 1 - Ingress RIF valid.
 * Must not be set for a non enabled RIF.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used.
 */
MLXSW_ITEM32(reg, svfa, irif_v, 0x14, 24, 1);

/* reg_svfa_irif
 * Ingress RIF (Router Interface).
 * Range is 0..cap_max_router_interfaces-1.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used and when irif_v=0.
 */
MLXSW_ITEM32(reg, svfa, irif, 0x14, 0, 16);

static inline void __mlxsw_reg_svfa_pack(char *payload,
					 enum mlxsw_reg_svfa_mt mt, bool valid,
					 u16 fid)
{
	MLXSW_REG_ZERO(svfa, payload);
	mlxsw_reg_svfa_swid_set(payload, 0);
	mlxsw_reg_svfa_mapping_table_set(payload, mt);
	mlxsw_reg_svfa_v_set(payload, valid);
	mlxsw_reg_svfa_fid_set(payload, fid);
}

static inline void mlxsw_reg_svfa_port_vid_pack(char *payload, u16 local_port,
						bool valid, u16 fid, u16 vid)
{
	enum mlxsw_reg_svfa_mt mt = MLXSW_REG_SVFA_MT_PORT_VID_TO_FID;

	__mlxsw_reg_svfa_pack(payload, mt, valid, fid);
	mlxsw_reg_svfa_local_port_set(payload, local_port);
	mlxsw_reg_svfa_vid_set(payload, vid);
}

static inline void mlxsw_reg_svfa_vid_pack(char *payload, bool valid, u16 fid,
					   u16 vid)
{
	enum mlxsw_reg_svfa_mt mt = MLXSW_REG_SVFA_MT_VID_TO_FID;

	__mlxsw_reg_svfa_pack(payload, mt, valid, fid);
	mlxsw_reg_svfa_vid_set(payload, vid);
}

static inline void mlxsw_reg_svfa_vni_pack(char *payload, bool valid, u16 fid,
					   u32 vni)
{
	enum mlxsw_reg_svfa_mt mt = MLXSW_REG_SVFA_MT_VNI_TO_FID;

	__mlxsw_reg_svfa_pack(payload, mt, valid, fid);
	mlxsw_reg_svfa_vni_set(payload, vni);
}

/*  SPVTR - Switch Port VLAN Stacking Register
 *  ------------------------------------------
 *  The Switch Port VLAN Stacking register configures the VLAN mode of the port
 *  to enable VLAN stacking.
 */
#define MLXSW_REG_SPVTR_ID 0x201D
#define MLXSW_REG_SPVTR_LEN 0x10

MLXSW_REG_DEFINE(spvtr, MLXSW_REG_SPVTR_ID, MLXSW_REG_SPVTR_LEN);

/* reg_spvtr_tport
 * Port is tunnel port.
 * Access: Index
 *
 * Note: Reserved when SwitchX/-2 or Spectrum-1.
 */
MLXSW_ITEM32(reg, spvtr, tport, 0x00, 24, 1);

/* reg_spvtr_local_port
 * When tport = 0: local port number (Not supported from/to CPU).
 * When tport = 1: tunnel port.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, spvtr, 0x00, 16, 0x00, 12);

/* reg_spvtr_ippe
 * Ingress Port Prio Mode Update Enable.
 * When set, the Port Prio Mode is updated with the provided ipprio_mode field.
 * Reserved on Get operations.
 * Access: OP
 */
MLXSW_ITEM32(reg, spvtr, ippe, 0x04, 31, 1);

/* reg_spvtr_ipve
 * Ingress Port VID Mode Update Enable.
 * When set, the Ingress Port VID Mode is updated with the provided ipvid_mode
 * field.
 * Reserved on Get operations.
 * Access: OP
 */
MLXSW_ITEM32(reg, spvtr, ipve, 0x04, 30, 1);

/* reg_spvtr_epve
 * Egress Port VID Mode Update Enable.
 * When set, the Egress Port VID Mode is updated with the provided epvid_mode
 * field.
 * Access: OP
 */
MLXSW_ITEM32(reg, spvtr, epve, 0x04, 29, 1);

/* reg_spvtr_ipprio_mode
 * Ingress Port Priority Mode.
 * This controls the PCP and DEI of the new outer VLAN
 * Note: for SwitchX/-2 the DEI is not affected.
 * 0: use port default PCP and DEI (configured by QPDPC).
 * 1: use C-VLAN PCP and DEI.
 * Has no effect when ipvid_mode = 0.
 * Reserved when tport = 1.
 * Access: RW
 */
MLXSW_ITEM32(reg, spvtr, ipprio_mode, 0x04, 20, 4);

enum mlxsw_reg_spvtr_ipvid_mode {
	/* IEEE Compliant PVID (default) */
	MLXSW_REG_SPVTR_IPVID_MODE_IEEE_COMPLIANT_PVID,
	/* Push VLAN (for VLAN stacking, except prio tagged packets) */
	MLXSW_REG_SPVTR_IPVID_MODE_PUSH_VLAN_FOR_UNTAGGED_PACKET,
	/* Always push VLAN (also for prio tagged packets) */
	MLXSW_REG_SPVTR_IPVID_MODE_ALWAYS_PUSH_VLAN,
};

/* reg_spvtr_ipvid_mode
 * Ingress Port VLAN-ID Mode.
 * For Spectrum family, this affects the values of SPVM.i
 * Access: RW
 */
MLXSW_ITEM32(reg, spvtr, ipvid_mode, 0x04, 16, 4);

enum mlxsw_reg_spvtr_epvid_mode {
	/* IEEE Compliant VLAN membership */
	MLXSW_REG_SPVTR_EPVID_MODE_IEEE_COMPLIANT_VLAN_MEMBERSHIP,
	/* Pop VLAN (for VLAN stacking) */
	MLXSW_REG_SPVTR_EPVID_MODE_POP_VLAN,
};

/* reg_spvtr_epvid_mode
 * Egress Port VLAN-ID Mode.
 * For Spectrum family, this affects the values of SPVM.e,u,pt.
 * Access: WO
 */
MLXSW_ITEM32(reg, spvtr, epvid_mode, 0x04, 0, 4);

static inline void mlxsw_reg_spvtr_pack(char *payload, bool tport,
					u16 local_port,
					enum mlxsw_reg_spvtr_ipvid_mode ipvid_mode)
{
	MLXSW_REG_ZERO(spvtr, payload);
	mlxsw_reg_spvtr_tport_set(payload, tport);
	mlxsw_reg_spvtr_local_port_set(payload, local_port);
	mlxsw_reg_spvtr_ipvid_mode_set(payload, ipvid_mode);
	mlxsw_reg_spvtr_ipve_set(payload, true);
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
MLXSW_ITEM32_LP(reg, svpe, 0x00, 16, 0x00, 12);

/* reg_svpe_vp_en
 * Virtual port enable.
 * 0 - Disable, VLAN mode (VID to FID).
 * 1 - Enable, Virtual port mode ({Port, VID} to FID).
 * Access: RW
 */
MLXSW_ITEM32(reg, svpe, vp_en, 0x00, 8, 1);

static inline void mlxsw_reg_svpe_pack(char *payload, u16 local_port,
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
#define MLXSW_REG_SFMR_LEN 0x30

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

/* reg_sfmr_flood_rsp
 * Router sub-port flooding table.
 * 0 - Regular flooding table.
 * 1 - Router sub-port flooding table. For this FID the flooding is per
 * router-sub-port local_port. Must not be set for a FID which is not a
 * router-sub-port and must be set prior to enabling the relevant RIF.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used.
 */
MLXSW_ITEM32(reg, sfmr, flood_rsp, 0x08, 31, 1);

/* reg_sfmr_flood_bridge_type
 * Flood bridge type (see SFGC.bridge_type).
 * 0 - type_0.
 * 1 - type_1.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used and when flood_rsp=1.
 */
MLXSW_ITEM32(reg, sfmr, flood_bridge_type, 0x08, 28, 1);

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
 * When legacy bridge model is used, a given VNI can only be assigned to one
 * FID. When unified bridge model is used, it configures only the FID->VNI,
 * the VNI->FID is done by SVFA.
 * Access: RW
 */
MLXSW_ITEM32(reg, sfmr, vni, 0x10, 0, 24);

/* reg_sfmr_irif_v
 * Ingress RIF valid.
 * 0 - Ingress RIF is not valid, no ingress RIF assigned.
 * 1 - Ingress RIF valid.
 * Must not be set for a non valid RIF.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used.
 */
MLXSW_ITEM32(reg, sfmr, irif_v, 0x14, 24, 1);

/* reg_sfmr_irif
 * Ingress RIF (Router Interface).
 * Range is 0..cap_max_router_interfaces-1.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used and when irif_v=0.
 */
MLXSW_ITEM32(reg, sfmr, irif, 0x14, 0, 16);

/* reg_sfmr_smpe_valid
 * SMPE is valid.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used, when flood_rsp=1 and on
 * Spectrum-1.
 */
MLXSW_ITEM32(reg, sfmr, smpe_valid, 0x28, 20, 1);

/* reg_sfmr_smpe
 * Switch multicast port to egress VID.
 * Range is 0..cap_max_rmpe-1
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used, when flood_rsp=1 and on
 * Spectrum-1.
 */
MLXSW_ITEM32(reg, sfmr, smpe, 0x28, 0, 16);

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
MLXSW_ITEM32_LP(reg, spvmlr, 0x00, 16, 0x00, 12);

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

static inline void mlxsw_reg_spvmlr_pack(char *payload, u16 local_port,
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

/* SPVC - Switch Port VLAN Classification Register
 * -----------------------------------------------
 * Configures the port to identify packets as untagged / single tagged /
 * double packets based on the packet EtherTypes.
 * Ethertype IDs are configured by SVER.
 */
#define MLXSW_REG_SPVC_ID 0x2026
#define MLXSW_REG_SPVC_LEN 0x0C

MLXSW_REG_DEFINE(spvc, MLXSW_REG_SPVC_ID, MLXSW_REG_SPVC_LEN);

/* reg_spvc_local_port
 * Local port.
 * Access: Index
 *
 * Note: applies both to Rx port and Tx port, so if a packet traverses
 * through Rx port i and a Tx port j then port i and port j must have the
 * same configuration.
 */
MLXSW_ITEM32_LP(reg, spvc, 0x00, 16, 0x00, 12);

/* reg_spvc_inner_et2
 * Vlan Tag1 EtherType2 enable.
 * Packet is initially classified as double VLAN Tag if in addition to
 * being classified with a tag0 VLAN Tag its tag1 EtherType value is
 * equal to ether_type2.
 * 0: disable (default)
 * 1: enable
 * Access: RW
 */
MLXSW_ITEM32(reg, spvc, inner_et2, 0x08, 17, 1);

/* reg_spvc_et2
 * Vlan Tag0 EtherType2 enable.
 * Packet is initially classified as VLAN Tag if its tag0 EtherType is
 * equal to ether_type2.
 * 0: disable (default)
 * 1: enable
 * Access: RW
 */
MLXSW_ITEM32(reg, spvc, et2, 0x08, 16, 1);

/* reg_spvc_inner_et1
 * Vlan Tag1 EtherType1 enable.
 * Packet is initially classified as double VLAN Tag if in addition to
 * being classified with a tag0 VLAN Tag its tag1 EtherType value is
 * equal to ether_type1.
 * 0: disable
 * 1: enable (default)
 * Access: RW
 */
MLXSW_ITEM32(reg, spvc, inner_et1, 0x08, 9, 1);

/* reg_spvc_et1
 * Vlan Tag0 EtherType1 enable.
 * Packet is initially classified as VLAN Tag if its tag0 EtherType is
 * equal to ether_type1.
 * 0: disable
 * 1: enable (default)
 * Access: RW
 */
MLXSW_ITEM32(reg, spvc, et1, 0x08, 8, 1);

/* reg_inner_et0
 * Vlan Tag1 EtherType0 enable.
 * Packet is initially classified as double VLAN Tag if in addition to
 * being classified with a tag0 VLAN Tag its tag1 EtherType value is
 * equal to ether_type0.
 * 0: disable
 * 1: enable (default)
 * Access: RW
 */
MLXSW_ITEM32(reg, spvc, inner_et0, 0x08, 1, 1);

/* reg_et0
 * Vlan Tag0 EtherType0 enable.
 * Packet is initially classified as VLAN Tag if its tag0 EtherType is
 * equal to ether_type0.
 * 0: disable
 * 1: enable (default)
 * Access: RW
 */
MLXSW_ITEM32(reg, spvc, et0, 0x08, 0, 1);

static inline void mlxsw_reg_spvc_pack(char *payload, u16 local_port, bool et1,
				       bool et0)
{
	MLXSW_REG_ZERO(spvc, payload);
	mlxsw_reg_spvc_local_port_set(payload, local_port);
	/* Enable inner_et1 and inner_et0 to enable identification of double
	 * tagged packets.
	 */
	mlxsw_reg_spvc_inner_et1_set(payload, 1);
	mlxsw_reg_spvc_inner_et0_set(payload, 1);
	mlxsw_reg_spvc_et1_set(payload, et1);
	mlxsw_reg_spvc_et0_set(payload, et0);
}

/* SPEVET - Switch Port Egress VLAN EtherType
 * ------------------------------------------
 * The switch port egress VLAN EtherType configures which EtherType to push at
 * egress for packets incoming through a local port for which 'SPVID.egr_et_set'
 * is set.
 */
#define MLXSW_REG_SPEVET_ID 0x202A
#define MLXSW_REG_SPEVET_LEN 0x08

MLXSW_REG_DEFINE(spevet, MLXSW_REG_SPEVET_ID, MLXSW_REG_SPEVET_LEN);

/* reg_spevet_local_port
 * Egress Local port number.
 * Not supported to CPU port.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, spevet, 0x00, 16, 0x00, 12);

/* reg_spevet_et_vlan
 * Egress EtherType VLAN to push when SPVID.egr_et_set field set for the packet:
 * 0: ether_type0 - (default)
 * 1: ether_type1
 * 2: ether_type2
 * Access: RW
 */
MLXSW_ITEM32(reg, spevet, et_vlan, 0x04, 16, 2);

static inline void mlxsw_reg_spevet_pack(char *payload, u16 local_port,
					 u8 et_vlan)
{
	MLXSW_REG_ZERO(spevet, payload);
	mlxsw_reg_spevet_local_port_set(payload, local_port);
	mlxsw_reg_spevet_et_vlan_set(payload, et_vlan);
}

/* SMPE - Switch Multicast Port to Egress VID
 * ------------------------------------------
 * The switch multicast port to egress VID maps
 * {egress_port, SMPE index} -> {VID}.
 */
#define MLXSW_REG_SMPE_ID 0x202B
#define MLXSW_REG_SMPE_LEN 0x0C

MLXSW_REG_DEFINE(smpe, MLXSW_REG_SMPE_ID, MLXSW_REG_SMPE_LEN);

/* reg_smpe_local_port
 * Local port number.
 * CPU port is not supported.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, smpe, 0x00, 16, 0x00, 12);

/* reg_smpe_smpe_index
 * Switch multicast port to egress VID.
 * Range is 0..cap_max_rmpe-1.
 * Access: Index
 */
MLXSW_ITEM32(reg, smpe, smpe_index, 0x04, 0, 16);

/* reg_smpe_evid
 * Egress VID.
 * Access: RW
 */
MLXSW_ITEM32(reg, smpe, evid, 0x08, 0, 12);

static inline void mlxsw_reg_smpe_pack(char *payload, u16 local_port,
				       u16 smpe_index, u16 evid)
{
	MLXSW_REG_ZERO(smpe, payload);
	mlxsw_reg_smpe_local_port_set(payload, local_port);
	mlxsw_reg_smpe_smpe_index_set(payload, smpe_index);
	mlxsw_reg_smpe_evid_set(payload, evid);
}

/* SFTR-V2 - Switch Flooding Table Version 2 Register
 * --------------------------------------------------
 * The switch flooding table is used for flooding packet replication. The table
 * defines a bit mask of ports for packet replication.
 */
#define MLXSW_REG_SFTR2_ID 0x202F
#define MLXSW_REG_SFTR2_LEN 0x120

MLXSW_REG_DEFINE(sftr2, MLXSW_REG_SFTR2_ID, MLXSW_REG_SFTR2_LEN);

/* reg_sftr2_swid
 * Switch partition ID with which to associate the port.
 * Access: Index
 */
MLXSW_ITEM32(reg, sftr2, swid, 0x00, 24, 8);

/* reg_sftr2_flood_table
 * Flooding table index to associate with the specific type on the specific
 * switch partition.
 * Access: Index
 */
MLXSW_ITEM32(reg, sftr2, flood_table, 0x00, 16, 6);

/* reg_sftr2_index
 * Index. Used as an index into the Flooding Table in case the table is
 * configured to use VID / FID or FID Offset.
 * Access: Index
 */
MLXSW_ITEM32(reg, sftr2, index, 0x00, 0, 16);

/* reg_sftr2_table_type
 * See mlxsw_flood_table_type
 * Access: RW
 */
MLXSW_ITEM32(reg, sftr2, table_type, 0x04, 16, 3);

/* reg_sftr2_range
 * Range of entries to update
 * Access: Index
 */
MLXSW_ITEM32(reg, sftr2, range, 0x04, 0, 16);

/* reg_sftr2_port
 * Local port membership (1 bit per port).
 * Access: RW
 */
MLXSW_ITEM_BIT_ARRAY(reg, sftr2, port, 0x20, 0x80, 1);

/* reg_sftr2_port_mask
 * Local port mask (1 bit per port).
 * Access: WO
 */
MLXSW_ITEM_BIT_ARRAY(reg, sftr2, port_mask, 0xA0, 0x80, 1);

static inline void mlxsw_reg_sftr2_pack(char *payload,
					unsigned int flood_table,
					unsigned int index,
					enum mlxsw_flood_table_type table_type,
					unsigned int range, u16 port, bool set)
{
	MLXSW_REG_ZERO(sftr2, payload);
	mlxsw_reg_sftr2_swid_set(payload, 0);
	mlxsw_reg_sftr2_flood_table_set(payload, flood_table);
	mlxsw_reg_sftr2_index_set(payload, index);
	mlxsw_reg_sftr2_table_type_set(payload, table_type);
	mlxsw_reg_sftr2_range_set(payload, range);
	mlxsw_reg_sftr2_port_set(payload, port, set);
	mlxsw_reg_sftr2_port_mask_set(payload, port, 1);
}

/* SMID-V2 - Switch Multicast ID Version 2 Register
 * ------------------------------------------------
 * The MID record maps from a MID (Multicast ID), which is a unique identifier
 * of the multicast group within the stacking domain, into a list of local
 * ports into which the packet is replicated.
 */
#define MLXSW_REG_SMID2_ID 0x2034
#define MLXSW_REG_SMID2_LEN 0x120

MLXSW_REG_DEFINE(smid2, MLXSW_REG_SMID2_ID, MLXSW_REG_SMID2_LEN);

/* reg_smid2_swid
 * Switch partition ID.
 * Access: Index
 */
MLXSW_ITEM32(reg, smid2, swid, 0x00, 24, 8);

/* reg_smid2_mid
 * Multicast identifier - global identifier that represents the multicast group
 * across all devices.
 * Access: Index
 */
MLXSW_ITEM32(reg, smid2, mid, 0x00, 0, 16);

/* reg_smid2_smpe_valid
 * SMPE is valid.
 * When not valid, the egress VID will not be modified by the SMPE table.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used and on Spectrum-2.
 */
MLXSW_ITEM32(reg, smid2, smpe_valid, 0x08, 20, 1);

/* reg_smid2_smpe
 * Switch multicast port to egress VID.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used and on Spectrum-2.
 */
MLXSW_ITEM32(reg, smid2, smpe, 0x08, 0, 16);

/* reg_smid2_port
 * Local port memebership (1 bit per port).
 * Access: RW
 */
MLXSW_ITEM_BIT_ARRAY(reg, smid2, port, 0x20, 0x80, 1);

/* reg_smid2_port_mask
 * Local port mask (1 bit per port).
 * Access: WO
 */
MLXSW_ITEM_BIT_ARRAY(reg, smid2, port_mask, 0xA0, 0x80, 1);

static inline void mlxsw_reg_smid2_pack(char *payload, u16 mid, u16 port,
					bool set, bool smpe_valid, u16 smpe)
{
	MLXSW_REG_ZERO(smid2, payload);
	mlxsw_reg_smid2_swid_set(payload, 0);
	mlxsw_reg_smid2_mid_set(payload, mid);
	mlxsw_reg_smid2_port_set(payload, port, set);
	mlxsw_reg_smid2_port_mask_set(payload, port, 1);
	mlxsw_reg_smid2_smpe_valid_set(payload, smpe_valid);
	mlxsw_reg_smid2_smpe_set(payload, smpe_valid ? smpe : 0);
}

/* CWTP - Congetion WRED ECN TClass Profile
 * ----------------------------------------
 * Configures the profiles for queues of egress port and traffic class
 */
#define MLXSW_REG_CWTP_ID 0x2802
#define MLXSW_REG_CWTP_BASE_LEN 0x28
#define MLXSW_REG_CWTP_PROFILE_DATA_REC_LEN 0x08
#define MLXSW_REG_CWTP_LEN 0x40

MLXSW_REG_DEFINE(cwtp, MLXSW_REG_CWTP_ID, MLXSW_REG_CWTP_LEN);

/* reg_cwtp_local_port
 * Local port number
 * Not supported for CPU port
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, cwtp, 0x00, 16, 0x00, 12);

/* reg_cwtp_traffic_class
 * Traffic Class to configure
 * Access: Index
 */
MLXSW_ITEM32(reg, cwtp, traffic_class, 32, 0, 8);

/* reg_cwtp_profile_min
 * Minimum Average Queue Size of the profile in cells.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, cwtp, profile_min, MLXSW_REG_CWTP_BASE_LEN,
		     0, 20, MLXSW_REG_CWTP_PROFILE_DATA_REC_LEN, 0, false);

/* reg_cwtp_profile_percent
 * Percentage of WRED and ECN marking for maximum Average Queue size
 * Range is 0 to 100, units of integer percentage
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, cwtp, profile_percent, MLXSW_REG_CWTP_BASE_LEN,
		     24, 7, MLXSW_REG_CWTP_PROFILE_DATA_REC_LEN, 4, false);

/* reg_cwtp_profile_max
 * Maximum Average Queue size of the profile in cells
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, cwtp, profile_max, MLXSW_REG_CWTP_BASE_LEN,
		     0, 20, MLXSW_REG_CWTP_PROFILE_DATA_REC_LEN, 4, false);

#define MLXSW_REG_CWTP_MIN_VALUE 64
#define MLXSW_REG_CWTP_MAX_PROFILE 2
#define MLXSW_REG_CWTP_DEFAULT_PROFILE 1

static inline void mlxsw_reg_cwtp_pack(char *payload, u16 local_port,
				       u8 traffic_class)
{
	int i;

	MLXSW_REG_ZERO(cwtp, payload);
	mlxsw_reg_cwtp_local_port_set(payload, local_port);
	mlxsw_reg_cwtp_traffic_class_set(payload, traffic_class);

	for (i = 0; i <= MLXSW_REG_CWTP_MAX_PROFILE; i++) {
		mlxsw_reg_cwtp_profile_min_set(payload, i,
					       MLXSW_REG_CWTP_MIN_VALUE);
		mlxsw_reg_cwtp_profile_max_set(payload, i,
					       MLXSW_REG_CWTP_MIN_VALUE);
	}
}

#define MLXSW_REG_CWTP_PROFILE_TO_INDEX(profile) (profile - 1)

static inline void
mlxsw_reg_cwtp_profile_pack(char *payload, u8 profile, u32 min, u32 max,
			    u32 probability)
{
	u8 index = MLXSW_REG_CWTP_PROFILE_TO_INDEX(profile);

	mlxsw_reg_cwtp_profile_min_set(payload, index, min);
	mlxsw_reg_cwtp_profile_max_set(payload, index, max);
	mlxsw_reg_cwtp_profile_percent_set(payload, index, probability);
}

/* CWTPM - Congestion WRED ECN TClass and Pool Mapping
 * ---------------------------------------------------
 * The CWTPM register maps each egress port and traffic class to profile num.
 */
#define MLXSW_REG_CWTPM_ID 0x2803
#define MLXSW_REG_CWTPM_LEN 0x44

MLXSW_REG_DEFINE(cwtpm, MLXSW_REG_CWTPM_ID, MLXSW_REG_CWTPM_LEN);

/* reg_cwtpm_local_port
 * Local port number
 * Not supported for CPU port
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, cwtpm, 0x00, 16, 0x00, 12);

/* reg_cwtpm_traffic_class
 * Traffic Class to configure
 * Access: Index
 */
MLXSW_ITEM32(reg, cwtpm, traffic_class, 32, 0, 8);

/* reg_cwtpm_ew
 * Control enablement of WRED for traffic class:
 * 0 - Disable
 * 1 - Enable
 * Access: RW
 */
MLXSW_ITEM32(reg, cwtpm, ew, 36, 1, 1);

/* reg_cwtpm_ee
 * Control enablement of ECN for traffic class:
 * 0 - Disable
 * 1 - Enable
 * Access: RW
 */
MLXSW_ITEM32(reg, cwtpm, ee, 36, 0, 1);

/* reg_cwtpm_tcp_g
 * TCP Green Profile.
 * Index of the profile within {port, traffic class} to use.
 * 0 for disabling both WRED and ECN for this type of traffic.
 * Access: RW
 */
MLXSW_ITEM32(reg, cwtpm, tcp_g, 52, 0, 2);

/* reg_cwtpm_tcp_y
 * TCP Yellow Profile.
 * Index of the profile within {port, traffic class} to use.
 * 0 for disabling both WRED and ECN for this type of traffic.
 * Access: RW
 */
MLXSW_ITEM32(reg, cwtpm, tcp_y, 56, 16, 2);

/* reg_cwtpm_tcp_r
 * TCP Red Profile.
 * Index of the profile within {port, traffic class} to use.
 * 0 for disabling both WRED and ECN for this type of traffic.
 * Access: RW
 */
MLXSW_ITEM32(reg, cwtpm, tcp_r, 56, 0, 2);

/* reg_cwtpm_ntcp_g
 * Non-TCP Green Profile.
 * Index of the profile within {port, traffic class} to use.
 * 0 for disabling both WRED and ECN for this type of traffic.
 * Access: RW
 */
MLXSW_ITEM32(reg, cwtpm, ntcp_g, 60, 0, 2);

/* reg_cwtpm_ntcp_y
 * Non-TCP Yellow Profile.
 * Index of the profile within {port, traffic class} to use.
 * 0 for disabling both WRED and ECN for this type of traffic.
 * Access: RW
 */
MLXSW_ITEM32(reg, cwtpm, ntcp_y, 64, 16, 2);

/* reg_cwtpm_ntcp_r
 * Non-TCP Red Profile.
 * Index of the profile within {port, traffic class} to use.
 * 0 for disabling both WRED and ECN for this type of traffic.
 * Access: RW
 */
MLXSW_ITEM32(reg, cwtpm, ntcp_r, 64, 0, 2);

#define MLXSW_REG_CWTPM_RESET_PROFILE 0

static inline void mlxsw_reg_cwtpm_pack(char *payload, u16 local_port,
					u8 traffic_class, u8 profile,
					bool wred, bool ecn)
{
	MLXSW_REG_ZERO(cwtpm, payload);
	mlxsw_reg_cwtpm_local_port_set(payload, local_port);
	mlxsw_reg_cwtpm_traffic_class_set(payload, traffic_class);
	mlxsw_reg_cwtpm_ew_set(payload, wred);
	mlxsw_reg_cwtpm_ee_set(payload, ecn);
	mlxsw_reg_cwtpm_tcp_g_set(payload, profile);
	mlxsw_reg_cwtpm_tcp_y_set(payload, profile);
	mlxsw_reg_cwtpm_tcp_r_set(payload, profile);
	mlxsw_reg_cwtpm_ntcp_g_set(payload, profile);
	mlxsw_reg_cwtpm_ntcp_y_set(payload, profile);
	mlxsw_reg_cwtpm_ntcp_r_set(payload, profile);
}

/* PGCR - Policy-Engine General Configuration Register
 * ---------------------------------------------------
 * This register configures general Policy-Engine settings.
 */
#define MLXSW_REG_PGCR_ID 0x3001
#define MLXSW_REG_PGCR_LEN 0x20

MLXSW_REG_DEFINE(pgcr, MLXSW_REG_PGCR_ID, MLXSW_REG_PGCR_LEN);

/* reg_pgcr_default_action_pointer_base
 * Default action pointer base. Each region has a default action pointer
 * which is equal to default_action_pointer_base + region_id.
 * Access: RW
 */
MLXSW_ITEM32(reg, pgcr, default_action_pointer_base, 0x1C, 0, 24);

static inline void mlxsw_reg_pgcr_pack(char *payload, u32 pointer_base)
{
	MLXSW_REG_ZERO(pgcr, payload);
	mlxsw_reg_pgcr_default_action_pointer_base_set(payload, pointer_base);
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
MLXSW_ITEM32_LP(reg, ppbt, 0x00, 16, 0x00, 12);

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
				       u16 local_port, u16 acl_info)
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

/* reg_pagt_multi
 * Multi-ACL
 * 0 - This ACL is the last ACL in the multi-ACL
 * 1 - This ACL is part of a multi-ACL
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, pagt, multi, 0x30, 31, 1, 0x04, 0x00, false);

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
					      u16 acl_id, bool multi)
{
	u8 size = mlxsw_reg_pagt_size_get(payload);

	if (index >= size)
		mlxsw_reg_pagt_size_set(payload, index + 1);
	mlxsw_reg_pagt_multi_set(payload, index, multi);
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
 * For Spectrum and Spectrum-2, this is always type 2 - "flexible"
 * Access: WO
 */
MLXSW_ITEM32(reg, ptar, action_set_type, 0x00, 16, 8);

enum mlxsw_reg_ptar_key_type {
	MLXSW_REG_PTAR_KEY_TYPE_FLEX = 0x50, /* Spetrum */
	MLXSW_REG_PTAR_KEY_TYPE_FLEX2 = 0x51, /* Spectrum-2 */
};

/* reg_ptar_key_type
 * TCAM key type for the region.
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
				       enum mlxsw_reg_ptar_key_type key_type,
				       u16 region_size, u16 region_id,
				       const char *tcam_region_info)
{
	MLXSW_REG_ZERO(ptar, payload);
	mlxsw_reg_ptar_op_set(payload, op);
	mlxsw_reg_ptar_action_set_type_set(payload, 2); /* "flexible" */
	mlxsw_reg_ptar_key_type_set(payload, key_type);
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

/* reg_pefa_a
 * Index in the KVD Linear Centralized Database.
 * Activity
 * For a new entry: set if ca=0, clear if ca=1
 * Set if a packet lookup has hit on the specific entry
 * Access: RO
 */
MLXSW_ITEM32(reg, pefa, a, 0x04, 29, 1);

/* reg_pefa_ca
 * Clear activity
 * When write: activity is according to this field
 * When read: after reading the activity is cleared according to ca
 * Access: OP
 */
MLXSW_ITEM32(reg, pefa, ca, 0x04, 24, 1);

#define MLXSW_REG_FLEX_ACTION_SET_LEN 0xA8

/* reg_pefa_flex_action_set
 * Action-set to perform when rule is matched.
 * Must be zero padded if action set is shorter.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, pefa, flex_action_set, 0x08, MLXSW_REG_FLEX_ACTION_SET_LEN);

static inline void mlxsw_reg_pefa_pack(char *payload, u32 index, bool ca,
				       const char *flex_action_set)
{
	MLXSW_REG_ZERO(pefa, payload);
	mlxsw_reg_pefa_index_set(payload, index);
	mlxsw_reg_pefa_ca_set(payload, ca);
	if (flex_action_set)
		mlxsw_reg_pefa_flex_action_set_memcpy_to(payload,
							 flex_action_set);
}

static inline void mlxsw_reg_pefa_unpack(char *payload, bool *p_a)
{
	*p_a = mlxsw_reg_pefa_a_get(payload);
}

/* PEMRBT - Policy-Engine Multicast Router Binding Table Register
 * --------------------------------------------------------------
 * This register is used for binding Multicast router to an ACL group
 * that serves the MC router.
 * This register is not supported by SwitchX/-2 and Spectrum.
 */
#define MLXSW_REG_PEMRBT_ID 0x3014
#define MLXSW_REG_PEMRBT_LEN 0x14

MLXSW_REG_DEFINE(pemrbt, MLXSW_REG_PEMRBT_ID, MLXSW_REG_PEMRBT_LEN);

enum mlxsw_reg_pemrbt_protocol {
	MLXSW_REG_PEMRBT_PROTO_IPV4,
	MLXSW_REG_PEMRBT_PROTO_IPV6,
};

/* reg_pemrbt_protocol
 * Access: Index
 */
MLXSW_ITEM32(reg, pemrbt, protocol, 0x00, 0, 1);

/* reg_pemrbt_group_id
 * ACL group identifier.
 * Range 0..cap_max_acl_groups-1
 * Access: RW
 */
MLXSW_ITEM32(reg, pemrbt, group_id, 0x10, 0, 16);

static inline void
mlxsw_reg_pemrbt_pack(char *payload, enum mlxsw_reg_pemrbt_protocol protocol,
		      u16 group_id)
{
	MLXSW_REG_ZERO(pemrbt, payload);
	mlxsw_reg_pemrbt_protocol_set(payload, protocol);
	mlxsw_reg_pemrbt_group_id_set(payload, group_id);
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

/* reg_ptce2_priority
 * Priority of the rule, higher values win. The range is 1..cap_kvd_size-1.
 * Note: priority does not have to be unique per rule.
 * Within a region, higher priority should have lower offset (no limitation
 * between regions in a multi-region).
 * Access: RW
 */
MLXSW_ITEM32(reg, ptce2, priority, 0x04, 0, 24);

/* reg_ptce2_tcam_region_info
 * Opaque object that represents the TCAM region.
 * Access: Index
 */
MLXSW_ITEM_BUF(reg, ptce2, tcam_region_info, 0x10,
	       MLXSW_REG_PXXX_TCAM_REGION_INFO_LEN);

#define MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN 96

/* reg_ptce2_flex_key_blocks
 * ACL Key.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, ptce2, flex_key_blocks, 0x20,
	       MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN);

/* reg_ptce2_mask
 * mask- in the same size as key. A bit that is set directs the TCAM
 * to compare the corresponding bit in key. A bit that is clear directs
 * the TCAM to ignore the corresponding bit in key.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, ptce2, mask, 0x80,
	       MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN);

/* reg_ptce2_flex_action_set
 * ACL action set.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, ptce2, flex_action_set, 0xE0,
	       MLXSW_REG_FLEX_ACTION_SET_LEN);

static inline void mlxsw_reg_ptce2_pack(char *payload, bool valid,
					enum mlxsw_reg_ptce2_op op,
					const char *tcam_region_info,
					u16 offset, u32 priority)
{
	MLXSW_REG_ZERO(ptce2, payload);
	mlxsw_reg_ptce2_v_set(payload, valid);
	mlxsw_reg_ptce2_op_set(payload, op);
	mlxsw_reg_ptce2_offset_set(payload, offset);
	mlxsw_reg_ptce2_priority_set(payload, priority);
	mlxsw_reg_ptce2_tcam_region_info_memcpy_to(payload, tcam_region_info);
}

/* PERPT - Policy-Engine ERP Table Register
 * ----------------------------------------
 * This register adds and removes eRPs from the eRP table.
 */
#define MLXSW_REG_PERPT_ID 0x3021
#define MLXSW_REG_PERPT_LEN 0x80

MLXSW_REG_DEFINE(perpt, MLXSW_REG_PERPT_ID, MLXSW_REG_PERPT_LEN);

/* reg_perpt_erpt_bank
 * eRP table bank.
 * Range 0 .. cap_max_erp_table_banks - 1
 * Access: Index
 */
MLXSW_ITEM32(reg, perpt, erpt_bank, 0x00, 16, 4);

/* reg_perpt_erpt_index
 * Index to eRP table within the eRP bank.
 * Range is 0 .. cap_max_erp_table_bank_size - 1
 * Access: Index
 */
MLXSW_ITEM32(reg, perpt, erpt_index, 0x00, 0, 8);

enum mlxsw_reg_perpt_key_size {
	MLXSW_REG_PERPT_KEY_SIZE_2KB,
	MLXSW_REG_PERPT_KEY_SIZE_4KB,
	MLXSW_REG_PERPT_KEY_SIZE_8KB,
	MLXSW_REG_PERPT_KEY_SIZE_12KB,
};

/* reg_perpt_key_size
 * Access: OP
 */
MLXSW_ITEM32(reg, perpt, key_size, 0x04, 0, 4);

/* reg_perpt_bf_bypass
 * 0 - The eRP is used only if bloom filter state is set for the given
 * rule.
 * 1 - The eRP is used regardless of bloom filter state.
 * The bypass is an OR condition of region_id or eRP. See PERCR.bf_bypass
 * Access: RW
 */
MLXSW_ITEM32(reg, perpt, bf_bypass, 0x08, 8, 1);

/* reg_perpt_erp_id
 * eRP ID for use by the rules.
 * Access: RW
 */
MLXSW_ITEM32(reg, perpt, erp_id, 0x08, 0, 4);

/* reg_perpt_erpt_base_bank
 * Base eRP table bank, points to head of erp_vector
 * Range is 0 .. cap_max_erp_table_banks - 1
 * Access: OP
 */
MLXSW_ITEM32(reg, perpt, erpt_base_bank, 0x0C, 16, 4);

/* reg_perpt_erpt_base_index
 * Base index to eRP table within the eRP bank
 * Range is 0 .. cap_max_erp_table_bank_size - 1
 * Access: OP
 */
MLXSW_ITEM32(reg, perpt, erpt_base_index, 0x0C, 0, 8);

/* reg_perpt_erp_index_in_vector
 * eRP index in the vector.
 * Access: OP
 */
MLXSW_ITEM32(reg, perpt, erp_index_in_vector, 0x10, 0, 4);

/* reg_perpt_erp_vector
 * eRP vector.
 * Access: OP
 */
MLXSW_ITEM_BIT_ARRAY(reg, perpt, erp_vector, 0x14, 4, 1);

/* reg_perpt_mask
 * Mask
 * 0 - A-TCAM will ignore the bit in key
 * 1 - A-TCAM will compare the bit in key
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, perpt, mask, 0x20, MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN);

static inline void mlxsw_reg_perpt_erp_vector_pack(char *payload,
						   unsigned long *erp_vector,
						   unsigned long size)
{
	unsigned long bit;

	for_each_set_bit(bit, erp_vector, size)
		mlxsw_reg_perpt_erp_vector_set(payload, bit, true);
}

static inline void
mlxsw_reg_perpt_pack(char *payload, u8 erpt_bank, u8 erpt_index,
		     enum mlxsw_reg_perpt_key_size key_size, u8 erp_id,
		     u8 erpt_base_bank, u8 erpt_base_index, u8 erp_index,
		     char *mask)
{
	MLXSW_REG_ZERO(perpt, payload);
	mlxsw_reg_perpt_erpt_bank_set(payload, erpt_bank);
	mlxsw_reg_perpt_erpt_index_set(payload, erpt_index);
	mlxsw_reg_perpt_key_size_set(payload, key_size);
	mlxsw_reg_perpt_bf_bypass_set(payload, false);
	mlxsw_reg_perpt_erp_id_set(payload, erp_id);
	mlxsw_reg_perpt_erpt_base_bank_set(payload, erpt_base_bank);
	mlxsw_reg_perpt_erpt_base_index_set(payload, erpt_base_index);
	mlxsw_reg_perpt_erp_index_in_vector_set(payload, erp_index);
	mlxsw_reg_perpt_mask_memcpy_to(payload, mask);
}

/* PERAR - Policy-Engine Region Association Register
 * -------------------------------------------------
 * This register associates a hw region for region_id's. Changing on the fly
 * is supported by the device.
 */
#define MLXSW_REG_PERAR_ID 0x3026
#define MLXSW_REG_PERAR_LEN 0x08

MLXSW_REG_DEFINE(perar, MLXSW_REG_PERAR_ID, MLXSW_REG_PERAR_LEN);

/* reg_perar_region_id
 * Region identifier
 * Range 0 .. cap_max_regions-1
 * Access: Index
 */
MLXSW_ITEM32(reg, perar, region_id, 0x00, 0, 16);

static inline unsigned int
mlxsw_reg_perar_hw_regions_needed(unsigned int block_num)
{
	return DIV_ROUND_UP(block_num, 4);
}

/* reg_perar_hw_region
 * HW Region
 * Range 0 .. cap_max_regions-1
 * Default: hw_region = region_id
 * For a 8 key block region, 2 consecutive regions are used
 * For a 12 key block region, 3 consecutive regions are used
 * Access: RW
 */
MLXSW_ITEM32(reg, perar, hw_region, 0x04, 0, 16);

static inline void mlxsw_reg_perar_pack(char *payload, u16 region_id,
					u16 hw_region)
{
	MLXSW_REG_ZERO(perar, payload);
	mlxsw_reg_perar_region_id_set(payload, region_id);
	mlxsw_reg_perar_hw_region_set(payload, hw_region);
}

/* PTCE-V3 - Policy-Engine TCAM Entry Register Version 3
 * -----------------------------------------------------
 * This register is a new version of PTCE-V2 in order to support the
 * A-TCAM. This register is not supported by SwitchX/-2 and Spectrum.
 */
#define MLXSW_REG_PTCE3_ID 0x3027
#define MLXSW_REG_PTCE3_LEN 0xF0

MLXSW_REG_DEFINE(ptce3, MLXSW_REG_PTCE3_ID, MLXSW_REG_PTCE3_LEN);

/* reg_ptce3_v
 * Valid.
 * Access: RW
 */
MLXSW_ITEM32(reg, ptce3, v, 0x00, 31, 1);

enum mlxsw_reg_ptce3_op {
	/* Write operation. Used to write a new entry to the table.
	 * All R/W fields are relevant for new entry. Activity bit is set
	 * for new entries. Write with v = 0 will delete the entry. Must
	 * not be used if an entry exists.
	 */
	 MLXSW_REG_PTCE3_OP_WRITE_WRITE = 0,
	 /* Update operation */
	 MLXSW_REG_PTCE3_OP_WRITE_UPDATE = 1,
	 /* Read operation */
	 MLXSW_REG_PTCE3_OP_QUERY_READ = 0,
};

/* reg_ptce3_op
 * Access: OP
 */
MLXSW_ITEM32(reg, ptce3, op, 0x00, 20, 3);

/* reg_ptce3_priority
 * Priority of the rule. Higher values win.
 * For Spectrum-2 range is 1..cap_kvd_size - 1
 * Note: Priority does not have to be unique per rule.
 * Access: RW
 */
MLXSW_ITEM32(reg, ptce3, priority, 0x04, 0, 24);

/* reg_ptce3_tcam_region_info
 * Opaque object that represents the TCAM region.
 * Access: Index
 */
MLXSW_ITEM_BUF(reg, ptce3, tcam_region_info, 0x10,
	       MLXSW_REG_PXXX_TCAM_REGION_INFO_LEN);

/* reg_ptce3_flex2_key_blocks
 * ACL key. The key must be masked according to eRP (if exists) or
 * according to master mask.
 * Access: Index
 */
MLXSW_ITEM_BUF(reg, ptce3, flex2_key_blocks, 0x20,
	       MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN);

/* reg_ptce3_erp_id
 * eRP ID.
 * Access: Index
 */
MLXSW_ITEM32(reg, ptce3, erp_id, 0x80, 0, 4);

/* reg_ptce3_delta_start
 * Start point of delta_value and delta_mask, in bits. Must not exceed
 * num_key_blocks * 36 - 8. Reserved when delta_mask = 0.
 * Access: Index
 */
MLXSW_ITEM32(reg, ptce3, delta_start, 0x84, 0, 10);

/* reg_ptce3_delta_mask
 * Delta mask.
 * 0 - Ignore relevant bit in delta_value
 * 1 - Compare relevant bit in delta_value
 * Delta mask must not be set for reserved fields in the key blocks.
 * Note: No delta when no eRPs. Thus, for regions with
 * PERERP.erpt_pointer_valid = 0 the delta mask must be 0.
 * Access: Index
 */
MLXSW_ITEM32(reg, ptce3, delta_mask, 0x88, 16, 8);

/* reg_ptce3_delta_value
 * Delta value.
 * Bits which are masked by delta_mask must be 0.
 * Access: Index
 */
MLXSW_ITEM32(reg, ptce3, delta_value, 0x88, 0, 8);

/* reg_ptce3_prune_vector
 * Pruning vector relative to the PERPT.erp_id.
 * Used for reducing lookups.
 * 0 - NEED: Do a lookup using the eRP.
 * 1 - PRUNE: Do not perform a lookup using the eRP.
 * Maybe be modified by PEAPBL and PEAPBM.
 * Note: In Spectrum-2, a region of 8 key blocks must be set to either
 * all 1's or all 0's.
 * Access: RW
 */
MLXSW_ITEM_BIT_ARRAY(reg, ptce3, prune_vector, 0x90, 4, 1);

/* reg_ptce3_prune_ctcam
 * Pruning on C-TCAM. Used for reducing lookups.
 * 0 - NEED: Do a lookup in the C-TCAM.
 * 1 - PRUNE: Do not perform a lookup in the C-TCAM.
 * Access: RW
 */
MLXSW_ITEM32(reg, ptce3, prune_ctcam, 0x94, 31, 1);

/* reg_ptce3_large_exists
 * Large entry key ID exists.
 * Within the region:
 * 0 - SINGLE: The large_entry_key_id is not currently in use.
 * For rule insert: The MSB of the key (blocks 6..11) will be added.
 * For rule delete: The MSB of the key will be removed.
 * 1 - NON_SINGLE: The large_entry_key_id is currently in use.
 * For rule insert: The MSB of the key (blocks 6..11) will not be added.
 * For rule delete: The MSB of the key will not be removed.
 * Access: WO
 */
MLXSW_ITEM32(reg, ptce3, large_exists, 0x98, 31, 1);

/* reg_ptce3_large_entry_key_id
 * Large entry key ID.
 * A key for 12 key blocks rules. Reserved when region has less than 12 key
 * blocks. Must be different for different keys which have the same common
 * 6 key blocks (MSB, blocks 6..11) key within a region.
 * Range is 0..cap_max_pe_large_key_id - 1
 * Access: RW
 */
MLXSW_ITEM32(reg, ptce3, large_entry_key_id, 0x98, 0, 24);

/* reg_ptce3_action_pointer
 * Pointer to action.
 * Range is 0..cap_max_kvd_action_sets - 1
 * Access: RW
 */
MLXSW_ITEM32(reg, ptce3, action_pointer, 0xA0, 0, 24);

static inline void mlxsw_reg_ptce3_pack(char *payload, bool valid,
					enum mlxsw_reg_ptce3_op op,
					u32 priority,
					const char *tcam_region_info,
					const char *key, u8 erp_id,
					u16 delta_start, u8 delta_mask,
					u8 delta_value, bool large_exists,
					u32 lkey_id, u32 action_pointer)
{
	MLXSW_REG_ZERO(ptce3, payload);
	mlxsw_reg_ptce3_v_set(payload, valid);
	mlxsw_reg_ptce3_op_set(payload, op);
	mlxsw_reg_ptce3_priority_set(payload, priority);
	mlxsw_reg_ptce3_tcam_region_info_memcpy_to(payload, tcam_region_info);
	mlxsw_reg_ptce3_flex2_key_blocks_memcpy_to(payload, key);
	mlxsw_reg_ptce3_erp_id_set(payload, erp_id);
	mlxsw_reg_ptce3_delta_start_set(payload, delta_start);
	mlxsw_reg_ptce3_delta_mask_set(payload, delta_mask);
	mlxsw_reg_ptce3_delta_value_set(payload, delta_value);
	mlxsw_reg_ptce3_large_exists_set(payload, large_exists);
	mlxsw_reg_ptce3_large_entry_key_id_set(payload, lkey_id);
	mlxsw_reg_ptce3_action_pointer_set(payload, action_pointer);
}

/* PERCR - Policy-Engine Region Configuration Register
 * ---------------------------------------------------
 * This register configures the region parameters. The region_id must be
 * allocated.
 */
#define MLXSW_REG_PERCR_ID 0x302A
#define MLXSW_REG_PERCR_LEN 0x80

MLXSW_REG_DEFINE(percr, MLXSW_REG_PERCR_ID, MLXSW_REG_PERCR_LEN);

/* reg_percr_region_id
 * Region identifier.
 * Range 0..cap_max_regions-1
 * Access: Index
 */
MLXSW_ITEM32(reg, percr, region_id, 0x00, 0, 16);

/* reg_percr_atcam_ignore_prune
 * Ignore prune_vector by other A-TCAM rules. Used e.g., for a new rule.
 * Access: RW
 */
MLXSW_ITEM32(reg, percr, atcam_ignore_prune, 0x04, 25, 1);

/* reg_percr_ctcam_ignore_prune
 * Ignore prune_ctcam by other A-TCAM rules. Used e.g., for a new rule.
 * Access: RW
 */
MLXSW_ITEM32(reg, percr, ctcam_ignore_prune, 0x04, 24, 1);

/* reg_percr_bf_bypass
 * Bloom filter bypass.
 * 0 - Bloom filter is used (default)
 * 1 - Bloom filter is bypassed. The bypass is an OR condition of
 * region_id or eRP. See PERPT.bf_bypass
 * Access: RW
 */
MLXSW_ITEM32(reg, percr, bf_bypass, 0x04, 16, 1);

/* reg_percr_master_mask
 * Master mask. Logical OR mask of all masks of all rules of a region
 * (both A-TCAM and C-TCAM). When there are no eRPs
 * (erpt_pointer_valid = 0), then this provides the mask.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, percr, master_mask, 0x20, 96);

static inline void mlxsw_reg_percr_pack(char *payload, u16 region_id)
{
	MLXSW_REG_ZERO(percr, payload);
	mlxsw_reg_percr_region_id_set(payload, region_id);
	mlxsw_reg_percr_atcam_ignore_prune_set(payload, false);
	mlxsw_reg_percr_ctcam_ignore_prune_set(payload, false);
	mlxsw_reg_percr_bf_bypass_set(payload, false);
}

/* PERERP - Policy-Engine Region eRP Register
 * ------------------------------------------
 * This register configures the region eRP. The region_id must be
 * allocated.
 */
#define MLXSW_REG_PERERP_ID 0x302B
#define MLXSW_REG_PERERP_LEN 0x1C

MLXSW_REG_DEFINE(pererp, MLXSW_REG_PERERP_ID, MLXSW_REG_PERERP_LEN);

/* reg_pererp_region_id
 * Region identifier.
 * Range 0..cap_max_regions-1
 * Access: Index
 */
MLXSW_ITEM32(reg, pererp, region_id, 0x00, 0, 16);

/* reg_pererp_ctcam_le
 * C-TCAM lookup enable. Reserved when erpt_pointer_valid = 0.
 * Access: RW
 */
MLXSW_ITEM32(reg, pererp, ctcam_le, 0x04, 28, 1);

/* reg_pererp_erpt_pointer_valid
 * erpt_pointer is valid.
 * Access: RW
 */
MLXSW_ITEM32(reg, pererp, erpt_pointer_valid, 0x10, 31, 1);

/* reg_pererp_erpt_bank_pointer
 * Pointer to eRP table bank. May be modified at any time.
 * Range 0..cap_max_erp_table_banks-1
 * Reserved when erpt_pointer_valid = 0
 */
MLXSW_ITEM32(reg, pererp, erpt_bank_pointer, 0x10, 16, 4);

/* reg_pererp_erpt_pointer
 * Pointer to eRP table within the eRP bank. Can be changed for an
 * existing region.
 * Range 0..cap_max_erp_table_size-1
 * Reserved when erpt_pointer_valid = 0
 * Access: RW
 */
MLXSW_ITEM32(reg, pererp, erpt_pointer, 0x10, 0, 8);

/* reg_pererp_erpt_vector
 * Vector of allowed eRP indexes starting from erpt_pointer within the
 * erpt_bank_pointer. Next entries will be in next bank.
 * Note that eRP index is used and not eRP ID.
 * Reserved when erpt_pointer_valid = 0
 * Access: RW
 */
MLXSW_ITEM_BIT_ARRAY(reg, pererp, erpt_vector, 0x14, 4, 1);

/* reg_pererp_master_rp_id
 * Master RP ID. When there are no eRPs, then this provides the eRP ID
 * for the lookup. Can be changed for an existing region.
 * Reserved when erpt_pointer_valid = 1
 * Access: RW
 */
MLXSW_ITEM32(reg, pererp, master_rp_id, 0x18, 0, 4);

static inline void mlxsw_reg_pererp_erp_vector_pack(char *payload,
						    unsigned long *erp_vector,
						    unsigned long size)
{
	unsigned long bit;

	for_each_set_bit(bit, erp_vector, size)
		mlxsw_reg_pererp_erpt_vector_set(payload, bit, true);
}

static inline void mlxsw_reg_pererp_pack(char *payload, u16 region_id,
					 bool ctcam_le, bool erpt_pointer_valid,
					 u8 erpt_bank_pointer, u8 erpt_pointer,
					 u8 master_rp_id)
{
	MLXSW_REG_ZERO(pererp, payload);
	mlxsw_reg_pererp_region_id_set(payload, region_id);
	mlxsw_reg_pererp_ctcam_le_set(payload, ctcam_le);
	mlxsw_reg_pererp_erpt_pointer_valid_set(payload, erpt_pointer_valid);
	mlxsw_reg_pererp_erpt_bank_pointer_set(payload, erpt_bank_pointer);
	mlxsw_reg_pererp_erpt_pointer_set(payload, erpt_pointer);
	mlxsw_reg_pererp_master_rp_id_set(payload, master_rp_id);
}

/* PEABFE - Policy-Engine Algorithmic Bloom Filter Entries Register
 * ----------------------------------------------------------------
 * This register configures the Bloom filter entries.
 */
#define MLXSW_REG_PEABFE_ID 0x3022
#define MLXSW_REG_PEABFE_BASE_LEN 0x10
#define MLXSW_REG_PEABFE_BF_REC_LEN 0x4
#define MLXSW_REG_PEABFE_BF_REC_MAX_COUNT 256
#define MLXSW_REG_PEABFE_LEN (MLXSW_REG_PEABFE_BASE_LEN + \
			      MLXSW_REG_PEABFE_BF_REC_LEN * \
			      MLXSW_REG_PEABFE_BF_REC_MAX_COUNT)

MLXSW_REG_DEFINE(peabfe, MLXSW_REG_PEABFE_ID, MLXSW_REG_PEABFE_LEN);

/* reg_peabfe_size
 * Number of BF entries to be updated.
 * Range 1..256
 * Access: Op
 */
MLXSW_ITEM32(reg, peabfe, size, 0x00, 0, 9);

/* reg_peabfe_bf_entry_state
 * Bloom filter state
 * 0 - Clear
 * 1 - Set
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, peabfe, bf_entry_state,
		     MLXSW_REG_PEABFE_BASE_LEN,	31, 1,
		     MLXSW_REG_PEABFE_BF_REC_LEN, 0x00, false);

/* reg_peabfe_bf_entry_bank
 * Bloom filter bank ID
 * Range 0..cap_max_erp_table_banks-1
 * Access: Index
 */
MLXSW_ITEM32_INDEXED(reg, peabfe, bf_entry_bank,
		     MLXSW_REG_PEABFE_BASE_LEN,	24, 4,
		     MLXSW_REG_PEABFE_BF_REC_LEN, 0x00, false);

/* reg_peabfe_bf_entry_index
 * Bloom filter entry index
 * Range 0..2^cap_max_bf_log-1
 * Access: Index
 */
MLXSW_ITEM32_INDEXED(reg, peabfe, bf_entry_index,
		     MLXSW_REG_PEABFE_BASE_LEN,	0, 24,
		     MLXSW_REG_PEABFE_BF_REC_LEN, 0x00, false);

static inline void mlxsw_reg_peabfe_pack(char *payload)
{
	MLXSW_REG_ZERO(peabfe, payload);
}

static inline void mlxsw_reg_peabfe_rec_pack(char *payload, int rec_index,
					     u8 state, u8 bank, u32 bf_index)
{
	u8 num_rec = mlxsw_reg_peabfe_size_get(payload);

	if (rec_index >= num_rec)
		mlxsw_reg_peabfe_size_set(payload, rec_index + 1);
	mlxsw_reg_peabfe_bf_entry_state_set(payload, rec_index, state);
	mlxsw_reg_peabfe_bf_entry_bank_set(payload, rec_index, bank);
	mlxsw_reg_peabfe_bf_entry_index_set(payload, rec_index, bf_index);
}

/* IEDR - Infrastructure Entry Delete Register
 * ----------------------------------------------------
 * This register is used for deleting entries from the entry tables.
 * It is legitimate to attempt to delete a nonexisting entry (the device will
 * respond as a good flow).
 */
#define MLXSW_REG_IEDR_ID 0x3804
#define MLXSW_REG_IEDR_BASE_LEN 0x10 /* base length, without records */
#define MLXSW_REG_IEDR_REC_LEN 0x8 /* record length */
#define MLXSW_REG_IEDR_REC_MAX_COUNT 64
#define MLXSW_REG_IEDR_LEN (MLXSW_REG_IEDR_BASE_LEN +	\
			    MLXSW_REG_IEDR_REC_LEN *	\
			    MLXSW_REG_IEDR_REC_MAX_COUNT)

MLXSW_REG_DEFINE(iedr, MLXSW_REG_IEDR_ID, MLXSW_REG_IEDR_LEN);

/* reg_iedr_num_rec
 * Number of records.
 * Access: OP
 */
MLXSW_ITEM32(reg, iedr, num_rec, 0x00, 0, 8);

/* reg_iedr_rec_type
 * Resource type.
 * Access: OP
 */
MLXSW_ITEM32_INDEXED(reg, iedr, rec_type, MLXSW_REG_IEDR_BASE_LEN, 24, 8,
		     MLXSW_REG_IEDR_REC_LEN, 0x00, false);

/* reg_iedr_rec_size
 * Size of entries do be deleted. The unit is 1 entry, regardless of entry type.
 * Access: OP
 */
MLXSW_ITEM32_INDEXED(reg, iedr, rec_size, MLXSW_REG_IEDR_BASE_LEN, 0, 13,
		     MLXSW_REG_IEDR_REC_LEN, 0x00, false);

/* reg_iedr_rec_index_start
 * Resource index start.
 * Access: OP
 */
MLXSW_ITEM32_INDEXED(reg, iedr, rec_index_start, MLXSW_REG_IEDR_BASE_LEN, 0, 24,
		     MLXSW_REG_IEDR_REC_LEN, 0x04, false);

static inline void mlxsw_reg_iedr_pack(char *payload)
{
	MLXSW_REG_ZERO(iedr, payload);
}

static inline void mlxsw_reg_iedr_rec_pack(char *payload, int rec_index,
					   u8 rec_type, u16 rec_size,
					   u32 rec_index_start)
{
	u8 num_rec = mlxsw_reg_iedr_num_rec_get(payload);

	if (rec_index >= num_rec)
		mlxsw_reg_iedr_num_rec_set(payload, rec_index + 1);
	mlxsw_reg_iedr_rec_type_set(payload, rec_index, rec_type);
	mlxsw_reg_iedr_rec_size_set(payload, rec_index, rec_size);
	mlxsw_reg_iedr_rec_index_start_set(payload, rec_index, rec_index_start);
}

/* QPTS - QoS Priority Trust State Register
 * ----------------------------------------
 * This register controls the port policy to calculate the switch priority and
 * packet color based on incoming packet fields.
 */
#define MLXSW_REG_QPTS_ID 0x4002
#define MLXSW_REG_QPTS_LEN 0x8

MLXSW_REG_DEFINE(qpts, MLXSW_REG_QPTS_ID, MLXSW_REG_QPTS_LEN);

/* reg_qpts_local_port
 * Local port number.
 * Access: Index
 *
 * Note: CPU port is supported.
 */
MLXSW_ITEM32_LP(reg, qpts, 0x00, 16, 0x00, 12);

enum mlxsw_reg_qpts_trust_state {
	MLXSW_REG_QPTS_TRUST_STATE_PCP = 1,
	MLXSW_REG_QPTS_TRUST_STATE_DSCP = 2, /* For MPLS, trust EXP. */
};

/* reg_qpts_trust_state
 * Trust state for a given port.
 * Access: RW
 */
MLXSW_ITEM32(reg, qpts, trust_state, 0x04, 0, 3);

static inline void mlxsw_reg_qpts_pack(char *payload, u16 local_port,
				       enum mlxsw_reg_qpts_trust_state ts)
{
	MLXSW_REG_ZERO(qpts, payload);

	mlxsw_reg_qpts_local_port_set(payload, local_port);
	mlxsw_reg_qpts_trust_state_set(payload, ts);
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

/* reg_qpcr_clear_counter
 * Clear counters.
 * Access: OP
 */
MLXSW_ITEM32(reg, qpcr, clear_counter, 0x04, 31, 1);

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

/* reg_qpcr_violate_count
 * Counts the number of times violate_action happened on this PID.
 * Access: RW
 */
MLXSW_ITEM64(reg, qpcr, violate_count, 0x20, 0, 64);

/* Packets */
#define MLXSW_REG_QPCR_LOWEST_CIR	1
#define MLXSW_REG_QPCR_HIGHEST_CIR	(2 * 1000 * 1000 * 1000) /* 2Gpps */
#define MLXSW_REG_QPCR_LOWEST_CBS	4
#define MLXSW_REG_QPCR_HIGHEST_CBS	24

/* Bandwidth */
#define MLXSW_REG_QPCR_LOWEST_CIR_BITS		1024 /* bps */
#define MLXSW_REG_QPCR_HIGHEST_CIR_BITS		2000000000000ULL /* 2Tbps */
#define MLXSW_REG_QPCR_LOWEST_CBS_BITS_SP1	4
#define MLXSW_REG_QPCR_LOWEST_CBS_BITS_SP2	4
#define MLXSW_REG_QPCR_HIGHEST_CBS_BITS_SP1	25
#define MLXSW_REG_QPCR_HIGHEST_CBS_BITS_SP2	31

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
MLXSW_ITEM32_LP(reg, qtct, 0x00, 16, 0x00, 12);

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

static inline void mlxsw_reg_qtct_pack(char *payload, u16 local_port,
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
#define MLXSW_REG_QEEC_LEN 0x20

MLXSW_REG_DEFINE(qeec, MLXSW_REG_QEEC_ID, MLXSW_REG_QEEC_LEN);

/* reg_qeec_local_port
 * Local port number.
 * Access: Index
 *
 * Note: CPU port is supported.
 */
MLXSW_ITEM32_LP(reg, qeec, 0x00, 16, 0x00, 12);

enum mlxsw_reg_qeec_hr {
	MLXSW_REG_QEEC_HR_PORT,
	MLXSW_REG_QEEC_HR_GROUP,
	MLXSW_REG_QEEC_HR_SUBGROUP,
	MLXSW_REG_QEEC_HR_TC,
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

/* reg_qeec_mise
 * Min shaper configuration enable. Enables configuration of the min
 * shaper on this ETS element
 * 0 - Disable
 * 1 - Enable
 * Access: RW
 */
MLXSW_ITEM32(reg, qeec, mise, 0x0C, 31, 1);

/* reg_qeec_ptps
 * PTP shaper
 * 0: regular shaper mode
 * 1: PTP oriented shaper
 * Allowed only for hierarchy 0
 * Not supported for CPU port
 * Note that ptps mode may affect the shaper rates of all hierarchies
 * Supported only on Spectrum-1
 * Access: RW
 */
MLXSW_ITEM32(reg, qeec, ptps, 0x0C, 29, 1);

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

/* The smallest permitted min shaper rate. */
#define MLXSW_REG_QEEC_MIS_MIN	200000		/* Kbps */

/* reg_qeec_min_shaper_rate
 * Min shaper information rate.
 * For CPU port, can only be configured for port hierarchy.
 * When in bytes mode, value is specified in units of 1000bps.
 * Access: RW
 */
MLXSW_ITEM32(reg, qeec, min_shaper_rate, 0x0C, 0, 28);

/* reg_qeec_mase
 * Max shaper configuration enable. Enables configuration of the max
 * shaper on this ETS element.
 * 0 - Disable
 * 1 - Enable
 * Access: RW
 */
MLXSW_ITEM32(reg, qeec, mase, 0x10, 31, 1);

/* The largest max shaper value possible to disable the shaper. */
#define MLXSW_REG_QEEC_MAS_DIS	((1u << 31) - 1)	/* Kbps */

/* reg_qeec_max_shaper_rate
 * Max shaper information rate.
 * For CPU port, can only be configured for port hierarchy.
 * When in bytes mode, value is specified in units of 1000bps.
 * Access: RW
 */
MLXSW_ITEM32(reg, qeec, max_shaper_rate, 0x10, 0, 31);

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

/* reg_qeec_max_shaper_bs
 * Max shaper burst size
 * Burst size is 2^max_shaper_bs * 512 bits
 * For Spectrum-1: Range is: 5..25
 * For Spectrum-2: Range is: 11..25
 * Reserved when ptps = 1
 * Access: RW
 */
MLXSW_ITEM32(reg, qeec, max_shaper_bs, 0x1C, 0, 6);

#define MLXSW_REG_QEEC_HIGHEST_SHAPER_BS	25
#define MLXSW_REG_QEEC_LOWEST_SHAPER_BS_SP1	5
#define MLXSW_REG_QEEC_LOWEST_SHAPER_BS_SP2	11
#define MLXSW_REG_QEEC_LOWEST_SHAPER_BS_SP3	11
#define MLXSW_REG_QEEC_LOWEST_SHAPER_BS_SP4	11

static inline void mlxsw_reg_qeec_pack(char *payload, u16 local_port,
				       enum mlxsw_reg_qeec_hr hr, u8 index,
				       u8 next_index)
{
	MLXSW_REG_ZERO(qeec, payload);
	mlxsw_reg_qeec_local_port_set(payload, local_port);
	mlxsw_reg_qeec_element_hierarchy_set(payload, hr);
	mlxsw_reg_qeec_element_index_set(payload, index);
	mlxsw_reg_qeec_next_element_index_set(payload, next_index);
}

static inline void mlxsw_reg_qeec_ptps_pack(char *payload, u16 local_port,
					    bool ptps)
{
	MLXSW_REG_ZERO(qeec, payload);
	mlxsw_reg_qeec_local_port_set(payload, local_port);
	mlxsw_reg_qeec_element_hierarchy_set(payload, MLXSW_REG_QEEC_HR_PORT);
	mlxsw_reg_qeec_ptps_set(payload, ptps);
}

/* QRWE - QoS ReWrite Enable
 * -------------------------
 * This register configures the rewrite enable per receive port.
 */
#define MLXSW_REG_QRWE_ID 0x400F
#define MLXSW_REG_QRWE_LEN 0x08

MLXSW_REG_DEFINE(qrwe, MLXSW_REG_QRWE_ID, MLXSW_REG_QRWE_LEN);

/* reg_qrwe_local_port
 * Local port number.
 * Access: Index
 *
 * Note: CPU port is supported. No support for router port.
 */
MLXSW_ITEM32_LP(reg, qrwe, 0x00, 16, 0x00, 12);

/* reg_qrwe_dscp
 * Whether to enable DSCP rewrite (default is 0, don't rewrite).
 * Access: RW
 */
MLXSW_ITEM32(reg, qrwe, dscp, 0x04, 1, 1);

/* reg_qrwe_pcp
 * Whether to enable PCP and DEI rewrite (default is 0, don't rewrite).
 * Access: RW
 */
MLXSW_ITEM32(reg, qrwe, pcp, 0x04, 0, 1);

static inline void mlxsw_reg_qrwe_pack(char *payload, u16 local_port,
				       bool rewrite_pcp, bool rewrite_dscp)
{
	MLXSW_REG_ZERO(qrwe, payload);
	mlxsw_reg_qrwe_local_port_set(payload, local_port);
	mlxsw_reg_qrwe_pcp_set(payload, rewrite_pcp);
	mlxsw_reg_qrwe_dscp_set(payload, rewrite_dscp);
}

/* QPDSM - QoS Priority to DSCP Mapping
 * ------------------------------------
 * QoS Priority to DSCP Mapping Register
 */
#define MLXSW_REG_QPDSM_ID 0x4011
#define MLXSW_REG_QPDSM_BASE_LEN 0x04 /* base length, without records */
#define MLXSW_REG_QPDSM_PRIO_ENTRY_REC_LEN 0x4 /* record length */
#define MLXSW_REG_QPDSM_PRIO_ENTRY_REC_MAX_COUNT 16
#define MLXSW_REG_QPDSM_LEN (MLXSW_REG_QPDSM_BASE_LEN +			\
			     MLXSW_REG_QPDSM_PRIO_ENTRY_REC_LEN *	\
			     MLXSW_REG_QPDSM_PRIO_ENTRY_REC_MAX_COUNT)

MLXSW_REG_DEFINE(qpdsm, MLXSW_REG_QPDSM_ID, MLXSW_REG_QPDSM_LEN);

/* reg_qpdsm_local_port
 * Local Port. Supported for data packets from CPU port.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, qpdsm, 0x00, 16, 0x00, 12);

/* reg_qpdsm_prio_entry_color0_e
 * Enable update of the entry for color 0 and a given port.
 * Access: WO
 */
MLXSW_ITEM32_INDEXED(reg, qpdsm, prio_entry_color0_e,
		     MLXSW_REG_QPDSM_BASE_LEN, 31, 1,
		     MLXSW_REG_QPDSM_PRIO_ENTRY_REC_LEN, 0x00, false);

/* reg_qpdsm_prio_entry_color0_dscp
 * DSCP field in the outer label of the packet for color 0 and a given port.
 * Reserved when e=0.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, qpdsm, prio_entry_color0_dscp,
		     MLXSW_REG_QPDSM_BASE_LEN, 24, 6,
		     MLXSW_REG_QPDSM_PRIO_ENTRY_REC_LEN, 0x00, false);

/* reg_qpdsm_prio_entry_color1_e
 * Enable update of the entry for color 1 and a given port.
 * Access: WO
 */
MLXSW_ITEM32_INDEXED(reg, qpdsm, prio_entry_color1_e,
		     MLXSW_REG_QPDSM_BASE_LEN, 23, 1,
		     MLXSW_REG_QPDSM_PRIO_ENTRY_REC_LEN, 0x00, false);

/* reg_qpdsm_prio_entry_color1_dscp
 * DSCP field in the outer label of the packet for color 1 and a given port.
 * Reserved when e=0.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, qpdsm, prio_entry_color1_dscp,
		     MLXSW_REG_QPDSM_BASE_LEN, 16, 6,
		     MLXSW_REG_QPDSM_PRIO_ENTRY_REC_LEN, 0x00, false);

/* reg_qpdsm_prio_entry_color2_e
 * Enable update of the entry for color 2 and a given port.
 * Access: WO
 */
MLXSW_ITEM32_INDEXED(reg, qpdsm, prio_entry_color2_e,
		     MLXSW_REG_QPDSM_BASE_LEN, 15, 1,
		     MLXSW_REG_QPDSM_PRIO_ENTRY_REC_LEN, 0x00, false);

/* reg_qpdsm_prio_entry_color2_dscp
 * DSCP field in the outer label of the packet for color 2 and a given port.
 * Reserved when e=0.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, qpdsm, prio_entry_color2_dscp,
		     MLXSW_REG_QPDSM_BASE_LEN, 8, 6,
		     MLXSW_REG_QPDSM_PRIO_ENTRY_REC_LEN, 0x00, false);

static inline void mlxsw_reg_qpdsm_pack(char *payload, u16 local_port)
{
	MLXSW_REG_ZERO(qpdsm, payload);
	mlxsw_reg_qpdsm_local_port_set(payload, local_port);
}

static inline void
mlxsw_reg_qpdsm_prio_pack(char *payload, unsigned short prio, u8 dscp)
{
	mlxsw_reg_qpdsm_prio_entry_color0_e_set(payload, prio, 1);
	mlxsw_reg_qpdsm_prio_entry_color0_dscp_set(payload, prio, dscp);
	mlxsw_reg_qpdsm_prio_entry_color1_e_set(payload, prio, 1);
	mlxsw_reg_qpdsm_prio_entry_color1_dscp_set(payload, prio, dscp);
	mlxsw_reg_qpdsm_prio_entry_color2_e_set(payload, prio, 1);
	mlxsw_reg_qpdsm_prio_entry_color2_dscp_set(payload, prio, dscp);
}

/* QPDP - QoS Port DSCP to Priority Mapping Register
 * -------------------------------------------------
 * This register controls the port default Switch Priority and Color. The
 * default Switch Priority and Color are used for frames where the trust state
 * uses default values. All member ports of a LAG should be configured with the
 * same default values.
 */
#define MLXSW_REG_QPDP_ID 0x4007
#define MLXSW_REG_QPDP_LEN 0x8

MLXSW_REG_DEFINE(qpdp, MLXSW_REG_QPDP_ID, MLXSW_REG_QPDP_LEN);

/* reg_qpdp_local_port
 * Local Port. Supported for data packets from CPU port.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, qpdp, 0x00, 16, 0x00, 12);

/* reg_qpdp_switch_prio
 * Default port Switch Priority (default 0)
 * Access: RW
 */
MLXSW_ITEM32(reg, qpdp, switch_prio, 0x04, 0, 4);

static inline void mlxsw_reg_qpdp_pack(char *payload, u16 local_port,
				       u8 switch_prio)
{
	MLXSW_REG_ZERO(qpdp, payload);
	mlxsw_reg_qpdp_local_port_set(payload, local_port);
	mlxsw_reg_qpdp_switch_prio_set(payload, switch_prio);
}

/* QPDPM - QoS Port DSCP to Priority Mapping Register
 * --------------------------------------------------
 * This register controls the mapping from DSCP field to
 * Switch Priority for IP packets.
 */
#define MLXSW_REG_QPDPM_ID 0x4013
#define MLXSW_REG_QPDPM_BASE_LEN 0x4 /* base length, without records */
#define MLXSW_REG_QPDPM_DSCP_ENTRY_REC_LEN 0x2 /* record length */
#define MLXSW_REG_QPDPM_DSCP_ENTRY_REC_MAX_COUNT 64
#define MLXSW_REG_QPDPM_LEN (MLXSW_REG_QPDPM_BASE_LEN +			\
			     MLXSW_REG_QPDPM_DSCP_ENTRY_REC_LEN *	\
			     MLXSW_REG_QPDPM_DSCP_ENTRY_REC_MAX_COUNT)

MLXSW_REG_DEFINE(qpdpm, MLXSW_REG_QPDPM_ID, MLXSW_REG_QPDPM_LEN);

/* reg_qpdpm_local_port
 * Local Port. Supported for data packets from CPU port.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, qpdpm, 0x00, 16, 0x00, 12);

/* reg_qpdpm_dscp_e
 * Enable update of the specific entry. When cleared, the switch_prio and color
 * fields are ignored and the previous switch_prio and color values are
 * preserved.
 * Access: WO
 */
MLXSW_ITEM16_INDEXED(reg, qpdpm, dscp_entry_e, MLXSW_REG_QPDPM_BASE_LEN, 15, 1,
		     MLXSW_REG_QPDPM_DSCP_ENTRY_REC_LEN, 0x00, false);

/* reg_qpdpm_dscp_prio
 * The new Switch Priority value for the relevant DSCP value.
 * Access: RW
 */
MLXSW_ITEM16_INDEXED(reg, qpdpm, dscp_entry_prio,
		     MLXSW_REG_QPDPM_BASE_LEN, 0, 4,
		     MLXSW_REG_QPDPM_DSCP_ENTRY_REC_LEN, 0x00, false);

static inline void mlxsw_reg_qpdpm_pack(char *payload, u16 local_port)
{
	MLXSW_REG_ZERO(qpdpm, payload);
	mlxsw_reg_qpdpm_local_port_set(payload, local_port);
}

static inline void
mlxsw_reg_qpdpm_dscp_pack(char *payload, unsigned short dscp, u8 prio)
{
	mlxsw_reg_qpdpm_dscp_entry_e_set(payload, dscp, 1);
	mlxsw_reg_qpdpm_dscp_entry_prio_set(payload, dscp, prio);
}

/* QTCTM - QoS Switch Traffic Class Table is Multicast-Aware Register
 * ------------------------------------------------------------------
 * This register configures if the Switch Priority to Traffic Class mapping is
 * based on Multicast packet indication. If so, then multicast packets will get
 * a Traffic Class that is plus (cap_max_tclass_data/2) the value configured by
 * QTCT.
 * By default, Switch Priority to Traffic Class mapping is not based on
 * Multicast packet indication.
 */
#define MLXSW_REG_QTCTM_ID 0x401A
#define MLXSW_REG_QTCTM_LEN 0x08

MLXSW_REG_DEFINE(qtctm, MLXSW_REG_QTCTM_ID, MLXSW_REG_QTCTM_LEN);

/* reg_qtctm_local_port
 * Local port number.
 * No support for CPU port.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, qtctm, 0x00, 16, 0x00, 12);

/* reg_qtctm_mc
 * Multicast Mode
 * Whether Switch Priority to Traffic Class mapping is based on Multicast packet
 * indication (default is 0, not based on Multicast packet indication).
 */
MLXSW_ITEM32(reg, qtctm, mc, 0x04, 0, 1);

static inline void
mlxsw_reg_qtctm_pack(char *payload, u16 local_port, bool mc)
{
	MLXSW_REG_ZERO(qtctm, payload);
	mlxsw_reg_qtctm_local_port_set(payload, local_port);
	mlxsw_reg_qtctm_mc_set(payload, mc);
}

/* QPSC - QoS PTP Shaper Configuration Register
 * --------------------------------------------
 * The QPSC allows advanced configuration of the shapers when QEEC.ptps=1.
 * Supported only on Spectrum-1.
 */
#define MLXSW_REG_QPSC_ID 0x401B
#define MLXSW_REG_QPSC_LEN 0x28

MLXSW_REG_DEFINE(qpsc, MLXSW_REG_QPSC_ID, MLXSW_REG_QPSC_LEN);

enum mlxsw_reg_qpsc_port_speed {
	MLXSW_REG_QPSC_PORT_SPEED_100M,
	MLXSW_REG_QPSC_PORT_SPEED_1G,
	MLXSW_REG_QPSC_PORT_SPEED_10G,
	MLXSW_REG_QPSC_PORT_SPEED_25G,
};

/* reg_qpsc_port_speed
 * Port speed.
 * Access: Index
 */
MLXSW_ITEM32(reg, qpsc, port_speed, 0x00, 0, 4);

/* reg_qpsc_shaper_time_exp
 * The base-time-interval for updating the shapers tokens (for all hierarchies).
 * shaper_update_rate = 2 ^ shaper_time_exp * (1 + shaper_time_mantissa) * 32nSec
 * shaper_rate = 64bit * shaper_inc / shaper_update_rate
 * Access: RW
 */
MLXSW_ITEM32(reg, qpsc, shaper_time_exp, 0x04, 16, 4);

/* reg_qpsc_shaper_time_mantissa
 * The base-time-interval for updating the shapers tokens (for all hierarchies).
 * shaper_update_rate = 2 ^ shaper_time_exp * (1 + shaper_time_mantissa) * 32nSec
 * shaper_rate = 64bit * shaper_inc / shaper_update_rate
 * Access: RW
 */
MLXSW_ITEM32(reg, qpsc, shaper_time_mantissa, 0x04, 0, 5);

/* reg_qpsc_shaper_inc
 * Number of tokens added to shaper on each update.
 * Units of 8B.
 * Access: RW
 */
MLXSW_ITEM32(reg, qpsc, shaper_inc, 0x08, 0, 5);

/* reg_qpsc_shaper_bs
 * Max shaper Burst size.
 * Burst size is 2 ^ max_shaper_bs * 512 [bits]
 * Range is: 5..25 (from 2KB..2GB)
 * Access: RW
 */
MLXSW_ITEM32(reg, qpsc, shaper_bs, 0x0C, 0, 6);

/* reg_qpsc_ptsc_we
 * Write enable to port_to_shaper_credits.
 * Access: WO
 */
MLXSW_ITEM32(reg, qpsc, ptsc_we, 0x10, 31, 1);

/* reg_qpsc_port_to_shaper_credits
 * For split ports: range 1..57
 * For non-split ports: range 1..112
 * Written only when ptsc_we is set.
 * Access: RW
 */
MLXSW_ITEM32(reg, qpsc, port_to_shaper_credits, 0x10, 0, 8);

/* reg_qpsc_ing_timestamp_inc
 * Ingress timestamp increment.
 * 2's complement.
 * The timestamp of MTPPTR at ingress will be incremented by this value. Global
 * value for all ports.
 * Same units as used by MTPPTR.
 * Access: RW
 */
MLXSW_ITEM32(reg, qpsc, ing_timestamp_inc, 0x20, 0, 32);

/* reg_qpsc_egr_timestamp_inc
 * Egress timestamp increment.
 * 2's complement.
 * The timestamp of MTPPTR at egress will be incremented by this value. Global
 * value for all ports.
 * Same units as used by MTPPTR.
 * Access: RW
 */
MLXSW_ITEM32(reg, qpsc, egr_timestamp_inc, 0x24, 0, 32);

static inline void
mlxsw_reg_qpsc_pack(char *payload, enum mlxsw_reg_qpsc_port_speed port_speed,
		    u8 shaper_time_exp, u8 shaper_time_mantissa, u8 shaper_inc,
		    u8 shaper_bs, u8 port_to_shaper_credits,
		    int ing_timestamp_inc, int egr_timestamp_inc)
{
	MLXSW_REG_ZERO(qpsc, payload);
	mlxsw_reg_qpsc_port_speed_set(payload, port_speed);
	mlxsw_reg_qpsc_shaper_time_exp_set(payload, shaper_time_exp);
	mlxsw_reg_qpsc_shaper_time_mantissa_set(payload, shaper_time_mantissa);
	mlxsw_reg_qpsc_shaper_inc_set(payload, shaper_inc);
	mlxsw_reg_qpsc_shaper_bs_set(payload, shaper_bs);
	mlxsw_reg_qpsc_ptsc_we_set(payload, true);
	mlxsw_reg_qpsc_port_to_shaper_credits_set(payload, port_to_shaper_credits);
	mlxsw_reg_qpsc_ing_timestamp_inc_set(payload, ing_timestamp_inc);
	mlxsw_reg_qpsc_egr_timestamp_inc_set(payload, egr_timestamp_inc);
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
MLXSW_ITEM32_LP(reg, pmlp, 0x00, 16, 0x00, 12);

/* reg_pmlp_width
 * 0 - Unmap local port.
 * 1 - Lane 0 is used.
 * 2 - Lanes 0 and 1 are used.
 * 4 - Lanes 0, 1, 2 and 3 are used.
 * 8 - Lanes 0-7 are used.
 * Access: RW
 */
MLXSW_ITEM32(reg, pmlp, width, 0x00, 0, 8);

/* reg_pmlp_module
 * Module number.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, pmlp, module, 0x04, 0, 8, 0x04, 0x00, false);

/* reg_pmlp_slot_index
 * Module number.
 * Slot_index
 * Slot_index = 0 represent the onboard (motherboard).
 * In case of non-modular system only slot_index = 0 is available.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, pmlp, slot_index, 0x04, 8, 4, 0x04, 0x00, false);

/* reg_pmlp_tx_lane
 * Tx Lane. When rxtx field is cleared, this field is used for Rx as well.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, pmlp, tx_lane, 0x04, 16, 4, 0x04, 0x00, false);

/* reg_pmlp_rx_lane
 * Rx Lane. When rxtx field is cleared, this field is ignored and Rx lane is
 * equal to Tx lane.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, pmlp, rx_lane, 0x04, 24, 4, 0x04, 0x00, false);

static inline void mlxsw_reg_pmlp_pack(char *payload, u16 local_port)
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
MLXSW_ITEM32_LP(reg, pmtu, 0x00, 16, 0x00, 12);

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

static inline void mlxsw_reg_pmtu_pack(char *payload, u16 local_port,
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

/* an_disable_admin
 * Auto negotiation disable administrative configuration
 * 0 - Device doesn't support AN disable.
 * 1 - Device supports AN disable.
 * Access: RW
 */
MLXSW_ITEM32(reg, ptys, an_disable_admin, 0x00, 30, 1);

/* reg_ptys_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, ptys, 0x00, 16, 0x00, 12);

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

#define MLXSW_REG_PTYS_EXT_ETH_SPEED_SGMII_100M				BIT(0)
#define MLXSW_REG_PTYS_EXT_ETH_SPEED_1000BASE_X_SGMII			BIT(1)
#define MLXSW_REG_PTYS_EXT_ETH_SPEED_5GBASE_R				BIT(3)
#define MLXSW_REG_PTYS_EXT_ETH_SPEED_XFI_XAUI_1_10G			BIT(4)
#define MLXSW_REG_PTYS_EXT_ETH_SPEED_XLAUI_4_XLPPI_4_40G		BIT(5)
#define MLXSW_REG_PTYS_EXT_ETH_SPEED_25GAUI_1_25GBASE_CR_KR		BIT(6)
#define MLXSW_REG_PTYS_EXT_ETH_SPEED_50GAUI_2_LAUI_2_50GBASE_CR2_KR2	BIT(7)
#define MLXSW_REG_PTYS_EXT_ETH_SPEED_50GAUI_1_LAUI_1_50GBASE_CR_KR	BIT(8)
#define MLXSW_REG_PTYS_EXT_ETH_SPEED_CAUI_4_100GBASE_CR4_KR4		BIT(9)
#define MLXSW_REG_PTYS_EXT_ETH_SPEED_100GAUI_2_100GBASE_CR2_KR2		BIT(10)
#define MLXSW_REG_PTYS_EXT_ETH_SPEED_200GAUI_4_200GBASE_CR4_KR4		BIT(12)
#define MLXSW_REG_PTYS_EXT_ETH_SPEED_400GAUI_8				BIT(15)

/* reg_ptys_ext_eth_proto_cap
 * Extended Ethernet port supported speeds and protocols.
 * Access: RO
 */
MLXSW_ITEM32(reg, ptys, ext_eth_proto_cap, 0x08, 0, 32);

#define MLXSW_REG_PTYS_ETH_SPEED_SGMII			BIT(0)
#define MLXSW_REG_PTYS_ETH_SPEED_1000BASE_KX		BIT(1)
#define MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CX4		BIT(2)
#define MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KX4		BIT(3)
#define MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KR		BIT(4)
#define MLXSW_REG_PTYS_ETH_SPEED_40GBASE_CR4		BIT(6)
#define MLXSW_REG_PTYS_ETH_SPEED_40GBASE_KR4		BIT(7)
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
#define MLXSW_REG_PTYS_ETH_SPEED_100BASE_T		BIT(24)
#define MLXSW_REG_PTYS_ETH_SPEED_1000BASE_T		BIT(25)
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

/* reg_ptys_ext_eth_proto_admin
 * Extended speed and protocol to set port to.
 * Access: RW
 */
MLXSW_ITEM32(reg, ptys, ext_eth_proto_admin, 0x14, 0, 32);

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

/* reg_ptys_ext_eth_proto_oper
 * The extended current speed and protocol configured for the port.
 * Access: RO
 */
MLXSW_ITEM32(reg, ptys, ext_eth_proto_oper, 0x20, 0, 32);

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

enum mlxsw_reg_ptys_connector_type {
	MLXSW_REG_PTYS_CONNECTOR_TYPE_UNKNOWN_OR_NO_CONNECTOR,
	MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_NONE,
	MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_TP,
	MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_AUI,
	MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_BNC,
	MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_MII,
	MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_FIBRE,
	MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_DA,
	MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_OTHER,
};

/* reg_ptys_connector_type
 * Connector type indication.
 * Access: RO
 */
MLXSW_ITEM32(reg, ptys, connector_type, 0x2C, 0, 4);

static inline void mlxsw_reg_ptys_eth_pack(char *payload, u16 local_port,
					   u32 proto_admin, bool autoneg)
{
	MLXSW_REG_ZERO(ptys, payload);
	mlxsw_reg_ptys_local_port_set(payload, local_port);
	mlxsw_reg_ptys_proto_mask_set(payload, MLXSW_REG_PTYS_PROTO_MASK_ETH);
	mlxsw_reg_ptys_eth_proto_admin_set(payload, proto_admin);
	mlxsw_reg_ptys_an_disable_admin_set(payload, !autoneg);
}

static inline void mlxsw_reg_ptys_ext_eth_pack(char *payload, u16 local_port,
					       u32 proto_admin, bool autoneg)
{
	MLXSW_REG_ZERO(ptys, payload);
	mlxsw_reg_ptys_local_port_set(payload, local_port);
	mlxsw_reg_ptys_proto_mask_set(payload, MLXSW_REG_PTYS_PROTO_MASK_ETH);
	mlxsw_reg_ptys_ext_eth_proto_admin_set(payload, proto_admin);
	mlxsw_reg_ptys_an_disable_admin_set(payload, !autoneg);
}

static inline void mlxsw_reg_ptys_eth_unpack(char *payload,
					     u32 *p_eth_proto_cap,
					     u32 *p_eth_proto_admin,
					     u32 *p_eth_proto_oper)
{
	if (p_eth_proto_cap)
		*p_eth_proto_cap =
			mlxsw_reg_ptys_eth_proto_cap_get(payload);
	if (p_eth_proto_admin)
		*p_eth_proto_admin =
			mlxsw_reg_ptys_eth_proto_admin_get(payload);
	if (p_eth_proto_oper)
		*p_eth_proto_oper =
			mlxsw_reg_ptys_eth_proto_oper_get(payload);
}

static inline void mlxsw_reg_ptys_ext_eth_unpack(char *payload,
						 u32 *p_eth_proto_cap,
						 u32 *p_eth_proto_admin,
						 u32 *p_eth_proto_oper)
{
	if (p_eth_proto_cap)
		*p_eth_proto_cap =
			mlxsw_reg_ptys_ext_eth_proto_cap_get(payload);
	if (p_eth_proto_admin)
		*p_eth_proto_admin =
			mlxsw_reg_ptys_ext_eth_proto_admin_get(payload);
	if (p_eth_proto_oper)
		*p_eth_proto_oper =
			mlxsw_reg_ptys_ext_eth_proto_oper_get(payload);
}

static inline void mlxsw_reg_ptys_ib_pack(char *payload, u16 local_port,
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
MLXSW_ITEM32_LP(reg, ppad, 0x00, 16, 0x00, 24);

/* reg_ppad_mac
 * If single_base_mac = 0 - base MAC address, mac[7:0] is reserved.
 * If single_base_mac = 1 - the per port MAC address
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, ppad, mac, 0x02, 6);

static inline void mlxsw_reg_ppad_pack(char *payload, bool single_base_mac,
				       u16 local_port)
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
MLXSW_ITEM32_LP(reg, paos, 0x00, 16, 0x00, 12);

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

static inline void mlxsw_reg_paos_pack(char *payload, u16 local_port,
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
MLXSW_ITEM32_LP(reg, pfcc, 0x00, 16, 0x00, 12);

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

static inline void mlxsw_reg_pfcc_pack(char *payload, u16 local_port)
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
#define MLXSW_REG_PPCNT_COUNTERS_OFFSET 0x08

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
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, ppcnt, 0x00, 16, 0x00, 12);

/* reg_ppcnt_pnat
 * Port number access type:
 * 0 - Local port number
 * 1 - IB port number
 * Access: Index
 */
MLXSW_ITEM32(reg, ppcnt, pnat, 0x00, 14, 2);

enum mlxsw_reg_ppcnt_grp {
	MLXSW_REG_PPCNT_IEEE_8023_CNT = 0x0,
	MLXSW_REG_PPCNT_RFC_2863_CNT = 0x1,
	MLXSW_REG_PPCNT_RFC_2819_CNT = 0x2,
	MLXSW_REG_PPCNT_RFC_3635_CNT = 0x3,
	MLXSW_REG_PPCNT_EXT_CNT = 0x5,
	MLXSW_REG_PPCNT_DISCARD_CNT = 0x6,
	MLXSW_REG_PPCNT_PRIO_CNT = 0x10,
	MLXSW_REG_PPCNT_TC_CNT = 0x11,
	MLXSW_REG_PPCNT_TC_CONG_CNT = 0x13,
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
 * 0x6: Ethernet Discard Counters
 * 0x8: Link Level Retransmission Counters
 * 0x10: Per Priority Counters
 * 0x11: Per Traffic Class Counters
 * 0x12: Physical Layer Counters
 * 0x13: Per Traffic Class Congestion Counters
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

/* reg_ppcnt_lp_gl
 * Local port global variable.
 * 0: local_port 255 = all ports of the device.
 * 1: local_port indicates local port number for all ports.
 * Access: OP
 */
MLXSW_ITEM32(reg, ppcnt, lp_gl, 0x04, 30, 1);

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
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x00, 0, 64);

/* reg_ppcnt_a_frames_received_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_frames_received_ok,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x08, 0, 64);

/* reg_ppcnt_a_frame_check_sequence_errors
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_frame_check_sequence_errors,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x10, 0, 64);

/* reg_ppcnt_a_alignment_errors
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_alignment_errors,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x18, 0, 64);

/* reg_ppcnt_a_octets_transmitted_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_octets_transmitted_ok,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x20, 0, 64);

/* reg_ppcnt_a_octets_received_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_octets_received_ok,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x28, 0, 64);

/* reg_ppcnt_a_multicast_frames_xmitted_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_multicast_frames_xmitted_ok,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x30, 0, 64);

/* reg_ppcnt_a_broadcast_frames_xmitted_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_broadcast_frames_xmitted_ok,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x38, 0, 64);

/* reg_ppcnt_a_multicast_frames_received_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_multicast_frames_received_ok,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x40, 0, 64);

/* reg_ppcnt_a_broadcast_frames_received_ok
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_broadcast_frames_received_ok,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x48, 0, 64);

/* reg_ppcnt_a_in_range_length_errors
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_in_range_length_errors,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x50, 0, 64);

/* reg_ppcnt_a_out_of_range_length_field
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_out_of_range_length_field,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x58, 0, 64);

/* reg_ppcnt_a_frame_too_long_errors
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_frame_too_long_errors,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x60, 0, 64);

/* reg_ppcnt_a_symbol_error_during_carrier
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_symbol_error_during_carrier,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x68, 0, 64);

/* reg_ppcnt_a_mac_control_frames_transmitted
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_mac_control_frames_transmitted,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x70, 0, 64);

/* reg_ppcnt_a_mac_control_frames_received
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_mac_control_frames_received,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x78, 0, 64);

/* reg_ppcnt_a_unsupported_opcodes_received
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_unsupported_opcodes_received,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x80, 0, 64);

/* reg_ppcnt_a_pause_mac_ctrl_frames_received
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_pause_mac_ctrl_frames_received,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x88, 0, 64);

/* reg_ppcnt_a_pause_mac_ctrl_frames_transmitted
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, a_pause_mac_ctrl_frames_transmitted,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x90, 0, 64);

/* Ethernet RFC 2863 Counter Group */

/* reg_ppcnt_if_in_discards
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, if_in_discards,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x10, 0, 64);

/* reg_ppcnt_if_out_discards
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, if_out_discards,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x38, 0, 64);

/* reg_ppcnt_if_out_errors
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, if_out_errors,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x40, 0, 64);

/* Ethernet RFC 2819 Counter Group */

/* reg_ppcnt_ether_stats_undersize_pkts
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ether_stats_undersize_pkts,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x30, 0, 64);

/* reg_ppcnt_ether_stats_oversize_pkts
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ether_stats_oversize_pkts,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x38, 0, 64);

/* reg_ppcnt_ether_stats_fragments
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ether_stats_fragments,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x40, 0, 64);

/* reg_ppcnt_ether_stats_pkts64octets
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ether_stats_pkts64octets,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x58, 0, 64);

/* reg_ppcnt_ether_stats_pkts65to127octets
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ether_stats_pkts65to127octets,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x60, 0, 64);

/* reg_ppcnt_ether_stats_pkts128to255octets
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ether_stats_pkts128to255octets,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x68, 0, 64);

/* reg_ppcnt_ether_stats_pkts256to511octets
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ether_stats_pkts256to511octets,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x70, 0, 64);

/* reg_ppcnt_ether_stats_pkts512to1023octets
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ether_stats_pkts512to1023octets,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x78, 0, 64);

/* reg_ppcnt_ether_stats_pkts1024to1518octets
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ether_stats_pkts1024to1518octets,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x80, 0, 64);

/* reg_ppcnt_ether_stats_pkts1519to2047octets
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ether_stats_pkts1519to2047octets,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x88, 0, 64);

/* reg_ppcnt_ether_stats_pkts2048to4095octets
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ether_stats_pkts2048to4095octets,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x90, 0, 64);

/* reg_ppcnt_ether_stats_pkts4096to8191octets
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ether_stats_pkts4096to8191octets,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x98, 0, 64);

/* reg_ppcnt_ether_stats_pkts8192to10239octets
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ether_stats_pkts8192to10239octets,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0xA0, 0, 64);

/* Ethernet RFC 3635 Counter Group */

/* reg_ppcnt_dot3stats_fcs_errors
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, dot3stats_fcs_errors,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x08, 0, 64);

/* reg_ppcnt_dot3stats_symbol_errors
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, dot3stats_symbol_errors,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x60, 0, 64);

/* reg_ppcnt_dot3control_in_unknown_opcodes
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, dot3control_in_unknown_opcodes,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x68, 0, 64);

/* reg_ppcnt_dot3in_pause_frames
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, dot3in_pause_frames,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x70, 0, 64);

/* Ethernet Extended Counter Group Counters */

/* reg_ppcnt_ecn_marked
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ecn_marked,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x08, 0, 64);

/* Ethernet Discard Counter Group Counters */

/* reg_ppcnt_ingress_general
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ingress_general,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x00, 0, 64);

/* reg_ppcnt_ingress_policy_engine
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ingress_policy_engine,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x08, 0, 64);

/* reg_ppcnt_ingress_vlan_membership
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ingress_vlan_membership,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x10, 0, 64);

/* reg_ppcnt_ingress_tag_frame_type
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ingress_tag_frame_type,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x18, 0, 64);

/* reg_ppcnt_egress_vlan_membership
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, egress_vlan_membership,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x20, 0, 64);

/* reg_ppcnt_loopback_filter
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, loopback_filter,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x28, 0, 64);

/* reg_ppcnt_egress_general
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, egress_general,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x30, 0, 64);

/* reg_ppcnt_egress_hoq
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, egress_hoq,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x40, 0, 64);

/* reg_ppcnt_egress_policy_engine
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, egress_policy_engine,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x50, 0, 64);

/* reg_ppcnt_ingress_tx_link_down
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ingress_tx_link_down,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x58, 0, 64);

/* reg_ppcnt_egress_stp_filter
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, egress_stp_filter,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x60, 0, 64);

/* reg_ppcnt_egress_sll
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, egress_sll,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x70, 0, 64);

/* Ethernet Per Priority Group Counters */

/* reg_ppcnt_rx_octets
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, rx_octets,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x00, 0, 64);

/* reg_ppcnt_rx_frames
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, rx_frames,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x20, 0, 64);

/* reg_ppcnt_tx_octets
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, tx_octets,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x28, 0, 64);

/* reg_ppcnt_tx_frames
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, tx_frames,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x48, 0, 64);

/* reg_ppcnt_rx_pause
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, rx_pause,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x50, 0, 64);

/* reg_ppcnt_rx_pause_duration
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, rx_pause_duration,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x58, 0, 64);

/* reg_ppcnt_tx_pause
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, tx_pause,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x60, 0, 64);

/* reg_ppcnt_tx_pause_duration
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, tx_pause_duration,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x68, 0, 64);

/* reg_ppcnt_rx_pause_transition
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, tx_pause_transition,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x70, 0, 64);

/* Ethernet Per Traffic Class Counters */

/* reg_ppcnt_tc_transmit_queue
 * Contains the transmit queue depth in cells of traffic class
 * selected by prio_tc and the port selected by local_port.
 * The field cannot be cleared.
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, tc_transmit_queue,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x00, 0, 64);

/* reg_ppcnt_tc_no_buffer_discard_uc
 * The number of unicast packets dropped due to lack of shared
 * buffer resources.
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, tc_no_buffer_discard_uc,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x08, 0, 64);

/* Ethernet Per Traffic Class Congestion Group Counters */

/* reg_ppcnt_wred_discard
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, wred_discard,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x00, 0, 64);

/* reg_ppcnt_ecn_marked_tc
 * Access: RO
 */
MLXSW_ITEM64(reg, ppcnt, ecn_marked_tc,
	     MLXSW_REG_PPCNT_COUNTERS_OFFSET + 0x08, 0, 64);

static inline void mlxsw_reg_ppcnt_pack(char *payload, u16 local_port,
					enum mlxsw_reg_ppcnt_grp grp,
					u8 prio_tc)
{
	MLXSW_REG_ZERO(ppcnt, payload);
	mlxsw_reg_ppcnt_swid_set(payload, 0);
	mlxsw_reg_ppcnt_local_port_set(payload, local_port);
	mlxsw_reg_ppcnt_pnat_set(payload, 0);
	mlxsw_reg_ppcnt_grp_set(payload, grp);
	mlxsw_reg_ppcnt_clr_set(payload, 0);
	mlxsw_reg_ppcnt_lp_gl_set(payload, 1);
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
MLXSW_ITEM32_LP(reg, plib, 0x00, 16, 0x00, 12);

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
MLXSW_ITEM32_LP(reg, pptb, 0x00, 16, 0x00, 12);

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

static inline void mlxsw_reg_pptb_pack(char *payload, u16 local_port)
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
MLXSW_ITEM32_LP(reg, pbmc, 0x00, 16, 0x00, 12);

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

static inline void mlxsw_reg_pbmc_pack(char *payload, u16 local_port,
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
MLXSW_ITEM32_LP(reg, pspa, 0x00, 16, 0x00, 0);

/* reg_pspa_sub_port
 * Virtual port within the local port. Set to 0 when virtual ports are
 * disabled on the local port.
 * Access: Index
 */
MLXSW_ITEM32(reg, pspa, sub_port, 0x00, 8, 8);

static inline void mlxsw_reg_pspa_pack(char *payload, u8 swid, u16 local_port)
{
	MLXSW_REG_ZERO(pspa, payload);
	mlxsw_reg_pspa_swid_set(payload, swid);
	mlxsw_reg_pspa_local_port_set(payload, local_port);
	mlxsw_reg_pspa_sub_port_set(payload, 0);
}

/* PMAOS - Ports Module Administrative and Operational Status
 * ----------------------------------------------------------
 * This register configures and retrieves the per module status.
 */
#define MLXSW_REG_PMAOS_ID 0x5012
#define MLXSW_REG_PMAOS_LEN 0x10

MLXSW_REG_DEFINE(pmaos, MLXSW_REG_PMAOS_ID, MLXSW_REG_PMAOS_LEN);

/* reg_pmaos_rst
 * Module reset toggle.
 * Note: Setting reset while module is plugged-in will result in transition to
 * "initializing" operational state.
 * Access: OP
 */
MLXSW_ITEM32(reg, pmaos, rst, 0x00, 31, 1);

/* reg_pmaos_slot_index
 * Slot index.
 * Access: Index
 */
MLXSW_ITEM32(reg, pmaos, slot_index, 0x00, 24, 4);

/* reg_pmaos_module
 * Module number.
 * Access: Index
 */
MLXSW_ITEM32(reg, pmaos, module, 0x00, 16, 8);

enum mlxsw_reg_pmaos_admin_status {
	MLXSW_REG_PMAOS_ADMIN_STATUS_ENABLED = 1,
	MLXSW_REG_PMAOS_ADMIN_STATUS_DISABLED = 2,
	/* If the module is active and then unplugged, or experienced an error
	 * event, the operational status should go to "disabled" and can only
	 * be enabled upon explicit enable command.
	 */
	MLXSW_REG_PMAOS_ADMIN_STATUS_ENABLED_ONCE = 3,
};

/* reg_pmaos_admin_status
 * Module administrative state (the desired state of the module).
 * Note: To disable a module, all ports associated with the port must be
 * administatively down first.
 * Access: RW
 */
MLXSW_ITEM32(reg, pmaos, admin_status, 0x00, 8, 4);

/* reg_pmaos_ase
 * Admin state update enable.
 * If this bit is set, admin state will be updated based on admin_state field.
 * Only relevant on Set() operations.
 * Access: WO
 */
MLXSW_ITEM32(reg, pmaos, ase, 0x04, 31, 1);

/* reg_pmaos_ee
 * Event update enable.
 * If this bit is set, event generation will be updated based on the e field.
 * Only relevant on Set operations.
 * Access: WO
 */
MLXSW_ITEM32(reg, pmaos, ee, 0x04, 30, 1);

enum mlxsw_reg_pmaos_e {
	MLXSW_REG_PMAOS_E_DO_NOT_GENERATE_EVENT,
	MLXSW_REG_PMAOS_E_GENERATE_EVENT,
	MLXSW_REG_PMAOS_E_GENERATE_SINGLE_EVENT,
};

/* reg_pmaos_e
 * Event Generation on operational state change.
 * Access: RW
 */
MLXSW_ITEM32(reg, pmaos, e, 0x04, 0, 2);

static inline void mlxsw_reg_pmaos_pack(char *payload, u8 slot_index, u8 module)
{
	MLXSW_REG_ZERO(pmaos, payload);
	mlxsw_reg_pmaos_slot_index_set(payload, slot_index);
	mlxsw_reg_pmaos_module_set(payload, module);
}

/* PPLR - Port Physical Loopback Register
 * --------------------------------------
 * This register allows configuration of the port's loopback mode.
 */
#define MLXSW_REG_PPLR_ID 0x5018
#define MLXSW_REG_PPLR_LEN 0x8

MLXSW_REG_DEFINE(pplr, MLXSW_REG_PPLR_ID, MLXSW_REG_PPLR_LEN);

/* reg_pplr_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, pplr, 0x00, 16, 0x00, 12);

/* Phy local loopback. When set the port's egress traffic is looped back
 * to the receiver and the port transmitter is disabled.
 */
#define MLXSW_REG_PPLR_LB_TYPE_BIT_PHY_LOCAL BIT(1)

/* reg_pplr_lb_en
 * Loopback enable.
 * Access: RW
 */
MLXSW_ITEM32(reg, pplr, lb_en, 0x04, 0, 8);

static inline void mlxsw_reg_pplr_pack(char *payload, u16 local_port,
				       bool phy_local)
{
	MLXSW_REG_ZERO(pplr, payload);
	mlxsw_reg_pplr_local_port_set(payload, local_port);
	mlxsw_reg_pplr_lb_en_set(payload,
				 phy_local ?
				 MLXSW_REG_PPLR_LB_TYPE_BIT_PHY_LOCAL : 0);
}

/* PMTDB - Port Module To local DataBase Register
 * ----------------------------------------------
 * The PMTDB register allows to query the possible module<->local port
 * mapping than can be used in PMLP. It does not represent the actual/current
 * mapping of the local to module. Actual mapping is only defined by PMLP.
 */
#define MLXSW_REG_PMTDB_ID 0x501A
#define MLXSW_REG_PMTDB_LEN 0x40

MLXSW_REG_DEFINE(pmtdb, MLXSW_REG_PMTDB_ID, MLXSW_REG_PMTDB_LEN);

/* reg_pmtdb_slot_index
 * Slot index (0: Main board).
 * Access: Index
 */
MLXSW_ITEM32(reg, pmtdb, slot_index, 0x00, 24, 4);

/* reg_pmtdb_module
 * Module number.
 * Access: Index
 */
MLXSW_ITEM32(reg, pmtdb, module, 0x00, 16, 8);

/* reg_pmtdb_ports_width
 * Port's width
 * Access: Index
 */
MLXSW_ITEM32(reg, pmtdb, ports_width, 0x00, 12, 4);

/* reg_pmtdb_num_ports
 * Number of ports in a single module (split/breakout)
 * Access: Index
 */
MLXSW_ITEM32(reg, pmtdb, num_ports, 0x00, 8, 4);

enum mlxsw_reg_pmtdb_status {
	MLXSW_REG_PMTDB_STATUS_SUCCESS,
};

/* reg_pmtdb_status
 * Status
 * Access: RO
 */
MLXSW_ITEM32(reg, pmtdb, status, 0x00, 0, 4);

/* reg_pmtdb_port_num
 * The local_port value which can be assigned to the module.
 * In case of more than one port, port<x> represent the /<x> port of
 * the module.
 * Access: RO
 */
MLXSW_ITEM16_INDEXED(reg, pmtdb, port_num, 0x04, 0, 10, 0x02, 0x00, false);

static inline void mlxsw_reg_pmtdb_pack(char *payload, u8 slot_index, u8 module,
					u8 ports_width, u8 num_ports)
{
	MLXSW_REG_ZERO(pmtdb, payload);
	mlxsw_reg_pmtdb_slot_index_set(payload, slot_index);
	mlxsw_reg_pmtdb_module_set(payload, module);
	mlxsw_reg_pmtdb_ports_width_set(payload, ports_width);
	mlxsw_reg_pmtdb_num_ports_set(payload, num_ports);
}

/* PMECR - Ports Mapping Event Configuration Register
 * --------------------------------------------------
 * The PMECR register is used to enable/disable event triggering
 * in case of local port mapping change.
 */
#define MLXSW_REG_PMECR_ID 0x501B
#define MLXSW_REG_PMECR_LEN 0x20

MLXSW_REG_DEFINE(pmecr, MLXSW_REG_PMECR_ID, MLXSW_REG_PMECR_LEN);

/* reg_pmecr_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, pmecr, 0x00, 16, 0x00, 12);

/* reg_pmecr_ee
 * Event update enable. If this bit is set, event generation will be updated
 * based on the e field. Only relevant on Set operations.
 * Access: WO
 */
MLXSW_ITEM32(reg, pmecr, ee, 0x04, 30, 1);

/* reg_pmecr_eswi
 * Software ignore enable bit. If this bit is set, the value of swi is used.
 * If this bit is clear, the value of swi is ignored.
 * Only relevant on Set operations.
 * Access: WO
 */
MLXSW_ITEM32(reg, pmecr, eswi, 0x04, 24, 1);

/* reg_pmecr_swi
 * Software ignore. If this bit is set, the device shouldn't generate events
 * in case of PMLP SET operation but only upon self local port mapping change
 * (if applicable according to e configuration). This is supplementary
 * configuration on top of e value.
 * Access: RW
 */
MLXSW_ITEM32(reg, pmecr, swi, 0x04, 8, 1);

enum mlxsw_reg_pmecr_e {
	MLXSW_REG_PMECR_E_DO_NOT_GENERATE_EVENT,
	MLXSW_REG_PMECR_E_GENERATE_EVENT,
	MLXSW_REG_PMECR_E_GENERATE_SINGLE_EVENT,
};

/* reg_pmecr_e
 * Event generation on local port mapping change.
 * Access: RW
 */
MLXSW_ITEM32(reg, pmecr, e, 0x04, 0, 2);

static inline void mlxsw_reg_pmecr_pack(char *payload, u16 local_port,
					enum mlxsw_reg_pmecr_e e)
{
	MLXSW_REG_ZERO(pmecr, payload);
	mlxsw_reg_pmecr_local_port_set(payload, local_port);
	mlxsw_reg_pmecr_e_set(payload, e);
	mlxsw_reg_pmecr_ee_set(payload, true);
	mlxsw_reg_pmecr_swi_set(payload, true);
	mlxsw_reg_pmecr_eswi_set(payload, true);
}

/* PMPE - Port Module Plug/Unplug Event Register
 * ---------------------------------------------
 * This register reports any operational status change of a module.
 * A change in the module’s state will generate an event only if the change
 * happens after arming the event mechanism. Any changes to the module state
 * while the event mechanism is not armed will not be reported. Software can
 * query the PMPE register for module status.
 */
#define MLXSW_REG_PMPE_ID 0x5024
#define MLXSW_REG_PMPE_LEN 0x10

MLXSW_REG_DEFINE(pmpe, MLXSW_REG_PMPE_ID, MLXSW_REG_PMPE_LEN);

/* reg_pmpe_slot_index
 * Slot index.
 * Access: Index
 */
MLXSW_ITEM32(reg, pmpe, slot_index, 0x00, 24, 4);

/* reg_pmpe_module
 * Module number.
 * Access: Index
 */
MLXSW_ITEM32(reg, pmpe, module, 0x00, 16, 8);

enum mlxsw_reg_pmpe_module_status {
	MLXSW_REG_PMPE_MODULE_STATUS_PLUGGED_ENABLED = 1,
	MLXSW_REG_PMPE_MODULE_STATUS_UNPLUGGED,
	MLXSW_REG_PMPE_MODULE_STATUS_PLUGGED_ERROR,
	MLXSW_REG_PMPE_MODULE_STATUS_PLUGGED_DISABLED,
};

/* reg_pmpe_module_status
 * Module status.
 * Access: RO
 */
MLXSW_ITEM32(reg, pmpe, module_status, 0x00, 0, 4);

/* reg_pmpe_error_type
 * Module error details.
 * Access: RO
 */
MLXSW_ITEM32(reg, pmpe, error_type, 0x04, 8, 4);

/* PDDR - Port Diagnostics Database Register
 * -----------------------------------------
 * The PDDR enables to read the Phy debug database
 */
#define MLXSW_REG_PDDR_ID 0x5031
#define MLXSW_REG_PDDR_LEN 0x100

MLXSW_REG_DEFINE(pddr, MLXSW_REG_PDDR_ID, MLXSW_REG_PDDR_LEN);

/* reg_pddr_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, pddr, 0x00, 16, 0x00, 12);

enum mlxsw_reg_pddr_page_select {
	MLXSW_REG_PDDR_PAGE_SELECT_TROUBLESHOOTING_INFO = 1,
};

/* reg_pddr_page_select
 * Page select index.
 * Access: Index
 */
MLXSW_ITEM32(reg, pddr, page_select, 0x04, 0, 8);

enum mlxsw_reg_pddr_trblsh_group_opcode {
	/* Monitor opcodes */
	MLXSW_REG_PDDR_TRBLSH_GROUP_OPCODE_MONITOR,
};

/* reg_pddr_group_opcode
 * Group selector.
 * Access: Index
 */
MLXSW_ITEM32(reg, pddr, trblsh_group_opcode, 0x08, 0, 16);

/* reg_pddr_status_opcode
 * Group selector.
 * Access: RO
 */
MLXSW_ITEM32(reg, pddr, trblsh_status_opcode, 0x0C, 0, 16);

static inline void mlxsw_reg_pddr_pack(char *payload, u16 local_port,
				       u8 page_select)
{
	MLXSW_REG_ZERO(pddr, payload);
	mlxsw_reg_pddr_local_port_set(payload, local_port);
	mlxsw_reg_pddr_page_select_set(payload, page_select);
}

/* PMMP - Port Module Memory Map Properties Register
 * -------------------------------------------------
 * The PMMP register allows to override the module memory map advertisement.
 * The register can only be set when the module is disabled by PMAOS register.
 */
#define MLXSW_REG_PMMP_ID 0x5044
#define MLXSW_REG_PMMP_LEN 0x2C

MLXSW_REG_DEFINE(pmmp, MLXSW_REG_PMMP_ID, MLXSW_REG_PMMP_LEN);

/* reg_pmmp_module
 * Module number.
 * Access: Index
 */
MLXSW_ITEM32(reg, pmmp, module, 0x00, 16, 8);

/* reg_pmmp_slot_index
 * Slot index.
 * Access: Index
 */
MLXSW_ITEM32(reg, pmmp, slot_index, 0x00, 24, 4);

/* reg_pmmp_sticky
 * When set, will keep eeprom_override values after plug-out event.
 * Access: OP
 */
MLXSW_ITEM32(reg, pmmp, sticky, 0x00, 0, 1);

/* reg_pmmp_eeprom_override_mask
 * Write mask bit (negative polarity).
 * 0 - Allow write
 * 1 - Ignore write
 * On write, indicates which of the bits from eeprom_override field are
 * updated.
 * Access: WO
 */
MLXSW_ITEM32(reg, pmmp, eeprom_override_mask, 0x04, 16, 16);

enum {
	/* Set module to low power mode */
	MLXSW_REG_PMMP_EEPROM_OVERRIDE_LOW_POWER_MASK = BIT(8),
};

/* reg_pmmp_eeprom_override
 * Override / ignore EEPROM advertisement properties bitmask
 * Access: RW
 */
MLXSW_ITEM32(reg, pmmp, eeprom_override, 0x04, 0, 16);

static inline void mlxsw_reg_pmmp_pack(char *payload, u8 slot_index, u8 module)
{
	MLXSW_REG_ZERO(pmmp, payload);
	mlxsw_reg_pmmp_slot_index_set(payload, slot_index);
	mlxsw_reg_pmmp_module_set(payload, module);
}

/* PLLP - Port Local port to Label Port mapping Register
 * -----------------------------------------------------
 * The PLLP register returns the mapping from Local Port into Label Port.
 */
#define MLXSW_REG_PLLP_ID 0x504A
#define MLXSW_REG_PLLP_LEN 0x10

MLXSW_REG_DEFINE(pllp, MLXSW_REG_PLLP_ID, MLXSW_REG_PLLP_LEN);

/* reg_pllp_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, pllp, 0x00, 16, 0x00, 12);

/* reg_pllp_label_port
 * Front panel label of the port.
 * Access: RO
 */
MLXSW_ITEM32(reg, pllp, label_port, 0x00, 0, 8);

/* reg_pllp_split_num
 * Label split mapping for local_port.
 * Access: RO
 */
MLXSW_ITEM32(reg, pllp, split_num, 0x04, 0, 4);

/* reg_pllp_slot_index
 * Slot index (0: Main board).
 * Access: RO
 */
MLXSW_ITEM32(reg, pllp, slot_index, 0x08, 0, 4);

static inline void mlxsw_reg_pllp_pack(char *payload, u16 local_port)
{
	MLXSW_REG_ZERO(pllp, payload);
	mlxsw_reg_pllp_local_port_set(payload, local_port);
}

static inline void mlxsw_reg_pllp_unpack(char *payload, u8 *label_port,
					 u8 *split_num, u8 *slot_index)
{
	*label_port = mlxsw_reg_pllp_label_port_get(payload);
	*split_num = mlxsw_reg_pllp_split_num_get(payload);
	*slot_index = mlxsw_reg_pllp_slot_index_get(payload);
}

/* PMTM - Port Module Type Mapping Register
 * ----------------------------------------
 * The PMTM register allows query or configuration of module types.
 * The register can only be set when the module is disabled by PMAOS register
 */
#define MLXSW_REG_PMTM_ID 0x5067
#define MLXSW_REG_PMTM_LEN 0x10

MLXSW_REG_DEFINE(pmtm, MLXSW_REG_PMTM_ID, MLXSW_REG_PMTM_LEN);

/* reg_pmtm_slot_index
 * Slot index.
 * Access: Index
 */
MLXSW_ITEM32(reg, pmtm, slot_index, 0x00, 24, 4);

/* reg_pmtm_module
 * Module number.
 * Access: Index
 */
MLXSW_ITEM32(reg, pmtm, module, 0x00, 16, 8);

enum mlxsw_reg_pmtm_module_type {
	MLXSW_REG_PMTM_MODULE_TYPE_BACKPLANE_4_LANES = 0,
	MLXSW_REG_PMTM_MODULE_TYPE_QSFP = 1,
	MLXSW_REG_PMTM_MODULE_TYPE_SFP = 2,
	MLXSW_REG_PMTM_MODULE_TYPE_BACKPLANE_SINGLE_LANE = 4,
	MLXSW_REG_PMTM_MODULE_TYPE_BACKPLANE_2_LANES = 8,
	MLXSW_REG_PMTM_MODULE_TYPE_CHIP2CHIP4X = 10,
	MLXSW_REG_PMTM_MODULE_TYPE_CHIP2CHIP2X = 11,
	MLXSW_REG_PMTM_MODULE_TYPE_CHIP2CHIP1X = 12,
	MLXSW_REG_PMTM_MODULE_TYPE_QSFP_DD = 14,
	MLXSW_REG_PMTM_MODULE_TYPE_OSFP = 15,
	MLXSW_REG_PMTM_MODULE_TYPE_SFP_DD = 16,
	MLXSW_REG_PMTM_MODULE_TYPE_DSFP = 17,
	MLXSW_REG_PMTM_MODULE_TYPE_CHIP2CHIP8X = 18,
	MLXSW_REG_PMTM_MODULE_TYPE_TWISTED_PAIR = 19,
};

/* reg_pmtm_module_type
 * Module type.
 * Access: RW
 */
MLXSW_ITEM32(reg, pmtm, module_type, 0x04, 0, 5);

static inline void mlxsw_reg_pmtm_pack(char *payload, u8 slot_index, u8 module)
{
	MLXSW_REG_ZERO(pmtm, payload);
	mlxsw_reg_pmtm_slot_index_set(payload, slot_index);
	mlxsw_reg_pmtm_module_set(payload, module);
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
	MLXSW_REG_HTGT_TRAP_GROUP_CORE_EVENT,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_STP,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_LACP,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_LLDP,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_MC_SNOOPING,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_BGP,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_OSPF,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_PIM,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_MULTICAST,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_NEIGH_DISCOVERY,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_ROUTER_EXP,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_EXTERNAL_ROUTE,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_IP2ME,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_DHCP,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_EVENT,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_IPV6,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_LBERROR,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_PTP0,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_PTP1,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_VRRP,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_PKT_SAMPLE,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_FLOW_LOGGING,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_FID_MISS,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_BFD,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_DUMMY,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_L2_DISCARDS,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_L3_DISCARDS,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_L3_EXCEPTIONS,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_TUNNEL_DISCARDS,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_ACL_DISCARDS,
	MLXSW_REG_HTGT_TRAP_GROUP_SP_BUFFER_DISCARDS,

	__MLXSW_REG_HTGT_TRAP_GROUP_MAX,
	MLXSW_REG_HTGT_TRAP_GROUP_MAX = __MLXSW_REG_HTGT_TRAP_GROUP_MAX - 1
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
	MLXSW_REG_HPKT_ACTION_TRAP_EXCEPTION_TO_CPU,
	MLXSW_REG_HPKT_ACTION_SET_FW_DEFAULT = 15,
};

/* reg_hpkt_action
 * Action to perform on packet when trapped.
 * 0 - No action. Forward to CPU based on switching rules.
 * 1 - Trap to CPU (CPU receives sole copy).
 * 2 - Mirror to CPU (CPU receives a replica of the packet).
 * 3 - Discard.
 * 4 - Soft discard (allow other traps to act on the packet).
 * 5 - Trap and soft discard (allow other traps to overwrite this trap).
 * 6 - Trap to CPU (CPU receives sole copy) and count it as error.
 * 15 - Restore the firmware's default action.
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
MLXSW_ITEM32(reg, hpkt, trap_id, 0x00, 0, 10);

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

/* reg_ritr_ipv4_mc
 * IPv4 multicast routing enable.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, ipv4_mc, 0x00, 27, 1);

/* reg_ritr_ipv6_mc
 * IPv6 multicast routing enable.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, ipv6_mc, 0x00, 26, 1);

enum mlxsw_reg_ritr_if_type {
	/* VLAN interface. */
	MLXSW_REG_RITR_VLAN_IF,
	/* FID interface. */
	MLXSW_REG_RITR_FID_IF,
	/* Sub-port interface. */
	MLXSW_REG_RITR_SP_IF,
	/* Loopback Interface. */
	MLXSW_REG_RITR_LOOPBACK_IF,
};

/* reg_ritr_type
 * Router interface type as per enum mlxsw_reg_ritr_if_type.
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

/* reg_ritr_ipv4_mc_fe
 * IPv4 Multicast Forwarding Enable.
 * When disabled, forwarding is blocked but local traffic (traps and IP to me)
 * will be enabled.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, ipv4_mc_fe, 0x04, 27, 1);

/* reg_ritr_ipv6_mc_fe
 * IPv6 Multicast Forwarding Enable.
 * When disabled, forwarding is blocked but local traffic (traps and IP to me)
 * will be enabled.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, ipv6_mc_fe, 0x04, 26, 1);

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

/* reg_ritr_if_mac_profile_id
 * MAC msb profile ID.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, if_mac_profile_id, 0x10, 16, 4);

/* reg_ritr_if_mac
 * Router interface MAC address.
 * In Spectrum, all MAC addresses must have the same 38 MSBits.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, ritr, if_mac, 0x12, 6);

/* reg_ritr_if_vrrp_id_ipv6
 * VRRP ID for IPv6
 * Note: Reserved for RIF types other than VLAN, FID and Sub-port.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, if_vrrp_id_ipv6, 0x1C, 8, 8);

/* reg_ritr_if_vrrp_id_ipv4
 * VRRP ID for IPv4
 * Note: Reserved for RIF types other than VLAN, FID and Sub-port.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, if_vrrp_id_ipv4, 0x1C, 0, 8);

/* VLAN Interface */

/* reg_ritr_vlan_if_vid
 * VLAN ID.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, vlan_if_vid, 0x08, 0, 12);

/* FID Interface */

/* reg_ritr_fid_if_fid
 * Filtering ID. Used to connect a bridge to the router.
 * When legacy bridge model is used, only FIDs from the vFID range are
 * supported. When unified bridge model is used, this is the egress FID for
 * router to bridge.
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

/* reg_ritr_sp_if_efid
 * Egress filtering ID.
 * Used to connect the eRIF to a bridge if eRIF-ACL has modified the DMAC or
 * the VID.
 * Access: RW
 *
 * Note: Reserved when legacy bridge model is used.
 */
MLXSW_ITEM32(reg, ritr, sp_if_efid, 0x0C, 0, 16);

/* reg_ritr_sp_if_vid
 * VLAN ID.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, sp_if_vid, 0x18, 0, 12);

/* Loopback Interface */

enum mlxsw_reg_ritr_loopback_protocol {
	/* IPinIP IPv4 underlay Unicast */
	MLXSW_REG_RITR_LOOPBACK_PROTOCOL_IPIP_IPV4,
	/* IPinIP IPv6 underlay Unicast */
	MLXSW_REG_RITR_LOOPBACK_PROTOCOL_IPIP_IPV6,
	/* IPinIP generic - used for Spectrum-2 underlay RIF */
	MLXSW_REG_RITR_LOOPBACK_GENERIC,
};

/* reg_ritr_loopback_protocol
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, loopback_protocol, 0x08, 28, 4);

enum mlxsw_reg_ritr_loopback_ipip_type {
	/* Tunnel is IPinIP. */
	MLXSW_REG_RITR_LOOPBACK_IPIP_TYPE_IP_IN_IP,
	/* Tunnel is GRE, no key. */
	MLXSW_REG_RITR_LOOPBACK_IPIP_TYPE_IP_IN_GRE_IN_IP,
	/* Tunnel is GRE, with a key. */
	MLXSW_REG_RITR_LOOPBACK_IPIP_TYPE_IP_IN_GRE_KEY_IN_IP,
};

/* reg_ritr_loopback_ipip_type
 * Encapsulation type.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, loopback_ipip_type, 0x10, 24, 4);

enum mlxsw_reg_ritr_loopback_ipip_options {
	/* The key is defined by gre_key. */
	MLXSW_REG_RITR_LOOPBACK_IPIP_OPTIONS_GRE_KEY_PRESET,
};

/* reg_ritr_loopback_ipip_options
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, loopback_ipip_options, 0x10, 20, 4);

/* reg_ritr_loopback_ipip_uvr
 * Underlay Virtual Router ID.
 * Range is 0..cap_max_virtual_routers-1.
 * Reserved for Spectrum-2.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, loopback_ipip_uvr, 0x10, 0, 16);

/* reg_ritr_loopback_ipip_underlay_rif
 * Underlay ingress router interface.
 * Reserved for Spectrum.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, loopback_ipip_underlay_rif, 0x14, 0, 16);

/* reg_ritr_loopback_ipip_usip*
 * Encapsulation Underlay source IP.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, ritr, loopback_ipip_usip6, 0x18, 16);
MLXSW_ITEM32(reg, ritr, loopback_ipip_usip4, 0x24, 0, 32);

/* reg_ritr_loopback_ipip_gre_key
 * GRE Key.
 * Reserved when ipip_type is not IP_IN_GRE_KEY_IN_IP.
 * Access: RW
 */
MLXSW_ITEM32(reg, ritr, loopback_ipip_gre_key, 0x28, 0, 32);

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

	if (egress) {
		mlxsw_reg_ritr_egress_counter_set_type_set(payload, set_type);
		mlxsw_reg_ritr_egress_counter_index_set(payload, index);
	} else {
		mlxsw_reg_ritr_ingress_counter_set_type_set(payload, set_type);
		mlxsw_reg_ritr_ingress_counter_index_set(payload, index);
	}
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
				       u16 rif, u16 vr_id, u16 mtu)
{
	bool op = enable ? MLXSW_REG_RITR_RIF_CREATE : MLXSW_REG_RITR_RIF_DEL;

	MLXSW_REG_ZERO(ritr, payload);
	mlxsw_reg_ritr_enable_set(payload, enable);
	mlxsw_reg_ritr_ipv4_set(payload, 1);
	mlxsw_reg_ritr_ipv6_set(payload, 1);
	mlxsw_reg_ritr_ipv4_mc_set(payload, 1);
	mlxsw_reg_ritr_ipv6_mc_set(payload, 1);
	mlxsw_reg_ritr_type_set(payload, type);
	mlxsw_reg_ritr_op_set(payload, op);
	mlxsw_reg_ritr_rif_set(payload, rif);
	mlxsw_reg_ritr_ipv4_fe_set(payload, 1);
	mlxsw_reg_ritr_ipv6_fe_set(payload, 1);
	mlxsw_reg_ritr_ipv4_mc_fe_set(payload, 1);
	mlxsw_reg_ritr_ipv6_mc_fe_set(payload, 1);
	mlxsw_reg_ritr_lb_en_set(payload, 1);
	mlxsw_reg_ritr_virtual_router_set(payload, vr_id);
	mlxsw_reg_ritr_mtu_set(payload, mtu);
}

static inline void mlxsw_reg_ritr_mac_pack(char *payload, const char *mac)
{
	mlxsw_reg_ritr_if_mac_memcpy_to(payload, mac);
}

static inline void
mlxsw_reg_ritr_loopback_ipip_common_pack(char *payload,
			    enum mlxsw_reg_ritr_loopback_ipip_type ipip_type,
			    enum mlxsw_reg_ritr_loopback_ipip_options options,
			    u16 uvr_id, u16 underlay_rif, u32 gre_key)
{
	mlxsw_reg_ritr_loopback_ipip_type_set(payload, ipip_type);
	mlxsw_reg_ritr_loopback_ipip_options_set(payload, options);
	mlxsw_reg_ritr_loopback_ipip_uvr_set(payload, uvr_id);
	mlxsw_reg_ritr_loopback_ipip_underlay_rif_set(payload, underlay_rif);
	mlxsw_reg_ritr_loopback_ipip_gre_key_set(payload, gre_key);
}

static inline void
mlxsw_reg_ritr_loopback_ipip4_pack(char *payload,
			    enum mlxsw_reg_ritr_loopback_ipip_type ipip_type,
			    enum mlxsw_reg_ritr_loopback_ipip_options options,
			    u16 uvr_id, u16 underlay_rif, u32 usip, u32 gre_key)
{
	mlxsw_reg_ritr_loopback_protocol_set(payload,
				    MLXSW_REG_RITR_LOOPBACK_PROTOCOL_IPIP_IPV4);
	mlxsw_reg_ritr_loopback_ipip_common_pack(payload, ipip_type, options,
						 uvr_id, underlay_rif, gre_key);
	mlxsw_reg_ritr_loopback_ipip_usip4_set(payload, usip);
}

static inline void
mlxsw_reg_ritr_loopback_ipip6_pack(char *payload,
				   enum mlxsw_reg_ritr_loopback_ipip_type ipip_type,
				   enum mlxsw_reg_ritr_loopback_ipip_options options,
				   u16 uvr_id, u16 underlay_rif,
				   const struct in6_addr *usip, u32 gre_key)
{
	enum mlxsw_reg_ritr_loopback_protocol protocol =
		MLXSW_REG_RITR_LOOPBACK_PROTOCOL_IPIP_IPV6;

	mlxsw_reg_ritr_loopback_protocol_set(payload, protocol);
	mlxsw_reg_ritr_loopback_ipip_common_pack(payload, ipip_type, options,
						 uvr_id, underlay_rif, gre_key);
	mlxsw_reg_ritr_loopback_ipip_usip6_memcpy_to(payload,
						     (const char *)usip);
}

/* RTAR - Router TCAM Allocation Register
 * --------------------------------------
 * This register is used for allocation of regions in the TCAM table.
 */
#define MLXSW_REG_RTAR_ID 0x8004
#define MLXSW_REG_RTAR_LEN 0x20

MLXSW_REG_DEFINE(rtar, MLXSW_REG_RTAR_ID, MLXSW_REG_RTAR_LEN);

enum mlxsw_reg_rtar_op {
	MLXSW_REG_RTAR_OP_ALLOCATE,
	MLXSW_REG_RTAR_OP_RESIZE,
	MLXSW_REG_RTAR_OP_DEALLOCATE,
};

/* reg_rtar_op
 * Access: WO
 */
MLXSW_ITEM32(reg, rtar, op, 0x00, 28, 4);

enum mlxsw_reg_rtar_key_type {
	MLXSW_REG_RTAR_KEY_TYPE_IPV4_MULTICAST = 1,
	MLXSW_REG_RTAR_KEY_TYPE_IPV6_MULTICAST = 3
};

/* reg_rtar_key_type
 * TCAM key type for the region.
 * Access: WO
 */
MLXSW_ITEM32(reg, rtar, key_type, 0x00, 0, 8);

/* reg_rtar_region_size
 * TCAM region size. When allocating/resizing this is the requested
 * size, the response is the actual size.
 * Note: Actual size may be larger than requested.
 * Reserved for op = Deallocate
 * Access: WO
 */
MLXSW_ITEM32(reg, rtar, region_size, 0x04, 0, 16);

static inline void mlxsw_reg_rtar_pack(char *payload,
				       enum mlxsw_reg_rtar_op op,
				       enum mlxsw_reg_rtar_key_type key_type,
				       u16 region_size)
{
	MLXSW_REG_ZERO(rtar, payload);
	mlxsw_reg_rtar_op_set(payload, op);
	mlxsw_reg_rtar_key_type_set(payload, key_type);
	mlxsw_reg_rtar_region_size_set(payload, region_size);
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

enum mlxsw_reg_ratr_type {
	/* Ethernet */
	MLXSW_REG_RATR_TYPE_ETHERNET,
	/* IPoIB Unicast without GRH.
	 * Reserved for Spectrum.
	 */
	MLXSW_REG_RATR_TYPE_IPOIB_UC,
	/* IPoIB Unicast with GRH. Supported only in table 0 (Ethernet unicast
	 * adjacency).
	 * Reserved for Spectrum.
	 */
	MLXSW_REG_RATR_TYPE_IPOIB_UC_W_GRH,
	/* IPoIB Multicast.
	 * Reserved for Spectrum.
	 */
	MLXSW_REG_RATR_TYPE_IPOIB_MC,
	/* MPLS.
	 * Reserved for SwitchX/-2.
	 */
	MLXSW_REG_RATR_TYPE_MPLS,
	/* IPinIP Encap.
	 * Reserved for SwitchX/-2.
	 */
	MLXSW_REG_RATR_TYPE_IPIP,
};

/* reg_ratr_type
 * Adjacency entry type.
 * Access: RW
 */
MLXSW_ITEM32(reg, ratr, type, 0x04, 28, 4);

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

/* reg_ratr_adjacency_index_high
 * Bits 23:16 of the adjacency_index.
 * Access: Index
 */
MLXSW_ITEM32(reg, ratr, adjacency_index_high, 0x0C, 16, 8);

enum mlxsw_reg_ratr_trap_id {
	MLXSW_REG_RATR_TRAP_ID_RTR_EGRESS0,
	MLXSW_REG_RATR_TRAP_ID_RTR_EGRESS1,
};

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

enum mlxsw_reg_ratr_ipip_type {
	/* IPv4, address set by mlxsw_reg_ratr_ipip_ipv4_udip. */
	MLXSW_REG_RATR_IPIP_TYPE_IPV4,
	/* IPv6, address set by mlxsw_reg_ratr_ipip_ipv6_ptr. */
	MLXSW_REG_RATR_IPIP_TYPE_IPV6,
};

/* reg_ratr_ipip_type
 * Underlay destination ip type.
 * Note: the type field must match the protocol of the router interface.
 * Access: RW
 */
MLXSW_ITEM32(reg, ratr, ipip_type, 0x10, 16, 4);

/* reg_ratr_ipip_ipv4_udip
 * Underlay ipv4 dip.
 * Reserved when ipip_type is IPv6.
 * Access: RW
 */
MLXSW_ITEM32(reg, ratr, ipip_ipv4_udip, 0x18, 0, 32);

/* reg_ratr_ipip_ipv6_ptr
 * Pointer to IPv6 underlay destination ip address.
 * For Spectrum: Pointer to KVD linear space.
 * Access: RW
 */
MLXSW_ITEM32(reg, ratr, ipip_ipv6_ptr, 0x1C, 0, 24);

enum mlxsw_reg_flow_counter_set_type {
	/* No count */
	MLXSW_REG_FLOW_COUNTER_SET_TYPE_NO_COUNT = 0x00,
	/* Count packets and bytes */
	MLXSW_REG_FLOW_COUNTER_SET_TYPE_PACKETS_BYTES = 0x03,
	/* Count only packets */
	MLXSW_REG_FLOW_COUNTER_SET_TYPE_PACKETS = 0x05,
};

/* reg_ratr_counter_set_type
 * Counter set type for flow counters
 * Access: RW
 */
MLXSW_ITEM32(reg, ratr, counter_set_type, 0x28, 24, 8);

/* reg_ratr_counter_index
 * Counter index for flow counters
 * Access: RW
 */
MLXSW_ITEM32(reg, ratr, counter_index, 0x28, 0, 24);

static inline void
mlxsw_reg_ratr_pack(char *payload,
		    enum mlxsw_reg_ratr_op op, bool valid,
		    enum mlxsw_reg_ratr_type type,
		    u32 adjacency_index, u16 egress_rif)
{
	MLXSW_REG_ZERO(ratr, payload);
	mlxsw_reg_ratr_op_set(payload, op);
	mlxsw_reg_ratr_v_set(payload, valid);
	mlxsw_reg_ratr_type_set(payload, type);
	mlxsw_reg_ratr_adjacency_index_low_set(payload, adjacency_index);
	mlxsw_reg_ratr_adjacency_index_high_set(payload, adjacency_index >> 16);
	mlxsw_reg_ratr_egress_router_interface_set(payload, egress_rif);
}

static inline void mlxsw_reg_ratr_eth_entry_pack(char *payload,
						 const char *dest_mac)
{
	mlxsw_reg_ratr_eth_destination_mac_memcpy_to(payload, dest_mac);
}

static inline void mlxsw_reg_ratr_ipip4_entry_pack(char *payload, u32 ipv4_udip)
{
	mlxsw_reg_ratr_ipip_type_set(payload, MLXSW_REG_RATR_IPIP_TYPE_IPV4);
	mlxsw_reg_ratr_ipip_ipv4_udip_set(payload, ipv4_udip);
}

static inline void mlxsw_reg_ratr_ipip6_entry_pack(char *payload, u32 ipv6_ptr)
{
	mlxsw_reg_ratr_ipip_type_set(payload, MLXSW_REG_RATR_IPIP_TYPE_IPV6);
	mlxsw_reg_ratr_ipip_ipv6_ptr_set(payload, ipv6_ptr);
}

static inline void mlxsw_reg_ratr_counter_pack(char *payload, u64 counter_index,
					       bool counter_enable)
{
	enum mlxsw_reg_flow_counter_set_type set_type;

	if (counter_enable)
		set_type = MLXSW_REG_FLOW_COUNTER_SET_TYPE_PACKETS_BYTES;
	else
		set_type = MLXSW_REG_FLOW_COUNTER_SET_TYPE_NO_COUNT;

	mlxsw_reg_ratr_counter_index_set(payload, counter_index);
	mlxsw_reg_ratr_counter_set_type_set(payload, set_type);
}

/* RDPM - Router DSCP to Priority Mapping
 * --------------------------------------
 * Controls the mapping from DSCP field to switch priority on routed packets
 */
#define MLXSW_REG_RDPM_ID 0x8009
#define MLXSW_REG_RDPM_BASE_LEN 0x00
#define MLXSW_REG_RDPM_DSCP_ENTRY_REC_LEN 0x01
#define MLXSW_REG_RDPM_DSCP_ENTRY_REC_MAX_COUNT 64
#define MLXSW_REG_RDPM_LEN 0x40
#define MLXSW_REG_RDPM_LAST_ENTRY (MLXSW_REG_RDPM_BASE_LEN + \
				   MLXSW_REG_RDPM_LEN - \
				   MLXSW_REG_RDPM_DSCP_ENTRY_REC_LEN)

MLXSW_REG_DEFINE(rdpm, MLXSW_REG_RDPM_ID, MLXSW_REG_RDPM_LEN);

/* reg_dscp_entry_e
 * Enable update of the specific entry
 * Access: Index
 */
MLXSW_ITEM8_INDEXED(reg, rdpm, dscp_entry_e, MLXSW_REG_RDPM_LAST_ENTRY, 7, 1,
		    -MLXSW_REG_RDPM_DSCP_ENTRY_REC_LEN, 0x00, false);

/* reg_dscp_entry_prio
 * Switch Priority
 * Access: RW
 */
MLXSW_ITEM8_INDEXED(reg, rdpm, dscp_entry_prio, MLXSW_REG_RDPM_LAST_ENTRY, 0, 4,
		    -MLXSW_REG_RDPM_DSCP_ENTRY_REC_LEN, 0x00, false);

static inline void mlxsw_reg_rdpm_pack(char *payload, unsigned short index,
				       u8 prio)
{
	mlxsw_reg_rdpm_dscp_entry_e_set(payload, index, 1);
	mlxsw_reg_rdpm_dscp_entry_prio_set(payload, index, prio);
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

/* RRCR - Router Rules Copy Register Layout
 * ----------------------------------------
 * This register is used for moving and copying route entry rules.
 */
#define MLXSW_REG_RRCR_ID 0x800F
#define MLXSW_REG_RRCR_LEN 0x24

MLXSW_REG_DEFINE(rrcr, MLXSW_REG_RRCR_ID, MLXSW_REG_RRCR_LEN);

enum mlxsw_reg_rrcr_op {
	/* Move rules */
	MLXSW_REG_RRCR_OP_MOVE,
	/* Copy rules */
	MLXSW_REG_RRCR_OP_COPY,
};

/* reg_rrcr_op
 * Access: WO
 */
MLXSW_ITEM32(reg, rrcr, op, 0x00, 28, 4);

/* reg_rrcr_offset
 * Offset within the region from which to copy/move.
 * Access: Index
 */
MLXSW_ITEM32(reg, rrcr, offset, 0x00, 0, 16);

/* reg_rrcr_size
 * The number of rules to copy/move.
 * Access: WO
 */
MLXSW_ITEM32(reg, rrcr, size, 0x04, 0, 16);

/* reg_rrcr_table_id
 * Identifier of the table on which to perform the operation. Encoding is the
 * same as in RTAR.key_type
 * Access: Index
 */
MLXSW_ITEM32(reg, rrcr, table_id, 0x10, 0, 4);

/* reg_rrcr_dest_offset
 * Offset within the region to which to copy/move
 * Access: Index
 */
MLXSW_ITEM32(reg, rrcr, dest_offset, 0x20, 0, 16);

static inline void mlxsw_reg_rrcr_pack(char *payload, enum mlxsw_reg_rrcr_op op,
				       u16 offset, u16 size,
				       enum mlxsw_reg_rtar_key_type table_id,
				       u16 dest_offset)
{
	MLXSW_REG_ZERO(rrcr, payload);
	mlxsw_reg_rrcr_op_set(payload, op);
	mlxsw_reg_rrcr_offset_set(payload, offset);
	mlxsw_reg_rrcr_size_set(payload, size);
	mlxsw_reg_rrcr_table_id_set(payload, table_id);
	mlxsw_reg_rrcr_dest_offset_set(payload, dest_offset);
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
 * The least significant bits must be '0' if the prefix_len is smaller
 * than 128 for IPv6 or smaller than 32 for IPv4.
 * IPv4 address uses bits dip[31:0] and bits dip[127:32] are reserved.
 * Access: Index
 */
MLXSW_ITEM32(reg, ralue, dip4, 0x18, 0, 32);
MLXSW_ITEM_BUF(reg, ralue, dip6, 0x0C, 16);

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

/* reg_ralue_ip2me_v
 * Valid bit for the tunnel_ptr field.
 * If valid = 0 then trap to CPU as IP2ME trap ID.
 * If valid = 1 and the packet format allows NVE or IPinIP tunnel
 * decapsulation then tunnel decapsulation is done.
 * If valid = 1 and packet format does not allow NVE or IPinIP tunnel
 * decapsulation then trap as IP2ME trap ID.
 * Only relevant in case of IP2ME action.
 * Access: RW
 */
MLXSW_ITEM32(reg, ralue, ip2me_v, 0x24, 31, 1);

/* reg_ralue_ip2me_tunnel_ptr
 * Tunnel Pointer for NVE or IPinIP tunnel decapsulation.
 * For Spectrum, pointer to KVD Linear.
 * Only relevant in case of IP2ME action.
 * Access: RW
 */
MLXSW_ITEM32(reg, ralue, ip2me_tunnel_ptr, 0x24, 0, 24);

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

static inline void mlxsw_reg_ralue_pack6(char *payload,
					 enum mlxsw_reg_ralxx_protocol protocol,
					 enum mlxsw_reg_ralue_op op,
					 u16 virtual_router, u8 prefix_len,
					 const void *dip)
{
	mlxsw_reg_ralue_pack(payload, protocol, op, virtual_router, prefix_len);
	mlxsw_reg_ralue_dip6_memcpy_to(payload, dip);
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

static inline void
mlxsw_reg_ralue_act_ip2me_tun_pack(char *payload, u32 tunnel_ptr)
{
	mlxsw_reg_ralue_action_type_set(payload,
					MLXSW_REG_RALUE_ACTION_TYPE_IP2ME);
	mlxsw_reg_ralue_ip2me_v_set(payload, 1);
	mlxsw_reg_ralue_ip2me_tunnel_ptr_set(payload, tunnel_ptr);
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
MLXSW_ITEM_BUF(reg, rauht, dip6, 0x10, 16);

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

static inline void mlxsw_reg_rauht_pack6(char *payload,
					 enum mlxsw_reg_rauht_op op, u16 rif,
					 const char *mac, const char *dip)
{
	mlxsw_reg_rauht_pack(payload, op, rif, mac);
	mlxsw_reg_rauht_type_set(payload, MLXSW_REG_RAUHT_TYPE_IPV6);
	mlxsw_reg_rauht_dip6_memcpy_to(payload, dip);
}

static inline void mlxsw_reg_rauht_pack_counter(char *payload,
						u64 counter_index)
{
	mlxsw_reg_rauht_counter_index_set(payload, counter_index);
	mlxsw_reg_rauht_counter_set_type_set(payload,
					     MLXSW_REG_FLOW_COUNTER_SET_TYPE_PACKETS_BYTES);
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

#define MLXSW_REG_RAUHTD_IPV6_ENT_LEN 0x20

/* reg_rauhtd_ipv6_ent_a
 * Activity. Set for new entries. Set if a packet lookup has hit on the
 * specific entry.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, rauhtd, ipv6_ent_a, MLXSW_REG_RAUHTD_BASE_LEN, 16, 1,
		     MLXSW_REG_RAUHTD_IPV6_ENT_LEN, 0x00, false);

/* reg_rauhtd_ipv6_ent_rif
 * Router interface.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, rauhtd, ipv6_ent_rif, MLXSW_REG_RAUHTD_BASE_LEN, 0,
		     16, MLXSW_REG_RAUHTD_IPV6_ENT_LEN, 0x00, false);

/* reg_rauhtd_ipv6_ent_dip
 * Destination IPv6 address.
 * Access: RO
 */
MLXSW_ITEM_BUF_INDEXED(reg, rauhtd, ipv6_ent_dip, MLXSW_REG_RAUHTD_BASE_LEN,
		       16, MLXSW_REG_RAUHTD_IPV6_ENT_LEN, 0x10);

static inline void mlxsw_reg_rauhtd_ent_ipv4_unpack(char *payload,
						    int ent_index, u16 *p_rif,
						    u32 *p_dip)
{
	*p_rif = mlxsw_reg_rauhtd_ipv4_ent_rif_get(payload, ent_index);
	*p_dip = mlxsw_reg_rauhtd_ipv4_ent_dip_get(payload, ent_index);
}

static inline void mlxsw_reg_rauhtd_ent_ipv6_unpack(char *payload,
						    int rec_index, u16 *p_rif,
						    char *p_dip)
{
	*p_rif = mlxsw_reg_rauhtd_ipv6_ent_rif_get(payload, rec_index);
	mlxsw_reg_rauhtd_ipv6_ent_dip_memcpy_from(payload, rec_index, p_dip);
}

/* RTDP - Routing Tunnel Decap Properties Register
 * -----------------------------------------------
 * The RTDP register is used for configuring the tunnel decap properties of NVE
 * and IPinIP.
 */
#define MLXSW_REG_RTDP_ID 0x8020
#define MLXSW_REG_RTDP_LEN 0x44

MLXSW_REG_DEFINE(rtdp, MLXSW_REG_RTDP_ID, MLXSW_REG_RTDP_LEN);

enum mlxsw_reg_rtdp_type {
	MLXSW_REG_RTDP_TYPE_NVE,
	MLXSW_REG_RTDP_TYPE_IPIP,
};

/* reg_rtdp_type
 * Type of the RTDP entry as per enum mlxsw_reg_rtdp_type.
 * Access: RW
 */
MLXSW_ITEM32(reg, rtdp, type, 0x00, 28, 4);

/* reg_rtdp_tunnel_index
 * Index to the Decap entry.
 * For Spectrum, Index to KVD Linear.
 * Access: Index
 */
MLXSW_ITEM32(reg, rtdp, tunnel_index, 0x00, 0, 24);

/* reg_rtdp_egress_router_interface
 * Underlay egress router interface.
 * Valid range is from 0 to cap_max_router_interfaces - 1
 * Access: RW
 */
MLXSW_ITEM32(reg, rtdp, egress_router_interface, 0x40, 0, 16);

/* IPinIP */

/* reg_rtdp_ipip_irif
 * Ingress Router Interface for the overlay router
 * Access: RW
 */
MLXSW_ITEM32(reg, rtdp, ipip_irif, 0x04, 16, 16);

enum mlxsw_reg_rtdp_ipip_sip_check {
	/* No sip checks. */
	MLXSW_REG_RTDP_IPIP_SIP_CHECK_NO,
	/* Filter packet if underlay is not IPv4 or if underlay SIP does not
	 * equal ipv4_usip.
	 */
	MLXSW_REG_RTDP_IPIP_SIP_CHECK_FILTER_IPV4,
	/* Filter packet if underlay is not IPv6 or if underlay SIP does not
	 * equal ipv6_usip.
	 */
	MLXSW_REG_RTDP_IPIP_SIP_CHECK_FILTER_IPV6 = 3,
};

/* reg_rtdp_ipip_sip_check
 * SIP check to perform. If decapsulation failed due to these configurations
 * then trap_id is IPIP_DECAP_ERROR.
 * Access: RW
 */
MLXSW_ITEM32(reg, rtdp, ipip_sip_check, 0x04, 0, 3);

/* If set, allow decapsulation of IPinIP (without GRE). */
#define MLXSW_REG_RTDP_IPIP_TYPE_CHECK_ALLOW_IPIP	BIT(0)
/* If set, allow decapsulation of IPinGREinIP without a key. */
#define MLXSW_REG_RTDP_IPIP_TYPE_CHECK_ALLOW_GRE	BIT(1)
/* If set, allow decapsulation of IPinGREinIP with a key. */
#define MLXSW_REG_RTDP_IPIP_TYPE_CHECK_ALLOW_GRE_KEY	BIT(2)

/* reg_rtdp_ipip_type_check
 * Flags as per MLXSW_REG_RTDP_IPIP_TYPE_CHECK_*. If decapsulation failed due to
 * these configurations then trap_id is IPIP_DECAP_ERROR.
 * Access: RW
 */
MLXSW_ITEM32(reg, rtdp, ipip_type_check, 0x08, 24, 3);

/* reg_rtdp_ipip_gre_key_check
 * Whether GRE key should be checked. When check is enabled:
 * - A packet received as IPinIP (without GRE) will always pass.
 * - A packet received as IPinGREinIP without a key will not pass the check.
 * - A packet received as IPinGREinIP with a key will pass the check only if the
 *   key in the packet is equal to expected_gre_key.
 * If decapsulation failed due to GRE key then trap_id is IPIP_DECAP_ERROR.
 * Access: RW
 */
MLXSW_ITEM32(reg, rtdp, ipip_gre_key_check, 0x08, 23, 1);

/* reg_rtdp_ipip_ipv4_usip
 * Underlay IPv4 address for ipv4 source address check.
 * Reserved when sip_check is not '1'.
 * Access: RW
 */
MLXSW_ITEM32(reg, rtdp, ipip_ipv4_usip, 0x0C, 0, 32);

/* reg_rtdp_ipip_ipv6_usip_ptr
 * This field is valid when sip_check is "sipv6 check explicitly". This is a
 * pointer to the IPv6 DIP which is configured by RIPS. For Spectrum, the index
 * is to the KVD linear.
 * Reserved when sip_check is not MLXSW_REG_RTDP_IPIP_SIP_CHECK_FILTER_IPV6.
 * Access: RW
 */
MLXSW_ITEM32(reg, rtdp, ipip_ipv6_usip_ptr, 0x10, 0, 24);

/* reg_rtdp_ipip_expected_gre_key
 * GRE key for checking.
 * Reserved when gre_key_check is '0'.
 * Access: RW
 */
MLXSW_ITEM32(reg, rtdp, ipip_expected_gre_key, 0x14, 0, 32);

static inline void mlxsw_reg_rtdp_pack(char *payload,
				       enum mlxsw_reg_rtdp_type type,
				       u32 tunnel_index)
{
	MLXSW_REG_ZERO(rtdp, payload);
	mlxsw_reg_rtdp_type_set(payload, type);
	mlxsw_reg_rtdp_tunnel_index_set(payload, tunnel_index);
}

static inline void
mlxsw_reg_rtdp_ipip_pack(char *payload, u16 irif,
			 enum mlxsw_reg_rtdp_ipip_sip_check sip_check,
			 unsigned int type_check, bool gre_key_check,
			 u32 expected_gre_key)
{
	mlxsw_reg_rtdp_ipip_irif_set(payload, irif);
	mlxsw_reg_rtdp_ipip_sip_check_set(payload, sip_check);
	mlxsw_reg_rtdp_ipip_type_check_set(payload, type_check);
	mlxsw_reg_rtdp_ipip_gre_key_check_set(payload, gre_key_check);
	mlxsw_reg_rtdp_ipip_expected_gre_key_set(payload, expected_gre_key);
}

static inline void
mlxsw_reg_rtdp_ipip4_pack(char *payload, u16 irif,
			  enum mlxsw_reg_rtdp_ipip_sip_check sip_check,
			  unsigned int type_check, bool gre_key_check,
			  u32 ipv4_usip, u32 expected_gre_key)
{
	mlxsw_reg_rtdp_ipip_pack(payload, irif, sip_check, type_check,
				 gre_key_check, expected_gre_key);
	mlxsw_reg_rtdp_ipip_ipv4_usip_set(payload, ipv4_usip);
}

static inline void
mlxsw_reg_rtdp_ipip6_pack(char *payload, u16 irif,
			  enum mlxsw_reg_rtdp_ipip_sip_check sip_check,
			  unsigned int type_check, bool gre_key_check,
			  u32 ipv6_usip_ptr, u32 expected_gre_key)
{
	mlxsw_reg_rtdp_ipip_pack(payload, irif, sip_check, type_check,
				 gre_key_check, expected_gre_key);
	mlxsw_reg_rtdp_ipip_ipv6_usip_ptr_set(payload, ipv6_usip_ptr);
}

/* RIPS - Router IP version Six Register
 * -------------------------------------
 * The RIPS register is used to store IPv6 addresses for use by the NVE and
 * IPinIP
 */
#define MLXSW_REG_RIPS_ID 0x8021
#define MLXSW_REG_RIPS_LEN 0x14

MLXSW_REG_DEFINE(rips, MLXSW_REG_RIPS_ID, MLXSW_REG_RIPS_LEN);

/* reg_rips_index
 * Index to IPv6 address.
 * For Spectrum, the index is to the KVD linear.
 * Access: Index
 */
MLXSW_ITEM32(reg, rips, index, 0x00, 0, 24);

/* reg_rips_ipv6
 * IPv6 address
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, rips, ipv6, 0x04, 16);

static inline void mlxsw_reg_rips_pack(char *payload, u32 index,
				       const struct in6_addr *ipv6)
{
	MLXSW_REG_ZERO(rips, payload);
	mlxsw_reg_rips_index_set(payload, index);
	mlxsw_reg_rips_ipv6_memcpy_to(payload, (const char *)ipv6);
}

/* RATRAD - Router Adjacency Table Activity Dump Register
 * ------------------------------------------------------
 * The RATRAD register is used to dump and optionally clear activity bits of
 * router adjacency table entries.
 */
#define MLXSW_REG_RATRAD_ID 0x8022
#define MLXSW_REG_RATRAD_LEN 0x210

MLXSW_REG_DEFINE(ratrad, MLXSW_REG_RATRAD_ID, MLXSW_REG_RATRAD_LEN);

enum {
	/* Read activity */
	MLXSW_REG_RATRAD_OP_READ_ACTIVITY,
	/* Read and clear activity */
	MLXSW_REG_RATRAD_OP_READ_CLEAR_ACTIVITY,
};

/* reg_ratrad_op
 * Access: Operation
 */
MLXSW_ITEM32(reg, ratrad, op, 0x00, 30, 2);

/* reg_ratrad_ecmp_size
 * ecmp_size is the amount of sequential entries from adjacency_index. Valid
 * ranges:
 * Spectrum-1: 32-64, 512, 1024, 2048, 4096
 * Spectrum-2/3: 32-128, 256, 512, 1024, 2048, 4096
 * Access: Index
 */
MLXSW_ITEM32(reg, ratrad, ecmp_size, 0x00, 0, 13);

/* reg_ratrad_adjacency_index
 * Index into the adjacency table.
 * Access: Index
 */
MLXSW_ITEM32(reg, ratrad, adjacency_index, 0x04, 0, 24);

/* reg_ratrad_activity_vector
 * Activity bit per adjacency index.
 * Bits higher than ecmp_size are reserved.
 * Access: RO
 */
MLXSW_ITEM_BIT_ARRAY(reg, ratrad, activity_vector, 0x10, 0x200, 1);

static inline void mlxsw_reg_ratrad_pack(char *payload, u32 adjacency_index,
					 u16 ecmp_size)
{
	MLXSW_REG_ZERO(ratrad, payload);
	mlxsw_reg_ratrad_op_set(payload,
				MLXSW_REG_RATRAD_OP_READ_CLEAR_ACTIVITY);
	mlxsw_reg_ratrad_ecmp_size_set(payload, ecmp_size);
	mlxsw_reg_ratrad_adjacency_index_set(payload, adjacency_index);
}

/* RIGR-V2 - Router Interface Group Register Version 2
 * ---------------------------------------------------
 * The RIGR_V2 register is used to add, remove and query egress interface list
 * of a multicast forwarding entry.
 */
#define MLXSW_REG_RIGR2_ID 0x8023
#define MLXSW_REG_RIGR2_LEN 0xB0

#define MLXSW_REG_RIGR2_MAX_ERIFS 32

MLXSW_REG_DEFINE(rigr2, MLXSW_REG_RIGR2_ID, MLXSW_REG_RIGR2_LEN);

/* reg_rigr2_rigr_index
 * KVD Linear index.
 * Access: Index
 */
MLXSW_ITEM32(reg, rigr2, rigr_index, 0x04, 0, 24);

/* reg_rigr2_vnext
 * Next RIGR Index is valid.
 * Access: RW
 */
MLXSW_ITEM32(reg, rigr2, vnext, 0x08, 31, 1);

/* reg_rigr2_next_rigr_index
 * Next RIGR Index. The index is to the KVD linear.
 * Reserved when vnxet = '0'.
 * Access: RW
 */
MLXSW_ITEM32(reg, rigr2, next_rigr_index, 0x08, 0, 24);

/* reg_rigr2_vrmid
 * RMID Index is valid.
 * Access: RW
 */
MLXSW_ITEM32(reg, rigr2, vrmid, 0x20, 31, 1);

/* reg_rigr2_rmid_index
 * RMID Index.
 * Range 0 .. max_mid - 1
 * Reserved when vrmid = '0'.
 * The index is to the Port Group Table (PGT)
 * Access: RW
 */
MLXSW_ITEM32(reg, rigr2, rmid_index, 0x20, 0, 16);

/* reg_rigr2_erif_entry_v
 * Egress Router Interface is valid.
 * Note that low-entries must be set if high-entries are set. For
 * example: if erif_entry[2].v is set then erif_entry[1].v and
 * erif_entry[0].v must be set.
 * Index can be from 0 to cap_mc_erif_list_entries-1
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, rigr2, erif_entry_v, 0x24, 31, 1, 4, 0, false);

/* reg_rigr2_erif_entry_erif
 * Egress Router Interface.
 * Valid range is from 0 to cap_max_router_interfaces - 1
 * Index can be from 0 to MLXSW_REG_RIGR2_MAX_ERIFS - 1
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, rigr2, erif_entry_erif, 0x24, 0, 16, 4, 0, false);

static inline void mlxsw_reg_rigr2_pack(char *payload, u32 rigr_index,
					bool vnext, u32 next_rigr_index)
{
	MLXSW_REG_ZERO(rigr2, payload);
	mlxsw_reg_rigr2_rigr_index_set(payload, rigr_index);
	mlxsw_reg_rigr2_vnext_set(payload, vnext);
	mlxsw_reg_rigr2_next_rigr_index_set(payload, next_rigr_index);
	mlxsw_reg_rigr2_vrmid_set(payload, 0);
	mlxsw_reg_rigr2_rmid_index_set(payload, 0);
}

static inline void mlxsw_reg_rigr2_erif_entry_pack(char *payload, int index,
						   bool v, u16 erif)
{
	mlxsw_reg_rigr2_erif_entry_v_set(payload, index, v);
	mlxsw_reg_rigr2_erif_entry_erif_set(payload, index, erif);
}

/* RECR-V2 - Router ECMP Configuration Version 2 Register
 * ------------------------------------------------------
 */
#define MLXSW_REG_RECR2_ID 0x8025
#define MLXSW_REG_RECR2_LEN 0x38

MLXSW_REG_DEFINE(recr2, MLXSW_REG_RECR2_ID, MLXSW_REG_RECR2_LEN);

/* reg_recr2_pp
 * Per-port configuration
 * Access: Index
 */
MLXSW_ITEM32(reg, recr2, pp, 0x00, 24, 1);

/* reg_recr2_sh
 * Symmetric hash
 * Access: RW
 */
MLXSW_ITEM32(reg, recr2, sh, 0x00, 8, 1);

/* reg_recr2_seed
 * Seed
 * Access: RW
 */
MLXSW_ITEM32(reg, recr2, seed, 0x08, 0, 32);

enum {
	/* Enable IPv4 fields if packet is not TCP and not UDP */
	MLXSW_REG_RECR2_IPV4_EN_NOT_TCP_NOT_UDP	= 3,
	/* Enable IPv4 fields if packet is TCP or UDP */
	MLXSW_REG_RECR2_IPV4_EN_TCP_UDP		= 4,
	/* Enable IPv6 fields if packet is not TCP and not UDP */
	MLXSW_REG_RECR2_IPV6_EN_NOT_TCP_NOT_UDP	= 5,
	/* Enable IPv6 fields if packet is TCP or UDP */
	MLXSW_REG_RECR2_IPV6_EN_TCP_UDP		= 6,
	/* Enable TCP/UDP header fields if packet is IPv4 */
	MLXSW_REG_RECR2_TCP_UDP_EN_IPV4		= 7,
	/* Enable TCP/UDP header fields if packet is IPv6 */
	MLXSW_REG_RECR2_TCP_UDP_EN_IPV6		= 8,

	__MLXSW_REG_RECR2_HEADER_CNT,
};

/* reg_recr2_outer_header_enables
 * Bit mask where each bit enables a specific layer to be included in
 * the hash calculation.
 * Access: RW
 */
MLXSW_ITEM_BIT_ARRAY(reg, recr2, outer_header_enables, 0x10, 0x04, 1);

enum {
	/* IPv4 Source IP */
	MLXSW_REG_RECR2_IPV4_SIP0			= 9,
	MLXSW_REG_RECR2_IPV4_SIP3			= 12,
	/* IPv4 Destination IP */
	MLXSW_REG_RECR2_IPV4_DIP0			= 13,
	MLXSW_REG_RECR2_IPV4_DIP3			= 16,
	/* IP Protocol */
	MLXSW_REG_RECR2_IPV4_PROTOCOL			= 17,
	/* IPv6 Source IP */
	MLXSW_REG_RECR2_IPV6_SIP0_7			= 21,
	MLXSW_REG_RECR2_IPV6_SIP8			= 29,
	MLXSW_REG_RECR2_IPV6_SIP15			= 36,
	/* IPv6 Destination IP */
	MLXSW_REG_RECR2_IPV6_DIP0_7			= 37,
	MLXSW_REG_RECR2_IPV6_DIP8			= 45,
	MLXSW_REG_RECR2_IPV6_DIP15			= 52,
	/* IPv6 Next Header */
	MLXSW_REG_RECR2_IPV6_NEXT_HEADER		= 53,
	/* IPv6 Flow Label */
	MLXSW_REG_RECR2_IPV6_FLOW_LABEL			= 57,
	/* TCP/UDP Source Port */
	MLXSW_REG_RECR2_TCP_UDP_SPORT			= 74,
	/* TCP/UDP Destination Port */
	MLXSW_REG_RECR2_TCP_UDP_DPORT			= 75,

	__MLXSW_REG_RECR2_FIELD_CNT,
};

/* reg_recr2_outer_header_fields_enable
 * Packet fields to enable for ECMP hash subject to outer_header_enable.
 * Access: RW
 */
MLXSW_ITEM_BIT_ARRAY(reg, recr2, outer_header_fields_enable, 0x14, 0x14, 1);

/* reg_recr2_inner_header_enables
 * Bit mask where each bit enables a specific inner layer to be included in the
 * hash calculation. Same values as reg_recr2_outer_header_enables.
 * Access: RW
 */
MLXSW_ITEM_BIT_ARRAY(reg, recr2, inner_header_enables, 0x2C, 0x04, 1);

enum {
	/* Inner IPv4 Source IP */
	MLXSW_REG_RECR2_INNER_IPV4_SIP0			= 3,
	MLXSW_REG_RECR2_INNER_IPV4_SIP3			= 6,
	/* Inner IPv4 Destination IP */
	MLXSW_REG_RECR2_INNER_IPV4_DIP0			= 7,
	MLXSW_REG_RECR2_INNER_IPV4_DIP3			= 10,
	/* Inner IP Protocol */
	MLXSW_REG_RECR2_INNER_IPV4_PROTOCOL		= 11,
	/* Inner IPv6 Source IP */
	MLXSW_REG_RECR2_INNER_IPV6_SIP0_7		= 12,
	MLXSW_REG_RECR2_INNER_IPV6_SIP8			= 20,
	MLXSW_REG_RECR2_INNER_IPV6_SIP15		= 27,
	/* Inner IPv6 Destination IP */
	MLXSW_REG_RECR2_INNER_IPV6_DIP0_7		= 28,
	MLXSW_REG_RECR2_INNER_IPV6_DIP8			= 36,
	MLXSW_REG_RECR2_INNER_IPV6_DIP15		= 43,
	/* Inner IPv6 Next Header */
	MLXSW_REG_RECR2_INNER_IPV6_NEXT_HEADER		= 44,
	/* Inner IPv6 Flow Label */
	MLXSW_REG_RECR2_INNER_IPV6_FLOW_LABEL		= 45,
	/* Inner TCP/UDP Source Port */
	MLXSW_REG_RECR2_INNER_TCP_UDP_SPORT		= 46,
	/* Inner TCP/UDP Destination Port */
	MLXSW_REG_RECR2_INNER_TCP_UDP_DPORT		= 47,

	__MLXSW_REG_RECR2_INNER_FIELD_CNT,
};

/* reg_recr2_inner_header_fields_enable
 * Inner packet fields to enable for ECMP hash subject to inner_header_enables.
 * Access: RW
 */
MLXSW_ITEM_BIT_ARRAY(reg, recr2, inner_header_fields_enable, 0x30, 0x08, 1);

static inline void mlxsw_reg_recr2_pack(char *payload, u32 seed)
{
	MLXSW_REG_ZERO(recr2, payload);
	mlxsw_reg_recr2_pp_set(payload, false);
	mlxsw_reg_recr2_sh_set(payload, true);
	mlxsw_reg_recr2_seed_set(payload, seed);
}

/* RMFT-V2 - Router Multicast Forwarding Table Version 2 Register
 * --------------------------------------------------------------
 * The RMFT_V2 register is used to configure and query the multicast table.
 */
#define MLXSW_REG_RMFT2_ID 0x8027
#define MLXSW_REG_RMFT2_LEN 0x174

MLXSW_REG_DEFINE(rmft2, MLXSW_REG_RMFT2_ID, MLXSW_REG_RMFT2_LEN);

/* reg_rmft2_v
 * Valid
 * Access: RW
 */
MLXSW_ITEM32(reg, rmft2, v, 0x00, 31, 1);

enum mlxsw_reg_rmft2_type {
	MLXSW_REG_RMFT2_TYPE_IPV4,
	MLXSW_REG_RMFT2_TYPE_IPV6
};

/* reg_rmft2_type
 * Access: Index
 */
MLXSW_ITEM32(reg, rmft2, type, 0x00, 28, 2);

enum mlxsw_sp_reg_rmft2_op {
	/* For Write:
	 * Write operation. Used to write a new entry to the table. All RW
	 * fields are relevant for new entry. Activity bit is set for new
	 * entries - Note write with v (Valid) 0 will delete the entry.
	 * For Query:
	 * Read operation
	 */
	MLXSW_REG_RMFT2_OP_READ_WRITE,
};

/* reg_rmft2_op
 * Operation.
 * Access: OP
 */
MLXSW_ITEM32(reg, rmft2, op, 0x00, 20, 2);

/* reg_rmft2_a
 * Activity. Set for new entries. Set if a packet lookup has hit on the specific
 * entry.
 * Access: RO
 */
MLXSW_ITEM32(reg, rmft2, a, 0x00, 16, 1);

/* reg_rmft2_offset
 * Offset within the multicast forwarding table to write to.
 * Access: Index
 */
MLXSW_ITEM32(reg, rmft2, offset, 0x00, 0, 16);

/* reg_rmft2_virtual_router
 * Virtual Router ID. Range from 0..cap_max_virtual_routers-1
 * Access: RW
 */
MLXSW_ITEM32(reg, rmft2, virtual_router, 0x04, 0, 16);

enum mlxsw_reg_rmft2_irif_mask {
	MLXSW_REG_RMFT2_IRIF_MASK_IGNORE,
	MLXSW_REG_RMFT2_IRIF_MASK_COMPARE
};

/* reg_rmft2_irif_mask
 * Ingress RIF mask.
 * Access: RW
 */
MLXSW_ITEM32(reg, rmft2, irif_mask, 0x08, 24, 1);

/* reg_rmft2_irif
 * Ingress RIF index.
 * Access: RW
 */
MLXSW_ITEM32(reg, rmft2, irif, 0x08, 0, 16);

/* reg_rmft2_dip{4,6}
 * Destination IPv4/6 address
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, rmft2, dip6, 0x10, 16);
MLXSW_ITEM32(reg, rmft2, dip4, 0x1C, 0, 32);

/* reg_rmft2_dip{4,6}_mask
 * A bit that is set directs the TCAM to compare the corresponding bit in key. A
 * bit that is clear directs the TCAM to ignore the corresponding bit in key.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, rmft2, dip6_mask, 0x20, 16);
MLXSW_ITEM32(reg, rmft2, dip4_mask, 0x2C, 0, 32);

/* reg_rmft2_sip{4,6}
 * Source IPv4/6 address
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, rmft2, sip6, 0x30, 16);
MLXSW_ITEM32(reg, rmft2, sip4, 0x3C, 0, 32);

/* reg_rmft2_sip{4,6}_mask
 * A bit that is set directs the TCAM to compare the corresponding bit in key. A
 * bit that is clear directs the TCAM to ignore the corresponding bit in key.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, rmft2, sip6_mask, 0x40, 16);
MLXSW_ITEM32(reg, rmft2, sip4_mask, 0x4C, 0, 32);

/* reg_rmft2_flexible_action_set
 * ACL action set. The only supported action types in this field and in any
 * action-set pointed from here are as follows:
 * 00h: ACTION_NULL
 * 01h: ACTION_MAC_TTL, only TTL configuration is supported.
 * 03h: ACTION_TRAP
 * 06h: ACTION_QOS
 * 08h: ACTION_POLICING_MONITORING
 * 10h: ACTION_ROUTER_MC
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, rmft2, flexible_action_set, 0x80,
	       MLXSW_REG_FLEX_ACTION_SET_LEN);

static inline void
mlxsw_reg_rmft2_common_pack(char *payload, bool v, u16 offset,
			    u16 virtual_router,
			    enum mlxsw_reg_rmft2_irif_mask irif_mask, u16 irif,
			    const char *flex_action_set)
{
	MLXSW_REG_ZERO(rmft2, payload);
	mlxsw_reg_rmft2_v_set(payload, v);
	mlxsw_reg_rmft2_op_set(payload, MLXSW_REG_RMFT2_OP_READ_WRITE);
	mlxsw_reg_rmft2_offset_set(payload, offset);
	mlxsw_reg_rmft2_virtual_router_set(payload, virtual_router);
	mlxsw_reg_rmft2_irif_mask_set(payload, irif_mask);
	mlxsw_reg_rmft2_irif_set(payload, irif);
	if (flex_action_set)
		mlxsw_reg_rmft2_flexible_action_set_memcpy_to(payload,
							      flex_action_set);
}

static inline void
mlxsw_reg_rmft2_ipv4_pack(char *payload, bool v, u16 offset, u16 virtual_router,
			  enum mlxsw_reg_rmft2_irif_mask irif_mask, u16 irif,
			  u32 dip4, u32 dip4_mask, u32 sip4, u32 sip4_mask,
			  const char *flexible_action_set)
{
	mlxsw_reg_rmft2_common_pack(payload, v, offset, virtual_router,
				    irif_mask, irif, flexible_action_set);
	mlxsw_reg_rmft2_type_set(payload, MLXSW_REG_RMFT2_TYPE_IPV4);
	mlxsw_reg_rmft2_dip4_set(payload, dip4);
	mlxsw_reg_rmft2_dip4_mask_set(payload, dip4_mask);
	mlxsw_reg_rmft2_sip4_set(payload, sip4);
	mlxsw_reg_rmft2_sip4_mask_set(payload, sip4_mask);
}

static inline void
mlxsw_reg_rmft2_ipv6_pack(char *payload, bool v, u16 offset, u16 virtual_router,
			  enum mlxsw_reg_rmft2_irif_mask irif_mask, u16 irif,
			  struct in6_addr dip6, struct in6_addr dip6_mask,
			  struct in6_addr sip6, struct in6_addr sip6_mask,
			  const char *flexible_action_set)
{
	mlxsw_reg_rmft2_common_pack(payload, v, offset, virtual_router,
				    irif_mask, irif, flexible_action_set);
	mlxsw_reg_rmft2_type_set(payload, MLXSW_REG_RMFT2_TYPE_IPV6);
	mlxsw_reg_rmft2_dip6_memcpy_to(payload, (void *)&dip6);
	mlxsw_reg_rmft2_dip6_mask_memcpy_to(payload, (void *)&dip6_mask);
	mlxsw_reg_rmft2_sip6_memcpy_to(payload, (void *)&sip6);
	mlxsw_reg_rmft2_sip6_mask_memcpy_to(payload, (void *)&sip6_mask);
}

/* REIV - Router Egress Interface to VID Register
 * ----------------------------------------------
 * The REIV register maps {eRIF, egress_port} -> VID.
 * This mapping is done at the egress, after the ACLs.
 * This mapping always takes effect after router, regardless of cast
 * (for unicast/multicast/port-base multicast), regardless of eRIF type and
 * regardless of bridge decisions (e.g. SFD for unicast or SMPE).
 * Reserved when the RIF is a loopback RIF.
 *
 * Note: Reserved when legacy bridge model is used.
 */
#define MLXSW_REG_REIV_ID 0x8034
#define MLXSW_REG_REIV_BASE_LEN 0x20 /* base length, without records */
#define MLXSW_REG_REIV_REC_LEN 0x04 /* record length */
#define MLXSW_REG_REIV_REC_MAX_COUNT 256 /* firmware limitation */
#define MLXSW_REG_REIV_LEN (MLXSW_REG_REIV_BASE_LEN +	\
			    MLXSW_REG_REIV_REC_LEN *	\
			    MLXSW_REG_REIV_REC_MAX_COUNT)

MLXSW_REG_DEFINE(reiv, MLXSW_REG_REIV_ID, MLXSW_REG_REIV_LEN);

/* reg_reiv_port_page
 * Port page - elport_record[0] is 256*port_page.
 * Access: Index
 */
MLXSW_ITEM32(reg, reiv, port_page, 0x00, 0, 4);

/* reg_reiv_erif
 * Egress RIF.
 * Range is 0..cap_max_router_interfaces-1.
 * Access: Index
 */
MLXSW_ITEM32(reg, reiv, erif, 0x04, 0, 16);

/* reg_reiv_rec_update
 * Update enable (when write):
 * 0 - Do not update the entry.
 * 1 - Update the entry.
 * Access: OP
 */
MLXSW_ITEM32_INDEXED(reg, reiv, rec_update, MLXSW_REG_REIV_BASE_LEN, 31, 1,
		     MLXSW_REG_REIV_REC_LEN, 0x00, false);

/* reg_reiv_rec_evid
 * Egress VID.
 * Range is 0..4095.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, reiv, rec_evid, MLXSW_REG_REIV_BASE_LEN, 0, 12,
		     MLXSW_REG_REIV_REC_LEN, 0x00, false);

static inline void mlxsw_reg_reiv_pack(char *payload, u8 port_page, u16 erif)
{
	MLXSW_REG_ZERO(reiv, payload);
	mlxsw_reg_reiv_port_page_set(payload, port_page);
	mlxsw_reg_reiv_erif_set(payload, erif);
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

/* FORE - Fan Out of Range Event Register
 * --------------------------------------
 * This register reports the status of the controlled fans compared to the
 * range defined by the MFSL register.
 */
#define MLXSW_REG_FORE_ID 0x9007
#define MLXSW_REG_FORE_LEN 0x0C

MLXSW_REG_DEFINE(fore, MLXSW_REG_FORE_ID, MLXSW_REG_FORE_LEN);

/* fan_under_limit
 * Fan speed is below the low limit defined in MFSL register. Each bit relates
 * to a single tachometer and indicates the specific tachometer reading is
 * below the threshold.
 * Access: RO
 */
MLXSW_ITEM32(reg, fore, fan_under_limit, 0x00, 16, 10);

static inline void mlxsw_reg_fore_unpack(char *payload, u8 tacho,
					 bool *fault)
{
	u16 limit;

	if (fault) {
		limit = mlxsw_reg_fore_fan_under_limit_get(payload);
		*fault = limit & BIT(tacho);
	}
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

/* reg_mtmp_slot_index
 * Slot index (0: Main board).
 * Access: Index
 */
MLXSW_ITEM32(reg, mtmp, slot_index, 0x00, 16, 4);

#define MLXSW_REG_MTMP_MODULE_INDEX_MIN 64
#define MLXSW_REG_MTMP_GBOX_INDEX_MIN 256
/* reg_mtmp_sensor_index
 * Sensors index to access.
 * 64-127 of sensor_index are mapped to the SFP+/QSFP modules sequentially
 * (module 0 is mapped to sensor_index 64).
 * Access: Index
 */
MLXSW_ITEM32(reg, mtmp, sensor_index, 0x00, 0, 12);

/* Convert to milli degrees Celsius */
#define MLXSW_REG_MTMP_TEMP_TO_MC(val) ({ typeof(val) v_ = (val); \
					  ((v_) >= 0) ? ((v_) * 125) : \
					  ((s16)((GENMASK(15, 0) + (v_) + 1) \
					   * 125)); })

/* reg_mtmp_max_operational_temperature
 * The highest temperature in the nominal operational range. Reading is in
 * 0.125 Celsius degrees units.
 * In case of module this is SFF critical temperature threshold.
 * Access: RO
 */
MLXSW_ITEM32(reg, mtmp, max_operational_temperature, 0x04, 16, 16);

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

/* reg_mtmp_tee
 * Temperature Event Enable.
 * 0 - Do not generate event
 * 1 - Generate event
 * 2 - Generate single event
 * Access: RW
 */

enum mlxsw_reg_mtmp_tee {
	MLXSW_REG_MTMP_TEE_NO_EVENT,
	MLXSW_REG_MTMP_TEE_GENERATE_EVENT,
	MLXSW_REG_MTMP_TEE_GENERATE_SINGLE_EVENT,
};

MLXSW_ITEM32(reg, mtmp, tee, 0x0C, 30, 2);

#define MLXSW_REG_MTMP_THRESH_HI 0x348	/* 105 Celsius */

/* reg_mtmp_temperature_threshold_hi
 * High threshold for Temperature Warning Event. In 0.125 Celsius.
 * Access: RW
 */
MLXSW_ITEM32(reg, mtmp, temperature_threshold_hi, 0x0C, 0, 16);

#define MLXSW_REG_MTMP_HYSTERESIS_TEMP 0x28 /* 5 Celsius */
/* reg_mtmp_temperature_threshold_lo
 * Low threshold for Temperature Warning Event. In 0.125 Celsius.
 * Access: RW
 */
MLXSW_ITEM32(reg, mtmp, temperature_threshold_lo, 0x10, 0, 16);

#define MLXSW_REG_MTMP_SENSOR_NAME_SIZE 8

/* reg_mtmp_sensor_name
 * Sensor Name
 * Access: RO
 */
MLXSW_ITEM_BUF(reg, mtmp, sensor_name, 0x18, MLXSW_REG_MTMP_SENSOR_NAME_SIZE);

static inline void mlxsw_reg_mtmp_pack(char *payload, u8 slot_index,
				       u16 sensor_index, bool max_temp_enable,
				       bool max_temp_reset)
{
	MLXSW_REG_ZERO(mtmp, payload);
	mlxsw_reg_mtmp_slot_index_set(payload, slot_index);
	mlxsw_reg_mtmp_sensor_index_set(payload, sensor_index);
	mlxsw_reg_mtmp_mte_set(payload, max_temp_enable);
	mlxsw_reg_mtmp_mtr_set(payload, max_temp_reset);
	mlxsw_reg_mtmp_temperature_threshold_hi_set(payload,
						    MLXSW_REG_MTMP_THRESH_HI);
}

static inline void mlxsw_reg_mtmp_unpack(char *payload, int *p_temp,
					 int *p_max_temp, int *p_temp_hi,
					 int *p_max_oper_temp,
					 char *sensor_name)
{
	s16 temp;

	if (p_temp) {
		temp = mlxsw_reg_mtmp_temperature_get(payload);
		*p_temp = MLXSW_REG_MTMP_TEMP_TO_MC(temp);
	}
	if (p_max_temp) {
		temp = mlxsw_reg_mtmp_max_temperature_get(payload);
		*p_max_temp = MLXSW_REG_MTMP_TEMP_TO_MC(temp);
	}
	if (p_temp_hi) {
		temp = mlxsw_reg_mtmp_temperature_threshold_hi_get(payload);
		*p_temp_hi = MLXSW_REG_MTMP_TEMP_TO_MC(temp);
	}
	if (p_max_oper_temp) {
		temp = mlxsw_reg_mtmp_max_operational_temperature_get(payload);
		*p_max_oper_temp = MLXSW_REG_MTMP_TEMP_TO_MC(temp);
	}
	if (sensor_name)
		mlxsw_reg_mtmp_sensor_name_memcpy_from(payload, sensor_name);
}

/* MTWE - Management Temperature Warning Event
 * -------------------------------------------
 * This register is used for over temperature warning.
 */
#define MLXSW_REG_MTWE_ID 0x900B
#define MLXSW_REG_MTWE_LEN 0x10

MLXSW_REG_DEFINE(mtwe, MLXSW_REG_MTWE_ID, MLXSW_REG_MTWE_LEN);

/* reg_mtwe_sensor_warning
 * Bit vector indicating which of the sensor reading is above threshold.
 * Address 00h bit31 is sensor_warning[127].
 * Address 0Ch bit0 is sensor_warning[0].
 * Access: RO
 */
MLXSW_ITEM_BIT_ARRAY(reg, mtwe, sensor_warning, 0x0, 0x10, 1);

/* MTBR - Management Temperature Bulk Register
 * -------------------------------------------
 * This register is used for bulk temperature reading.
 */
#define MLXSW_REG_MTBR_ID 0x900F
#define MLXSW_REG_MTBR_BASE_LEN 0x10 /* base length, without records */
#define MLXSW_REG_MTBR_REC_LEN 0x04 /* record length */
#define MLXSW_REG_MTBR_REC_MAX_COUNT 47 /* firmware limitation */
#define MLXSW_REG_MTBR_LEN (MLXSW_REG_MTBR_BASE_LEN +	\
			    MLXSW_REG_MTBR_REC_LEN *	\
			    MLXSW_REG_MTBR_REC_MAX_COUNT)

MLXSW_REG_DEFINE(mtbr, MLXSW_REG_MTBR_ID, MLXSW_REG_MTBR_LEN);

/* reg_mtbr_slot_index
 * Slot index (0: Main board).
 * Access: Index
 */
MLXSW_ITEM32(reg, mtbr, slot_index, 0x00, 16, 4);

/* reg_mtbr_base_sensor_index
 * Base sensors index to access (0 - ASIC sensor, 1-63 - ambient sensors,
 * 64-127 are mapped to the SFP+/QSFP modules sequentially).
 * Access: Index
 */
MLXSW_ITEM32(reg, mtbr, base_sensor_index, 0x00, 0, 12);

/* reg_mtbr_num_rec
 * Request: Number of records to read
 * Response: Number of records read
 * See above description for more details.
 * Range 1..255
 * Access: RW
 */
MLXSW_ITEM32(reg, mtbr, num_rec, 0x04, 0, 8);

/* reg_mtbr_rec_max_temp
 * The highest measured temperature from the sensor.
 * When the bit mte is cleared, the field max_temperature is reserved.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, mtbr, rec_max_temp, MLXSW_REG_MTBR_BASE_LEN, 16,
		     16, MLXSW_REG_MTBR_REC_LEN, 0x00, false);

/* reg_mtbr_rec_temp
 * Temperature reading from the sensor. Reading is in 0..125 Celsius
 * degrees units.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, mtbr, rec_temp, MLXSW_REG_MTBR_BASE_LEN, 0, 16,
		     MLXSW_REG_MTBR_REC_LEN, 0x00, false);

static inline void mlxsw_reg_mtbr_pack(char *payload, u8 slot_index,
				       u16 base_sensor_index, u8 num_rec)
{
	MLXSW_REG_ZERO(mtbr, payload);
	mlxsw_reg_mtbr_slot_index_set(payload, slot_index);
	mlxsw_reg_mtbr_base_sensor_index_set(payload, base_sensor_index);
	mlxsw_reg_mtbr_num_rec_set(payload, num_rec);
}

/* Error codes from temperatute reading */
enum mlxsw_reg_mtbr_temp_status {
	MLXSW_REG_MTBR_NO_CONN		= 0x8000,
	MLXSW_REG_MTBR_NO_TEMP_SENS	= 0x8001,
	MLXSW_REG_MTBR_INDEX_NA		= 0x8002,
	MLXSW_REG_MTBR_BAD_SENS_INFO	= 0x8003,
};

/* Base index for reading modules temperature */
#define MLXSW_REG_MTBR_BASE_MODULE_INDEX 64

static inline void mlxsw_reg_mtbr_temp_unpack(char *payload, int rec_ind,
					      u16 *p_temp, u16 *p_max_temp)
{
	if (p_temp)
		*p_temp = mlxsw_reg_mtbr_rec_temp_get(payload, rec_ind);
	if (p_max_temp)
		*p_max_temp = mlxsw_reg_mtbr_rec_max_temp_get(payload, rec_ind);
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

/* reg_mcia_slot_index
 * Slot index (0: Main board)
 * Access: Index
 */
MLXSW_ITEM32(reg, mcia, slot, 0x00, 12, 4);

enum {
	MLXSW_REG_MCIA_STATUS_GOOD = 0,
	/* No response from module's EEPROM. */
	MLXSW_REG_MCIA_STATUS_NO_EEPROM_MODULE = 1,
	/* Module type not supported by the device. */
	MLXSW_REG_MCIA_STATUS_MODULE_NOT_SUPPORTED = 2,
	/* No module present indication. */
	MLXSW_REG_MCIA_STATUS_MODULE_NOT_CONNECTED = 3,
	/* Error occurred while trying to access module's EEPROM using I2C. */
	MLXSW_REG_MCIA_STATUS_I2C_ERROR = 9,
	/* Module is disabled. */
	MLXSW_REG_MCIA_STATUS_MODULE_DISABLED = 16,
};

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

/* reg_mcia_bank_number
 * Bank number.
 * Access: Index
 */
MLXSW_ITEM32(reg, mcia, bank_number, 0x08, 16, 8);

/* reg_mcia_size
 * Number of bytes to read/write (up to 48 bytes).
 * Access: RW
 */
MLXSW_ITEM32(reg, mcia, size, 0x08, 0, 16);

#define MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH	256
#define MLXSW_REG_MCIA_EEPROM_UP_PAGE_LENGTH	128
#define MLXSW_REG_MCIA_EEPROM_SIZE		48
#define MLXSW_REG_MCIA_I2C_ADDR_LOW		0x50
#define MLXSW_REG_MCIA_I2C_ADDR_HIGH		0x51
#define MLXSW_REG_MCIA_PAGE0_LO_OFF		0xa0
#define MLXSW_REG_MCIA_TH_ITEM_SIZE		2
#define MLXSW_REG_MCIA_TH_PAGE_NUM		3
#define MLXSW_REG_MCIA_TH_PAGE_CMIS_NUM		2
#define MLXSW_REG_MCIA_PAGE0_LO			0
#define MLXSW_REG_MCIA_TH_PAGE_OFF		0x80
#define MLXSW_REG_MCIA_EEPROM_CMIS_FLAT_MEMORY	BIT(7)

enum mlxsw_reg_mcia_eeprom_module_info_rev_id {
	MLXSW_REG_MCIA_EEPROM_MODULE_INFO_REV_ID_UNSPC	= 0x00,
	MLXSW_REG_MCIA_EEPROM_MODULE_INFO_REV_ID_8436	= 0x01,
	MLXSW_REG_MCIA_EEPROM_MODULE_INFO_REV_ID_8636	= 0x03,
};

enum mlxsw_reg_mcia_eeprom_module_info_id {
	MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_SFP	= 0x03,
	MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP	= 0x0C,
	MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_PLUS	= 0x0D,
	MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP28	= 0x11,
	MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_DD	= 0x18,
	MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_OSFP	= 0x19,
};

enum mlxsw_reg_mcia_eeprom_module_info {
	MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID,
	MLXSW_REG_MCIA_EEPROM_MODULE_INFO_REV_ID,
	MLXSW_REG_MCIA_EEPROM_MODULE_INFO_TYPE_ID,
	MLXSW_REG_MCIA_EEPROM_MODULE_INFO_SIZE,
};

/* reg_mcia_eeprom
 * Bytes to read/write.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, mcia, eeprom, 0x10, MLXSW_REG_MCIA_EEPROM_SIZE);

/* This is used to access the optional upper pages (1-3) in the QSFP+
 * memory map. Page 1 is available on offset 256 through 383, page 2 -
 * on offset 384 through 511, page 3 - on offset 512 through 639.
 */
#define MLXSW_REG_MCIA_PAGE_GET(off) (((off) - \
				MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH) / \
				MLXSW_REG_MCIA_EEPROM_UP_PAGE_LENGTH + 1)

static inline void mlxsw_reg_mcia_pack(char *payload, u8 slot_index, u8 module,
				       u8 lock, u8 page_number,
				       u16 device_addr, u8 size,
				       u8 i2c_device_addr)
{
	MLXSW_REG_ZERO(mcia, payload);
	mlxsw_reg_mcia_slot_set(payload, slot_index);
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

/* reg_mpat_session_id
 * Mirror Session ID.
 * Used for MIRROR_SESSION<i> trap.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, session_id, 0x00, 24, 4);

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

enum mlxsw_reg_mpat_span_type {
	/* Local SPAN Ethernet.
	 * The original packet is not encapsulated.
	 */
	MLXSW_REG_MPAT_SPAN_TYPE_LOCAL_ETH = 0x0,

	/* Remote SPAN Ethernet VLAN.
	 * The packet is forwarded to the monitoring port on the monitoring
	 * VLAN.
	 */
	MLXSW_REG_MPAT_SPAN_TYPE_REMOTE_ETH = 0x1,

	/* Encapsulated Remote SPAN Ethernet L3 GRE.
	 * The packet is encapsulated with GRE header.
	 */
	MLXSW_REG_MPAT_SPAN_TYPE_REMOTE_ETH_L3 = 0x3,
};

/* reg_mpat_span_type
 * SPAN type.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, span_type, 0x04, 0, 4);

/* reg_mpat_pide
 * Policer enable.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, pide, 0x0C, 15, 1);

/* reg_mpat_pid
 * Policer ID.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, pid, 0x0C, 0, 14);

/* Remote SPAN - Ethernet VLAN
 * - - - - - - - - - - - - - -
 */

/* reg_mpat_eth_rspan_vid
 * Encapsulation header VLAN ID.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, eth_rspan_vid, 0x18, 0, 12);

/* Encapsulated Remote SPAN - Ethernet L2
 * - - - - - - - - - - - - - - - - - - -
 */

enum mlxsw_reg_mpat_eth_rspan_version {
	MLXSW_REG_MPAT_ETH_RSPAN_VERSION_NO_HEADER = 15,
};

/* reg_mpat_eth_rspan_version
 * RSPAN mirror header version.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, eth_rspan_version, 0x10, 18, 4);

/* reg_mpat_eth_rspan_mac
 * Destination MAC address.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, mpat, eth_rspan_mac, 0x12, 6);

/* reg_mpat_eth_rspan_tp
 * Tag Packet. Indicates whether the mirroring header should be VLAN tagged.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, eth_rspan_tp, 0x18, 16, 1);

/* Encapsulated Remote SPAN - Ethernet L3
 * - - - - - - - - - - - - - - - - - - -
 */

enum mlxsw_reg_mpat_eth_rspan_protocol {
	MLXSW_REG_MPAT_ETH_RSPAN_PROTOCOL_IPV4,
	MLXSW_REG_MPAT_ETH_RSPAN_PROTOCOL_IPV6,
};

/* reg_mpat_eth_rspan_protocol
 * SPAN encapsulation protocol.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, eth_rspan_protocol, 0x18, 24, 4);

/* reg_mpat_eth_rspan_ttl
 * Encapsulation header Time-to-Live/HopLimit.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, eth_rspan_ttl, 0x1C, 4, 8);

/* reg_mpat_eth_rspan_smac
 * Source MAC address
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, mpat, eth_rspan_smac, 0x22, 6);

/* reg_mpat_eth_rspan_dip*
 * Destination IP address. The IP version is configured by protocol.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, eth_rspan_dip4, 0x4C, 0, 32);
MLXSW_ITEM_BUF(reg, mpat, eth_rspan_dip6, 0x40, 16);

/* reg_mpat_eth_rspan_sip*
 * Source IP address. The IP version is configured by protocol.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpat, eth_rspan_sip4, 0x5C, 0, 32);
MLXSW_ITEM_BUF(reg, mpat, eth_rspan_sip6, 0x50, 16);

static inline void mlxsw_reg_mpat_pack(char *payload, u8 pa_id,
				       u16 system_port, bool e,
				       enum mlxsw_reg_mpat_span_type span_type)
{
	MLXSW_REG_ZERO(mpat, payload);
	mlxsw_reg_mpat_pa_id_set(payload, pa_id);
	mlxsw_reg_mpat_system_port_set(payload, system_port);
	mlxsw_reg_mpat_e_set(payload, e);
	mlxsw_reg_mpat_qos_set(payload, 1);
	mlxsw_reg_mpat_be_set(payload, 1);
	mlxsw_reg_mpat_span_type_set(payload, span_type);
}

static inline void mlxsw_reg_mpat_eth_rspan_pack(char *payload, u16 vid)
{
	mlxsw_reg_mpat_eth_rspan_vid_set(payload, vid);
}

static inline void
mlxsw_reg_mpat_eth_rspan_l2_pack(char *payload,
				 enum mlxsw_reg_mpat_eth_rspan_version version,
				 const char *mac,
				 bool tp)
{
	mlxsw_reg_mpat_eth_rspan_version_set(payload, version);
	mlxsw_reg_mpat_eth_rspan_mac_memcpy_to(payload, mac);
	mlxsw_reg_mpat_eth_rspan_tp_set(payload, tp);
}

static inline void
mlxsw_reg_mpat_eth_rspan_l3_ipv4_pack(char *payload, u8 ttl,
				      const char *smac,
				      u32 sip, u32 dip)
{
	mlxsw_reg_mpat_eth_rspan_ttl_set(payload, ttl);
	mlxsw_reg_mpat_eth_rspan_smac_memcpy_to(payload, smac);
	mlxsw_reg_mpat_eth_rspan_protocol_set(payload,
				    MLXSW_REG_MPAT_ETH_RSPAN_PROTOCOL_IPV4);
	mlxsw_reg_mpat_eth_rspan_sip4_set(payload, sip);
	mlxsw_reg_mpat_eth_rspan_dip4_set(payload, dip);
}

static inline void
mlxsw_reg_mpat_eth_rspan_l3_ipv6_pack(char *payload, u8 ttl,
				      const char *smac,
				      struct in6_addr sip, struct in6_addr dip)
{
	mlxsw_reg_mpat_eth_rspan_ttl_set(payload, ttl);
	mlxsw_reg_mpat_eth_rspan_smac_memcpy_to(payload, smac);
	mlxsw_reg_mpat_eth_rspan_protocol_set(payload,
				    MLXSW_REG_MPAT_ETH_RSPAN_PROTOCOL_IPV6);
	mlxsw_reg_mpat_eth_rspan_sip6_memcpy_to(payload, (void *)&sip);
	mlxsw_reg_mpat_eth_rspan_dip6_memcpy_to(payload, (void *)&dip);
}

/* MPAR - Monitoring Port Analyzer Register
 * ----------------------------------------
 * MPAR register is used to query and configure the port analyzer port mirroring
 * properties.
 */
#define MLXSW_REG_MPAR_ID 0x901B
#define MLXSW_REG_MPAR_LEN 0x0C

MLXSW_REG_DEFINE(mpar, MLXSW_REG_MPAR_ID, MLXSW_REG_MPAR_LEN);

/* reg_mpar_local_port
 * The local port to mirror the packets from.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, mpar, 0x00, 16, 0x00, 4);

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

#define MLXSW_REG_MPAR_RATE_MAX 3500000000UL

/* reg_mpar_probability_rate
 * Sampling rate.
 * Valid values are: 1 to 3.5*10^9
 * Value of 1 means "sample all". Default is 1.
 * Reserved when Spectrum-1.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpar, probability_rate, 0x08, 0, 32);

static inline void mlxsw_reg_mpar_pack(char *payload, u16 local_port,
				       enum mlxsw_reg_mpar_i_e i_e,
				       bool enable, u8 pa_id,
				       u32 probability_rate)
{
	MLXSW_REG_ZERO(mpar, payload);
	mlxsw_reg_mpar_local_port_set(payload, local_port);
	mlxsw_reg_mpar_enable_set(payload, enable);
	mlxsw_reg_mpar_i_e_set(payload, i_e);
	mlxsw_reg_mpar_pa_id_set(payload, pa_id);
	mlxsw_reg_mpar_probability_rate_set(payload, probability_rate);
}

/* MGIR - Management General Information Register
 * ----------------------------------------------
 * MGIR register allows software to query the hardware and firmware general
 * information.
 */
#define MLXSW_REG_MGIR_ID 0x9020
#define MLXSW_REG_MGIR_LEN 0x9C

MLXSW_REG_DEFINE(mgir, MLXSW_REG_MGIR_ID, MLXSW_REG_MGIR_LEN);

/* reg_mgir_hw_info_device_hw_revision
 * Access: RO
 */
MLXSW_ITEM32(reg, mgir, hw_info_device_hw_revision, 0x0, 16, 16);

#define MLXSW_REG_MGIR_FW_INFO_PSID_SIZE 16

/* reg_mgir_fw_info_psid
 * PSID (ASCII string).
 * Access: RO
 */
MLXSW_ITEM_BUF(reg, mgir, fw_info_psid, 0x30, MLXSW_REG_MGIR_FW_INFO_PSID_SIZE);

/* reg_mgir_fw_info_extended_major
 * Access: RO
 */
MLXSW_ITEM32(reg, mgir, fw_info_extended_major, 0x44, 0, 32);

/* reg_mgir_fw_info_extended_minor
 * Access: RO
 */
MLXSW_ITEM32(reg, mgir, fw_info_extended_minor, 0x48, 0, 32);

/* reg_mgir_fw_info_extended_sub_minor
 * Access: RO
 */
MLXSW_ITEM32(reg, mgir, fw_info_extended_sub_minor, 0x4C, 0, 32);

static inline void mlxsw_reg_mgir_pack(char *payload)
{
	MLXSW_REG_ZERO(mgir, payload);
}

static inline void
mlxsw_reg_mgir_unpack(char *payload, u32 *hw_rev, char *fw_info_psid,
		      u32 *fw_major, u32 *fw_minor, u32 *fw_sub_minor)
{
	*hw_rev = mlxsw_reg_mgir_hw_info_device_hw_revision_get(payload);
	mlxsw_reg_mgir_fw_info_psid_memcpy_from(payload, fw_info_psid);
	*fw_major = mlxsw_reg_mgir_fw_info_extended_major_get(payload);
	*fw_minor = mlxsw_reg_mgir_fw_info_extended_minor_get(payload);
	*fw_sub_minor = mlxsw_reg_mgir_fw_info_extended_sub_minor_get(payload);
}

/* MRSR - Management Reset and Shutdown Register
 * ---------------------------------------------
 * MRSR register is used to reset or shutdown the switch or
 * the entire system (when applicable).
 */
#define MLXSW_REG_MRSR_ID 0x9023
#define MLXSW_REG_MRSR_LEN 0x08

MLXSW_REG_DEFINE(mrsr, MLXSW_REG_MRSR_ID, MLXSW_REG_MRSR_LEN);

/* reg_mrsr_command
 * Reset/shutdown command
 * 0 - do nothing
 * 1 - software reset
 * Access: WO
 */
MLXSW_ITEM32(reg, mrsr, command, 0x00, 0, 4);

static inline void mlxsw_reg_mrsr_pack(char *payload)
{
	MLXSW_REG_ZERO(mrsr, payload);
	mlxsw_reg_mrsr_command_set(payload, 1);
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
MLXSW_ITEM32_LP(reg, mlcr, 0x00, 16, 0x00, 24);

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

static inline void mlxsw_reg_mlcr_pack(char *payload, u16 local_port,
				       bool active)
{
	MLXSW_REG_ZERO(mlcr, payload);
	mlxsw_reg_mlcr_local_port_set(payload, local_port);
	mlxsw_reg_mlcr_beacon_duration_set(payload, active ?
					   MLXSW_REG_MLCR_DURATION_MAX : 0);
}

/* MCION - Management Cable IO and Notifications Register
 * ------------------------------------------------------
 * The MCION register is used to query transceiver modules' IO pins and other
 * notifications.
 */
#define MLXSW_REG_MCION_ID 0x9052
#define MLXSW_REG_MCION_LEN 0x18

MLXSW_REG_DEFINE(mcion, MLXSW_REG_MCION_ID, MLXSW_REG_MCION_LEN);

/* reg_mcion_module
 * Module number.
 * Access: Index
 */
MLXSW_ITEM32(reg, mcion, module, 0x00, 16, 8);

/* reg_mcion_slot_index
 * Slot index.
 * Access: Index
 */
MLXSW_ITEM32(reg, mcion, slot_index, 0x00, 12, 4);

enum {
	MLXSW_REG_MCION_MODULE_STATUS_BITS_PRESENT_MASK = BIT(0),
	MLXSW_REG_MCION_MODULE_STATUS_BITS_LOW_POWER_MASK = BIT(8),
};

/* reg_mcion_module_status_bits
 * Module IO status as defined by SFF.
 * Access: RO
 */
MLXSW_ITEM32(reg, mcion, module_status_bits, 0x04, 0, 16);

static inline void mlxsw_reg_mcion_pack(char *payload, u8 slot_index, u8 module)
{
	MLXSW_REG_ZERO(mcion, payload);
	mlxsw_reg_mcion_slot_index_set(payload, slot_index);
	mlxsw_reg_mcion_module_set(payload, module);
}

/* MTPPS - Management Pulse Per Second Register
 * --------------------------------------------
 * This register provides the device PPS capabilities, configure the PPS in and
 * out modules and holds the PPS in time stamp.
 */
#define MLXSW_REG_MTPPS_ID 0x9053
#define MLXSW_REG_MTPPS_LEN 0x3C

MLXSW_REG_DEFINE(mtpps, MLXSW_REG_MTPPS_ID, MLXSW_REG_MTPPS_LEN);

/* reg_mtpps_enable
 * Enables the PPS functionality the specific pin.
 * A boolean variable.
 * Access: RW
 */
MLXSW_ITEM32(reg, mtpps, enable, 0x20, 31, 1);

enum mlxsw_reg_mtpps_pin_mode {
	MLXSW_REG_MTPPS_PIN_MODE_VIRTUAL_PIN = 0x2,
};

/* reg_mtpps_pin_mode
 * Pin mode to be used. The mode must comply with the supported modes of the
 * requested pin.
 * Access: RW
 */
MLXSW_ITEM32(reg, mtpps, pin_mode, 0x20, 8, 4);

#define MLXSW_REG_MTPPS_PIN_SP_VIRTUAL_PIN	7

/* reg_mtpps_pin
 * Pin to be configured or queried out of the supported pins.
 * Access: Index
 */
MLXSW_ITEM32(reg, mtpps, pin, 0x20, 0, 8);

/* reg_mtpps_time_stamp
 * When pin_mode = pps_in, the latched device time when it was triggered from
 * the external GPIO pin.
 * When pin_mode = pps_out or virtual_pin or pps_out_and_virtual_pin, the target
 * time to generate next output signal.
 * Time is in units of device clock.
 * Access: RW
 */
MLXSW_ITEM64(reg, mtpps, time_stamp, 0x28, 0, 64);

static inline void
mlxsw_reg_mtpps_vpin_pack(char *payload, u64 time_stamp)
{
	MLXSW_REG_ZERO(mtpps, payload);
	mlxsw_reg_mtpps_pin_set(payload, MLXSW_REG_MTPPS_PIN_SP_VIRTUAL_PIN);
	mlxsw_reg_mtpps_pin_mode_set(payload,
				     MLXSW_REG_MTPPS_PIN_MODE_VIRTUAL_PIN);
	mlxsw_reg_mtpps_enable_set(payload, true);
	mlxsw_reg_mtpps_time_stamp_set(payload, time_stamp);
}

/* MTUTC - Management UTC Register
 * -------------------------------
 * Configures the HW UTC counter.
 */
#define MLXSW_REG_MTUTC_ID 0x9055
#define MLXSW_REG_MTUTC_LEN 0x1C

MLXSW_REG_DEFINE(mtutc, MLXSW_REG_MTUTC_ID, MLXSW_REG_MTUTC_LEN);

enum mlxsw_reg_mtutc_operation {
	MLXSW_REG_MTUTC_OPERATION_SET_TIME_AT_NEXT_SEC = 0,
	MLXSW_REG_MTUTC_OPERATION_ADJUST_FREQ = 3,
};

/* reg_mtutc_operation
 * Operation.
 * Access: OP
 */
MLXSW_ITEM32(reg, mtutc, operation, 0x00, 0, 4);

/* reg_mtutc_freq_adjustment
 * Frequency adjustment: Every PPS the HW frequency will be
 * adjusted by this value. Units of HW clock, where HW counts
 * 10^9 HW clocks for 1 HW second.
 * Access: RW
 */
MLXSW_ITEM32(reg, mtutc, freq_adjustment, 0x04, 0, 32);

/* reg_mtutc_utc_sec
 * UTC seconds.
 * Access: WO
 */
MLXSW_ITEM32(reg, mtutc, utc_sec, 0x10, 0, 32);

static inline void
mlxsw_reg_mtutc_pack(char *payload, enum mlxsw_reg_mtutc_operation oper,
		     u32 freq_adj, u32 utc_sec)
{
	MLXSW_REG_ZERO(mtutc, payload);
	mlxsw_reg_mtutc_operation_set(payload, oper);
	mlxsw_reg_mtutc_freq_adjustment_set(payload, freq_adj);
	mlxsw_reg_mtutc_utc_sec_set(payload, utc_sec);
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
MLXSW_ITEM32_LP(reg, mpsc, 0x00, 16, 0x00, 12);

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

static inline void mlxsw_reg_mpsc_pack(char *payload, u16 local_port, bool e,
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
				       enum mlxsw_reg_flow_counter_set_type set_type)
{
	MLXSW_REG_ZERO(mgpc, payload);
	mlxsw_reg_mgpc_counter_index_set(payload, counter_index);
	mlxsw_reg_mgpc_counter_set_type_set(payload, set_type);
	mlxsw_reg_mgpc_opcode_set(payload, opcode);
}

/* MPRS - Monitoring Parsing State Register
 * ----------------------------------------
 * The MPRS register is used for setting up the parsing for hash,
 * policy-engine and routing.
 */
#define MLXSW_REG_MPRS_ID 0x9083
#define MLXSW_REG_MPRS_LEN 0x14

MLXSW_REG_DEFINE(mprs, MLXSW_REG_MPRS_ID, MLXSW_REG_MPRS_LEN);

/* reg_mprs_parsing_depth
 * Minimum parsing depth.
 * Need to enlarge parsing depth according to L3, MPLS, tunnels, ACL
 * rules, traps, hash, etc. Default is 96 bytes. Reserved when SwitchX-2.
 * Access: RW
 */
MLXSW_ITEM32(reg, mprs, parsing_depth, 0x00, 0, 16);

/* reg_mprs_parsing_en
 * Parsing enable.
 * Bit 0 - Enable parsing of NVE of types VxLAN, VxLAN-GPE, GENEVE and
 * NVGRE. Default is enabled. Reserved when SwitchX-2.
 * Access: RW
 */
MLXSW_ITEM32(reg, mprs, parsing_en, 0x04, 0, 16);

/* reg_mprs_vxlan_udp_dport
 * VxLAN UDP destination port.
 * Used for identifying VxLAN packets and for dport field in
 * encapsulation. Default is 4789.
 * Access: RW
 */
MLXSW_ITEM32(reg, mprs, vxlan_udp_dport, 0x10, 0, 16);

static inline void mlxsw_reg_mprs_pack(char *payload, u16 parsing_depth,
				       u16 vxlan_udp_dport)
{
	MLXSW_REG_ZERO(mprs, payload);
	mlxsw_reg_mprs_parsing_depth_set(payload, parsing_depth);
	mlxsw_reg_mprs_parsing_en_set(payload, true);
	mlxsw_reg_mprs_vxlan_udp_dport_set(payload, vxlan_udp_dport);
}

/* MOGCR - Monitoring Global Configuration Register
 * ------------------------------------------------
 */
#define MLXSW_REG_MOGCR_ID 0x9086
#define MLXSW_REG_MOGCR_LEN 0x20

MLXSW_REG_DEFINE(mogcr, MLXSW_REG_MOGCR_ID, MLXSW_REG_MOGCR_LEN);

/* reg_mogcr_ptp_iftc
 * PTP Ingress FIFO Trap Clear
 * The PTP_ING_FIFO trap provides MTPPTR with clr according
 * to this value. Default 0.
 * Reserved when IB switches and when SwitchX/-2, Spectrum-2
 * Access: RW
 */
MLXSW_ITEM32(reg, mogcr, ptp_iftc, 0x00, 1, 1);

/* reg_mogcr_ptp_eftc
 * PTP Egress FIFO Trap Clear
 * The PTP_EGR_FIFO trap provides MTPPTR with clr according
 * to this value. Default 0.
 * Reserved when IB switches and when SwitchX/-2, Spectrum-2
 * Access: RW
 */
MLXSW_ITEM32(reg, mogcr, ptp_eftc, 0x00, 0, 1);

/* reg_mogcr_mirroring_pid_base
 * Base policer id for mirroring policers.
 * Must have an even value (e.g. 1000, not 1001).
 * Reserved when SwitchX/-2, Switch-IB/2, Spectrum-1 and Quantum.
 * Access: RW
 */
MLXSW_ITEM32(reg, mogcr, mirroring_pid_base, 0x0C, 0, 14);

/* MPAGR - Monitoring Port Analyzer Global Register
 * ------------------------------------------------
 * This register is used for global port analyzer configurations.
 * Note: This register is not supported by current FW versions for Spectrum-1.
 */
#define MLXSW_REG_MPAGR_ID 0x9089
#define MLXSW_REG_MPAGR_LEN 0x0C

MLXSW_REG_DEFINE(mpagr, MLXSW_REG_MPAGR_ID, MLXSW_REG_MPAGR_LEN);

enum mlxsw_reg_mpagr_trigger {
	MLXSW_REG_MPAGR_TRIGGER_EGRESS,
	MLXSW_REG_MPAGR_TRIGGER_INGRESS,
	MLXSW_REG_MPAGR_TRIGGER_INGRESS_WRED,
	MLXSW_REG_MPAGR_TRIGGER_INGRESS_SHARED_BUFFER,
	MLXSW_REG_MPAGR_TRIGGER_INGRESS_ING_CONG,
	MLXSW_REG_MPAGR_TRIGGER_INGRESS_EGR_CONG,
	MLXSW_REG_MPAGR_TRIGGER_EGRESS_ECN,
	MLXSW_REG_MPAGR_TRIGGER_EGRESS_HIGH_LATENCY,
};

/* reg_mpagr_trigger
 * Mirror trigger.
 * Access: Index
 */
MLXSW_ITEM32(reg, mpagr, trigger, 0x00, 0, 4);

/* reg_mpagr_pa_id
 * Port analyzer ID.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpagr, pa_id, 0x04, 0, 4);

#define MLXSW_REG_MPAGR_RATE_MAX 3500000000UL

/* reg_mpagr_probability_rate
 * Sampling rate.
 * Valid values are: 1 to 3.5*10^9
 * Value of 1 means "sample all". Default is 1.
 * Access: RW
 */
MLXSW_ITEM32(reg, mpagr, probability_rate, 0x08, 0, 32);

static inline void mlxsw_reg_mpagr_pack(char *payload,
					enum mlxsw_reg_mpagr_trigger trigger,
					u8 pa_id, u32 probability_rate)
{
	MLXSW_REG_ZERO(mpagr, payload);
	mlxsw_reg_mpagr_trigger_set(payload, trigger);
	mlxsw_reg_mpagr_pa_id_set(payload, pa_id);
	mlxsw_reg_mpagr_probability_rate_set(payload, probability_rate);
}

/* MOMTE - Monitoring Mirror Trigger Enable Register
 * -------------------------------------------------
 * This register is used to configure the mirror enable for different mirror
 * reasons.
 */
#define MLXSW_REG_MOMTE_ID 0x908D
#define MLXSW_REG_MOMTE_LEN 0x10

MLXSW_REG_DEFINE(momte, MLXSW_REG_MOMTE_ID, MLXSW_REG_MOMTE_LEN);

/* reg_momte_local_port
 * Local port number.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, momte, 0x00, 16, 0x00, 12);

enum mlxsw_reg_momte_type {
	MLXSW_REG_MOMTE_TYPE_WRED = 0x20,
	MLXSW_REG_MOMTE_TYPE_SHARED_BUFFER_TCLASS = 0x31,
	MLXSW_REG_MOMTE_TYPE_SHARED_BUFFER_TCLASS_DESCRIPTORS = 0x32,
	MLXSW_REG_MOMTE_TYPE_SHARED_BUFFER_EGRESS_PORT = 0x33,
	MLXSW_REG_MOMTE_TYPE_ING_CONG = 0x40,
	MLXSW_REG_MOMTE_TYPE_EGR_CONG = 0x50,
	MLXSW_REG_MOMTE_TYPE_ECN = 0x60,
	MLXSW_REG_MOMTE_TYPE_HIGH_LATENCY = 0x70,
};

/* reg_momte_type
 * Type of mirroring.
 * Access: Index
 */
MLXSW_ITEM32(reg, momte, type, 0x04, 0, 8);

/* reg_momte_tclass_en
 * TClass/PG mirror enable. Each bit represents corresponding tclass.
 * 0: disable (default)
 * 1: enable
 * Access: RW
 */
MLXSW_ITEM_BIT_ARRAY(reg, momte, tclass_en, 0x08, 0x08, 1);

static inline void mlxsw_reg_momte_pack(char *payload, u16 local_port,
					enum mlxsw_reg_momte_type type)
{
	MLXSW_REG_ZERO(momte, payload);
	mlxsw_reg_momte_local_port_set(payload, local_port);
	mlxsw_reg_momte_type_set(payload, type);
}

/* MTPPPC - Time Precision Packet Port Configuration
 * -------------------------------------------------
 * This register serves for configuration of which PTP messages should be
 * timestamped. This is a global configuration, despite the register name.
 *
 * Reserved when Spectrum-2.
 */
#define MLXSW_REG_MTPPPC_ID 0x9090
#define MLXSW_REG_MTPPPC_LEN 0x28

MLXSW_REG_DEFINE(mtpppc, MLXSW_REG_MTPPPC_ID, MLXSW_REG_MTPPPC_LEN);

/* reg_mtpppc_ing_timestamp_message_type
 * Bitwise vector of PTP message types to timestamp at ingress.
 * MessageType field as defined by IEEE 1588
 * Each bit corresponds to a value (e.g. Bit0: Sync, Bit1: Delay_Req)
 * Default all 0
 * Access: RW
 */
MLXSW_ITEM32(reg, mtpppc, ing_timestamp_message_type, 0x08, 0, 16);

/* reg_mtpppc_egr_timestamp_message_type
 * Bitwise vector of PTP message types to timestamp at egress.
 * MessageType field as defined by IEEE 1588
 * Each bit corresponds to a value (e.g. Bit0: Sync, Bit1: Delay_Req)
 * Default all 0
 * Access: RW
 */
MLXSW_ITEM32(reg, mtpppc, egr_timestamp_message_type, 0x0C, 0, 16);

static inline void mlxsw_reg_mtpppc_pack(char *payload, u16 ing, u16 egr)
{
	MLXSW_REG_ZERO(mtpppc, payload);
	mlxsw_reg_mtpppc_ing_timestamp_message_type_set(payload, ing);
	mlxsw_reg_mtpppc_egr_timestamp_message_type_set(payload, egr);
}

/* MTPPTR - Time Precision Packet Timestamping Reading
 * ---------------------------------------------------
 * The MTPPTR is used for reading the per port PTP timestamp FIFO.
 * There is a trap for packets which are latched to the timestamp FIFO, thus the
 * SW knows which FIFO to read. Note that packets enter the FIFO before been
 * trapped. The sequence number is used to synchronize the timestamp FIFO
 * entries and the trapped packets.
 * Reserved when Spectrum-2.
 */

#define MLXSW_REG_MTPPTR_ID 0x9091
#define MLXSW_REG_MTPPTR_BASE_LEN 0x10 /* base length, without records */
#define MLXSW_REG_MTPPTR_REC_LEN 0x10 /* record length */
#define MLXSW_REG_MTPPTR_REC_MAX_COUNT 4
#define MLXSW_REG_MTPPTR_LEN (MLXSW_REG_MTPPTR_BASE_LEN +		\
		    MLXSW_REG_MTPPTR_REC_LEN * MLXSW_REG_MTPPTR_REC_MAX_COUNT)

MLXSW_REG_DEFINE(mtpptr, MLXSW_REG_MTPPTR_ID, MLXSW_REG_MTPPTR_LEN);

/* reg_mtpptr_local_port
 * Not supported for CPU port.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, mtpptr, 0x00, 16, 0x00, 12);

enum mlxsw_reg_mtpptr_dir {
	MLXSW_REG_MTPPTR_DIR_INGRESS,
	MLXSW_REG_MTPPTR_DIR_EGRESS,
};

/* reg_mtpptr_dir
 * Direction.
 * Access: Index
 */
MLXSW_ITEM32(reg, mtpptr, dir, 0x00, 0, 1);

/* reg_mtpptr_clr
 * Clear the records.
 * Access: OP
 */
MLXSW_ITEM32(reg, mtpptr, clr, 0x04, 31, 1);

/* reg_mtpptr_num_rec
 * Number of valid records in the response
 * Range 0.. cap_ptp_timestamp_fifo
 * Access: RO
 */
MLXSW_ITEM32(reg, mtpptr, num_rec, 0x08, 0, 4);

/* reg_mtpptr_rec_message_type
 * MessageType field as defined by IEEE 1588 Each bit corresponds to a value
 * (e.g. Bit0: Sync, Bit1: Delay_Req)
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, mtpptr, rec_message_type,
		     MLXSW_REG_MTPPTR_BASE_LEN, 8, 4,
		     MLXSW_REG_MTPPTR_REC_LEN, 0, false);

/* reg_mtpptr_rec_domain_number
 * DomainNumber field as defined by IEEE 1588
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, mtpptr, rec_domain_number,
		     MLXSW_REG_MTPPTR_BASE_LEN, 0, 8,
		     MLXSW_REG_MTPPTR_REC_LEN, 0, false);

/* reg_mtpptr_rec_sequence_id
 * SequenceId field as defined by IEEE 1588
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, mtpptr, rec_sequence_id,
		     MLXSW_REG_MTPPTR_BASE_LEN, 0, 16,
		     MLXSW_REG_MTPPTR_REC_LEN, 0x4, false);

/* reg_mtpptr_rec_timestamp_high
 * Timestamp of when the PTP packet has passed through the port Units of PLL
 * clock time.
 * For Spectrum-1 the PLL clock is 156.25Mhz and PLL clock time is 6.4nSec.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, mtpptr, rec_timestamp_high,
		     MLXSW_REG_MTPPTR_BASE_LEN, 0, 32,
		     MLXSW_REG_MTPPTR_REC_LEN, 0x8, false);

/* reg_mtpptr_rec_timestamp_low
 * See rec_timestamp_high.
 * Access: RO
 */
MLXSW_ITEM32_INDEXED(reg, mtpptr, rec_timestamp_low,
		     MLXSW_REG_MTPPTR_BASE_LEN, 0, 32,
		     MLXSW_REG_MTPPTR_REC_LEN, 0xC, false);

static inline void mlxsw_reg_mtpptr_unpack(const char *payload,
					   unsigned int rec,
					   u8 *p_message_type,
					   u8 *p_domain_number,
					   u16 *p_sequence_id,
					   u64 *p_timestamp)
{
	u32 timestamp_high, timestamp_low;

	*p_message_type = mlxsw_reg_mtpptr_rec_message_type_get(payload, rec);
	*p_domain_number = mlxsw_reg_mtpptr_rec_domain_number_get(payload, rec);
	*p_sequence_id = mlxsw_reg_mtpptr_rec_sequence_id_get(payload, rec);
	timestamp_high = mlxsw_reg_mtpptr_rec_timestamp_high_get(payload, rec);
	timestamp_low = mlxsw_reg_mtpptr_rec_timestamp_low_get(payload, rec);
	*p_timestamp = (u64)timestamp_high << 32 | timestamp_low;
}

/* MTPTPT - Monitoring Precision Time Protocol Trap Register
 * ---------------------------------------------------------
 * This register is used for configuring under which trap to deliver PTP
 * packets depending on type of the packet.
 */
#define MLXSW_REG_MTPTPT_ID 0x9092
#define MLXSW_REG_MTPTPT_LEN 0x08

MLXSW_REG_DEFINE(mtptpt, MLXSW_REG_MTPTPT_ID, MLXSW_REG_MTPTPT_LEN);

enum mlxsw_reg_mtptpt_trap_id {
	MLXSW_REG_MTPTPT_TRAP_ID_PTP0,
	MLXSW_REG_MTPTPT_TRAP_ID_PTP1,
};

/* reg_mtptpt_trap_id
 * Trap id.
 * Access: Index
 */
MLXSW_ITEM32(reg, mtptpt, trap_id, 0x00, 0, 4);

/* reg_mtptpt_message_type
 * Bitwise vector of PTP message types to trap. This is a necessary but
 * non-sufficient condition since need to enable also per port. See MTPPPC.
 * Message types are defined by IEEE 1588 Each bit corresponds to a value (e.g.
 * Bit0: Sync, Bit1: Delay_Req)
 */
MLXSW_ITEM32(reg, mtptpt, message_type, 0x04, 0, 16);

static inline void mlxsw_reg_mtptptp_pack(char *payload,
					  enum mlxsw_reg_mtptpt_trap_id trap_id,
					  u16 message_type)
{
	MLXSW_REG_ZERO(mtptpt, payload);
	mlxsw_reg_mtptpt_trap_id_set(payload, trap_id);
	mlxsw_reg_mtptpt_message_type_set(payload, message_type);
}

/* MFGD - Monitoring FW General Debug Register
 * -------------------------------------------
 */
#define MLXSW_REG_MFGD_ID 0x90F0
#define MLXSW_REG_MFGD_LEN 0x0C

MLXSW_REG_DEFINE(mfgd, MLXSW_REG_MFGD_ID, MLXSW_REG_MFGD_LEN);

/* reg_mfgd_fw_fatal_event_mode
 * 0 - don't check FW fatal (default)
 * 1 - check FW fatal - enable MFDE trap
 * Access: RW
 */
MLXSW_ITEM32(reg, mfgd, fatal_event_mode, 0x00, 9, 2);

/* reg_mfgd_trigger_test
 * Access: WO
 */
MLXSW_ITEM32(reg, mfgd, trigger_test, 0x00, 11, 1);

/* MGPIR - Management General Peripheral Information Register
 * ----------------------------------------------------------
 * MGPIR register allows software to query the hardware and
 * firmware general information of peripheral entities.
 */
#define MLXSW_REG_MGPIR_ID 0x9100
#define MLXSW_REG_MGPIR_LEN 0xA0

MLXSW_REG_DEFINE(mgpir, MLXSW_REG_MGPIR_ID, MLXSW_REG_MGPIR_LEN);

enum mlxsw_reg_mgpir_device_type {
	MLXSW_REG_MGPIR_DEVICE_TYPE_NONE,
	MLXSW_REG_MGPIR_DEVICE_TYPE_GEARBOX_DIE,
};

/* mgpir_slot_index
 * Slot index (0: Main board).
 * Access: Index
 */
MLXSW_ITEM32(reg, mgpir, slot_index, 0x00, 28, 4);

/* mgpir_device_type
 * Access: RO
 */
MLXSW_ITEM32(reg, mgpir, device_type, 0x00, 24, 4);

/* mgpir_devices_per_flash
 * Number of devices of device_type per flash (can be shared by few devices).
 * Access: RO
 */
MLXSW_ITEM32(reg, mgpir, devices_per_flash, 0x00, 16, 8);

/* mgpir_num_of_devices
 * Number of devices of device_type.
 * Access: RO
 */
MLXSW_ITEM32(reg, mgpir, num_of_devices, 0x00, 0, 8);

/* max_modules_per_slot
 * Maximum number of modules that can be connected per slot.
 * Access: RO
 */
MLXSW_ITEM32(reg, mgpir, max_modules_per_slot, 0x04, 16, 8);

/* mgpir_num_of_slots
 * Number of slots in the system.
 * Access: RO
 */
MLXSW_ITEM32(reg, mgpir, num_of_slots, 0x04, 8, 8);

/* mgpir_num_of_modules
 * Number of modules.
 * Access: RO
 */
MLXSW_ITEM32(reg, mgpir, num_of_modules, 0x04, 0, 8);

static inline void mlxsw_reg_mgpir_pack(char *payload, u8 slot_index)
{
	MLXSW_REG_ZERO(mgpir, payload);
	mlxsw_reg_mgpir_slot_index_set(payload, slot_index);
}

static inline void
mlxsw_reg_mgpir_unpack(char *payload, u8 *num_of_devices,
		       enum mlxsw_reg_mgpir_device_type *device_type,
		       u8 *devices_per_flash, u8 *num_of_modules,
		       u8 *num_of_slots)
{
	if (num_of_devices)
		*num_of_devices = mlxsw_reg_mgpir_num_of_devices_get(payload);
	if (device_type)
		*device_type = mlxsw_reg_mgpir_device_type_get(payload);
	if (devices_per_flash)
		*devices_per_flash =
				mlxsw_reg_mgpir_devices_per_flash_get(payload);
	if (num_of_modules)
		*num_of_modules = mlxsw_reg_mgpir_num_of_modules_get(payload);
	if (num_of_slots)
		*num_of_slots = mlxsw_reg_mgpir_num_of_slots_get(payload);
}

/* MBCT - Management Binary Code Transfer Register
 * -----------------------------------------------
 * This register allows to transfer binary codes from the host to
 * the management FW by transferring it by chunks of maximum 1KB.
 */
#define MLXSW_REG_MBCT_ID 0x9120
#define MLXSW_REG_MBCT_LEN 0x420

MLXSW_REG_DEFINE(mbct, MLXSW_REG_MBCT_ID, MLXSW_REG_MBCT_LEN);

/* reg_mbct_slot_index
 * Slot index. 0 is reserved.
 * Access: Index
 */
MLXSW_ITEM32(reg, mbct, slot_index, 0x00, 0, 4);

/* reg_mbct_data_size
 * Actual data field size in bytes for the current data transfer.
 * Access: WO
 */
MLXSW_ITEM32(reg, mbct, data_size, 0x04, 0, 11);

enum mlxsw_reg_mbct_op {
	MLXSW_REG_MBCT_OP_ERASE_INI_IMAGE = 1,
	MLXSW_REG_MBCT_OP_DATA_TRANSFER, /* Download */
	MLXSW_REG_MBCT_OP_ACTIVATE,
	MLXSW_REG_MBCT_OP_CLEAR_ERRORS = 6,
	MLXSW_REG_MBCT_OP_QUERY_STATUS,
};

/* reg_mbct_op
 * Access: WO
 */
MLXSW_ITEM32(reg, mbct, op, 0x08, 28, 4);

/* reg_mbct_last
 * Indicates that the current data field is the last chunk of the INI.
 * Access: WO
 */
MLXSW_ITEM32(reg, mbct, last, 0x08, 26, 1);

/* reg_mbct_oee
 * Opcode Event Enable. When set a BCTOE event will be sent once the opcode
 * was executed and the fsm_state has changed.
 * Access: WO
 */
MLXSW_ITEM32(reg, mbct, oee, 0x08, 25, 1);

enum mlxsw_reg_mbct_status {
	/* Partial data transfer completed successfully and ready for next
	 * data transfer.
	 */
	MLXSW_REG_MBCT_STATUS_PART_DATA = 2,
	MLXSW_REG_MBCT_STATUS_LAST_DATA,
	MLXSW_REG_MBCT_STATUS_ERASE_COMPLETE,
	/* Error - trying to erase INI while it being used. */
	MLXSW_REG_MBCT_STATUS_ERROR_INI_IN_USE,
	/* Last data transfer completed, applying magic pattern. */
	MLXSW_REG_MBCT_STATUS_ERASE_FAILED = 7,
	MLXSW_REG_MBCT_STATUS_INI_ERROR,
	MLXSW_REG_MBCT_STATUS_ACTIVATION_FAILED,
	MLXSW_REG_MBCT_STATUS_ILLEGAL_OPERATION = 11,
};

/* reg_mbct_status
 * Status.
 * Access: RO
 */
MLXSW_ITEM32(reg, mbct, status, 0x0C, 24, 5);

enum mlxsw_reg_mbct_fsm_state {
	MLXSW_REG_MBCT_FSM_STATE_INI_IN_USE = 5,
	MLXSW_REG_MBCT_FSM_STATE_ERROR,
};

/* reg_mbct_fsm_state
 * FSM state.
 * Access: RO
 */
MLXSW_ITEM32(reg, mbct, fsm_state,  0x0C, 16, 4);

#define MLXSW_REG_MBCT_DATA_LEN 1024

/* reg_mbct_data
 * Up to 1KB of data.
 * Access: WO
 */
MLXSW_ITEM_BUF(reg, mbct, data, 0x20, MLXSW_REG_MBCT_DATA_LEN);

static inline void mlxsw_reg_mbct_pack(char *payload, u8 slot_index,
				       enum mlxsw_reg_mbct_op op, bool oee)
{
	MLXSW_REG_ZERO(mbct, payload);
	mlxsw_reg_mbct_slot_index_set(payload, slot_index);
	mlxsw_reg_mbct_op_set(payload, op);
	mlxsw_reg_mbct_oee_set(payload, oee);
}

static inline void mlxsw_reg_mbct_dt_pack(char *payload,
					  u16 data_size, bool last,
					  const char *data)
{
	if (WARN_ON(data_size > MLXSW_REG_MBCT_DATA_LEN))
		return;
	mlxsw_reg_mbct_data_size_set(payload, data_size);
	mlxsw_reg_mbct_last_set(payload, last);
	mlxsw_reg_mbct_data_memcpy_to(payload, data);
}

static inline void
mlxsw_reg_mbct_unpack(const char *payload, u8 *p_slot_index,
		      enum mlxsw_reg_mbct_status *p_status,
		      enum mlxsw_reg_mbct_fsm_state *p_fsm_state)
{
	if (p_slot_index)
		*p_slot_index = mlxsw_reg_mbct_slot_index_get(payload);
	*p_status = mlxsw_reg_mbct_status_get(payload);
	if (p_fsm_state)
		*p_fsm_state = mlxsw_reg_mbct_fsm_state_get(payload);
}

/* MDDQ - Management DownStream Device Query Register
 * --------------------------------------------------
 * This register allows to query the DownStream device properties. The desired
 * information is chosen upon the query_type field and is delivered by 32B
 * of data blocks.
 */
#define MLXSW_REG_MDDQ_ID 0x9161
#define MLXSW_REG_MDDQ_LEN 0x30

MLXSW_REG_DEFINE(mddq, MLXSW_REG_MDDQ_ID, MLXSW_REG_MDDQ_LEN);

/* reg_mddq_sie
 * Slot info event enable.
 * When set to '1', each change in the slot_info.provisioned / sr_valid /
 * active / ready will generate a DSDSC event.
 * Access: RW
 */
MLXSW_ITEM32(reg, mddq, sie, 0x00, 31, 1);

enum mlxsw_reg_mddq_query_type {
	MLXSW_REG_MDDQ_QUERY_TYPE_SLOT_INFO = 1,
	MLXSW_REG_MDDQ_QUERY_TYPE_SLOT_NAME = 3,
};

/* reg_mddq_query_type
 * Access: Index
 */
MLXSW_ITEM32(reg, mddq, query_type, 0x00, 16, 8);

/* reg_mddq_slot_index
 * Slot index. 0 is reserved.
 * Access: Index
 */
MLXSW_ITEM32(reg, mddq, slot_index, 0x00, 0, 4);

/* reg_mddq_slot_info_provisioned
 * If set, the INI file is applied and the card is provisioned.
 * Access: RO
 */
MLXSW_ITEM32(reg, mddq, slot_info_provisioned, 0x10, 31, 1);

/* reg_mddq_slot_info_sr_valid
 * If set, Shift Register is valid (after being provisioned) and data
 * can be sent from the switch ASIC to the line-card CPLD over Shift-Register.
 * Access: RO
 */
MLXSW_ITEM32(reg, mddq, slot_info_sr_valid, 0x10, 30, 1);

enum mlxsw_reg_mddq_slot_info_ready {
	MLXSW_REG_MDDQ_SLOT_INFO_READY_NOT_READY,
	MLXSW_REG_MDDQ_SLOT_INFO_READY_READY,
	MLXSW_REG_MDDQ_SLOT_INFO_READY_ERROR,
};

/* reg_mddq_slot_info_lc_ready
 * If set, the LC is powered on, matching the INI version and a new FW
 * version can be burnt (if necessary).
 * Access: RO
 */
MLXSW_ITEM32(reg, mddq, slot_info_lc_ready, 0x10, 28, 2);

/* reg_mddq_slot_info_active
 * If set, the FW has completed the MDDC.device_enable command.
 * Access: RO
 */
MLXSW_ITEM32(reg, mddq, slot_info_active, 0x10, 27, 1);

/* reg_mddq_slot_info_hw_revision
 * Major user-configured version number of the current INI file.
 * Valid only when active or ready are '1'.
 * Access: RO
 */
MLXSW_ITEM32(reg, mddq, slot_info_hw_revision, 0x14, 16, 16);

/* reg_mddq_slot_info_ini_file_version
 * User-configured version number of the current INI file.
 * Valid only when active or lc_ready are '1'.
 * Access: RO
 */
MLXSW_ITEM32(reg, mddq, slot_info_ini_file_version, 0x14, 0, 16);

/* reg_mddq_slot_info_card_type
 * Access: RO
 */
MLXSW_ITEM32(reg, mddq, slot_info_card_type, 0x18, 0, 8);

static inline void
__mlxsw_reg_mddq_pack(char *payload, u8 slot_index,
		      enum mlxsw_reg_mddq_query_type query_type)
{
	MLXSW_REG_ZERO(mddq, payload);
	mlxsw_reg_mddq_slot_index_set(payload, slot_index);
	mlxsw_reg_mddq_query_type_set(payload, query_type);
}

static inline void
mlxsw_reg_mddq_slot_info_pack(char *payload, u8 slot_index, bool sie)
{
	__mlxsw_reg_mddq_pack(payload, slot_index,
			      MLXSW_REG_MDDQ_QUERY_TYPE_SLOT_INFO);
	mlxsw_reg_mddq_sie_set(payload, sie);
}

static inline void
mlxsw_reg_mddq_slot_info_unpack(const char *payload, u8 *p_slot_index,
				bool *p_provisioned, bool *p_sr_valid,
				enum mlxsw_reg_mddq_slot_info_ready *p_lc_ready,
				bool *p_active, u16 *p_hw_revision,
				u16 *p_ini_file_version,
				u8 *p_card_type)
{
	*p_slot_index = mlxsw_reg_mddq_slot_index_get(payload);
	*p_provisioned = mlxsw_reg_mddq_slot_info_provisioned_get(payload);
	*p_sr_valid = mlxsw_reg_mddq_slot_info_sr_valid_get(payload);
	*p_lc_ready = mlxsw_reg_mddq_slot_info_lc_ready_get(payload);
	*p_active = mlxsw_reg_mddq_slot_info_active_get(payload);
	*p_hw_revision = mlxsw_reg_mddq_slot_info_hw_revision_get(payload);
	*p_ini_file_version = mlxsw_reg_mddq_slot_info_ini_file_version_get(payload);
	*p_card_type = mlxsw_reg_mddq_slot_info_card_type_get(payload);
}

#define MLXSW_REG_MDDQ_SLOT_ASCII_NAME_LEN 20

/* reg_mddq_slot_ascii_name
 * Slot's ASCII name.
 * Access: RO
 */
MLXSW_ITEM_BUF(reg, mddq, slot_ascii_name, 0x10,
	       MLXSW_REG_MDDQ_SLOT_ASCII_NAME_LEN);

static inline void
mlxsw_reg_mddq_slot_name_pack(char *payload, u8 slot_index)
{
	__mlxsw_reg_mddq_pack(payload, slot_index,
			      MLXSW_REG_MDDQ_QUERY_TYPE_SLOT_NAME);
}

static inline void
mlxsw_reg_mddq_slot_name_unpack(const char *payload, char *slot_ascii_name)
{
	mlxsw_reg_mddq_slot_ascii_name_memcpy_from(payload, slot_ascii_name);
}

/* MDDC - Management DownStream Device Control Register
 * ----------------------------------------------------
 * This register allows to control downstream devices and line cards.
 */
#define MLXSW_REG_MDDC_ID 0x9163
#define MLXSW_REG_MDDC_LEN 0x30

MLXSW_REG_DEFINE(mddc, MLXSW_REG_MDDC_ID, MLXSW_REG_MDDC_LEN);

/* reg_mddc_slot_index
 * Slot index. 0 is reserved.
 * Access: Index
 */
MLXSW_ITEM32(reg, mddc, slot_index, 0x00, 0, 4);

/* reg_mddc_rst
 * Reset request.
 * Access: OP
 */
MLXSW_ITEM32(reg, mddc, rst, 0x04, 29, 1);

/* reg_mddc_device_enable
 * When set, FW is the manager and allowed to program the downstream device.
 * Access: RW
 */
MLXSW_ITEM32(reg, mddc, device_enable, 0x04, 28, 1);

static inline void mlxsw_reg_mddc_pack(char *payload, u8 slot_index, bool rst,
				       bool device_enable)
{
	MLXSW_REG_ZERO(mddc, payload);
	mlxsw_reg_mddc_slot_index_set(payload, slot_index);
	mlxsw_reg_mddc_rst_set(payload, rst);
	mlxsw_reg_mddc_device_enable_set(payload, device_enable);
}

/* MFDE - Monitoring FW Debug Register
 * -----------------------------------
 */
#define MLXSW_REG_MFDE_ID 0x9200
#define MLXSW_REG_MFDE_LEN 0x30

MLXSW_REG_DEFINE(mfde, MLXSW_REG_MFDE_ID, MLXSW_REG_MFDE_LEN);

/* reg_mfde_irisc_id
 * Which irisc triggered the event
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, irisc_id, 0x00, 24, 8);

enum mlxsw_reg_mfde_severity {
	/* Unrecoverable switch behavior */
	MLXSW_REG_MFDE_SEVERITY_FATL = 2,
	/* Unexpected state with possible systemic failure */
	MLXSW_REG_MFDE_SEVERITY_NRML = 3,
	/* Unexpected state without systemic failure */
	MLXSW_REG_MFDE_SEVERITY_INTR = 5,
};

/* reg_mfde_severity
 * The severity of the event.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, severity, 0x00, 16, 8);

enum mlxsw_reg_mfde_event_id {
	/* CRspace timeout */
	MLXSW_REG_MFDE_EVENT_ID_CRSPACE_TO = 1,
	/* KVD insertion machine stopped */
	MLXSW_REG_MFDE_EVENT_ID_KVD_IM_STOP,
	/* Triggered by MFGD.trigger_test */
	MLXSW_REG_MFDE_EVENT_ID_TEST,
	/* Triggered when firmware hits an assert */
	MLXSW_REG_MFDE_EVENT_ID_FW_ASSERT,
	/* Fatal error interrupt from hardware */
	MLXSW_REG_MFDE_EVENT_ID_FATAL_CAUSE,
};

/* reg_mfde_event_id
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, event_id, 0x00, 0, 16);

enum mlxsw_reg_mfde_method {
	MLXSW_REG_MFDE_METHOD_QUERY,
	MLXSW_REG_MFDE_METHOD_WRITE,
};

/* reg_mfde_method
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, method, 0x04, 29, 1);

/* reg_mfde_long_process
 * Indicates if the command is in long_process mode.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, long_process, 0x04, 28, 1);

enum mlxsw_reg_mfde_command_type {
	MLXSW_REG_MFDE_COMMAND_TYPE_MAD,
	MLXSW_REG_MFDE_COMMAND_TYPE_EMAD,
	MLXSW_REG_MFDE_COMMAND_TYPE_CMDIF,
};

/* reg_mfde_command_type
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, command_type, 0x04, 24, 2);

/* reg_mfde_reg_attr_id
 * EMAD - register id, MAD - attibute id
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, reg_attr_id, 0x04, 0, 16);

/* reg_mfde_crspace_to_log_address
 * crspace address accessed, which resulted in timeout.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, crspace_to_log_address, 0x10, 0, 32);

/* reg_mfde_crspace_to_oe
 * 0 - New event
 * 1 - Old event, occurred before MFGD activation.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, crspace_to_oe, 0x14, 24, 1);

/* reg_mfde_crspace_to_log_id
 * Which irisc triggered the timeout.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, crspace_to_log_id, 0x14, 0, 4);

/* reg_mfde_crspace_to_log_ip
 * IP (instruction pointer) that triggered the timeout.
 * Access: RO
 */
MLXSW_ITEM64(reg, mfde, crspace_to_log_ip, 0x18, 0, 64);

/* reg_mfde_kvd_im_stop_oe
 * 0 - New event
 * 1 - Old event, occurred before MFGD activation.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, kvd_im_stop_oe, 0x10, 24, 1);

/* reg_mfde_kvd_im_stop_pipes_mask
 * Bit per kvh pipe.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, kvd_im_stop_pipes_mask, 0x10, 0, 16);

/* reg_mfde_fw_assert_var0-4
 * Variables passed to assert.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, fw_assert_var0, 0x10, 0, 32);
MLXSW_ITEM32(reg, mfde, fw_assert_var1, 0x14, 0, 32);
MLXSW_ITEM32(reg, mfde, fw_assert_var2, 0x18, 0, 32);
MLXSW_ITEM32(reg, mfde, fw_assert_var3, 0x1C, 0, 32);
MLXSW_ITEM32(reg, mfde, fw_assert_var4, 0x20, 0, 32);

/* reg_mfde_fw_assert_existptr
 * The instruction pointer when assert was triggered.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, fw_assert_existptr, 0x24, 0, 32);

/* reg_mfde_fw_assert_callra
 * The return address after triggering assert.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, fw_assert_callra, 0x28, 0, 32);

/* reg_mfde_fw_assert_oe
 * 0 - New event
 * 1 - Old event, occurred before MFGD activation.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, fw_assert_oe, 0x2C, 24, 1);

/* reg_mfde_fw_assert_tile_v
 * 0: The assert was from main
 * 1: The assert was from a tile
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, fw_assert_tile_v, 0x2C, 23, 1);

/* reg_mfde_fw_assert_tile_index
 * When tile_v=1, the tile_index that caused the assert.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, fw_assert_tile_index, 0x2C, 16, 6);

/* reg_mfde_fw_assert_ext_synd
 * A generated one-to-one identifier which is specific per-assert.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, fw_assert_ext_synd, 0x2C, 0, 16);

/* reg_mfde_fatal_cause_id
 * HW interrupt cause id.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, fatal_cause_id, 0x10, 0, 18);

/* reg_mfde_fatal_cause_tile_v
 * 0: The assert was from main
 * 1: The assert was from a tile
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, fatal_cause_tile_v, 0x14, 23, 1);

/* reg_mfde_fatal_cause_tile_index
 * When tile_v=1, the tile_index that caused the assert.
 * Access: RO
 */
MLXSW_ITEM32(reg, mfde, fatal_cause_tile_index, 0x14, 16, 6);

/* TNGCR - Tunneling NVE General Configuration Register
 * ----------------------------------------------------
 * The TNGCR register is used for setting up the NVE Tunneling configuration.
 */
#define MLXSW_REG_TNGCR_ID 0xA001
#define MLXSW_REG_TNGCR_LEN 0x44

MLXSW_REG_DEFINE(tngcr, MLXSW_REG_TNGCR_ID, MLXSW_REG_TNGCR_LEN);

enum mlxsw_reg_tngcr_type {
	MLXSW_REG_TNGCR_TYPE_VXLAN,
	MLXSW_REG_TNGCR_TYPE_VXLAN_GPE,
	MLXSW_REG_TNGCR_TYPE_GENEVE,
	MLXSW_REG_TNGCR_TYPE_NVGRE,
};

/* reg_tngcr_type
 * Tunnel type for encapsulation and decapsulation. The types are mutually
 * exclusive.
 * Note: For Spectrum the NVE parsing must be enabled in MPRS.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, type, 0x00, 0, 4);

/* reg_tngcr_nve_valid
 * The VTEP is valid. Allows adding FDB entries for tunnel encapsulation.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, nve_valid, 0x04, 31, 1);

/* reg_tngcr_nve_ttl_uc
 * The TTL for NVE tunnel encapsulation underlay unicast packets.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, nve_ttl_uc, 0x04, 0, 8);

/* reg_tngcr_nve_ttl_mc
 * The TTL for NVE tunnel encapsulation underlay multicast packets.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, nve_ttl_mc, 0x08, 0, 8);

enum {
	/* Do not copy flow label. Calculate flow label using nve_flh. */
	MLXSW_REG_TNGCR_FL_NO_COPY,
	/* Copy flow label from inner packet if packet is IPv6 and
	 * encapsulation is by IPv6. Otherwise, calculate flow label using
	 * nve_flh.
	 */
	MLXSW_REG_TNGCR_FL_COPY,
};

/* reg_tngcr_nve_flc
 * For NVE tunnel encapsulation: Flow label copy from inner packet.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, nve_flc, 0x0C, 25, 1);

enum {
	/* Flow label is static. In Spectrum this means '0'. Spectrum-2
	 * uses {nve_fl_prefix, nve_fl_suffix}.
	 */
	MLXSW_REG_TNGCR_FL_NO_HASH,
	/* 8 LSBs of the flow label are calculated from ECMP hash of the
	 * inner packet. 12 MSBs are configured by nve_fl_prefix.
	 */
	MLXSW_REG_TNGCR_FL_HASH,
};

/* reg_tngcr_nve_flh
 * NVE flow label hash.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, nve_flh, 0x0C, 24, 1);

/* reg_tngcr_nve_fl_prefix
 * NVE flow label prefix. Constant 12 MSBs of the flow label.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, nve_fl_prefix, 0x0C, 8, 12);

/* reg_tngcr_nve_fl_suffix
 * NVE flow label suffix. Constant 8 LSBs of the flow label.
 * Reserved when nve_flh=1 and for Spectrum.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, nve_fl_suffix, 0x0C, 0, 8);

enum {
	/* Source UDP port is fixed (default '0') */
	MLXSW_REG_TNGCR_UDP_SPORT_NO_HASH,
	/* Source UDP port is calculated based on hash */
	MLXSW_REG_TNGCR_UDP_SPORT_HASH,
};

/* reg_tngcr_nve_udp_sport_type
 * NVE UDP source port type.
 * Spectrum uses LAG hash (SLCRv2). Spectrum-2 uses ECMP hash (RECRv2).
 * When the source UDP port is calculated based on hash, then the 8 LSBs
 * are calculated from hash the 8 MSBs are configured by
 * nve_udp_sport_prefix.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, nve_udp_sport_type, 0x10, 24, 1);

/* reg_tngcr_nve_udp_sport_prefix
 * NVE UDP source port prefix. Constant 8 MSBs of the UDP source port.
 * Reserved when NVE type is NVGRE.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, nve_udp_sport_prefix, 0x10, 8, 8);

/* reg_tngcr_nve_group_size_mc
 * The amount of sequential linked lists of MC entries. The first linked
 * list is configured by SFD.underlay_mc_ptr.
 * Valid values: 1, 2, 4, 8, 16, 32, 64
 * The linked list are configured by TNUMT.
 * The hash is set by LAG hash.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, nve_group_size_mc, 0x18, 0, 8);

/* reg_tngcr_nve_group_size_flood
 * The amount of sequential linked lists of flooding entries. The first
 * linked list is configured by SFMR.nve_tunnel_flood_ptr
 * Valid values: 1, 2, 4, 8, 16, 32, 64
 * The linked list are configured by TNUMT.
 * The hash is set by LAG hash.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, nve_group_size_flood, 0x1C, 0, 8);

/* reg_tngcr_learn_enable
 * During decapsulation, whether to learn from NVE port.
 * Reserved when Spectrum-2. See TNPC.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, learn_enable, 0x20, 31, 1);

/* reg_tngcr_underlay_virtual_router
 * Underlay virtual router.
 * Reserved when Spectrum-2.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, underlay_virtual_router, 0x20, 0, 16);

/* reg_tngcr_underlay_rif
 * Underlay ingress router interface. RIF type should be loopback generic.
 * Reserved when Spectrum.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, underlay_rif, 0x24, 0, 16);

/* reg_tngcr_usipv4
 * Underlay source IPv4 address of the NVE.
 * Access: RW
 */
MLXSW_ITEM32(reg, tngcr, usipv4, 0x28, 0, 32);

/* reg_tngcr_usipv6
 * Underlay source IPv6 address of the NVE. For Spectrum, must not be
 * modified under traffic of NVE tunneling encapsulation.
 * Access: RW
 */
MLXSW_ITEM_BUF(reg, tngcr, usipv6, 0x30, 16);

static inline void mlxsw_reg_tngcr_pack(char *payload,
					enum mlxsw_reg_tngcr_type type,
					bool valid, u8 ttl)
{
	MLXSW_REG_ZERO(tngcr, payload);
	mlxsw_reg_tngcr_type_set(payload, type);
	mlxsw_reg_tngcr_nve_valid_set(payload, valid);
	mlxsw_reg_tngcr_nve_ttl_uc_set(payload, ttl);
	mlxsw_reg_tngcr_nve_ttl_mc_set(payload, ttl);
	mlxsw_reg_tngcr_nve_flc_set(payload, MLXSW_REG_TNGCR_FL_NO_COPY);
	mlxsw_reg_tngcr_nve_flh_set(payload, 0);
	mlxsw_reg_tngcr_nve_udp_sport_type_set(payload,
					       MLXSW_REG_TNGCR_UDP_SPORT_HASH);
	mlxsw_reg_tngcr_nve_udp_sport_prefix_set(payload, 0);
	mlxsw_reg_tngcr_nve_group_size_mc_set(payload, 1);
	mlxsw_reg_tngcr_nve_group_size_flood_set(payload, 1);
}

/* TNUMT - Tunneling NVE Underlay Multicast Table Register
 * -------------------------------------------------------
 * The TNUMT register is for building the underlay MC table. It is used
 * for MC, flooding and BC traffic into the NVE tunnel.
 */
#define MLXSW_REG_TNUMT_ID 0xA003
#define MLXSW_REG_TNUMT_LEN 0x20

MLXSW_REG_DEFINE(tnumt, MLXSW_REG_TNUMT_ID, MLXSW_REG_TNUMT_LEN);

enum mlxsw_reg_tnumt_record_type {
	MLXSW_REG_TNUMT_RECORD_TYPE_IPV4,
	MLXSW_REG_TNUMT_RECORD_TYPE_IPV6,
	MLXSW_REG_TNUMT_RECORD_TYPE_LABEL,
};

/* reg_tnumt_record_type
 * Record type.
 * Access: RW
 */
MLXSW_ITEM32(reg, tnumt, record_type, 0x00, 28, 4);

/* reg_tnumt_tunnel_port
 * Tunnel port.
 * Access: RW
 */
MLXSW_ITEM32(reg, tnumt, tunnel_port, 0x00, 24, 4);

/* reg_tnumt_underlay_mc_ptr
 * Index to the underlay multicast table.
 * For Spectrum the index is to the KVD linear.
 * Access: Index
 */
MLXSW_ITEM32(reg, tnumt, underlay_mc_ptr, 0x00, 0, 24);

/* reg_tnumt_vnext
 * The next_underlay_mc_ptr is valid.
 * Access: RW
 */
MLXSW_ITEM32(reg, tnumt, vnext, 0x04, 31, 1);

/* reg_tnumt_next_underlay_mc_ptr
 * The next index to the underlay multicast table.
 * Access: RW
 */
MLXSW_ITEM32(reg, tnumt, next_underlay_mc_ptr, 0x04, 0, 24);

/* reg_tnumt_record_size
 * Number of IP addresses in the record.
 * Range is 1..cap_max_nve_mc_entries_ipv{4,6}
 * Access: RW
 */
MLXSW_ITEM32(reg, tnumt, record_size, 0x08, 0, 3);

/* reg_tnumt_udip
 * The underlay IPv4 addresses. udip[i] is reserved if i >= size
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, tnumt, udip, 0x0C, 0, 32, 0x04, 0x00, false);

/* reg_tnumt_udip_ptr
 * The pointer to the underlay IPv6 addresses. udip_ptr[i] is reserved if
 * i >= size. The IPv6 addresses are configured by RIPS.
 * Access: RW
 */
MLXSW_ITEM32_INDEXED(reg, tnumt, udip_ptr, 0x0C, 0, 24, 0x04, 0x00, false);

static inline void mlxsw_reg_tnumt_pack(char *payload,
					enum mlxsw_reg_tnumt_record_type type,
					enum mlxsw_reg_tunnel_port tport,
					u32 underlay_mc_ptr, bool vnext,
					u32 next_underlay_mc_ptr,
					u8 record_size)
{
	MLXSW_REG_ZERO(tnumt, payload);
	mlxsw_reg_tnumt_record_type_set(payload, type);
	mlxsw_reg_tnumt_tunnel_port_set(payload, tport);
	mlxsw_reg_tnumt_underlay_mc_ptr_set(payload, underlay_mc_ptr);
	mlxsw_reg_tnumt_vnext_set(payload, vnext);
	mlxsw_reg_tnumt_next_underlay_mc_ptr_set(payload, next_underlay_mc_ptr);
	mlxsw_reg_tnumt_record_size_set(payload, record_size);
}

/* TNQCR - Tunneling NVE QoS Configuration Register
 * ------------------------------------------------
 * The TNQCR register configures how QoS is set in encapsulation into the
 * underlay network.
 */
#define MLXSW_REG_TNQCR_ID 0xA010
#define MLXSW_REG_TNQCR_LEN 0x0C

MLXSW_REG_DEFINE(tnqcr, MLXSW_REG_TNQCR_ID, MLXSW_REG_TNQCR_LEN);

/* reg_tnqcr_enc_set_dscp
 * For encapsulation: How to set DSCP field:
 * 0 - Copy the DSCP from the overlay (inner) IP header to the underlay
 * (outer) IP header. If there is no IP header, use TNQDR.dscp
 * 1 - Set the DSCP field as TNQDR.dscp
 * Access: RW
 */
MLXSW_ITEM32(reg, tnqcr, enc_set_dscp, 0x04, 28, 1);

static inline void mlxsw_reg_tnqcr_pack(char *payload)
{
	MLXSW_REG_ZERO(tnqcr, payload);
	mlxsw_reg_tnqcr_enc_set_dscp_set(payload, 0);
}

/* TNQDR - Tunneling NVE QoS Default Register
 * ------------------------------------------
 * The TNQDR register configures the default QoS settings for NVE
 * encapsulation.
 */
#define MLXSW_REG_TNQDR_ID 0xA011
#define MLXSW_REG_TNQDR_LEN 0x08

MLXSW_REG_DEFINE(tnqdr, MLXSW_REG_TNQDR_ID, MLXSW_REG_TNQDR_LEN);

/* reg_tnqdr_local_port
 * Local port number (receive port). CPU port is supported.
 * Access: Index
 */
MLXSW_ITEM32_LP(reg, tnqdr, 0x00, 16, 0x00, 12);

/* reg_tnqdr_dscp
 * For encapsulation, the default DSCP.
 * Access: RW
 */
MLXSW_ITEM32(reg, tnqdr, dscp, 0x04, 0, 6);

static inline void mlxsw_reg_tnqdr_pack(char *payload, u16 local_port)
{
	MLXSW_REG_ZERO(tnqdr, payload);
	mlxsw_reg_tnqdr_local_port_set(payload, local_port);
	mlxsw_reg_tnqdr_dscp_set(payload, 0);
}

/* TNEEM - Tunneling NVE Encapsulation ECN Mapping Register
 * --------------------------------------------------------
 * The TNEEM register maps ECN of the IP header at the ingress to the
 * encapsulation to the ECN of the underlay network.
 */
#define MLXSW_REG_TNEEM_ID 0xA012
#define MLXSW_REG_TNEEM_LEN 0x0C

MLXSW_REG_DEFINE(tneem, MLXSW_REG_TNEEM_ID, MLXSW_REG_TNEEM_LEN);

/* reg_tneem_overlay_ecn
 * ECN of the IP header in the overlay network.
 * Access: Index
 */
MLXSW_ITEM32(reg, tneem, overlay_ecn, 0x04, 24, 2);

/* reg_tneem_underlay_ecn
 * ECN of the IP header in the underlay network.
 * Access: RW
 */
MLXSW_ITEM32(reg, tneem, underlay_ecn, 0x04, 16, 2);

static inline void mlxsw_reg_tneem_pack(char *payload, u8 overlay_ecn,
					u8 underlay_ecn)
{
	MLXSW_REG_ZERO(tneem, payload);
	mlxsw_reg_tneem_overlay_ecn_set(payload, overlay_ecn);
	mlxsw_reg_tneem_underlay_ecn_set(payload, underlay_ecn);
}

/* TNDEM - Tunneling NVE Decapsulation ECN Mapping Register
 * --------------------------------------------------------
 * The TNDEM register configures the actions that are done in the
 * decapsulation.
 */
#define MLXSW_REG_TNDEM_ID 0xA013
#define MLXSW_REG_TNDEM_LEN 0x0C

MLXSW_REG_DEFINE(tndem, MLXSW_REG_TNDEM_ID, MLXSW_REG_TNDEM_LEN);

/* reg_tndem_underlay_ecn
 * ECN field of the IP header in the underlay network.
 * Access: Index
 */
MLXSW_ITEM32(reg, tndem, underlay_ecn, 0x04, 24, 2);

/* reg_tndem_overlay_ecn
 * ECN field of the IP header in the overlay network.
 * Access: Index
 */
MLXSW_ITEM32(reg, tndem, overlay_ecn, 0x04, 16, 2);

/* reg_tndem_eip_ecn
 * Egress IP ECN. ECN field of the IP header of the packet which goes out
 * from the decapsulation.
 * Access: RW
 */
MLXSW_ITEM32(reg, tndem, eip_ecn, 0x04, 8, 2);

/* reg_tndem_trap_en
 * Trap enable:
 * 0 - No trap due to decap ECN
 * 1 - Trap enable with trap_id
 * Access: RW
 */
MLXSW_ITEM32(reg, tndem, trap_en, 0x08, 28, 4);

/* reg_tndem_trap_id
 * Trap ID. Either DECAP_ECN0 or DECAP_ECN1.
 * Reserved when trap_en is '0'.
 * Access: RW
 */
MLXSW_ITEM32(reg, tndem, trap_id, 0x08, 0, 9);

static inline void mlxsw_reg_tndem_pack(char *payload, u8 underlay_ecn,
					u8 overlay_ecn, u8 ecn, bool trap_en,
					u16 trap_id)
{
	MLXSW_REG_ZERO(tndem, payload);
	mlxsw_reg_tndem_underlay_ecn_set(payload, underlay_ecn);
	mlxsw_reg_tndem_overlay_ecn_set(payload, overlay_ecn);
	mlxsw_reg_tndem_eip_ecn_set(payload, ecn);
	mlxsw_reg_tndem_trap_en_set(payload, trap_en);
	mlxsw_reg_tndem_trap_id_set(payload, trap_id);
}

/* TNPC - Tunnel Port Configuration Register
 * -----------------------------------------
 * The TNPC register is used for tunnel port configuration.
 * Reserved when Spectrum.
 */
#define MLXSW_REG_TNPC_ID 0xA020
#define MLXSW_REG_TNPC_LEN 0x18

MLXSW_REG_DEFINE(tnpc, MLXSW_REG_TNPC_ID, MLXSW_REG_TNPC_LEN);

/* reg_tnpc_tunnel_port
 * Tunnel port.
 * Access: Index
 */
MLXSW_ITEM32(reg, tnpc, tunnel_port, 0x00, 0, 4);

/* reg_tnpc_learn_enable_v6
 * During IPv6 underlay decapsulation, whether to learn from tunnel port.
 * Access: RW
 */
MLXSW_ITEM32(reg, tnpc, learn_enable_v6, 0x04, 1, 1);

/* reg_tnpc_learn_enable_v4
 * During IPv4 underlay decapsulation, whether to learn from tunnel port.
 * Access: RW
 */
MLXSW_ITEM32(reg, tnpc, learn_enable_v4, 0x04, 0, 1);

static inline void mlxsw_reg_tnpc_pack(char *payload,
				       enum mlxsw_reg_tunnel_port tport,
				       bool learn_enable)
{
	MLXSW_REG_ZERO(tnpc, payload);
	mlxsw_reg_tnpc_tunnel_port_set(payload, tport);
	mlxsw_reg_tnpc_learn_enable_v4_set(payload, learn_enable);
	mlxsw_reg_tnpc_learn_enable_v6_set(payload, learn_enable);
}

/* TIGCR - Tunneling IPinIP General Configuration Register
 * -------------------------------------------------------
 * The TIGCR register is used for setting up the IPinIP Tunnel configuration.
 */
#define MLXSW_REG_TIGCR_ID 0xA801
#define MLXSW_REG_TIGCR_LEN 0x10

MLXSW_REG_DEFINE(tigcr, MLXSW_REG_TIGCR_ID, MLXSW_REG_TIGCR_LEN);

/* reg_tigcr_ipip_ttlc
 * For IPinIP Tunnel encapsulation: whether to copy the ttl from the packet
 * header.
 * Access: RW
 */
MLXSW_ITEM32(reg, tigcr, ttlc, 0x04, 8, 1);

/* reg_tigcr_ipip_ttl_uc
 * The TTL for IPinIP Tunnel encapsulation of unicast packets if
 * reg_tigcr_ipip_ttlc is unset.
 * Access: RW
 */
MLXSW_ITEM32(reg, tigcr, ttl_uc, 0x04, 0, 8);

static inline void mlxsw_reg_tigcr_pack(char *payload, bool ttlc, u8 ttl_uc)
{
	MLXSW_REG_ZERO(tigcr, payload);
	mlxsw_reg_tigcr_ttlc_set(payload, ttlc);
	mlxsw_reg_tigcr_ttl_uc_set(payload, ttl_uc);
}

/* TIEEM - Tunneling IPinIP Encapsulation ECN Mapping Register
 * -----------------------------------------------------------
 * The TIEEM register maps ECN of the IP header at the ingress to the
 * encapsulation to the ECN of the underlay network.
 */
#define MLXSW_REG_TIEEM_ID 0xA812
#define MLXSW_REG_TIEEM_LEN 0x0C

MLXSW_REG_DEFINE(tieem, MLXSW_REG_TIEEM_ID, MLXSW_REG_TIEEM_LEN);

/* reg_tieem_overlay_ecn
 * ECN of the IP header in the overlay network.
 * Access: Index
 */
MLXSW_ITEM32(reg, tieem, overlay_ecn, 0x04, 24, 2);

/* reg_tineem_underlay_ecn
 * ECN of the IP header in the underlay network.
 * Access: RW
 */
MLXSW_ITEM32(reg, tieem, underlay_ecn, 0x04, 16, 2);

static inline void mlxsw_reg_tieem_pack(char *payload, u8 overlay_ecn,
					u8 underlay_ecn)
{
	MLXSW_REG_ZERO(tieem, payload);
	mlxsw_reg_tieem_overlay_ecn_set(payload, overlay_ecn);
	mlxsw_reg_tieem_underlay_ecn_set(payload, underlay_ecn);
}

/* TIDEM - Tunneling IPinIP Decapsulation ECN Mapping Register
 * -----------------------------------------------------------
 * The TIDEM register configures the actions that are done in the
 * decapsulation.
 */
#define MLXSW_REG_TIDEM_ID 0xA813
#define MLXSW_REG_TIDEM_LEN 0x0C

MLXSW_REG_DEFINE(tidem, MLXSW_REG_TIDEM_ID, MLXSW_REG_TIDEM_LEN);

/* reg_tidem_underlay_ecn
 * ECN field of the IP header in the underlay network.
 * Access: Index
 */
MLXSW_ITEM32(reg, tidem, underlay_ecn, 0x04, 24, 2);

/* reg_tidem_overlay_ecn
 * ECN field of the IP header in the overlay network.
 * Access: Index
 */
MLXSW_ITEM32(reg, tidem, overlay_ecn, 0x04, 16, 2);

/* reg_tidem_eip_ecn
 * Egress IP ECN. ECN field of the IP header of the packet which goes out
 * from the decapsulation.
 * Access: RW
 */
MLXSW_ITEM32(reg, tidem, eip_ecn, 0x04, 8, 2);

/* reg_tidem_trap_en
 * Trap enable:
 * 0 - No trap due to decap ECN
 * 1 - Trap enable with trap_id
 * Access: RW
 */
MLXSW_ITEM32(reg, tidem, trap_en, 0x08, 28, 4);

/* reg_tidem_trap_id
 * Trap ID. Either DECAP_ECN0 or DECAP_ECN1.
 * Reserved when trap_en is '0'.
 * Access: RW
 */
MLXSW_ITEM32(reg, tidem, trap_id, 0x08, 0, 9);

static inline void mlxsw_reg_tidem_pack(char *payload, u8 underlay_ecn,
					u8 overlay_ecn, u8 eip_ecn,
					bool trap_en, u16 trap_id)
{
	MLXSW_REG_ZERO(tidem, payload);
	mlxsw_reg_tidem_underlay_ecn_set(payload, underlay_ecn);
	mlxsw_reg_tidem_overlay_ecn_set(payload, overlay_ecn);
	mlxsw_reg_tidem_eip_ecn_set(payload, eip_ecn);
	mlxsw_reg_tidem_trap_en_set(payload, trap_en);
	mlxsw_reg_tidem_trap_id_set(payload, trap_id);
}

/* SBPR - Shared Buffer Pools Register
 * -----------------------------------
 * The SBPR configures and retrieves the shared buffer pools and configuration.
 */
#define MLXSW_REG_SBPR_ID 0xB001
#define MLXSW_REG_SBPR_LEN 0x14

MLXSW_REG_DEFINE(sbpr, MLXSW_REG_SBPR_ID, MLXSW_REG_SBPR_LEN);

/* reg_sbpr_desc
 * When set, configures descriptor buffer.
 * Access: Index
 */
MLXSW_ITEM32(reg, sbpr, desc, 0x00, 31, 1);

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

/* reg_sbpr_infi_size
 * Size is infinite.
 * Access: RW
 */
MLXSW_ITEM32(reg, sbpr, infi_size, 0x04, 31, 1);

/* reg_sbpr_size
 * Pool size in buffer cells.
 * Reserved when infi_size = 1.
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
				       enum mlxsw_reg_sbpr_mode mode, u32 size,
				       bool infi_size)
{
	MLXSW_REG_ZERO(sbpr, payload);
	mlxsw_reg_sbpr_pool_set(payload, pool);
	mlxsw_reg_sbpr_dir_set(payload, dir);
	mlxsw_reg_sbpr_mode_set(payload, mode);
	mlxsw_reg_sbpr_size_set(payload, size);
	mlxsw_reg_sbpr_infi_size_set(payload, infi_size);
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
MLXSW_ITEM32_LP(reg, sbcm, 0x00, 16, 0x00, 4);

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

/* reg_sbcm_infi_max
 * Max buffer is infinite.
 * Access: RW
 */
MLXSW_ITEM32(reg, sbcm, infi_max, 0x1C, 31, 1);

/* reg_sbcm_max_buff
 * When the pool associated to the port-pg/tclass is configured to
 * static, Maximum buffer size for the limiter configured in cells.
 * When the pool associated to the port-pg/tclass is configured to
 * dynamic, the max_buff holds the "alpha" parameter, supporting
 * the following values:
 * 0: 0
 * i: (1/128)*2^(i-1), for i=1..14
 * 0xFF: Infinity
 * Reserved when infi_max = 1.
 * Access: RW
 */
MLXSW_ITEM32(reg, sbcm, max_buff, 0x1C, 0, 24);

/* reg_sbcm_pool
 * Association of the port-priority to a pool.
 * Access: RW
 */
MLXSW_ITEM32(reg, sbcm, pool, 0x24, 0, 4);

static inline void mlxsw_reg_sbcm_pack(char *payload, u16 local_port, u8 pg_buff,
				       enum mlxsw_reg_sbxx_dir dir,
				       u32 min_buff, u32 max_buff,
				       bool infi_max, u8 pool)
{
	MLXSW_REG_ZERO(sbcm, payload);
	mlxsw_reg_sbcm_local_port_set(payload, local_port);
	mlxsw_reg_sbcm_pg_buff_set(payload, pg_buff);
	mlxsw_reg_sbcm_dir_set(payload, dir);
	mlxsw_reg_sbcm_min_buff_set(payload, min_buff);
	mlxsw_reg_sbcm_max_buff_set(payload, max_buff);
	mlxsw_reg_sbcm_infi_max_set(payload, infi_max);
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
MLXSW_ITEM32_LP(reg, sbpm, 0x00, 16, 0x00, 12);

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

static inline void mlxsw_reg_sbpm_pack(char *payload, u16 local_port, u8 pool,
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

#define MLXSW_REG_SBSR_NUM_PORTS_IN_PAGE 256

/* reg_sbsr_port_page
 * Determines the range of the ports specified in the 'ingress_port_mask'
 * and 'egress_port_mask' bit masks.
 * {ingress,egress}_port_mask[x] is (256 * port_page) + x
 * Access: Index
 */
MLXSW_ITEM32(reg, sbsr, port_page, 0x04, 0, 4);

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
MLXSW_ITEM32_LP(reg, sbib, 0x00, 16, 0x00, 12);

/* reg_sbib_buff_size
 * Units represented in cells
 * Allowed range is 0 to (cap_max_headroom_size - 1)
 * Default is 0
 * Access: RW
 */
MLXSW_ITEM32(reg, sbib, buff_size, 0x08, 0, 24);

static inline void mlxsw_reg_sbib_pack(char *payload, u16 local_port,
				       u32 buff_size)
{
	MLXSW_REG_ZERO(sbib, payload);
	mlxsw_reg_sbib_local_port_set(payload, local_port);
	mlxsw_reg_sbib_buff_size_set(payload, buff_size);
}

static const struct mlxsw_reg_info *mlxsw_reg_infos[] = {
	MLXSW_REG(sgcr),
	MLXSW_REG(spad),
	MLXSW_REG(sspr),
	MLXSW_REG(sfdat),
	MLXSW_REG(sfd),
	MLXSW_REG(sfn),
	MLXSW_REG(spms),
	MLXSW_REG(spvid),
	MLXSW_REG(spvm),
	MLXSW_REG(spaft),
	MLXSW_REG(sfgc),
	MLXSW_REG(sfdf),
	MLXSW_REG(sldr),
	MLXSW_REG(slcr),
	MLXSW_REG(slcor),
	MLXSW_REG(spmlr),
	MLXSW_REG(svfa),
	MLXSW_REG(spvtr),
	MLXSW_REG(svpe),
	MLXSW_REG(sfmr),
	MLXSW_REG(spvmlr),
	MLXSW_REG(spvc),
	MLXSW_REG(spevet),
	MLXSW_REG(smpe),
	MLXSW_REG(sftr2),
	MLXSW_REG(smid2),
	MLXSW_REG(cwtp),
	MLXSW_REG(cwtpm),
	MLXSW_REG(pgcr),
	MLXSW_REG(ppbt),
	MLXSW_REG(pacl),
	MLXSW_REG(pagt),
	MLXSW_REG(ptar),
	MLXSW_REG(ppbs),
	MLXSW_REG(prcr),
	MLXSW_REG(pefa),
	MLXSW_REG(pemrbt),
	MLXSW_REG(ptce2),
	MLXSW_REG(perpt),
	MLXSW_REG(peabfe),
	MLXSW_REG(perar),
	MLXSW_REG(ptce3),
	MLXSW_REG(percr),
	MLXSW_REG(pererp),
	MLXSW_REG(iedr),
	MLXSW_REG(qpts),
	MLXSW_REG(qpcr),
	MLXSW_REG(qtct),
	MLXSW_REG(qeec),
	MLXSW_REG(qrwe),
	MLXSW_REG(qpdsm),
	MLXSW_REG(qpdp),
	MLXSW_REG(qpdpm),
	MLXSW_REG(qtctm),
	MLXSW_REG(qpsc),
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
	MLXSW_REG(pmaos),
	MLXSW_REG(pplr),
	MLXSW_REG(pmtdb),
	MLXSW_REG(pmecr),
	MLXSW_REG(pmpe),
	MLXSW_REG(pddr),
	MLXSW_REG(pmmp),
	MLXSW_REG(pllp),
	MLXSW_REG(pmtm),
	MLXSW_REG(htgt),
	MLXSW_REG(hpkt),
	MLXSW_REG(rgcr),
	MLXSW_REG(ritr),
	MLXSW_REG(rtar),
	MLXSW_REG(ratr),
	MLXSW_REG(rtdp),
	MLXSW_REG(rips),
	MLXSW_REG(ratrad),
	MLXSW_REG(rdpm),
	MLXSW_REG(ricnt),
	MLXSW_REG(rrcr),
	MLXSW_REG(ralta),
	MLXSW_REG(ralst),
	MLXSW_REG(raltb),
	MLXSW_REG(ralue),
	MLXSW_REG(rauht),
	MLXSW_REG(raleu),
	MLXSW_REG(rauhtd),
	MLXSW_REG(rigr2),
	MLXSW_REG(recr2),
	MLXSW_REG(rmft2),
	MLXSW_REG(reiv),
	MLXSW_REG(mfcr),
	MLXSW_REG(mfsc),
	MLXSW_REG(mfsm),
	MLXSW_REG(mfsl),
	MLXSW_REG(fore),
	MLXSW_REG(mtcap),
	MLXSW_REG(mtmp),
	MLXSW_REG(mtwe),
	MLXSW_REG(mtbr),
	MLXSW_REG(mcia),
	MLXSW_REG(mpat),
	MLXSW_REG(mpar),
	MLXSW_REG(mgir),
	MLXSW_REG(mrsr),
	MLXSW_REG(mlcr),
	MLXSW_REG(mcion),
	MLXSW_REG(mtpps),
	MLXSW_REG(mtutc),
	MLXSW_REG(mpsc),
	MLXSW_REG(mcqi),
	MLXSW_REG(mcc),
	MLXSW_REG(mcda),
	MLXSW_REG(mgpc),
	MLXSW_REG(mprs),
	MLXSW_REG(mogcr),
	MLXSW_REG(mpagr),
	MLXSW_REG(momte),
	MLXSW_REG(mtpppc),
	MLXSW_REG(mtpptr),
	MLXSW_REG(mtptpt),
	MLXSW_REG(mfgd),
	MLXSW_REG(mgpir),
	MLXSW_REG(mbct),
	MLXSW_REG(mddq),
	MLXSW_REG(mddc),
	MLXSW_REG(mfde),
	MLXSW_REG(tngcr),
	MLXSW_REG(tnumt),
	MLXSW_REG(tnqcr),
	MLXSW_REG(tnqdr),
	MLXSW_REG(tneem),
	MLXSW_REG(tndem),
	MLXSW_REG(tnpc),
	MLXSW_REG(tigcr),
	MLXSW_REG(tieem),
	MLXSW_REG(tidem),
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
MLXSW_ITEM32_LP(reg, pude, 0x00, 16, 0x00, 12);

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
