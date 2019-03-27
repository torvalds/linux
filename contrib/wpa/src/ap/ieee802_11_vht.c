/*
 * hostapd / IEEE 802.11ac VHT
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of BSD license
 *
 * See README and COPYING for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "common/ieee802_11_defs.h"
#include "hostapd.h"
#include "ap_config.h"
#include "sta_info.h"
#include "beacon.h"
#include "ieee802_11.h"
#include "dfs.h"


u8 * hostapd_eid_vht_capabilities(struct hostapd_data *hapd, u8 *eid, u32 nsts)
{
	struct ieee80211_vht_capabilities *cap;
	struct hostapd_hw_modes *mode = hapd->iface->current_mode;
	u8 *pos = eid;

	if (!mode)
		return eid;

	if (mode->mode == HOSTAPD_MODE_IEEE80211G && hapd->conf->vendor_vht &&
	    mode->vht_capab == 0 && hapd->iface->hw_features) {
		int i;

		for (i = 0; i < hapd->iface->num_hw_features; i++) {
			if (hapd->iface->hw_features[i].mode ==
			    HOSTAPD_MODE_IEEE80211A) {
				mode = &hapd->iface->hw_features[i];
				break;
			}
		}
	}

	*pos++ = WLAN_EID_VHT_CAP;
	*pos++ = sizeof(*cap);

	cap = (struct ieee80211_vht_capabilities *) pos;
	os_memset(cap, 0, sizeof(*cap));
	cap->vht_capabilities_info = host_to_le32(
		hapd->iface->conf->vht_capab);

	if (nsts != 0) {
		u32 hapd_nsts;

		hapd_nsts = le_to_host32(cap->vht_capabilities_info);
		hapd_nsts = (hapd_nsts >> VHT_CAP_BEAMFORMEE_STS_OFFSET) & 7;
		cap->vht_capabilities_info &=
			~(host_to_le32(hapd_nsts <<
				       VHT_CAP_BEAMFORMEE_STS_OFFSET));
		cap->vht_capabilities_info |=
			host_to_le32(nsts << VHT_CAP_BEAMFORMEE_STS_OFFSET);
	}

	/* Supported MCS set comes from hw */
	os_memcpy(&cap->vht_supported_mcs_set, mode->vht_mcs_set, 8);

	pos += sizeof(*cap);

	return pos;
}


u8 * hostapd_eid_vht_operation(struct hostapd_data *hapd, u8 *eid)
{
	struct ieee80211_vht_operation *oper;
	u8 *pos = eid;

	*pos++ = WLAN_EID_VHT_OPERATION;
	*pos++ = sizeof(*oper);

	oper = (struct ieee80211_vht_operation *) pos;
	os_memset(oper, 0, sizeof(*oper));

	/*
	 * center freq = 5 GHz + (5 * index)
	 * So index 42 gives center freq 5.210 GHz
	 * which is channel 42 in 5G band
	 */
	oper->vht_op_info_chan_center_freq_seg0_idx =
		hapd->iconf->vht_oper_centr_freq_seg0_idx;
	oper->vht_op_info_chan_center_freq_seg1_idx =
		hapd->iconf->vht_oper_centr_freq_seg1_idx;

	oper->vht_op_info_chwidth = hapd->iconf->vht_oper_chwidth;
	if (hapd->iconf->vht_oper_chwidth == 2) {
		/*
		 * Convert 160 MHz channel width to new style as interop
		 * workaround.
		 */
		oper->vht_op_info_chwidth = 1;
		oper->vht_op_info_chan_center_freq_seg1_idx =
			oper->vht_op_info_chan_center_freq_seg0_idx;
		if (hapd->iconf->channel <
		    hapd->iconf->vht_oper_centr_freq_seg0_idx)
			oper->vht_op_info_chan_center_freq_seg0_idx -= 8;
		else
			oper->vht_op_info_chan_center_freq_seg0_idx += 8;
	} else if (hapd->iconf->vht_oper_chwidth == 3) {
		/*
		 * Convert 80+80 MHz channel width to new style as interop
		 * workaround.
		 */
		oper->vht_op_info_chwidth = 1;
	}

	/* VHT Basic MCS set comes from hw */
	/* Hard code 1 stream, MCS0-7 is a min Basic VHT MCS rates */
	oper->vht_basic_mcs_set = host_to_le16(0xfffc);
	pos += sizeof(*oper);

	return pos;
}


