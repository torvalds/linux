/*
 * WPA/RSN - Shared functions for supplicant and authenticator
 * Copyright (c) 2002-2018, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/md5.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "crypto/sha384.h"
#include "crypto/sha512.h"
#include "crypto/aes_wrap.h"
#include "crypto/crypto.h"
#include "ieee802_11_defs.h"
#include "defs.h"
#include "wpa_common.h"


static unsigned int wpa_kck_len(int akmp, size_t pmk_len)
{
	switch (akmp) {
	case WPA_KEY_MGMT_IEEE8021X_SUITE_B_192:
	case WPA_KEY_MGMT_FT_IEEE8021X_SHA384:
		return 24;
	case WPA_KEY_MGMT_FILS_SHA256:
	case WPA_KEY_MGMT_FT_FILS_SHA256:
	case WPA_KEY_MGMT_FILS_SHA384:
	case WPA_KEY_MGMT_FT_FILS_SHA384:
		return 0;
	case WPA_KEY_MGMT_DPP:
		return pmk_len / 2;
	case WPA_KEY_MGMT_OWE:
		return pmk_len / 2;
	default:
		return 16;
	}
}


#ifdef CONFIG_IEEE80211R
static unsigned int wpa_kck2_len(int akmp)
{
	switch (akmp) {
	case WPA_KEY_MGMT_FT_FILS_SHA256:
		return 16;
	case WPA_KEY_MGMT_FT_FILS_SHA384:
		return 24;
	default:
		return 0;
	}
}
#endif /* CONFIG_IEEE80211R */


static unsigned int wpa_kek_len(int akmp, size_t pmk_len)
{
	switch (akmp) {
	case WPA_KEY_MGMT_FILS_SHA384:
	case WPA_KEY_MGMT_FT_FILS_SHA384:
		return 64;
	case WPA_KEY_MGMT_IEEE8021X_SUITE_B_192:
	case WPA_KEY_MGMT_FILS_SHA256:
	case WPA_KEY_MGMT_FT_FILS_SHA256:
	case WPA_KEY_MGMT_FT_IEEE8021X_SHA384:
		return 32;
	case WPA_KEY_MGMT_DPP:
		return pmk_len <= 32 ? 16 : 32;
	case WPA_KEY_MGMT_OWE:
		return pmk_len <= 32 ? 16 : 32;
	default:
		return 16;
	}
}


#ifdef CONFIG_IEEE80211R
static unsigned int wpa_kek2_len(int akmp)
{
	switch (akmp) {
	case WPA_KEY_MGMT_FT_FILS_SHA256:
		return 16;
	case WPA_KEY_MGMT_FT_FILS_SHA384:
		return 32;
	default:
		return 0;
	}
}
#endif /* CONFIG_IEEE80211R */


unsigned int wpa_mic_len(int akmp, size_t pmk_len)
{
	switch (akmp) {
	case WPA_KEY_MGMT_IEEE8021X_SUITE_B_192:
	case WPA_KEY_MGMT_FT_IEEE8021X_SHA384:
		return 24;
	case WPA_KEY_MGMT_FILS_SHA256:
	case WPA_KEY_MGMT_FILS_SHA384:
	case WPA_KEY_MGMT_FT_FILS_SHA256:
	case WPA_KEY_MGMT_FT_FILS_SHA384:
		return 0;
	case WPA_KEY_MGMT_DPP:
		return pmk_len / 2;
	case WPA_KEY_MGMT_OWE:
		return pmk_len / 2;
	default:
		return 16;
	}
}


/**
 * wpa_use_akm_defined - Is AKM-defined Key Descriptor Version used
 * @akmp: WPA_KEY_MGMT_* used in key derivation
 * Returns: 1 if AKM-defined Key Descriptor Version is used; 0 otherwise
 */
int wpa_use_akm_defined(int akmp)
{
	return akmp == WPA_KEY_MGMT_OSEN ||
		akmp == WPA_KEY_MGMT_OWE ||
		akmp == WPA_KEY_MGMT_DPP ||
		akmp == WPA_KEY_MGMT_FT_IEEE8021X_SHA384 ||
		wpa_key_mgmt_sae(akmp) ||
		wpa_key_mgmt_suite_b(akmp) ||
		wpa_key_mgmt_fils(akmp);
}


/**
 * wpa_use_cmac - Is CMAC integrity algorithm used for EAPOL-Key MIC
 * @akmp: WPA_KEY_MGMT_* used in key derivation
 * Returns: 1 if CMAC is used; 0 otherwise
 */
int wpa_use_cmac(int akmp)
{
	return akmp == WPA_KEY_MGMT_OSEN ||
		akmp == WPA_KEY_MGMT_OWE ||
		akmp == WPA_KEY_MGMT_DPP ||
		wpa_key_mgmt_ft(akmp) ||
		wpa_key_mgmt_sha256(akmp) ||
		wpa_key_mgmt_sae(akmp) ||
		wpa_key_mgmt_suite_b(akmp);
}


/**
 * wpa_use_aes_key_wrap - Is AES Keywrap algorithm used for EAPOL-Key Key Data
 * @akmp: WPA_KEY_MGMT_* used in key derivation
 * Returns: 1 if AES Keywrap is used; 0 otherwise
 *
 * Note: AKM 00-0F-AC:1 and 00-0F-AC:2 have special rules for selecting whether
 * to use AES Keywrap based on the negotiated pairwise cipher. This function
 * does not cover those special cases.
 */
int wpa_use_aes_key_wrap(int akmp)
{
	return akmp == WPA_KEY_MGMT_OSEN ||
		akmp == WPA_KEY_MGMT_OWE ||
		akmp == WPA_KEY_MGMT_DPP ||
		wpa_key_mgmt_ft(akmp) ||
		wpa_key_mgmt_sha256(akmp) ||
		wpa_key_mgmt_sae(akmp) ||
		wpa_key_mgmt_suite_b(akmp);
}


/**
 * wpa_eapol_key_mic - Calculate EAPOL-Key MIC
 * @key: EAPOL-Key Key Confirmation Key (KCK)
 * @key_len: KCK length in octets
 * @akmp: WPA_KEY_MGMT_* used in key derivation
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
int wpa_eapol_key_mic(const u8 *key, size_t key_len, int akmp, int ver,
		      const u8 *buf, size_t len, u8 *mic)
{
	u8 hash[SHA512_MAC_LEN];

	if (key_len == 0) {
		wpa_printf(MSG_DEBUG,
			   "WPA: KCK not set - cannot calculate MIC");
		return -1;
	}

	switch (ver) {
#ifndef CONFIG_FIPS
	case WPA_KEY_INFO_TYPE_HMAC_MD5_RC4:
		wpa_printf(MSG_DEBUG, "WPA: EAPOL-Key MIC using HMAC-MD5");
		return hmac_md5(key, key_len, buf, len, mic);
#endif /* CONFIG_FIPS */
	case WPA_KEY_INFO_TYPE_HMAC_SHA1_AES:
		wpa_printf(MSG_DEBUG, "WPA: EAPOL-Key MIC using HMAC-SHA1");
		if (hmac_sha1(key, key_len, buf, len, hash))
			return -1;
		os_memcpy(mic, hash, MD5_MAC_LEN);
		break;
#if defined(CONFIG_IEEE80211R) || defined(CONFIG_IEEE80211W)
	case WPA_KEY_INFO_TYPE_AES_128_CMAC:
		wpa_printf(MSG_DEBUG, "WPA: EAPOL-Key MIC using AES-CMAC");
		return omac1_aes_128(key, buf, len, mic);
#endif /* CONFIG_IEEE80211R || CONFIG_IEEE80211W */
	case WPA_KEY_INFO_TYPE_AKM_DEFINED:
		switch (akmp) {
#ifdef CONFIG_SAE
		case WPA_KEY_MGMT_SAE:
		case WPA_KEY_MGMT_FT_SAE:
			wpa_printf(MSG_DEBUG,
				   "WPA: EAPOL-Key MIC using AES-CMAC (AKM-defined - SAE)");
			return omac1_aes_128(key, buf, len, mic);
#endif /* CONFIG_SAE */
#ifdef CONFIG_HS20
		case WPA_KEY_MGMT_OSEN:
			wpa_printf(MSG_DEBUG,
				   "WPA: EAPOL-Key MIC using AES-CMAC (AKM-defined - OSEN)");
			return omac1_aes_128(key, buf, len, mic);
#endif /* CONFIG_HS20 */
#ifdef CONFIG_SUITEB
		case WPA_KEY_MGMT_IEEE8021X_SUITE_B:
			wpa_printf(MSG_DEBUG,
				   "WPA: EAPOL-Key MIC using HMAC-SHA256 (AKM-defined - Suite B)");
			if (hmac_sha256(key, key_len, buf, len, hash))
				return -1;
			os_memcpy(mic, hash, MD5_MAC_LEN);
			break;
#endif /* CONFIG_SUITEB */
#ifdef CONFIG_SUITEB192
		case WPA_KEY_MGMT_IEEE8021X_SUITE_B_192:
			wpa_printf(MSG_DEBUG,
				   "WPA: EAPOL-Key MIC using HMAC-SHA384 (AKM-defined - Suite B 192-bit)");
			if (hmac_sha384(key, key_len, buf, len, hash))
				return -1;
			os_memcpy(mic, hash, 24);
			break;
#endif /* CONFIG_SUITEB192 */
#ifdef CONFIG_OWE
		case WPA_KEY_MGMT_OWE:
			wpa_printf(MSG_DEBUG,
				   "WPA: EAPOL-Key MIC using HMAC-SHA%u (AKM-defined - OWE)",
				   (unsigned int) key_len * 8 * 2);
			if (key_len == 128 / 8) {
				if (hmac_sha256(key, key_len, buf, len, hash))
					return -1;
			} else if (key_len == 192 / 8) {
				if (hmac_sha384(key, key_len, buf, len, hash))
					return -1;
			} else if (key_len == 256 / 8) {
				if (hmac_sha512(key, key_len, buf, len, hash))
					return -1;
			} else {
				wpa_printf(MSG_INFO,
					   "OWE: Unsupported KCK length: %u",
					   (unsigned int) key_len);
				return -1;
			}
			os_memcpy(mic, hash, key_len);
			break;
#endif /* CONFIG_OWE */
#ifdef CONFIG_DPP
		case WPA_KEY_MGMT_DPP:
			wpa_printf(MSG_DEBUG,
				   "WPA: EAPOL-Key MIC using HMAC-SHA%u (AKM-defined - DPP)",
				   (unsigned int) key_len * 8 * 2);
			if (key_len == 128 / 8) {
				if (hmac_sha256(key, key_len, buf, len, hash))
					return -1;
			} else if (key_len == 192 / 8) {
				if (hmac_sha384(key, key_len, buf, len, hash))
					return -1;
			} else if (key_len == 256 / 8) {
				if (hmac_sha512(key, key_len, buf, len, hash))
					return -1;
			} else {
				wpa_printf(MSG_INFO,
					   "DPP: Unsupported KCK length: %u",
					   (unsigned int) key_len);
				return -1;
			}
			os_memcpy(mic, hash, key_len);
			break;
#endif /* CONFIG_DPP */
#if defined(CONFIG_IEEE80211R) && defined(CONFIG_SHA384)
		case WPA_KEY_MGMT_FT_IEEE8021X_SHA384:
			wpa_printf(MSG_DEBUG,
				   "WPA: EAPOL-Key MIC using HMAC-SHA384 (AKM-defined - FT 802.1X SHA384)");
			if (hmac_sha384(key, key_len, buf, len, hash))
				return -1;
			os_memcpy(mic, hash, 24);
			break;
#endif /* CONFIG_IEEE80211R && CONFIG_SHA384 */
		default:
			wpa_printf(MSG_DEBUG,
				   "WPA: EAPOL-Key MIC algorithm not known (AKM-defined - akmp=0x%x)",
				   akmp);
			return -1;
		}
		break;
	default:
		wpa_printf(MSG_DEBUG,
			   "WPA: EAPOL-Key MIC algorithm not known (ver=%d)",
			   ver);
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
 * @akmp: Negotiated AKM
 * @cipher: Negotiated pairwise cipher
 * Returns: 0 on success, -1 on failure
 *
 * IEEE Std 802.11i-2004 - 8.5.1.2 Pairwise key hierarchy
 * PTK = PRF-X(PMK, "Pairwise key expansion",
 *             Min(AA, SA) || Max(AA, SA) ||
 *             Min(ANonce, SNonce) || Max(ANonce, SNonce))
 */
