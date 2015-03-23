/*
 * WPA/RSN - Shared functions for supplicant and authenticator
 * Copyright (c) 2002-2008, Jouni Malinen <j@w1.fi>
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
#include "crypto/md5.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "crypto/aes_wrap.h"
#include "crypto/crypto.h"
#include "ieee802_11_defs.h"
#include "defs.h"
#include "wpa_common.h"


/**
 * wpa_eapol_key_mic - Calculate EAPOL-Key MIC
 * @key: EAPOL-Key Key Confirmation Key (KCK)
 * @ver: Key descriptor version (WPA_KEY_INFO_TYPE_*)
 * @buf: Pointer to the beginning of the EAPOL header (version field)
 * @len: Length of the EAPOL frame (from EAPOL header to the end of the frame)
 * @mic: Pointer to the buffer to which the EAPOL-Key MIC is written
 * Returns: 0 on success, -1 on failure
 *
 * Calculate EAPOL-Key MIC for an EAPOL-Key packet. The EAPOL-Key MIC field has
 * to be cleared (all zeroes) when calling this function.
 *
 * Note: 'IEEE Std 802.11i-2004 - 8.5.2 EAPOL-Key frames' has an error in the
 * description of the Key MIC calculation. It includes packet data from the
 * beginning of the EAPOL-Key header, not EAPOL header. This incorrect change
 * happened during final editing of the standard and the correct behavior is
 * defined in the last draft (IEEE 802.11i/D10).
 */
int wpa_eapol_key_mic(const u8 *key, int ver, const u8 *buf, size_t len,
		      u8 *mic)
{
	u8 hash[SHA1_MAC_LEN];

	switch (ver) {
	case WPA_KEY_INFO_TYPE_HMAC_MD5_RC4:
		return hmac_md5(key, 16, buf, len, mic);
	case WPA_KEY_INFO_TYPE_HMAC_SHA1_AES:
		if (hmac_sha1(key, 16, buf, len, hash))
			return -1;
		os_memcpy(mic, hash, MD5_MAC_LEN);
		break;
#if defined(CONFIG_IEEE80211R) || defined(CONFIG_IEEE80211W)
	case WPA_KEY_INFO_TYPE_AES_128_CMAC:
		return omac1_aes_128(key, buf, len, mic);
#endif /* CONFIG_IEEE80211R || CONFIG_IEEE80211W */
	default:
		return -1;
	}

	return 0;
}


/**
 * wpa_pmk_to_ptk - Calculate PTK from PMK, addresses, and nonces
 * @pmk: Pairwise master key
 * @pmk_len: Length of PMK
 * @label: Label to use in derivation
 * @addr1: AA or SA
 * @addr2: SA or AA
 * @nonce1: ANonce or SNonce
 * @nonce2: SNonce or ANonce
 * @ptk: Buffer for pairwise transient key
 * @ptk_len: Length of PTK
 * @use_sha256: Whether to use SHA256-based KDF
 *
 * IEEE Std 802.11i-2004 - 8.5.1.2 Pairwise key hierarchy
 * PTK = PRF-X(PMK, "Pairwise key expansion",
 *             Min(AA, SA) || Max(AA, SA) ||
 *             Min(ANonce, SNonce) || Max(ANonce, SNonce))
 *
 * STK = PRF-X(SMK, "Peer key expansion",
 *             Min(MAC_I, MAC_P) || Max(MAC_I, MAC_P) ||
 *             Min(INonce, PNonce) || Max(INonce, PNonce))
 */
