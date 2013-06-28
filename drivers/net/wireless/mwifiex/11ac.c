/*
 * Marvell Wireless LAN device driver: 802.11ac
 *
 * Copyright (C) 2013, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
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

#include "decl.h"
#include "ioctl.h"
#include "fw.h"
#include "main.h"
#include "11ac.h"

/* This function converts the 2-bit MCS map to the highest long GI
 * VHT data rate.
 */
static u16
mwifiex_convert_mcsmap_to_maxrate(struct mwifiex_private *priv,
				  u8 bands, u16 mcs_map)
{
	u8 i, nss, max_mcs;
	u16 max_rate = 0;
	u32 usr_vht_cap_info = 0;
	struct mwifiex_adapter *adapter = priv->adapter;
	/* tables of the MCS map to the highest data rate (in Mbps)
	 * supported for long GI
	 */
	u16 max_rate_lgi_80MHZ[8][3] = {
		{0x124, 0x15F, 0x186},	/* NSS = 1 */
		{0x249, 0x2BE, 0x30C},  /* NSS = 2 */
		{0x36D, 0x41D, 0x492},  /* NSS = 3 */
		{0x492, 0x57C, 0x618},  /* NSS = 4 */
		{0x5B6, 0x6DB, 0x79E},  /* NSS = 5 */
		{0x6DB, 0x83A, 0x0},    /* NSS = 6 */
		{0x7FF, 0x999, 0xAAA},  /* NSS = 7 */
		{0x924, 0xAF8, 0xC30}   /* NSS = 8 */
	};
	u16 max_rate_lgi_160MHZ[8][3] = {
		{0x249, 0x2BE, 0x30C},   /* NSS = 1 */
		{0x492, 0x57C, 0x618},   /* NSS = 2 */
		{0x6DB, 0x83A, 0x0},     /* NSS = 3 */
		{0x924, 0xAF8, 0xC30},   /* NSS = 4 */
		{0xB6D, 0xDB6, 0xF3C},   /* NSS = 5 */
		{0xDB6, 0x1074, 0x1248}, /* NSS = 6 */
		{0xFFF, 0x1332, 0x1554}, /* NSS = 7 */
		{0x1248, 0x15F0, 0x1860} /* NSS = 8 */
	};

	if (bands & BAND_AAC)
		usr_vht_cap_info = adapter->usr_dot_11ac_dev_cap_a;
	else
		usr_vht_cap_info = adapter->usr_dot_11ac_dev_cap_bg;

	/* find the max NSS supported */
	nss = 0;
	for (i = 0; i < 8; i++) {
		max_mcs = (mcs_map >> (2 * i)) & 0x3;
		if (max_mcs < 3)
			nss = i;
	}
	max_mcs = (mcs_map >> (2 * nss)) & 0x3;

	/* if max_mcs is 3, nss must be 0 (SS = 1). Thus, max mcs is MCS 9 */
	if (max_mcs >= 3)
		max_mcs = 2;

	if (GET_VHTCAP_CHWDSET(usr_vht_cap_info)) {
		/* support 160 MHz */
		max_rate = max_rate_lgi_160MHZ[nss][max_mcs];
		if (!max_rate)
			/* MCS9 is not supported in NSS6 */
			max_rate = max_rate_lgi_160MHZ[nss][max_mcs - 1];
	} else {
		max_rate = max_rate_lgi_80MHZ[nss][max_mcs];
		if (!max_rate)
			/* MCS9 is not supported in NSS3 */
			max_rate = max_rate_lgi_80MHZ[nss][max_mcs - 1];
	}

	return max_rate;
}

static void
mwifiex_fill_vht_cap_info(struct mwifiex_private *priv,
			  struct mwifiex_ie_types_vhtcap *vht_cap, u8 bands)
{
	struct mwifiex_adapter *adapter = priv->adapter;

	if (bands & BAND_A)
		vht_cap->vht_cap.vht_cap_info =
				cpu_to_le32(adapter->usr_dot_11ac_dev_cap_a);
	else
		vht_cap->vht_cap.vht_cap_info =
				cpu_to_le32(adapter->usr_dot_11ac_dev_cap_bg);
}

