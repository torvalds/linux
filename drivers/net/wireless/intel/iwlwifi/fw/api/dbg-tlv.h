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

#define IWL_FW_INI_MAX_REGION_ID		64
#define IWL_FW_INI_MAX_NAME			32
#define IWL_FW_INI_MAX_CFG_NAME			64
#define IWL_FW_INI_DOMAIN_ALWAYS_ON		0

/**
 * struct iwl_fw_ini_hcmd
 *
 * @id: the debug configuration command type for instance: 0xf6 / 0xf5 / DHC
 * @group: the desired cmd group
 * @reserved: to align to FW struct
 * @data: all of the relevant command data to be sent
 */
struct iwl_fw_ini_hcmd {
	u8 id;
	u8 group;
	__le16 reserved;
	u8 data[0];
} __packed; /* FW_DEBUG_TLV_HCMD_DATA_API_S_VER_1 */

/**
 * struct iwl_fw_ini_header - Common Header for all ini debug TLV's structures
 *
 * @version: TLV version
 * @domain: domain of the TLV. One of &enum iwl_fw_ini_dbg_domain
 * @data: TLV data
 */
struct iwl_fw_ini_header {
	__le32 version;
	__le32 domain;
	u8 data[];
} __packed; /* FW_TLV_DEBUG_HEADER_S_VER_1 */

/**
 * struct iwl_fw_ini_region_dev_addr - Configuration to read device addresses
 *
 * @size: size of each memory chunk
 * @offset: offset to add to the base address of each chunk
 */
struct iwl_fw_ini_region_dev_addr {
	__le32 size;
	__le32 offset;
} __packed; /* FW_TLV_DEBUG_DEVICE_ADDR_API_S_VER_1 */

/**
 * struct iwl_fw_ini_region_fifos - Configuration to read Tx/Rx fifos
 *
 * @fid: fifos ids array. Used to determine what fifos to collect
 * @hdr_only: if non zero, collect only the registers
 * @offset: offset to add to the registers addresses
 */
struct iwl_fw_ini_region_fifos {
	__le32 fid[2];
	__le32 hdr_only;
	__le32 offset;
} __packed; /* FW_TLV_DEBUG_REGION_FIFOS_API_S_VER_1 */

/**
 * struct iwl_fw_ini_region_err_table - error table region data
 *
 * Configuration to read Umac/Lmac error table
 *
 * @version: version of the error table
 * @base_addr: base address of the error table
 * @size: size of the error table
 * @offset: offset to add to &base_addr
 */
struct iwl_fw_ini_region_err_table {
	__le32 version;
	__le32 base_addr;
	__le32 size;
	__le32 offset;
} __packed; /* FW_TLV_DEBUG_REGION_ERROR_TABLE_API_S_VER_1 */

/**
 * struct iwl_fw_ini_region_internal_buffer - internal buffer region data
 *
 * Configuration to read internal monitor buffer
 *
 * @alloc_id: allocation id one of &enum iwl_fw_ini_allocation_id
 * @base_addr: internal buffer base address
 * @size: size internal buffer size
 */
struct iwl_fw_ini_region_internal_buffer {
	__le32 alloc_id;
	__le32 base_addr;
	__le32 size;
} __packed; /* FW_TLV_DEBUG_REGION_INTERNAL_BUFFER_API_S_VER_1 */

