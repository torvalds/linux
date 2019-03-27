/*
 * IEEE 802.11 Common routines
 * Copyright (c) 2002-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "defs.h"
#include "wpa_common.h"
#include "drivers/driver.h"
#include "qca-vendor.h"
#include "ieee802_11_defs.h"
#include "ieee802_11_common.h"


static int ieee802_11_parse_vendor_specific(const u8 *pos, size_t elen,
					    struct ieee802_11_elems *elems,
					    int show_errors)
{
	unsigned int oui;

	/* first 3 bytes in vendor specific information element are the IEEE
	 * OUI of the vendor. The following byte is used a vendor specific
	 * sub-type. */
	if (elen < 4) {
		if (show_errors) {
			wpa_printf(MSG_MSGDUMP, "short vendor specific "
				   "information element ignored (len=%lu)",
				   (unsigned long) elen);
		}
		return -1;
	}

	oui = WPA_GET_BE24(pos);
	switch (oui) {
	case OUI_MICROSOFT:
		/* Microsoft/Wi-Fi information elements are further typed and
		 * subtyped */
		switch (pos[3]) {
		case 1:
			/* Microsoft OUI (00:50:F2) with OUI Type 1:
			 * real WPA information element */
			elems->wpa_ie = pos;
			elems->wpa_ie_len = elen;
			break;
		case WMM_OUI_TYPE:
			/* WMM information element */
			if (elen < 5) {
				wpa_printf(MSG_MSGDUMP, "short WMM "
					   "information element ignored "
					   "(len=%lu)",
					   (unsigned long) elen);
				return -1;
			}
			switch (pos[4]) {
			case WMM_OUI_SUBTYPE_INFORMATION_ELEMENT:
			case WMM_OUI_SUBTYPE_PARAMETER_ELEMENT:
				/*
				 * Share same pointer since only one of these
				 * is used and they start with same data.
				 * Length field can be used to distinguish the
				 * IEs.
				 */
				elems->wmm = pos;
				elems->wmm_len = elen;
				break;
			case WMM_OUI_SUBTYPE_TSPEC_ELEMENT:
				elems->wmm_tspec = pos;
				elems->wmm_tspec_len = elen;
				break;
			default:
				wpa_printf(MSG_EXCESSIVE, "unknown WMM "
					   "information element ignored "
					   "(subtype=%d len=%lu)",
					   pos[4], (unsigned long) elen);
				return -1;
			}
			break;
		case 4:
			/* Wi-Fi Protected Setup (WPS) IE */
			elems->wps_ie = pos;
			elems->wps_ie_len = elen;
			break;
		default:
			wpa_printf(MSG_EXCESSIVE, "Unknown Microsoft "
				   "information element ignored "
				   "(type=%d len=%lu)",
				   pos[3], (unsigned long) elen);
			return -1;
		}
		break;

	case OUI_WFA:
		switch (pos[3]) {
		case P2P_OUI_TYPE:
			/* Wi-Fi Alliance - P2P IE */
			elems->p2p = pos;
			elems->p2p_len = elen;
			break;
		case WFD_OUI_TYPE:
			/* Wi-Fi Alliance - WFD IE */
			elems->wfd = pos;
			elems->wfd_len = elen;
			break;
		case HS20_INDICATION_OUI_TYPE:
			/* Hotspot 2.0 */
			elems->hs20 = pos;
			elems->hs20_len = elen;
			break;
		case HS20_OSEN_OUI_TYPE:
			/* Hotspot 2.0 OSEN */
			elems->osen = pos;
			elems->osen_len = elen;
			break;
		case MBO_OUI_TYPE:
			/* MBO-OCE */
			elems->mbo = pos;
			elems->mbo_len = elen;
			break;
		case HS20_ROAMING_CONS_SEL_OUI_TYPE:
			/* Hotspot 2.0 Roaming Consortium Selection */
			elems->roaming_cons_sel = pos;
			elems->roaming_cons_sel_len = elen;
			break;
		default:
			wpa_printf(MSG_MSGDUMP, "Unknown WFA "
				   "information element ignored "
				   "(type=%d len=%lu)",
				   pos[3], (unsigned long) elen);
			return -1;
		}
		break;

	case OUI_BROADCOM:
		switch (pos[3]) {
		case VENDOR_HT_CAPAB_OUI_TYPE:
			elems->vendor_ht_cap = pos;
			elems->vendor_ht_cap_len = elen;
			break;
		case VENDOR_VHT_TYPE:
			if (elen > 4 &&
			    (pos[4] == VENDOR_VHT_SUBTYPE ||
			     pos[4] == VENDOR_VHT_SUBTYPE2)) {
				elems->vendor_vht = pos;
				elems->vendor_vht_len = elen;
			} else
				return -1;
			break;
		default:
			wpa_printf(MSG_EXCESSIVE, "Unknown Broadcom "
				   "information element ignored "
				   "(type=%d len=%lu)",
				   pos[3], (unsigned long) elen);
			return -1;
		}
		break;

	case OUI_QCA:
		switch (pos[3]) {
		case QCA_VENDOR_ELEM_P2P_PREF_CHAN_LIST:
			elems->pref_freq_list = pos;
			elems->pref_freq_list_len = elen;
			break;
		default:
			wpa_printf(MSG_EXCESSIVE,
				   "Unknown QCA information element ignored (type=%d len=%lu)",
				   pos[3], (unsigned long) elen);
			return -1;
		}
		break;

	default:
		wpa_printf(MSG_EXCESSIVE, "unknown vendor specific "
			   "information element ignored (vendor OUI "
			   "%02x:%02x:%02x len=%lu)",
			   pos[0], pos[1], pos[2], (unsigned long) elen);
		return -1;
	}

	return 0;
}


