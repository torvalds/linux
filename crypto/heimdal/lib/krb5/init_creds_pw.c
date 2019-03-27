/*
 * Copyright (c) 1997 - 2008 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

#include "krb5_locl.h"

typedef struct krb5_get_init_creds_ctx {
    KDCOptions flags;
    krb5_creds cred;
    krb5_addresses *addrs;
    krb5_enctype *etypes;
    krb5_preauthtype *pre_auth_types;
    char *in_tkt_service;
    unsigned nonce;
    unsigned pk_nonce;

    krb5_data req_buffer;
    AS_REQ as_req;
    int pa_counter;

    /* password and keytab_data is freed on completion */
    char *password;
    krb5_keytab_key_proc_args *keytab_data;

    krb5_pointer *keyseed;
    krb5_s2k_proc keyproc;

    krb5_get_init_creds_tristate req_pac;

    krb5_pk_init_ctx pk_init_ctx;
    int ic_flags;

    int used_pa_types;
#define  USED_PKINIT	1
#define  USED_PKINIT_W2K	2
#define  USED_ENC_TS_GUESS	4
#define  USED_ENC_TS_INFO	8

    METHOD_DATA md;
    KRB_ERROR error;
    AS_REP as_rep;
    EncKDCRepPart enc_part;

    krb5_prompter_fct prompter;
    void *prompter_data;

    struct pa_info_data *ppaid;

} krb5_get_init_creds_ctx;


struct pa_info_data {
    krb5_enctype etype;
    krb5_salt salt;
    krb5_data *s2kparams;
};

static void
free_paid(krb5_context context, struct pa_info_data *ppaid)
{
    krb5_free_salt(context, ppaid->salt);
    if (ppaid->s2kparams)
	krb5_free_data(context, ppaid->s2kparams);
}

static krb5_error_code KRB5_CALLCONV
default_s2k_func(krb5_context context, krb5_enctype type,
		 krb5_const_pointer keyseed,
		 krb5_salt salt, krb5_data *s2kparms,
		 krb5_keyblock **key)
{
    krb5_error_code ret;
    krb5_data password;
    krb5_data opaque;

    _krb5_debug(context, 5, "krb5_get_init_creds: using default_s2k_func");

    password.data = rk_UNCONST(keyseed);
    password.length = strlen(keyseed);
    if (s2kparms)
	opaque = *s2kparms;
    else
	krb5_data_zero(&opaque);

    *key = malloc(sizeof(**key));
    if (*key == NULL)
	return ENOMEM;
    ret = krb5_string_to_key_data_salt_opaque(context, type, password,
					      salt, opaque, *key);
    if (ret) {
	free(*key);
	*key = NULL;
    }
    return ret;
}

static void
free_init_creds_ctx(krb5_context context, krb5_init_creds_context ctx)
{
    if (ctx->etypes)
	free(ctx->etypes);
    if (ctx->pre_auth_types)
	free (ctx->pre_auth_types);
    if (ctx->in_tkt_service)
	free(ctx->in_tkt_service);
    if (ctx->keytab_data)
	free(ctx->keytab_data);
    if (ctx->password) {
	memset(ctx->password, 0, strlen(ctx->password));
	free(ctx->password);
    }
    krb5_data_free(&ctx->req_buffer);
    krb5_free_cred_contents(context, &ctx->cred);
    free_METHOD_DATA(&ctx->md);
    free_AS_REP(&ctx->as_rep);
    free_EncKDCRepPart(&ctx->enc_part);
    free_KRB_ERROR(&ctx->error);
    free_AS_REQ(&ctx->as_req);
    if (ctx->ppaid) {
	free_paid(context, ctx->ppaid);
	free(ctx->ppaid);
    }
    memset(ctx, 0, sizeof(*ctx));
}

static int
get_config_time (krb5_context context,
		 const char *realm,
		 const char *name,
		 int def)
{
    int ret;

    ret = krb5_config_get_time (context, NULL,
				"realms",
				realm,
				name,
				NULL);
    if (ret >= 0)
	return ret;
    ret = krb5_config_get_time (context, NULL,
				"libdefaults",
				name,
				NULL);
    if (ret >= 0)
	return ret;
    return def;
}

static krb5_error_code
init_cred (krb5_context context,
	   krb5_creds *cred,
	   krb5_principal client,
	   krb5_deltat start_time,
	   krb5_get_init_creds_opt *options)
{
    krb5_error_code ret;
    int tmp;
    krb5_timestamp now;

    krb5_timeofday (context, &now);

    memset (cred, 0, sizeof(*cred));

    if (client)
	krb5_copy_principal(context, client, &cred->client);
    else {
	ret = krb5_get_default_principal (context,
					  &cred->client);
	if (ret)
	    goto out;
    }

    if (start_time)
	cred->times.starttime  = now + start_time;

    if (options->flags & KRB5_GET_INIT_CREDS_OPT_TKT_LIFE)
	tmp = options->tkt_life;
    else
	tmp = 10 * 60 * 60;
    cred->times.endtime = now + tmp;

    if ((options->flags & KRB5_GET_INIT_CREDS_OPT_RENEW_LIFE) &&
	options->renew_life > 0) {
	cred->times.renew_till = now + options->renew_life;
    }

    return 0;

out:
    krb5_free_cred_contents (context, cred);
    return ret;
}

/*
 * Print a message (str) to the user about the expiration in `lr'
 */

static void
report_expiration (krb5_context context,
		   krb5_prompter_fct prompter,
		   krb5_data *data,
		   const char *str,
		   time_t now)
{
    char *p = NULL;

    if (asprintf(&p, "%s%s", str, ctime(&now)) < 0 || p == NULL)
	return;
    (*prompter)(context, data, NULL, p, 0, NULL);
    free(p);
}

/*
 * Check the context, and in the case there is a expiration warning,
 * use the prompter to print the warning.
 *
 * @param context A Kerberos 5 context.
 * @param options An GIC options structure
 * @param ctx The krb5_init_creds_context check for expiration.
 */

static krb5_error_code
process_last_request(krb5_context context,
		     krb5_get_init_creds_opt *options,
		     krb5_init_creds_context ctx)
{
    krb5_const_realm realm;
    LastReq *lr;
    krb5_boolean reported = FALSE;
    krb5_timestamp sec;
    time_t t;
    size_t i;

    /*
     * First check if there is a API consumer.
     */

    realm = krb5_principal_get_realm (context, ctx->cred.client);
    lr = &ctx->enc_part.last_req;

    if (options && options->opt_private && options->opt_private->lr.func) {
	krb5_last_req_entry **lre;

	lre = calloc(lr->len + 1, sizeof(**lre));
	if (lre == NULL) {
	    krb5_set_error_message(context, ENOMEM,
				   N_("malloc: out of memory", ""));
	    return ENOMEM;
	}
	for (i = 0; i < lr->len; i++) {
	    lre[i] = calloc(1, sizeof(*lre[i]));
	    if (lre[i] == NULL)
		break;
	    lre[i]->lr_type = lr->val[i].lr_type;
	    lre[i]->value = lr->val[i].lr_value;
	}

	(*options->opt_private->lr.func)(context, lre,
					 options->opt_private->lr.ctx);

	for (i = 0; i < lr->len; i++)
	    free(lre[i]);
	free(lre);
    }

    /*
     * Now check if we should prompt the user
     */

    if (ctx->prompter == NULL)
        return 0;

    krb5_timeofday (context, &sec);

    t = sec + get_config_time (context,
			       realm,
			       "warn_pwexpire",
			       7 * 24 * 60 * 60);

    for (i = 0; i < lr->len; ++i) {
	if (lr->val[i].lr_value <= t) {
	    switch (abs(lr->val[i].lr_type)) {
	    case LR_PW_EXPTIME :
		report_expiration(context, ctx->prompter,
				  ctx->prompter_data,
				  "Your password will expire at ",
				  lr->val[i].lr_value);
		reported = TRUE;
		break;
	    case LR_ACCT_EXPTIME :
		report_expiration(context, ctx->prompter,
				  ctx->prompter_data,
				  "Your account will expire at ",
				  lr->val[i].lr_value);
		reported = TRUE;
		break;
	    }
	}
    }

    if (!reported
	&& ctx->enc_part.key_expiration
	&& *ctx->enc_part.key_expiration <= t) {
        report_expiration(context, ctx->prompter,
			  ctx->prompter_data,
			  "Your password/account will expire at ",
			  *ctx->enc_part.key_expiration);
    }
    return 0;
}