void wpa_pmk_to_ptk(const u8 *pmk, size_t pmk_len, const char *label,
		    const u8 *addr1, const u8 *addr2,
		    const u8 *nonce1, const u8 *nonce2,
		    u8 *ptk, size_t ptk_len, int use_sha256)
{
	u8 data[2 * ETH_ALEN + 2 * WPA_NONCE_LEN];

	if (os_memcmp(addr1, addr2, ETH_ALEN) < 0) {
		os_memcpy(data, addr1, ETH_ALEN);
		os_memcpy(data + ETH_ALEN, addr2, ETH_ALEN);
	} else {
		os_memcpy(data, addr2, ETH_ALEN);
		os_memcpy(data + ETH_ALEN, addr1, ETH_ALEN);
	}

	if (os_memcmp(nonce1, nonce2, WPA_NONCE_LEN) < 0) {
		os_memcpy(data + 2 * ETH_ALEN, nonce1, WPA_NONCE_LEN);
		os_memcpy(data + 2 * ETH_ALEN + WPA_NONCE_LEN, nonce2,
			  WPA_NONCE_LEN);
	} else {
		os_memcpy(data + 2 * ETH_ALEN, nonce2, WPA_NONCE_LEN);
		os_memcpy(data + 2 * ETH_ALEN + WPA_NONCE_LEN, nonce1,
			  WPA_NONCE_LEN);
	}

#ifdef CONFIG_IEEE80211W
	if (use_sha256)
		sha256_prf(pmk, pmk_len, label, data, sizeof(data),
			   ptk, ptk_len);
	else
#endif /* CONFIG_IEEE80211W */
		sha1_prf(pmk, pmk_len, label, data, sizeof(data), ptk,
			 ptk_len);

	wpa_printf(MSG_DEBUG, "WPA: PTK derivation - A1=" MACSTR " A2=" MACSTR,
		   MAC2STR(addr1), MAC2STR(addr2));
	wpa_hexdump(MSG_DEBUG, "WPA: Nonce1", nonce1, WPA_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "WPA: Nonce2", nonce2, WPA_NONCE_LEN);
	wpa_hexdump_key(MSG_DEBUG, "WPA: PMK", pmk, pmk_len);
	wpa_hexdump_key(MSG_DEBUG, "WPA: PTK", ptk, ptk_len);
}


#ifdef CONFIG_IEEE80211R
int wpa_ft_mic(const u8 *kck, const u8 *sta_addr, const u8 *ap_addr,
	       u8 transaction_seqnum, const u8 *mdie, size_t mdie_len,
	       const u8 *ftie, size_t ftie_len,
	       const u8 *rsnie, size_t rsnie_len,
	       const u8 *ric, size_t ric_len, u8 *mic)
{
	u8 *buf, *pos;
	size_t buf_len;

	buf_len = 2 * ETH_ALEN + 1 + mdie_len + ftie_len + rsnie_len + ric_len;
	buf = os_malloc(buf_len);
	if (buf == NULL)
		return -1;

	pos = buf;
	os_memcpy(pos, sta_addr, ETH_ALEN);
	pos += ETH_ALEN;
	os_memcpy(pos, ap_addr, ETH_ALEN);
	pos += ETH_ALEN;
	*pos++ = transaction_seqnum;
	if (rsnie) {
		os_memcpy(pos, rsnie, rsnie_len);
		pos += rsnie_len;
	}
	if (mdie) {
		os_memcpy(pos, mdie, mdie_len);
		pos += mdie_len;
	}
	if (ftie) {
		struct rsn_ftie *_ftie;
		os_memcpy(pos, ftie, ftie_len);
		if (ftie_len < 2 + sizeof(*_ftie)) {
			os_free(buf);
			return -1;
		}
		_ftie = (struct rsn_ftie *) (pos + 2);
		os_memset(_ftie->mic, 0, sizeof(_ftie->mic));
		pos += ftie_len;
	}
	if (ric) {
		os_memcpy(pos, ric, ric_len);
		pos += ric_len;
	}

	wpa_hexdump(MSG_MSGDUMP, "FT: MIC data", buf, pos - buf);
	if (omac1_aes_128(kck, buf, pos - buf, mic)) {
		os_free(buf);
		return -1;
	}

	os_free(buf);

	return 0;
}
#endif /* CONFIG_IEEE80211R */


#ifndef CONFIG_NO_WPA2
static int rsn_selector_to_bitfield(const u8 *s)
{
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_NONE)
		return WPA_CIPHER_NONE;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_WEP40)
		return WPA_CIPHER_WEP40;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_TKIP)
		return WPA_CIPHER_TKIP;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_CCMP)
		return WPA_CIPHER_CCMP;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_WEP104)
		return WPA_CIPHER_WEP104;
#ifdef CONFIG_IEEE80211W
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_AES_128_CMAC)
		return WPA_CIPHER_AES_128_CMAC;
#endif /* CONFIG_IEEE80211W */
	return 0;
}


static int rsn_key_mgmt_to_bitfield(const u8 *s)
{
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_UNSPEC_802_1X)
		return WPA_KEY_MGMT_IEEE8021X;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X)
		return WPA_KEY_MGMT_PSK;
#ifdef CONFIG_IEEE80211R
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FT_802_1X)
		return WPA_KEY_MGMT_FT_IEEE8021X;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FT_PSK)
		return WPA_KEY_MGMT_FT_PSK;
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_IEEE80211W
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_802_1X_SHA256)
		return WPA_KEY_MGMT_IEEE8021X_SHA256;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_PSK_SHA256)
		return WPA_KEY_MGMT_PSK_SHA256;
