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

/* see inner.h */
const unsigned char *
br_get_curve_OID(int curve)
{
	static const unsigned char OID_secp256r1[] = {
		0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07
	};
	static const unsigned char OID_secp384r1[] = {
		0x05, 0x2b, 0x81, 0x04, 0x00, 0x22
	};
	static const unsigned char OID_secp521r1[] = {
		0x05, 0x2b, 0x81, 0x04, 0x00, 0x23
	};

	switch (curve) {
	case BR_EC_secp256r1:  return OID_secp256r1;
	case BR_EC_secp384r1:  return OID_secp384r1;
	case BR_EC_secp521r1:  return OID_secp521r1;
	default:
		return NULL;
	}
}

/* see inner.h */
size_t
br_encode_ec_raw_der_inner(void *dest,
	const br_ec_private_key *sk, const br_ec_public_key *pk,
	int include_curve_oid)
{
	/*
	 * ASN.1 format:
	 *
	 *   ECPrivateKey ::= SEQUENCE {
	 *     version        INTEGER { ecPrivkeyVer1(1) } (ecPrivkeyVer1),
	 *     privateKey     OCTET STRING,
	 *     parameters [0] ECParameters {{ NamedCurve }} OPTIONAL,
	 *     publicKey  [1] BIT STRING OPTIONAL
	 *   }
	 *
	 * The tages '[0]' and '[1]' are explicit. The 'ECParameters'
	 * is a CHOICE; in our case, it will always be an OBJECT IDENTIFIER
	 * that identifies the curve.
	 *
	 * The value of the 'privateKey' field is the raw unsigned big-endian
	 * encoding of the private key (integer modulo the curve subgroup
	 * order); there is no INTEGER tag, and the leading bit may be 1.
	 * Also, leading bytes of value 0x00 are _not_ removed.
	 *
	 * The 'publicKey' contents are the raw encoded public key point,
	 * normally uncompressed (leading byte of value 0x04, followed
	 * by the unsigned big-endian encodings of the X and Y coordinates,
	 * padded to the full field length if necessary).
	 */

	size_t len_version, len_privateKey, len_parameters, len_publicKey;
	size_t len_publicKey_bits, len_seq;
	const unsigned char *oid;

	if (include_curve_oid) {
		oid = br_get_curve_OID(sk->curve);
		if (oid == NULL) {
			return 0;
		}
	} else {
		oid = NULL;
	}
	len_version = 3;
	len_privateKey = 1 + len_of_len(sk->xlen) + sk->xlen;
	if (include_curve_oid) {
		len_parameters = 4 + oid[0];
	} else {
		len_parameters = 0;
	}
	if (pk == NULL) {
		len_publicKey = 0;
		len_publicKey_bits = 0;
	} else {
		len_publicKey_bits = 2 + len_of_len(pk->qlen) + pk->qlen;
		len_publicKey = 1 + len_of_len(len_publicKey_bits)
			+ len_publicKey_bits;
	}
	len_seq = len_version + len_privateKey + len_parameters + len_publicKey;
	if (dest == NULL) {
		return 1 + len_of_len(len_seq) + len_seq;
	} else {
		unsigned char *buf;
		size_t lenlen;

		buf = dest;
		*buf ++ = 0x30;  /* SEQUENCE tag */
		lenlen = br_asn1_encode_length(buf, len_seq);
		buf += lenlen;

		/* version */
		*buf ++ = 0x02;
		*buf ++ = 0x01;
		*buf ++ = 0x01;

		/* privateKey */
		*buf ++ = 0x04;
		buf += br_asn1_encode_length(buf, sk->xlen);
		memcpy(buf, sk->x, sk->xlen);
		buf += sk->xlen;

		/* parameters */
		if (include_curve_oid) {
			*buf ++ = 0xA0;
			*buf ++ = oid[0] + 2;
			*buf ++ = 0x06;
			memcpy(buf, oid, oid[0] + 1);
			buf += oid[0] + 1;
		}

		/* publicKey */
		if (pk != NULL) {
			*buf ++ = 0xA1;
			buf += br_asn1_encode_length(buf, len_publicKey_bits);
			*buf ++ = 0x03;
			buf += br_asn1_encode_length(buf, pk->qlen + 1);
			*buf ++ = 0x00;
			memcpy(buf, pk->q, pk->qlen);
			/* buf += pk->qlen; */
		}

		return 1 + lenlen + len_seq;
	}
}

/* see bearssl_x509.h */
size_t
br_encode_ec_raw_der(void *dest,
	const br_ec_private_key *sk, const br_ec_public_key *pk)
{
	return br_encode_ec_raw_der_inner(dest, sk, pk, 1);
}
