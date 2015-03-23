/*
 * SSL/TLS interface functions for NSS
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
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
#include <nspr/prtypes.h>
#include <nspr/plarenas.h>
#include <nspr/plhash.h>
#include <nspr/prio.h>
#include <nspr/prclist.h>
#include <nspr/prlock.h>
#include <nspr/prinit.h>
#include <nspr/prerror.h>
#include <nspr/prmem.h>
#include <nss/nss.h>
#include <nss/nssilckt.h>
#include <nss/ssl.h>
#include <nss/pk11func.h>
#include <nss/secerr.h>

#include "common.h"
#include "tls.h"

static int tls_nss_ref_count = 0;

static PRDescIdentity nss_layer_id;


struct tls_connection {
	PRFileDesc *fd;

	int established;
	int verify_peer;
	u8 *push_buf, *pull_buf, *pull_buf_offset;
	size_t push_buf_len, pull_buf_len;
};


static PRStatus nss_io_close(PRFileDesc *fd)
{
	wpa_printf(MSG_DEBUG, "NSS: I/O close");
	return PR_SUCCESS;
}


static PRInt32 nss_io_read(PRFileDesc *fd, void *buf, PRInt32 amount)
{
	wpa_printf(MSG_DEBUG, "NSS: I/O read(%d)", amount);
	return PR_FAILURE;
}


static PRInt32 nss_io_write(PRFileDesc *fd, const void *buf, PRInt32 amount)
{
	wpa_printf(MSG_DEBUG, "NSS: I/O write(%d)", amount);
	return PR_FAILURE;
}


static PRInt32 nss_io_writev(PRFileDesc *fd, const PRIOVec *iov,
			     PRInt32 iov_size, PRIntervalTime timeout)
{
	wpa_printf(MSG_DEBUG, "NSS: I/O writev(%d)", iov_size);
	return PR_FAILURE;
}


static PRInt32 nss_io_recv(PRFileDesc *fd, void *buf, PRInt32 amount,
			   PRIntn flags, PRIntervalTime timeout)
{
	struct tls_connection *conn = (struct tls_connection *) fd->secret;
	u8 *end;

	wpa_printf(MSG_DEBUG, "NSS: I/O recv(%d)", amount);

	if (conn->pull_buf == NULL) {
		wpa_printf(MSG_DEBUG, "NSS: No data available to be read yet");
		return PR_FAILURE;
	}

	end = conn->pull_buf + conn->pull_buf_len;
	if (end - conn->pull_buf_offset < amount)
		amount = end - conn->pull_buf_offset;
	os_memcpy(buf, conn->pull_buf_offset, amount);
	conn->pull_buf_offset += amount;
	if (conn->pull_buf_offset == end) {
		wpa_printf(MSG_DEBUG, "%s - pull_buf consumed", __func__);
		os_free(conn->pull_buf);
		conn->pull_buf = conn->pull_buf_offset = NULL;
		conn->pull_buf_len = 0;
	} else {
		wpa_printf(MSG_DEBUG, "%s - %lu bytes remaining in pull_buf",
			   __func__,
			   (unsigned long) (end - conn->pull_buf_offset));
	}
	return amount;
}


static PRInt32 nss_io_send(PRFileDesc *fd, const void *buf, PRInt32 amount,
			   PRIntn flags, PRIntervalTime timeout)
{
	struct tls_connection *conn = (struct tls_connection *) fd->secret;
	u8 *nbuf;

	wpa_printf(MSG_DEBUG, "NSS: I/O %s", __func__);
	wpa_hexdump(MSG_MSGDUMP, "NSS: I/O send data", buf, amount);

	nbuf = os_realloc(conn->push_buf, conn->push_buf_len + amount);
	if (nbuf == NULL) {
		wpa_printf(MSG_ERROR, "NSS: Failed to allocate memory for the "
			   "data to be sent");
		return PR_FAILURE;
	}
	os_memcpy(nbuf + conn->push_buf_len, buf, amount);
	conn->push_buf = nbuf;
	conn->push_buf_len += amount;

	return amount;
}


static PRInt32 nss_io_recvfrom(PRFileDesc *fd, void *buf, PRInt32 amount,
			       PRIntn flags, PRNetAddr *addr,
			       PRIntervalTime timeout)
{
	wpa_printf(MSG_DEBUG, "NSS: I/O %s", __func__);
	return PR_FAILURE;
}


static PRInt32 nss_io_sendto(PRFileDesc *fd, const void *buf, PRInt32 amount,
			     PRIntn flags, const PRNetAddr *addr,
			     PRIntervalTime timeout)
{
	wpa_printf(MSG_DEBUG, "NSS: I/O %s", __func__);
	return PR_FAILURE;
}


static PRStatus nss_io_getpeername(PRFileDesc *fd, PRNetAddr *addr)
{
	wpa_printf(MSG_DEBUG, "NSS: I/O getpeername");

	/*
	 * It Looks like NSS only supports IPv4 and IPv6 TCP sockets. Provide a
	 * fake IPv4 address to work around this even though we are not really
	 * using TCP.
	 */
	os_memset(addr, 0, sizeof(*addr));
	addr->inet.family = PR_AF_INET;

	return PR_SUCCESS;
}


