/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _ATH12K_DEBUGFS_STA_H_
#define _ATH12K_DEBUGFS_STA_H_

#include <net/mac80211.h>

#include "core.h"

#define ATH12K_STA_RX_STATS_BUF_SIZE		(1024 * 16)

#ifdef CONFIG_ATH12K_DEBUGFS

void ath12k_debugfs_link_sta_op_add(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_link_sta *link_sta,
				    struct dentry *dir);

#endif /* CONFIG_ATH12K_DEBUGFS */

#endif /* _ATH12K_DEBUGFS_STA_H_ */
