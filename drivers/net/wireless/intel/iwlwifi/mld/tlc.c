// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */

#include <net/mac80211.h>

#include "tlc.h"
#include "hcmd.h"
#include "sta.h"

#include "fw/api/rs.h"
#include "fw/api/context.h"
#include "fw/api/dhc.h"

static u8 iwl_mld_fw_bw_from_sta_bw(const struct ieee80211_link_sta *link_sta)
{
	switch (link_sta->bandwidth) {
	case IEEE80211_STA_RX_BW_320:
		return IWL_TLC_MNG_CH_WIDTH_320MHZ;
	case IEEE80211_STA_RX_BW_160:
		return IWL_TLC_MNG_CH_WIDTH_160MHZ;
	case IEEE80211_STA_RX_BW_80:
		return IWL_TLC_MNG_CH_WIDTH_80MHZ;
	case IEEE80211_STA_RX_BW_40:
		return IWL_TLC_MNG_CH_WIDTH_40MHZ;
	case IEEE80211_STA_RX_BW_20:
	default:
		return IWL_TLC_MNG_CH_WIDTH_20MHZ;
	}
}

static __le16
iwl_mld_get_tlc_cmd_flags(struct iwl_mld *mld,
			  struct ieee80211_vif *vif,
			  struct ieee80211_link_sta *link_sta,
			  const struct ieee80211_sta_he_cap *own_he_cap,
			  const struct ieee80211_sta_eht_cap *own_eht_cap)
{
	struct ieee80211_sta_ht_cap *ht_cap = &link_sta->ht_cap;
	struct ieee80211_sta_vht_cap *vht_cap = &link_sta->vht_cap;
	struct ieee80211_sta_he_cap *he_cap = &link_sta->he_cap;
	bool has_vht = vht_cap->vht_supported;
	u16 flags = 0;

	/* STBC flags */
	if (mld->cfg->ht_params->stbc &&
	    (hweight8(iwl_mld_get_valid_tx_ant(mld)) > 1)) {
		if (he_cap->has_he && he_cap->he_cap_elem.phy_cap_info[2] &
				      IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ)
			flags |= IWL_TLC_MNG_CFG_FLAGS_STBC_MSK;
		else if (vht_cap->cap & IEEE80211_VHT_CAP_RXSTBC_MASK)
			flags |= IWL_TLC_MNG_CFG_FLAGS_STBC_MSK;
		else if (ht_cap->cap & IEEE80211_HT_CAP_RX_STBC)
			flags |= IWL_TLC_MNG_CFG_FLAGS_STBC_MSK;
	}

	/* LDPC */
	if (mld->cfg->ht_params->ldpc &&
	    ((ht_cap->cap & IEEE80211_HT_CAP_LDPC_CODING) ||
	     (has_vht && (vht_cap->cap & IEEE80211_VHT_CAP_RXLDPC))))
		flags |= IWL_TLC_MNG_CFG_FLAGS_LDPC_MSK;

	if (he_cap->has_he && (he_cap->he_cap_elem.phy_cap_info[1] &
	    IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD))
		flags |= IWL_TLC_MNG_CFG_FLAGS_LDPC_MSK;

	if (own_he_cap &&
	    !(own_he_cap->he_cap_elem.phy_cap_info[1] &
			IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD))
		flags &= ~IWL_TLC_MNG_CFG_FLAGS_LDPC_MSK;

	/* DCM */
	if (he_cap->has_he &&
	    (he_cap->he_cap_elem.phy_cap_info[3] &
	     IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_MASK &&
	     own_he_cap &&
	     own_he_cap->he_cap_elem.phy_cap_info[3] &
			IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_MASK))
		flags |= IWL_TLC_MNG_CFG_FLAGS_HE_DCM_NSS_1_MSK;

	/* Extra EHT LTF */
	if (own_eht_cap &&
	    own_eht_cap->eht_cap_elem.phy_cap_info[5] &
		IEEE80211_EHT_PHY_CAP5_SUPP_EXTRA_EHT_LTF &&
	    link_sta->eht_cap.has_eht &&
	    link_sta->eht_cap.eht_cap_elem.phy_cap_info[5] &
	    IEEE80211_EHT_PHY_CAP5_SUPP_EXTRA_EHT_LTF) {
		flags |= IWL_TLC_MNG_CFG_FLAGS_EHT_EXTRA_LTF_MSK;
	}

	return cpu_to_le16(flags);
}

