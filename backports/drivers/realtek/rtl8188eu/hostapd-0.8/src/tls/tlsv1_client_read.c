/*
 * TLSv1 client - read handshake message
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
#include "tlsv1_client.h"
#include "tlsv1_client_i.h"

static int tls_process_server_key_exchange(struct tlsv1_client *conn, u8 ct,
					   const u8 *in_data, size_t *in_len);
static int tls_process_certificate_request(struct tlsv1_client *conn, u8 ct,
					   const u8 *in_data, size_t *in_len);
static int tls_process_server_hello_done(struct tlsv1_client *conn, u8 ct,
					 const u8 *in_data, size_t *in_len);


static int tls_process_server_hello(struct tlsv1_client *conn, u8 ct,
				    const u8 *in_data, size_t *in_len)
{
	const u8 *pos, *end;
	size_t left, len, i;
	u16 cipher_suite;

	if (ct != TLS_CONTENT_TYPE_HANDSHAKE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected Handshake; "
			   "received content type 0x%x", ct);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4)
		goto decode_error;

	/* HandshakeType msg_type */
	if (*pos != TLS_HANDSHAKE_TYPE_SERVER_HELLO) {
		wpa_printf(MSG_DEBUG, "TLSv1: Received unexpected handshake "
			   "message %d (expected ServerHello)", *pos);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}
	wpa_printf(MSG_DEBUG, "TLSv1: Received ServerHello");
	pos++;
	/* uint24 length */
	len = WPA_GET_BE24(pos);
	pos += 3;
	left -= 4;

	if (len > left)
		goto decode_error;

	/* body - ServerHello */

	wpa_hexdump(MSG_MSGDUMP, "TLSv1: ServerHello", pos, len);
	end = pos + len;

	/* ProtocolVersion server_version */
	if (end - pos < 2)
		goto decode_error;
	if (WPA_GET_BE16(pos) != TLS_VERSION) {
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected protocol version in "
			   "ServerHello");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_PROTOCOL_VERSION);
		return -1;
	}
	pos += 2;

	/* Random random */
	if (end - pos < TLS_RANDOM_LEN)
		goto decode_error;

	os_memcpy(conn->server_random, pos, TLS_RANDOM_LEN);
	pos += TLS_RANDOM_LEN;
	wpa_hexdump(MSG_MSGDUMP, "TLSv1: server_random",
		    conn->server_random, TLS_RANDOM_LEN);

	/* SessionID session_id */
	if (end - pos < 1)
		goto decode_error;
	if (end - pos < 1 + *pos || *pos > TLS_SESSION_ID_MAX_LEN)
		goto decode_error;
	if (conn->session_id_len && conn->session_id_len == *pos &&
	    os_memcmp(conn->session_id, pos + 1, conn->session_id_len) == 0) {
		pos += 1 + conn->session_id_len;
		wpa_printf(MSG_DEBUG, "TLSv1: Resuming old session");
		conn->session_resumed = 1;
	} else {
		conn->session_id_len = *pos;
		pos++;
		os_memcpy(conn->session_id, pos, conn->session_id_len);
		pos += conn->session_id_len;
	}
	wpa_hexdump(MSG_MSGDUMP, "TLSv1: session_id",
		    conn->session_id, conn->session_id_len);

	/* CipherSuite cipher_suite */
	if (end - pos < 2)
		goto decode_error;
	cipher_suite = WPA_GET_BE16(pos);
	pos += 2;
	for (i = 0; i < conn->num_cipher_suites; i++) {
		if (cipher_suite == conn->cipher_suites[i])
			break;
	}
	if (i == conn->num_cipher_suites) {
		wpa_printf(MSG_INFO, "TLSv1: Server selected unexpected "
			   "cipher suite 0x%04x", cipher_suite);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_ILLEGAL_PARAMETER);
		return -1;
	}

	if (conn->session_resumed && cipher_suite != conn->prev_cipher_suite) {
		wpa_printf(MSG_DEBUG, "TLSv1: Server selected a different "
			   "cipher suite for a resumed connection (0x%04x != "
			   "0x%04x)", cipher_suite, conn->prev_cipher_suite);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_ILLEGAL_PARAMETER);
		return -1;
	}

	if (tlsv1_record_set_cipher_suite(&conn->rl, cipher_suite) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to set CipherSuite for "
			   "record layer");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	conn->prev_cipher_suite = cipher_suite;

	/* CompressionMethod compression_method */
	if (end - pos < 1)
		goto decode_error;
	if (*pos != TLS_COMPRESSION_NULL) {
		wpa_printf(MSG_INFO, "TLSv1: Server selected unexpected "
			   "compression 0x%02x", *pos);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_ILLEGAL_PARAMETER);
		return -1;
	}
	pos++;

	if (end != pos) {
		/* TODO: ServerHello extensions */
		wpa_hexdump(MSG_DEBUG, "TLSv1: Unexpected extra data in the "
			    "end of ServerHello", pos, end - pos);
		goto decode_error;
	}

	if (conn->session_ticket_included && conn->session_ticket_cb) {
		/* TODO: include SessionTicket extension if one was included in
		 * ServerHello */
		int res = conn->session_ticket_cb(
			conn->session_ticket_cb_ctx, NULL, 0,
			conn->client_random, conn->server_random,
			conn->master_secret);
		if (res < 0) {
			wpa_printf(MSG_DEBUG, "TLSv1: SessionTicket callback "
				   "indicated failure");
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
				  TLS_ALERT_HANDSHAKE_FAILURE);
			return -1;
		}
		conn->use_session_ticket = !!res;
	}

	if ((conn->session_resumed || conn->use_session_ticket) &&
	    tls_derive_keys(conn, NULL, 0)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to derive keys");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	*in_len = end - in_data;

	conn->state = (conn->session_resumed || conn->use_session_ticket) ?
		SERVER_CHANGE_CIPHER_SPEC : SERVER_CERTIFICATE;

	return 0;

decode_error:
	wpa_printf(MSG_DEBUG, "TLSv1: Failed to decode ServerHello");
	tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
	return -1;
}