/**
 * struct iwl_fw_ini_region_tlv - region TLV
 *
 * Configures parameters for region data collection
 *
 * @hdr: debug header
 * @id: region id. Max id is &IWL_FW_INI_MAX_REGION_ID
 * @type: region type. One of &enum iwl_fw_ini_region_type
 * @name: region name
 * @dev_addr: device address configuration. Used by
 *	&IWL_FW_INI_REGION_DEVICE_MEMORY, &IWL_FW_INI_REGION_PERIPHERY_MAC,
 *	&IWL_FW_INI_REGION_PERIPHERY_PHY, &IWL_FW_INI_REGION_PERIPHERY_AUX,
 *	&IWL_FW_INI_REGION_PAGING, &IWL_FW_INI_REGION_CSR,
 *	&IWL_FW_INI_REGION_DRAM_IMR and &IWL_FW_INI_REGION_PCI_IOSF_CONFIG
 * @fifos: fifos configuration. Used by &IWL_FW_INI_REGION_TXF and
 *	&IWL_FW_INI_REGION_RXF
 * @err_table: error table configuration. Used by
 *	IWL_FW_INI_REGION_LMAC_ERROR_TABLE and
 *	IWL_FW_INI_REGION_UMAC_ERROR_TABLE
 * @internal_buffer: internal monitor buffer configuration. Used by
 *	&IWL_FW_INI_REGION_INTERNAL_BUFFER
 * @dram_alloc_id: dram allocation id. One of &enum iwl_fw_ini_allocation_id.
 *	Used by &IWL_FW_INI_REGION_DRAM_BUFFER
 * @tlv_mask: tlv collection mask. Used by &IWL_FW_INI_REGION_TLV
 * @addrs: array of addresses attached to the end of the region tlv
 */
struct iwl_fw_ini_region_tlv {
	struct iwl_fw_ini_header hdr;
	__le32 id;
	__le32 type;
	u8 name[IWL_FW_INI_MAX_NAME];
	union {
		struct iwl_fw_ini_region_dev_addr dev_addr;
		struct iwl_fw_ini_region_fifos fifos;
		struct iwl_fw_ini_region_err_table err_table;
		struct iwl_fw_ini_region_internal_buffer internal_buffer;
		__le32 dram_alloc_id;
		__le32 tlv_mask;
	}; /* FW_TLV_DEBUG_REGION_CONF_PARAMS_API_U_VER_1 */
	__le32 addrs[];
} __packed; /* FW_TLV_DEBUG_REGION_API_S_VER_1 */

/**
 * struct iwl_fw_ini_debug_info_tlv
 *
 * debug configuration name for a specific image
 *
 * @hdr: debug header
 * @image_type: image type
 * @debug_cfg_name: debug configuration name
 */
struct iwl_fw_ini_debug_info_tlv {
	struct iwl_fw_ini_header hdr;
	__le32 image_type;
	u8 debug_cfg_name[IWL_FW_INI_MAX_CFG_NAME];
} __packed; /* FW_TLV_DEBUG_INFO_API_S_VER_1 */

/**
 * struct iwl_fw_ini_allocation_tlv - Allocates DRAM buffers
 *
 * @hdr: debug header
 * @alloc_id: allocation id. One of &enum iwl_fw_ini_allocation_id
 * @buf_location: buffer location. One of &enum iwl_fw_ini_buffer_location
 * @req_size: requested buffer size
 * @max_frags_num: maximum number of fragments
 * @min_size: minimum buffer size
 */
struct iwl_fw_ini_allocation_tlv {
	struct iwl_fw_ini_header hdr;
	__le32 alloc_id;
	__le32 buf_location;
	__le32 req_size;
	__le32 max_frags_num;
	__le32 min_size;
} __packed; /* FW_TLV_DEBUG_BUFFER_ALLOCATION_API_S_VER_1 */

/**
 * struct iwl_fw_ini_trigger_tlv - trigger TLV
 *
 * Trigger that upon firing, determines what regions to collect
 *
 * @hdr: debug header
 * @time_point: time point. One of &enum iwl_fw_ini_time_point
 * @trigger_reason: trigger reason
 * @apply_policy: uses &enum iwl_fw_ini_trigger_apply_policy
 * @dump_delay: delay from trigger fire to dump, in usec
 * @occurrences: max trigger fire occurrences allowed
 * @reserved: unused
 * @ignore_consec: ignore consecutive triggers, in usec
 * @reset_fw: if non zero, will reset and reload the FW
 * @multi_dut: initiate debug dump data on several DUTs
 * @regions_mask: mask of regions to collect
 * @data: trigger data
 */
