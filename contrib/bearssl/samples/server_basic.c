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
 * This sample code can use three possible certificate chains:
 * -- A full-RSA chain (server key is RSA, certificates are signed with RSA)
 * -- A full-EC chain (server key is EC, certificates are signed with ECDSA)
 * -- A mixed chain (server key is EC, certificates are signed with RSA)
 *
 * The macros below define which chain is selected. This impacts the list
 * of supported cipher suites.
 *
 * Other macros, which can be defined (with a non-zero value):
 *
 *   SERVER_PROFILE_MIN_FS
 *      Select a "minimal" profile with forward security (ECDHE cipher
 *      suite).
 *
 *   SERVER_PROFILE_MIN_NOFS
 *      Select a "minimal" profile without forward security (RSA or ECDH
 *      cipher suite, but not ECDHE).
 *
 *   SERVER_CHACHA20
 *      If SERVER_PROFILE_MIN_FS is selected, then this macro selects
 *      a cipher suite with ChaCha20+Poly1305; otherwise, AES/GCM is
 *      used. This macro has no effect otherwise, since there is no
 *      non-forward secure cipher suite that uses ChaCha20+Poly1305.
 */

#if !(SERVER_RSA || SERVER_EC || SERVER_MIXED)
#define SERVER_RSA     1
#define SERVER_EC      0
#define SERVER_MIXED   0
#endif

#if SERVER_RSA
#include "chain-rsa.h"
#include "key-rsa.h"
#define SKEY   RSA
#elif SERVER_EC
#include "chain-ec.h"
#include "key-ec.h"
#define SKEY   EC
#elif SERVER_MIXED
#include "chain-ec+rsa.h"
#include "key-ec.h"
#define SKEY   EC
#else
#error Must use one of RSA, EC or MIXED chains.
#endif

/*
 * Create a server socket bound to the specified host and port. If 'host'
 * is NULL, this will bind "generically" (all addresses).
 *
 * Returned value is the server socket descriptor, or -1 on error.
 */
static int
host_bind(const char *host, const char *port)
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
		struct sockaddr_in sa4;
		struct sockaddr_in6 sa6;
		size_t sa_len;
		void *addr;
		char tmp[INET6_ADDRSTRLEN + 50];
		int opt;

		sa = (struct sockaddr *)p->ai_addr;
		if (sa->sa_family == AF_INET) {
			sa4 = *(struct sockaddr_in *)sa;
			sa = (struct sockaddr *)&sa4;
			sa_len = sizeof sa4;
			addr = &sa4.sin_addr;
			if (host == NULL) {
				sa4.sin_addr.s_addr = INADDR_ANY;
			}
		} else if (sa->sa_family == AF_INET6) {
			sa6 = *(struct sockaddr_in6 *)sa;
			sa = (struct sockaddr *)&sa6;
			sa_len = sizeof sa6;
			addr = &sa6.sin6_addr;
			if (host == NULL) {
				sa6.sin6_addr = in6addr_any;
			}
		} else {
			addr = NULL;
			sa_len = p->ai_addrlen;
		}
		if (addr != NULL) {
			inet_ntop(p->ai_family, addr, tmp, sizeof tmp);
		} else {
			sprintf(tmp, "<unknown family: %d>",
				(int)sa->sa_family);
		}
		fprintf(stderr, "binding to: %s\n", tmp);
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (fd < 0) {
			perror("socket()");
			continue;
		}
		opt = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
		opt = 0;
		setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof opt);
		if (bind(fd, sa, sa_len) < 0) {
			perror("bind()");
			close(fd);
			continue;
		}
		break;
	}
	if (p == NULL) {
		freeaddrinfo(si);
		fprintf(stderr, "ERROR: failed to bind\n");
		return -1;
	}
	freeaddrinfo(si);
	if (listen(fd, 5) < 0) {
		perror("listen()");
		close(fd);
		return -1;
	}
	fprintf(stderr, "bound.\n");
	return fd;
}

/*
 * Accept a single client on the provided server socket. This is blocking.
 * On error, this returns -1.
 */