static int tls_process_certificate(struct tlsv1_client *conn, u8 ct,
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
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4) {
		wpa_printf(MSG_DEBUG, "TLSv1: Too short Certificate message "
			   "(len=%lu)", (unsigned long) left);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
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
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	if (type == TLS_HANDSHAKE_TYPE_SERVER_KEY_EXCHANGE)
		return tls_process_server_key_exchange(conn, ct, in_data,
						       in_len);
	if (type == TLS_HANDSHAKE_TYPE_CERTIFICATE_REQUEST)
		return tls_process_certificate_request(conn, ct, in_data,
						       in_len);
	if (type == TLS_HANDSHAKE_TYPE_SERVER_HELLO_DONE)
		return tls_process_server_hello_done(conn, ct, in_data,
						     in_len);
	if (type != TLS_HANDSHAKE_TYPE_CERTIFICATE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Received unexpected handshake "
			   "message %d (expected Certificate/"
			   "ServerKeyExchange/CertificateRequest/"
			   "ServerHelloDone)", type);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
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
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	list_len = WPA_GET_BE24(pos);
	pos += 3;

	if ((size_t) (end - pos) != list_len) {
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected certificate_list "
			   "length (len=%lu left=%lu)",
			   (unsigned long) list_len,
			   (unsigned long) (end - pos));
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	idx = 0;
	while (pos < end) {
		if (end - pos < 3) {
			wpa_printf(MSG_DEBUG, "TLSv1: Failed to parse "
				   "certificate_list");
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
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
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
				  TLS_ALERT_DECODE_ERROR);
			x509_certificate_chain_free(chain);
			return -1;
		}

		wpa_printf(MSG_DEBUG, "TLSv1: Certificate %lu (len %lu)",
			   (unsigned long) idx, (unsigned long) cert_len);

		if (idx == 0) {
			crypto_public_key_free(conn->server_rsa_key);
			if (tls_parse_cert(pos, cert_len,
					   &conn->server_rsa_key)) {
				wpa_printf(MSG_DEBUG, "TLSv1: Failed to parse "
					   "the certificate");
				tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
					  TLS_ALERT_BAD_CERTIFICATE);
				x509_certificate_chain_free(chain);
				return -1;
			}
		}

		cert = x509_certificate_parse(pos, cert_len);
		if (cert == NULL) {
			wpa_printf(MSG_DEBUG, "TLSv1: Failed to parse "
				   "the certificate");
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
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

	if (conn->cred &&
	    x509_certificate_chain_validate(conn->cred->trusted_certs, chain,
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
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, tls_reason);
		x509_certificate_chain_free(chain);
		return -1;
	}

	x509_certificate_chain_free(chain);

	*in_len = end - in_data;

	conn->state = SERVER_KEY_EXCHANGE;

	return 0;
}


