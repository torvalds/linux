/*
 * TLSv1 server - write handshake message
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
#include "crypto/random.h"
#include "x509v3.h"
#include "tlsv1_common.h"
#include "tlsv1_record.h"
#include "tlsv1_server.h"
#include "tlsv1_server_i.h"


static size_t tls_server_cert_chain_der_len(struct tlsv1_server *conn)
{
	size_t len = 0;
	struct x509_certificate *cert;

	cert = conn->cred->cert;
	while (cert) {
		len += 3 + cert->cert_len;
		if (x509_certificate_self_signed(cert))
			break;
		cert = x509_certificate_get_subject(conn->cred->trusted_certs,
						    &cert->issuer);
	}

	return len;
}


static int tls_write_server_hello(struct tlsv1_server *conn,
				  u8 **msgpos, u8 *end)
{
	u8 *pos, *rhdr, *hs_start, *hs_length;
	struct os_time now;
	size_t rlen;

	pos = *msgpos;

	wpa_printf(MSG_DEBUG, "TLSv1: Send ServerHello");
	rhdr = pos;
	pos += TLS_RECORD_HEADER_LEN;

	os_get_time(&now);
	WPA_PUT_BE32(conn->server_random, now.sec);
	if (random_get_bytes(conn->server_random + 4, TLS_RANDOM_LEN - 4)) {
		wpa_printf(MSG_ERROR, "TLSv1: Could not generate "
			   "server_random");
		return -1;
	}
	wpa_hexdump(MSG_MSGDUMP, "TLSv1: server_random",
		    conn->server_random, TLS_RANDOM_LEN);

	conn->session_id_len = TLS_SESSION_ID_MAX_LEN;
	if (random_get_bytes(conn->session_id, conn->session_id_len)) {
		wpa_printf(MSG_ERROR, "TLSv1: Could not generate "
			   "session_id");
		return -1;
	}
	wpa_hexdump(MSG_MSGDUMP, "TLSv1: session_id",
		    conn->session_id, conn->session_id_len);

	/* opaque fragment[TLSPlaintext.length] */

	/* Handshake */
	hs_start = pos;
	/* HandshakeType msg_type */
	*pos++ = TLS_HANDSHAKE_TYPE_SERVER_HELLO;
	/* uint24 length (to be filled) */
	hs_length = pos;
	pos += 3;
	/* body - ServerHello */
	/* ProtocolVersion server_version */
	WPA_PUT_BE16(pos, TLS_VERSION);
	pos += 2;
	/* Random random: uint32 gmt_unix_time, opaque random_bytes */
	os_memcpy(pos, conn->server_random, TLS_RANDOM_LEN);
	pos += TLS_RANDOM_LEN;
	/* SessionID session_id */
	*pos++ = conn->session_id_len;
	os_memcpy(pos, conn->session_id, conn->session_id_len);
	pos += conn->session_id_len;
	/* CipherSuite cipher_suite */
	WPA_PUT_BE16(pos, conn->cipher_suite);
	pos += 2;
	/* CompressionMethod compression_method */
	*pos++ = TLS_COMPRESSION_NULL;

	if (conn->session_ticket && conn->session_ticket_cb) {
		int res = conn->session_ticket_cb(
			conn->session_ticket_cb_ctx,
			conn->session_ticket, conn->session_ticket_len,
			conn->client_random, conn->server_random,
			conn->master_secret);
		if (res < 0) {
			wpa_printf(MSG_DEBUG, "TLSv1: SessionTicket callback "
				   "indicated failure");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_HANDSHAKE_FAILURE);
			return -1;
		}
		conn->use_session_ticket = res;

		if (conn->use_session_ticket) {
			if (tlsv1_server_derive_keys(conn, NULL, 0) < 0) {
				wpa_printf(MSG_DEBUG, "TLSv1: Failed to "
					   "derive keys");
				tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
						   TLS_ALERT_INTERNAL_ERROR);
				return -1;
			}
		}

		/*
		 * RFC 4507 specifies that server would include an empty
		 * SessionTicket extension in ServerHello and a
		 * NewSessionTicket message after the ServerHello. However,
		 * EAP-FAST (RFC 4851), i.e., the only user of SessionTicket
		 * extension at the moment, does not use such extensions.
		 *
		 * TODO: Add support for configuring RFC 4507 behavior and make
		 * EAP-FAST disable it.
		 */
	}

	WPA_PUT_BE24(hs_length, pos - hs_length - 3);
	tls_verify_hash_add(&conn->verify, hs_start, pos - hs_start);

	if (tlsv1_record_send(&conn->rl, TLS_CONTENT_TYPE_HANDSHAKE,
			      rhdr, end - rhdr, pos - hs_start, &rlen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to create TLS record");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	pos = rhdr + rlen;

	*msgpos = pos;

	return 0;
}


static int tls_write_server_certificate(struct tlsv1_server *conn,
					u8 **msgpos, u8 *end)
{
	u8 *pos, *rhdr, *hs_start, *hs_length, *cert_start;
	size_t rlen;
	struct x509_certificate *cert;
	const struct tls_cipher_suite *suite;

	suite = tls_get_cipher_suite(conn->rl.cipher_suite);
	if (suite && suite->key_exchange == TLS_KEY_X_DH_anon) {
		wpa_printf(MSG_DEBUG, "TLSv1: Do not send Certificate when "
			   "using anonymous DH");
		return 0;
	}

	pos = *msgpos;

	wpa_printf(MSG_DEBUG, "TLSv1: Send Certificate");
	rhdr = pos;
	pos += TLS_RECORD_HEADER_LEN;

	/* opaque fragment[TLSPlaintext.length] */

	/* Handshake */
	hs_start = pos;
	/* HandshakeType msg_type */
	*pos++ = TLS_HANDSHAKE_TYPE_CERTIFICATE;
	/* uint24 length (to be filled) */
	hs_length = pos;
	pos += 3;
	/* body - Certificate */
	/* uint24 length (to be filled) */
	cert_start = pos;
	pos += 3;
	cert = conn->cred->cert;
	while (cert) {
		if (pos + 3 + cert->cert_len > end) {
			wpa_printf(MSG_DEBUG, "TLSv1: Not enough buffer space "
				   "for Certificate (cert_len=%lu left=%lu)",
				   (unsigned long) cert->cert_len,
				   (unsigned long) (end - pos));
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_INTERNAL_ERROR);
			return -1;
		}
		WPA_PUT_BE24(pos, cert->cert_len);
		pos += 3;
		os_memcpy(pos, cert->cert_start, cert->cert_len);
		pos += cert->cert_len;

		if (x509_certificate_self_signed(cert))
			break;
		cert = x509_certificate_get_subject(conn->cred->trusted_certs,
						    &cert->issuer);
	}
	if (cert == conn->cred->cert || cert == NULL) {
		/*
		 * Server was not configured with all the needed certificates
		 * to form a full certificate chain. The client may fail to
		 * validate the chain unless it is configured with all the
		 * missing CA certificates.
		 */
		wpa_printf(MSG_DEBUG, "TLSv1: Full server certificate chain "
			   "not configured - validation may fail");
	}
	WPA_PUT_BE24(cert_start, pos - cert_start - 3);

	WPA_PUT_BE24(hs_length, pos - hs_length - 3);

	if (tlsv1_record_send(&conn->rl, TLS_CONTENT_TYPE_HANDSHAKE,
			      rhdr, end - rhdr, pos - hs_start, &rlen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to generate a record");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	pos = rhdr + rlen;

	tls_verify_hash_add(&conn->verify, hs_start, pos - hs_start);

	*msgpos = pos;

	return 0;
}


static int tls_write_server_key_exchange(struct tlsv1_server *conn,
					 u8 **msgpos, u8 *end)
{
	tls_key_exchange keyx;
	const struct tls_cipher_suite *suite;
	u8 *pos, *rhdr, *hs_start, *hs_length;
	size_t rlen;
	u8 *dh_ys;
	size_t dh_ys_len;

	suite = tls_get_cipher_suite(conn->rl.cipher_suite);
	if (suite == NULL)
		keyx = TLS_KEY_X_NULL;
	else
		keyx = suite->key_exchange;

	if (!tls_server_key_exchange_allowed(conn->rl.cipher_suite)) {
		wpa_printf(MSG_DEBUG, "TLSv1: No ServerKeyExchange needed");
		return 0;
	}

	if (keyx != TLS_KEY_X_DH_anon) {
		/* TODO? */
		wpa_printf(MSG_DEBUG, "TLSv1: ServerKeyExchange not yet "
			   "supported with key exchange type %d", keyx);
		return -1;
	}

	if (conn->cred == NULL || conn->cred->dh_p == NULL ||
	    conn->cred->dh_g == NULL) {
		wpa_printf(MSG_DEBUG, "TLSv1: No DH parameters available for "
			   "ServerKeyExhcange");
		return -1;
	}

	os_free(conn->dh_secret);
	conn->dh_secret_len = conn->cred->dh_p_len;
	conn->dh_secret = os_malloc(conn->dh_secret_len);
	if (conn->dh_secret == NULL) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to allocate "
			   "memory for secret (Diffie-Hellman)");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	if (random_get_bytes(conn->dh_secret, conn->dh_secret_len)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to get random "
			   "data for Diffie-Hellman");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		os_free(conn->dh_secret);
		conn->dh_secret = NULL;
		return -1;
	}

	if (os_memcmp(conn->dh_secret, conn->cred->dh_p, conn->dh_secret_len) >
	    0)
		conn->dh_secret[0] = 0; /* make sure secret < p */

	pos = conn->dh_secret;
	while (pos + 1 < conn->dh_secret + conn->dh_secret_len && *pos == 0)
		pos++;
	if (pos != conn->dh_secret) {
		os_memmove(conn->dh_secret, pos,
			   conn->dh_secret_len - (pos - conn->dh_secret));
		conn->dh_secret_len -= pos - conn->dh_secret;
	}
	wpa_hexdump_key(MSG_DEBUG, "TLSv1: DH server's secret value",
			conn->dh_secret, conn->dh_secret_len);

	/* Ys = g^secret mod p */
	dh_ys_len = conn->cred->dh_p_len;
	dh_ys = os_malloc(dh_ys_len);
	if (dh_ys == NULL) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to allocate memory for "
			   "Diffie-Hellman");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	if (crypto_mod_exp(conn->cred->dh_g, conn->cred->dh_g_len,
			   conn->dh_secret, conn->dh_secret_len,
			   conn->cred->dh_p, conn->cred->dh_p_len,
			   dh_ys, &dh_ys_len)) {
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		os_free(dh_ys);
		return -1;
	}

	wpa_hexdump(MSG_DEBUG, "TLSv1: DH Ys (server's public value)",
		    dh_ys, dh_ys_len);

	/*
	 * struct {
	 *    select (KeyExchangeAlgorithm) {
	 *       case diffie_hellman:
	 *          ServerDHParams params;
	 *          Signature signed_params;
	 *       case rsa:
	 *          ServerRSAParams params;
	 *          Signature signed_params;
	 *    };
	 * } ServerKeyExchange;
	 *
	 * struct {
	 *    opaque dh_p<1..2^16-1>;
	 *    opaque dh_g<1..2^16-1>;
	 *    opaque dh_Ys<1..2^16-1>;
	 * } ServerDHParams;
	 */

	pos = *msgpos;

	wpa_printf(MSG_DEBUG, "TLSv1: Send ServerKeyExchange");
	rhdr = pos;
	pos += TLS_RECORD_HEADER_LEN;

	/* opaque fragment[TLSPlaintext.length] */

	/* Handshake */
	hs_start = pos;
	/* HandshakeType msg_type */
	*pos++ = TLS_HANDSHAKE_TYPE_SERVER_KEY_EXCHANGE;
	/* uint24 length (to be filled) */
	hs_length = pos;
	pos += 3;

	/* body - ServerDHParams */
	/* dh_p */
	if (pos + 2 + conn->cred->dh_p_len > end) {
		wpa_printf(MSG_DEBUG, "TLSv1: Not enough buffer space for "
			   "dh_p");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		os_free(dh_ys);
		return -1;
	}
	WPA_PUT_BE16(pos, conn->cred->dh_p_len);
	pos += 2;
	os_memcpy(pos, conn->cred->dh_p, conn->cred->dh_p_len);
	pos += conn->cred->dh_p_len;

	/* dh_g */
	if (pos + 2 + conn->cred->dh_g_len > end) {
		wpa_printf(MSG_DEBUG, "TLSv1: Not enough buffer space for "
			   "dh_g");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		os_free(dh_ys);
		return -1;
	}
	WPA_PUT_BE16(pos, conn->cred->dh_g_len);
	pos += 2;
	os_memcpy(pos, conn->cred->dh_g, conn->cred->dh_g_len);
	pos += conn->cred->dh_g_len;

	/* dh_Ys */
	if (pos + 2 + dh_ys_len > end) {
		wpa_printf(MSG_DEBUG, "TLSv1: Not enough buffer space for "
			   "dh_Ys");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		os_free(dh_ys);
		return -1;
	}
	WPA_PUT_BE16(pos, dh_ys_len);
	pos += 2;
	os_memcpy(pos, dh_ys, dh_ys_len);
	pos += dh_ys_len;
	os_free(dh_ys);

	WPA_PUT_BE24(hs_length, pos - hs_length - 3);

	if (tlsv1_record_send(&conn->rl, TLS_CONTENT_TYPE_HANDSHAKE,
			      rhdr, end - rhdr, pos - hs_start, &rlen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to generate a record");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	pos = rhdr + rlen;

	tls_verify_hash_add(&conn->verify, hs_start, pos - hs_start);

	*msgpos = pos;

	return 0;
}


static int tls_write_server_certificate_request(struct tlsv1_server *conn,
						u8 **msgpos, u8 *end)
{
	u8 *pos, *rhdr, *hs_start, *hs_length;
	size_t rlen;

	if (!conn->verify_peer) {
		wpa_printf(MSG_DEBUG, "TLSv1: No CertificateRequest needed");
		return 0;
	}

	pos = *msgpos;

	wpa_printf(MSG_DEBUG, "TLSv1: Send CertificateRequest");
	rhdr = pos;
	pos += TLS_RECORD_HEADER_LEN;

	/* opaque fragment[TLSPlaintext.length] */

	/* Handshake */
	hs_start = pos;
	/* HandshakeType msg_type */
	*pos++ = TLS_HANDSHAKE_TYPE_CERTIFICATE_REQUEST;
	/* uint24 length (to be filled) */
	hs_length = pos;
	pos += 3;
	/* body - CertificateRequest */

	/*
	 * enum {
	 *   rsa_sign(1), dss_sign(2), rsa_fixed_dh(3), dss_fixed_dh(4),
	 *   (255)
	 * } ClientCertificateType;
	 * ClientCertificateType certificate_types<1..2^8-1>
	 */
	*pos++ = 1;
	*pos++ = 1; /* rsa_sign */

	/*
	 * opaque DistinguishedName<1..2^16-1>
	 * DistinguishedName certificate_authorities<3..2^16-1>
	 */
	/* TODO: add support for listing DNs for trusted CAs */
	WPA_PUT_BE16(pos, 0);
	pos += 2;

	WPA_PUT_BE24(hs_length, pos - hs_length - 3);

	if (tlsv1_record_send(&conn->rl, TLS_CONTENT_TYPE_HANDSHAKE,
			      rhdr, end - rhdr, pos - hs_start, &rlen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to generate a record");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	pos = rhdr + rlen;

	tls_verify_hash_add(&conn->verify, hs_start, pos - hs_start);

	*msgpos = pos;

	return 0;
}


static int tls_write_server_hello_done(struct tlsv1_server *conn,
				       u8 **msgpos, u8 *end)
{
	u8 *pos, *rhdr, *hs_start, *hs_length;
	size_t rlen;

	pos = *msgpos;

	wpa_printf(MSG_DEBUG, "TLSv1: Send ServerHelloDone");
	rhdr = pos;
	pos += TLS_RECORD_HEADER_LEN;

	/* opaque fragment[TLSPlaintext.length] */

	/* Handshake */
	hs_start = pos;
	/* HandshakeType msg_type */
	*pos++ = TLS_HANDSHAKE_TYPE_SERVER_HELLO_DONE;
	/* uint24 length (to be filled) */
	hs_length = pos;
	pos += 3;
	/* body - ServerHelloDone (empty) */

	WPA_PUT_BE24(hs_length, pos - hs_length - 3);

	if (tlsv1_record_send(&conn->rl, TLS_CONTENT_TYPE_HANDSHAKE,
			      rhdr, end - rhdr, pos - hs_start, &rlen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to generate a record");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	pos = rhdr + rlen;

	tls_verify_hash_add(&conn->verify, hs_start, pos - hs_start);

	*msgpos = pos;

	return 0;
}


static int tls_write_server_change_cipher_spec(struct tlsv1_server *conn,
					       u8 **msgpos, u8 *end)
{
	u8 *pos, *rhdr;
	size_t rlen;

	pos = *msgpos;

	wpa_printf(MSG_DEBUG, "TLSv1: Send ChangeCipherSpec");
	rhdr = pos;
	pos += TLS_RECORD_HEADER_LEN;
	*pos = TLS_CHANGE_CIPHER_SPEC;
	if (tlsv1_record_send(&conn->rl, TLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC,
			      rhdr, end - rhdr, 1, &rlen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to create a record");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	if (tlsv1_record_change_write_cipher(&conn->rl) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to set write cipher for "
			   "record layer");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	*msgpos = rhdr + rlen;

	return 0;
}


static int tls_write_server_finished(struct tlsv1_server *conn,
				     u8 **msgpos, u8 *end)
{
	u8 *pos, *rhdr, *hs_start, *hs_length;
	size_t rlen, hlen;
	u8 verify_data[TLS_VERIFY_DATA_LEN];
	u8 hash[MD5_MAC_LEN + SHA1_MAC_LEN];

	pos = *msgpos;

	wpa_printf(MSG_DEBUG, "TLSv1: Send Finished");

	/* Encrypted Handshake Message: Finished */

	hlen = MD5_MAC_LEN;
	if (conn->verify.md5_server == NULL ||
	    crypto_hash_finish(conn->verify.md5_server, hash, &hlen) < 0) {
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
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
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	conn->verify.sha1_server = NULL;

	if (tls_prf(conn->master_secret, TLS_MASTER_SECRET_LEN,
		    "server finished", hash, MD5_MAC_LEN + SHA1_MAC_LEN,
		    verify_data, TLS_VERIFY_DATA_LEN)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to generate verify_data");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	wpa_hexdump_key(MSG_DEBUG, "TLSv1: verify_data (server)",
			verify_data, TLS_VERIFY_DATA_LEN);

	rhdr = pos;
	pos += TLS_RECORD_HEADER_LEN;
	/* Handshake */
	hs_start = pos;
	/* HandshakeType msg_type */
	*pos++ = TLS_HANDSHAKE_TYPE_FINISHED;
	/* uint24 length (to be filled) */
	hs_length = pos;
	pos += 3;
	os_memcpy(pos, verify_data, TLS_VERIFY_DATA_LEN);
	pos += TLS_VERIFY_DATA_LEN;
	WPA_PUT_BE24(hs_length, pos - hs_length - 3);
	tls_verify_hash_add(&conn->verify, hs_start, pos - hs_start);

	if (tlsv1_record_send(&conn->rl, TLS_CONTENT_TYPE_HANDSHAKE,
			      rhdr, end - rhdr, pos - hs_start, &rlen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to create a record");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	pos = rhdr + rlen;

	*msgpos = pos;

	return 0;
}


static u8 * tls_send_server_hello(struct tlsv1_server *conn, size_t *out_len)
{
	u8 *msg, *end, *pos;
	size_t msglen;

	*out_len = 0;

	msglen = 1000 + tls_server_cert_chain_der_len(conn);

	msg = os_malloc(msglen);
	if (msg == NULL)
		return NULL;

	pos = msg;
	end = msg + msglen;

	if (tls_write_server_hello(conn, &pos, end) < 0) {
		os_free(msg);
		return NULL;
	}

	if (conn->use_session_ticket) {
		/* Abbreviated handshake using session ticket; RFC 4507 */
		if (tls_write_server_change_cipher_spec(conn, &pos, end) < 0 ||
		    tls_write_server_finished(conn, &pos, end) < 0) {
			os_free(msg);
			return NULL;
		}

		*out_len = pos - msg;

		conn->state = CHANGE_CIPHER_SPEC;

		return msg;
	}

	/* Full handshake */
	if (tls_write_server_certificate(conn, &pos, end) < 0 ||
	    tls_write_server_key_exchange(conn, &pos, end) < 0 ||
	    tls_write_server_certificate_request(conn, &pos, end) < 0 ||
	    tls_write_server_hello_done(conn, &pos, end) < 0) {
		os_free(msg);
		return NULL;
	}

	*out_len = pos - msg;

	conn->state = CLIENT_CERTIFICATE;

	return msg;
}


static u8 * tls_send_change_cipher_spec(struct tlsv1_server *conn,
					size_t *out_len)
{
	u8 *msg, *end, *pos;

	*out_len = 0;

	msg = os_malloc(1000);
	if (msg == NULL)
		return NULL;

	pos = msg;
	end = msg + 1000;

	if (tls_write_server_change_cipher_spec(conn, &pos, end) < 0 ||
	    tls_write_server_finished(conn, &pos, end) < 0) {
		os_free(msg);
		return NULL;
	}

	*out_len = pos - msg;

	wpa_printf(MSG_DEBUG, "TLSv1: Handshake completed successfully");
	conn->state = ESTABLISHED;

	return msg;
}


u8 * tlsv1_server_handshake_write(struct tlsv1_server *conn, size_t *out_len)
{
	switch (conn->state) {
	case SERVER_HELLO:
		return tls_send_server_hello(conn, out_len);
	case SERVER_CHANGE_CIPHER_SPEC:
		return tls_send_change_cipher_spec(conn, out_len);
	default:
		if (conn->state == ESTABLISHED && conn->use_session_ticket) {
			/* Abbreviated handshake was already completed. */
			return NULL;
		}
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected state %d while "
			   "generating reply", conn->state);
		return NULL;
	}
}


u8 * tlsv1_server_send_alert(struct tlsv1_server *conn, u8 level,
			     u8 description, size_t *out_len)
{
	u8 *alert, *pos, *length;

	wpa_printf(MSG_DEBUG, "TLSv1: Send Alert(%d:%d)", level, description);
	*out_len = 0;

	alert = os_malloc(10);
	if (alert == NULL)
		return NULL;

	pos = alert;

	/* TLSPlaintext */
	/* ContentType type */
	*pos++ = TLS_CONTENT_TYPE_ALERT;
	/* ProtocolVersion version */
	WPA_PUT_BE16(pos, TLS_VERSION);
	pos += 2;
	/* uint16 length (to be filled) */
	length = pos;
	pos += 2;
	/* opaque fragment[TLSPlaintext.length] */

	/* Alert */
	/* AlertLevel level */
	*pos++ = level;
	/* AlertDescription description */
	*pos++ = description;

	WPA_PUT_BE16(length, pos - length - 2);
	*out_len = pos - alert;

	return alert;
}
