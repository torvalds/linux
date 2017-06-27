/* Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016 NXP
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * * Neither the name of the above-listed copyright holders nor the
 * names of any contributors may be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _FSL_DPNI_CMD_H
#define _FSL_DPNI_CMD_H

#include "dpni.h"

/* DPNI Version */
#define DPNI_VER_MAJOR				7
#define DPNI_VER_MINOR				0
#define DPNI_CMD_BASE_VERSION			1
#define DPNI_CMD_ID_OFFSET			4

#define DPNI_CMD(id)	(((id) << DPNI_CMD_ID_OFFSET) | DPNI_CMD_BASE_VERSION)

#define DPNI_CMDID_OPEN					DPNI_CMD(0x801)
#define DPNI_CMDID_CLOSE				DPNI_CMD(0x800)
#define DPNI_CMDID_CREATE				DPNI_CMD(0x901)
#define DPNI_CMDID_DESTROY				DPNI_CMD(0x900)
#define DPNI_CMDID_GET_API_VERSION			DPNI_CMD(0xa01)

#define DPNI_CMDID_ENABLE				DPNI_CMD(0x002)
#define DPNI_CMDID_DISABLE				DPNI_CMD(0x003)
#define DPNI_CMDID_GET_ATTR				DPNI_CMD(0x004)
#define DPNI_CMDID_RESET				DPNI_CMD(0x005)
#define DPNI_CMDID_IS_ENABLED				DPNI_CMD(0x006)

#define DPNI_CMDID_SET_IRQ				DPNI_CMD(0x010)
#define DPNI_CMDID_GET_IRQ				DPNI_CMD(0x011)
#define DPNI_CMDID_SET_IRQ_ENABLE			DPNI_CMD(0x012)
#define DPNI_CMDID_GET_IRQ_ENABLE			DPNI_CMD(0x013)
#define DPNI_CMDID_SET_IRQ_MASK				DPNI_CMD(0x014)
#define DPNI_CMDID_GET_IRQ_MASK				DPNI_CMD(0x015)
#define DPNI_CMDID_GET_IRQ_STATUS			DPNI_CMD(0x016)
#define DPNI_CMDID_CLEAR_IRQ_STATUS			DPNI_CMD(0x017)

#define DPNI_CMDID_SET_POOLS				DPNI_CMD(0x200)
#define DPNI_CMDID_SET_ERRORS_BEHAVIOR			DPNI_CMD(0x20B)

#define DPNI_CMDID_GET_QDID				DPNI_CMD(0x210)
#define DPNI_CMDID_GET_TX_DATA_OFFSET			DPNI_CMD(0x212)
#define DPNI_CMDID_GET_LINK_STATE			DPNI_CMD(0x215)
#define DPNI_CMDID_SET_MAX_FRAME_LENGTH			DPNI_CMD(0x216)
#define DPNI_CMDID_GET_MAX_FRAME_LENGTH			DPNI_CMD(0x217)
#define DPNI_CMDID_SET_LINK_CFG				DPNI_CMD(0x21A)
#define DPNI_CMDID_SET_TX_SHAPING			DPNI_CMD(0x21B)

#define DPNI_CMDID_SET_MCAST_PROMISC			DPNI_CMD(0x220)
#define DPNI_CMDID_GET_MCAST_PROMISC			DPNI_CMD(0x221)
#define DPNI_CMDID_SET_UNICAST_PROMISC			DPNI_CMD(0x222)
#define DPNI_CMDID_GET_UNICAST_PROMISC			DPNI_CMD(0x223)
#define DPNI_CMDID_SET_PRIM_MAC				DPNI_CMD(0x224)
#define DPNI_CMDID_GET_PRIM_MAC				DPNI_CMD(0x225)
#define DPNI_CMDID_ADD_MAC_ADDR				DPNI_CMD(0x226)
#define DPNI_CMDID_REMOVE_MAC_ADDR			DPNI_CMD(0x227)
#define DPNI_CMDID_CLR_MAC_FILTERS			DPNI_CMD(0x228)

#define DPNI_CMDID_SET_RX_TC_DIST			DPNI_CMD(0x235)

#define DPNI_CMDID_ADD_FS_ENT				DPNI_CMD(0x244)
#define DPNI_CMDID_REMOVE_FS_ENT			DPNI_CMD(0x245)
#define DPNI_CMDID_CLR_FS_ENT				DPNI_CMD(0x246)

