// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * File: key.c
 *
 * Purpose: Implement functions for 802.11i Key management
 *
 * Author: Jerry Chen
 *
 * Date: May 29, 2003
 *
 * Functions:
 *
 * Revision History:
 *
 */

#include "mac.h"
#include "key.h"
#include "usbpipe.h"

int vnt_key_init_table(struct vnt_private *priv)
{
	u8 i;
	u8 data[MAX_KEY_TABLE];

	for (i = 0; i < MAX_KEY_TABLE; i++)
		data[i] = i;

	return vnt_control_out(priv, MESSAGE_TYPE_CLRKEYENTRY,
			0, 0, ARRAY_SIZE(data), data);
}

static int vnt_set_keymode(struct ieee80211_hw *hw, u8 *mac_addr,
			   struct ieee80211_key_conf *key, u32 key_type,
			   u32 mode)
{
	struct vnt_private *priv = hw->priv;
	u8 broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u16 key_mode = 0;
	u32 entry = 0;
	u8 *bssid;
	u8 key_inx = key->keyidx;
	u8 i;

	if (mac_addr)
		bssid = mac_addr;
	else
		bssid = &broadcast[0];

	if (key_type != VNT_KEY_DEFAULTKEY) {
		for (i = 0; i < (MAX_KEY_TABLE - 1); i++) {
			if (!test_bit(i, &priv->key_entry_inuse)) {
				set_bit(i, &priv->key_entry_inuse);

				key->hw_key_idx = i;
				entry = key->hw_key_idx;
				break;
			}
		}
	}

	switch (key_type) {
	case VNT_KEY_DEFAULTKEY:
		/* default key last entry */
		entry = MAX_KEY_TABLE - 1;
		key->hw_key_idx = entry;
		/* fall through */
	case VNT_KEY_GROUP_ADDRESS:
		key_mode = mode | (mode << 4);
		break;
	case VNT_KEY_GROUP:
		key_mode = mode << 4;
		break;
	case  VNT_KEY_PAIRWISE:
		key_mode |= mode;
		key_inx = 4;
		break;
	default:
		return -EINVAL;
	}

	key_mode |= key_type;

	if (mode == KEY_CTL_WEP) {
		if (key->keylen == WLAN_KEY_LEN_WEP40)
			key->key[15] &= 0x7f;
		if (key->keylen == WLAN_KEY_LEN_WEP104)
			key->key[15] |= 0x80;
	}

	return vnt_mac_set_keyentry(priv, key_mode, entry,
				    key_inx, bssid, key->key);
}

int vnt_set_keys(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
		 struct ieee80211_vif *vif, struct ieee80211_key_conf *key)
{
	struct vnt_private *priv = hw->priv;
	u8 *mac_addr = NULL;
	u8 key_dec_mode = 0;

	if (sta)
		mac_addr = &sta->addr[0];

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		vnt_set_keymode(hw, mac_addr, key, VNT_KEY_DEFAULTKEY,
				KEY_CTL_WEP);

		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;

		return vnt_set_keymode(hw, mac_addr, key, VNT_KEY_DEFAULTKEY,
				       KEY_CTL_WEP);

	case WLAN_CIPHER_SUITE_TKIP:
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;

		key_dec_mode = KEY_CTL_TKIP;

		break;
	case WLAN_CIPHER_SUITE_CCMP:
		if (priv->local_id <= MAC_REVISION_A1)
			return -EOPNOTSUPP;

		key_dec_mode = KEY_CTL_CCMP;

		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE)
		return vnt_set_keymode(hw, mac_addr, key, VNT_KEY_PAIRWISE,
				       key_dec_mode);

	return vnt_set_keymode(hw, mac_addr, key,
				VNT_KEY_GROUP_ADDRESS, key_dec_mode);
}
