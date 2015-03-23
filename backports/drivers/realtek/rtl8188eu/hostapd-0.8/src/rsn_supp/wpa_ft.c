/*
 * WPA Supplicant - IEEE 802.11r - Fast BSS Transition
 * Copyright (c) 2006-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/aes_wrap.h"
#include "crypto/random.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "wpa.h"
#include "wpa_i.h"
#include "wpa_ie.h"

#ifdef CONFIG_IEEE80211R

struct wpa_ft_ies {
	const u8 *mdie;
	size_t mdie_len;
	const u8 *ftie;
	size_t ftie_len;
	const u8 *r1kh_id;
	const u8 *gtk;
	size_t gtk_len;
	const u8 *r0kh_id;
	size_t r0kh_id_len;
	const u8 *rsn;
	size_t rsn_len;
	const u8 *rsn_pmkid;
	const u8 *tie;
	size_t tie_len;
	const u8 *igtk;
	size_t igtk_len;
	const u8 *ric;
	size_t ric_len;
};

static int wpa_ft_parse_ies(const u8 *ies, size_t ies_len,
			    struct wpa_ft_ies *parse);


int wpa_derive_ptk_ft(struct wpa_sm *sm, const unsigned char *src_addr,
		      const struct wpa_eapol_key *key,
		      struct wpa_ptk *ptk, size_t ptk_len)
{
	u8 ptk_name[WPA_PMK_NAME_LEN];
	const u8 *anonce = key->key_nonce;

	if (sm->xxkey_len == 0) {
		wpa_printf(MSG_DEBUG, "FT: XXKey not available for key "
			   "derivation");
		return -1;
	}

	wpa_derive_pmk_r0(sm->xxkey, sm->xxkey_len, sm->ssid,
			  sm->ssid_len, sm->mobility_domain,
			  sm->r0kh_id, sm->r0kh_id_len, sm->own_addr,
			  sm->pmk_r0, sm->pmk_r0_name);
	wpa_hexdump_key(MSG_DEBUG, "FT: PMK-R0", sm->pmk_r0, PMK_LEN);
	wpa_hexdump(MSG_DEBUG, "FT: PMKR0Name",
		    sm->pmk_r0_name, WPA_PMK_NAME_LEN);
	wpa_derive_pmk_r1(sm->pmk_r0, sm->pmk_r0_name, sm->r1kh_id,
			  sm->own_addr, sm->pmk_r1, sm->pmk_r1_name);
	wpa_hexdump_key(MSG_DEBUG, "FT: PMK-R1", sm->pmk_r1, PMK_LEN);
	wpa_hexdump(MSG_DEBUG, "FT: PMKR1Name", sm->pmk_r1_name,
		    WPA_PMK_NAME_LEN);
	wpa_pmk_r1_to_ptk(sm->pmk_r1, sm->snonce, anonce, sm->own_addr,
			  sm->bssid, sm->pmk_r1_name,
			  (u8 *) ptk, ptk_len, ptk_name);
	wpa_hexdump_key(MSG_DEBUG, "FT: PTK", (u8 *) ptk, ptk_len);
	wpa_hexdump(MSG_DEBUG, "FT: PTKName", ptk_name, WPA_PMK_NAME_LEN);

	return 0;
}


/**
 * wpa_sm_set_ft_params - Set FT (IEEE 802.11r) parameters
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @ies: Association Response IEs or %NULL to clear FT parameters
 * @ies_len: Length of ies buffer in octets
 * Returns: 0 on success, -1 on failure
 */
int wpa_sm_set_ft_params(struct wpa_sm *sm, const u8 *ies, size_t ies_len)
{
	struct wpa_ft_ies ft;

	if (sm == NULL)
		return 0;

	if (wpa_ft_parse_ies(ies, ies_len, &ft) < 0)
		return -1;

	if (ft.mdie && ft.mdie_len < MOBILITY_DOMAIN_ID_LEN + 1)
		return -1;

	if (ft.mdie) {
		wpa_hexdump(MSG_DEBUG, "FT: Mobility domain",
			    ft.mdie, MOBILITY_DOMAIN_ID_LEN);
		os_memcpy(sm->mobility_domain, ft.mdie,
			  MOBILITY_DOMAIN_ID_LEN);
		sm->mdie_ft_capab = ft.mdie[MOBILITY_DOMAIN_ID_LEN];
		wpa_printf(MSG_DEBUG, "FT: Capability and Policy: 0x%02x",
			   sm->mdie_ft_capab);
	} else
		os_memset(sm->mobility_domain, 0, MOBILITY_DOMAIN_ID_LEN);

	if (ft.r0kh_id) {
		wpa_hexdump(MSG_DEBUG, "FT: R0KH-ID",
			    ft.r0kh_id, ft.r0kh_id_len);
		os_memcpy(sm->r0kh_id, ft.r0kh_id, ft.r0kh_id_len);
		sm->r0kh_id_len = ft.r0kh_id_len;
	} else {
		/* FIX: When should R0KH-ID be cleared? We need to keep the
		 * old R0KH-ID in order to be able to use this during FT. */
		/*
		 * os_memset(sm->r0kh_id, 0, FT_R0KH_ID_LEN);
		 * sm->r0kh_id_len = 0;
		 */
	}

	if (ft.r1kh_id) {
		wpa_hexdump(MSG_DEBUG, "FT: R1KH-ID",
			    ft.r1kh_id, FT_R1KH_ID_LEN);
		os_memcpy(sm->r1kh_id, ft.r1kh_id, FT_R1KH_ID_LEN);
	} else
		os_memset(sm->r1kh_id, 0, FT_R1KH_ID_LEN);

	os_free(sm->assoc_resp_ies);
	sm->assoc_resp_ies = os_malloc(ft.mdie_len + 2 + ft.ftie_len + 2);
	if (sm->assoc_resp_ies) {
		u8 *pos = sm->assoc_resp_ies;
		if (ft.mdie) {
			os_memcpy(pos, ft.mdie - 2, ft.mdie_len + 2);
			pos += ft.mdie_len + 2;
		}
		if (ft.ftie) {
			os_memcpy(pos, ft.ftie - 2, ft.ftie_len + 2);
			pos += ft.ftie_len + 2;
		}
		sm->assoc_resp_ies_len = pos - sm->assoc_resp_ies;
		wpa_hexdump(MSG_DEBUG, "FT: Stored MDIE and FTIE from "
			    "(Re)Association Response",
			    sm->assoc_resp_ies, sm->assoc_resp_ies_len);
	}

	return 0;
}


