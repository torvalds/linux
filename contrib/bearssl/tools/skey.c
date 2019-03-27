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

#include "brssl.h"
#include "bearssl.h"

typedef struct {
	int print_text;
	int print_C;
	const char *rawder;
	const char *rawpem;
	const char *pk8der;
	const char *pk8pem;
} outspec;

static void
print_int_text(const char *name, const unsigned char *buf, size_t len)
{
	size_t u;

	printf("%s = ", name);
	for (u = 0; u < len; u ++) {
		printf("%02X", buf[u]);
	}
	printf("\n");
}

static void
print_int_C(const char *name, const unsigned char *buf, size_t len)
{
	size_t u;

	printf("\nstatic const unsigned char %s[] = {", name);
	for (u = 0; u < len; u ++) {
		if (u != 0) {
			printf(",");
		}
		if (u % 12 == 0) {
			printf("\n\t");
		} else {
			printf(" ");
		}
		printf("0x%02X", buf[u]);
	}
	printf("\n};\n");
}

static int
write_to_file(const char *name, const void *data, size_t len)
{
	FILE *f;

	f = fopen(name, "wb");
	if (f == NULL) {
		fprintf(stderr,
			"ERROR: cannot open file '%s' for writing\n",
			name);
		return 0;
	}
	if (fwrite(data, 1, len, f) != len) {
		fclose(f);
		fprintf(stderr,
			"ERROR: cannot write to file '%s'\n",
			name);
		return 0;
	}
	fclose(f);
	return 1;
}

static int
write_to_pem_file(const char *name,
	const void *data, size_t len, const char *banner)
{
	void *pem;
	size_t pemlen;
	int r;

	pemlen = br_pem_encode(NULL, NULL, len, banner, 0);
	pem = xmalloc(pemlen + 1);
	br_pem_encode(pem, data, len, banner, 0);
	r = write_to_file(name, pem, pemlen);
	xfree(pem);
	return r;
}

static int
print_rsa(const br_rsa_private_key *sk, outspec *os)
{
	int ret;
	unsigned char *n, *d, *buf;
	uint32_t e;
	size_t nlen, dlen, len;
	br_rsa_compute_modulus cm;
	br_rsa_compute_pubexp ce;
	br_rsa_compute_privexp cd;
	br_rsa_public_key pk;
	unsigned char ebuf[4];

	n = NULL;
	d = NULL;
	buf = NULL;
	ret = 1;
	if (os->print_text) {
		print_int_text("p ", sk->p, sk->plen);
		print_int_text("q ", sk->q, sk->qlen);
		print_int_text("dp", sk->dp, sk->dplen);
		print_int_text("dq", sk->dq, sk->dqlen);
		print_int_text("iq", sk->iq, sk->iqlen);
	}
	if (os->print_C) {
		print_int_C("RSA_P", sk->p, sk->plen);
		print_int_C("RSA_Q", sk->q, sk->qlen);
		print_int_C("RSA_DP", sk->dp, sk->dplen);
		print_int_C("RSA_DQ", sk->dq, sk->dqlen);
		print_int_C("RSA_IQ", sk->iq, sk->iqlen);
		printf("\nstatic const br_rsa_private_key RSA = {\n");
		printf("\t%lu,\n", (unsigned long)sk->n_bitlen);
		printf("\t(unsigned char *)RSA_P, sizeof RSA_P,\n");
		printf("\t(unsigned char *)RSA_Q, sizeof RSA_Q,\n");
		printf("\t(unsigned char *)RSA_DP, sizeof RSA_DP,\n");
		printf("\t(unsigned char *)RSA_DQ, sizeof RSA_DQ,\n");
		printf("\t(unsigned char *)RSA_IQ, sizeof RSA_IQ\n");
		printf("};\n");
	}

	if (os->rawder == NULL && os->rawpem == NULL
		&& os->pk8der == NULL && os->pk8pem == NULL)
	{
		return ret;
	}

	cm = br_rsa_compute_modulus_get_default();
	ce = br_rsa_compute_pubexp_get_default();
	cd = br_rsa_compute_privexp_get_default();
	nlen = cm(NULL, sk);
	if (nlen == 0) {
		goto print_RSA_error;
	}
	n = xmalloc(nlen);
	if (cm(n, sk) != nlen) {
		goto print_RSA_error;
	}
	e = ce(sk);
	if (e == 0) {
		goto print_RSA_error;
	}
	dlen = cd(NULL, sk, e);
	if (dlen == 0) {
		goto print_RSA_error;
	}
	d = xmalloc(dlen);
	if (cd(d, sk, e) != dlen) {
		goto print_RSA_error;
	}
	ebuf[0] = e >> 24;
	ebuf[1] = e >> 16;
	ebuf[2] = e >> 8;
	ebuf[3] = e;
	pk.n = n;
	pk.nlen = nlen;
	pk.e = ebuf;
	pk.elen = sizeof ebuf;

	if (os->rawder != NULL || os->rawpem != NULL) {
		len = br_encode_rsa_raw_der(NULL, sk, &pk, d, dlen);
		if (len == 0) {
			goto print_RSA_error;
		}
		buf = xmalloc(len);
		if (br_encode_rsa_raw_der(buf, sk, &pk, d, dlen) != len) {
			goto print_RSA_error;
		}
		if (os->rawder != NULL) {
			ret &= write_to_file(os->rawder, buf, len);
		}
		if (os->rawpem != NULL) {
			ret &= write_to_pem_file(os->rawpem,
				buf, len, "RSA PRIVATE KEY");
		}
		xfree(buf);
		buf = NULL;
	}

	if (os->pk8der != NULL || os->pk8pem != NULL) {
		len = br_encode_rsa_pkcs8_der(NULL, sk, &pk, d, dlen);
		if (len == 0) {
			goto print_RSA_error;
		}
		buf = xmalloc(len);
		if (br_encode_rsa_pkcs8_der(buf, sk, &pk, d, dlen) != len) {
			goto print_RSA_error;
		}
		if (os->pk8der != NULL) {
			ret &= write_to_file(os->pk8der, buf, len);
		}
		if (os->pk8pem != NULL) {
			ret &= write_to_pem_file(os->pk8pem,
				buf, len, "PRIVATE KEY");
		}
		xfree(buf);
		buf = NULL;
	}

print_RSA_exit:
	xfree(n);
	xfree(d);
	xfree(buf);
	return ret;

print_RSA_error:
	fprintf(stderr, "ERROR: cannot encode RSA key\n");
	ret = 0;
	goto print_RSA_exit;
}

