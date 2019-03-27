/*
 * Copyright (c) 2006 - 2008 Kungliga Tekniska Högskolan
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

#include "krb5/gsskrb5_locl.h"
#include <err.h>
#include <getarg.h>
#include <gssapi.h>
#include <gssapi_krb5.h>
#include <gssapi_spnego.h>
#include <gssapi_ntlm.h>
#include "test_common.h"

static char *type_string;
static char *mech_string;
static char *ret_mech_string;
static char *client_name;
static char *client_password;
static int dns_canon_flag = -1;
static int mutual_auth_flag = 0;
static int dce_style_flag = 0;
static int wrapunwrap_flag = 0;
static int iov_flag = 0;
static int getverifymic_flag = 0;
static int deleg_flag = 0;
static int policy_deleg_flag = 0;
static int server_no_deleg_flag = 0;
static int ei_flag = 0;
static char *gsskrb5_acceptor_identity = NULL;
static char *session_enctype_string = NULL;
static int client_time_offset = 0;
static int server_time_offset = 0;
static int max_loops = 0;
static char *limit_enctype_string = NULL;
static int version_flag = 0;
static int verbose_flag = 0;
static int help_flag	= 0;

static krb5_context context;
static krb5_enctype limit_enctype = 0;

static struct {
    const char *name;
    gss_OID oid;
} o2n[] = {
    { "krb5", NULL /* GSS_KRB5_MECHANISM */ },
    { "spnego", NULL /* GSS_SPNEGO_MECHANISM */ },
    { "ntlm", NULL /* GSS_NTLM_MECHANISM */ },
    { "sasl-digest-md5", NULL /* GSS_SASL_DIGEST_MD5_MECHANISM */ }
};

static void
init_o2n(void)
{
    o2n[0].oid = GSS_KRB5_MECHANISM;
    o2n[1].oid = GSS_SPNEGO_MECHANISM;
    o2n[2].oid = GSS_NTLM_MECHANISM;
    o2n[3].oid = GSS_SASL_DIGEST_MD5_MECHANISM;
}

static gss_OID
string_to_oid(const char *name)
{
    int i;
    for (i = 0; i < sizeof(o2n)/sizeof(o2n[0]); i++)
	if (strcasecmp(name, o2n[i].name) == 0)
	    return o2n[i].oid;
    errx(1, "name '%s' not unknown", name);
}

static const char *
oid_to_string(const gss_OID oid)
{
    int i;
    for (i = 0; i < sizeof(o2n)/sizeof(o2n[0]); i++)
	if (gss_oid_equal(oid, o2n[i].oid))
	    return o2n[i].name;
    return "unknown oid";
}