/**
 * wpa_ft_gen_req_ies - Generate FT (IEEE 802.11r) IEs for Auth/ReAssoc Request
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @len: Buffer for returning the length of the IEs
 * @anonce: ANonce or %NULL if not yet available
 * @pmk_name: PMKR0Name or PMKR1Name to be added into the RSN IE PMKID List
 * @kck: 128-bit KCK for MIC or %NULL if no MIC is used
 * @target_ap: Target AP address
 * @ric_ies: Optional IE(s), e.g., WMM TSPEC(s), for RIC-Request or %NULL
 * @ric_ies_len: Length of ric_ies buffer in octets
 * @ap_mdie: Mobility Domain IE from the target AP
 * Returns: Pointer to buffer with IEs or %NULL on failure
 *
 * Caller is responsible for freeing the returned buffer with os_free();
 */
static u8 * wpa_ft_gen_req_ies(struct wpa_sm *sm, size_t *len,
			       const u8 *anonce, const u8 *pmk_name,
			       const u8 *kck, const u8 *target_ap,
			       const u8 *ric_ies, size_t ric_ies_len,
			       const u8 *ap_mdie)
{
	size_t buf_len;
	u8 *buf, *pos, *ftie_len, *ftie_pos;
	struct rsn_mdie *mdie;
	struct rsn_ftie *ftie;
	struct rsn_ie_hdr *rsnie;
	u16 capab;

	sm->ft_completed = 0;

	buf_len = 2 + sizeof(struct rsn_mdie) + 2 + sizeof(struct rsn_ftie) +
		2 + sm->r0kh_id_len + ric_ies_len + 100;
	buf = os_zalloc(buf_len);
	if (buf == NULL)
		return NULL;
	pos = buf;

	/* RSNIE[PMKR0Name/PMKR1Name] */
	rsnie = (struct rsn_ie_hdr *) pos;
	rsnie->elem_id = WLAN_EID_RSN;
	WPA_PUT_LE16(rsnie->version, RSN_VERSION);
	pos = (u8 *) (rsnie + 1);

	/* Group Suite Selector */
	if (sm->group_cipher == WPA_CIPHER_CCMP)
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_CCMP);
	else if (sm->group_cipher == WPA_CIPHER_TKIP)
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_TKIP);
	else {
		wpa_printf(MSG_WARNING, "FT: Invalid group cipher (%d)",
			   sm->group_cipher);
		os_free(buf);
		return NULL;
	}
	pos += RSN_SELECTOR_LEN;

	/* Pairwise Suite Count */
	WPA_PUT_LE16(pos, 1);
	pos += 2;

	/* Pairwise Suite List */
	if (sm->pairwise_cipher == WPA_CIPHER_CCMP)
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_CCMP);
	else if (sm->pairwise_cipher == WPA_CIPHER_TKIP)
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_TKIP);
	else {
		wpa_printf(MSG_WARNING, "FT: Invalid pairwise cipher (%d)",
			   sm->pairwise_cipher);
		os_free(buf);
		return NULL;
	}
	pos += RSN_SELECTOR_LEN;

	/* Authenticated Key Management Suite Count */
	WPA_PUT_LE16(pos, 1);
	pos += 2;

	/* Authenticated Key Management Suite List */
	if (sm->key_mgmt == WPA_KEY_MGMT_FT_IEEE8021X)
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_FT_802_1X);
	else if (sm->key_mgmt == WPA_KEY_MGMT_FT_PSK)
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_FT_PSK);
	else {
		wpa_printf(MSG_WARNING, "FT: Invalid key management type (%d)",
			   sm->key_mgmt);
		os_free(buf);
		return NULL;
	}
	pos += RSN_SELECTOR_LEN;

	/* RSN Capabilities */
	capab = 0;
