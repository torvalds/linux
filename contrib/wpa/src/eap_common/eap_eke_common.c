/*
 * EAP server/peer: EAP-EKE shared routines
 * Copyright (c) 2011-2013, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/aes.h"
#include "crypto/aes_wrap.h"
#include "crypto/crypto.h"
#include "crypto/dh_groups.h"
#include "crypto/random.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "eap_common/eap_defs.h"
#include "eap_eke_common.h"


static int eap_eke_dh_len(u8 group)
{
	switch (group) {
	case EAP_EKE_DHGROUP_EKE_2:
		return 128;
	case EAP_EKE_DHGROUP_EKE_5:
		return 192;
	case EAP_EKE_DHGROUP_EKE_14:
		return 256;
	case EAP_EKE_DHGROUP_EKE_15:
		return 384;
	case EAP_EKE_DHGROUP_EKE_16:
		return 512;
	}

	return -1;
}


static int eap_eke_dhcomp_len(u8 dhgroup, u8 encr)
{
	int dhlen;

	dhlen = eap_eke_dh_len(dhgroup);
	if (dhlen < 0 || encr != EAP_EKE_ENCR_AES128_CBC)
		return -1;
	return AES_BLOCK_SIZE + dhlen;
}


static const struct dh_group * eap_eke_dh_group(u8 group)
{
	switch (group) {
	case EAP_EKE_DHGROUP_EKE_2:
		return dh_groups_get(2);
	case EAP_EKE_DHGROUP_EKE_5:
		return dh_groups_get(5);
	case EAP_EKE_DHGROUP_EKE_14:
		return dh_groups_get(14);
	case EAP_EKE_DHGROUP_EKE_15:
		return dh_groups_get(15);
	case EAP_EKE_DHGROUP_EKE_16:
		return dh_groups_get(16);
	}

	return NULL;
}


static int eap_eke_dh_generator(u8 group)
{
	switch (group) {
	case EAP_EKE_DHGROUP_EKE_2:
		return 5;
	case EAP_EKE_DHGROUP_EKE_5:
		return 31;
	case EAP_EKE_DHGROUP_EKE_14:
		return 11;
	case EAP_EKE_DHGROUP_EKE_15:
		return 5;
	case EAP_EKE_DHGROUP_EKE_16:
		return 5;
	}

	return -1;
}


static int eap_eke_pnonce_len(u8 mac)
{
	int mac_len;

	if (mac == EAP_EKE_MAC_HMAC_SHA1)
		mac_len = SHA1_MAC_LEN;
	else if (mac == EAP_EKE_MAC_HMAC_SHA2_256)
		mac_len = SHA256_MAC_LEN;
	else
		return -1;

	return AES_BLOCK_SIZE + 16 + mac_len;
}


static int eap_eke_pnonce_ps_len(u8 mac)
{
	int mac_len;

	if (mac == EAP_EKE_MAC_HMAC_SHA1)
		mac_len = SHA1_MAC_LEN;
	else if (mac == EAP_EKE_MAC_HMAC_SHA2_256)
		mac_len = SHA256_MAC_LEN;
	else
		return -1;

	return AES_BLOCK_SIZE + 2 * 16 + mac_len;
}


static int eap_eke_prf_len(u8 prf)
{
	if (prf == EAP_EKE_PRF_HMAC_SHA1)
		return 20;
	if (prf == EAP_EKE_PRF_HMAC_SHA2_256)
		return 32;
	return -1;
}


static int eap_eke_nonce_len(u8 prf)
{
	int prf_len;

	prf_len = eap_eke_prf_len(prf);
	if (prf_len < 0)
		return -1;

	if (prf_len > 2 * 16)
		return (prf_len + 1) / 2;

	return 16;
}


static int eap_eke_auth_len(u8 prf)
{
	switch (prf) {
	case EAP_EKE_PRF_HMAC_SHA1:
		return SHA1_MAC_LEN;
	case EAP_EKE_PRF_HMAC_SHA2_256:
		return SHA256_MAC_LEN;
	}

	return -1;
}


int eap_eke_dh_init(u8 group, u8 *ret_priv, u8 *ret_pub)
{
	int generator;
	u8 gen;
	const struct dh_group *dh;

	generator = eap_eke_dh_generator(group);
	dh = eap_eke_dh_group(group);
	if (generator < 0 || generator > 255 || !dh)
		return -1;
	gen = generator;

	if (crypto_dh_init(gen, dh->prime, dh->prime_len, ret_priv,
			   ret_pub) < 0)
		return -1;
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: DH private value",
			ret_priv, dh->prime_len);
	wpa_hexdump(MSG_DEBUG, "EAP-EKE: DH public value",
		    ret_pub, dh->prime_len);

	return 0;
}


static int eap_eke_prf(u8 prf, const u8 *key, size_t key_len, const u8 *data,
		       size_t data_len, const u8 *data2, size_t data2_len,
		       u8 *res)
{
	const u8 *addr[2];
	size_t len[2];
	size_t num_elem = 1;

	addr[0] = data;
	len[0] = data_len;
	if (data2) {
		num_elem++;
		addr[1] = data2;
		len[1] = data2_len;
	}

	if (prf == EAP_EKE_PRF_HMAC_SHA1)
		return hmac_sha1_vector(key, key_len, num_elem, addr, len, res);
	if (prf == EAP_EKE_PRF_HMAC_SHA2_256)
		return hmac_sha256_vector(key, key_len, num_elem, addr, len,
					  res);
	return -1;
}


static int eap_eke_prf_hmac_sha1(const u8 *key, size_t key_len, const u8 *data,
				 size_t data_len, u8 *res, size_t len)
{
	u8 hash[SHA1_MAC_LEN];
	u8 idx;
	const u8 *addr[3];
	size_t vlen[3];
	int ret;

	idx = 0;
	addr[0] = hash;
	vlen[0] = SHA1_MAC_LEN;
	addr[1] = data;
	vlen[1] = data_len;
	addr[2] = &idx;
	vlen[2] = 1;

	while (len > 0) {
		idx++;
		if (idx == 1)
			ret = hmac_sha1_vector(key, key_len, 2, &addr[1],
					       &vlen[1], hash);
		else
			ret = hmac_sha1_vector(key, key_len, 3, addr, vlen,
					       hash);
		if (ret < 0)
			return -1;
		if (len > SHA1_MAC_LEN) {
			os_memcpy(res, hash, SHA1_MAC_LEN);
			res += SHA1_MAC_LEN;
			len -= SHA1_MAC_LEN;
		} else {
			os_memcpy(res, hash, len);
			len = 0;
		}
	}

	return 0;
}


static int eap_eke_prf_hmac_sha256(const u8 *key, size_t key_len, const u8 *data,
				   size_t data_len, u8 *res, size_t len)
{
	u8 hash[SHA256_MAC_LEN];
	u8 idx;
	const u8 *addr[3];
	size_t vlen[3];
	int ret;

	idx = 0;
	addr[0] = hash;
	vlen[0] = SHA256_MAC_LEN;
	addr[1] = data;
	vlen[1] = data_len;
	addr[2] = &idx;
	vlen[2] = 1;

	while (len > 0) {
		idx++;
		if (idx == 1)
			ret = hmac_sha256_vector(key, key_len, 2, &addr[1],
						 &vlen[1], hash);
		else
			ret = hmac_sha256_vector(key, key_len, 3, addr, vlen,
						 hash);
		if (ret < 0)
			return -1;
		if (len > SHA256_MAC_LEN) {
			os_memcpy(res, hash, SHA256_MAC_LEN);
			res += SHA256_MAC_LEN;
			len -= SHA256_MAC_LEN;
		} else {
			os_memcpy(res, hash, len);
			len = 0;
		}
	}

	return 0;
}


static int eap_eke_prfplus(u8 prf, const u8 *key, size_t key_len,
			   const u8 *data, size_t data_len, u8 *res, size_t len)
{
	if (prf == EAP_EKE_PRF_HMAC_SHA1)
		return eap_eke_prf_hmac_sha1(key, key_len, data, data_len, res,
					     len);
	if (prf == EAP_EKE_PRF_HMAC_SHA2_256)
		return eap_eke_prf_hmac_sha256(key, key_len, data, data_len,
					       res, len);
	return -1;
}


int eap_eke_derive_key(struct eap_eke_session *sess,
		       const u8 *password, size_t password_len,
		       const u8 *id_s, size_t id_s_len, const u8 *id_p,
		       size_t id_p_len, u8 *key)
{
	u8 zeros[EAP_EKE_MAX_HASH_LEN];
	u8 temp[EAP_EKE_MAX_HASH_LEN];
	size_t key_len = 16; /* Only AES-128-CBC is used here */
	u8 *id;

	/* temp = prf(0+, password) */
	os_memset(zeros, 0, sess->prf_len);
	if (eap_eke_prf(sess->prf, zeros, sess->prf_len,
			password, password_len, NULL, 0, temp) < 0)
		return -1;
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: temp = prf(0+, password)",
			temp, sess->prf_len);

	/* key = prf+(temp, ID_S | ID_P) */
	id = os_malloc(id_s_len + id_p_len);
	if (id == NULL)
		return -1;
	os_memcpy(id, id_s, id_s_len);
	os_memcpy(id + id_s_len, id_p, id_p_len);
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-EKE: ID_S | ID_P",
			  id, id_s_len + id_p_len);
	if (eap_eke_prfplus(sess->prf, temp, sess->prf_len,
			    id, id_s_len + id_p_len, key, key_len) < 0) {
		os_free(id);
		return -1;
	}
	os_free(id);
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: key = prf+(temp, ID_S | ID_P)",
			key, key_len);

	return 0;
}