#endif /* CONFIG_IEEE80211W */
	return 0;
}
#endif /* CONFIG_NO_WPA2 */


/**
 * wpa_parse_wpa_ie_rsn - Parse RSN IE
 * @rsn_ie: Buffer containing RSN IE
 * @rsn_ie_len: RSN IE buffer length (including IE number and length octets)
 * @data: Pointer to structure that will be filled in with parsed data
 * Returns: 0 on success, <0 on failure
 */
int wpa_parse_wpa_ie_rsn(const u8 *rsn_ie, size_t rsn_ie_len,
			 struct wpa_ie_data *data)
{
#ifndef CONFIG_NO_WPA2
	const struct rsn_ie_hdr *hdr;
	const u8 *pos;
	int left;
	int i, count;

	os_memset(data, 0, sizeof(*data));
	data->proto = WPA_PROTO_RSN;
	data->pairwise_cipher = WPA_CIPHER_CCMP;
	data->group_cipher = WPA_CIPHER_CCMP;
	data->key_mgmt = WPA_KEY_MGMT_IEEE8021X;
	data->capabilities = 0;
	data->pmkid = NULL;
	data->num_pmkid = 0;
#ifdef CONFIG_IEEE80211W
	data->mgmt_group_cipher = WPA_CIPHER_AES_128_CMAC;
#else /* CONFIG_IEEE80211W */
	data->mgmt_group_cipher = 0;
#endif /* CONFIG_IEEE80211W */

	if (rsn_ie_len == 0) {
		/* No RSN IE - fail silently */
		return -1;
	}

	if (rsn_ie_len < sizeof(struct rsn_ie_hdr)) {
		wpa_printf(MSG_DEBUG, "%s: ie len too short %lu",
			   __func__, (unsigned long) rsn_ie_len);
		return -1;
	}

	hdr = (const struct rsn_ie_hdr *) rsn_ie;

	if (hdr->elem_id != WLAN_EID_RSN ||
	    hdr->len != rsn_ie_len - 2 ||
	    WPA_GET_LE16(hdr->version) != RSN_VERSION) {
		wpa_printf(MSG_DEBUG, "%s: malformed ie or unknown version",
			   __func__);
		return -2;
	}

	pos = (const u8 *) (hdr + 1);
	left = rsn_ie_len - sizeof(*hdr);

	if (left >= RSN_SELECTOR_LEN) {
		data->group_cipher = rsn_selector_to_bitfield(pos);
#ifdef CONFIG_IEEE80211W
		if (data->group_cipher == WPA_CIPHER_AES_128_CMAC) {
			wpa_printf(MSG_DEBUG, "%s: AES-128-CMAC used as group "
				   "cipher", __func__);
			return -1;
		}
#endif /* CONFIG_IEEE80211W */
		pos += RSN_SELECTOR_LEN;
		left -= RSN_SELECTOR_LEN;
	} else if (left > 0) {
		wpa_printf(MSG_DEBUG, "%s: ie length mismatch, %u too much",
			   __func__, left);
		return -3;
	}

	if (left >= 2) {
		data->pairwise_cipher = 0;
		count = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (count == 0 || left < count * RSN_SELECTOR_LEN) {
			wpa_printf(MSG_DEBUG, "%s: ie count botch (pairwise), "
				   "count %u left %u", __func__, count, left);
			return -4;
		}
		for (i = 0; i < count; i++) {
			data->pairwise_cipher |= rsn_selector_to_bitfield(pos);
			pos += RSN_SELECTOR_LEN;
			left -= RSN_SELECTOR_LEN;
		}
#ifdef CONFIG_IEEE80211W
		if (data->pairwise_cipher & WPA_CIPHER_AES_128_CMAC) {
			wpa_printf(MSG_DEBUG, "%s: AES-128-CMAC used as "
				   "pairwise cipher", __func__);
			return -1;
		}
#endif /* CONFIG_IEEE80211W */
	} else if (left == 1) {
		wpa_printf(MSG_DEBUG, "%s: ie too short (for key mgmt)",
			   __func__);
		return -5;
	}

	if (left >= 2) {
		data->key_mgmt = 0;
		count = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (count == 0 || left < count * RSN_SELECTOR_LEN) {
			wpa_printf(MSG_DEBUG, "%s: ie count botch (key mgmt), "
				   "count %u left %u", __func__, count, left);
			return -6;
		}
		for (i = 0; i < count; i++) {
			data->key_mgmt |= rsn_key_mgmt_to_bitfield(pos);
			pos += RSN_SELECTOR_LEN;
			left -= RSN_SELECTOR_LEN;
		}
	} else if (left == 1) {
		wpa_printf(MSG_DEBUG, "%s: ie too short (for capabilities)",
			   __func__);
		return -7;
	}