#ifdef CONFIG_IEEE80211W
	if (sm->mgmt_group_cipher == WPA_CIPHER_AES_128_CMAC)
		capab |= WPA_CAPABILITY_MFPC;
#endif /* CONFIG_IEEE80211W */
	WPA_PUT_LE16(pos, capab);
	pos += 2;

	/* PMKID Count */
	WPA_PUT_LE16(pos, 1);
	pos += 2;

	/* PMKID List [PMKR0Name/PMKR1Name] */
	os_memcpy(pos, pmk_name, WPA_PMK_NAME_LEN);
	pos += WPA_PMK_NAME_LEN;

#ifdef CONFIG_IEEE80211W
	if (sm->mgmt_group_cipher == WPA_CIPHER_AES_128_CMAC) {
		/* Management Group Cipher Suite */
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_AES_128_CMAC);
		pos += RSN_SELECTOR_LEN;
	}
#endif /* CONFIG_IEEE80211W */

	rsnie->len = (pos - (u8 *) rsnie) - 2;

	/* MDIE */
	*pos++ = WLAN_EID_MOBILITY_DOMAIN;
	*pos++ = sizeof(*mdie);
	mdie = (struct rsn_mdie *) pos;
	pos += sizeof(*mdie);
	os_memcpy(mdie->mobility_domain, sm->mobility_domain,
		  MOBILITY_DOMAIN_ID_LEN);
	mdie->ft_capab = ap_mdie && ap_mdie[1] >= 3 ? ap_mdie[4] :
		sm->mdie_ft_capab;

	/* FTIE[SNonce, [R1KH-ID,] R0KH-ID ] */
	ftie_pos = pos;
	*pos++ = WLAN_EID_FAST_BSS_TRANSITION;
	ftie_len = pos++;
	ftie = (struct rsn_ftie *) pos;
	pos += sizeof(*ftie);
	os_memcpy(ftie->snonce, sm->snonce, WPA_NONCE_LEN);
	if (anonce)
		os_memcpy(ftie->anonce, anonce, WPA_NONCE_LEN);
	if (kck) {
		/* R1KH-ID sub-element in third FT message */
		*pos++ = FTIE_SUBELEM_R1KH_ID;
		*pos++ = FT_R1KH_ID_LEN;
		os_memcpy(pos, sm->r1kh_id, FT_R1KH_ID_LEN);
		pos += FT_R1KH_ID_LEN;
	}
	/* R0KH-ID sub-element */
	*pos++ = FTIE_SUBELEM_R0KH_ID;
	*pos++ = sm->r0kh_id_len;
	os_memcpy(pos, sm->r0kh_id, sm->r0kh_id_len);
	pos += sm->r0kh_id_len;
	*ftie_len = pos - ftie_len - 1;

	if (ric_ies) {
		/* RIC Request */
		os_memcpy(pos, ric_ies, ric_ies_len);
		pos += ric_ies_len;
	}

	if (kck) {
		/*
		 * IEEE Std 802.11r-2008, 11A.8.4
		 * MIC shall be calculated over:
		 * non-AP STA MAC address
		 * Target AP MAC address
		 * Transaction seq number (5 for ReassocReq, 3 otherwise)
		 * RSN IE
		 * MDIE
		 * FTIE (with MIC field set to 0)
		 * RIC-Request (if present)
		 */
		/* Information element count */
		ftie->mic_control[1] = 3 + ieee802_11_ie_count(ric_ies,
							       ric_ies_len);
		if (wpa_ft_mic(kck, sm->own_addr, target_ap, 5,
			       ((u8 *) mdie) - 2, 2 + sizeof(*mdie),
			       ftie_pos, 2 + *ftie_len,
			       (u8 *) rsnie, 2 + rsnie->len, ric_ies,
			       ric_ies_len, ftie->mic) < 0) {
			wpa_printf(MSG_INFO, "FT: Failed to calculate MIC");
			os_free(buf);
			return NULL;
		}
	}

	*len = pos - buf;

	return buf;
}


static int wpa_ft_parse_ftie(const u8 *ie, size_t ie_len,
			     struct wpa_ft_ies *parse)
{
	const u8 *end, *pos;

	parse->ftie = ie;
	parse->ftie_len = ie_len;

	pos = ie + sizeof(struct rsn_ftie);
	end = ie + ie_len;

	while (pos + 2 <= end && pos + 2 + pos[1] <= end) {
		switch (pos[0]) {
		case FTIE_SUBELEM_R1KH_ID:
			if (pos[1] != FT_R1KH_ID_LEN) {
				wpa_printf(MSG_DEBUG, "FT: Invalid R1KH-ID "
					   "length in FTIE: %d", pos[1]);
				return -1;
			}
			parse->r1kh_id = pos + 2;
			break;
		case FTIE_SUBELEM_GTK:
			parse->gtk = pos + 2;
			parse->gtk_len = pos[1];
			break;
		case FTIE_SUBELEM_R0KH_ID:
			if (pos[1] < 1 || pos[1] > FT_R0KH_ID_MAX_LEN) {
				wpa_printf(MSG_DEBUG, "FT: Invalid R0KH-ID "
					   "length in FTIE: %d", pos[1]);
				return -1;
			}
			parse->r0kh_id = pos + 2;
			parse->r0kh_id_len = pos[1];
			break;
#ifdef CONFIG_IEEE80211W
		case FTIE_SUBELEM_IGTK:
			parse->igtk = pos + 2;
			parse->igtk_len = pos[1];
			break;
#endif /* CONFIG_IEEE80211W */
		}

		pos += 2 + pos[1];
	}