static u8 iwl_mld_get_fw_chains(struct iwl_mld *mld)
{
	u8 chains = iwl_mld_get_valid_tx_ant(mld);
	u8 fw_chains = 0;

	if (chains & ANT_A)
		fw_chains |= IWL_TLC_MNG_CHAIN_A_MSK;
	if (chains & ANT_B)
		fw_chains |= IWL_TLC_MNG_CHAIN_B_MSK;

	return fw_chains;
}

static u8 iwl_mld_get_fw_sgi(struct ieee80211_link_sta *link_sta)
{
	struct ieee80211_sta_ht_cap *ht_cap = &link_sta->ht_cap;
	struct ieee80211_sta_vht_cap *vht_cap = &link_sta->vht_cap;
	struct ieee80211_sta_he_cap *he_cap = &link_sta->he_cap;
	u8 sgi_chwidths = 0;

	/* If the association supports HE, HT/VHT rates will never be used for
	 * Tx and therefor there's no need to set the
	 * sgi-per-channel-width-support bits
	 */
	if (he_cap->has_he)
		return 0;

	if (ht_cap->cap & IEEE80211_HT_CAP_SGI_20)
		sgi_chwidths |= BIT(IWL_TLC_MNG_CH_WIDTH_20MHZ);
	if (ht_cap->cap & IEEE80211_HT_CAP_SGI_40)
		sgi_chwidths |= BIT(IWL_TLC_MNG_CH_WIDTH_40MHZ);
	if (vht_cap->cap & IEEE80211_VHT_CAP_SHORT_GI_80)
		sgi_chwidths |= BIT(IWL_TLC_MNG_CH_WIDTH_80MHZ);
	if (vht_cap->cap & IEEE80211_VHT_CAP_SHORT_GI_160)
		sgi_chwidths |= BIT(IWL_TLC_MNG_CH_WIDTH_160MHZ);

	return sgi_chwidths;
}

