/*
 * EAP peer/server: EAP-SIM/AKA/AKA' shared routines
 * Copyright (c) 2004-2008, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "wpabuf.h"
#include "crypto/aes_wrap.h"
#include "crypto/crypto.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "crypto/random.h"
#include "eap_common/eap_defs.h"
#include "eap_common/eap_sim_common.h"


static int eap_sim_prf(const u8 *key, u8 *x, size_t xlen)
{
	return fips186_2_prf(key, EAP_SIM_MK_LEN, x, xlen);
}


void eap_sim_derive_mk(const u8 *identity, size_t identity_len,
		       const u8 *nonce_mt, u16 selected_version,
		       const u8 *ver_list, size_t ver_list_len,
		       int num_chal, const u8 *kc, u8 *mk)
{
	u8 sel_ver[2];
	const unsigned char *addr[5];
	size_t len[5];

	addr[0] = identity;
	len[0] = identity_len;
	addr[1] = kc;
	len[1] = num_chal * EAP_SIM_KC_LEN;
	addr[2] = nonce_mt;
	len[2] = EAP_SIM_NONCE_MT_LEN;
	addr[3] = ver_list;
	len[3] = ver_list_len;
	addr[4] = sel_ver;
	len[4] = 2;

	WPA_PUT_BE16(sel_ver, selected_version);

	/* MK = SHA1(Identity|n*Kc|NONCE_MT|Version List|Selected Version) */
	sha1_vector(5, addr, len, mk);
	wpa_hexdump_key(MSG_DEBUG, "EAP-SIM: MK", mk, EAP_SIM_MK_LEN);
}


void eap_aka_derive_mk(const u8 *identity, size_t identity_len,
		       const u8 *ik, const u8 *ck, u8 *mk)
{
	const u8 *addr[3];
	size_t len[3];

	addr[0] = identity;
	len[0] = identity_len;
	addr[1] = ik;
	len[1] = EAP_AKA_IK_LEN;
	addr[2] = ck;
	len[2] = EAP_AKA_CK_LEN;

	/* MK = SHA1(Identity|IK|CK) */
	sha1_vector(3, addr, len, mk);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA: IK", ik, EAP_AKA_IK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA: CK", ck, EAP_AKA_CK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA: MK", mk, EAP_SIM_MK_LEN);
}


