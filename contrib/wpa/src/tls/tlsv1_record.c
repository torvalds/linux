/*
 * TLSv1 Record Protocol
 * Copyright (c) 2006-2011, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/md5.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "tlsv1_common.h"
#include "tlsv1_record.h"


/**
 * tlsv1_record_set_cipher_suite - TLS record layer: Set cipher suite
 * @rl: Pointer to TLS record layer data
 * @cipher_suite: New cipher suite
 * Returns: 0 on success, -1 on failure
 *
 * This function is used to prepare TLS record layer for cipher suite change.
 * tlsv1_record_change_write_cipher() and
 * tlsv1_record_change_read_cipher() functions can then be used to change the
 * currently used ciphers.
 */
int tlsv1_record_set_cipher_suite(struct tlsv1_record_layer *rl,
				  u16 cipher_suite)
{
	const struct tls_cipher_suite *suite;
	const struct tls_cipher_data *data;

	wpa_printf(MSG_DEBUG, "TLSv1: Selected cipher suite: 0x%04x",
		   cipher_suite);
	rl->cipher_suite = cipher_suite;

	suite = tls_get_cipher_suite(cipher_suite);
	if (suite == NULL)
		return -1;

	if (suite->hash == TLS_HASH_MD5) {
		rl->hash_alg = CRYPTO_HASH_ALG_HMAC_MD5;
		rl->hash_size = MD5_MAC_LEN;
	} else if (suite->hash == TLS_HASH_SHA) {
		rl->hash_alg = CRYPTO_HASH_ALG_HMAC_SHA1;
		rl->hash_size = SHA1_MAC_LEN;
	} else if (suite->hash == TLS_HASH_SHA256) {
		rl->hash_alg = CRYPTO_HASH_ALG_HMAC_SHA256;
		rl->hash_size = SHA256_MAC_LEN;
	}

	data = tls_get_cipher_data(suite->cipher);
	if (data == NULL)
		return -1;

	rl->key_material_len = data->key_material;
	rl->iv_size = data->block_size;
	rl->cipher_alg = data->alg;

	return 0;
}


/**
 * tlsv1_record_change_write_cipher - TLS record layer: Change write cipher
 * @rl: Pointer to TLS record layer data
 * Returns: 0 on success (cipher changed), -1 on failure
 *
 * This function changes TLS record layer to use the new cipher suite
 * configured with tlsv1_record_set_cipher_suite() for writing.
 */
int tlsv1_record_change_write_cipher(struct tlsv1_record_layer *rl)
{
	wpa_printf(MSG_DEBUG, "TLSv1: Record Layer - New write cipher suite "
		   "0x%04x", rl->cipher_suite);
	rl->write_cipher_suite = rl->cipher_suite;
	os_memset(rl->write_seq_num, 0, TLS_SEQ_NUM_LEN);

	if (rl->write_cbc) {
		crypto_cipher_deinit(rl->write_cbc);
		rl->write_cbc = NULL;
	}
	if (rl->cipher_alg != CRYPTO_CIPHER_NULL) {
		rl->write_cbc = crypto_cipher_init(rl->cipher_alg,
						   rl->write_iv, rl->write_key,
						   rl->key_material_len);
		if (rl->write_cbc == NULL) {
			wpa_printf(MSG_DEBUG, "TLSv1: Failed to initialize "
				   "cipher");
			return -1;
		}
	}

	return 0;
}


/**
 * tlsv1_record_change_read_cipher - TLS record layer: Change read cipher
 * @rl: Pointer to TLS record layer data
 * Returns: 0 on success (cipher changed), -1 on failure
 *
 * This function changes TLS record layer to use the new cipher suite
 * configured with tlsv1_record_set_cipher_suite() for reading.
 */
int tlsv1_record_change_read_cipher(struct tlsv1_record_layer *rl)
{
	wpa_printf(MSG_DEBUG, "TLSv1: Record Layer - New read cipher suite "
		   "0x%04x", rl->cipher_suite);
	rl->read_cipher_suite = rl->cipher_suite;
	os_memset(rl->read_seq_num, 0, TLS_SEQ_NUM_LEN);

	if (rl->read_cbc) {
		crypto_cipher_deinit(rl->read_cbc);
		rl->read_cbc = NULL;
	}
	if (rl->cipher_alg != CRYPTO_CIPHER_NULL) {
		rl->read_cbc = crypto_cipher_init(rl->cipher_alg,
						  rl->read_iv, rl->read_key,
						  rl->key_material_len);
		if (rl->read_cbc == NULL) {
			wpa_printf(MSG_DEBUG, "TLSv1: Failed to initialize "
				   "cipher");
			return -1;
		}
	}

	return 0;
}


