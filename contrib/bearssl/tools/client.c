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

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#define SOCKET             int
#define INVALID_SOCKET     (-1)
#endif

#include "brssl.h"

static int
host_connect(const char *host, const char *port, int verbose)
{
	struct addrinfo hints, *si, *p;
	SOCKET fd;
	int err;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	err = getaddrinfo(host, port, &hints, &si);
	if (err != 0) {
		fprintf(stderr, "ERROR: getaddrinfo(): %s\n",
			gai_strerror(err));
		return INVALID_SOCKET;
	}
	fd = INVALID_SOCKET;
	for (p = si; p != NULL; p = p->ai_next) {
		if (verbose) {
			struct sockaddr *sa;
			void *addr;
			char tmp[INET6_ADDRSTRLEN + 50];

			sa = (struct sockaddr *)p->ai_addr;
			if (sa->sa_family == AF_INET) {
				addr = &((struct sockaddr_in *)
					(void *)sa)->sin_addr;
			} else if (sa->sa_family == AF_INET6) {
				addr = &((struct sockaddr_in6 *)
					(void *)sa)->sin6_addr;
			} else {
				addr = NULL;
			}
			if (addr != NULL) {
				if (!inet_ntop(p->ai_family, addr,
					tmp, sizeof tmp))
				{
					strcpy(tmp, "<invalid>");
				}
			} else {
				sprintf(tmp, "<unknown family: %d>",
					(int)sa->sa_family);
			}
			fprintf(stderr, "connecting to: %s\n", tmp);
		}
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (fd == INVALID_SOCKET) {
			if (verbose) {
				perror("socket()");
			}
			continue;
		}
		if (connect(fd, p->ai_addr, p->ai_addrlen) == INVALID_SOCKET) {
			if (verbose) {
				perror("connect()");
			}
#ifdef _WIN32
			closesocket(fd);
#else
			close(fd);
#endif
			continue;
		}
		break;
	}
	if (p == NULL) {
		freeaddrinfo(si);
		fprintf(stderr, "ERROR: failed to connect\n");
		return INVALID_SOCKET;
	}
	freeaddrinfo(si);
	if (verbose) {
		fprintf(stderr, "connected.\n");
	}

	/*
	 * We make the socket non-blocking, since we are going to use
	 * poll() or select() to organise I/O.
	 */
#ifdef _WIN32
	{
		u_long arg;

		arg = 1;
		ioctlsocket(fd, FIONBIO, &arg);
	}
#else
	fcntl(fd, F_SETFL, O_NONBLOCK);
#endif
	return fd;
}

typedef struct {
	const br_ssl_client_certificate_class *vtable;
	int verbose;
	br_x509_certificate *chain;
	size_t chain_len;
	private_key *sk;
	int issuer_key_type;
} ccert_context;

static void
cc_start_name_list(const br_ssl_client_certificate_class **pctx)
{
	ccert_context *zc;

	zc = (ccert_context *)pctx;
	if (zc->verbose) {
		fprintf(stderr, "Server requests a client certificate.\n");
		fprintf(stderr, "--- anchor DN list start ---\n");
	}
}

static void
cc_start_name(const br_ssl_client_certificate_class **pctx, size_t len)
{
	ccert_context *zc;

	zc = (ccert_context *)pctx;
	if (zc->verbose) {
		fprintf(stderr, "new anchor name, length = %u\n",
			(unsigned)len);
	}
}

static void
cc_append_name(const br_ssl_client_certificate_class **pctx,
	const unsigned char *data, size_t len)
{
	ccert_context *zc;

	zc = (ccert_context *)pctx;
	if (zc->verbose) {
		size_t u;

		for (u = 0; u < len; u ++) {
			if (u == 0) {
				fprintf(stderr, "  ");
			} else if (u > 0 && u % 16 == 0) {
				fprintf(stderr, "\n  ");
			}
			fprintf(stderr, " %02x", data[u]);
		}
		if (len > 0) {
			fprintf(stderr, "\n");
		}
	}
}

