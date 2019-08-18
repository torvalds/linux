/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright (C) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright (C) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#ifndef __iwl_fw_dbg_tlv_h__
#define __iwl_fw_dbg_tlv_h__

#include <linux/bitops.h>

/*
 * struct iwl_fw_ini_header: Common Header for all debug group TLV's structures
 * @tlv_version: version info
 * @apply_point: &enum iwl_fw_ini_apply_point
 * @data: TLV data followed
 **/
struct iwl_fw_ini_header {
	__le32 tlv_version;
	__le32 apply_point;
	u8 data[];
} __packed; /* FW_DEBUG_TLV_HEADER_S */

/**
 * struct iwl_fw_ini_allocation_tlv - (IWL_FW_INI_TLV_TYPE_BUFFER_ALLOCATION)
 * buffer allocation TLV - for debug
 *
 * @iwl_fw_ini_header: header
 * @allocation_id: &enum iwl_fw_ini_allocation_id - to bind allocation and hcmd
 *	if needed (DBGC1/DBGC2/SDFX/...)
 * @buffer_location: type of iwl_fw_ini_buffer_location
 * @size: size in bytes
 * @max_fragments: the maximum allowed fragmentation in the desired memory
 *	allocation above
 * @min_frag_size: the minimum allowed fragmentation size in bytes
*/
struct iwl_fw_ini_allocation_tlv {
	struct iwl_fw_ini_header header;
	__le32 allocation_id;
	__le32 buffer_location;
	__le32 size;
	__le32 max_fragments;
	__le32 min_frag_size;
} __packed; /* FW_DEBUG_TLV_BUFFER_ALLOCATION_TLV_S_VER_1 */

/**
 * struct iwl_fw_ini_hcmd (IWL_FW_INI_TLV_TYPE_HCMD)
 * Generic Host command pass through TLV
 *
 * @id: the debug configuration command type for instance: 0xf6 / 0xf5 / DHC
 * @group: the desired cmd group
 * @padding: all zeros for dword alignment
 * @data: all of the relevant command (0xf6/0xf5) to be sent
*/
struct iwl_fw_ini_hcmd {
	u8 id;
	u8 group;
	__le16 padding;
	u8 data[0];
} __packed; /* FW_DEBUG_TLV_HCMD_DATA_S */

/**
 * struct iwl_fw_ini_hcmd_tlv
 * @header: header
 * @hcmd: a variable length host-command to be sent to apply the configuration.
 */
struct iwl_fw_ini_hcmd_tlv {
	struct iwl_fw_ini_header header;
	struct iwl_fw_ini_hcmd hcmd;
} __packed; /* FW_DEBUG_TLV_HCMD_S_VER_1 */

/*
 * struct iwl_fw_ini_debug_flow_tlv (IWL_FW_INI_TLV_TYPE_DEBUG_FLOW)
 *
 * @header: header
 * @debug_flow_cfg: &enum iwl_fw_ini_debug_flow
 */
struct iwl_fw_ini_debug_flow_tlv {
	struct iwl_fw_ini_header header;
	__le32 debug_flow_cfg;
} __packed; /* FW_DEBUG_TLV_FLOW_TLV_S_VER_1 */

#define IWL_FW_INI_MAX_REGION_ID	64
#define IWL_FW_INI_MAX_NAME		32

/**
 * struct iwl_fw_ini_region_cfg_internal - meta data of internal memory region
 * @num_of_range: the amount of ranges in the region
 * @range_data_size: size of the data to read per range, in bytes.
 */
struct iwl_fw_ini_region_cfg_internal {
	__le32 num_of_ranges;
	__le32 range_data_size;
} __packed; /* FW_DEBUG_TLV_REGION_NIC_INTERNAL_RANGES_S */

/**
 * struct iwl_fw_ini_region_cfg_fifos - meta data of fifos region
 * @fid1: fifo id 1 - bitmap of lmac tx/rx fifos to include in the region
 * @fid2: fifo id 2 - bitmap of umac rx fifos to include in the region.
 *	It is unused for tx.
 * @num_of_registers: number of prph registers in the region, each register is
 *	4 bytes size.
 * @header_only: none zero value indicates that this region does not include
 *	fifo data and includes only the given registers.
 */
struct iwl_fw_ini_region_cfg_fifos {
	__le32 fid1;
	__le32 fid2;
	__le32 num_of_registers;
	__le32 header_only;
} __packed; /* FW_DEBUG_TLV_REGION_FIFOS_S */

