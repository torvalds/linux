/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "bearssl.h"

/*
 * Connect to the specified host and port. The connected socket is
 * returned, or -1 on error.
 */
static int
host_connect(const char *host, const char *port)
{
	struct addrinfo hints, *si, *p;
	int fd;
	int err;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	err = getaddrinfo(host, port, &hints, &si);
	if (err != 0) {
		fprintf(stderr, "ERROR: getaddrinfo(): %s\n",
			gai_strerror(err));
		return -1;
	}
	fd = -1;
	for (p = si; p != NULL; p = p->ai_next) {
		struct sockaddr *sa;
		void *addr;
		char tmp[INET6_ADDRSTRLEN + 50];

		sa = (struct sockaddr *)p->ai_addr;
		if (sa->sa_family == AF_INET) {
			addr = &((struct sockaddr_in *)sa)->sin_addr;
		} else if (sa->sa_family == AF_INET6) {
			addr = &((struct sockaddr_in6 *)sa)->sin6_addr;
		} else {
			addr = NULL;
		}
		if (addr != NULL) {
			inet_ntop(p->ai_family, addr, tmp, sizeof tmp);
		} else {
			sprintf(tmp, "<unknown family: %d>",
				(int)sa->sa_family);
		}
		fprintf(stderr, "connecting to: %s\n", tmp);
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (fd < 0) {
			perror("socket()");
			continue;
		}
		if (connect(fd, p->ai_addr, p->ai_addrlen) < 0) {
			perror("connect()");
			close(fd);
			continue;
		}
		break;
	}
	if (p == NULL) {
		freeaddrinfo(si);
		fprintf(stderr, "ERROR: failed to connect\n");
		return -1;
	}
	freeaddrinfo(si);
	fprintf(stderr, "connected.\n");
	return fd;
}

/*
 * Low-level data read callback for the simplified SSL I/O API.
 */
static int
sock_read(void *ctx, unsigned char *buf, size_t len)
{
	for (;;) {
		ssize_t rlen;

		rlen = read(*(int *)ctx, buf, len);
		if (rlen <= 0) {
			if (rlen < 0 && errno == EINTR) {
				continue;
			}
			return -1;
		}
		return (int)rlen;
	}
}

/*
 * Low-level data write callback for the simplified SSL I/O API.
 */
static int
sock_write(void *ctx, const unsigned char *buf, size_t len)
{
	for (;;) {
		ssize_t wlen;

		wlen = write(*(int *)ctx, buf, len);
		if (wlen <= 0) {
			if (wlen < 0 && errno == EINTR) {
				continue;
			}
			return -1;
		}
		return (int)wlen;
	}
}

/*
 * The hardcoded trust anchors. These are the two DN + public key that
 * correspond to the self-signed certificates cert-root-rsa.pem and
 * cert-root-ec.pem.
 *
 * C code for hardcoded trust anchors can be generated with the "brssl"
 * command-line tool (with the "ta" command).
 */

static const unsigned char TA0_DN[] = {
	0x30, 0x1C, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
	0x02, 0x43, 0x41, 0x31, 0x0D, 0x30, 0x0B, 0x06, 0x03, 0x55, 0x04, 0x03,
	0x13, 0x04, 0x52, 0x6F, 0x6F, 0x74
};

