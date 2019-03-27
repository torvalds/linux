/*
 * PKCS #1 (RSA Encryption)
 * Copyright (c) 2006-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/crypto.h"
#include "rsa.h"
#include "asn1.h"
#include "pkcs1.h"


static int pkcs1_generate_encryption_block(u8 block_type, size_t modlen,
					   const u8 *in, size_t inlen,
					   u8 *out, size_t *outlen)
{
	size_t ps_len;
	u8 *pos;

	/*
	 * PKCS #1 v1.5, 8.1:
	 *
	 * EB = 00 || BT || PS || 00 || D
	 * BT = 00 or 01 for private-key operation; 02 for public-key operation
	 * PS = k-3-||D||; at least eight octets
	 * (BT=0: PS=0x00, BT=1: PS=0xff, BT=2: PS=pseudorandom non-zero)
	 * k = length of modulus in octets (modlen)
	 */

	if (modlen < 12 || modlen > *outlen || inlen > modlen - 11) {
		wpa_printf(MSG_DEBUG, "PKCS #1: %s - Invalid buffer "
			   "lengths (modlen=%lu outlen=%lu inlen=%lu)",
			   __func__, (unsigned long) modlen,
			   (unsigned long) *outlen,
			   (unsigned long) inlen);
		return -1;
	}

	pos = out;
	*pos++ = 0x00;
	*pos++ = block_type; /* BT */
	ps_len = modlen - inlen - 3;
	switch (block_type) {
	case 0:
		os_memset(pos, 0x00, ps_len);
		pos += ps_len;
		break;
	case 1:
		os_memset(pos, 0xff, ps_len);
		pos += ps_len;
		break;
	case 2:
		if (os_get_random(pos, ps_len) < 0) {
			wpa_printf(MSG_DEBUG, "PKCS #1: %s - Failed to get "
				   "random data for PS", __func__);
			return -1;
		}
		while (ps_len--) {
			if (*pos == 0x00)
				*pos = 0x01;
			pos++;
		}
		break;
	default:
		wpa_printf(MSG_DEBUG, "PKCS #1: %s - Unsupported block type "
			   "%d", __func__, block_type);
		return -1;
	}
	*pos++ = 0x00;
	os_memcpy(pos, in, inlen); /* D */

	return 0;
}


int pkcs1_encrypt(int block_type, struct crypto_rsa_key *key,
		  int use_private, const u8 *in, size_t inlen,
		  u8 *out, size_t *outlen)
{
	size_t modlen;

	modlen = crypto_rsa_get_modulus_len(key);

	if (pkcs1_generate_encryption_block(block_type, modlen, in, inlen,
					    out, outlen) < 0)
		return -1;

	return crypto_rsa_exptmod(out, modlen, out, outlen, key, use_private);
}


int pkcs1_v15_private_key_decrypt(struct crypto_rsa_key *key,
				  const u8 *in, size_t inlen,
				  u8 *out, size_t *outlen)
{
	int res;
	u8 *pos, *end;

	res = crypto_rsa_exptmod(in, inlen, out, outlen, key, 1);
	if (res)
		return res;

	if (*outlen < 2 || out[0] != 0 || out[1] != 2)
		return -1;

	/* Skip PS (pseudorandom non-zero octets) */
	pos = out + 2;
	end = out + *outlen;
	while (*pos && pos < end)
		pos++;
	if (pos == end)
		return -1;
	if (pos - out - 2 < 8) {
		/* PKCS #1 v1.5, 8.1: At least eight octets long PS */
		wpa_printf(MSG_INFO, "LibTomCrypt: Too short padding");
		return -1;
	}
	pos++;

	*outlen -= pos - out;

	/* Strip PKCS #1 header */
	os_memmove(out, pos, *outlen);

	return 0;
}