static int ieee802_11_parse_extension(const u8 *pos, size_t elen,
				      struct ieee802_11_elems *elems,
				      int show_errors)
{
	u8 ext_id;

	if (elen < 1) {
		if (show_errors) {
			wpa_printf(MSG_MSGDUMP,
				   "short information element (Ext)");
		}
		return -1;
	}

	ext_id = *pos++;
	elen--;

	switch (ext_id) {
	case WLAN_EID_EXT_ASSOC_DELAY_INFO:
		if (elen != 1)
			break;
		elems->assoc_delay_info = pos;
		break;
	case WLAN_EID_EXT_FILS_REQ_PARAMS:
		if (elen < 3)
			break;
		elems->fils_req_params = pos;
		elems->fils_req_params_len = elen;
		break;
	case WLAN_EID_EXT_FILS_KEY_CONFIRM:
		elems->fils_key_confirm = pos;
		elems->fils_key_confirm_len = elen;
		break;
	case WLAN_EID_EXT_FILS_SESSION:
		if (elen != FILS_SESSION_LEN)
			break;
		elems->fils_session = pos;
		break;
	case WLAN_EID_EXT_FILS_HLP_CONTAINER:
		if (elen < 2 * ETH_ALEN)
			break;
		elems->fils_hlp = pos;
		elems->fils_hlp_len = elen;
		break;
	case WLAN_EID_EXT_FILS_IP_ADDR_ASSIGN:
		if (elen < 1)
			break;
		elems->fils_ip_addr_assign = pos;
		elems->fils_ip_addr_assign_len = elen;
		break;
	case WLAN_EID_EXT_KEY_DELIVERY:
		if (elen < WPA_KEY_RSC_LEN)
			break;
		elems->key_delivery = pos;
		elems->key_delivery_len = elen;
		break;
	case WLAN_EID_EXT_FILS_WRAPPED_DATA:
		elems->fils_wrapped_data = pos;
		elems->fils_wrapped_data_len = elen;
		break;
	case WLAN_EID_EXT_FILS_PUBLIC_KEY:
		if (elen < 1)
			break;
		elems->fils_pk = pos;
		elems->fils_pk_len = elen;
		break;
	case WLAN_EID_EXT_FILS_NONCE:
		if (elen != FILS_NONCE_LEN)
			break;
		elems->fils_nonce = pos;
		break;
	case WLAN_EID_EXT_OWE_DH_PARAM:
		if (elen < 2)
			break;
		elems->owe_dh = pos;
		elems->owe_dh_len = elen;
		break;
	case WLAN_EID_EXT_PASSWORD_IDENTIFIER:
		elems->password_id = pos;
		elems->password_id_len = elen;
		break;
	default:
		if (show_errors) {
			wpa_printf(MSG_MSGDUMP,
				   "IEEE 802.11 element parsing ignored unknown element extension (ext_id=%u elen=%u)",
				   ext_id, (unsigned int) elen);
		}
		return -1;
	}

	return 0;
}


/**
 * ieee802_11_parse_elems - Parse information elements in management frames
 * @start: Pointer to the start of IEs
 * @len: Length of IE buffer in octets
 * @elems: Data structure for parsed elements
 * @show_errors: Whether to show parsing errors in debug log
 * Returns: Parsing result
 */
ParseRes ieee802_11_parse_elems(const u8 *start, size_t len,
				struct ieee802_11_elems *elems,
				int show_errors)
{
	size_t left = len;
	const u8 *pos = start;
	int unknown = 0;

	os_memset(elems, 0, sizeof(*elems));

	while (left >= 2) {
		u8 id, elen;

		id = *pos++;
		elen = *pos++;
		left -= 2;

		if (elen > left) {
			if (show_errors) {
				wpa_printf(MSG_DEBUG, "IEEE 802.11 element "
					   "parse failed (id=%d elen=%d "
					   "left=%lu)",
					   id, elen, (unsigned long) left);
				wpa_hexdump(MSG_MSGDUMP, "IEs", start, len);
			}
			return ParseFailed;
		}

		switch (id) {
		case WLAN_EID_SSID:
			if (elen > SSID_MAX_LEN) {
				wpa_printf(MSG_DEBUG,
					   "Ignored too long SSID element (elen=%u)",
					   elen);
				break;
			}
			elems->ssid = pos;
			elems->ssid_len = elen;
			break;
		case WLAN_EID_SUPP_RATES:
			elems->supp_rates = pos;
			elems->supp_rates_len = elen;
			break;
		case WLAN_EID_DS_PARAMS:
			if (elen < 1)
				break;
			elems->ds_params = pos;
			break;
		case WLAN_EID_CF_PARAMS:
		case WLAN_EID_TIM:
			break;
		case WLAN_EID_CHALLENGE:
			elems->challenge = pos;
			elems->challenge_len = elen;
			break;
		case WLAN_EID_ERP_INFO:
			if (elen < 1)
				break;
			elems->erp_info = pos;
			break;
		case WLAN_EID_EXT_SUPP_RATES:
			elems->ext_supp_rates = pos;
			elems->ext_supp_rates_len = elen;
			break;
		case WLAN_EID_VENDOR_SPECIFIC:
			if (ieee802_11_parse_vendor_specific(pos, elen,
							     elems,
							     show_errors))
				unknown++;
			break;
		case WLAN_EID_RSN:
			elems->rsn_ie = pos;
			elems->rsn_ie_len = elen;
			break;
		case WLAN_EID_PWR_CAPABILITY:
			if (elen < 2)
				break;
			elems->power_capab = pos;
			elems->power_capab_len = elen;
			break;
		case WLAN_EID_SUPPORTED_CHANNELS:
			elems->supp_channels = pos;
			elems->supp_channels_len = elen;
			break;
		case WLAN_EID_MOBILITY_DOMAIN:
			if (elen < sizeof(struct rsn_mdie))
				break;
			elems->mdie = pos;
			elems->mdie_len = elen;
			break;
		case WLAN_EID_FAST_BSS_TRANSITION:
			if (elen < sizeof(struct rsn_ftie))
				break;
			elems->ftie = pos;
			elems->ftie_len = elen;
			break;
		case WLAN_EID_TIMEOUT_INTERVAL:
			if (elen != 5)
				break;
			elems->timeout_int = pos;
			break;
		case WLAN_EID_HT_CAP:
			if (elen < sizeof(struct ieee80211_ht_capabilities))
				break;
			elems->ht_capabilities = pos;
			break;
		case WLAN_EID_HT_OPERATION:
			if (elen < sizeof(struct ieee80211_ht_operation))
				break;
			elems->ht_operation = pos;
			break;
		case WLAN_EID_MESH_CONFIG:
			elems->mesh_config = pos;
			elems->mesh_config_len = elen;
			break;
		case WLAN_EID_MESH_ID:
			elems->mesh_id = pos;
			elems->mesh_id_len = elen;
			break;
		case WLAN_EID_PEER_MGMT:
			elems->peer_mgmt = pos;
			elems->peer_mgmt_len = elen;
			break;
		case WLAN_EID_VHT_CAP:
			if (elen < sizeof(struct ieee80211_vht_capabilities))
				break;
			elems->vht_capabilities = pos;
			break;
		case WLAN_EID_VHT_OPERATION:
			if (elen < sizeof(struct ieee80211_vht_operation))
				break;
			elems->vht_operation = pos;
			break;
		case WLAN_EID_VHT_OPERATING_MODE_NOTIFICATION:
			if (elen != 1)
				break;
			elems->vht_opmode_notif = pos;
			break;
		case WLAN_EID_LINK_ID:
			if (elen < 18)
				break;
			elems->link_id = pos;
			break;
		case WLAN_EID_INTERWORKING:
			elems->interworking = pos;
			elems->interworking_len = elen;
			break;
		case WLAN_EID_QOS_MAP_SET:
			if (elen < 16)
				break;
			elems->qos_map_set = pos;
			elems->qos_map_set_len = elen;
			break;
		case WLAN_EID_EXT_CAPAB:
			elems->ext_capab = pos;
			elems->ext_capab_len = elen;
			break;
		case WLAN_EID_BSS_MAX_IDLE_PERIOD:
			if (elen < 3)
				break;
			elems->bss_max_idle_period = pos;
			break;
		case WLAN_EID_SSID_LIST:
			elems->ssid_list = pos;
			elems->ssid_list_len = elen;
			break;
		case WLAN_EID_AMPE:
			elems->ampe = pos;
			elems->ampe_len = elen;
			break;
		case WLAN_EID_MIC:
			elems->mic = pos;
			elems->mic_len = elen;
			/* after mic everything is encrypted, so stop. */
			left = elen;
			break;
		case WLAN_EID_MULTI_BAND:
			if (elems->mb_ies.nof_ies >= MAX_NOF_MB_IES_SUPPORTED) {
				wpa_printf(MSG_MSGDUMP,
					   "IEEE 802.11 element parse ignored MB IE (id=%d elen=%d)",
					   id, elen);
				break;
			}

			elems->mb_ies.ies[elems->mb_ies.nof_ies].ie = pos;
			elems->mb_ies.ies[elems->mb_ies.nof_ies].ie_len = elen;
			elems->mb_ies.nof_ies++;
			break;
		case WLAN_EID_SUPPORTED_OPERATING_CLASSES:
			elems->supp_op_classes = pos;
			elems->supp_op_classes_len = elen;
			break;
		case WLAN_EID_RRM_ENABLED_CAPABILITIES:
			elems->rrm_enabled = pos;
			elems->rrm_enabled_len = elen;
			break;
		case WLAN_EID_CAG_NUMBER:
			elems->cag_number = pos;
			elems->cag_number_len = elen;
			break;
		case WLAN_EID_AP_CSN:
			if (elen < 1)
				break;
			elems->ap_csn = pos;
			break;
		case WLAN_EID_FILS_INDICATION:
			if (elen < 2)
				break;
			elems->fils_indic = pos;
			elems->fils_indic_len = elen;
			break;
		case WLAN_EID_DILS:
			if (elen < 2)
				break;
			elems->dils = pos;
			elems->dils_len = elen;
			break;
		case WLAN_EID_FRAGMENT:
			/* TODO */
			break;
		case WLAN_EID_EXTENSION:
			if (ieee802_11_parse_extension(pos, elen, elems,
						       show_errors))
				unknown++;
			break;
		default:
			unknown++;
			if (!show_errors)
				break;
			wpa_printf(MSG_MSGDUMP, "IEEE 802.11 element parse "
				   "ignored unknown element (id=%d elen=%d)",
				   id, elen);
			break;
		}

		left -= elen;
		pos += elen;
	}

	if (left)
		return ParseFailed;

	return unknown ? ParseUnknown : ParseOK;
}


