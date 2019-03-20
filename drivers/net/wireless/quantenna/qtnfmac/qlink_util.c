// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2015-2016 Quantenna Communications. All rights reserved. */

#include <linux/nl80211.h>

#include "qlink_util.h"

u16 qlink_iface_type_to_nl_mask(u16 qlink_type)
{
	u16 result = 0;

	switch (qlink_type) {
	case QLINK_IFTYPE_AP:
		result |= BIT(NL80211_IFTYPE_AP);
		break;
	case QLINK_IFTYPE_STATION:
		result |= BIT(NL80211_IFTYPE_STATION);
		break;
	case QLINK_IFTYPE_ADHOC:
		result |= BIT(NL80211_IFTYPE_ADHOC);
		break;
	case QLINK_IFTYPE_MONITOR:
		result |= BIT(NL80211_IFTYPE_MONITOR);
		break;
	case QLINK_IFTYPE_WDS:
		result |= BIT(NL80211_IFTYPE_WDS);
		break;
	case QLINK_IFTYPE_AP_VLAN:
		result |= BIT(NL80211_IFTYPE_AP_VLAN);
		break;
	}

	return result;
}

u8 qlink_chan_width_mask_to_nl(u16 qlink_mask)
{
	u8 result = 0;

	if (qlink_mask & BIT(QLINK_CHAN_WIDTH_5))
		result |= BIT(NL80211_CHAN_WIDTH_5);

	if (qlink_mask & BIT(QLINK_CHAN_WIDTH_10))
		result |= BIT(NL80211_CHAN_WIDTH_10);

	if (qlink_mask & BIT(QLINK_CHAN_WIDTH_20_NOHT))
		result |= BIT(NL80211_CHAN_WIDTH_20_NOHT);

	if (qlink_mask & BIT(QLINK_CHAN_WIDTH_20))
		result |= BIT(NL80211_CHAN_WIDTH_20);

	if (qlink_mask & BIT(QLINK_CHAN_WIDTH_40))
		result |= BIT(NL80211_CHAN_WIDTH_40);

	if (qlink_mask & BIT(QLINK_CHAN_WIDTH_80))
		result |= BIT(NL80211_CHAN_WIDTH_80);

	if (qlink_mask & BIT(QLINK_CHAN_WIDTH_80P80))
		result |= BIT(NL80211_CHAN_WIDTH_80P80);

	if (qlink_mask & BIT(QLINK_CHAN_WIDTH_160))
		result |= BIT(NL80211_CHAN_WIDTH_160);

	return result;
}

static enum nl80211_chan_width qlink_chanwidth_to_nl(u8 qlw)
{
	switch (qlw) {
	case QLINK_CHAN_WIDTH_20_NOHT:
		return NL80211_CHAN_WIDTH_20_NOHT;
	case QLINK_CHAN_WIDTH_20:
		return NL80211_CHAN_WIDTH_20;
	case QLINK_CHAN_WIDTH_40:
		return NL80211_CHAN_WIDTH_40;
	case QLINK_CHAN_WIDTH_80:
		return NL80211_CHAN_WIDTH_80;
	case QLINK_CHAN_WIDTH_80P80:
		return NL80211_CHAN_WIDTH_80P80;
	case QLINK_CHAN_WIDTH_160:
		return NL80211_CHAN_WIDTH_160;
	case QLINK_CHAN_WIDTH_5:
		return NL80211_CHAN_WIDTH_5;
	case QLINK_CHAN_WIDTH_10:
		return NL80211_CHAN_WIDTH_10;
	default:
		return -1;
	}
}

static u8 qlink_chanwidth_nl_to_qlink(enum nl80211_chan_width nlwidth)
{
	switch (nlwidth) {
	case NL80211_CHAN_WIDTH_20_NOHT:
		return QLINK_CHAN_WIDTH_20_NOHT;
	case NL80211_CHAN_WIDTH_20:
		return QLINK_CHAN_WIDTH_20;
	case NL80211_CHAN_WIDTH_40:
		return QLINK_CHAN_WIDTH_40;
	case NL80211_CHAN_WIDTH_80:
		return QLINK_CHAN_WIDTH_80;
	case NL80211_CHAN_WIDTH_80P80:
		return QLINK_CHAN_WIDTH_80P80;
	case NL80211_CHAN_WIDTH_160:
		return QLINK_CHAN_WIDTH_160;
	case NL80211_CHAN_WIDTH_5:
		return QLINK_CHAN_WIDTH_5;
	case NL80211_CHAN_WIDTH_10:
		return QLINK_CHAN_WIDTH_10;
	default:
		return -1;
	}
}

void qlink_chandef_q2cfg(struct wiphy *wiphy,
			 const struct qlink_chandef *qch,
			 struct cfg80211_chan_def *chdef)
{
	struct ieee80211_channel *chan;