int eap_sim_derive_keys(const u8 *mk, u8 *k_encr, u8 *k_aut, u8 *msk, u8 *emsk)
{
	u8 buf[EAP_SIM_K_ENCR_LEN + EAP_SIM_K_AUT_LEN +
	       EAP_SIM_KEYING_DATA_LEN + EAP_EMSK_LEN], *pos;
	if (eap_sim_prf(mk, buf, sizeof(buf)) < 0) {
		wpa_printf(MSG_ERROR, "EAP-SIM: Failed to derive keys");
		return -1;
	}
	pos = buf;
	os_memcpy(k_encr, pos, EAP_SIM_K_ENCR_LEN);
	pos += EAP_SIM_K_ENCR_LEN;
	os_memcpy(k_aut, pos, EAP_SIM_K_AUT_LEN);
	pos += EAP_SIM_K_AUT_LEN;
	os_memcpy(msk, pos, EAP_SIM_KEYING_DATA_LEN);
	pos += EAP_SIM_KEYING_DATA_LEN;
	os_memcpy(emsk, pos, EAP_EMSK_LEN);

	wpa_hexdump_key(MSG_DEBUG, "EAP-SIM: K_encr",
			k_encr, EAP_SIM_K_ENCR_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-SIM: K_aut",
			k_aut, EAP_SIM_K_AUT_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-SIM: keying material (MSK)",
			msk, EAP_SIM_KEYING_DATA_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-SIM: EMSK", emsk, EAP_EMSK_LEN);
	os_memset(buf, 0, sizeof(buf));

	return 0;
}


int eap_sim_derive_keys_reauth(u16 _counter,
			       const u8 *identity, size_t identity_len,
			       const u8 *nonce_s, const u8 *mk, u8 *msk,
			       u8 *emsk)
{
	u8 xkey[SHA1_MAC_LEN];
	u8 buf[EAP_SIM_KEYING_DATA_LEN + EAP_EMSK_LEN + 32];
	u8 counter[2];
	const u8 *addr[4];
	size_t len[4];

	while (identity_len > 0 && identity[identity_len - 1] == 0) {
		wpa_printf(MSG_DEBUG, "EAP-SIM: Workaround - drop null "
			   "character from the end of identity");
		identity_len--;
	}
	addr[0] = identity;
	len[0] = identity_len;
	addr[1] = counter;
	len[1] = 2;
	addr[2] = nonce_s;
	len[2] = EAP_SIM_NONCE_S_LEN;
	addr[3] = mk;
	len[3] = EAP_SIM_MK_LEN;

	WPA_PUT_BE16(counter, _counter);

	wpa_printf(MSG_DEBUG, "EAP-SIM: Deriving keying data from reauth");
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-SIM: Identity",
			  identity, identity_len);
	wpa_hexdump(MSG_DEBUG, "EAP-SIM: counter", counter, 2);
	wpa_hexdump(MSG_DEBUG, "EAP-SIM: NONCE_S", nonce_s,
		    EAP_SIM_NONCE_S_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-SIM: MK", mk, EAP_SIM_MK_LEN);

	/* XKEY' = SHA1(Identity|counter|NONCE_S|MK) */
	sha1_vector(4, addr, len, xkey);
	wpa_hexdump(MSG_DEBUG, "EAP-SIM: XKEY'", xkey, SHA1_MAC_LEN);

	if (eap_sim_prf(xkey, buf, sizeof(buf)) < 0) {
		wpa_printf(MSG_ERROR, "EAP-SIM: Failed to derive keys");
		return -1;
	}
	if (msk) {
		os_memcpy(msk, buf, EAP_SIM_KEYING_DATA_LEN);
		wpa_hexdump(MSG_DEBUG, "EAP-SIM: keying material (MSK)",
			    msk, EAP_SIM_KEYING_DATA_LEN);
	}
	if (emsk) {
		os_memcpy(emsk, buf + EAP_SIM_KEYING_DATA_LEN, EAP_EMSK_LEN);
		wpa_hexdump(MSG_DEBUG, "EAP-SIM: EMSK", emsk, EAP_EMSK_LEN);
	}
	os_memset(buf, 0, sizeof(buf));

	return 0;
}


int eap_sim_verify_mac(const u8 *k_aut, const struct wpabuf *req,
		       const u8 *mac, const u8 *extra, size_t extra_len)
{
	unsigned char hmac[SHA1_MAC_LEN];
	const u8 *addr[2];
	size_t len[2];
	u8 *tmp;

	if (mac == NULL || wpabuf_len(req) < EAP_SIM_MAC_LEN ||
	    mac < wpabuf_head_u8(req) ||
	    mac > wpabuf_head_u8(req) + wpabuf_len(req) - EAP_SIM_MAC_LEN)
		return -1;

	tmp = os_memdup(wpabuf_head(req), wpabuf_len(req));
	if (tmp == NULL)
		return -1;

	addr[0] = tmp;
	len[0] = wpabuf_len(req);
	addr[1] = extra;
	len[1] = extra_len;

	/* HMAC-SHA1-128 */
	os_memset(tmp + (mac - wpabuf_head_u8(req)), 0, EAP_SIM_MAC_LEN);
	wpa_hexdump(MSG_MSGDUMP, "EAP-SIM: Verify MAC - msg",
		    tmp, wpabuf_len(req));
	wpa_hexdump(MSG_MSGDUMP, "EAP-SIM: Verify MAC - extra data",
		    extra, extra_len);
	wpa_hexdump_key(MSG_MSGDUMP, "EAP-SIM: Verify MAC - K_aut",
			k_aut, EAP_SIM_K_AUT_LEN);
	hmac_sha1_vector(k_aut, EAP_SIM_K_AUT_LEN, 2, addr, len, hmac);
	wpa_hexdump(MSG_MSGDUMP, "EAP-SIM: Verify MAC: MAC",
		    hmac, EAP_SIM_MAC_LEN);
	os_free(tmp);

	return (os_memcmp_const(hmac, mac, EAP_SIM_MAC_LEN) == 0) ? 0 : 1;
}


void eap_sim_add_mac(const u8 *k_aut, const u8 *msg, size_t msg_len, u8 *mac,
		     const u8 *extra, size_t extra_len)
{
	unsigned char hmac[SHA1_MAC_LEN];
	const u8 *addr[2];
	size_t len[2];

	addr[0] = msg;
	len[0] = msg_len;
	addr[1] = extra;
	len[1] = extra_len;

	/* HMAC-SHA1-128 */
	os_memset(mac, 0, EAP_SIM_MAC_LEN);
	wpa_hexdump(MSG_MSGDUMP, "EAP-SIM: Add MAC - msg", msg, msg_len);
	wpa_hexdump(MSG_MSGDUMP, "EAP-SIM: Add MAC - extra data",
		    extra, extra_len);
	wpa_hexdump_key(MSG_MSGDUMP, "EAP-SIM: Add MAC - K_aut",
			k_aut, EAP_SIM_K_AUT_LEN);
	hmac_sha1_vector(k_aut, EAP_SIM_K_AUT_LEN, 2, addr, len, hmac);
	os_memcpy(mac, hmac, EAP_SIM_MAC_LEN);
	wpa_hexdump(MSG_MSGDUMP, "EAP-SIM: Add MAC: MAC",
		    mac, EAP_SIM_MAC_LEN);
}


#if defined(EAP_AKA_PRIME) || defined(EAP_SERVER_AKA_PRIME)
static void prf_prime(const u8 *k, const char *seed1,
		      const u8 *seed2, size_t seed2_len,
		      const u8 *seed3, size_t seed3_len,
		      u8 *res, size_t res_len)
{
	const u8 *addr[5];
	size_t len[5];
	u8 hash[SHA256_MAC_LEN];
	u8 iter;

	/*
	 * PRF'(K,S) = T1 | T2 | T3 | T4 | ...
	 * T1 = HMAC-SHA-256 (K, S | 0x01)
	 * T2 = HMAC-SHA-256 (K, T1 | S | 0x02)
	 * T3 = HMAC-SHA-256 (K, T2 | S | 0x03)
	 * T4 = HMAC-SHA-256 (K, T3 | S | 0x04)
	 * ...
	 */

	addr[0] = hash;
	len[0] = 0;
	addr[1] = (const u8 *) seed1;
	len[1] = os_strlen(seed1);
	addr[2] = seed2;
	len[2] = seed2_len;
	addr[3] = seed3;
	len[3] = seed3_len;
	addr[4] = &iter;
	len[4] = 1;

	iter = 0;
	while (res_len) {
		size_t hlen;
		iter++;
		hmac_sha256_vector(k, 32, 5, addr, len, hash);
		len[0] = SHA256_MAC_LEN;
		hlen = res_len > SHA256_MAC_LEN ? SHA256_MAC_LEN : res_len;
		os_memcpy(res, hash, hlen);
		res += hlen;
		res_len -= hlen;
	}
}


void eap_aka_prime_derive_keys(const u8 *identity, size_t identity_len,
			       const u8 *ik, const u8 *ck, u8 *k_encr,
			       u8 *k_aut, u8 *k_re, u8 *msk, u8 *emsk)
{
	u8 key[EAP_AKA_IK_LEN + EAP_AKA_CK_LEN];
	u8 keys[EAP_SIM_K_ENCR_LEN + EAP_AKA_PRIME_K_AUT_LEN +
		EAP_AKA_PRIME_K_RE_LEN + EAP_MSK_LEN + EAP_EMSK_LEN];
	u8 *pos;

	/*
	 * MK = PRF'(IK'|CK',"EAP-AKA'"|Identity)
	 * K_encr = MK[0..127]
	 * K_aut  = MK[128..383]
	 * K_re   = MK[384..639]
	 * MSK    = MK[640..1151]
	 * EMSK   = MK[1152..1663]
	 */

	os_memcpy(key, ik, EAP_AKA_IK_LEN);
	os_memcpy(key + EAP_AKA_IK_LEN, ck, EAP_AKA_CK_LEN);

	prf_prime(key, "EAP-AKA'", identity, identity_len, NULL, 0,
		  keys, sizeof(keys));

	pos = keys;
	os_memcpy(k_encr, pos, EAP_SIM_K_ENCR_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA': K_encr",
			k_encr, EAP_SIM_K_ENCR_LEN);
	pos += EAP_SIM_K_ENCR_LEN;

	os_memcpy(k_aut, pos, EAP_AKA_PRIME_K_AUT_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA': K_aut",
			k_aut, EAP_AKA_PRIME_K_AUT_LEN);
	pos += EAP_AKA_PRIME_K_AUT_LEN;

	os_memcpy(k_re, pos, EAP_AKA_PRIME_K_RE_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA': K_re",
			k_re, EAP_AKA_PRIME_K_RE_LEN);
	pos += EAP_AKA_PRIME_K_RE_LEN;

	os_memcpy(msk, pos, EAP_MSK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA': MSK", msk, EAP_MSK_LEN);
	pos += EAP_MSK_LEN;

	os_memcpy(emsk, pos, EAP_EMSK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA': EMSK", emsk, EAP_EMSK_LEN);
}


int eap_aka_prime_derive_keys_reauth(const u8 *k_re, u16 counter,
				     const u8 *identity, size_t identity_len,
				     const u8 *nonce_s, u8 *msk, u8 *emsk)
{
	u8 seed3[2 + EAP_SIM_NONCE_S_LEN];
	u8 keys[EAP_MSK_LEN + EAP_EMSK_LEN];
	u8 *pos;

	/*
	 * MK = PRF'(K_re,"EAP-AKA' re-auth"|Identity|counter|NONCE_S)
	 * MSK  = MK[0..511]
	 * EMSK = MK[512..1023]
	 */

	WPA_PUT_BE16(seed3, counter);
	os_memcpy(seed3 + 2, nonce_s, EAP_SIM_NONCE_S_LEN);

	prf_prime(k_re, "EAP-AKA' re-auth", identity, identity_len,
		  seed3, sizeof(seed3),
		  keys, sizeof(keys));

	pos = keys;
	os_memcpy(msk, pos, EAP_MSK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA': MSK", msk, EAP_MSK_LEN);
	pos += EAP_MSK_LEN;

	os_memcpy(emsk, pos, EAP_EMSK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA': EMSK", emsk, EAP_EMSK_LEN);

	os_memset(keys, 0, sizeof(keys));

	return 0;
}


int eap_sim_verify_mac_sha256(const u8 *k_aut, const struct wpabuf *req,
			      const u8 *mac, const u8 *extra, size_t extra_len)
{
	unsigned char hmac[SHA256_MAC_LEN];
	const u8 *addr[2];
	size_t len[2];
	u8 *tmp;

	if (mac == NULL || wpabuf_len(req) < EAP_SIM_MAC_LEN ||
	    mac < wpabuf_head_u8(req) ||
	    mac > wpabuf_head_u8(req) + wpabuf_len(req) - EAP_SIM_MAC_LEN)
		return -1;

	tmp = os_memdup(wpabuf_head(req), wpabuf_len(req));
	if (tmp == NULL)
		return -1;

	addr[0] = tmp;
	len[0] = wpabuf_len(req);
	addr[1] = extra;
	len[1] = extra_len;

	/* HMAC-SHA-256-128 */
	os_memset(tmp + (mac - wpabuf_head_u8(req)), 0, EAP_SIM_MAC_LEN);
	wpa_hexdump(MSG_MSGDUMP, "EAP-AKA': Verify MAC - msg",
		    tmp, wpabuf_len(req));
	wpa_hexdump(MSG_MSGDUMP, "EAP-AKA': Verify MAC - extra data",
		    extra, extra_len);
	wpa_hexdump_key(MSG_MSGDUMP, "EAP-AKA': Verify MAC - K_aut",
			k_aut, EAP_AKA_PRIME_K_AUT_LEN);
	hmac_sha256_vector(k_aut, EAP_AKA_PRIME_K_AUT_LEN, 2, addr, len, hmac);
	wpa_hexdump(MSG_MSGDUMP, "EAP-AKA': Verify MAC: MAC",
		    hmac, EAP_SIM_MAC_LEN);
	os_free(tmp);

	return (os_memcmp_const(hmac, mac, EAP_SIM_MAC_LEN) == 0) ? 0 : 1;
}


void eap_sim_add_mac_sha256(const u8 *k_aut, const u8 *msg, size_t msg_len,
			    u8 *mac, const u8 *extra, size_t extra_len)
{
	unsigned char hmac[SHA256_MAC_LEN];
	const u8 *addr[2];
	size_t len[2];

	addr[0] = msg;
	len[0] = msg_len;
	addr[1] = extra;
	len[1] = extra_len;

	/* HMAC-SHA-256-128 */
	os_memset(mac, 0, EAP_SIM_MAC_LEN);
	wpa_hexdump(MSG_MSGDUMP, "EAP-AKA': Add MAC - msg", msg, msg_len);
	wpa_hexdump(MSG_MSGDUMP, "EAP-AKA': Add MAC - extra data",
		    extra, extra_len);
	wpa_hexdump_key(MSG_MSGDUMP, "EAP-AKA': Add MAC - K_aut",
			k_aut, EAP_AKA_PRIME_K_AUT_LEN);
	hmac_sha256_vector(k_aut, EAP_AKA_PRIME_K_AUT_LEN, 2, addr, len, hmac);
	os_memcpy(mac, hmac, EAP_SIM_MAC_LEN);
	wpa_hexdump(MSG_MSGDUMP, "EAP-AKA': Add MAC: MAC",
		    mac, EAP_SIM_MAC_LEN);
}


void eap_aka_prime_derive_ck_ik_prime(u8 *ck, u8 *ik, const u8 *sqn_ak,
				      const u8 *network_name,
				      size_t network_name_len)
{
	u8 key[EAP_AKA_CK_LEN + EAP_AKA_IK_LEN];
	u8 hash[SHA256_MAC_LEN];
	const u8 *addr[5];
	size_t len[5];
	u8 fc;
	u8 l0[2], l1[2];

	/* 3GPP TS 33.402 V8.0.0
	 * (CK', IK') = F(CK, IK, <access network identity>)
	 */
	/* TODO: CK', IK' generation should really be moved into the actual
	 * AKA procedure with network name passed in there and option to use
	 * AMF separation bit = 1 (3GPP TS 33.401). */

	/* Change Request 33.402 CR 0033 to version 8.1.1 from
	 * 3GPP TSG-SA WG3 Meeting #53 in September 2008:
	 *
	 * CK' || IK' = HMAC-SHA-256(Key, S)
	 * S = FC || P0 || L0 || P1 || L1 || ... || Pn || Ln
	 * Key = CK || IK
	 * FC = 0x20
	 * P0 = access network identity (3GPP TS 24.302)
	 * L0 = length of acceess network identity (2 octets, big endian)
	 * P1 = SQN xor AK (if AK is not used, AK is treaded as 000..0
	 * L1 = 0x00 0x06
	 */

	fc = 0x20;

	wpa_printf(MSG_DEBUG, "EAP-AKA': Derive (CK',IK') from (CK,IK)");
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA': CK", ck, EAP_AKA_CK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA': IK", ik, EAP_AKA_IK_LEN);
	wpa_printf(MSG_DEBUG, "EAP-AKA': FC = 0x%x", fc);
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-AKA': P0 = Access network identity",
			  network_name, network_name_len);
	wpa_hexdump(MSG_DEBUG, "EAP-AKA': P1 = SQN xor AK", sqn_ak, 6);

	os_memcpy(key, ck, EAP_AKA_CK_LEN);
	os_memcpy(key + EAP_AKA_CK_LEN, ik, EAP_AKA_IK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA': Key = CK || IK",
			key, sizeof(key));

	addr[0] = &fc;
	len[0] = 1;
	addr[1] = network_name;
	len[1] = network_name_len;
	WPA_PUT_BE16(l0, network_name_len);
	addr[2] = l0;
	len[2] = 2;
	addr[3] = sqn_ak;
	len[3] = 6;
	WPA_PUT_BE16(l1, 6);
	addr[4] = l1;
	len[4] = 2;

	hmac_sha256_vector(key, sizeof(key), 5, addr, len, hash);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA': KDF output (CK' || IK')",
			hash, sizeof(hash));

	os_memcpy(ck, hash, EAP_AKA_CK_LEN);
	os_memcpy(ik, hash + EAP_AKA_CK_LEN, EAP_AKA_IK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA': CK'", ck, EAP_AKA_CK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-AKA': IK'", ik, EAP_AKA_IK_LEN);
}
#endif /* EAP_AKA_PRIME || EAP_SERVER_AKA_PRIME */


int eap_sim_parse_attr(const u8 *start, const u8 *end,
		       struct eap_sim_attrs *attr, int aka, int encr)
{
	const u8 *pos = start, *apos;
	size_t alen, plen, i, list_len;

	os_memset(attr, 0, sizeof(*attr));
	attr->id_req = NO_ID_REQ;
	attr->notification = -1;
	attr->counter = -1;
	attr->selected_version = -1;
	attr->client_error_code = -1;

	while (pos < end) {
		if (pos + 2 > end) {
			wpa_printf(MSG_INFO, "EAP-SIM: Attribute overflow(1)");
			return -1;
		}
		wpa_printf(MSG_MSGDUMP, "EAP-SIM: Attribute: Type=%d Len=%d",
			   pos[0], pos[1] * 4);
		if (pos + pos[1] * 4 > end) {
			wpa_printf(MSG_INFO, "EAP-SIM: Attribute overflow "
				   "(pos=%p len=%d end=%p)",
				   pos, pos[1] * 4, end);
			return -1;
		}
		if (pos[1] == 0) {
			wpa_printf(MSG_INFO, "EAP-SIM: Attribute underflow");
			return -1;
		}
		apos = pos + 2;
		alen = pos[1] * 4 - 2;
		wpa_hexdump(MSG_MSGDUMP, "EAP-SIM: Attribute data",
			    apos, alen);

		switch (pos[0]) {
		case EAP_SIM_AT_RAND:
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_RAND");
			apos += 2;
			alen -= 2;
			if ((!aka && (alen % GSM_RAND_LEN)) ||
			    (aka && alen != EAP_AKA_RAND_LEN)) {
				wpa_printf(MSG_INFO, "EAP-SIM: Invalid AT_RAND"
					   " (len %lu)",
					   (unsigned long) alen);
				return -1;
			}
			attr->rand = apos;
			attr->num_chal = alen / GSM_RAND_LEN;
			break;
		case EAP_SIM_AT_AUTN:
			wpa_printf(MSG_DEBUG, "EAP-AKA: AT_AUTN");
			if (!aka) {
				wpa_printf(MSG_DEBUG, "EAP-SIM: "
					   "Unexpected AT_AUTN");
				return -1;
			}
			apos += 2;
			alen -= 2;
			if (alen != EAP_AKA_AUTN_LEN) {
				wpa_printf(MSG_INFO, "EAP-AKA: Invalid AT_AUTN"
					   " (len %lu)",
					   (unsigned long) alen);
				return -1;
			}
			attr->autn = apos;
			break;
		case EAP_SIM_AT_PADDING:
			if (!encr) {
				wpa_printf(MSG_ERROR, "EAP-SIM: Unencrypted "
					   "AT_PADDING");
				return -1;
			}
			wpa_printf(MSG_DEBUG, "EAP-SIM: (encr) AT_PADDING");
			for (i = 2; i < alen; i++) {
				if (apos[i] != 0) {
					wpa_printf(MSG_INFO, "EAP-SIM: (encr) "
						   "AT_PADDING used a non-zero"
						   " padding byte");
					wpa_hexdump(MSG_DEBUG, "EAP-SIM: "
						    "(encr) padding bytes",
						    apos + 2, alen - 2);
					return -1;
				}
			}
			break;
		case EAP_SIM_AT_NONCE_MT:
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_NONCE_MT");
			if (alen != 2 + EAP_SIM_NONCE_MT_LEN) {
				wpa_printf(MSG_INFO, "EAP-SIM: Invalid "
					   "AT_NONCE_MT length");
				return -1;
			}
			attr->nonce_mt = apos + 2;
			break;
		case EAP_SIM_AT_PERMANENT_ID_REQ:
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_PERMANENT_ID_REQ");
			attr->id_req = PERMANENT_ID;
			break;
		case EAP_SIM_AT_MAC:
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_MAC");
			if (alen != 2 + EAP_SIM_MAC_LEN) {
				wpa_printf(MSG_INFO, "EAP-SIM: Invalid AT_MAC "
					   "length");
				return -1;
			}
			attr->mac = apos + 2;
			break;
		case EAP_SIM_AT_NOTIFICATION:
			if (alen != 2) {
				wpa_printf(MSG_INFO, "EAP-SIM: Invalid "
					   "AT_NOTIFICATION length %lu",
					   (unsigned long) alen);
				return -1;
			}
			attr->notification = apos[0] * 256 + apos[1];
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_NOTIFICATION %d",
				   attr->notification);
			break;
		case EAP_SIM_AT_ANY_ID_REQ:
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_ANY_ID_REQ");
			attr->id_req = ANY_ID;
			break;
		case EAP_SIM_AT_IDENTITY:
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_IDENTITY");
			plen = WPA_GET_BE16(apos);
			apos += 2;
			alen -= 2;
			if (plen > alen) {
				wpa_printf(MSG_INFO, "EAP-SIM: Invalid "
					   "AT_IDENTITY (Actual Length %lu, "
					   "remaining length %lu)",
					   (unsigned long) plen,
					   (unsigned long) alen);
				return -1;
			}

			attr->identity = apos;
			attr->identity_len = plen;
			break;
		case EAP_SIM_AT_VERSION_LIST:
			if (aka) {
				wpa_printf(MSG_DEBUG, "EAP-AKA: "
					   "Unexpected AT_VERSION_LIST");
				return -1;
			}
			list_len = apos[0] * 256 + apos[1];
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_VERSION_LIST");
			if (list_len < 2 || list_len > alen - 2) {
				wpa_printf(MSG_WARNING, "EAP-SIM: Invalid "
					   "AT_VERSION_LIST (list_len=%lu "
					   "attr_len=%lu)",
					   (unsigned long) list_len,
					   (unsigned long) alen);
				return -1;
			}
			attr->version_list = apos + 2;
			attr->version_list_len = list_len;
			break;
		case EAP_SIM_AT_SELECTED_VERSION:
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_SELECTED_VERSION");
			if (alen != 2) {
				wpa_printf(MSG_INFO, "EAP-SIM: Invalid "
					   "AT_SELECTED_VERSION length %lu",
					   (unsigned long) alen);
				return -1;
			}
			attr->selected_version = apos[0] * 256 + apos[1];
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_SELECTED_VERSION "
				   "%d", attr->selected_version);
			break;
		case EAP_SIM_AT_FULLAUTH_ID_REQ:
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_FULLAUTH_ID_REQ");
			attr->id_req = FULLAUTH_ID;
			break;
		case EAP_SIM_AT_COUNTER:
			if (!encr) {
				wpa_printf(MSG_ERROR, "EAP-SIM: Unencrypted "
					   "AT_COUNTER");
				return -1;
			}
			if (alen != 2) {
				wpa_printf(MSG_INFO, "EAP-SIM: (encr) Invalid "
					   "AT_COUNTER (alen=%lu)",
					   (unsigned long) alen);
				return -1;
			}
			attr->counter = apos[0] * 256 + apos[1];
			wpa_printf(MSG_DEBUG, "EAP-SIM: (encr) AT_COUNTER %d",
				   attr->counter);
			break;
		case EAP_SIM_AT_COUNTER_TOO_SMALL:
			if (!encr) {
				wpa_printf(MSG_ERROR, "EAP-SIM: Unencrypted "
					   "AT_COUNTER_TOO_SMALL");
				return -1;
			}
			if (alen != 2) {
				wpa_printf(MSG_INFO, "EAP-SIM: (encr) Invalid "
					   "AT_COUNTER_TOO_SMALL (alen=%lu)",
					   (unsigned long) alen);
				return -1;
			}
			wpa_printf(MSG_DEBUG, "EAP-SIM: (encr) "
				   "AT_COUNTER_TOO_SMALL");
			attr->counter_too_small = 1;
			break;
		case EAP_SIM_AT_NONCE_S:
			if (!encr) {
				wpa_printf(MSG_ERROR, "EAP-SIM: Unencrypted "
					   "AT_NONCE_S");
				return -1;
			}
			wpa_printf(MSG_DEBUG, "EAP-SIM: (encr) "
				   "AT_NONCE_S");
			if (alen != 2 + EAP_SIM_NONCE_S_LEN) {
				wpa_printf(MSG_INFO, "EAP-SIM: (encr) Invalid "
					   "AT_NONCE_S (alen=%lu)",
					   (unsigned long) alen);
				return -1;
			}
			attr->nonce_s = apos + 2;
			break;
		case EAP_SIM_AT_CLIENT_ERROR_CODE:
			if (alen != 2) {
				wpa_printf(MSG_INFO, "EAP-SIM: Invalid "
					   "AT_CLIENT_ERROR_CODE length %lu",
					   (unsigned long) alen);
				return -1;
			}
			attr->client_error_code = apos[0] * 256 + apos[1];
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_CLIENT_ERROR_CODE "
				   "%d", attr->client_error_code);
			break;
		case EAP_SIM_AT_IV:
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_IV");
			if (alen != 2 + EAP_SIM_MAC_LEN) {
				wpa_printf(MSG_INFO, "EAP-SIM: Invalid AT_IV "
					   "length %lu", (unsigned long) alen);
				return -1;
			}
			attr->iv = apos + 2;
			break;
		case EAP_SIM_AT_ENCR_DATA:
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_ENCR_DATA");
			attr->encr_data = apos + 2;
			attr->encr_data_len = alen - 2;
			if (attr->encr_data_len % 16) {
				wpa_printf(MSG_INFO, "EAP-SIM: Invalid "
					   "AT_ENCR_DATA length %lu",
					   (unsigned long)
					   attr->encr_data_len);
				return -1;
			}
			break;
		case EAP_SIM_AT_NEXT_PSEUDONYM:
			if (!encr) {
				wpa_printf(MSG_ERROR, "EAP-SIM: Unencrypted "
					   "AT_NEXT_PSEUDONYM");
				return -1;
			}
			wpa_printf(MSG_DEBUG, "EAP-SIM: (encr) "
				   "AT_NEXT_PSEUDONYM");
			plen = apos[0] * 256 + apos[1];
			if (plen > alen - 2) {
				wpa_printf(MSG_INFO, "EAP-SIM: (encr) Invalid"
					   " AT_NEXT_PSEUDONYM (actual"
					   " len %lu, attr len %lu)",
					   (unsigned long) plen,
					   (unsigned long) alen);
				return -1;
			}
			attr->next_pseudonym = pos + 4;
			attr->next_pseudonym_len = plen;
			break;
		case EAP_SIM_AT_NEXT_REAUTH_ID:
			if (!encr) {
				wpa_printf(MSG_ERROR, "EAP-SIM: Unencrypted "
					   "AT_NEXT_REAUTH_ID");
				return -1;
			}
			wpa_printf(MSG_DEBUG, "EAP-SIM: (encr) "
				   "AT_NEXT_REAUTH_ID");
			plen = apos[0] * 256 + apos[1];
			if (plen > alen - 2) {
				wpa_printf(MSG_INFO, "EAP-SIM: (encr) Invalid"
					   " AT_NEXT_REAUTH_ID (actual"
					   " len %lu, attr len %lu)",
					   (unsigned long) plen,
					   (unsigned long) alen);
				return -1;
			}
			attr->next_reauth_id = pos + 4;
			attr->next_reauth_id_len = plen;
			break;
		case EAP_SIM_AT_RES:
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_RES");
			attr->res_len_bits = WPA_GET_BE16(apos);
			apos += 2;
			alen -= 2;
			if (!aka || alen < EAP_AKA_MIN_RES_LEN ||
			    alen > EAP_AKA_MAX_RES_LEN) {
				wpa_printf(MSG_INFO, "EAP-SIM: Invalid AT_RES "
					   "(len %lu)",
					   (unsigned long) alen);
				return -1;
			}
			attr->res = apos;
			attr->res_len = alen;
			break;
		case EAP_SIM_AT_AUTS:
			wpa_printf(MSG_DEBUG, "EAP-AKA: AT_AUTS");
			if (!aka) {
				wpa_printf(MSG_DEBUG, "EAP-SIM: "
					   "Unexpected AT_AUTS");
				return -1;
			}
			if (alen != EAP_AKA_AUTS_LEN) {
				wpa_printf(MSG_INFO, "EAP-AKA: Invalid AT_AUTS"
					   " (len %lu)",
					   (unsigned long) alen);
				return -1;
			}
			attr->auts = apos;
			break;
		case EAP_SIM_AT_CHECKCODE:
			wpa_printf(MSG_DEBUG, "EAP-AKA: AT_CHECKCODE");
			if (!aka) {
				wpa_printf(MSG_DEBUG, "EAP-SIM: "
					   "Unexpected AT_CHECKCODE");
				return -1;
			}
			apos += 2;
			alen -= 2;
			if (alen != 0 && alen != EAP_AKA_CHECKCODE_LEN &&
			    alen != EAP_AKA_PRIME_CHECKCODE_LEN) {
				wpa_printf(MSG_INFO, "EAP-AKA: Invalid "
					   "AT_CHECKCODE (len %lu)",
					   (unsigned long) alen);
				return -1;
			}
			attr->checkcode = apos;
			attr->checkcode_len = alen;
			break;
		case EAP_SIM_AT_RESULT_IND:
			if (encr) {
				wpa_printf(MSG_ERROR, "EAP-SIM: Encrypted "
					   "AT_RESULT_IND");
				return -1;
			}
			if (alen != 2) {
				wpa_printf(MSG_INFO, "EAP-SIM: Invalid "
					   "AT_RESULT_IND (alen=%lu)",
					   (unsigned long) alen);
				return -1;
			}
			wpa_printf(MSG_DEBUG, "EAP-SIM: AT_RESULT_IND");
			attr->result_ind = 1;
			break;
#if defined(EAP_AKA_PRIME) || defined(EAP_SERVER_AKA_PRIME)
		case EAP_SIM_AT_KDF_INPUT:
			if (aka != 2) {
				wpa_printf(MSG_INFO, "EAP-AKA: Unexpected "
					   "AT_KDF_INPUT");
				return -1;
			}

			wpa_printf(MSG_DEBUG, "EAP-AKA: AT_KDF_INPUT");
			plen = WPA_GET_BE16(apos);
			apos += 2;
			alen -= 2;
			if (plen > alen) {
				wpa_printf(MSG_INFO, "EAP-AKA': Invalid "
					   "AT_KDF_INPUT (Actual Length %lu, "
					   "remaining length %lu)",
					   (unsigned long) plen,
					   (unsigned long) alen);
				return -1;
			}
			attr->kdf_input = apos;
			attr->kdf_input_len = plen;
			break;
		case EAP_SIM_AT_KDF:
			if (aka != 2) {
				wpa_printf(MSG_INFO, "EAP-AKA: Unexpected "
					   "AT_KDF");
				return -1;
			}

			wpa_printf(MSG_DEBUG, "EAP-AKA: AT_KDF");
			if (alen != 2) {
				wpa_printf(MSG_INFO, "EAP-AKA': Invalid "
					   "AT_KDF (len %lu)",
					   (unsigned long) alen);
				return -1;
			}
			if (attr->kdf_count == EAP_AKA_PRIME_KDF_MAX) {
				wpa_printf(MSG_DEBUG, "EAP-AKA': Too many "
					   "AT_KDF attributes - ignore this");
				break;
			}
			attr->kdf[attr->kdf_count] = WPA_GET_BE16(apos);
			attr->kdf_count++;
			break;
		case EAP_SIM_AT_BIDDING:
			wpa_printf(MSG_DEBUG, "EAP-AKA: AT_BIDDING");
			if (alen != 2) {
				wpa_printf(MSG_INFO, "EAP-AKA: Invalid "
					   "AT_BIDDING (len %lu)",
					   (unsigned long) alen);
				return -1;
			}
			attr->bidding = apos;
			break;
#endif /* EAP_AKA_PRIME || EAP_SERVER_AKA_PRIME */
		default:
			if (pos[0] < 128) {
				wpa_printf(MSG_INFO, "EAP-SIM: Unrecognized "
					   "non-skippable attribute %d",
					   pos[0]);
				return -1;
			}

			wpa_printf(MSG_DEBUG, "EAP-SIM: Unrecognized skippable"
				   " attribute %d ignored", pos[0]);
			break;
		}

		pos += pos[1] * 4;
	}

	wpa_printf(MSG_DEBUG, "EAP-SIM: Attributes parsed successfully "
		   "(aka=%d encr=%d)", aka, encr);

	return 0;
}


