/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __iwl_mld_d3_h__
#define __iwl_mld_d3_h__

#include "fw/api/d3.h"

struct iwl_mld_rekey_data {
	bool valid;
	u8 kck[NL80211_KCK_EXT_LEN];
	u8 kek[NL80211_KEK_EXT_LEN];
	size_t kck_len;
	size_t kek_len;
	__le64 replay_ctr;
	u32 akm;
};

/**
 * struct iwl_mld_wowlan_data - data used by the wowlan suspend flow
 *
 * @target_ipv6_addrs: IPv6 addresses on this interface for offload
 * @tentative_addrs: bitmap of tentative IPv6 addresses in @target_ipv6_addrs
 * @num_target_ipv6_addrs: number of @target_ipv6_addrs
 * @rekey_data: security key data used for rekeying during D3
 */
struct iwl_mld_wowlan_data {
#if IS_ENABLED(CONFIG_IPV6)
	struct in6_addr target_ipv6_addrs[IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_MAX];
	unsigned long tentative_addrs[BITS_TO_LONGS(IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_MAX)];
	int num_target_ipv6_addrs;
#endif
	struct iwl_mld_rekey_data rekey_data;
};

int iwl_mld_no_wowlan_resume(struct iwl_mld *mld);
int iwl_mld_no_wowlan_suspend(struct iwl_mld *mld);
int iwl_mld_wowlan_suspend(struct iwl_mld *mld,
			   struct cfg80211_wowlan *wowlan);
int iwl_mld_wowlan_resume(struct iwl_mld *mld);
void iwl_mld_set_rekey_data(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct cfg80211_gtk_rekey_data *data);
#if IS_ENABLED(CONFIG_IPV6)
void iwl_mld_ipv6_addr_change(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct inet6_dev *idev);
#endif

#endif /* __iwl_mld_d3_h__ */
