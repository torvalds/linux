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
#define SOCKADDR_STORAGE   struct sockaddr_storage
#endif

#include "brssl.h"

static SOCKET
host_bind(const char *host, const char *port, int verbose)
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
		struct sockaddr *sa;
		struct sockaddr_in sa4;
		struct sockaddr_in6 sa6;
		size_t sa_len;
		void *addr;
		int opt;

		sa = (struct sockaddr *)p->ai_addr;
		if (sa->sa_family == AF_INET) {
			memcpy(&sa4, sa, sizeof sa4);
			sa = (struct sockaddr *)&sa4;
			sa_len = sizeof sa4;
			addr = &sa4.sin_addr;
			if (host == NULL) {
				sa4.sin_addr.s_addr = INADDR_ANY;
			}
		} else if (sa->sa_family == AF_INET6) {
			memcpy(&sa6, sa, sizeof sa6);
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
		if (verbose) {
			char tmp[INET6_ADDRSTRLEN + 50];

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
			fprintf(stderr, "binding to: %s\n", tmp);
		}
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (fd == INVALID_SOCKET) {
			if (verbose) {
				perror("socket()");
			}
			continue;
		}
		opt = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
			(void *)&opt, sizeof opt);
#ifdef IPV6_V6ONLY
		/*
		 * We want to make sure that the server socket works for
		 * both IPv4 and IPv6. But IPV6_V6ONLY is not defined on
		 * some very old systems.
		 */
		opt = 0;
		setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
			(void *)&opt, sizeof opt);
#endif
		if (bind(fd, sa, sa_len) < 0) {
			if (verbose) {
				perror("bind()");
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
		fprintf(stderr, "ERROR: failed to bind\n");
		return INVALID_SOCKET;
	}
	freeaddrinfo(si);
	if (listen(fd, 5) < 0) {
		if (verbose) {
			perror("listen()");
		}
#ifdef _WIN32
		closesocket(fd);
#else
		close(fd);
#endif
		return INVALID_SOCKET;
	}
	if (verbose) {
		fprintf(stderr, "bound.\n");
	}
	return fd;
}

static SOCKET
accept_client(SOCKET server_fd, int verbose, int nonblock)
{
	int fd;
	SOCKADDR_STORAGE sa;
	socklen_t sa_len;

	sa_len = sizeof sa;
	fd = accept(server_fd, (struct sockaddr *)&sa, &sa_len);
	if (fd == INVALID_SOCKET) {
		if (verbose) {
			perror("accept()");
		}
		return INVALID_SOCKET;
	}
	if (verbose) {
		char tmp[INET6_ADDRSTRLEN + 50];
		const char *name;

		name = NULL;
		switch (((struct sockaddr *)&sa)->sa_family) {
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
			sprintf(tmp, "<unknown: %lu>", (unsigned long)
				((struct sockaddr *)&sa)->sa_family);
			name = tmp;
		}
		fprintf(stderr, "accepting connection from: %s\n", name);
	}

	/*
	 * We make the socket non-blocking, since we are going to use
	 * poll() or select() to organise I/O.
	 */
	if (nonblock) {
#ifdef _WIN32
		u_long arg;

		arg = 1;
		ioctlsocket(fd, FIONBIO, &arg);
#else
		fcntl(fd, F_SETFL, O_NONBLOCK);
#endif
	}
	return fd;
}

