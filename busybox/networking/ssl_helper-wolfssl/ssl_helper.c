/*
 * Adapted from:
 *
 * client.c
 *
 * Copyright (C) 2006-2015 wolfSSL Inc.
 *
 * This file is part of wolfSSL. (formerly known as CyaSSL)
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <poll.h>
#include <sys/socket.h>

#include <wolfssl/wolfcrypt/types.h>
#include <wolfssl/ssl.h>

#if 0
# define dbg(...) say(__VA_ARGS__)
#else
# define dbg(...) ((void)0)
#endif

static ssize_t safe_write(int fd, const void *buf, size_t count)
{
	ssize_t n;

	do {
		n = write(fd, buf, count);
	} while (n < 0 && errno == EINTR);

	return n;
}

static ssize_t full_write(int fd, const void *buf, size_t len)
{
	ssize_t cc;
	ssize_t total;

	total = 0;

	while (len) {
		cc = safe_write(fd, buf, len);

		if (cc < 0) {
			if (total) {
				/* we already wrote some! */
				/* user can do another write to know the error code */
				return total;
			}
			return cc;  /* write() returns -1 on failure. */
		}

		total += cc;
		buf = ((const char *)buf) + cc;
		len -= cc;
	}

	return total;
}

static void say(const char *s, ...)
{
	char buf[256];
	va_list p;
	int sz;

	va_start(p, s);
	sz = vsnprintf(buf, sizeof(buf), s, p);
	full_write(STDERR_FILENO, buf, sz >= 0 && sz < sizeof(buf) ? sz : strlen(buf));
	va_end(p);
}

static void die(const char *s, ...)
{
	char buf[256];
	va_list p;
	int sz;

	va_start(p, s);
	sz = vsnprintf(buf, sizeof(buf), s, p);
	full_write(STDERR_FILENO, buf, sz >= 0 && sz < sizeof(buf) ? sz : strlen(buf));
	exit(1);
	va_end(p);
}

static void err_sys(const char *msg)
{
	die("%s\n", msg);
}

/* ==== */

#if 0
static void showPeer(WOLFSSL* ssl)
{
	WOLFSSL_CIPHER* cipher;
	WOLFSSL_X509* peer = wolfSSL_get_peer_certificate(ssl);
	if (peer)
		ShowX509(peer, "peer's cert info:");
	else
		say("peer has no cert!\n");
	say("SSL version is %s\n", wolfSSL_get_version(ssl));

	cipher = wolfSSL_get_current_cipher(ssl);
	say("SSL cipher suite is %s\n", wolfSSL_CIPHER_get_name(cipher));

	{
		WOLFSSL_X509_CHAIN* chain = wolfSSL_get_peer_chain(ssl);
		int count = wolfSSL_get_chain_count(chain);
		int i;

		for (i = 0; i < count; i++) {
			int length;
			unsigned char buffer[3072];
			WOLFSSL_X509* chainX509;

			wolfSSL_get_chain_cert_pem(chain, i, buffer, sizeof(buffer), &length);
			buffer[length] = 0;
			say("cert %d has length %d data = \n%s\n", i, length, buffer);

			chainX509 = wolfSSL_get_chain_X509(chain, i);
			if (chainX509)
				ShowX509(chainX509, "session cert info:");
			else
				say("get_chain_X509 failed\n");
			wolfSSL_FreeX509(chainX509);
		}
	}
}
#endif