int wpa_pmk_to_ptk(const u8 *pmk, size_t pmk_len, const char *label,
		   const u8 *addr1, const u8 *addr2,
		   const u8 *nonce1, const u8 *nonce2,
		   struct wpa_ptk *ptk, int akmp, int cipher)
{
	u8 data[2 * ETH_ALEN + 2 * WPA_NONCE_LEN];
	u8 tmp[WPA_KCK_MAX_LEN + WPA_KEK_MAX_LEN + WPA_TK_MAX_LEN];
	size_t ptk_len;

	if (pmk_len == 0) {
		wpa_printf(MSG_ERROR, "WPA: No PMK set for PTK derivation");
		return -1;
	}

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

	ptk->kck_len = wpa_kck_len(akmp, pmk_len);
	ptk->kek_len = wpa_kek_len(akmp, pmk_len);
	ptk->tk_len = wpa_cipher_key_len(cipher);
	if (ptk->tk_len == 0) {
		wpa_printf(MSG_ERROR,
			   "WPA: Unsupported cipher (0x%x) used in PTK derivation",
			   cipher);
		return -1;
	}
	ptk_len = ptk->kck_len + ptk->kek_len + ptk->tk_len;

	if (wpa_key_mgmt_sha384(akmp)) {
#if defined(CONFIG_SUITEB192) || defined(CONFIG_FILS)
		wpa_printf(MSG_DEBUG, "WPA: PTK derivation using PRF(SHA384)");
		if (sha384_prf(pmk, pmk_len, label, data, sizeof(data),
			       tmp, ptk_len) < 0)
			return -1;
#else /* CONFIG_SUITEB192 || CONFIG_FILS */
		return -1;
#endif /* CONFIG_SUITEB192 || CONFIG_FILS */
	} else if (wpa_key_mgmt_sha256(akmp) || akmp == WPA_KEY_MGMT_OWE) {
#if defined(CONFIG_IEEE80211W) || defined(CONFIG_SAE) || defined(CONFIG_FILS)
		wpa_printf(MSG_DEBUG, "WPA: PTK derivation using PRF(SHA256)");
		if (sha256_prf(pmk, pmk_len, label, data, sizeof(data),
			       tmp, ptk_len) < 0)
			return -1;
#else /* CONFIG_IEEE80211W or CONFIG_SAE or CONFIG_FILS */
		return -1;
#endif /* CONFIG_IEEE80211W or CONFIG_SAE or CONFIG_FILS */
#ifdef CONFIG_DPP
	} else if (akmp == WPA_KEY_MGMT_DPP && pmk_len == 32) {
		wpa_printf(MSG_DEBUG, "WPA: PTK derivation using PRF(SHA256)");
		if (sha256_prf(pmk, pmk_len, label, data, sizeof(data),
			       tmp, ptk_len) < 0)
			return -1;
	} else if (akmp == WPA_KEY_MGMT_DPP && pmk_len == 48) {
		wpa_printf(MSG_DEBUG, "WPA: PTK derivation using PRF(SHA384)");
		if (sha384_prf(pmk, pmk_len, label, data, sizeof(data),
			       tmp, ptk_len) < 0)
			return -1;
	} else if (akmp == WPA_KEY_MGMT_DPP && pmk_len == 64) {
		wpa_printf(MSG_DEBUG, "WPA: PTK derivation using PRF(SHA512)");
		if (sha512_prf(pmk, pmk_len, label, data, sizeof(data),
			       tmp, ptk_len) < 0)
			return -1;
	} else if (akmp == WPA_KEY_MGMT_DPP) {
		wpa_printf(MSG_INFO, "DPP: Unknown PMK length %u",
			   (unsigned int) pmk_len);
		return -1;
#endif /* CONFIG_DPP */
	} else {
		wpa_printf(MSG_DEBUG, "WPA: PTK derivation using PRF(SHA1)");
		if (sha1_prf(pmk, pmk_len, label, data, sizeof(data), tmp,
			     ptk_len) < 0)
			return -1;
	}

	wpa_printf(MSG_DEBUG, "WPA: PTK derivation - A1=" MACSTR " A2=" MACSTR,
		   MAC2STR(addr1), MAC2STR(addr2));
	wpa_hexdump(MSG_DEBUG, "WPA: Nonce1", nonce1, WPA_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "WPA: Nonce2", nonce2, WPA_NONCE_LEN);
	wpa_hexdump_key(MSG_DEBUG, "WPA: PMK", pmk, pmk_len);
	wpa_hexdump_key(MSG_DEBUG, "WPA: PTK", tmp, ptk_len);

	os_memcpy(ptk->kck, tmp, ptk->kck_len);
	wpa_hexdump_key(MSG_DEBUG, "WPA: KCK", ptk->kck, ptk->kck_len);

	os_memcpy(ptk->kek, tmp + ptk->kck_len, ptk->kek_len);
	wpa_hexdump_key(MSG_DEBUG, "WPA: KEK", ptk->kek, ptk->kek_len);

	os_memcpy(ptk->tk, tmp + ptk->kck_len + ptk->kek_len, ptk->tk_len);
	wpa_hexdump_key(MSG_DEBUG, "WPA: TK", ptk->tk, ptk->tk_len);

	ptk->kek2_len = 0;
	ptk->kck2_len = 0;

	os_memset(tmp, 0, sizeof(tmp));
	return 0;
}

#ifdef CONFIG_FILS

int fils_rmsk_to_pmk(int akmp, const u8 *rmsk, size_t rmsk_len,
		     const u8 *snonce, const u8 *anonce, const u8 *dh_ss,
		     size_t dh_ss_len, u8 *pmk, size_t *pmk_len)
{
	u8 nonces[2 * FILS_NONCE_LEN];
	const u8 *addr[2];
	size_t len[2];
	size_t num_elem;
	int res;

	/* PMK = HMAC-Hash(SNonce || ANonce, rMSK [ || DHss ]) */
	wpa_printf(MSG_DEBUG, "FILS: rMSK to PMK derivation");

	if (wpa_key_mgmt_sha384(akmp))
		*pmk_len = SHA384_MAC_LEN;
	else if (wpa_key_mgmt_sha256(akmp))
		*pmk_len = SHA256_MAC_LEN;
	else
		return -1;

	wpa_hexdump_key(MSG_DEBUG, "FILS: rMSK", rmsk, rmsk_len);
	wpa_hexdump(MSG_DEBUG, "FILS: SNonce", snonce, FILS_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "FILS: ANonce", anonce, FILS_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "FILS: DHss", dh_ss, dh_ss_len);

	os_memcpy(nonces, snonce, FILS_NONCE_LEN);
	os_memcpy(&nonces[FILS_NONCE_LEN], anonce, FILS_NONCE_LEN);
	addr[0] = rmsk;
	len[0] = rmsk_len;
	num_elem = 1;
	if (dh_ss) {
		addr[1] = dh_ss;
		len[1] = dh_ss_len;
		num_elem++;
	}
	if (wpa_key_mgmt_sha384(akmp))
		res = hmac_sha384_vector(nonces, 2 * FILS_NONCE_LEN, num_elem,
					 addr, len, pmk);
	else
		res = hmac_sha256_vector(nonces, 2 * FILS_NONCE_LEN, num_elem,
					 addr, len, pmk);
	if (res == 0)
		wpa_hexdump_key(MSG_DEBUG, "FILS: PMK", pmk, *pmk_len);
	else
		*pmk_len = 0;
	return res;
}


int fils_pmkid_erp(int akmp, const u8 *reauth, size_t reauth_len,
		   u8 *pmkid)
{
	const u8 *addr[1];
	size_t len[1];
	u8 hash[SHA384_MAC_LEN];
	int res;

	/* PMKID = Truncate-128(Hash(EAP-Initiate/Reauth)) */
	addr[0] = reauth;
	len[0] = reauth_len;
	if (wpa_key_mgmt_sha384(akmp))
		res = sha384_vector(1, addr, len, hash);
	else if (wpa_key_mgmt_sha256(akmp))
		res = sha256_vector(1, addr, len, hash);
	else
		return -1;
	if (res)
		return res;
	os_memcpy(pmkid, hash, PMKID_LEN);
	wpa_hexdump(MSG_DEBUG, "FILS: PMKID", pmkid, PMKID_LEN);
	return 0;
}