static void
usage_server(void)
{
	fprintf(stderr,
"usage: brssl server [ options ]\n");
	fprintf(stderr,
"options:\n");
	fprintf(stderr,
"   -q              suppress verbose messages\n");
	fprintf(stderr,
"   -trace          activate extra debug messages (dump of all packets)\n");
	fprintf(stderr,
"   -b name         bind to a specific address or host name\n");
	fprintf(stderr,
"   -p port         bind to a specific port (default: 4433)\n");
	fprintf(stderr,
"   -mono           use monodirectional buffering\n");
	fprintf(stderr,
"   -buf length     set the I/O buffer length (in bytes)\n");
	fprintf(stderr,
"   -cache length   set the session cache storage length (in bytes)\n");
	fprintf(stderr,
"   -cert fname     read certificate chain from file 'fname'\n");
	fprintf(stderr,
"   -key fname      read private key from file 'fname'\n");
	fprintf(stderr,
"   -CA file        add trust anchors from 'file' (for client auth)\n");
	fprintf(stderr,
"   -anon_ok        request but do not require a client certificate\n");
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
"   -cbhash         test hashing in policy callback\n");
	fprintf(stderr,
"   -serverpref     enforce server's preferences for cipher suites\n");
	fprintf(stderr,
"   -noreneg        prohibit renegotiations\n");
	fprintf(stderr,
"   -alpn name      add protocol name to list of protocols (ALPN extension)\n");
	fprintf(stderr,
"   -strictalpn     fail on ALPN mismatch\n");
	exit(EXIT_FAILURE);
}

typedef struct {
	const br_ssl_server_policy_class *vtable;
	int verbose;
	br_x509_certificate *chain;
	size_t chain_len;
	int cert_signer_algo;
	private_key *sk;
	int cbhash;
} policy_context;

static void
print_hashes(unsigned chashes)
{
	int i;

	for (i = 2; i <= 6; i ++) {
		if ((chashes >> i) & 1) {
			int z;

			switch (i) {
			case 3: z = 224; break;
			case 4: z = 256; break;
			case 5: z = 384; break;
			case 6: z = 512; break;
			default:
				z = 1;
				break;
			}
			fprintf(stderr, " sha%d", z);
		}
	}
}

static unsigned
choose_hash(unsigned chashes)
{
	unsigned hash_id;

	for (hash_id = 6; hash_id >= 2; hash_id --) {
		if (((chashes >> hash_id) & 1) != 0) {
			return hash_id;
		}
	}
	/*
	 * Normally unreachable.
	 */
	return 0;
}

