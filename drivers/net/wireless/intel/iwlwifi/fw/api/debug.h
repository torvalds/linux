/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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
#ifndef __iwl_fw_api_debug_h__
#define __iwl_fw_api_debug_h__

/**
 * enum iwl_debug_cmds - debug commands
 */
enum iwl_debug_cmds {
	/**
	 * @LMAC_RD_WR:
	 * LMAC memory read/write, using &struct iwl_dbg_mem_access_cmd and
	 * &struct iwl_dbg_mem_access_rsp
	 */
	LMAC_RD_WR = 0x0,
	/**
	 * @UMAC_RD_WR:
	 * UMAC memory read/write, using &struct iwl_dbg_mem_access_cmd and
	 * &struct iwl_dbg_mem_access_rsp
	 */
	UMAC_RD_WR = 0x1,
	/**
	 * @MFU_ASSERT_DUMP_NTF:
	 * &struct iwl_mfu_assert_dump_notif
	 */
	MFU_ASSERT_DUMP_NTF = 0xFE,
};

/* Error response/notification */
enum {
	FW_ERR_UNKNOWN_CMD = 0x0,
	FW_ERR_INVALID_CMD_PARAM = 0x1,
	FW_ERR_SERVICE = 0x2,
	FW_ERR_ARC_MEMORY = 0x3,
	FW_ERR_ARC_CODE = 0x4,
	FW_ERR_WATCH_DOG = 0x5,
	FW_ERR_WEP_GRP_KEY_INDX = 0x10,
	FW_ERR_WEP_KEY_SIZE = 0x11,
	FW_ERR_OBSOLETE_FUNC = 0x12,
	FW_ERR_UNEXPECTED = 0xFE,
	FW_ERR_FATAL = 0xFF
};

/**
 * struct iwl_error_resp - FW error indication
 * ( REPLY_ERROR = 0x2 )
 * @error_type: one of FW_ERR_*
 * @cmd_id: the command ID for which the error occurred
 * @reserved1: reserved
 * @bad_cmd_seq_num: sequence number of the erroneous command
 * @error_service: which service created the error, applicable only if
 *     error_type = 2, otherwise 0
 * @timestamp: TSF in usecs.
 */
struct iwl_error_resp {
	__le32 error_type;
	u8 cmd_id;
	u8 reserved1;
	__le16 bad_cmd_seq_num;
	__le32 error_service;
	__le64 timestamp;
} __packed;

#define TX_FIFO_MAX_NUM_9000		8
#define TX_FIFO_MAX_NUM			15
#define RX_FIFO_MAX_NUM			2
#define TX_FIFO_INTERNAL_MAX_NUM	6

/**
 * struct iwl_shared_mem_cfg_v2 - Shared memory configuration information
 *
 * @shared_mem_addr: shared memory addr (pre 8000 HW set to 0x0 as MARBH is not
 *	accessible)
 * @shared_mem_size: shared memory size
 * @sample_buff_addr: internal sample (mon/adc) buff addr (pre 8000 HW set to
 *	0x0 as accessible only via DBGM RDAT)
 * @sample_buff_size: internal sample buff size
 * @txfifo_addr: start addr of TXF0 (excluding the context table 0.5KB), (pre
 *	8000 HW set to 0x0 as not accessible)
 * @txfifo_size: size of TXF0 ... TXF7
 * @rxfifo_size: RXF1, RXF2 sizes. If there is no RXF2, it'll have a value of 0
 * @page_buff_addr: used by UMAC and performance debug (page miss analysis),
 *	when paging is not supported this should be 0
 * @page_buff_size: size of %page_buff_addr
 * @rxfifo_addr: Start address of rxFifo
 * @internal_txfifo_addr: start address of internalFifo
 * @internal_txfifo_size: internal fifos' size
 *
 * NOTE: on firmware that don't have IWL_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG
 *	 set, the last 3 members don't exist.
 */
struct iwl_shared_mem_cfg_v2 {
	__le32 shared_mem_addr;
	__le32 shared_mem_size;
	__le32 sample_buff_addr;
	__le32 sample_buff_size;
	__le32 txfifo_addr;
	__le32 txfifo_size[TX_FIFO_MAX_NUM_9000];
	__le32 rxfifo_size[RX_FIFO_MAX_NUM];
	__le32 page_buff_addr;
	__le32 page_buff_size;
	__le32 rxfifo_addr;
	__le32 internal_txfifo_addr;
	__le32 internal_txfifo_size[TX_FIFO_INTERNAL_MAX_NUM];
} __packed; /* SHARED_MEM_ALLOC_API_S_VER_2 */

/**
 * struct iwl_shared_mem_lmac_cfg - LMAC shared memory configuration
 *
 * @txfifo_addr: start addr of TXF0 (excluding the context table 0.5KB)
 * @txfifo_size: size of TX FIFOs
 * @rxfifo1_addr: RXF1 addr
 * @rxfifo1_size: RXF1 size
 */
struct iwl_shared_mem_lmac_cfg {
	__le32 txfifo_addr;
	__le32 txfifo_size[TX_FIFO_MAX_NUM];
	__le32 rxfifo1_addr;
	__le32 rxfifo1_size;

} __packed; /* SHARED_MEM_ALLOC_LMAC_API_S_VER_1 */

/**
 * struct iwl_shared_mem_cfg - Shared memory configuration information
 *
 * @shared_mem_addr: shared memory address
 * @shared_mem_size: shared memory size
 * @sample_buff_addr: internal sample (mon/adc) buff addr
 * @sample_buff_size: internal sample buff size
 * @rxfifo2_addr: start addr of RXF2
 * @rxfifo2_size: size of RXF2
 * @page_buff_addr: used by UMAC and performance debug (page miss analysis),
 *	when paging is not supported this should be 0
 * @page_buff_size: size of %page_buff_addr
 * @lmac_num: number of LMACs (1 or 2)
 * @lmac_smem: per - LMAC smem data
 */