int fils_pmk_to_ptk(const u8 *pmk, size_t pmk_len, const u8 *spa, const u8 *aa,
		    const u8 *snonce, const u8 *anonce, const u8 *dhss,
		    size_t dhss_len, struct wpa_ptk *ptk,
		    u8 *ick, size_t *ick_len, int akmp, int cipher,
		    u8 *fils_ft, size_t *fils_ft_len)
{
	u8 *data, *pos;
	size_t data_len;
	u8 tmp[FILS_ICK_MAX_LEN + WPA_KEK_MAX_LEN + WPA_TK_MAX_LEN +
	       FILS_FT_MAX_LEN];
	size_t key_data_len;
	const char *label = "FILS PTK Derivation";
	int ret = -1;

	/*
	 * FILS-Key-Data = PRF-X(PMK, "FILS PTK Derivation",
	 *                       SPA || AA || SNonce || ANonce [ || DHss ])
	 * ICK = L(FILS-Key-Data, 0, ICK_bits)
	 * KEK = L(FILS-Key-Data, ICK_bits, KEK_bits)
	 * TK = L(FILS-Key-Data, ICK_bits + KEK_bits, TK_bits)
	 * If doing FT initial mobility domain association:
	 * FILS-FT = L(FILS-Key-Data, ICK_bits + KEK_bits + TK_bits,
	 *             FILS-FT_bits)
	 */
	data_len = 2 * ETH_ALEN + 2 * FILS_NONCE_LEN + dhss_len;
	data = os_malloc(data_len);
	if (!data)
		goto err;
	pos = data;
	os_memcpy(pos, spa, ETH_ALEN);
	pos += ETH_ALEN;
	os_memcpy(pos, aa, ETH_ALEN);
	pos += ETH_ALEN;
	os_memcpy(pos, snonce, FILS_NONCE_LEN);
	pos += FILS_NONCE_LEN;
	os_memcpy(pos, anonce, FILS_NONCE_LEN);
	pos += FILS_NONCE_LEN;
	if (dhss)
		os_memcpy(pos, dhss, dhss_len);

	ptk->kck_len = 0;
	ptk->kek_len = wpa_kek_len(akmp, pmk_len);
	ptk->tk_len = wpa_cipher_key_len(cipher);
	if (wpa_key_mgmt_sha384(akmp))
		*ick_len = 48;
	else if (wpa_key_mgmt_sha256(akmp))
		*ick_len = 32;
	else
		goto err;
	key_data_len = *ick_len + ptk->kek_len + ptk->tk_len;

	if (fils_ft && fils_ft_len) {
		if (akmp == WPA_KEY_MGMT_FT_FILS_SHA256) {
			*fils_ft_len = 32;
		} else if (akmp == WPA_KEY_MGMT_FT_FILS_SHA384) {
			*fils_ft_len = 48;
		} else {
			*fils_ft_len = 0;
			fils_ft = NULL;
		}
		key_data_len += *fils_ft_len;
	}

	if (wpa_key_mgmt_sha384(akmp)) {
		wpa_printf(MSG_DEBUG, "FILS: PTK derivation using PRF(SHA384)");
		if (sha384_prf(pmk, pmk_len, label, data, data_len,
			       tmp, key_data_len) < 0)
			goto err;
	} else {
		wpa_printf(MSG_DEBUG, "FILS: PTK derivation using PRF(SHA256)");
		if (sha256_prf(pmk, pmk_len, label, data, data_len,
			       tmp, key_data_len) < 0)
			goto err;
	}

	wpa_printf(MSG_DEBUG, "FILS: PTK derivation - SPA=" MACSTR
		   " AA=" MACSTR, MAC2STR(spa), MAC2STR(aa));
	wpa_hexdump(MSG_DEBUG, "FILS: SNonce", snonce, FILS_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "FILS: ANonce", anonce, FILS_NONCE_LEN);
	if (dhss)
		wpa_hexdump_key(MSG_DEBUG, "FILS: DHss", dhss, dhss_len);
	wpa_hexdump_key(MSG_DEBUG, "FILS: PMK", pmk, pmk_len);
	wpa_hexdump_key(MSG_DEBUG, "FILS: FILS-Key-Data", tmp, key_data_len);

	os_memcpy(ick, tmp, *ick_len);
	wpa_hexdump_key(MSG_DEBUG, "FILS: ICK", ick, *ick_len);

	os_memcpy(ptk->kek, tmp + *ick_len, ptk->kek_len);
	wpa_hexdump_key(MSG_DEBUG, "FILS: KEK", ptk->kek, ptk->kek_len);

	os_memcpy(ptk->tk, tmp + *ick_len + ptk->kek_len, ptk->tk_len);
	wpa_hexdump_key(MSG_DEBUG, "FILS: TK", ptk->tk, ptk->tk_len);

	if (fils_ft && fils_ft_len) {
		os_memcpy(fils_ft, tmp + *ick_len + ptk->kek_len + ptk->tk_len,
			  *fils_ft_len);
		wpa_hexdump_key(MSG_DEBUG, "FILS: FILS-FT",
				fils_ft, *fils_ft_len);
	}

	ptk->kek2_len = 0;
	ptk->kck2_len = 0;

	os_memset(tmp, 0, sizeof(tmp));
	ret = 0;
err:
	bin_clear_free(data, data_len);
	return ret;
}


int fils_key_auth_sk(const u8 *ick, size_t ick_len, const u8 *snonce,
		     const u8 *anonce, const u8 *sta_addr, const u8 *bssid,
		     const u8 *g_sta, size_t g_sta_len,
		     const u8 *g_ap, size_t g_ap_len,
		     int akmp, u8 *key_auth_sta, u8 *key_auth_ap,
		     size_t *key_auth_len)
{
	const u8 *addr[6];
	size_t len[6];
	size_t num_elem = 4;
	int res;

	wpa_printf(MSG_DEBUG, "FILS: Key-Auth derivation: STA-MAC=" MACSTR
		   " AP-BSSID=" MACSTR, MAC2STR(sta_addr), MAC2STR(bssid));
	wpa_hexdump_key(MSG_DEBUG, "FILS: ICK", ick, ick_len);
	wpa_hexdump(MSG_DEBUG, "FILS: SNonce", snonce, FILS_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "FILS: ANonce", anonce, FILS_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "FILS: gSTA", g_sta, g_sta_len);
	wpa_hexdump(MSG_DEBUG, "FILS: gAP", g_ap, g_ap_len);

	/*
	 * For (Re)Association Request frame (STA->AP):
	 * Key-Auth = HMAC-Hash(ICK, SNonce || ANonce || STA-MAC || AP-BSSID
	 *                      [ || gSTA || gAP ])
	 */
	addr[0] = snonce;
	len[0] = FILS_NONCE_LEN;
	addr[1] = anonce;
	len[1] = FILS_NONCE_LEN;
	addr[2] = sta_addr;
	len[2] = ETH_ALEN;
	addr[3] = bssid;
	len[3] = ETH_ALEN;
	if (g_sta && g_ap_len && g_ap && g_ap_len) {
		addr[4] = g_sta;
		len[4] = g_sta_len;
		addr[5] = g_ap;
		len[5] = g_ap_len;
		num_elem = 6;
	}

	if (wpa_key_mgmt_sha384(akmp)) {
		*key_auth_len = 48;
		res = hmac_sha384_vector(ick, ick_len, num_elem, addr, len,
					 key_auth_sta);
	} else if (wpa_key_mgmt_sha256(akmp)) {
		*key_auth_len = 32;
		res = hmac_sha256_vector(ick, ick_len, num_elem, addr, len,
					 key_auth_sta);
	} else {
		return -1;
	}
	if (res < 0)
		return res;

	/*
	 * For (Re)Association Response frame (AP->STA):
	 * Key-Auth = HMAC-Hash(ICK, ANonce || SNonce || AP-BSSID || STA-MAC
	 *                      [ || gAP || gSTA ])
	 */
	addr[0] = anonce;
	addr[1] = snonce;
	addr[2] = bssid;
	addr[3] = sta_addr;
	if (g_sta && g_ap_len && g_ap && g_ap_len) {
		addr[4] = g_ap;
		len[4] = g_ap_len;
		addr[5] = g_sta;
		len[5] = g_sta_len;
	}

	if (wpa_key_mgmt_sha384(akmp))
		res = hmac_sha384_vector(ick, ick_len, num_elem, addr, len,
					 key_auth_ap);
	else if (wpa_key_mgmt_sha256(akmp))
		res = hmac_sha256_vector(ick, ick_len, num_elem, addr, len,
					 key_auth_ap);
	if (res < 0)
		return res;

	wpa_hexdump(MSG_DEBUG, "FILS: Key-Auth (STA)",
		    key_auth_sta, *key_auth_len);
	wpa_hexdump(MSG_DEBUG, "FILS: Key-Auth (AP)",
		    key_auth_ap, *key_auth_len);

	return 0;
}

#endif /* CONFIG_FILS */


#ifdef CONFIG_IEEE80211R
int wpa_ft_mic(const u8 *kck, size_t kck_len, const u8 *sta_addr,
	       const u8 *ap_addr, u8 transaction_seqnum,
	       const u8 *mdie, size_t mdie_len,
	       const u8 *ftie, size_t ftie_len,
	       const u8 *rsnie, size_t rsnie_len,
	       const u8 *ric, size_t ric_len, u8 *mic)
{
	const u8 *addr[9];
	size_t len[9];
	size_t i, num_elem = 0;
	u8 zero_mic[24];
	size_t mic_len, fte_fixed_len;

	if (kck_len == 16) {
		mic_len = 16;
#ifdef CONFIG_SHA384
	} else if (kck_len == 24) {
		mic_len = 24;
#endif /* CONFIG_SHA384 */
	} else {
		wpa_printf(MSG_WARNING, "FT: Unsupported KCK length %u",
			   (unsigned int) kck_len);
		return -1;
	}

	fte_fixed_len = sizeof(struct rsn_ftie) - 16 + mic_len;

	addr[num_elem] = sta_addr;
	len[num_elem] = ETH_ALEN;
	num_elem++;

	addr[num_elem] = ap_addr;
	len[num_elem] = ETH_ALEN;
	num_elem++;

	addr[num_elem] = &transaction_seqnum;
	len[num_elem] = 1;
	num_elem++;

	if (rsnie) {
		addr[num_elem] = rsnie;
		len[num_elem] = rsnie_len;
		num_elem++;
	}
	if (mdie) {
		addr[num_elem] = mdie;
		len[num_elem] = mdie_len;
		num_elem++;
	}
	if (ftie) {
		if (ftie_len < 2 + fte_fixed_len)
			return -1;

		/* IE hdr and mic_control */
		addr[num_elem] = ftie;
		len[num_elem] = 2 + 2;
		num_elem++;

		/* MIC field with all zeros */
		os_memset(zero_mic, 0, mic_len);
		addr[num_elem] = zero_mic;
		len[num_elem] = mic_len;
		num_elem++;

		/* Rest of FTIE */
		addr[num_elem] = ftie + 2 + 2 + mic_len;
		len[num_elem] = ftie_len - (2 + 2 + mic_len);
		num_elem++;
	}
	if (ric) {
		addr[num_elem] = ric;
		len[num_elem] = ric_len;
		num_elem++;
	}

	for (i = 0; i < num_elem; i++)
		wpa_hexdump(MSG_MSGDUMP, "FT: MIC data", addr[i], len[i]);
#ifdef CONFIG_SHA384
	if (kck_len == 24) {
		u8 hash[SHA384_MAC_LEN];

		if (hmac_sha384_vector(kck, kck_len, num_elem, addr, len, hash))
			return -1;
		os_memcpy(mic, hash, 24);
	}
#endif /* CONFIG_SHA384 */
	if (kck_len == 16 &&
	    omac1_aes_128_vector(kck, num_elem, addr, len, mic))
		return -1;

	return 0;
}


