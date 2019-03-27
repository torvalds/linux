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
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "krb5_locl.h"
#include <getarg.h>

static void
test_int8(krb5_context context, krb5_storage *sp)
{
    krb5_error_code ret;
    int i;
    int8_t val[] = {
	0, 1, -1, 128, -127
    }, v;

    krb5_storage_truncate(sp, 0);

    for (i = 0; i < sizeof(val[0])/sizeof(val); i++) {

	ret = krb5_store_int8(sp, val[i]);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_store_int8");
	krb5_storage_seek(sp, 0, SEEK_SET);
	ret = krb5_ret_int8(sp, &v);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_ret_int8");
	if (v != val[i])
	    krb5_errx(context, 1, "store and ret mismatch");
    }
}

static void
test_int16(krb5_context context, krb5_storage *sp)
{
    krb5_error_code ret;
    int i;
    int16_t val[] = {
	0, 1, -1, 32768, -32767
    }, v;

    krb5_storage_truncate(sp, 0);

    for (i = 0; i < sizeof(val[0])/sizeof(val); i++) {

	ret = krb5_store_int16(sp, val[i]);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_store_int16");
	krb5_storage_seek(sp, 0, SEEK_SET);
	ret = krb5_ret_int16(sp, &v);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_ret_int16");
	if (v != val[i])
	    krb5_errx(context, 1, "store and ret mismatch");
    }
}

static void
test_int32(krb5_context context, krb5_storage *sp)
{
    krb5_error_code ret;
    int i;
    int32_t val[] = {
	0, 1, -1, 2147483647, -2147483646
    }, v;

    krb5_storage_truncate(sp, 0);

    for (i = 0; i < sizeof(val[0])/sizeof(val); i++) {

	ret = krb5_store_int32(sp, val[i]);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_store_int32");
	krb5_storage_seek(sp, 0, SEEK_SET);
	ret = krb5_ret_int32(sp, &v);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_ret_int32");
	if (v != val[i])
	    krb5_errx(context, 1, "store and ret mismatch");
    }
}

static void
test_uint8(krb5_context context, krb5_storage *sp)
{
    krb5_error_code ret;
    int i;
    uint8_t val[] = {
	0, 1, 255
    }, v;

    krb5_storage_truncate(sp, 0);

    for (i = 0; i < sizeof(val[0])/sizeof(val); i++) {

	ret = krb5_store_uint8(sp, val[i]);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_store_uint8");
	krb5_storage_seek(sp, 0, SEEK_SET);
	ret = krb5_ret_uint8(sp, &v);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_ret_uint8");
	if (v != val[i])
	    krb5_errx(context, 1, "store and ret mismatch");
    }
}

static void
test_uint16(krb5_context context, krb5_storage *sp)
{
    krb5_error_code ret;
    int i;
    uint16_t val[] = {
	0, 1, 65535
    }, v;

    krb5_storage_truncate(sp, 0);

    for (i = 0; i < sizeof(val[0])/sizeof(val); i++) {

	ret = krb5_store_uint16(sp, val[i]);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_store_uint16");
	krb5_storage_seek(sp, 0, SEEK_SET);
	ret = krb5_ret_uint16(sp, &v);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_ret_uint16");
	if (v != val[i])
	    krb5_errx(context, 1, "store and ret mismatch");
    }
}

static void
test_uint32(krb5_context context, krb5_storage *sp)
{
    krb5_error_code ret;
    int i;
    uint32_t val[] = {
	0, 1, 4294967295UL
    }, v;

    krb5_storage_truncate(sp, 0);

    for (i = 0; i < sizeof(val[0])/sizeof(val); i++) {

	ret = krb5_store_uint32(sp, val[i]);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_store_uint32");
	krb5_storage_seek(sp, 0, SEEK_SET);
	ret = krb5_ret_uint32(sp, &v);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_ret_uint32");
	if (v != val[i])
	    krb5_errx(context, 1, "store and ret mismatch");
    }
}


