/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _WOW_H_
#define _WOW_H_

struct ath11k_wow {
	u32 max_num_patterns;
	struct completion wakeup_completed;
	struct wiphy_wowlan_support wowlan_support;
};

struct rfc1042_hdr {
	u8 llc_dsap;
	u8 llc_ssap;
	u8 llc_ctrl;
	u8 snap_oui[3];
	__be16 snap_type;
} __packed;

#define ATH11K_WOW_RETRY_NUM		3
#define ATH11K_WOW_RETRY_WAIT_MS	200
#define ATH11K_WOW_PATTERNS		22

#ifdef CONFIG_PM

int ath11k_wow_init(struct ath11k *ar);
int ath11k_wow_op_suspend(struct ieee80211_hw *hw,
			  struct cfg80211_wowlan *wowlan);
int ath11k_wow_op_resume(struct ieee80211_hw *hw);
void ath11k_wow_op_set_wakeup(struct ieee80211_hw *hw, bool enabled);
int ath11k_wow_enable(struct ath11k_base *ab);
int ath11k_wow_wakeup(struct ath11k_base *ab);

#else

static inline int ath11k_wow_init(struct ath11k *ar)
{
	return 0;
}

static inline int ath11k_wow_enable(struct ath11k_base *ab)
{
	return 0;
}

static inline int ath11k_wow_wakeup(struct ath11k_base *ab)
{
	return 0;
}

#endif /* CONFIG_PM */
#endif /* _WOW_H_ */