int eap_eke_dhcomp(struct eap_eke_session *sess, const u8 *key, const u8 *dhpub,
		   u8 *ret_dhcomp)
{
	u8 pub[EAP_EKE_MAX_DH_LEN];
	int dh_len;
	u8 iv[AES_BLOCK_SIZE];

	dh_len = eap_eke_dh_len(sess->dhgroup);
	if (dh_len < 0)
		return -1;

	/*
	 * DHComponent = Encr(key, y)
	 *
	 * All defined DH groups use primes that have length devisible by 16, so
	 * no need to do extra padding for y (= pub).
	 */
	if (sess->encr != EAP_EKE_ENCR_AES128_CBC)
		return -1;
	if (random_get_bytes(iv, AES_BLOCK_SIZE))
		return -1;
	wpa_hexdump(MSG_DEBUG, "EAP-EKE: IV for Encr(key, y)",
		    iv, AES_BLOCK_SIZE);
	os_memcpy(pub, dhpub, dh_len);
	if (aes_128_cbc_encrypt(key, iv, pub, dh_len) < 0)
		return -1;
	os_memcpy(ret_dhcomp, iv, AES_BLOCK_SIZE);
	os_memcpy(ret_dhcomp + AES_BLOCK_SIZE, pub, dh_len);
	wpa_hexdump(MSG_DEBUG, "EAP-EKE: DHComponent = Encr(key, y)",
		    ret_dhcomp, AES_BLOCK_SIZE + dh_len);

	return 0;
}


