/*
 * wpa_supplicant - WPA/RSN IE and KDE processing
 * Copyright (c) 2003-2018, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "wpa.h"
#include "pmksa_cache.h"
#include "common/ieee802_11_defs.h"
#include "wpa_i.h"
#include "wpa_ie.h"


/**
 * wpa_parse_wpa_ie - Parse WPA/RSN IE
 * @wpa_ie: Pointer to WPA or RSN IE
 * @wpa_ie_len: Length of the WPA/RSN IE
 * @data: Pointer to data area for parsing results
 * Returns: 0 on success, -1 on failure
 *
 * Parse the contents of WPA or RSN IE and write the parsed data into data.
 */
int wpa_parse_wpa_ie(const u8 *wpa_ie, size_t wpa_ie_len,
		     struct wpa_ie_data *data)
{
	if (wpa_ie_len >= 1 && wpa_ie[0] == WLAN_EID_RSN)
		return wpa_parse_wpa_ie_rsn(wpa_ie, wpa_ie_len, data);
	if (wpa_ie_len >= 6 && wpa_ie[0] == WLAN_EID_VENDOR_SPECIFIC &&
	    wpa_ie[1] >= 4 && WPA_GET_BE32(&wpa_ie[2]) == OSEN_IE_VENDOR_TYPE)
		return wpa_parse_wpa_ie_rsn(wpa_ie, wpa_ie_len, data);
	else
		return wpa_parse_wpa_ie_wpa(wpa_ie, wpa_ie_len, data);
}


static int wpa_gen_wpa_ie_wpa(u8 *wpa_ie, size_t wpa_ie_len,
			      int pairwise_cipher, int group_cipher,
			      int key_mgmt)
{
	u8 *pos;
	struct wpa_ie_hdr *hdr;
	u32 suite;

	if (wpa_ie_len < sizeof(*hdr) + WPA_SELECTOR_LEN +
	    2 + WPA_SELECTOR_LEN + 2 + WPA_SELECTOR_LEN)
		return -1;

	hdr = (struct wpa_ie_hdr *) wpa_ie;
	hdr->elem_id = WLAN_EID_VENDOR_SPECIFIC;
	RSN_SELECTOR_PUT(hdr->oui, WPA_OUI_TYPE);
	WPA_PUT_LE16(hdr->version, WPA_VERSION);
	pos = (u8 *) (hdr + 1);

	suite = wpa_cipher_to_suite(WPA_PROTO_WPA, group_cipher);
	if (suite == 0) {
		wpa_printf(MSG_WARNING, "Invalid group cipher (%d).",
			   group_cipher);
		return -1;
	}
	RSN_SELECTOR_PUT(pos, suite);
	pos += WPA_SELECTOR_LEN;

	*pos++ = 1;
	*pos++ = 0;
	suite = wpa_cipher_to_suite(WPA_PROTO_WPA, pairwise_cipher);
	if (suite == 0 ||
	    (!wpa_cipher_valid_pairwise(pairwise_cipher) &&
	     pairwise_cipher != WPA_CIPHER_NONE)) {
		wpa_printf(MSG_WARNING, "Invalid pairwise cipher (%d).",
			   pairwise_cipher);
		return -1;
	}
	RSN_SELECTOR_PUT(pos, suite);
	pos += WPA_SELECTOR_LEN;

	*pos++ = 1;
	*pos++ = 0;
	if (key_mgmt == WPA_KEY_MGMT_IEEE8021X) {
		RSN_SELECTOR_PUT(pos, WPA_AUTH_KEY_MGMT_UNSPEC_802_1X);
	} else if (key_mgmt == WPA_KEY_MGMT_PSK) {
		RSN_SELECTOR_PUT(pos, WPA_AUTH_KEY_MGMT_PSK_OVER_802_1X);
	} else if (key_mgmt == WPA_KEY_MGMT_WPA_NONE) {
		RSN_SELECTOR_PUT(pos, WPA_AUTH_KEY_MGMT_NONE);
	} else if (key_mgmt == WPA_KEY_MGMT_CCKM) {
		RSN_SELECTOR_PUT(pos, WPA_AUTH_KEY_MGMT_CCKM);
	} else {
		wpa_printf(MSG_WARNING, "Invalid key management type (%d).",
			   key_mgmt);
		return -1;
	}
	pos += WPA_SELECTOR_LEN;

	/* WPA Capabilities; use defaults, so no need to include it */

	hdr->len = (pos - wpa_ie) - 2;

	WPA_ASSERT((size_t) (pos - wpa_ie) <= wpa_ie_len);

	return pos - wpa_ie;
}