static int
sp_choose(const br_ssl_server_policy_class **pctx,
	const br_ssl_server_context *cc,
	br_ssl_server_choices *choices)
{
	policy_context *pc;
	const br_suite_translated *st;
	size_t u, st_num;
	unsigned chashes;

	pc = (policy_context *)pctx;
	st = br_ssl_server_get_client_suites(cc, &st_num);
	chashes = br_ssl_server_get_client_hashes(cc);
	if (pc->verbose) {
		fprintf(stderr, "Client parameters:\n");
		fprintf(stderr, "   Maximum version:      ");
		switch (cc->client_max_version) {
		case BR_SSL30:
			fprintf(stderr, "SSL 3.0");
			break;
		case BR_TLS10:
			fprintf(stderr, "TLS 1.0");
			break;
		case BR_TLS11:
			fprintf(stderr, "TLS 1.1");
			break;
		case BR_TLS12:
			fprintf(stderr, "TLS 1.2");
			break;
		default:
			fprintf(stderr, "unknown (0x%04X)",
				(unsigned)cc->client_max_version);
			break;
		}
		fprintf(stderr, "\n");
		fprintf(stderr, "   Compatible cipher suites:\n");
		for (u = 0; u < st_num; u ++) {
			char csn[80];

			get_suite_name_ext(st[u][0], csn, sizeof csn);
			fprintf(stderr, "      %s\n", csn);
		}
		fprintf(stderr, "   Common sign+hash functions:\n");
		if ((chashes & 0xFF) != 0) {
			fprintf(stderr, "      with RSA:");
			print_hashes(chashes);
			fprintf(stderr, "\n");
		}
		if ((chashes >> 8) != 0) {
			fprintf(stderr, "      with ECDSA:");
			print_hashes(chashes >> 8);
			fprintf(stderr, "\n");
		}
	}
	for (u = 0; u < st_num; u ++) {
		unsigned tt;

		tt = st[u][1];
		switch (tt >> 12) {
		case BR_SSLKEYX_RSA:
			if (pc->sk->key_type == BR_KEYTYPE_RSA) {
				choices->cipher_suite = st[u][0];
				goto choose_ok;
			}
			break;
		case BR_SSLKEYX_ECDHE_RSA:
			if (pc->sk->key_type == BR_KEYTYPE_RSA) {
				choices->cipher_suite = st[u][0];
				if (br_ssl_engine_get_version(&cc->eng)
					< BR_TLS12)
				{
					if (pc->cbhash) {
						choices->algo_id = 0x0001;
					} else {
						choices->algo_id = 0xFF00;
					}
				} else {
					unsigned id;

					id = choose_hash(chashes);
					if (pc->cbhash) {
						choices->algo_id =
							(id << 8) + 0x01;
					} else {
						choices->algo_id = 0xFF00 + id;
					}
				}
				goto choose_ok;
			}
			break;
		case BR_SSLKEYX_ECDHE_ECDSA:
			if (pc->sk->key_type == BR_KEYTYPE_EC) {
				choices->cipher_suite = st[u][0];
				if (br_ssl_engine_get_version(&cc->eng)
					< BR_TLS12)
				{
					if (pc->cbhash) {
						choices->algo_id = 0x0203;
					} else {
						choices->algo_id =
							0xFF00 + br_sha1_ID;
					}
				} else {
					unsigned id;

					id = choose_hash(chashes >> 8);
					if (pc->cbhash) {
						choices->algo_id =
							(id << 8) + 0x03;
					} else {
						choices->algo_id =
							0xFF00 + id;
					}
				}
				goto choose_ok;
			}
			break;
		case BR_SSLKEYX_ECDH_RSA:
			if (pc->sk->key_type == BR_KEYTYPE_EC
				&& pc->cert_signer_algo == BR_KEYTYPE_RSA)
			{
				choices->cipher_suite = st[u][0];
				goto choose_ok;
			}
			break;
		case BR_SSLKEYX_ECDH_ECDSA:
			if (pc->sk->key_type == BR_KEYTYPE_EC
				&& pc->cert_signer_algo == BR_KEYTYPE_EC)
			{
				choices->cipher_suite = st[u][0];
				goto choose_ok;
			}
			break;
		}
	}
	return 0;

choose_ok:
	choices->chain = pc->chain;
	choices->chain_len = pc->chain_len;
	if (pc->verbose) {
		char csn[80];

		get_suite_name_ext(choices->cipher_suite, csn, sizeof csn);
		fprintf(stderr, "Using: %s\n", csn);
	}
	return 1;
}

static uint32_t
sp_do_keyx(const br_ssl_server_policy_class **pctx,
	unsigned char *data, size_t *len)
{
	policy_context *pc;
	uint32_t r;
	size_t xoff, xlen;

	pc = (policy_context *)pctx;
	switch (pc->sk->key_type) {
		const br_ec_impl *iec;

	case BR_KEYTYPE_RSA:
		return br_rsa_ssl_decrypt(
			br_rsa_private_get_default(),
			&pc->sk->key.rsa, data, *len);
	case BR_KEYTYPE_EC:
		iec = br_ec_get_default();
		r = iec->mul(data, *len, pc->sk->key.ec.x,
			pc->sk->key.ec.xlen, pc->sk->key.ec.curve);
		xoff = iec->xoff(pc->sk->key.ec.curve, &xlen);
		memmove(data, data + xoff, xlen);
		*len = xlen;
		return r;
	default:
		fprintf(stderr, "ERROR: unknown private key type (%d)\n",
			(int)pc->sk->key_type);
		return 0;
	}
}

