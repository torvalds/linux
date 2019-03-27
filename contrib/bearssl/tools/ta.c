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

static const char *
curve_to_sym(int curve)
{
	switch (curve) {
	case BR_EC_sect163k1:         return "BR_EC_sect163k1";
	case BR_EC_sect163r1:         return "BR_EC_sect163r1";
	case BR_EC_sect163r2:         return "BR_EC_sect163r2";
	case BR_EC_sect193r1:         return "BR_EC_sect193r1";
	case BR_EC_sect193r2:         return "BR_EC_sect193r2";
	case BR_EC_sect233k1:         return "BR_EC_sect233k1";
	case BR_EC_sect233r1:         return "BR_EC_sect233r1";
	case BR_EC_sect239k1:         return "BR_EC_sect239k1";
	case BR_EC_sect283k1:         return "BR_EC_sect283k1";
	case BR_EC_sect283r1:         return "BR_EC_sect283r1";
	case BR_EC_sect409k1:         return "BR_EC_sect409k1";
	case BR_EC_sect409r1:         return "BR_EC_sect409r1";
	case BR_EC_sect571k1:         return "BR_EC_sect571k1";
	case BR_EC_sect571r1:         return "BR_EC_sect571r1";
	case BR_EC_secp160k1:         return "BR_EC_secp160k1";
	case BR_EC_secp160r1:         return "BR_EC_secp160r1";
	case BR_EC_secp160r2:         return "BR_EC_secp160r2";
	case BR_EC_secp192k1:         return "BR_EC_secp192k1";
	case BR_EC_secp192r1:         return "BR_EC_secp192r1";
	case BR_EC_secp224k1:         return "BR_EC_secp224k1";
	case BR_EC_secp224r1:         return "BR_EC_secp224r1";
	case BR_EC_secp256k1:         return "BR_EC_secp256k1";
	case BR_EC_secp256r1:         return "BR_EC_secp256r1";
	case BR_EC_secp384r1:         return "BR_EC_secp384r1";
	case BR_EC_secp521r1:         return "BR_EC_secp521r1";
	case BR_EC_brainpoolP256r1:   return "BR_EC_brainpoolP256r1";
	case BR_EC_brainpoolP384r1:   return "BR_EC_brainpoolP384r1";
	case BR_EC_brainpoolP512r1:   return "BR_EC_brainpoolP512r1";
	}
	return NULL;
}

static void
print_blob(const char *name, const unsigned char *buf, size_t len)
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
print_ta_internals(br_x509_trust_anchor *ta, long ctr)
{
	char tmp[25];

	sprintf(tmp, "TA%ld_DN", ctr);
	print_blob(tmp, ta->dn.data, ta->dn.len);
	switch (ta->pkey.key_type) {
	case BR_KEYTYPE_RSA:
		sprintf(tmp, "TA%ld_RSA_N", ctr);
		print_blob(tmp, ta->pkey.key.rsa.n, ta->pkey.key.rsa.nlen);
		sprintf(tmp, "TA%ld_RSA_E", ctr);
		print_blob(tmp, ta->pkey.key.rsa.e, ta->pkey.key.rsa.elen);
		break;
	case BR_KEYTYPE_EC:
		sprintf(tmp, "TA%ld_EC_Q", ctr);
		print_blob(tmp, ta->pkey.key.ec.q, ta->pkey.key.ec.qlen);
		break;
	default:
		fprintf(stderr, "ERROR: unknown anchor key type '%d'\n",
			ta->pkey.key_type);
		return -1;
	}
	return 0;
}

static void
print_ta(br_x509_trust_anchor *ta, long ctr)
{
	char tmp[25];

	printf("\t{\n");
	printf("\t\t{ (unsigned char *)TA%ld_DN, sizeof TA%ld_DN },\n",
		ctr, ctr);
	printf("\t\t%s,\n", (ta->flags & BR_X509_TA_CA)
		? "BR_X509_TA_CA" : "0");
	printf("\t\t{\n");
	switch (ta->pkey.key_type) {
		const char *cname;

	case BR_KEYTYPE_RSA:
		printf("\t\t\tBR_KEYTYPE_RSA,\n");
		printf("\t\t\t{ .rsa = {\n");
		printf("\t\t\t\t(unsigned char *)TA%ld_RSA_N,"
			" sizeof TA%ld_RSA_N,\n", ctr, ctr);
		printf("\t\t\t\t(unsigned char *)TA%ld_RSA_E,"
			" sizeof TA%ld_RSA_E,\n", ctr, ctr);
		printf("\t\t\t} }\n");
		break;
	case BR_KEYTYPE_EC:
		printf("\t\t\tBR_KEYTYPE_EC,\n");
		printf("\t\t\t{ .ec = {\n");
		cname = curve_to_sym(ta->pkey.key.ec.curve);
		if (cname == NULL) {
			sprintf(tmp, "%d", ta->pkey.key.ec.curve);
			cname = tmp;
		}
		printf("\t\t\t\t%s,\n", cname);
		printf("\t\t\t\t(unsigned char *)TA%ld_EC_Q,"
			" sizeof TA%ld_EC_Q,\n", ctr, ctr);
		printf("\t\t\t} }\n");
	}
	printf("\t\t}\n");
	printf("\t}");
}

static void
usage_ta(void)
{
	fprintf(stderr,
"usage: brssl ta [ options ] file...\n");
	fprintf(stderr,
"options:\n");
	fprintf(stderr,
"   -q            suppress verbose messages\n");
}

/* see brssl.h */
int
do_ta(int argc, char *argv[])
{
	int retcode;
	int verbose;
	int i, num_files;
	anchor_list tas = VEC_INIT;
	size_t u, num;

	retcode = 0;
	verbose = 1;
	num_files = 0;
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
		} else {
			fprintf(stderr, "ERROR: unknown option: '%s'\n", arg);
			usage_ta();
			goto ta_exit_error;
		}
	}
	if (num_files == 0) {
		fprintf(stderr, "ERROR: no certificate file provided\n");
		usage_ta();
		goto ta_exit_error;
	}

	for (i = 0; i < argc; i ++) {
		const char *fname;
		size_t len1, len2;

		fname = argv[i];
		if (fname == NULL) {
			continue;
		}
		if (verbose) {
			fprintf(stderr, "Reading file '%s': ", fname);
			fflush(stderr);
		}
		len1 = VEC_LEN(tas);
		if (read_trust_anchors(&tas, fname) == 0) {
			goto ta_exit_error;
		}
		len2 = VEC_LEN(tas) - len1;
		if (verbose) {
			fprintf(stderr, "%lu trust anchor%s\n",
				(unsigned long)len2, len2 > 1 ? "s" : "");
		}
	}
	num = VEC_LEN(tas);
	for (u = 0; u < num; u ++) {
		if (print_ta_internals(&VEC_ELT(tas, u), u) < 0) {
			goto ta_exit_error;
		}
	}
	printf("\nstatic const br_x509_trust_anchor TAs[%ld] = {", (long)num);
	for (u = 0; u < num; u ++) {
		if (u != 0) {
			printf(",");
		}
		printf("\n");
		print_ta(&VEC_ELT(tas, u), u);
	}
	printf("\n};\n");
	printf("\n#define TAs_NUM   %ld\n", (long)num);

	/*
	 * Release allocated structures.
	 */
ta_exit:
	VEC_CLEAREXT(tas, free_ta_contents);
	return retcode;

ta_exit_error:
	retcode = -1;
	goto ta_exit;
}