	if (left >= 2) {
		data->capabilities = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
	}

	if (left >= 2) {
		data->num_pmkid = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (left < (int) data->num_pmkid * PMKID_LEN) {
			wpa_printf(MSG_DEBUG, "%s: PMKID underflow "
				   "(num_pmkid=%lu left=%d)",
				   __func__, (unsigned long) data->num_pmkid,
				   left);
			data->num_pmkid = 0;
			return -9;
		} else {
			data->pmkid = pos;
			pos += data->num_pmkid * PMKID_LEN;
			left -= data->num_pmkid * PMKID_LEN;
		}
	}

#ifdef CONFIG_IEEE80211W
	if (left >= 4) {
		data->mgmt_group_cipher = rsn_selector_to_bitfield(pos);
		if (data->mgmt_group_cipher != WPA_CIPHER_AES_128_CMAC) {
			wpa_printf(MSG_DEBUG, "%s: Unsupported management "
				   "group cipher 0x%x", __func__,
				   data->mgmt_group_cipher);
			return -10;
		}
		pos += RSN_SELECTOR_LEN;
		left -= RSN_SELECTOR_LEN;
	}
#endif /* CONFIG_IEEE80211W */

	if (left > 0) {
		wpa_printf(MSG_DEBUG, "%s: ie has %u trailing bytes - ignored",
			   __func__, left);
	}

	return 0;
#else /* CONFIG_NO_WPA2 */
	return -1;
#endif /* CONFIG_NO_WPA2 */
}


static int wpa_selector_to_bitfield(const u8 *s)
{
	if (RSN_SELECTOR_GET(s) == WPA_CIPHER_SUITE_NONE)
		return WPA_CIPHER_NONE;
	if (RSN_SELECTOR_GET(s) == WPA_CIPHER_SUITE_WEP40)
		return WPA_CIPHER_WEP40;
	if (RSN_SELECTOR_GET(s) == WPA_CIPHER_SUITE_TKIP)
		return WPA_CIPHER_TKIP;
	if (RSN_SELECTOR_GET(s) == WPA_CIPHER_SUITE_CCMP)
		return WPA_CIPHER_CCMP;
	if (RSN_SELECTOR_GET(s) == WPA_CIPHER_SUITE_WEP104)
		return WPA_CIPHER_WEP104;
	return 0;
}


static int wpa_key_mgmt_to_bitfield(const u8 *s)
{
	if (RSN_SELECTOR_GET(s) == WPA_AUTH_KEY_MGMT_UNSPEC_802_1X)
		return WPA_KEY_MGMT_IEEE8021X;
	if (RSN_SELECTOR_GET(s) == WPA_AUTH_KEY_MGMT_PSK_OVER_802_1X)
		return WPA_KEY_MGMT_PSK;
	if (RSN_SELECTOR_GET(s) == WPA_AUTH_KEY_MGMT_NONE)
		return WPA_KEY_MGMT_WPA_NONE;
	return 0;
}


int wpa_parse_wpa_ie_wpa(const u8 *wpa_ie, size_t wpa_ie_len,
			 struct wpa_ie_data *data)
{
	const struct wpa_ie_hdr *hdr;
	const u8 *pos;
	int left;
	int i, count;

	os_memset(data, 0, sizeof(*data));
	data->proto = WPA_PROTO_WPA;
	data->pairwise_cipher = WPA_CIPHER_TKIP;
	data->group_cipher = WPA_CIPHER_TKIP;
	data->key_mgmt = WPA_KEY_MGMT_IEEE8021X;
	data->capabilities = 0;
	data->pmkid = NULL;
	data->num_pmkid = 0;
	data->mgmt_group_cipher = 0;

	if (wpa_ie_len == 0) {
		/* No WPA IE - fail silently */
		return -1;
	}

	if (wpa_ie_len < sizeof(struct wpa_ie_hdr)) {
		wpa_printf(MSG_DEBUG, "%s: ie len too short %lu",
			   __func__, (unsigned long) wpa_ie_len);
		return -1;
	}

	hdr = (const struct wpa_ie_hdr *) wpa_ie;