int ieee802_11_ie_count(const u8 *ies, size_t ies_len)
{
	int count = 0;
	const u8 *pos, *end;

	if (ies == NULL)
		return 0;

	pos = ies;
	end = ies + ies_len;

	while (end - pos >= 2) {
		if (2 + pos[1] > end - pos)
			break;
		count++;
		pos += 2 + pos[1];
	}

	return count;
}


struct wpabuf * ieee802_11_vendor_ie_concat(const u8 *ies, size_t ies_len,
					    u32 oui_type)
{
	struct wpabuf *buf;
	const u8 *end, *pos, *ie;

	pos = ies;
	end = ies + ies_len;
	ie = NULL;

	while (end - pos > 1) {
		if (2 + pos[1] > end - pos)
			return NULL;
		if (pos[0] == WLAN_EID_VENDOR_SPECIFIC && pos[1] >= 4 &&
		    WPA_GET_BE32(&pos[2]) == oui_type) {
			ie = pos;
			break;
		}
		pos += 2 + pos[1];
	}

	if (ie == NULL)
		return NULL; /* No specified vendor IE found */

	buf = wpabuf_alloc(ies_len);
	if (buf == NULL)
		return NULL;

	/*
	 * There may be multiple vendor IEs in the message, so need to
	 * concatenate their data fields.
	 */
	while (end - pos > 1) {
		if (2 + pos[1] > end - pos)
			break;
		if (pos[0] == WLAN_EID_VENDOR_SPECIFIC && pos[1] >= 4 &&
		    WPA_GET_BE32(&pos[2]) == oui_type)
			wpabuf_put_data(buf, pos + 6, pos[1] - 4);
		pos += 2 + pos[1];
	}

	return buf;
}


const u8 * get_hdr_bssid(const struct ieee80211_hdr *hdr, size_t len)
{
	u16 fc, type, stype;

	/*
	 * PS-Poll frames are 16 bytes. All other frames are
	 * 24 bytes or longer.
	 */
	if (len < 16)
		return NULL;

	fc = le_to_host16(hdr->frame_control);
	type = WLAN_FC_GET_TYPE(fc);
	stype = WLAN_FC_GET_STYPE(fc);

	switch (type) {
	case WLAN_FC_TYPE_DATA:
		if (len < 24)
			return NULL;
		switch (fc & (WLAN_FC_FROMDS | WLAN_FC_TODS)) {
		case WLAN_FC_FROMDS | WLAN_FC_TODS:
		case WLAN_FC_TODS:
			return hdr->addr1;
		case WLAN_FC_FROMDS:
			return hdr->addr2;
		default:
			return NULL;
		}
	case WLAN_FC_TYPE_CTRL:
		if (stype != WLAN_FC_STYPE_PSPOLL)
			return NULL;
		return hdr->addr1;
	case WLAN_FC_TYPE_MGMT:
		return hdr->addr3;
	default:
		return NULL;
	}
}


int hostapd_config_wmm_ac(struct hostapd_wmm_ac_params wmm_ac_params[],
			  const char *name, const char *val)
{
	int num, v;
	const char *pos;
	struct hostapd_wmm_ac_params *ac;

	/* skip 'wme_ac_' or 'wmm_ac_' prefix */
	pos = name + 7;
	if (os_strncmp(pos, "be_", 3) == 0) {
		num = 0;
		pos += 3;
	} else if (os_strncmp(pos, "bk_", 3) == 0) {
		num = 1;
		pos += 3;
	} else if (os_strncmp(pos, "vi_", 3) == 0) {
		num = 2;
		pos += 3;
	} else if (os_strncmp(pos, "vo_", 3) == 0) {
		num = 3;
		pos += 3;
	} else {
		wpa_printf(MSG_ERROR, "Unknown WMM name '%s'", pos);
		return -1;
	}

	ac = &wmm_ac_params[num];

	if (os_strcmp(pos, "aifs") == 0) {
		v = atoi(val);
		if (v < 1 || v > 255) {
			wpa_printf(MSG_ERROR, "Invalid AIFS value %d", v);
			return -1;
		}
		ac->aifs = v;
	} else if (os_strcmp(pos, "cwmin") == 0) {
		v = atoi(val);
		if (v < 0 || v > 15) {
			wpa_printf(MSG_ERROR, "Invalid cwMin value %d", v);
			return -1;
		}
		ac->cwmin = v;
	} else if (os_strcmp(pos, "cwmax") == 0) {
		v = atoi(val);
		if (v < 0 || v > 15) {
			wpa_printf(MSG_ERROR, "Invalid cwMax value %d", v);
			return -1;
		}
		ac->cwmax = v;
	} else if (os_strcmp(pos, "txop_limit") == 0) {
		v = atoi(val);
		if (v < 0 || v > 0xffff) {
			wpa_printf(MSG_ERROR, "Invalid txop value %d", v);
			return -1;
		}
		ac->txop_limit = v;
	} else if (os_strcmp(pos, "acm") == 0) {
		v = atoi(val);
		if (v < 0 || v > 1) {
			wpa_printf(MSG_ERROR, "Invalid acm value %d", v);
			return -1;
		}
		ac->admission_control_mandatory = v;
	} else {
		wpa_printf(MSG_ERROR, "Unknown wmm_ac_ field '%s'", pos);
		return -1;
	}

	return 0;
}


