/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_WOW_H
#define ATH12K_WOW_H

#define ATH12K_WOW_RETRY_NUM		10
#define ATH12K_WOW_RETRY_WAIT_MS	200
#define ATH12K_WOW_PATTERNS		22

struct ath12k_wow {
	u32 max_num_patterns;
	struct completion wakeup_completed;
	struct wiphy_wowlan_support wowlan_support;
};

struct ath12k_pkt_pattern {
	u8 pattern[WOW_MAX_PATTERN_SIZE];
	u8 bytemask[WOW_MAX_PATTERN_SIZE];
	int pattern_len;
	int pkt_offset;
};

struct rfc1042_hdr {
	u8 llc_dsap;
	u8 llc_ssap;
	u8 llc_ctrl;
	u8 snap_oui[3];
	__be16 eth_type;
} __packed;

#ifdef CONFIG_PM

int ath12k_wow_init(struct ath12k *ar);
int ath12k_wow_op_suspend(struct ieee80211_hw *hw,
			  struct cfg80211_wowlan *wowlan);
int ath12k_wow_op_resume(struct ieee80211_hw *hw);
void ath12k_wow_op_set_wakeup(struct ieee80211_hw *hw, bool enabled);
int ath12k_wow_enable(struct ath12k *ar);
int ath12k_wow_wakeup(struct ath12k *ar);

#else

static inline int ath12k_wow_init(struct ath12k *ar)
{
	return 0;
}

static inline int ath12k_wow_enable(struct ath12k *ar)
{
	return 0;
}

static inline int ath12k_wow_wakeup(struct ath12k *ar)
{
	return 0;
}
#endif /* CONFIG_PM */
#endif /* ATH12K_WOW_H */
