/*
 * Copyright (c) 2006 - 2007 Kungliga Tekniska Högskolan
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

#define HC_DEPRECATED_CRYPTO

#include "kuser_locl.h"

#include <kdigest-commands.h>
#include <hex.h>
#include <base64.h>
#include <heimntlm.h>
#include "crypto-headers.h"

static int version_flag = 0;
static int help_flag	= 0;
static char *ccache_string;
static krb5_ccache id;

static struct getargs args[] = {
    {"ccache",	0,	arg_string,	&ccache_string, "credential cache", NULL },
    {"version",	0,	arg_flag,	&version_flag, "print version", NULL },
    {"help",	0,	arg_flag,	&help_flag,  NULL, NULL }
};

static void
usage (int ret)
{
    arg_printusage (args, sizeof(args)/sizeof(*args),
		    NULL, "");
    exit (ret);
}

static krb5_context context;

int
digest_probe(struct digest_probe_options *opt,
	     int argc, char ** argv)
{
    krb5_error_code ret;
    krb5_realm realm;
    unsigned flags;

    realm = opt->realm_string;

    if (realm == NULL)
	errx(1, "realm missing");

    ret = krb5_digest_probe(context, realm, id, &flags);
    if (ret)
	krb5_err(context, 1, ret, "digest_probe");

    printf("flags: %u\n", flags);

    return 0;
}

int
digest_server_init(struct digest_server_init_options *opt,
		   int argc, char ** argv)
{
    krb5_error_code ret;
    krb5_digest digest;

    ret = krb5_digest_alloc(context, &digest);
    if (ret)
	krb5_err(context, 1, ret, "digest_alloc");

    ret = krb5_digest_set_type(context, digest, opt->type_string);
    if (ret)
	krb5_err(context, 1, ret, "krb5_digest_set_type");

    if (opt->cb_type_string && opt->cb_value_string) {
	ret = krb5_digest_set_server_cb(context, digest,
					opt->cb_type_string,
					opt->cb_value_string);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_digest_set_server_cb");
    }
    ret = krb5_digest_init_request(context,
				   digest,
				   opt->kerberos_realm_string,
				   id);
    if (ret)
	krb5_err(context, 1, ret, "krb5_digest_init_request");

    printf("type=%s\n", opt->type_string);
    printf("server-nonce=%s\n",
	   krb5_digest_get_server_nonce(context, digest));
    {
	const char *s = krb5_digest_get_identifier(context, digest);
	if (s)
	    printf("identifier=%s\n", s);
    }
    printf("opaque=%s\n", krb5_digest_get_opaque(context, digest));

    krb5_digest_free(digest);

    return 0;
}

int
digest_server_request(struct digest_server_request_options *opt,
		      int argc, char **argv)
{
    krb5_error_code ret;
    krb5_digest digest;
    const char *status, *rsp;
    krb5_data session_key;

    if (opt->server_nonce_string == NULL)
	errx(1, "server nonce missing");
    if (opt->type_string == NULL)
	errx(1, "type missing");
    if (opt->opaque_string == NULL)
	errx(1, "opaque missing");
    if (opt->client_response_string == NULL)
	errx(1, "client response missing");

    ret = krb5_digest_alloc(context, &digest);
    if (ret)
	krb5_err(context, 1, ret, "digest_alloc");

    if (strcasecmp(opt->type_string, "CHAP") == 0) {
	if (opt->server_identifier_string == NULL)
	    errx(1, "server identifier missing");

	ret = krb5_digest_set_identifier(context, digest,
					 opt->server_identifier_string);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_digest_set_type");
    }

    ret = krb5_digest_set_type(context, digest, opt->type_string);
    if (ret)
	krb5_err(context, 1, ret, "krb5_digest_set_type");

    ret = krb5_digest_set_username(context, digest, opt->username_string);
    if (ret)
	krb5_err(context, 1, ret, "krb5_digest_set_username");

    ret = krb5_digest_set_server_nonce(context, digest,
				       opt->server_nonce_string);
    if (ret)
	krb5_err(context, 1, ret, "krb5_digest_set_server_nonce");

    if(opt->client_nonce_string) {
	ret = krb5_digest_set_client_nonce(context, digest,
					   opt->client_nonce_string);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_digest_set_client_nonce");
    }


    ret = krb5_digest_set_opaque(context, digest, opt->opaque_string);
    if (ret)
	krb5_err(context, 1, ret, "krb5_digest_set_opaque");

    ret = krb5_digest_set_responseData(context, digest,
				       opt->client_response_string);
    if (ret)
	krb5_err(context, 1, ret, "krb5_digest_set_responseData");

    ret = krb5_digest_request(context, digest,
			      opt->kerberos_realm_string, id);
    if (ret)
	krb5_err(context, 1, ret, "krb5_digest_request");

    status = krb5_digest_rep_get_status(context, digest) ? "ok" : "failed";
    rsp = krb5_digest_get_rsp(context, digest);

    printf("status=%s\n", status);
    if (rsp)
	printf("rsp=%s\n", rsp);
    printf("tickets=no\n");

    ret = krb5_digest_get_session_key(context, digest, &session_key);
    if (ret)
	krb5_err(context, 1, ret, "krb5_digest_get_session_key");

    if (session_key.length) {
	char *key;
	hex_encode(session_key.data, session_key.length, &key);
	if (key == NULL)
	    krb5_errx(context, 1, "hex_encode");
	krb5_data_free(&session_key);
	printf("session-key=%s\n", key);
	free(key);
    }

    krb5_digest_free(digest);

    return 0;
}

static void
client_chap(const void *server_nonce, size_t snoncelen,
	    unsigned char server_identifier,
	    const char *password)
{
    EVP_MD_CTX *ctx;
    unsigned char md[MD5_DIGEST_LENGTH];
    char *h;

    ctx = EVP_MD_CTX_create();
    EVP_DigestInit_ex(ctx, EVP_md5(), NULL);

    EVP_DigestUpdate(ctx, &server_identifier, 1);
    EVP_DigestUpdate(ctx, password, strlen(password));
    EVP_DigestUpdate(ctx, server_nonce, snoncelen);
    EVP_DigestFinal_ex(ctx, md, NULL);

    EVP_MD_CTX_destroy(ctx);

    hex_encode(md, 16, &h);

    printf("responseData=%s\n", h);
    free(h);
}

static const unsigned char ms_chap_v2_magic1[39] = {
    0x4D, 0x61, 0x67, 0x69, 0x63, 0x20, 0x73, 0x65, 0x72, 0x76,
    0x65, 0x72, 0x20, 0x74, 0x6F, 0x20, 0x63, 0x6C, 0x69, 0x65,
    0x6E, 0x74, 0x20, 0x73, 0x69, 0x67, 0x6E, 0x69, 0x6E, 0x67,
    0x20, 0x63, 0x6F, 0x6E, 0x73, 0x74, 0x61, 0x6E, 0x74
};
static const unsigned char ms_chap_v2_magic2[41] = {
    0x50, 0x61, 0x64, 0x20, 0x74, 0x6F, 0x20, 0x6D, 0x61, 0x6B,
    0x65, 0x20, 0x69, 0x74, 0x20, 0x64, 0x6F, 0x20, 0x6D, 0x6F,
    0x72, 0x65, 0x20, 0x74, 0x68, 0x61, 0x6E, 0x20, 0x6F, 0x6E,
    0x65, 0x20, 0x69, 0x74, 0x65, 0x72, 0x61, 0x74, 0x69, 0x6F,
    0x6E
};
static const unsigned char ms_rfc3079_magic1[27] = {
    0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x74,
    0x68, 0x65, 0x20, 0x4d, 0x50, 0x50, 0x45, 0x20, 0x4d,
    0x61, 0x73, 0x74, 0x65, 0x72, 0x20, 0x4b, 0x65, 0x79
};

static void
client_mschapv2(const void *server_nonce, size_t snoncelen,
		const void *client_nonce, size_t cnoncelen,
		const char *username,
		const char *password)
{
    EVP_MD_CTX *hctx, *ctx;
    unsigned char md[SHA_DIGEST_LENGTH], challenge[SHA_DIGEST_LENGTH];
    unsigned char hmd[MD4_DIGEST_LENGTH];
    struct ntlm_buf answer;
    int i, len, ret;
    char *h;

    ctx = EVP_MD_CTX_create();
    EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);

    EVP_DigestUpdate(ctx, client_nonce, cnoncelen);
    EVP_DigestUpdate(ctx, server_nonce, snoncelen);
    EVP_DigestUpdate(ctx, username, strlen(username));
    EVP_DigestFinal_ex(ctx, md, NULL);


    hctx = EVP_MD_CTX_create();
    EVP_DigestInit_ex(hctx, EVP_md4(), NULL);
    len = strlen(password);
    for (i = 0; i < len; i++) {
	EVP_DigestUpdate(hctx, &password[i], 1);
	EVP_DigestUpdate(hctx, &password[len], 1);
    }
    EVP_DigestFinal_ex(hctx, hmd, NULL);


    /* ChallengeResponse */
    ret = heim_ntlm_calculate_ntlm1(hmd, sizeof(hmd), md, &answer);
    if (ret)
	errx(1, "heim_ntlm_calculate_ntlm1");

    hex_encode(answer.data, answer.length, &h);
    printf("responseData=%s\n", h);
    free(h);

    /* PasswordHash */
    EVP_DigestInit_ex(hctx, EVP_md4(), NULL);
    EVP_DigestUpdate(hctx, hmd, sizeof(hmd));
    EVP_DigestFinal_ex(hctx, hmd, NULL);


    /* GenerateAuthenticatorResponse */
    EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(ctx, hmd, sizeof(hmd));
    EVP_DigestUpdate(ctx, answer.data, answer.length);
    EVP_DigestUpdate(ctx, ms_chap_v2_magic1, sizeof(ms_chap_v2_magic1));
    EVP_DigestFinal_ex(ctx, md, NULL);

    /* ChallengeHash */
    EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(ctx, client_nonce, cnoncelen);
    EVP_DigestUpdate(ctx, server_nonce, snoncelen);
    EVP_DigestUpdate(ctx, username, strlen(username));
    EVP_DigestFinal_ex(ctx, challenge, NULL);

    EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(ctx, md, sizeof(md));
    EVP_DigestUpdate(ctx, challenge, 8);
    EVP_DigestUpdate(ctx, ms_chap_v2_magic2, sizeof(ms_chap_v2_magic2));
    EVP_DigestFinal_ex(ctx, md, NULL);

    hex_encode(md, sizeof(md), &h);
    printf("AuthenticatorResponse=%s\n", h);
    free(h);

    /* get_master, rfc 3079 3.4 */
    EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(ctx, hmd, sizeof(hmd));
    EVP_DigestUpdate(ctx, answer.data, answer.length);
    EVP_DigestUpdate(ctx, ms_rfc3079_magic1, sizeof(ms_rfc3079_magic1));
    EVP_DigestFinal_ex(ctx, md, NULL);

    free(answer.data);

    hex_encode(md, 16, &h);
    printf("session-key=%s\n", h);
    free(h);

    EVP_MD_CTX_destroy(hctx);
    EVP_MD_CTX_destroy(ctx);
}


