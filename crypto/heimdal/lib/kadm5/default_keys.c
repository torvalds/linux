/*
 * Copyright (c) 2003 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "kadm5_locl.h"
#include <err.h>

RCSID("$Id$");

static void
print_keys(krb5_context context, Key *keys, size_t nkeys)
{
    krb5_error_code ret;
    char *str;
    int i;

    printf("keys:\n");

    for (i = 0; i < nkeys; i++) {

	ret = krb5_enctype_to_string(context, keys[i].key.keytype, &str);
	if (ret)
	    krb5_err(context, ret, 1, "krb5_enctype_to_string: %d\n",
		     (int)keys[i].key.keytype);

	printf("\tenctype %s", str);
	free(str);

	if (keys[i].salt) {
	    printf(" salt: ");

	    switch (keys[i].salt->type) {
	    case KRB5_PW_SALT:
		printf("pw-salt:");
		break;
	    case KRB5_AFS3_SALT:
		printf("afs3-salt:");
		break;
	    default:
		printf("unknown salt: %d", keys[i].salt->type);
		break;
	    }
	    if (keys[i].salt->salt.length)
		printf("%.*s", (int)keys[i].salt->salt.length,
		       (char *)keys[i].salt->salt.data);
	}
	printf("\n");
    }
    printf("end keys:\n");
}

static void
parse_file(krb5_context context, krb5_principal principal, int no_salt)
{
    krb5_error_code ret;
    size_t nkeys;
    Key *keys;

    ret = hdb_generate_key_set(context, principal, &keys, &nkeys, no_salt);
    if (ret)
	krb5_err(context, 1, ret, "hdb_generate_key_set");

    print_keys(context, keys, nkeys);

    hdb_free_keys(context, nkeys, keys);
}

int
main(int argc, char **argv)
{
    krb5_error_code ret;
    krb5_context context;
    krb5_principal principal;

    ret = krb5_init_context(&context);
    if (ret)
	errx(1, "krb5_init_context");

    ret = krb5_parse_name(context, "lha@SU.SE", &principal);
    if (ret)
	krb5_err(context, ret, 1, "krb5_parse_name");

    parse_file(context, principal, 0);
    parse_file(context, principal, 1);

    krb5_free_principal(context, principal);

    krb5_free_context(context);

    return 0;
}
