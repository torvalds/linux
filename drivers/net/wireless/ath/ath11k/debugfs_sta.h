/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _ATH11K_DEBUGFS_STA_H_
#define _ATH11K_DEBUGFS_STA_H_

#include <net/mac80211.h>

#include "core.h"
#include "hal_tx.h"

#ifdef CONFIG_ATH11K_DEBUGFS

void ath11k_debugfs_sta_op_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta, struct dentry *dir);
void ath11k_debugfs_sta_add_tx_stats(struct ath11k_sta *arsta,
				     struct ath11k_per_peer_tx_stats *peer_stats,
				     u8 legacy_rate_idx);
void ath11k_debugfs_sta_update_txcompl(struct ath11k *ar,
				       struct hal_tx_status *ts);

#else /* CONFIG_ATH11K_DEBUGFS */

#define ath11k_debugfs_sta_op_add NULL

static inline void
ath11k_debugfs_sta_add_tx_stats(struct ath11k_sta *arsta,
				struct ath11k_per_peer_tx_stats *peer_stats,
				u8 legacy_rate_idx)
{
}

static inline void ath11k_debugfs_sta_update_txcompl(struct ath11k *ar,
						     struct hal_tx_status *ts)
{
}

#endif /* CONFIG_ATH11K_DEBUGFS */

#endif /* _ATH11K_DEBUGFS_STA_H_ */
