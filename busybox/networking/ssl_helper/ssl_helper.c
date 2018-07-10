/*
 * Copyright (c) 2013 INSIDE Secure Corporation
 * Copyright (c) PeerSec Networks, 2002-2011
 * All Rights Reserved
 *
 * The latest version of this code is available at http://www.matrixssl.org
 *
 * This software is open source; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <poll.h>
#include <sys/socket.h>

#include "matrixssl/matrixsslApi.h"

//#warning "DO NOT USE THESE DEFAULT KEYS IN PRODUCTION ENVIRONMENTS."

/*
 * If supporting client authentication, pick ONE identity to auto select a
 * certificate and private key that support desired algorithms.
 */
#define ID_RSA /* RSA Certificate and Key */

#define USE_HEADER_KEYS

/* If the algorithm type is supported, load a CA for it */
#ifdef USE_HEADER_KEYS
/* CAs */
# include "sampleCerts/RSA/ALL_RSA_CAS.h"
/* Identity Certs and Keys for use with Client Authentication */
# ifdef ID_RSA
#  define EXAMPLE_RSA_KEYS
#  include "sampleCerts/RSA/2048_RSA.h"
#  include "sampleCerts/RSA/2048_RSA_KEY.h"
# endif
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

#if 0
# define dbg(...) say(__VA_ARGS__)
#else
# define dbg(...) ((void)0)
#endif

static struct pollfd pfd[2] = {
	{ -1, POLLIN|POLLERR|POLLHUP, 0 },
	{ -1, POLLIN|POLLERR|POLLHUP, 0 },
};
#define STDIN           pfd[0]
#define NETWORK         pfd[1]
#define STDIN_READY()   (pfd[0].revents & (POLLIN|POLLERR|POLLHUP))
#define NETWORK_READY() (pfd[1].revents & (POLLIN|POLLERR|POLLHUP))

static int wait_for_input(void)
{
	if (STDIN.fd == NETWORK.fd) /* means both are -1 */
		exit(0);
	dbg("polling\n");
	STDIN.revents = NETWORK.revents = 0;
	return poll(pfd, 2, -1);
}

static int32 certCb(ssl_t *ssl, psX509Cert_t *cert, int32 alert)
{
	/* Example to allow anonymous connections based on a define */
	if (alert > 0) {
		return SSL_ALLOW_ANON_CONNECTION; // = 254
	}
#if 0
	/* Validate the 'not before' and 'not after' dates, etc */
	return PS_FAILURE; /* if we don't like this cert */
#endif
	return PS_SUCCESS;
}

static void close_conn_and_exit(ssl_t *ssl, int fd)
{
	unsigned char *buf;
	int len;

	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	/* Quick attempt to send a closure alert, don't worry about failure */
	if (matrixSslEncodeClosureAlert(ssl) >= 0) {
		len = matrixSslGetOutdata(ssl, &buf);
		if (len > 0) {
			len = safe_write(fd, buf, len);
			//if (len > 0) {
			//	matrixSslSentData(ssl, len);
			//}
		}
	}
	//matrixSslDeleteSession(ssl);
	shutdown(fd, SHUT_WR);
	exit(0);
}

static int encode_data(ssl_t *ssl, const void *data, int len)
{
	unsigned char *buf;
	int available;

	available = matrixSslGetWritebuf(ssl, &buf, len);
	if (available < 0)
		die("matrixSslGetWritebuf\n");
	if (len > available)
		die("len > available\n");
	memcpy(buf, data, len);
	if (matrixSslEncodeWritebuf(ssl, len) < 0)
		die("matrixSslEncodeWritebuf\n");
	return len;
}

static void flush_to_net(ssl_t *ssl, int fd)
{
	int rc;
	int len;
	unsigned char *buf;

	while ((len = matrixSslGetOutdata(ssl, &buf)) > 0) {
		dbg("writing net %d bytes\n", len);
		if (full_write(fd, buf, len) != len)
			die("write to network\n");
		rc = matrixSslSentData(ssl, len);
		if (rc < 0)
			die("matrixSslSentData\n");
	}
}