	return 0;
}


static int wpa_ft_parse_ies(const u8 *ies, size_t ies_len,
			    struct wpa_ft_ies *parse)
{
	const u8 *end, *pos;
	struct wpa_ie_data data;
	int ret;
	const struct rsn_ftie *ftie;
	int prot_ie_count = 0;

	os_memset(parse, 0, sizeof(*parse));
	if (ies == NULL)
		return 0;

	pos = ies;
	end = ies + ies_len;
	while (pos + 2 <= end && pos + 2 + pos[1] <= end) {
		switch (pos[0]) {
		case WLAN_EID_RSN:
			parse->rsn = pos + 2;
			parse->rsn_len = pos[1];
			ret = wpa_parse_wpa_ie_rsn(parse->rsn - 2,
						   parse->rsn_len + 2,
						   &data);
			if (ret < 0) {
				wpa_printf(MSG_DEBUG, "FT: Failed to parse "
					   "RSN IE: %d", ret);
				return -1;
			}
			if (data.num_pmkid == 1 && data.pmkid)
				parse->rsn_pmkid = data.pmkid;
			break;
		case WLAN_EID_MOBILITY_DOMAIN:
			parse->mdie = pos + 2;
			parse->mdie_len = pos[1];
			break;
		case WLAN_EID_FAST_BSS_TRANSITION:
			if (pos[1] < sizeof(*ftie))
				return -1;
			ftie = (const struct rsn_ftie *) (pos + 2);
			prot_ie_count = ftie->mic_control[1];
			if (wpa_ft_parse_ftie(pos + 2, pos[1], parse) < 0)
				return -1;
			break;
		case WLAN_EID_TIMEOUT_INTERVAL:
			parse->tie = pos + 2;
			parse->tie_len = pos[1];
			break;
		case WLAN_EID_RIC_DATA:
			if (parse->ric == NULL)
				parse->ric = pos;
		}

		pos += 2 + pos[1];
	}

	if (prot_ie_count == 0)
		return 0; /* no MIC */

	/*
	 * Check that the protected IE count matches with IEs included in the
	 * frame.
	 */
	if (parse->rsn)
		prot_ie_count--;
	if (parse->mdie)
		prot_ie_count--;
	if (parse->ftie)
		prot_ie_count--;
	if (parse->tie)
		prot_ie_count--;
	if (prot_ie_count < 0) {
		wpa_printf(MSG_DEBUG, "FT: Some required IEs not included in "
			   "the protected IE count");
		return -1;
	}

	if (prot_ie_count == 0 && parse->ric) {
		wpa_printf(MSG_DEBUG, "FT: RIC IE(s) in the frame, but not "
			   "included in protected IE count");
		return -1;
	}

	/* Determine the end of the RIC IE(s) */
	pos = parse->ric;
	while (pos && pos + 2 <= end && pos + 2 + pos[1] <= end &&
	       prot_ie_count) {
		prot_ie_count--;
		pos += 2 + pos[1];
	}
	parse->ric_len = pos - parse->ric;
	if (prot_ie_count) {
		wpa_printf(MSG_DEBUG, "FT: %d protected IEs missing from "
			   "frame", (int) prot_ie_count);
		return -1;
	}

	return 0;
}


static int wpa_ft_install_ptk(struct wpa_sm *sm, const u8 *bssid)
{
	int keylen;
	enum wpa_alg alg;
	u8 null_rsc[6] = { 0, 0, 0, 0, 0, 0 };

	wpa_printf(MSG_DEBUG, "FT: Installing PTK to the driver.");

	switch (sm->pairwise_cipher) {
	case WPA_CIPHER_CCMP:
		alg = WPA_ALG_CCMP;
		keylen = 16;
		break;
	case WPA_CIPHER_TKIP:
		alg = WPA_ALG_TKIP;
		keylen = 32;
		break;
	default:
		wpa_printf(MSG_WARNING, "FT: Unsupported pairwise cipher %d",
			   sm->pairwise_cipher);
		return -1;
	}

	if (wpa_sm_set_key(sm, alg, bssid, 0, 1, null_rsc,
			   sizeof(null_rsc), (u8 *) sm->ptk.tk1, keylen) < 0) {
		wpa_printf(MSG_WARNING, "FT: Failed to set PTK to the driver");
		return -1;
	}

	return 0;
}


/**
 * wpa_ft_prepare_auth_request - Generate over-the-air auth request
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @mdie: Target AP MDIE
 * Returns: 0 on success, -1 on failure
 */
