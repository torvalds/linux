/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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

#include <net/mac80211.h>

/*---------------------  Export Definitions -------------------------*/
#define MAX_GROUP_KEY       4
#define MAX_KEY_TABLE       11
#define MAX_KEY_LEN         32
#define AES_KEY_LEN         16

#define AUTHENTICATOR_KEY   0x10000000
#define USE_KEYRSC          0x20000000
#define PAIRWISE_KEY        0x40000000
#define TRANSMIT_KEY        0x80000000

#define GROUP_KEY           0x00000000

#define KEY_CTL_WEP         0x00
#define KEY_CTL_NONE        0x01
#define KEY_CTL_TKIP        0x02
#define KEY_CTL_CCMP        0x03
#define KEY_CTL_INVALID     0xFF

#define VNT_KEY_DEFAULTKEY	0x1
#define VNT_KEY_GROUP_ADDRESS	0x2
#define VNT_KEY_ALLGROUP	0x4
#define VNT_KEY_GROUP		0x40
#define VNT_KEY_PAIRWISE	0x00
#define VNT_KEY_ONFLY		0x8000
#define VNT_KEY_ONFLY_ALL	0x4000

struct vnt_private;

int vnt_set_keys(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
		 struct ieee80211_vif *vif, struct ieee80211_key_conf *key);

#endif /* __KEY_H__ */
