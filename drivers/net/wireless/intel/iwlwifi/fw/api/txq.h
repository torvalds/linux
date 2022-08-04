/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2014, 2019-2020 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_txq_h__
#define __iwl_fw_api_txq_h__

/*
 * DQA queue numbers
 *
 * @IWL_MVM_DQA_CMD_QUEUE: a queue reserved for sending HCMDs to the FW
 * @IWL_MVM_DQA_AUX_QUEUE: a queue reserved for aux frames
 * @IWL_MVM_DQA_P2P_DEVICE_QUEUE: a queue reserved for P2P device frames
 * @IWL_MVM_DQA_INJECT_MONITOR_QUEUE: a queue reserved for injection using
 *	monitor mode. Note this queue is the same as the queue for P2P device
 *	but we can't have active monitor mode along with P2P device anyway.
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
	IWL_MVM_DQA_INJECT_MONITOR_QUEUE = 2,
	IWL_MVM_DQA_GCAST_QUEUE = 3,
	IWL_MVM_DQA_BSS_CLIENT_QUEUE = 4,
	IWL_MVM_DQA_MIN_MGMT_QUEUE = 5,
	IWL_MVM_DQA_MAX_MGMT_QUEUE = 8,
	IWL_MVM_DQA_AP_PROBE_RESP_QUEUE = 9,
	IWL_MVM_DQA_MIN_DATA_QUEUE = 10,
	IWL_MVM_DQA_MAX_DATA_QUEUE = 30,
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

#define IWL_DEFAULT_QUEUE_SIZE 256
#define IWL_MGMT_QUEUE_SIZE 16
#define IWL_CMD_QUEUE_SIZE 32
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