static int
print_ec(const br_ec_private_key *sk, outspec *os)
{
	br_ec_public_key pk;
	unsigned kbuf[BR_EC_KBUF_PUB_MAX_SIZE];
	unsigned char *buf;
	size_t len;
	int r;

	if (os->print_text) {
		print_int_text("x", sk->x, sk->xlen);
	}
	if (os->print_C) {
		print_int_C("EC_X", sk->x, sk->xlen);
		printf("\nstatic const br_ec_private_key EC = {\n");
		printf("\t%d,\n", sk->curve);
		printf("\t(unsigned char *)EC_X, sizeof EC_X\n");
		printf("};\n");
	}

	if (os->rawder == NULL && os->rawpem == NULL
		&& os->pk8der == NULL && os->pk8pem == NULL)
	{
		return 1;
	}
	if (br_ec_compute_pub(br_ec_get_default(), &pk, kbuf, sk) == 0) {
		fprintf(stderr,
			"ERROR: cannot re-encode (unsupported curve)\n");
		return 0;
	}

	r = 1;
	if (os->rawder != NULL || os->rawpem != NULL) {
		len = br_encode_ec_raw_der(NULL, sk, &pk);
		if (len == 0) {
			fprintf(stderr, "ERROR: cannot re-encode"
				" (unsupported curve)\n");
			return 0;
		}
		buf = xmalloc(len);
		if (br_encode_ec_raw_der(buf, sk, &pk) != len) {
			fprintf(stderr, "ERROR: re-encode failure\n");
			xfree(buf);
			return 0;
		}
		if (os->rawder != NULL) {
			r &= write_to_file(os->rawder, buf, len);
		}
		if (os->rawpem != NULL) {
			r &= write_to_pem_file(os->rawpem,
				buf, len, "EC PRIVATE KEY");
		}
		xfree(buf);
	}
	if (os->pk8der != NULL || os->pk8pem != NULL) {
		len = br_encode_ec_pkcs8_der(NULL, sk, &pk);
		if (len == 0) {
			fprintf(stderr, "ERROR: cannot re-encode"
				" (unsupported curve)\n");
			return 0;
		}
		buf = xmalloc(len);
		if (br_encode_ec_pkcs8_der(buf, sk, &pk) != len) {
			fprintf(stderr, "ERROR: re-encode failure\n");
			xfree(buf);
			return 0;
		}
		if (os->pk8der != NULL) {
			r &= write_to_file(os->pk8der, buf, len);
		}
		if (os->pk8pem != NULL) {
			r &= write_to_pem_file(os->pk8pem,
				buf, len, "PRIVATE KEY");
		}
		xfree(buf);
	}
	return r;
}