	if (hdr->elem_id != WLAN_EID_VENDOR_SPECIFIC ||
	    hdr->len != wpa_ie_len - 2 ||
	    RSN_SELECTOR_GET(hdr->oui) != WPA_OUI_TYPE ||
	    WPA_GET_LE16(hdr->version) != WPA_VERSION) {
		wpa_printf(MSG_DEBUG, "%s: malformed ie or unknown version",
			   __func__);
		return -2;
	}

	pos = (const u8 *) (hdr + 1);
	left = wpa_ie_len - sizeof(*hdr);

	if (left >= WPA_SELECTOR_LEN) {
		data->group_cipher = wpa_selector_to_bitfield(pos);
		pos += WPA_SELECTOR_LEN;
		left -= WPA_SELECTOR_LEN;
	} else if (left > 0) {
		wpa_printf(MSG_DEBUG, "%s: ie length mismatch, %u too much",
			   __func__, left);
		return -3;
	}

	if (left >= 2) {
		data->pairwise_cipher = 0;
		count = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (count == 0 || left < count * WPA_SELECTOR_LEN) {
			wpa_printf(MSG_DEBUG, "%s: ie count botch (pairwise), "
				   "count %u left %u", __func__, count, left);
			return -4;
		}
		for (i = 0; i < count; i++) {
			data->pairwise_cipher |= wpa_selector_to_bitfield(pos);
			pos += WPA_SELECTOR_LEN;
			left -= WPA_SELECTOR_LEN;
		}
	} else if (left == 1) {
		wpa_printf(MSG_DEBUG, "%s: ie too short (for key mgmt)",
			   __func__);
		return -5;
	}

	if (left >= 2) {
		data->key_mgmt = 0;
		count = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (count == 0 || left < count * WPA_SELECTOR_LEN) {
			wpa_printf(MSG_DEBUG, "%s: ie count botch (key mgmt), "
				   "count %u left %u", __func__, count, left);
			return -6;
		}
		for (i = 0; i < count; i++) {
			data->key_mgmt |= wpa_key_mgmt_to_bitfield(pos);
			pos += WPA_SELECTOR_LEN;
			left -= WPA_SELECTOR_LEN;
		}
	} else if (left == 1) {
		wpa_printf(MSG_DEBUG, "%s: ie too short (for capabilities)",
			   __func__);
		return -7;
	}

	if (left >= 2) {
		data->capabilities = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
	}

	if (left > 0) {
		wpa_printf(MSG_DEBUG, "%s: ie has %u trailing bytes - ignored",
			   __func__, left);
	}

	return 0;
}


#ifdef CONFIG_IEEE80211R

/**
 * wpa_derive_pmk_r0 - Derive PMK-R0 and PMKR0Name
 *
 * IEEE Std 802.11r-2008 - 8.5.1.5.3
 */
void wpa_derive_pmk_r0(const u8 *xxkey, size_t xxkey_len,
		       const u8 *ssid, size_t ssid_len,
		       const u8 *mdid, const u8 *r0kh_id, size_t r0kh_id_len,
		       const u8 *s0kh_id, u8 *pmk_r0, u8 *pmk_r0_name)
{
	u8 buf[1 + WPA_MAX_SSID_LEN + MOBILITY_DOMAIN_ID_LEN + 1 +
	       FT_R0KH_ID_MAX_LEN + ETH_ALEN];
	u8 *pos, r0_key_data[48], hash[32];
	const u8 *addr[2];
	size_t len[2];

	/*
	 * R0-Key-Data = KDF-384(XXKey, "FT-R0",
	 *                       SSIDlength || SSID || MDID || R0KHlength ||
	 *                       R0KH-ID || S0KH-ID)
	 * XXKey is either the second 256 bits of MSK or PSK.
	 * PMK-R0 = L(R0-Key-Data, 0, 256)
	 * PMK-R0Name-Salt = L(R0-Key-Data, 256, 128)
	 */
	if (ssid_len > WPA_MAX_SSID_LEN || r0kh_id_len > FT_R0KH_ID_MAX_LEN)
		return;
	pos = buf;
	*pos++ = ssid_len;
	os_memcpy(pos, ssid, ssid_len);
	pos += ssid_len;
	os_memcpy(pos, mdid, MOBILITY_DOMAIN_ID_LEN);
	pos += MOBILITY_DOMAIN_ID_LEN;
	*pos++ = r0kh_id_len;
	os_memcpy(pos, r0kh_id, r0kh_id_len);
	pos += r0kh_id_len;
	os_memcpy(pos, s0kh_id, ETH_ALEN);
	pos += ETH_ALEN;

	sha256_prf(xxkey, xxkey_len, "FT-R0", buf, pos - buf,
		   r0_key_data, sizeof(r0_key_data));
	os_memcpy(pmk_r0, r0_key_data, PMK_LEN);

	/*
	 * PMKR0Name = Truncate-128(SHA-256("FT-R0N" || PMK-R0Name-Salt)
	 */
	addr[0] = (const u8 *) "FT-R0N";
	len[0] = 6;
	addr[1] = r0_key_data + PMK_LEN;
	len[1] = 16;

	sha256_vector(2, addr, len, hash);
	os_memcpy(pmk_r0_name, hash, WPA_PMK_NAME_LEN);
}


