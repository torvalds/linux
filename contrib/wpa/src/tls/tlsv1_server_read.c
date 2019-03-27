/*
 * TLSv1 server - read handshake message
 * Copyright (c) 2006-2014, Jouni Malinen <j@w1.fi>
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
#include "tlsv1_server.h"
#include "tlsv1_server_i.h"


static int tls_process_client_key_exchange(struct tlsv1_server *conn, u8 ct,
					   const u8 *in_data, size_t *in_len);
static int tls_process_change_cipher_spec(struct tlsv1_server *conn,
					  u8 ct, const u8 *in_data,
					  size_t *in_len);


static int testing_cipher_suite_filter(struct tlsv1_server *conn, u16 suite)
{
#ifdef CONFIG_TESTING_OPTIONS
	if ((conn->test_flags &
	     (TLS_BREAK_SRV_KEY_X_HASH | TLS_BREAK_SRV_KEY_X_SIGNATURE |
	      TLS_DHE_PRIME_511B | TLS_DHE_PRIME_767B | TLS_DHE_PRIME_15 |
	      TLS_DHE_PRIME_58B | TLS_DHE_NON_PRIME)) &&
	    suite != TLS_DHE_RSA_WITH_AES_256_CBC_SHA256 &&
	    suite != TLS_DHE_RSA_WITH_AES_256_CBC_SHA &&
	    suite != TLS_DHE_RSA_WITH_AES_128_CBC_SHA256 &&
	    suite != TLS_DHE_RSA_WITH_AES_128_CBC_SHA &&
	    suite != TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA)
		return 1;
#endif /* CONFIG_TESTING_OPTIONS */

	return 0;
}


static void tls_process_status_request_item(struct tlsv1_server *conn,
					    const u8 *req, size_t req_len)
{
	const u8 *pos, *end;
	u8 status_type;

	pos = req;
	end = req + req_len;

	/*
	 * RFC 6961, 2.2:
	 * struct {
	 *   CertificateStatusType status_type;
	 *   uint16 request_length;
	 *   select (status_type) {
	 *     case ocsp: OCSPStatusRequest;
	 *     case ocsp_multi: OCSPStatusRequest;
	 *   } request;
	 * } CertificateStatusRequestItemV2;
	 *
	 * enum { ocsp(1), ocsp_multi(2), (255) } CertificateStatusType;
	 */

	if (end - pos < 1)
		return; /* Truncated data */

	status_type = *pos++;
	wpa_printf(MSG_DEBUG, "TLSv1: CertificateStatusType %u", status_type);
	if (status_type != 1 && status_type != 2)
		return; /* Unsupported status type */
	/*
	 * For now, only OCSP stapling is supported, so ignore the specific
	 * request, if any.
	 */
	wpa_hexdump(MSG_DEBUG, "TLSv1: OCSPStatusRequest", pos, end - pos);

	if (status_type == 2)
		conn->status_request_multi = 1;
}


static void tls_process_status_request_v2(struct tlsv1_server *conn,
					  const u8 *ext, size_t ext_len)
{
	const u8 *pos, *end;

	conn->status_request_v2 = 1;

	pos = ext;
	end = ext + ext_len;

	/*
	 * RFC 6961, 2.2:
	 * struct {
	 *   CertificateStatusRequestItemV2
	 *                    certificate_status_req_list<1..2^16-1>;
	 * } CertificateStatusRequestListV2;
	 */

	while (end - pos >= 2) {
		u16 len;

		len = WPA_GET_BE16(pos);
		pos += 2;
		if (len > end - pos)
			break; /* Truncated data */
		tls_process_status_request_item(conn, pos, len);
		pos += len;
	}
}


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
		tlsv1_server_log(conn, "Expected Handshake; received content type 0x%x",
				 ct);
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
		tlsv1_server_log(conn, "Received unexpected handshake message %d (expected ClientHello)",
				 *pos);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}
	tlsv1_server_log(conn, "Received ClientHello");
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
	tlsv1_server_log(conn, "Client version %d.%d",
			 conn->client_version >> 8,
			 conn->client_version & 0xff);
	if (conn->client_version < TLS_VERSION_1) {
		tlsv1_server_log(conn, "Unexpected protocol version in ClientHello %u.%u",
				 conn->client_version >> 8,
				 conn->client_version & 0xff);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_PROTOCOL_VERSION);
		return -1;
	}
	pos += 2;

	if (TLS_VERSION == TLS_VERSION_1)
		conn->rl.tls_version = TLS_VERSION_1;
