/*
 * TLSv1 client - read handshake message
 * Copyright (c) 2006-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/md5.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
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


static int tls_version_disabled(struct tlsv1_client *conn, u16 ver)
{
	return (((conn->flags & TLS_CONN_DISABLE_TLSv1_0) &&
		 ver == TLS_VERSION_1) ||
		((conn->flags & TLS_CONN_DISABLE_TLSv1_1) &&
		 ver == TLS_VERSION_1_1) ||
		((conn->flags & TLS_CONN_DISABLE_TLSv1_2) &&
		 ver == TLS_VERSION_1_2));
}


static int tls_process_server_hello_extensions(struct tlsv1_client *conn,
					       const u8 *pos, size_t len)
{
	const u8 *end = pos + len;

	wpa_hexdump(MSG_MSGDUMP, "TLSv1: ServerHello extensions",
		    pos, len);
	while (pos < end) {
		u16 ext, elen;

		if (end - pos < 4) {
			wpa_printf(MSG_INFO, "TLSv1: Truncated ServerHello extension header");
			return -1;
		}

		ext = WPA_GET_BE16(pos);
		pos += 2;
		elen = WPA_GET_BE16(pos);
		pos += 2;

		if (elen > end - pos) {
			wpa_printf(MSG_INFO, "TLSv1: Truncated ServerHello extension");
			return -1;
		}

		wpa_printf(MSG_DEBUG, "TLSv1: ServerHello ExtensionType %u",
			   ext);
		wpa_hexdump(MSG_DEBUG, "TLSv1: ServerHello extension data",
			    pos, elen);

		pos += elen;
	}

	return 0;
}


static int tls_process_server_hello(struct tlsv1_client *conn, u8 ct,
				    const u8 *in_data, size_t *in_len)
{
	const u8 *pos, *end;
	size_t left, len, i;
	u16 cipher_suite;
	u16 tls_version;

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
	tls_version = WPA_GET_BE16(pos);
	if (!tls_version_ok(tls_version) ||
	    tls_version_disabled(conn, tls_version)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected protocol version in "
			   "ServerHello %u.%u", pos[0], pos[1]);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_PROTOCOL_VERSION);
		return -1;
	}
	pos += 2;

	wpa_printf(MSG_DEBUG, "TLSv1: Using TLS v%s",
		   tls_version_str(tls_version));
	conn->rl.tls_version = tls_version;

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

	if (end - pos >= 2) {
		u16 ext_len;

		ext_len = WPA_GET_BE16(pos);
		pos += 2;
		if (end - pos < ext_len) {
			wpa_printf(MSG_INFO,
				   "TLSv1: Invalid ServerHello extension length: %u (left: %u)",
				   ext_len, (unsigned int) (end - pos));
			goto decode_error;
		}

		if (tls_process_server_hello_extensions(conn, pos, ext_len))
			goto decode_error;
		pos += ext_len;
	}

	if (end != pos) {
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


static void tls_peer_cert_event(struct tlsv1_client *conn, int depth,
				struct x509_certificate *cert)
{
	union tls_event_data ev;
	struct wpabuf *cert_buf = NULL;
#ifdef CONFIG_SHA256
	u8 hash[32];
#endif /* CONFIG_SHA256 */
	char subject[128];

	if (!conn->event_cb)
		return;

	os_memset(&ev, 0, sizeof(ev));
	if (conn->cred->cert_probe || conn->cert_in_cb) {
		cert_buf = wpabuf_alloc_copy(cert->cert_start,
					     cert->cert_len);
		ev.peer_cert.cert = cert_buf;
	}
#ifdef CONFIG_SHA256
	if (cert_buf) {
		const u8 *addr[1];
		size_t len[1];
		addr[0] = wpabuf_head(cert_buf);
		len[0] = wpabuf_len(cert_buf);
		if (sha256_vector(1, addr, len, hash) == 0) {
			ev.peer_cert.hash = hash;
			ev.peer_cert.hash_len = sizeof(hash);
		}
	}
#endif /* CONFIG_SHA256 */

	ev.peer_cert.depth = depth;
	x509_name_string(&cert->subject, subject, sizeof(subject));
	ev.peer_cert.subject = subject;

	conn->event_cb(conn->cb_ctx, TLS_PEER_CERTIFICATE, &ev);
	wpabuf_free(cert_buf);
}


