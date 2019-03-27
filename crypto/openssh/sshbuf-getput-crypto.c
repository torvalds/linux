/*	$OpenBSD: sshbuf-getput-crypto.c,v 1.5 2016/01/12 23:42:54 djm Exp $	*/
/*
 * Copyright (c) 2011 Damien Miller
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define SSHBUF_INTERNAL
#include "includes.h"

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <openssl/bn.h>
#ifdef OPENSSL_HAS_ECC
# include <openssl/ec.h>
#endif /* OPENSSL_HAS_ECC */

#include "ssherr.h"
#include "sshbuf.h"

int
sshbuf_get_bignum2(struct sshbuf *buf, BIGNUM *v)
{
	const u_char *d;
	size_t len;
	int r;

	if ((r = sshbuf_get_bignum2_bytes_direct(buf, &d, &len)) != 0)
		return r;
	if (v != NULL && BN_bin2bn(d, len, v) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	return 0;
}

int
sshbuf_get_bignum1(struct sshbuf *buf, BIGNUM *v)
{
	const u_char *d = sshbuf_ptr(buf);
	u_int16_t len_bits;
	size_t len_bytes;

	/* Length in bits */
	if (sshbuf_len(buf) < 2)
		return SSH_ERR_MESSAGE_INCOMPLETE;
	len_bits = PEEK_U16(d);
	len_bytes = (len_bits + 7) >> 3;
	if (len_bytes > SSHBUF_MAX_BIGNUM)
		return SSH_ERR_BIGNUM_TOO_LARGE;
	if (sshbuf_len(buf) < 2 + len_bytes)
		return SSH_ERR_MESSAGE_INCOMPLETE;
	if (v != NULL && BN_bin2bn(d + 2, len_bytes, v) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	if (sshbuf_consume(buf, 2 + len_bytes) != 0) {
		SSHBUF_DBG(("SSH_ERR_INTERNAL_ERROR"));
		SSHBUF_ABORT();
		return SSH_ERR_INTERNAL_ERROR;
	}
	return 0;
}

#ifdef OPENSSL_HAS_ECC
static int
get_ec(const u_char *d, size_t len, EC_POINT *v, const EC_GROUP *g)
{
	/* Refuse overlong bignums */
	if (len == 0 || len > SSHBUF_MAX_ECPOINT)
		return SSH_ERR_ECPOINT_TOO_LARGE;
	/* Only handle uncompressed points */
	if (*d != POINT_CONVERSION_UNCOMPRESSED)
		return SSH_ERR_INVALID_FORMAT;
	if (v != NULL && EC_POINT_oct2point(g, v, d, len, NULL) != 1)
		return SSH_ERR_INVALID_FORMAT; /* XXX assumption */
	return 0;
}

int
sshbuf_get_ec(struct sshbuf *buf, EC_POINT *v, const EC_GROUP *g)
{
	const u_char *d;
	size_t len;
	int r;

	if ((r = sshbuf_peek_string_direct(buf, &d, &len)) < 0)
		return r;
	if ((r = get_ec(d, len, v, g)) != 0)
		return r;
	/* Skip string */
	if (sshbuf_get_string_direct(buf, NULL, NULL) != 0) {
		/* Shouldn't happen */
		SSHBUF_DBG(("SSH_ERR_INTERNAL_ERROR"));
		SSHBUF_ABORT();
		return SSH_ERR_INTERNAL_ERROR;
	}
	return 0;
}

int
sshbuf_get_eckey(struct sshbuf *buf, EC_KEY *v)
{
	EC_POINT *pt = EC_POINT_new(EC_KEY_get0_group(v));
	int r;
	const u_char *d;
	size_t len;

	if (pt == NULL) {
		SSHBUF_DBG(("SSH_ERR_ALLOC_FAIL"));
		return SSH_ERR_ALLOC_FAIL;
	}
	if ((r = sshbuf_peek_string_direct(buf, &d, &len)) < 0) {
		EC_POINT_free(pt);
		return r;
	}
	if ((r = get_ec(d, len, pt, EC_KEY_get0_group(v))) != 0) {
		EC_POINT_free(pt);
		return r;
	}
	if (EC_KEY_set_public_key(v, pt) != 1) {
		EC_POINT_free(pt);
		return SSH_ERR_ALLOC_FAIL; /* XXX assumption */
	}
	EC_POINT_free(pt);
	/* Skip string */
	if (sshbuf_get_string_direct(buf, NULL, NULL) != 0) {
		/* Shouldn't happen */
		SSHBUF_DBG(("SSH_ERR_INTERNAL_ERROR"));
		SSHBUF_ABORT();
		return SSH_ERR_INTERNAL_ERROR;
	}
	return 0;	
}
#endif /* OPENSSL_HAS_ECC */

int
sshbuf_put_bignum2(struct sshbuf *buf, const BIGNUM *v)
{
	u_char d[SSHBUF_MAX_BIGNUM + 1];
	int len = BN_num_bytes(v), prepend = 0, r;

	if (len < 0 || len > SSHBUF_MAX_BIGNUM)
		return SSH_ERR_INVALID_ARGUMENT;
	*d = '\0';
	if (BN_bn2bin(v, d + 1) != len)
		return SSH_ERR_INTERNAL_ERROR; /* Shouldn't happen */
	/* If MSB is set, prepend a \0 */
	if (len > 0 && (d[1] & 0x80) != 0)
		prepend = 1;
	if ((r = sshbuf_put_string(buf, d + 1 - prepend, len + prepend)) < 0) {
		explicit_bzero(d, sizeof(d));
		return r;
	}
	explicit_bzero(d, sizeof(d));
	return 0;
}

int
sshbuf_put_bignum1(struct sshbuf *buf, const BIGNUM *v)
{
	int r, len_bits = BN_num_bits(v);
	size_t len_bytes = (len_bits + 7) / 8;
	u_char d[SSHBUF_MAX_BIGNUM], *dp;

	if (len_bits < 0 || len_bytes > SSHBUF_MAX_BIGNUM)
		return SSH_ERR_INVALID_ARGUMENT;
	if (BN_bn2bin(v, d) != (int)len_bytes)
		return SSH_ERR_INTERNAL_ERROR; /* Shouldn't happen */
	if ((r = sshbuf_reserve(buf, len_bytes + 2, &dp)) < 0) {
		explicit_bzero(d, sizeof(d));
		return r;
	}
	POKE_U16(dp, len_bits);
	if (len_bytes != 0)
		memcpy(dp + 2, d, len_bytes);
	explicit_bzero(d, sizeof(d));
	return 0;
}

#ifdef OPENSSL_HAS_ECC
int
sshbuf_put_ec(struct sshbuf *buf, const EC_POINT *v, const EC_GROUP *g)
{
	u_char d[SSHBUF_MAX_ECPOINT];
	BN_CTX *bn_ctx;
	size_t len;
	int ret;

	if ((bn_ctx = BN_CTX_new()) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	if ((len = EC_POINT_point2oct(g, v, POINT_CONVERSION_UNCOMPRESSED,
	    NULL, 0, bn_ctx)) > SSHBUF_MAX_ECPOINT) {
		BN_CTX_free(bn_ctx);
		return SSH_ERR_INVALID_ARGUMENT;
	}
	if (EC_POINT_point2oct(g, v, POINT_CONVERSION_UNCOMPRESSED,
	    d, len, bn_ctx) != len) {
		BN_CTX_free(bn_ctx);
		return SSH_ERR_INTERNAL_ERROR; /* Shouldn't happen */
	}
	BN_CTX_free(bn_ctx);
	ret = sshbuf_put_string(buf, d, len);
	explicit_bzero(d, len);
	return ret;
}

int
sshbuf_put_eckey(struct sshbuf *buf, const EC_KEY *v)
{
	return sshbuf_put_ec(buf, EC_KEY_get0_public_key(v),
	    EC_KEY_get0_group(v));
}
#endif /* OPENSSL_HAS_ECC */