/**
 * struct iwl_fw_ini_region_cfg
 * @region_id: ID of this dump configuration
 * @region_type: &enum iwl_fw_ini_region_type
 * @num_regions: amount of regions in the address array.
 * @name_len: name length
 * @name: file name to use for this region
 * @internal: used in case the region uses internal memory.
 * @allocation_id: For DRAM type field substitutes for allocation_id
 * @fifos: used in case of fifos region.
 * @offset: offset to use for each memory base address
 * @start_addr: array of addresses.
 */
struct iwl_fw_ini_region_cfg {
	__le32 region_id;
	__le32 region_type;
	__le32 name_len;
	u8 name[IWL_FW_INI_MAX_NAME];
	union {
		struct iwl_fw_ini_region_cfg_internal internal;
		__le32 allocation_id;
		struct iwl_fw_ini_region_cfg_fifos fifos;
	};
	__le32 offset;
	__le32 start_addr[];
} __packed; /* FW_DEBUG_TLV_REGION_CONFIG_S */

/**
 * struct iwl_fw_ini_region_tlv - (IWL_FW_INI_TLV_TYPE_REGION_CFG)
 * DUMP sections define IDs and triggers that use those IDs TLV
 * @header: header
 * @num_regions: how many different region section and IDs are coming next
 * @region_config: list of dump configurations
 */
struct iwl_fw_ini_region_tlv {
	struct iwl_fw_ini_header header;
	__le32 num_regions;
	struct iwl_fw_ini_region_cfg region_config[];
} __packed; /* FW_DEBUG_TLV_REGIONS_S_VER_1 */

/**
 * struct iwl_fw_ini_trigger - (IWL_FW_INI_TLV_TYPE_DUMP_CFG)
 * Region sections define IDs and triggers that use those IDs TLV
 *
 * @trigger_id: enum &iwl_fw_ini_tigger_id
 * @override_trig: determines how apply trigger in case a trigger with the
 *	same id is already in use. Using the first 2 bytes:
 *	Byte 0: if 0, override trigger configuration, otherwise use the
 *	existing configuration.
 *	Byte 1: if 0, override trigger regions, otherwise append regions to
 *	existing trigger.
 * @dump_delay: delay from trigger fire to dump, in usec
 * @occurrences: max amount of times to be fired
 * @ignore_consec: ignore consecutive triggers, in usec
 * @force_restart: force FW restart
 * @multi_dut: initiate debug dump data on several DUTs
 * @trigger_data: generic data to be utilized per trigger
 * @num_regions: number of dump regions defined for this trigger
 * @data: region IDs
 */
struct iwl_fw_ini_trigger {
	__le32 trigger_id;
	__le32 override_trig;
	__le32 dump_delay;
	__le32 occurrences;
	__le32 ignore_consec;
	__le32 force_restart;
	__le32 multi_dut;
	__le32 trigger_data;
	__le32 num_regions;
	__le32 data[];
} __packed; /* FW_TLV_DEBUG_TRIGGER_CONFIG_S */

/**
 * struct iwl_fw_ini_trigger_tlv - (IWL_FW_INI_TLV_TYPE_TRIGGERS_CFG)
 * DUMP sections define IDs and triggers that use those IDs TLV
 *
 * @header: header
 * @num_triggers: how many different triggers section and IDs are coming next
 * @trigger_config: list of trigger configurations
 */
struct iwl_fw_ini_trigger_tlv {
	struct iwl_fw_ini_header header;
	__le32 num_triggers;
	struct iwl_fw_ini_trigger trigger_config[];
} __packed; /* FW_TLV_DEBUG_TRIGGERS_S_VER_1 */