	chan = ieee80211_get_channel(wiphy, le16_to_cpu(qch->chan.center_freq));

	chdef->chan = chan;
	chdef->center_freq1 = le16_to_cpu(qch->center_freq1);
	chdef->center_freq2 = le16_to_cpu(qch->center_freq2);
	chdef->width = qlink_chanwidth_to_nl(qch->width);
}

void qlink_chandef_cfg2q(const struct cfg80211_chan_def *chdef,
			 struct qlink_chandef *qch)
{
	struct ieee80211_channel *chan = chdef->chan;

	qch->chan.hw_value = cpu_to_le16(chan->hw_value);
	qch->chan.center_freq = cpu_to_le16(chan->center_freq);
	qch->chan.flags = cpu_to_le32(chan->flags);

	qch->center_freq1 = cpu_to_le16(chdef->center_freq1);
	qch->center_freq2 = cpu_to_le16(chdef->center_freq2);
	qch->width = qlink_chanwidth_nl_to_qlink(chdef->width);
}

enum qlink_hidden_ssid qlink_hidden_ssid_nl2q(enum nl80211_hidden_ssid nl_val)
{
	switch (nl_val) {
	case NL80211_HIDDEN_SSID_ZERO_LEN:
		return QLINK_HIDDEN_SSID_ZERO_LEN;
	case NL80211_HIDDEN_SSID_ZERO_CONTENTS:
		return QLINK_HIDDEN_SSID_ZERO_CONTENTS;
	case NL80211_HIDDEN_SSID_NOT_IN_USE:
	default:
		return QLINK_HIDDEN_SSID_NOT_IN_USE;
	}
}

bool qtnf_utils_is_bit_set(const u8 *arr, unsigned int bit,
			   unsigned int arr_max_len)
{
	unsigned int idx = bit / BITS_PER_BYTE;
	u8 mask = 1 << (bit - (idx * BITS_PER_BYTE));

	if (idx >= arr_max_len)
		return false;

	return arr[idx] & mask;
}

void qlink_acl_data_cfg2q(const struct cfg80211_acl_data *acl,
			  struct qlink_acl_data *qacl)
{
	switch (acl->acl_policy) {
	case NL80211_ACL_POLICY_ACCEPT_UNLESS_LISTED:
		qacl->policy =
			cpu_to_le32(QLINK_ACL_POLICY_ACCEPT_UNLESS_LISTED);
		break;
	case NL80211_ACL_POLICY_DENY_UNLESS_LISTED:
		qacl->policy = cpu_to_le32(QLINK_ACL_POLICY_DENY_UNLESS_LISTED);
		break;
	}

	qacl->num_entries = cpu_to_le32(acl->n_acl_entries);
	memcpy(qacl->mac_addrs, acl->mac_addrs,
	       acl->n_acl_entries * sizeof(*qacl->mac_addrs));
}

enum qlink_band qlink_utils_band_cfg2q(enum nl80211_band band)
{
	switch (band) {
	case NL80211_BAND_2GHZ:
		return QLINK_BAND_2GHZ;
	case NL80211_BAND_5GHZ:
		return QLINK_BAND_5GHZ;
	case NL80211_BAND_60GHZ:
		return QLINK_BAND_60GHZ;
	default:
		return -EINVAL;
	}
}

enum qlink_dfs_state qlink_utils_dfs_state_cfg2q(enum nl80211_dfs_state state)
{
	switch (state) {
	case NL80211_DFS_USABLE:
		return QLINK_DFS_USABLE;
	case NL80211_DFS_AVAILABLE:
		return QLINK_DFS_AVAILABLE;
	case NL80211_DFS_UNAVAILABLE:
	default:
		return QLINK_DFS_UNAVAILABLE;
	}
}

u32 qlink_utils_chflags_cfg2q(u32 cfgflags)
{
	u32 flags = 0;

	if (cfgflags & IEEE80211_CHAN_DISABLED)
		flags |= QLINK_CHAN_DISABLED;

	if (cfgflags & IEEE80211_CHAN_NO_IR)
		flags |= QLINK_CHAN_NO_IR;

	if (cfgflags & IEEE80211_CHAN_RADAR)
		flags |= QLINK_CHAN_RADAR;

	if (cfgflags & IEEE80211_CHAN_NO_HT40PLUS)
		flags |= QLINK_CHAN_NO_HT40PLUS;

	if (cfgflags & IEEE80211_CHAN_NO_HT40MINUS)
		flags |= QLINK_CHAN_NO_HT40MINUS;

	if (cfgflags & IEEE80211_CHAN_NO_80MHZ)
		flags |= QLINK_CHAN_NO_80MHZ;

	if (cfgflags & IEEE80211_CHAN_NO_160MHZ)
		flags |= QLINK_CHAN_NO_160MHZ;

	return flags;
}