static krb5_addresses no_addrs = { 0, NULL };

static krb5_error_code
get_init_creds_common(krb5_context context,
		      krb5_principal client,
		      krb5_deltat start_time,
		      krb5_get_init_creds_opt *options,
		      krb5_init_creds_context ctx)
{
    krb5_get_init_creds_opt *default_opt = NULL;
    krb5_error_code ret;
    krb5_enctype *etypes;
    krb5_preauthtype *pre_auth_types;

    memset(ctx, 0, sizeof(*ctx));

    if (options == NULL) {
	const char *realm = krb5_principal_get_realm(context, client);

        krb5_get_init_creds_opt_alloc (context, &default_opt);
	options = default_opt;
	krb5_get_init_creds_opt_set_default_flags(context, NULL, realm, options);
    }

    if (options->opt_private) {
	if (options->opt_private->password) {
	    ret = krb5_init_creds_set_password(context, ctx,
					       options->opt_private->password);
	    if (ret)
		goto out;
	}

	ctx->keyproc = options->opt_private->key_proc;
	ctx->req_pac = options->opt_private->req_pac;
	ctx->pk_init_ctx = options->opt_private->pk_init_ctx;
	ctx->ic_flags = options->opt_private->flags;
    } else
	ctx->req_pac = KRB5_INIT_CREDS_TRISTATE_UNSET;

    if (ctx->keyproc == NULL)
	ctx->keyproc = default_s2k_func;

    /* Enterprise name implicitly turns on canonicalize */
    if ((ctx->ic_flags & KRB5_INIT_CREDS_CANONICALIZE) ||
	krb5_principal_get_type(context, client) == KRB5_NT_ENTERPRISE_PRINCIPAL)
	ctx->flags.canonicalize = 1;

    ctx->pre_auth_types = NULL;
    ctx->addrs = NULL;
    ctx->etypes = NULL;
    ctx->pre_auth_types = NULL;

    ret = init_cred(context, &ctx->cred, client, start_time, options);
    if (ret) {
	if (default_opt)
	    krb5_get_init_creds_opt_free(context, default_opt);
	return ret;
    }

    ret = krb5_init_creds_set_service(context, ctx, NULL);
    if (ret)
	goto out;

    if (options->flags & KRB5_GET_INIT_CREDS_OPT_FORWARDABLE)
	ctx->flags.forwardable = options->forwardable;

    if (options->flags & KRB5_GET_INIT_CREDS_OPT_PROXIABLE)
	ctx->flags.proxiable = options->proxiable;

    if (start_time)
	ctx->flags.postdated = 1;
    if (ctx->cred.times.renew_till)
	ctx->flags.renewable = 1;
    if (options->flags & KRB5_GET_INIT_CREDS_OPT_ADDRESS_LIST) {
	ctx->addrs = options->address_list;
    } else if (options->opt_private) {
	switch (options->opt_private->addressless) {
	case KRB5_INIT_CREDS_TRISTATE_UNSET:
#if KRB5_ADDRESSLESS_DEFAULT == TRUE
	    ctx->addrs = &no_addrs;
#else
	    ctx->addrs = NULL;
#endif
	    break;
	case KRB5_INIT_CREDS_TRISTATE_FALSE:
	    ctx->addrs = NULL;
	    break;
	case KRB5_INIT_CREDS_TRISTATE_TRUE:
	    ctx->addrs = &no_addrs;
	    break;
	}
    }
    if (options->flags & KRB5_GET_INIT_CREDS_OPT_ETYPE_LIST) {
	if (ctx->etypes)
	    free(ctx->etypes);

	etypes = malloc((options->etype_list_length + 1)
			* sizeof(krb5_enctype));
	if (etypes == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	    goto out;
	}
	memcpy (etypes, options->etype_list,
		options->etype_list_length * sizeof(krb5_enctype));
	etypes[options->etype_list_length] = ETYPE_NULL;
	ctx->etypes = etypes;
    }
    if (options->flags & KRB5_GET_INIT_CREDS_OPT_PREAUTH_LIST) {
	pre_auth_types = malloc((options->preauth_list_length + 1)
				* sizeof(krb5_preauthtype));
	if (pre_auth_types == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	    goto out;
	}
	memcpy (pre_auth_types, options->preauth_list,
		options->preauth_list_length * sizeof(krb5_preauthtype));
	pre_auth_types[options->preauth_list_length] = KRB5_PADATA_NONE;
	ctx->pre_auth_types = pre_auth_types;
    }
    if (options->flags & KRB5_GET_INIT_CREDS_OPT_ANONYMOUS)
	ctx->flags.request_anonymous = options->anonymous;
    if (default_opt)
        krb5_get_init_creds_opt_free(context, default_opt);
    return 0;
 out:
    if (default_opt)
	krb5_get_init_creds_opt_free(context, default_opt);
    return ret;
}