static int
iwl_mld_get_highest_fw_mcs(const struct ieee80211_sta_vht_cap *vht_cap,
			   int nss)
{
	u16 rx_mcs = le16_to_cpu(vht_cap->vht_mcs.rx_mcs_map) &
		(0x3 << (2 * (nss - 1)));
	rx_mcs >>= (2 * (nss - 1));

	switch (rx_mcs) {
	case IEEE80211_VHT_MCS_SUPPORT_0_7:
		return IWL_TLC_MNG_HT_RATE_MCS7;
	case IEEE80211_VHT_MCS_SUPPORT_0_8:
		return IWL_TLC_MNG_HT_RATE_MCS8;
	case IEEE80211_VHT_MCS_SUPPORT_0_9:
		return IWL_TLC_MNG_HT_RATE_MCS9;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	return 0;
}

static void
iwl_mld_fill_vht_rates(const struct ieee80211_link_sta *link_sta,
		       const struct ieee80211_sta_vht_cap *vht_cap,
		       struct iwl_tlc_config_cmd_v4 *cmd)
{
	u16 supp;
	int i, highest_mcs;
	u8 max_nss = link_sta->rx_nss;
	struct ieee80211_vht_cap ieee_vht_cap = {
		.vht_cap_info = cpu_to_le32(vht_cap->cap),
		.supp_mcs = vht_cap->vht_mcs,
	};

	/* the station support only a single receive chain */
	if (link_sta->smps_mode == IEEE80211_SMPS_STATIC)
		max_nss = 1;

	for (i = 0; i < max_nss && i < IWL_TLC_NSS_MAX; i++) {
		int nss = i + 1;

		highest_mcs = iwl_mld_get_highest_fw_mcs(vht_cap, nss);
		if (!highest_mcs)
			continue;

		supp = BIT(highest_mcs + 1) - 1;
		if (link_sta->bandwidth == IEEE80211_STA_RX_BW_20)
			supp &= ~BIT(IWL_TLC_MNG_HT_RATE_MCS9);

		cmd->ht_rates[i][IWL_TLC_MCS_PER_BW_80] = cpu_to_le16(supp);
		/* Check if VHT extended NSS indicates that the bandwidth/NSS
		 * configuration is supported - only for MCS 0 since we already
		 * decoded the MCS bits anyway ourselves.
		 */
		if (link_sta->bandwidth == IEEE80211_STA_RX_BW_160 &&
		    ieee80211_get_vht_max_nss(&ieee_vht_cap,
					      IEEE80211_VHT_CHANWIDTH_160MHZ,
					      0, true, nss) >= nss)
			cmd->ht_rates[i][IWL_TLC_MCS_PER_BW_160] =
				cmd->ht_rates[i][IWL_TLC_MCS_PER_BW_80];
	}
}

static u16 iwl_mld_he_mac80211_mcs_to_fw_mcs(u16 mcs)
{
	switch (mcs) {
	case IEEE80211_HE_MCS_SUPPORT_0_7:
		return BIT(IWL_TLC_MNG_HT_RATE_MCS7 + 1) - 1;
	case IEEE80211_HE_MCS_SUPPORT_0_9:
		return BIT(IWL_TLC_MNG_HT_RATE_MCS9 + 1) - 1;
	case IEEE80211_HE_MCS_SUPPORT_0_11:
		return BIT(IWL_TLC_MNG_HT_RATE_MCS11 + 1) - 1;
	case IEEE80211_HE_MCS_NOT_SUPPORTED:
		return 0;
	}

	WARN(1, "invalid HE MCS %d\n", mcs);
	return 0;
}

static void
iwl_mld_fill_he_rates(const struct ieee80211_link_sta *link_sta,
		      const struct ieee80211_sta_he_cap *own_he_cap,
		      struct iwl_tlc_config_cmd_v4 *cmd)
{
	const struct ieee80211_sta_he_cap *he_cap = &link_sta->he_cap;
	u16 mcs_160 = le16_to_cpu(he_cap->he_mcs_nss_supp.rx_mcs_160);
	u16 mcs_80 = le16_to_cpu(he_cap->he_mcs_nss_supp.rx_mcs_80);
	u16 tx_mcs_80 = le16_to_cpu(own_he_cap->he_mcs_nss_supp.tx_mcs_80);
	u16 tx_mcs_160 = le16_to_cpu(own_he_cap->he_mcs_nss_supp.tx_mcs_160);
	int i;
	u8 nss = link_sta->rx_nss;

	/* the station support only a single receive chain */
	if (link_sta->smps_mode == IEEE80211_SMPS_STATIC)
		nss = 1;

	for (i = 0; i < nss && i < IWL_TLC_NSS_MAX; i++) {
		u16 _mcs_160 = (mcs_160 >> (2 * i)) & 0x3;
		u16 _mcs_80 = (mcs_80 >> (2 * i)) & 0x3;
		u16 _tx_mcs_160 = (tx_mcs_160 >> (2 * i)) & 0x3;
		u16 _tx_mcs_80 = (tx_mcs_80 >> (2 * i)) & 0x3;

		/* If one side doesn't support - mark both as not supporting */
		if (_mcs_80 == IEEE80211_HE_MCS_NOT_SUPPORTED ||
		    _tx_mcs_80 == IEEE80211_HE_MCS_NOT_SUPPORTED) {
			_mcs_80 = IEEE80211_HE_MCS_NOT_SUPPORTED;
			_tx_mcs_80 = IEEE80211_HE_MCS_NOT_SUPPORTED;
		}
		if (_mcs_80 > _tx_mcs_80)
			_mcs_80 = _tx_mcs_80;
		cmd->ht_rates[i][IWL_TLC_MCS_PER_BW_80] =
			cpu_to_le16(iwl_mld_he_mac80211_mcs_to_fw_mcs(_mcs_80));

		/* If one side doesn't support - mark both as not supporting */
		if (_mcs_160 == IEEE80211_HE_MCS_NOT_SUPPORTED ||
		    _tx_mcs_160 == IEEE80211_HE_MCS_NOT_SUPPORTED) {
			_mcs_160 = IEEE80211_HE_MCS_NOT_SUPPORTED;
			_tx_mcs_160 = IEEE80211_HE_MCS_NOT_SUPPORTED;
		}
		if (_mcs_160 > _tx_mcs_160)
			_mcs_160 = _tx_mcs_160;
		cmd->ht_rates[i][IWL_TLC_MCS_PER_BW_160] =
			cpu_to_le16(iwl_mld_he_mac80211_mcs_to_fw_mcs(_mcs_160));
	}
}

static void iwl_mld_set_eht_mcs(__le16 ht_rates[][3],
				enum IWL_TLC_MCS_PER_BW bw,
				u8 max_nss, u16 mcs_msk)
{
	if (max_nss >= 2)
		ht_rates[IWL_TLC_NSS_2][bw] |= cpu_to_le16(mcs_msk);

	if (max_nss >= 1)
		ht_rates[IWL_TLC_NSS_1][bw] |= cpu_to_le16(mcs_msk);
}

static const
struct ieee80211_eht_mcs_nss_supp_bw *
iwl_mld_get_eht_mcs_of_bw(enum IWL_TLC_MCS_PER_BW bw,
			  const struct ieee80211_eht_mcs_nss_supp *eht_mcs)
{
	switch (bw) {
	case IWL_TLC_MCS_PER_BW_80:
		return &eht_mcs->bw._80;
	case IWL_TLC_MCS_PER_BW_160:
		return &eht_mcs->bw._160;
	case IWL_TLC_MCS_PER_BW_320:
		return &eht_mcs->bw._320;
	default:
		return NULL;
	}
}

static u8 iwl_mld_get_eht_max_nss(u8 rx_nss, u8 tx_nss)
{
	u8 tx = u8_get_bits(tx_nss, IEEE80211_EHT_MCS_NSS_TX);
	u8 rx = u8_get_bits(rx_nss, IEEE80211_EHT_MCS_NSS_RX);
	/* the max nss that can be used,
	 * is the min with our tx capa and the peer rx capa.
	 */
	return min(tx, rx);
}

#define MAX_NSS_MCS(mcs_num, rx, tx) \
	iwl_mld_get_eht_max_nss((rx)->rx_tx_mcs ##mcs_num## _max_nss, \
				(tx)->rx_tx_mcs ##mcs_num## _max_nss)

static void
iwl_mld_fill_eht_rates(struct ieee80211_vif *vif,
		       const struct ieee80211_link_sta *link_sta,
		       const struct ieee80211_sta_he_cap *own_he_cap,
		       const struct ieee80211_sta_eht_cap *own_eht_cap,
		       struct iwl_tlc_config_cmd_v4 *cmd)
{
	/* peer RX mcs capa */
	const struct ieee80211_eht_mcs_nss_supp *eht_rx_mcs =
		&link_sta->eht_cap.eht_mcs_nss_supp;
	/* our TX mcs capa */
	const struct ieee80211_eht_mcs_nss_supp *eht_tx_mcs =
		&own_eht_cap->eht_mcs_nss_supp;

	enum IWL_TLC_MCS_PER_BW bw;
	struct ieee80211_eht_mcs_nss_supp_20mhz_only mcs_rx_20;
	struct ieee80211_eht_mcs_nss_supp_20mhz_only mcs_tx_20;

	/* peer is 20 MHz only */
	if (vif->type == NL80211_IFTYPE_AP &&
	    !(link_sta->he_cap.he_cap_elem.phy_cap_info[0] &
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_MASK_ALL)) {
		mcs_rx_20 = eht_rx_mcs->only_20mhz;
	} else {
		mcs_rx_20.rx_tx_mcs7_max_nss =
			eht_rx_mcs->bw._80.rx_tx_mcs9_max_nss;
		mcs_rx_20.rx_tx_mcs9_max_nss =
			eht_rx_mcs->bw._80.rx_tx_mcs9_max_nss;
		mcs_rx_20.rx_tx_mcs11_max_nss =
			eht_rx_mcs->bw._80.rx_tx_mcs11_max_nss;
		mcs_rx_20.rx_tx_mcs13_max_nss =
			eht_rx_mcs->bw._80.rx_tx_mcs13_max_nss;
	}

	/* NIC is capable of 20 MHz only */
	if (!(own_he_cap->he_cap_elem.phy_cap_info[0] &
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_MASK_ALL)) {
		mcs_tx_20 = eht_tx_mcs->only_20mhz;
	} else {
		mcs_tx_20.rx_tx_mcs7_max_nss =
			eht_tx_mcs->bw._80.rx_tx_mcs9_max_nss;
		mcs_tx_20.rx_tx_mcs9_max_nss =
			eht_tx_mcs->bw._80.rx_tx_mcs9_max_nss;
		mcs_tx_20.rx_tx_mcs11_max_nss =
			eht_tx_mcs->bw._80.rx_tx_mcs11_max_nss;
		mcs_tx_20.rx_tx_mcs13_max_nss =
			eht_tx_mcs->bw._80.rx_tx_mcs13_max_nss;
	}

	/* rates for 20/40/80 MHz */
	bw = IWL_TLC_MCS_PER_BW_80;
	iwl_mld_set_eht_mcs(cmd->ht_rates, bw,
			    MAX_NSS_MCS(7, &mcs_rx_20, &mcs_tx_20),
			    GENMASK(7, 0));
	iwl_mld_set_eht_mcs(cmd->ht_rates, bw,
			    MAX_NSS_MCS(9, &mcs_rx_20, &mcs_tx_20),
			    GENMASK(9, 8));
	iwl_mld_set_eht_mcs(cmd->ht_rates, bw,
			    MAX_NSS_MCS(11, &mcs_rx_20, &mcs_tx_20),
			    GENMASK(11, 10));
	iwl_mld_set_eht_mcs(cmd->ht_rates, bw,
			    MAX_NSS_MCS(13, &mcs_rx_20, &mcs_tx_20),
			    GENMASK(13, 12));

	/* rates for 160/320 MHz */
	for (bw = IWL_TLC_MCS_PER_BW_160; bw <= IWL_TLC_MCS_PER_BW_320; bw++) {
		const struct ieee80211_eht_mcs_nss_supp_bw *mcs_rx =
			iwl_mld_get_eht_mcs_of_bw(bw, eht_rx_mcs);
		const struct ieee80211_eht_mcs_nss_supp_bw *mcs_tx =
			iwl_mld_get_eht_mcs_of_bw(bw, eht_tx_mcs);

		/* got unsupported index for bw */
		if (!mcs_rx || !mcs_tx)
			continue;

		/* break out if we don't support the bandwidth */
		if (cmd->max_ch_width < (bw + IWL_TLC_MNG_CH_WIDTH_80MHZ))
			break;

		iwl_mld_set_eht_mcs(cmd->ht_rates, bw,
				    MAX_NSS_MCS(9, mcs_rx, mcs_tx),
				    GENMASK(9, 0));
		iwl_mld_set_eht_mcs(cmd->ht_rates, bw,
				    MAX_NSS_MCS(11, mcs_rx, mcs_tx),
				    GENMASK(11, 10));
		iwl_mld_set_eht_mcs(cmd->ht_rates, bw,
				    MAX_NSS_MCS(13, mcs_rx, mcs_tx),
				    GENMASK(13, 12));
	}

	/* the station support only a single receive chain */
	if (link_sta->smps_mode == IEEE80211_SMPS_STATIC ||
	    link_sta->rx_nss < 2)
		memset(cmd->ht_rates[IWL_TLC_NSS_2], 0,
		       sizeof(cmd->ht_rates[IWL_TLC_NSS_2]));
}

static void
iwl_mld_fill_supp_rates(struct iwl_mld *mld, struct ieee80211_vif *vif,
			struct ieee80211_link_sta *link_sta,
			struct ieee80211_supported_band *sband,
			const struct ieee80211_sta_he_cap *own_he_cap,
			const struct ieee80211_sta_eht_cap *own_eht_cap,
			struct iwl_tlc_config_cmd_v4 *cmd)
{
	int i;
	u16 non_ht_rates = 0;
	unsigned long rates_bitmap;
	const struct ieee80211_sta_ht_cap *ht_cap = &link_sta->ht_cap;
	const struct ieee80211_sta_vht_cap *vht_cap = &link_sta->vht_cap;
	const struct ieee80211_sta_he_cap *he_cap = &link_sta->he_cap;

	/* non HT rates */
	rates_bitmap = link_sta->supp_rates[sband->band];
	for_each_set_bit(i, &rates_bitmap, BITS_PER_LONG)
		non_ht_rates |= BIT(sband->bitrates[i].hw_value);

	cmd->non_ht_rates = cpu_to_le16(non_ht_rates);
	cmd->mode = IWL_TLC_MNG_MODE_NON_HT;

	if (link_sta->eht_cap.has_eht && own_he_cap && own_eht_cap) {
		cmd->mode = IWL_TLC_MNG_MODE_EHT;
		iwl_mld_fill_eht_rates(vif, link_sta, own_he_cap,
				       own_eht_cap, cmd);
	} else if (he_cap->has_he && own_he_cap) {
		cmd->mode = IWL_TLC_MNG_MODE_HE;
		iwl_mld_fill_he_rates(link_sta, own_he_cap, cmd);
	} else if (vht_cap->vht_supported) {
		cmd->mode = IWL_TLC_MNG_MODE_VHT;
		iwl_mld_fill_vht_rates(link_sta, vht_cap, cmd);
	} else if (ht_cap->ht_supported) {
		cmd->mode = IWL_TLC_MNG_MODE_HT;
		cmd->ht_rates[IWL_TLC_NSS_1][IWL_TLC_MCS_PER_BW_80] =
			cpu_to_le16(ht_cap->mcs.rx_mask[0]);

		/* the station support only a single receive chain */
		if (link_sta->smps_mode == IEEE80211_SMPS_STATIC)
			cmd->ht_rates[IWL_TLC_NSS_2][IWL_TLC_MCS_PER_BW_80] =
				0;
		else
			cmd->ht_rates[IWL_TLC_NSS_2][IWL_TLC_MCS_PER_BW_80] =
				cpu_to_le16(ht_cap->mcs.rx_mask[1]);
	}
}

static void iwl_mld_send_tlc_cmd(struct iwl_mld *mld,
				 struct ieee80211_vif *vif,
				 struct ieee80211_link_sta *link_sta,
				 enum nl80211_band band)
{
	struct iwl_mld_sta *mld_sta = iwl_mld_sta_from_mac80211(link_sta->sta);
	struct ieee80211_supported_band *sband = mld->hw->wiphy->bands[band];
	const struct ieee80211_sta_he_cap *own_he_cap =
		ieee80211_get_he_iftype_cap_vif(sband, vif);
	const struct ieee80211_sta_eht_cap *own_eht_cap =
		ieee80211_get_eht_iftype_cap_vif(sband, vif);
	struct iwl_tlc_config_cmd_v4 cmd = {
		/* For AP mode, use 20 MHz until the STA is authorized */
		.max_ch_width = mld_sta->sta_state > IEEE80211_STA_ASSOC ?
			iwl_mld_fw_bw_from_sta_bw(link_sta) :
			IWL_TLC_MNG_CH_WIDTH_20MHZ,
		.flags = iwl_mld_get_tlc_cmd_flags(mld, vif, link_sta,
						   own_he_cap, own_eht_cap),
		.chains = iwl_mld_get_fw_chains(mld),
		.sgi_ch_width_supp = iwl_mld_get_fw_sgi(link_sta),
		.max_mpdu_len = cpu_to_le16(link_sta->agg.max_amsdu_len),
	};
	int fw_sta_id = iwl_mld_fw_sta_id_from_link_sta(mld, link_sta);
	int ret;

	if (fw_sta_id < 0)
		return;

	cmd.sta_id = fw_sta_id;

	iwl_mld_fill_supp_rates(mld, vif, link_sta, sband,
				own_he_cap, own_eht_cap,
				&cmd);

	IWL_DEBUG_RATE(mld,
		       "TLC CONFIG CMD, sta_id=%d, max_ch_width=%d, mode=%d\n",
		       cmd.sta_id, cmd.max_ch_width, cmd.mode);

	/* Send async since this can be called within a RCU-read section */
	ret = iwl_mld_send_cmd_with_flags_pdu(mld, WIDE_ID(DATA_PATH_GROUP,
							   TLC_MNG_CONFIG_CMD),
					      CMD_ASYNC, &cmd);
	if (ret)
		IWL_ERR(mld, "Failed to send TLC cmd (%d)\n", ret);
}

int iwl_mld_send_tlc_dhc(struct iwl_mld *mld, u8 sta_id, u32 type, u32 data)
{
	struct {
		struct iwl_dhc_cmd dhc;
		struct iwl_dhc_tlc_cmd tlc;
	} __packed cmd = {
		.tlc.sta_id = sta_id,
		.tlc.type = cpu_to_le32(type),
		.tlc.data[0] = cpu_to_le32(data),
		.dhc.length = cpu_to_le32(sizeof(cmd.tlc) >> 2),
		.dhc.index_and_mask =
			cpu_to_le32(DHC_TABLE_INTEGRATION | DHC_TARGET_UMAC |
				    DHC_INTEGRATION_TLC_DEBUG_CONFIG),
	};
	int ret;

	ret = iwl_mld_send_cmd_with_flags_pdu(mld,
					      WIDE_ID(IWL_ALWAYS_LONG_GROUP,
						      DEBUG_HOST_COMMAND),
					      CMD_ASYNC, &cmd);
	IWL_DEBUG_RATE(mld, "sta_id %d, type: 0x%X, value: 0x%X, ret%d\n",
		       sta_id, type, data, ret);
	return ret;
}

void iwl_mld_config_tlc_link(struct iwl_mld *mld,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *link_conf,
			     struct ieee80211_link_sta *link_sta)
{
	struct iwl_mld_sta *mld_sta = iwl_mld_sta_from_mac80211(link_sta->sta);
	enum nl80211_band band;

	if (WARN_ON_ONCE(!link_conf->chanreq.oper.chan))
		return;

	/* Before we have information about a station, configure the A-MSDU RC
	 * limit such that iwlmd and mac80211 would not be allowed to build
	 * A-MSDUs.
	 */
	if (mld_sta->sta_state < IEEE80211_STA_ASSOC) {
		link_sta->agg.max_rc_amsdu_len = 1;
		ieee80211_sta_recalc_aggregates(link_sta->sta);
	}

	band = link_conf->chanreq.oper.chan->band;
	iwl_mld_send_tlc_cmd(mld, vif, link_sta, band);
}

void iwl_mld_config_tlc(struct iwl_mld *mld, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta)
{
	struct ieee80211_bss_conf *link;
	int link_id;

	lockdep_assert_wiphy(mld->wiphy);

	for_each_vif_active_link(vif, link, link_id) {
		struct ieee80211_link_sta *link_sta =
			link_sta_dereference_check(sta, link_id);

		if (!link || !link_sta)
			continue;

		iwl_mld_config_tlc_link(mld, vif, link, link_sta);
	}
}

static u16
iwl_mld_get_amsdu_size_of_tid(struct iwl_mld *mld,
			      struct ieee80211_link_sta *link_sta,
			      unsigned int tid)
{
	struct ieee80211_sta *sta = link_sta->sta;
	struct ieee80211_vif *vif = iwl_mld_sta_from_mac80211(sta)->vif;
	const u8 tid_to_mac80211_ac[] = {
		IEEE80211_AC_BE,
		IEEE80211_AC_BK,
		IEEE80211_AC_BK,
		IEEE80211_AC_BE,
		IEEE80211_AC_VI,
		IEEE80211_AC_VI,
		IEEE80211_AC_VO,
		IEEE80211_AC_VO,
	};
	unsigned int result = link_sta->agg.max_rc_amsdu_len;
	u8 ac, txf, lmac;

	lockdep_assert_wiphy(mld->wiphy);

	/* Don't send an AMSDU that will be longer than the TXF.
	 * Add a security margin of 256 for the TX command + headers.
	 * We also want to have the start of the next packet inside the
	 * fifo to be able to send bursts.
	 */

	if (WARN_ON(tid >= ARRAY_SIZE(tid_to_mac80211_ac)))
		return 0;

	ac = tid_to_mac80211_ac[tid];

	/* For HE redirect to trigger based fifos */
	if (link_sta->he_cap.has_he)
		ac += 4;

	txf = iwl_mld_mac80211_ac_to_fw_tx_fifo(ac);

	/* Only one link: take the lmac according to the band */
	if (hweight16(sta->valid_links) <= 1) {
		enum nl80211_band band;
		struct ieee80211_bss_conf *link =
			wiphy_dereference(mld->wiphy,
					  vif->link_conf[link_sta->link_id]);

		if (WARN_ON(!link || !link->chanreq.oper.chan))
			band = NL80211_BAND_2GHZ;
		else
			band = link->chanreq.oper.chan->band;
		lmac = iwl_mld_get_lmac_id(mld, band);

	/* More than one link but with 2 lmacs: take the minimum */
	} else if (fw_has_capa(&mld->fw->ucode_capa,
			       IWL_UCODE_TLV_CAPA_CDB_SUPPORT)) {
		lmac = IWL_LMAC_5G_INDEX;
		result = min_t(unsigned int, result,
			       mld->fwrt.smem_cfg.lmac[lmac].txfifo_size[txf] - 256);
		lmac = IWL_LMAC_24G_INDEX;
	/* More than one link but only one lmac */
	} else {
		lmac = IWL_LMAC_24G_INDEX;
	}

	return min_t(unsigned int, result,
		     mld->fwrt.smem_cfg.lmac[lmac].txfifo_size[txf] - 256);
}

void iwl_mld_handle_tlc_notif(struct iwl_mld *mld,
			      struct iwl_rx_packet *pkt)
{
	struct iwl_tlc_update_notif *notif = (void *)pkt->data;
	struct ieee80211_link_sta *link_sta;
	u32 flags = le32_to_cpu(notif->flags);
	u32 enabled;
	u16 size;

	if (IWL_FW_CHECK(mld, notif->sta_id >= mld->fw->ucode_capa.num_stations,
			 "Invalid sta id (%d) in TLC notification\n",
			 notif->sta_id))
		return;

	link_sta = wiphy_dereference(mld->wiphy,
				     mld->fw_id_to_link_sta[notif->sta_id]);

	if (WARN(IS_ERR_OR_NULL(link_sta),
		 "link_sta of sta id (%d) doesn't exist\n", notif->sta_id))
		return;

	if (flags & IWL_TLC_NOTIF_FLAG_RATE) {
		struct iwl_mld_link_sta *mld_link_sta =
			iwl_mld_link_sta_from_mac80211(link_sta);
		char pretty_rate[100];

		if (WARN_ON(!mld_link_sta))
			return;

		mld_link_sta->last_rate_n_flags = le32_to_cpu(notif->rate);

		rs_pretty_print_rate(pretty_rate, sizeof(pretty_rate),
				     mld_link_sta->last_rate_n_flags);
		IWL_DEBUG_RATE(mld, "TLC notif: new rate = %s\n", pretty_rate);
	}

	/* We are done processing the notif */
	if (!(flags & IWL_TLC_NOTIF_FLAG_AMSDU))
		return;

	enabled = le32_to_cpu(notif->amsdu_enabled);
	size = le32_to_cpu(notif->amsdu_size);

	if (size < 2000) {
		size = 0;
		enabled = 0;
	}

	if (IWL_FW_CHECK(mld, size > link_sta->agg.max_amsdu_len,
			 "Invalid AMSDU len in TLC notif: %d (Max AMSDU len: %d)\n",
			 size, link_sta->agg.max_amsdu_len))
		return;

	link_sta->agg.max_rc_amsdu_len = size;

	for (int i = 0; i < IWL_MAX_TID_COUNT; i++) {
		if (enabled & BIT(i))
			link_sta->agg.max_tid_amsdu_len[i] =
				iwl_mld_get_amsdu_size_of_tid(mld, link_sta, i);
		else
			link_sta->agg.max_tid_amsdu_len[i] = 1;
	}

	ieee80211_sta_recalc_aggregates(link_sta->sta);

	IWL_DEBUG_RATE(mld,
		       "AMSDU update. AMSDU size: %d, AMSDU selected size: %d, AMSDU TID bitmap 0x%X\n",
		       le32_to_cpu(notif->amsdu_size), size, enabled);
}
