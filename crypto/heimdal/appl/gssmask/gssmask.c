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

#include "common.h"
RCSID("$Id$");

/*
 *
 */

enum handle_type { handle_context, handle_cred };

struct handle {
    int32_t idx;
    enum handle_type type;
    void *ptr;
    struct handle *next;
};

struct client {
    krb5_storage *sock;
    krb5_storage *logging;
    char *moniker;
    int32_t nHandle;
    struct handle *handles;
    struct sockaddr_storage sa;
    socklen_t salen;
    char servername[MAXHOSTNAMELEN];
};

FILE *logfile;
static char *targetname;
krb5_context context;

/*
 *
 */

static void
logmessage(struct client *c, const char *file, unsigned int lineno,
	   int level, const char *fmt, ...)
{
    char *message;
    va_list ap;
    int32_t ackid;

    va_start(ap, fmt);
    vasprintf(&message, fmt, ap);
    va_end(ap);

    if (logfile)
	fprintf(logfile, "%s:%u: %d %s\n", file, lineno, level, message);

    if (c->logging) {
	if (krb5_store_int32(c->logging, eLogInfo) != 0)
	    errx(1, "krb5_store_int32: log level");
	if (krb5_store_string(c->logging, file) != 0)
	    errx(1, "krb5_store_string: filename");
	if (krb5_store_int32(c->logging, lineno) != 0)
	    errx(1, "krb5_store_string: filename");
	if (krb5_store_string(c->logging, message) != 0)
	    errx(1, "krb5_store_string: message");
	if (krb5_ret_int32(c->logging, &ackid) != 0)
	    errx(1, "krb5_ret_int32: ackid");
    }
    free(message);
}

/*
 *
 */

static int32_t
add_handle(struct client *c, enum handle_type type, void *data)
{
    struct handle *h;

    h = ecalloc(1, sizeof(*h));

    h->idx = ++c->nHandle;
    h->type = type;
    h->ptr = data;
    h->next = c->handles;
    c->handles = h;

    return h->idx;
}

static void
del_handle(struct handle **h, int32_t idx)
{
    OM_uint32 min_stat;

    if (idx == 0)
	return;

    while (*h) {
	if ((*h)->idx == idx) {
	    struct handle *p = *h;
	    *h = (*h)->next;
	    switch(p->type) {
	    case handle_context: {
		gss_ctx_id_t c = p->ptr;
		gss_delete_sec_context(&min_stat, &c, NULL);
		break; }
	    case handle_cred: {
		gss_cred_id_t c = p->ptr;
		gss_release_cred(&min_stat, &c);
		break; }
	    }
	    free(p);
	    return;
	}
	h = &((*h)->next);
    }
    errx(1, "tried to delete an unexisting handle");
}

static void *
find_handle(struct handle *h, int32_t idx, enum handle_type type)
{
    if (idx == 0)
	return NULL;

    while (h) {
	if (h->idx == idx) {
	    if (type == h->type)
		return h->ptr;
	    errx(1, "monger switched type on handle!");
	}
	h = h->next;
    }
    return NULL;
}


static int32_t
convert_gss_to_gsm(OM_uint32 maj_stat)
{
    switch(maj_stat) {
    case 0:
	return GSMERR_OK;
    case GSS_S_CONTINUE_NEEDED:
	return GSMERR_CONTINUE_NEEDED;
    case GSS_S_DEFECTIVE_TOKEN:
        return GSMERR_INVALID_TOKEN;
    case GSS_S_BAD_MIC:
	return GSMERR_AP_MODIFIED;
    default:
	return GSMERR_ERROR;
    }
}

static int32_t
convert_krb5_to_gsm(krb5_error_code ret)
{
    switch(ret) {
    case 0:
	return GSMERR_OK;
    default:
	return GSMERR_ERROR;
    }
}

/*
 *
 */