static int tlsv1_process_diffie_hellman(struct tlsv1_client *conn,
					const u8 *buf, size_t len)
{
	const u8 *pos, *end;

	tlsv1_client_free_dh(conn);

	pos = buf;
	end = buf + len;

	if (end - pos < 3)
		goto fail;
	conn->dh_p_len = WPA_GET_BE16(pos);
	pos += 2;
	if (conn->dh_p_len == 0 || end - pos < (int) conn->dh_p_len) {
		wpa_printf(MSG_DEBUG, "TLSv1: Invalid dh_p length %lu",
			   (unsigned long) conn->dh_p_len);
		goto fail;
	}
	conn->dh_p = os_malloc(conn->dh_p_len);
	if (conn->dh_p == NULL)
		goto fail;
	os_memcpy(conn->dh_p, pos, conn->dh_p_len);
	pos += conn->dh_p_len;
	wpa_hexdump(MSG_DEBUG, "TLSv1: DH p (prime)",
		    conn->dh_p, conn->dh_p_len);

	if (end - pos < 3)
		goto fail;
	conn->dh_g_len = WPA_GET_BE16(pos);
	pos += 2;
	if (conn->dh_g_len == 0 || end - pos < (int) conn->dh_g_len)
		goto fail;
	conn->dh_g = os_malloc(conn->dh_g_len);
	if (conn->dh_g == NULL)
		goto fail;
	os_memcpy(conn->dh_g, pos, conn->dh_g_len);
	pos += conn->dh_g_len;
	wpa_hexdump(MSG_DEBUG, "TLSv1: DH g (generator)",
		    conn->dh_g, conn->dh_g_len);
	if (conn->dh_g_len == 1 && conn->dh_g[0] < 2)
		goto fail;

	if (end - pos < 3)
		goto fail;
	conn->dh_ys_len = WPA_GET_BE16(pos);
	pos += 2;
	if (conn->dh_ys_len == 0 || end - pos < (int) conn->dh_ys_len)
		goto fail;
	conn->dh_ys = os_malloc(conn->dh_ys_len);
	if (conn->dh_ys == NULL)
		goto fail;
	os_memcpy(conn->dh_ys, pos, conn->dh_ys_len);
	pos += conn->dh_ys_len;
	wpa_hexdump(MSG_DEBUG, "TLSv1: DH Ys (server's public value)",
		    conn->dh_ys, conn->dh_ys_len);

	return 0;

fail:
	wpa_printf(MSG_DEBUG, "TLSv1: Processing DH params failed");
	tlsv1_client_free_dh(conn);
	return -1;
}