int eap_eke_shared_secret(struct eap_eke_session *sess, const u8 *key,
			  const u8 *dhpriv, const u8 *peer_dhcomp)
{
	u8 zeros[EAP_EKE_MAX_HASH_LEN];
	u8 peer_pub[EAP_EKE_MAX_DH_LEN];
	u8 modexp[EAP_EKE_MAX_DH_LEN];
	size_t len;
	const struct dh_group *dh;

	dh = eap_eke_dh_group(sess->dhgroup);
	if (sess->encr != EAP_EKE_ENCR_AES128_CBC || !dh)
		return -1;

	/* Decrypt peer DHComponent */
	os_memcpy(peer_pub, peer_dhcomp + AES_BLOCK_SIZE, dh->prime_len);
	if (aes_128_cbc_decrypt(key, peer_dhcomp, peer_pub, dh->prime_len) < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to decrypt DHComponent");
		return -1;
	}
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: Decrypted peer DH pubkey",
			peer_pub, dh->prime_len);

	/* SharedSecret = prf(0+, g ^ (x_s * x_p) (mod p)) */
	len = dh->prime_len;
	if (crypto_dh_derive_secret(*dh->generator, dh->prime, dh->prime_len,
				    dhpriv, dh->prime_len, peer_pub,
				    dh->prime_len, modexp, &len) < 0)
		return -1;
	if (len < dh->prime_len) {
		size_t pad = dh->prime_len - len;
		os_memmove(modexp + pad, modexp, len);
		os_memset(modexp, 0, pad);
	}

	os_memset(zeros, 0, sess->auth_len);
	if (eap_eke_prf(sess->prf, zeros, sess->auth_len, modexp, dh->prime_len,
			NULL, 0, sess->shared_secret) < 0)
		return -1;
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: SharedSecret",
			sess->shared_secret, sess->auth_len);

	return 0;
}