static void
cc_end_name(const br_ssl_client_certificate_class **pctx)
{
	(void)pctx;
}

static void
cc_end_name_list(const br_ssl_client_certificate_class **pctx)
{
	ccert_context *zc;

	zc = (ccert_context *)pctx;
	if (zc->verbose) {
		fprintf(stderr, "--- anchor DN list end ---\n");
	}
}

static void
print_hashes(unsigned hh, unsigned hh2)
{
	int i;

	for (i = 0; i < 8; i ++) {
		const char *name;

		name = hash_function_name(i);
		if (((hh >> i) & 1) != 0) {
			fprintf(stderr, " %s", name);
		} else if (((hh2 >> i) & 1) != 0) {
			fprintf(stderr, " (%s)", name);
		}
	}
}

static int
choose_hash(unsigned hh)
{
	static const int f[] = {
		br_sha256_ID, br_sha224_ID, br_sha384_ID, br_sha512_ID,
		br_sha1_ID, br_md5sha1_ID, -1
	};

	size_t u;

	for (u = 0; f[u] >= 0; u ++) {
		if (((hh >> f[u]) & 1) != 0) {
			return f[u];
		}
	}
	return -1;
}

static void
cc_choose(const br_ssl_client_certificate_class **pctx,
	const br_ssl_client_context *cc, uint32_t auth_types,
	br_ssl_client_certificate *choices)
{
	ccert_context *zc;
	int scurve;

	zc = (ccert_context *)pctx;
	scurve = br_ssl_client_get_server_curve(cc);
	if (zc->verbose) {
		unsigned hashes;

		hashes = br_ssl_client_get_server_hashes(cc);
		if ((auth_types & 0x00FF) != 0) {
			fprintf(stderr, "supported: RSA signatures:");
			print_hashes(auth_types, hashes);
			fprintf(stderr, "\n");
		}
		if ((auth_types & 0xFF00) != 0) {
			fprintf(stderr, "supported: ECDSA signatures:");
			print_hashes(auth_types >> 8, hashes >> 8);
			fprintf(stderr, "\n");
		}
		if ((auth_types & 0x010000) != 0) {
			fprintf(stderr, "supported:"
				" fixed ECDH (cert signed with RSA)\n");
		}
		if ((auth_types & 0x020000) != 0) {
			fprintf(stderr, "supported:"
				" fixed ECDH (cert signed with ECDSA)\n");
		}
		if (scurve) {
			fprintf(stderr, "server key curve: %s (%d)\n",
				ec_curve_name(scurve), scurve);
		} else {
			fprintf(stderr, "server key is not EC\n");
		}
	}
	switch (zc->sk->key_type) {
	case BR_KEYTYPE_RSA:
		if ((choices->hash_id = choose_hash(auth_types)) >= 0) {
			if (zc->verbose) {
				fprintf(stderr, "using RSA, hash = %d (%s)\n",
					choices->hash_id,
					hash_function_name(choices->hash_id));
			}
			choices->auth_type = BR_AUTH_RSA;
			choices->chain = zc->chain;
			choices->chain_len = zc->chain_len;
			return;
		}
		break;
	case BR_KEYTYPE_EC:
		if (zc->issuer_key_type != 0
			&& scurve == zc->sk->key.ec.curve)
		{
			int x;

			x = (zc->issuer_key_type == BR_KEYTYPE_RSA) ? 16 : 17;
			if (((auth_types >> x) & 1) != 0) {
				if (zc->verbose) {
					fprintf(stderr, "using static ECDH\n");
				}
				choices->auth_type = BR_AUTH_ECDH;
				choices->hash_id = -1;
				choices->chain = zc->chain;
				choices->chain_len = zc->chain_len;
				return;
			}
		}
		if ((choices->hash_id = choose_hash(auth_types >> 8)) >= 0) {
			if (zc->verbose) {
				fprintf(stderr, "using ECDSA, hash = %d (%s)\n",
					choices->hash_id,
					hash_function_name(choices->hash_id));
			}
			choices->auth_type = BR_AUTH_ECDSA;
			choices->chain = zc->chain;
			choices->chain_len = zc->chain_len;
			return;
		}
		break;
	}
	if (zc->verbose) {
		fprintf(stderr, "no matching client certificate\n");
	}
	choices->chain = NULL;
	choices->chain_len = 0;
}