WOLFSSL *prepare(int sockfd)
{
	WOLFSSL_METHOD* method;
	WOLFSSL_CTX* ctx;
	WOLFSSL* ssl;

	wolfSSL_Init();

	method = wolfTLSv1_1_client_method();
	if (method == NULL)
		err_sys("out of memory");
	ctx = wolfSSL_CTX_new(method);
	if (ctx == NULL)
		err_sys("out of memory");
//	if (cipherList)
//		if (wolfSSL_CTX_set_cipher_list(ctx, cipherList) != SSL_SUCCESS)
//			err_sys("client can't set cipher list 1");

//	if (fewerPackets)
//		wolfSSL_CTX_set_group_messages(ctx);

//#ifndef NO_DH
//	wolfSSL_CTX_SetMinDhKey_Sz(ctx, (word16)minDhKeyBits);
//#endif

//	if (usePsk) {
//		wolfSSL_CTX_set_psk_client_callback(ctx, my_psk_client_cb);
//		if (cipherList == NULL) {
//			const char *defaultCipherList;
//#if defined(HAVE_AESGCM) && !defined(NO_DH)
//			defaultCipherList = "DHE-PSK-AES128-GCM-SHA256";
//#elif defined(HAVE_NULL_CIPHER)
//			defaultCipherList = "PSK-NULL-SHA256";
//#else
//			defaultCipherList = "PSK-AES128-CBC-SHA256";
//#endif
//			if (wolfSSL_CTX_set_cipher_list(ctx,defaultCipherList) != SSL_SUCCESS)
//				err_sys("client can't set cipher list 2");
//		}
//		useClientCert = 0;
//	}

//	if (useAnon) {
//		if (cipherList == NULL) {
//			wolfSSL_CTX_allow_anon_cipher(ctx);
//			if (wolfSSL_CTX_set_cipher_list(ctx,"ADH-AES128-SHA") != SSL_SUCCESS)
//				err_sys("client can't set cipher list 4");
//		}
//		useClientCert = 0;
//	}

//#if defined(OPENSSL_EXTRA) || defined(HAVE_WEBSERVER)
//	wolfSSL_CTX_set_default_passwd_cb(ctx, PasswordCallBack);
//#endif

//	if (useOcsp) {
//		if (ocspUrl != NULL) {
//			wolfSSL_CTX_SetOCSP_OverrideURL(ctx, ocspUrl);
//			wolfSSL_CTX_EnableOCSP(ctx, WOLFSSL_OCSP_NO_NONCE
//					| WOLFSSL_OCSP_URL_OVERRIDE);
//		}
//		else
//			wolfSSL_CTX_EnableOCSP(ctx, WOLFSSL_OCSP_NO_NONCE);
//	}
//
//#ifdef USER_CA_CB
//	wolfSSL_CTX_SetCACb(ctx, CaCb);
//#endif
//
//#ifdef VERIFY_CALLBACK
//	wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, myVerify);
//#endif
//#if !defined(NO_FILESYSTEM) && !defined(NO_CERTS)
//	if (useClientCert) {
//		if (wolfSSL_CTX_use_certificate_chain_file(ctx, ourCert) != SSL_SUCCESS)
//			err_sys("can't load client cert file, check file and run from"
//				" wolfSSL home dir");
//		if (wolfSSL_CTX_use_PrivateKey_file(ctx, ourKey, SSL_FILETYPE_PEM) != SSL_SUCCESS)
//			err_sys("can't load client private key file, check file and run "
//				"from wolfSSL home dir");
//	}
//
//	if (!usePsk && !useAnon) {
//		if (wolfSSL_CTX_load_verify_locations(ctx, verifyCert,0) != SSL_SUCCESS)
//			err_sys("can't load ca file, Please run from wolfSSL home dir");
//#ifdef HAVE_ECC
//		/* load ecc verify too, echoserver uses it by default w/ ecc */
//		if (wolfSSL_CTX_load_verify_locations(ctx, eccCert, 0) != SSL_SUCCESS)
//			err_sys("can't load ecc ca file, Please run from wolfSSL home dir");
//#endif
//	}
//#endif /* !NO_FILESYSTEM && !NO_CERTS */

//#if !defined(NO_CERTS)
//	if (!usePsk && !useAnon && doPeerCheck == 0)
//		wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, 0);
//	if (!usePsk && !useAnon && overrideDateErrors == 1)
//		wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, myDateCb);
//#endif

	wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, 0);

//#ifdef HAVE_SNI
//	if (sniHostName)
//		if (wolfSSL_CTX_UseSNI(ctx, 0, sniHostName, XSTRLEN(sniHostName)) != SSL_SUCCESS)
//			err_sys("UseSNI failed");
//#endif