static krb5_error_code
change_password (krb5_context context,
		 krb5_principal client,
		 const char *password,
		 char *newpw,
		 size_t newpw_sz,
		 krb5_prompter_fct prompter,
		 void *data,
		 krb5_get_init_creds_opt *old_options)
{
    krb5_prompt prompts[2];
    krb5_error_code ret;
    krb5_creds cpw_cred;
    char buf1[BUFSIZ], buf2[BUFSIZ];
    krb5_data password_data[2];
    int result_code;
    krb5_data result_code_string;
    krb5_data result_string;
    char *p;
    krb5_get_init_creds_opt *options;

    memset (&cpw_cred, 0, sizeof(cpw_cred));

    ret = krb5_get_init_creds_opt_alloc(context, &options);
    if (ret)
        return ret;
    krb5_get_init_creds_opt_set_tkt_life (options, 60);
    krb5_get_init_creds_opt_set_forwardable (options, FALSE);
    krb5_get_init_creds_opt_set_proxiable (options, FALSE);
    if (old_options && old_options->flags & KRB5_GET_INIT_CREDS_OPT_PREAUTH_LIST)
	krb5_get_init_creds_opt_set_preauth_list (options,
						  old_options->preauth_list,
						  old_options->preauth_list_length);

    krb5_data_zero (&result_code_string);
    krb5_data_zero (&result_string);

    ret = krb5_get_init_creds_password (context,
					&cpw_cred,
					client,
					password,
					prompter,
					data,
					0,
					"kadmin/changepw",
					options);
    krb5_get_init_creds_opt_free(context, options);
    if (ret)
	goto out;

    for(;;) {
	password_data[0].data   = buf1;
	password_data[0].length = sizeof(buf1);

	prompts[0].hidden = 1;
	prompts[0].prompt = "New password: ";
	prompts[0].reply  = &password_data[0];
	prompts[0].type   = KRB5_PROMPT_TYPE_NEW_PASSWORD;

	password_data[1].data   = buf2;
	password_data[1].length = sizeof(buf2);

	prompts[1].hidden = 1;
	prompts[1].prompt = "Repeat new password: ";
	prompts[1].reply  = &password_data[1];
	prompts[1].type   = KRB5_PROMPT_TYPE_NEW_PASSWORD_AGAIN;

	ret = (*prompter) (context, data, NULL, "Changing password",
			   2, prompts);
	if (ret) {
	    memset (buf1, 0, sizeof(buf1));
	    memset (buf2, 0, sizeof(buf2));
	    goto out;
	}

	if (strcmp (buf1, buf2) == 0)
	    break;
	memset (buf1, 0, sizeof(buf1));
	memset (buf2, 0, sizeof(buf2));
    }

    ret = krb5_set_password (context,
			     &cpw_cred,
			     buf1,
			     client,
			     &result_code,
			     &result_code_string,
			     &result_string);
    if (ret)
	goto out;
    if (asprintf(&p, "%s: %.*s\n",
		 result_code ? "Error" : "Success",
		 (int)result_string.length,
		 result_string.length > 0 ? (char*)result_string.data : "") < 0)
    {
	ret = ENOMEM;
	goto out;
    }

    /* return the result */
    (*prompter) (context, data, NULL, p, 0, NULL);

    free (p);
    if (result_code == 0) {
	strlcpy (newpw, buf1, newpw_sz);
	ret = 0;
    } else {
	ret = ENOTTY;
	krb5_set_error_message(context, ret,
			       N_("failed changing password", ""));
    }

out:
    memset (buf1, 0, sizeof(buf1));
    memset (buf2, 0, sizeof(buf2));
    krb5_data_free (&result_string);
    krb5_data_free (&result_code_string);
    krb5_free_cred_contents (context, &cpw_cred);
    return ret;
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_keyblock_key_proc (krb5_context context,
			krb5_keytype type,
			krb5_data *salt,
			krb5_const_pointer keyseed,
			krb5_keyblock **key)
{
    return krb5_copy_keyblock (context, keyseed, key);
}

/*
 *
 */

static krb5_error_code
init_as_req (krb5_context context,
	     KDCOptions opts,
	     const krb5_creds *creds,
	     const krb5_addresses *addrs,
	     const krb5_enctype *etypes,
	     AS_REQ *a)
{
    krb5_error_code ret;

    memset(a, 0, sizeof(*a));

    a->pvno = 5;
    a->msg_type = krb_as_req;
    a->req_body.kdc_options = opts;
    a->req_body.cname = malloc(sizeof(*a->req_body.cname));
    if (a->req_body.cname == NULL) {
	ret = ENOMEM;
	krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	goto fail;
    }
    a->req_body.sname = malloc(sizeof(*a->req_body.sname));
    if (a->req_body.sname == NULL) {
	ret = ENOMEM;
	krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	goto fail;
    }

    ret = _krb5_principal2principalname (a->req_body.cname, creds->client);
    if (ret)
	goto fail;
    ret = copy_Realm(&creds->client->realm, &a->req_body.realm);
    if (ret)
	goto fail;

    ret = _krb5_principal2principalname (a->req_body.sname, creds->server);
    if (ret)
	goto fail;

    if(creds->times.starttime) {
	a->req_body.from = malloc(sizeof(*a->req_body.from));
	if (a->req_body.from == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	    goto fail;
	}
	*a->req_body.from = creds->times.starttime;
    }
    if(creds->times.endtime){
	ALLOC(a->req_body.till, 1);
	*a->req_body.till = creds->times.endtime;
    }
    if(creds->times.renew_till){
	a->req_body.rtime = malloc(sizeof(*a->req_body.rtime));
	if (a->req_body.rtime == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	    goto fail;
	}
	*a->req_body.rtime = creds->times.renew_till;
    }
    a->req_body.nonce = 0;
    ret = _krb5_init_etype(context,
			   KRB5_PDU_AS_REQUEST,
			   &a->req_body.etype.len,
			   &a->req_body.etype.val,
			   etypes);
    if (ret)
	goto fail;

    /*
     * This means no addresses
     */

    if (addrs && addrs->len == 0) {
	a->req_body.addresses = NULL;
    } else {
	a->req_body.addresses = malloc(sizeof(*a->req_body.addresses));
	if (a->req_body.addresses == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	    goto fail;
	}

	if (addrs)
	    ret = krb5_copy_addresses(context, addrs, a->req_body.addresses);
	else {
	    ret = krb5_get_all_client_addrs (context, a->req_body.addresses);
	    if(ret == 0 && a->req_body.addresses->len == 0) {
		free(a->req_body.addresses);
		a->req_body.addresses = NULL;
	    }
	}
	if (ret)
	    goto fail;
    }

    a->req_body.enc_authorization_data = NULL;
    a->req_body.additional_tickets = NULL;

    a->padata = NULL;

    return 0;
 fail:
    free_AS_REQ(a);
    memset(a, 0, sizeof(*a));
    return ret;
}


static krb5_error_code
set_paid(struct pa_info_data *paid, krb5_context context,
	 krb5_enctype etype,
	 krb5_salttype salttype, void *salt_string, size_t salt_len,
	 krb5_data *s2kparams)
{
    paid->etype = etype;
    paid->salt.salttype = salttype;
    paid->salt.saltvalue.data = malloc(salt_len + 1);
    if (paid->salt.saltvalue.data == NULL) {
	krb5_clear_error_message(context);
	return ENOMEM;
    }
    memcpy(paid->salt.saltvalue.data, salt_string, salt_len);
    ((char *)paid->salt.saltvalue.data)[salt_len] = '\0';
    paid->salt.saltvalue.length = salt_len;
    if (s2kparams) {
	krb5_error_code ret;

	ret = krb5_copy_data(context, s2kparams, &paid->s2kparams);
	if (ret) {
	    krb5_clear_error_message(context);
	    krb5_free_salt(context, paid->salt);
	    return ret;
	}
    } else
	paid->s2kparams = NULL;

    return 0;
}

static struct pa_info_data *
pa_etype_info2(krb5_context context,
	       const krb5_principal client,
	       const AS_REQ *asreq,
	       struct pa_info_data *paid,
	       heim_octet_string *data)
{
    krb5_error_code ret;
    ETYPE_INFO2 e;
    size_t sz;
    size_t i, j;

    memset(&e, 0, sizeof(e));
    ret = decode_ETYPE_INFO2(data->data, data->length, &e, &sz);
    if (ret)
	goto out;
    if (e.len == 0)
	goto out;
    for (j = 0; j < asreq->req_body.etype.len; j++) {
	for (i = 0; i < e.len; i++) {
	    if (asreq->req_body.etype.val[j] == e.val[i].etype) {
		krb5_salt salt;
		if (e.val[i].salt == NULL)
		    ret = krb5_get_pw_salt(context, client, &salt);
		else {
		    salt.saltvalue.data = *e.val[i].salt;
		    salt.saltvalue.length = strlen(*e.val[i].salt);
		    ret = 0;
		}
		if (ret == 0)
		    ret = set_paid(paid, context, e.val[i].etype,
				   KRB5_PW_SALT,
				   salt.saltvalue.data,
				   salt.saltvalue.length,
				   e.val[i].s2kparams);
		if (e.val[i].salt == NULL)
		    krb5_free_salt(context, salt);
		if (ret == 0) {
		    free_ETYPE_INFO2(&e);
		    return paid;
		}
	    }
	}
    }
 out:
    free_ETYPE_INFO2(&e);
    return NULL;
}

static struct pa_info_data *
pa_etype_info(krb5_context context,
	      const krb5_principal client,
	      const AS_REQ *asreq,
	      struct pa_info_data *paid,
	      heim_octet_string *data)
{
    krb5_error_code ret;
    ETYPE_INFO e;
    size_t sz;
    size_t i, j;

    memset(&e, 0, sizeof(e));
    ret = decode_ETYPE_INFO(data->data, data->length, &e, &sz);
    if (ret)
	goto out;
    if (e.len == 0)
	goto out;
    for (j = 0; j < asreq->req_body.etype.len; j++) {
	for (i = 0; i < e.len; i++) {
	    if (asreq->req_body.etype.val[j] == e.val[i].etype) {
		krb5_salt salt;
		salt.salttype = KRB5_PW_SALT;
		if (e.val[i].salt == NULL)
		    ret = krb5_get_pw_salt(context, client, &salt);
		else {
		    salt.saltvalue = *e.val[i].salt;
		    ret = 0;
		}
		if (e.val[i].salttype)
		    salt.salttype = *e.val[i].salttype;
		if (ret == 0) {
		    ret = set_paid(paid, context, e.val[i].etype,
				   salt.salttype,
				   salt.saltvalue.data,
				   salt.saltvalue.length,
				   NULL);
		    if (e.val[i].salt == NULL)
			krb5_free_salt(context, salt);
		}
		if (ret == 0) {
		    free_ETYPE_INFO(&e);
		    return paid;
		}
	    }
	}
    }
 out:
    free_ETYPE_INFO(&e);
    return NULL;
}

static struct pa_info_data *
pa_pw_or_afs3_salt(krb5_context context,
		   const krb5_principal client,
		   const AS_REQ *asreq,
		   struct pa_info_data *paid,
		   heim_octet_string *data)
{
    krb5_error_code ret;
    if (paid->etype == ENCTYPE_NULL)
	return NULL;
    ret = set_paid(paid, context,
		   paid->etype,
		   paid->salt.salttype,
		   data->data,
		   data->length,
		   NULL);
    if (ret)
	return NULL;
    return paid;
}


struct pa_info {
    krb5_preauthtype type;
    struct pa_info_data *(*salt_info)(krb5_context,
				      const krb5_principal,
				      const AS_REQ *,
				      struct pa_info_data *,
				      heim_octet_string *);
};

static struct pa_info pa_prefs[] = {
    { KRB5_PADATA_ETYPE_INFO2, pa_etype_info2 },
    { KRB5_PADATA_ETYPE_INFO, pa_etype_info },
    { KRB5_PADATA_PW_SALT, pa_pw_or_afs3_salt },
    { KRB5_PADATA_AFS3_SALT, pa_pw_or_afs3_salt }
};

static PA_DATA *
find_pa_data(const METHOD_DATA *md, unsigned type)
{
    size_t i;
    if (md == NULL)
	return NULL;
    for (i = 0; i < md->len; i++)
	if (md->val[i].padata_type == type)
	    return &md->val[i];
    return NULL;
}

static struct pa_info_data *
process_pa_info(krb5_context context,
		const krb5_principal client,
		const AS_REQ *asreq,
		struct pa_info_data *paid,
		METHOD_DATA *md)
{
    struct pa_info_data *p = NULL;
    size_t i;

    for (i = 0; p == NULL && i < sizeof(pa_prefs)/sizeof(pa_prefs[0]); i++) {
	PA_DATA *pa = find_pa_data(md, pa_prefs[i].type);
	if (pa == NULL)
	    continue;
	paid->salt.salttype = (krb5_salttype)pa_prefs[i].type;
	p = (*pa_prefs[i].salt_info)(context, client, asreq,
				     paid, &pa->padata_value);
    }
    return p;
}

static krb5_error_code
make_pa_enc_timestamp(krb5_context context, METHOD_DATA *md,
		      krb5_enctype etype, krb5_keyblock *key)
{
    PA_ENC_TS_ENC p;
    unsigned char *buf;
    size_t buf_size;
    size_t len = 0;
    EncryptedData encdata;
    krb5_error_code ret;
    int32_t usec;
    int usec2;
    krb5_crypto crypto;

    krb5_us_timeofday (context, &p.patimestamp, &usec);
    usec2         = usec;
    p.pausec      = &usec2;

    ASN1_MALLOC_ENCODE(PA_ENC_TS_ENC, buf, buf_size, &p, &len, ret);
    if (ret)
	return ret;
    if(buf_size != len)
	krb5_abortx(context, "internal error in ASN.1 encoder");

    ret = krb5_crypto_init(context, key, 0, &crypto);
    if (ret) {
	free(buf);
	return ret;
    }
    ret = krb5_encrypt_EncryptedData(context,
				     crypto,
				     KRB5_KU_PA_ENC_TIMESTAMP,
				     buf,
				     len,
				     0,
				     &encdata);
    free(buf);
    krb5_crypto_destroy(context, crypto);
    if (ret)
	return ret;

    ASN1_MALLOC_ENCODE(EncryptedData, buf, buf_size, &encdata, &len, ret);
    free_EncryptedData(&encdata);
    if (ret)
	return ret;
    if(buf_size != len)
	krb5_abortx(context, "internal error in ASN.1 encoder");

    ret = krb5_padata_add(context, md, KRB5_PADATA_ENC_TIMESTAMP, buf, len);
    if (ret)
	free(buf);
    return ret;
}

static krb5_error_code
add_enc_ts_padata(krb5_context context,
		  METHOD_DATA *md,
		  krb5_principal client,
		  krb5_s2k_proc keyproc,
		  krb5_const_pointer keyseed,
		  krb5_enctype *enctypes,
		  unsigned netypes,
		  krb5_salt *salt,
		  krb5_data *s2kparams)
{
    krb5_error_code ret;
    krb5_salt salt2;
    krb5_enctype *ep;
    size_t i;

    if(salt == NULL) {
	/* default to standard salt */
	ret = krb5_get_pw_salt (context, client, &salt2);
	if (ret)
	    return ret;
	salt = &salt2;
    }
    if (!enctypes) {
	enctypes = context->etypes;
	netypes = 0;
	for (ep = enctypes; *ep != ETYPE_NULL; ep++)
	    netypes++;
    }

    for (i = 0; i < netypes; ++i) {
	krb5_keyblock *key;

	_krb5_debug(context, 5, "krb5_get_init_creds: using ENC-TS with enctype %d", enctypes[i]);

	ret = (*keyproc)(context, enctypes[i], keyseed,
			 *salt, s2kparams, &key);
	if (ret)
	    continue;
	ret = make_pa_enc_timestamp (context, md, enctypes[i], key);
	krb5_free_keyblock (context, key);
	if (ret)
	    return ret;
    }
    if(salt == &salt2)
	krb5_free_salt(context, salt2);
    return 0;
}

static krb5_error_code
pa_data_to_md_ts_enc(krb5_context context,
		     const AS_REQ *a,
		     const krb5_principal client,
		     krb5_get_init_creds_ctx *ctx,
		     struct pa_info_data *ppaid,
		     METHOD_DATA *md)
{
    if (ctx->keyproc == NULL || ctx->keyseed == NULL)
	return 0;

    if (ppaid) {
	add_enc_ts_padata(context, md, client,
			  ctx->keyproc, ctx->keyseed,
			  &ppaid->etype, 1,
			  &ppaid->salt, ppaid->s2kparams);
    } else {
	krb5_salt salt;

	_krb5_debug(context, 5, "krb5_get_init_creds: pa-info not found, guessing salt");

	/* make a v5 salted pa-data */
	add_enc_ts_padata(context, md, client,
			  ctx->keyproc, ctx->keyseed,
			  a->req_body.etype.val, a->req_body.etype.len,
			  NULL, NULL);

	/* make a v4 salted pa-data */
	salt.salttype = KRB5_PW_SALT;
	krb5_data_zero(&salt.saltvalue);
	add_enc_ts_padata(context, md, client,
			  ctx->keyproc, ctx->keyseed,
			  a->req_body.etype.val, a->req_body.etype.len,
			  &salt, NULL);
    }
    return 0;
}

static krb5_error_code
pa_data_to_key_plain(krb5_context context,
		     const krb5_principal client,
		     krb5_get_init_creds_ctx *ctx,
		     krb5_salt salt,
		     krb5_data *s2kparams,
		     krb5_enctype etype,
		     krb5_keyblock **key)
{
    krb5_error_code ret;

    ret = (*ctx->keyproc)(context, etype, ctx->keyseed,
			   salt, s2kparams, key);
    return ret;
}


static krb5_error_code
pa_data_to_md_pkinit(krb5_context context,
		     const AS_REQ *a,
		     const krb5_principal client,
		     int win2k,
		     krb5_get_init_creds_ctx *ctx,
		     METHOD_DATA *md)
{
    if (ctx->pk_init_ctx == NULL)
	return 0;
#ifdef PKINIT
    return _krb5_pk_mk_padata(context,
			      ctx->pk_init_ctx,
			      ctx->ic_flags,
			      win2k,
			      &a->req_body,
			      ctx->pk_nonce,
			      md);
#else
    krb5_set_error_message(context, EINVAL,
			   N_("no support for PKINIT compiled in", ""));
    return EINVAL;
#endif
}

static krb5_error_code
pa_data_add_pac_request(krb5_context context,
			krb5_get_init_creds_ctx *ctx,
			METHOD_DATA *md)
{
    size_t len = 0, length;
    krb5_error_code ret;
    PA_PAC_REQUEST req;
    void *buf;

    switch (ctx->req_pac) {
    case KRB5_INIT_CREDS_TRISTATE_UNSET:
	return 0; /* don't bother */
    case KRB5_INIT_CREDS_TRISTATE_TRUE:
	req.include_pac = 1;
	break;
    case KRB5_INIT_CREDS_TRISTATE_FALSE:
	req.include_pac = 0;
    }

    ASN1_MALLOC_ENCODE(PA_PAC_REQUEST, buf, length,
		       &req, &len, ret);
    if (ret)
	return ret;
    if(len != length)
	krb5_abortx(context, "internal error in ASN.1 encoder");

    ret = krb5_padata_add(context, md, KRB5_PADATA_PA_PAC_REQUEST, buf, len);
    if (ret)
	free(buf);

    return 0;
}

/*
 * Assumes caller always will free `out_md', even on error.
 */

static krb5_error_code
process_pa_data_to_md(krb5_context context,
		      const krb5_creds *creds,
		      const AS_REQ *a,
		      krb5_get_init_creds_ctx *ctx,
		      METHOD_DATA *in_md,
		      METHOD_DATA **out_md,
		      krb5_prompter_fct prompter,
		      void *prompter_data)
{
    krb5_error_code ret;

    ALLOC(*out_md, 1);
    if (*out_md == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    (*out_md)->len = 0;
    (*out_md)->val = NULL;

    if (_krb5_have_debug(context, 5)) {
	unsigned i;
	_krb5_debug(context, 5, "KDC send %d patypes", in_md->len);
	for (i = 0; i < in_md->len; i++)
	    _krb5_debug(context, 5, "KDC send PA-DATA type: %d", in_md->val[i].padata_type);
    }

    /*
     * Make sure we don't sent both ENC-TS and PK-INIT pa data, no
     * need to expose our password protecting our PKCS12 key.
     */

    if (ctx->pk_init_ctx) {

 	_krb5_debug(context, 5, "krb5_get_init_creds: "
		    "prepareing PKINIT padata (%s)",
 		    (ctx->used_pa_types & USED_PKINIT_W2K) ? "win2k" : "ietf");

 	if (ctx->used_pa_types & USED_PKINIT_W2K) {
 	    krb5_set_error_message(context, KRB5_GET_IN_TKT_LOOP,
 				   "Already tried pkinit, looping");
 	    return KRB5_GET_IN_TKT_LOOP;
 	}

	ret = pa_data_to_md_pkinit(context, a, creds->client,
				   (ctx->used_pa_types & USED_PKINIT),
				   ctx, *out_md);
	if (ret)
	    return ret;

	if (ctx->used_pa_types & USED_PKINIT)
	    ctx->used_pa_types |= USED_PKINIT_W2K;
 	else
 	    ctx->used_pa_types |= USED_PKINIT;

    } else if (in_md->len != 0) {
	struct pa_info_data *paid, *ppaid;
 	unsigned flag;

	paid = calloc(1, sizeof(*paid));

	paid->etype = ENCTYPE_NULL;
	ppaid = process_pa_info(context, creds->client, a, paid, in_md);

 	if (ppaid)
 	    flag = USED_ENC_TS_INFO;
 	else
 	    flag = USED_ENC_TS_GUESS;

 	if (ctx->used_pa_types & flag) {
 	    if (ppaid)
 		free_paid(context, ppaid);
 	    krb5_set_error_message(context, KRB5_GET_IN_TKT_LOOP,
 				   "Already tried ENC-TS-%s, looping",
 				   flag == USED_ENC_TS_INFO ? "info" : "guess");
 	    return KRB5_GET_IN_TKT_LOOP;
 	}

	pa_data_to_md_ts_enc(context, a, creds->client, ctx, ppaid, *out_md);

	ctx->used_pa_types |= flag;

	if (ppaid) {
	    if (ctx->ppaid) {
		free_paid(context, ctx->ppaid);
		free(ctx->ppaid);
	    }
	    ctx->ppaid = ppaid;
	} else
	    free(paid);
    }

    pa_data_add_pac_request(context, ctx, *out_md);

    if ((*out_md)->len == 0) {
	free(*out_md);
	*out_md = NULL;
    }

    return 0;
}

static krb5_error_code
process_pa_data_to_key(krb5_context context,
		       krb5_get_init_creds_ctx *ctx,
		       krb5_creds *creds,
		       AS_REQ *a,
		       AS_REP *rep,
		       const krb5_krbhst_info *hi,
		       krb5_keyblock **key)
{
    struct pa_info_data paid, *ppaid = NULL;
    krb5_error_code ret;
    krb5_enctype etype;
    PA_DATA *pa;

    memset(&paid, 0, sizeof(paid));

    etype = rep->enc_part.etype;

    if (rep->padata) {
	paid.etype = etype;
	ppaid = process_pa_info(context, creds->client, a, &paid,
				rep->padata);
    }
    if (ppaid == NULL)
	ppaid = ctx->ppaid;
    if (ppaid == NULL) {
	ret = krb5_get_pw_salt (context, creds->client, &paid.salt);
	if (ret)
	    return ret;
	paid.etype = etype;
	paid.s2kparams = NULL;
	ppaid = &paid;
    }

    pa = NULL;
    if (rep->padata) {
	int idx = 0;
	pa = krb5_find_padata(rep->padata->val,
			      rep->padata->len,
			      KRB5_PADATA_PK_AS_REP,
			      &idx);
	if (pa == NULL) {
	    idx = 0;
	    pa = krb5_find_padata(rep->padata->val,
				  rep->padata->len,
				  KRB5_PADATA_PK_AS_REP_19,
				  &idx);
	}
    }
    if (pa && ctx->pk_init_ctx) {
#ifdef PKINIT
	_krb5_debug(context, 5, "krb5_get_init_creds: using PKINIT");

	ret = _krb5_pk_rd_pa_reply(context,
				   a->req_body.realm,
				   ctx->pk_init_ctx,
				   etype,
				   hi,
				   ctx->pk_nonce,
				   &ctx->req_buffer,
				   pa,
				   key);
#else
	ret = EINVAL;
	krb5_set_error_message(context, ret, N_("no support for PKINIT compiled in", ""));
#endif
    } else if (ctx->keyseed) {
 	_krb5_debug(context, 5, "krb5_get_init_creds: using keyproc");
	ret = pa_data_to_key_plain(context, creds->client, ctx,
				   ppaid->salt, ppaid->s2kparams, etype, key);
    } else {
	ret = EINVAL;
	krb5_set_error_message(context, ret, N_("No usable pa data type", ""));
    }

    free_paid(context, &paid);
    return ret;
}

/**
 * Start a new context to get a new initial credential.
 *
 * @param context A Kerberos 5 context.
 * @param client The Kerberos principal to get the credential for, if
 *     NULL is given, the default principal is used as determined by
 *     krb5_get_default_principal().
 * @param prompter
 * @param prompter_data
 * @param start_time the time the ticket should start to be valid or 0 for now.
 * @param options a options structure, can be NULL for default options.
 * @param rctx A new allocated free with krb5_init_creds_free().
 *
 * @return 0 for success or an Kerberos 5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_credential
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_init(krb5_context context,
		     krb5_principal client,
		     krb5_prompter_fct prompter,
		     void *prompter_data,
		     krb5_deltat start_time,
		     krb5_get_init_creds_opt *options,
		     krb5_init_creds_context *rctx)
{
    krb5_init_creds_context ctx;
    krb5_error_code ret;

    *rctx = NULL;

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    ret = get_init_creds_common(context, client, start_time, options, ctx);
    if (ret) {
	free(ctx);
	return ret;
    }

    /* Set a new nonce. */
    krb5_generate_random_block (&ctx->nonce, sizeof(ctx->nonce));
    ctx->nonce &= 0x7fffffff;
    /* XXX these just needs to be the same when using Windows PK-INIT */
    ctx->pk_nonce = ctx->nonce;

    ctx->prompter = prompter;
    ctx->prompter_data = prompter_data;

    *rctx = ctx;

    return ret;
}

/**
 * Sets the service that the is requested. This call is only neede for
 * special initial tickets, by default the a krbtgt is fetched in the default realm.
 *
 * @param context a Kerberos 5 context.
 * @param ctx a krb5_init_creds_context context.
 * @param service the service given as a string, for example
 *        "kadmind/admin". If NULL, the default krbtgt in the clients
 *        realm is set.
 *
 * @return 0 for success, or an Kerberos 5 error code, see krb5_get_error_message().
 * @ingroup krb5_credential
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_set_service(krb5_context context,
			    krb5_init_creds_context ctx,
			    const char *service)
{
    krb5_const_realm client_realm;
    krb5_principal principal;
    krb5_error_code ret;

    client_realm = krb5_principal_get_realm (context, ctx->cred.client);

    if (service) {
	ret = krb5_parse_name (context, service, &principal);
	if (ret)
	    return ret;
	krb5_principal_set_realm (context, principal, client_realm);
    } else {
	ret = krb5_make_principal(context, &principal,
				  client_realm, KRB5_TGS_NAME, client_realm,
				  NULL);
	if (ret)
	    return ret;
    }

    /*
     * This is for Windows RODC that are picky about what name type
     * the server principal have, and the really strange part is that
     * they are picky about the AS-REQ name type and not the TGS-REQ
     * later. Oh well.
     */

    if (krb5_principal_is_krbtgt(context, principal))
	krb5_principal_set_type(context, principal, KRB5_NT_SRV_INST);

    krb5_free_principal(context, ctx->cred.server);
    ctx->cred.server = principal;

    return 0;
}