u8 * eap_sim_parse_encr(const u8 *k_encr, const u8 *encr_data,
			size_t encr_data_len, const u8 *iv,
			struct eap_sim_attrs *attr, int aka)
{
	u8 *decrypted;

	if (!iv) {
		wpa_printf(MSG_INFO, "EAP-SIM: Encrypted data, but no IV");
		return NULL;
	}

	decrypted = os_memdup(encr_data, encr_data_len);
	if (decrypted == NULL)
		return NULL;

	if (aes_128_cbc_decrypt(k_encr, iv, decrypted, encr_data_len)) {
		os_free(decrypted);
		return NULL;
	}
	wpa_hexdump(MSG_MSGDUMP, "EAP-SIM: Decrypted AT_ENCR_DATA",
		    decrypted, encr_data_len);

	if (eap_sim_parse_attr(decrypted, decrypted + encr_data_len, attr,
			       aka, 1)) {
		wpa_printf(MSG_INFO, "EAP-SIM: (encr) Failed to parse "
			   "decrypted AT_ENCR_DATA");
		os_free(decrypted);
		return NULL;
	}

	return decrypted;
}


#define EAP_SIM_INIT_LEN 128

struct eap_sim_msg {
	struct wpabuf *buf;
	size_t mac, iv, encr; /* index from buf */
};