enum hostapd_hw_mode ieee80211_freq_to_chan(int freq, u8 *channel)
{
	u8 op_class;

	return ieee80211_freq_to_channel_ext(freq, 0, VHT_CHANWIDTH_USE_HT,
					     &op_class, channel);
}


/**
 * ieee80211_freq_to_channel_ext - Convert frequency into channel info
 * for HT40 and VHT. DFS channels are not covered.
 * @freq: Frequency (MHz) to convert
 * @sec_channel: 0 = non-HT40, 1 = sec. channel above, -1 = sec. channel below
 * @vht: VHT channel width (VHT_CHANWIDTH_*)
 * @op_class: Buffer for returning operating class
 * @channel: Buffer for returning channel number
 * Returns: hw_mode on success, NUM_HOSTAPD_MODES on failure
 */
enum hostapd_hw_mode ieee80211_freq_to_channel_ext(unsigned int freq,
						   int sec_channel, int vht,
						   u8 *op_class, u8 *channel)
{
	u8 vht_opclass;

	/* TODO: more operating classes */

	if (sec_channel > 1 || sec_channel < -1)
		return NUM_HOSTAPD_MODES;

	if (freq >= 2412 && freq <= 2472) {
		if ((freq - 2407) % 5)
			return NUM_HOSTAPD_MODES;

		if (vht)
			return NUM_HOSTAPD_MODES;

		/* 2.407 GHz, channels 1..13 */
		if (sec_channel == 1)
			*op_class = 83;
		else if (sec_channel == -1)
			*op_class = 84;
		else
			*op_class = 81;

		*channel = (freq - 2407) / 5;

		return HOSTAPD_MODE_IEEE80211G;
	}

	if (freq == 2484) {
		if (sec_channel || vht)
			return NUM_HOSTAPD_MODES;

		*op_class = 82; /* channel 14 */
		*channel = 14;

		return HOSTAPD_MODE_IEEE80211B;
	}

	if (freq >= 4900 && freq < 5000) {
		if ((freq - 4000) % 5)
			return NUM_HOSTAPD_MODES;
		*channel = (freq - 4000) / 5;
		*op_class = 0; /* TODO */
		return HOSTAPD_MODE_IEEE80211A;
	}

	switch (vht) {
	case VHT_CHANWIDTH_80MHZ:
		vht_opclass = 128;
		break;
	case VHT_CHANWIDTH_160MHZ:
		vht_opclass = 129;
		break;
	case VHT_CHANWIDTH_80P80MHZ:
		vht_opclass = 130;
		break;
	default:
		vht_opclass = 0;
		break;
	}

	/* 5 GHz, channels 36..48 */
	if (freq >= 5180 && freq <= 5240) {
		if ((freq - 5000) % 5)
			return NUM_HOSTAPD_MODES;

		if (vht_opclass)
			*op_class = vht_opclass;
		else if (sec_channel == 1)
			*op_class = 116;
		else if (sec_channel == -1)
			*op_class = 117;
		else
			*op_class = 115;

		*channel = (freq - 5000) / 5;

		return HOSTAPD_MODE_IEEE80211A;
	}

	/* 5 GHz, channels 52..64 */
	if (freq >= 5260 && freq <= 5320) {
		if ((freq - 5000) % 5)
			return NUM_HOSTAPD_MODES;

		if (vht_opclass)
			*op_class = vht_opclass;
		else if (sec_channel == 1)
			*op_class = 119;
		else if (sec_channel == -1)
			*op_class = 120;
		else
			*op_class = 118;

		*channel = (freq - 5000) / 5;

		return HOSTAPD_MODE_IEEE80211A;
	}

	/* 5 GHz, channels 149..169 */
	if (freq >= 5745 && freq <= 5845) {
		if ((freq - 5000) % 5)
			return NUM_HOSTAPD_MODES;

		if (vht_opclass)
			*op_class = vht_opclass;
		else if (sec_channel == 1)
			*op_class = 126;
		else if (sec_channel == -1)
			*op_class = 127;
		else if (freq <= 5805)
			*op_class = 124;
		else
			*op_class = 125;

		*channel = (freq - 5000) / 5;

		return HOSTAPD_MODE_IEEE80211A;
	}

	/* 5 GHz, channels 100..140 */
	if (freq >= 5000 && freq <= 5700) {
		if ((freq - 5000) % 5)
			return NUM_HOSTAPD_MODES;

		if (vht_opclass)
			*op_class = vht_opclass;
		else if (sec_channel == 1)
			*op_class = 122;
		else if (sec_channel == -1)
			*op_class = 123;
		else
			*op_class = 121;

		*channel = (freq - 5000) / 5;

		return HOSTAPD_MODE_IEEE80211A;
	}

	if (freq >= 5000 && freq < 5900) {
		if ((freq - 5000) % 5)
			return NUM_HOSTAPD_MODES;
		*channel = (freq - 5000) / 5;
		*op_class = 0; /* TODO */
		return HOSTAPD_MODE_IEEE80211A;
	}

	/* 56.16 GHz, channel 1..4 */
	if (freq >= 56160 + 2160 * 1 && freq <= 56160 + 2160 * 4) {
		if (sec_channel || vht)
			return NUM_HOSTAPD_MODES;

		*channel = (freq - 56160) / 2160;
		*op_class = 180;

		return HOSTAPD_MODE_IEEE80211AD;
	}

	return NUM_HOSTAPD_MODES;
}


static const char *const us_op_class_cc[] = {
	"US", "CA", NULL
};

static const char *const eu_op_class_cc[] = {
	"AL", "AM", "AT", "AZ", "BA", "BE", "BG", "BY", "CH", "CY", "CZ", "DE",
	"DK", "EE", "EL", "ES", "FI", "FR", "GE", "HR", "HU", "IE", "IS", "IT",
	"LI", "LT", "LU", "LV", "MD", "ME", "MK", "MT", "NL", "NO", "PL", "PT",
	"RO", "RS", "RU", "SE", "SI", "SK", "TR", "UA", "UK", NULL
};

static const char *const jp_op_class_cc[] = {
	"JP", NULL
};

static const char *const cn_op_class_cc[] = {
	"CN", NULL
};