/**
 * Sets the password that will use for the request.
 *
 * @param context a Kerberos 5 context.
 * @param ctx ctx krb5_init_creds_context context.
 * @param password the password to use.
 *
 * @return 0 for success, or an Kerberos 5 error code, see krb5_get_error_message().
 * @ingroup krb5_credential
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_set_password(krb5_context context,
			     krb5_init_creds_context ctx,
			     const char *password)
{
    if (ctx->password) {
	memset(ctx->password, 0, strlen(ctx->password));
	free(ctx->password);
    }
    if (password) {
	ctx->password = strdup(password);
	if (ctx->password == NULL) {
	    krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	    return ENOMEM;
	}
	ctx->keyseed = (void *) ctx->password;
    } else {
	ctx->keyseed = NULL;
	ctx->password = NULL;
    }

    return 0;
}

static krb5_error_code KRB5_CALLCONV
keytab_key_proc(krb5_context context, krb5_enctype enctype,
		krb5_const_pointer keyseed,
		krb5_salt salt, krb5_data *s2kparms,
		krb5_keyblock **key)
{
    krb5_keytab_key_proc_args *args  = rk_UNCONST(keyseed);
    krb5_keytab keytab = args->keytab;
    krb5_principal principal = args->principal;
    krb5_error_code ret;
    krb5_keytab real_keytab;
    krb5_keytab_entry entry;

    if(keytab == NULL)
	krb5_kt_default(context, &real_keytab);
    else
	real_keytab = keytab;

    ret = krb5_kt_get_entry (context, real_keytab, principal,
			     0, enctype, &entry);

    if (keytab == NULL)
	krb5_kt_close (context, real_keytab);

    if (ret)
	return ret;

    ret = krb5_copy_keyblock (context, &entry.keyblock, key);
    krb5_kt_free_entry(context, &entry);
    return ret;
}


/**
 * Set the keytab to use for authentication.
 *
 * @param context a Kerberos 5 context.
 * @param ctx ctx krb5_init_creds_context context.
 * @param keytab the keytab to read the key from.
 *
 * @return 0 for success, or an Kerberos 5 error code, see krb5_get_error_message().
 * @ingroup krb5_credential
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_set_keytab(krb5_context context,
			   krb5_init_creds_context ctx,
			   krb5_keytab keytab)
{
    krb5_keytab_key_proc_args *a;
    krb5_keytab_entry entry;
    krb5_kt_cursor cursor;
    krb5_enctype *etypes = NULL;
    krb5_error_code ret;
    size_t netypes = 0;
    int kvno = 0;

    a = malloc(sizeof(*a));
    if (a == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    a->principal = ctx->cred.client;
    a->keytab    = keytab;

    ctx->keytab_data = a;
    ctx->keyseed = (void *)a;
    ctx->keyproc = keytab_key_proc;

    /*
     * We need to the KDC what enctypes we support for this keytab,
     * esp if the keytab is really a password based entry, then the
     * KDC might have more enctypes in the database then what we have
     * in the keytab.
     */

    ret = krb5_kt_start_seq_get(context, keytab, &cursor);
    if(ret)
	goto out;

    while(krb5_kt_next_entry(context, keytab, &entry, &cursor) == 0){
	void *ptr;

	if (!krb5_principal_compare(context, entry.principal, ctx->cred.client))
	    goto next;

	/* check if we ahve this kvno already */
	if (entry.vno > kvno) {
	    /* remove old list of etype */
	    if (etypes)
		free(etypes);
	    etypes = NULL;
	    netypes = 0;
	    kvno = entry.vno;
	} else if (entry.vno != kvno)
	    goto next;

	/* check if enctype is supported */
	if (krb5_enctype_valid(context, entry.keyblock.keytype) != 0)
	    goto next;

	/* add enctype to supported list */
	ptr = realloc(etypes, sizeof(etypes[0]) * (netypes + 2));
	if (ptr == NULL)
	    goto next;

	etypes = ptr;
	etypes[netypes] = entry.keyblock.keytype;
	etypes[netypes + 1] = ETYPE_NULL;
	netypes++;
    next:
	krb5_kt_free_entry(context, &entry);
    }
    krb5_kt_end_seq_get(context, keytab, &cursor);

    if (etypes) {
	if (ctx->etypes)
	    free(ctx->etypes);
	ctx->etypes = etypes;
    }

 out:
    return 0;
}