struct iwl_fw_ini_trigger_tlv {
	struct iwl_fw_ini_header hdr;
	__le32 time_point;
	__le32 trigger_reason;
	__le32 apply_policy;
	__le32 dump_delay;
	__le32 occurrences;
	__le32 reserved;
	__le32 ignore_consec;
	__le32 reset_fw;
	__le32 multi_dut;
	__le64 regions_mask;
	__le32 data[];
} __packed; /* FW_TLV_DEBUG_TRIGGER_API_S_VER_1 */

/**
 * struct iwl_fw_ini_hcmd_tlv - Generic Host command pass through TLV
 *
 * @hdr: debug header
 * @time_point: time point. One of &enum iwl_fw_ini_time_point
 * @period_msec: interval at which the hcmd will be sent to the FW.
 *	Measured in msec (0 = one time command)
 * @hcmd: a variable length host-command to be sent to apply the configuration
 */
struct iwl_fw_ini_hcmd_tlv {
	struct iwl_fw_ini_header hdr;
	__le32 time_point;
	__le32 period_msec;
	struct iwl_fw_ini_hcmd hcmd;
} __packed; /* FW_TLV_DEBUG_HCMD_API_S_VER_1 */

/**
 * enum iwl_fw_ini_allocation_id
 *
 * @IWL_FW_INI_ALLOCATION_INVALID: invalid
 * @IWL_FW_INI_ALLOCATION_ID_DBGC1: allocation meant for DBGC1 configuration
 * @IWL_FW_INI_ALLOCATION_ID_DBGC2: allocation meant for DBGC2 configuration
 * @IWL_FW_INI_ALLOCATION_ID_DBGC3: allocation meant for DBGC3 configuration
 * @IWL_FW_INI_ALLOCATION_NUM: number of allocation ids
*/
enum iwl_fw_ini_allocation_id {
	IWL_FW_INI_ALLOCATION_INVALID,
	IWL_FW_INI_ALLOCATION_ID_DBGC1,
	IWL_FW_INI_ALLOCATION_ID_DBGC2,
	IWL_FW_INI_ALLOCATION_ID_DBGC3,
	IWL_FW_INI_ALLOCATION_NUM,
}; /* FW_DEBUG_TLV_ALLOCATION_ID_E_VER_1 */

/**
 * enum iwl_fw_ini_buffer_location
 *
 * @IWL_FW_INI_LOCATION_INVALID: invalid
 * @IWL_FW_INI_LOCATION_SRAM_PATH: SRAM location
 * @IWL_FW_INI_LOCATION_DRAM_PATH: DRAM location
 * @IWL_FW_INI_LOCATION_NPK_PATH: NPK location
 */
enum iwl_fw_ini_buffer_location {
	IWL_FW_INI_LOCATION_INVALID,
	IWL_FW_INI_LOCATION_SRAM_PATH,
	IWL_FW_INI_LOCATION_DRAM_PATH,
	IWL_FW_INI_LOCATION_NPK_PATH,
}; /* FW_DEBUG_TLV_BUFFER_LOCATION_E_VER_1 */

/**
 * enum iwl_fw_ini_region_type
 *
 * @IWL_FW_INI_REGION_INVALID: invalid
 * @IWL_FW_INI_REGION_TLV: uCode and debug TLVs
 * @IWL_FW_INI_REGION_INTERNAL_BUFFER: monitor SMEM buffer
 * @IWL_FW_INI_REGION_DRAM_BUFFER: monitor DRAM buffer
 * @IWL_FW_INI_REGION_TXF: TX fifos
 * @IWL_FW_INI_REGION_RXF: RX fifo
 * @IWL_FW_INI_REGION_LMAC_ERROR_TABLE: lmac error table
 * @IWL_FW_INI_REGION_UMAC_ERROR_TABLE: umac error table
 * @IWL_FW_INI_REGION_RSP_OR_NOTIF: FW response or notification data
 * @IWL_FW_INI_REGION_DEVICE_MEMORY: device internal memory
 * @IWL_FW_INI_REGION_PERIPHERY_MAC: periphery registers of MAC
 * @IWL_FW_INI_REGION_PERIPHERY_PHY: periphery registers of PHY
 * @IWL_FW_INI_REGION_PERIPHERY_AUX: periphery registers of AUX
 * @IWL_FW_INI_REGION_PAGING: paging memory
 * @IWL_FW_INI_REGION_CSR: CSR registers
 * @IWL_FW_INI_REGION_DRAM_IMR: IMR memory
 * @IWL_FW_INI_REGION_PCI_IOSF_CONFIG: PCI/IOSF config
 * @IWL_FW_INI_REGION_NUM: number of region types
 */