static int32_t
acquire_cred(struct client *c,
	     krb5_principal principal,
	     krb5_get_init_creds_opt *opt,
	     int32_t *handle)
{
    krb5_error_code ret;
    krb5_creds cred;
    krb5_ccache id;
    gss_cred_id_t gcred;
    OM_uint32 maj_stat, min_stat;

    *handle = 0;

    krb5_get_init_creds_opt_set_forwardable (opt, 1);
    krb5_get_init_creds_opt_set_renew_life (opt, 3600 * 24 * 30);

    memset(&cred, 0, sizeof(cred));

    ret = krb5_get_init_creds_password (context,
					&cred,
					principal,
					NULL,
					NULL,
					NULL,
					0,
					NULL,
					opt);
    if (ret) {
	logmessage(c, __FILE__, __LINE__, 0,
		   "krb5_get_init_creds failed: %d", ret);
	return convert_krb5_to_gsm(ret);
    }

    ret = krb5_cc_new_unique(context, "MEMORY", NULL, &id);
    if (ret)
	krb5_err (context, 1, ret, "krb5_cc_initialize");

    ret = krb5_cc_initialize (context, id, cred.client);
    if (ret)
	krb5_err (context, 1, ret, "krb5_cc_initialize");

    ret = krb5_cc_store_cred (context, id, &cred);
    if (ret)
	krb5_err (context, 1, ret, "krb5_cc_store_cred");

    krb5_free_cred_contents (context, &cred);

    maj_stat = gss_krb5_import_cred(&min_stat,
				    id,
				    NULL,
				    NULL,
				    &gcred);
    krb5_cc_close(context, id);
    if (maj_stat) {
	logmessage(c, __FILE__, __LINE__, 0,
		   "krb5 import creds failed with: %d", maj_stat);
	return convert_gss_to_gsm(maj_stat);
    }

    *handle = add_handle(c, handle_cred, gcred);

    return 0;
}


/*
 *
 */

#define HandleOP(h) \
handle##h(enum gssMaggotOp op, struct client *c)

/*
 *
 */

static int
HandleOP(GetVersionInfo)
{
    put32(c, GSSMAGGOTPROTOCOL);
    errx(1, "GetVersionInfo");
}

static int
HandleOP(GoodBye)
{
    struct handle *h = c->handles;
    unsigned int i = 0;

    while (h) {
	h = h->next;
	i++;
    }

    if (i)
	logmessage(c, __FILE__, __LINE__, 0,
		   "Did not toast all resources: %d", i);
    return 1;
}