/**
 * enum iwl_fw_ini_trigger_id
 * @IWL_FW_TRIGGER_ID_FW_ASSERT: FW assert
 * @IWL_FW_TRIGGER_ID_FW_HW_ERROR: HW assert
 * @IWL_FW_TRIGGER_ID_FW_TFD_Q_HANG: TFD queue hang
 * @IWL_FW_TRIGGER_ID_FW_DEBUG_HOST_TRIGGER: FW debug notification
 * @IWL_FW_TRIGGER_ID_FW_GENERIC_NOTIFOCATION: FW generic notification
 * @IWL_FW_TRIGGER_ID_USER_TRIGGER: User trigger
 * @IWL_FW_TRIGGER_ID_HOST_PEER_CLIENT_INACTIVITY: peer inactivity
 * @IWL_FW_TRIGGER_ID_HOST_TX_LATENCY_THRESHOLD_CROSSED: TX latency
 *	threshold was crossed
 * @IWL_FW_TRIGGER_ID_HOST_TX_RESPONSE_STATUS_FAILED: TX failed
 * @IWL_FW_TRIGGER_ID_HOST_OS_REQ_DEAUTH_PEER: Deauth initiated by host
 * @IWL_FW_TRIGGER_ID_HOST_STOP_GO_REQUEST: stop GO request
 * @IWL_FW_TRIGGER_ID_HOST_START_GO_REQUEST: start GO request
 * @IWL_FW_TRIGGER_ID_HOST_JOIN_GROUP_REQUEST: join P2P group request
 * @IWL_FW_TRIGGER_ID_HOST_SCAN_START: scan started event
 * @IWL_FW_TRIGGER_ID_HOST_SCAN_SUBMITTED: undefined
 * @IWL_FW_TRIGGER_ID_HOST_SCAN_PARAMS: undefined
 * @IWL_FW_TRIGGER_ID_HOST_CHECK_FOR_HANG: undefined
 * @IWL_FW_TRIGGER_ID_HOST_BAR_RECEIVED: BAR frame was received
 * @IWL_FW_TRIGGER_ID_HOST_AGG_TX_RESPONSE_STATUS_FAILED: agg TX failed
 * @IWL_FW_TRIGGER_ID_HOST_EAPOL_TX_RESPONSE_FAILED: EAPOL TX failed
 * @IWL_FW_TRIGGER_ID_HOST_FAKE_TX_RESPONSE_SUSPECTED: suspicious TX response
 * @IWL_FW_TRIGGER_ID_HOST_AUTH_REQ_FROM_ASSOC_CLIENT: received suspicious auth
 * @IWL_FW_TRIGGER_ID_HOST_ROAM_COMPLETE: roaming was completed
 * @IWL_FW_TRIGGER_ID_HOST_AUTH_ASSOC_FAST_FAILED: fast assoc failed
 * @IWL_FW_TRIGGER_ID_HOST_D3_START: D3 start
 * @IWL_FW_TRIGGER_ID_HOST_D3_END: D3 end
 * @IWL_FW_TRIGGER_ID_HOST_BSS_MISSED_BEACONS: missed beacon events
 * @IWL_FW_TRIGGER_ID_HOST_P2P_CLIENT_MISSED_BEACONS: P2P missed beacon events
 * @IWL_FW_TRIGGER_ID_HOST_PEER_CLIENT_TX_FAILURES:  undefined
 * @IWL_FW_TRIGGER_ID_HOST_TX_WFD_ACTION_FRAME_FAILED: undefined
 * @IWL_FW_TRIGGER_ID_HOST_AUTH_ASSOC_FAILED: authentication / association
 *	failed
 * @IWL_FW_TRIGGER_ID_HOST_SCAN_COMPLETE: scan complete event
 * @IWL_FW_TRIGGER_ID_HOST_SCAN_ABORT: scan abort complete
 * @IWL_FW_TRIGGER_ID_HOST_NIC_ALIVE: nic alive message was received
 * @IWL_FW_TRIGGER_ID_HOST_CHANNEL_SWITCH_COMPLETE: CSA was completed
 * @IWL_FW_TRIGGER_ID_NUM: number of trigger IDs
 */
enum iwl_fw_ini_trigger_id {
	IWL_FW_TRIGGER_ID_INVALID				= 0,

	/* Errors triggers */
	IWL_FW_TRIGGER_ID_FW_ASSERT				= 1,
	IWL_FW_TRIGGER_ID_FW_HW_ERROR				= 2,
	IWL_FW_TRIGGER_ID_FW_TFD_Q_HANG				= 3,

	/* FW triggers */
	IWL_FW_TRIGGER_ID_FW_DEBUG_HOST_TRIGGER			= 4,
	IWL_FW_TRIGGER_ID_FW_GENERIC_NOTIFOCATION		= 5,

	/* User trigger */
	IWL_FW_TRIGGER_ID_USER_TRIGGER				= 6,