static int wpa_ft_parse_ftie(const u8 *ie, size_t ie_len,
			     struct wpa_ft_ies *parse, int use_sha384)
{
	const u8 *end, *pos;

	parse->ftie = ie;
	parse->ftie_len = ie_len;

	pos = ie + (use_sha384 ? sizeof(struct rsn_ftie_sha384) :
		    sizeof(struct rsn_ftie));
	end = ie + ie_len;
	wpa_hexdump(MSG_DEBUG, "FT: Parse FTE subelements", pos, end - pos);

	while (end - pos >= 2) {
		u8 id, len;

		id = *pos++;
		len = *pos++;
		if (len > end - pos) {
			wpa_printf(MSG_DEBUG, "FT: Truncated subelement");
			break;
		}

		switch (id) {
		case FTIE_SUBELEM_R1KH_ID:
			if (len != FT_R1KH_ID_LEN) {
				wpa_printf(MSG_DEBUG,
					   "FT: Invalid R1KH-ID length in FTIE: %d",
					   len);
				return -1;
			}
			parse->r1kh_id = pos;
			break;
		case FTIE_SUBELEM_GTK:
			parse->gtk = pos;
			parse->gtk_len = len;
			break;
		case FTIE_SUBELEM_R0KH_ID:
			if (len < 1 || len > FT_R0KH_ID_MAX_LEN) {
				wpa_printf(MSG_DEBUG,
					   "FT: Invalid R0KH-ID length in FTIE: %d",
					   len);
				return -1;
			}
			parse->r0kh_id = pos;
			parse->r0kh_id_len = len;
			break;
#ifdef CONFIG_IEEE80211W
		case FTIE_SUBELEM_IGTK:
			parse->igtk = pos;
			parse->igtk_len = len;
			break;
#endif /* CONFIG_IEEE80211W */
		default:
			wpa_printf(MSG_DEBUG, "FT: Unknown subelem id %u", id);
			break;
		}

		pos += len;
	}

	return 0;
}


int wpa_ft_parse_ies(const u8 *ies, size_t ies_len,
		     struct wpa_ft_ies *parse, int use_sha384)
{
	const u8 *end, *pos;
	struct wpa_ie_data data;
	int ret;
	const struct rsn_ftie *ftie;
	int prot_ie_count = 0;
	int update_use_sha384 = 0;

	if (use_sha384 < 0) {
		use_sha384 = 0;
		update_use_sha384 = 1;
	}

	os_memset(parse, 0, sizeof(*parse));
	if (ies == NULL)
		return 0;

	pos = ies;
	end = ies + ies_len;
	while (end - pos >= 2) {
		u8 id, len;

		id = *pos++;
		len = *pos++;
		if (len > end - pos)
			break;

		switch (id) {
		case WLAN_EID_RSN:
			wpa_hexdump(MSG_DEBUG, "FT: RSNE", pos, len);
			parse->rsn = pos;
			parse->rsn_len = len;
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
			parse->key_mgmt = data.key_mgmt;
			parse->pairwise_cipher = data.pairwise_cipher;
			if (update_use_sha384) {
				use_sha384 =
					wpa_key_mgmt_sha384(parse->key_mgmt);
				update_use_sha384 = 0;
			}
			break;
		case WLAN_EID_MOBILITY_DOMAIN:
			wpa_hexdump(MSG_DEBUG, "FT: MDE", pos, len);
			if (len < sizeof(struct rsn_mdie))
				return -1;
			parse->mdie = pos;
			parse->mdie_len = len;
			break;
		case WLAN_EID_FAST_BSS_TRANSITION:
			wpa_hexdump(MSG_DEBUG, "FT: FTE", pos, len);
			if (use_sha384) {
				const struct rsn_ftie_sha384 *ftie_sha384;

				if (len < sizeof(*ftie_sha384))
					return -1;
				ftie_sha384 =
					(const struct rsn_ftie_sha384 *) pos;
				wpa_hexdump(MSG_DEBUG, "FT: FTE-MIC Control",
					    ftie_sha384->mic_control, 2);
				wpa_hexdump(MSG_DEBUG, "FT: FTE-MIC",
					    ftie_sha384->mic,
					    sizeof(ftie_sha384->mic));
				wpa_hexdump(MSG_DEBUG, "FT: FTE-ANonce",
					    ftie_sha384->anonce,
					    WPA_NONCE_LEN);
				wpa_hexdump(MSG_DEBUG, "FT: FTE-SNonce",
					    ftie_sha384->snonce,
					    WPA_NONCE_LEN);
				prot_ie_count = ftie_sha384->mic_control[1];
				if (wpa_ft_parse_ftie(pos, len, parse, 1) < 0)
					return -1;
				break;
			}

			if (len < sizeof(*ftie))
				return -1;
			ftie = (const struct rsn_ftie *) pos;
			wpa_hexdump(MSG_DEBUG, "FT: FTE-MIC Control",
				    ftie->mic_control, 2);
			wpa_hexdump(MSG_DEBUG, "FT: FTE-MIC",
				    ftie->mic, sizeof(ftie->mic));
			wpa_hexdump(MSG_DEBUG, "FT: FTE-ANonce",
				    ftie->anonce, WPA_NONCE_LEN);
			wpa_hexdump(MSG_DEBUG, "FT: FTE-SNonce",
				    ftie->snonce, WPA_NONCE_LEN);
			prot_ie_count = ftie->mic_control[1];
			if (wpa_ft_parse_ftie(pos, len, parse, 0) < 0)
				return -1;
			break;
		case WLAN_EID_TIMEOUT_INTERVAL:
			wpa_hexdump(MSG_DEBUG, "FT: Timeout Interval",
				    pos, len);
			if (len != 5)
				break;
			parse->tie = pos;
			parse->tie_len = len;
			break;
		case WLAN_EID_RIC_DATA:
			if (parse->ric == NULL)
				parse->ric = pos - 2;
			break;
		}

		pos += len;
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
	if (parse->ric) {
		pos = parse->ric;
		while (end - pos >= 2 && 2 + pos[1] <= end - pos &&
		       prot_ie_count) {
			prot_ie_count--;
			pos += 2 + pos[1];
		}
		parse->ric_len = pos - parse->ric;
	}
	if (prot_ie_count) {
		wpa_printf(MSG_DEBUG, "FT: %d protected IEs missing from "
			   "frame", (int) prot_ie_count);
		return -1;
	}

	return 0;
}
#endif /* CONFIG_IEEE80211R */


static int rsn_selector_to_bitfield(const u8 *s)
{
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_NONE)
		return WPA_CIPHER_NONE;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_TKIP)
		return WPA_CIPHER_TKIP;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_CCMP)
		return WPA_CIPHER_CCMP;
#ifdef CONFIG_IEEE80211W
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_AES_128_CMAC)
		return WPA_CIPHER_AES_128_CMAC;
#endif /* CONFIG_IEEE80211W */
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_GCMP)
		return WPA_CIPHER_GCMP;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_CCMP_256)
		return WPA_CIPHER_CCMP_256;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_GCMP_256)
		return WPA_CIPHER_GCMP_256;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_BIP_GMAC_128)
		return WPA_CIPHER_BIP_GMAC_128;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_BIP_GMAC_256)
		return WPA_CIPHER_BIP_GMAC_256;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_BIP_CMAC_256)
		return WPA_CIPHER_BIP_CMAC_256;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_NO_GROUP_ADDRESSED)
		return WPA_CIPHER_GTK_NOT_USED;
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
#ifdef CONFIG_SHA384
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FT_802_1X_SHA384)
		return WPA_KEY_MGMT_FT_IEEE8021X_SHA384;
#endif /* CONFIG_SHA384 */
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_IEEE80211W
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_802_1X_SHA256)
		return WPA_KEY_MGMT_IEEE8021X_SHA256;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_PSK_SHA256)
		return WPA_KEY_MGMT_PSK_SHA256;
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_SAE
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_SAE)
		return WPA_KEY_MGMT_SAE;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FT_SAE)
		return WPA_KEY_MGMT_FT_SAE;
#endif /* CONFIG_SAE */
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_802_1X_SUITE_B)
		return WPA_KEY_MGMT_IEEE8021X_SUITE_B;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_802_1X_SUITE_B_192)
		return WPA_KEY_MGMT_IEEE8021X_SUITE_B_192;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FILS_SHA256)
		return WPA_KEY_MGMT_FILS_SHA256;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FILS_SHA384)
		return WPA_KEY_MGMT_FILS_SHA384;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FT_FILS_SHA256)
		return WPA_KEY_MGMT_FT_FILS_SHA256;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FT_FILS_SHA384)
		return WPA_KEY_MGMT_FT_FILS_SHA384;
#ifdef CONFIG_OWE
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_OWE)
		return WPA_KEY_MGMT_OWE;
#endif /* CONFIG_OWE */
#ifdef CONFIG_DPP
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_DPP)
		return WPA_KEY_MGMT_DPP;
#endif /* CONFIG_DPP */
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_OSEN)
		return WPA_KEY_MGMT_OSEN;
	return 0;
}


int wpa_cipher_valid_group(int cipher)
{
	return wpa_cipher_valid_pairwise(cipher) ||
		cipher == WPA_CIPHER_GTK_NOT_USED;
}


