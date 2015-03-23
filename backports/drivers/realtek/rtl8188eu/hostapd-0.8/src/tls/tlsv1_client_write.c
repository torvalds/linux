/*
 * TLSv1 client - write handshake message
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
#include "tlsv1_client.h"
#include "tlsv1_client_i.h"


static size_t tls_client_cert_chain_der_len(struct tlsv1_client *conn)
{
	size_t len = 0;
	struct x509_certificate *cert;

	if (conn->cred == NULL)
		return 0;

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


u8 * tls_send_client_hello(struct tlsv1_client *conn, size_t *out_len)
{
	u8 *hello, *end, *pos, *hs_length, *hs_start, *rhdr;
	struct os_time now;
	size_t len, i;

	wpa_printf(MSG_DEBUG, "TLSv1: Send ClientHello");
	*out_len = 0;

	os_get_time(&now);
	WPA_PUT_BE32(conn->client_random, now.sec);
	if (random_get_bytes(conn->client_random + 4, TLS_RANDOM_LEN - 4)) {
		wpa_printf(MSG_ERROR, "TLSv1: Could not generate "
			   "client_random");
		return NULL;
	}
	wpa_hexdump(MSG_MSGDUMP, "TLSv1: client_random",
		    conn->client_random, TLS_RANDOM_LEN);

	len = 100 + conn->num_cipher_suites * 2 + conn->client_hello_ext_len;
	hello = os_malloc(len);
	if (hello == NULL)
		return NULL;
	end = hello + len;

	rhdr = hello;
	pos = rhdr + TLS_RECORD_HEADER_LEN;

	/* opaque fragment[TLSPlaintext.length] */

	/* Handshake */
	hs_start = pos;
	/* HandshakeType msg_type */
	*pos++ = TLS_HANDSHAKE_TYPE_CLIENT_HELLO;
	/* uint24 length (to be filled) */
	hs_length = pos;
	pos += 3;
	/* body - ClientHello */
	/* ProtocolVersion client_version */
	WPA_PUT_BE16(pos, TLS_VERSION);
	pos += 2;
	/* Random random: uint32 gmt_unix_time, opaque random_bytes */
	os_memcpy(pos, conn->client_random, TLS_RANDOM_LEN);
	pos += TLS_RANDOM_LEN;
	/* SessionID session_id */
	*pos++ = conn->session_id_len;
	os_memcpy(pos, conn->session_id, conn->session_id_len);
	pos += conn->session_id_len;
	/* CipherSuite cipher_suites<2..2^16-1> */
	WPA_PUT_BE16(pos, 2 * conn->num_cipher_suites);
	pos += 2;
	for (i = 0; i < conn->num_cipher_suites; i++) {
		WPA_PUT_BE16(pos, conn->cipher_suites[i]);
		pos += 2;
	}
	/* CompressionMethod compression_methods<1..2^8-1> */
	*pos++ = 1;
	*pos++ = TLS_COMPRESSION_NULL;

	if (conn->client_hello_ext) {
		os_memcpy(pos, conn->client_hello_ext,
			  conn->client_hello_ext_len);
		pos += conn->client_hello_ext_len;
	}

	WPA_PUT_BE24(hs_length, pos - hs_length - 3);
	tls_verify_hash_add(&conn->verify, hs_start, pos - hs_start);

	if (tlsv1_record_send(&conn->rl, TLS_CONTENT_TYPE_HANDSHAKE,
			      rhdr, end - rhdr, pos - hs_start, out_len) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to create TLS record");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		os_free(hello);
		return NULL;
	}

	conn->state = SERVER_HELLO;

	return hello;
}