static int
parse_rsa_spec(const char *kgen_spec, unsigned *size, uint32_t *pubexp)
{
	const char *p;
	char *end;
	unsigned long ul;

	p = kgen_spec;
	if (*p != 'r' && *p != 'R') {
		return 0;
	}
	p ++;
	if (*p != 's' && *p != 'S') {
		return 0;
	}
	p ++;
	if (*p != 'a' && *p != 'A') {
		return 0;
	}
	p ++;
	if (*p == 0) {
		*size = 2048;
		*pubexp = 3;
		return 1;
	} else if (*p != ':') {
		return 0;
	}
	p ++;
	ul = strtoul(p, &end, 10);
	if (ul < 512 || ul > 32768) {
		return 0;
	}
	*size = ul;
	p = end;
	if (*p == 0) {
		*pubexp = 3;
		return 1;
	} else if (*p != ':') {
		return 0;
	}
	p ++;
	ul = strtoul(p, &end, 10);
	if ((ul & 1) == 0 || ul == 1 || ((ul >> 30) >> 2) != 0) {
		return 0;
	}
	*pubexp = ul;
	if (*end != 0) {
		return 0;
	}
	return 1;
}

static int
keygen_rsa(unsigned size, uint32_t pubexp, outspec *os)
{
	br_hmac_drbg_context rng;
	br_prng_seeder seeder;
	br_rsa_keygen kg;
	br_rsa_private_key sk;
	unsigned char *kbuf_priv;
	uint32_t r;

	seeder = br_prng_seeder_system(NULL);
	if (seeder == 0) {
		fprintf(stderr, "ERROR: no system source of randomness\n");
		return 0;
	}
	br_hmac_drbg_init(&rng, &br_sha256_vtable, NULL, 0);
	if (!seeder(&rng.vtable)) {
		fprintf(stderr, "ERROR: system source of randomness failed\n");
		return 0;
	}
	kbuf_priv = xmalloc(BR_RSA_KBUF_PRIV_SIZE(size));
	kg = br_rsa_keygen_get_default();
	r = kg(&rng.vtable, &sk, kbuf_priv, NULL, NULL, size, pubexp);
	if (!r) {
		fprintf(stderr, "ERROR: RSA key pair generation failed\n");
	} else {
		r = print_rsa(&sk, os);
	}
	xfree(kbuf_priv);
	return r;
}

static int
parse_ec_spec(const char *kgen_spec, int *curve)
{
	const char *p;

	*curve = 0;
	p = kgen_spec;
	if (*p != 'e' && *p != 'E') {
		return 0;
	}
	p ++;
	if (*p != 'c' && *p != 'C') {
		return 0;
	}
	p ++;
	if (*p == 0) {
		*curve = BR_EC_secp256r1;
		return 1;
	}
	if (*p != ':') {
		return 0;
	}
	*curve = get_curve_by_name(p);
	return *curve > 0;
}

static int
keygen_ec(int curve, outspec *os)
{
	br_hmac_drbg_context rng;
	br_prng_seeder seeder;
	const br_ec_impl *impl;
	br_ec_private_key sk;
	unsigned char kbuf_priv[BR_EC_KBUF_PRIV_MAX_SIZE];
	size_t len;

	seeder = br_prng_seeder_system(NULL);
	if (seeder == 0) {
		fprintf(stderr, "ERROR: no system source of randomness\n");
		return 0;
	}
	br_hmac_drbg_init(&rng, &br_sha256_vtable, NULL, 0);
	if (!seeder(&rng.vtable)) {
		fprintf(stderr, "ERROR: system source of randomness failed\n");
		return 0;
	}
	impl = br_ec_get_default();
	len = br_ec_keygen(&rng.vtable, impl, &sk, kbuf_priv, curve);
	if (len == 0) {
		fprintf(stderr, "ERROR: curve is not supported\n");
		return 0;
	}
	return print_ec(&sk, os);
}

