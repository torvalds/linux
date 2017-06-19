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
#ifndef __iwl_fw_api_txq_h__
#define __iwl_fw_api_txq_h__

/*
 * DQA queue numbers
 *
 * @IWL_MVM_DQA_CMD_QUEUE: a queue reserved for sending HCMDs to the FW
 * @IWL_MVM_DQA_AUX_QUEUE: a queue reserved for aux frames
 * @IWL_MVM_DQA_P2P_DEVICE_QUEUE: a queue reserved for P2P device frames
 * @IWL_MVM_DQA_GCAST_QUEUE: a queue reserved for P2P GO/SoftAP GCAST frames
 * @IWL_MVM_DQA_BSS_CLIENT_QUEUE: a queue reserved for BSS activity, to ensure
 *	that we are never left without the possibility to connect to an AP.
 * @IWL_MVM_DQA_MIN_MGMT_QUEUE: first TXQ in pool for MGMT and non-QOS frames.
 *	Each MGMT queue is mapped to a single STA
 *	MGMT frames are frames that return true on ieee80211_is_mgmt()
 * @IWL_MVM_DQA_MAX_MGMT_QUEUE: last TXQ in pool for MGMT frames
 * @IWL_MVM_DQA_AP_PROBE_RESP_QUEUE: a queue reserved for P2P GO/SoftAP probe
 *	responses
 * @IWL_MVM_DQA_MIN_DATA_QUEUE: first TXQ in pool for DATA frames.
 *	DATA frames are intended for !ieee80211_is_mgmt() frames, but if
 *	the MGMT TXQ pool is exhausted, mgmt frames can be sent on DATA queues
 *	as well
 * @IWL_MVM_DQA_MAX_DATA_QUEUE: last TXQ in pool for DATA frames
 */
enum iwl_mvm_dqa_txq {
	IWL_MVM_DQA_CMD_QUEUE = 0,
	IWL_MVM_DQA_AUX_QUEUE = 1,
	IWL_MVM_DQA_P2P_DEVICE_QUEUE = 2,
	IWL_MVM_DQA_GCAST_QUEUE = 3,
	IWL_MVM_DQA_BSS_CLIENT_QUEUE = 4,
	IWL_MVM_DQA_MIN_MGMT_QUEUE = 5,
	IWL_MVM_DQA_MAX_MGMT_QUEUE = 8,
	IWL_MVM_DQA_AP_PROBE_RESP_QUEUE = 9,
	IWL_MVM_DQA_MIN_DATA_QUEUE = 10,
	IWL_MVM_DQA_MAX_DATA_QUEUE = 31,
};

enum iwl_mvm_tx_fifo {
	IWL_MVM_TX_FIFO_BK = 0,
	IWL_MVM_TX_FIFO_BE,
	IWL_MVM_TX_FIFO_VI,
	IWL_MVM_TX_FIFO_VO,
	IWL_MVM_TX_FIFO_MCAST = 5,
	IWL_MVM_TX_FIFO_CMD = 7,
};

enum iwl_gen2_tx_fifo {
	IWL_GEN2_TX_FIFO_CMD = 0,
	IWL_GEN2_EDCA_TX_FIFO_BK,
	IWL_GEN2_EDCA_TX_FIFO_BE,
	IWL_GEN2_EDCA_TX_FIFO_VI,
	IWL_GEN2_EDCA_TX_FIFO_VO,
	IWL_GEN2_TRIG_TX_FIFO_BK,
	IWL_GEN2_TRIG_TX_FIFO_BE,
	IWL_GEN2_TRIG_TX_FIFO_VI,
	IWL_GEN2_TRIG_TX_FIFO_VO,
};

/**
 * enum iwl_tx_queue_cfg_actions - TXQ config options
 * @TX_QUEUE_CFG_ENABLE_QUEUE: enable a queue
 * @TX_QUEUE_CFG_TFD_SHORT_FORMAT: use short TFD format
 */
enum iwl_tx_queue_cfg_actions {
	TX_QUEUE_CFG_ENABLE_QUEUE		= BIT(0),
	TX_QUEUE_CFG_TFD_SHORT_FORMAT		= BIT(1),
};

/**
 * struct iwl_tx_queue_cfg_cmd - txq hw scheduler config command
 * @sta_id: station id
 * @tid: tid of the queue
 * @flags: see &enum iwl_tx_queue_cfg_actions
 * @cb_size: size of TFD cyclic buffer. Value is exponent - 3.
 *	Minimum value 0 (8 TFDs), maximum value 5 (256 TFDs)
 * @byte_cnt_addr: address of byte count table
 * @tfdq_addr: address of TFD circular buffer
 */
struct iwl_tx_queue_cfg_cmd {
	u8 sta_id;
	u8 tid;
	__le16 flags;
	__le32 cb_size;
	__le64 byte_cnt_addr;
	__le64 tfdq_addr;
} __packed; /* TX_QUEUE_CFG_CMD_API_S_VER_2 */

/**
 * struct iwl_tx_queue_cfg_rsp - response to txq hw scheduler config
 * @queue_number: queue number assigned to this RA -TID
 * @flags: set on failure
 * @write_pointer: initial value for write pointer
 * @reserved: reserved
 */
struct iwl_tx_queue_cfg_rsp {
	__le16 queue_number;
	__le16 flags;
	__le16 write_pointer;
	__le16 reserved;
} __packed; /* TX_QUEUE_CFG_RSP_API_S_VER_2 */

#endif /* __iwl_fw_api_txq_h__ */