static const unsigned char TA0_RSA_N[] = {
	0xB6, 0xD9, 0x34, 0xD4, 0x50, 0xFD, 0xB3, 0xAF, 0x7A, 0x73, 0xF1, 0xCE,
	0x38, 0xBF, 0x5D, 0x6F, 0x45, 0xE1, 0xFD, 0x4E, 0xB1, 0x98, 0xC6, 0x60,
	0x83, 0x26, 0xD2, 0x17, 0xD1, 0xC5, 0xB7, 0x9A, 0xA3, 0xC1, 0xDE, 0x63,
	0x39, 0x97, 0x9C, 0xF0, 0x5E, 0x5C, 0xC8, 0x1C, 0x17, 0xB9, 0x88, 0x19,
	0x6D, 0xF0, 0xB6, 0x2E, 0x30, 0x50, 0xA1, 0x54, 0x6E, 0x93, 0xC0, 0xDB,
	0xCF, 0x30, 0xCB, 0x9F, 0x1E, 0x27, 0x79, 0xF1, 0xC3, 0x99, 0x52, 0x35,
	0xAA, 0x3D, 0xB6, 0xDF, 0xB0, 0xAD, 0x7C, 0xCB, 0x49, 0xCD, 0xC0, 0xED,
	0xE7, 0x66, 0x10, 0x2A, 0xE9, 0xCE, 0x28, 0x1F, 0x21, 0x50, 0xFA, 0x77,
	0x4C, 0x2D, 0xDA, 0xEF, 0x3C, 0x58, 0xEB, 0x4E, 0xBF, 0xCE, 0xE9, 0xFB,
	0x1A, 0xDA, 0xA3, 0x83, 0xA3, 0xCD, 0xA3, 0xCA, 0x93, 0x80, 0xDC, 0xDA,
	0xF3, 0x17, 0xCC, 0x7A, 0xAB, 0x33, 0x80, 0x9C, 0xB2, 0xD4, 0x7F, 0x46,
	0x3F, 0xC5, 0x3C, 0xDC, 0x61, 0x94, 0xB7, 0x27, 0x29, 0x6E, 0x2A, 0xBC,
	0x5B, 0x09, 0x36, 0xD4, 0xC6, 0x3B, 0x0D, 0xEB, 0xBE, 0xCE, 0xDB, 0x1D,
	0x1C, 0xBC, 0x10, 0x6A, 0x71, 0x71, 0xB3, 0xF2, 0xCA, 0x28, 0x9A, 0x77,
	0xF2, 0x8A, 0xEC, 0x42, 0xEF, 0xB1, 0x4A, 0x8E, 0xE2, 0xF2, 0x1A, 0x32,
	0x2A, 0xCD, 0xC0, 0xA6, 0x46, 0x2C, 0x9A, 0xC2, 0x85, 0x37, 0x91, 0x7F,
	0x46, 0xA1, 0x93, 0x81, 0xA1, 0x74, 0x66, 0xDF, 0xBA, 0xB3, 0x39, 0x20,
	0x91, 0x93, 0xFA, 0x1D, 0xA1, 0xA8, 0x85, 0xE7, 0xE4, 0xF9, 0x07, 0xF6,
	0x10, 0xF6, 0xA8, 0x27, 0x01, 0xB6, 0x7F, 0x12, 0xC3, 0x40, 0xC3, 0xC9,
	0xE2, 0xB0, 0xAB, 0x49, 0x18, 0x3A, 0x64, 0xB6, 0x59, 0xB7, 0x95, 0xB5,
	0x96, 0x36, 0xDF, 0x22, 0x69, 0xAA, 0x72, 0x6A, 0x54, 0x4E, 0x27, 0x29,
	0xA3, 0x0E, 0x97, 0x15
};

static const unsigned char TA0_RSA_E[] = {
	0x01, 0x00, 0x01
};

static const unsigned char TA1_DN[] = {
	0x30, 0x1C, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
	0x02, 0x43, 0x41, 0x31, 0x0D, 0x30, 0x0B, 0x06, 0x03, 0x55, 0x04, 0x03,
	0x13, 0x04, 0x52, 0x6F, 0x6F, 0x74
};

static const unsigned char TA1_EC_Q[] = {
	0x04, 0x71, 0x74, 0xBA, 0xAB, 0xB9, 0x30, 0x2E, 0x81, 0xD5, 0xE5, 0x57,
	0xF9, 0xF3, 0x20, 0x68, 0x0C, 0x9C, 0xF9, 0x64, 0xDB, 0xB4, 0x20, 0x0D,
	0x6D, 0xEA, 0x40, 0xD0, 0x4A, 0x6E, 0x42, 0xFD, 0xB6, 0x9A, 0x68, 0x25,
	0x44, 0xF6, 0xDF, 0x7B, 0xC4, 0xFC, 0xDE, 0xDD, 0x7B, 0xBB, 0xC5, 0xDB,
	0x7C, 0x76, 0x3F, 0x41, 0x66, 0x40, 0x6E, 0xDB, 0xA7, 0x87, 0xC2, 0xE5,
	0xD8, 0xC5, 0xF3, 0x7F, 0x8D
};

static const br_x509_trust_anchor TAs[2] = {
	{
		{ (unsigned char *)TA0_DN, sizeof TA0_DN },
		BR_X509_TA_CA,
		{
			BR_KEYTYPE_RSA,
			{ .rsa = {
				(unsigned char *)TA0_RSA_N, sizeof TA0_RSA_N,
				(unsigned char *)TA0_RSA_E, sizeof TA0_RSA_E,
			} }
		}
	},
	{
		{ (unsigned char *)TA1_DN, sizeof TA1_DN },
		BR_X509_TA_CA,
		{
			BR_KEYTYPE_EC,
			{ .ec = {
				BR_EC_secp256r1,
				(unsigned char *)TA1_EC_Q, sizeof TA1_EC_Q,
			} }
		}
	}
};

#define TAs_NUM   2