static void do_io_until_eof_and_exit(int fd, sslKeys_t *keys)
{
	int rc;
	int len;
	uint32_t len32u;
	sslSessionId_t *sid;
	ssl_t *ssl;
	unsigned char *buf;

	NETWORK.fd = fd;
	/* Note! STDIN.fd is disabled (-1) until SSL handshake is over:
	 * we do not attempt to feed any user data to MatrixSSL
	 * before it is ready.
	 */

	matrixSslNewSessionId(&sid);
	rc = matrixSslNewClientSession(&ssl, keys, sid, 0, certCb, NULL, NULL, 0);
dbg("matrixSslNewClientSession:rc=%d\n", rc);
	if (rc != MATRIXSSL_REQUEST_SEND)
		die("matrixSslNewClientSession\n");

	len = 0; /* only to suppress compiler warning */
 again:
	switch (rc) {
	case MATRIXSSL_REQUEST_SEND:
		dbg("MATRIXSSL_REQUEST_SEND\n");
		flush_to_net(ssl, fd);
		goto poll_input;

	case 0:
		dbg("rc==0\n");
		flush_to_net(ssl, fd);
		goto poll_input;

	case MATRIXSSL_REQUEST_CLOSE:
		/* what does this mean if we are here? */
		dbg("MATRIXSSL_REQUEST_CLOSE\n");
		close_conn_and_exit(ssl, fd);

	case MATRIXSSL_HANDSHAKE_COMPLETE:
		dbg("MATRIXSSL_HANDSHAKE_COMPLETE\n");
		/* Init complete, can start reading local user's data: */
		STDIN.fd = STDIN_FILENO;
 poll_input:
		wait_for_input();
		if (STDIN_READY()) {
			char ibuf[4 * 1024];
			dbg("reading stdin\n");
			len = read(STDIN_FILENO, ibuf, sizeof(ibuf));
			if (len < 0)
				die("read error on stdin\n");
			if (len == 0)
				STDIN.fd = -1;
			else {
				len = encode_data(ssl, ibuf, len);
				if (len) {
					rc = MATRIXSSL_REQUEST_SEND;
dbg("rc=%d\n", rc);
					goto again;
				}
			}
		}
 read_network:
		if (NETWORK_READY()) {
			dbg("%s%s%s\n",
				(pfd[1].revents & POLLIN)  ? "POLLIN"  : "",
				(pfd[1].revents & POLLERR) ? "|POLLERR" : "",
				(pfd[1].revents & POLLHUP) ? "|POLLHUP" : ""
			);
			len = matrixSslGetReadbuf(ssl, &buf);
			if (len <= 0)
				die("matrixSslGetReadbuf\n");
			dbg("reading net up to %d\n", len);
			len = read(fd, buf, len);
			dbg("reading net:%d\n", len);
			if (len < 0)
				die("read error on network\n");
			if (len == 0) /*eof*/
				NETWORK.fd = -1;
			len32u = len;
			rc = matrixSslReceivedData(ssl, len, &buf, &len32u);
dbg("matrixSslReceivedData:rc=%d\n", rc);
			len = len32u;
			if (rc < 0)
				die("matrixSslReceivedData\n");
		}
		goto again;

	case MATRIXSSL_APP_DATA:
		dbg("MATRIXSSL_APP_DATA: writing stdout\n");
		do {
			if (full_write(STDOUT_FILENO, buf, len) != len)
				die("write to stdout\n");
			len32u = len;
			rc = matrixSslProcessedData(ssl, &buf, &len32u);
//this was seen returning rc=0:
dbg("matrixSslProcessedData:rc=%d\n", rc);
			len = len32u;
		} while (rc == MATRIXSSL_APP_DATA);
		if (pfd[1].fd == -1) {
			/* Already saw EOF on network, and we processed
			 * and wrote out all ssl data. Signal it:
			 */
			close(STDOUT_FILENO);
		}
		goto again;

	case MATRIXSSL_REQUEST_RECV:
		dbg("MATRIXSSL_REQUEST_RECV\n");
		wait_for_input();
		goto read_network;

	case MATRIXSSL_RECEIVED_ALERT:
		dbg("MATRIXSSL_RECEIVED_ALERT\n");
		/* The first byte of the buffer is the level */
		/* The second byte is the description */
		if (buf[0] == SSL_ALERT_LEVEL_FATAL)
			die("Fatal alert\n");
		/* Closure alert is normal (and best) way to close */
		if (buf[1] == SSL_ALERT_CLOSE_NOTIFY)
			close_conn_and_exit(ssl, fd);
		die("Warning alert\n");
		len32u = len;
		rc = matrixSslProcessedData(ssl, &buf, &len32u);
dbg("matrixSslProcessedData:rc=%d\n", rc);
		len = len32u;
		goto again;

	default:
		/* If rc < 0 it is an error */
		die("bad rc:%d\n", rc);
	}
}

static sslKeys_t* make_keys(void)
{
	int rc, CAstreamLen;
	char *CAstream;
	sslKeys_t *keys;

	if (matrixSslNewKeys(&keys) < 0)
		die("matrixSslNewKeys\n");

#ifdef USE_HEADER_KEYS
	/*
	 * In-memory based keys
	 * Build the CA list first for potential client auth usage
	 */
	CAstream = NULL;
	CAstreamLen = sizeof(RSACAS);
	if (CAstreamLen > 0) {
		CAstream = psMalloc(NULL, CAstreamLen);
		memcpy(CAstream, RSACAS, sizeof(RSACAS));
	}

 #ifdef ID_RSA
	rc = matrixSslLoadRsaKeysMem(keys, RSA2048, sizeof(RSA2048),
			RSA2048KEY, sizeof(RSA2048KEY), (unsigned char*)CAstream,
			CAstreamLen);
	if (rc < 0)
		die("matrixSslLoadRsaKeysMem\n");
 #endif

	if (CAstream)
		psFree(CAstream);
#endif /* USE_HEADER_KEYS */
	return keys;
}

int main(int argc, char **argv)
{
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

	if (matrixSslOpen() < 0)
		die("matrixSslOpen\n");

	do_io_until_eof_and_exit(fd, make_keys());
	/* does not return */

	return 0;
}
