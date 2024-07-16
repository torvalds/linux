/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#include "core.h"

#ifdef CONFIG_NL80211_TESTMODE

bool ath11k_tm_event_wmi(struct ath11k *ar, u32 cmd_id, struct sk_buff *skb);
int ath11k_tm_cmd(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  void *data, int len);

#else

static inline bool ath11k_tm_event_wmi(struct ath11k *ar, u32 cmd_id,
				       struct sk_buff *skb)
{
	return false;
}

static inline int ath11k_tm_cmd(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				void *data, int len)
{
	return 0;
}

#endif