#ifdef CONFIG_TLSV12
	else if (conn->client_version >= TLS_VERSION_1_2)
		conn->rl.tls_version = TLS_VERSION_1_2;
#endif /* CONFIG_TLSV12 */
	else if (conn->client_version > TLS_VERSION_1_1)
		conn->rl.tls_version = TLS_VERSION_1_1;
	else
		conn->rl.tls_version = conn->client_version;
	tlsv1_server_log(conn, "Using TLS v%s",
			 tls_version_str(conn->rl.tls_version));

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
		if (testing_cipher_suite_filter(conn, conn->cipher_suites[i]))
			continue;
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
		tlsv1_server_log(conn, "No supported cipher suite available");
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
		tlsv1_server_log(conn, "Client does not accept NULL compression");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_ILLEGAL_PARAMETER);
		return -1;
	}

	if (end - pos == 1) {
		tlsv1_server_log(conn, "Unexpected extra octet in the end of ClientHello: 0x%02x",
				 *pos);
		goto decode_error;
	}

	if (end - pos >= 2) {
		/* Extension client_hello_extension_list<0..2^16-1> */
		ext_len = WPA_GET_BE16(pos);
		pos += 2;

		tlsv1_server_log(conn, "%u bytes of ClientHello extensions",
				 ext_len);
		if (end - pos != ext_len) {
			tlsv1_server_log(conn, "Invalid ClientHello extension list length %u (expected %u)",
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
				tlsv1_server_log(conn, "Invalid extension_type field");
				goto decode_error;
			}

			ext_type = WPA_GET_BE16(pos);
			pos += 2;

			if (end - pos < 2) {
				tlsv1_server_log(conn, "Invalid extension_data length field");
				goto decode_error;
			}

			ext_len = WPA_GET_BE16(pos);
			pos += 2;

			if (end - pos < ext_len) {
				tlsv1_server_log(conn, "Invalid extension_data field");
				goto decode_error;
			}

			tlsv1_server_log(conn, "ClientHello Extension type %u",
					 ext_type);
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
			} else if (ext_type == TLS_EXT_STATUS_REQUEST) {
				conn->status_request = 1;
			} else if (ext_type == TLS_EXT_STATUS_REQUEST_V2) {
				tls_process_status_request_v2(conn, pos,
							      ext_len);
			}

			pos += ext_len;
		}
	}

	*in_len = end - in_data;

	tlsv1_server_log(conn, "ClientHello OK - proceed to ServerHello");
	conn->state = SERVER_HELLO;

	return 0;

