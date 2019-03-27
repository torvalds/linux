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

static unsigned
rsa_bit_length(const br_rsa_public_key *pk)
{
	size_t u;
	unsigned x, bl;

	for (u = 0; u < pk->nlen; u ++) {
		if (pk->n[u] != 0) {
			break;
		}
	}
	if (u == pk->nlen) {
		return 0;
	}
	bl = (unsigned)(pk->nlen - u - 1) << 3;
	x = pk->n[u];
	while (x != 0) {
		bl ++;
		x >>= 1;
	}
	return bl;
}

static void
print_rsa(const br_rsa_public_key *pk, int print_text, int print_C)
{
	if (print_text) {
		size_t u;

		printf("n = ");
		for (u = 0; u < pk->nlen; u ++) {
			printf("%02X", pk->n[u]);
		}
		printf("\n");
		printf("e = ");
		for (u = 0; u < pk->elen; u ++) {
			printf("%02X", pk->e[u]);
		}
		printf("\n");
	}
	if (print_C) {
		size_t u;

		printf("\nstatic const unsigned char RSA_N[] = {");
		for (u = 0; u < pk->nlen; u ++) {
			if (u != 0) {
				printf(",");
			}
			if (u % 12 == 0) {
				printf("\n\t");
			} else {
				printf(" ");
			}
			printf("0x%02X", pk->n[u]);
		}
		printf("\n};\n");
		printf("\nstatic const unsigned char RSA_E[] = {");
		for (u = 0; u < pk->elen; u ++) {
			if (u != 0) {
				printf(",");
			}
			if (u % 12 == 0) {
				printf("\n\t");
			} else {
				printf(" ");
			}
			printf("0x%02X", pk->e[u]);
		}
		printf("\n};\n");
		printf("\nstatic const br_rsa_public_key RSA = {\n");
		printf("\t(unsigned char *)RSA_N, sizeof RSA_N,\n");
		printf("\t(unsigned char *)RSA_E, sizeof RSA_E\n");
		printf("};\n");
	}
}

static void
print_ec(const br_ec_public_key *pk, int print_text, int print_C)
{
	if (print_text) {
		size_t u;

		printf("Q = ");
		for (u = 0; u < pk->qlen; u ++) {
			printf("%02X", pk->q[u]);
		}
		printf("\n");
	}
	if (print_C) {
		size_t u;

		printf("\nstatic const unsigned char EC_Q[] = {");
		for (u = 0; u < pk->qlen; u ++) {
			if (u != 0) {
				printf(",");
			}
			if (u % 12 == 0) {
				printf("\n\t");
			} else {
				printf(" ");
			}
			printf("0x%02X", pk->q[u]);
		}
		printf("\n};\n");
		printf("\nstatic const br_ec_public_key EC = {\n");
		printf("\t%d,\n", pk->curve);
		printf("\t(unsigned char *)EC_Q, sizeof EC_Q\n");
		printf("};\n");
	}
}

static void
usage_verify(void)
{
	fprintf(stderr,
"usage: brssl verify [ options ] file...\n");
	fprintf(stderr,
"options:\n");
	fprintf(stderr,
"   -q            suppress verbose messages\n");
	fprintf(stderr,
"   -sni name     check presence of a specific server name\n");
	fprintf(stderr,
"   -CA file      add certificates in 'file' to trust anchors\n");
	fprintf(stderr,
"   -text         print public key details (human-readable)\n");
	fprintf(stderr,
"   -C            print public key details (C code)\n");
}

typedef VECTOR(br_x509_certificate) cert_list;

static void
free_cert_contents(br_x509_certificate *xc)
{
	xfree(xc->data);
}

