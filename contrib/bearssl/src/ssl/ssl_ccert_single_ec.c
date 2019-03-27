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
	br_ssl_client_certificate_ec_context *zc;
	int x;
	int scurve;

	zc = (br_ssl_client_certificate_ec_context *)pctx;
	scurve = br_ssl_client_get_server_curve(cc);

	if ((zc->allowed_usages & BR_KEYTYPE_KEYX) != 0
		&& scurve == zc->sk->curve)
	{
		int x;

		x = (zc->issuer_key_type == BR_KEYTYPE_RSA) ? 16 : 17;
		if (((auth_types >> x) & 1) != 0) {
			choices->auth_type = BR_AUTH_ECDH;
			choices->hash_id = -1;
			choices->chain = zc->chain;
			choices->chain_len = zc->chain_len;
		}
	}

	/*
	 * For ECDSA authentication, we must choose an appropriate
	 * hash function.
	 */
	x = br_ssl_choose_hash((unsigned)(auth_types >> 8));
	if (x == 0 || (zc->allowed_usages & BR_KEYTYPE_SIGN) == 0) {
		memset(choices, 0, sizeof *choices);
		return;
	}
	choices->auth_type = BR_AUTH_ECDSA;
	choices->hash_id = x;
	choices->chain = zc->chain;
	choices->chain_len = zc->chain_len;
}

static uint32_t
cc_do_keyx(const br_ssl_client_certificate_class **pctx,
	unsigned char *data, size_t *len)
{
	br_ssl_client_certificate_ec_context *zc;
	uint32_t r;
	size_t xoff, xlen;

	zc = (br_ssl_client_certificate_ec_context *)pctx;
	r = zc->iec->mul(data, *len, zc->sk->x, zc->sk->xlen, zc->sk->curve);
	xoff = zc->iec->xoff(zc->sk->curve, &xlen);
	memmove(data, data + xoff, xlen);
	*len = xlen;
	return r;
}

static size_t
cc_do_sign(const br_ssl_client_certificate_class **pctx,
	int hash_id, size_t hv_len, unsigned char *data, size_t len)
{
	br_ssl_client_certificate_ec_context *zc;
	unsigned char hv[64];
	const br_hash_class *hc;

	zc = (br_ssl_client_certificate_ec_context *)pctx;
	memcpy(hv, data, hv_len);
	hc = br_multihash_getimpl(zc->mhash, hash_id);
	if (hc == NULL) {
		return 0;
	}
	if (len < 139) {
		return 0;
	}
	return zc->iecdsa(zc->iec, hc, hv, zc->sk, data);
}

static const br_ssl_client_certificate_class ccert_vtable = {
	sizeof(br_ssl_client_certificate_ec_context),
	cc_none0, /* start_name_list */
	cc_none1, /* start_name */
	cc_none2, /* append_name */
	cc_none0, /* end_name */
	cc_none0, /* end_name_list */
	cc_choose,
	cc_do_keyx,
	cc_do_sign
};

/* see bearssl_ssl.h */
void
br_ssl_client_set_single_ec(br_ssl_client_context *cc,
	const br_x509_certificate *chain, size_t chain_len,
	const br_ec_private_key *sk, unsigned allowed_usages,
	unsigned cert_issuer_key_type,
	const br_ec_impl *iec, br_ecdsa_sign iecdsa)
{
	cc->client_auth.single_ec.vtable = &ccert_vtable;
	cc->client_auth.single_ec.chain = chain;
	cc->client_auth.single_ec.chain_len = chain_len;
	cc->client_auth.single_ec.sk = sk;
	cc->client_auth.single_ec.allowed_usages = allowed_usages;
	cc->client_auth.single_ec.issuer_key_type = cert_issuer_key_type;
	cc->client_auth.single_ec.mhash = &cc->eng.mhash;
	cc->client_auth.single_ec.iec = iec;
	cc->client_auth.single_ec.iecdsa = iecdsa;
	cc->client_auth_vtable = &cc->client_auth.single_ec.vtable;
}
