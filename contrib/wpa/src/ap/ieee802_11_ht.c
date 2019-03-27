/*
 * hostapd / IEEE 802.11n HT
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2007-2008, Intel Corporation
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "common/ieee802_11_defs.h"
#include "hostapd.h"
#include "ap_config.h"
#include "sta_info.h"
#include "beacon.h"
#include "ieee802_11.h"
#include "hw_features.h"
#include "ap_drv_ops.h"


u8 * hostapd_eid_ht_capabilities(struct hostapd_data *hapd, u8 *eid)
{
	struct ieee80211_ht_capabilities *cap;
	u8 *pos = eid;

	if (!hapd->iconf->ieee80211n || !hapd->iface->current_mode ||
	    hapd->conf->disable_11n)
		return eid;

	*pos++ = WLAN_EID_HT_CAP;
	*pos++ = sizeof(*cap);

	cap = (struct ieee80211_ht_capabilities *) pos;
	os_memset(cap, 0, sizeof(*cap));
	cap->ht_capabilities_info = host_to_le16(hapd->iconf->ht_capab);
	cap->a_mpdu_params = hapd->iface->current_mode->a_mpdu_params;
	os_memcpy(cap->supported_mcs_set, hapd->iface->current_mode->mcs_set,
		  16);

	/* TODO: ht_extended_capabilities (now fully disabled) */
	/* TODO: tx_bf_capability_info (now fully disabled) */
	/* TODO: asel_capabilities (now fully disabled) */

 	pos += sizeof(*cap);

	if (hapd->iconf->obss_interval) {
		struct ieee80211_obss_scan_parameters *scan_params;

		*pos++ = WLAN_EID_OVERLAPPING_BSS_SCAN_PARAMS;
		*pos++ = sizeof(*scan_params);

		scan_params = (struct ieee80211_obss_scan_parameters *) pos;
		os_memset(scan_params, 0, sizeof(*scan_params));
		scan_params->width_trigger_scan_interval =
			host_to_le16(hapd->iconf->obss_interval);

		/* Fill in default values for remaining parameters
		 * (IEEE Std 802.11-2012, 8.4.2.61 and MIB defval) */
		scan_params->scan_passive_dwell =
			host_to_le16(20);
		scan_params->scan_active_dwell =
			host_to_le16(10);
		scan_params->scan_passive_total_per_channel =
			host_to_le16(200);
		scan_params->scan_active_total_per_channel =
			host_to_le16(20);
		scan_params->channel_transition_delay_factor =
			host_to_le16(5);
		scan_params->scan_activity_threshold =
			host_to_le16(25);

		pos += sizeof(*scan_params);
	}

	return pos;
}


u8 * hostapd_eid_ht_operation(struct hostapd_data *hapd, u8 *eid)
{
	struct ieee80211_ht_operation *oper;
	u8 *pos = eid;

	if (!hapd->iconf->ieee80211n || hapd->conf->disable_11n)
		return eid;

	*pos++ = WLAN_EID_HT_OPERATION;
	*pos++ = sizeof(*oper);

	oper = (struct ieee80211_ht_operation *) pos;
	os_memset(oper, 0, sizeof(*oper));

	oper->primary_chan = hapd->iconf->channel;
	oper->operation_mode = host_to_le16(hapd->iface->ht_op_mode);
	if (hapd->iconf->secondary_channel == 1)
		oper->ht_param |= HT_INFO_HT_PARAM_SECONDARY_CHNL_ABOVE |
			HT_INFO_HT_PARAM_STA_CHNL_WIDTH;
	if (hapd->iconf->secondary_channel == -1)
		oper->ht_param |= HT_INFO_HT_PARAM_SECONDARY_CHNL_BELOW |
			HT_INFO_HT_PARAM_STA_CHNL_WIDTH;

	pos += sizeof(*oper);

	return pos;
}


u8 * hostapd_eid_secondary_channel(struct hostapd_data *hapd, u8 *eid)
{
	u8 sec_ch;

	if (!hapd->cs_freq_params.channel ||
	    !hapd->cs_freq_params.sec_channel_offset)
		return eid;

	if (hapd->cs_freq_params.sec_channel_offset == -1)
		sec_ch = HT_INFO_HT_PARAM_SECONDARY_CHNL_BELOW;
	else if (hapd->cs_freq_params.sec_channel_offset == 1)
		sec_ch = HT_INFO_HT_PARAM_SECONDARY_CHNL_ABOVE;
	else
		return eid;

	*eid++ = WLAN_EID_SECONDARY_CHANNEL_OFFSET;
	*eid++ = 1;
	*eid++ = sec_ch;

	return eid;
}