static int country_match(const char *const cc[], const char *const country)
{
	int i;

	if (country == NULL)
		return 0;
	for (i = 0; cc[i]; i++) {
		if (cc[i][0] == country[0] && cc[i][1] == country[1])
			return 1;
	}

	return 0;
}


static int ieee80211_chan_to_freq_us(u8 op_class, u8 chan)
{
	switch (op_class) {
	case 12: /* channels 1..11 */
	case 32: /* channels 1..7; 40 MHz */
	case 33: /* channels 5..11; 40 MHz */
		if (chan < 1 || chan > 11)
			return -1;
		return 2407 + 5 * chan;
	case 1: /* channels 36,40,44,48 */
	case 2: /* channels 52,56,60,64; dfs */
	case 22: /* channels 36,44; 40 MHz */
	case 23: /* channels 52,60; 40 MHz */
	case 27: /* channels 40,48; 40 MHz */
	case 28: /* channels 56,64; 40 MHz */
		if (chan < 36 || chan > 64)
			return -1;
		return 5000 + 5 * chan;
	case 4: /* channels 100-144 */
	case 24: /* channels 100-140; 40 MHz */
		if (chan < 100 || chan > 144)
			return -1;
		return 5000 + 5 * chan;
	case 3: /* channels 149,153,157,161 */
	case 25: /* channels 149,157; 40 MHz */
	case 26: /* channels 149,157; 40 MHz */
	case 30: /* channels 153,161; 40 MHz */
	case 31: /* channels 153,161; 40 MHz */
		if (chan < 149 || chan > 161)
			return -1;
		return 5000 + 5 * chan;
	case 5: /* channels 149,153,157,161,165 */
		if (chan < 149 || chan > 165)
			return -1;
		return 5000 + 5 * chan;
	case 34: /* 60 GHz band, channels 1..3 */
		if (chan < 1 || chan > 3)
			return -1;
		return 56160 + 2160 * chan;
	}
	return -1;
}


static int ieee80211_chan_to_freq_eu(u8 op_class, u8 chan)
{
	switch (op_class) {
	case 4: /* channels 1..13 */
	case 11: /* channels 1..9; 40 MHz */
	case 12: /* channels 5..13; 40 MHz */
		if (chan < 1 || chan > 13)
			return -1;
		return 2407 + 5 * chan;
	case 1: /* channels 36,40,44,48 */
	case 2: /* channels 52,56,60,64; dfs */
	case 5: /* channels 36,44; 40 MHz */
	case 6: /* channels 52,60; 40 MHz */
	case 8: /* channels 40,48; 40 MHz */
	case 9: /* channels 56,64; 40 MHz */
		if (chan < 36 || chan > 64)
			return -1;
		return 5000 + 5 * chan;
	case 3: /* channels 100-140 */
	case 7: /* channels 100-132; 40 MHz */
	case 10: /* channels 104-136; 40 MHz */
	case 16: /* channels 100-140 */
		if (chan < 100 || chan > 140)
			return -1;
		return 5000 + 5 * chan;
	case 17: /* channels 149,153,157,161,165,169 */
		if (chan < 149 || chan > 169)
			return -1;
		return 5000 + 5 * chan;
	case 18: /* 60 GHz band, channels 1..4 */
		if (chan < 1 || chan > 4)
			return -1;
		return 56160 + 2160 * chan;
	}
	return -1;
}


static int ieee80211_chan_to_freq_jp(u8 op_class, u8 chan)
{
	switch (op_class) {
	case 30: /* channels 1..13 */
	case 56: /* channels 1..9; 40 MHz */
	case 57: /* channels 5..13; 40 MHz */
		if (chan < 1 || chan > 13)
			return -1;
		return 2407 + 5 * chan;
	case 31: /* channel 14 */
		if (chan != 14)
			return -1;
		return 2414 + 5 * chan;
	case 1: /* channels 34,38,42,46(old) or 36,40,44,48 */
	case 32: /* channels 52,56,60,64 */
	case 33: /* channels 52,56,60,64 */
	case 36: /* channels 36,44; 40 MHz */
	case 37: /* channels 52,60; 40 MHz */
	case 38: /* channels 52,60; 40 MHz */
	case 41: /* channels 40,48; 40 MHz */
	case 42: /* channels 56,64; 40 MHz */
	case 43: /* channels 56,64; 40 MHz */
		if (chan < 34 || chan > 64)
			return -1;
		return 5000 + 5 * chan;
	case 34: /* channels 100-140 */
	case 35: /* channels 100-140 */
	case 39: /* channels 100-132; 40 MHz */
	case 40: /* channels 100-132; 40 MHz */
	case 44: /* channels 104-136; 40 MHz */
	case 45: /* channels 104-136; 40 MHz */
	case 58: /* channels 100-140 */
		if (chan < 100 || chan > 140)
			return -1;
		return 5000 + 5 * chan;
	case 59: /* 60 GHz band, channels 1..4 */
		if (chan < 1 || chan > 3)
			return -1;
		return 56160 + 2160 * chan;
	}
	return -1;
}


static int ieee80211_chan_to_freq_cn(u8 op_class, u8 chan)
{
	switch (op_class) {
	case 7: /* channels 1..13 */
	case 8: /* channels 1..9; 40 MHz */
	case 9: /* channels 5..13; 40 MHz */
		if (chan < 1 || chan > 13)
			return -1;
		return 2407 + 5 * chan;
	case 1: /* channels 36,40,44,48 */
	case 2: /* channels 52,56,60,64; dfs */
	case 4: /* channels 36,44; 40 MHz */
	case 5: /* channels 52,60; 40 MHz */
		if (chan < 36 || chan > 64)
			return -1;
		return 5000 + 5 * chan;
	case 3: /* channels 149,153,157,161,165 */
	case 6: /* channels 149,157; 40 MHz */
		if (chan < 149 || chan > 165)
			return -1;
		return 5000 + 5 * chan;
	}
	return -1;
}