static krb5_error_code KRB5_CALLCONV
keyblock_key_proc(krb5_context context, krb5_enctype enctype,
		  krb5_const_pointer keyseed,
		  krb5_salt salt, krb5_data *s2kparms,
		  krb5_keyblock **key)
{
    return krb5_copy_keyblock (context, keyseed, key);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_set_keyblock(krb5_context context,
			     krb5_init_creds_context ctx,
			     krb5_keyblock *keyblock)
{
    ctx->keyseed = (void *)keyblock;
    ctx->keyproc = keyblock_key_proc;

    return 0;
}

/**
 * The core loop if krb5_get_init_creds() function family. Create the
 * packets and have the caller send them off to the KDC.
 *
 * If the caller want all work been done for them, use
 * krb5_init_creds_get() instead.
 *
 * @param context a Kerberos 5 context.
 * @param ctx ctx krb5_init_creds_context context.
 * @param in input data from KDC, first round it should be reset by krb5_data_zer().
 * @param out reply to KDC.
 * @param hostinfo KDC address info, first round it can be NULL.
 * @param flags status of the round, if
 *        KRB5_INIT_CREDS_STEP_FLAG_CONTINUE is set, continue one more round.
 *
 * @return 0 for success, or an Kerberos 5 error code, see
 *     krb5_get_error_message().
 *
 * @ingroup krb5_credential
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_step(krb5_context context,
		     krb5_init_creds_context ctx,
		     krb5_data *in,
		     krb5_data *out,
		     krb5_krbhst_info *hostinfo,
		     unsigned int *flags)
{
    krb5_error_code ret;
    size_t len = 0;
    size_t size;

    krb5_data_zero(out);

    if (ctx->as_req.req_body.cname == NULL) {
	ret = init_as_req(context, ctx->flags, &ctx->cred,
			  ctx->addrs, ctx->etypes, &ctx->as_req);
	if (ret) {
	    free_init_creds_ctx(context, ctx);
	    return ret;
	}
    }

#define MAX_PA_COUNTER 10
    if (ctx->pa_counter > MAX_PA_COUNTER) {
	krb5_set_error_message(context, KRB5_GET_IN_TKT_LOOP,
			       N_("Looping %d times while getting "
				  "initial credentials", ""),
			       ctx->pa_counter);
	return KRB5_GET_IN_TKT_LOOP;
    }
    ctx->pa_counter++;

    _krb5_debug(context, 5, "krb5_get_init_creds: loop %d", ctx->pa_counter);

    /* Lets process the input packet */
    if (in && in->length) {
	krb5_kdc_rep rep;

	memset(&rep, 0, sizeof(rep));

	_krb5_debug(context, 5, "krb5_get_init_creds: processing input");

	ret = decode_AS_REP(in->data, in->length, &rep.kdc_rep, &size);
	if (ret == 0) {
	    krb5_keyblock *key = NULL;
	    unsigned eflags = EXTRACT_TICKET_AS_REQ | EXTRACT_TICKET_TIMESYNC;

	    if (ctx->flags.canonicalize) {
		eflags |= EXTRACT_TICKET_ALLOW_SERVER_MISMATCH;
		eflags |= EXTRACT_TICKET_MATCH_REALM;
	    }
	    if (ctx->ic_flags & KRB5_INIT_CREDS_NO_C_CANON_CHECK)
		eflags |= EXTRACT_TICKET_ALLOW_CNAME_MISMATCH;

	    ret = process_pa_data_to_key(context, ctx, &ctx->cred,
					 &ctx->as_req, &rep.kdc_rep, hostinfo, &key);
	    if (ret) {
		free_AS_REP(&rep.kdc_rep);
		goto out;
	    }

	    _krb5_debug(context, 5, "krb5_get_init_creds: extracting ticket");

	    ret = _krb5_extract_ticket(context,
				       &rep,
				       &ctx->cred,
				       key,
				       NULL,
				       KRB5_KU_AS_REP_ENC_PART,
				       NULL,
				       ctx->nonce,
				       eflags,
				       NULL,
				       NULL);
	    krb5_free_keyblock(context, key);

	    *flags = 0;

	    if (ret == 0)
		ret = copy_EncKDCRepPart(&rep.enc_part, &ctx->enc_part);

	    free_AS_REP(&rep.kdc_rep);
	    free_EncASRepPart(&rep.enc_part);

	    return ret;

	} else {
	    /* let's try to parse it as a KRB-ERROR */

	    _krb5_debug(context, 5, "krb5_get_init_creds: got an error");

	    free_KRB_ERROR(&ctx->error);

	    ret = krb5_rd_error(context, in, &ctx->error);
	    if(ret && in->length && ((char*)in->data)[0] == 4)
		ret = KRB5KRB_AP_ERR_V4_REPLY;
	    if (ret) {
		_krb5_debug(context, 5, "krb5_get_init_creds: failed to read error");
		goto out;
	    }

	    ret = krb5_error_from_rd_error(context, &ctx->error, &ctx->cred);

	    _krb5_debug(context, 5, "krb5_get_init_creds: KRB-ERROR %d", ret);

	    /*
	     * If no preauth was set and KDC requires it, give it one
	     * more try.
	     */

	    if (ret == KRB5KDC_ERR_PREAUTH_REQUIRED) {

	        free_METHOD_DATA(&ctx->md);
	        memset(&ctx->md, 0, sizeof(ctx->md));

		if (ctx->error.e_data) {
		    ret = decode_METHOD_DATA(ctx->error.e_data->data,
					     ctx->error.e_data->length,
					     &ctx->md,
					     NULL);
		    if (ret)
			krb5_set_error_message(context, ret,
					       N_("Failed to decode METHOD-DATA", ""));
		} else {
		    krb5_set_error_message(context, ret,
					   N_("Preauth required but no preauth "
					      "options send by KDC", ""));
		}
	    } else if (ret == KRB5KRB_AP_ERR_SKEW && context->kdc_sec_offset == 0) {
		/*
		 * Try adapt to timeskrew when we are using pre-auth, and
		 * if there was a time skew, try again.
		 */
		krb5_set_real_time(context, ctx->error.stime, -1);
		if (context->kdc_sec_offset)
		    ret = 0;

		_krb5_debug(context, 10, "init_creds: err skew updateing kdc offset to %d",
			    context->kdc_sec_offset);

		ctx->used_pa_types = 0;

	    } else if (ret == KRB5_KDC_ERR_WRONG_REALM && ctx->flags.canonicalize) {
	        /* client referal to a new realm */

		if (ctx->error.crealm == NULL) {
		    krb5_set_error_message(context, ret,
					   N_("Got a client referral, not but no realm", ""));
		    goto out;
		}
		_krb5_debug(context, 5,
			    "krb5_get_init_creds: got referal to realm %s",
			    *ctx->error.crealm);

		ret = krb5_principal_set_realm(context,
					       ctx->cred.client,
					       *ctx->error.crealm);

		ctx->used_pa_types = 0;
	    }
	    if (ret)
		goto out;
	}
    }

    if (ctx->as_req.padata) {
	free_METHOD_DATA(ctx->as_req.padata);
	free(ctx->as_req.padata);
	ctx->as_req.padata = NULL;
    }

    /* Set a new nonce. */
    ctx->as_req.req_body.nonce = ctx->nonce;

    /* fill_in_md_data */
    ret = process_pa_data_to_md(context, &ctx->cred, &ctx->as_req, ctx,
				&ctx->md, &ctx->as_req.padata,
				ctx->prompter, ctx->prompter_data);
    if (ret)
	goto out;

    krb5_data_free(&ctx->req_buffer);

    ASN1_MALLOC_ENCODE(AS_REQ,
		       ctx->req_buffer.data, ctx->req_buffer.length,
		       &ctx->as_req, &len, ret);
    if (ret)
	goto out;
    if(len != ctx->req_buffer.length)
	krb5_abortx(context, "internal error in ASN.1 encoder");

    out->data = ctx->req_buffer.data;
    out->length = ctx->req_buffer.length;

    *flags = KRB5_INIT_CREDS_STEP_FLAG_CONTINUE;

    return 0;
 out:
    return ret;
}