static int
HandleOP(InitContext)
{
    OM_uint32 maj_stat, min_stat, ret_flags;
    int32_t hContext, hCred, flags;
    krb5_data target_name, in_token;
    int32_t new_context_id = 0, gsm_error = 0;
    krb5_data out_token = { 0 , NULL };

    gss_ctx_id_t ctx;
    gss_cred_id_t creds;
    gss_name_t gss_target_name;
    gss_buffer_desc input_token, output_token;
    gss_OID oid = GSS_C_NO_OID;
    gss_buffer_t input_token_ptr = GSS_C_NO_BUFFER;

    ret32(c, hContext);
    ret32(c, hCred);
    ret32(c, flags);
    retdata(c, target_name);
    retdata(c, in_token);

    logmessage(c, __FILE__, __LINE__, 0,
	       "targetname: <%.*s>", (int)target_name.length,
	       (char *)target_name.data);

    ctx = find_handle(c->handles, hContext, handle_context);
    if (ctx == NULL)
	hContext = 0;
    creds = find_handle(c->handles, hCred, handle_cred);
    if (creds == NULL)
	abort();

    input_token.length = target_name.length;
    input_token.value = target_name.data;

    maj_stat = gss_import_name(&min_stat,
			       &input_token,
			       GSS_KRB5_NT_PRINCIPAL_NAME,
			       &gss_target_name);
    if (GSS_ERROR(maj_stat)) {
	logmessage(c, __FILE__, __LINE__, 0,
		   "import name creds failed with: %d", maj_stat);
	gsm_error = convert_gss_to_gsm(maj_stat);
	goto out;
    }

    /* oid from flags */

    if (in_token.length) {
	input_token.length = in_token.length;
	input_token.value = in_token.data;
	input_token_ptr = &input_token;
	if (ctx == NULL)
	    krb5_errx(context, 1, "initcreds, context NULL, but not first req");
    } else {
	input_token.length = 0;
	input_token.value = NULL;
	if (ctx)
	    krb5_errx(context, 1, "initcreds, context not NULL, but first req");
    }

    if ((flags & GSS_C_DELEG_FLAG) != 0)
	logmessage(c, __FILE__, __LINE__, 0, "init_sec_context delegating");
    if ((flags & GSS_C_DCE_STYLE) != 0)
	logmessage(c, __FILE__, __LINE__, 0, "init_sec_context dce-style");

    maj_stat = gss_init_sec_context(&min_stat,
				    creds,
				    &ctx,
				    gss_target_name,
				    oid,
				    flags & 0x7f,
				    0,
				    NULL,
				    input_token_ptr,
				    NULL,
				    &output_token,
				    &ret_flags,
				    NULL);
    if (GSS_ERROR(maj_stat)) {
	if (hContext != 0)
	    del_handle(&c->handles, hContext);
	new_context_id = 0;
	logmessage(c, __FILE__, __LINE__, 0,
		   "gss_init_sec_context returns code: %d/%d",
		   maj_stat, min_stat);
    } else {
	if (input_token.length == 0)
	    new_context_id = add_handle(c, handle_context, ctx);
	else
	    new_context_id = hContext;
    }

    gsm_error = convert_gss_to_gsm(maj_stat);

    if (output_token.length) {
	out_token.data = output_token.value;
	out_token.length = output_token.length;
    }

out:
    logmessage(c, __FILE__, __LINE__, 0,
	       "InitContext return code: %d", gsm_error);

    put32(c, new_context_id);
    put32(c, gsm_error);
    putdata(c, out_token);

    gss_release_name(&min_stat, &gss_target_name);
    if (output_token.length)
	gss_release_buffer(&min_stat, &output_token);
    krb5_data_free(&in_token);
    krb5_data_free(&target_name);

    return 0;
}

static int
HandleOP(AcceptContext)
{
    OM_uint32 maj_stat, min_stat, ret_flags;
    int32_t hContext, deleg_hcred, flags;
    krb5_data in_token;
    int32_t new_context_id = 0, gsm_error = 0;
    krb5_data out_token = { 0 , NULL };

    gss_ctx_id_t ctx;
    gss_cred_id_t deleg_cred = GSS_C_NO_CREDENTIAL;
    gss_buffer_desc input_token, output_token;
    gss_buffer_t input_token_ptr = GSS_C_NO_BUFFER;

    ret32(c, hContext);
    ret32(c, flags);
    retdata(c, in_token);

    ctx = find_handle(c->handles, hContext, handle_context);
    if (ctx == NULL)
	hContext = 0;

    if (in_token.length) {
	input_token.length = in_token.length;
	input_token.value = in_token.data;
	input_token_ptr = &input_token;
    } else {
	input_token.length = 0;
	input_token.value = NULL;
    }

    maj_stat = gss_accept_sec_context(&min_stat,
				      &ctx,
				      GSS_C_NO_CREDENTIAL,
				      &input_token,
				      GSS_C_NO_CHANNEL_BINDINGS,
				      NULL,
				      NULL,
				      &output_token,
				      &ret_flags,
				      NULL,
				      &deleg_cred);
    if (GSS_ERROR(maj_stat)) {
	if (hContext != 0)
	    del_handle(&c->handles, hContext);
	logmessage(c, __FILE__, __LINE__, 0,
		   "gss_accept_sec_context returns code: %d/%d",
		   maj_stat, min_stat);
	new_context_id = 0;
    } else {
	if (hContext == 0)
	    new_context_id = add_handle(c, handle_context, ctx);
	else
	    new_context_id = hContext;
    }
    if (output_token.length) {
	out_token.data = output_token.value;
	out_token.length = output_token.length;
    }
    if ((ret_flags & GSS_C_DCE_STYLE) != 0)
	logmessage(c, __FILE__, __LINE__, 0, "accept_sec_context dce-style");
    if ((ret_flags & GSS_C_DELEG_FLAG) != 0) {
	deleg_hcred = add_handle(c, handle_cred, deleg_cred);
	logmessage(c, __FILE__, __LINE__, 0,
		   "accept_context delegated handle: %d", deleg_hcred);
    } else {
	gss_release_cred(&min_stat, &deleg_cred);
	deleg_hcred = 0;
    }


    gsm_error = convert_gss_to_gsm(maj_stat);

    put32(c, new_context_id);
    put32(c, gsm_error);
    putdata(c, out_token);
    put32(c, deleg_hcred);

    if (output_token.length)
	gss_release_buffer(&min_stat, &output_token);
    krb5_data_free(&in_token);

    return 0;
}