struct eap_sim_msg * eap_sim_msg_init(int code, int id, int type, int subtype)
{
	struct eap_sim_msg *msg;
	struct eap_hdr *eap;
	u8 *pos;

	msg = os_zalloc(sizeof(*msg));
	if (msg == NULL)
		return NULL;

	msg->buf = wpabuf_alloc(EAP_SIM_INIT_LEN);
	if (msg->buf == NULL) {
		os_free(msg);
		return NULL;
	}
	eap = wpabuf_put(msg->buf, sizeof(*eap));
	eap->code = code;
	eap->identifier = id;

	pos = wpabuf_put(msg->buf, 4);
	*pos++ = type;
	*pos++ = subtype;
	*pos++ = 0; /* Reserved */
	*pos++ = 0; /* Reserved */

	return msg;
}


struct wpabuf * eap_sim_msg_finish(struct eap_sim_msg *msg, int type,
				   const u8 *k_aut,
				   const u8 *extra, size_t extra_len)
{
	struct eap_hdr *eap;
	struct wpabuf *buf;

	if (msg == NULL)
		return NULL;

	eap = wpabuf_mhead(msg->buf);
	eap->length = host_to_be16(wpabuf_len(msg->buf));

#if defined(EAP_AKA_PRIME) || defined(EAP_SERVER_AKA_PRIME)
	if (k_aut && msg->mac && type == EAP_TYPE_AKA_PRIME) {
		eap_sim_add_mac_sha256(k_aut, (u8 *) wpabuf_head(msg->buf),
				       wpabuf_len(msg->buf),
				       (u8 *) wpabuf_mhead(msg->buf) +
				       msg->mac, extra, extra_len);
	} else
#endif /* EAP_AKA_PRIME || EAP_SERVER_AKA_PRIME */
	if (k_aut && msg->mac) {
		eap_sim_add_mac(k_aut, (u8 *) wpabuf_head(msg->buf),
				wpabuf_len(msg->buf),
				(u8 *) wpabuf_mhead(msg->buf) + msg->mac,
				extra, extra_len);
	}

	buf = msg->buf;
	os_free(msg);
	return buf;
}