	/* Host triggers */
	IWL_FW_TRIGGER_ID_HOST_PEER_CLIENT_INACTIVITY		= 7,
	IWL_FW_TRIGGER_ID_HOST_TX_LATENCY_THRESHOLD_CROSSED	= 8,
	IWL_FW_TRIGGER_ID_HOST_TX_RESPONSE_STATUS_FAILED	= 9,
	IWL_FW_TRIGGER_ID_HOST_OS_REQ_DEAUTH_PEER		= 10,
	IWL_FW_TRIGGER_ID_HOST_STOP_GO_REQUEST			= 11,
	IWL_FW_TRIGGER_ID_HOST_START_GO_REQUEST			= 12,
	IWL_FW_TRIGGER_ID_HOST_JOIN_GROUP_REQUEST		= 13,
	IWL_FW_TRIGGER_ID_HOST_SCAN_START			= 14,
	IWL_FW_TRIGGER_ID_HOST_SCAN_SUBMITTED			= 15,
	IWL_FW_TRIGGER_ID_HOST_SCAN_PARAMS			= 16,
	IWL_FW_TRIGGER_ID_HOST_CHECK_FOR_HANG			= 17,
	IWL_FW_TRIGGER_ID_HOST_BAR_RECEIVED			= 18,
	IWL_FW_TRIGGER_ID_HOST_AGG_TX_RESPONSE_STATUS_FAILED	= 19,
	IWL_FW_TRIGGER_ID_HOST_EAPOL_TX_RESPONSE_FAILED		= 20,
	IWL_FW_TRIGGER_ID_HOST_FAKE_TX_RESPONSE_SUSPECTED	= 21,
	IWL_FW_TRIGGER_ID_HOST_AUTH_REQ_FROM_ASSOC_CLIENT	= 22,
	IWL_FW_TRIGGER_ID_HOST_ROAM_COMPLETE			= 23,
	IWL_FW_TRIGGER_ID_HOST_AUTH_ASSOC_FAST_FAILED		= 24,
	IWL_FW_TRIGGER_ID_HOST_D3_START				= 25,
	IWL_FW_TRIGGER_ID_HOST_D3_END				= 26,
	IWL_FW_TRIGGER_ID_HOST_BSS_MISSED_BEACONS		= 27,
	IWL_FW_TRIGGER_ID_HOST_P2P_CLIENT_MISSED_BEACONS	= 28,
	IWL_FW_TRIGGER_ID_HOST_PEER_CLIENT_TX_FAILURES		= 29,
	IWL_FW_TRIGGER_ID_HOST_TX_WFD_ACTION_FRAME_FAILED	= 30,
	IWL_FW_TRIGGER_ID_HOST_AUTH_ASSOC_FAILED		= 31,
	IWL_FW_TRIGGER_ID_HOST_SCAN_COMPLETE			= 32,
	IWL_FW_TRIGGER_ID_HOST_SCAN_ABORT			= 33,
	IWL_FW_TRIGGER_ID_HOST_NIC_ALIVE			= 34,
	IWL_FW_TRIGGER_ID_HOST_CHANNEL_SWITCH_COMPLETE		= 35,

	IWL_FW_TRIGGER_ID_NUM,
}; /* FW_DEBUG_TLV_TRIGGER_ID_E_VER_1 */

/**
 * enum iwl_fw_ini_apply_point
 * @IWL_FW_INI_APPLY_INVALID: invalid
 * @IWL_FW_INI_APPLY_EARLY: pre loading FW
 * @IWL_FW_INI_APPLY_AFTER_ALIVE: first cmd from host after alive
 * @IWL_FW_INI_APPLY_POST_INIT: last cmd in initialization sequence
 * @IWL_FW_INI_APPLY_MISSED_BEACONS: missed beacons notification
 * @IWL_FW_INI_APPLY_SCAN_COMPLETE: scan completed
 * @IWL_FW_INI_APPLY_NUM: number of apply points
*/
enum iwl_fw_ini_apply_point {
	IWL_FW_INI_APPLY_INVALID,
	IWL_FW_INI_APPLY_EARLY,
	IWL_FW_INI_APPLY_AFTER_ALIVE,
	IWL_FW_INI_APPLY_POST_INIT,
	IWL_FW_INI_APPLY_MISSED_BEACONS,
	IWL_FW_INI_APPLY_SCAN_COMPLETE,
	IWL_FW_INI_APPLY_NUM,
}; /* FW_DEBUG_TLV_APPLY_POINT_E_VER_1 */