static int wpa_gen_wpa_ie_rsn(u8 *rsn_ie, size_t rsn_ie_len,
			      int pairwise_cipher, int group_cipher,
			      int key_mgmt, int mgmt_group_cipher,
			      struct wpa_sm *sm)
{
	u8 *pos;
	struct rsn_ie_hdr *hdr;
	u16 capab;
	u32 suite;

	if (rsn_ie_len < sizeof(*hdr) + RSN_SELECTOR_LEN +
	    2 + RSN_SELECTOR_LEN + 2 + RSN_SELECTOR_LEN + 2 +
	    (sm->cur_pmksa ? 2 + PMKID_LEN : 0)) {
		wpa_printf(MSG_DEBUG, "RSN: Too short IE buffer (%lu bytes)",
			   (unsigned long) rsn_ie_len);
		return -1;
	}

	hdr = (struct rsn_ie_hdr *) rsn_ie;
	hdr->elem_id = WLAN_EID_RSN;
	WPA_PUT_LE16(hdr->version, RSN_VERSION);
	pos = (u8 *) (hdr + 1);

	suite = wpa_cipher_to_suite(WPA_PROTO_RSN, group_cipher);
	if (suite == 0) {
		wpa_printf(MSG_WARNING, "Invalid group cipher (%d).",
			   group_cipher);
		return -1;
	}
	RSN_SELECTOR_PUT(pos, suite);
	pos += RSN_SELECTOR_LEN;

	*pos++ = 1;
	*pos++ = 0;
	suite = wpa_cipher_to_suite(WPA_PROTO_RSN, pairwise_cipher);
	if (suite == 0 ||
	    (!wpa_cipher_valid_pairwise(pairwise_cipher) &&
	     pairwise_cipher != WPA_CIPHER_NONE)) {
		wpa_printf(MSG_WARNING, "Invalid pairwise cipher (%d).",
			   pairwise_cipher);
		return -1;
	}
	RSN_SELECTOR_PUT(pos, suite);
	pos += RSN_SELECTOR_LEN;

	*pos++ = 1;
	*pos++ = 0;
	if (key_mgmt == WPA_KEY_MGMT_IEEE8021X) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_UNSPEC_802_1X);
	} else if (key_mgmt == WPA_KEY_MGMT_PSK) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X);
	} else if (key_mgmt == WPA_KEY_MGMT_CCKM) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_CCKM);
#ifdef CONFIG_IEEE80211R
	} else if (key_mgmt == WPA_KEY_MGMT_FT_IEEE8021X) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_FT_802_1X);
#ifdef CONFIG_SHA384
	} else if (key_mgmt == WPA_KEY_MGMT_FT_IEEE8021X_SHA384) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_FT_802_1X_SHA384);
#endif /* CONFIG_SHA384 */
	} else if (key_mgmt == WPA_KEY_MGMT_FT_PSK) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_FT_PSK);
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_IEEE80211W
	} else if (key_mgmt == WPA_KEY_MGMT_IEEE8021X_SHA256) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_802_1X_SHA256);
	} else if (key_mgmt == WPA_KEY_MGMT_PSK_SHA256) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_PSK_SHA256);
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_SAE
	} else if (key_mgmt == WPA_KEY_MGMT_SAE) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_SAE);
	} else if (key_mgmt == WPA_KEY_MGMT_FT_SAE) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_FT_SAE);
#endif /* CONFIG_SAE */
	} else if (key_mgmt == WPA_KEY_MGMT_IEEE8021X_SUITE_B_192) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_802_1X_SUITE_B_192);
	} else if (key_mgmt == WPA_KEY_MGMT_IEEE8021X_SUITE_B) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_802_1X_SUITE_B);
#ifdef CONFIG_FILS
	} else if (key_mgmt & WPA_KEY_MGMT_FILS_SHA256) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_FILS_SHA256);
	} else if (key_mgmt & WPA_KEY_MGMT_FILS_SHA384) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_FILS_SHA384);
#ifdef CONFIG_IEEE80211R
	} else if (key_mgmt & WPA_KEY_MGMT_FT_FILS_SHA256) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_FT_FILS_SHA256);
	} else if (key_mgmt & WPA_KEY_MGMT_FT_FILS_SHA384) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_FT_FILS_SHA384);