void eap_sim_msg_free(struct eap_sim_msg *msg)
{
	if (msg) {
		wpabuf_free(msg->buf);
		os_free(msg);
	}
}


u8 * eap_sim_msg_add_full(struct eap_sim_msg *msg, u8 attr,
			  const u8 *data, size_t len)
{
	int attr_len = 2 + len;
	int pad_len;
	u8 *start;

	if (msg == NULL)
		return NULL;

	pad_len = (4 - attr_len % 4) % 4;
	attr_len += pad_len;
	if (wpabuf_resize(&msg->buf, attr_len))
		return NULL;
	start = wpabuf_put(msg->buf, 0);
	wpabuf_put_u8(msg->buf, attr);
	wpabuf_put_u8(msg->buf, attr_len / 4);
	wpabuf_put_data(msg->buf, data, len);
	if (pad_len)
		os_memset(wpabuf_put(msg->buf, pad_len), 0, pad_len);
	return start;
}


u8 * eap_sim_msg_add(struct eap_sim_msg *msg, u8 attr, u16 value,
		     const u8 *data, size_t len)
{
	int attr_len = 4 + len;
	int pad_len;
	u8 *start;

	if (msg == NULL)
		return NULL;

	pad_len = (4 - attr_len % 4) % 4;
	attr_len += pad_len;
	if (wpabuf_resize(&msg->buf, attr_len))
		return NULL;
	start = wpabuf_put(msg->buf, 0);
	wpabuf_put_u8(msg->buf, attr);
	wpabuf_put_u8(msg->buf, attr_len / 4);
	wpabuf_put_be16(msg->buf, value);
	if (data)
		wpabuf_put_data(msg->buf, data, len);
	else
		wpabuf_put(msg->buf, len);
	if (pad_len)
		os_memset(wpabuf_put(msg->buf, pad_len), 0, pad_len);
	return start;
}