#ifdef CONFIG_IEEE80211W
int wpa_cipher_valid_mgmt_group(int cipher)
{
	return cipher == WPA_CIPHER_AES_128_CMAC ||
		cipher == WPA_CIPHER_BIP_GMAC_128 ||
		cipher == WPA_CIPHER_BIP_GMAC_256 ||
		cipher == WPA_CIPHER_BIP_CMAC_256;
}
#endif /* CONFIG_IEEE80211W */


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

	if (rsn_ie_len >= 6 && rsn_ie[1] >= 4 &&
	    rsn_ie[1] == rsn_ie_len - 2 &&
	    WPA_GET_BE32(&rsn_ie[2]) == OSEN_IE_VENDOR_TYPE) {
		pos = rsn_ie + 6;
		left = rsn_ie_len - 6;

		data->group_cipher = WPA_CIPHER_GTK_NOT_USED;
		data->key_mgmt = WPA_KEY_MGMT_OSEN;
		data->proto = WPA_PROTO_OSEN;
	} else {
		const struct rsn_ie_hdr *hdr;

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
	}

	if (left >= RSN_SELECTOR_LEN) {
		data->group_cipher = rsn_selector_to_bitfield(pos);
		if (!wpa_cipher_valid_group(data->group_cipher)) {
			wpa_printf(MSG_DEBUG,
				   "%s: invalid group cipher 0x%x (%08x)",
				   __func__, data->group_cipher,
				   WPA_GET_BE32(pos));
			return -1;
		}
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
		if (count == 0 || count > left / RSN_SELECTOR_LEN) {
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
		if (count == 0 || count > left / RSN_SELECTOR_LEN) {
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
		u16 num_pmkid = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (num_pmkid > (unsigned int) left / PMKID_LEN) {
			wpa_printf(MSG_DEBUG, "%s: PMKID underflow "
				   "(num_pmkid=%u left=%d)",
				   __func__, num_pmkid, left);
			data->num_pmkid = 0;
			return -9;
		} else {
			data->num_pmkid = num_pmkid;
			data->pmkid = pos;
			pos += data->num_pmkid * PMKID_LEN;
			left -= data->num_pmkid * PMKID_LEN;
		}
	}

#ifdef CONFIG_IEEE80211W
	if (left >= 4) {
		data->mgmt_group_cipher = rsn_selector_to_bitfield(pos);
		if (!wpa_cipher_valid_mgmt_group(data->mgmt_group_cipher)) {
			wpa_printf(MSG_DEBUG,
				   "%s: Unsupported management group cipher 0x%x (%08x)",
				   __func__, data->mgmt_group_cipher,
				   WPA_GET_BE32(pos));
			return -10;
		}
		pos += RSN_SELECTOR_LEN;
		left -= RSN_SELECTOR_LEN;
	}
#endif /* CONFIG_IEEE80211W */

	if (left > 0) {
		wpa_hexdump(MSG_DEBUG,
			    "wpa_parse_wpa_ie_rsn: ignore trailing bytes",
			    pos, left);
	}

	return 0;
}


static int wpa_selector_to_bitfield(const u8 *s)
{
	if (RSN_SELECTOR_GET(s) == WPA_CIPHER_SUITE_NONE)
		return WPA_CIPHER_NONE;
	if (RSN_SELECTOR_GET(s) == WPA_CIPHER_SUITE_TKIP)
		return WPA_CIPHER_TKIP;
	if (RSN_SELECTOR_GET(s) == WPA_CIPHER_SUITE_CCMP)
		return WPA_CIPHER_CCMP;
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
		if (count == 0 || count > left / WPA_SELECTOR_LEN) {
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
		if (count == 0 || count > left / WPA_SELECTOR_LEN) {
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
		wpa_hexdump(MSG_DEBUG,
			    "wpa_parse_wpa_ie_wpa: ignore trailing bytes",
			    pos, left);
	}

	return 0;
}


#ifdef CONFIG_IEEE80211R

/**
 * wpa_derive_pmk_r0 - Derive PMK-R0 and PMKR0Name
 *
 * IEEE Std 802.11r-2008 - 8.5.1.5.3
 */
int wpa_derive_pmk_r0(const u8 *xxkey, size_t xxkey_len,
		      const u8 *ssid, size_t ssid_len,
		      const u8 *mdid, const u8 *r0kh_id, size_t r0kh_id_len,
		      const u8 *s0kh_id, u8 *pmk_r0, u8 *pmk_r0_name,
		      int use_sha384)
{
	u8 buf[1 + SSID_MAX_LEN + MOBILITY_DOMAIN_ID_LEN + 1 +
	       FT_R0KH_ID_MAX_LEN + ETH_ALEN];
	u8 *pos, r0_key_data[64], hash[48];
	const u8 *addr[2];
	size_t len[2];
	size_t q = use_sha384 ? 48 : 32;
	size_t r0_key_data_len = q + 16;

	/*
	 * R0-Key-Data = KDF-384(XXKey, "FT-R0",
	 *                       SSIDlength || SSID || MDID || R0KHlength ||
	 *                       R0KH-ID || S0KH-ID)
	 * XXKey is either the second 256 bits of MSK or PSK; or the first
	 * 384 bits of MSK for FT-EAP-SHA384.
	 * PMK-R0 = L(R0-Key-Data, 0, Q)
	 * PMK-R0Name-Salt = L(R0-Key-Data, Q, 128)
	 * Q = 384 for FT-EAP-SHA384; otherwise, 256
	 */
	if (ssid_len > SSID_MAX_LEN || r0kh_id_len > FT_R0KH_ID_MAX_LEN)
		return -1;
	wpa_printf(MSG_DEBUG, "FT: Derive PMK-R0 using KDF-%s",
		   use_sha384 ? "SHA384" : "SHA256");
	wpa_hexdump_key(MSG_DEBUG, "FT: XXKey", xxkey, xxkey_len);
	wpa_hexdump_ascii(MSG_DEBUG, "FT: SSID", ssid, ssid_len);
	wpa_hexdump(MSG_DEBUG, "FT: MDID", mdid, MOBILITY_DOMAIN_ID_LEN);
	wpa_hexdump_ascii(MSG_DEBUG, "FT: R0KH-ID", r0kh_id, r0kh_id_len);
	wpa_printf(MSG_DEBUG, "FT: S0KH-ID: " MACSTR, MAC2STR(s0kh_id));
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

#ifdef CONFIG_SHA384
	if (use_sha384) {
		if (xxkey_len != SHA384_MAC_LEN) {
			wpa_printf(MSG_ERROR,
				   "FT: Unexpected XXKey length %d (expected %d)",
				   (int) xxkey_len, SHA384_MAC_LEN);
			return -1;
		}
		if (sha384_prf(xxkey, xxkey_len, "FT-R0", buf, pos - buf,
			       r0_key_data, r0_key_data_len) < 0)
			return -1;
	}
#endif /* CONFIG_SHA384 */
	if (!use_sha384) {
		if (xxkey_len != PMK_LEN) {
			wpa_printf(MSG_ERROR,
				   "FT: Unexpected XXKey length %d (expected %d)",
				   (int) xxkey_len, PMK_LEN);
			return -1;
		}
		if (sha256_prf(xxkey, xxkey_len, "FT-R0", buf, pos - buf,
			       r0_key_data, r0_key_data_len) < 0)
			return -1;
	}
	os_memcpy(pmk_r0, r0_key_data, q);
	wpa_hexdump_key(MSG_DEBUG, "FT: PMK-R0", pmk_r0, q);
	wpa_hexdump_key(MSG_DEBUG, "FT: PMK-R0Name-Salt", &r0_key_data[q], 16);

	/*
	 * PMKR0Name = Truncate-128(Hash("FT-R0N" || PMK-R0Name-Salt)
	 */
	addr[0] = (const u8 *) "FT-R0N";
	len[0] = 6;
	addr[1] = &r0_key_data[q];
	len[1] = 16;

#ifdef CONFIG_SHA384
	if (use_sha384 && sha384_vector(2, addr, len, hash) < 0)
		return -1;
#endif /* CONFIG_SHA384 */
	if (!use_sha384 && sha256_vector(2, addr, len, hash) < 0)
		return -1;
	os_memcpy(pmk_r0_name, hash, WPA_PMK_NAME_LEN);
	os_memset(r0_key_data, 0, sizeof(r0_key_data));
	return 0;
}


/**
 * wpa_derive_pmk_r1_name - Derive PMKR1Name
 *
 * IEEE Std 802.11r-2008 - 8.5.1.5.4
 */
int wpa_derive_pmk_r1_name(const u8 *pmk_r0_name, const u8 *r1kh_id,
			   const u8 *s1kh_id, u8 *pmk_r1_name, int use_sha384)
{
	u8 hash[48];
	const u8 *addr[4];
	size_t len[4];

	/*
	 * PMKR1Name = Truncate-128(Hash("FT-R1N" || PMKR0Name ||
	 *                               R1KH-ID || S1KH-ID))
	 */
	addr[0] = (const u8 *) "FT-R1N";
	len[0] = 6;
	addr[1] = pmk_r0_name;
	len[1] = WPA_PMK_NAME_LEN;
	addr[2] = r1kh_id;
	len[2] = FT_R1KH_ID_LEN;
	addr[3] = s1kh_id;
	len[3] = ETH_ALEN;

#ifdef CONFIG_SHA384
	if (use_sha384 && sha384_vector(4, addr, len, hash) < 0)
		return -1;
#endif /* CONFIG_SHA384 */
	if (!use_sha384 && sha256_vector(4, addr, len, hash) < 0)
		return -1;
	os_memcpy(pmk_r1_name, hash, WPA_PMK_NAME_LEN);
	return 0;
}


/**
 * wpa_derive_pmk_r1 - Derive PMK-R1 and PMKR1Name from PMK-R0
 *
 * IEEE Std 802.11r-2008 - 8.5.1.5.4
 */
int wpa_derive_pmk_r1(const u8 *pmk_r0, size_t pmk_r0_len,
		      const u8 *pmk_r0_name,
		      const u8 *r1kh_id, const u8 *s1kh_id,
		      u8 *pmk_r1, u8 *pmk_r1_name)
{
	u8 buf[FT_R1KH_ID_LEN + ETH_ALEN];
	u8 *pos;

	/* PMK-R1 = KDF-256(PMK-R0, "FT-R1", R1KH-ID || S1KH-ID) */
	wpa_printf(MSG_DEBUG, "FT: Derive PMK-R1 using KDF-%s",
		   pmk_r0_len == SHA384_MAC_LEN ? "SHA384" : "SHA256");
	wpa_hexdump_key(MSG_DEBUG, "FT: PMK-R0", pmk_r0, pmk_r0_len);
	wpa_hexdump(MSG_DEBUG, "FT: R1KH-ID", r1kh_id, FT_R1KH_ID_LEN);
	wpa_printf(MSG_DEBUG, "FT: S1KH-ID: " MACSTR, MAC2STR(s1kh_id));
	pos = buf;
	os_memcpy(pos, r1kh_id, FT_R1KH_ID_LEN);
	pos += FT_R1KH_ID_LEN;
	os_memcpy(pos, s1kh_id, ETH_ALEN);
	pos += ETH_ALEN;

#ifdef CONFIG_SHA384
	if (pmk_r0_len == SHA384_MAC_LEN &&
	    sha384_prf(pmk_r0, pmk_r0_len, "FT-R1",
		       buf, pos - buf, pmk_r1, pmk_r0_len) < 0)
		return -1;
#endif /* CONFIG_SHA384 */
	if (pmk_r0_len == PMK_LEN &&
	    sha256_prf(pmk_r0, pmk_r0_len, "FT-R1",
		       buf, pos - buf, pmk_r1, pmk_r0_len) < 0)
		return -1;
	if (pmk_r0_len != SHA384_MAC_LEN && pmk_r0_len != PMK_LEN) {
		wpa_printf(MSG_ERROR, "FT: Unexpected PMK-R0 length %d",
			   (int) pmk_r0_len);
		return -1;
	}
	wpa_hexdump_key(MSG_DEBUG, "FT: PMK-R1", pmk_r1, pmk_r0_len);

	return wpa_derive_pmk_r1_name(pmk_r0_name, r1kh_id, s1kh_id,
				      pmk_r1_name,
				      pmk_r0_len == SHA384_MAC_LEN);
}


/**
 * wpa_pmk_r1_to_ptk - Derive PTK and PTKName from PMK-R1
 *
 * IEEE Std 802.11r-2008 - 8.5.1.5.5
 */
int wpa_pmk_r1_to_ptk(const u8 *pmk_r1, size_t pmk_r1_len,
		      const u8 *snonce, const u8 *anonce,
		      const u8 *sta_addr, const u8 *bssid,
		      const u8 *pmk_r1_name,
		      struct wpa_ptk *ptk, u8 *ptk_name, int akmp, int cipher)
{
	u8 buf[2 * WPA_NONCE_LEN + 2 * ETH_ALEN];
	u8 *pos, hash[32];
	const u8 *addr[6];
	size_t len[6];
	u8 tmp[2 * WPA_KCK_MAX_LEN + 2 * WPA_KEK_MAX_LEN + WPA_TK_MAX_LEN];
	size_t ptk_len, offset;
	int use_sha384 = wpa_key_mgmt_sha384(akmp);

	/*
	 * PTK = KDF-PTKLen(PMK-R1, "FT-PTK", SNonce || ANonce ||
	 *                  BSSID || STA-ADDR)
	 */
	wpa_printf(MSG_DEBUG, "FT: Derive PTK using KDF-%s",
		   use_sha384 ? "SHA384" : "SHA256");
	wpa_hexdump_key(MSG_DEBUG, "FT: PMK-R1", pmk_r1, pmk_r1_len);
	wpa_hexdump(MSG_DEBUG, "FT: SNonce", snonce, WPA_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "FT: ANonce", anonce, WPA_NONCE_LEN);
	wpa_printf(MSG_DEBUG, "FT: BSSID=" MACSTR " STA-ADDR=" MACSTR,
		   MAC2STR(bssid), MAC2STR(sta_addr));
	pos = buf;
	os_memcpy(pos, snonce, WPA_NONCE_LEN);
	pos += WPA_NONCE_LEN;
	os_memcpy(pos, anonce, WPA_NONCE_LEN);
	pos += WPA_NONCE_LEN;
	os_memcpy(pos, bssid, ETH_ALEN);
	pos += ETH_ALEN;
	os_memcpy(pos, sta_addr, ETH_ALEN);
	pos += ETH_ALEN;

	ptk->kck_len = wpa_kck_len(akmp, PMK_LEN);
	ptk->kck2_len = wpa_kck2_len(akmp);
	ptk->kek_len = wpa_kek_len(akmp, PMK_LEN);
	ptk->kek2_len = wpa_kek2_len(akmp);
	ptk->tk_len = wpa_cipher_key_len(cipher);
	ptk_len = ptk->kck_len + ptk->kek_len + ptk->tk_len +
		ptk->kck2_len + ptk->kek2_len;

#ifdef CONFIG_SHA384
	if (use_sha384) {
		if (pmk_r1_len != SHA384_MAC_LEN) {
			wpa_printf(MSG_ERROR,
				   "FT: Unexpected PMK-R1 length %d (expected %d)",
				   (int) pmk_r1_len, SHA384_MAC_LEN);
			return -1;
		}
		if (sha384_prf(pmk_r1, pmk_r1_len, "FT-PTK",
			       buf, pos - buf, tmp, ptk_len) < 0)
			return -1;
	}
#endif /* CONFIG_SHA384 */
	if (!use_sha384) {
		if (pmk_r1_len != PMK_LEN) {
			wpa_printf(MSG_ERROR,
				   "FT: Unexpected PMK-R1 length %d (expected %d)",
				   (int) pmk_r1_len, PMK_LEN);
			return -1;
		}
		if (sha256_prf(pmk_r1, pmk_r1_len, "FT-PTK",
			       buf, pos - buf, tmp, ptk_len) < 0)
			return -1;
	}
	wpa_hexdump_key(MSG_DEBUG, "FT: PTK", tmp, ptk_len);

	/*
	 * PTKName = Truncate-128(SHA-256(PMKR1Name || "FT-PTKN" || SNonce ||
	 *                                ANonce || BSSID || STA-ADDR))
	 */
	wpa_hexdump(MSG_DEBUG, "FT: PMKR1Name", pmk_r1_name, WPA_PMK_NAME_LEN);
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

	if (sha256_vector(6, addr, len, hash) < 0)
		return -1;
	os_memcpy(ptk_name, hash, WPA_PMK_NAME_LEN);

	os_memcpy(ptk->kck, tmp, ptk->kck_len);
	offset = ptk->kck_len;
	os_memcpy(ptk->kek, tmp + offset, ptk->kek_len);
	offset += ptk->kek_len;
	os_memcpy(ptk->tk, tmp + offset, ptk->tk_len);
	offset += ptk->tk_len;
	os_memcpy(ptk->kck2, tmp + offset, ptk->kck2_len);
	offset = ptk->kck2_len;
	os_memcpy(ptk->kek2, tmp + offset, ptk->kek2_len);

	wpa_hexdump_key(MSG_DEBUG, "FT: KCK", ptk->kck, ptk->kck_len);
	wpa_hexdump_key(MSG_DEBUG, "FT: KEK", ptk->kek, ptk->kek_len);
	if (ptk->kck2_len)
		wpa_hexdump_key(MSG_DEBUG, "FT: KCK2",
				ptk->kck2, ptk->kck2_len);
	if (ptk->kek2_len)
		wpa_hexdump_key(MSG_DEBUG, "FT: KEK2",
				ptk->kek2, ptk->kek2_len);
	wpa_hexdump_key(MSG_DEBUG, "FT: TK", ptk->tk, ptk->tk_len);
	wpa_hexdump(MSG_DEBUG, "FT: PTKName", ptk_name, WPA_PMK_NAME_LEN);

	os_memset(tmp, 0, sizeof(tmp));

	return 0;
}

#endif /* CONFIG_IEEE80211R */


/**
 * rsn_pmkid - Calculate PMK identifier
 * @pmk: Pairwise master key
 * @pmk_len: Length of pmk in bytes
 * @aa: Authenticator address
 * @spa: Supplicant address
 * @pmkid: Buffer for PMKID
 * @akmp: Negotiated key management protocol
 *
 * IEEE Std 802.11-2016 - 12.7.1.3 Pairwise key hierarchy
 * AKM: 00-0F-AC:5, 00-0F-AC:6, 00-0F-AC:14, 00-0F-AC:16
 * PMKID = Truncate-128(HMAC-SHA-256(PMK, "PMK Name" || AA || SPA))
 * AKM: 00-0F-AC:11
 * See rsn_pmkid_suite_b()
 * AKM: 00-0F-AC:12
 * See rsn_pmkid_suite_b_192()
 * AKM: 00-0F-AC:13, 00-0F-AC:15, 00-0F-AC:17
 * PMKID = Truncate-128(HMAC-SHA-384(PMK, "PMK Name" || AA || SPA))
 * Otherwise:
 * PMKID = Truncate-128(HMAC-SHA-1(PMK, "PMK Name" || AA || SPA))
 */
void rsn_pmkid(const u8 *pmk, size_t pmk_len, const u8 *aa, const u8 *spa,
	       u8 *pmkid, int akmp)
{
	char *title = "PMK Name";
	const u8 *addr[3];
	const size_t len[3] = { 8, ETH_ALEN, ETH_ALEN };
	unsigned char hash[SHA384_MAC_LEN];

	addr[0] = (u8 *) title;
	addr[1] = aa;
	addr[2] = spa;

	if (0) {
#if defined(CONFIG_FILS) || defined(CONFIG_SHA384)
	} else if (wpa_key_mgmt_sha384(akmp)) {
		wpa_printf(MSG_DEBUG, "RSN: Derive PMKID using HMAC-SHA-384");
		hmac_sha384_vector(pmk, pmk_len, 3, addr, len, hash);
#endif /* CONFIG_FILS || CONFIG_SHA384 */
#if defined(CONFIG_IEEE80211W) || defined(CONFIG_FILS)
	} else if (wpa_key_mgmt_sha256(akmp)) {
		wpa_printf(MSG_DEBUG, "RSN: Derive PMKID using HMAC-SHA-256");
		hmac_sha256_vector(pmk, pmk_len, 3, addr, len, hash);
#endif /* CONFIG_IEEE80211W || CONFIG_FILS */
	} else {
		wpa_printf(MSG_DEBUG, "RSN: Derive PMKID using HMAC-SHA-1");
		hmac_sha1_vector(pmk, pmk_len, 3, addr, len, hash);
	}
	wpa_hexdump(MSG_DEBUG, "RSN: Derived PMKID", hash, PMKID_LEN);
	os_memcpy(pmkid, hash, PMKID_LEN);
}


#ifdef CONFIG_SUITEB
/**
 * rsn_pmkid_suite_b - Calculate PMK identifier for Suite B AKM
 * @kck: Key confirmation key
 * @kck_len: Length of kck in bytes
 * @aa: Authenticator address
 * @spa: Supplicant address
 * @pmkid: Buffer for PMKID
 * Returns: 0 on success, -1 on failure
 *
 * IEEE Std 802.11ac-2013 - 11.6.1.3 Pairwise key hierarchy
 * PMKID = Truncate(HMAC-SHA-256(KCK, "PMK Name" || AA || SPA))
 */
int rsn_pmkid_suite_b(const u8 *kck, size_t kck_len, const u8 *aa,
		      const u8 *spa, u8 *pmkid)
{
	char *title = "PMK Name";
	const u8 *addr[3];
	const size_t len[3] = { 8, ETH_ALEN, ETH_ALEN };
	unsigned char hash[SHA256_MAC_LEN];

	addr[0] = (u8 *) title;
	addr[1] = aa;
	addr[2] = spa;

	if (hmac_sha256_vector(kck, kck_len, 3, addr, len, hash) < 0)
		return -1;
	os_memcpy(pmkid, hash, PMKID_LEN);
	return 0;
}
#endif /* CONFIG_SUITEB */


#ifdef CONFIG_SUITEB192
/**
 * rsn_pmkid_suite_b_192 - Calculate PMK identifier for Suite B AKM
 * @kck: Key confirmation key
 * @kck_len: Length of kck in bytes
 * @aa: Authenticator address
 * @spa: Supplicant address
 * @pmkid: Buffer for PMKID
 * Returns: 0 on success, -1 on failure
 *
 * IEEE Std 802.11ac-2013 - 11.6.1.3 Pairwise key hierarchy
 * PMKID = Truncate(HMAC-SHA-384(KCK, "PMK Name" || AA || SPA))
 */
int rsn_pmkid_suite_b_192(const u8 *kck, size_t kck_len, const u8 *aa,
			  const u8 *spa, u8 *pmkid)
{
	char *title = "PMK Name";
	const u8 *addr[3];
	const size_t len[3] = { 8, ETH_ALEN, ETH_ALEN };
	unsigned char hash[SHA384_MAC_LEN];

	addr[0] = (u8 *) title;
	addr[1] = aa;
	addr[2] = spa;

	if (hmac_sha384_vector(kck, kck_len, 3, addr, len, hash) < 0)
		return -1;
	os_memcpy(pmkid, hash, PMKID_LEN);
	return 0;
}
#endif /* CONFIG_SUITEB192 */


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
	case WPA_CIPHER_GCMP:
		return "GCMP";
	case WPA_CIPHER_GCMP_256:
		return "GCMP-256";
	case WPA_CIPHER_CCMP_256:
		return "CCMP-256";
	case WPA_CIPHER_AES_128_CMAC:
		return "BIP";
	case WPA_CIPHER_BIP_GMAC_128:
		return "BIP-GMAC-128";
	case WPA_CIPHER_BIP_GMAC_256:
		return "BIP-GMAC-256";
	case WPA_CIPHER_BIP_CMAC_256:
		return "BIP-CMAC-256";
	case WPA_CIPHER_GTK_NOT_USED:
		return "GTK_NOT_USED";
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
	case WPA_KEY_MGMT_WPA_NONE:
		return "WPA-NONE";
	case WPA_KEY_MGMT_IEEE8021X_NO_WPA:
		return "IEEE 802.1X (no WPA)";
#ifdef CONFIG_IEEE80211R
	case WPA_KEY_MGMT_FT_IEEE8021X:
		return "FT-EAP";
	case WPA_KEY_MGMT_FT_IEEE8021X_SHA384:
		return "FT-EAP-SHA384";
	case WPA_KEY_MGMT_FT_PSK:
		return "FT-PSK";
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_IEEE80211W
	case WPA_KEY_MGMT_IEEE8021X_SHA256:
		return "WPA2-EAP-SHA256";
	case WPA_KEY_MGMT_PSK_SHA256:
		return "WPA2-PSK-SHA256";
#endif /* CONFIG_IEEE80211W */
	case WPA_KEY_MGMT_WPS:
		return "WPS";
	case WPA_KEY_MGMT_SAE:
		return "SAE";
	case WPA_KEY_MGMT_FT_SAE:
		return "FT-SAE";
	case WPA_KEY_MGMT_OSEN:
		return "OSEN";
	case WPA_KEY_MGMT_IEEE8021X_SUITE_B:
		return "WPA2-EAP-SUITE-B";
	case WPA_KEY_MGMT_IEEE8021X_SUITE_B_192:
		return "WPA2-EAP-SUITE-B-192";
	case WPA_KEY_MGMT_FILS_SHA256:
		return "FILS-SHA256";
	case WPA_KEY_MGMT_FILS_SHA384:
		return "FILS-SHA384";
	case WPA_KEY_MGMT_FT_FILS_SHA256:
		return "FT-FILS-SHA256";
	case WPA_KEY_MGMT_FT_FILS_SHA384:
		return "FT-FILS-SHA384";
	case WPA_KEY_MGMT_OWE:
		return "OWE";
	case WPA_KEY_MGMT_DPP:
		return "DPP";
	default:
		return "UNKNOWN";
	}
}


