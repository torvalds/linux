/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#ifndef __iwl_mld_rx_h__
#define __iwl_mld_rx_h__

#include "mld.h"

/**
 * enum iwl_mld_internal_rxq_notif_type - RX queue sync notif types
 *
 * @IWL_MLD_RXQ_EMPTY: empty sync notification
 * @IWL_MLD_RXQ_NOTIF_DEL_BA: notify RSS queues of delBA
 */
enum iwl_mld_internal_rxq_notif_type {
	IWL_MLD_RXQ_EMPTY,
	IWL_MLD_RXQ_NOTIF_DEL_BA,
};

/**
 * struct iwl_mld_internal_rxq_notif - @iwl_rxq_sync_cmd internal data.
 * This data is echoed by the firmware to all RSS queues and should be DWORD
 * aligned. FW is agnostic to the data, so there are no endianness requirements
 *
 * @type: one of &iwl_mld_internal_rxq_notif_type
 * @cookie: unique internal cookie to identify old notifications
 * @reserved: reserved for alignment
 * @payload: data to send to RX queues based on the type (may be empty)
 */
struct iwl_mld_internal_rxq_notif {
	u8 type;
	u8 reserved[3];
	u32 cookie;
	u8 payload[];
} __packed;

/**
 * struct iwl_mld_rx_queues_sync - RX queues sync data
 *
 * @waitq: wait queue for RX queues sync completion
 * @cookie: unique id to correlate sync requests with responses
 * @state: bitmask representing the sync state of RX queues
 *	all RX queues bits are set before sending the command, and the
 *	corresponding queue bit cleared upon handling the notification
 */
struct iwl_mld_rx_queues_sync {
	wait_queue_head_t waitq;
	u32 cookie;
	unsigned long state;
};

void iwl_mld_rx_mpdu(struct iwl_mld *mld, struct napi_struct *napi,
		     struct iwl_rx_cmd_buffer *rxb, int queue);

void iwl_mld_sync_rx_queues(struct iwl_mld *mld,
			    enum iwl_mld_internal_rxq_notif_type type,
			    const void *notif_payload, u32 notif_payload_size);

void iwl_mld_handle_rx_queues_sync_notif(struct iwl_mld *mld,
					 struct napi_struct *napi,
					 struct iwl_rx_packet *pkt, int queue);

void iwl_mld_pass_packet_to_mac80211(struct iwl_mld *mld,
				     struct napi_struct *napi,
				     struct sk_buff *skb, int queue,
				     struct ieee80211_sta *sta);

void iwl_mld_rx_monitor_no_data(struct iwl_mld *mld, struct napi_struct *napi,
				struct iwl_rx_packet *pkt, int queue);

#endif /* __iwl_mld_agg_h__ */