static void tls_cert_chain_failure_event(struct tlsv1_client *conn, int depth,
					 struct x509_certificate *cert,
					 enum tls_fail_reason reason,
					 const char *reason_txt)
{
	struct wpabuf *cert_buf = NULL;
	union tls_event_data ev;
	char subject[128];

	if (!conn->event_cb || !cert)
		return;

	os_memset(&ev, 0, sizeof(ev));
	ev.cert_fail.depth = depth;
	x509_name_string(&cert->subject, subject, sizeof(subject));
	ev.peer_cert.subject = subject;
	ev.cert_fail.reason = reason;
	ev.cert_fail.reason_txt = reason_txt;
	cert_buf = wpabuf_alloc_copy(cert->cert_start,
				     cert->cert_len);
	ev.cert_fail.cert = cert_buf;
	conn->event_cb(conn->cb_ctx, TLS_CERT_CHAIN_FAILURE, &ev);
	wpabuf_free(cert_buf);
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

		tls_peer_cert_event(conn, idx, cert);

		if (last == NULL)
			chain = cert;
		else
			last->next = cert;
		last = cert;

		idx++;
		pos += cert_len;
	}

	if (conn->cred && conn->cred->server_cert_only && chain) {
		u8 hash[SHA256_MAC_LEN];
		char buf[128];

		wpa_printf(MSG_DEBUG,
			   "TLSv1: Validate server certificate hash");
		x509_name_string(&chain->subject, buf, sizeof(buf));
		wpa_printf(MSG_DEBUG, "TLSv1: 0: %s", buf);
		if (sha256_vector(1, &chain->cert_start, &chain->cert_len,
				  hash) < 0 ||
		    os_memcmp(conn->cred->srv_cert_hash, hash,
			      SHA256_MAC_LEN) != 0) {
			wpa_printf(MSG_DEBUG,
				   "TLSv1: Server certificate hash mismatch");
			wpa_hexdump(MSG_MSGDUMP, "TLSv1: SHA256 hash",
				    hash, SHA256_MAC_LEN);
			if (conn->event_cb) {
				union tls_event_data ev;

				os_memset(&ev, 0, sizeof(ev));
				ev.cert_fail.reason = TLS_FAIL_UNSPECIFIED;
				ev.cert_fail.reason_txt =
					"Server certificate mismatch";
				ev.cert_fail.subject = buf;
				conn->event_cb(conn->cb_ctx,
					       TLS_CERT_CHAIN_FAILURE, &ev);
			}
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
				  TLS_ALERT_BAD_CERTIFICATE);
			x509_certificate_chain_free(chain);
			return -1;
		}
	} else if (conn->cred && conn->cred->cert_probe) {
		wpa_printf(MSG_DEBUG,
			   "TLSv1: Reject server certificate on probe-only rune");
		if (conn->event_cb) {
			union tls_event_data ev;
			char buf[128];

			os_memset(&ev, 0, sizeof(ev));
			ev.cert_fail.reason = TLS_FAIL_SERVER_CHAIN_PROBE;
			ev.cert_fail.reason_txt =
				"Server certificate chain probe";
			if (chain) {
				x509_name_string(&chain->subject, buf,
						 sizeof(buf));
				ev.cert_fail.subject = buf;
			}
			conn->event_cb(conn->cb_ctx, TLS_CERT_CHAIN_FAILURE,
				       &ev);
		}
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_BAD_CERTIFICATE);
		x509_certificate_chain_free(chain);
		return -1;
	} else if (conn->cred && conn->cred->ca_cert_verify &&
		   x509_certificate_chain_validate(
			   conn->cred->trusted_certs, chain, &reason,
			   !!(conn->flags & TLS_CONN_DISABLE_TIME_CHECKS))
		   < 0) {
		int tls_reason;
		wpa_printf(MSG_DEBUG, "TLSv1: Server certificate chain "
			   "validation failed (reason=%d)", reason);
		switch (reason) {
		case X509_VALIDATE_BAD_CERTIFICATE:
			tls_reason = TLS_ALERT_BAD_CERTIFICATE;
			tls_cert_chain_failure_event(
				conn, 0, chain, TLS_FAIL_BAD_CERTIFICATE,
				"bad certificate");
			break;
		case X509_VALIDATE_UNSUPPORTED_CERTIFICATE:
			tls_reason = TLS_ALERT_UNSUPPORTED_CERTIFICATE;
			break;
		case X509_VALIDATE_CERTIFICATE_REVOKED:
			tls_reason = TLS_ALERT_CERTIFICATE_REVOKED;
			tls_cert_chain_failure_event(
				conn, 0, chain, TLS_FAIL_REVOKED,
				"certificate revoked");
			break;
		case X509_VALIDATE_CERTIFICATE_EXPIRED:
			tls_reason = TLS_ALERT_CERTIFICATE_EXPIRED;
			tls_cert_chain_failure_event(
				conn, 0, chain, TLS_FAIL_EXPIRED,
				"certificate has expired or is not yet valid");
			break;
		case X509_VALIDATE_CERTIFICATE_UNKNOWN:
			tls_reason = TLS_ALERT_CERTIFICATE_UNKNOWN;
			break;
		case X509_VALIDATE_UNKNOWN_CA:
			tls_reason = TLS_ALERT_UNKNOWN_CA;
			tls_cert_chain_failure_event(
				conn, 0, chain, TLS_FAIL_UNTRUSTED,
				"unknown CA");
			break;
		default:
			tls_reason = TLS_ALERT_BAD_CERTIFICATE;
			break;
		}
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, tls_reason);
		x509_certificate_chain_free(chain);
		return -1;
	}

	if (conn->cred && !conn->cred->server_cert_only && chain &&
	    (chain->extensions_present & X509_EXT_EXT_KEY_USAGE) &&
	    !(chain->ext_key_usage &
	      (X509_EXT_KEY_USAGE_ANY | X509_EXT_KEY_USAGE_SERVER_AUTH))) {
		tls_cert_chain_failure_event(
			conn, 0, chain, TLS_FAIL_BAD_CERTIFICATE,
			"certificate not allowed for server authentication");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_BAD_CERTIFICATE);
		x509_certificate_chain_free(chain);
		return -1;
	}

	if (conn->flags & TLS_CONN_REQUEST_OCSP) {
		x509_certificate_chain_free(conn->server_cert);
		conn->server_cert = chain;
	} else {
		x509_certificate_chain_free(chain);
	}

	*in_len = end - in_data;

	conn->state = SERVER_KEY_EXCHANGE;

	return 0;
}