u32 wpa_akm_to_suite(int akm)
{
	if (akm & WPA_KEY_MGMT_FT_IEEE8021X_SHA384)
		return RSN_AUTH_KEY_MGMT_FT_802_1X_SHA384;
	if (akm & WPA_KEY_MGMT_FT_IEEE8021X)
		return RSN_AUTH_KEY_MGMT_FT_802_1X;
	if (akm & WPA_KEY_MGMT_FT_PSK)
		return RSN_AUTH_KEY_MGMT_FT_PSK;
	if (akm & WPA_KEY_MGMT_IEEE8021X_SHA256)
		return RSN_AUTH_KEY_MGMT_802_1X_SHA256;
	if (akm & WPA_KEY_MGMT_IEEE8021X)
		return RSN_AUTH_KEY_MGMT_UNSPEC_802_1X;
	if (akm & WPA_KEY_MGMT_PSK_SHA256)
		return RSN_AUTH_KEY_MGMT_PSK_SHA256;
	if (akm & WPA_KEY_MGMT_PSK)
		return RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X;
	if (akm & WPA_KEY_MGMT_CCKM)
		return RSN_AUTH_KEY_MGMT_CCKM;
	if (akm & WPA_KEY_MGMT_OSEN)
		return RSN_AUTH_KEY_MGMT_OSEN;
	if (akm & WPA_KEY_MGMT_IEEE8021X_SUITE_B)
		return RSN_AUTH_KEY_MGMT_802_1X_SUITE_B;
	if (akm & WPA_KEY_MGMT_IEEE8021X_SUITE_B_192)
		return RSN_AUTH_KEY_MGMT_802_1X_SUITE_B_192;
	if (akm & WPA_KEY_MGMT_FILS_SHA256)
		return RSN_AUTH_KEY_MGMT_FILS_SHA256;
	if (akm & WPA_KEY_MGMT_FILS_SHA384)
		return RSN_AUTH_KEY_MGMT_FILS_SHA384;
	if (akm & WPA_KEY_MGMT_FT_FILS_SHA256)
		return RSN_AUTH_KEY_MGMT_FT_FILS_SHA256;
	if (akm & WPA_KEY_MGMT_FT_FILS_SHA384)
		return RSN_AUTH_KEY_MGMT_FT_FILS_SHA384;
	return 0;
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


#if defined(CONFIG_IEEE80211R) || defined(CONFIG_FILS)
int wpa_insert_pmkid(u8 *ies, size_t *ies_len, const u8 *pmkid)
{
	u8 *start, *end, *rpos, *rend;
	int added = 0;

	start = ies;
	end = ies + *ies_len;

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
		added += 2;
		start[1] += 2;
		rend = rpos;
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
		os_memmove(rpos + 2 + PMKID_LEN, rpos, end + added - rpos);
		WPA_PUT_LE16(rpos, 1);
		rpos += 2;
		os_memcpy(rpos, pmkid, PMKID_LEN);
		added += 2 + PMKID_LEN;
		start[1] += 2 + PMKID_LEN;
	} else {
		u16 num_pmkid;

		if (rend - rpos < 2)
			return -1;
		num_pmkid = WPA_GET_LE16(rpos);
		/* PMKID-Count was included; use it */
		if (num_pmkid != 0) {
			u8 *after;

			if (num_pmkid * PMKID_LEN > rend - rpos - 2)
				return -1;
			/*
			 * PMKID may have been included in RSN IE in
			 * (Re)Association Request frame, so remove the old
			 * PMKID(s) first before adding the new one.
			 */
			wpa_printf(MSG_DEBUG,
				   "FT: Remove %u old PMKID(s) from RSN IE",
				   num_pmkid);
			after = rpos + 2 + num_pmkid * PMKID_LEN;
			os_memmove(rpos + 2, after, rend - after);
			start[1] -= num_pmkid * PMKID_LEN;
			added -= num_pmkid * PMKID_LEN;
		}
		WPA_PUT_LE16(rpos, 1);
		rpos += 2;
		os_memmove(rpos + PMKID_LEN, rpos, end + added - rpos);
		os_memcpy(rpos, pmkid, PMKID_LEN);
		added += PMKID_LEN;
		start[1] += PMKID_LEN;
	}

	wpa_hexdump(MSG_DEBUG, "FT: RSN IE after modification "
		    "(PMKID inserted)", start, 2 + start[1]);

	*ies_len += added;

	return 0;
}
#endif /* CONFIG_IEEE80211R || CONFIG_FILS */