static uint32_t
cc_do_keyx(const br_ssl_client_certificate_class **pctx,
	unsigned char *data, size_t *len)
{
	const br_ec_impl *iec;
	ccert_context *zc;
	size_t xoff, xlen;
	uint32_t r;

	zc = (ccert_context *)pctx;
	iec = br_ec_get_default();
	r = iec->mul(data, *len, zc->sk->key.ec.x,
		zc->sk->key.ec.xlen, zc->sk->key.ec.curve);
	xoff = iec->xoff(zc->sk->key.ec.curve, &xlen);
	memmove(data, data + xoff, xlen);
	*len = xlen;
	return r;
}

static size_t
cc_do_sign(const br_ssl_client_certificate_class **pctx,
	int hash_id, size_t hv_len, unsigned char *data, size_t len)
{
	ccert_context *zc;
	unsigned char hv[64];

	zc = (ccert_context *)pctx;
	memcpy(hv, data, hv_len);
	switch (zc->sk->key_type) {
		const br_hash_class *hc;
		const unsigned char *hash_oid;
		uint32_t x;
		size_t sig_len;

	case BR_KEYTYPE_RSA:
		hash_oid = get_hash_oid(hash_id);
		if (hash_oid == NULL && hash_id != 0) {
			if (zc->verbose) {
				fprintf(stderr, "ERROR: cannot RSA-sign with"
					" unknown hash function: %d\n",
					hash_id);
			}
			return 0;
		}
		sig_len = (zc->sk->key.rsa.n_bitlen + 7) >> 3;
		if (len < sig_len) {
			if (zc->verbose) {
				fprintf(stderr, "ERROR: cannot RSA-sign,"
					" buffer is too small"
					" (sig=%lu, buf=%lu)\n",
					(unsigned long)sig_len,
					(unsigned long)len);
			}
			return 0;
		}
		x = br_rsa_pkcs1_sign_get_default()(
			hash_oid, hv, hv_len, &zc->sk->key.rsa, data);
		if (!x) {
			if (zc->verbose) {
				fprintf(stderr, "ERROR: RSA-sign failure\n");
			}
			return 0;
		}
		return sig_len;

	case BR_KEYTYPE_EC:
		hc = get_hash_impl(hash_id);
		if (hc == NULL) {
			if (zc->verbose) {
				fprintf(stderr, "ERROR: cannot ECDSA-sign with"
					" unknown hash function: %d\n",
					hash_id);
			}
			return 0;
		}
		if (len < 139) {
			if (zc->verbose) {
				fprintf(stderr, "ERROR: cannot ECDSA-sign"
					" (output buffer = %lu)\n",
					(unsigned long)len);
			}
			return 0;
		}
		sig_len = br_ecdsa_sign_asn1_get_default()(
			br_ec_get_default(), hc, hv, &zc->sk->key.ec, data);
		if (sig_len == 0) {
			if (zc->verbose) {
				fprintf(stderr, "ERROR: ECDSA-sign failure\n");
			}
			return 0;
		}
		return sig_len;

	default:
		return 0;
	}
}

static const br_ssl_client_certificate_class ccert_vtable = {
	sizeof(ccert_context),
	cc_start_name_list,
	cc_start_name,
	cc_append_name,
	cc_end_name,
	cc_end_name_list,
	cc_choose,
	cc_do_keyx,
	cc_do_sign
};

static void
free_alpn(void *alpn)
{
	xfree(*(char **)alpn);
}

