/*-
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Shteryana Sotirova Shopova under
 * sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#elif defined(HAVE_INTTYPES_H)
#include <inttypes.h>
#endif
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>

#ifdef HAVE_LIBCRYPTO
#include <openssl/evp.h>
#endif

#include "asn1.h"
#include "snmp.h"
#include "snmppriv.h"

#define	SNMP_PRIV_AES_IV_SIZ		16
#define	SNMP_EXTENDED_KEY_SIZ		64
#define	SNMP_AUTH_KEY_LOOPCNT		1048576
#define	SNMP_AUTH_BUF_SIZE		72

static const uint8_t ipad = 0x36;
static const uint8_t opad = 0x5c;

#ifdef HAVE_LIBCRYPTO

static int32_t
snmp_digest_init(const struct snmp_user *user, EVP_MD_CTX *ctx,
    const EVP_MD **dtype, uint32_t *keylen)
{
	if (user->auth_proto == SNMP_AUTH_HMAC_MD5) {
		*dtype = EVP_md5();
		*keylen = SNMP_AUTH_HMACMD5_KEY_SIZ;
	} else if (user->auth_proto == SNMP_AUTH_HMAC_SHA) {
		*dtype = EVP_sha1();
		*keylen = SNMP_AUTH_HMACSHA_KEY_SIZ;
	} else if (user->auth_proto == SNMP_AUTH_NOAUTH)
		return (0);
	else {
		snmp_error("unknown authentication option - %d",
		    user->auth_proto);
		return (-1);
	}

	if (EVP_DigestInit(ctx, *dtype) != 1)
		return (-1);

	return (1);
}

enum snmp_code
snmp_pdu_calc_digest(const struct snmp_pdu *pdu, uint8_t *digest)
{
	uint8_t md[EVP_MAX_MD_SIZE], extkey[SNMP_EXTENDED_KEY_SIZ];
	uint8_t key1[SNMP_EXTENDED_KEY_SIZ], key2[SNMP_EXTENDED_KEY_SIZ];
	uint32_t i, keylen, olen;
	int32_t err;
	const EVP_MD *dtype;
	EVP_MD_CTX *ctx;

	ctx = EVP_MD_CTX_new();
	if (ctx == NULL)
		return (SNMP_CODE_FAILED);
	err = snmp_digest_init(&pdu->user, ctx, &dtype, &keylen);
	if (err <= 0)
		EVP_MD_CTX_free(ctx);
	if (err < 0)
		return (SNMP_CODE_BADDIGEST);
	else if (err == 0)
		return (SNMP_CODE_OK);

	memset(pdu->digest_ptr, 0, sizeof(pdu->msg_digest));
	memcpy(extkey, pdu->user.auth_key, keylen);
	memset(extkey + keylen, 0, sizeof(extkey) - keylen);

	for (i = 0; i < SNMP_EXTENDED_KEY_SIZ; i++) {
		key1[i] = extkey[i] ^ ipad;
		key2[i] = extkey[i] ^ opad;
	}

	if (EVP_DigestUpdate(ctx, key1, SNMP_EXTENDED_KEY_SIZ) != 1 ||
	    EVP_DigestUpdate(ctx, pdu->outer_ptr, pdu->outer_len) != 1 ||
	    EVP_DigestFinal(ctx, md, &olen) != 1)
		goto failed;

	if (EVP_DigestInit(ctx, dtype) != 1 ||
	    EVP_DigestUpdate(ctx, key2, SNMP_EXTENDED_KEY_SIZ) != 1 ||
	    EVP_DigestUpdate(ctx, md, olen) != 1 ||
	    EVP_DigestFinal(ctx, md, &olen) != 1)
		goto failed;

	if (olen < SNMP_USM_AUTH_SIZE) {
		snmp_error("bad digest size - %d", olen);
		EVP_MD_CTX_free(ctx);
		return (SNMP_CODE_BADDIGEST);
	}

	memcpy(digest, md, SNMP_USM_AUTH_SIZE);
	EVP_MD_CTX_free(ctx);
	return (SNMP_CODE_OK);

failed:
	EVP_MD_CTX_free(ctx);
	return (SNMP_CODE_BADDIGEST);
}

static int32_t
snmp_pdu_cipher_init(const struct snmp_pdu *pdu, int32_t len,
    const EVP_CIPHER **ctype, uint8_t *piv)
{
	int i;
	uint32_t netint;

	if (pdu->user.priv_proto == SNMP_PRIV_DES) {
		if (len  % 8 != 0)
			return (-1);
		*ctype = EVP_des_cbc();
		memcpy(piv, pdu->msg_salt, sizeof(pdu->msg_salt));
		for (i = 0; i < 8; i++)
			piv[i] = piv[i] ^ pdu->user.priv_key[8 + i];
	} else if (pdu->user.priv_proto == SNMP_PRIV_AES) {
		*ctype = EVP_aes_128_cfb128();
		netint = htonl(pdu->engine.engine_boots);
		memcpy(piv, &netint, sizeof(netint));
		piv += sizeof(netint);
		netint = htonl(pdu->engine.engine_time);
		memcpy(piv, &netint, sizeof(netint));
		piv += sizeof(netint);
		memcpy(piv, pdu->msg_salt, sizeof(pdu->msg_salt));
	} else if (pdu->user.priv_proto == SNMP_PRIV_NOPRIV)
		return (0);
	else {
		snmp_error("unknown privacy option - %d", pdu->user.priv_proto);
		return (-1);
	}

	return (1);
}

enum snmp_code
snmp_pdu_encrypt(const struct snmp_pdu *pdu)
{
	int32_t err, olen;
	uint8_t iv[SNMP_PRIV_AES_IV_SIZ];
	const EVP_CIPHER *ctype;
	EVP_CIPHER_CTX *ctx;

	err = snmp_pdu_cipher_init(pdu, pdu->scoped_len, &ctype, iv);
	if (err < 0)
		return (SNMP_CODE_EDECRYPT);
	else if (err == 0)
		return (SNMP_CODE_OK);

	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		return (SNMP_CODE_FAILED);
	if (EVP_EncryptInit(ctx, ctype, pdu->user.priv_key, iv) != 1)
		goto failed;

	if (EVP_EncryptUpdate(ctx, pdu->scoped_ptr, &olen, pdu->scoped_ptr,
	    pdu->scoped_len) != 1 ||
	    EVP_EncryptFinal(ctx, pdu->scoped_ptr + olen, &olen) != 1)
		goto failed;

	EVP_CIPHER_CTX_free(ctx);
	return (SNMP_CODE_OK);

failed:
	EVP_CIPHER_CTX_free(ctx);
	return (SNMP_CODE_FAILED);
}

enum snmp_code
snmp_pdu_decrypt(const struct snmp_pdu *pdu)
{
	int32_t err, olen;
	uint8_t iv[SNMP_PRIV_AES_IV_SIZ];
	const EVP_CIPHER *ctype;
	EVP_CIPHER_CTX *ctx;

	err = snmp_pdu_cipher_init(pdu, pdu->scoped_len, &ctype, iv);
	if (err < 0)
		return (SNMP_CODE_EDECRYPT);
	else if (err == 0)
		return (SNMP_CODE_OK);

	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		return (SNMP_CODE_FAILED);
	if (EVP_DecryptInit(ctx, ctype, pdu->user.priv_key, iv) != 1 ||
	    EVP_CIPHER_CTX_set_padding(ctx, 0) != 1)
		goto failed;

	if (EVP_DecryptUpdate(ctx, pdu->scoped_ptr, &olen, pdu->scoped_ptr,
	    pdu->scoped_len) != 1 ||
	    EVP_DecryptFinal(ctx, pdu->scoped_ptr + olen, &olen) != 1)
		goto failed;

	EVP_CIPHER_CTX_free(ctx);
	return (SNMP_CODE_OK);

failed:
	EVP_CIPHER_CTX_free(ctx);
	return (SNMP_CODE_EDECRYPT);
}

/* [RFC 3414] - A.2. Password to Key Algorithm */
enum snmp_code
snmp_passwd_to_keys(struct snmp_user *user, char *passwd)
{
	int err, loop, i, pwdlen;
	uint32_t  keylen, olen;
	const EVP_MD *dtype;
	EVP_MD_CTX *ctx;
	uint8_t authbuf[SNMP_AUTH_BUF_SIZE];

	if (passwd == NULL || user == NULL)
		return (SNMP_CODE_FAILED);

	ctx = EVP_MD_CTX_new();
	if (ctx == NULL)
		return (SNMP_CODE_FAILED);

	err = snmp_digest_init(user, ctx, &dtype, &keylen);
	if (err <= 0)
		EVP_MD_CTX_free(ctx);
	if (err < 0)
		return (SNMP_CODE_BADDIGEST);
	else if (err == 0)
		return (SNMP_CODE_OK);

	memset(user->auth_key, 0, sizeof(user->auth_key));
	pwdlen = strlen(passwd);

	for (loop = 0; loop < SNMP_AUTH_KEY_LOOPCNT; loop += i) {
		for (i = 0; i < SNMP_EXTENDED_KEY_SIZ; i++)
			authbuf[i] = passwd[(loop + i) % pwdlen];
		if (EVP_DigestUpdate(ctx, authbuf, SNMP_EXTENDED_KEY_SIZ) != 1)
			goto failed;
	}

	if (EVP_DigestFinal(ctx, user->auth_key, &olen) != 1)
		goto failed;

	EVP_MD_CTX_free(ctx);
	return (SNMP_CODE_OK);

failed:
	EVP_MD_CTX_free(ctx);
	return (SNMP_CODE_BADDIGEST);
}