/**
 * tlsv1_record_send - TLS record layer: Send a message
 * @rl: Pointer to TLS record layer data
 * @content_type: Content type (TLS_CONTENT_TYPE_*)
 * @buf: Buffer for the generated TLS message (needs to have extra space for
 * header, IV (TLS v1.1), and HMAC)
 * @buf_size: Maximum buf size
 * @payload: Payload to be sent
 * @payload_len: Length of the payload
 * @out_len: Buffer for returning the used buf length
 * Returns: 0 on success, -1 on failure
 *
 * This function fills in the TLS record layer header, adds HMAC, and encrypts
 * the data using the current write cipher.
 */
int tlsv1_record_send(struct tlsv1_record_layer *rl, u8 content_type, u8 *buf,
		      size_t buf_size, const u8 *payload, size_t payload_len,
		      size_t *out_len)
{
	u8 *pos, *ct_start, *length, *cpayload;
	struct crypto_hash *hmac;
	size_t clen;
	int explicit_iv;

	pos = buf;
	if (pos + TLS_RECORD_HEADER_LEN > buf + buf_size)
		return -1;

	/* ContentType type */
	ct_start = pos;
	*pos++ = content_type;
	/* ProtocolVersion version */
	WPA_PUT_BE16(pos, rl->tls_version);
	pos += 2;
	/* uint16 length */
	length = pos;
	WPA_PUT_BE16(length, payload_len);
	pos += 2;

	cpayload = pos;
	explicit_iv = rl->write_cipher_suite != TLS_NULL_WITH_NULL_NULL &&
		rl->iv_size && rl->tls_version >= TLS_VERSION_1_1;
	if (explicit_iv) {
		/* opaque IV[Cipherspec.block_length] */
		if (pos + rl->iv_size > buf + buf_size)
			return -1;

		/*
		 * Use random number R per the RFC 4346, 6.2.3.2 CBC Block
		 * Cipher option 2a.
		 */

		if (os_get_random(pos, rl->iv_size))
			return -1;
		pos += rl->iv_size;
	}

	/*
	 * opaque fragment[TLSPlaintext.length]
	 * (opaque content[TLSCompressed.length] in GenericBlockCipher)
	 */
	if (pos + payload_len > buf + buf_size)
		return -1;
	os_memmove(pos, payload, payload_len);
	pos += payload_len;

	if (rl->write_cipher_suite != TLS_NULL_WITH_NULL_NULL) {
		/*
		 * MAC calculated over seq_num + TLSCompressed.type +
		 * TLSCompressed.version + TLSCompressed.length +
		 * TLSCompressed.fragment
		 */
		hmac = crypto_hash_init(rl->hash_alg, rl->write_mac_secret,
					rl->hash_size);
		if (hmac == NULL) {
			wpa_printf(MSG_DEBUG, "TLSv1: Record Layer - Failed "
				   "to initialize HMAC");
			return -1;
		}
		crypto_hash_update(hmac, rl->write_seq_num, TLS_SEQ_NUM_LEN);
		/* type + version + length + fragment */
		crypto_hash_update(hmac, ct_start, TLS_RECORD_HEADER_LEN);
		crypto_hash_update(hmac, payload, payload_len);
		clen = buf + buf_size - pos;
		if (clen < rl->hash_size) {
			wpa_printf(MSG_DEBUG, "TLSv1: Record Layer - Not "
				   "enough room for MAC");
			crypto_hash_finish(hmac, NULL, NULL);
			return -1;
		}

		if (crypto_hash_finish(hmac, pos, &clen) < 0) {
			wpa_printf(MSG_DEBUG, "TLSv1: Record Layer - Failed "
				   "to calculate HMAC");
			return -1;
		}
		wpa_hexdump(MSG_MSGDUMP, "TLSv1: Record Layer - Write HMAC",
			    pos, clen);
		pos += clen;
		if (rl->iv_size) {
			size_t len = pos - cpayload;
			size_t pad;
			pad = (len + 1) % rl->iv_size;
			if (pad)
				pad = rl->iv_size - pad;
			if (pos + pad + 1 > buf + buf_size) {
				wpa_printf(MSG_DEBUG, "TLSv1: No room for "
					   "block cipher padding");
				return -1;
			}
			os_memset(pos, pad, pad + 1);
			pos += pad + 1;
		}

		if (crypto_cipher_encrypt(rl->write_cbc, cpayload,
					  cpayload, pos - cpayload) < 0)
			return -1;
	}

	WPA_PUT_BE16(length, pos - length - 2);
	inc_byte_array(rl->write_seq_num, TLS_SEQ_NUM_LEN);

	*out_len = pos - buf;

	return 0;
}