static PRStatus nss_io_getsocketoption(PRFileDesc *fd,
				       PRSocketOptionData *data)
{
	switch (data->option) {
	case PR_SockOpt_Nonblocking:
		wpa_printf(MSG_DEBUG, "NSS: I/O getsocketoption(Nonblocking)");
		data->value.non_blocking = PR_TRUE;
		return PR_SUCCESS;
	default:
		wpa_printf(MSG_DEBUG, "NSS: I/O getsocketoption(%d)",
			   data->option);
		return PR_FAILURE;
	}
}


static const PRIOMethods nss_io = {
	PR_DESC_LAYERED,
	nss_io_close,
	nss_io_read,
	nss_io_write,
	NULL /* available */,
	NULL /* available64 */,
	NULL /* fsync */,
	NULL /* fseek */,
	NULL /* fseek64 */,
	NULL /* fileinfo */,
	NULL /* fileinfo64 */,
	nss_io_writev,
	NULL /* connect */,
	NULL /* accept */,
	NULL /* bind */,
	NULL /* listen */,
	NULL /* shutdown */,
	nss_io_recv,
	nss_io_send,
	nss_io_recvfrom,
	nss_io_sendto,
	NULL /* poll */,
	NULL /* acceptread */,
	NULL /* transmitfile */,
	NULL /* getsockname */,
	nss_io_getpeername,
	NULL /* reserved_fn_6 */,
	NULL /* reserved_fn_5 */,
	nss_io_getsocketoption,
	NULL /* setsocketoption */,
	NULL /* sendfile */,
	NULL /* connectcontinue */,
	NULL /* reserved_fn_3 */,
	NULL /* reserved_fn_2 */,
	NULL /* reserved_fn_1 */,
	NULL /* reserved_fn_0 */
};


static char * nss_password_cb(PK11SlotInfo *slot, PRBool retry, void *arg)
{
	wpa_printf(MSG_ERROR, "NSS: TODO - %s", __func__);
	return NULL;
}


void * tls_init(const struct tls_config *conf)
{
	char *dir;

	tls_nss_ref_count++;
	if (tls_nss_ref_count > 1)
		return (void *) 1;

	PR_Init(PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);

	nss_layer_id = PR_GetUniqueIdentity("wpa_supplicant");

	PK11_SetPasswordFunc(nss_password_cb);

	dir = getenv("SSL_DIR");
	if (dir) {
		if (NSS_Init(dir) != SECSuccess) {
			wpa_printf(MSG_ERROR, "NSS: NSS_Init(cert_dir=%s) "
				   "failed", dir);
			return NULL;
		}
	} else {
		if (NSS_NoDB_Init(NULL) != SECSuccess) {
			wpa_printf(MSG_ERROR, "NSS: NSS_NoDB_Init(NULL) "
				   "failed");
			return NULL;
		}
	}

	if (SSL_OptionSetDefault(SSL_V2_COMPATIBLE_HELLO, PR_FALSE) !=
	    SECSuccess ||
	    SSL_OptionSetDefault(SSL_ENABLE_SSL3, PR_FALSE) != SECSuccess ||
	    SSL_OptionSetDefault(SSL_ENABLE_SSL2, PR_FALSE) != SECSuccess ||
	    SSL_OptionSetDefault(SSL_ENABLE_TLS, PR_TRUE) != SECSuccess) {
		wpa_printf(MSG_ERROR, "NSS: SSL_OptionSetDefault failed");
		return NULL;
	}

	if (NSS_SetDomesticPolicy() != SECSuccess) {
		wpa_printf(MSG_ERROR, "NSS: NSS_SetDomesticPolicy() failed");
		return NULL;
	}

	return (void *) 1;
}