static int tls_write_client_certificate(struct tlsv1_client *conn,
					u8 **msgpos, u8 *end)
{
	u8 *pos, *rhdr, *hs_start, *hs_length, *cert_start;
	size_t rlen;
	struct x509_certificate *cert;

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
	cert = conn->cred ? conn->cred->cert : NULL;
	while (cert) {
		if (pos + 3 + cert->cert_len > end) {
			wpa_printf(MSG_DEBUG, "TLSv1: Not enough buffer space "
				   "for Certificate (cert_len=%lu left=%lu)",
				   (unsigned long) cert->cert_len,
				   (unsigned long) (end - pos));
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
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
	if (conn->cred == NULL || cert == conn->cred->cert || cert == NULL) {
		/*
		 * Client was not configured with all the needed certificates
		 * to form a full certificate chain. The server may fail to
		 * validate the chain unless it is configured with all the
		 * missing CA certificates.
		 */
		wpa_printf(MSG_DEBUG, "TLSv1: Full client certificate chain "
			   "not configured - validation may fail");
	}
	WPA_PUT_BE24(cert_start, pos - cert_start - 3);

	WPA_PUT_BE24(hs_length, pos - hs_length - 3);

	if (tlsv1_record_send(&conn->rl, TLS_CONTENT_TYPE_HANDSHAKE,
			      rhdr, end - rhdr, pos - hs_start, &rlen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to generate a record");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	pos = rhdr + rlen;

	tls_verify_hash_add(&conn->verify, hs_start, pos - hs_start);

	*msgpos = pos;

	return 0;
}


static int tlsv1_key_x_anon_dh(struct tlsv1_client *conn, u8 **pos, u8 *end)
{
	/* ClientDiffieHellmanPublic */
	u8 *csecret, *csecret_start, *dh_yc, *shared;
	size_t csecret_len, dh_yc_len, shared_len;

	csecret_len = conn->dh_p_len;
	csecret = os_malloc(csecret_len);
	if (csecret == NULL) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to allocate "
			   "memory for Yc (Diffie-Hellman)");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	if (random_get_bytes(csecret, csecret_len)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to get random "
			   "data for Diffie-Hellman");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		os_free(csecret);
		return -1;
	}

	if (os_memcmp(csecret, conn->dh_p, csecret_len) > 0)
		csecret[0] = 0; /* make sure Yc < p */

	csecret_start = csecret;
	while (csecret_len > 1 && *csecret_start == 0) {
		csecret_start++;
		csecret_len--;
	}
	wpa_hexdump_key(MSG_DEBUG, "TLSv1: DH client's secret value",
			csecret_start, csecret_len);

	/* Yc = g^csecret mod p */
	dh_yc_len = conn->dh_p_len;
	dh_yc = os_malloc(dh_yc_len);
	if (dh_yc == NULL) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to allocate "
			   "memory for Diffie-Hellman");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		os_free(csecret);
		return -1;
	}
	if (crypto_mod_exp(conn->dh_g, conn->dh_g_len,
			   csecret_start, csecret_len,
			   conn->dh_p, conn->dh_p_len,
			   dh_yc, &dh_yc_len)) {
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		os_free(csecret);
		os_free(dh_yc);
		return -1;
	}

	wpa_hexdump(MSG_DEBUG, "TLSv1: DH Yc (client's public value)",
		    dh_yc, dh_yc_len);

	WPA_PUT_BE16(*pos, dh_yc_len);
	*pos += 2;
	if (*pos + dh_yc_len > end) {
		wpa_printf(MSG_DEBUG, "TLSv1: Not enough room in the "
			   "message buffer for Yc");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		os_free(csecret);
		os_free(dh_yc);
		return -1;
	}
	os_memcpy(*pos, dh_yc, dh_yc_len);
	*pos += dh_yc_len;
	os_free(dh_yc);

	shared_len = conn->dh_p_len;
	shared = os_malloc(shared_len);
	if (shared == NULL) {
		wpa_printf(MSG_DEBUG, "TLSv1: Could not allocate memory for "
			   "DH");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		os_free(csecret);
		return -1;
	}

	/* shared = Ys^csecret mod p */
	if (crypto_mod_exp(conn->dh_ys, conn->dh_ys_len,
			   csecret_start, csecret_len,
			   conn->dh_p, conn->dh_p_len,
			   shared, &shared_len)) {
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		os_free(csecret);
		os_free(shared);
		return -1;
	}
	wpa_hexdump_key(MSG_DEBUG, "TLSv1: Shared secret from DH key exchange",
			shared, shared_len);

	os_memset(csecret_start, 0, csecret_len);
	os_free(csecret);
	if (tls_derive_keys(conn, shared, shared_len)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to derive keys");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		os_free(shared);
		return -1;
	}
	os_memset(shared, 0, shared_len);
	os_free(shared);
	tlsv1_client_free_dh(conn);
	return 0;
}


