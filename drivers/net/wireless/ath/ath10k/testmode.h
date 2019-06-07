/* SPDX-License-Identifier: ISC */
/*
 * Copyright (c) 2014 Qualcomm Atheros, Inc.
 */

#include "core.h"

#ifdef CONFIG_NL80211_TESTMODE

void ath10k_testmode_destroy(struct ath10k *ar);

bool ath10k_tm_event_wmi(struct ath10k *ar, u32 cmd_id, struct sk_buff *skb);
int ath10k_tm_cmd(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  void *data, int len);

#else

static inline void ath10k_testmode_destroy(struct ath10k *ar)
{
}

static inline bool ath10k_tm_event_wmi(struct ath10k *ar, u32 cmd_id,
				       struct sk_buff *skb)
{
	return false;
}

static inline int ath10k_tm_cmd(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				void *data, int len)
{
	return 0;
}

#endif