static int check_valid_vht_mcs(struct hostapd_hw_modes *mode,
			       const u8 *sta_vht_capab)
{
	const struct ieee80211_vht_capabilities *vht_cap;
	struct ieee80211_vht_capabilities ap_vht_cap;
	u16 sta_rx_mcs_set, ap_tx_mcs_set;
	int i;

	if (!mode)
		return 1;

	/*
	 * Disable VHT caps for STAs for which there is not even a single
	 * allowed MCS in any supported number of streams, i.e., STA is
	 * advertising 3 (not supported) as VHT MCS rates for all supported
	 * stream cases.
	 */
	os_memcpy(&ap_vht_cap.vht_supported_mcs_set, mode->vht_mcs_set,
		  sizeof(ap_vht_cap.vht_supported_mcs_set));
	vht_cap = (const struct ieee80211_vht_capabilities *) sta_vht_capab;

	/* AP Tx MCS map vs. STA Rx MCS map */
	sta_rx_mcs_set = le_to_host16(vht_cap->vht_supported_mcs_set.rx_map);
	ap_tx_mcs_set = le_to_host16(ap_vht_cap.vht_supported_mcs_set.tx_map);

	for (i = 0; i < VHT_RX_NSS_MAX_STREAMS; i++) {
		if ((ap_tx_mcs_set & (0x3 << (i * 2))) == 3)
			continue;

		if ((sta_rx_mcs_set & (0x3 << (i * 2))) == 3)
			continue;

		return 1;
	}

	wpa_printf(MSG_DEBUG,
		   "No matching VHT MCS found between AP TX and STA RX");
	return 0;
}


u8 * hostapd_eid_wb_chsw_wrapper(struct hostapd_data *hapd, u8 *eid)
{
	u8 bw, chan1, chan2 = 0;
	int freq1;

	if (!hapd->cs_freq_params.channel ||
	    !hapd->cs_freq_params.vht_enabled)
		return eid;

	/* bandwidth: 0: 40, 1: 80, 2: 160, 3: 80+80 */
	switch (hapd->cs_freq_params.bandwidth) {
	case 40:
		bw = 0;
		break;
	case 80:
		/* check if it's 80+80 */
		if (!hapd->cs_freq_params.center_freq2)
			bw = 1;
		else
			bw = 3;
		break;
	case 160:
		bw = 2;
		break;
	default:
		/* not valid VHT bandwidth or not in CSA */
		return eid;
	}

	freq1 = hapd->cs_freq_params.center_freq1 ?
		hapd->cs_freq_params.center_freq1 :
		hapd->cs_freq_params.freq;
	if (ieee80211_freq_to_chan(freq1, &chan1) !=
	    HOSTAPD_MODE_IEEE80211A)
		return eid;

	if (hapd->cs_freq_params.center_freq2 &&
	    ieee80211_freq_to_chan(hapd->cs_freq_params.center_freq2,
				   &chan2) != HOSTAPD_MODE_IEEE80211A)
		return eid;

	*eid++ = WLAN_EID_VHT_CHANNEL_SWITCH_WRAPPER;
	*eid++ = 5; /* Length of Channel Switch Wrapper */
	*eid++ = WLAN_EID_VHT_WIDE_BW_CHSWITCH;
	*eid++ = 3; /* Length of Wide Bandwidth Channel Switch element */
	*eid++ = bw; /* New Channel Width */
	*eid++ = chan1; /* New Channel Center Frequency Segment 0 */
	*eid++ = chan2; /* New Channel Center Frequency Segment 1 */

	return eid;
}