static unsigned int count_bits(const u8 *val, size_t len)
{
	size_t i;
	unsigned int bits;
	u8 tmp;

	for (i = 0; i < len; i++) {
		if (val[i])
			break;
	}
	if (i == len)
		return 0;

	bits = (len - i - 1) * 8;
	tmp = val[i];
	while (tmp) {
		bits++;
		tmp >>= 1;
	}

	return bits;
}


static int tlsv1_process_diffie_hellman(struct tlsv1_client *conn,
					const u8 *buf, size_t len,
					tls_key_exchange key_exchange)
{
	const u8 *pos, *end, *server_params, *server_params_end;
	u8 alert;
	unsigned int bits;
	u16 val;

	tlsv1_client_free_dh(conn);

	pos = buf;
	end = buf + len;

	if (end - pos < 3)
		goto fail;
	server_params = pos;
	val = WPA_GET_BE16(pos);
	pos += 2;
	if (val == 0 || val > (size_t) (end - pos)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Invalid dh_p length %u", val);
		goto fail;
	}
	conn->dh_p_len = val;
	bits = count_bits(pos, conn->dh_p_len);
	if (bits < 768) {
		wpa_printf(MSG_INFO, "TLSv1: Reject under 768-bit DH prime (insecure; only %u bits)",
			   bits);
		wpa_hexdump(MSG_DEBUG, "TLSv1: Rejected DH prime",
			    pos, conn->dh_p_len);
		goto fail;
	}
	conn->dh_p = os_memdup(pos, conn->dh_p_len);
	if (conn->dh_p == NULL)
		goto fail;
	pos += conn->dh_p_len;
	wpa_hexdump(MSG_DEBUG, "TLSv1: DH p (prime)",
		    conn->dh_p, conn->dh_p_len);

	if (end - pos < 3)
		goto fail;
	val = WPA_GET_BE16(pos);
	pos += 2;
	if (val == 0 || val > (size_t) (end - pos))
		goto fail;
	conn->dh_g_len = val;
	conn->dh_g = os_memdup(pos, conn->dh_g_len);
	if (conn->dh_g == NULL)
		goto fail;
	pos += conn->dh_g_len;
	wpa_hexdump(MSG_DEBUG, "TLSv1: DH g (generator)",
		    conn->dh_g, conn->dh_g_len);
	if (conn->dh_g_len == 1 && conn->dh_g[0] < 2)
		goto fail;

	if (end - pos < 3)
		goto fail;
	val = WPA_GET_BE16(pos);
	pos += 2;
	if (val == 0 || val > (size_t) (end - pos))
		goto fail;
	conn->dh_ys_len = val;
	conn->dh_ys = os_memdup(pos, conn->dh_ys_len);
	if (conn->dh_ys == NULL)
		goto fail;
	pos += conn->dh_ys_len;
	wpa_hexdump(MSG_DEBUG, "TLSv1: DH Ys (server's public value)",
		    conn->dh_ys, conn->dh_ys_len);
	server_params_end = pos;

	if (key_exchange == TLS_KEY_X_DHE_RSA) {
		u8 hash[64];
		int hlen;

		if (conn->rl.tls_version == TLS_VERSION_1_2) {
#ifdef CONFIG_TLSV12
			/*
			 * RFC 5246, 4.7:
			 * TLS v1.2 adds explicit indication of the used
			 * signature and hash algorithms.
			 *
			 * struct {
			 *   HashAlgorithm hash;
			 *   SignatureAlgorithm signature;
			 * } SignatureAndHashAlgorithm;
			 */
			if (end - pos < 2)
				goto fail;
			if ((pos[0] != TLS_HASH_ALG_SHA256 &&
			     pos[0] != TLS_HASH_ALG_SHA384 &&
			     pos[0] != TLS_HASH_ALG_SHA512) ||
			    pos[1] != TLS_SIGN_ALG_RSA) {
				wpa_printf(MSG_DEBUG, "TLSv1.2: Unsupported hash(%u)/signature(%u) algorithm",
					   pos[0], pos[1]);
				goto fail;
			}

			hlen = tlsv12_key_x_server_params_hash(
				conn->rl.tls_version, pos[0],
				conn->client_random,
				conn->server_random, server_params,
				server_params_end - server_params, hash);
			pos += 2;
#else /* CONFIG_TLSV12 */
			goto fail;
#endif /* CONFIG_TLSV12 */
		} else {
			hlen = tls_key_x_server_params_hash(
				conn->rl.tls_version, conn->client_random,
				conn->server_random, server_params,
				server_params_end - server_params, hash);
		}

		if (hlen < 0)
			goto fail;
		wpa_hexdump(MSG_MSGDUMP, "TLSv1: ServerKeyExchange hash",
			    hash, hlen);

		if (tls_verify_signature(conn->rl.tls_version,
					 conn->server_rsa_key,
					 hash, hlen, pos, end - pos,
					 &alert) < 0)
			goto fail;
	}

	return 0;

fail:
	wpa_printf(MSG_DEBUG, "TLSv1: Processing DH params failed");
	tlsv1_client_free_dh(conn);
	return -1;
}