/* [RFC 3414] - 2.6. Key Localization Algorithm */
enum snmp_code
snmp_get_local_keys(struct snmp_user *user, uint8_t *eid, uint32_t elen)
{
	int err;
	uint32_t  keylen, olen;
	const EVP_MD *dtype;
	EVP_MD_CTX *ctx;
	uint8_t authbuf[SNMP_AUTH_BUF_SIZE];

	if (user == NULL || eid == NULL || elen > SNMP_ENGINE_ID_SIZ)
		return (SNMP_CODE_FAILED);

	ctx = EVP_MD_CTX_new();
	if (ctx == NULL)
		return (SNMP_CODE_FAILED);

	memset(user->priv_key, 0, sizeof(user->priv_key));
	memset(authbuf, 0, sizeof(authbuf));

	err = snmp_digest_init(user, ctx, &dtype, &keylen);
	if (err <= 0)
		EVP_MD_CTX_free(ctx);
	if (err < 0)
		return (SNMP_CODE_BADDIGEST);
	else if (err == 0)
		return (SNMP_CODE_OK);

	memcpy(authbuf, user->auth_key, keylen);
	memcpy(authbuf + keylen, eid, elen);
	memcpy(authbuf + keylen + elen, user->auth_key, keylen);

	if (EVP_DigestUpdate(ctx, authbuf, 2 * keylen + elen) != 1 ||
	    EVP_DigestFinal(ctx, user->auth_key, &olen) != 1) {
		EVP_MD_CTX_free(ctx);
		return (SNMP_CODE_BADDIGEST);
	}
	EVP_MD_CTX_free(ctx);

	if (user->priv_proto != SNMP_PRIV_NOPRIV)
		memcpy(user->priv_key, user->auth_key, sizeof(user->priv_key));

	return (SNMP_CODE_OK);
}

