/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NXP Wireless LAN device driver: 802.11ac
 *
 * Copyright 2011-2020 NXP
 */

#ifndef _MWIFIEX_11AC_H_
#define _MWIFIEX_11AC_H_

#define VHT_CFG_2GHZ BIT(0)
#define VHT_CFG_5GHZ BIT(1)

enum vht_cfg_misc_config {
	VHT_CAP_TX_OPERATION = 1,
	VHT_CAP_ASSOCIATION,
	VHT_CAP_UAP_ONLY
};

#define DEFAULT_VHT_MCS_SET 0xfffa
#define DISABLE_VHT_MCS_SET 0xffff

#define VHT_BW_80_160_80P80 BIT(2)

int mwifiex_cmd_append_11ac_tlv(struct mwifiex_private *priv,
				struct mwifiex_bssdescriptor *bss_desc,
				u8 **buffer);
int mwifiex_cmd_11ac_cfg(struct mwifiex_private *priv,
			 struct host_cmd_ds_command *cmd, u16 cmd_action,
			 struct mwifiex_11ac_vht_cfg *cfg);
void mwifiex_fill_vht_cap_tlv(struct mwifiex_private *priv,
			      struct ieee80211_vht_cap *vht_cap, u8 bands);
#endif /* _MWIFIEX_11AC_H_ */