static enum tls_ocsp_result
tls_process_certificate_status_ocsp_response(struct tlsv1_client *conn,
					     const u8 *pos, size_t len)
{
	const u8 *end = pos + len;
	u32 ocsp_resp_len;

	/* opaque OCSPResponse<1..2^24-1>; */
	if (end - pos < 3) {
		wpa_printf(MSG_INFO, "TLSv1: Too short OCSPResponse");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return TLS_OCSP_INVALID;
	}
	ocsp_resp_len = WPA_GET_BE24(pos);
	pos += 3;
	if (end - pos < ocsp_resp_len) {
		wpa_printf(MSG_INFO, "TLSv1: Truncated OCSPResponse");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return TLS_OCSP_INVALID;
	}

	return tls_process_ocsp_response(conn, pos, ocsp_resp_len);
}


static int tls_process_certificate_status(struct tlsv1_client *conn, u8 ct,
					   const u8 *in_data, size_t *in_len)
{
	const u8 *pos, *end;
	size_t left, len;
	u8 type, status_type;
	enum tls_ocsp_result res;
	struct x509_certificate *cert;
	int depth;

	if (ct != TLS_CONTENT_TYPE_HANDSHAKE) {
		wpa_printf(MSG_DEBUG,
			   "TLSv1: Expected Handshake; received content type 0x%x",
			   ct);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4) {
		wpa_printf(MSG_DEBUG,
			   "TLSv1: Too short CertificateStatus (left=%lu)",
			   (unsigned long) left);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	type = *pos++;
	len = WPA_GET_BE24(pos);
	pos += 3;
	left -= 4;

	if (len > left) {
		wpa_printf(MSG_DEBUG,
			   "TLSv1: Mismatch in CertificateStatus length (len=%lu != left=%lu)",
			   (unsigned long) len, (unsigned long) left);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	end = pos + len;

	if (type != TLS_HANDSHAKE_TYPE_CERTIFICATE_STATUS) {
		wpa_printf(MSG_DEBUG,
			   "TLSv1: Received unexpected handshake message %d (expected CertificateStatus)",
			   type);
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "TLSv1: Received CertificateStatus");

	/*
	 * struct {
	 *     CertificateStatusType status_type;
	 *     select (status_type) {
	 *         case ocsp: OCSPResponse;
	 *         case ocsp_multi: OCSPResponseList;
	 *     } response;
	 * } CertificateStatus;
	 */
	if (end - pos < 1) {
		wpa_printf(MSG_INFO, "TLSv1: Too short CertificateStatus");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL, TLS_ALERT_DECODE_ERROR);
		return -1;
	}
	status_type = *pos++;
	wpa_printf(MSG_DEBUG, "TLSv1: CertificateStatus status_type %u",
		   status_type);

	if (status_type == 1 /* ocsp */) {
		res = tls_process_certificate_status_ocsp_response(
			conn, pos, end - pos);
	} else if (status_type == 2 /* ocsp_multi */) {
		int good = 0, revoked = 0;
		u32 resp_len;

		res = TLS_OCSP_NO_RESPONSE;

		/*
		 * opaque OCSPResponse<0..2^24-1>;
		 *
		 * struct {
		 *   OCSPResponse ocsp_response_list<1..2^24-1>;
		 * } OCSPResponseList;
		 */
		if (end - pos < 3) {
			wpa_printf(MSG_DEBUG,
				   "TLSv1: Truncated OCSPResponseList");
			res = TLS_OCSP_INVALID;
			goto done;
		}
		resp_len = WPA_GET_BE24(pos);
		pos += 3;
		if (end - pos < resp_len) {
			wpa_printf(MSG_DEBUG,
				   "TLSv1: Truncated OCSPResponseList(len=%u)",
				   resp_len);
			res = TLS_OCSP_INVALID;
			goto done;
		}
		end = pos + resp_len;

		while (end - pos >= 3) {
			resp_len = WPA_GET_BE24(pos);
			pos += 3;
			if (resp_len > end - pos) {
				wpa_printf(MSG_DEBUG,
					   "TLSv1: Truncated OCSPResponse(len=%u; left=%d) in ocsp_multi",
					   resp_len, (int) (end - pos));
				res = TLS_OCSP_INVALID;
				break;
			}
			if (!resp_len)
				continue; /* Skip an empty response */
			res = tls_process_certificate_status_ocsp_response(
				conn, pos - 3, resp_len + 3);
			if (res == TLS_OCSP_REVOKED)
				revoked++;
			else if (res == TLS_OCSP_GOOD)
				good++;
			pos += resp_len;
		}

		if (revoked)
			res = TLS_OCSP_REVOKED;
		else if (good)
			res = TLS_OCSP_GOOD;
	} else {
		wpa_printf(MSG_DEBUG,
			   "TLSv1: Ignore unsupported CertificateStatus");
		goto skip;
	}

done:
	if (res == TLS_OCSP_REVOKED) {
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_CERTIFICATE_REVOKED);
		for (cert = conn->server_cert, depth = 0; cert;
		     cert = cert->next, depth++) {
			if (cert->ocsp_revoked) {
				tls_cert_chain_failure_event(
					conn, depth, cert, TLS_FAIL_REVOKED,
					"certificate revoked");
			}
		}
		return -1;
	}

	if (conn->flags & TLS_CONN_REQUIRE_OCSP_ALL) {
		/*
		 * Verify that each certificate on the chain that is not part
		 * of the trusted certificates has a good status. If not,
		 * terminate handshake.
		 */
		for (cert = conn->server_cert, depth = 0; cert;
		     cert = cert->next, depth++) {
			if (!cert->ocsp_good) {
				tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
					  TLS_ALERT_BAD_CERTIFICATE_STATUS_RESPONSE);
				tls_cert_chain_failure_event(
					conn, depth, cert,
					TLS_FAIL_UNSPECIFIED,
					"bad certificate status response");
				return -1;
			}
			if (cert->issuer_trusted)
				break;
		}
	}

	if ((conn->flags & TLS_CONN_REQUIRE_OCSP) && res != TLS_OCSP_GOOD) {
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  res == TLS_OCSP_INVALID ? TLS_ALERT_DECODE_ERROR :
			  TLS_ALERT_BAD_CERTIFICATE_STATUS_RESPONSE);
		if (conn->server_cert)
			tls_cert_chain_failure_event(
				conn, 0, conn->server_cert,
				TLS_FAIL_UNSPECIFIED,
				"bad certificate status response");
		return -1;
	}

	conn->ocsp_resp_received = 1;