static int tls_process_server_key_exchange(struct tlsv1_client *conn, u8 ct,
					   const u8 *in_data, size_t *in_len)
{
	const u8 *pos, *end;
	size_t left, len;
	u8 type;
	const struct tls_cipher_suite *suite;

	if (ct != TLS_CONTENT_TYPE_HANDSHAKE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected Handshake; "
			   "received content type 0x%x", ct);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4) {
		wpa_printf(MSG_DEBUG, "TLSv1: Too short ServerKeyExchange "
			   "(Left=%lu)", (unsigned long) left);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	type = *pos++;
	len = WPA_GET_BE24(pos);
	pos += 3;
	left -= 4;

	if (len > left) {
		wpa_printf(MSG_DEBUG, "TLSv1: Mismatch in ServerKeyExchange "
			   "length (len=%lu != left=%lu)",
			   (unsigned long) len, (unsigned long) left);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	end = pos + len;

	if (type == TLS_HANDSHAKE_TYPE_CERTIFICATE_REQUEST)
		return tls_process_certificate_request(conn, ct, in_data,
						       in_len);
	if (type == TLS_HANDSHAKE_TYPE_SERVER_HELLO_DONE)
		return tls_process_server_hello_done(conn, ct, in_data,
						     in_len);
	if (type != TLS_HANDSHAKE_TYPE_SERVER_KEY_EXCHANGE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Received unexpected handshake "
			   "message %d (expected ServerKeyExchange/"
			   "CertificateRequest/ServerHelloDone)", type);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "TLSv1: Received ServerKeyExchange");

	if (!tls_server_key_exchange_allowed(conn->rl.cipher_suite)) {
		wpa_printf(MSG_DEBUG, "TLSv1: ServerKeyExchange not allowed "
			   "with the selected cipher suite");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	wpa_hexdump(MSG_DEBUG, "TLSv1: ServerKeyExchange", pos, len);
	suite = tls_get_cipher_suite(conn->rl.cipher_suite);
	if (suite && suite->key_exchange == TLS_KEY_X_DH_anon) {
		if (tlsv1_process_diffie_hellman(conn, pos, len) < 0) {
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
				  TLS_ALERT_DECODE_ERROR);
			return -1;
		}
	} else {
		wpa_printf(MSG_DEBUG, "TLSv1: UnexpectedServerKeyExchange");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	*in_len = end - in_data;

	conn->state = SERVER_CERTIFICATE_REQUEST;

	return 0;
}


static int tls_process_certificate_request(struct tlsv1_client *conn, u8 ct,
					   const u8 *in_data, size_t *in_len)
{
	const u8 *pos, *end;
	size_t left, len;
	u8 type;

	if (ct != TLS_CONTENT_TYPE_HANDSHAKE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected Handshake; "
			   "received content type 0x%x", ct);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4) {
		wpa_printf(MSG_DEBUG, "TLSv1: Too short CertificateRequest "
			   "(left=%lu)", (unsigned long) left);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	type = *pos++;
	len = WPA_GET_BE24(pos);
	pos += 3;
	left -= 4;

	if (len > left) {
		wpa_printf(MSG_DEBUG, "TLSv1: Mismatch in CertificateRequest "
			   "length (len=%lu != left=%lu)",
			   (unsigned long) len, (unsigned long) left);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	end = pos + len;

	if (type == TLS_HANDSHAKE_TYPE_SERVER_HELLO_DONE)
		return tls_process_server_hello_done(conn, ct, in_data,
						     in_len);
	if (type != TLS_HANDSHAKE_TYPE_CERTIFICATE_REQUEST) {
		wpa_printf(MSG_DEBUG, "TLSv1: Received unexpected handshake "
			   "message %d (expected CertificateRequest/"
			   "ServerHelloDone)", type);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "TLSv1: Received CertificateRequest");

	conn->certificate_requested = 1;

	*in_len = end - in_data;

	conn->state = SERVER_HELLO_DONE;

	return 0;
}


static int tls_process_server_hello_done(struct tlsv1_client *conn, u8 ct,
					 const u8 *in_data, size_t *in_len)
{
	const u8 *pos, *end;
	size_t left, len;
	u8 type;

	if (ct != TLS_CONTENT_TYPE_HANDSHAKE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected Handshake; "
			   "received content type 0x%x", ct);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4) {
		wpa_printf(MSG_DEBUG, "TLSv1: Too short ServerHelloDone "
			   "(left=%lu)", (unsigned long) left);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	type = *pos++;
	len = WPA_GET_BE24(pos);
	pos += 3;
	left -= 4;

	if (len > left) {
		wpa_printf(MSG_DEBUG, "TLSv1: Mismatch in ServerHelloDone "
			   "length (len=%lu != left=%lu)",
			   (unsigned long) len, (unsigned long) left);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return -1;
	}
	end = pos + len;

	if (type != TLS_HANDSHAKE_TYPE_SERVER_HELLO_DONE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Received unexpected handshake "
			   "message %d (expected ServerHelloDone)", type);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "TLSv1: Received ServerHelloDone");

	*in_len = end - in_data;

	conn->state = CLIENT_KEY_EXCHANGE;

	return 0;
}


static int tls_process_server_change_cipher_spec(struct tlsv1_client *conn,
						 u8 ct, const u8 *in_data,
						 size_t *in_len)
{
	const u8 *pos;
	size_t left;

	if (ct != TLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected ChangeCipherSpec; "
			   "received content type 0x%x", ct);
		if (conn->use_session_ticket) {
			int res;
			wpa_printf(MSG_DEBUG, "TLSv1: Server may have "
				   "rejected SessionTicket");
			conn->use_session_ticket = 0;

			/* Notify upper layers that SessionTicket failed */
			res = conn->session_ticket_cb(
				conn->session_ticket_cb_ctx, NULL, 0, NULL,
				NULL, NULL);
			if (res < 0) {
				wpa_printf(MSG_DEBUG, "TLSv1: SessionTicket "
					   "callback indicated failure");
				tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
					  TLS_ALERT_HANDSHAKE_FAILURE);
				return -1;
			}

			conn->state = SERVER_CERTIFICATE;
			return tls_process_certificate(conn, ct, in_data,
						       in_len);
		}
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 1) {
		wpa_printf(MSG_DEBUG, "TLSv1: Too short ChangeCipherSpec");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	if (*pos != TLS_CHANGE_CIPHER_SPEC) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected ChangeCipherSpec; "
			   "received data 0x%x", *pos);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "TLSv1: Received ChangeCipherSpec");
	if (tlsv1_record_change_read_cipher(&conn->rl) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to change read cipher "
			   "for record layer");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	*in_len = pos + 1 - in_data;

	conn->state = SERVER_FINISHED;

	return 0;
}