void tls_deinit(void *ssl_ctx)
{
	tls_nss_ref_count--;
	if (tls_nss_ref_count == 0) {
		if (NSS_Shutdown() != SECSuccess)
			wpa_printf(MSG_ERROR, "NSS: NSS_Shutdown() failed");
	}
}


int tls_get_errors(void *tls_ctx)
{
	return 0;
}


static SECStatus nss_bad_cert_cb(void *arg, PRFileDesc *fd)
{
	struct tls_connection *conn = arg;
	SECStatus res = SECSuccess;
	PRErrorCode err;
	CERTCertificate *cert;
	char *subject, *issuer;

	err = PR_GetError();
	if (IS_SEC_ERROR(err))
		wpa_printf(MSG_DEBUG, "NSS: Bad Server Certificate (sec err "
			   "%d)", err - SEC_ERROR_BASE);
	else
		wpa_printf(MSG_DEBUG, "NSS: Bad Server Certificate (err %d)",
			   err);
	cert = SSL_PeerCertificate(fd);
	subject = CERT_NameToAscii(&cert->subject);
	issuer = CERT_NameToAscii(&cert->issuer);
	wpa_printf(MSG_DEBUG, "NSS: Peer certificate subject='%s' issuer='%s'",
		   subject, issuer);
	CERT_DestroyCertificate(cert);
	PR_Free(subject);
	PR_Free(issuer);
	if (conn->verify_peer)
		res = SECFailure;

	return res;
}


static void nss_handshake_cb(PRFileDesc *fd, void *client_data)
{
	struct tls_connection *conn = client_data;
	wpa_printf(MSG_DEBUG, "NSS: Handshake completed");
	conn->established = 1;
}


struct tls_connection * tls_connection_init(void *tls_ctx)
{
	struct tls_connection *conn;

	conn = os_zalloc(sizeof(*conn));
	if (conn == NULL)
		return NULL;

	conn->fd = PR_CreateIOLayerStub(nss_layer_id, &nss_io);
	if (conn->fd == NULL) {
		os_free(conn);
		return NULL;
	}
	conn->fd->secret = (void *) conn;

	conn->fd = SSL_ImportFD(NULL, conn->fd);
	if (conn->fd == NULL) {
		os_free(conn);
		return NULL;
	}

	if (SSL_OptionSet(conn->fd, SSL_SECURITY, PR_TRUE) != SECSuccess ||
	    SSL_OptionSet(conn->fd, SSL_HANDSHAKE_AS_CLIENT, PR_TRUE) !=
	    SECSuccess ||
	    SSL_OptionSet(conn->fd, SSL_HANDSHAKE_AS_SERVER, PR_FALSE) !=
	    SECSuccess ||
	    SSL_OptionSet(conn->fd, SSL_ENABLE_TLS, PR_TRUE) != SECSuccess ||
	    SSL_BadCertHook(conn->fd, nss_bad_cert_cb, conn) != SECSuccess ||
	    SSL_HandshakeCallback(conn->fd, nss_handshake_cb, conn) !=
	    SECSuccess) {
		wpa_printf(MSG_ERROR, "NSS: Failed to set options");
		PR_Close(conn->fd);
		os_free(conn);
		return NULL;
	}

	SSL_ResetHandshake(conn->fd, PR_FALSE);

	return conn;
}


void tls_connection_deinit(void *tls_ctx, struct tls_connection *conn)
{
	PR_Close(conn->fd);
	os_free(conn->push_buf);
	os_free(conn->pull_buf);
	os_free(conn);
}