int eap_eke_derive_ke_ki(struct eap_eke_session *sess,
			 const u8 *id_s, size_t id_s_len,
			 const u8 *id_p, size_t id_p_len)
{
	u8 buf[EAP_EKE_MAX_KE_LEN + EAP_EKE_MAX_KI_LEN];
	size_t ke_len, ki_len;
	u8 *data;
	size_t data_len;
	const char *label = "EAP-EKE Keys";
	size_t label_len;

	/*
	 * Ke | Ki = prf+(SharedSecret, "EAP-EKE Keys" | ID_S | ID_P)
	 * Ke = encryption key
	 * Ki = integrity protection key
	 * Length of each key depends on the selected algorithms.
	 */

	if (sess->encr == EAP_EKE_ENCR_AES128_CBC)
		ke_len = 16;
	else
		return -1;

	if (sess->mac == EAP_EKE_PRF_HMAC_SHA1)
		ki_len = 20;
	else if (sess->mac == EAP_EKE_PRF_HMAC_SHA2_256)
		ki_len = 32;
	else
		return -1;

	label_len = os_strlen(label);
	data_len = label_len + id_s_len + id_p_len;
	data = os_malloc(data_len);
	if (data == NULL)
		return -1;
	os_memcpy(data, label, label_len);
	os_memcpy(data + label_len, id_s, id_s_len);
	os_memcpy(data + label_len + id_s_len, id_p, id_p_len);
	if (eap_eke_prfplus(sess->prf, sess->shared_secret, sess->prf_len,
			    data, data_len, buf, ke_len + ki_len) < 0) {
		os_free(data);
		return -1;
	}

	os_memcpy(sess->ke, buf, ke_len);
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: Ke", sess->ke, ke_len);
	os_memcpy(sess->ki, buf + ke_len, ki_len);
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: Ki", sess->ki, ki_len);

	os_free(data);
	return 0;
}


int eap_eke_derive_ka(struct eap_eke_session *sess,
		      const u8 *id_s, size_t id_s_len,
		      const u8 *id_p, size_t id_p_len,
		      const u8 *nonce_p, const u8 *nonce_s)
{
	u8 *data, *pos;
	size_t data_len;
	const char *label = "EAP-EKE Ka";
	size_t label_len;

	/*
	 * Ka = prf+(SharedSecret, "EAP-EKE Ka" | ID_S | ID_P | Nonce_P |
	 *	     Nonce_S)
	 * Ka = authentication key
	 * Length of the key depends on the selected algorithms.
	 */

	label_len = os_strlen(label);
	data_len = label_len + id_s_len + id_p_len + 2 * sess->nonce_len;
	data = os_malloc(data_len);
	if (data == NULL)
		return -1;
	pos = data;
	os_memcpy(pos, label, label_len);
	pos += label_len;
	os_memcpy(pos, id_s, id_s_len);
	pos += id_s_len;
	os_memcpy(pos, id_p, id_p_len);
	pos += id_p_len;
	os_memcpy(pos, nonce_p, sess->nonce_len);
	pos += sess->nonce_len;
	os_memcpy(pos, nonce_s, sess->nonce_len);
	if (eap_eke_prfplus(sess->prf, sess->shared_secret, sess->prf_len,
			    data, data_len, sess->ka, sess->prf_len) < 0) {
		os_free(data);
		return -1;
	}
	os_free(data);

	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: Ka", sess->ka, sess->prf_len);

	return 0;
}