struct iwl_shared_mem_cfg {
	__le32 shared_mem_addr;
	__le32 shared_mem_size;
	__le32 sample_buff_addr;
	__le32 sample_buff_size;
	__le32 rxfifo2_addr;
	__le32 rxfifo2_size;
	__le32 page_buff_addr;
	__le32 page_buff_size;
	__le32 lmac_num;
	struct iwl_shared_mem_lmac_cfg lmac_smem[2];
} __packed; /* SHARED_MEM_ALLOC_API_S_VER_3 */

/**
 * struct iwl_mfuart_load_notif - mfuart image version & status
 * ( MFUART_LOAD_NOTIFICATION = 0xb1 )
 * @installed_ver: installed image version
 * @external_ver: external image version
 * @status: MFUART loading status
 * @duration: MFUART loading time
 * @image_size: MFUART image size in bytes
*/
struct iwl_mfuart_load_notif {
	__le32 installed_ver;
	__le32 external_ver;
	__le32 status;
	__le32 duration;
	/* image size valid only in v2 of the command */
	__le32 image_size;
} __packed; /* MFU_LOADER_NTFY_API_S_VER_2 */

/**
 * struct iwl_mfu_assert_dump_notif - mfuart dump logs
 * ( MFU_ASSERT_DUMP_NTF = 0xfe )
 * @assert_id: mfuart assert id that cause the notif
 * @curr_reset_num: number of asserts since uptime
 * @index_num: current chunk id
 * @parts_num: total number of chunks
 * @data_size: number of data bytes sent
 * @data: data buffer
 */
struct iwl_mfu_assert_dump_notif {
	__le32   assert_id;
	__le32   curr_reset_num;
	__le16   index_num;
	__le16   parts_num;
	__le32   data_size;
	__le32   data[0];
} __packed; /* MFU_DUMP_ASSERT_API_S_VER_1 */

/**
 * enum iwl_mvm_marker_id - marker ids
 *
 * The ids for different type of markers to insert into the usniffer logs
 *
 * @MARKER_ID_TX_FRAME_LATENCY: TX latency marker
 */
enum iwl_mvm_marker_id {
	MARKER_ID_TX_FRAME_LATENCY = 1,
}; /* MARKER_ID_API_E_VER_1 */

/**
 * struct iwl_mvm_marker - mark info into the usniffer logs
 *
 * (MARKER_CMD = 0xcb)
 *
 * Mark the UTC time stamp into the usniffer logs together with additional
 * metadata, so the usniffer output can be parsed.
 * In the command response the ucode will return the GP2 time.
 *
 * @dw_len: The amount of dwords following this byte including this byte.
 * @marker_id: A unique marker id (iwl_mvm_marker_id).
 * @reserved: reserved.
 * @timestamp: in milliseconds since 1970-01-01 00:00:00 UTC
 * @metadata: additional meta data that will be written to the unsiffer log
 */
struct iwl_mvm_marker {
	u8 dw_len;
	u8 marker_id;
	__le16 reserved;
	__le64 timestamp;
	__le32 metadata[0];
} __packed; /* MARKER_API_S_VER_1 */

/* Operation types for the debug mem access */
enum {
	DEBUG_MEM_OP_READ = 0,
	DEBUG_MEM_OP_WRITE = 1,
	DEBUG_MEM_OP_WRITE_BYTES = 2,
};

#define DEBUG_MEM_MAX_SIZE_DWORDS 32

/**
 * struct iwl_dbg_mem_access_cmd - Request the device to read/write memory
 * @op: DEBUG_MEM_OP_*
 * @addr: address to read/write from/to
 * @len: in dwords, to read/write
 * @data: for write opeations, contains the source buffer
 */
struct iwl_dbg_mem_access_cmd {
	__le32 op;
	__le32 addr;
	__le32 len;
	__le32 data[];
} __packed; /* DEBUG_(U|L)MAC_RD_WR_CMD_API_S_VER_1 */

/* Status responses for the debug mem access */
enum {
	DEBUG_MEM_STATUS_SUCCESS = 0x0,
	DEBUG_MEM_STATUS_FAILED = 0x1,
	DEBUG_MEM_STATUS_LOCKED = 0x2,
	DEBUG_MEM_STATUS_HIDDEN = 0x3,
	DEBUG_MEM_STATUS_LENGTH = 0x4,
};

/**
 * struct iwl_dbg_mem_access_rsp - Response to debug mem commands
 * @status: DEBUG_MEM_STATUS_*
 * @len: read dwords (0 for write operations)
 * @data: contains the read DWs
 */
struct iwl_dbg_mem_access_rsp {
	__le32 status;
	__le32 len;
	__le32 data[];
} __packed; /* DEBUG_(U|L)MAC_RD_WR_RSP_API_S_VER_1 */

#define CONT_REC_COMMAND_SIZE	80
#define ENABLE_CONT_RECORDING	0x15
#define DISABLE_CONT_RECORDING	0x16

/*
 * struct iwl_continuous_record_mode - recording mode
 */
struct iwl_continuous_record_mode {
	__le16 enable_recording;
} __packed;

/*
 * struct iwl_continuous_record_cmd - enable/disable continuous recording
 */
struct iwl_continuous_record_cmd {
	struct iwl_continuous_record_mode record_mode;
	u8 pad[CONT_REC_COMMAND_SIZE -
		sizeof(struct iwl_continuous_record_mode)];
} __packed;

#endif /* __iwl_fw_api_debug_h__ */