int
digest_client_request(struct digest_client_request_options *opt,
		      int argc, char **argv)
{
    char *server_nonce, *client_nonce = NULL, server_identifier;
    ssize_t snoncelen, cnoncelen = 0;

    if (opt->server_nonce_string == NULL)
	errx(1, "server nonce missing");
    if (opt->password_string == NULL)
	errx(1, "password missing");

    if (opt->opaque_string == NULL)
	errx(1, "opaque missing");

    snoncelen = strlen(opt->server_nonce_string);
    server_nonce = malloc(snoncelen);
    if (server_nonce == NULL)
	errx(1, "server_nonce");

    snoncelen = hex_decode(opt->server_nonce_string, server_nonce, snoncelen);
    if (snoncelen <= 0)
	errx(1, "server nonce wrong");

    if (opt->client_nonce_string) {
	cnoncelen = strlen(opt->client_nonce_string);
	client_nonce = malloc(cnoncelen);
	if (client_nonce == NULL)
	    errx(1, "client_nonce");

	cnoncelen = hex_decode(opt->client_nonce_string,
			       client_nonce, cnoncelen);
	if (cnoncelen <= 0)
	    errx(1, "client nonce wrong");
    }

    if (opt->server_identifier_string) {
	int ret;

	ret = hex_decode(opt->server_identifier_string, &server_identifier, 1);
	if (ret != 1)
	    errx(1, "server identifier wrong length");
    }

    if (strcasecmp(opt->type_string, "CHAP") == 0) {
	if (opt->server_identifier_string == NULL)
	    errx(1, "server identifier missing");

	client_chap(server_nonce, snoncelen, server_identifier,
		    opt->password_string);

    } else if (strcasecmp(opt->type_string, "MS-CHAP-V2") == 0) {
	if (opt->client_nonce_string == NULL)
	    errx(1, "client nonce missing");
	if (opt->username_string == NULL)
	    errx(1, "client nonce missing");

	client_mschapv2(server_nonce, snoncelen,
			client_nonce, cnoncelen,
			opt->username_string,
			opt->password_string);
    }
    if (client_nonce)
	free(client_nonce);
    free(server_nonce);

    return 0;
}