enum iwl_fw_ini_region_type {
	IWL_FW_INI_REGION_INVALID,
	IWL_FW_INI_REGION_TLV,
	IWL_FW_INI_REGION_INTERNAL_BUFFER,
	IWL_FW_INI_REGION_DRAM_BUFFER,
	IWL_FW_INI_REGION_TXF,
	IWL_FW_INI_REGION_RXF,
	IWL_FW_INI_REGION_LMAC_ERROR_TABLE,
	IWL_FW_INI_REGION_UMAC_ERROR_TABLE,
	IWL_FW_INI_REGION_RSP_OR_NOTIF,
	IWL_FW_INI_REGION_DEVICE_MEMORY,
	IWL_FW_INI_REGION_PERIPHERY_MAC,
	IWL_FW_INI_REGION_PERIPHERY_PHY,
	IWL_FW_INI_REGION_PERIPHERY_AUX,
	IWL_FW_INI_REGION_PAGING,
	IWL_FW_INI_REGION_CSR,
	IWL_FW_INI_REGION_DRAM_IMR,
	IWL_FW_INI_REGION_PCI_IOSF_CONFIG,
	IWL_FW_INI_REGION_NUM
}; /* FW_TLV_DEBUG_REGION_TYPE_API_E */

/**
 * enum iwl_fw_ini_time_point
 *
 * Hard coded time points in which the driver can send hcmd or perform dump
 * collection
 *
 * @IWL_FW_INI_TIME_POINT_EARLY: pre loading the FW
 * @IWL_FW_INI_TIME_POINT_AFTER_ALIVE: first cmd from host after alive notif
 * @IWL_FW_INI_TIME_POINT_POST_INIT: last cmd in series of init sequence
 * @IWL_FW_INI_TIME_POINT_FW_ASSERT: FW assert
 * @IWL_FW_INI_TIME_POINT_FW_HW_ERROR: FW HW error
 * @IWL_FW_INI_TIME_POINT_FW_TFD_Q_HANG: TFD queue hang
 * @IWL_FW_INI_TIME_POINT_FW_DHC_NOTIFOCATION: DHC cmd response and notif
 * @IWL_FW_INI_TIME_POINT_FW_RSP_OR_NOTIF: FW response or notification.
 *	data field holds id and group
 * @IWL_FW_INI_TIME_POINT_USER_TRIGGER: user trigger time point
 * @IWL_FW_INI_TIME_POINT_PERIODIC: periodic timepoint that fires in constant
 *	intervals. data field holds the interval time in msec
 * @IWL_FW_INI_TIME_POINT_WDG_TIMEOUT: watchdog timeout
 * @IWL_FW_INI_TIME_POINT_HOST_ASSERT: Unused
 * @IWL_FW_INI_TIME_POINT_HOST_ALIVE_TIMEOUT: alive timeout
 * @IWL_FW_INI_TIME_POINT_HOST_DEVICE_ENABLE: device enable
 * @IWL_FW_INI_TIME_POINT_HOST_DEVICE_DISABLE: device disable
 * @IWL_FW_INI_TIME_POINT_HOST_D3_START: D3 start
 * @IWL_FW_INI_TIME_POINT_HOST_D3_END: D3 end
 * @IWL_FW_INI_TIME_POINT_MISSED_BEACONS: missed beacons
 * @IWL_FW_INI_TIME_POINT_ASSOC_FAILED: association failure
 * @IWL_FW_INI_TIME_POINT_TX_FAILED: Tx frame failed
 * @IWL_FW_INI_TIME_POINT_TX_WFD_ACTION_FRAME_FAILED: wifi direct action
 *	frame failed
 * @IWL_FW_INI_TIME_POINT_TX_LATENCY_THRESHOLD: Tx latency threshold
 * @IWL_FW_INI_TIME_POINT_HANG_OCCURRED: hang occurred
 * @IWL_FW_INI_TIME_POINT_EAPOL_FAILED: EAPOL failed
 * @IWL_FW_INI_TIME_POINT_FAKE_TX: fake Tx
 * @IWL_FW_INI_TIME_POINT_DEASSOC: de association
 * @IWL_FW_INI_TIME_POINT_NUM: number of time points
 */