decode_error:
	tlsv1_server_log(conn, "Failed to decode ClientHello");
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
		tlsv1_server_log(conn, "Expected Handshake; received content type 0x%x",
				 ct);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4) {
		tlsv1_server_log(conn, "Too short Certificate message (len=%lu)",
				 (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	type = *pos++;
	len = WPA_GET_BE24(pos);
	pos += 3;
	left -= 4;

	if (len > left) {
		tlsv1_server_log(conn, "Unexpected Certificate message length (len=%lu != left=%lu)",
				 (unsigned long) len, (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	if (type == TLS_HANDSHAKE_TYPE_CLIENT_KEY_EXCHANGE) {
		if (conn->verify_peer) {
			tlsv1_server_log(conn, "Client did not include Certificate");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_UNEXPECTED_MESSAGE);
			return -1;
		}

		return tls_process_client_key_exchange(conn, ct, in_data,
						       in_len);
	}
	if (type != TLS_HANDSHAKE_TYPE_CERTIFICATE) {
		tlsv1_server_log(conn, "Received unexpected handshake message %d (expected Certificate/ClientKeyExchange)",
				 type);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	tlsv1_server_log(conn, "Received Certificate (certificate_list len %lu)",
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
		tlsv1_server_log(conn, "Too short Certificate (left=%lu)",
				 (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	list_len = WPA_GET_BE24(pos);
	pos += 3;

	if ((size_t) (end - pos) != list_len) {
		tlsv1_server_log(conn, "Unexpected certificate_list length (len=%lu left=%lu)",
				 (unsigned long) list_len,
				 (unsigned long) (end - pos));
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	idx = 0;
	while (pos < end) {
		if (end - pos < 3) {
			tlsv1_server_log(conn, "Failed to parse certificate_list");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_DECODE_ERROR);
			x509_certificate_chain_free(chain);
			return -1;
		}

		cert_len = WPA_GET_BE24(pos);
		pos += 3;

		if ((size_t) (end - pos) < cert_len) {
			tlsv1_server_log(conn, "Unexpected certificate length (len=%lu left=%lu)",
					 (unsigned long) cert_len,
					 (unsigned long) (end - pos));
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_DECODE_ERROR);
			x509_certificate_chain_free(chain);
			return -1;
		}

		tlsv1_server_log(conn, "Certificate %lu (len %lu)",
				 (unsigned long) idx, (unsigned long) cert_len);

		if (idx == 0) {
			crypto_public_key_free(conn->client_rsa_key);
			if (tls_parse_cert(pos, cert_len,
					   &conn->client_rsa_key)) {
				tlsv1_server_log(conn, "Failed to parse the certificate");
				tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
						   TLS_ALERT_BAD_CERTIFICATE);
				x509_certificate_chain_free(chain);
				return -1;
			}
		}

		cert = x509_certificate_parse(pos, cert_len);
		if (cert == NULL) {
			tlsv1_server_log(conn, "Failed to parse the certificate");
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
					    &reason, 0) < 0) {
		int tls_reason;
		tlsv1_server_log(conn, "Server certificate chain validation failed (reason=%d)",
				 reason);
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

	if (chain && (chain->extensions_present & X509_EXT_EXT_KEY_USAGE) &&
	    !(chain->ext_key_usage &
	      (X509_EXT_KEY_USAGE_ANY | X509_EXT_KEY_USAGE_CLIENT_AUTH))) {
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_BAD_CERTIFICATE);
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
	if (pos + encr_len > end) {
		tlsv1_server_log(conn, "Invalid ClientKeyExchange format: encr_len=%u left=%u",
				 encr_len, (unsigned int) (end - pos));
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

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
						 pos, encr_len,
						 out, &outlen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to decrypt "
			   "PreMasterSecret (encr_len=%u outlen=%lu)",
			   encr_len, (unsigned long) outlen);
		use_random = 1;
	}

	if (!use_random && outlen != TLS_PRE_MASTER_SECRET_LEN) {
		tlsv1_server_log(conn, "Unexpected PreMasterSecret length %lu",
				 (unsigned long) outlen);
		use_random = 1;
	}

	if (!use_random && WPA_GET_BE16(out) != conn->client_version) {
		tlsv1_server_log(conn, "Client version in ClientKeyExchange does not match with version in ClientHello");
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


static int tls_process_client_key_exchange_dh(
	struct tlsv1_server *conn, const u8 *pos, const u8 *end)
{
	const u8 *dh_yc;
	u16 dh_yc_len;
	u8 *shared;
	size_t shared_len;
	int res;
	const u8 *dh_p;
	size_t dh_p_len;

	/*
	 * struct {
	 *   select (PublicValueEncoding) {
	 *     case implicit: struct { };
	 *     case explicit: opaque dh_Yc<1..2^16-1>;
	 *   } dh_public;
	 * } ClientDiffieHellmanPublic;
	 */

	tlsv1_server_log(conn, "ClientDiffieHellmanPublic received");
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
		tlsv1_server_log(conn, "Invalid client public value length");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	dh_yc_len = WPA_GET_BE16(pos);
	dh_yc = pos + 2;

	if (dh_yc_len > end - dh_yc) {
		tlsv1_server_log(conn, "Client public value overflow (length %d)",
				 dh_yc_len);
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

	tlsv1_server_get_dh_p(conn, &dh_p, &dh_p_len);

	shared_len = dh_p_len;
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
			   conn->dh_secret_len, dh_p, dh_p_len,
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
		tlsv1_server_log(conn, "Expected Handshake; received content type 0x%x",
				 ct);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4) {
		tlsv1_server_log(conn, "Too short ClientKeyExchange (Left=%lu)",
				 (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	type = *pos++;
	len = WPA_GET_BE24(pos);
	pos += 3;
	left -= 4;

	if (len > left) {
		tlsv1_server_log(conn, "Mismatch in ClientKeyExchange length (len=%lu != left=%lu)",
				 (unsigned long) len, (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	end = pos + len;

	if (type != TLS_HANDSHAKE_TYPE_CLIENT_KEY_EXCHANGE) {
		tlsv1_server_log(conn, "Received unexpected handshake message %d (expected ClientKeyExchange)",
				 type);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	tlsv1_server_log(conn, "Received ClientKeyExchange");

	wpa_hexdump(MSG_DEBUG, "TLSv1: ClientKeyExchange", pos, len);

	suite = tls_get_cipher_suite(conn->rl.cipher_suite);
	if (suite == NULL)
		keyx = TLS_KEY_X_NULL;
	else
		keyx = suite->key_exchange;

	if ((keyx == TLS_KEY_X_DH_anon || keyx == TLS_KEY_X_DHE_RSA) &&
	    tls_process_client_key_exchange_dh(conn, pos, end) < 0)
		return -1;

	if (keyx != TLS_KEY_X_DH_anon && keyx != TLS_KEY_X_DHE_RSA &&
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
	size_t hlen;
	u8 hash[MD5_MAC_LEN + SHA1_MAC_LEN], *hpos;
	u8 alert;

	if (ct == TLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC) {
		if (conn->verify_peer) {
			tlsv1_server_log(conn, "Client did not include CertificateVerify");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_UNEXPECTED_MESSAGE);
			return -1;
		}

		return tls_process_change_cipher_spec(conn, ct, in_data,
						      in_len);
	}

	if (ct != TLS_CONTENT_TYPE_HANDSHAKE) {
		tlsv1_server_log(conn, "Expected Handshake; received content type 0x%x",
				 ct);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4) {
		tlsv1_server_log(conn, "Too short CertificateVerify message (len=%lu)",
				 (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	type = *pos++;
	len = WPA_GET_BE24(pos);
	pos += 3;
	left -= 4;

	if (len > left) {
		tlsv1_server_log(conn, "Unexpected CertificateVerify message length (len=%lu != left=%lu)",
				 (unsigned long) len, (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	end = pos + len;

	if (type != TLS_HANDSHAKE_TYPE_CERTIFICATE_VERIFY) {
		tlsv1_server_log(conn, "Received unexpected handshake message %d (expected CertificateVerify)",
				 type);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	tlsv1_server_log(conn, "Received CertificateVerify");

	/*
	 * struct {
	 *   Signature signature;
	 * } CertificateVerify;
	 */

	hpos = hash;

#ifdef CONFIG_TLSV12
	if (conn->rl.tls_version == TLS_VERSION_1_2) {
		/*
		 * RFC 5246, 4.7:
		 * TLS v1.2 adds explicit indication of the used signature and
		 * hash algorithms.
		 *
		 * struct {
		 *   HashAlgorithm hash;
		 *   SignatureAlgorithm signature;
		 * } SignatureAndHashAlgorithm;
		 */
		if (end - pos < 2) {
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_DECODE_ERROR);
			return -1;
		}
		if (pos[0] != TLS_HASH_ALG_SHA256 ||
		    pos[1] != TLS_SIGN_ALG_RSA) {
			wpa_printf(MSG_DEBUG, "TLSv1.2: Unsupported hash(%u)/"
				   "signature(%u) algorithm",
				   pos[0], pos[1]);
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_INTERNAL_ERROR);
			return -1;
		}
		pos += 2;

		hlen = SHA256_MAC_LEN;
		if (conn->verify.sha256_cert == NULL ||
		    crypto_hash_finish(conn->verify.sha256_cert, hpos, &hlen) <
		    0) {
			conn->verify.sha256_cert = NULL;
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_INTERNAL_ERROR);
			return -1;
		}
		conn->verify.sha256_cert = NULL;
	} else {
#endif /* CONFIG_TLSV12 */

	hlen = MD5_MAC_LEN;
	if (conn->verify.md5_cert == NULL ||
	    crypto_hash_finish(conn->verify.md5_cert, hpos, &hlen) < 0) {
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		conn->verify.md5_cert = NULL;
		crypto_hash_finish(conn->verify.sha1_cert, NULL, NULL);
		conn->verify.sha1_cert = NULL;
		return -1;
	}
	hpos += MD5_MAC_LEN;

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

	hlen += MD5_MAC_LEN;

#ifdef CONFIG_TLSV12
	}
#endif /* CONFIG_TLSV12 */

	wpa_hexdump(MSG_MSGDUMP, "TLSv1: CertificateVerify hash", hash, hlen);

	if (tls_verify_signature(conn->rl.tls_version, conn->client_rsa_key,
				 hash, hlen, pos, end - pos, &alert) < 0) {
		tlsv1_server_log(conn, "Invalid Signature in CertificateVerify");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL, alert);
		return -1;
	}

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
		tlsv1_server_log(conn, "Expected ChangeCipherSpec; received content type 0x%x",
				 ct);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 1) {
		tlsv1_server_log(conn, "Too short ChangeCipherSpec");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}

	if (*pos != TLS_CHANGE_CIPHER_SPEC) {
		tlsv1_server_log(conn, "Expected ChangeCipherSpec; received data 0x%x",
				 *pos);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	tlsv1_server_log(conn, "Received ChangeCipherSpec");
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

#ifdef CONFIG_TESTING_OPTIONS
	if ((conn->test_flags &
	     (TLS_BREAK_SRV_KEY_X_HASH | TLS_BREAK_SRV_KEY_X_SIGNATURE)) &&
	    !conn->test_failure_reported) {
		tlsv1_server_log(conn, "TEST-FAILURE: Client Finished received after invalid ServerKeyExchange");
		conn->test_failure_reported = 1;
	}

	if ((conn->test_flags & TLS_DHE_PRIME_15) &&
	    !conn->test_failure_reported) {
		tlsv1_server_log(conn, "TEST-FAILURE: Client Finished received after bogus DHE \"prime\" 15");
		conn->test_failure_reported = 1;
	}

	if ((conn->test_flags & TLS_DHE_PRIME_58B) &&
	    !conn->test_failure_reported) {
		tlsv1_server_log(conn, "TEST-FAILURE: Client Finished received after short 58-bit DHE prime in long container");
		conn->test_failure_reported = 1;
	}

	if ((conn->test_flags & TLS_DHE_PRIME_511B) &&
	    !conn->test_failure_reported) {
		tlsv1_server_log(conn, "TEST-WARNING: Client Finished received after short 511-bit DHE prime (insecure)");
		conn->test_failure_reported = 1;
	}

	if ((conn->test_flags & TLS_DHE_PRIME_767B) &&
	    !conn->test_failure_reported) {
		tlsv1_server_log(conn, "TEST-NOTE: Client Finished received after 767-bit DHE prime (relatively insecure)");
		conn->test_failure_reported = 1;
	}

	if ((conn->test_flags & TLS_DHE_NON_PRIME) &&
	    !conn->test_failure_reported) {
		tlsv1_server_log(conn, "TEST-NOTE: Client Finished received after non-prime claimed as DHE prime");
		conn->test_failure_reported = 1;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	if (ct != TLS_CONTENT_TYPE_HANDSHAKE) {
		tlsv1_server_log(conn, "Expected Finished; received content type 0x%x",
				 ct);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_UNEXPECTED_MESSAGE);
		return -1;
	}

	pos = in_data;
	left = *in_len;

	if (left < 4) {
		tlsv1_server_log(conn, "Too short record (left=%lu) forFinished",
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
		tlsv1_server_log(conn, "Too short buffer for Finished (len=%lu > left=%lu)",
				 (unsigned long) len, (unsigned long) left);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}
	end = pos + len;
	if (len != TLS_VERIFY_DATA_LEN) {
		tlsv1_server_log(conn, "Unexpected verify_data length in Finished: %lu (expected %d)",
				 (unsigned long) len, TLS_VERIFY_DATA_LEN);
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECODE_ERROR);
		return -1;
	}
	wpa_hexdump(MSG_MSGDUMP, "TLSv1: verify_data in Finished",
		    pos, TLS_VERIFY_DATA_LEN);

#ifdef CONFIG_TLSV12
	if (conn->rl.tls_version >= TLS_VERSION_1_2) {
		hlen = SHA256_MAC_LEN;
		if (conn->verify.sha256_client == NULL ||
		    crypto_hash_finish(conn->verify.sha256_client, hash, &hlen)
		    < 0) {
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_INTERNAL_ERROR);
			conn->verify.sha256_client = NULL;
			return -1;
		}
		conn->verify.sha256_client = NULL;
	} else {
#endif /* CONFIG_TLSV12 */

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
	hlen = MD5_MAC_LEN + SHA1_MAC_LEN;

#ifdef CONFIG_TLSV12
	}
#endif /* CONFIG_TLSV12 */

	if (tls_prf(conn->rl.tls_version,
		    conn->master_secret, TLS_MASTER_SECRET_LEN,
		    "client finished", hash, hlen,
		    verify_data, TLS_VERIFY_DATA_LEN)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to derive verify_data");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_DECRYPT_ERROR);
		return -1;
	}
	wpa_hexdump_key(MSG_DEBUG, "TLSv1: verify_data (client)",
			verify_data, TLS_VERIFY_DATA_LEN);

	if (os_memcmp_const(pos, verify_data, TLS_VERIFY_DATA_LEN) != 0) {
		tlsv1_server_log(conn, "Mismatch in verify_data");
		return -1;
	}

	tlsv1_server_log(conn, "Received Finished");

	*in_len = end - in_data;

	if (conn->use_session_ticket) {
		/* Abbreviated handshake using session ticket; RFC 4507 */
		tlsv1_server_log(conn, "Abbreviated handshake completed successfully");
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
			tlsv1_server_log(conn, "Alert underflow");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_DECODE_ERROR);
			return -1;
		}
		tlsv1_server_log(conn, "Received alert %d:%d", buf[0], buf[1]);
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
		tlsv1_server_log(conn, "Unexpected state %d while processing received message",
				 conn->state);
		return -1;
	}

	if (ct == TLS_CONTENT_TYPE_HANDSHAKE)
		tls_verify_hash_add(&conn->verify, buf, *len);

	return 0;
}