int wpa_ft_prepare_auth_request(struct wpa_sm *sm, const u8 *mdie)
{
	u8 *ft_ies;
	size_t ft_ies_len;

	/* Generate a new SNonce */
	if (random_get_bytes(sm->snonce, WPA_NONCE_LEN)) {
		wpa_printf(MSG_INFO, "FT: Failed to generate a new SNonce");
		return -1;
	}

	ft_ies = wpa_ft_gen_req_ies(sm, &ft_ies_len, NULL, sm->pmk_r0_name,
				    NULL, sm->bssid, NULL, 0, mdie);
	if (ft_ies) {
		wpa_sm_update_ft_ies(sm, sm->mobility_domain,
				     ft_ies, ft_ies_len);
		os_free(ft_ies);
	}

	return 0;
}


int wpa_ft_process_response(struct wpa_sm *sm, const u8 *ies, size_t ies_len,
			    int ft_action, const u8 *target_ap,
			    const u8 *ric_ies, size_t ric_ies_len)
{
	u8 *ft_ies;
	size_t ft_ies_len, ptk_len;
	struct wpa_ft_ies parse;
	struct rsn_mdie *mdie;
	struct rsn_ftie *ftie;
	u8 ptk_name[WPA_PMK_NAME_LEN];
	int ret;
	const u8 *bssid;

	wpa_hexdump(MSG_DEBUG, "FT: Response IEs", ies, ies_len);
	wpa_hexdump(MSG_DEBUG, "FT: RIC IEs", ric_ies, ric_ies_len);

	if (ft_action) {
		if (!sm->over_the_ds_in_progress) {
			wpa_printf(MSG_DEBUG, "FT: No over-the-DS in progress "
				   "- drop FT Action Response");
			return -1;
		}

		if (os_memcmp(target_ap, sm->target_ap, ETH_ALEN) != 0) {
			wpa_printf(MSG_DEBUG, "FT: No over-the-DS in progress "
				   "with this Target AP - drop FT Action "
				   "Response");
			return -1;
		}
	}

	if (sm->key_mgmt != WPA_KEY_MGMT_FT_IEEE8021X &&
	    sm->key_mgmt != WPA_KEY_MGMT_FT_PSK) {
		wpa_printf(MSG_DEBUG, "FT: Reject FT IEs since FT is not "
			   "enabled for this connection");
		return -1;
	}

	if (wpa_ft_parse_ies(ies, ies_len, &parse) < 0) {
		wpa_printf(MSG_DEBUG, "FT: Failed to parse IEs");
		return -1;
	}

	mdie = (struct rsn_mdie *) parse.mdie;
	if (mdie == NULL || parse.mdie_len < sizeof(*mdie) ||
	    os_memcmp(mdie->mobility_domain, sm->mobility_domain,
		      MOBILITY_DOMAIN_ID_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "FT: Invalid MDIE");
		return -1;
	}

	ftie = (struct rsn_ftie *) parse.ftie;
	if (ftie == NULL || parse.ftie_len < sizeof(*ftie)) {
		wpa_printf(MSG_DEBUG, "FT: Invalid FTIE");
		return -1;
	}

	if (os_memcmp(ftie->snonce, sm->snonce, WPA_NONCE_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "FT: SNonce mismatch in FTIE");
		wpa_hexdump(MSG_DEBUG, "FT: Received SNonce",
			    ftie->snonce, WPA_NONCE_LEN);
		wpa_hexdump(MSG_DEBUG, "FT: Expected SNonce",
			    sm->snonce, WPA_NONCE_LEN);
		return -1;
	}

	if (parse.r0kh_id == NULL) {
		wpa_printf(MSG_DEBUG, "FT: No R0KH-ID subelem in FTIE");
		return -1;
	}

	if (parse.r0kh_id_len != sm->r0kh_id_len ||
	    os_memcmp(parse.r0kh_id, sm->r0kh_id, parse.r0kh_id_len) != 0) {
		wpa_printf(MSG_DEBUG, "FT: R0KH-ID in FTIE did not match with "
			   "the current R0KH-ID");
		wpa_hexdump(MSG_DEBUG, "FT: R0KH-ID in FTIE",
			    parse.r0kh_id, parse.r0kh_id_len);
		wpa_hexdump(MSG_DEBUG, "FT: The current R0KH-ID",
			    sm->r0kh_id, sm->r0kh_id_len);
		return -1;
	}

	if (parse.r1kh_id == NULL) {
		wpa_printf(MSG_DEBUG, "FT: No R1KH-ID subelem in FTIE");
		return -1;
	}

	if (parse.rsn_pmkid == NULL ||
	    os_memcmp(parse.rsn_pmkid, sm->pmk_r0_name, WPA_PMK_NAME_LEN)) {
		wpa_printf(MSG_DEBUG, "FT: No matching PMKR0Name (PMKID) in "
			   "RSNIE");
		return -1;
	}

	os_memcpy(sm->r1kh_id, parse.r1kh_id, FT_R1KH_ID_LEN);
	wpa_hexdump(MSG_DEBUG, "FT: R1KH-ID", sm->r1kh_id, FT_R1KH_ID_LEN);
	wpa_hexdump(MSG_DEBUG, "FT: SNonce", sm->snonce, WPA_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "FT: ANonce", ftie->anonce, WPA_NONCE_LEN);
	os_memcpy(sm->anonce, ftie->anonce, WPA_NONCE_LEN);
	wpa_derive_pmk_r1(sm->pmk_r0, sm->pmk_r0_name, sm->r1kh_id,
			  sm->own_addr, sm->pmk_r1, sm->pmk_r1_name);
	wpa_hexdump_key(MSG_DEBUG, "FT: PMK-R1", sm->pmk_r1, PMK_LEN);
	wpa_hexdump(MSG_DEBUG, "FT: PMKR1Name",
		    sm->pmk_r1_name, WPA_PMK_NAME_LEN);

	bssid = target_ap;
	ptk_len = sm->pairwise_cipher == WPA_CIPHER_CCMP ? 48 : 64;
	wpa_pmk_r1_to_ptk(sm->pmk_r1, sm->snonce, ftie->anonce, sm->own_addr,
			  bssid, sm->pmk_r1_name,
			  (u8 *) &sm->ptk, ptk_len, ptk_name);
	wpa_hexdump_key(MSG_DEBUG, "FT: PTK",
			(u8 *) &sm->ptk, ptk_len);
	wpa_hexdump(MSG_DEBUG, "FT: PTKName", ptk_name, WPA_PMK_NAME_LEN);

	ft_ies = wpa_ft_gen_req_ies(sm, &ft_ies_len, ftie->anonce,
				    sm->pmk_r1_name, sm->ptk.kck, bssid,
				    ric_ies, ric_ies_len,
				    parse.mdie ? parse.mdie - 2 : NULL);
	if (ft_ies) {
		wpa_sm_update_ft_ies(sm, sm->mobility_domain,
				     ft_ies, ft_ies_len);
		os_free(ft_ies);
	}

	wpa_sm_mark_authenticated(sm, bssid);
	ret = wpa_ft_install_ptk(sm, bssid);
	if (ret) {
		/*
		 * Some drivers do not support key configuration when we are
		 * not associated with the target AP. Work around this by
		 * trying again after the following reassociation gets
		 * completed.
		 */
		wpa_printf(MSG_DEBUG, "FT: Failed to set PTK prior to "
			   "association - try again after reassociation");
		sm->set_ptk_after_assoc = 1;
	} else
		sm->set_ptk_after_assoc = 0;

	sm->ft_completed = 1;
	if (ft_action) {
		/*
		 * The caller is expected trigger re-association with the
		 * Target AP.
		 */
		os_memcpy(sm->bssid, target_ap, ETH_ALEN);
	}

	return 0;
}