static int tlsv1_key_x_rsa(struct tlsv1_client *conn, u8 **pos, u8 *end)
{
	u8 pre_master_secret[TLS_PRE_MASTER_SECRET_LEN];
	size_t clen;
	int res;

	if (tls_derive_pre_master_secret(pre_master_secret) < 0 ||
	    tls_derive_keys(conn, pre_master_secret,
			    TLS_PRE_MASTER_SECRET_LEN)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to derive keys");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	/* EncryptedPreMasterSecret */
	if (conn->server_rsa_key == NULL) {
		wpa_printf(MSG_DEBUG, "TLSv1: No server RSA key to "
			   "use for encrypting pre-master secret");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	/* RSA encrypted value is encoded with PKCS #1 v1.5 block type 2. */
	*pos += 2;
	clen = end - *pos;
	res = crypto_public_key_encrypt_pkcs1_v15(
		conn->server_rsa_key,
		pre_master_secret, TLS_PRE_MASTER_SECRET_LEN,
		*pos, &clen);
	os_memset(pre_master_secret, 0, TLS_PRE_MASTER_SECRET_LEN);
	if (res < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: RSA encryption failed");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	WPA_PUT_BE16(*pos - 2, clen);
	wpa_hexdump(MSG_MSGDUMP, "TLSv1: Encrypted pre_master_secret",
		    *pos, clen);
	*pos += clen;

	return 0;
}


static int tls_write_client_key_exchange(struct tlsv1_client *conn,
					 u8 **msgpos, u8 *end)
{
	u8 *pos, *rhdr, *hs_start, *hs_length;
	size_t rlen;
	tls_key_exchange keyx;
	const struct tls_cipher_suite *suite;

	suite = tls_get_cipher_suite(conn->rl.cipher_suite);
	if (suite == NULL)
		keyx = TLS_KEY_X_NULL;
	else
		keyx = suite->key_exchange;

	pos = *msgpos;

	wpa_printf(MSG_DEBUG, "TLSv1: Send ClientKeyExchange");

	rhdr = pos;
	pos += TLS_RECORD_HEADER_LEN;

	/* opaque fragment[TLSPlaintext.length] */

	/* Handshake */
	hs_start = pos;
	/* HandshakeType msg_type */
	*pos++ = TLS_HANDSHAKE_TYPE_CLIENT_KEY_EXCHANGE;
	/* uint24 length (to be filled) */
	hs_length = pos;
	pos += 3;
	/* body - ClientKeyExchange */
	if (keyx == TLS_KEY_X_DH_anon) {
		if (tlsv1_key_x_anon_dh(conn, &pos, end) < 0)
			return -1;
	} else {
		if (tlsv1_key_x_rsa(conn, &pos, end) < 0)
			return -1;
	}

	WPA_PUT_BE24(hs_length, pos - hs_length - 3);

	if (tlsv1_record_send(&conn->rl, TLS_CONTENT_TYPE_HANDSHAKE,
			      rhdr, end - rhdr, pos - hs_start, &rlen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to create a record");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	pos = rhdr + rlen;
	tls_verify_hash_add(&conn->verify, hs_start, pos - hs_start);

	*msgpos = pos;

	return 0;
}


static int tls_write_client_certificate_verify(struct tlsv1_client *conn,
					       u8 **msgpos, u8 *end)
{
	u8 *pos, *rhdr, *hs_start, *hs_length, *signed_start;
	size_t rlen, hlen, clen;
	u8 hash[MD5_MAC_LEN + SHA1_MAC_LEN], *hpos;
	enum { SIGN_ALG_RSA, SIGN_ALG_DSA } alg = SIGN_ALG_RSA;

	pos = *msgpos;

	wpa_printf(MSG_DEBUG, "TLSv1: Send CertificateVerify");
	rhdr = pos;
	pos += TLS_RECORD_HEADER_LEN;

	/* Handshake */
	hs_start = pos;
	/* HandshakeType msg_type */
	*pos++ = TLS_HANDSHAKE_TYPE_CERTIFICATE_VERIFY;
	/* uint24 length (to be filled) */
	hs_length = pos;
	pos += 3;

	/*
	 * RFC 2246: 7.4.3 and 7.4.8:
	 * Signature signature
	 *
	 * RSA:
	 * digitally-signed struct {
	 *     opaque md5_hash[16];
	 *     opaque sha_hash[20];
	 * };
	 *
	 * DSA:
	 * digitally-signed struct {
	 *     opaque sha_hash[20];
	 * };
	 *
	 * The hash values are calculated over all handshake messages sent or
	 * received starting at ClientHello up to, but not including, this
	 * CertificateVerify message, including the type and length fields of
	 * the handshake messages.
	 */

	hpos = hash;

	if (alg == SIGN_ALG_RSA) {
		hlen = MD5_MAC_LEN;
		if (conn->verify.md5_cert == NULL ||
		    crypto_hash_finish(conn->verify.md5_cert, hpos, &hlen) < 0)
		{
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
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
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	conn->verify.sha1_cert = NULL;

	if (alg == SIGN_ALG_RSA)
		hlen += MD5_MAC_LEN;

	wpa_hexdump(MSG_MSGDUMP, "TLSv1: CertificateVerify hash", hash, hlen);

	/*
	 * RFC 2246, 4.7:
	 * In digital signing, one-way hash functions are used as input for a
	 * signing algorithm. A digitally-signed element is encoded as an
	 * opaque vector <0..2^16-1>, where the length is specified by the
	 * signing algorithm and key.
	 *
	 * In RSA signing, a 36-byte structure of two hashes (one SHA and one
	 * MD5) is signed (encrypted with the private key). It is encoded with
	 * PKCS #1 block type 0 or type 1 as described in [PKCS1].
	 */
	signed_start = pos; /* length to be filled */
	pos += 2;
	clen = end - pos;
	if (conn->cred == NULL ||
	    crypto_private_key_sign_pkcs1(conn->cred->key, hash, hlen,
					  pos, &clen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to sign hash (PKCS #1)");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	WPA_PUT_BE16(signed_start, clen);

	pos += clen;

	WPA_PUT_BE24(hs_length, pos - hs_length - 3);

	if (tlsv1_record_send(&conn->rl, TLS_CONTENT_TYPE_HANDSHAKE,
			      rhdr, end - rhdr, pos - hs_start, &rlen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to generate a record");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	pos = rhdr + rlen;

	tls_verify_hash_add(&conn->verify, hs_start, pos - hs_start);

	*msgpos = pos;

	return 0;
}


static int tls_write_client_change_cipher_spec(struct tlsv1_client *conn,
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
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	if (tlsv1_record_change_write_cipher(&conn->rl) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to set write cipher for "
			   "record layer");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	*msgpos = rhdr + rlen;

	return 0;
}


static int tls_write_client_finished(struct tlsv1_client *conn,
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
	if (conn->verify.md5_client == NULL ||
	    crypto_hash_finish(conn->verify.md5_client, hash, &hlen) < 0) {
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
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
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	conn->verify.sha1_client = NULL;

	if (tls_prf(conn->master_secret, TLS_MASTER_SECRET_LEN,
		    "client finished", hash, MD5_MAC_LEN + SHA1_MAC_LEN,
		    verify_data, TLS_VERIFY_DATA_LEN)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to generate verify_data");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}
	wpa_hexdump_key(MSG_DEBUG, "TLSv1: verify_data (client)",
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
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	pos = rhdr + rlen;

	*msgpos = pos;

	return 0;
}


static u8 * tls_send_client_key_exchange(struct tlsv1_client *conn,
					 size_t *out_len)
{
	u8 *msg, *end, *pos;
	size_t msglen;

	*out_len = 0;

	msglen = 2000;
	if (conn->certificate_requested)
		msglen += tls_client_cert_chain_der_len(conn);

	msg = os_malloc(msglen);
	if (msg == NULL)
		return NULL;

	pos = msg;
	end = msg + msglen;

	if (conn->certificate_requested) {
		if (tls_write_client_certificate(conn, &pos, end) < 0) {
			os_free(msg);
			return NULL;
		}
	}

	if (tls_write_client_key_exchange(conn, &pos, end) < 0 ||
	    (conn->certificate_requested && conn->cred && conn->cred->key &&
	     tls_write_client_certificate_verify(conn, &pos, end) < 0) ||
	    tls_write_client_change_cipher_spec(conn, &pos, end) < 0 ||
	    tls_write_client_finished(conn, &pos, end) < 0) {
		os_free(msg);
		return NULL;
	}

	*out_len = pos - msg;

	conn->state = SERVER_CHANGE_CIPHER_SPEC;

	return msg;
}


static u8 * tls_send_change_cipher_spec(struct tlsv1_client *conn,
					size_t *out_len)
{
	u8 *msg, *end, *pos;

	*out_len = 0;

	msg = os_malloc(1000);
	if (msg == NULL)
		return NULL;

	pos = msg;
	end = msg + 1000;

	if (tls_write_client_change_cipher_spec(conn, &pos, end) < 0 ||
	    tls_write_client_finished(conn, &pos, end) < 0) {
		os_free(msg);
		return NULL;
	}

	*out_len = pos - msg;

	wpa_printf(MSG_DEBUG, "TLSv1: Session resumption completed "
		   "successfully");
	conn->state = ESTABLISHED;

	return msg;
}


u8 * tlsv1_client_handshake_write(struct tlsv1_client *conn, size_t *out_len,
				  int no_appl_data)
{
	switch (conn->state) {
	case CLIENT_KEY_EXCHANGE:
		return tls_send_client_key_exchange(conn, out_len);
	case CHANGE_CIPHER_SPEC:
		return tls_send_change_cipher_spec(conn, out_len);
	case ACK_FINISHED:
		wpa_printf(MSG_DEBUG, "TLSv1: Handshake completed "
			   "successfully");
		conn->state = ESTABLISHED;
		*out_len = 0;
		if (no_appl_data) {
			/* Need to return something to get final TLS ACK. */
			return os_malloc(1);
		}
		return NULL;
	default:
		wpa_printf(MSG_DEBUG, "TLSv1: Unexpected state %d while "
			   "generating reply", conn->state);
		return NULL;
	}
}


u8 * tlsv1_client_send_alert(struct tlsv1_client *conn, u8 level,
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