u8 * hostapd_eid_txpower_envelope(struct hostapd_data *hapd, u8 *eid)
{
	struct hostapd_iface *iface = hapd->iface;
	struct hostapd_config *iconf = iface->conf;
	struct hostapd_hw_modes *mode = iface->current_mode;
	struct hostapd_channel_data *chan;
	int dfs, i;
	u8 channel, tx_pwr_count, local_pwr_constraint;
	int max_tx_power;
	u8 tx_pwr;

	if (!mode)
		return eid;

	if (ieee80211_freq_to_chan(iface->freq, &channel) == NUM_HOSTAPD_MODES)
		return eid;

	for (i = 0; i < mode->num_channels; i++) {
		if (mode->channels[i].freq == iface->freq)
			break;
	}
	if (i == mode->num_channels)
		return eid;

	switch (iface->conf->vht_oper_chwidth) {
	case VHT_CHANWIDTH_USE_HT:
		if (iconf->secondary_channel == 0) {
			/* Max Transmit Power count = 0 (20 MHz) */
			tx_pwr_count = 0;
		} else {
			/* Max Transmit Power count = 1 (20, 40 MHz) */
			tx_pwr_count = 1;
		}
		break;
	case VHT_CHANWIDTH_80MHZ:
		/* Max Transmit Power count = 2 (20, 40, and 80 MHz) */
		tx_pwr_count = 2;
		break;
	case VHT_CHANWIDTH_80P80MHZ:
	case VHT_CHANWIDTH_160MHZ:
		/* Max Transmit Power count = 3 (20, 40, 80, 160/80+80 MHz) */
		tx_pwr_count = 3;
		break;
	default:
		return eid;
	}

	/*
	 * Below local_pwr_constraint logic is referred from
	 * hostapd_eid_pwr_constraint.
	 *
	 * Check if DFS is required by regulatory.
	 */
	dfs = hostapd_is_dfs_required(hapd->iface);
	if (dfs < 0)
		dfs = 0;

	/*
	 * In order to meet regulations when TPC is not implemented using
	 * a transmit power that is below the legal maximum (including any
	 * mitigation factor) should help. In this case, indicate 3 dB below
	 * maximum allowed transmit power.
	 */
	if (hapd->iconf->local_pwr_constraint == -1)
		local_pwr_constraint = (dfs == 0) ? 0 : 3;
	else
		local_pwr_constraint = hapd->iconf->local_pwr_constraint;

	/*
	 * A STA that is not an AP shall use a transmit power less than or
	 * equal to the local maximum transmit power level for the channel.
	 * The local maximum transmit power can be calculated from the formula:
	 * local max TX pwr = max TX pwr - local pwr constraint
	 * Where max TX pwr is maximum transmit power level specified for
	 * channel in Country element and local pwr constraint is specified
	 * for channel in this Power Constraint element.
	 */
	chan = &mode->channels[i];
	max_tx_power = chan->max_tx_power - local_pwr_constraint;

	/*
	 * Local Maximum Transmit power is encoded as two's complement
	 * with a 0.5 dB step.
	 */
	max_tx_power *= 2; /* in 0.5 dB steps */
	if (max_tx_power > 127) {
		/* 63.5 has special meaning of 63.5 dBm or higher */
		max_tx_power = 127;
	}
	if (max_tx_power < -128)
		max_tx_power = -128;
	if (max_tx_power < 0)
		tx_pwr = 0x80 + max_tx_power + 128;
	else
		tx_pwr = max_tx_power;

	*eid++ = WLAN_EID_VHT_TRANSMIT_POWER_ENVELOPE;
	*eid++ = 2 + tx_pwr_count;

	/*
	 * Max Transmit Power count and
	 * Max Transmit Power units = 0 (EIRP)
	 */
	*eid++ = tx_pwr_count;

	for (i = 0; i <= tx_pwr_count; i++)
		*eid++ = tx_pwr;

	return eid;
}