enum snmp_code
snmp_calc_keychange(struct snmp_user *user, uint8_t *keychange)
{
	int32_t err, rvalue[SNMP_AUTH_HMACSHA_KEY_SIZ / 4];
	uint32_t i, keylen, olen;
	const EVP_MD *dtype;
	EVP_MD_CTX *ctx;

	ctx = EVP_MD_CTX_new();
	if (ctx == NULL)
		return (SNMP_CODE_FAILED);

	err = snmp_digest_init(user, ctx, &dtype, &keylen);
	if (err <= 0)
		EVP_MD_CTX_free(ctx);
	if (err < 0)
		return (SNMP_CODE_BADDIGEST);
	else if (err == 0)
		return (SNMP_CODE_OK);

	for (i = 0; i < keylen / 4; i++)
		rvalue[i] = random();

	memcpy(keychange, user->auth_key, keylen);
	memcpy(keychange + keylen, rvalue, keylen);

	if (EVP_DigestUpdate(ctx, keychange, 2 * keylen) != 1 ||
	    EVP_DigestFinal(ctx, keychange, &olen) != 1) {
		EVP_MD_CTX_free(ctx);
		return (SNMP_CODE_BADDIGEST);
	}

	EVP_MD_CTX_free(ctx);
	return (SNMP_CODE_OK);
}

#else /* !HAVE_LIBCRYPTO */

enum snmp_code
snmp_pdu_calc_digest(const struct snmp_pdu *pdu, uint8_t *digest __unused)
{
	if  (pdu->user.auth_proto != SNMP_AUTH_NOAUTH)
		return (SNMP_CODE_BADSECLEVEL);


	return (SNMP_CODE_OK);
}

enum snmp_code
snmp_pdu_encrypt(const struct snmp_pdu *pdu)
{
	if (pdu->user.priv_proto != SNMP_PRIV_NOPRIV)
		return (SNMP_CODE_BADSECLEVEL);

	return (SNMP_CODE_OK);
}

enum snmp_code
snmp_pdu_decrypt(const struct snmp_pdu *pdu)
{
	if (pdu->user.priv_proto != SNMP_PRIV_NOPRIV)
		return (SNMP_CODE_BADSECLEVEL);

	return (SNMP_CODE_OK);
}

enum snmp_code
snmp_passwd_to_keys(struct snmp_user *user, char *passwd __unused)
{
	if (user->auth_proto == SNMP_AUTH_NOAUTH &&
	    user->priv_proto == SNMP_PRIV_NOPRIV)
		return (SNMP_CODE_OK);

	errno = EPROTONOSUPPORT;

	return (SNMP_CODE_FAILED);
}

enum snmp_code
snmp_get_local_keys(struct snmp_user *user, uint8_t *eid __unused,
    uint32_t elen __unused)
{
	if (user->auth_proto == SNMP_AUTH_NOAUTH &&
	    user->priv_proto == SNMP_PRIV_NOPRIV)
		return (SNMP_CODE_OK);

	errno = EPROTONOSUPPORT;

	return (SNMP_CODE_FAILED);
}

enum snmp_code
snmp_calc_keychange(struct snmp_user *user __unused,
    uint8_t *keychange __unused)
{
	errno = EPROTONOSUPPORT;
	return (SNMP_CODE_FAILED);
}

#endif /* HAVE_LIBCRYPTO */