//#ifdef HAVE_MAX_FRAGMENT
//	if (maxFragment)
//		if (wolfSSL_CTX_UseMaxFragment(ctx, maxFragment) != SSL_SUCCESS)
//			err_sys("UseMaxFragment failed");
//#endif
//#ifdef HAVE_TRUNCATED_HMAC
//	if (truncatedHMAC)
//		if (wolfSSL_CTX_UseTruncatedHMAC(ctx) != SSL_SUCCESS)
//			err_sys("UseTruncatedHMAC failed");
//#endif
//#ifdef HAVE_SESSION_TICKET
//	if (wolfSSL_CTX_UseSessionTicket(ctx) != SSL_SUCCESS)
//		err_sys("UseSessionTicket failed");
//#endif

//#if defined(WOLFSSL_MDK_ARM)
//	wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, 0);
//#endif

	ssl = wolfSSL_new(ctx);
	if (ssl == NULL)
		err_sys("out of memory");

//#ifdef HAVE_SESSION_TICKET
//	wolfSSL_set_SessionTicket_cb(ssl, sessionTicketCB, (void*)"initial session");
//#endif

//	if (doDTLS) {
//		SOCKADDR_IN_T addr;
//		build_addr(&addr, host, port, 1);
//		wolfSSL_dtls_set_peer(ssl, &addr, sizeof(addr));
//		tcp_socket(&sockfd, 1);
//	} wlse {
//		tcp_connect(&sockfd, host, port, 0);
//	}

//#ifdef HAVE_POLY1305
//	/* use old poly to connect with google server */
//	if (!XSTRNCMP(domain, "www.google.com", 14)) {
//		if (wolfSSL_use_old_poly(ssl, 1) != 0)
//			err_sys("unable to set to old poly");
//	}
//#endif

	wolfSSL_set_fd(ssl, sockfd);

//#ifdef HAVE_CRL
//	if (disableCRL == 0) {
//		if (wolfSSL_EnableCRL(ssl, WOLFSSL_CRL_CHECKALL) != SSL_SUCCESS)
//			err_sys("can't enable crl check");
//		if (wolfSSL_LoadCRL(ssl, crlPemDir, SSL_FILETYPE_PEM, 0) != SSL_SUCCESS)
//			err_sys("can't load crl, check crlfile and date validity");
//		if (wolfSSL_SetCRL_Cb(ssl, CRL_CallBack) != SSL_SUCCESS)
//			err_sys("can't set crl callback");
//	}
//#endif
//#ifdef HAVE_SECURE_RENEGOTIATION
//	if (scr) {
//		if (wolfSSL_UseSecureRenegotiation(ssl) != SSL_SUCCESS)
//			err_sys("can't enable secure renegotiation");
//	}
//#endif
//#ifdef ATOMIC_USER
//	if (atomicUser)
//		SetupAtomicUser(ctx, ssl);
//#endif
//#ifdef HAVE_PK_CALLBACKS
//	if (pkCallbacks)
//		SetupPkCallbacks(ctx, ssl);
//#endif
//	if (matchName && doPeerCheck)
//		wolfSSL_check_domain_name(ssl, domain);

	if (wolfSSL_connect(ssl) != SSL_SUCCESS) {
//		/* see note at top of README */
//		int  err = wolfSSL_get_error(ssl, 0);
//		char buffer[WOLFSSL_MAX_ERROR_SZ];
//		say("err = %d, %s\n", err,
//			wolfSSL_ERR_error_string(err, buffer));
		err_sys("SSL_connect failed");
	}
//	showPeer(ssl);

//#ifdef HAVE_SECURE_RENEGOTIATION
//	if (scr && forceScr) {
//		if (wolfSSL_Rehandshake(ssl) != SSL_SUCCESS) {
//			int err = wolfSSL_get_error(ssl, 0);
//			char buffer[WOLFSSL_MAX_ERROR_SZ];
//			say("err = %d, %s\n", err,
//				wolfSSL_ERR_error_string(err, buffer));
//			err_sys("wolfSSL_Rehandshake failed");
//		}
//	}
//#endif

	return ssl;
}