u8 * eap_sim_msg_add_mac(struct eap_sim_msg *msg, u8 attr)
{
	u8 *pos = eap_sim_msg_add(msg, attr, 0, NULL, EAP_SIM_MAC_LEN);
	if (pos)
		msg->mac = (pos - wpabuf_head_u8(msg->buf)) + 4;
	return pos;
}


int eap_sim_msg_add_encr_start(struct eap_sim_msg *msg, u8 attr_iv,
			       u8 attr_encr)
{
	u8 *pos = eap_sim_msg_add(msg, attr_iv, 0, NULL, EAP_SIM_IV_LEN);
	if (pos == NULL)
		return -1;
	msg->iv = (pos - wpabuf_head_u8(msg->buf)) + 4;
	if (random_get_bytes(wpabuf_mhead_u8(msg->buf) + msg->iv,
			     EAP_SIM_IV_LEN)) {
		msg->iv = 0;
		return -1;
	}

	pos = eap_sim_msg_add(msg, attr_encr, 0, NULL, 0);
	if (pos == NULL) {
		msg->iv = 0;
		return -1;
	}
	msg->encr = pos - wpabuf_head_u8(msg->buf);

	return 0;
}


int eap_sim_msg_add_encr_end(struct eap_sim_msg *msg, u8 *k_encr, int attr_pad)
{
	size_t encr_len;

	if (msg == NULL || k_encr == NULL || msg->iv == 0 || msg->encr == 0)
		return -1;

	encr_len = wpabuf_len(msg->buf) - msg->encr - 4;
	if (encr_len % 16) {
		u8 *pos;
		int pad_len = 16 - (encr_len % 16);
		if (pad_len < 4) {
			wpa_printf(MSG_WARNING, "EAP-SIM: "
				   "eap_sim_msg_add_encr_end - invalid pad_len"
				   " %d", pad_len);
			return -1;
		}
		wpa_printf(MSG_DEBUG, "   *AT_PADDING");
		pos = eap_sim_msg_add(msg, attr_pad, 0, NULL, pad_len - 4);
		if (pos == NULL)
			return -1;
		os_memset(pos + 4, 0, pad_len - 4);
		encr_len += pad_len;
	}
	wpa_printf(MSG_DEBUG, "   (AT_ENCR_DATA data len %lu)",
		   (unsigned long) encr_len);
	wpabuf_mhead_u8(msg->buf)[msg->encr + 1] = encr_len / 4 + 1;
	return aes_128_cbc_encrypt(k_encr, wpabuf_head_u8(msg->buf) + msg->iv,
				   wpabuf_mhead_u8(msg->buf) + msg->encr + 4,
				   encr_len);
}


