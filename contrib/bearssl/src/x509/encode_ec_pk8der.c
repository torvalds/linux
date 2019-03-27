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
br_encode_ec_pkcs8_der(void *dest,
	const br_ec_private_key *sk, const br_ec_public_key *pk)
{
	/*
	 * ASN.1 format:
	 *
	 *   OneAsymmetricKey ::= SEQUENCE {
	 *     version                   Version,
	 *     privateKeyAlgorithm       PrivateKeyAlgorithmIdentifier,
	 *     privateKey                PrivateKey,
	 *     attributes            [0] Attributes OPTIONAL,
	 *     ...,
	 *     [[2: publicKey        [1] PublicKey OPTIONAL ]],
	 *     ...
	 *   }
	 *
	 * We don't include attributes or public key (the public key
	 * is included in the private key value instead). The
	 * 'version' field is an INTEGER that we will set to 0
	 * (meaning 'v1', compatible with previous versions of PKCS#8).
	 * The 'privateKeyAlgorithm' structure is an AlgorithmIdentifier
	 * whose OID should be id-ecPublicKey, with, as parameters, the
	 * curve OID. The 'privateKey' is an OCTET STRING, whose value
	 * is the "raw DER" encoding of the key pair.
	 */

	/*
	 * OID id-ecPublicKey (1.2.840.10045.2.1), DER-encoded (with
	 * the tag).
	 */
	static const unsigned char OID_ECPUBKEY[] = {
		0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01
	};

	size_t len_version, len_privateKeyAlgorithm, len_privateKeyValue;
	size_t len_privateKey, len_seq;
	const unsigned char *oid;

	oid = br_get_curve_OID(sk->curve);
	if (oid == NULL) {
		return 0;
	}
	len_version = 3;
	len_privateKeyAlgorithm = 2 + sizeof OID_ECPUBKEY + 2 + oid[0];
	len_privateKeyValue = br_encode_ec_raw_der_inner(NULL, sk, pk, 0);
	len_privateKey = 1 + len_of_len(len_privateKeyValue)
		+ len_privateKeyValue;
	len_seq = len_version + len_privateKeyAlgorithm + len_privateKey;

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
		*buf ++ = 0x00;

		/* privateKeyAlgorithm */
		*buf ++ = 0x30;
		*buf ++ = (sizeof OID_ECPUBKEY) + 2 + oid[0];
		memcpy(buf, OID_ECPUBKEY, sizeof OID_ECPUBKEY);
		buf += sizeof OID_ECPUBKEY;
		*buf ++ = 0x06;
		memcpy(buf, oid, 1 + oid[0]);
		buf += 1 + oid[0];

		/* privateKey */
		*buf ++ = 0x04;
		buf += br_asn1_encode_length(buf, len_privateKeyValue);
		br_encode_ec_raw_der_inner(buf, sk, pk, 0);

		return 1 + lenlen + len_seq;
	}
}