static int
HandleOP(ToastResource)
{
    int32_t handle;

    ret32(c, handle);
    logmessage(c, __FILE__, __LINE__, 0, "toasting %d", handle);
    del_handle(&c->handles, handle);
    put32(c, GSMERR_OK);

    return 0;
}

static int
HandleOP(AcquireCreds)
{
    char *name, *password;
    int32_t gsm_error, flags, handle = 0;
    krb5_principal principal = NULL;
    krb5_get_init_creds_opt *opt = NULL;
    krb5_error_code ret;

    retstring(c, name);
    retstring(c, password);
    ret32(c, flags);

    logmessage(c, __FILE__, __LINE__, 0,
	       "username: %s password: %s", name, password);

    ret = krb5_parse_name(context, name, &principal);
    if (ret) {
	gsm_error = convert_krb5_to_gsm(ret);
	goto out;
    }

    ret = krb5_get_init_creds_opt_alloc (context, &opt);
    if (ret)
	krb5_err(context, 1, ret, "krb5_get_init_creds_opt_alloc");

    krb5_get_init_creds_opt_set_pa_password(context, opt, password, NULL);

    gsm_error = acquire_cred(c, principal, opt, &handle);

out:
    logmessage(c, __FILE__, __LINE__, 0,
	       "AcquireCreds handle: %d return code: %d", handle, gsm_error);

    if (opt)
	krb5_get_init_creds_opt_free (context, opt);
    if (principal)
	krb5_free_principal(context, principal);
    free(name);
    free(password);

    put32(c, gsm_error);
    put32(c, handle);

    return 0;
}

static int
HandleOP(Sign)
{
    OM_uint32 maj_stat, min_stat;
    int32_t hContext, flags, seqno;
    krb5_data token;
    gss_ctx_id_t ctx;
    gss_buffer_desc input_token, output_token;

    ret32(c, hContext);
    ret32(c, flags);
    ret32(c, seqno);
    retdata(c, token);

    ctx = find_handle(c->handles, hContext, handle_context);
    if (ctx == NULL)
	errx(1, "sign: reference to unknown context");

    input_token.length = token.length;
    input_token.value = token.data;

    maj_stat = gss_get_mic(&min_stat, ctx, 0, &input_token,
			   &output_token);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_get_mic failed");

    krb5_data_free(&token);

    token.data = output_token.value;
    token.length = output_token.length;

    put32(c, 0); /* XXX fix gsm_error */
    putdata(c, token);

    gss_release_buffer(&min_stat, &output_token);

    return 0;
}

static int
HandleOP(Verify)
{
    OM_uint32 maj_stat, min_stat;
    int32_t hContext, flags, seqno;
    krb5_data msg, mic;
    gss_ctx_id_t ctx;
    gss_buffer_desc msg_token, mic_token;
    gss_qop_t qop;

    ret32(c, hContext);

    ctx = find_handle(c->handles, hContext, handle_context);
    if (ctx == NULL)
	errx(1, "verify: reference to unknown context");

    ret32(c, flags);
    ret32(c, seqno);
    retdata(c, msg);

    msg_token.length = msg.length;
    msg_token.value = msg.data;

    retdata(c, mic);

    mic_token.length = mic.length;
    mic_token.value = mic.data;

    maj_stat = gss_verify_mic(&min_stat, ctx, &msg_token,
			      &mic_token, &qop);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_verify_mic failed");

    krb5_data_free(&mic);
    krb5_data_free(&msg);

    put32(c, 0); /* XXX fix gsm_error */

    return 0;
}

