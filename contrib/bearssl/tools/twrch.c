/*
 * Copyright (c) 2017 Thomas Pornin <pornin@bolet.org>
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

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "brssl.h"

static int verbose = 0;

static void
usage_twrch(void)
{
	fprintf(stderr,
"usage: brssl twrch [ options ]\n");
	fprintf(stderr,
"options:\n");
	fprintf(stderr,
"   -trace          dump all packets on stderr\n");
	fprintf(stderr,
"   -v              verbose error messages on stderr\n");
	fprintf(stderr,
"   -server         act as an SSL server\n");
	fprintf(stderr,
"   -client         act as an SSL client\n");
	fprintf(stderr,
"   -sni name       use specified name for SNI\n");
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
"   -CA file        add trust anchors from 'file' (for peer auth)\n");
	fprintf(stderr,
"   -anon_ok        request but do not require a client certificate\n");
	fprintf(stderr,
"   -nostaticecdh   prohibit full-static ECDH (client only)\n");
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
"   -serverpref     enforce server's preferences for cipher suites\n");
	fprintf(stderr,
"   -noreneg        prohibit renegotiations\n");
	fprintf(stderr,
"   -alpn name      add protocol name to list of protocols (ALPN extension)\n");
	fprintf(stderr,
"   -strictalpn     fail on ALPN mismatch\n");
}

static void
free_alpn(void *alpn)
{
	xfree(*(char **)alpn);
}

static void
dump_blob(const char *name, const void *data, size_t len)
{
	const unsigned char *buf;
	size_t u;

	buf = data;
	fprintf(stderr, "%s (len = %lu)", name, (unsigned long)len);
	for (u = 0; u < len; u ++) {
		if ((u & 15) == 0) {
			fprintf(stderr, "\n%08lX  ", (unsigned long)u);
		} else if ((u & 7) == 0) {
			fprintf(stderr, " ");
		}
		fprintf(stderr, " %02x", buf[u]);
	}
	fprintf(stderr, "\n");
}

/*
 * Callback for reading bytes from standard input.
 */
static int
stdin_read(void *ctx, unsigned char *buf, size_t len)
{
	for (;;) {
#ifdef _WIN32
		DWORD rlen;
#else
		ssize_t rlen;
#endif
		int eof;

#ifdef _WIN32
		eof = !ReadFile(GetStdHandle(STD_INPUT_HANDLE),
			buf, len, &rlen, NULL) || rlen == 0;
#else
		rlen = read(0, buf, len);
		if (rlen <= 0) {
			if (rlen < 0 && errno == EINTR) {
				continue;
			}
			eof = 1;
		} else {
			eof = 0;
		}
#endif
		if (eof) {
			if (*(int *)ctx) {
				if (verbose) {
					fprintf(stderr, "recv: EOF\n");
				}
			}
			return -1;
		}
		if (*(int *)ctx) {
			dump_blob("recv", buf, (size_t)rlen);
		}
		return (int)rlen;
	}
}

/*
 * Callback for writing bytes on standard output.
 */
static int
stdout_write(void *ctx, const unsigned char *buf, size_t len)
{
	for (;;) {
#ifdef _WIN32
		DWORD wlen;
#else
		ssize_t wlen;
#endif
		int eof;

#ifdef _WIN32
		eof = !WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
			buf, len, &wlen, NULL);
#else
		wlen = write(1, buf, len);
		if (wlen <= 0) {
			if (wlen < 0 && errno == EINTR) {
				continue;
			}
			eof = 1;
		} else {
			eof = 0;
		}
#endif
		if (eof) {
			if (*(int *)ctx) {
				if (verbose) {
					fprintf(stderr, "send: EOF\n");
				}
			}
			return -1;
		}
		if (*(int *)ctx) {
			dump_blob("send", buf, (size_t)wlen);
		}
		return (int)wlen;
	}
}