/*
op_mode
Set to 0 (HT pure) under the followign conditions
	- all STAs in the BSS are 20/40 MHz HT in 20/40 MHz BSS or
	- all STAs in the BSS are 20 MHz HT in 20 MHz BSS
Set to 1 (HT non-member protection) if there may be non-HT STAs
	in both the primary and the secondary channel
Set to 2 if only HT STAs are associated in BSS,
	however and at least one 20 MHz HT STA is associated
Set to 3 (HT mixed mode) when one or more non-HT STAs are associated
*/
int hostapd_ht_operation_update(struct hostapd_iface *iface)
{
	u16 cur_op_mode, new_op_mode;
	int op_mode_changes = 0;

	if (!iface->conf->ieee80211n || iface->conf->ht_op_mode_fixed)
		return 0;

	wpa_printf(MSG_DEBUG, "%s current operation mode=0x%X",
		   __func__, iface->ht_op_mode);

	if (!(iface->ht_op_mode & HT_OPER_OP_MODE_NON_GF_HT_STAS_PRESENT)
	    && iface->num_sta_ht_no_gf) {
		iface->ht_op_mode |= HT_OPER_OP_MODE_NON_GF_HT_STAS_PRESENT;
		op_mode_changes++;
	} else if ((iface->ht_op_mode &
		    HT_OPER_OP_MODE_NON_GF_HT_STAS_PRESENT) &&
		   iface->num_sta_ht_no_gf == 0) {
		iface->ht_op_mode &= ~HT_OPER_OP_MODE_NON_GF_HT_STAS_PRESENT;
		op_mode_changes++;
	}

	if (!(iface->ht_op_mode & HT_OPER_OP_MODE_OBSS_NON_HT_STAS_PRESENT) &&
	    (iface->num_sta_no_ht || iface->olbc_ht)) {
		iface->ht_op_mode |= HT_OPER_OP_MODE_OBSS_NON_HT_STAS_PRESENT;
		op_mode_changes++;
	} else if ((iface->ht_op_mode &
		    HT_OPER_OP_MODE_OBSS_NON_HT_STAS_PRESENT) &&
		   (iface->num_sta_no_ht == 0 && !iface->olbc_ht)) {
		iface->ht_op_mode &= ~HT_OPER_OP_MODE_OBSS_NON_HT_STAS_PRESENT;
		op_mode_changes++;
	}

	if (iface->num_sta_no_ht)
		new_op_mode = HT_PROT_NON_HT_MIXED;
	else if (iface->conf->secondary_channel && iface->num_sta_ht_20mhz)
		new_op_mode = HT_PROT_20MHZ_PROTECTION;
	else if (iface->olbc_ht)
		new_op_mode = HT_PROT_NONMEMBER_PROTECTION;
	else
		new_op_mode = HT_PROT_NO_PROTECTION;

	cur_op_mode = iface->ht_op_mode & HT_OPER_OP_MODE_HT_PROT_MASK;
	if (cur_op_mode != new_op_mode) {
		iface->ht_op_mode &= ~HT_OPER_OP_MODE_HT_PROT_MASK;
		iface->ht_op_mode |= new_op_mode;
		op_mode_changes++;
	}

	wpa_printf(MSG_DEBUG, "%s new operation mode=0x%X changes=%d",
		   __func__, iface->ht_op_mode, op_mode_changes);

	return op_mode_changes;
}


static int is_40_allowed(struct hostapd_iface *iface, int channel)
{
	int pri_freq, sec_freq;
	int affected_start, affected_end;
	int pri = 2407 + 5 * channel;

	if (iface->current_mode->mode != HOSTAPD_MODE_IEEE80211G)
		return 1;

	pri_freq = hostapd_hw_get_freq(iface->bss[0], iface->conf->channel);

	if (iface->conf->secondary_channel > 0)
		sec_freq = pri_freq + 20;
	else
		sec_freq = pri_freq - 20;

	affected_start = (pri_freq + sec_freq) / 2 - 25;
	affected_end = (pri_freq + sec_freq) / 2 + 25;
	if ((pri < affected_start || pri > affected_end))
		return 1; /* not within affected channel range */

	wpa_printf(MSG_ERROR, "40 MHz affected channel range: [%d,%d] MHz",
		   affected_start, affected_end);
	wpa_printf(MSG_ERROR, "Neighboring BSS: freq=%d", pri);
	return 0;
}