static int ieee80211_chan_to_freq_global(u8 op_class, u8 chan)
{
	/* Table E-4 in IEEE Std 802.11-2012 - Global operating classes */
	switch (op_class) {
	case 81:
		/* channels 1..13 */
		if (chan < 1 || chan > 13)
			return -1;
		return 2407 + 5 * chan;
	case 82:
		/* channel 14 */
		if (chan != 14)
			return -1;
		return 2414 + 5 * chan;
	case 83: /* channels 1..9; 40 MHz */
	case 84: /* channels 5..13; 40 MHz */
		if (chan < 1 || chan > 13)
			return -1;
		return 2407 + 5 * chan;
	case 115: /* channels 36,40,44,48; indoor only */
	case 116: /* channels 36,44; 40 MHz; indoor only */
	case 117: /* channels 40,48; 40 MHz; indoor only */
	case 118: /* channels 52,56,60,64; dfs */
	case 119: /* channels 52,60; 40 MHz; dfs */
	case 120: /* channels 56,64; 40 MHz; dfs */
		if (chan < 36 || chan > 64)
			return -1;
		return 5000 + 5 * chan;
	case 121: /* channels 100-140 */
	case 122: /* channels 100-142; 40 MHz */
	case 123: /* channels 104-136; 40 MHz */
		if (chan < 100 || chan > 140)
			return -1;
		return 5000 + 5 * chan;
	case 124: /* channels 149,153,157,161 */
	case 126: /* channels 149,157; 40 MHz */
	case 127: /* channels 153,161; 40 MHz */
		if (chan < 149 || chan > 161)
			return -1;
		return 5000 + 5 * chan;
	case 125: /* channels 149,153,157,161,165,169 */
		if (chan < 149 || chan > 169)
			return -1;
		return 5000 + 5 * chan;
	case 128: /* center freqs 42, 58, 106, 122, 138, 155; 80 MHz */
	case 130: /* center freqs 42, 58, 106, 122, 138, 155; 80 MHz */
		if (chan < 36 || chan > 161)
			return -1;
		return 5000 + 5 * chan;
	case 129: /* center freqs 50, 114; 160 MHz */
		if (chan < 36 || chan > 128)
			return -1;
		return 5000 + 5 * chan;
	case 180: /* 60 GHz band, channels 1..4 */
		if (chan < 1 || chan > 4)
			return -1;
		return 56160 + 2160 * chan;
	}
	return -1;
}

/**
 * ieee80211_chan_to_freq - Convert channel info to frequency
 * @country: Country code, if known; otherwise, global operating class is used
 * @op_class: Operating class
 * @chan: Channel number
 * Returns: Frequency in MHz or -1 if the specified channel is unknown
 */
int ieee80211_chan_to_freq(const char *country, u8 op_class, u8 chan)
{
	int freq;

	if (country_match(us_op_class_cc, country)) {
		freq = ieee80211_chan_to_freq_us(op_class, chan);
		if (freq > 0)
			return freq;
	}

	if (country_match(eu_op_class_cc, country)) {
		freq = ieee80211_chan_to_freq_eu(op_class, chan);
		if (freq > 0)
			return freq;
	}

	if (country_match(jp_op_class_cc, country)) {
		freq = ieee80211_chan_to_freq_jp(op_class, chan);
		if (freq > 0)
			return freq;
	}

	if (country_match(cn_op_class_cc, country)) {
		freq = ieee80211_chan_to_freq_cn(op_class, chan);
		if (freq > 0)
			return freq;
	}

	return ieee80211_chan_to_freq_global(op_class, chan);
}


int ieee80211_is_dfs(int freq, const struct hostapd_hw_modes *modes,
		     u16 num_modes)
{
	int i, j;

	if (!modes || !num_modes)
		return (freq >= 5260 && freq <= 5320) ||
			(freq >= 5500 && freq <= 5700);

	for (i = 0; i < num_modes; i++) {
		for (j = 0; j < modes[i].num_channels; j++) {
			if (modes[i].channels[j].freq == freq &&
			    (modes[i].channels[j].flag & HOSTAPD_CHAN_RADAR))
				return 1;
		}
	}

	return 0;
}


static int is_11b(u8 rate)
{
	return rate == 0x02 || rate == 0x04 || rate == 0x0b || rate == 0x16;
}


int supp_rates_11b_only(struct ieee802_11_elems *elems)
{
	int num_11b = 0, num_others = 0;
	int i;

	if (elems->supp_rates == NULL && elems->ext_supp_rates == NULL)
		return 0;

	for (i = 0; elems->supp_rates && i < elems->supp_rates_len; i++) {
		if (is_11b(elems->supp_rates[i]))
			num_11b++;
		else
			num_others++;
	}

	for (i = 0; elems->ext_supp_rates && i < elems->ext_supp_rates_len;
	     i++) {
		if (is_11b(elems->ext_supp_rates[i]))
			num_11b++;
		else
			num_others++;
	}

	return num_11b > 0 && num_others == 0;
}


const char * fc2str(u16 fc)
{
	u16 stype = WLAN_FC_GET_STYPE(fc);
#define C2S(x) case x: return #x;

	switch (WLAN_FC_GET_TYPE(fc)) {
	case WLAN_FC_TYPE_MGMT:
		switch (stype) {
		C2S(WLAN_FC_STYPE_ASSOC_REQ)
		C2S(WLAN_FC_STYPE_ASSOC_RESP)
		C2S(WLAN_FC_STYPE_REASSOC_REQ)
		C2S(WLAN_FC_STYPE_REASSOC_RESP)
		C2S(WLAN_FC_STYPE_PROBE_REQ)
		C2S(WLAN_FC_STYPE_PROBE_RESP)
		C2S(WLAN_FC_STYPE_BEACON)
		C2S(WLAN_FC_STYPE_ATIM)
		C2S(WLAN_FC_STYPE_DISASSOC)
		C2S(WLAN_FC_STYPE_AUTH)
		C2S(WLAN_FC_STYPE_DEAUTH)
		C2S(WLAN_FC_STYPE_ACTION)
		}
		break;
	case WLAN_FC_TYPE_CTRL:
		switch (stype) {
		C2S(WLAN_FC_STYPE_PSPOLL)
		C2S(WLAN_FC_STYPE_RTS)
		C2S(WLAN_FC_STYPE_CTS)
		C2S(WLAN_FC_STYPE_ACK)
		C2S(WLAN_FC_STYPE_CFEND)
		C2S(WLAN_FC_STYPE_CFENDACK)
		}
		break;
	case WLAN_FC_TYPE_DATA:
		switch (stype) {
		C2S(WLAN_FC_STYPE_DATA)
		C2S(WLAN_FC_STYPE_DATA_CFACK)
		C2S(WLAN_FC_STYPE_DATA_CFPOLL)
		C2S(WLAN_FC_STYPE_DATA_CFACKPOLL)
		C2S(WLAN_FC_STYPE_NULLFUNC)
		C2S(WLAN_FC_STYPE_CFACK)
		C2S(WLAN_FC_STYPE_CFPOLL)
		C2S(WLAN_FC_STYPE_CFACKPOLL)
		C2S(WLAN_FC_STYPE_QOS_DATA)
		C2S(WLAN_FC_STYPE_QOS_DATA_CFACK)
		C2S(WLAN_FC_STYPE_QOS_DATA_CFPOLL)
		C2S(WLAN_FC_STYPE_QOS_DATA_CFACKPOLL)
		C2S(WLAN_FC_STYPE_QOS_NULL)
		C2S(WLAN_FC_STYPE_QOS_CFPOLL)
		C2S(WLAN_FC_STYPE_QOS_CFACKPOLL)
		}
		break;
	}
	return "WLAN_FC_TYPE_UNKNOWN";
#undef C2S
}