static void
print_error(int err)
{
	const char *name, *comment;

	name = find_error_name(err, &comment);
	if (name != NULL) {
		fprintf(stderr, "ERR %d: %s\n   %s\n", err, name, comment);
		return;
	}
	if (err >= BR_ERR_RECV_FATAL_ALERT
		&& err < BR_ERR_RECV_FATAL_ALERT + 256)
	{
		fprintf(stderr, "ERR %d: received fatal alert %d\n",
			err, err - BR_ERR_RECV_FATAL_ALERT);
		return;
	}
	if (err >= BR_ERR_SEND_FATAL_ALERT
		&& err < BR_ERR_SEND_FATAL_ALERT + 256)
	{
		fprintf(stderr, "ERR %d: sent fatal alert %d\n",
			err, err - BR_ERR_SEND_FATAL_ALERT);
		return;
	}
	fprintf(stderr, "ERR %d: UNKNOWN\n", err);
}

/* see brssl.h */
int
do_twrch(int argc, char *argv[])
{
	int retcode;
	int trace;
	int is_client;
	int is_server;
	const char *sni;
	int i, bidi;
	unsigned vmin, vmax;
	cipher_suite *suites;
	size_t num_suites;
	uint16_t *suite_ids;
	unsigned hfuns;
	br_x509_certificate *chain;
	size_t chain_len;
	int cert_signer_algo;
	private_key *sk;
	int nostaticecdh;
	anchor_list anchors = VEC_INIT;
	VECTOR(char *) alpn_names = VEC_INIT;
	br_x509_minimal_context xc;
	x509_noanchor_context xwc;
	const br_hash_class *dnhash;
	size_t u;
	union {
		br_ssl_engine_context eng;
		br_ssl_server_context srv;
		br_ssl_client_context cnt;
	} cc;
	br_ssl_session_cache_lru lru;
	unsigned char *iobuf, *cache;
	size_t iobuf_len, cache_len, minhello_len;
	br_sslio_context ioc;
	uint32_t flags;
	int reconnect;

	retcode = 0;
	trace = 0;
	is_client = 0;
	is_server = 0;
	sni = NULL;
	bidi = 1;
	vmin = 0;
	vmax = 0;
	suites = NULL;
	num_suites = 0;
	suite_ids = NULL;
	hfuns = 0;
	chain = NULL;
	chain_len = 0;
	cert_signer_algo = 0;
	sk = NULL;
	nostaticecdh = 0;
	iobuf = NULL;
	iobuf_len = 0;
	cache = NULL;
	cache_len = (size_t)-1;
	minhello_len = (size_t)-1;
	flags = 0;
	reconnect = 0;
	for (i = 0; i < argc; i ++) {
		const char *arg;

		arg = argv[i];
		if (arg[0] != '-') {
			usage_twrch();
			goto twrch_exit_error;
		}
		if (eqstr(arg, "-trace")) {
			trace = 1;
		} else if (eqstr(arg, "-v")) {
			verbose = 1;
		} else if (eqstr(arg, "-server")) {
			is_server = 1;
		} else if (eqstr(arg, "-client")) {
			is_client = 1;
		} else if (eqstr(arg, "-sni")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-sni'\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			arg = argv[i];
			if (sni != NULL) {
				fprintf(stderr, "ERROR: duplicate SNI\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			sni = arg;
		} else if (eqstr(arg, "-mono")) {
			bidi = 0;
		} else if (eqstr(arg, "-buf")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-buf'\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			arg = argv[i];
			if (iobuf_len != 0) {
				fprintf(stderr,
					"ERROR: duplicate I/O buffer length\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			iobuf_len = parse_size(arg);
			if (iobuf_len == (size_t)-1) {
				usage_twrch();
				goto twrch_exit_error;
			}
		} else if (eqstr(arg, "-cache")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-cache'\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			arg = argv[i];
			if (cache_len != (size_t)-1) {
				fprintf(stderr, "ERROR: duplicate session"
					" cache length\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			cache_len = parse_size(arg);
			if (cache_len == (size_t)-1) {
				usage_twrch();
				goto twrch_exit_error;
			}
		} else if (eqstr(arg, "-cert")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-cert'\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			if (chain != NULL) {
				fprintf(stderr,
					"ERROR: duplicate certificate chain\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			arg = argv[i];
			chain = read_certificates(arg, &chain_len);
			if (chain == NULL || chain_len == 0) {
				goto twrch_exit_error;
			}
		} else if (eqstr(arg, "-key")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-key'\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			if (sk != NULL) {
				fprintf(stderr,
					"ERROR: duplicate private key\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			arg = argv[i];
			sk = read_private_key(arg);
			if (sk == NULL) {
				goto twrch_exit_error;
			}
		} else if (eqstr(arg, "-CA")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-CA'\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			arg = argv[i];
			if (read_trust_anchors(&anchors, arg) == 0) {
				usage_twrch();
				goto twrch_exit_error;
			}
		} else if (eqstr(arg, "-anon_ok")) {
			flags |= BR_OPT_TOLERATE_NO_CLIENT_AUTH;
		} else if (eqstr(arg, "-nostaticecdh")) {
			nostaticecdh = 1;
		} else if (eqstr(arg, "-list")) {
			list_names();
			goto twrch_exit;
		} else if (eqstr(arg, "-vmin")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-vmin'\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			arg = argv[i];
			if (vmin != 0) {
				fprintf(stderr,
					"ERROR: duplicate minimum version\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			vmin = parse_version(arg, strlen(arg));
			if (vmin == 0) {
				fprintf(stderr,
					"ERROR: unrecognised version '%s'\n",
					arg);
				usage_twrch();
				goto twrch_exit_error;
			}
		} else if (eqstr(arg, "-vmax")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-vmax'\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			arg = argv[i];
			if (vmax != 0) {
				fprintf(stderr,
					"ERROR: duplicate maximum version\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			vmax = parse_version(arg, strlen(arg));
			if (vmax == 0) {
				fprintf(stderr,
					"ERROR: unrecognised version '%s'\n",
					arg);
				usage_twrch();
				goto twrch_exit_error;
			}
		} else if (eqstr(arg, "-cs")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-cs'\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			arg = argv[i];
			if (suites != NULL) {
				fprintf(stderr, "ERROR: duplicate list"
					" of cipher suites\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			suites = parse_suites(arg, &num_suites);
			if (suites == NULL) {
				usage_twrch();
				goto twrch_exit_error;
			}
		} else if (eqstr(arg, "-hf")) {
			unsigned x;

			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-hf'\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			arg = argv[i];
			x = parse_hash_functions(arg);
			if (x == 0) {
				usage_twrch();
				goto twrch_exit_error;
			}
			hfuns |= x;
		} else if (eqstr(arg, "-minhello")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-minhello'\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			arg = argv[i];
			if (minhello_len != (size_t)-1) {
				fprintf(stderr, "ERROR: duplicate minimum"
					" ClientHello length\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			minhello_len = parse_size(arg);
			/*
			 * Minimum ClientHello length must fit on 16 bits.
			 */
			if (minhello_len == (size_t)-1
				|| (((minhello_len >> 12) >> 4) != 0))
			{
				usage_twrch();
				goto twrch_exit_error;
			}
		} else if (eqstr(arg, "-serverpref")) {
			flags |= BR_OPT_ENFORCE_SERVER_PREFERENCES;
		} else if (eqstr(arg, "-noreneg")) {
			flags |= BR_OPT_NO_RENEGOTIATION;
		} else if (eqstr(arg, "-alpn")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-alpn'\n");
				usage_twrch();
				goto twrch_exit_error;
			}
			VEC_ADD(alpn_names, xstrdup(argv[i]));
		} else if (eqstr(arg, "-strictalpn")) {
			flags |= BR_OPT_FAIL_ON_ALPN_MISMATCH;
		} else {
			fprintf(stderr, "ERROR: unknown option: '%s'\n", arg);
			usage_twrch();
			goto twrch_exit_error;
		}
	}

	/*
	 * Verify consistency of options.
	 */
	if (!is_client && !is_server) {
		fprintf(stderr, "ERROR:"
			" one of -server and -client must be specified\n");
		usage_twrch();
		goto twrch_exit_error;
	}
	if (is_client && is_server) {
		fprintf(stderr, "ERROR:"
			" -server and -client may not be both specified\n");
		usage_twrch();
		goto twrch_exit_error;
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
		usage_twrch();
		goto twrch_exit_error;
	}
	if (is_server) {
		if (chain == NULL) {
			fprintf(stderr, "ERROR: no certificate specified"
				" for server (-cert)\n");
			usage_twrch();
			goto twrch_exit_error;
		}
		if (sk == NULL) {
			fprintf(stderr, "ERROR: no private key specified"
				" for server (-key)\n");
			usage_twrch();
			goto twrch_exit_error;
		}
	} else {
		if (chain == NULL && sk != NULL) {
			fprintf(stderr, "ERROR: private key (-key)"
				" but no certificate (-cert)");
			usage_twrch();
			goto twrch_exit_error;
		}
		if (chain != NULL && sk == NULL) {
			fprintf(stderr, "ERROR: certificate (-cert)"
				" but no private key (-key)");
			usage_twrch();
			goto twrch_exit_error;
		}
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
	if (sk != NULL) {
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
				goto twrch_exit_error;
			}
			break;
		default:
			fprintf(stderr, "ERROR: unsupported"
				" private key type (%d)\n", sk->key_type);
			goto twrch_exit_error;
		}
	}
	if (chain != NULL) {
		cert_signer_algo = get_cert_signer_algo(chain);
		if (cert_signer_algo == 0) {
			goto twrch_exit_error;
		}
	}
	if (iobuf_len == 0) {
		if (bidi) {
			iobuf_len = BR_SSL_BUFSIZE_BIDI;
		} else {
			iobuf_len = BR_SSL_BUFSIZE_MONO;
		}
	}
	iobuf = xmalloc(iobuf_len);
	if (is_server) {
		if (cache_len == (size_t)-1) {
			cache_len = 5000;
		}
		cache = xmalloc(cache_len);
	}

	/*
	 * Initialise the relevant context.
	 */
	if (is_client) {
		br_ssl_client_zero(&cc.cnt);
	} else {
		br_ssl_server_zero(&cc.srv);
	}

	/*
	 * Compute implementation requirements and inject implementations.
	 */
	suite_ids = xmalloc(num_suites * sizeof *suite_ids);
	br_ssl_engine_set_versions(&cc.eng, vmin, vmax);
	br_ssl_engine_set_all_flags(&cc.eng, flags);
	if (vmin <= BR_TLS11) {
		if (!(hfuns & (1 << br_md5_ID))) {
			fprintf(stderr, "ERROR: TLS 1.0 and 1.1 need MD5\n");
			goto twrch_exit_error;
		}
		if (!(hfuns & (1 << br_sha1_ID))) {
			fprintf(stderr, "ERROR: TLS 1.0 and 1.1 need SHA-1\n");
			goto twrch_exit_error;
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
			goto twrch_exit_error;
		}
		if ((req & REQ_SHA1) != 0 && !(hfuns & (1 << br_sha1_ID))) {
			fprintf(stderr,
				"ERROR: cipher suite %s requires SHA-1\n",
				suites[u].name);
			goto twrch_exit_error;
		}
		if ((req & REQ_SHA256) != 0 && !(hfuns & (1 << br_sha256_ID))) {
			fprintf(stderr,
				"ERROR: cipher suite %s requires SHA-256\n",
				suites[u].name);
			goto twrch_exit_error;
		}
		if ((req & REQ_SHA384) != 0 && !(hfuns & (1 << br_sha384_ID))) {
			fprintf(stderr,
				"ERROR: cipher suite %s requires SHA-384\n",
				suites[u].name);
			goto twrch_exit_error;
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
		if (is_client && (req & REQ_RSAKEYX) != 0) {
			br_ssl_client_set_default_rsapub(&cc.cnt);
		}
		if (is_client && (req & REQ_ECDHE_RSA) != 0) {
			br_ssl_engine_set_default_rsavrfy(&cc.eng);
		}
		if (is_client && (req & REQ_ECDH) != 0) {
			br_ssl_engine_set_default_ec(&cc.eng);
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
	if (VEC_LEN(alpn_names) != 0) {
		br_ssl_engine_set_protocol_names(&cc.eng,
			(const char **)&VEC_ELT(alpn_names, 0),
			VEC_LEN(alpn_names));
	}

	/*
	 * In server role, we use a session cache (size can be
	 * specified; if size is zero, then no cache is set).
	 */
	if (is_server && cache != NULL) {
		br_ssl_session_cache_lru_init(&lru, cache, cache_len);
		br_ssl_server_set_cache(&cc.srv, &lru.vtable);
	}

	/*
	 * For a server, set the policy handler.
	 */
	if (is_server) {
		switch (sk->key_type) {
		case BR_KEYTYPE_RSA:
			br_ssl_server_set_single_rsa(&cc.srv,
				chain, chain_len, &sk->key.rsa,
				BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN,
				br_rsa_private_get_default(),
				br_rsa_pkcs1_sign_get_default());
			break;
		case BR_KEYTYPE_EC:
			br_ssl_server_set_single_ec(&cc.srv,
				chain, chain_len, &sk->key.ec,
				BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN,
				cert_signer_algo,
				br_ec_get_default(),
				br_ecdsa_sign_asn1_get_default());
			break;
		default:
			fprintf(stderr, "ERROR: unsupported"
				" private key type (%d)\n", sk->key_type);
			goto twrch_exit_error;
		}
	}

	/*
	 * For a client, if a certificate was specified, use it.
	 */
	if (is_client && chain != NULL) {
		switch (sk->key_type) {
			unsigned usages;

		case BR_KEYTYPE_RSA:
			br_ssl_client_set_single_rsa(&cc.cnt,
				chain, chain_len, &sk->key.rsa,
				br_rsa_pkcs1_sign_get_default());
			break;
		case BR_KEYTYPE_EC:
			if (nostaticecdh) {
				cert_signer_algo = 0;
				usages = BR_KEYTYPE_SIGN;
			} else {
				usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
			}
			br_ssl_client_set_single_ec(&cc.cnt,
				chain, chain_len, &sk->key.ec,
				usages, cert_signer_algo,
				br_ec_get_default(),
				br_ecdsa_sign_asn1_get_default());
			break;
		default:
			fprintf(stderr, "ERROR: unsupported"
				" private key type (%d)\n", sk->key_type);
			goto twrch_exit_error;
		}
	}

	/*
	 * On a client, or if trust anchors have been configured, then
	 * set an X.509 validation engine. If there are no trust anchors
	 * (client only), then a "no anchor" wrapper will be applied.
	 */
	if (is_client || VEC_LEN(anchors) != 0) {
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

		if (VEC_LEN(anchors) == 0) {
			x509_noanchor_init(&xwc, &xc.vtable);
			br_ssl_engine_set_x509(&cc.eng, &xwc.vtable);
		} else {
			br_ssl_engine_set_x509(&cc.eng, &xc.vtable);
		}
		if (is_server) {
			br_ssl_server_set_trust_anchor_names_alt(&cc.srv,
				&VEC_ELT(anchors, 0), VEC_LEN(anchors));
		}
	}

	/*
	 * Set I/O buffer.
	 */
	br_ssl_engine_set_buffer(&cc.eng, iobuf, iobuf_len, bidi);

	/*
	 * Start the engine.
	 */
	if (is_client) {
		br_ssl_client_reset(&cc.cnt, sni, 0);
	}
	if (is_server) {
		br_ssl_server_reset(&cc.srv);
	}

	/*
	 * On Unix systems, we want to ignore SIGPIPE: if the peer
	 * closes the connection abruptly, then we want to report it
	 * as a "normal" error (exit code = 1).
	 */
#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
#endif

	/*
	 * Initialize the callbacks for exchanging data over stdin and
	 * stdout.
	 */
	br_sslio_init(&ioc, &cc.eng, stdin_read, &trace, stdout_write, &trace);

	/*
	 * Run the Twrch protocol.
	 */
	for (;;) {
		br_sha1_context sc;
		unsigned char hv[20], tmp[41];
		uint64_t count;
		int fb, i;

		/*
		 * Read line, byte by byte, hashing it on the fly.
		 */
		br_sha1_init(&sc);
		count = 0;
		fb = 0;
		for (;;) {
			unsigned char x;

			if (br_sslio_read(&ioc, &x, 1) < 0) {
				if (count == 0 && reconnect) {
					reconnect = 0;
					if (br_sslio_close(&ioc) < 0) {
						goto twrch_loop_finished;
					}
					if (is_client) {
						br_ssl_client_reset(
							&cc.cnt, sni, 1);
					}
					if (is_server) {
						br_ssl_server_reset(&cc.srv);
					}
					br_sslio_init(&ioc, &cc.eng,
						stdin_read, &trace,
						stdout_write, &trace);
					continue;
				}
				goto twrch_loop_finished;
			}
			if (count == 0) {
				fb = x;
			}
			if (x == 0x0A) {
				break;
			}
			br_sha1_update(&sc, &x, 1);
			count ++;
		}
		if (count == 1) {
			switch (fb) {
			case 'C':
				br_sslio_close(&ioc);
				goto twrch_loop_finished;
			case 'T':
				if (br_sslio_close(&ioc) < 0) {
					goto twrch_loop_finished;
				}
				if (is_client) {
					br_ssl_client_reset(&cc.cnt, sni, 1);
				}
				if (is_server) {
					br_ssl_server_reset(&cc.srv);
				}
				br_sslio_init(&ioc, &cc.eng,
					stdin_read, &trace,
					stdout_write, &trace);
				continue;
			case 'G':
				if (!br_ssl_engine_renegotiate(&cc.eng)) {
					br_sslio_write_all(&ioc, "DENIED\n", 7);
					br_sslio_flush(&ioc);
				} else {
					br_sslio_write_all(&ioc, "OK\n", 3);
					br_sslio_flush(&ioc);
				}
				continue;
			case 'R':
				reconnect = 1;
				br_sslio_write_all(&ioc, "OK\n", 3);
				br_sslio_flush(&ioc);
				continue;
			case 'U':
				if (is_client) {
					br_ssl_client_forget_session(&cc.cnt);
				}
				if (is_server && cache != NULL) {
					br_ssl_session_parameters pp;

					br_ssl_engine_get_session_parameters(
						&cc.eng, &pp);
					if (pp.session_id_len == 32) {
						br_ssl_session_cache_lru_forget(
							&lru, pp.session_id);
					}
				}
				br_sslio_write_all(&ioc, "DONE\n", 5);
				br_sslio_flush(&ioc);
				continue;
			}
		}
		br_sha1_out(&sc, hv);
		for (i = 0; i < 20; i ++) {
			int x;

			x = hv[i];
			tmp[(i << 1) + 0] = "0123456789abcdef"[x >> 4];
			tmp[(i << 1) + 1] = "0123456789abcdef"[x & 15];
		}
		tmp[40] = 0x0A;
		br_sslio_write_all(&ioc, tmp, 41);
		br_sslio_flush(&ioc);
	}

twrch_loop_finished:
	if (br_ssl_engine_current_state(&cc.eng) == BR_SSL_CLOSED) {
		int err;

		err = br_ssl_engine_last_error(&cc.eng);
		if (err == 0) {
			retcode = 0;
		} else {
			if (verbose) {
				print_error(err);
			}
			retcode = 1;
		}
	} else {
		if (verbose) {
			fprintf(stderr, "Engine not closed!\n");
		}
		retcode = 1;
	}

	/*
	 * Release allocated structures.
	 */
twrch_exit:
	xfree(suites);
	xfree(suite_ids);
	free_certificates(chain, chain_len);
	free_private_key(sk);
	VEC_CLEAREXT(anchors, &free_ta_contents);
	VEC_CLEAREXT(alpn_names, &free_alpn);
	xfree(iobuf);
	xfree(cache);
	return retcode;

twrch_exit_error:
	retcode = -1;
	goto twrch_exit;
}