static void
loop(gss_OID mechoid,
     gss_OID nameoid, const char *target,
     gss_cred_id_t init_cred,
     gss_ctx_id_t *sctx, gss_ctx_id_t *cctx,
     gss_OID *actual_mech,
     gss_cred_id_t *deleg_cred)
{
    int server_done = 0, client_done = 0;
    int num_loops = 0;
    OM_uint32 maj_stat, min_stat;
    gss_name_t gss_target_name;
    gss_buffer_desc input_token, output_token;
    OM_uint32 flags = 0, ret_cflags, ret_sflags;
    gss_OID actual_mech_client;
    gss_OID actual_mech_server;

    *actual_mech = GSS_C_NO_OID;

    flags |= GSS_C_INTEG_FLAG;
    flags |= GSS_C_CONF_FLAG;

    if (mutual_auth_flag)
	flags |= GSS_C_MUTUAL_FLAG;
    if (dce_style_flag)
	flags |= GSS_C_DCE_STYLE;
    if (deleg_flag)
	flags |= GSS_C_DELEG_FLAG;
    if (policy_deleg_flag)
	flags |= GSS_C_DELEG_POLICY_FLAG;

    input_token.value = rk_UNCONST(target);
    input_token.length = strlen(target);

    maj_stat = gss_import_name(&min_stat,
			       &input_token,
			       nameoid,
			       &gss_target_name);
    if (GSS_ERROR(maj_stat))
	err(1, "import name creds failed with: %d", maj_stat);

    input_token.length = 0;
    input_token.value = NULL;

    while (!server_done || !client_done) {
	num_loops++;

	gsskrb5_set_time_offset(client_time_offset);

	maj_stat = gss_init_sec_context(&min_stat,
					init_cred,
					cctx,
					gss_target_name,
					mechoid,
					flags,
					0,
					NULL,
					&input_token,
					&actual_mech_client,
					&output_token,
					&ret_cflags,
					NULL);
	if (GSS_ERROR(maj_stat))
	    errx(1, "init_sec_context: %s",
		 gssapi_err(maj_stat, min_stat, mechoid));
	if (maj_stat & GSS_S_CONTINUE_NEEDED)
	    ;
	else
	    client_done = 1;

	gsskrb5_get_time_offset(&client_time_offset);

	if (client_done && server_done)
	    break;

	if (input_token.length != 0)
	    gss_release_buffer(&min_stat, &input_token);

	gsskrb5_set_time_offset(server_time_offset);

	maj_stat = gss_accept_sec_context(&min_stat,
					  sctx,
					  GSS_C_NO_CREDENTIAL,
					  &output_token,
					  GSS_C_NO_CHANNEL_BINDINGS,
					  NULL,
					  &actual_mech_server,
					  &input_token,
					  &ret_sflags,
					  NULL,
					  deleg_cred);
	if (GSS_ERROR(maj_stat))
		errx(1, "accept_sec_context: %s",
		     gssapi_err(maj_stat, min_stat, actual_mech_server));

	gsskrb5_get_time_offset(&server_time_offset);

	if (output_token.length != 0)
	    gss_release_buffer(&min_stat, &output_token);

	if (maj_stat & GSS_S_CONTINUE_NEEDED)
	    ;
	else
	    server_done = 1;
    }
    if (output_token.length != 0)
	gss_release_buffer(&min_stat, &output_token);
    if (input_token.length != 0)
	gss_release_buffer(&min_stat, &input_token);
    gss_release_name(&min_stat, &gss_target_name);

    if (deleg_flag || policy_deleg_flag) {
	if (server_no_deleg_flag) {
	    if (*deleg_cred != GSS_C_NO_CREDENTIAL)
		errx(1, "got delegated cred but didn't expect one");
	} else if (*deleg_cred == GSS_C_NO_CREDENTIAL)
	    errx(1, "asked for delegarated cred but did get one");
    } else if (*deleg_cred != GSS_C_NO_CREDENTIAL)
	  errx(1, "got deleg_cred cred but didn't ask");

    if (gss_oid_equal(actual_mech_server, actual_mech_client) == 0)
	errx(1, "mech mismatch");
    *actual_mech = actual_mech_server;

    if (max_loops && num_loops > max_loops)
	errx(1, "num loops %d was lager then max loops %d",
	     num_loops, max_loops);

    if (verbose_flag) {
	printf("server time offset: %d\n", server_time_offset);
	printf("client time offset: %d\n", client_time_offset);
	printf("num loops %d\n", num_loops);
    }
}

static void
wrapunwrap(gss_ctx_id_t cctx, gss_ctx_id_t sctx, int flags, gss_OID mechoid)
{
    gss_buffer_desc input_token, output_token, output_token2;
    OM_uint32 min_stat, maj_stat;
    gss_qop_t qop_state;
    int conf_state;

    input_token.value = "foo";
    input_token.length = 3;

    maj_stat = gss_wrap(&min_stat, cctx, flags, 0, &input_token,
			&conf_state, &output_token);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_wrap failed: %s",
	     gssapi_err(maj_stat, min_stat, mechoid));

    maj_stat = gss_unwrap(&min_stat, sctx, &output_token,
			  &output_token2, &conf_state, &qop_state);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_unwrap failed: %s",
	     gssapi_err(maj_stat, min_stat, mechoid));

    gss_release_buffer(&min_stat, &output_token);
    gss_release_buffer(&min_stat, &output_token2);

#if 0 /* doesn't work for NTLM yet */
    if (!!conf_state != !!flags)
	errx(1, "conf_state mismatch");
#endif
}

#define USE_CONF		1
#define USE_HEADER_ONLY		2
#define USE_SIGN_ONLY		4
#define FORCE_IOV		8