static void
mwifiex_fill_vht_cap_tlv(struct mwifiex_private *priv,
			 struct mwifiex_ie_types_vhtcap *vht_cap, u8 bands)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	u16 mcs_map_user, mcs_map_resp, mcs_map_result;
	u16 mcs_user, mcs_resp, nss, tmp;

	/* Fill VHT cap info */
	mwifiex_fill_vht_cap_info(priv, vht_cap, bands);

	/* rx MCS Set: find the minimum of the user rx mcs and ap rx mcs */
	mcs_map_user = GET_DEVRXMCSMAP(adapter->usr_dot_11ac_mcs_support);
	mcs_map_resp = le16_to_cpu(vht_cap->vht_cap.supp_mcs.rx_mcs_map);
	mcs_map_result = 0;

	for (nss = 1; nss <= 8; nss++) {
		mcs_user = GET_VHTNSSMCS(mcs_map_user, nss);
		mcs_resp = GET_VHTNSSMCS(mcs_map_resp, nss);

		if ((mcs_user == NO_NSS_SUPPORT) ||
		    (mcs_resp == NO_NSS_SUPPORT))
			SET_VHTNSSMCS(mcs_map_result, nss, NO_NSS_SUPPORT);
		else
			SET_VHTNSSMCS(mcs_map_result, nss,
				      min(mcs_user, mcs_resp));
	}

	vht_cap->vht_cap.supp_mcs.rx_mcs_map = cpu_to_le16(mcs_map_result);

	tmp = mwifiex_convert_mcsmap_to_maxrate(priv, bands, mcs_map_result);
	vht_cap->vht_cap.supp_mcs.rx_highest = cpu_to_le16(tmp);

	/* tx MCS Set: find the minimum of the user tx mcs and ap tx mcs */
	mcs_map_user = GET_DEVTXMCSMAP(adapter->usr_dot_11ac_mcs_support);
	mcs_map_resp = le16_to_cpu(vht_cap->vht_cap.supp_mcs.tx_mcs_map);
	mcs_map_result = 0;

	for (nss = 1; nss <= 8; nss++) {
		mcs_user = GET_VHTNSSMCS(mcs_map_user, nss);
		mcs_resp = GET_VHTNSSMCS(mcs_map_resp, nss);
		if ((mcs_user == NO_NSS_SUPPORT) ||
		    (mcs_resp == NO_NSS_SUPPORT))
			SET_VHTNSSMCS(mcs_map_result, nss, NO_NSS_SUPPORT);
		else
			SET_VHTNSSMCS(mcs_map_result, nss,
				      min(mcs_user, mcs_resp));
	}

	vht_cap->vht_cap.supp_mcs.tx_mcs_map = cpu_to_le16(mcs_map_result);

	tmp = mwifiex_convert_mcsmap_to_maxrate(priv, bands, mcs_map_result);
	vht_cap->vht_cap.supp_mcs.tx_highest = cpu_to_le16(tmp);

	return;
}