int wpa_ft_is_completed(struct wpa_sm *sm)
{
	if (sm == NULL)
		return 0;

	if (sm->key_mgmt != WPA_KEY_MGMT_FT_IEEE8021X &&
	    sm->key_mgmt != WPA_KEY_MGMT_FT_PSK)
		return 0;

	return sm->ft_completed;
}


static int wpa_ft_process_gtk_subelem(struct wpa_sm *sm, const u8 *gtk_elem,
				      size_t gtk_elem_len)
{
	u8 gtk[32];
	int keyidx;
	enum wpa_alg alg;
	size_t gtk_len, keylen, rsc_len;

	if (gtk_elem == NULL) {
		wpa_printf(MSG_DEBUG, "FT: No GTK included in FTIE");
		return 0;
	}

	wpa_hexdump_key(MSG_DEBUG, "FT: Received GTK in Reassoc Resp",
			gtk_elem, gtk_elem_len);

	if (gtk_elem_len < 11 + 24 || (gtk_elem_len - 11) % 8 ||
	    gtk_elem_len - 19 > sizeof(gtk)) {
		wpa_printf(MSG_DEBUG, "FT: Invalid GTK sub-elem "
			   "length %lu", (unsigned long) gtk_elem_len);
		return -1;
	}
	gtk_len = gtk_elem_len - 19;
	if (aes_unwrap(sm->ptk.kek, gtk_len / 8, gtk_elem + 11, gtk)) {
		wpa_printf(MSG_WARNING, "FT: AES unwrap failed - could not "
			   "decrypt GTK");
		return -1;
	}

	switch (sm->group_cipher) {
	case WPA_CIPHER_CCMP:
		keylen = 16;
		rsc_len = 6;
		alg = WPA_ALG_CCMP;
		break;
	case WPA_CIPHER_TKIP:
		keylen = 32;
		rsc_len = 6;
		alg = WPA_ALG_TKIP;
		break;
	case WPA_CIPHER_WEP104:
		keylen = 13;
		rsc_len = 0;
		alg = WPA_ALG_WEP;
		break;
	case WPA_CIPHER_WEP40:
		keylen = 5;
		rsc_len = 0;
		alg = WPA_ALG_WEP;
		break;
	default:
		wpa_printf(MSG_WARNING, "WPA: Unsupported Group Cipher %d",
			   sm->group_cipher);
		return -1;
	}

	if (gtk_len < keylen) {
		wpa_printf(MSG_DEBUG, "FT: Too short GTK in FTIE");
		return -1;
	}

	/* Key Info[2] | Key Length[1] | RSC[8] | Key[5..32]. */

	keyidx = WPA_GET_LE16(gtk_elem) & 0x03;

	if (gtk_elem[2] != keylen) {
		wpa_printf(MSG_DEBUG, "FT: GTK length mismatch: received %d "
			   "negotiated %lu",
			   gtk_elem[2], (unsigned long) keylen);
		return -1;
	}

	wpa_hexdump_key(MSG_DEBUG, "FT: GTK from Reassoc Resp", gtk, keylen);
	if (wpa_sm_set_key(sm, alg, broadcast_ether_addr, keyidx, 0,
			   gtk_elem + 3, rsc_len, gtk, keylen) < 0) {
		wpa_printf(MSG_WARNING, "WPA: Failed to set GTK to the "
			   "driver.");
		return -1;
	}

	return 0;
}


