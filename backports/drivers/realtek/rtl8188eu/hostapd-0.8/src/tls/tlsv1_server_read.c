/*
 * TLSv1 server - read handshake message
 * Copyright (c) 2006-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/md5.h"
#include "crypto/sha1.h"
#include "crypto/tls.h"
#include "x509v3.h"
#include "tlsv1_common.h"
#include "tlsv1_record.h"
#include "tlsv1_server.h"
#include "tlsv1_server_i.h"


static int tls_process_client_key_exchange(struct tlsv1_server *conn, u8 ct,
					   const u8 *in_data, size_t *in_len);
static int tls_process_change_cipher_spec(struct tlsv1_server *conn,
					  u8 ct, const u8 *in_data,
					  size_t *in_len);


static int tls_process_client_hello(struct tlsv1_server *conn, u8 ct,
				    const u8 *in_data, size_t *in_len)
{
	const u8 *pos, *end, *c;
	size_t left, len, i, j;
	u16 cipher_suite;
	u16 num_suites;
	int compr_null_found;
	u16 ext_type, ext_len;

	if (ct != TLS_CONTENT_TYPE_HANDSHAKE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected Handshake; "
			   "received content type 0x%x", ct);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4)
		goto decode_error;

	/* HandshakeType msg_type */
	if (*pos != TLS_HANDSHAKE_TYPE_CLIENT_HELLO) {
		wpa_printf(MSG_DEBUG, "TLSv1: Received unexpected handshake "
			   "message %d (expected ClientHello)", *pos);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}
	wpa_printf(MSG_DEBUG, "TLSv1: Received ClientHello");
	pos++;
	/* uint24 length */
	len = WPA_GET_BE24(pos);
	pos += 3;
	left -= 4;

	if (len > left)
		goto decode_error;

	/* body - ClientHello */

	wpa_hexdump(MSG_MSGDUMP, "TLSv1: ClientHello", pos, len);
	end = pos + len;

	/* ProtocolVersion client_version */
	if (end - pos < 2)
		goto decode_error;
	conn->client_version = WPA_GET_BE16(pos);
	wpa_printf(MSG_DEBUG, "TLSv1: Client version %d.%d",
		   conn->client_version >> 8, conn->client_version & 0xff);
	if (conn->client_version < TLS_VERSION) {
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected protocol version in "
			   "ClientHello");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_PROTOCOL_VERSION);
		return -1;
	}
	pos += 2;

	/* Random random */
	if (end - pos < TLS_RANDOM_LEN)
		goto decode_error;

	os_memcpy(conn->client_random, pos, TLS_RANDOM_LEN);
	pos += TLS_RANDOM_LEN;
	wpa_hexdump(MSG_MSGDUMP, "TLSv1: client_random",
		    conn->client_random, TLS_RANDOM_LEN);

	/* SessionID session_id */
	if (end - pos < 1)
		goto decode_error;
	if (end - pos < 1 + *pos || *pos > TLS_SESSION_ID_MAX_LEN)
		goto decode_error;
	wpa_hexdump(MSG_MSGDUMP, "TLSv1: client session_id", pos + 1, *pos);
	pos += 1 + *pos;
	/* TODO: add support for session resumption */

	/* CipherSuite cipher_suites<2..2^16-1> */
	if (end - pos < 2)
		goto decode_error;
	num_suites = WPA_GET_BE16(pos);
	pos += 2;
	if (end - pos < num_suites)
		goto decode_error;
	wpa_hexdump(MSG_MSGDUMP, "TLSv1: client cipher suites",
		    pos, num_suites);
	if (num_suites & 1)
		goto decode_error;
	num_suites /= 2;

	cipher_suite = 0;
	for (i = 0; !cipher_suite && i < conn->num_cipher_suites; i++) {
		c = pos;
		for (j = 0; j < num_suites; j++) {
			u16 tmp = WPA_GET_BE16(c);
			c += 2;
			if (!cipher_suite && tmp == conn->cipher_suites[i]) {
				cipher_suite = tmp;
				break;
			}
		}
	}
	pos += num_suites * 2;
	if (!cipher_suite) {
		wpa_printf(MSG_INFO, "TLSv1: No supported cipher suite "
			   "available");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_ILLEGAL_PARAMETER);
		return -1;
	}

	if (tlsv1_record_set_cipher_suite(&conn->rl, cipher_suite) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to set CipherSuite for "
			   "record layer");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	conn->cipher_suite = cipher_suite;

	/* CompressionMethod compression_methods<1..2^8-1> */
	if (end - pos < 1)
		goto decode_error;
	num_suites = *pos++;
	if (end - pos < num_suites)
		goto decode_error;
	wpa_hexdump(MSG_MSGDUMP, "TLSv1: client compression_methods",
		    pos, num_suites);
	compr_null_found = 0;
	for (i = 0; i < num_suites; i++) {
		if (*pos++ == TLS_COMPRESSION_NULL)
			compr_null_found = 1;
	}
	if (!compr_null_found) {
		wpa_printf(MSG_INFO, "TLSv1: Client does not accept NULL "
			   "compression");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_ILLEGAL_PARAMETER);
		return -1;
	}

	if (end - pos == 1) {
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected extra octet in the "
			    "end of ClientHello: 0x%02x", *pos);
		goto decode_error;
	}

	if (end - pos >= 2) {
		/* Extension client_hello_extension_list<0..2^16-1> */
		ext_len = WPA_GET_BE16(pos);
		pos += 2;

		wpa_printf(MSG_DEBUG, "TLSv1: %u bytes of ClientHello "
			   "extensions", ext_len);
		if (end - pos != ext_len) {
			wpa_printf(MSG_DEBUG, "TLSv1: Invalid ClientHello "
				   "extension list length %u (expected %u)",
				   ext_len, (unsigned int) (end - pos));
			goto decode_error;
		}

		/*
		 * struct {
		 *   ExtensionType extension_type (0..65535)
		 *   opaque extension_data<0..2^16-1>
		 * } Extension;
		 */

		while (pos < end) {
			if (end - pos < 2) {
				wpa_printf(MSG_DEBUG, "TLSv1: Invalid "
					   "extension_type field");
				goto decode_error;
			}

			ext_type = WPA_GET_BE16(pos);
			pos += 2;

			if (end - pos < 2) {
				wpa_printf(MSG_DEBUG, "TLSv1: Invalid "
					   "extension_data length field");
				goto decode_error;
			}

			ext_len = WPA_GET_BE16(pos);
			pos += 2;

			if (end - pos < ext_len) {
				wpa_printf(MSG_DEBUG, "TLSv1: Invalid "
					   "extension_data field");
				goto decode_error;
			}

			wpa_printf(MSG_DEBUG, "TLSv1: ClientHello Extension "
				   "type %u", ext_type);
			wpa_hexdump(MSG_MSGDUMP, "TLSv1: ClientHello "
				    "Extension data", pos, ext_len);

			if (ext_type == TLS_EXT_SESSION_TICKET) {
				os_free(conn->session_ticket);
				conn->session_ticket = os_malloc(ext_len);
				if (conn->session_ticket) {
					os_memcpy(conn->session_ticket, pos,
						  ext_len);
					conn->session_ticket_len = ext_len;
				}
			}

			pos += ext_len;
		}
	}

	*in_len = end - in_data;

	wpa_printf(MSG_DEBUG, "TLSv1: ClientHello OK - proceed to "
		   "ServerHello");
	conn->state = SERVER_HELLO;

	return 0;