static void
usage_client(void)
{
	fprintf(stderr,
"usage: brssl client server[:port] [ options ]\n");
	fprintf(stderr,
"options:\n");
	fprintf(stderr,
"   -q              suppress verbose messages\n");
	fprintf(stderr,
"   -trace          activate extra debug messages (dump of all packets)\n");
	fprintf(stderr,
"   -sni name       use this specific name for SNI\n");
	fprintf(stderr,
"   -nosni          do not send any SNI\n");
	fprintf(stderr,
"   -mono           use monodirectional buffering\n");
	fprintf(stderr,
"   -buf length     set the I/O buffer length (in bytes)\n");
	fprintf(stderr,
"   -CA file        add certificates in 'file' to trust anchors\n");
	fprintf(stderr,
"   -cert file      set client certificate chain\n");
	fprintf(stderr,
"   -key file       set client private key (for certificate authentication)\n");
	fprintf(stderr,
"   -nostaticecdh   prohibit full-static ECDH (client certificate)\n");
	fprintf(stderr,
"   -list           list supported names (protocols, algorithms...)\n");
	fprintf(stderr,
"   -vmin name      set minimum supported version (default: TLS-1.0)\n");
	fprintf(stderr,
"   -vmax name      set maximum supported version (default: TLS-1.2)\n");
	fprintf(stderr,
"   -cs names       set list of supported cipher suites (comma-separated)\n");
	fprintf(stderr,
"   -hf names       add support for some hash functions (comma-separated)\n");
	fprintf(stderr,
"   -minhello len   set minimum ClientHello length (in bytes)\n");
	fprintf(stderr,
"   -fallback       send the TLS_FALLBACK_SCSV (i.e. claim a downgrade)\n");
	fprintf(stderr,
"   -noreneg        prohibit renegotiations\n");
	fprintf(stderr,
"   -alpn name      add protocol name to list of protocols (ALPN extension)\n");
	fprintf(stderr,
"   -strictalpn     fail on ALPN mismatch\n");
}