int tls_connection_established(void *tls_ctx, struct tls_connection *conn)
{
	return conn->established;
}


int tls_connection_shutdown(void *tls_ctx, struct tls_connection *conn)
{
	return -1;
}


int tls_connection_set_params(void *tls_ctx, struct tls_connection *conn,
			      const struct tls_connection_params *params)
{
	wpa_printf(MSG_ERROR, "NSS: TODO - %s", __func__);
	return 0;
}


int tls_global_set_params(void *tls_ctx,
			  const struct tls_connection_params *params)
{
	return -1;
}


int tls_global_set_verify(void *tls_ctx, int check_crl)
{
	return -1;
}


int tls_connection_set_verify(void *tls_ctx, struct tls_connection *conn,
			      int verify_peer)
{
	conn->verify_peer = verify_peer;
	return 0;
}


int tls_connection_set_ia(void *tls_ctx, struct tls_connection *conn,
			  int tls_ia)
{
	return -1;
}


int tls_connection_get_keys(void *tls_ctx, struct tls_connection *conn,
			    struct tls_keys *keys)
{
	/* NSS does not export master secret or client/server random. */
	return -1;
}


int tls_connection_prf(void *tls_ctx, struct tls_connection *conn,
		       const char *label, int server_random_first,
		       u8 *out, size_t out_len)
{
	if (conn == NULL || server_random_first) {
		wpa_printf(MSG_INFO, "NSS: Unsupported PRF request "
			   "(server_random_first=%d)",
			   server_random_first);
		return -1;
	}

	if (SSL_ExportKeyingMaterial(conn->fd, label, NULL, 0, out, out_len) !=
	    SECSuccess) {
		wpa_printf(MSG_INFO, "NSS: Failed to use TLS extractor "
			   "(label='%s' out_len=%d", label, (int) out_len);
		return -1;
	}

	return 0;
}


struct wpabuf * tls_connection_handshake(void *tls_ctx,
					 struct tls_connection *conn,
					 const struct wpabuf *in_data,
					 struct wpabuf **appl_data)
{
	struct wpabuf *out_data;

	wpa_printf(MSG_DEBUG, "NSS: handshake: in_len=%u",
		   in_data ? (unsigned int) wpabuf_len(in_data) : 0);

	if (appl_data)
		*appl_data = NULL;

	if (in_data && wpabuf_len(in_data) > 0) {
		if (conn->pull_buf) {
			wpa_printf(MSG_DEBUG, "%s - %lu bytes remaining in "
				   "pull_buf", __func__,
				   (unsigned long) conn->pull_buf_len);
			os_free(conn->pull_buf);
		}
		conn->pull_buf = os_malloc(wpabuf_len(in_data));
		if (conn->pull_buf == NULL)
			return NULL;
		os_memcpy(conn->pull_buf, wpabuf_head(in_data),
			  wpabuf_len(in_data));
		conn->pull_buf_offset = conn->pull_buf;
		conn->pull_buf_len = wpabuf_len(in_data);
	}

	SSL_ForceHandshake(conn->fd);

	if (conn->established && conn->push_buf == NULL) {
		/* Need to return something to get final TLS ACK. */
		conn->push_buf = os_malloc(1);
	}

	if (conn->push_buf == NULL)
		return NULL;
	out_data = wpabuf_alloc_ext_data(conn->push_buf, conn->push_buf_len);
	if (out_data == NULL)
		os_free(conn->push_buf);
	conn->push_buf = NULL;
	conn->push_buf_len = 0;
	return out_data;
}


struct wpabuf * tls_connection_server_handshake(void *tls_ctx,
						struct tls_connection *conn,
						const struct wpabuf *in_data,
						struct wpabuf **appl_data)
{
	return NULL;
}