/**
 * wpa_derive_pmk_r1_name - Derive PMKR1Name
 *
 * IEEE Std 802.11r-2008 - 8.5.1.5.4
 */
void wpa_derive_pmk_r1_name(const u8 *pmk_r0_name, const u8 *r1kh_id,
			    const u8 *s1kh_id, u8 *pmk_r1_name)
{
	u8 hash[32];
	const u8 *addr[4];
	size_t len[4];

	/*
	 * PMKR1Name = Truncate-128(SHA-256("FT-R1N" || PMKR0Name ||
	 *                                  R1KH-ID || S1KH-ID))
	 */
	addr[0] = (const u8 *) "FT-R1N";
	len[0] = 6;
	addr[1] = pmk_r0_name;
	len[1] = WPA_PMK_NAME_LEN;
	addr[2] = r1kh_id;
	len[2] = FT_R1KH_ID_LEN;
	addr[3] = s1kh_id;
	len[3] = ETH_ALEN;

	sha256_vector(4, addr, len, hash);
	os_memcpy(pmk_r1_name, hash, WPA_PMK_NAME_LEN);
}


/**
 * wpa_derive_pmk_r1 - Derive PMK-R1 and PMKR1Name from PMK-R0
 *
 * IEEE Std 802.11r-2008 - 8.5.1.5.4
 */
void wpa_derive_pmk_r1(const u8 *pmk_r0, const u8 *pmk_r0_name,
		       const u8 *r1kh_id, const u8 *s1kh_id,
		       u8 *pmk_r1, u8 *pmk_r1_name)
{
	u8 buf[FT_R1KH_ID_LEN + ETH_ALEN];
	u8 *pos;

	/* PMK-R1 = KDF-256(PMK-R0, "FT-R1", R1KH-ID || S1KH-ID) */
	pos = buf;
	os_memcpy(pos, r1kh_id, FT_R1KH_ID_LEN);
	pos += FT_R1KH_ID_LEN;
	os_memcpy(pos, s1kh_id, ETH_ALEN);
	pos += ETH_ALEN;

	sha256_prf(pmk_r0, PMK_LEN, "FT-R1", buf, pos - buf, pmk_r1, PMK_LEN);

	wpa_derive_pmk_r1_name(pmk_r0_name, r1kh_id, s1kh_id, pmk_r1_name);
}


/**
 * wpa_pmk_r1_to_ptk - Derive PTK and PTKName from PMK-R1
 *
 * IEEE Std 802.11r-2008 - 8.5.1.5.5
 */
void wpa_pmk_r1_to_ptk(const u8 *pmk_r1, const u8 *snonce, const u8 *anonce,
		       const u8 *sta_addr, const u8 *bssid,
		       const u8 *pmk_r1_name,
		       u8 *ptk, size_t ptk_len, u8 *ptk_name)
{
	u8 buf[2 * WPA_NONCE_LEN + 2 * ETH_ALEN];
	u8 *pos, hash[32];
	const u8 *addr[6];
	size_t len[6];

	/*
	 * PTK = KDF-PTKLen(PMK-R1, "FT-PTK", SNonce || ANonce ||
	 *                  BSSID || STA-ADDR)
	 */
	pos = buf;
	os_memcpy(pos, snonce, WPA_NONCE_LEN);
	pos += WPA_NONCE_LEN;
	os_memcpy(pos, anonce, WPA_NONCE_LEN);
	pos += WPA_NONCE_LEN;
	os_memcpy(pos, bssid, ETH_ALEN);
	pos += ETH_ALEN;
	os_memcpy(pos, sta_addr, ETH_ALEN);
	pos += ETH_ALEN;

	sha256_prf(pmk_r1, PMK_LEN, "FT-PTK", buf, pos - buf, ptk, ptk_len);

	/*
	 * PTKName = Truncate-128(SHA-256(PMKR1Name || "FT-PTKN" || SNonce ||
	 *                                ANonce || BSSID || STA-ADDR))
	 */
	addr[0] = pmk_r1_name;
	len[0] = WPA_PMK_NAME_LEN;
	addr[1] = (const u8 *) "FT-PTKN";
	len[1] = 7;
	addr[2] = snonce;
	len[2] = WPA_NONCE_LEN;
	addr[3] = anonce;
	len[3] = WPA_NONCE_LEN;
	addr[4] = bssid;
	len[4] = ETH_ALEN;
	addr[5] = sta_addr;
	len[5] = ETH_ALEN;

	sha256_vector(6, addr, len, hash);
	os_memcpy(ptk_name, hash, WPA_PMK_NAME_LEN);
}