#endif /* CONFIG_IEEE80211R */
#endif /* CONFIG_FILS */
#ifdef CONFIG_OWE
	} else if (key_mgmt & WPA_KEY_MGMT_OWE) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_OWE);
#endif /* CONFIG_OWE */
#ifdef CONFIG_DPP
	} else if (key_mgmt & WPA_KEY_MGMT_DPP) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_DPP);
#endif /* CONFIG_DPP */
#ifdef CONFIG_HS20
	} else if (key_mgmt & WPA_KEY_MGMT_OSEN) {
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_OSEN);
#endif /* CONFIG_HS20 */
	} else {
		wpa_printf(MSG_WARNING, "Invalid key management type (%d).",
			   key_mgmt);
		return -1;
	}
	pos += RSN_SELECTOR_LEN;

	/* RSN Capabilities */
	capab = 0;
#ifdef CONFIG_IEEE80211W
	if (sm->mfp)
		capab |= WPA_CAPABILITY_MFPC;
	if (sm->mfp == 2)
		capab |= WPA_CAPABILITY_MFPR;
#endif /* CONFIG_IEEE80211W */
	WPA_PUT_LE16(pos, capab);
	pos += 2;

	if (sm->cur_pmksa) {
		/* PMKID Count (2 octets, little endian) */
		*pos++ = 1;
		*pos++ = 0;
		/* PMKID */
		os_memcpy(pos, sm->cur_pmksa->pmkid, PMKID_LEN);
		pos += PMKID_LEN;
	}

#ifdef CONFIG_IEEE80211W
	if (wpa_cipher_valid_mgmt_group(mgmt_group_cipher)) {
		if (!sm->cur_pmksa) {
			/* PMKID Count */
			WPA_PUT_LE16(pos, 0);
			pos += 2;
		}

		/* Management Group Cipher Suite */
		RSN_SELECTOR_PUT(pos, wpa_cipher_to_suite(WPA_PROTO_RSN,
							  mgmt_group_cipher));
		pos += RSN_SELECTOR_LEN;
	}
#endif /* CONFIG_IEEE80211W */

	hdr->len = (pos - rsn_ie) - 2;

	WPA_ASSERT((size_t) (pos - rsn_ie) <= rsn_ie_len);

	return pos - rsn_ie;
}


#ifdef CONFIG_HS20
static int wpa_gen_wpa_ie_osen(u8 *wpa_ie, size_t wpa_ie_len,
			       int pairwise_cipher, int group_cipher,
			       int key_mgmt)
{
	u8 *pos, *len;
	u32 suite;

	if (wpa_ie_len < 2 + 4 + RSN_SELECTOR_LEN +
	    2 + RSN_SELECTOR_LEN + 2 + RSN_SELECTOR_LEN)
		return -1;

	pos = wpa_ie;
	*pos++ = WLAN_EID_VENDOR_SPECIFIC;
	len = pos++; /* to be filled */
	WPA_PUT_BE24(pos, OUI_WFA);
	pos += 3;
	*pos++ = HS20_OSEN_OUI_TYPE;

	/* Group Data Cipher Suite */
	suite = wpa_cipher_to_suite(WPA_PROTO_RSN, group_cipher);
	if (suite == 0) {
		wpa_printf(MSG_WARNING, "Invalid group cipher (%d).",
			   group_cipher);
		return -1;
	}
	RSN_SELECTOR_PUT(pos, suite);
	pos += RSN_SELECTOR_LEN;

	/* Pairwise Cipher Suite Count and List */
	WPA_PUT_LE16(pos, 1);
	pos += 2;
	suite = wpa_cipher_to_suite(WPA_PROTO_RSN, pairwise_cipher);
	if (suite == 0 ||
	    (!wpa_cipher_valid_pairwise(pairwise_cipher) &&
	     pairwise_cipher != WPA_CIPHER_NONE)) {
		wpa_printf(MSG_WARNING, "Invalid pairwise cipher (%d).",
			   pairwise_cipher);
		return -1;
	}
	RSN_SELECTOR_PUT(pos, suite);
	pos += RSN_SELECTOR_LEN;

	/* AKM Suite Count and List */
	WPA_PUT_LE16(pos, 1);
	pos += 2;
	RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_OSEN);
	pos += RSN_SELECTOR_LEN;

	*len = pos - len - 1;

	WPA_ASSERT((size_t) (pos - wpa_ie) <= wpa_ie_len);

	return pos - wpa_ie;
}
#endif /* CONFIG_HS20 */