/**
 * enum iwl_fw_ini_allocation_id
 * @IWL_FW_INI_ALLOCATION_INVALID: invalid
 * @IWL_FW_INI_ALLOCATION_ID_DBGC1: allocation meant for DBGC1 configuration
 * @IWL_FW_INI_ALLOCATION_ID_DBGC2: allocation meant for DBGC2 configuration
 * @IWL_FW_INI_ALLOCATION_ID_DBGC3: allocation meant for DBGC3 configuration
 * @IWL_FW_INI_ALLOCATION_ID_SDFX: for SDFX module
 * @IWL_FW_INI_ALLOCATION_ID_FW_DUMP: used for crash and runtime dumps
 * @IWL_FW_INI_ALLOCATION_ID_USER_DEFINED: for future user scenarios
*/
enum iwl_fw_ini_allocation_id {
	IWL_FW_INI_ALLOCATION_INVALID,
	IWL_FW_INI_ALLOCATION_ID_DBGC1,
	IWL_FW_INI_ALLOCATION_ID_DBGC2,
	IWL_FW_INI_ALLOCATION_ID_DBGC3,
	IWL_FW_INI_ALLOCATION_ID_SDFX,
	IWL_FW_INI_ALLOCATION_ID_FW_DUMP,
	IWL_FW_INI_ALLOCATION_ID_USER_DEFINED,
}; /* FW_DEBUG_TLV_ALLOCATION_ID_E_VER_1 */

/**
 * enum iwl_fw_ini_buffer_location
 * @IWL_FW_INI_LOCATION_INVALID: invalid
 * @IWL_FW_INI_LOCATION_SRAM_PATH: SRAM location
 * @IWL_FW_INI_LOCATION_DRAM_PATH: DRAM location
 */
enum iwl_fw_ini_buffer_location {
	IWL_FW_INI_LOCATION_INVALID,
	IWL_FW_INI_LOCATION_SRAM_PATH,
	IWL_FW_INI_LOCATION_DRAM_PATH,
}; /* FW_DEBUG_TLV_BUFFER_LOCATION_E_VER_1 */

/**
 * enum iwl_fw_ini_debug_flow
 * @IWL_FW_INI_DEBUG_INVALID: invalid
 * @IWL_FW_INI_DEBUG_DBTR_FLOW: undefined
 * @IWL_FW_INI_DEBUG_TB2DTF_FLOW: undefined
 */
enum iwl_fw_ini_debug_flow {
	IWL_FW_INI_DEBUG_INVALID,
	IWL_FW_INI_DEBUG_DBTR_FLOW,
	IWL_FW_INI_DEBUG_TB2DTF_FLOW,
}; /* FW_DEBUG_TLV_FLOW_E_VER_1 */

/**
 * enum iwl_fw_ini_region_type
 * @IWL_FW_INI_REGION_INVALID: invalid
 * @IWL_FW_INI_REGION_DEVICE_MEMORY: device internal memory
 * @IWL_FW_INI_REGION_PERIPHERY_MAC: periphery registers of MAC
 * @IWL_FW_INI_REGION_PERIPHERY_PHY: periphery registers of PHY
 * @IWL_FW_INI_REGION_PERIPHERY_AUX: periphery registers of AUX
 * @IWL_FW_INI_REGION_DRAM_BUFFER: DRAM buffer
 * @IWL_FW_INI_REGION_DRAM_IMR: IMR memory
 * @IWL_FW_INI_REGION_INTERNAL_BUFFER: undefined
 * @IWL_FW_INI_REGION_TXF: TX fifos
 * @IWL_FW_INI_REGION_RXF: RX fifo
 * @IWL_FW_INI_REGION_PAGING: paging memory
 * @IWL_FW_INI_REGION_CSR: CSR registers
 * @IWL_FW_INI_REGION_NUM: number of region types
 */
enum iwl_fw_ini_region_type {
	IWL_FW_INI_REGION_INVALID,
	IWL_FW_INI_REGION_DEVICE_MEMORY,
	IWL_FW_INI_REGION_PERIPHERY_MAC,
	IWL_FW_INI_REGION_PERIPHERY_PHY,
	IWL_FW_INI_REGION_PERIPHERY_AUX,
	IWL_FW_INI_REGION_DRAM_BUFFER,
	IWL_FW_INI_REGION_DRAM_IMR,
	IWL_FW_INI_REGION_INTERNAL_BUFFER,
	IWL_FW_INI_REGION_TXF,
	IWL_FW_INI_REGION_RXF,
	IWL_FW_INI_REGION_PAGING,
	IWL_FW_INI_REGION_CSR,
	IWL_FW_INI_REGION_NUM
}; /* FW_DEBUG_TLV_REGION_TYPE_E_VER_1 */

#endif