static int tls_process_server_finished(struct tlsv1_client *conn, u8 ct,
				       const u8 *in_data, size_t *in_len)
{
	const u8 *pos, *end;
	size_t left, len, hlen;
	u8 verify_data[TLS_VERIFY_DATA_LEN];
	u8 hash[MD5_MAC_LEN + SHA1_MAC_LEN];

	if (ct != TLS_CONTENT_TYPE_HANDSHAKE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected Finished; "
			   "received content type 0x%x", ct);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4) {
		wpa_printf(MSG_DEBUG, "TLSv1: Too short record (left=%lu) for "
			   "Finished",
			   (unsigned long) left);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	if (pos[0] != TLS_HANDSHAKE_TYPE_FINISHED) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected Finished; received "
			   "type 0x%x", pos[0]);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
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
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_DECODE_ERROR);
		return -1;
	}
	end = pos + len;
	if (len != TLS_VERIFY_DATA_LEN) {
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected verify_data length "
			   "in Finished: %lu (expected %d)",
			   (unsigned long) len, TLS_VERIFY_DATA_LEN);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_DECODE_ERROR);
		return -1;
	}
	wpa_hexdump(MSG_MSGDUMP, "TLSv1: verify_data in Finished",
		    pos, TLS_VERIFY_DATA_LEN);

	hlen = MD5_MAC_LEN;
	if (conn->verify.md5_server == NULL ||
	    crypto_hash_finish(conn->verify.md5_server, hash, &hlen) < 0) {
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		conn->verify.md5_server = NULL;
		crypto_hash_finish(conn->verify.sha1_server, NULL, NULL);
		conn->verify.sha1_server = NULL;
		return -1;
	}
	conn->verify.md5_server = NULL;
	hlen = SHA1_MAC_LEN;
	if (conn->verify.sha1_server == NULL ||
	    crypto_hash_finish(conn->verify.sha1_server, hash + MD5_MAC_LEN,
			       &hlen) < 0) {
		conn->verify.sha1_server = NULL;
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	conn->verify.sha1_server = NULL;

	if (tls_prf(conn->master_secret, TLS_MASTER_SECRET_LEN,
		    "server finished", hash, MD5_MAC_LEN + SHA1_MAC_LEN,
		    verify_data, TLS_VERIFY_DATA_LEN)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to derive verify_data");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_DECRYPT_ERROR);
		return -1;
	}
	wpa_hexdump_key(MSG_DEBUG, "TLSv1: verify_data (server)",
			verify_data, TLS_VERIFY_DATA_LEN);

	if (os_memcmp(pos, verify_data, TLS_VERIFY_DATA_LEN) != 0) {
		wpa_printf(MSG_INFO, "TLSv1: Mismatch in verify_data");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "TLSv1: Received Finished");

	*in_len = end - in_data;

	conn->state = (conn->session_resumed || conn->use_session_ticket) ?
		CHANGE_CIPHER_SPEC : ACK_FINISHED;

	return 0;
}


