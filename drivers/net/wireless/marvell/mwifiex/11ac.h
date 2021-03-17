/*
 * NXP Wireless LAN device driver: 802.11ac
 *
 * Copyright 2011-2020 NXP
 *
 * This software file (the "File") is distributed by NXP
 * under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
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
