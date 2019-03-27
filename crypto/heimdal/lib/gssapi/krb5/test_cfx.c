/*
 * Copyright (c) 2006 Kungliga Tekniska Högskolan
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

#include "gsskrb5_locl.h"

struct range {
    size_t lower;
    size_t upper;
};

struct range tests[] = {
    { 0, 1040 },
    { 2040, 2080 },
    { 4080, 5000 },
    { 8180, 8292 },
    { 9980, 10010 }
};

static void
test_range(const struct range *r, int integ,
	   krb5_context context, krb5_crypto crypto)
{
    krb5_error_code ret;
    size_t size, rsize;
    struct gsskrb5_ctx ctx;

    for (size = r->lower; size < r->upper; size++) {
	size_t cksumsize;
	uint16_t padsize;
	OM_uint32 minor;
	OM_uint32 max_wrap_size;

	ctx.crypto = crypto;

	ret = _gssapi_wrap_size_cfx(&minor,
				    &ctx,
				    context,
				    integ,
				    0,
				    size,
				    &max_wrap_size);
	if (ret)
	    krb5_errx(context, 1, "_gsskrb5cfx_max_wrap_length_cfx: %d", ret);
	if (max_wrap_size == 0)
	    continue;

	ret = _gsskrb5cfx_wrap_length_cfx(context,
					  crypto,
					  integ,
					  0,
					  max_wrap_size,
					  &rsize, &cksumsize, &padsize);
	if (ret)
	    krb5_errx(context, 1, "_gsskrb5cfx_wrap_length_cfx: %d", ret);

	if (size < rsize)
	    krb5_errx(context, 1,
		      "size (%d) < rsize (%d) for max_wrap_size %d",
		      (int)size, (int)rsize, (int)max_wrap_size);
    }
}

static void
test_special(krb5_context context, krb5_crypto crypto,
	     int integ, size_t testsize)
{
    krb5_error_code ret;
    size_t rsize;
    OM_uint32 max_wrap_size;
    size_t cksumsize;
    uint16_t padsize;
    struct gsskrb5_ctx ctx;
    OM_uint32 minor;

    ctx.crypto = crypto;

    ret = _gssapi_wrap_size_cfx(&minor,
				&ctx,
				context,
				integ,
				0,
				testsize,
				&max_wrap_size);
    if (ret)
      krb5_errx(context, 1, "_gsskrb5cfx_max_wrap_length_cfx: %d", ret);
    if (ret)
	krb5_errx(context, 1, "_gsskrb5cfx_max_wrap_length_cfx: %d", ret);

    ret = _gsskrb5cfx_wrap_length_cfx(context,
				      crypto,
				      integ,
				      0,
				      max_wrap_size,
				      &rsize, &cksumsize, &padsize);
    if (ret)
	krb5_errx(context, 1, "_gsskrb5cfx_wrap_length_cfx: %d", ret);

    if (testsize < rsize)
	krb5_errx(context, 1,
		  "testsize (%d) < rsize (%d) for max_wrap_size %d",
		  (int)testsize, (int)rsize, (int)max_wrap_size);
}




int
main(int argc, char **argv)
{
    krb5_keyblock keyblock;
    krb5_error_code ret;
    krb5_context context;
    krb5_crypto crypto;
    int i;

    ret = krb5_init_context(&context);
    if (ret)
	errx(1, "krb5_context_init: %d", ret);

    ret = krb5_generate_random_keyblock(context,
					ENCTYPE_AES256_CTS_HMAC_SHA1_96,
					&keyblock);
    if (ret)
	krb5_err(context, 1, ret, "krb5_generate_random_keyblock");

    ret = krb5_crypto_init(context, &keyblock, 0, &crypto);
    if (ret)
	krb5_err(context, 1, ret, "krb5_crypto_init");

    test_special(context, crypto, 1, 60);
    test_special(context, crypto, 0, 60);

    for (i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
	test_range(&tests[i], 1, context, crypto);
	test_range(&tests[i], 0, context, crypto);
    }

    krb5_free_keyblock_contents(context, &keyblock);
    krb5_crypto_destroy(context, crypto);
    krb5_free_context(context);

    return 0;
}
