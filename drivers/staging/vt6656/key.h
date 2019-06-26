/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * File: key.h
 *
 * Purpose: Implement functions for 802.11i Key management
 *
 * Author: Jerry Chen
 *
 * Date: May 29, 2003
 *
 */

#ifndef __KEY_H__
#define __KEY_H__

#include "device.h"

#define MAX_KEY_TABLE       11

#define KEY_CTL_WEP         0x00
#define KEY_CTL_NONE        0x01
#define KEY_CTL_TKIP        0x02
#define KEY_CTL_CCMP        0x03

#define VNT_KEY_DEFAULTKEY	0x1
#define VNT_KEY_GROUP_ADDRESS	0x2
#define VNT_KEY_ALLGROUP	0x4
#define VNT_KEY_GROUP		0x40
#define VNT_KEY_PAIRWISE	0x00
#define VNT_KEY_ONFLY		0x8000
#define VNT_KEY_ONFLY_ALL	0x4000

int vnt_key_init_table(struct vnt_private *priv);

int vnt_set_keys(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
		 struct ieee80211_vif *vif, struct ieee80211_key_conf *key);

#endif /* __KEY_H__ */