int eap_eke_derive_msk(struct eap_eke_session *sess,
		       const u8 *id_s, size_t id_s_len,
		       const u8 *id_p, size_t id_p_len,
		       const u8 *nonce_p, const u8 *nonce_s,
		       u8 *msk, u8 *emsk)
{
	u8 *data, *pos;
	size_t data_len;
	const char *label = "EAP-EKE Exported Keys";
	size_t label_len;
	u8 buf[EAP_MSK_LEN + EAP_EMSK_LEN];

	/*
	 * MSK | EMSK = prf+(SharedSecret, "EAP-EKE Exported Keys" | ID_S |
	 *		     ID_P | Nonce_P | Nonce_S)
	 */

	label_len = os_strlen(label);
	data_len = label_len + id_s_len + id_p_len + 2 * sess->nonce_len;
	data = os_malloc(data_len);
	if (data == NULL)
		return -1;
	pos = data;
	os_memcpy(pos, label, label_len);
	pos += label_len;
	os_memcpy(pos, id_s, id_s_len);
	pos += id_s_len;
	os_memcpy(pos, id_p, id_p_len);
	pos += id_p_len;
	os_memcpy(pos, nonce_p, sess->nonce_len);
	pos += sess->nonce_len;
	os_memcpy(pos, nonce_s, sess->nonce_len);
	if (eap_eke_prfplus(sess->prf, sess->shared_secret, sess->prf_len,
			    data, data_len, buf, EAP_MSK_LEN + EAP_EMSK_LEN) <
	    0) {
		os_free(data);
		return -1;
	}
	os_free(data);

	os_memcpy(msk, buf, EAP_MSK_LEN);
	os_memcpy(emsk, buf + EAP_MSK_LEN, EAP_EMSK_LEN);
	os_memset(buf, 0, sizeof(buf));

	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: MSK", msk, EAP_MSK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: EMSK", msk, EAP_EMSK_LEN);

	return 0;
}


static int eap_eke_mac(u8 mac, const u8 *key, const u8 *data, size_t data_len,
		       u8 *res)
{
	if (mac == EAP_EKE_MAC_HMAC_SHA1)
		return hmac_sha1(key, SHA1_MAC_LEN, data, data_len, res);
	if (mac == EAP_EKE_MAC_HMAC_SHA2_256)
		return hmac_sha256(key, SHA256_MAC_LEN, data, data_len, res);
	return -1;
}


int eap_eke_prot(struct eap_eke_session *sess,
		 const u8 *data, size_t data_len,
		 u8 *prot, size_t *prot_len)
{
	size_t block_size, icv_len, pad;
	u8 *pos, *iv, *e;

	if (sess->encr == EAP_EKE_ENCR_AES128_CBC)
		block_size = AES_BLOCK_SIZE;
	else
		return -1;

	if (sess->mac == EAP_EKE_PRF_HMAC_SHA1)
		icv_len = SHA1_MAC_LEN;
	else if (sess->mac == EAP_EKE_PRF_HMAC_SHA2_256)
		icv_len = SHA256_MAC_LEN;
	else
		return -1;

	pad = data_len % block_size;
	if (pad)
		pad = block_size - pad;

	if (*prot_len < block_size + data_len + pad + icv_len) {
		wpa_printf(MSG_INFO, "EAP-EKE: Not enough room for Prot() data");
		return -1;
	}
	pos = prot;

	if (random_get_bytes(pos, block_size))
		return -1;
	iv = pos;
	wpa_hexdump(MSG_DEBUG, "EAP-EKE: IV for Prot()", iv, block_size);
	pos += block_size;

	e = pos;
	os_memcpy(pos, data, data_len);
	pos += data_len;
	if (pad) {
		if (random_get_bytes(pos, pad))
			return -1;
		pos += pad;
	}

	if (aes_128_cbc_encrypt(sess->ke, iv, e, data_len + pad) < 0 ||
	    eap_eke_mac(sess->mac, sess->ki, e, data_len + pad, pos) < 0)
		return -1;
	pos += icv_len;

	*prot_len = pos - prot;
	return 0;
}