decode_error:
	wpa_printf(MSG_DEBUG, "TLSv1: Failed to decode ClientHello");
	tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
			   TLS_ALERT_DECODE_ERROR);
	return -1;
}


static int tls_process_certificate(struct tlsv1_server *conn, u8 ct,
				   const u8 *in_data, size_t *in_len)
{
	const u8 *pos, *end;
	size_t left, len, list_len, cert_len, idx;
	u8 type;
	struct x509_certificate *chain = NULL, *last = NULL, *cert;
	int reason;

	if (ct != TLS_CONTENT_TYPE_HANDSHAKE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected Handshake; "
			   "received content type 0x%x", ct);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4) {
		wpa_printf(MSG_DEBUG, "TLSv1: Too short Certificate message "
			   "(len=%lu)", (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	type = *pos++;
	len = WPA_GET_BE24(pos);
	pos += 3;
	left -= 4;

	if (len > left) {
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected Certificate message "
			   "length (len=%lu != left=%lu)",
			   (unsigned long) len, (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	if (type == TLS_HANDSHAKE_TYPE_CLIENT_KEY_EXCHANGE) {
		if (conn->verify_peer) {
			wpa_printf(MSG_DEBUG, "TLSv1: Client did not include "
				   "Certificate");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_UNEXPECTED_MESSAGE);
			return -1;
		}

		return tls_process_client_key_exchange(conn, ct, in_data,
						       in_len);
	}
	if (type != TLS_HANDSHAKE_TYPE_CERTIFICATE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Received unexpected handshake "
			   "message %d (expected Certificate/"
			   "ClientKeyExchange)", type);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	wpa_printf(MSG_DEBUG,
		   "TLSv1: Received Certificate (certificate_list len %lu)",
		   (unsigned long) len);

	/*
	 * opaque ASN.1Cert<2^24-1>;
	 *
	 * struct {
	 *     ASN.1Cert certificate_list<1..2^24-1>;
	 * } Certificate;
	 */

	end = pos + len;

	if (end - pos < 3) {
		wpa_printf(MSG_DEBUG, "TLSv1: Too short Certificate "
			   "(left=%lu)", (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	list_len = WPA_GET_BE24(pos);
	pos += 3;

	if ((size_t) (end - pos) != list_len) {
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected certificate_list "
			   "length (len=%lu left=%lu)",
			   (unsigned long) list_len,
			   (unsigned long) (end - pos));
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	idx = 0;
	while (pos < end) {
		if (end - pos < 3) {
			wpa_printf(MSG_DEBUG, "TLSv1: Failed to parse "
				   "certificate_list");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_DECODE_ERROR);
			x509_certificate_chain_free(chain);
			return -1;
		}

		cert_len = WPA_GET_BE24(pos);
		pos += 3;

		if ((size_t) (end - pos) < cert_len) {
			wpa_printf(MSG_DEBUG, "TLSv1: Unexpected certificate "
				   "length (len=%lu left=%lu)",
				   (unsigned long) cert_len,
				   (unsigned long) (end - pos));
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_DECODE_ERROR);
			x509_certificate_chain_free(chain);
			return -1;
		}

		wpa_printf(MSG_DEBUG, "TLSv1: Certificate %lu (len %lu)",
			   (unsigned long) idx, (unsigned long) cert_len);

		if (idx == 0) {
			crypto_public_key_free(conn->client_rsa_key);
			if (tls_parse_cert(pos, cert_len,
					   &conn->client_rsa_key)) {
				wpa_printf(MSG_DEBUG, "TLSv1: Failed to parse "
					   "the certificate");
				tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
						   TLS_ALERT_BAD_CERTIFICATE);
				x509_certificate_chain_free(chain);
				return -1;
			}
		}

		cert = x509_certificate_parse(pos, cert_len);
		if (cert == NULL) {
			wpa_printf(MSG_DEBUG, "TLSv1: Failed to parse "
				   "the certificate");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_BAD_CERTIFICATE);
			x509_certificate_chain_free(chain);
			return -1;
		}

		if (last == NULL)
			chain = cert;
		else
			last->next = cert;
		last = cert;

		idx++;
		pos += cert_len;
	}

	if (x509_certificate_chain_validate(conn->cred->trusted_certs, chain,
					    &reason) < 0) {
		int tls_reason;
		wpa_printf(MSG_DEBUG, "TLSv1: Server certificate chain "
			   "validation failed (reason=%d)", reason);
		switch (reason) {
		case X509_VALIDATE_BAD_CERTIFICATE:
			tls_reason = TLS_ALERT_BAD_CERTIFICATE;
			break;
		case X509_VALIDATE_UNSUPPORTED_CERTIFICATE:
			tls_reason = TLS_ALERT_UNSUPPORTED_CERTIFICATE;
			break;
		case X509_VALIDATE_CERTIFICATE_REVOKED:
			tls_reason = TLS_ALERT_CERTIFICATE_REVOKED;
			break;
		case X509_VALIDATE_CERTIFICATE_EXPIRED:
			tls_reason = TLS_ALERT_CERTIFICATE_EXPIRED;
			break;
		case X509_VALIDATE_CERTIFICATE_UNKNOWN:
			tls_reason = TLS_ALERT_CERTIFICATE_UNKNOWN;
			break;
		case X509_VALIDATE_UNKNOWN_CA:
			tls_reason = TLS_ALERT_UNKNOWN_CA;
			break;
		default:
			tls_reason = TLS_ALERT_BAD_CERTIFICATE;
			break;
		}
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL, tls_reason);
		x509_certificate_chain_free(chain);
		return -1;
	}

	x509_certificate_chain_free(chain);

	*in_len = end - in_data;

	conn->state = CLIENT_KEY_EXCHANGE;

	return 0;
}


static int tls_process_client_key_exchange_rsa(
	struct tlsv1_server *conn, const u8 *pos, const u8 *end)
{
	u8 *out;
	size_t outlen, outbuflen;
	u16 encr_len;
	int res;
	int use_random = 0;

	if (end - pos < 2) {
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	encr_len = WPA_GET_BE16(pos);
	pos += 2;

	outbuflen = outlen = end - pos;
	out = os_malloc(outlen >= TLS_PRE_MASTER_SECRET_LEN ?
			outlen : TLS_PRE_MASTER_SECRET_LEN);
	if (out == NULL) {
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	/*
	 * struct {
	 *   ProtocolVersion client_version;
	 *   opaque random[46];
	 * } PreMasterSecret;
	 *
	 * struct {
	 *   public-key-encrypted PreMasterSecret pre_master_secret;
	 * } EncryptedPreMasterSecret;
	 */

	/*
	 * Note: To avoid Bleichenbacher attack, we do not report decryption or
	 * parsing errors from EncryptedPreMasterSecret processing to the
	 * client. Instead, a random pre-master secret is used to force the
	 * handshake to fail.
	 */

	if (crypto_private_key_decrypt_pkcs1_v15(conn->cred->key,
						 pos, end - pos,
						 out, &outlen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to decrypt "
			   "PreMasterSecret (encr_len=%d outlen=%lu)",
			   (int) (end - pos), (unsigned long) outlen);
		use_random = 1;
	}

	if (outlen != TLS_PRE_MASTER_SECRET_LEN) {
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected PreMasterSecret "
			   "length %lu", (unsigned long) outlen);
		use_random = 1;
	}

	if (WPA_GET_BE16(out) != conn->client_version) {
		wpa_printf(MSG_DEBUG, "TLSv1: Client version in "
			   "ClientKeyExchange does not match with version in "
			   "ClientHello");
		use_random = 1;
	}

	if (use_random) {
		wpa_printf(MSG_DEBUG, "TLSv1: Using random premaster secret "
			   "to avoid revealing information about private key");
		outlen = TLS_PRE_MASTER_SECRET_LEN;
		if (os_get_random(out, outlen)) {
			wpa_printf(MSG_DEBUG, "TLSv1: Failed to get random "
				   "data");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_INTERNAL_ERROR);
			os_free(out);
			return -1;
		}
	}

	res = tlsv1_server_derive_keys(conn, out, outlen);

	/* Clear the pre-master secret since it is not needed anymore */
	os_memset(out, 0, outbuflen);
	os_free(out);

	if (res) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to derive keys");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	return 0;
}


static int tls_process_client_key_exchange_dh_anon(
	struct tlsv1_server *conn, const u8 *pos, const u8 *end)
{
	const u8 *dh_yc;
	u16 dh_yc_len;
	u8 *shared;
	size_t shared_len;
	int res;

	/*
	 * struct {
	 *   select (PublicValueEncoding) {
	 *     case implicit: struct { };
	 *     case explicit: opaque dh_Yc<1..2^16-1>;
	 *   } dh_public;
	 * } ClientDiffieHellmanPublic;
	 */

	wpa_hexdump(MSG_MSGDUMP, "TLSv1: ClientDiffieHellmanPublic",
		    pos, end - pos);

	if (end == pos) {
		wpa_printf(MSG_DEBUG, "TLSv1: Implicit public value encoding "
			   "not supported");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	if (end - pos < 3) {
		wpa_printf(MSG_DEBUG, "TLSv1: Invalid client public value "
			   "length");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	dh_yc_len = WPA_GET_BE16(pos);
	dh_yc = pos + 2;

	if (dh_yc + dh_yc_len > end) {
		wpa_printf(MSG_DEBUG, "TLSv1: Client public value overflow "
			   "(length %d)", dh_yc_len);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	wpa_hexdump(MSG_DEBUG, "TLSv1: DH Yc (client's public value)",
		    dh_yc, dh_yc_len);

	if (conn->cred == NULL || conn->cred->dh_p == NULL ||
	    conn->dh_secret == NULL) {
		wpa_printf(MSG_DEBUG, "TLSv1: No DH parameters available");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	shared_len = conn->cred->dh_p_len;
	shared = os_malloc(shared_len);
	if (shared == NULL) {
		wpa_printf(MSG_DEBUG, "TLSv1: Could not allocate memory for "
			   "DH");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	/* shared = Yc^secret mod p */
	if (crypto_mod_exp(dh_yc, dh_yc_len, conn->dh_secret,
			   conn->dh_secret_len,
			   conn->cred->dh_p, conn->cred->dh_p_len,
			   shared, &shared_len)) {
		os_free(shared);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	wpa_hexdump_key(MSG_DEBUG, "TLSv1: Shared secret from DH key exchange",
			shared, shared_len);

	os_memset(conn->dh_secret, 0, conn->dh_secret_len);
	os_free(conn->dh_secret);
	conn->dh_secret = NULL;

	res = tlsv1_server_derive_keys(conn, shared, shared_len);

	/* Clear the pre-master secret since it is not needed anymore */
	os_memset(shared, 0, shared_len);
	os_free(shared);

	if (res) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to derive keys");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	return 0;
}


static int tls_process_client_key_exchange(struct tlsv1_server *conn, u8 ct,
					   const u8 *in_data, size_t *in_len)
{
	const u8 *pos, *end;
	size_t left, len;
	u8 type;
	tls_key_exchange keyx;
	const struct tls_cipher_suite *suite;

	if (ct != TLS_CONTENT_TYPE_HANDSHAKE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected Handshake; "
			   "received content type 0x%x", ct);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4) {
		wpa_printf(MSG_DEBUG, "TLSv1: Too short ClientKeyExchange "
			   "(Left=%lu)", (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	type = *pos++;
	len = WPA_GET_BE24(pos);
	pos += 3;
	left -= 4;

	if (len > left) {
		wpa_printf(MSG_DEBUG, "TLSv1: Mismatch in ClientKeyExchange "
			   "length (len=%lu != left=%lu)",
			   (unsigned long) len, (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	end = pos + len;

	if (type != TLS_HANDSHAKE_TYPE_CLIENT_KEY_EXCHANGE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Received unexpected handshake "
			   "message %d (expected ClientKeyExchange)", type);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "TLSv1: Received ClientKeyExchange");

	wpa_hexdump(MSG_DEBUG, "TLSv1: ClientKeyExchange", pos, len);

	suite = tls_get_cipher_suite(conn->rl.cipher_suite);
	if (suite == NULL)
		keyx = TLS_KEY_X_NULL;
	else
		keyx = suite->key_exchange;

	if (keyx == TLS_KEY_X_DH_anon &&
	    tls_process_client_key_exchange_dh_anon(conn, pos, end) < 0)
		return -1;

	if (keyx != TLS_KEY_X_DH_anon &&
	    tls_process_client_key_exchange_rsa(conn, pos, end) < 0)
		return -1;

	*in_len = end - in_data;

	conn->state = CERTIFICATE_VERIFY;

	return 0;
}


static int tls_process_certificate_verify(struct tlsv1_server *conn, u8 ct,
					  const u8 *in_data, size_t *in_len)
{
	const u8 *pos, *end;
	size_t left, len;
	u8 type;
	size_t hlen, buflen;
	u8 hash[MD5_MAC_LEN + SHA1_MAC_LEN], *hpos, *buf;
	enum { SIGN_ALG_RSA, SIGN_ALG_DSA } alg = SIGN_ALG_RSA;
	u16 slen;

	if (ct == TLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC) {
		if (conn->verify_peer) {
			wpa_printf(MSG_DEBUG, "TLSv1: Client did not include "
				   "CertificateVerify");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_UNEXPECTED_MESSAGE);
			return -1;
		}

		return tls_process_change_cipher_spec(conn, ct, in_data,
						      in_len);
	}

	if (ct != TLS_CONTENT_TYPE_HANDSHAKE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected Handshake; "
			   "received content type 0x%x", ct);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4) {
		wpa_printf(MSG_DEBUG, "TLSv1: Too short CertificateVerify "
			   "message (len=%lu)", (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	type = *pos++;
	len = WPA_GET_BE24(pos);
	pos += 3;
	left -= 4;

	if (len > left) {
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected CertificateVerify "
			   "message length (len=%lu != left=%lu)",
			   (unsigned long) len, (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	end = pos + len;

	if (type != TLS_HANDSHAKE_TYPE_CERTIFICATE_VERIFY) {
		wpa_printf(MSG_DEBUG, "TLSv1: Received unexpected handshake "
			   "message %d (expected CertificateVerify)", type);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "TLSv1: Received CertificateVerify");

	/*
	 * struct {
	 *   Signature signature;
	 * } CertificateVerify;
	 */

	hpos = hash;

	if (alg == SIGN_ALG_RSA) {
		hlen = MD5_MAC_LEN;
		if (conn->verify.md5_cert == NULL ||
		    crypto_hash_finish(conn->verify.md5_cert, hpos, &hlen) < 0)
		{
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_INTERNAL_ERROR);
			conn->verify.md5_cert = NULL;
			crypto_hash_finish(conn->verify.sha1_cert, NULL, NULL);
			conn->verify.sha1_cert = NULL;
			return -1;
		}
		hpos += MD5_MAC_LEN;
	} else
		crypto_hash_finish(conn->verify.md5_cert, NULL, NULL);

	conn->verify.md5_cert = NULL;
	hlen = SHA1_MAC_LEN;
	if (conn->verify.sha1_cert == NULL ||
	    crypto_hash_finish(conn->verify.sha1_cert, hpos, &hlen) < 0) {
		conn->verify.sha1_cert = NULL;
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	conn->verify.sha1_cert = NULL;

	if (alg == SIGN_ALG_RSA)
		hlen += MD5_MAC_LEN;

	wpa_hexdump(MSG_MSGDUMP, "TLSv1: CertificateVerify hash", hash, hlen);

	if (end - pos < 2) {
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}
	slen = WPA_GET_BE16(pos);
	pos += 2;
	if (end - pos < slen) {
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	wpa_hexdump(MSG_MSGDUMP, "TLSv1: Signature", pos, end - pos);
	if (conn->client_rsa_key == NULL) {
		wpa_printf(MSG_DEBUG, "TLSv1: No client public key to verify "
			   "signature");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	buflen = end - pos;
	buf = os_malloc(end - pos);
	if (crypto_public_key_decrypt_pkcs1(conn->client_rsa_key,
					    pos, end - pos, buf, &buflen) < 0)
	{
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to decrypt signature");
		os_free(buf);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECRYPT_ERROR);
		return -1;
	}

	wpa_hexdump_key(MSG_MSGDUMP, "TLSv1: Decrypted Signature",
			buf, buflen);

	if (buflen != hlen || os_memcmp(buf, hash, buflen) != 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Invalid Signature in "
			   "CertificateVerify - did not match with calculated "
			   "hash");
		os_free(buf);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECRYPT_ERROR);
		return -1;
	}

	os_free(buf);

	*in_len = end - in_data;

	conn->state = CHANGE_CIPHER_SPEC;

	return 0;
}


static int tls_process_change_cipher_spec(struct tlsv1_server *conn,
					  u8 ct, const u8 *in_data,
					  size_t *in_len)
{
	const u8 *pos;
	size_t left;

	if (ct != TLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected ChangeCipherSpec; "
			   "received content type 0x%x", ct);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 1) {
		wpa_printf(MSG_DEBUG, "TLSv1: Too short ChangeCipherSpec");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	if (*pos != TLS_CHANGE_CIPHER_SPEC) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected ChangeCipherSpec; "
			   "received data 0x%x", *pos);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "TLSv1: Received ChangeCipherSpec");
	if (tlsv1_record_change_read_cipher(&conn->rl) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to change read cipher "
			   "for record layer");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	*in_len = pos + 1 - in_data;

	conn->state = CLIENT_FINISHED;

	return 0;
}


static int tls_process_client_finished(struct tlsv1_server *conn, u8 ct,
				       const u8 *in_data, size_t *in_len)
{
	const u8 *pos, *end;
	size_t left, len, hlen;
	u8 verify_data[TLS_VERIFY_DATA_LEN];
	u8 hash[MD5_MAC_LEN + SHA1_MAC_LEN];

	if (ct != TLS_CONTENT_TYPE_HANDSHAKE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected Finished; "
			   "received content type 0x%x", ct);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4) {
		wpa_printf(MSG_DEBUG, "TLSv1: Too short record (left=%lu) for "
			   "Finished",
			   (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	if (pos[0] != TLS_HANDSHAKE_TYPE_FINISHED) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected Finished; received "
			   "type 0x%x", pos[0]);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	len = WPA_GET_BE24(pos + 1);

	pos += 4;
	left -= 4;

	if (len > left) {
		wpa_printf(MSG_DEBUG, "TLSv1: Too short buffer for Finished "
			   "(len=%lu > left=%lu)",
			   (unsigned long) len, (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}
	end = pos + len;
	if (len != TLS_VERIFY_DATA_LEN) {
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected verify_data length "
			   "in Finished: %lu (expected %d)",
			   (unsigned long) len, TLS_VERIFY_DATA_LEN);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}
	wpa_hexdump(MSG_MSGDUMP, "TLSv1: verify_data in Finished",
		    pos, TLS_VERIFY_DATA_LEN);

	hlen = MD5_MAC_LEN;
	if (conn->verify.md5_client == NULL ||
	    crypto_hash_finish(conn->verify.md5_client, hash, &hlen) < 0) {
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		conn->verify.md5_client = NULL;
		crypto_hash_finish(conn->verify.sha1_client, NULL, NULL);
		conn->verify.sha1_client = NULL;
		return -1;
	}
	conn->verify.md5_client = NULL;
	hlen = SHA1_MAC_LEN;
	if (conn->verify.sha1_client == NULL ||
	    crypto_hash_finish(conn->verify.sha1_client, hash + MD5_MAC_LEN,
			       &hlen) < 0) {
		conn->verify.sha1_client = NULL;
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	conn->verify.sha1_client = NULL;

	if (tls_prf(conn->master_secret, TLS_MASTER_SECRET_LEN,
		    "client finished", hash, MD5_MAC_LEN + SHA1_MAC_LEN,
		    verify_data, TLS_VERIFY_DATA_LEN)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to derive verify_data");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECRYPT_ERROR);
		return -1;
	}
	wpa_hexdump_key(MSG_DEBUG, "TLSv1: verify_data (client)",
			verify_data, TLS_VERIFY_DATA_LEN);

	if (os_memcmp(pos, verify_data, TLS_VERIFY_DATA_LEN) != 0) {
		wpa_printf(MSG_INFO, "TLSv1: Mismatch in verify_data");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "TLSv1: Received Finished");

	*in_len = end - in_data;

	if (conn->use_session_ticket) {
		/* Abbreviated handshake using session ticket; RFC 4507 */
		wpa_printf(MSG_DEBUG, "TLSv1: Abbreviated handshake completed "
			   "successfully");
		conn->state = ESTABLISHED;
	} else {
		/* Full handshake */
		conn->state = SERVER_CHANGE_CIPHER_SPEC;
	}

	return 0;
}


int tlsv1_server_process_handshake(struct tlsv1_server *conn, u8 ct,
				   const u8 *buf, size_t *len)
{
	if (ct == TLS_CONTENT_TYPE_ALERT) {
		if (*len < 2) {
			wpa_printf(MSG_DEBUG, "TLSv1: Alert underflow");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_DECODE_ERROR);
			return -1;
		}
		wpa_printf(MSG_DEBUG, "TLSv1: Received alert %d:%d",
			   buf[0], buf[1]);
		*len = 2;
		conn->state = FAILED;
		return -1;
	}

	switch (conn->state) {
	case CLIENT_HELLO:
		if (tls_process_client_hello(conn, ct, buf, len))
			return -1;
		break;
	case CLIENT_CERTIFICATE:
		if (tls_process_certificate(conn, ct, buf, len))
			return -1;
		break;
	case CLIENT_KEY_EXCHANGE:
		if (tls_process_client_key_exchange(conn, ct, buf, len))
			return -1;
		break;
	case CERTIFICATE_VERIFY:
		if (tls_process_certificate_verify(conn, ct, buf, len))
			return -1;
		break;
	case CHANGE_CIPHER_SPEC:
		if (tls_process_change_cipher_spec(conn, ct, buf, len))
			return -1;
		break;
	case CLIENT_FINISHED:
		if (tls_process_client_finished(conn, ct, buf, len))
			return -1;
		break;
	default:
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected state %d "
			   "while processing received message",
			   conn->state);
		return -1;
	}

	if (ct == TLS_CONTENT_TYPE_HANDSHAKE)
		tls_verify_hash_add(&conn->verify, buf, *len);

	return 0;
}
