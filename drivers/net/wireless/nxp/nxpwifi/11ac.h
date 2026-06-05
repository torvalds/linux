/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * nxpwifi: 802.11ac (VHT) definitions
 *
 * Copyright 2011-2024 NXP
 */

#ifndef _NXPWIFI_11AC_H_
#define _NXPWIFI_11AC_H_

#define VHT_CFG_2GHZ BIT(0)
#define VHT_CFG_5GHZ BIT(1)

enum vht_cfg_misc_config {
	VHT_CAP_TX_OPERATION = 1,
	VHT_CAP_ASSOCIATION,
	VHT_CAP_UAP_ONLY
};

#define DEFAULT_VHT_MCS_SET 0xfffe
#define DISABLE_VHT_MCS_SET 0xffff

#define VHT_BW_80_160_80P80 BIT(2)

int nxpwifi_cmd_append_11ac_tlv(struct nxpwifi_private *priv,
				struct nxpwifi_bssdescriptor *bss_desc,
				u8 **buffer);
int nxpwifi_cmd_11ac_cfg(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *cmd, u16 cmd_action,
			 struct nxpwifi_11ac_vht_cfg *cfg);
void nxpwifi_fill_vht_cap_tlv(struct nxpwifi_private *priv,
			      struct ieee80211_vht_cap *vht_cap, u16 bands);
#endif /* _NXPWIFI_11AC_H_ */
