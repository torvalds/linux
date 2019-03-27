/*
 * Copyright (c) 2018 Thomas Pornin <pornin@bolet.org>
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

/* see bearssl_x509.h */
size_t
br_encode_rsa_raw_der(void *dest, const br_rsa_private_key *sk,
	const br_rsa_public_key *pk, const void *d, size_t dlen)
{
	/*
	 * ASN.1 format:
	 *
	 *   RSAPrivateKey ::= SEQUENCE {
	 *       version           Version,
	 *       modulus           INTEGER,  -- n
	 *       publicExponent    INTEGER,  -- e
	 *       privateExponent   INTEGER,  -- d
	 *       prime1            INTEGER,  -- p
	 *       prime2            INTEGER,  -- q
	 *       exponent1         INTEGER,  -- d mod (p-1)
	 *       exponent2         INTEGER,  -- d mod (q-1)
	 *       coefficient       INTEGER,  -- (inverse of q) mod p
	 *       otherPrimeInfos   OtherPrimeInfos OPTIONAL
	 *   }
	 *
	 * The 'version' field is an INTEGER of value 0 (meaning: there
	 * are exactly two prime factors), and 'otherPrimeInfos' will
	 * be absent (because there are exactly two prime factors).
	 */

	br_asn1_uint num[9];
	size_t u, slen;

	/*
	 * For all INTEGER values, get the pointer and length for the
	 * data bytes.
	 */
	num[0] = br_asn1_uint_prepare(NULL, 0);
	num[1] = br_asn1_uint_prepare(pk->n, pk->nlen);
	num[2] = br_asn1_uint_prepare(pk->e, pk->elen);
	num[3] = br_asn1_uint_prepare(d, dlen);
	num[4] = br_asn1_uint_prepare(sk->p, sk->plen);
	num[5] = br_asn1_uint_prepare(sk->q, sk->qlen);
	num[6] = br_asn1_uint_prepare(sk->dp, sk->dplen);
	num[7] = br_asn1_uint_prepare(sk->dq, sk->dqlen);
	num[8] = br_asn1_uint_prepare(sk->iq, sk->iqlen);

	/*
	 * Get the length of the SEQUENCE contents.
	 */
	slen = 0;
	for (u = 0; u < 9; u ++) {
		uint32_t ilen;

		ilen = num[u].asn1len;
		slen += 1 + len_of_len(ilen) + ilen;
	}

	if (dest == NULL) {
		return 1 + len_of_len(slen) + slen;
	} else {
		unsigned char *buf;
		size_t lenlen;

		buf = dest;
		*buf ++ = 0x30;  /* SEQUENCE tag */
		lenlen = br_asn1_encode_length(buf, slen);
		buf += lenlen;
		for (u = 0; u < 9; u ++) {
			buf += br_asn1_encode_uint(buf, num[u]);
		}
		return 1 + lenlen + slen;
	}
}