#define DPNI_CMDID_GET_STATISTICS			DPNI_CMD(0x25D)
#define DPNI_CMDID_GET_QUEUE				DPNI_CMD(0x25F)
#define DPNI_CMDID_SET_QUEUE				DPNI_CMD(0x260)
#define DPNI_CMDID_GET_TAILDROP				DPNI_CMD(0x261)
#define DPNI_CMDID_SET_TAILDROP				DPNI_CMD(0x262)

#define DPNI_CMDID_GET_PORT_MAC_ADDR			DPNI_CMD(0x263)

#define DPNI_CMDID_GET_BUFFER_LAYOUT			DPNI_CMD(0x264)
#define DPNI_CMDID_SET_BUFFER_LAYOUT			DPNI_CMD(0x265)

#define DPNI_CMDID_SET_TX_CONFIRMATION_MODE		DPNI_CMD(0x266)
#define DPNI_CMDID_SET_CONGESTION_NOTIFICATION		DPNI_CMD(0x267)
#define DPNI_CMDID_GET_CONGESTION_NOTIFICATION		DPNI_CMD(0x268)
#define DPNI_CMDID_SET_EARLY_DROP			DPNI_CMD(0x269)
#define DPNI_CMDID_GET_EARLY_DROP			DPNI_CMD(0x26A)
#define DPNI_CMDID_GET_OFFLOAD				DPNI_CMD(0x26B)
#define DPNI_CMDID_SET_OFFLOAD				DPNI_CMD(0x26C)