/**
 * Extract the newly acquired credentials from krb5_init_creds_context
 * context.
 *
 * @param context A Kerberos 5 context.
 * @param ctx
 * @param cred credentials, free with krb5_free_cred_contents().
 *
 * @return 0 for sucess or An Kerberos error code, see krb5_get_error_message().
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_get_creds(krb5_context context,
			  krb5_init_creds_context ctx,
			  krb5_creds *cred)
{
    return krb5_copy_creds_contents(context, &ctx->cred, cred);
}

/**
 * Get the last error from the transaction.
 *
 * @return Returns 0 or an error code
 *
 * @ingroup krb5_credential
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_get_error(krb5_context context,
			  krb5_init_creds_context ctx,
			  KRB_ERROR *error)
{
    krb5_error_code ret;

    ret = copy_KRB_ERROR(&ctx->error, error);
    if (ret)
	krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));

    return ret;
}

/**
 * Free the krb5_init_creds_context allocated by krb5_init_creds_init().
 *
 * @param context A Kerberos 5 context.
 * @param ctx The krb5_init_creds_context to free.
 *
 * @ingroup krb5_credential
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_init_creds_free(krb5_context context,
		     krb5_init_creds_context ctx)
{
    free_init_creds_ctx(context, ctx);
    free(ctx);
}

/**
 * Get new credentials as setup by the krb5_init_creds_context.
 *
 * @param context A Kerberos 5 context.
 * @param ctx The krb5_init_creds_context to process.
 *
 * @ingroup krb5_credential
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_get(krb5_context context, krb5_init_creds_context ctx)
{
    krb5_sendto_ctx stctx = NULL;
    krb5_krbhst_info *hostinfo = NULL;
    krb5_error_code ret;
    krb5_data in, out;
    unsigned int flags = 0;

    krb5_data_zero(&in);
    krb5_data_zero(&out);

    ret = krb5_sendto_ctx_alloc(context, &stctx);
    if (ret)
	goto out;
    krb5_sendto_ctx_set_func(stctx, _krb5_kdc_retry, NULL);

    while (1) {
	flags = 0;
	ret = krb5_init_creds_step(context, ctx, &in, &out, hostinfo, &flags);
	krb5_data_free(&in);
	if (ret)
	    goto out;

	if ((flags & 1) == 0)
	    break;

	ret = krb5_sendto_context (context, stctx, &out,
				   ctx->cred.client->realm, &in);
    	if (ret)
	    goto out;

    }

 out:
    if (stctx)
	krb5_sendto_ctx_free(context, stctx);

    return ret;
}

/**
 * Get new credentials using password.
 *
 * @ingroup krb5_credential
 */


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_password(krb5_context context,
			     krb5_creds *creds,
			     krb5_principal client,
			     const char *password,
			     krb5_prompter_fct prompter,
			     void *data,
			     krb5_deltat start_time,
			     const char *in_tkt_service,
			     krb5_get_init_creds_opt *options)
{
    krb5_init_creds_context ctx;
    char buf[BUFSIZ];
    krb5_error_code ret;
    int chpw = 0;

 again:
    ret = krb5_init_creds_init(context, client, prompter, data, start_time, options, &ctx);
    if (ret)
	goto out;

    ret = krb5_init_creds_set_service(context, ctx, in_tkt_service);
    if (ret)
	goto out;

    if (prompter != NULL && ctx->password == NULL && password == NULL) {
	krb5_prompt prompt;
	krb5_data password_data;
	char *p, *q;

	krb5_unparse_name (context, client, &p);
	asprintf (&q, "%s's Password: ", p);
	free (p);
	prompt.prompt = q;
	password_data.data   = buf;
	password_data.length = sizeof(buf);
	prompt.hidden = 1;
	prompt.reply  = &password_data;
	prompt.type   = KRB5_PROMPT_TYPE_PASSWORD;

	ret = (*prompter) (context, data, NULL, NULL, 1, &prompt);
	free (q);
	if (ret) {
	    memset (buf, 0, sizeof(buf));
	    ret = KRB5_LIBOS_PWDINTR;
	    krb5_clear_error_message (context);
	    goto out;
	}
	password = password_data.data;
    }

    if (password) {
	ret = krb5_init_creds_set_password(context, ctx, password);
	if (ret)
	    goto out;
    }

    ret = krb5_init_creds_get(context, ctx);

    if (ret == 0)
	process_last_request(context, options, ctx);


    if (ret == KRB5KDC_ERR_KEY_EXPIRED && chpw == 0) {
	char buf2[1024];

	/* try to avoid recursion */
	if (in_tkt_service != NULL && strcmp(in_tkt_service, "kadmin/changepw") == 0)
	   goto out;

	/* don't try to change password where then where none */
	if (prompter == NULL)
	    goto out;

	ret = change_password (context,
			       client,
			       ctx->password,
			       buf2,
			       sizeof(buf),
			       prompter,
			       data,
			       options);
	if (ret)
	    goto out;
	chpw = 1;
	krb5_init_creds_free(context, ctx);
	goto again;
    }

 out:
    if (ret == 0)
	krb5_init_creds_get_creds(context, ctx, creds);

    if (ctx)
	krb5_init_creds_free(context, ctx);

    memset(buf, 0, sizeof(buf));
    return ret;
}

