/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef _RTW_HT_H_
#define _RTW_HT_H_

#include <linux/ieee80211.h>

struct ht_priv {
	u32	ht_option;
	u32	ampdu_enable;/* for enable Tx A-MPDU */
	u8	bwmode;/*  */
	u8	ch_offset;/* PRIME_CHNL_OFFSET */
	u8	sgi;/* short GI */

	/* for processing Tx A-MPDU */
	u8	agg_enable_bitmap;
	u8	candidate_tid_bitmap;

	struct ieee80211_ht_cap ht_cap;
};

#endif	/* _RTL871X_HT_H_ */