void hostapd_2040_coex_action(struct hostapd_data *hapd,
			      const struct ieee80211_mgmt *mgmt, size_t len)
{
	struct hostapd_iface *iface = hapd->iface;
	struct ieee80211_2040_bss_coex_ie *bc_ie;
	struct ieee80211_2040_intol_chan_report *ic_report;
	int is_ht40_allowed = 1;
	int i;
	const u8 *start = (const u8 *) mgmt;
	const u8 *data = start + IEEE80211_HDRLEN + 2;
	struct sta_info *sta;

	wpa_printf(MSG_DEBUG,
		   "HT: Received 20/40 BSS Coexistence Management frame from "
		   MACSTR, MAC2STR(mgmt->sa));

	hostapd_logger(hapd, mgmt->sa, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_DEBUG, "hostapd_public_action - action=%d",
		       mgmt->u.action.u.public_action.action);

	if (!(iface->conf->ht_capab & HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET)) {
		wpa_printf(MSG_DEBUG,
			   "Ignore 20/40 BSS Coexistence Management frame since 40 MHz capability is not enabled");
		return;
	}

	if (len < IEEE80211_HDRLEN + 2 + sizeof(*bc_ie)) {
		wpa_printf(MSG_DEBUG,
			   "Ignore too short 20/40 BSS Coexistence Management frame");
		return;
	}

	/* 20/40 BSS Coexistence element */
	bc_ie = (struct ieee80211_2040_bss_coex_ie *) data;
	if (bc_ie->element_id != WLAN_EID_20_40_BSS_COEXISTENCE ||
	    bc_ie->length < 1) {
		wpa_printf(MSG_DEBUG, "Unexpected IE (%u,%u) in coex report",
			   bc_ie->element_id, bc_ie->length);
		return;
	}
	if (len < IEEE80211_HDRLEN + 2 + 2 + bc_ie->length) {
		wpa_printf(MSG_DEBUG,
			   "Truncated 20/40 BSS Coexistence element");
		return;
	}
	data += 2 + bc_ie->length;

	wpa_printf(MSG_DEBUG,
		   "20/40 BSS Coexistence Information field: 0x%x (%s%s%s%s%s%s)",
		   bc_ie->coex_param,
		   (bc_ie->coex_param & BIT(0)) ? "[InfoReq]" : "",
		   (bc_ie->coex_param & BIT(1)) ? "[40MHzIntolerant]" : "",
		   (bc_ie->coex_param & BIT(2)) ? "[20MHzBSSWidthReq]" : "",
		   (bc_ie->coex_param & BIT(3)) ? "[OBSSScanExemptionReq]" : "",
		   (bc_ie->coex_param & BIT(4)) ?
		   "[OBSSScanExemptionGrant]" : "",
		   (bc_ie->coex_param & (BIT(5) | BIT(6) | BIT(7))) ?
		   "[Reserved]" : "");

	if (bc_ie->coex_param & WLAN_20_40_BSS_COEX_20MHZ_WIDTH_REQ) {
		/* Intra-BSS communication prohibiting 20/40 MHz BSS operation
		 */
		sta = ap_get_sta(hapd, mgmt->sa);
		if (!sta || !(sta->flags & WLAN_STA_ASSOC)) {
			wpa_printf(MSG_DEBUG,
				   "Ignore intra-BSS 20/40 BSS Coexistence Management frame from not-associated STA");
			return;
		}

		hostapd_logger(hapd, mgmt->sa,
			       HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "20 MHz BSS width request bit is set in BSS coexistence information field");
		is_ht40_allowed = 0;
	}

	if (bc_ie->coex_param & WLAN_20_40_BSS_COEX_40MHZ_INTOL) {
		/* Inter-BSS communication prohibiting 20/40 MHz BSS operation
		 */
		hostapd_logger(hapd, mgmt->sa,
			       HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "40 MHz intolerant bit is set in BSS coexistence information field");
		is_ht40_allowed = 0;
	}

	/* 20/40 BSS Intolerant Channel Report element (zero or more times) */
	while (start + len - data >= 3 &&
	       data[0] == WLAN_EID_20_40_BSS_INTOLERANT && data[1] >= 1) {
		u8 ielen = data[1];

		if (ielen > start + len - data - 2) {
			wpa_printf(MSG_DEBUG,
				   "Truncated 20/40 BSS Intolerant Channel Report element");
			return;
		}
		ic_report = (struct ieee80211_2040_intol_chan_report *) data;
		wpa_printf(MSG_DEBUG,
			   "20/40 BSS Intolerant Channel Report: Operating Class %u",
			   ic_report->op_class);

		/* Go through the channel report to find any BSS there in the
		 * affected channel range */
		for (i = 0; i < ielen - 1; i++) {
			u8 chan = ic_report->variable[i];

			if (chan == iface->conf->channel)
				continue; /* matching own primary channel */
			if (is_40_allowed(iface, chan))
				continue; /* not within affected channels */
			hostapd_logger(hapd, mgmt->sa,
				       HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_DEBUG,
				       "20_40_INTOLERANT channel %d reported",
				       chan);
			is_ht40_allowed = 0;
		}

		data += 2 + ielen;
	}
	wpa_printf(MSG_DEBUG, "is_ht40_allowed=%d num_sta_ht40_intolerant=%d",
		   is_ht40_allowed, iface->num_sta_ht40_intolerant);

	if (!is_ht40_allowed &&
	    (iface->drv_flags & WPA_DRIVER_FLAGS_HT_2040_COEX)) {
		if (iface->conf->secondary_channel) {
			hostapd_logger(hapd, mgmt->sa,
				       HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_INFO,
				       "Switching to 20 MHz operation");
			iface->conf->secondary_channel = 0;
			ieee802_11_set_beacons(iface);
		}
		if (!iface->num_sta_ht40_intolerant &&
		    iface->conf->obss_interval) {
			unsigned int delay_time;
			delay_time = OVERLAPPING_BSS_TRANS_DELAY_FACTOR *
				iface->conf->obss_interval;
			eloop_cancel_timeout(ap_ht2040_timeout, hapd->iface,
					     NULL);
			eloop_register_timeout(delay_time, 0, ap_ht2040_timeout,
					       hapd->iface, NULL);
			wpa_printf(MSG_DEBUG,
				   "Reschedule HT 20/40 timeout to occur in %u seconds",
				   delay_time);
		}
	}
}