enum iwl_fw_ini_time_point {
	IWL_FW_INI_TIME_POINT_INVALID,
	IWL_FW_INI_TIME_POINT_EARLY,
	IWL_FW_INI_TIME_POINT_AFTER_ALIVE,
	IWL_FW_INI_TIME_POINT_POST_INIT,
	IWL_FW_INI_TIME_POINT_FW_ASSERT,
	IWL_FW_INI_TIME_POINT_FW_HW_ERROR,
	IWL_FW_INI_TIME_POINT_FW_TFD_Q_HANG,
	IWL_FW_INI_TIME_POINT_FW_DHC_NOTIFOCATION,
	IWL_FW_INI_TIME_POINT_FW_RSP_OR_NOTIF,
	IWL_FW_INI_TIME_POINT_USER_TRIGGER,
	IWL_FW_INI_TIME_POINT_PERIODIC,
	IWL_FW_INI_TIME_POINT_WDG_TIMEOUT,
	IWL_FW_INI_TIME_POINT_HOST_ASSERT,
	IWL_FW_INI_TIME_POINT_HOST_ALIVE_TIMEOUT,
	IWL_FW_INI_TIME_POINT_HOST_DEVICE_ENABLE,
	IWL_FW_INI_TIME_POINT_HOST_DEVICE_DISABLE,
	IWL_FW_INI_TIME_POINT_HOST_D3_START,
	IWL_FW_INI_TIME_POINT_HOST_D3_END,
	IWL_FW_INI_TIME_POINT_MISSED_BEACONS,
	IWL_FW_INI_TIME_POINT_ASSOC_FAILED,
	IWL_FW_INI_TIME_POINT_TX_FAILED,
	IWL_FW_INI_TIME_POINT_TX_WFD_ACTION_FRAME_FAILED,
	IWL_FW_INI_TIME_POINT_TX_LATENCY_THRESHOLD,
	IWL_FW_INI_TIME_POINT_HANG_OCCURRED,
	IWL_FW_INI_TIME_POINT_EAPOL_FAILED,
	IWL_FW_INI_TIME_POINT_FAKE_TX,
	IWL_FW_INI_TIME_POINT_DEASSOC,
	IWL_FW_INI_TIME_POINT_NUM,
}; /* FW_TLV_DEBUG_TIME_POINT_API_E */

/**
 * enum iwl_fw_ini_trigger_apply_policy - Determines how to apply triggers
 *
 * @IWL_FW_INI_APPLY_POLICY_MATCH_TIME_POINT: match by time point
 * @IWL_FW_INI_APPLY_POLICY_MATCH_DATA: match by trigger data
 * @IWL_FW_INI_APPLY_POLICY_OVERRIDE_REGIONS: override regions mask.
 *	Append otherwise
 * @IWL_FW_INI_APPLY_POLICY_OVERRIDE_CFG: override trigger configuration
 * @IWL_FW_INI_APPLY_POLICY_OVERRIDE_DATA: override trigger data.
 *	Append otherwise
 */
enum iwl_fw_ini_trigger_apply_policy {
	IWL_FW_INI_APPLY_POLICY_MATCH_TIME_POINT	= BIT(0),
	IWL_FW_INI_APPLY_POLICY_MATCH_DATA		= BIT(1),
	IWL_FW_INI_APPLY_POLICY_OVERRIDE_REGIONS	= BIT(8),
	IWL_FW_INI_APPLY_POLICY_OVERRIDE_CFG		= BIT(9),
	IWL_FW_INI_APPLY_POLICY_OVERRIDE_DATA		= BIT(10),
};
#endif
