/*
 * Copyright (c) 2001 Kungliga Tekniska Högskolan
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
 * 3. Neither the name of KTH nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY KTH AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KTH OR ITS CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "krb5_locl.h"

static void
print_data(unsigned char *data, size_t len)
{
    int i;
    for(i = 0; i < len; i++) {
	if(i > 0 && (i % 16) == 0)
	    printf("\n            ");
	printf("%02x ", data[i]);
    }
    printf("\n");
}

static int
compare(const char *name, krb5_storage *sp, void *expected, size_t len)
{
    int ret = 0;
    krb5_data data;
    if (krb5_storage_to_data(sp, &data))
	errx(1, "krb5_storage_to_data failed");
    krb5_storage_free(sp);
    if(data.length != len || memcmp(data.data, expected, len) != 0) {
	printf("%s mismatch\n", name);
	printf("  Expected: ");
	print_data(expected, len);
	printf("  Actual:   ");
	print_data(data.data, data.length);
	ret++;
    }
    krb5_data_free(&data);
    return ret;
}

int
main(int argc, char **argv)
{
    int nerr = 0;
    krb5_storage *sp;
    krb5_context context;
    krb5_principal principal;


    krb5_init_context(&context);

    sp = krb5_storage_emem();
    krb5_store_int32(sp, 0x01020304);
    nerr += compare("Integer", sp, "\x1\x2\x3\x4", 4);

    sp = krb5_storage_emem();
    krb5_storage_set_byteorder(sp, KRB5_STORAGE_BYTEORDER_LE);
    krb5_store_int32(sp, 0x01020304);
    nerr += compare("Integer (LE)", sp, "\x4\x3\x2\x1", 4);

    sp = krb5_storage_emem();
    krb5_storage_set_byteorder(sp, KRB5_STORAGE_BYTEORDER_BE);
    krb5_store_int32(sp, 0x01020304);
    nerr += compare("Integer (BE)", sp, "\x1\x2\x3\x4", 4);

    sp = krb5_storage_emem();
    krb5_storage_set_byteorder(sp, KRB5_STORAGE_BYTEORDER_HOST);
    krb5_store_int32(sp, 0x01020304);
    {
	int test = 1;
	void *data;
	if(*(char*)&test)
	    data = "\x4\x3\x2\x1";
	else
	    data = "\x1\x2\x3\x4";
	nerr += compare("Integer (host)", sp, data, 4);
    }

    sp = krb5_storage_emem();
    krb5_make_principal(context, &principal, "TEST", "foobar", NULL);
    krb5_store_principal(sp, principal);
    krb5_free_principal(context, principal);
    nerr += compare("Principal", sp, "\x0\x0\x0\x1"
		    "\x0\x0\x0\x1"
		    "\x0\x0\x0\x4TEST"
		    "\x0\x0\x0\x6""foobar", 26);

    krb5_free_context(context);

    return nerr ? 1 : 0;
}