static int
HandleOP(GetVersionAndCapabilities)
{
    int32_t cap = HAS_MONIKER;
    char name[256] = "unknown", *str;

    if (targetname)
	cap |= ISSERVER; /* is server */

#ifdef HAVE_UNAME
    {
	struct utsname ut;
	if (uname(&ut) == 0) {
	    snprintf(name, sizeof(name), "%s-%s-%s",
		     ut.sysname, ut.version, ut.machine);
	}
    }
#endif

    asprintf(&str, "gssmask %s %s", PACKAGE_STRING, name);

    put32(c, GSSMAGGOTPROTOCOL);
    put32(c, cap);
    putstring(c, str);
    free(str);

    return 0;
}

static int
HandleOP(GetTargetName)
{
    if (targetname)
	putstring(c, targetname);
    else
	putstring(c, "");
    return 0;
}

static int
HandleOP(SetLoggingSocket)
{
    int32_t portnum;
    int fd, ret;

    ret32(c, portnum);

    logmessage(c, __FILE__, __LINE__, 0,
	       "logging port on peer is: %d", (int)portnum);

    socket_set_port((struct sockaddr *)(&c->sa), htons(portnum));

    fd = socket(((struct sockaddr *)&c->sa)->sa_family, SOCK_STREAM, 0);
    if (fd < 0)
	return 0;

    ret = connect(fd, (struct sockaddr *)&c->sa, c->salen);
    if (ret < 0) {
	logmessage(c, __FILE__, __LINE__, 0, "failed connect to log port: %s",
		   strerror(errno));
	close(fd);
	return 0;
    }

    if (c->logging)
	krb5_storage_free(c->logging);
    c->logging = krb5_storage_from_fd(fd);
    close(fd);

    krb5_store_int32(c->logging, eLogSetMoniker);
    store_string(c->logging, c->moniker);

    logmessage(c, __FILE__, __LINE__, 0, "logging turned on");

    return 0;
}


static int
HandleOP(ChangePassword)
{
    errx(1, "ChangePassword");
}

static int
HandleOP(SetPasswordSelf)
{
    errx(1, "SetPasswordSelf");
}

static int
HandleOP(Wrap)
{
    OM_uint32 maj_stat, min_stat;
    int32_t hContext, flags, seqno;
    krb5_data token;
    gss_ctx_id_t ctx;
    gss_buffer_desc input_token, output_token;
    int conf_state;

    ret32(c, hContext);
    ret32(c, flags);
    ret32(c, seqno);
    retdata(c, token);

    ctx = find_handle(c->handles, hContext, handle_context);
    if (ctx == NULL)
	errx(1, "wrap: reference to unknown context");

    input_token.length = token.length;
    input_token.value = token.data;

    maj_stat = gss_wrap(&min_stat, ctx, flags, 0, &input_token,
			&conf_state, &output_token);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_wrap failed");

    krb5_data_free(&token);

    token.data = output_token.value;
    token.length = output_token.length;

    put32(c, 0); /* XXX fix gsm_error */
    putdata(c, token);

    gss_release_buffer(&min_stat, &output_token);

    return 0;
}