int wpa_cipher_key_len(int cipher)
{
	switch (cipher) {
	case WPA_CIPHER_CCMP_256:
	case WPA_CIPHER_GCMP_256:
	case WPA_CIPHER_BIP_GMAC_256:
	case WPA_CIPHER_BIP_CMAC_256:
		return 32;
	case WPA_CIPHER_CCMP:
	case WPA_CIPHER_GCMP:
	case WPA_CIPHER_AES_128_CMAC:
	case WPA_CIPHER_BIP_GMAC_128:
		return 16;
	case WPA_CIPHER_TKIP:
		return 32;
	}

	return 0;
}


int wpa_cipher_rsc_len(int cipher)
{
	switch (cipher) {
	case WPA_CIPHER_CCMP_256:
	case WPA_CIPHER_GCMP_256:
	case WPA_CIPHER_CCMP:
	case WPA_CIPHER_GCMP:
	case WPA_CIPHER_TKIP:
		return 6;
	}

	return 0;
}


enum wpa_alg wpa_cipher_to_alg(int cipher)
{
	switch (cipher) {
	case WPA_CIPHER_CCMP_256:
		return WPA_ALG_CCMP_256;
	case WPA_CIPHER_GCMP_256:
		return WPA_ALG_GCMP_256;
	case WPA_CIPHER_CCMP:
		return WPA_ALG_CCMP;
	case WPA_CIPHER_GCMP:
		return WPA_ALG_GCMP;
	case WPA_CIPHER_TKIP:
		return WPA_ALG_TKIP;
	case WPA_CIPHER_AES_128_CMAC:
		return WPA_ALG_IGTK;
	case WPA_CIPHER_BIP_GMAC_128:
		return WPA_ALG_BIP_GMAC_128;
	case WPA_CIPHER_BIP_GMAC_256:
		return WPA_ALG_BIP_GMAC_256;
	case WPA_CIPHER_BIP_CMAC_256:
		return WPA_ALG_BIP_CMAC_256;
	}
	return WPA_ALG_NONE;
}


int wpa_cipher_valid_pairwise(int cipher)
{
	return cipher == WPA_CIPHER_CCMP_256 ||
		cipher == WPA_CIPHER_GCMP_256 ||
		cipher == WPA_CIPHER_CCMP ||
		cipher == WPA_CIPHER_GCMP ||
		cipher == WPA_CIPHER_TKIP;
}


u32 wpa_cipher_to_suite(int proto, int cipher)
{
	if (cipher & WPA_CIPHER_CCMP_256)
		return RSN_CIPHER_SUITE_CCMP_256;
	if (cipher & WPA_CIPHER_GCMP_256)
		return RSN_CIPHER_SUITE_GCMP_256;
	if (cipher & WPA_CIPHER_CCMP)
		return (proto == WPA_PROTO_RSN ?
			RSN_CIPHER_SUITE_CCMP : WPA_CIPHER_SUITE_CCMP);
	if (cipher & WPA_CIPHER_GCMP)
		return RSN_CIPHER_SUITE_GCMP;
	if (cipher & WPA_CIPHER_TKIP)
		return (proto == WPA_PROTO_RSN ?
			RSN_CIPHER_SUITE_TKIP : WPA_CIPHER_SUITE_TKIP);
	if (cipher & WPA_CIPHER_NONE)
		return (proto == WPA_PROTO_RSN ?
			RSN_CIPHER_SUITE_NONE : WPA_CIPHER_SUITE_NONE);
	if (cipher & WPA_CIPHER_GTK_NOT_USED)
		return RSN_CIPHER_SUITE_NO_GROUP_ADDRESSED;
	if (cipher & WPA_CIPHER_AES_128_CMAC)
		return RSN_CIPHER_SUITE_AES_128_CMAC;
	if (cipher & WPA_CIPHER_BIP_GMAC_128)
		return RSN_CIPHER_SUITE_BIP_GMAC_128;
	if (cipher & WPA_CIPHER_BIP_GMAC_256)
		return RSN_CIPHER_SUITE_BIP_GMAC_256;
	if (cipher & WPA_CIPHER_BIP_CMAC_256)
		return RSN_CIPHER_SUITE_BIP_CMAC_256;
	return 0;
}


