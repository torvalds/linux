/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2022 Intel Corporation
 */
#ifndef __TIME_SYNC_H__
#define __TIME_SYNC_H__

#include "mvm.h"
#include <linux/ieee80211.h>

void iwl_mvm_init_time_sync(struct iwl_time_sync_data *data);
void iwl_mvm_time_sync_msmt_event(struct iwl_mvm *mvm,
				  struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_time_sync_msmt_confirm_event(struct iwl_mvm *mvm,
					  struct iwl_rx_cmd_buffer *rxb);
int iwl_mvm_time_sync_config(struct iwl_mvm *mvm, const u8 *addr,
			     u32 protocols);

static inline
bool iwl_mvm_time_sync_frame(struct iwl_mvm *mvm, struct sk_buff *skb, u8 *addr)
{
	if (ether_addr_equal(mvm->time_sync.peer_addr, addr) &&
	    (ieee80211_is_timing_measurement(skb) || ieee80211_is_ftm(skb))) {
		skb_queue_tail(&mvm->time_sync.frame_list, skb);
		return true;
	}

	return false;
}
#endif