static int
accept_client(int server_fd)
{
	int fd;
	struct sockaddr sa;
	socklen_t sa_len;
	char tmp[INET6_ADDRSTRLEN + 50];
	const char *name;

	sa_len = sizeof sa;
	fd = accept(server_fd, &sa, &sa_len);
	if (fd < 0) {
		perror("accept()");
		return -1;
	}
	name = NULL;
	switch (sa.sa_family) {
	case AF_INET:
		name = inet_ntop(AF_INET,
			&((struct sockaddr_in *)&sa)->sin_addr,
			tmp, sizeof tmp);
		break;
	case AF_INET6:
		name = inet_ntop(AF_INET6,
			&((struct sockaddr_in6 *)&sa)->sin6_addr,
			tmp, sizeof tmp);
		break;
	}
	if (name == NULL) {
		sprintf(tmp, "<unknown: %lu>", (unsigned long)sa.sa_family);
		name = tmp;
	}
	fprintf(stderr, "accepting connection from: %s\n", name);
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
 * Sample HTTP response to send.
 */
static const char *HTTP_RES =
	"HTTP/1.0 200 OK\r\n"
	"Content-Length: 46\r\n"
	"Connection: close\r\n"
	"Content-Type: text/html; charset=iso-8859-1\r\n"
	"\r\n"
	"<html>\r\n"
	"<body>\r\n"
	"<p>Test!</p>\r\n"
	"</body>\r\n"
	"</html>\r\n";

/*
 * Main program: this is a simple program that expects 1 argument: a
 * port number. This will start a simple network server on that port,
 * that expects incoming SSL clients. It handles only one client at a
 * time (handling several would require threads, sub-processes, or
 * multiplexing with select()/poll(), all of which being possible).
 *
 * For each client, the server will wait for two successive newline
 * characters (ignoring CR characters, so CR+LF is accepted), then
 * produce a sample static HTTP response. This is very crude, but
 * sufficient for explanatory purposes.
 */
int
main(int argc, char *argv[])
{
	const char *port;
	int fd;

	if (argc != 2) {
		return EXIT_FAILURE;
	}
	port = argv[1];

	/*
	 * Ignore SIGPIPE to avoid crashing in case of abrupt socket close.
	 */
	signal(SIGPIPE, SIG_IGN);

	/*
	 * Open the server socket.
	 */
	fd = host_bind(NULL, port);
	if (fd < 0) {
		return EXIT_FAILURE;
	}

	/*
	 * Process each client, one at a time.
	 */
	for (;;) {
		int cfd;
		br_ssl_server_context sc;
		unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
		br_sslio_context ioc;
		int lcwn, err;

		cfd = accept_client(fd);
		if (cfd < 0) {
			return EXIT_FAILURE;
		}

		/*
		 * Initialise the context with the cipher suites and
		 * algorithms. This depends on the server key type
		 * (and, for EC keys, the signature algorithm used by
		 * the CA to sign the server's certificate).
		 *
		 * Depending on the defined macros, we may select one of
		 * the "minimal" profiles. Key exchange algorithm depends
		 * on the key type:
		 *   RSA key: RSA or ECDHE_RSA
		 *   EC key, cert signed with ECDSA: ECDH_ECDSA or ECDHE_ECDSA
		 *   EC key, cert signed with RSA: ECDH_RSA or ECDHE_ECDSA
		 */
#if SERVER_RSA
#if SERVER_PROFILE_MIN_FS
#if SERVER_CHACHA20
		br_ssl_server_init_mine2c(&sc, CHAIN, CHAIN_LEN, &SKEY);
#else
		br_ssl_server_init_mine2g(&sc, CHAIN, CHAIN_LEN, &SKEY);
#endif
#elif SERVER_PROFILE_MIN_NOFS
		br_ssl_server_init_minr2g(&sc, CHAIN, CHAIN_LEN, &SKEY);
#else
		br_ssl_server_init_full_rsa(&sc, CHAIN, CHAIN_LEN, &SKEY);
#endif
#elif SERVER_EC
#if SERVER_PROFILE_MIN_FS
#if SERVER_CHACHA20
		br_ssl_server_init_minf2c(&sc, CHAIN, CHAIN_LEN, &SKEY);
#else
		br_ssl_server_init_minf2g(&sc, CHAIN, CHAIN_LEN, &SKEY);
#endif
#elif SERVER_PROFILE_MIN_NOFS
		br_ssl_server_init_minv2g(&sc, CHAIN, CHAIN_LEN, &SKEY);
#else
		br_ssl_server_init_full_ec(&sc, CHAIN, CHAIN_LEN,
			BR_KEYTYPE_EC, &SKEY);
#endif
#else /* SERVER_MIXED */
#if SERVER_PROFILE_MIN_FS
#if SERVER_CHACHA20
		br_ssl_server_init_minf2c(&sc, CHAIN, CHAIN_LEN, &SKEY);
#else
		br_ssl_server_init_minf2g(&sc, CHAIN, CHAIN_LEN, &SKEY);
#endif
#elif SERVER_PROFILE_MIN_NOFS
		br_ssl_server_init_minu2g(&sc, CHAIN, CHAIN_LEN, &SKEY);
#else
		br_ssl_server_init_full_ec(&sc, CHAIN, CHAIN_LEN,
			BR_KEYTYPE_RSA, &SKEY);
#endif
#endif
		/*
		 * Set the I/O buffer to the provided array. We
		 * allocated a buffer large enough for full-duplex
		 * behaviour with all allowed sizes of SSL records,
		 * hence we set the last argument to 1 (which means
		 * "split the buffer into separate input and output
		 * areas").
		 */
		br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof iobuf, 1);

		/*
		 * Reset the server context, for a new handshake.
		 */
		br_ssl_server_reset(&sc);

		/*
		 * Initialise the simplified I/O wrapper context.
		 */
		br_sslio_init(&ioc, &sc.eng, sock_read, &cfd, sock_write, &cfd);

		/*
		 * Read bytes until two successive LF (or CR+LF) are received.
		 */
		lcwn = 0;
		for (;;) {
			unsigned char x;

			if (br_sslio_read(&ioc, &x, 1) < 0) {
				goto client_drop;
			}
			if (x == 0x0D) {
				continue;
			}
			if (x == 0x0A) {
				if (lcwn) {
					break;
				}
				lcwn = 1;
			} else {
				lcwn = 0;
			}
		}

		/*
		 * Write a response and close the connection.
		 */
		br_sslio_write_all(&ioc, HTTP_RES, strlen(HTTP_RES));
		br_sslio_close(&ioc);

	client_drop:
		err = br_ssl_engine_last_error(&sc.eng);
		if (err == 0) {
			fprintf(stderr, "SSL closed (correctly).\n");
		} else {
			fprintf(stderr, "SSL error: %d\n", err);
		}
		close(cfd);
	}
}