/**
 * Get new credentials using keyblock.
 *
 * @ingroup krb5_credential
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_keyblock(krb5_context context,
			     krb5_creds *creds,
			     krb5_principal client,
			     krb5_keyblock *keyblock,
			     krb5_deltat start_time,
			     const char *in_tkt_service,
			     krb5_get_init_creds_opt *options)
{
    krb5_init_creds_context ctx;
    krb5_error_code ret;

    memset(creds, 0, sizeof(*creds));

    ret = krb5_init_creds_init(context, client, NULL, NULL, start_time, options, &ctx);
    if (ret)
	goto out;

    ret = krb5_init_creds_set_service(context, ctx, in_tkt_service);
    if (ret)
	goto out;

    ret = krb5_init_creds_set_keyblock(context, ctx, keyblock);
    if (ret)
	goto out;

    ret = krb5_init_creds_get(context, ctx);

    if (ret == 0)
        process_last_request(context, options, ctx);

 out:
    if (ret == 0)
	krb5_init_creds_get_creds(context, ctx, creds);

    if (ctx)
	krb5_init_creds_free(context, ctx);

    return ret;
}

/**
 * Get new credentials using keytab.
 *
 * @ingroup krb5_credential
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_keytab(krb5_context context,
			   krb5_creds *creds,
			   krb5_principal client,
			   krb5_keytab keytab,
			   krb5_deltat start_time,
			   const char *in_tkt_service,
			   krb5_get_init_creds_opt *options)
{
    krb5_init_creds_context ctx;
    krb5_error_code ret;

    memset(creds, 0, sizeof(*creds));

    ret = krb5_init_creds_init(context, client, NULL, NULL, start_time, options, &ctx);
    if (ret)
	goto out;

    ret = krb5_init_creds_set_service(context, ctx, in_tkt_service);
    if (ret)
	goto out;

    ret = krb5_init_creds_set_keytab(context, ctx, keytab);
    if (ret)
	goto out;

    ret = krb5_init_creds_get(context, ctx);
    if (ret == 0)
        process_last_request(context, options, ctx);

 out:
    if (ret == 0)
	krb5_init_creds_get_creds(context, ctx, creds);

    if (ctx)
	krb5_init_creds_free(context, ctx);

    return ret;
}