/**
 * tlsv1_record_receive - TLS record layer: Process a received message
 * @rl: Pointer to TLS record layer data
 * @in_data: Received data
 * @in_len: Length of the received data
 * @out_data: Buffer for output data (must be at least as long as in_data)
 * @out_len: Set to maximum out_data length by caller; used to return the
 * length of the used data
 * @alert: Buffer for returning an alert value on failure
 * Returns: Number of bytes used from in_data on success, 0 if record was not
 *	complete (more data needed), or -1 on failure
 *
 * This function decrypts the received message, verifies HMAC and TLS record
 * layer header.
 */
int tlsv1_record_receive(struct tlsv1_record_layer *rl,
			 const u8 *in_data, size_t in_len,
			 u8 *out_data, size_t *out_len, u8 *alert)
{
	size_t i, rlen, hlen;
	u8 padlen;
	struct crypto_hash *hmac;
	u8 len[2], hash[100];
	int force_mac_error = 0;
	u8 ct;

	if (in_len < TLS_RECORD_HEADER_LEN) {
		wpa_printf(MSG_DEBUG, "TLSv1: Too short record (in_len=%lu) - "
			   "need more data",
			   (unsigned long) in_len);
		wpa_hexdump(MSG_MSGDUMP, "TLSv1: Record Layer - Received",
			    in_data, in_len);
		return 0;
	}

	ct = in_data[0];
	rlen = WPA_GET_BE16(in_data + 3);
	wpa_printf(MSG_DEBUG, "TLSv1: Received content type %d version %d.%d "
		   "length %d", ct, in_data[1], in_data[2], (int) rlen);

	/*
	 * TLS v1.0 and v1.1 RFCs were not exactly clear on the use of the
	 * protocol version in record layer. As such, accept any {03,xx} value
	 * to remain compatible with existing implementations.
	 */
	if (in_data[1] != 0x03) {
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected protocol version "
			   "%u.%u", in_data[1], in_data[2]);
		*alert = TLS_ALERT_PROTOCOL_VERSION;
		return -1;
	}

	/* TLSCiphertext must not be more than 2^14+2048 bytes */
	if (TLS_RECORD_HEADER_LEN + rlen > 18432) {
		wpa_printf(MSG_DEBUG, "TLSv1: Record overflow (len=%lu)",
			   (unsigned long) (TLS_RECORD_HEADER_LEN + rlen));
		*alert = TLS_ALERT_RECORD_OVERFLOW;
		return -1;
	}

	in_data += TLS_RECORD_HEADER_LEN;
	in_len -= TLS_RECORD_HEADER_LEN;

	if (rlen > in_len) {
		wpa_printf(MSG_DEBUG, "TLSv1: Not all record data included "
			   "(rlen=%lu > in_len=%lu)",
			   (unsigned long) rlen, (unsigned long) in_len);
		return 0;
	}

	wpa_hexdump(MSG_MSGDUMP, "TLSv1: Record Layer - Received",
		    in_data, rlen);

	if (ct != TLS_CONTENT_TYPE_HANDSHAKE &&
	    ct != TLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC &&
	    ct != TLS_CONTENT_TYPE_ALERT &&
	    ct != TLS_CONTENT_TYPE_APPLICATION_DATA) {
		wpa_printf(MSG_DEBUG, "TLSv1: Ignore record with unknown "
			   "content type 0x%x", ct);
		*alert = TLS_ALERT_UNEXPECTED_MESSAGE;
		return -1;
	}

	in_len = rlen;

	if (*out_len < in_len) {
		wpa_printf(MSG_DEBUG, "TLSv1: Not enough output buffer for "
			   "processing received record");
		*alert = TLS_ALERT_INTERNAL_ERROR;
		return -1;
	}

	if (rl->read_cipher_suite != TLS_NULL_WITH_NULL_NULL) {
		size_t plen;
		if (crypto_cipher_decrypt(rl->read_cbc, in_data,
					  out_data, in_len) < 0) {
			*alert = TLS_ALERT_DECRYPTION_FAILED;
			return -1;
		}
		plen = in_len;
		wpa_hexdump_key(MSG_MSGDUMP, "TLSv1: Record Layer - Decrypted "
				"data", out_data, plen);

		if (rl->iv_size) {
			/*
			 * TLS v1.0 defines different alert values for various
			 * failures. That may information to aid in attacks, so
			 * use the same bad_record_mac alert regardless of the
			 * issues.
			 *
			 * In addition, instead of returning immediately on
			 * error, run through the MAC check to make timing
			 * attacks more difficult.
			 */

			if (rl->tls_version >= TLS_VERSION_1_1) {
				/* Remove opaque IV[Cipherspec.block_length] */
				if (plen < rl->iv_size) {
					wpa_printf(MSG_DEBUG, "TLSv1.1: Not "
						   "enough room for IV");
					force_mac_error = 1;
					goto check_mac;
				}
				os_memmove(out_data, out_data + rl->iv_size,
					   plen - rl->iv_size);
				plen -= rl->iv_size;
			}

			/* Verify and remove padding */
			if (plen == 0) {
				wpa_printf(MSG_DEBUG, "TLSv1: Too short record"
					   " (no pad)");
				force_mac_error = 1;
				goto check_mac;
			}
			padlen = out_data[plen - 1];
			if (padlen >= plen) {
				wpa_printf(MSG_DEBUG, "TLSv1: Incorrect pad "
					   "length (%u, plen=%lu) in "
					   "received record",
					   padlen, (unsigned long) plen);
				force_mac_error = 1;
				goto check_mac;
			}
			for (i = plen - padlen - 1; i < plen - 1; i++) {
				if (out_data[i] != padlen) {
					wpa_hexdump(MSG_DEBUG,
						    "TLSv1: Invalid pad in "
						    "received record",
						    out_data + plen - padlen -
						    1, padlen + 1);
					force_mac_error = 1;
					goto check_mac;
				}
			}

			plen -= padlen + 1;

			wpa_hexdump_key(MSG_MSGDUMP, "TLSv1: Record Layer - "
					"Decrypted data with IV and padding "
					"removed", out_data, plen);
		}

	check_mac:
		if (plen < rl->hash_size) {
			wpa_printf(MSG_DEBUG, "TLSv1: Too short record; no "
				   "hash value");
			*alert = TLS_ALERT_BAD_RECORD_MAC;
			return -1;
		}

		plen -= rl->hash_size;

		hmac = crypto_hash_init(rl->hash_alg, rl->read_mac_secret,
					rl->hash_size);
		if (hmac == NULL) {
			wpa_printf(MSG_DEBUG, "TLSv1: Record Layer - Failed "
				   "to initialize HMAC");
			*alert = TLS_ALERT_INTERNAL_ERROR;
			return -1;
		}

		crypto_hash_update(hmac, rl->read_seq_num, TLS_SEQ_NUM_LEN);
		/* type + version + length + fragment */
		crypto_hash_update(hmac, in_data - TLS_RECORD_HEADER_LEN, 3);
		WPA_PUT_BE16(len, plen);
		crypto_hash_update(hmac, len, 2);
		crypto_hash_update(hmac, out_data, plen);
		hlen = sizeof(hash);
		if (crypto_hash_finish(hmac, hash, &hlen) < 0) {
			wpa_printf(MSG_DEBUG, "TLSv1: Record Layer - Failed "
				   "to calculate HMAC");
			*alert = TLS_ALERT_INTERNAL_ERROR;
			return -1;
		}
		if (hlen != rl->hash_size ||
		    os_memcmp_const(hash, out_data + plen, hlen) != 0 ||
		    force_mac_error) {
			wpa_printf(MSG_DEBUG, "TLSv1: Invalid HMAC value in "
				   "received message (force_mac_error=%d)",
				   force_mac_error);
			*alert = TLS_ALERT_BAD_RECORD_MAC;
			return -1;
		}

		*out_len = plen;
	} else {
		os_memcpy(out_data, in_data, in_len);
		*out_len = in_len;
	}

	/* TLSCompressed must not be more than 2^14+1024 bytes */
	if (TLS_RECORD_HEADER_LEN + *out_len > 17408) {
		wpa_printf(MSG_DEBUG, "TLSv1: Record overflow (len=%lu)",
			   (unsigned long) (TLS_RECORD_HEADER_LEN + *out_len));
		*alert = TLS_ALERT_RECORD_OVERFLOW;
		return -1;
	}

	inc_byte_array(rl->read_seq_num, TLS_SEQ_NUM_LEN);

	return TLS_RECORD_HEADER_LEN + rlen;
}