int pkcs1_decrypt_public_key(struct crypto_rsa_key *key,
			     const u8 *crypt, size_t crypt_len,
			     u8 *plain, size_t *plain_len)
{
	size_t len;
	u8 *pos;

	len = *plain_len;
	if (crypto_rsa_exptmod(crypt, crypt_len, plain, &len, key, 0) < 0)
		return -1;

	/*
	 * PKCS #1 v1.5, 8.1:
	 *
	 * EB = 00 || BT || PS || 00 || D
	 * BT = 00 or 01
	 * PS = k-3-||D|| times (00 if BT=00) or (FF if BT=01)
	 * k = length of modulus in octets
	 *
	 * Based on 10.1.3, "The block type shall be 01" for a signature.
	 */

	if (len < 3 + 8 + 16 /* min hash len */ ||
	    plain[0] != 0x00 || plain[1] != 0x01) {
		wpa_printf(MSG_INFO, "LibTomCrypt: Invalid signature EB "
			   "structure");
		return -1;
	}

	pos = plain + 3;
	/* BT = 01 */
	if (plain[2] != 0xff) {
		wpa_printf(MSG_INFO, "LibTomCrypt: Invalid signature "
			   "PS (BT=01)");
		return -1;
	}
	while (pos < plain + len && *pos == 0xff)
		pos++;

	if (pos - plain - 2 < 8) {
		/* PKCS #1 v1.5, 8.1: At least eight octets long PS */
		wpa_printf(MSG_INFO, "LibTomCrypt: Too short signature "
			   "padding");
		return -1;
	}

	if (pos + 16 /* min hash len */ >= plain + len || *pos != 0x00) {
		wpa_printf(MSG_INFO, "LibTomCrypt: Invalid signature EB "
			   "structure (2)");
		return -1;
	}
	pos++;
	len -= pos - plain;

	/* Strip PKCS #1 header */
	os_memmove(plain, pos, len);
	*plain_len = len;

	return 0;
}


int pkcs1_v15_sig_ver(struct crypto_public_key *pk,
		      const u8 *s, size_t s_len,
		      const struct asn1_oid *hash_alg,
		      const u8 *hash, size_t hash_len)
{
	int res;
	u8 *decrypted;
	size_t decrypted_len;
	const u8 *pos, *end, *next, *da_end;
	struct asn1_hdr hdr;
	struct asn1_oid oid;

	decrypted = os_malloc(s_len);
	if (decrypted == NULL)
		return -1;
	decrypted_len = s_len;
	res = crypto_public_key_decrypt_pkcs1(pk, s, s_len, decrypted,
					      &decrypted_len);
	if (res < 0) {
		wpa_printf(MSG_INFO, "PKCS #1: RSA decrypt failed");
		os_free(decrypted);
		return -1;
	}
	wpa_hexdump(MSG_DEBUG, "Decrypted(S)", decrypted, decrypted_len);

	/*
	 * PKCS #1 v1.5, 10.1.2:
	 *
	 * DigestInfo ::= SEQUENCE {
	 *     digestAlgorithm DigestAlgorithmIdentifier,
	 *     digest Digest
	 * }
	 *
	 * DigestAlgorithmIdentifier ::= AlgorithmIdentifier
	 *
	 * Digest ::= OCTET STRING
	 *
	 */
	if (asn1_get_next(decrypted, decrypted_len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #1: Expected SEQUENCE (DigestInfo) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		os_free(decrypted);
		return -1;
	}

	pos = hdr.payload;
	end = pos + hdr.length;

	/*
	 * X.509:
	 * AlgorithmIdentifier ::= SEQUENCE {
	 *     algorithm            OBJECT IDENTIFIER,
	 *     parameters           ANY DEFINED BY algorithm OPTIONAL
	 * }
	 */

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #1: Expected SEQUENCE (AlgorithmIdentifier) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		os_free(decrypted);
		return -1;
	}
	da_end = hdr.payload + hdr.length;

	if (asn1_get_oid(hdr.payload, hdr.length, &oid, &next)) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #1: Failed to parse digestAlgorithm");
		os_free(decrypted);
		return -1;
	}

	if (!asn1_oid_equal(&oid, hash_alg)) {
		char txt[100], txt2[100];
		asn1_oid_to_str(&oid, txt, sizeof(txt));
		asn1_oid_to_str(hash_alg, txt2, sizeof(txt2));
		wpa_printf(MSG_DEBUG,
			   "PKCS #1: Hash alg OID mismatch: was %s, expected %s",
			   txt, txt2);
		os_free(decrypted);
		return -1;
	}

	/* Digest ::= OCTET STRING */
	pos = da_end;
	end = decrypted + decrypted_len;

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #1: Expected OCTETSTRING (Digest) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		os_free(decrypted);
		return -1;
	}
	wpa_hexdump(MSG_MSGDUMP, "PKCS #1: Decrypted Digest",
		    hdr.payload, hdr.length);

	if (hdr.length != hash_len ||
	    os_memcmp_const(hdr.payload, hash, hdr.length) != 0) {
		wpa_printf(MSG_INFO, "PKCS #1: Digest value does not match calculated hash");
		os_free(decrypted);
		return -1;
	}

	os_free(decrypted);

	if (hdr.payload + hdr.length != end) {
		wpa_printf(MSG_INFO,
			   "PKCS #1: Extra data after signature - reject");

		wpa_hexdump(MSG_DEBUG, "PKCS #1: Extra data",
			    hdr.payload + hdr.length,
			    end - hdr.payload - hdr.length);
		return -1;
	}

	return 0;
}