#ifdef CONFIG_IEEE80211W
static int wpa_ft_process_igtk_subelem(struct wpa_sm *sm, const u8 *igtk_elem,
				       size_t igtk_elem_len)
{
	u8 igtk[WPA_IGTK_LEN];
	u16 keyidx;

	if (sm->mgmt_group_cipher != WPA_CIPHER_AES_128_CMAC)
		return 0;

	if (igtk_elem == NULL) {
		wpa_printf(MSG_DEBUG, "FT: No IGTK included in FTIE");
		return 0;
	}

	wpa_hexdump_key(MSG_DEBUG, "FT: Received IGTK in Reassoc Resp",
			igtk_elem, igtk_elem_len);

	if (igtk_elem_len != 2 + 6 + 1 + WPA_IGTK_LEN + 8) {
		wpa_printf(MSG_DEBUG, "FT: Invalid IGTK sub-elem "
			   "length %lu", (unsigned long) igtk_elem_len);
		return -1;
	}
	if (igtk_elem[8] != WPA_IGTK_LEN) {
		wpa_printf(MSG_DEBUG, "FT: Invalid IGTK sub-elem Key Length "
			   "%d", igtk_elem[8]);
		return -1;
	}

	if (aes_unwrap(sm->ptk.kek, WPA_IGTK_LEN / 8, igtk_elem + 9, igtk)) {
		wpa_printf(MSG_WARNING, "FT: AES unwrap failed - could not "
			   "decrypt IGTK");
		return -1;
	}

	/* KeyID[2] | IPN[6] | Key Length[1] | Key[16+8] */

	keyidx = WPA_GET_LE16(igtk_elem);

	wpa_hexdump_key(MSG_DEBUG, "FT: IGTK from Reassoc Resp", igtk,
			WPA_IGTK_LEN);
	if (wpa_sm_set_key(sm, WPA_ALG_IGTK, broadcast_ether_addr, keyidx, 0,
			   igtk_elem + 2, 6, igtk, WPA_IGTK_LEN) < 0) {
		wpa_printf(MSG_WARNING, "WPA: Failed to set IGTK to the "
			   "driver.");
		return -1;
	}

	return 0;
}
#endif /* CONFIG_IEEE80211W */