static void
test_storage(krb5_context context, krb5_storage *sp)
{
    test_int8(context, sp);
    test_int16(context, sp);
    test_int32(context, sp);
    test_uint8(context, sp);
    test_uint16(context, sp);
    test_uint32(context, sp);
}


static void
test_truncate(krb5_context context, krb5_storage *sp, int fd)
{
    struct stat sb;

    krb5_store_string(sp, "hej");
    krb5_storage_truncate(sp, 2);

    if (fstat(fd, &sb) != 0)
	krb5_err(context, 1, errno, "fstat");
    if (sb.st_size != 2)
	krb5_errx(context, 1, "length not 2");

    krb5_storage_truncate(sp, 1024);

    if (fstat(fd, &sb) != 0)
	krb5_err(context, 1, errno, "fstat");
    if (sb.st_size != 1024)
	krb5_errx(context, 1, "length not 2");
}

static void
check_too_large(krb5_context context, krb5_storage *sp)
{
    uint32_t too_big_sizes[] = { INT_MAX, INT_MAX / 2, INT_MAX / 4, INT_MAX / 8 + 1};
    krb5_error_code ret;
    krb5_data data;
    size_t n;

    for (n = 0; n < sizeof(too_big_sizes) / sizeof(too_big_sizes); n++) {
	krb5_storage_truncate(sp, 0);
	krb5_store_uint32(sp, too_big_sizes[n]);
	krb5_storage_seek(sp, 0, SEEK_SET);
	ret = krb5_ret_data(sp, &data);
	if (ret != HEIM_ERR_TOO_BIG)
	    errx(1, "not too big: %lu", (unsigned long)n);
    }
}

/*
 *
 */

static int version_flag = 0;
static int help_flag	= 0;

static struct getargs args[] = {
    {"version",	0,	arg_flag,	&version_flag,
     "print version", NULL },
    {"help",	0,	arg_flag,	&help_flag,
     NULL, NULL }
};

static void
usage (int ret)
{
    arg_printusage (args,
		    sizeof(args)/sizeof(*args),
		    NULL,
		    "");
    exit (ret);
}

int
main(int argc, char **argv)
{
    krb5_context context;
    krb5_error_code ret;
    int fd, optidx = 0;
    krb5_storage *sp;
    const char *fn = "test-store-data";

    setprogname(argv[0]);

    if(getarg(args, sizeof(args) / sizeof(args[0]), argc, argv, &optidx))
	usage(1);

    if (help_flag)
	usage (0);

    if(version_flag){
	print_version(NULL);
	exit(0);
    }

    argc -= optidx;
    argv += optidx;

    ret = krb5_init_context (&context);
    if (ret)
	errx (1, "krb5_init_context failed: %d", ret);

    /*
     * Test encoding/decoding of primotive types on diffrent backends
     */

    sp = krb5_storage_emem();
    if (sp == NULL)
	krb5_errx(context, 1, "krb5_storage_emem: no mem");

    test_storage(context, sp);
    check_too_large(context, sp);
    krb5_storage_free(sp);


    fd = open(fn, O_RDWR|O_CREAT|O_TRUNC, 0600);
    if (fd < 0)
	krb5_err(context, 1, errno, "open(%s)", fn);

    sp = krb5_storage_from_fd(fd);
    close(fd);
    if (sp == NULL)
	krb5_errx(context, 1, "krb5_storage_from_fd: %s no mem", fn);

    test_storage(context, sp);
    krb5_storage_free(sp);
    unlink(fn);

    /*
     * test truncate behavior
     */

    fd = open(fn, O_RDWR|O_CREAT|O_TRUNC, 0600);
    if (fd < 0)
	krb5_err(context, 1, errno, "open(%s)", fn);

    sp = krb5_storage_from_fd(fd);
    if (sp == NULL)
	krb5_errx(context, 1, "krb5_storage_from_fd: %s no mem", fn);

    test_truncate(context, sp, fd);
    krb5_storage_free(sp);
    close(fd);
    unlink(fn);

    krb5_free_context(context);

    return 0;
}