static int
HandleOP(Unwrap)
{
    OM_uint32 maj_stat, min_stat;
    int32_t hContext, flags, seqno;
    krb5_data token;
    gss_ctx_id_t ctx;
    gss_buffer_desc input_token, output_token;
    int conf_state;
    gss_qop_t qop_state;

    ret32(c, hContext);
    ret32(c, flags);
    ret32(c, seqno);
    retdata(c, token);

    ctx = find_handle(c->handles, hContext, handle_context);
    if (ctx == NULL)
	errx(1, "unwrap: reference to unknown context");

    input_token.length = token.length;
    input_token.value = token.data;

    maj_stat = gss_unwrap(&min_stat, ctx, &input_token,
			  &output_token, &conf_state, &qop_state);

    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_unwrap failed: %d/%d", maj_stat, min_stat);

    krb5_data_free(&token);
    if (maj_stat == GSS_S_COMPLETE) {
	token.data = output_token.value;
	token.length = output_token.length;
    } else {
	token.data = NULL;
	token.length = 0;
    }
    put32(c, 0); /* XXX fix gsm_error */
    putdata(c, token);

    if (maj_stat == GSS_S_COMPLETE)
	gss_release_buffer(&min_stat, &output_token);

    return 0;
}

static int
HandleOP(Encrypt)
{
    return handleWrap(op, c);
}

static int
HandleOP(Decrypt)
{
    return handleUnwrap(op, c);
}

static int
HandleOP(ConnectLoggingService2)
{
    errx(1, "ConnectLoggingService2");
}

static int
HandleOP(GetMoniker)
{
    putstring(c, c->moniker);
    return 0;
}

static int
HandleOP(CallExtension)
{
    errx(1, "CallExtension");
}

static int
HandleOP(AcquirePKInitCreds)
{
    int32_t flags;
    krb5_data pfxdata;
    char fn[] = "FILE:/tmp/pkcs12-creds-XXXXXXX";
    krb5_principal principal = NULL;
    int fd;

    ret32(c, flags);
    retdata(c, pfxdata);

    fd = mkstemp(fn + 5);
    if (fd < 0)
	errx(1, "mkstemp");

    net_write(fd, pfxdata.data, pfxdata.length);
    krb5_data_free(&pfxdata);
    close(fd);

    if (principal)
	krb5_free_principal(context, principal);

    put32(c, -1); /* hResource */
    put32(c, GSMERR_NOT_SUPPORTED);
    return 0;
}

static int
HandleOP(WrapExt)
{
    OM_uint32 maj_stat, min_stat;
    int32_t hContext, flags, bflags;
    krb5_data token, header, trailer;
    gss_ctx_id_t ctx;
    unsigned char *p;
    int conf_state, iov_len;
    gss_iov_buffer_desc iov[6];

    ret32(c, hContext);
    ret32(c, flags);
    ret32(c, bflags);
    retdata(c, header);
    retdata(c, token);
    retdata(c, trailer);

    ctx = find_handle(c->handles, hContext, handle_context);
    if (ctx == NULL)
	errx(1, "wrap: reference to unknown context");

    memset(&iov, 0, sizeof(iov));

    iov_len = sizeof(iov)/sizeof(iov[0]);

    if (bflags & WRAP_EXP_ONLY_HEADER)
	iov_len -= 2; /* skip trailer and padding, aka dce-style */

    iov[0].type = GSS_IOV_BUFFER_TYPE_HEADER | GSS_IOV_BUFFER_TYPE_FLAG_ALLOCATE;
    if (header.length != 0) {
	iov[1].type = GSS_IOV_BUFFER_TYPE_SIGN_ONLY;
	iov[1].buffer.length = header.length;
	iov[1].buffer.value = header.data;
    } else {
	iov[1].type = GSS_IOV_BUFFER_TYPE_EMPTY;
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
    }
    iov[4].type = GSS_IOV_BUFFER_TYPE_PADDING | GSS_IOV_BUFFER_TYPE_FLAG_ALLOCATE;
    iov[5].type = GSS_IOV_BUFFER_TYPE_TRAILER | GSS_IOV_BUFFER_TYPE_FLAG_ALLOCATE;

    maj_stat = gss_wrap_iov_length(&min_stat, ctx, flags, 0, &conf_state,
				   iov, iov_len);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_wrap_iov_length failed");

    maj_stat = gss_wrap_iov(&min_stat, ctx, flags, 0, &conf_state,
			    iov, iov_len);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_wrap_iov failed");

    krb5_data_free(&token);

    token.length = iov[0].buffer.length + iov[2].buffer.length + iov[4].buffer.length + iov[5].buffer.length;
    token.data = malloc(token.length);

    p = token.data;
    memcpy(p, iov[0].buffer.value, iov[0].buffer.length);
    p += iov[0].buffer.length;
    memcpy(p, iov[2].buffer.value, iov[2].buffer.length);
    p += iov[2].buffer.length;
    memcpy(p, iov[4].buffer.value, iov[4].buffer.length);
    p += iov[4].buffer.length;
    memcpy(p, iov[5].buffer.value, iov[5].buffer.length);
    p += iov[5].buffer.length;

    gss_release_iov_buffer(NULL, iov, iov_len);

    put32(c, 0); /* XXX fix gsm_error */
    putdata(c, token);

    free(token.data);

    return 0;
}


