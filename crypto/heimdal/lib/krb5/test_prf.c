/*
 * Copyright (c) 2007 Kungliga Tekniska Högskolan
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
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "krb5_locl.h"

#include <hex.h>
#include <err.h>

/*
 * key: string2key(aes256, "testkey", "testkey", default_params)
 * input: unhex(1122334455667788)
 * output: 58b594b8a61df6e9439b7baa991ff5c1
 *
 * key: string2key(aes128, "testkey", "testkey", default_params)
 * input: unhex(1122334455667788)
 * output: ffa2f823aa7f83a8ce3c5fb730587129
 */

int
main(int argc, char **argv)
{
    krb5_context context;
    krb5_error_code ret;
    krb5_keyblock key;
    krb5_crypto crypto;
    size_t length;
    krb5_data input, output, output2;
    krb5_enctype etype = ETYPE_AES256_CTS_HMAC_SHA1_96;

    ret = krb5_init_context(&context);
    if (ret)
	errx(1, "krb5_init_context %d", ret);

    ret = krb5_generate_random_keyblock(context, etype, &key);
    if (ret)
	krb5_err(context, 1, ret, "krb5_generate_random_keyblock");

    ret = krb5_crypto_prf_length(context, etype, &length);
    if (ret)
	krb5_err(context, 1, ret, "krb5_crypto_prf_length");

    ret = krb5_crypto_init(context, &key, 0, &crypto);
    if (ret)
	krb5_err(context, 1, ret, "krb5_crypto_init");

    input.data = rk_UNCONST("foo");
    input.length = 3;

    ret = krb5_crypto_prf(context, crypto, &input, &output);
    if (ret)
	krb5_err(context, 1, ret, "krb5_crypto_prf");

    ret = krb5_crypto_prf(context, crypto, &input, &output2);
    if (ret)
	krb5_err(context, 1, ret, "krb5_crypto_prf");

    if (krb5_data_cmp(&output, &output2) != 0)
	krb5_errx(context, 1, "krb5_data_cmp");

    krb5_data_free(&output);
    krb5_data_free(&output2);

    krb5_crypto_destroy(context, crypto);

    krb5_free_keyblock_contents(context, &key);

    krb5_free_context(context);

    return 0;
}