/**
 * wpa_gen_wpa_ie - Generate WPA/RSN IE based on current security policy
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @wpa_ie: Pointer to memory area for the generated WPA/RSN IE
 * @wpa_ie_len: Maximum length of the generated WPA/RSN IE
 * Returns: Length of the generated WPA/RSN IE or -1 on failure
 */
int wpa_gen_wpa_ie(struct wpa_sm *sm, u8 *wpa_ie, size_t wpa_ie_len)
{
	if (sm->proto == WPA_PROTO_RSN)
		return wpa_gen_wpa_ie_rsn(wpa_ie, wpa_ie_len,
					  sm->pairwise_cipher,
					  sm->group_cipher,
					  sm->key_mgmt, sm->mgmt_group_cipher,
					  sm);
#ifdef CONFIG_HS20
	else if (sm->proto == WPA_PROTO_OSEN)
		return wpa_gen_wpa_ie_osen(wpa_ie, wpa_ie_len,
					   sm->pairwise_cipher,
					   sm->group_cipher,
					   sm->key_mgmt);
#endif /* CONFIG_HS20 */
	else
		return wpa_gen_wpa_ie_wpa(wpa_ie, wpa_ie_len,
					  sm->pairwise_cipher,
					  sm->group_cipher,
					  sm->key_mgmt);
}


/**
 * wpa_parse_vendor_specific - Parse Vendor Specific IEs
 * @pos: Pointer to the IE header
 * @end: Pointer to the end of the Key Data buffer
 * @ie: Pointer to parsed IE data
 * Returns: 0 on success, 1 if end mark is found, -1 on failure
 */
static int wpa_parse_vendor_specific(const u8 *pos, const u8 *end,
				     struct wpa_eapol_ie_parse *ie)
{
	unsigned int oui;

	if (pos[1] < 4) {
		wpa_printf(MSG_MSGDUMP, "Too short vendor specific IE ignored (len=%u)",
			   pos[1]);
		return 1;
	}

	oui = WPA_GET_BE24(&pos[2]);
	if (oui == OUI_MICROSOFT && pos[5] == WMM_OUI_TYPE && pos[1] > 4) {
		if (pos[6] == WMM_OUI_SUBTYPE_INFORMATION_ELEMENT) {
			ie->wmm = &pos[2];
			ie->wmm_len = pos[1];
			wpa_hexdump(MSG_DEBUG, "WPA: WMM IE",
				    ie->wmm, ie->wmm_len);
		} else if (pos[6] == WMM_OUI_SUBTYPE_PARAMETER_ELEMENT) {
			ie->wmm = &pos[2];
			ie->wmm_len = pos[1];
			wpa_hexdump(MSG_DEBUG, "WPA: WMM Parameter Element",
				    ie->wmm, ie->wmm_len);
		}
	}
	return 0;
}


/**
 * wpa_parse_generic - Parse EAPOL-Key Key Data Generic IEs
 * @pos: Pointer to the IE header
 * @end: Pointer to the end of the Key Data buffer
 * @ie: Pointer to parsed IE data
 * Returns: 0 on success, 1 if end mark is found, -1 on failure
 */
static int wpa_parse_generic(const u8 *pos, const u8 *end,
			     struct wpa_eapol_ie_parse *ie)
{
	if (pos[1] == 0)
		return 1;

	if (pos[1] >= 6 &&
	    RSN_SELECTOR_GET(pos + 2) == WPA_OUI_TYPE &&
	    pos[2 + WPA_SELECTOR_LEN] == 1 &&
	    pos[2 + WPA_SELECTOR_LEN + 1] == 0) {
		ie->wpa_ie = pos;
		ie->wpa_ie_len = pos[1] + 2;
		wpa_hexdump(MSG_DEBUG, "WPA: WPA IE in EAPOL-Key",
			    ie->wpa_ie, ie->wpa_ie_len);
		return 0;
	}

	if (1 + RSN_SELECTOR_LEN < end - pos &&
	    pos[1] >= RSN_SELECTOR_LEN + PMKID_LEN &&
	    RSN_SELECTOR_GET(pos + 2) == RSN_KEY_DATA_PMKID) {
		ie->pmkid = pos + 2 + RSN_SELECTOR_LEN;
		wpa_hexdump(MSG_DEBUG, "WPA: PMKID in EAPOL-Key",
			    pos, pos[1] + 2);
		return 0;
	}