struct wpabuf * tls_connection_encrypt(void *tls_ctx,
				       struct tls_connection *conn,
				       const struct wpabuf *in_data)
{
	PRInt32 res;
	struct wpabuf *buf;

	wpa_printf(MSG_DEBUG, "NSS: encrypt %d bytes",
		   (int) wpabuf_len(in_data));
	res = PR_Send(conn->fd, wpabuf_head(in_data), wpabuf_len(in_data), 0,
		      0);
	if (res < 0) {
		wpa_printf(MSG_ERROR, "NSS: Encryption failed");
		return NULL;
	}
	if (conn->push_buf == NULL)
		return NULL;
	buf = wpabuf_alloc_ext_data(conn->push_buf, conn->push_buf_len);
	if (buf == NULL)
		os_free(conn->push_buf);
	conn->push_buf = NULL;
	conn->push_buf_len = 0;
	return buf;
}


struct wpabuf * tls_connection_decrypt(void *tls_ctx,
				       struct tls_connection *conn,
				       const struct wpabuf *in_data)
{
	PRInt32 res;
	struct wpabuf *out;

	wpa_printf(MSG_DEBUG, "NSS: decrypt %d bytes",
		   (int) wpabuf_len(in_data));
	if (conn->pull_buf) {
		wpa_printf(MSG_DEBUG, "%s - %lu bytes remaining in "
			   "pull_buf", __func__,
			   (unsigned long) conn->pull_buf_len);
		os_free(conn->pull_buf);
	}
	conn->pull_buf = os_malloc(wpabuf_len(in_data));
	if (conn->pull_buf == NULL)
		return NULL;
	os_memcpy(conn->pull_buf, wpabuf_head(in_data), wpabuf_len(in_data));
	conn->pull_buf_offset = conn->pull_buf;
	conn->pull_buf_len = wpabuf_len(in_data);

	/*
	 * Even though we try to disable TLS compression, it is possible that
	 * this cannot be done with all TLS libraries. Add extra buffer space
	 * to handle the possibility of the decrypted data being longer than
	 * input data.
	 */
	out = wpabuf_alloc((wpabuf_len(in_data) + 500) * 3);
	if (out == NULL)
		return NULL;

	res = PR_Recv(conn->fd, wpabuf_mhead(out), wpabuf_size(out), 0, 0);
	wpa_printf(MSG_DEBUG, "NSS: PR_Recv: %d", res);
	if (res < 0) {
		wpabuf_free(out);
		return NULL;
	}
	wpabuf_put(out, res);

	return out;
}


int tls_connection_resumed(void *tls_ctx, struct tls_connection *conn)
{
	return 0;
}


int tls_connection_set_cipher_list(void *tls_ctx, struct tls_connection *conn,
				   u8 *ciphers)
{
	return -1;
}


int tls_get_cipher(void *tls_ctx, struct tls_connection *conn,
		   char *buf, size_t buflen)
{
	return -1;
}


int tls_connection_enable_workaround(void *tls_ctx,
				     struct tls_connection *conn)
{
	return -1;
}


int tls_connection_client_hello_ext(void *tls_ctx, struct tls_connection *conn,
				    int ext_type, const u8 *data,
				    size_t data_len)
{
	return -1;
}


int tls_connection_get_failed(void *tls_ctx, struct tls_connection *conn)
{
	return 0;
}


int tls_connection_get_read_alerts(void *tls_ctx, struct tls_connection *conn)
{
	return 0;
}


int tls_connection_get_write_alerts(void *tls_ctx,
				    struct tls_connection *conn)
{
	return 0;
}


int tls_connection_get_keyblock_size(void *tls_ctx,
				     struct tls_connection *conn)
{
	return -1;
}


unsigned int tls_capabilities(void *tls_ctx)
{
	return 0;
}


struct wpabuf * tls_connection_ia_send_phase_finished(
	void *tls_ctx, struct tls_connection *conn, int final)
{
	return NULL;
}


int tls_connection_ia_final_phase_finished(void *tls_ctx,
					   struct tls_connection *conn)
{
	return -1;
}


int tls_connection_ia_permute_inner_secret(void *tls_ctx,
					   struct tls_connection *conn,
					   const u8 *key, size_t key_len)
{
	return -1;
}


int tls_connection_set_session_ticket_cb(void *tls_ctx,
					 struct tls_connection *conn,
					 tls_session_ticket_cb cb,
					 void *ctx)
{
	return -1;
}
