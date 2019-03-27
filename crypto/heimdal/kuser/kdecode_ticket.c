/*
 * Copyright (c) 1997 - 2004 Kungliga Tekniska Högskolan
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

#include "kuser_locl.h"

static char *etype_str;
static int version_flag;
static int help_flag;

static void
print_and_decode_tkt (krb5_context context,
		      krb5_data *ticket,
		      krb5_principal server,
		      krb5_enctype enctype)
{
    krb5_error_code ret;
    krb5_crypto crypto;
    krb5_data dec_data;
    size_t len;
    EncTicketPart decr_part;
    krb5_keyblock key;
    Ticket tkt;

    ret = decode_Ticket (ticket->data, ticket->length, &tkt, &len);
    if (ret)
	krb5_err (context, 1, ret, "decode_Ticket");

    ret = krb5_string_to_key (context, enctype, "foo", server, &key);
    if (ret)
	krb5_err (context, 1, ret, "krb5_string_to_key");

    ret = krb5_crypto_init(context, &key, 0, &crypto);
    if (ret)
	krb5_err (context, 1, ret, "krb5_crypto_init");

    ret = krb5_decrypt_EncryptedData (context, crypto, KRB5_KU_TICKET,
				      &tkt.enc_part, &dec_data);
    krb5_crypto_destroy (context, crypto);
    if (ret)
	krb5_err (context, 1, ret, "krb5_decrypt_EncryptedData");
    ret = decode_EncTicketPart (dec_data.data, dec_data.length,
				&decr_part, &len);
    krb5_data_free (&dec_data);
    if (ret)
	krb5_err (context, 1, ret, "krb5_decode_EncTicketPart");
    free_EncTicketPart(&decr_part);
}

struct getargs args[] = {
    { "enctype",	'e', arg_string, &etype_str,
      "encryption type to use", "enctype"},
    { "version", 	0,   arg_flag, &version_flag },
    { "help",		0,   arg_flag, &help_flag }
};

static void
usage (int ret)
{
    arg_printusage (args,
		    sizeof(args)/sizeof(*args),
		    NULL,
		    "service");
    exit (ret);
}

int
main(int argc, char **argv)
{
    krb5_error_code ret;
    krb5_context context;
    krb5_ccache cache;
    krb5_creds in, *out;
    int optidx = 0;

    setprogname (argv[0]);

    ret = krb5_init_context (&context);
    if (ret)
	errx(1, "krb5_init_context failed: %d", ret);

    if(getarg(args, sizeof(args) / sizeof(args[0]), argc, argv, &optidx))
	usage(1);

    if (help_flag)
	usage (0);

    if(version_flag) {
	print_version(NULL);
	exit(0);
    }

    argc -= optidx;
    argv += optidx;

    if (argc != 1)
	usage (1);

    ret = krb5_cc_default(context, &cache);
    if (ret)
	krb5_err (context, 1, ret, "krb5_cc_default");

    memset(&in, 0, sizeof(in));

    if (etype_str) {
	krb5_enctype enctype;

	ret = krb5_string_to_enctype(context, etype_str, &enctype);
	if (ret)
	    krb5_errx (context, 1, "unrecognized enctype: %s", etype_str);
	in.session.keytype = enctype;
    }

    ret = krb5_cc_get_principal(context, cache, &in.client);
    if (ret)
	krb5_err (context, 1, ret, "krb5_cc_get_principal");

    ret = krb5_parse_name(context, argv[0], &in.server);
    if (ret)
	krb5_err (context, 1, ret, "krb5_parse_name %s", argv[0]);

    in.times.endtime = 0;
    ret = krb5_get_credentials(context, 0, cache, &in, &out);
    if (ret)
	krb5_err (context, 1, ret, "krb5_get_credentials");

    print_and_decode_tkt (context, &out->ticket, out->server,
			  out->session.keytype);

    krb5_free_cred_contents(context, out);
    return 0;
}