static int tls_process_application_data(struct tlsv1_client *conn, u8 ct,
					const u8 *in_data, size_t *in_len,
					u8 **out_data, size_t *out_len)
{
	const u8 *pos;
	size_t left;

	if (ct != TLS_CONTENT_TYPE_APPLICATION_DATA) {
		wpa_printf(MSG_DEBUG, "TLSv1: Expected Application Data; "
			   "received content type 0x%x", ct);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	wpa_hexdump(MSG_DEBUG, "TLSv1: Application Data included in Handshake",
		    pos, left);

	*out_data = os_malloc(left);
	if (*out_data) {
		os_memcpy(*out_data, pos, left);
		*out_len = left;
	}

	return 0;
}


int tlsv1_client_process_handshake(struct tlsv1_client *conn, u8 ct,
				   const u8 *buf, size_t *len,
				   u8 **out_data, size_t *out_len)
{
	if (ct == TLS_CONTENT_TYPE_ALERT) {
		if (*len < 2) {
			wpa_printf(MSG_DEBUG, "TLSv1: Alert underflow");
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
				  TLS_ALERT_DECODE_ERROR);
			return -1;
		}
		wpa_printf(MSG_DEBUG, "TLSv1: Received alert %d:%d",
			   buf[0], buf[1]);
		*len = 2;
		conn->state = FAILED;
		return -1;
	}

	if (ct == TLS_CONTENT_TYPE_HANDSHAKE && *len >= 4 &&
	    buf[0] == TLS_HANDSHAKE_TYPE_HELLO_REQUEST) {
		size_t hr_len = WPA_GET_BE24(buf + 1);
		if (hr_len > *len - 4) {
			wpa_printf(MSG_DEBUG, "TLSv1: HelloRequest underflow");
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
				  TLS_ALERT_DECODE_ERROR);
			return -1;
		}
		wpa_printf(MSG_DEBUG, "TLSv1: Ignored HelloRequest");
		*len = 4 + hr_len;
		return 0;
	}

	switch (conn->state) {
	case SERVER_HELLO:
		if (tls_process_server_hello(conn, ct, buf, len))
			return -1;
		break;
	case SERVER_CERTIFICATE:
		if (tls_process_certificate(conn, ct, buf, len))
			return -1;
		break;
	case SERVER_KEY_EXCHANGE:
		if (tls_process_server_key_exchange(conn, ct, buf, len))
			return -1;
		break;
	case SERVER_CERTIFICATE_REQUEST:
		if (tls_process_certificate_request(conn, ct, buf, len))
			return -1;
		break;
	case SERVER_HELLO_DONE:
		if (tls_process_server_hello_done(conn, ct, buf, len))
			return -1;
		break;
	case SERVER_CHANGE_CIPHER_SPEC:
		if (tls_process_server_change_cipher_spec(conn, ct, buf, len))
			return -1;
		break;
	case SERVER_FINISHED:
		if (tls_process_server_finished(conn, ct, buf, len))
			return -1;
		break;
	case ACK_FINISHED:
		if (out_data &&
		    tls_process_application_data(conn, ct, buf, len, out_data,
						 out_len))
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