int rsn_cipher_put_suites(u8 *start, int ciphers)
{
	u8 *pos = start;

	if (ciphers & WPA_CIPHER_CCMP_256) {
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_CCMP_256);
		pos += RSN_SELECTOR_LEN;
	}
	if (ciphers & WPA_CIPHER_GCMP_256) {
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_GCMP_256);
		pos += RSN_SELECTOR_LEN;
	}
	if (ciphers & WPA_CIPHER_CCMP) {
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_CCMP);
		pos += RSN_SELECTOR_LEN;
	}
	if (ciphers & WPA_CIPHER_GCMP) {
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_GCMP);
		pos += RSN_SELECTOR_LEN;
	}
	if (ciphers & WPA_CIPHER_TKIP) {
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_TKIP);
		pos += RSN_SELECTOR_LEN;
	}
	if (ciphers & WPA_CIPHER_NONE) {
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_NONE);
		pos += RSN_SELECTOR_LEN;
	}

	return (pos - start) / RSN_SELECTOR_LEN;
}


int wpa_cipher_put_suites(u8 *start, int ciphers)
{
	u8 *pos = start;

	if (ciphers & WPA_CIPHER_CCMP) {
		RSN_SELECTOR_PUT(pos, WPA_CIPHER_SUITE_CCMP);
		pos += WPA_SELECTOR_LEN;
	}
	if (ciphers & WPA_CIPHER_TKIP) {
		RSN_SELECTOR_PUT(pos, WPA_CIPHER_SUITE_TKIP);
		pos += WPA_SELECTOR_LEN;
	}
	if (ciphers & WPA_CIPHER_NONE) {
		RSN_SELECTOR_PUT(pos, WPA_CIPHER_SUITE_NONE);
		pos += WPA_SELECTOR_LEN;
	}

	return (pos - start) / RSN_SELECTOR_LEN;
}


int wpa_pick_pairwise_cipher(int ciphers, int none_allowed)
{
	if (ciphers & WPA_CIPHER_CCMP_256)
		return WPA_CIPHER_CCMP_256;
	if (ciphers & WPA_CIPHER_GCMP_256)
		return WPA_CIPHER_GCMP_256;
	if (ciphers & WPA_CIPHER_CCMP)
		return WPA_CIPHER_CCMP;
	if (ciphers & WPA_CIPHER_GCMP)
		return WPA_CIPHER_GCMP;
	if (ciphers & WPA_CIPHER_TKIP)
		return WPA_CIPHER_TKIP;
	if (none_allowed && (ciphers & WPA_CIPHER_NONE))
		return WPA_CIPHER_NONE;
	return -1;
}


int wpa_pick_group_cipher(int ciphers)
{
	if (ciphers & WPA_CIPHER_CCMP_256)
		return WPA_CIPHER_CCMP_256;
	if (ciphers & WPA_CIPHER_GCMP_256)
		return WPA_CIPHER_GCMP_256;
	if (ciphers & WPA_CIPHER_CCMP)
		return WPA_CIPHER_CCMP;
	if (ciphers & WPA_CIPHER_GCMP)
		return WPA_CIPHER_GCMP;
	if (ciphers & WPA_CIPHER_GTK_NOT_USED)
		return WPA_CIPHER_GTK_NOT_USED;
	if (ciphers & WPA_CIPHER_TKIP)
		return WPA_CIPHER_TKIP;
	return -1;
}


int wpa_parse_cipher(const char *value)
{
	int val = 0, last;
	char *start, *end, *buf;

	buf = os_strdup(value);
	if (buf == NULL)
		return -1;
	start = buf;

	while (*start != '\0') {
		while (*start == ' ' || *start == '\t')
			start++;
		if (*start == '\0')
			break;
		end = start;
		while (*end != ' ' && *end != '\t' && *end != '\0')
			end++;
		last = *end == '\0';
		*end = '\0';
		if (os_strcmp(start, "CCMP-256") == 0)
			val |= WPA_CIPHER_CCMP_256;
		else if (os_strcmp(start, "GCMP-256") == 0)
			val |= WPA_CIPHER_GCMP_256;
		else if (os_strcmp(start, "CCMP") == 0)
			val |= WPA_CIPHER_CCMP;
		else if (os_strcmp(start, "GCMP") == 0)
			val |= WPA_CIPHER_GCMP;
		else if (os_strcmp(start, "TKIP") == 0)
			val |= WPA_CIPHER_TKIP;
		else if (os_strcmp(start, "WEP104") == 0)
			val |= WPA_CIPHER_WEP104;
		else if (os_strcmp(start, "WEP40") == 0)
			val |= WPA_CIPHER_WEP40;
		else if (os_strcmp(start, "NONE") == 0)
			val |= WPA_CIPHER_NONE;
		else if (os_strcmp(start, "GTK_NOT_USED") == 0)
			val |= WPA_CIPHER_GTK_NOT_USED;
		else if (os_strcmp(start, "AES-128-CMAC") == 0)
			val |= WPA_CIPHER_AES_128_CMAC;
		else if (os_strcmp(start, "BIP-GMAC-128") == 0)
			val |= WPA_CIPHER_BIP_GMAC_128;
		else if (os_strcmp(start, "BIP-GMAC-256") == 0)
			val |= WPA_CIPHER_BIP_GMAC_256;
		else if (os_strcmp(start, "BIP-CMAC-256") == 0)
			val |= WPA_CIPHER_BIP_CMAC_256;
		else {
			os_free(buf);
			return -1;
		}

		if (last)
			break;
		start = end + 1;
	}
	os_free(buf);

	return val;
}


int wpa_write_ciphers(char *start, char *end, int ciphers, const char *delim)
{
	char *pos = start;
	int ret;

	if (ciphers & WPA_CIPHER_CCMP_256) {
		ret = os_snprintf(pos, end - pos, "%sCCMP-256",
				  pos == start ? "" : delim);
		if (os_snprintf_error(end - pos, ret))
			return -1;
		pos += ret;
	}
	if (ciphers & WPA_CIPHER_GCMP_256) {
		ret = os_snprintf(pos, end - pos, "%sGCMP-256",
				  pos == start ? "" : delim);
		if (os_snprintf_error(end - pos, ret))
			return -1;
		pos += ret;
	}
	if (ciphers & WPA_CIPHER_CCMP) {
		ret = os_snprintf(pos, end - pos, "%sCCMP",
				  pos == start ? "" : delim);
		if (os_snprintf_error(end - pos, ret))
			return -1;
		pos += ret;
	}
	if (ciphers & WPA_CIPHER_GCMP) {
		ret = os_snprintf(pos, end - pos, "%sGCMP",
				  pos == start ? "" : delim);
		if (os_snprintf_error(end - pos, ret))
			return -1;
		pos += ret;
	}
	if (ciphers & WPA_CIPHER_TKIP) {
		ret = os_snprintf(pos, end - pos, "%sTKIP",
				  pos == start ? "" : delim);
		if (os_snprintf_error(end - pos, ret))
			return -1;
		pos += ret;
	}
	if (ciphers & WPA_CIPHER_AES_128_CMAC) {
		ret = os_snprintf(pos, end - pos, "%sAES-128-CMAC",
				  pos == start ? "" : delim);
		if (os_snprintf_error(end - pos, ret))
			return -1;
		pos += ret;
	}
	if (ciphers & WPA_CIPHER_BIP_GMAC_128) {
		ret = os_snprintf(pos, end - pos, "%sBIP-GMAC-128",
				  pos == start ? "" : delim);
		if (os_snprintf_error(end - pos, ret))
			return -1;
		pos += ret;
	}
	if (ciphers & WPA_CIPHER_BIP_GMAC_256) {
		ret = os_snprintf(pos, end - pos, "%sBIP-GMAC-256",
				  pos == start ? "" : delim);
		if (os_snprintf_error(end - pos, ret))
			return -1;
		pos += ret;
	}
	if (ciphers & WPA_CIPHER_BIP_CMAC_256) {
		ret = os_snprintf(pos, end - pos, "%sBIP-CMAC-256",
				  pos == start ? "" : delim);
		if (os_snprintf_error(end - pos, ret))
			return -1;
		pos += ret;
	}
	if (ciphers & WPA_CIPHER_NONE) {
		ret = os_snprintf(pos, end - pos, "%sNONE",
				  pos == start ? "" : delim);
		if (os_snprintf_error(end - pos, ret))
			return -1;
		pos += ret;
	}

	return pos - start;
}


int wpa_select_ap_group_cipher(int wpa, int wpa_pairwise, int rsn_pairwise)
{
	int pairwise = 0;

	/* Select group cipher based on the enabled pairwise cipher suites */
	if (wpa & 1)
		pairwise |= wpa_pairwise;
	if (wpa & 2)
		pairwise |= rsn_pairwise;

	if (pairwise & WPA_CIPHER_TKIP)
		return WPA_CIPHER_TKIP;
	if ((pairwise & (WPA_CIPHER_CCMP | WPA_CIPHER_GCMP)) == WPA_CIPHER_GCMP)
		return WPA_CIPHER_GCMP;
	if ((pairwise & (WPA_CIPHER_GCMP_256 | WPA_CIPHER_CCMP |
			 WPA_CIPHER_GCMP)) == WPA_CIPHER_GCMP_256)
		return WPA_CIPHER_GCMP_256;
	if ((pairwise & (WPA_CIPHER_CCMP_256 | WPA_CIPHER_CCMP |
			 WPA_CIPHER_GCMP)) == WPA_CIPHER_CCMP_256)
		return WPA_CIPHER_CCMP_256;
	return WPA_CIPHER_CCMP;
}


#ifdef CONFIG_FILS
int fils_domain_name_hash(const char *domain, u8 *hash)
{
	char buf[255], *wpos = buf;
	const char *pos = domain;
	size_t len;
	const u8 *addr[1];
	u8 mac[SHA256_MAC_LEN];

	for (len = 0; len < sizeof(buf) && *pos; len++) {
		if (isalpha(*pos) && isupper(*pos))
			*wpos++ = tolower(*pos);
		else
			*wpos++ = *pos;
		pos++;
	}

	addr[0] = (const u8 *) buf;
	if (sha256_vector(1, addr, &len, mac) < 0)
		return -1;
	os_memcpy(hash, mac, 2);
	return 0;
}
#endif /* CONFIG_FILS */
