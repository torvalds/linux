/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#ifndef __iwl_utils_h__
#define __iwl_utils_h__

#include <net/cfg80211.h>

#ifdef CONFIG_INET
/**
 * iwl_tx_tso_segment - Segments a TSO packet into subframes for A-MSDU.
 * @skb: buffer to segment.
 * @num_subframes: number of subframes to create.
 * @netdev_flags: netdev feature flags.
 * @mpdus_skbs: list to hold the segmented subframes.
 *
 * This function segments a large TCP packet into subframes.
 * subframes are added to the mpdus_skbs list
 *
 * Returns: 0 on success and negative value on failure.
 */
int iwl_tx_tso_segment(struct sk_buff *skb, unsigned int num_subframes,
		       netdev_features_t netdev_flags,
		       struct sk_buff_head *mpdus_skbs);
#else
static inline
int iwl_tx_tso_segment(struct sk_buff *skb, unsigned int num_subframes,
		       netdev_features_t netdev_flags,
		       struct sk_buff_head *mpdus_skbs)
{
	WARN_ON(1);

	return -1;
}
#endif /* CONFIG_INET */

static inline
u32 iwl_find_ie_offset(u8 *beacon, u8 eid, u32 frame_size)
{
	struct ieee80211_mgmt *mgmt = (void *)beacon;
	const u8 *ie;

	if (WARN_ON_ONCE(frame_size <= (mgmt->u.beacon.variable - beacon)))
		return 0;

	frame_size -= mgmt->u.beacon.variable - beacon;

	ie = cfg80211_find_ie(eid, mgmt->u.beacon.variable, frame_size);
	if (!ie)
		return 0;

	return ie - beacon;
}

s8 iwl_average_neg_dbm(const u8 *neg_dbm_values, u8 len);

#endif /* __iwl_utils_h__ */