static struct pollfd pfd[2] = {
	{ -1, POLLIN|POLLERR|POLLHUP, 0 },
	{ -1, POLLIN|POLLERR|POLLHUP, 0 },
};
#define STDIN           pfd[0]
#define NETWORK         pfd[1]
#define STDIN_READY()   (pfd[0].revents & (POLLIN|POLLERR|POLLHUP))
#define NETWORK_READY() (pfd[1].revents & (POLLIN|POLLERR|POLLHUP))

static void wait_for_input(void)
{
	if (STDIN.fd == NETWORK.fd) /* means both are -1 */
		exit(0);
	dbg("polling\n");
	STDIN.revents = NETWORK.revents = 0;
	while (poll(pfd, 2, -1) < 0 && errno == EINTR)
		continue;
}

static void do_io_until_eof_and_exit(WOLFSSL *ssl, int fd)
{
	int len;
	char ibuf[4 * 1024];

	NETWORK.fd = fd;
	STDIN.fd = 0;

	len = 0; /* only to suppress compiler warning */
	for (;;) {
		wait_for_input();

		if (STDIN_READY()) {
			dbg("reading stdin\n");
			len = read(STDIN_FILENO, ibuf, sizeof(ibuf));
			if (len < 0)
				die("read error on stdin\n");
			if (len == 0) {
				dbg("read len = 0, stdin not polled anymore\n");
				STDIN.fd = -1;
			} else {
				int n = wolfSSL_write(ssl, ibuf, len);
				if (n != len)
					die("SSL_write(%d) failed (returned %d)\n", len, n);
			}
		}

		if (NETWORK_READY()) {
			dbg("%s%s%s\n",
				(pfd[1].revents & POLLIN)  ? "POLLIN"  : "",
				(pfd[1].revents & POLLERR) ? "|POLLERR" : "",
				(pfd[1].revents & POLLHUP) ? "|POLLHUP" : ""
			);
/* We are using blocking socket here.
 * (Nonblocking socket would complicate writing to it).
 * Therefore, SSL_read _can block_ here.
 * This is not what wget expects (it wants to see short reads).
 * Therefore, we use smallish buffer here, to approximate that.
 */
			len = wolfSSL_read(ssl, ibuf,
				sizeof(ibuf) < 1024 ? sizeof(ibuf) : 1024
			);
			if (len < 0)
				die("SSL_read error on network (%d)\n", len);
			if (len > 0) {
				int n;
				n = full_write(STDOUT_FILENO, ibuf, len);
				if (n != len)
					die("write(%d) to stdout returned %d\n", len, n);
				continue;
			}
/* Blocking reads are easier wtr EOF detection (no EAGAIN error to check for) */
			dbg("read len = 0, network not polled anymore\n");
			NETWORK.fd = -1;
			/* saw EOF on network, and we processed
			 * and wrote out all ssl data. Signal it:
			 */
			close(STDOUT_FILENO);
		}
	}
}

int main(int argc, char **argv)
{
	WOLFSSL *ssl;
	int fd;
	char *fd_str;

	if (!argv[1])
		die("Syntax error\n");
	if (argv[1][0] != '-')
		die("Syntax error\n");
	if (argv[1][1] != 'd')
		die("Syntax error\n");
	fd_str = argv[1] + 2;
	if (!fd_str[0])
		fd_str = argv[2];
	if (!fd_str || fd_str[0] < '0' || fd_str[0] > '9')
		die("Syntax error\n");

	fd = atoi(fd_str);
	if (fd < 3)
		die("Syntax error\n");

	ssl = prepare(fd);
	do_io_until_eof_and_exit(ssl, fd);
	/* does not return */

//	if (doDTLS == 0) { /* don't send alert after "break" command */
//		ret = wolfSSL_shutdown(ssl);
//		if (wc_shutdown && ret == SSL_SHUTDOWN_NOT_DONE)
//			wolfSSL_shutdown(ssl); /* bidirectional shutdown */
//	}
//#ifdef ATOMIC_USER
//	if (atomicUser)
//		FreeAtomicUser(ssl);
//#endif
//	wolfSSL_free(ssl);
//	CloseSocket(sockfd);
//	wolfSSL_CTX_free(ctx);

	return 0;
}