u16 copy_sta_vht_capab(struct hostapd_data *hapd, struct sta_info *sta,
		       const u8 *vht_capab)
{
	/* Disable VHT caps for STAs associated to no-VHT BSSes. */
	if (!vht_capab ||
	    !hapd->iconf->ieee80211ac || hapd->conf->disable_11ac ||
	    !check_valid_vht_mcs(hapd->iface->current_mode, vht_capab)) {
		sta->flags &= ~WLAN_STA_VHT;
		os_free(sta->vht_capabilities);
		sta->vht_capabilities = NULL;
		return WLAN_STATUS_SUCCESS;
	}

	if (sta->vht_capabilities == NULL) {
		sta->vht_capabilities =
			os_zalloc(sizeof(struct ieee80211_vht_capabilities));
		if (sta->vht_capabilities == NULL)
			return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	sta->flags |= WLAN_STA_VHT;
	os_memcpy(sta->vht_capabilities, vht_capab,
		  sizeof(struct ieee80211_vht_capabilities));

	return WLAN_STATUS_SUCCESS;
}


u16 copy_sta_vendor_vht(struct hostapd_data *hapd, struct sta_info *sta,
			const u8 *ie, size_t len)
{
	const u8 *vht_capab;
	unsigned int vht_capab_len;

	if (!ie || len < 5 + 2 + sizeof(struct ieee80211_vht_capabilities) ||
	    hapd->conf->disable_11ac)
		goto no_capab;

	/* The VHT Capabilities element embedded in vendor VHT */
	vht_capab = ie + 5;
	if (vht_capab[0] != WLAN_EID_VHT_CAP)
		goto no_capab;
	vht_capab_len = vht_capab[1];
	if (vht_capab_len < sizeof(struct ieee80211_vht_capabilities) ||
	    (int) vht_capab_len > ie + len - vht_capab - 2)
		goto no_capab;
	vht_capab += 2;

	if (sta->vht_capabilities == NULL) {
		sta->vht_capabilities =
			os_zalloc(sizeof(struct ieee80211_vht_capabilities));
		if (sta->vht_capabilities == NULL)
			return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	sta->flags |= WLAN_STA_VHT | WLAN_STA_VENDOR_VHT;
	os_memcpy(sta->vht_capabilities, vht_capab,
		  sizeof(struct ieee80211_vht_capabilities));
	return WLAN_STATUS_SUCCESS;

no_capab:
	sta->flags &= ~WLAN_STA_VENDOR_VHT;
	return WLAN_STATUS_SUCCESS;
}


u8 * hostapd_eid_vendor_vht(struct hostapd_data *hapd, u8 *eid)
{
	u8 *pos = eid;

	if (!hapd->iface->current_mode)
		return eid;

	*pos++ = WLAN_EID_VENDOR_SPECIFIC;
	*pos++ = (5 +		/* The Vendor OUI, type and subtype */
		  2 + sizeof(struct ieee80211_vht_capabilities) +
		  2 + sizeof(struct ieee80211_vht_operation));

	WPA_PUT_BE32(pos, (OUI_BROADCOM << 8) | VENDOR_VHT_TYPE);
	pos += 4;
	*pos++ = VENDOR_VHT_SUBTYPE;
	pos = hostapd_eid_vht_capabilities(hapd, pos, 0);
	pos = hostapd_eid_vht_operation(hapd, pos);

	return pos;
}


u16 set_sta_vht_opmode(struct hostapd_data *hapd, struct sta_info *sta,
		       const u8 *vht_oper_notif)
{
	if (!vht_oper_notif) {
		sta->flags &= ~WLAN_STA_VHT_OPMODE_ENABLED;
		return WLAN_STATUS_SUCCESS;
	}

	sta->flags |= WLAN_STA_VHT_OPMODE_ENABLED;
	sta->vht_opmode = *vht_oper_notif;
	return WLAN_STATUS_SUCCESS;
}


void hostapd_get_vht_capab(struct hostapd_data *hapd,
			   struct ieee80211_vht_capabilities *vht_cap,
			   struct ieee80211_vht_capabilities *neg_vht_cap)
{
	u32 cap, own_cap, sym_caps;

	if (vht_cap == NULL)
		return;
	os_memcpy(neg_vht_cap, vht_cap, sizeof(*neg_vht_cap));

	cap = le_to_host32(neg_vht_cap->vht_capabilities_info);
	own_cap = hapd->iconf->vht_capab;

	/* mask out symmetric VHT capabilities we don't support */
	sym_caps = VHT_CAP_SHORT_GI_80 | VHT_CAP_SHORT_GI_160;
	cap &= ~sym_caps | (own_cap & sym_caps);

	/* mask out beamformer/beamformee caps if not supported */
	if (!(own_cap & VHT_CAP_SU_BEAMFORMER_CAPABLE))
		cap &= ~(VHT_CAP_SU_BEAMFORMEE_CAPABLE |
			 VHT_CAP_BEAMFORMEE_STS_MAX);

	if (!(own_cap & VHT_CAP_SU_BEAMFORMEE_CAPABLE))
		cap &= ~(VHT_CAP_SU_BEAMFORMER_CAPABLE |
			 VHT_CAP_SOUNDING_DIMENSION_MAX);

	if (!(own_cap & VHT_CAP_MU_BEAMFORMER_CAPABLE))
		cap &= ~VHT_CAP_MU_BEAMFORMEE_CAPABLE;

	if (!(own_cap & VHT_CAP_MU_BEAMFORMEE_CAPABLE))
		cap &= ~VHT_CAP_MU_BEAMFORMER_CAPABLE;

	/* mask channel widths we don't support */
	switch (own_cap & VHT_CAP_SUPP_CHAN_WIDTH_MASK) {
	case VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ:
		break;
	case VHT_CAP_SUPP_CHAN_WIDTH_160MHZ:
		if (cap & VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ) {
			cap &= ~VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ;
			cap |= VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
		}
		break;
	default:
		cap &= ~VHT_CAP_SUPP_CHAN_WIDTH_MASK;
		break;
	}

	if (!(cap & VHT_CAP_SUPP_CHAN_WIDTH_MASK))
		cap &= ~VHT_CAP_SHORT_GI_160;

	/*
	 * if we don't support RX STBC, mask out TX STBC in the STA's HT caps
	 * if we don't support TX STBC, mask out RX STBC in the STA's HT caps
	 */
	if (!(own_cap & VHT_CAP_RXSTBC_MASK))
		cap &= ~VHT_CAP_TXSTBC;
	if (!(own_cap & VHT_CAP_TXSTBC))
		cap &= ~VHT_CAP_RXSTBC_MASK;

	neg_vht_cap->vht_capabilities_info = host_to_le32(cap);
}