skip:
	*in_len = end - in_data;

	conn->state = SERVER_KEY_EXCHANGE;

	return 0;
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

	if ((conn->flags & TLS_CONN_REQUEST_OCSP) &&
	    type == TLS_HANDSHAKE_TYPE_CERTIFICATE_STATUS)
		return tls_process_certificate_status(conn, ct, in_data,
						      in_len);
	if (type == TLS_HANDSHAKE_TYPE_CERTIFICATE_REQUEST)
		return tls_process_certificate_request(conn, ct, in_data,
						       in_len);
	if (type == TLS_HANDSHAKE_TYPE_SERVER_HELLO_DONE)
		return tls_process_server_hello_done(conn, ct, in_data,
						     in_len);
	if (type != TLS_HANDSHAKE_TYPE_SERVER_KEY_EXCHANGE) {
		wpa_printf(MSG_DEBUG, "TLSv1: Received unexpected handshake "
			   "message %d (expected ServerKeyExchange/"
			   "CertificateRequest/ServerHelloDone%s)", type,
			   (conn->flags & TLS_CONN_REQUEST_OCSP) ?
			   "/CertificateStatus" : "");
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
	if (suite && (suite->key_exchange == TLS_KEY_X_DH_anon ||
		      suite->key_exchange == TLS_KEY_X_DHE_RSA)) {
		if (tlsv1_process_diffie_hellman(conn, pos, len,
						 suite->key_exchange) < 0) {
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

	if ((conn->flags & TLS_CONN_REQUIRE_OCSP) &&
	    !conn->ocsp_resp_received) {
		wpa_printf(MSG_INFO,
			   "TLSv1: No OCSP response received - reject handshake");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_BAD_CERTIFICATE_STATUS_RESPONSE);
		return -1;
	}

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

#ifdef CONFIG_TLSV12
	if (conn->rl.tls_version >= TLS_VERSION_1_2) {
		hlen = SHA256_MAC_LEN;
		if (conn->verify.sha256_server == NULL ||
		    crypto_hash_finish(conn->verify.sha256_server, hash, &hlen)
		    < 0) {
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
				  TLS_ALERT_INTERNAL_ERROR);
			conn->verify.sha256_server = NULL;
			return -1;
		}
		conn->verify.sha256_server = NULL;
	} else {
#endif /* CONFIG_TLSV12 */

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
	hlen = MD5_MAC_LEN + SHA1_MAC_LEN;

#ifdef CONFIG_TLSV12
	}
#endif /* CONFIG_TLSV12 */

	if (tls_prf(conn->rl.tls_version,
		    conn->master_secret, TLS_MASTER_SECRET_LEN,
		    "server finished", hash, hlen,
		    verify_data, TLS_VERIFY_DATA_LEN)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to derive verify_data");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_DECRYPT_ERROR);
		return -1;
	}
	wpa_hexdump_key(MSG_DEBUG, "TLSv1: verify_data (server)",
			verify_data, TLS_VERIFY_DATA_LEN);

	if (os_memcmp_const(pos, verify_data, TLS_VERIFY_DATA_LEN) != 0) {
		wpa_printf(MSG_INFO, "TLSv1: Mismatch in verify_data");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_DECRYPT_ERROR);
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