static int
HandleOP(UnwrapExt)
{
    OM_uint32 maj_stat, min_stat;
    int32_t hContext, flags, bflags;
    krb5_data token, header, trailer;
    gss_ctx_id_t ctx;
    gss_iov_buffer_desc iov[3];
    int conf_state, iov_len;
    gss_qop_t qop_state;

    ret32(c, hContext);
    ret32(c, flags);
    ret32(c, bflags);
    retdata(c, header);
    retdata(c, token);
    retdata(c, trailer);

    iov_len = sizeof(iov)/sizeof(iov[0]);

    if (bflags & WRAP_EXP_ONLY_HEADER)
	iov_len -= 1; /* skip trailer and padding, aka dce-style */

    ctx = find_handle(c->handles, hContext, handle_context);
    if (ctx == NULL)
	errx(1, "unwrap: reference to unknown context");

    if (header.length != 0) {
	iov[0].type = GSS_IOV_BUFFER_TYPE_SIGN_ONLY;
	iov[0].buffer.length = header.length;
	iov[0].buffer.value = header.data;
    } else {
	iov[0].type = GSS_IOV_BUFFER_TYPE_EMPTY;
    }
    iov[1].type = GSS_IOV_BUFFER_TYPE_DATA;
    iov[1].buffer.length = token.length;
    iov[1].buffer.value = token.data;

    if (trailer.length != 0) {
	iov[2].type = GSS_IOV_BUFFER_TYPE_SIGN_ONLY;
	iov[2].buffer.length = trailer.length;
	iov[2].buffer.value = trailer.data;
    } else {
	iov[2].type = GSS_IOV_BUFFER_TYPE_EMPTY;
    }

    maj_stat = gss_unwrap_iov(&min_stat, ctx, &conf_state, &qop_state,
			      iov, iov_len);

    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_unwrap failed: %d/%d", maj_stat, min_stat);

    if (maj_stat == GSS_S_COMPLETE) {
	token.data = iov[1].buffer.value;
	token.length = iov[1].buffer.length;
    } else {
	token.data = NULL;
	token.length = 0;
    }
    put32(c, 0); /* XXX fix gsm_error */
    putdata(c, token);

    return 0;
}

/*
 *
 */

struct handler {
    enum gssMaggotOp op;
    const char *name;
    int (*func)(enum gssMaggotOp, struct client *);
};

#define S(a) { e##a, #a, handle##a }

struct handler handlers[] = {
    S(GetVersionInfo),
    S(GoodBye),
    S(InitContext),
    S(AcceptContext),
    S(ToastResource),
    S(AcquireCreds),
    S(Encrypt),
    S(Decrypt),
    S(Sign),
    S(Verify),
    S(GetVersionAndCapabilities),
    S(GetTargetName),
    S(SetLoggingSocket),
    S(ChangePassword),
    S(SetPasswordSelf),
    S(Wrap),
    S(Unwrap),
    S(ConnectLoggingService2),
    S(GetMoniker),
    S(CallExtension),
    S(AcquirePKInitCreds),
    S(WrapExt),
    S(UnwrapExt),
};

#undef S

/*
 *
 */

