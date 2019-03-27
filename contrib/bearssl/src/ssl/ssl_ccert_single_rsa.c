/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "inner.h"

static void
cc_none0(const br_ssl_client_certificate_class **pctx)
{
	(void)pctx;
}

static void
cc_none1(const br_ssl_client_certificate_class **pctx, size_t len)
{
	(void)pctx;
	(void)len;
}

static void
cc_none2(const br_ssl_client_certificate_class **pctx,
	const unsigned char *data, size_t len)
{
	(void)pctx;
	(void)data;
	(void)len;
}

static void
cc_choose(const br_ssl_client_certificate_class **pctx,
	const br_ssl_client_context *cc, uint32_t auth_types,
	br_ssl_client_certificate *choices)
{
	br_ssl_client_certificate_rsa_context *zc;
	int x;

	(void)cc;
	zc = (br_ssl_client_certificate_rsa_context *)pctx;
	x = br_ssl_choose_hash((unsigned)auth_types);
	if (x == 0 && (auth_types & 1) == 0) {
		memset(choices, 0, sizeof *choices);
	}
	choices->auth_type = BR_AUTH_RSA;
	choices->hash_id = x;
	choices->chain = zc->chain;
	choices->chain_len = zc->chain_len;
}

/*
 * OID for hash functions in RSA signatures.
 */
static const unsigned char HASH_OID_SHA1[] = {
	0x05, 0x2B, 0x0E, 0x03, 0x02, 0x1A
};

static const unsigned char HASH_OID_SHA224[] = {
	0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x04
};

static const unsigned char HASH_OID_SHA256[] = {
	0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01
};

static const unsigned char HASH_OID_SHA384[] = {
	0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02
};

static const unsigned char HASH_OID_SHA512[] = {
	0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03
};

static const unsigned char *HASH_OID[] = {
	HASH_OID_SHA1,
	HASH_OID_SHA224,
	HASH_OID_SHA256,
	HASH_OID_SHA384,
	HASH_OID_SHA512
};

static size_t
cc_do_sign(const br_ssl_client_certificate_class **pctx,
	int hash_id, size_t hv_len, unsigned char *data, size_t len)
{
	br_ssl_client_certificate_rsa_context *zc;
	unsigned char hv[64];
	const unsigned char *hash_oid;
	size_t sig_len;

	zc = (br_ssl_client_certificate_rsa_context *)pctx;
	memcpy(hv, data, hv_len);
	if (hash_id == 0) {
		hash_oid = NULL;
	} else if (hash_id >= 2 && hash_id <= 6) {
		hash_oid = HASH_OID[hash_id - 2];
	} else {
		return 0;
	}
	sig_len = (zc->sk->n_bitlen + 7) >> 3;
	if (len < sig_len) {
		return 0;
	}
	return zc->irsasign(hash_oid, hv, hv_len, zc->sk, data) ? sig_len : 0;
}

static const br_ssl_client_certificate_class ccert_vtable = {
	sizeof(br_ssl_client_certificate_rsa_context),
	cc_none0, /* start_name_list */
	cc_none1, /* start_name */
	cc_none2, /* append_name */
	cc_none0, /* end_name */
	cc_none0, /* end_name_list */
	cc_choose,
	0,
	cc_do_sign
};

/* see bearssl_ssl.h */
void
br_ssl_client_set_single_rsa(br_ssl_client_context *cc,
	const br_x509_certificate *chain, size_t chain_len,
	const br_rsa_private_key *sk, br_rsa_pkcs1_sign irsasign)
{
	cc->client_auth.single_rsa.vtable = &ccert_vtable;
	cc->client_auth.single_rsa.chain = chain;
	cc->client_auth.single_rsa.chain_len = chain_len;
	cc->client_auth.single_rsa.sk = sk;
	cc->client_auth.single_rsa.irsasign = irsasign;
	cc->client_auth_vtable = &cc->client_auth.single_rsa.vtable;
}