int mb_ies_info_by_ies(struct mb_ies_info *info, const u8 *ies_buf,
		       size_t ies_len)
{
	os_memset(info, 0, sizeof(*info));

	while (ies_buf && ies_len >= 2 &&
	       info->nof_ies < MAX_NOF_MB_IES_SUPPORTED) {
		size_t len = 2 + ies_buf[1];

		if (len > ies_len) {
			wpa_hexdump(MSG_DEBUG, "Truncated IEs",
				    ies_buf, ies_len);
			return -1;
		}

		if (ies_buf[0] == WLAN_EID_MULTI_BAND) {
			wpa_printf(MSG_DEBUG, "MB IE of %zu bytes found", len);
			info->ies[info->nof_ies].ie = ies_buf + 2;
			info->ies[info->nof_ies].ie_len = ies_buf[1];
			info->nof_ies++;
		}

		ies_len -= len;
		ies_buf += len;
	}

	return 0;
}


struct wpabuf * mb_ies_by_info(struct mb_ies_info *info)
{
	struct wpabuf *mb_ies = NULL;

	WPA_ASSERT(info != NULL);

	if (info->nof_ies) {
		u8 i;
		size_t mb_ies_size = 0;

		for (i = 0; i < info->nof_ies; i++)
			mb_ies_size += 2 + info->ies[i].ie_len;

		mb_ies = wpabuf_alloc(mb_ies_size);
		if (mb_ies) {
			for (i = 0; i < info->nof_ies; i++) {
				wpabuf_put_u8(mb_ies, WLAN_EID_MULTI_BAND);
				wpabuf_put_u8(mb_ies, info->ies[i].ie_len);
				wpabuf_put_data(mb_ies,
						info->ies[i].ie,
						info->ies[i].ie_len);
			}
		}
	}

	return mb_ies;
}


const struct oper_class_map global_op_class[] = {
	{ HOSTAPD_MODE_IEEE80211G, 81, 1, 13, 1, BW20, P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211G, 82, 14, 14, 1, BW20, NO_P2P_SUPP },

	/* Do not enable HT40 on 2.4 GHz for P2P use for now */
	{ HOSTAPD_MODE_IEEE80211G, 83, 1, 9, 1, BW40PLUS, NO_P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211G, 84, 5, 13, 1, BW40MINUS, NO_P2P_SUPP },

	{ HOSTAPD_MODE_IEEE80211A, 115, 36, 48, 4, BW20, P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211A, 116, 36, 44, 8, BW40PLUS, P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211A, 117, 40, 48, 8, BW40MINUS, P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211A, 118, 52, 64, 4, BW20, NO_P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211A, 119, 52, 60, 8, BW40PLUS, NO_P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211A, 120, 56, 64, 8, BW40MINUS, NO_P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211A, 121, 100, 140, 4, BW20, NO_P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211A, 122, 100, 132, 8, BW40PLUS, NO_P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211A, 123, 104, 136, 8, BW40MINUS, NO_P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211A, 124, 149, 161, 4, BW20, P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211A, 125, 149, 169, 4, BW20, P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211A, 126, 149, 157, 8, BW40PLUS, P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211A, 127, 153, 161, 8, BW40MINUS, P2P_SUPP },

	/*
	 * IEEE P802.11ac/D7.0 Table E-4 actually talks about channel center
	 * frequency index 42, 58, 106, 122, 138, 155 with channel spacing of
	 * 80 MHz, but currently use the following definition for simplicity
	 * (these center frequencies are not actual channels, which makes
	 * wpas_p2p_allow_channel() fail). wpas_p2p_verify_80mhz() should take
	 * care of removing invalid channels.
	 */
	{ HOSTAPD_MODE_IEEE80211A, 128, 36, 161, 4, BW80, P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211A, 129, 50, 114, 16, BW160, P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211A, 130, 36, 161, 4, BW80P80, P2P_SUPP },
	{ HOSTAPD_MODE_IEEE80211AD, 180, 1, 4, 1, BW2160, P2P_SUPP },
	{ -1, 0, 0, 0, 0, BW20, NO_P2P_SUPP }
};


static enum phy_type ieee80211_phy_type_by_freq(int freq)
{
	enum hostapd_hw_mode hw_mode;
	u8 channel;

	hw_mode = ieee80211_freq_to_chan(freq, &channel);

	switch (hw_mode) {
	case HOSTAPD_MODE_IEEE80211A:
		return PHY_TYPE_OFDM;
	case HOSTAPD_MODE_IEEE80211B:
		return PHY_TYPE_HRDSSS;
	case HOSTAPD_MODE_IEEE80211G:
		return PHY_TYPE_ERP;
	case HOSTAPD_MODE_IEEE80211AD:
		return PHY_TYPE_DMG;
	default:
		return PHY_TYPE_UNSPECIFIED;
	};
}


/* ieee80211_get_phy_type - Derive the phy type by freq and bandwidth */
enum phy_type ieee80211_get_phy_type(int freq, int ht, int vht)
{
	if (vht)
		return PHY_TYPE_VHT;
	if (ht)
		return PHY_TYPE_HT;

	return ieee80211_phy_type_by_freq(freq);
}


size_t global_op_class_size = ARRAY_SIZE(global_op_class);


/**
 * get_ie - Fetch a specified information element from IEs buffer
 * @ies: Information elements buffer
 * @len: Information elements buffer length
 * @eid: Information element identifier (WLAN_EID_*)
 * Returns: Pointer to the information element (id field) or %NULL if not found
 *
 * This function returns the first matching information element in the IEs
 * buffer or %NULL in case the element is not found.
 */
const u8 * get_ie(const u8 *ies, size_t len, u8 eid)
{
	const u8 *end;

	if (!ies)
		return NULL;

	end = ies + len;

	while (end - ies > 1) {
		if (2 + ies[1] > end - ies)
			break;

		if (ies[0] == eid)
			return ies;

		ies += 2 + ies[1];
	}

	return NULL;
}


/**
 * get_ie_ext - Fetch a specified extended information element from IEs buffer
 * @ies: Information elements buffer
 * @len: Information elements buffer length
 * @ext: Information element extension identifier (WLAN_EID_EXT_*)
 * Returns: Pointer to the information element (id field) or %NULL if not found
 *
 * This function returns the first matching information element in the IEs
 * buffer or %NULL in case the element is not found.
 */
const u8 * get_ie_ext(const u8 *ies, size_t len, u8 ext)
{
	const u8 *end;

	if (!ies)
		return NULL;

	end = ies + len;

	while (end - ies > 1) {
		if (2 + ies[1] > end - ies)
			break;

		if (ies[0] == WLAN_EID_EXTENSION && ies[1] >= 1 &&
		    ies[2] == ext)
			return ies;

		ies += 2 + ies[1];
	}

	return NULL;
}


size_t mbo_add_ie(u8 *buf, size_t len, const u8 *attr, size_t attr_len)
{
	/*
	 * MBO IE requires 6 bytes without the attributes: EID (1), length (1),
	 * OUI (3), OUI type (1).
	 */
	if (len < 6 + attr_len) {
		wpa_printf(MSG_DEBUG,
			   "MBO: Not enough room in buffer for MBO IE: buf len = %zu, attr_len = %zu",
			   len, attr_len);
		return 0;
	}

	*buf++ = WLAN_EID_VENDOR_SPECIFIC;
	*buf++ = attr_len + 4;
	WPA_PUT_BE24(buf, OUI_WFA);
	buf += 3;
	*buf++ = MBO_OUI_TYPE;
	os_memcpy(buf, attr, attr_len);

	return 6 + attr_len;
}