#include <heimntlm.h>

int
ntlm_server_init(struct ntlm_server_init_options *opt,
		 int argc, char ** argv)
{
    krb5_error_code ret;
    krb5_ntlm ntlm;
    struct ntlm_type2 type2;
    krb5_data challenge, opaque;
    struct ntlm_buf data;
    char *s;
    static char zero2[] = "\x00\x00";

    memset(&type2, 0, sizeof(type2));

    ret = krb5_ntlm_alloc(context, &ntlm);
    if (ret)
	krb5_err(context, 1, ret, "krb5_ntlm_alloc");

    ret = krb5_ntlm_init_request(context,
				 ntlm,
				 opt->kerberos_realm_string,
				 id,
				 NTLM_NEG_UNICODE|NTLM_NEG_NTLM,
				 "NUTCRACKER",
				 "L");
    if (ret)
	krb5_err(context, 1, ret, "krb5_ntlm_init_request");

    /*
     *
     */

    ret = krb5_ntlm_init_get_challange(context, ntlm, &challenge);
    if (ret)
	krb5_err(context, 1, ret, "krb5_ntlm_init_get_challange");

    if (challenge.length != sizeof(type2.challenge))
	krb5_errx(context, 1, "ntlm challenge have wrong length");
    memcpy(type2.challenge, challenge.data, sizeof(type2.challenge));
    krb5_data_free(&challenge);

    ret = krb5_ntlm_init_get_flags(context, ntlm, &type2.flags);
    if (ret)
	krb5_err(context, 1, ret, "krb5_ntlm_init_get_flags");

    krb5_ntlm_init_get_targetname(context, ntlm, &type2.targetname);
    type2.targetinfo.data = zero2;
    type2.targetinfo.length = 2;

    ret = heim_ntlm_encode_type2(&type2, &data);
    if (ret)
	krb5_errx(context, 1, "heim_ntlm_encode_type2");

    free(type2.targetname);

    /*
     *
     */

    base64_encode(data.data, data.length, &s);
    free(data.data);
    printf("type2=%s\n", s);
    free(s);

    /*
     *
     */

    ret = krb5_ntlm_init_get_opaque(context, ntlm, &opaque);
    if (ret)
	krb5_err(context, 1, ret, "krb5_ntlm_init_get_opaque");

    base64_encode(opaque.data, opaque.length, &s);
    krb5_data_free(&opaque);
    printf("opaque=%s\n", s);
    free(s);

    /*
     *
     */

    krb5_ntlm_free(context, ntlm);

    return 0;
}


/*
 *
 */

int
help(void *opt, int argc, char **argv)
{
    sl_slc_help(commands, argc, argv);
    return 0;
}

int
main(int argc, char **argv)
{
    krb5_error_code ret;
    int optidx = 0;

    setprogname(argv[0]);

    ret = krb5_init_context (&context);
    if (ret == KRB5_CONFIG_BADFORMAT)
	errx (1, "krb5_init_context failed to parse configuration file");
    else if (ret)
	errx(1, "krb5_init_context failed: %d", ret);

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

    if (argc == 0) {
	help(NULL, argc, argv);
	return 1;
    }

    if (ccache_string) {
	ret = krb5_cc_resolve(context, ccache_string, &id);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_cc_resolve");
    }

    ret = sl_command (commands, argc, argv);
    if (ret == -1) {
	help(NULL, argc, argv);
	return 1;
    }
    return ret;
}
