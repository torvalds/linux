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

#include "kdc_locl.h"

static int version_flag;
static int help_flag;

struct getargs args[] = {
    { "version",   0,	arg_flag, &version_flag },
    { "help",     'h',	arg_flag, &help_flag }
};

const static int num_args = sizeof(args) / sizeof(args[0]);

static void
usage(int ret)
{
    arg_printusage (args, num_args, NULL, "kdc-request-log-file");
    exit (ret);
}

int
main(int argc, char **argv)
{
    krb5_error_code ret;
    krb5_context context;
    krb5_kdc_configuration *config;
    krb5_storage *sp;
    int fd, optidx = 0;

    setprogname(argv[0]);

    if(getarg(args, num_args, argc, argv, &optidx))
	usage(1);

    if(help_flag)
	usage(0);

    if(version_flag){
	print_version(NULL);
	exit(0);
    }

    ret = krb5_init_context(&context);
    if (ret)
	errx (1, "krb5_init_context failed to parse configuration file");

    ret = krb5_kdc_get_config(context, &config);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kdc_default_config");

    kdc_openlog(context, "kdc-replay", config);

    ret = krb5_kdc_set_dbinfo(context, config);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kdc_set_dbinfo");

#ifdef PKINIT
    if (config->enable_pkinit) {
	if (config->pkinit_kdc_identity == NULL)
	    krb5_errx(context, 1, "pkinit enabled but no identity");

	if (config->pkinit_kdc_anchors == NULL)
	    krb5_errx(context, 1, "pkinit enabled but no X509 anchors");

	krb5_kdc_pk_initialize(context, config,
			       config->pkinit_kdc_identity,
			       config->pkinit_kdc_anchors,
			       config->pkinit_kdc_cert_pool,
			       config->pkinit_kdc_revoke);

    }
#endif /* PKINIT */

    if (argc != 2)
	errx(1, "argc != 2");

    printf("kdc replay\n");

    fd = open(argv[1], O_RDONLY);
    if (fd < 0)
	err(1, "open: %s", argv[1]);

    sp = krb5_storage_from_fd(fd);
    if (sp == NULL)
	krb5_errx(context, 1, "krb5_storage_from_fd");

    while(1) {
	struct sockaddr_storage sa;
	krb5_socklen_t salen = sizeof(sa);
	struct timeval tv;
	krb5_address a;
	krb5_data d, r;
	uint32_t t, clty, tag;
	char astr[80];

	ret = krb5_ret_uint32(sp, &t);
	if (ret == HEIM_ERR_EOF)
	    break;
	else if (ret)
	    krb5_errx(context, 1, "krb5_ret_uint32(version)");
	if (t != 1)
	    krb5_errx(context, 1, "version not 1");
	ret = krb5_ret_uint32(sp, &t);
	if (ret)
	    krb5_errx(context, 1, "krb5_ret_uint32(time)");
	ret = krb5_ret_address(sp, &a);
	if (ret)
	    krb5_errx(context, 1, "krb5_ret_address");
	ret = krb5_ret_data(sp, &d);
	if (ret)
	    krb5_errx(context, 1, "krb5_ret_data");
	ret = krb5_ret_uint32(sp, &clty);
	if (ret)
	    krb5_errx(context, 1, "krb5_ret_uint32(class|type)");
	ret = krb5_ret_uint32(sp, &tag);
	if (ret)
	    krb5_errx(context, 1, "krb5_ret_uint32(tag)");


	ret = krb5_addr2sockaddr (context, &a, (struct sockaddr *)&sa,
				  &salen, 88);
	if (ret == KRB5_PROG_ATYPE_NOSUPP)
	    goto out;
	else if (ret)
	    krb5_err(context, 1, ret, "krb5_addr2sockaddr");

	ret = krb5_print_address(&a, astr, sizeof(astr), NULL);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_print_address");

	printf("processing request from %s, %lu bytes\n",
	       astr, (unsigned long)d.length);

	r.length = 0;
	r.data = NULL;

	tv.tv_sec = t;
	tv.tv_usec = 0;

	krb5_kdc_update_time(&tv);
	krb5_set_real_time(context, tv.tv_sec, 0);

	ret = krb5_kdc_process_request(context, config, d.data, d.length,
				       &r, NULL, astr,
				       (struct sockaddr *)&sa, 0);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_kdc_process_request");

	if (r.length) {
	    Der_class cl;
	    Der_type ty;
	    unsigned int tag2;
	    ret = der_get_tag (r.data, r.length,
			       &cl, &ty, &tag2, NULL);
	    if (MAKE_TAG(cl, ty, 0) != clty)
		krb5_errx(context, 1, "class|type mismatch: %d != %d",
			  (int)MAKE_TAG(cl, ty, 0), (int)clty);
	    if (tag != tag2)
		krb5_errx(context, 1, "tag mismatch");

	    krb5_data_free(&r);
	} else {
	    if (clty != 0xffffffff)
		krb5_errx(context, 1, "clty not invalid");
	    if (tag != 0xffffffff)
		krb5_errx(context, 1, "tag not invalid");
	}

    out:
	krb5_data_free(&d);
	krb5_free_address(context, &a);
    }

    krb5_storage_free(sp);
    krb5_free_context(context);

    printf("done\n");

    return 0;
}