#endif /* CONFIG_IEEE80211R */


/**
 * rsn_pmkid - Calculate PMK identifier
 * @pmk: Pairwise master key
 * @pmk_len: Length of pmk in bytes
 * @aa: Authenticator address
 * @spa: Supplicant address
 * @pmkid: Buffer for PMKID
 * @use_sha256: Whether to use SHA256-based KDF
 *
 * IEEE Std 802.11i-2004 - 8.5.1.2 Pairwise key hierarchy
 * PMKID = HMAC-SHA1-128(PMK, "PMK Name" || AA || SPA)
 */
void rsn_pmkid(const u8 *pmk, size_t pmk_len, const u8 *aa, const u8 *spa,
	       u8 *pmkid, int use_sha256)
{
	char *title = "PMK Name";
	const u8 *addr[3];
	const size_t len[3] = { 8, ETH_ALEN, ETH_ALEN };
	unsigned char hash[SHA256_MAC_LEN];

	addr[0] = (u8 *) title;
	addr[1] = aa;
	addr[2] = spa;

#ifdef CONFIG_IEEE80211W
	if (use_sha256)
		hmac_sha256_vector(pmk, pmk_len, 3, addr, len, hash);
	else
#endif /* CONFIG_IEEE80211W */
		hmac_sha1_vector(pmk, pmk_len, 3, addr, len, hash);
	os_memcpy(pmkid, hash, PMKID_LEN);
}


/**
 * wpa_cipher_txt - Convert cipher suite to a text string
 * @cipher: Cipher suite (WPA_CIPHER_* enum)
 * Returns: Pointer to a text string of the cipher suite name
 */
const char * wpa_cipher_txt(int cipher)
{
	switch (cipher) {
	case WPA_CIPHER_NONE:
		return "NONE";
	case WPA_CIPHER_WEP40:
		return "WEP-40";
	case WPA_CIPHER_WEP104:
		return "WEP-104";
	case WPA_CIPHER_TKIP:
		return "TKIP";
	case WPA_CIPHER_CCMP:
		return "CCMP";
	case WPA_CIPHER_CCMP | WPA_CIPHER_TKIP:
		return "CCMP+TKIP";
	default:
		return "UNKNOWN";
	}
}


/**
 * wpa_key_mgmt_txt - Convert key management suite to a text string
 * @key_mgmt: Key management suite (WPA_KEY_MGMT_* enum)
 * @proto: WPA/WPA2 version (WPA_PROTO_*)
 * Returns: Pointer to a text string of the key management suite name
 */
const char * wpa_key_mgmt_txt(int key_mgmt, int proto)
{
	switch (key_mgmt) {
	case WPA_KEY_MGMT_IEEE8021X:
		if (proto == (WPA_PROTO_RSN | WPA_PROTO_WPA))
			return "WPA2+WPA/IEEE 802.1X/EAP";
		return proto == WPA_PROTO_RSN ?
			"WPA2/IEEE 802.1X/EAP" : "WPA/IEEE 802.1X/EAP";
	case WPA_KEY_MGMT_PSK:
		if (proto == (WPA_PROTO_RSN | WPA_PROTO_WPA))
			return "WPA2-PSK+WPA-PSK";
		return proto == WPA_PROTO_RSN ?
			"WPA2-PSK" : "WPA-PSK";
	case WPA_KEY_MGMT_NONE:
		return "NONE";
	case WPA_KEY_MGMT_IEEE8021X_NO_WPA:
		return "IEEE 802.1X (no WPA)";
#ifdef CONFIG_IEEE80211R
	case WPA_KEY_MGMT_FT_IEEE8021X:
		return "FT-EAP";
	case WPA_KEY_MGMT_FT_PSK:
		return "FT-PSK";
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_IEEE80211W
	case WPA_KEY_MGMT_IEEE8021X_SHA256:
		return "WPA2-EAP-SHA256";
	case WPA_KEY_MGMT_PSK_SHA256:
		return "WPA2-PSK-SHA256";
#endif /* CONFIG_IEEE80211W */
	default:
		return "UNKNOWN";
	}
}