static void
wrapunwrap_iov(gss_ctx_id_t cctx, gss_ctx_id_t sctx, int flags, gss_OID mechoid)
{
    krb5_data token, header, trailer;
    OM_uint32 min_stat, maj_stat;
    gss_qop_t qop_state;
    int conf_state, conf_state2;
    gss_iov_buffer_desc iov[6];
    unsigned char *p;
    int iov_len;
    char header_data[9] = "ABCheader";
    char trailer_data[10] = "trailerXYZ";

    char token_data[16] = "0123456789abcdef";

    memset(&iov, 0, sizeof(iov));

    if (flags & USE_SIGN_ONLY) {
	header.data = header_data;
	header.length = 9;
	trailer.data = trailer_data;
	trailer.length = 10;
    } else {
	header.data = NULL;
	header.length = 0;
	trailer.data = NULL;
	trailer.length = 0;
    }

    token.data = token_data;
    token.length = 16;

    iov_len = sizeof(iov)/sizeof(iov[0]);

    memset(iov, 0, sizeof(iov));

    iov[0].type = GSS_IOV_BUFFER_TYPE_HEADER | GSS_IOV_BUFFER_TYPE_FLAG_ALLOCATE;

    if (header.length != 0) {
	iov[1].type = GSS_IOV_BUFFER_TYPE_SIGN_ONLY;
	iov[1].buffer.length = header.length;
	iov[1].buffer.value = header.data;
    } else {
	iov[1].type = GSS_IOV_BUFFER_TYPE_EMPTY;
	iov[1].buffer.length = 0;
	iov[1].buffer.value = NULL;
    }
    iov[2].type = GSS_IOV_BUFFER_TYPE_DATA;
    iov[2].buffer.length = token.length;
    iov[2].buffer.value = token.data;
    if (trailer.length != 0) {
	iov[3].type = GSS_IOV_BUFFER_TYPE_SIGN_ONLY;
	iov[3].buffer.length = trailer.length;
	iov[3].buffer.value = trailer.data;
    } else {
	iov[3].type = GSS_IOV_BUFFER_TYPE_EMPTY;
	iov[3].buffer.length = 0;
	iov[3].buffer.value = NULL;
    }
    if (dce_style_flag) {
	iov[4].type = GSS_IOV_BUFFER_TYPE_EMPTY;
    } else {
	iov[4].type = GSS_IOV_BUFFER_TYPE_PADDING | GSS_IOV_BUFFER_TYPE_FLAG_ALLOCATE;
    }
    iov[4].buffer.length = 0;
    iov[4].buffer.value = 0;
    if (dce_style_flag) {
	iov[5].type = GSS_IOV_BUFFER_TYPE_EMPTY;
    } else if (flags & USE_HEADER_ONLY) {
	iov[5].type = GSS_IOV_BUFFER_TYPE_EMPTY;
    } else {
	iov[5].type = GSS_IOV_BUFFER_TYPE_TRAILER | GSS_IOV_BUFFER_TYPE_FLAG_ALLOCATE;
    }
    iov[5].buffer.length = 0;
    iov[5].buffer.value = 0;

    maj_stat = gss_wrap_iov(&min_stat, cctx, dce_style_flag || flags & USE_CONF, 0, &conf_state,
			    iov, iov_len);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_wrap_iov failed");

    token.length =
	iov[0].buffer.length +
	iov[1].buffer.length +
	iov[2].buffer.length +
	iov[3].buffer.length +
	iov[4].buffer.length +
	iov[5].buffer.length;
    token.data = emalloc(token.length);

    p = token.data;
    memcpy(p, iov[0].buffer.value, iov[0].buffer.length);
    p += iov[0].buffer.length;
    memcpy(p, iov[1].buffer.value, iov[1].buffer.length);
    p += iov[1].buffer.length;
    memcpy(p, iov[2].buffer.value, iov[2].buffer.length);
    p += iov[2].buffer.length;
    memcpy(p, iov[3].buffer.value, iov[3].buffer.length);
    p += iov[3].buffer.length;
    memcpy(p, iov[4].buffer.value, iov[4].buffer.length);
    p += iov[4].buffer.length;
    memcpy(p, iov[5].buffer.value, iov[5].buffer.length);
    p += iov[5].buffer.length;

    assert(p - ((unsigned char *)token.data) == token.length);

    if ((flags & (USE_SIGN_ONLY|FORCE_IOV)) == 0) {
	gss_buffer_desc input, output;

	input.value = token.data;
	input.length = token.length;

	maj_stat = gss_unwrap(&min_stat, sctx, &input,
			      &output, &conf_state2, &qop_state);

	if (maj_stat != GSS_S_COMPLETE)
	    errx(1, "gss_unwrap from gss_wrap_iov failed: %s",
		 gssapi_err(maj_stat, min_stat, mechoid));

	gss_release_buffer(&min_stat, &output);
    } else {
	maj_stat = gss_unwrap_iov(&min_stat, sctx, &conf_state2, &qop_state,
				  iov, iov_len);

	if (maj_stat != GSS_S_COMPLETE)
	    errx(1, "gss_unwrap_iov failed: %x %s", flags,
		 gssapi_err(maj_stat, min_stat, mechoid));

    }
    if (conf_state2 != conf_state)
	errx(1, "conf state wrong for iov: %x", flags);


    free(token.data);
}