/*
 * Main program: this is a simple program that expects 2 or 3 arguments.
 * The first two arguments are a hostname and a port; the program will
 * open a SSL connection with that server and port. It will then send
 * a simple HTTP GET request, using the third argument as target path
 * ("/" is used as path if no third argument was provided). The HTTP
 * response, complete with header and contents, is received and written
 * on stdout.
 */
int
main(int argc, char *argv[])
{
	const char *host, *port, *path;
	int fd;
	br_ssl_client_context sc;
	br_x509_minimal_context xc;
	unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
	br_sslio_context ioc;

	/*
	 * Parse command-line argument: host, port, and path. The path
	 * is optional; if absent, "/" is used.
	 */
	if (argc < 3 || argc > 4) {
		return EXIT_FAILURE;
	}
	host = argv[1];
	port = argv[2];
	if (argc == 4) {
		path = argv[3];
	} else {
		path = "/";
	}

	/*
	 * Ignore SIGPIPE to avoid crashing in case of abrupt socket close.
	 */
	signal(SIGPIPE, SIG_IGN);

	/*
	 * Open the socket to the target server.
	 */
	fd = host_connect(host, port);
	if (fd < 0) {
		return EXIT_FAILURE;
	}

	/*
	 * Initialise the client context:
	 * -- Use the "full" profile (all supported algorithms).
	 * -- The provided X.509 validation engine is initialised, with
	 *    the hardcoded trust anchor.
	 */
	br_ssl_client_init_full(&sc, &xc, TAs, TAs_NUM);

	/*
	 * Set the I/O buffer to the provided array. We allocated a
	 * buffer large enough for full-duplex behaviour with all
	 * allowed sizes of SSL records, hence we set the last argument
	 * to 1 (which means "split the buffer into separate input and
	 * output areas").
	 */
	br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof iobuf, 1);

	/*
	 * Reset the client context, for a new handshake. We provide the
	 * target host name: it will be used for the SNI extension. The
	 * last parameter is 0: we are not trying to resume a session.
	 */
	br_ssl_client_reset(&sc, host, 0);

	/*
	 * Initialise the simplified I/O wrapper context, to use our
	 * SSL client context, and the two callbacks for socket I/O.
	 */
	br_sslio_init(&ioc, &sc.eng, sock_read, &fd, sock_write, &fd);

	/*
	 * Note that while the context has, at that point, already
	 * assembled the ClientHello to send, nothing happened on the
	 * network yet. Real I/O will occur only with the next call.
	 *
	 * We write our simple HTTP request. We could test each call
	 * for an error (-1), but this is not strictly necessary, since
	 * the error state "sticks": if the context fails for any reason
	 * (e.g. bad server certificate), then it will remain in failed
	 * state and all subsequent calls will return -1 as well.
	 */
	br_sslio_write_all(&ioc, "GET ", 4);
	br_sslio_write_all(&ioc, path, strlen(path));
	br_sslio_write_all(&ioc, " HTTP/1.0\r\nHost: ", 17);
	br_sslio_write_all(&ioc, host, strlen(host));
	br_sslio_write_all(&ioc, "\r\n\r\n", 4);

	/*
	 * SSL is a buffered protocol: we make sure that all our request
	 * bytes are sent onto the wire.
	 */
	br_sslio_flush(&ioc);

	/*
	 * Read the server's response. We use here a small 512-byte buffer,
	 * but most of the buffering occurs in the client context: the
	 * server will send full records (up to 16384 bytes worth of data
	 * each), and the client context buffers one full record at a time.
	 */
	for (;;) {
		int rlen;
		unsigned char tmp[512];

		rlen = br_sslio_read(&ioc, tmp, sizeof tmp);
		if (rlen < 0) {
			break;
		}
		fwrite(tmp, 1, rlen, stdout);
	}

	/*
	 * Close the socket.
	 */
	close(fd);

	/*
	 * Check whether we closed properly or not. If the engine is
	 * closed, then its error status allows to distinguish between
	 * a normal closure and a SSL error.
	 *
	 * If the engine is NOT closed, then this means that the
	 * underlying network socket was closed or failed in some way.
	 * Note that many Web servers out there do not properly close
	 * their SSL connections (they don't send a close_notify alert),
	 * which will be reported here as "socket closed without proper
	 * SSL termination".
	 */
	if (br_ssl_engine_current_state(&sc.eng) == BR_SSL_CLOSED) {
		int err;

		err = br_ssl_engine_last_error(&sc.eng);
		if (err == 0) {
			fprintf(stderr, "closed.\n");
			return EXIT_SUCCESS;
		} else {
			fprintf(stderr, "SSL error %d\n", err);
			return EXIT_FAILURE;
		}
	} else {
		fprintf(stderr,
			"socket closed without proper SSL termination\n");
		return EXIT_FAILURE;
	}
}