int wpa_compare_rsn_ie(int ft_initial_assoc,
		       const u8 *ie1, size_t ie1len,
		       const u8 *ie2, size_t ie2len)
{
	if (ie1 == NULL || ie2 == NULL)
		return -1;

	if (ie1len == ie2len && os_memcmp(ie1, ie2, ie1len) == 0)
		return 0; /* identical IEs */

#ifdef CONFIG_IEEE80211R
	if (ft_initial_assoc) {
		struct wpa_ie_data ie1d, ie2d;
		/*
		 * The PMKID-List in RSN IE is different between Beacon/Probe
		 * Response/(Re)Association Request frames and EAPOL-Key
		 * messages in FT initial mobility domain association. Allow
		 * for this, but verify that other parts of the RSN IEs are
		 * identical.
		 */
		if (wpa_parse_wpa_ie_rsn(ie1, ie1len, &ie1d) < 0 ||
		    wpa_parse_wpa_ie_rsn(ie2, ie2len, &ie2d) < 0)
			return -1;
		if (ie1d.proto == ie2d.proto &&
		    ie1d.pairwise_cipher == ie2d.pairwise_cipher &&
		    ie1d.group_cipher == ie2d.group_cipher &&
		    ie1d.key_mgmt == ie2d.key_mgmt &&
		    ie1d.capabilities == ie2d.capabilities &&
		    ie1d.mgmt_group_cipher == ie2d.mgmt_group_cipher)
			return 0;
	}
#endif /* CONFIG_IEEE80211R */

	return -1;
}


#ifdef CONFIG_IEEE80211R
int wpa_insert_pmkid(u8 *ies, size_t ies_len, const u8 *pmkid)
{
	u8 *start, *end, *rpos, *rend;
	int added = 0;

	start = ies;
	end = ies + ies_len;

	while (start < end) {
		if (*start == WLAN_EID_RSN)
			break;
		start += 2 + start[1];
	}
	if (start >= end) {
		wpa_printf(MSG_ERROR, "FT: Could not find RSN IE in "
			   "IEs data");
		return -1;
	}
	wpa_hexdump(MSG_DEBUG, "FT: RSN IE before modification",
		    start, 2 + start[1]);

	/* Find start of PMKID-Count */
	rpos = start + 2;
	rend = rpos + start[1];

	/* Skip Version and Group Data Cipher Suite */
	rpos += 2 + 4;
	/* Skip Pairwise Cipher Suite Count and List */
	rpos += 2 + WPA_GET_LE16(rpos) * RSN_SELECTOR_LEN;
	/* Skip AKM Suite Count and List */
	rpos += 2 + WPA_GET_LE16(rpos) * RSN_SELECTOR_LEN;

	if (rpos == rend) {
		/* Add RSN Capabilities */
		os_memmove(rpos + 2, rpos, end - rpos);
		*rpos++ = 0;
		*rpos++ = 0;
	} else {
		/* Skip RSN Capabilities */
		rpos += 2;
		if (rpos > rend) {
			wpa_printf(MSG_ERROR, "FT: Could not parse RSN IE in "
				   "IEs data");
			return -1;
		}
	}

	if (rpos == rend) {
		/* No PMKID-Count field included; add it */
		os_memmove(rpos + 2 + PMKID_LEN, rpos, end - rpos);
		WPA_PUT_LE16(rpos, 1);
		rpos += 2;
		os_memcpy(rpos, pmkid, PMKID_LEN);
		added += 2 + PMKID_LEN;
		start[1] += 2 + PMKID_LEN;
	} else {
		/* PMKID-Count was included; use it */
		if (WPA_GET_LE16(rpos) != 0) {
			wpa_printf(MSG_ERROR, "FT: Unexpected PMKID "
				   "in RSN IE in EAPOL-Key data");
			return -1;
		}
		WPA_PUT_LE16(rpos, 1);
		rpos += 2;
		os_memmove(rpos + PMKID_LEN, rpos, end - rpos);
		os_memcpy(rpos, pmkid, PMKID_LEN);
		added += PMKID_LEN;
		start[1] += PMKID_LEN;
	}

	wpa_hexdump(MSG_DEBUG, "FT: RSN IE after modification "
		    "(PMKID inserted)", start, 2 + start[1]);

	return added;
}
#endif /* CONFIG_IEEE80211R */
