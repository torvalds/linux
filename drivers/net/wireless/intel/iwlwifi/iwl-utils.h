/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __iwl_utils_h__
#define __iwl_utils_h__

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

#endif /* __iwl_utils_h__ */