	if (pos[1] > RSN_SELECTOR_LEN + 2 &&
	    RSN_SELECTOR_GET(pos + 2) == RSN_KEY_DATA_GROUPKEY) {
		ie->gtk = pos + 2 + RSN_SELECTOR_LEN;
		ie->gtk_len = pos[1] - RSN_SELECTOR_LEN;
		wpa_hexdump_key(MSG_DEBUG, "WPA: GTK in EAPOL-Key",
				pos, pos[1] + 2);
		return 0;
	}

	if (pos[1] > RSN_SELECTOR_LEN + 2 &&
	    RSN_SELECTOR_GET(pos + 2) == RSN_KEY_DATA_MAC_ADDR) {
		ie->mac_addr = pos + 2 + RSN_SELECTOR_LEN;
		ie->mac_addr_len = pos[1] - RSN_SELECTOR_LEN;
		wpa_hexdump(MSG_DEBUG, "WPA: MAC Address in EAPOL-Key",
			    pos, pos[1] + 2);
		return 0;
	}

#ifdef CONFIG_IEEE80211W
	if (pos[1] > RSN_SELECTOR_LEN + 2 &&
	    RSN_SELECTOR_GET(pos + 2) == RSN_KEY_DATA_IGTK) {
		ie->igtk = pos + 2 + RSN_SELECTOR_LEN;
		ie->igtk_len = pos[1] - RSN_SELECTOR_LEN;
		wpa_hexdump_key(MSG_DEBUG, "WPA: IGTK in EAPOL-Key",
				pos, pos[1] + 2);
		return 0;
	}
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_P2P
	if (pos[1] >= RSN_SELECTOR_LEN + 1 &&
	    RSN_SELECTOR_GET(pos + 2) == WFA_KEY_DATA_IP_ADDR_REQ) {
		ie->ip_addr_req = pos + 2 + RSN_SELECTOR_LEN;
		wpa_hexdump(MSG_DEBUG, "WPA: IP Address Request in EAPOL-Key",
			    ie->ip_addr_req, pos[1] - RSN_SELECTOR_LEN);
		return 0;
	}

	if (pos[1] >= RSN_SELECTOR_LEN + 3 * 4 &&
	    RSN_SELECTOR_GET(pos + 2) == WFA_KEY_DATA_IP_ADDR_ALLOC) {
		ie->ip_addr_alloc = pos + 2 + RSN_SELECTOR_LEN;
		wpa_hexdump(MSG_DEBUG,
			    "WPA: IP Address Allocation in EAPOL-Key",
			    ie->ip_addr_alloc, pos[1] - RSN_SELECTOR_LEN);
		return 0;
	}
#endif /* CONFIG_P2P */

	return 0;
}


/**
 * wpa_supplicant_parse_ies - Parse EAPOL-Key Key Data IEs
 * @buf: Pointer to the Key Data buffer
 * @len: Key Data Length
 * @ie: Pointer to parsed IE data
 * Returns: 0 on success, -1 on failure
 */
int wpa_supplicant_parse_ies(const u8 *buf, size_t len,
			     struct wpa_eapol_ie_parse *ie)
{
	const u8 *pos, *end;
	int ret = 0;

