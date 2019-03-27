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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include "brssl.h"

static void
dn_append(void *ctx, const void *buf, size_t len)
{
	VEC_ADDMANY(*(bvector *)ctx, buf, len);
}

static int
certificate_to_trust_anchor_inner(br_x509_trust_anchor *ta,
	br_x509_certificate *xc)
{
	br_x509_decoder_context dc;
	bvector vdn = VEC_INIT;
	br_x509_pkey *pk;

	br_x509_decoder_init(&dc, dn_append, &vdn);
	br_x509_decoder_push(&dc, xc->data, xc->data_len);
	pk = br_x509_decoder_get_pkey(&dc);
	if (pk == NULL) {
		fprintf(stderr, "ERROR: CA decoding failed with error %d\n",
			br_x509_decoder_last_error(&dc));
		VEC_CLEAR(vdn);
		return -1;
	}
	ta->dn.data = VEC_TOARRAY(vdn);
	ta->dn.len = VEC_LEN(vdn);
	VEC_CLEAR(vdn);
	ta->flags = 0;
	if (br_x509_decoder_isCA(&dc)) {
		ta->flags |= BR_X509_TA_CA;
	}
	switch (pk->key_type) {
	case BR_KEYTYPE_RSA:
		ta->pkey.key_type = BR_KEYTYPE_RSA;
		ta->pkey.key.rsa.n = xblobdup(pk->key.rsa.n, pk->key.rsa.nlen);
		ta->pkey.key.rsa.nlen = pk->key.rsa.nlen;
		ta->pkey.key.rsa.e = xblobdup(pk->key.rsa.e, pk->key.rsa.elen);
		ta->pkey.key.rsa.elen = pk->key.rsa.elen;
		break;
	case BR_KEYTYPE_EC:
		ta->pkey.key_type = BR_KEYTYPE_EC;
		ta->pkey.key.ec.curve = pk->key.ec.curve;
		ta->pkey.key.ec.q = xblobdup(pk->key.ec.q, pk->key.ec.qlen);
		ta->pkey.key.ec.qlen = pk->key.ec.qlen;
		break;
	default:
		fprintf(stderr, "ERROR: unsupported public key type in CA\n");
		xfree(ta->dn.data);
		return -1;
	}
	return 0;
}

/* see brssl.h */
br_x509_trust_anchor *
certificate_to_trust_anchor(br_x509_certificate *xc)
{
	br_x509_trust_anchor ta;

	if (certificate_to_trust_anchor_inner(&ta, xc) < 0) {
		return NULL;
	} else {
		return xblobdup(&ta, sizeof ta);
	}
}

/* see brssl.h */
void
free_ta_contents(br_x509_trust_anchor *ta)
{
	xfree(ta->dn.data);
	switch (ta->pkey.key_type) {
	case BR_KEYTYPE_RSA:
		xfree(ta->pkey.key.rsa.n);
		xfree(ta->pkey.key.rsa.e);
		break;
	case BR_KEYTYPE_EC:
		xfree(ta->pkey.key.ec.q);
		break;
	}
}

/* see brssl.h */
size_t
read_trust_anchors(anchor_list *dst, const char *fname)
{
	br_x509_certificate *xcs;
	anchor_list tas = VEC_INIT;
	size_t u, num;

	xcs = read_certificates(fname, &num);
	if (xcs == NULL) {
		return 0;
	}
	for (u = 0; u < num; u ++) {
		br_x509_trust_anchor ta;

		if (certificate_to_trust_anchor_inner(&ta, &xcs[u]) < 0) {
			VEC_CLEAREXT(tas, free_ta_contents);
			free_certificates(xcs, num);
			return 0;
		}
		VEC_ADD(tas, ta);
	}
	VEC_ADDMANY(*dst, &VEC_ELT(tas, 0), num);
	VEC_CLEAR(tas);
	free_certificates(xcs, num);
	return num;
}

/* see brssl.h */
int
get_cert_signer_algo(br_x509_certificate *xc)
{
	br_x509_decoder_context dc;
	int err;

	br_x509_decoder_init(&dc, 0, 0);
	br_x509_decoder_push(&dc, xc->data, xc->data_len);
	err = br_x509_decoder_last_error(&dc);
	if (err != 0) {
		fprintf(stderr,
			"ERROR: certificate decoding failed with error %d\n",
			-err);
		return 0;
	}
	return br_x509_decoder_get_signer_key_type(&dc);
}

static void
xwc_start_chain(const br_x509_class **ctx, const char *server_name)
{
	x509_noanchor_context *xwc;

	xwc = (x509_noanchor_context *)ctx;
	(*xwc->inner)->start_chain(xwc->inner, server_name);
}

static void
xwc_start_cert(const br_x509_class **ctx, uint32_t length)
{
	x509_noanchor_context *xwc;

	xwc = (x509_noanchor_context *)ctx;
	(*xwc->inner)->start_cert(xwc->inner, length);
}

static void
xwc_append(const br_x509_class **ctx, const unsigned char *buf, size_t len)
{
	x509_noanchor_context *xwc;

	xwc = (x509_noanchor_context *)ctx;
	(*xwc->inner)->append(xwc->inner, buf, len);
}

static void
xwc_end_cert(const br_x509_class **ctx)
{
	x509_noanchor_context *xwc;

	xwc = (x509_noanchor_context *)ctx;
	(*xwc->inner)->end_cert(xwc->inner);
}

static unsigned
xwc_end_chain(const br_x509_class **ctx)
{
	x509_noanchor_context *xwc;
	unsigned r;

	xwc = (x509_noanchor_context *)ctx;
	r = (*xwc->inner)->end_chain(xwc->inner);
	if (r == BR_ERR_X509_NOT_TRUSTED) {
		r = 0;
	}
	return r;
}

static const br_x509_pkey *
xwc_get_pkey(const br_x509_class *const *ctx, unsigned *usages)
{
	x509_noanchor_context *xwc;

	xwc = (x509_noanchor_context *)ctx;
	return (*xwc->inner)->get_pkey(xwc->inner, usages);
}

/* see brssl.h */
const br_x509_class x509_noanchor_vtable = {
	sizeof(x509_noanchor_context),
	xwc_start_chain,
	xwc_start_cert,
	xwc_append,
	xwc_end_cert,
	xwc_end_chain,
	xwc_get_pkey
};

/* see brssl.h */
void
x509_noanchor_init(x509_noanchor_context *xwc, const br_x509_class **inner)
{
	xwc->vtable = &x509_noanchor_vtable;
	xwc->inner = inner;
}