static size_t
sp_do_sign(const br_ssl_server_policy_class **pctx,
	unsigned algo_id, unsigned char *data, size_t hv_len, size_t len)
{
	policy_context *pc;
	unsigned char hv[64];

	pc = (policy_context *)pctx;
	if (algo_id >= 0xFF00) {
		algo_id &= 0xFF;
		memcpy(hv, data, hv_len);
	} else {
		const br_hash_class *hc;
		br_hash_compat_context zc;

		if (pc->verbose) {
			fprintf(stderr, "Callback hashing, algo = 0x%04X,"
				" data_len = %lu\n",
				algo_id, (unsigned long)hv_len);
		}
		algo_id >>= 8;
		hc = get_hash_impl(algo_id);
		if (hc == NULL) {
			if (pc->verbose) {
				fprintf(stderr,
					"ERROR: unsupported hash function %u\n",
					algo_id);
			}
			return 0;
		}
		hc->init(&zc.vtable);
		hc->update(&zc.vtable, data, hv_len);
		hc->out(&zc.vtable, hv);
		hv_len = (hc->desc >> BR_HASHDESC_OUT_OFF)
			& BR_HASHDESC_OUT_MASK;
	}
	switch (pc->sk->key_type) {
		size_t sig_len;
		uint32_t x;
		const unsigned char *hash_oid;
		const br_hash_class *hc;

	case BR_KEYTYPE_RSA:
		hash_oid = get_hash_oid(algo_id);
		if (hash_oid == NULL && algo_id != 0) {
			if (pc->verbose) {
				fprintf(stderr, "ERROR: cannot RSA-sign with"
					" unknown hash function: %u\n",
					algo_id);
			}
			return 0;
		}
		sig_len = (pc->sk->key.rsa.n_bitlen + 7) >> 3;
		if (len < sig_len) {
			if (pc->verbose) {
				fprintf(stderr, "ERROR: cannot RSA-sign,"
					" buffer is too small"
					" (sig=%lu, buf=%lu)\n",
					(unsigned long)sig_len,
					(unsigned long)len);
			}
			return 0;
		}
		x = br_rsa_pkcs1_sign_get_default()(
			hash_oid, hv, hv_len, &pc->sk->key.rsa, data);
		if (!x) {
			if (pc->verbose) {
				fprintf(stderr, "ERROR: RSA-sign failure\n");
			}
			return 0;
		}
		return sig_len;

	case BR_KEYTYPE_EC:
		hc = get_hash_impl(algo_id);
		if (hc == NULL) {
			if (pc->verbose) {
				fprintf(stderr, "ERROR: cannot ECDSA-sign with"
					" unknown hash function: %u\n",
					algo_id);
			}
			return 0;
		}
		if (len < 139) {
			if (pc->verbose) {
				fprintf(stderr, "ERROR: cannot ECDSA-sign"
					" (output buffer = %lu)\n",
					(unsigned long)len);
			}
			return 0;
		}
		sig_len = br_ecdsa_sign_asn1_get_default()(
			br_ec_get_default(), hc, hv, &pc->sk->key.ec, data);
		if (sig_len == 0) {
			if (pc->verbose) {
				fprintf(stderr, "ERROR: ECDSA-sign failure\n");
			}
			return 0;
		}
		return sig_len;

	default:
		return 0;
	}
}

static const br_ssl_server_policy_class policy_vtable = {
	sizeof(policy_context),
	sp_choose,
	sp_do_keyx,
	sp_do_sign
};

void
free_alpn(void *alpn)
{
	xfree(*(char **)alpn);
}