static const struct country_op_class us_op_class[] = {
	{ 1, 115 },
	{ 2, 118 },
	{ 3, 124 },
	{ 4, 121 },
	{ 5, 125 },
	{ 12, 81 },
	{ 22, 116 },
	{ 23, 119 },
	{ 24, 122 },
	{ 25, 126 },
	{ 26, 126 },
	{ 27, 117 },
	{ 28, 120 },
	{ 29, 123 },
	{ 30, 127 },
	{ 31, 127 },
	{ 32, 83 },
	{ 33, 84 },
	{ 34, 180 },
};

static const struct country_op_class eu_op_class[] = {
	{ 1, 115 },
	{ 2, 118 },
	{ 3, 121 },
	{ 4, 81 },
	{ 5, 116 },
	{ 6, 119 },
	{ 7, 122 },
	{ 8, 117 },
	{ 9, 120 },
	{ 10, 123 },
	{ 11, 83 },
	{ 12, 84 },
	{ 17, 125 },
	{ 18, 180 },
};

static const struct country_op_class jp_op_class[] = {
	{ 1, 115 },
	{ 30, 81 },
	{ 31, 82 },
	{ 32, 118 },
	{ 33, 118 },
	{ 34, 121 },
	{ 35, 121 },
	{ 36, 116 },
	{ 37, 119 },
	{ 38, 119 },
	{ 39, 122 },
	{ 40, 122 },
	{ 41, 117 },
	{ 42, 120 },
	{ 43, 120 },
	{ 44, 123 },
	{ 45, 123 },
	{ 56, 83 },
	{ 57, 84 },
	{ 58, 121 },
	{ 59, 180 },
};

static const struct country_op_class cn_op_class[] = {
	{ 1, 115 },
	{ 2, 118 },
	{ 3, 125 },
	{ 4, 116 },
	{ 5, 119 },
	{ 6, 126 },
	{ 7, 81 },
	{ 8, 83 },
	{ 9, 84 },
};

static u8
global_op_class_from_country_array(u8 op_class, size_t array_size,
				   const struct country_op_class *country_array)
{
	size_t i;

	for (i = 0; i < array_size; i++) {
		if (country_array[i].country_op_class == op_class)
			return country_array[i].global_op_class;
	}

	return 0;
}


u8 country_to_global_op_class(const char *country, u8 op_class)
{
	const struct country_op_class *country_array;
	size_t size;
	u8 g_op_class;

	if (country_match(us_op_class_cc, country)) {
		country_array = us_op_class;
		size = ARRAY_SIZE(us_op_class);
	} else if (country_match(eu_op_class_cc, country)) {
		country_array = eu_op_class;
		size = ARRAY_SIZE(eu_op_class);
	} else if (country_match(jp_op_class_cc, country)) {
		country_array = jp_op_class;
		size = ARRAY_SIZE(jp_op_class);
	} else if (country_match(cn_op_class_cc, country)) {
		country_array = cn_op_class;
		size = ARRAY_SIZE(cn_op_class);
	} else {
		/*
		 * Countries that do not match any of the above countries use
		 * global operating classes
		 */
		return op_class;
	}

	g_op_class = global_op_class_from_country_array(op_class, size,
							country_array);

	/*
	 * If the given operating class did not match any of the country's
	 * operating classes, assume that global operating class is used.
	 */
	return g_op_class ? g_op_class : op_class;
}


const struct oper_class_map * get_oper_class(const char *country, u8 op_class)
{
	const struct oper_class_map *op;

	if (country)
		op_class = country_to_global_op_class(country, op_class);

	op = &global_op_class[0];
	while (op->op_class && op->op_class != op_class)
		op++;

	if (!op->op_class)
		return NULL;

	return op;
}


int ieee802_11_parse_candidate_list(const char *pos, u8 *nei_rep,
				    size_t nei_rep_len)
{
	u8 *nei_pos = nei_rep;
	const char *end;

	/*
	 * BSS Transition Candidate List Entries - Neighbor Report elements
	 * neighbor=<BSSID>,<BSSID Information>,<Operating Class>,
	 * <Channel Number>,<PHY Type>[,<hexdump of Optional Subelements>]
	 */
	while (pos) {
		u8 *nei_start;
		long int val;
		char *endptr, *tmp;

		pos = os_strstr(pos, " neighbor=");
		if (!pos)
			break;
		if (nei_pos + 15 > nei_rep + nei_rep_len) {
			wpa_printf(MSG_DEBUG,
				   "Not enough room for additional neighbor");
			return -1;
		}
		pos += 10;

		nei_start = nei_pos;
		*nei_pos++ = WLAN_EID_NEIGHBOR_REPORT;
		nei_pos++; /* length to be filled in */

		if (hwaddr_aton(pos, nei_pos)) {
			wpa_printf(MSG_DEBUG, "Invalid BSSID");
			return -1;
		}
		nei_pos += ETH_ALEN;
		pos += 17;
		if (*pos != ',') {
			wpa_printf(MSG_DEBUG, "Missing BSSID Information");
			return -1;
		}
		pos++;

		val = strtol(pos, &endptr, 0);
		WPA_PUT_LE32(nei_pos, val);
		nei_pos += 4;
		if (*endptr != ',') {
			wpa_printf(MSG_DEBUG, "Missing Operating Class");
			return -1;
		}
		pos = endptr + 1;

		*nei_pos++ = atoi(pos); /* Operating Class */
		pos = os_strchr(pos, ',');
		if (pos == NULL) {
			wpa_printf(MSG_DEBUG, "Missing Channel Number");
			return -1;
		}
		pos++;

		*nei_pos++ = atoi(pos); /* Channel Number */
		pos = os_strchr(pos, ',');
		if (pos == NULL) {
			wpa_printf(MSG_DEBUG, "Missing PHY Type");
			return -1;
		}
		pos++;

		*nei_pos++ = atoi(pos); /* PHY Type */
		end = os_strchr(pos, ' ');
		tmp = os_strchr(pos, ',');
		if (tmp && (!end || tmp < end)) {
			/* Optional Subelements (hexdump) */
			size_t len;

			pos = tmp + 1;
			end = os_strchr(pos, ' ');
			if (end)
				len = end - pos;
			else
				len = os_strlen(pos);
			if (nei_pos + len / 2 > nei_rep + nei_rep_len) {
				wpa_printf(MSG_DEBUG,
					   "Not enough room for neighbor subelements");
				return -1;
			}
			if (len & 0x01 ||
			    hexstr2bin(pos, nei_pos, len / 2) < 0) {
				wpa_printf(MSG_DEBUG,
					   "Invalid neighbor subelement info");
				return -1;
			}
			nei_pos += len / 2;
			pos = end;
		}

		nei_start[1] = nei_pos - nei_start - 2;
	}

	return nei_pos - nei_rep;
}