static void
getverifymic(gss_ctx_id_t cctx, gss_ctx_id_t sctx, gss_OID mechoid)
{
    gss_buffer_desc input_token, output_token;
    OM_uint32 min_stat, maj_stat;
    gss_qop_t qop_state;

    input_token.value = "bar";
    input_token.length = 3;

    maj_stat = gss_get_mic(&min_stat, cctx, 0, &input_token,
			   &output_token);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_get_mic failed: %s",
	     gssapi_err(maj_stat, min_stat, mechoid));

    maj_stat = gss_verify_mic(&min_stat, sctx, &input_token,
			      &output_token, &qop_state);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_verify_mic failed: %s",
	     gssapi_err(maj_stat, min_stat, mechoid));

    gss_release_buffer(&min_stat, &output_token);
}

static void
empty_release(void)
{
    gss_ctx_id_t ctx = GSS_C_NO_CONTEXT;
    gss_cred_id_t cred = GSS_C_NO_CREDENTIAL;
    gss_name_t name = GSS_C_NO_NAME;
    gss_OID_set oidset = GSS_C_NO_OID_SET;
    OM_uint32 junk;

    gss_delete_sec_context(&junk, &ctx, NULL);
    gss_release_cred(&junk, &cred);
    gss_release_name(&junk, &name);
    gss_release_oid_set(&junk, &oidset);
}

/*
 *
 */

static struct getargs args[] = {
    {"name-type",0,	arg_string, &type_string,  "type of name", NULL },
    {"mech-type",0,	arg_string, &mech_string,  "type of mech", NULL },
    {"ret-mech-type",0,	arg_string, &ret_mech_string,
     "type of return mech", NULL },
    {"dns-canonicalize",0,arg_negative_flag, &dns_canon_flag,
     "use dns to canonicalize", NULL },
    {"mutual-auth",0,	arg_flag,	&mutual_auth_flag,"mutual auth", NULL },
    {"client-name", 0,  arg_string,     &client_name, "client name", NULL },
    {"client-password", 0,  arg_string, &client_password, "client password", NULL },
    {"limit-enctype",0,	arg_string,	&limit_enctype_string, "enctype", NULL },
    {"dce-style",0,	arg_flag,	&dce_style_flag, "dce-style", NULL },
    {"wrapunwrap",0,	arg_flag,	&wrapunwrap_flag, "wrap/unwrap", NULL },
    {"iov", 0, 		arg_flag,	&iov_flag, "wrap/unwrap iov", NULL },
    {"getverifymic",0,	arg_flag,	&getverifymic_flag,
     "get and verify mic", NULL },
    {"delegate",0,	arg_flag,	&deleg_flag, "delegate credential", NULL },
    {"policy-delegate",0,	arg_flag,	&policy_deleg_flag, "policy delegate credential", NULL },
    {"server-no-delegate",0,	arg_flag,	&server_no_deleg_flag,
     "server should get a credential", NULL },
    {"export-import-cred",0,	arg_flag,	&ei_flag, "test export/import cred", NULL },
    {"gsskrb5-acceptor-identity", 0, arg_string, &gsskrb5_acceptor_identity, "keytab", NULL },
    {"session-enctype",	0, arg_string,	&session_enctype_string, "enctype", NULL },
    {"client-time-offset",	0, arg_integer,	&client_time_offset, "time", NULL },
    {"server-time-offset",	0, arg_integer,	&server_time_offset, "time", NULL },
    {"max-loops",	0, arg_integer,	&max_loops, "time", NULL },
    {"version",	0,	arg_flag,	&version_flag, "print version", NULL },
    {"verbose",	'v',	arg_flag,	&verbose_flag, "verbose", NULL },
    {"help",	0,	arg_flag,	&help_flag,  NULL, NULL }
};

static void
usage (int ret)
{
    arg_printusage (args, sizeof(args)/sizeof(*args),
		    NULL, "service@host");
    exit (ret);
}

