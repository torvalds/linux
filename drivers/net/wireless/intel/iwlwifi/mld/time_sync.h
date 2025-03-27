/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2025 Intel Corporation
 */
#ifndef __iwl_mld_time_sync_h__
#define __iwl_mld_time_sync_h__

struct iwl_mld_time_sync_data {
	struct rcu_head rcu_head;
	u8 peer_addr[ETH_ALEN];
	u32 active_protocols;
	struct sk_buff_head frame_list;
};

int iwl_mld_time_sync_config(struct iwl_mld *mld, const u8 *addr,
			     u32 protocols);
int iwl_mld_time_sync_fw_config(struct iwl_mld *mld);
void iwl_mld_deinit_time_sync(struct iwl_mld *mld);
void iwl_mld_handle_time_msmt_notif(struct iwl_mld *mld,
				    struct iwl_rx_packet *pkt);
bool iwl_mld_time_sync_frame(struct iwl_mld *mld, struct sk_buff *skb,
			     u8 *addr);
void iwl_mld_handle_time_sync_confirm_notif(struct iwl_mld *mld,
					    struct iwl_rx_packet *pkt);

#endif /* __iwl_mld_time_sync_h__ */