u16 copy_sta_ht_capab(struct hostapd_data *hapd, struct sta_info *sta,
		      const u8 *ht_capab)
{
	/*
	 * Disable HT caps for STAs associated to no-HT BSSes, or for stations
	 * that did not specify a valid WMM IE in the (Re)Association Request
	 * frame.
	 */
	if (!ht_capab || !(sta->flags & WLAN_STA_WMM) ||
	    !hapd->iconf->ieee80211n || hapd->conf->disable_11n) {
		sta->flags &= ~WLAN_STA_HT;
		os_free(sta->ht_capabilities);
		sta->ht_capabilities = NULL;
		return WLAN_STATUS_SUCCESS;
	}

	if (sta->ht_capabilities == NULL) {
		sta->ht_capabilities =
			os_zalloc(sizeof(struct ieee80211_ht_capabilities));
		if (sta->ht_capabilities == NULL)
			return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	sta->flags |= WLAN_STA_HT;
	os_memcpy(sta->ht_capabilities, ht_capab,
		  sizeof(struct ieee80211_ht_capabilities));

	return WLAN_STATUS_SUCCESS;
}


void ht40_intolerant_add(struct hostapd_iface *iface, struct sta_info *sta)
{
	if (iface->current_mode->mode != HOSTAPD_MODE_IEEE80211G)
		return;

	wpa_printf(MSG_INFO, "HT: Forty MHz Intolerant is set by STA " MACSTR
		   " in Association Request", MAC2STR(sta->addr));

	if (sta->ht40_intolerant_set)
		return;

	sta->ht40_intolerant_set = 1;
	iface->num_sta_ht40_intolerant++;
	eloop_cancel_timeout(ap_ht2040_timeout, iface, NULL);

	if (iface->conf->secondary_channel &&
	    (iface->drv_flags & WPA_DRIVER_FLAGS_HT_2040_COEX)) {
		iface->conf->secondary_channel = 0;
		ieee802_11_set_beacons(iface);
	}
}


void ht40_intolerant_remove(struct hostapd_iface *iface, struct sta_info *sta)
{
	if (!sta->ht40_intolerant_set)
		return;

	sta->ht40_intolerant_set = 0;
	iface->num_sta_ht40_intolerant--;

	if (iface->num_sta_ht40_intolerant == 0 &&
	    (iface->conf->ht_capab & HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET) &&
	    (iface->drv_flags & WPA_DRIVER_FLAGS_HT_2040_COEX)) {
		unsigned int delay_time = OVERLAPPING_BSS_TRANS_DELAY_FACTOR *
			iface->conf->obss_interval;
		wpa_printf(MSG_DEBUG,
			   "HT: Start 20->40 MHz transition timer (%d seconds)",
			   delay_time);
		eloop_cancel_timeout(ap_ht2040_timeout, iface, NULL);
		eloop_register_timeout(delay_time, 0, ap_ht2040_timeout,
				       iface, NULL);
	}
}


static void update_sta_ht(struct hostapd_data *hapd, struct sta_info *sta)
{
	u16 ht_capab;

	ht_capab = le_to_host16(sta->ht_capabilities->ht_capabilities_info);
	wpa_printf(MSG_DEBUG, "HT: STA " MACSTR " HT Capabilities Info: "
		   "0x%04x", MAC2STR(sta->addr), ht_capab);
	if ((ht_capab & HT_CAP_INFO_GREEN_FIELD) == 0) {
		if (!sta->no_ht_gf_set) {
			sta->no_ht_gf_set = 1;
			hapd->iface->num_sta_ht_no_gf++;
		}
		wpa_printf(MSG_DEBUG, "%s STA " MACSTR " - no greenfield, num "
			   "of non-gf stations %d",
			   __func__, MAC2STR(sta->addr),
			   hapd->iface->num_sta_ht_no_gf);
	}
	if ((ht_capab & HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET) == 0) {
		if (!sta->ht_20mhz_set) {
			sta->ht_20mhz_set = 1;
			hapd->iface->num_sta_ht_20mhz++;
		}
		wpa_printf(MSG_DEBUG, "%s STA " MACSTR " - 20 MHz HT, num of "
			   "20MHz HT STAs %d",
			   __func__, MAC2STR(sta->addr),
			   hapd->iface->num_sta_ht_20mhz);
	}

	if (ht_capab & HT_CAP_INFO_40MHZ_INTOLERANT)
		ht40_intolerant_add(hapd->iface, sta);
}


static void update_sta_no_ht(struct hostapd_data *hapd, struct sta_info *sta)
{
	if (!sta->no_ht_set) {
		sta->no_ht_set = 1;
		hapd->iface->num_sta_no_ht++;
	}
	if (hapd->iconf->ieee80211n) {
		wpa_printf(MSG_DEBUG, "%s STA " MACSTR " - no HT, num of "
			   "non-HT stations %d",
			   __func__, MAC2STR(sta->addr),
			   hapd->iface->num_sta_no_ht);
	}
}


void update_ht_state(struct hostapd_data *hapd, struct sta_info *sta)
{
	if ((sta->flags & WLAN_STA_HT) && sta->ht_capabilities)
		update_sta_ht(hapd, sta);
	else
		update_sta_no_ht(hapd, sta);

	if (hostapd_ht_operation_update(hapd->iface) > 0)
		ieee802_11_set_beacons(hapd->iface);
}


void hostapd_get_ht_capab(struct hostapd_data *hapd,
			  struct ieee80211_ht_capabilities *ht_cap,
			  struct ieee80211_ht_capabilities *neg_ht_cap)
{
	u16 cap;

	if (ht_cap == NULL)
		return;
	os_memcpy(neg_ht_cap, ht_cap, sizeof(*neg_ht_cap));
	cap = le_to_host16(neg_ht_cap->ht_capabilities_info);

	/*
	 * Mask out HT features we don't support, but don't overwrite
	 * non-symmetric features like STBC and SMPS. Just because
	 * we're not in dynamic SMPS mode the STA might still be.
	 */
	cap &= (hapd->iconf->ht_capab | HT_CAP_INFO_RX_STBC_MASK |
		HT_CAP_INFO_TX_STBC | HT_CAP_INFO_SMPS_MASK);

	/*
	 * STBC needs to be handled specially
	 * if we don't support RX STBC, mask out TX STBC in the STA's HT caps
	 * if we don't support TX STBC, mask out RX STBC in the STA's HT caps
	 */
	if (!(hapd->iconf->ht_capab & HT_CAP_INFO_RX_STBC_MASK))
		cap &= ~HT_CAP_INFO_TX_STBC;
	if (!(hapd->iconf->ht_capab & HT_CAP_INFO_TX_STBC))
		cap &= ~HT_CAP_INFO_RX_STBC_MASK;

	neg_ht_cap->ht_capabilities_info = host_to_le16(cap);
}


void ap_ht2040_timeout(void *eloop_data, void *user_data)
{
	struct hostapd_iface *iface = eloop_data;

	wpa_printf(MSG_INFO, "Switching to 40 MHz operation");

	iface->conf->secondary_channel = iface->secondary_ch;
	ieee802_11_set_beacons(iface);
}