/* see brssl.h */
int
do_client(int argc, char *argv[])
{
	int retcode;
	int verbose;
	int trace;
	int i, bidi;
	const char *server_name;
	char *host;
	char *port;
	const char *sni;
	anchor_list anchors = VEC_INIT;
	unsigned vmin, vmax;
	VECTOR(char *) alpn_names = VEC_INIT;
	cipher_suite *suites;
	size_t num_suites;
	uint16_t *suite_ids;
	unsigned hfuns;
	size_t u;
	br_ssl_client_context cc;
	br_x509_minimal_context xc;
	x509_noanchor_context xwc;
	const br_hash_class *dnhash;
	ccert_context zc;
	br_x509_certificate *chain;
	size_t chain_len;
	private_key *sk;
	int nostaticecdh;
	unsigned char *iobuf;
	size_t iobuf_len;
	size_t minhello_len;
	int fallback;
	uint32_t flags;
	SOCKET fd;

	retcode = 0;
	verbose = 1;
	trace = 0;
	server_name = NULL;
	host = NULL;
	port = NULL;
	sni = NULL;
	bidi = 1;
	vmin = 0;
	vmax = 0;
	suites = NULL;
	num_suites = 0;
	hfuns = 0;
	suite_ids = NULL;
	chain = NULL;
	chain_len = 0;
	sk = NULL;
	nostaticecdh = 0;
	iobuf = NULL;
	iobuf_len = 0;
	minhello_len = (size_t)-1;
	fallback = 0;
	flags = 0;
	fd = INVALID_SOCKET;
	for (i = 0; i < argc; i ++) {
		const char *arg;

		arg = argv[i];
		if (arg[0] != '-') {
			if (server_name != NULL) {
				fprintf(stderr,
					"ERROR: duplicate server name\n");
				usage_client();
				goto client_exit_error;
			}
			server_name = arg;
			continue;
		}
		if (eqstr(arg, "-v") || eqstr(arg, "-verbose")) {
			verbose = 1;
		} else if (eqstr(arg, "-q") || eqstr(arg, "-quiet")) {
			verbose = 0;
		} else if (eqstr(arg, "-trace")) {
			trace = 1;
		} else if (eqstr(arg, "-sni")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-sni'\n");
				usage_client();
				goto client_exit_error;
			}
			if (sni != NULL) {
				fprintf(stderr, "ERROR: duplicate SNI\n");
				usage_client();
				goto client_exit_error;
			}
			sni = argv[i];
		} else if (eqstr(arg, "-nosni")) {
			if (sni != NULL) {
				fprintf(stderr, "ERROR: duplicate SNI\n");
				usage_client();
				goto client_exit_error;
			}
			sni = "";
		} else if (eqstr(arg, "-mono")) {
			bidi = 0;
		} else if (eqstr(arg, "-buf")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-buf'\n");
				usage_client();
				goto client_exit_error;
			}
			arg = argv[i];
			if (iobuf_len != 0) {
				fprintf(stderr,
					"ERROR: duplicate I/O buffer length\n");
				usage_client();
				goto client_exit_error;
			}
			iobuf_len = parse_size(arg);
			if (iobuf_len == (size_t)-1) {
				usage_client();
				goto client_exit_error;
			}
		} else if (eqstr(arg, "-CA")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-CA'\n");
				usage_client();
				goto client_exit_error;
			}
			arg = argv[i];
			if (read_trust_anchors(&anchors, arg) == 0) {
				usage_client();
				goto client_exit_error;
			}
		} else if (eqstr(arg, "-cert")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-cert'\n");
				usage_client();
				goto client_exit_error;
			}
			if (chain != NULL) {
				fprintf(stderr,
					"ERROR: duplicate certificate chain\n");
				usage_client();
				goto client_exit_error;
			}
			arg = argv[i];
			chain = read_certificates(arg, &chain_len);
			if (chain == NULL || chain_len == 0) {
				goto client_exit_error;
			}
		} else if (eqstr(arg, "-key")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-key'\n");
				usage_client();
				goto client_exit_error;
			}
			if (sk != NULL) {
				fprintf(stderr,
					"ERROR: duplicate private key\n");
				usage_client();
				goto client_exit_error;
			}
			arg = argv[i];
			sk = read_private_key(arg);
			if (sk == NULL) {
				goto client_exit_error;
			}
		} else if (eqstr(arg, "-nostaticecdh")) {
			nostaticecdh = 1;
		} else if (eqstr(arg, "-list")) {
			list_names();
			goto client_exit;
		} else if (eqstr(arg, "-vmin")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-vmin'\n");
				usage_client();
				goto client_exit_error;
			}
			arg = argv[i];
			if (vmin != 0) {
				fprintf(stderr,
					"ERROR: duplicate minimum version\n");
				usage_client();
				goto client_exit_error;
			}
			vmin = parse_version(arg, strlen(arg));
			if (vmin == 0) {
				fprintf(stderr,
					"ERROR: unrecognised version '%s'\n",
					arg);
				usage_client();
				goto client_exit_error;
			}
		} else if (eqstr(arg, "-vmax")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-vmax'\n");
				usage_client();
				goto client_exit_error;
			}
			arg = argv[i];
			if (vmax != 0) {
				fprintf(stderr,
					"ERROR: duplicate maximum version\n");
				usage_client();
				goto client_exit_error;
			}
			vmax = parse_version(arg, strlen(arg));
			if (vmax == 0) {
				fprintf(stderr,
					"ERROR: unrecognised version '%s'\n",
					arg);
				usage_client();
				goto client_exit_error;
			}
		} else if (eqstr(arg, "-cs")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-cs'\n");
				usage_client();
				goto client_exit_error;
			}
			arg = argv[i];
			if (suites != NULL) {
				fprintf(stderr, "ERROR: duplicate list"
					" of cipher suites\n");
				usage_client();
				goto client_exit_error;
			}
			suites = parse_suites(arg, &num_suites);
			if (suites == NULL) {
				usage_client();
				goto client_exit_error;
			}
		} else if (eqstr(arg, "-hf")) {
			unsigned x;

			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-hf'\n");
				usage_client();
				goto client_exit_error;
			}
			arg = argv[i];
			x = parse_hash_functions(arg);
			if (x == 0) {
				usage_client();
				goto client_exit_error;
			}
			hfuns |= x;
		} else if (eqstr(arg, "-minhello")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-minhello'\n");
				usage_client();
				goto client_exit_error;
			}
			arg = argv[i];
			if (minhello_len != (size_t)-1) {
				fprintf(stderr, "ERROR: duplicate minimum"
					" ClientHello length\n");
				usage_client();
				goto client_exit_error;
			}
			minhello_len = parse_size(arg);
			/*
			 * Minimum ClientHello length must fit on 16 bits.
			 */
			if (minhello_len == (size_t)-1
				|| (((minhello_len >> 12) >> 4) != 0))
			{
				usage_client();
				goto client_exit_error;
			}
		} else if (eqstr(arg, "-fallback")) {
			fallback = 1;
		} else if (eqstr(arg, "-noreneg")) {
			flags |= BR_OPT_NO_RENEGOTIATION;
		} else if (eqstr(arg, "-alpn")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-alpn'\n");
				usage_client();
				goto client_exit_error;
			}
			VEC_ADD(alpn_names, xstrdup(argv[i]));
		} else if (eqstr(arg, "-strictalpn")) {
			flags |= BR_OPT_FAIL_ON_ALPN_MISMATCH;
		} else {
			fprintf(stderr, "ERROR: unknown option: '%s'\n", arg);
			usage_client();
			goto client_exit_error;
		}
	}
	if (server_name == NULL) {
		fprintf(stderr, "ERROR: no server name/address provided\n");
		usage_client();
		goto client_exit_error;
	}
	for (u = strlen(server_name); u > 0; u --) {
		int c = server_name[u - 1];
		if (c == ':') {
			break;
		}
		if (c < '0' || c > '9') {
			u = 0;
			break;
		}
	}
	if (u == 0) {
		host = xstrdup(server_name);
		port = xstrdup("443");
	} else {
		port = xstrdup(server_name + u);
		host = xmalloc(u);
		memcpy(host, server_name, u - 1);
		host[u - 1] = 0;
	}
	if (sni == NULL) {
		sni = host;
	}

	if (chain == NULL && sk != NULL) {
		fprintf(stderr, "ERROR: private key specified, but"
			" no certificate chain\n");
		usage_client();
		goto client_exit_error;
	}
	if (chain != NULL && sk == NULL) {
		fprintf(stderr, "ERROR: certificate chain specified, but"
			" no private key\n");
		usage_client();
		goto client_exit_error;
	}

	if (vmin == 0) {
		vmin = BR_TLS10;
	}
	if (vmax == 0) {
		vmax = BR_TLS12;
	}
	if (vmax < vmin) {
		fprintf(stderr, "ERROR: impossible minimum/maximum protocol"
			" version combination\n");
		usage_client();
		goto client_exit_error;
	}
	if (suites == NULL) {
		num_suites = 0;

		for (u = 0; cipher_suites[u].name; u ++) {
			if ((cipher_suites[u].req & REQ_TLS12) == 0
				|| vmax >= BR_TLS12)
			{
				num_suites ++;
			}
		}
		suites = xmalloc(num_suites * sizeof *suites);
		num_suites = 0;
		for (u = 0; cipher_suites[u].name; u ++) {
			if ((cipher_suites[u].req & REQ_TLS12) == 0
				|| vmax >= BR_TLS12)
			{
				suites[num_suites ++] = cipher_suites[u];
			}
		}
	}
	if (hfuns == 0) {
		hfuns = (unsigned)-1;
	}
	if (iobuf_len == 0) {
		if (bidi) {
			iobuf_len = BR_SSL_BUFSIZE_BIDI;
		} else {
			iobuf_len = BR_SSL_BUFSIZE_MONO;
		}
	}
	iobuf = xmalloc(iobuf_len);

	/*
	 * Compute implementation requirements and inject implementations.
	 */
	suite_ids = xmalloc((num_suites + 1) * sizeof *suite_ids);
	br_ssl_client_zero(&cc);
	br_ssl_engine_set_versions(&cc.eng, vmin, vmax);
	dnhash = NULL;
	for (u = 0; hash_functions[u].name; u ++) {
		const br_hash_class *hc;
		int id;

		hc = hash_functions[u].hclass;
		id = (hc->desc >> BR_HASHDESC_ID_OFF) & BR_HASHDESC_ID_MASK;
		if ((hfuns & ((unsigned)1 << id)) != 0) {
			dnhash = hc;
		}
	}
	if (dnhash == NULL) {
		fprintf(stderr, "ERROR: no supported hash function\n");
		goto client_exit_error;
	}
	br_x509_minimal_init(&xc, dnhash,
		&VEC_ELT(anchors, 0), VEC_LEN(anchors));
	if (vmin <= BR_TLS11) {
		if (!(hfuns & (1 << br_md5_ID))) {
			fprintf(stderr, "ERROR: TLS 1.0 and 1.1 need MD5\n");
			goto client_exit_error;
		}
		if (!(hfuns & (1 << br_sha1_ID))) {
			fprintf(stderr, "ERROR: TLS 1.0 and 1.1 need SHA-1\n");
			goto client_exit_error;
		}
	}
	for (u = 0; u < num_suites; u ++) {
		unsigned req;

		req = suites[u].req;
		suite_ids[u] = suites[u].suite;
		if ((req & REQ_TLS12) != 0 && vmax < BR_TLS12) {
			fprintf(stderr,
				"ERROR: cipher suite %s requires TLS 1.2\n",
				suites[u].name);
			goto client_exit_error;
		}
		if ((req & REQ_SHA1) != 0 && !(hfuns & (1 << br_sha1_ID))) {
			fprintf(stderr,
				"ERROR: cipher suite %s requires SHA-1\n",
				suites[u].name);
			goto client_exit_error;
		}
		if ((req & REQ_SHA256) != 0 && !(hfuns & (1 << br_sha256_ID))) {
			fprintf(stderr,
				"ERROR: cipher suite %s requires SHA-256\n",
				suites[u].name);
			goto client_exit_error;
		}
		if ((req & REQ_SHA384) != 0 && !(hfuns & (1 << br_sha384_ID))) {
			fprintf(stderr,
				"ERROR: cipher suite %s requires SHA-384\n",
				suites[u].name);
			goto client_exit_error;
		}
		/* TODO: algorithm implementation selection */
		if ((req & REQ_AESCBC) != 0) {
			br_ssl_engine_set_default_aes_cbc(&cc.eng);
		}
		if ((req & REQ_AESCCM) != 0) {
			br_ssl_engine_set_default_aes_ccm(&cc.eng);
		}
		if ((req & REQ_AESGCM) != 0) {
			br_ssl_engine_set_default_aes_gcm(&cc.eng);
		}
		if ((req & REQ_CHAPOL) != 0) {
			br_ssl_engine_set_default_chapol(&cc.eng);
		}
		if ((req & REQ_3DESCBC) != 0) {
			br_ssl_engine_set_default_des_cbc(&cc.eng);
		}
		if ((req & REQ_RSAKEYX) != 0) {
			br_ssl_client_set_default_rsapub(&cc);
		}
		if ((req & REQ_ECDHE_RSA) != 0) {
			br_ssl_engine_set_default_ec(&cc.eng);
			br_ssl_engine_set_default_rsavrfy(&cc.eng);
		}
		if ((req & REQ_ECDHE_ECDSA) != 0) {
			br_ssl_engine_set_default_ecdsa(&cc.eng);
		}
		if ((req & REQ_ECDH) != 0) {
			br_ssl_engine_set_default_ec(&cc.eng);
		}
	}
	if (fallback) {
		suite_ids[num_suites ++] = 0x5600;
	}
	br_ssl_engine_set_suites(&cc.eng, suite_ids, num_suites);

	for (u = 0; hash_functions[u].name; u ++) {
		const br_hash_class *hc;
		int id;

		hc = hash_functions[u].hclass;
		id = (hc->desc >> BR_HASHDESC_ID_OFF) & BR_HASHDESC_ID_MASK;
		if ((hfuns & ((unsigned)1 << id)) != 0) {
			br_ssl_engine_set_hash(&cc.eng, id, hc);
			br_x509_minimal_set_hash(&xc, id, hc);
		}
	}
	if (vmin <= BR_TLS11) {
		br_ssl_engine_set_prf10(&cc.eng, &br_tls10_prf);
	}
	if (vmax >= BR_TLS12) {
		if ((hfuns & ((unsigned)1 << br_sha256_ID)) != 0) {
			br_ssl_engine_set_prf_sha256(&cc.eng,
				&br_tls12_sha256_prf);
		}
		if ((hfuns & ((unsigned)1 << br_sha384_ID)) != 0) {
			br_ssl_engine_set_prf_sha384(&cc.eng,
				&br_tls12_sha384_prf);
		}
	}
	br_x509_minimal_set_rsa(&xc, br_rsa_pkcs1_vrfy_get_default());
	br_x509_minimal_set_ecdsa(&xc,
		br_ec_get_default(), br_ecdsa_vrfy_asn1_get_default());

	/*
	 * If there is no provided trust anchor, then certificate validation
	 * will always fail. In that situation, we use our custom wrapper
	 * that tolerates unknown anchors.
	 */
	if (VEC_LEN(anchors) == 0) {
		if (verbose) {
			fprintf(stderr,
				"WARNING: no configured trust anchor\n");
		}
		x509_noanchor_init(&xwc, &xc.vtable);
		br_ssl_engine_set_x509(&cc.eng, &xwc.vtable);
	} else {
		br_ssl_engine_set_x509(&cc.eng, &xc.vtable);
	}

	if (minhello_len != (size_t)-1) {
		br_ssl_client_set_min_clienthello_len(&cc, minhello_len);
	}
	br_ssl_engine_set_all_flags(&cc.eng, flags);
	if (VEC_LEN(alpn_names) != 0) {
		br_ssl_engine_set_protocol_names(&cc.eng,
			(const char **)&VEC_ELT(alpn_names, 0),
			VEC_LEN(alpn_names));
	}

	if (chain != NULL) {
		zc.vtable = &ccert_vtable;
		zc.verbose = verbose;
		zc.chain = chain;
		zc.chain_len = chain_len;
		zc.sk = sk;
		if (nostaticecdh || sk->key_type != BR_KEYTYPE_EC) {
			zc.issuer_key_type = 0;
		} else {
			zc.issuer_key_type = get_cert_signer_algo(&chain[0]);
			if (zc.issuer_key_type == 0) {
				goto client_exit_error;
			}
		}
		br_ssl_client_set_client_certificate(&cc, &zc.vtable);
	}

	br_ssl_engine_set_buffer(&cc.eng, iobuf, iobuf_len, bidi);
	br_ssl_client_reset(&cc, sni, 0);

	/*
	 * On Unix systems, we need to avoid SIGPIPE.
	 */
#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
#endif

	/*
	 * Connect to the peer.
	 */
	fd = host_connect(host, port, verbose);
	if (fd == INVALID_SOCKET) {
		goto client_exit_error;
	}

	/*
	 * Run the engine until completion.
	 */
	if (run_ssl_engine(&cc.eng, fd,
		(verbose ? RUN_ENGINE_VERBOSE : 0)
		| (trace ? RUN_ENGINE_TRACE : 0)) != 0)
	{
		goto client_exit_error;
	} else {
		goto client_exit;
	}

	/*
	 * Release allocated structures.
	 */
client_exit:
	xfree(host);
	xfree(port);
	xfree(suites);
	xfree(suite_ids);
	VEC_CLEAREXT(anchors, &free_ta_contents);
	VEC_CLEAREXT(alpn_names, &free_alpn);
	free_certificates(chain, chain_len);
	free_private_key(sk);
	xfree(iobuf);
	if (fd != INVALID_SOCKET) {
#ifdef _WIN32
		closesocket(fd);
#else
		close(fd);
#endif
	}
	return retcode;

client_exit_error:
	retcode = -1;
	goto client_exit;
}