static int
decode_key(const unsigned char *buf, size_t len, outspec *os)
{
	br_skey_decoder_context dc;
	int err, ret;

	br_skey_decoder_init(&dc);
	br_skey_decoder_push(&dc, buf, len);
	err = br_skey_decoder_last_error(&dc);
	if (err != 0) {
		const char *errname, *errmsg;

		fprintf(stderr, "ERROR (decoding): err=%d\n", err);
		errname = find_error_name(err, &errmsg);
		if (errname != NULL) {
			fprintf(stderr, "  %s: %s\n", errname, errmsg);
		} else {
			fprintf(stderr, "  (unknown)\n");
		}
		return 0;
	}
	ret = 1;
	switch (br_skey_decoder_key_type(&dc)) {
		const br_rsa_private_key *rk;
		const br_ec_private_key *ek;

	case BR_KEYTYPE_RSA:
		rk = br_skey_decoder_get_rsa(&dc);
		printf("RSA key (%lu bits)\n", (unsigned long)rk->n_bitlen);
		ret = print_rsa(rk, os);
		break;

	case BR_KEYTYPE_EC:
		ek = br_skey_decoder_get_ec(&dc);
		printf("EC key (curve = %d: %s)\n",
			ek->curve, ec_curve_name(ek->curve));
		ret = print_ec(ek, os);
		break;

	default:
		fprintf(stderr, "Unknown key type: %d\n",
			br_skey_decoder_key_type(&dc));
		ret = 0;
		break;
	}

	return ret;
}

static void
usage_skey(void)
{
	fprintf(stderr,
"usage: brssl skey [ options ] file...\n");
	fprintf(stderr,
"options:\n");
	fprintf(stderr,
"   -q             suppress verbose messages\n");
	fprintf(stderr,
"   -text          print private key details (human-readable)\n");
	fprintf(stderr,
"   -C             print private key details (C code)\n");
	fprintf(stderr,
"   -rawder file   save private key in 'file' (raw format, DER)\n");
	fprintf(stderr,
"   -rawpem file   save private key in 'file' (raw format, PEM)\n");
	fprintf(stderr,
"   -pk8der file   save private key in 'file' (PKCS#8 format, DER)\n");
	fprintf(stderr,
"   -pk8pem file   save private key in 'file' (PKCS#8 format, PEM)\n");
	fprintf(stderr,
"   -gen spec      generate a new key using the provided key specification\n");
	fprintf(stderr,
"   -list          list known elliptic curve names\n");
	fprintf(stderr,
"Key specification begins with a key type, followed by optional parameters\n");
	fprintf(stderr,
"that depend on the key type, separated by colon characters:\n");
	fprintf(stderr,
"   rsa[:size[:pubexep]]   RSA key (defaults: size = 2048, pubexp = 3)\n");
	fprintf(stderr,
"   ec[:curvename]         EC key (default curve: secp256r1)\n");
}