int
main(int argc, char **argv)
{
    int optind = 0;
    OM_uint32 min_stat, maj_stat;
    gss_ctx_id_t cctx, sctx;
    void *ctx;
    gss_OID nameoid, mechoid, actual_mech, actual_mech2;
    gss_cred_id_t client_cred = GSS_C_NO_CREDENTIAL, deleg_cred = GSS_C_NO_CREDENTIAL;
    gss_name_t cname = GSS_C_NO_NAME;
    gss_buffer_desc credential_data = GSS_C_EMPTY_BUFFER;

    setprogname(argv[0]);

    init_o2n();

    if (krb5_init_context(&context))
	errx(1, "krb5_init_context");

    cctx = sctx = GSS_C_NO_CONTEXT;

    if(getarg(args, sizeof(args) / sizeof(args[0]), argc, argv, &optind))
	usage(1);

    if (help_flag)
	usage (0);

    if(version_flag){
	print_version(NULL);
	exit(0);
    }

    argc -= optind;
    argv += optind;

    if (argc != 1)
	usage(1);

    if (dns_canon_flag != -1)
	gsskrb5_set_dns_canonicalize(dns_canon_flag);

    if (type_string == NULL)
	nameoid = GSS_C_NT_HOSTBASED_SERVICE;
    else if (strcmp(type_string, "hostbased-service") == 0)
	nameoid = GSS_C_NT_HOSTBASED_SERVICE;
    else if (strcmp(type_string, "krb5-principal-name") == 0)
	nameoid = GSS_KRB5_NT_PRINCIPAL_NAME;
    else
	errx(1, "%s not suppported", type_string);

    if (mech_string == NULL)
	mechoid = GSS_KRB5_MECHANISM;
    else
	mechoid = string_to_oid(mech_string);

    if (gsskrb5_acceptor_identity) {
	maj_stat = gsskrb5_register_acceptor_identity(gsskrb5_acceptor_identity);
	if (maj_stat)
	    errx(1, "gsskrb5_acceptor_identity: %s",
		 gssapi_err(maj_stat, 0, GSS_C_NO_OID));
    }

    if (client_password) {
	credential_data.value = client_password;
	credential_data.length = strlen(client_password);
    }

    if (client_name) {
	gss_buffer_desc cn;

	cn.value = client_name;
	cn.length = strlen(client_name);

	maj_stat = gss_import_name(&min_stat, &cn, GSS_C_NT_USER_NAME, &cname);
	if (maj_stat)
	    errx(1, "gss_import_name: %s",
		 gssapi_err(maj_stat, min_stat, GSS_C_NO_OID));
    }

    if (client_password) {
	maj_stat = gss_acquire_cred_with_password(&min_stat,
						  cname,
						  &credential_data,
						  GSS_C_INDEFINITE,
						  GSS_C_NO_OID_SET,
						  GSS_C_INITIATE,
						  &client_cred,
						  NULL,
						  NULL);
	if (GSS_ERROR(maj_stat))
	    errx(1, "gss_acquire_cred_with_password: %s",
		 gssapi_err(maj_stat, min_stat, GSS_C_NO_OID));
    } else {
	maj_stat = gss_acquire_cred(&min_stat,
				    cname,
				    GSS_C_INDEFINITE,
				    GSS_C_NO_OID_SET,
				    GSS_C_INITIATE,
				    &client_cred,
				    NULL,
				    NULL);
	if (GSS_ERROR(maj_stat))
	    errx(1, "gss_acquire_cred: %s",
		 gssapi_err(maj_stat, min_stat, GSS_C_NO_OID));
    }

    if (limit_enctype_string) {
	krb5_error_code ret;

	ret = krb5_string_to_enctype(context,
				     limit_enctype_string,
				     &limit_enctype);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_string_to_enctype");
    }


    if (limit_enctype) {
	if (client_cred == NULL)
	    errx(1, "client_cred missing");

	maj_stat = gss_krb5_set_allowable_enctypes(&min_stat, client_cred,
						   1, &limit_enctype);
	if (maj_stat)
	    errx(1, "gss_krb5_set_allowable_enctypes: %s",
		 gssapi_err(maj_stat, min_stat, GSS_C_NO_OID));
    }

    loop(mechoid, nameoid, argv[0], client_cred,
	 &sctx, &cctx, &actual_mech, &deleg_cred);

    if (verbose_flag)
	printf("resulting mech: %s\n", oid_to_string(actual_mech));

    if (ret_mech_string) {
	gss_OID retoid;

	retoid = string_to_oid(ret_mech_string);

	if (gss_oid_equal(retoid, actual_mech) == 0)
	    errx(1, "actual_mech mech is not the expected type %s",
		 ret_mech_string);
    }

    /* XXX should be actual_mech */
    if (gss_oid_equal(mechoid, GSS_KRB5_MECHANISM)) {
	time_t time;
	gss_buffer_desc authz_data;
	gss_buffer_desc in, out1, out2;
	krb5_keyblock *keyblock, *keyblock2;
	krb5_timestamp now;
	krb5_error_code ret;

	ret = krb5_timeofday(context, &now);
	if (ret)
	    errx(1, "krb5_timeofday failed");

	/* client */
	maj_stat = gss_krb5_export_lucid_sec_context(&min_stat,
						     &cctx,
						     1, /* version */
						     &ctx);
	if (maj_stat != GSS_S_COMPLETE)
	    errx(1, "gss_krb5_export_lucid_sec_context failed: %s",
		 gssapi_err(maj_stat, min_stat, actual_mech));


	maj_stat = gss_krb5_free_lucid_sec_context(&maj_stat, ctx);
	if (maj_stat != GSS_S_COMPLETE)
	    errx(1, "gss_krb5_free_lucid_sec_context failed: %s",
		     gssapi_err(maj_stat, min_stat, actual_mech));

	/* server */
	maj_stat = gss_krb5_export_lucid_sec_context(&min_stat,
						     &sctx,
						     1, /* version */
						     &ctx);
	if (maj_stat != GSS_S_COMPLETE)
	    errx(1, "gss_krb5_export_lucid_sec_context failed: %s",
		     gssapi_err(maj_stat, min_stat, actual_mech));
	maj_stat = gss_krb5_free_lucid_sec_context(&min_stat, ctx);
	if (maj_stat != GSS_S_COMPLETE)
	    errx(1, "gss_krb5_free_lucid_sec_context failed: %s",
		     gssapi_err(maj_stat, min_stat, actual_mech));

 	maj_stat = gsskrb5_extract_authtime_from_sec_context(&min_stat,
							     sctx,
							     &time);
	if (maj_stat != GSS_S_COMPLETE)
	    errx(1, "gsskrb5_extract_authtime_from_sec_context failed: %s",
		     gssapi_err(maj_stat, min_stat, actual_mech));

	if (time > now)
	    errx(1, "gsskrb5_extract_authtime_from_sec_context failed: "
		 "time authtime is before now: %ld %ld",
		 (long)time, (long)now);

 	maj_stat = gsskrb5_extract_service_keyblock(&min_stat,
						    sctx,
						    &keyblock);
	if (maj_stat != GSS_S_COMPLETE)
	    errx(1, "gsskrb5_export_service_keyblock failed: %s",
		     gssapi_err(maj_stat, min_stat, actual_mech));

	krb5_free_keyblock(context, keyblock);

 	maj_stat = gsskrb5_get_subkey(&min_stat,
				      sctx,
				      &keyblock);
	if (maj_stat != GSS_S_COMPLETE
	    && (!(maj_stat == GSS_S_FAILURE && min_stat == GSS_KRB5_S_KG_NO_SUBKEY)))
	    errx(1, "gsskrb5_get_subkey server failed: %s",
		     gssapi_err(maj_stat, min_stat, actual_mech));

	if (maj_stat != GSS_S_COMPLETE)
	    keyblock = NULL;
	else if (limit_enctype && keyblock->keytype != limit_enctype)
	    errx(1, "gsskrb5_get_subkey wrong enctype");

 	maj_stat = gsskrb5_get_subkey(&min_stat,
				      cctx,
				      &keyblock2);
	if (maj_stat != GSS_S_COMPLETE
	    && (!(maj_stat == GSS_S_FAILURE && min_stat == GSS_KRB5_S_KG_NO_SUBKEY)))
	    errx(1, "gsskrb5_get_subkey client failed: %s",
		     gssapi_err(maj_stat, min_stat, actual_mech));

	if (maj_stat != GSS_S_COMPLETE)
	    keyblock2 = NULL;
	else if (limit_enctype && keyblock->keytype != limit_enctype)
	    errx(1, "gsskrb5_get_subkey wrong enctype");

	if (keyblock || keyblock2) {
	    if (keyblock == NULL)
		errx(1, "server missing token keyblock");
	    if (keyblock2 == NULL)
		errx(1, "client missing token keyblock");

	    if (keyblock->keytype != keyblock2->keytype)
		errx(1, "enctype mismatch");
	    if (keyblock->keyvalue.length != keyblock2->keyvalue.length)
		errx(1, "key length mismatch");
	    if (memcmp(keyblock->keyvalue.data, keyblock2->keyvalue.data,
		       keyblock2->keyvalue.length) != 0)
		errx(1, "key data mismatch");
	}

	if (session_enctype_string) {
	    krb5_enctype enctype;

	    ret = krb5_string_to_enctype(context,
					 session_enctype_string,
					 &enctype);

	    if (ret)
		krb5_err(context, 1, ret, "krb5_string_to_enctype");

	    if (enctype != keyblock->keytype)
		errx(1, "keytype is not the expected %d != %d",
		     (int)enctype, (int)keyblock2->keytype);
	}

	if (keyblock)
	    krb5_free_keyblock(context, keyblock);
	if (keyblock2)
	    krb5_free_keyblock(context, keyblock2);

 	maj_stat = gsskrb5_get_initiator_subkey(&min_stat,
						sctx,
						&keyblock);
	if (maj_stat != GSS_S_COMPLETE
	    && (!(maj_stat == GSS_S_FAILURE && min_stat == GSS_KRB5_S_KG_NO_SUBKEY)))
	    errx(1, "gsskrb5_get_initiator_subkey failed: %s",
		     gssapi_err(maj_stat, min_stat, actual_mech));

	if (maj_stat == GSS_S_COMPLETE) {

	    if (limit_enctype && keyblock->keytype != limit_enctype)
		errx(1, "gsskrb5_get_initiator_subkey wrong enctype");
	    krb5_free_keyblock(context, keyblock);
	}

 	maj_stat = gsskrb5_extract_authz_data_from_sec_context(&min_stat,
							       sctx,
							       128,
							       &authz_data);
	if (maj_stat == GSS_S_COMPLETE)
	    gss_release_buffer(&min_stat, &authz_data);


	memset(&out1, 0, sizeof(out1));
	memset(&out2, 0, sizeof(out2));

	in.value = "foo";
	in.length = 3;

	gss_pseudo_random(&min_stat, sctx, GSS_C_PRF_KEY_FULL, &in,
			  100, &out1);
	gss_pseudo_random(&min_stat, cctx, GSS_C_PRF_KEY_FULL, &in,
			  100, &out2);

	if (out1.length != out2.length)
	    errx(1, "prf len mismatch");
	if (memcmp(out1.value, out2.value, out1.length) != 0)
	    errx(1, "prf data mismatch");

	gss_release_buffer(&min_stat, &out1);

	gss_pseudo_random(&min_stat, sctx, GSS_C_PRF_KEY_FULL, &in,
			  100, &out1);

	if (out1.length != out2.length)
	    errx(1, "prf len mismatch");
	if (memcmp(out1.value, out2.value, out1.length) != 0)
	    errx(1, "prf data mismatch");

	gss_release_buffer(&min_stat, &out1);
	gss_release_buffer(&min_stat, &out2);

	in.value = "bar";
	in.length = 3;

	gss_pseudo_random(&min_stat, sctx, GSS_C_PRF_KEY_PARTIAL, &in,
			  100, &out1);
	gss_pseudo_random(&min_stat, cctx, GSS_C_PRF_KEY_PARTIAL, &in,
			  100, &out2);

	if (out1.length != out2.length)
	    errx(1, "prf len mismatch");
	if (memcmp(out1.value, out2.value, out1.length) != 0)
	    errx(1, "prf data mismatch");

	gss_release_buffer(&min_stat, &out1);
	gss_release_buffer(&min_stat, &out2);

	wrapunwrap_flag = 1;
	getverifymic_flag = 1;
    }

    if (wrapunwrap_flag) {
	wrapunwrap(cctx, sctx, 0, actual_mech);
	wrapunwrap(cctx, sctx, 1, actual_mech);
	wrapunwrap(sctx, cctx, 0, actual_mech);
	wrapunwrap(sctx, cctx, 1, actual_mech);
    }

    if (iov_flag) {
	wrapunwrap_iov(cctx, sctx, 0, actual_mech);
	wrapunwrap_iov(cctx, sctx, USE_HEADER_ONLY|FORCE_IOV, actual_mech);
	wrapunwrap_iov(cctx, sctx, USE_HEADER_ONLY, actual_mech);
	wrapunwrap_iov(cctx, sctx, USE_CONF, actual_mech);
	wrapunwrap_iov(cctx, sctx, USE_CONF|USE_HEADER_ONLY, actual_mech);

	wrapunwrap_iov(cctx, sctx, FORCE_IOV, actual_mech);
	wrapunwrap_iov(cctx, sctx, USE_CONF|FORCE_IOV, actual_mech);
	wrapunwrap_iov(cctx, sctx, USE_HEADER_ONLY|FORCE_IOV, actual_mech);
	wrapunwrap_iov(cctx, sctx, USE_CONF|USE_HEADER_ONLY|FORCE_IOV, actual_mech);

	wrapunwrap_iov(cctx, sctx, USE_SIGN_ONLY|FORCE_IOV, actual_mech);
	wrapunwrap_iov(cctx, sctx, USE_CONF|USE_SIGN_ONLY|FORCE_IOV, actual_mech);
	wrapunwrap_iov(cctx, sctx, USE_CONF|USE_HEADER_ONLY|USE_SIGN_ONLY|FORCE_IOV, actual_mech);

/* works */
	wrapunwrap_iov(cctx, sctx, 0, actual_mech);
	wrapunwrap_iov(cctx, sctx, FORCE_IOV, actual_mech);

	wrapunwrap_iov(cctx, sctx, USE_CONF, actual_mech);
	wrapunwrap_iov(cctx, sctx, USE_CONF|FORCE_IOV, actual_mech);

	wrapunwrap_iov(cctx, sctx, USE_SIGN_ONLY, actual_mech);
	wrapunwrap_iov(cctx, sctx, USE_SIGN_ONLY|FORCE_IOV, actual_mech);

	wrapunwrap_iov(cctx, sctx, USE_CONF|USE_SIGN_ONLY, actual_mech);
	wrapunwrap_iov(cctx, sctx, USE_CONF|USE_SIGN_ONLY|FORCE_IOV, actual_mech);

	wrapunwrap_iov(cctx, sctx, USE_HEADER_ONLY, actual_mech);
	wrapunwrap_iov(cctx, sctx, USE_HEADER_ONLY|FORCE_IOV, actual_mech);

	wrapunwrap_iov(cctx, sctx, USE_CONF|USE_HEADER_ONLY, actual_mech);
	wrapunwrap_iov(cctx, sctx, USE_CONF|USE_HEADER_ONLY|FORCE_IOV, actual_mech);
    }

    if (getverifymic_flag) {
	getverifymic(cctx, sctx, actual_mech);
	getverifymic(cctx, sctx, actual_mech);
	getverifymic(sctx, cctx, actual_mech);
	getverifymic(sctx, cctx, actual_mech);
    }


    gss_delete_sec_context(&min_stat, &cctx, NULL);
    gss_delete_sec_context(&min_stat, &sctx, NULL);

    if (deleg_cred != GSS_C_NO_CREDENTIAL) {
	gss_cred_id_t cred2 = GSS_C_NO_CREDENTIAL;
	gss_buffer_desc cb;

	if (verbose_flag)
	    printf("checking actual mech (%s) on delegated cred\n",
		   oid_to_string(actual_mech));
	loop(actual_mech, nameoid, argv[0], deleg_cred, &sctx, &cctx, &actual_mech2, &cred2);

	gss_delete_sec_context(&min_stat, &cctx, NULL);
	gss_delete_sec_context(&min_stat, &sctx, NULL);

	gss_release_cred(&min_stat, &cred2);

	/* try again using SPNEGO */
	if (verbose_flag)
	    printf("checking spnego on delegated cred\n");
	loop(GSS_SPNEGO_MECHANISM, nameoid, argv[0], deleg_cred, &sctx, &cctx,
	     &actual_mech2, &cred2);

	gss_delete_sec_context(&min_stat, &cctx, NULL);
	gss_delete_sec_context(&min_stat, &sctx, NULL);

	gss_release_cred(&min_stat, &cred2);

	/* check export/import */
	if (ei_flag) {

	    maj_stat = gss_export_cred(&min_stat, deleg_cred, &cb);
	    if (maj_stat != GSS_S_COMPLETE)
		errx(1, "export failed: %s",
		     gssapi_err(maj_stat, min_stat, NULL));

	    maj_stat = gss_import_cred(&min_stat, &cb, &cred2);
	    if (maj_stat != GSS_S_COMPLETE)
		errx(1, "import failed: %s",
		     gssapi_err(maj_stat, min_stat, NULL));

	    gss_release_buffer(&min_stat, &cb);
	    gss_release_cred(&min_stat, &deleg_cred);

	    if (verbose_flag)
		printf("checking actual mech (%s) on export/imported cred\n",
		       oid_to_string(actual_mech));
	    loop(actual_mech, nameoid, argv[0], cred2, &sctx, &cctx,
		 &actual_mech2, &deleg_cred);

	    gss_release_cred(&min_stat, &deleg_cred);

	    gss_delete_sec_context(&min_stat, &cctx, NULL);
	    gss_delete_sec_context(&min_stat, &sctx, NULL);

	    /* try again using SPNEGO */
	    if (verbose_flag)
		printf("checking SPNEGO on export/imported cred\n");
	    loop(GSS_SPNEGO_MECHANISM, nameoid, argv[0], cred2, &sctx, &cctx,
		 &actual_mech2, &deleg_cred);

	    gss_release_cred(&min_stat, &deleg_cred);

	    gss_delete_sec_context(&min_stat, &cctx, NULL);
	    gss_delete_sec_context(&min_stat, &sctx, NULL);

	    gss_release_cred(&min_stat, &cred2);

	} else  {
	    gss_release_cred(&min_stat, &deleg_cred);
	}

    }

    empty_release();

    krb5_free_context(context);

    return 0;
}