void eap_sim_report_notification(void *msg_ctx, int notification, int aka)
{
#ifndef CONFIG_NO_STDOUT_DEBUG
	const char *type = aka ? "AKA" : "SIM";
#endif /* CONFIG_NO_STDOUT_DEBUG */

	switch (notification) {
	case EAP_SIM_GENERAL_FAILURE_AFTER_AUTH:
		wpa_printf(MSG_WARNING, "EAP-%s: General failure "
			   "notification (after authentication)", type);
		break;
	case EAP_SIM_TEMPORARILY_DENIED:
		wpa_printf(MSG_WARNING, "EAP-%s: Failure notification: "
			   "User has been temporarily denied access to the "
			   "requested service", type);
		break;
	case EAP_SIM_NOT_SUBSCRIBED:
		wpa_printf(MSG_WARNING, "EAP-%s: Failure notification: "
			   "User has not subscribed to the requested service",
			   type);
		break;
	case EAP_SIM_GENERAL_FAILURE_BEFORE_AUTH:
		wpa_printf(MSG_WARNING, "EAP-%s: General failure "
			   "notification (before authentication)", type);
		break;
	case EAP_SIM_SUCCESS:
		wpa_printf(MSG_INFO, "EAP-%s: Successful authentication "
			   "notification", type);
		break;
	default:
		if (notification >= 32768) {
			wpa_printf(MSG_INFO, "EAP-%s: Unrecognized "
				   "non-failure notification %d",
				   type, notification);
		} else {
			wpa_printf(MSG_WARNING, "EAP-%s: Unrecognized "
				   "failure notification %d",
				   type, notification);
		}
	}
}