int eap_eke_decrypt_prot(struct eap_eke_session *sess,
			 const u8 *prot, size_t prot_len,
			 u8 *data, size_t *data_len)
{
	size_t block_size, icv_len;
	u8 icv[EAP_EKE_MAX_HASH_LEN];

	if (sess->encr == EAP_EKE_ENCR_AES128_CBC)
		block_size = AES_BLOCK_SIZE;
	else
		return -1;

	if (sess->mac == EAP_EKE_PRF_HMAC_SHA1)
		icv_len = SHA1_MAC_LEN;
	else if (sess->mac == EAP_EKE_PRF_HMAC_SHA2_256)
		icv_len = SHA256_MAC_LEN;
	else
		return -1;

	if (prot_len < 2 * block_size + icv_len ||
	    (prot_len - icv_len) % block_size)
		return -1;

	if (eap_eke_mac(sess->mac, sess->ki, prot + block_size,
			prot_len - block_size - icv_len, icv) < 0)
		return -1;
	if (os_memcmp_const(icv, prot + prot_len - icv_len, icv_len) != 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: ICV mismatch in Prot() data");
		return -1;
	}

	if (*data_len < prot_len - block_size - icv_len) {
		wpa_printf(MSG_INFO, "EAP-EKE: Not enough room for decrypted Prot() data");
		return -1;
	}

	*data_len = prot_len - block_size - icv_len;
	os_memcpy(data, prot + block_size, *data_len);
	if (aes_128_cbc_decrypt(sess->ke, prot, data, *data_len) < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to decrypt Prot() data");
		return -1;
	}
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: Decrypted Prot() data",
			data, *data_len);

	return 0;
}


int eap_eke_auth(struct eap_eke_session *sess, const char *label,
		 const struct wpabuf *msgs, u8 *auth)
{
	wpa_printf(MSG_DEBUG, "EAP-EKE: Auth(%s)", label);
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: Ka for Auth",
			sess->ka, sess->auth_len);
	wpa_hexdump_buf(MSG_MSGDUMP, "EAP-EKE: Messages for Auth", msgs);
	return eap_eke_prf(sess->prf, sess->ka, sess->auth_len,
			   (const u8 *) label, os_strlen(label),
			   wpabuf_head(msgs), wpabuf_len(msgs), auth);
}


int eap_eke_session_init(struct eap_eke_session *sess, u8 dhgroup, u8 encr,
			 u8 prf, u8 mac)
{
	sess->dhgroup = dhgroup;
	sess->encr = encr;
	sess->prf = prf;
	sess->mac = mac;

	sess->prf_len = eap_eke_prf_len(prf);
	sess->nonce_len = eap_eke_nonce_len(prf);
	sess->auth_len = eap_eke_auth_len(prf);
	sess->dhcomp_len = eap_eke_dhcomp_len(sess->dhgroup, sess->encr);
	sess->pnonce_len = eap_eke_pnonce_len(sess->mac);
	sess->pnonce_ps_len = eap_eke_pnonce_ps_len(sess->mac);
	if (sess->prf_len < 0 || sess->nonce_len < 0 || sess->auth_len < 0 ||
	    sess->dhcomp_len < 0 || sess->pnonce_len < 0 ||
	    sess->pnonce_ps_len < 0)
		return -1;

	return 0;
}


void eap_eke_session_clean(struct eap_eke_session *sess)
{
	os_memset(sess->shared_secret, 0, EAP_EKE_MAX_HASH_LEN);
	os_memset(sess->ke, 0, EAP_EKE_MAX_KE_LEN);
	os_memset(sess->ki, 0, EAP_EKE_MAX_KI_LEN);
	os_memset(sess->ka, 0, EAP_EKE_MAX_KA_LEN);
}