/* see brssl.h */
int
do_server(int argc, char *argv[])
{
	int retcode;
	int verbose;
	int trace;
	int i, bidi;
	const char *bind_name;
	const char *port;
	unsigned vmin, vmax;
	cipher_suite *suites;
	size_t num_suites;
	uint16_t *suite_ids;
	unsigned hfuns;
	int cbhash;
	br_x509_certificate *chain;
	size_t chain_len;
	int cert_signer_algo;
	private_key *sk;
	anchor_list anchors = VEC_INIT;
	VECTOR(char *) alpn_names = VEC_INIT;
	br_x509_minimal_context xc;
	const br_hash_class *dnhash;
	size_t u;
	br_ssl_server_context cc;
	policy_context pc;
	br_ssl_session_cache_lru lru;
	unsigned char *iobuf, *cache;
	size_t iobuf_len, cache_len;
	uint32_t flags;
	SOCKET server_fd, fd;

	retcode = 0;
	verbose = 1;
	trace = 0;
	bind_name = NULL;
	port = NULL;
	bidi = 1;
	vmin = 0;
	vmax = 0;
	suites = NULL;
	num_suites = 0;
	hfuns = 0;
	cbhash = 0;
	suite_ids = NULL;
	chain = NULL;
	chain_len = 0;
	sk = NULL;
	iobuf = NULL;
	iobuf_len = 0;
	cache = NULL;
	cache_len = (size_t)-1;
	flags = 0;
	server_fd = INVALID_SOCKET;
	fd = INVALID_SOCKET;
	for (i = 0; i < argc; i ++) {
		const char *arg;

		arg = argv[i];
		if (arg[0] != '-') {
			usage_server();
			goto server_exit_error;
		}
		if (eqstr(arg, "-v") || eqstr(arg, "-verbose")) {
			verbose = 1;
		} else if (eqstr(arg, "-q") || eqstr(arg, "-quiet")) {
			verbose = 0;
		} else if (eqstr(arg, "-trace")) {
			trace = 1;
		} else if (eqstr(arg, "-b")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-b'\n");
				usage_server();
				goto server_exit_error;
			}
			if (bind_name != NULL) {
				fprintf(stderr, "ERROR: duplicate bind host\n");
				usage_server();
				goto server_exit_error;
			}
			bind_name = argv[i];
		} else if (eqstr(arg, "-p")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-p'\n");
				usage_server();
				goto server_exit_error;
			}
			if (port != NULL) {
				fprintf(stderr, "ERROR: duplicate bind port\n");
				usage_server();
				goto server_exit_error;
			}
			port = argv[i];
		} else if (eqstr(arg, "-mono")) {
			bidi = 0;
		} else if (eqstr(arg, "-buf")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-buf'\n");
				usage_server();
				goto server_exit_error;
			}
			arg = argv[i];
			if (iobuf_len != 0) {
				fprintf(stderr,
					"ERROR: duplicate I/O buffer length\n");
				usage_server();
				goto server_exit_error;
			}
			iobuf_len = parse_size(arg);
			if (iobuf_len == (size_t)-1) {
				usage_server();
				goto server_exit_error;
			}
		} else if (eqstr(arg, "-cache")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-cache'\n");
				usage_server();
				goto server_exit_error;
			}
			arg = argv[i];
			if (cache_len != (size_t)-1) {
				fprintf(stderr, "ERROR: duplicate session"
					" cache length\n");
				usage_server();
				goto server_exit_error;
			}
			cache_len = parse_size(arg);
			if (cache_len == (size_t)-1) {
				usage_server();
				goto server_exit_error;
			}
		} else if (eqstr(arg, "-cert")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-cert'\n");
				usage_server();
				goto server_exit_error;
			}
			if (chain != NULL) {
				fprintf(stderr,
					"ERROR: duplicate certificate chain\n");
				usage_server();
				goto server_exit_error;
			}
			arg = argv[i];
			chain = read_certificates(arg, &chain_len);
			if (chain == NULL || chain_len == 0) {
				goto server_exit_error;
			}
		} else if (eqstr(arg, "-key")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-key'\n");
				usage_server();
				goto server_exit_error;
			}
			if (sk != NULL) {
				fprintf(stderr,
					"ERROR: duplicate private key\n");
				usage_server();
				goto server_exit_error;
			}
			arg = argv[i];
			sk = read_private_key(arg);
			if (sk == NULL) {
				goto server_exit_error;
			}
		} else if (eqstr(arg, "-CA")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-CA'\n");
				usage_server();
				goto server_exit_error;
			}
			arg = argv[i];
			if (read_trust_anchors(&anchors, arg) == 0) {
				usage_server();
				goto server_exit_error;
			}
		} else if (eqstr(arg, "-anon_ok")) {
			flags |= BR_OPT_TOLERATE_NO_CLIENT_AUTH;
		} else if (eqstr(arg, "-list")) {
			list_names();
			goto server_exit;
		} else if (eqstr(arg, "-vmin")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-vmin'\n");
				usage_server();
				goto server_exit_error;
			}
			arg = argv[i];
			if (vmin != 0) {
				fprintf(stderr,
					"ERROR: duplicate minimum version\n");
				usage_server();
				goto server_exit_error;
			}
			vmin = parse_version(arg, strlen(arg));
			if (vmin == 0) {
				fprintf(stderr,
					"ERROR: unrecognised version '%s'\n",
					arg);
				usage_server();
				goto server_exit_error;
			}
		} else if (eqstr(arg, "-vmax")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-vmax'\n");
				usage_server();
				goto server_exit_error;
			}
			arg = argv[i];
			if (vmax != 0) {
				fprintf(stderr,
					"ERROR: duplicate maximum version\n");
				usage_server();
				goto server_exit_error;
			}
			vmax = parse_version(arg, strlen(arg));
			if (vmax == 0) {
				fprintf(stderr,
					"ERROR: unrecognised version '%s'\n",
					arg);
				usage_server();
				goto server_exit_error;
			}
		} else if (eqstr(arg, "-cs")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-cs'\n");
				usage_server();
				goto server_exit_error;
			}
			arg = argv[i];
			if (suites != NULL) {
				fprintf(stderr, "ERROR: duplicate list"
					" of cipher suites\n");
				usage_server();
				goto server_exit_error;
			}
			suites = parse_suites(arg, &num_suites);
			if (suites == NULL) {
				usage_server();
				goto server_exit_error;
			}
		} else if (eqstr(arg, "-hf")) {
			unsigned x;

			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-hf'\n");
				usage_server();
				goto server_exit_error;
			}
			arg = argv[i];
			x = parse_hash_functions(arg);
			if (x == 0) {
				usage_server();
				goto server_exit_error;
			}
			hfuns |= x;
		} else if (eqstr(arg, "-cbhash")) {
			cbhash = 1;
		} else if (eqstr(arg, "-serverpref")) {
			flags |= BR_OPT_ENFORCE_SERVER_PREFERENCES;
		} else if (eqstr(arg, "-noreneg")) {
			flags |= BR_OPT_NO_RENEGOTIATION;
		} else if (eqstr(arg, "-alpn")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-alpn'\n");
				usage_server();
				goto server_exit_error;
			}
			VEC_ADD(alpn_names, xstrdup(argv[i]));
		} else if (eqstr(arg, "-strictalpn")) {
			flags |= BR_OPT_FAIL_ON_ALPN_MISMATCH;
		} else {
			fprintf(stderr, "ERROR: unknown option: '%s'\n", arg);
			usage_server();
			goto server_exit_error;
		}
	}
	if (port == NULL) {
		port = "4433";
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
		usage_server();
		goto server_exit_error;
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
	if (chain == NULL || chain_len == 0) {
		fprintf(stderr, "ERROR: no certificate chain provided\n");
		goto server_exit_error;
	}
	if (sk == NULL) {
		fprintf(stderr, "ERROR: no private key provided\n");
		goto server_exit_error;
	}
	switch (sk->key_type) {
		int curve;
		uint32_t supp;

	case BR_KEYTYPE_RSA:
		break;
	case BR_KEYTYPE_EC:
		curve = sk->key.ec.curve;
		supp = br_ec_get_default()->supported_curves;
		if (curve > 31 || !((supp >> curve) & 1)) {
			fprintf(stderr, "ERROR: private key curve (%d)"
				" is not supported\n", curve);
			goto server_exit_error;
		}
		break;
	default:
		fprintf(stderr, "ERROR: unsupported private key type (%d)\n",
			sk->key_type);
		break;
	}
	cert_signer_algo = get_cert_signer_algo(chain);
	if (cert_signer_algo == 0) {
		goto server_exit_error;
	}
	if (verbose) {
		const char *csas;

		switch (cert_signer_algo) {
		case BR_KEYTYPE_RSA: csas = "RSA"; break;
		case BR_KEYTYPE_EC:  csas = "EC"; break;
		default:
			csas = "unknown";
			break;
		}
		fprintf(stderr, "Issuing CA key type: %d (%s)\n",
			cert_signer_algo, csas);
	}
	if (iobuf_len == 0) {
		if (bidi) {
			iobuf_len = BR_SSL_BUFSIZE_BIDI;
		} else {
			iobuf_len = BR_SSL_BUFSIZE_MONO;
		}
	}
	iobuf = xmalloc(iobuf_len);
	if (cache_len == (size_t)-1) {
		cache_len = 5000;
	}
	cache = xmalloc(cache_len);

	/*
	 * Compute implementation requirements and inject implementations.
	 */
	suite_ids = xmalloc(num_suites * sizeof *suite_ids);
	br_ssl_server_zero(&cc);
	br_ssl_engine_set_versions(&cc.eng, vmin, vmax);
	br_ssl_engine_set_all_flags(&cc.eng, flags);
	if (vmin <= BR_TLS11) {
		if (!(hfuns & (1 << br_md5_ID))) {
			fprintf(stderr, "ERROR: TLS 1.0 and 1.1 need MD5\n");
			goto server_exit_error;
		}
		if (!(hfuns & (1 << br_sha1_ID))) {
			fprintf(stderr, "ERROR: TLS 1.0 and 1.1 need SHA-1\n");
			goto server_exit_error;
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
			goto server_exit_error;
		}
		if ((req & REQ_SHA1) != 0 && !(hfuns & (1 << br_sha1_ID))) {
			fprintf(stderr,
				"ERROR: cipher suite %s requires SHA-1\n",
				suites[u].name);
			goto server_exit_error;
		}
		if ((req & REQ_SHA256) != 0 && !(hfuns & (1 << br_sha256_ID))) {
			fprintf(stderr,
				"ERROR: cipher suite %s requires SHA-256\n",
				suites[u].name);
			goto server_exit_error;
		}
		if ((req & REQ_SHA384) != 0 && !(hfuns & (1 << br_sha384_ID))) {
			fprintf(stderr,
				"ERROR: cipher suite %s requires SHA-384\n",
				suites[u].name);
			goto server_exit_error;
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
		if ((req & (REQ_ECDHE_RSA | REQ_ECDHE_ECDSA)) != 0) {
			br_ssl_engine_set_default_ec(&cc.eng);
		}
	}
	br_ssl_engine_set_suites(&cc.eng, suite_ids, num_suites);

	dnhash = NULL;
	for (u = 0; hash_functions[u].name; u ++) {
		const br_hash_class *hc;
		int id;

		hc = hash_functions[u].hclass;
		id = (hc->desc >> BR_HASHDESC_ID_OFF) & BR_HASHDESC_ID_MASK;
		if ((hfuns & ((unsigned)1 << id)) != 0) {
			dnhash = hc;
			br_ssl_engine_set_hash(&cc.eng, id, hc);
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

	br_ssl_session_cache_lru_init(&lru, cache, cache_len);
	br_ssl_server_set_cache(&cc, &lru.vtable);

	if (VEC_LEN(alpn_names) != 0) {
		br_ssl_engine_set_protocol_names(&cc.eng,
			(const char **)&VEC_ELT(alpn_names, 0),
			VEC_LEN(alpn_names));
	}

	/*
	 * Set the policy handler (that chooses the actual cipher suite,
	 * selects the certificate chain, and runs the private key
	 * operations).
	 */
	pc.vtable = &policy_vtable;
	pc.verbose = verbose;
	pc.chain = chain;
	pc.chain_len = chain_len;
	pc.cert_signer_algo = cert_signer_algo;
	pc.sk = sk;
	pc.cbhash = cbhash;
	br_ssl_server_set_policy(&cc, &pc.vtable);

	/*
	 * If trust anchors have been configured, then set an X.509
	 * validation engine and activate client certificate
	 * authentication.
	 */
	if (VEC_LEN(anchors) != 0) {
		br_x509_minimal_init(&xc, dnhash,
			&VEC_ELT(anchors, 0), VEC_LEN(anchors));
		for (u = 0; hash_functions[u].name; u ++) {
			const br_hash_class *hc;
			int id;

			hc = hash_functions[u].hclass;
			id = (hc->desc >> BR_HASHDESC_ID_OFF)
				& BR_HASHDESC_ID_MASK;
			if ((hfuns & ((unsigned)1 << id)) != 0) {
				br_x509_minimal_set_hash(&xc, id, hc);
			}
		}
		br_ssl_engine_set_default_rsavrfy(&cc.eng);
		br_ssl_engine_set_default_ecdsa(&cc.eng);
		br_x509_minimal_set_rsa(&xc, br_rsa_pkcs1_vrfy_get_default());
		br_x509_minimal_set_ecdsa(&xc,
			br_ec_get_default(), br_ecdsa_vrfy_asn1_get_default());
		br_ssl_engine_set_x509(&cc.eng, &xc.vtable);
		br_ssl_server_set_trust_anchor_names_alt(&cc,
			&VEC_ELT(anchors, 0), VEC_LEN(anchors));
	}

	br_ssl_engine_set_buffer(&cc.eng, iobuf, iobuf_len, bidi);

	/*
	 * On Unix systems, we need to ignore SIGPIPE.
	 */
#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
#endif

	/*
	 * Open the server socket.
	 */
	server_fd = host_bind(bind_name, port, verbose);
	if (server_fd == INVALID_SOCKET) {
		goto server_exit_error;
	}

	/*
	 * Process incoming clients, one at a time. Note that we do not
	 * accept any client until the previous connection has finished:
	 * this is voluntary, since the tool uses stdin/stdout for
	 * application data, and thus cannot really run two connections
	 * simultaneously.
	 */
	for (;;) {
		int x;
		unsigned run_flags;

		fd = accept_client(server_fd, verbose, 1);
		if (fd == INVALID_SOCKET) {
			goto server_exit_error;
		}
		br_ssl_server_reset(&cc);
		run_flags = (verbose ? RUN_ENGINE_VERBOSE : 0)
			| (trace ? RUN_ENGINE_TRACE : 0);
		x = run_ssl_engine(&cc.eng, fd, run_flags);
#ifdef _WIN32
		closesocket(fd);
#else
		close(fd);
#endif
		fd = INVALID_SOCKET;
		if (x < -1) {
			goto server_exit_error;
		}
	}

	/*
	 * Release allocated structures.
	 */
server_exit:
	xfree(suites);
	xfree(suite_ids);
	free_certificates(chain, chain_len);
	free_private_key(sk);
	VEC_CLEAREXT(anchors, &free_ta_contents);
	VEC_CLEAREXT(alpn_names, &free_alpn);
	xfree(iobuf);
	xfree(cache);
	if (fd != INVALID_SOCKET) {
#ifdef _WIN32
		closesocket(fd);
#else
		close(fd);
#endif
	}
	if (server_fd != INVALID_SOCKET) {
#ifdef _WIN32
		closesocket(server_fd);
#else
		close(server_fd);
#endif
	}
	return retcode;

server_exit_error:
	retcode = -1;
	goto server_exit;
}