int mwifiex_cmd_append_11ac_tlv(struct mwifiex_private *priv,
			     struct mwifiex_bssdescriptor *bss_desc,
			     u8 **buffer)
{
	struct mwifiex_ie_types_vhtcap *vht_cap;
	struct mwifiex_ie_types_oper_mode_ntf *oper_ntf;
	struct ieee_types_oper_mode_ntf *ieee_oper_ntf;
	struct mwifiex_ie_types_vht_oper *vht_op;
	struct mwifiex_adapter *adapter = priv->adapter;
	u8 supp_chwd_set;
	u32 usr_vht_cap_info;
	int ret_len = 0;

	if (bss_desc->bss_band & BAND_A)
		usr_vht_cap_info = adapter->usr_dot_11ac_dev_cap_a;
	else
		usr_vht_cap_info = adapter->usr_dot_11ac_dev_cap_bg;

	/* VHT Capabilities IE */
	if (bss_desc->bcn_vht_cap) {
		vht_cap = (struct mwifiex_ie_types_vhtcap *)*buffer;
		memset(vht_cap, 0, sizeof(*vht_cap));
		vht_cap->header.type = cpu_to_le16(WLAN_EID_VHT_CAPABILITY);
		vht_cap->header.len  =
				cpu_to_le16(sizeof(struct ieee80211_vht_cap));
		memcpy((u8 *)vht_cap + sizeof(struct mwifiex_ie_types_header),
		       (u8 *)bss_desc->bcn_vht_cap +
		       sizeof(struct ieee_types_header),
		       le16_to_cpu(vht_cap->header.len));

		mwifiex_fill_vht_cap_tlv(priv, vht_cap, bss_desc->bss_band);
		*buffer += sizeof(*vht_cap);
		ret_len += sizeof(*vht_cap);
	}

	/* VHT Operation IE */
	if (bss_desc->bcn_vht_oper) {
		if (priv->bss_mode == NL80211_IFTYPE_STATION) {
			vht_op = (struct mwifiex_ie_types_vht_oper *)*buffer;
			memset(vht_op, 0, sizeof(*vht_op));
			vht_op->header.type =
					cpu_to_le16(WLAN_EID_VHT_OPERATION);
			vht_op->header.len  = cpu_to_le16(sizeof(*vht_op) -
				      sizeof(struct mwifiex_ie_types_header));
			memcpy((u8 *)vht_op +
				sizeof(struct mwifiex_ie_types_header),
			       (u8 *)bss_desc->bcn_vht_oper +
			       sizeof(struct ieee_types_header),
			       le16_to_cpu(vht_op->header.len));

			/* negotiate the channel width and central freq
			 * and keep the central freq as the peer suggests
			 */
			supp_chwd_set = GET_VHTCAP_CHWDSET(usr_vht_cap_info);

			switch (supp_chwd_set) {
			case 0:
				vht_op->chan_width =
				     min_t(u8, IEEE80211_VHT_CHANWIDTH_80MHZ,
					   bss_desc->bcn_vht_oper->chan_width);
				break;
			case 1:
				vht_op->chan_width =
				     min_t(u8, IEEE80211_VHT_CHANWIDTH_160MHZ,
					   bss_desc->bcn_vht_oper->chan_width);
				break;
			case 2:
				vht_op->chan_width =
				     min_t(u8, IEEE80211_VHT_CHANWIDTH_80P80MHZ,
					   bss_desc->bcn_vht_oper->chan_width);
				break;
			default:
				vht_op->chan_width =
				     IEEE80211_VHT_CHANWIDTH_USE_HT;
				break;
			}

			*buffer += sizeof(*vht_op);
			ret_len += sizeof(*vht_op);
		}
	}

	/* Operating Mode Notification IE */
	if (bss_desc->oper_mode) {
		ieee_oper_ntf = bss_desc->oper_mode;
		oper_ntf = (void *)*buffer;
		memset(oper_ntf, 0, sizeof(*oper_ntf));
		oper_ntf->header.type = cpu_to_le16(WLAN_EID_OPMODE_NOTIF);
		oper_ntf->header.len = cpu_to_le16(sizeof(u8));
		oper_ntf->oper_mode = ieee_oper_ntf->oper_mode;
		*buffer += sizeof(*oper_ntf);
		ret_len += sizeof(*oper_ntf);
	}

	return ret_len;
}

int mwifiex_cmd_11ac_cfg(struct mwifiex_private *priv,
			 struct host_cmd_ds_command *cmd, u16 cmd_action,
			 struct mwifiex_11ac_vht_cfg *cfg)
{
	struct host_cmd_11ac_vht_cfg *vhtcfg = &cmd->params.vht_cfg;

	cmd->command = cpu_to_le16(HostCmd_CMD_11AC_CFG);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_11ac_vht_cfg) +
				S_DS_GEN);
	vhtcfg->action = cpu_to_le16(cmd_action);
	vhtcfg->band_config = cfg->band_config;
	vhtcfg->misc_config = cfg->misc_config;
	vhtcfg->cap_info = cpu_to_le32(cfg->cap_info);
	vhtcfg->mcs_tx_set = cpu_to_le32(cfg->mcs_tx_set);
	vhtcfg->mcs_rx_set = cpu_to_le32(cfg->mcs_rx_set);

	return 0;
}

/* This function initializes the BlockACK setup information for given
 * mwifiex_private structure for 11ac enabled networks.
 */
void mwifiex_set_11ac_ba_params(struct mwifiex_private *priv)
{
	priv->add_ba_param.timeout = MWIFIEX_DEFAULT_BLOCK_ACK_TIMEOUT;

	if (GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_UAP) {
		priv->add_ba_param.tx_win_size =
					   MWIFIEX_11AC_UAP_AMPDU_DEF_TXWINSIZE;
		priv->add_ba_param.rx_win_size =
					   MWIFIEX_11AC_UAP_AMPDU_DEF_RXWINSIZE;
	} else {
		priv->add_ba_param.tx_win_size =
					   MWIFIEX_11AC_STA_AMPDU_DEF_TXWINSIZE;
		priv->add_ba_param.rx_win_size =
					   MWIFIEX_11AC_STA_AMPDU_DEF_RXWINSIZE;
	}

	return;
}