/* Macros for accessing command fields smaller than 1byte */
#define DPNI_MASK(field)	\
	GENMASK(DPNI_##field##_SHIFT + DPNI_##field##_SIZE - 1, \
		DPNI_##field##_SHIFT)

#define dpni_set_field(var, field, val)	\
	((var) |= (((val) << DPNI_##field##_SHIFT) & DPNI_MASK(field)))
#define dpni_get_field(var, field)	\
	(((var) & DPNI_MASK(field)) >> DPNI_##field##_SHIFT)

struct dpni_cmd_open {
	__le32 dpni_id;
};

#define DPNI_BACKUP_POOL(val, order)	(((val) & 0x1) << (order))
struct dpni_cmd_set_pools {
	/* cmd word 0 */
	u8 num_dpbp;
	u8 backup_pool_mask;
	__le16 pad;
	/* cmd word 0..4 */
	__le32 dpbp_id[DPNI_MAX_DPBP];
	/* cmd word 4..6 */
	__le16 buffer_size[DPNI_MAX_DPBP];
};

/* The enable indication is always the least significant bit */
#define DPNI_ENABLE_SHIFT		0
#define DPNI_ENABLE_SIZE		1

struct dpni_rsp_is_enabled {
	u8 enabled;
};

struct dpni_rsp_get_irq {
	/* response word 0 */
	__le32 irq_val;
	__le32 pad;
	/* response word 1 */
	__le64 irq_addr;
	/* response word 2 */
	__le32 irq_num;
	__le32 type;
};

struct dpni_cmd_set_irq_enable {
	u8 enable;
	u8 pad[3];
	u8 irq_index;
};

struct dpni_cmd_get_irq_enable {
	__le32 pad;
	u8 irq_index;
};

struct dpni_rsp_get_irq_enable {
	u8 enabled;
};

struct dpni_cmd_set_irq_mask {
	__le32 mask;
	u8 irq_index;
};

struct dpni_cmd_get_irq_mask {
	__le32 pad;
	u8 irq_index;
};

struct dpni_rsp_get_irq_mask {
	__le32 mask;
};

struct dpni_cmd_get_irq_status {
	__le32 status;
	u8 irq_index;
};

struct dpni_rsp_get_irq_status {
	__le32 status;
};

struct dpni_cmd_clear_irq_status {
	__le32 status;
	u8 irq_index;
};

struct dpni_rsp_get_attr {
	/* response word 0 */
	__le32 options;
	u8 num_queues;
	u8 num_tcs;
	u8 mac_filter_entries;
	u8 pad0;
	/* response word 1 */
	u8 vlan_filter_entries;
	u8 pad1;
	u8 qos_entries;
	u8 pad2;
	__le16 fs_entries;
	__le16 pad3;
	/* response word 2 */
	u8 qos_key_size;
	u8 fs_key_size;
	__le16 wriop_version;
};

#define DPNI_ERROR_ACTION_SHIFT		0
#define DPNI_ERROR_ACTION_SIZE		4
#define DPNI_FRAME_ANN_SHIFT		4
#define DPNI_FRAME_ANN_SIZE		1

struct dpni_cmd_set_errors_behavior {
	__le32 errors;
	/* from least significant bit: error_action:4, set_frame_annotation:1 */
	u8 flags;
};

/* There are 3 separate commands for configuring Rx, Tx and Tx confirmation
 * buffer layouts, but they all share the same parameters.
 * If one of the functions changes, below structure needs to be split.
 */

#define DPNI_PASS_TS_SHIFT		0
#define DPNI_PASS_TS_SIZE		1
#define DPNI_PASS_PR_SHIFT		1
#define DPNI_PASS_PR_SIZE		1
#define DPNI_PASS_FS_SHIFT		2
#define DPNI_PASS_FS_SIZE		1

struct dpni_cmd_get_buffer_layout {
	u8 qtype;
};

struct dpni_rsp_get_buffer_layout {
	/* response word 0 */
	u8 pad0[6];
	/* from LSB: pass_timestamp:1, parser_result:1, frame_status:1 */
	u8 flags;
	u8 pad1;
	/* response word 1 */
	__le16 private_data_size;
	__le16 data_align;
	__le16 head_room;
	__le16 tail_room;
};

struct dpni_cmd_set_buffer_layout {
	/* cmd word 0 */
	u8 qtype;
	u8 pad0[3];
	__le16 options;
	/* from LSB: pass_timestamp:1, parser_result:1, frame_status:1 */
	u8 flags;
	u8 pad1;
	/* cmd word 1 */
	__le16 private_data_size;
	__le16 data_align;
	__le16 head_room;
	__le16 tail_room;
};

struct dpni_cmd_set_offload {
	u8 pad[3];
	u8 dpni_offload;
	__le32 config;
};

struct dpni_cmd_get_offload {
	u8 pad[3];
	u8 dpni_offload;
};

struct dpni_rsp_get_offload {
	__le32 pad;
	__le32 config;
};

struct dpni_cmd_get_qdid {
	u8 qtype;
};

struct dpni_rsp_get_qdid {
	__le16 qdid;
};

struct dpni_rsp_get_tx_data_offset {
	__le16 data_offset;
};

struct dpni_cmd_get_statistics {
	u8 page_number;
};

struct dpni_rsp_get_statistics {
	__le64 counter[DPNI_STATISTICS_CNT];
};

struct dpni_cmd_set_link_cfg {
	/* cmd word 0 */
	__le64 pad0;
	/* cmd word 1 */
	__le32 rate;
	__le32 pad1;
	/* cmd word 2 */
	__le64 options;
};

#define DPNI_LINK_STATE_SHIFT		0
#define DPNI_LINK_STATE_SIZE		1

struct dpni_rsp_get_link_state {
	/* response word 0 */
	__le32 pad0;
	/* from LSB: up:1 */
	u8 flags;
	u8 pad1[3];
	/* response word 1 */
	__le32 rate;
	__le32 pad2;
	/* response word 2 */
	__le64 options;
};

struct dpni_cmd_set_max_frame_length {
	__le16 max_frame_length;
};

struct dpni_rsp_get_max_frame_length {
	__le16 max_frame_length;
};

struct dpni_cmd_set_multicast_promisc {
	u8 enable;
};

struct dpni_rsp_get_multicast_promisc {
	u8 enabled;
};

struct dpni_cmd_set_unicast_promisc {
	u8 enable;
};

struct dpni_rsp_get_unicast_promisc {
	u8 enabled;
};

struct dpni_cmd_set_primary_mac_addr {
	__le16 pad;
	u8 mac_addr[6];
};

struct dpni_rsp_get_primary_mac_addr {
	__le16 pad;
	u8 mac_addr[6];
};

struct dpni_rsp_get_port_mac_addr {
	__le16 pad;
	u8 mac_addr[6];
};

struct dpni_cmd_add_mac_addr {
	__le16 pad;
	u8 mac_addr[6];
};

struct dpni_cmd_remove_mac_addr {
	__le16 pad;
	u8 mac_addr[6];
};

#define DPNI_UNICAST_FILTERS_SHIFT	0
#define DPNI_UNICAST_FILTERS_SIZE	1
#define DPNI_MULTICAST_FILTERS_SHIFT	1
#define DPNI_MULTICAST_FILTERS_SIZE	1

struct dpni_cmd_clear_mac_filters {
	/* from LSB: unicast:1, multicast:1 */
	u8 flags;
};

#define DPNI_DIST_MODE_SHIFT		0
#define DPNI_DIST_MODE_SIZE		4
#define DPNI_MISS_ACTION_SHIFT		4
#define DPNI_MISS_ACTION_SIZE		4

struct dpni_cmd_set_rx_tc_dist {
	/* cmd word 0 */
	__le16 dist_size;
	u8 tc_id;
	/* from LSB: dist_mode:4, miss_action:4 */
	u8 flags;
	__le16 pad0;
	__le16 default_flow_id;
	/* cmd word 1..5 */
	__le64 pad1[5];
	/* cmd word 6 */
	__le64 key_cfg_iova;
};

/* dpni_set_rx_tc_dist extension (structure of the DMA-able memory at
 * key_cfg_iova)
 */
struct dpni_mask_cfg {
	u8 mask;
	u8 offset;
};

#define DPNI_EFH_TYPE_SHIFT		0
#define DPNI_EFH_TYPE_SIZE		4
#define DPNI_EXTRACT_TYPE_SHIFT		0
#define DPNI_EXTRACT_TYPE_SIZE		4

struct dpni_dist_extract {
	/* word 0 */
	u8 prot;
	/* EFH type stored in the 4 least significant bits */
	u8 efh_type;
	u8 size;
	u8 offset;
	__le32 field;
	/* word 1 */
	u8 hdr_index;
	u8 constant;
	u8 num_of_repeats;
	u8 num_of_byte_masks;
	/* Extraction type is stored in the 4 LSBs */
	u8 extract_type;
	u8 pad[3];
	/* word 2 */
	struct dpni_mask_cfg masks[4];
};

struct dpni_ext_set_rx_tc_dist {
	/* extension word 0 */
	u8 num_extracts;
	u8 pad[7];
	/* words 1..25 */
	struct dpni_dist_extract extracts[DPKG_MAX_NUM_OF_EXTRACTS];
};

struct dpni_cmd_get_queue {
	u8 qtype;
	u8 tc;
	u8 index;
};

#define DPNI_DEST_TYPE_SHIFT		0
#define DPNI_DEST_TYPE_SIZE		4
#define DPNI_STASH_CTRL_SHIFT		6
#define DPNI_STASH_CTRL_SIZE		1
#define DPNI_HOLD_ACTIVE_SHIFT		7
#define DPNI_HOLD_ACTIVE_SIZE		1

struct dpni_rsp_get_queue {
	/* response word 0 */
	__le64 pad0;
	/* response word 1 */
	__le32 dest_id;
	__le16 pad1;
	u8 dest_prio;
	/* From LSB: dest_type:4, pad:2, flc_stash_ctrl:1, hold_active:1 */
	u8 flags;
	/* response word 2 */
	__le64 flc;
	/* response word 3 */
	__le64 user_context;
	/* response word 4 */
	__le32 fqid;
	__le16 qdbin;
};

struct dpni_cmd_set_queue {
	/* cmd word 0 */
	u8 qtype;
	u8 tc;
	u8 index;
	u8 options;
	__le32 pad0;
	/* cmd word 1 */
	__le32 dest_id;
	__le16 pad1;
	u8 dest_prio;
	u8 flags;
	/* cmd word 2 */
	__le64 flc;
	/* cmd word 3 */
	__le64 user_context;
};

struct dpni_cmd_set_taildrop {
	/* cmd word 0 */
	u8 congestion_point;
	u8 qtype;
	u8 tc;
	u8 index;
	__le32 pad0;
	/* cmd word 1 */
	/* Only least significant bit is relevant */
	u8 enable;
	u8 pad1;
	u8 units;
	u8 pad2;
	__le32 threshold;
};

struct dpni_cmd_get_taildrop {
	u8 congestion_point;
	u8 qtype;
	u8 tc;
	u8 index;
};

struct dpni_rsp_get_taildrop {
	/* cmd word 0 */
	__le64 pad0;
	/* cmd word 1 */
	/* only least significant bit is relevant */
	u8 enable;
	u8 pad1;
	u8 units;
	u8 pad2;
	__le32 threshold;
};

#endif /* _FSL_DPNI_CMD_H */