/* see brssl.h */
int
do_skey(int argc, char *argv[])
{
	int retcode;
	int verbose;
	int i, num_files;
	outspec os;
	unsigned char *buf;
	size_t len;
	pem_object *pos;
	const char *kgen_spec;

	retcode = 0;
	verbose = 1;
	os.print_text = 0;
	os.print_C = 0;
	os.rawder = NULL;
	os.rawpem = NULL;
	os.pk8der = NULL;
	os.pk8pem = NULL;
	num_files = 0;
	buf = NULL;
	pos = NULL;
	kgen_spec = NULL;
	for (i = 0; i < argc; i ++) {
		const char *arg;

		arg = argv[i];
		if (arg[0] != '-') {
			num_files ++;
			continue;
		}
		argv[i] = NULL;
		if (eqstr(arg, "-v") || eqstr(arg, "-verbose")) {
			verbose = 1;
		} else if (eqstr(arg, "-q") || eqstr(arg, "-quiet")) {
			verbose = 0;
		} else if (eqstr(arg, "-text")) {
			os.print_text = 1;
		} else if (eqstr(arg, "-C")) {
			os.print_C = 1;
		} else if (eqstr(arg, "-rawder")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-rawder'\n");
				usage_skey();
				goto skey_exit_error;
			}
			if (os.rawder != NULL) {
				fprintf(stderr,
					"ERROR: multiple '-rawder' options\n");
				usage_skey();
				goto skey_exit_error;
			}
			os.rawder = argv[i];
			argv[i] = NULL;
		} else if (eqstr(arg, "-rawpem")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-rawpem'\n");
				usage_skey();
				goto skey_exit_error;
			}
			if (os.rawpem != NULL) {
				fprintf(stderr,
					"ERROR: multiple '-rawpem' options\n");
				usage_skey();
				goto skey_exit_error;
			}
			os.rawpem = argv[i];
			argv[i] = NULL;
		} else if (eqstr(arg, "-pk8der")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-pk8der'\n");
				usage_skey();
				goto skey_exit_error;
			}
			if (os.pk8der != NULL) {
				fprintf(stderr,
					"ERROR: multiple '-pk8der' options\n");
				usage_skey();
				goto skey_exit_error;
			}
			os.pk8der = argv[i];
			argv[i] = NULL;
		} else if (eqstr(arg, "-pk8pem")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-pk8pem'\n");
				usage_skey();
				goto skey_exit_error;
			}
			if (os.pk8pem != NULL) {
				fprintf(stderr,
					"ERROR: multiple '-pk8pem' options\n");
				usage_skey();
				goto skey_exit_error;
			}
			os.pk8pem = argv[i];
			argv[i] = NULL;
		} else if (eqstr(arg, "-gen")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-gen'\n");
				usage_skey();
				goto skey_exit_error;
			}
			if (kgen_spec != NULL) {
				fprintf(stderr,
					"ERROR: multiple '-gen' options\n");
				usage_skey();
				goto skey_exit_error;
			}
			kgen_spec = argv[i];
			argv[i] = NULL;
		} else if (eqstr(arg, "-list")) {
			list_curves();
			goto skey_exit;
		} else {
			fprintf(stderr, "ERROR: unknown option: '%s'\n", arg);
			usage_skey();
			goto skey_exit_error;
		}
	}
	if (kgen_spec != NULL) {
		unsigned rsa_size;
		uint32_t rsa_pubexp;
		int curve;

		if (num_files != 0) {
			fprintf(stderr,
				"ERROR: key files provided while generating\n");
			usage_skey();
			goto skey_exit_error;
		}

		if (parse_rsa_spec(kgen_spec, &rsa_size, &rsa_pubexp)) {
			if (!keygen_rsa(rsa_size, rsa_pubexp, &os)) {
				goto skey_exit_error;
			}
		} else if (parse_ec_spec(kgen_spec, &curve)) {
			if (!keygen_ec(curve, &os)) {
				goto skey_exit_error;
			}
		} else {
			fprintf(stderr,
				"ERROR: unknown key specification: '%s'\n",
				kgen_spec);
			usage_skey();
			goto skey_exit_error;
		}
	} else if (num_files == 0) {
		fprintf(stderr, "ERROR: no private key provided\n");
		usage_skey();
		goto skey_exit_error;
	}

	for (i = 0; i < argc; i ++) {
		const char *fname;

		fname = argv[i];
		if (fname == NULL) {
			continue;
		}
		buf = read_file(fname, &len);
		if (buf == NULL) {
			goto skey_exit_error;
		}
		if (looks_like_DER(buf, len)) {
			if (verbose) {
				fprintf(stderr, "File '%s': ASN.1/DER object\n",
					fname);
			}
			if (!decode_key(buf, len, &os)) {
				goto skey_exit_error;
			}
		} else {
			size_t u, num;

			if (verbose) {
				fprintf(stderr, "File '%s': decoding as PEM\n",
					fname);
			}
			pos = decode_pem(buf, len, &num);
			if (pos == NULL) {
				goto skey_exit_error;
			}
			for (u = 0; pos[u].name; u ++) {
				const char *name;

				name = pos[u].name;
				if (eqstr(name, "RSA PRIVATE KEY")
					|| eqstr(name, "EC PRIVATE KEY")
					|| eqstr(name, "PRIVATE KEY"))
				{
					if (!decode_key(pos[u].data,
						pos[u].data_len, &os))
					{
						goto skey_exit_error;
					}
				} else {
					if (verbose) {
						fprintf(stderr,
							"(skipping '%s')\n",
							name);
					}
				}
			}
			for (u = 0; pos[u].name; u ++) {
				free_pem_object_contents(&pos[u]);
			}
			xfree(pos);
			pos = NULL;
		}
		xfree(buf);
		buf = NULL;
	}

	/*
	 * Release allocated structures.
	 */
skey_exit:
	xfree(buf);
	if (pos != NULL) {
		size_t u;

		for (u = 0; pos[u].name; u ++) {
			free_pem_object_contents(&pos[u]);
		}
		xfree(pos);
	}
	return retcode;

skey_exit_error:
	retcode = -1;
	goto skey_exit;
}