	os_memset(ie, 0, sizeof(*ie));
	for (pos = buf, end = pos + len; end - pos > 1; pos += 2 + pos[1]) {
		if (pos[0] == 0xdd &&
		    ((pos == buf + len - 1) || pos[1] == 0)) {
			/* Ignore padding */
			break;
		}
		if (2 + pos[1] > end - pos) {
			wpa_printf(MSG_DEBUG, "WPA: EAPOL-Key Key Data "
				   "underflow (ie=%d len=%d pos=%d)",
				   pos[0], pos[1], (int) (pos - buf));
			wpa_hexdump_key(MSG_DEBUG, "WPA: Key Data",
					buf, len);
			ret = -1;
			break;
		}
		if (*pos == WLAN_EID_RSN) {
			ie->rsn_ie = pos;
			ie->rsn_ie_len = pos[1] + 2;
			wpa_hexdump(MSG_DEBUG, "WPA: RSN IE in EAPOL-Key",
				    ie->rsn_ie, ie->rsn_ie_len);
		} else if (*pos == WLAN_EID_MOBILITY_DOMAIN &&
			   pos[1] >= sizeof(struct rsn_mdie)) {
			ie->mdie = pos;
			ie->mdie_len = pos[1] + 2;
			wpa_hexdump(MSG_DEBUG, "WPA: MDIE in EAPOL-Key",
				    ie->mdie, ie->mdie_len);
		} else if (*pos == WLAN_EID_FAST_BSS_TRANSITION &&
			   pos[1] >= sizeof(struct rsn_ftie)) {
			ie->ftie = pos;
			ie->ftie_len = pos[1] + 2;
			wpa_hexdump(MSG_DEBUG, "WPA: FTIE in EAPOL-Key",
				    ie->ftie, ie->ftie_len);
		} else if (*pos == WLAN_EID_TIMEOUT_INTERVAL && pos[1] >= 5) {
			if (pos[2] == WLAN_TIMEOUT_REASSOC_DEADLINE) {
				ie->reassoc_deadline = pos;
				wpa_hexdump(MSG_DEBUG, "WPA: Reassoc Deadline "
					    "in EAPOL-Key",
					    ie->reassoc_deadline, pos[1] + 2);
			} else if (pos[2] == WLAN_TIMEOUT_KEY_LIFETIME) {
				ie->key_lifetime = pos;
				wpa_hexdump(MSG_DEBUG, "WPA: KeyLifetime "
					    "in EAPOL-Key",
					    ie->key_lifetime, pos[1] + 2);
			} else {
				wpa_hexdump(MSG_DEBUG, "WPA: Unrecognized "
					    "EAPOL-Key Key Data IE",
					    pos, 2 + pos[1]);
			}
		} else if (*pos == WLAN_EID_LINK_ID) {
			if (pos[1] >= 18) {
				ie->lnkid = pos;
				ie->lnkid_len = pos[1] + 2;
			}
		} else if (*pos == WLAN_EID_EXT_CAPAB) {
			ie->ext_capab = pos;
			ie->ext_capab_len = pos[1] + 2;
		} else if (*pos == WLAN_EID_SUPP_RATES) {
			ie->supp_rates = pos;
			ie->supp_rates_len = pos[1] + 2;
		} else if (*pos == WLAN_EID_EXT_SUPP_RATES) {
			ie->ext_supp_rates = pos;
			ie->ext_supp_rates_len = pos[1] + 2;
		} else if (*pos == WLAN_EID_HT_CAP &&
			   pos[1] >= sizeof(struct ieee80211_ht_capabilities)) {
			ie->ht_capabilities = pos + 2;
		} else if (*pos == WLAN_EID_VHT_AID) {
			if (pos[1] >= 2)
				ie->aid = WPA_GET_LE16(pos + 2) & 0x3fff;
		} else if (*pos == WLAN_EID_VHT_CAP &&
			   pos[1] >= sizeof(struct ieee80211_vht_capabilities))
		{
			ie->vht_capabilities = pos + 2;
		} else if (*pos == WLAN_EID_QOS && pos[1] >= 1) {
			ie->qosinfo = pos[2];
		} else if (*pos == WLAN_EID_SUPPORTED_CHANNELS) {
			ie->supp_channels = pos + 2;
			ie->supp_channels_len = pos[1];
		} else if (*pos == WLAN_EID_SUPPORTED_OPERATING_CLASSES) {
			/*
			 * The value of the Length field of the Supported
			 * Operating Classes element is between 2 and 253.
			 * Silently skip invalid elements to avoid interop
			 * issues when trying to use the value.
			 */
			if (pos[1] >= 2 && pos[1] <= 253) {
				ie->supp_oper_classes = pos + 2;
				ie->supp_oper_classes_len = pos[1];
			}
		} else if (*pos == WLAN_EID_VENDOR_SPECIFIC) {
			ret = wpa_parse_generic(pos, end, ie);
			if (ret < 0)
				break;
			if (ret > 0) {
				ret = 0;
				break;
			}

			ret = wpa_parse_vendor_specific(pos, end, ie);
			if (ret < 0)
				break;
			if (ret > 0) {
				ret = 0;
				break;
			}
		} else {
			wpa_hexdump(MSG_DEBUG, "WPA: Unrecognized EAPOL-Key "
				    "Key Data IE", pos, 2 + pos[1]);
		}
	}

	return ret;
}