int wpa_ft_validate_reassoc_resp(struct wpa_sm *sm, const u8 *ies,
				 size_t ies_len, const u8 *src_addr)
{
	struct wpa_ft_ies parse;
	struct rsn_mdie *mdie;
	struct rsn_ftie *ftie;
	unsigned int count;
	u8 mic[16];

	wpa_hexdump(MSG_DEBUG, "FT: Response IEs", ies, ies_len);

	if (sm->key_mgmt != WPA_KEY_MGMT_FT_IEEE8021X &&
	    sm->key_mgmt != WPA_KEY_MGMT_FT_PSK) {
		wpa_printf(MSG_DEBUG, "FT: Reject FT IEs since FT is not "
			   "enabled for this connection");
		return -1;
	}

	if (wpa_ft_parse_ies(ies, ies_len, &parse) < 0) {
		wpa_printf(MSG_DEBUG, "FT: Failed to parse IEs");
		return -1;
	}

	mdie = (struct rsn_mdie *) parse.mdie;
	if (mdie == NULL || parse.mdie_len < sizeof(*mdie) ||
	    os_memcmp(mdie->mobility_domain, sm->mobility_domain,
		      MOBILITY_DOMAIN_ID_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "FT: Invalid MDIE");
		return -1;
	}

	ftie = (struct rsn_ftie *) parse.ftie;
	if (ftie == NULL || parse.ftie_len < sizeof(*ftie)) {
		wpa_printf(MSG_DEBUG, "FT: Invalid FTIE");
		return -1;
	}

	if (os_memcmp(ftie->snonce, sm->snonce, WPA_NONCE_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "FT: SNonce mismatch in FTIE");
		wpa_hexdump(MSG_DEBUG, "FT: Received SNonce",
			    ftie->snonce, WPA_NONCE_LEN);
		wpa_hexdump(MSG_DEBUG, "FT: Expected SNonce",
			    sm->snonce, WPA_NONCE_LEN);
		return -1;
	}

	if (os_memcmp(ftie->anonce, sm->anonce, WPA_NONCE_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "FT: ANonce mismatch in FTIE");
		wpa_hexdump(MSG_DEBUG, "FT: Received ANonce",
			    ftie->anonce, WPA_NONCE_LEN);
		wpa_hexdump(MSG_DEBUG, "FT: Expected ANonce",
			    sm->anonce, WPA_NONCE_LEN);
		return -1;
	}

	if (parse.r0kh_id == NULL) {
		wpa_printf(MSG_DEBUG, "FT: No R0KH-ID subelem in FTIE");
		return -1;
	}

	if (parse.r0kh_id_len != sm->r0kh_id_len ||
	    os_memcmp(parse.r0kh_id, sm->r0kh_id, parse.r0kh_id_len) != 0) {
		wpa_printf(MSG_DEBUG, "FT: R0KH-ID in FTIE did not match with "
			   "the current R0KH-ID");
		wpa_hexdump(MSG_DEBUG, "FT: R0KH-ID in FTIE",
			    parse.r0kh_id, parse.r0kh_id_len);
		wpa_hexdump(MSG_DEBUG, "FT: The current R0KH-ID",
			    sm->r0kh_id, sm->r0kh_id_len);
		return -1;
	}

	if (parse.r1kh_id == NULL) {
		wpa_printf(MSG_DEBUG, "FT: No R1KH-ID subelem in FTIE");
		return -1;
	}

	if (os_memcmp(parse.r1kh_id, sm->r1kh_id, FT_R1KH_ID_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "FT: Unknown R1KH-ID used in "
			   "ReassocResp");
		return -1;
	}

	if (parse.rsn_pmkid == NULL ||
	    os_memcmp(parse.rsn_pmkid, sm->pmk_r1_name, WPA_PMK_NAME_LEN)) {
		wpa_printf(MSG_DEBUG, "FT: No matching PMKR1Name (PMKID) in "
			   "RSNIE (pmkid=%d)", !!parse.rsn_pmkid);
		return -1;
	}

	count = 3;
	if (parse.tie)
		count++;
	if (ftie->mic_control[1] != count) {
		wpa_printf(MSG_DEBUG, "FT: Unexpected IE count in MIC "
			   "Control: received %u expected %u",
			   ftie->mic_control[1], count);
		return -1;
	}

	if (wpa_ft_mic(sm->ptk.kck, sm->own_addr, src_addr, 6,
		       parse.mdie - 2, parse.mdie_len + 2,
		       parse.ftie - 2, parse.ftie_len + 2,
		       parse.rsn - 2, parse.rsn_len + 2,
		       parse.ric, parse.ric_len,
		       mic) < 0) {
		wpa_printf(MSG_DEBUG, "FT: Failed to calculate MIC");
		return -1;
	}

	if (os_memcmp(mic, ftie->mic, 16) != 0) {
		wpa_printf(MSG_DEBUG, "FT: Invalid MIC in FTIE");
		wpa_hexdump(MSG_MSGDUMP, "FT: Received MIC", ftie->mic, 16);
		wpa_hexdump(MSG_MSGDUMP, "FT: Calculated MIC", mic, 16);
		return -1;
	}

	if (wpa_ft_process_gtk_subelem(sm, parse.gtk, parse.gtk_len) < 0)
		return -1;

#ifdef CONFIG_IEEE80211W
	if (wpa_ft_process_igtk_subelem(sm, parse.igtk, parse.igtk_len) < 0)
		return -1;
#endif /* CONFIG_IEEE80211W */

	if (sm->set_ptk_after_assoc) {
		wpa_printf(MSG_DEBUG, "FT: Try to set PTK again now that we "
			   "are associated");
		if (wpa_ft_install_ptk(sm, src_addr) < 0)
			return -1;
		sm->set_ptk_after_assoc = 0;
	}

	if (parse.ric) {
		wpa_hexdump(MSG_MSGDUMP, "FT: RIC Response",
			    parse.ric, parse.ric_len);
		/* TODO: parse response and inform driver about results */
	}

	return 0;
}


/**
 * wpa_ft_start_over_ds - Generate over-the-DS auth request
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @target_ap: Target AP Address
 * @mdie: Mobility Domain IE from the target AP
 * Returns: 0 on success, -1 on failure
 */
int wpa_ft_start_over_ds(struct wpa_sm *sm, const u8 *target_ap,
			 const u8 *mdie)
{
	u8 *ft_ies;
	size_t ft_ies_len;

	wpa_printf(MSG_DEBUG, "FT: Request over-the-DS with " MACSTR,
		   MAC2STR(target_ap));

	/* Generate a new SNonce */
	if (random_get_bytes(sm->snonce, WPA_NONCE_LEN)) {
		wpa_printf(MSG_INFO, "FT: Failed to generate a new SNonce");
		return -1;
	}

	ft_ies = wpa_ft_gen_req_ies(sm, &ft_ies_len, NULL, sm->pmk_r0_name,
				    NULL, target_ap, NULL, 0, mdie);
	if (ft_ies) {
		sm->over_the_ds_in_progress = 1;
		os_memcpy(sm->target_ap, target_ap, ETH_ALEN);
		wpa_sm_send_ft_action(sm, 1, target_ap, ft_ies, ft_ies_len);
		os_free(ft_ies);
	}

	return 0;
}

#endif /* CONFIG_IEEE80211R */