/* see brssl.h */
int
do_verify(int argc, char *argv[])
{
	int retcode;
	int verbose;
	int i;
	const char *sni;
	anchor_list anchors = VEC_INIT;
	cert_list chain = VEC_INIT;
	size_t u;
	br_x509_minimal_context mc;
	int err;
	int print_text, print_C;
	br_x509_pkey *pk;
	const br_x509_pkey *tpk;
	unsigned usages;

	retcode = 0;
	verbose = 1;
	sni = NULL;
	print_text = 0;
	print_C = 0;
	pk = NULL;
	for (i = 0; i < argc; i ++) {
		const char *arg;

		arg = argv[i];
		if (arg[0] != '-') {
			br_x509_certificate *xcs;
			size_t num;

			xcs = read_certificates(arg, &num);
			if (xcs == NULL) {
				usage_verify();
				goto verify_exit_error;
			}
			VEC_ADDMANY(chain, xcs, num);
			xfree(xcs);
			continue;
		}
		if (eqstr(arg, "-v") || eqstr(arg, "-verbose")) {
			verbose = 1;
		} else if (eqstr(arg, "-q") || eqstr(arg, "-quiet")) {
			verbose = 0;
		} else if (eqstr(arg, "-sni")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-sni'\n");
				usage_verify();
				goto verify_exit_error;
			}
			if (sni != NULL) {
				fprintf(stderr, "ERROR: duplicate SNI\n");
				usage_verify();
				goto verify_exit_error;
			}
			sni = argv[i];
			continue;
		} else if (eqstr(arg, "-CA")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-CA'\n");
				usage_verify();
				goto verify_exit_error;
			}
			arg = argv[i];
			if (read_trust_anchors(&anchors, arg) == 0) {
				usage_verify();
				goto verify_exit_error;
			}
			continue;
		} else if (eqstr(arg, "-text")) {
			print_text = 1;
		} else if (eqstr(arg, "-C")) {
			print_C = 1;
		} else {
			fprintf(stderr, "ERROR: unknown option: '%s'\n", arg);
			usage_verify();
			goto verify_exit_error;
		}
	}
	if (VEC_LEN(chain) == 0) {
		fprintf(stderr, "ERROR: no certificate chain provided\n");
		usage_verify();
		goto verify_exit_error;
	}
	br_x509_minimal_init(&mc, &br_sha256_vtable,
		&VEC_ELT(anchors, 0), VEC_LEN(anchors));
	br_x509_minimal_set_hash(&mc, br_sha1_ID, &br_sha1_vtable);
	br_x509_minimal_set_hash(&mc, br_sha224_ID, &br_sha224_vtable);
	br_x509_minimal_set_hash(&mc, br_sha256_ID, &br_sha256_vtable);
	br_x509_minimal_set_hash(&mc, br_sha384_ID, &br_sha384_vtable);
	br_x509_minimal_set_hash(&mc, br_sha512_ID, &br_sha512_vtable);
	br_x509_minimal_set_rsa(&mc, &br_rsa_i31_pkcs1_vrfy);
	br_x509_minimal_set_ecdsa(&mc,
		&br_ec_prime_i31, &br_ecdsa_i31_vrfy_asn1);

	mc.vtable->start_chain(&mc.vtable, sni);
	for (u = 0; u < VEC_LEN(chain); u ++) {
		br_x509_certificate *xc;

		xc = &VEC_ELT(chain, u);
		mc.vtable->start_cert(&mc.vtable, xc->data_len);
		mc.vtable->append(&mc.vtable, xc->data, xc->data_len);
		mc.vtable->end_cert(&mc.vtable);
	}
	err = mc.vtable->end_chain(&mc.vtable);
	tpk = mc.vtable->get_pkey(&mc.vtable, &usages);
	if (tpk != NULL) {
		pk = xpkeydup(tpk);
	}

	if (err == 0) {
		if (verbose) {
			int hkx;

			fprintf(stderr, "Validation success; usages:");
			hkx = 0;
			if (usages & BR_KEYTYPE_KEYX) {
				fprintf(stderr, " key exchange");
				hkx = 1;
			}
			if (usages & BR_KEYTYPE_SIGN) {
				if (hkx) {
					fprintf(stderr, ",");
				}
				fprintf(stderr, " signature");
			}
			fprintf(stderr, "\n");
		}
	} else {
		if (verbose) {
			const char *errname, *errmsg;

			fprintf(stderr, "Validation failed, err = %d", err);
			errname = find_error_name(err, &errmsg);
			if (errname != NULL) {
				fprintf(stderr, " (%s): %s\n", errname, errmsg);
			} else {
				fprintf(stderr, " (unknown)\n");
			}
		}
		retcode = -1;
	}
	if (pk != NULL) {
		switch (pk->key_type) {
		case BR_KEYTYPE_RSA:
			if (verbose) {
				fprintf(stderr, "Key type: RSA (%u bits)\n",
					rsa_bit_length(&pk->key.rsa));
			}
			print_rsa(&pk->key.rsa, print_text, print_C);
			break;
		case BR_KEYTYPE_EC:
			if (verbose) {
				fprintf(stderr, "Key type: EC (%s)\n",
					ec_curve_name(pk->key.ec.curve));
			}
			print_ec(&pk->key.ec, print_text, print_C);
			break;
		default:
			if (verbose) {
				fprintf(stderr, "Unknown key type\n");
				break;
			}
		}
	}

	/*
	 * Release allocated structures.
	 */
verify_exit:
	VEC_CLEAREXT(anchors, &free_ta_contents);
	VEC_CLEAREXT(chain, &free_cert_contents);
	xfreepkey(pk);
	return retcode;

verify_exit_error:
	retcode = -1;
	goto verify_exit;
}