static struct handler *
find_op(int32_t op)
{
    int i;

    for (i = 0; i < sizeof(handlers)/sizeof(handlers[0]); i++)
	if (handlers[i].op == op)
	    return &handlers[i];
    return NULL;
}

static struct client *
create_client(int fd, int port, const char *moniker)
{
    struct client *c;

    c = ecalloc(1, sizeof(*c));

    if (moniker) {
	c->moniker = estrdup(moniker);
    } else {
	char hostname[MAXHOSTNAMELEN];
	gethostname(hostname, sizeof(hostname));
	asprintf(&c->moniker, "gssmask: %s:%d", hostname, port);
    }

    {
	c->salen = sizeof(c->sa);
	getpeername(fd, (struct sockaddr *)&c->sa, &c->salen);

	getnameinfo((struct sockaddr *)&c->sa, c->salen,
		    c->servername, sizeof(c->servername),
		    NULL, 0, NI_NUMERICHOST);
    }

    c->sock = krb5_storage_from_fd(fd);
    if (c->sock == NULL)
	errx(1, "krb5_storage_from_fd");

    close(fd);

    return c;
}

static void
free_client(struct client *c)
{
    while(c->handles)
	del_handle(&c->handles, c->handles->idx);

    free(c->moniker);
    krb5_storage_free(c->sock);
    if (c->logging)
	krb5_storage_free(c->logging);
    free(c);
}


static void *
handleServer(void *ptr)
{
    struct handler *handler;
    struct client *c;
    int32_t op;

    c = (struct client *)ptr;


    while(1) {
	ret32(c, op);

	handler = find_op(op);
	if (handler == NULL) {
	    logmessage(c, __FILE__, __LINE__, 0,
		       "op %d not supported", (int)op);
	    exit(1);
	}

	logmessage(c, __FILE__, __LINE__, 0,
		   "---> Got op %s from server %s",
		   handler->name, c->servername);

	if ((handler->func)(handler->op, c))
	    break;
    }

    return NULL;
}


static char *port_str;
static int version_flag;
static int help_flag;
static char *logfile_str;
static char *moniker_str;

static int port = 4711;

struct getargs args[] = {
    { "spn",	0,   arg_string,	&targetname,	"This host's SPN",
      "service/host@REALM" },
    { "port",	'p', arg_string,	&port_str,	"Use this port",
      "number-of-service" },
    { "logfile", 0,  arg_string,	&logfile_str,	"logfile",
      "number-of-service" },
    { "moniker", 0,  arg_string,	&moniker_str,	"nickname",
      "name" },
    { "version", 0,  arg_flag,		&version_flag,	"Print version",
      NULL },
    { "help",	 0,  arg_flag,		&help_flag,	NULL,
      NULL }
};

static void
usage(int ret)
{
    arg_printusage (args,
		    sizeof(args) / sizeof(args[0]),
		    NULL,
		    "");
    exit (ret);
}

int
main(int argc, char **argv)
{
    int optidx	= 0;

    setprogname (argv[0]);

    if (getarg (args, sizeof(args) / sizeof(args[0]), argc, argv, &optidx))
	usage (1);

    if (help_flag)
	usage (0);

    if (version_flag) {
	print_version (NULL);
	return 0;
    }

    if (optidx != argc)
	usage (1);

    if (port_str) {
	char *ptr;

	port = strtol (port_str, &ptr, 10);
	if (port == 0 && ptr == port_str)
	    errx (1, "Bad port `%s'", port_str);
    }

    krb5_init_context(&context);

    {
	const char *lf = logfile_str;
	if (lf == NULL)
	    lf = "/dev/tty";

	logfile = fopen(lf, "w");
	if (logfile == NULL)
	    err(1, "error opening %s", lf);
    }

    mini_inetd(htons(port), NULL);
    fprintf(logfile, "connected\n");

    {
	struct client *c;

	c = create_client(0, port, moniker_str);
	/* close(0); */

	handleServer(c);

	free_client(c);
    }

    krb5_free_context(context);

    return 0;
}
