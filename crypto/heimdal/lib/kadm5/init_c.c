/*
 * Copyright (c) 1997 - 2006 Kungliga Tekniska Högskolan
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

#include "kadm5_locl.h"
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

RCSID("$Id$");

static void
set_funcs(kadm5_client_context *c)
{
#define SET(C, F) (C)->funcs.F = kadm5 ## _c_ ## F
    SET(c, chpass_principal);
    SET(c, chpass_principal_with_key);
    SET(c, create_principal);
    SET(c, delete_principal);
    SET(c, destroy);
    SET(c, flush);
    SET(c, get_principal);
    SET(c, get_principals);
    SET(c, get_privs);
    SET(c, modify_principal);
    SET(c, randkey_principal);
    SET(c, rename_principal);
}

kadm5_ret_t
_kadm5_c_init_context(kadm5_client_context **ctx,
		      kadm5_config_params *params,
		      krb5_context context)
{
    krb5_error_code ret;
    char *colon;

    *ctx = malloc(sizeof(**ctx));
    if(*ctx == NULL)
	return ENOMEM;
    memset(*ctx, 0, sizeof(**ctx));
    krb5_add_et_list (context, initialize_kadm5_error_table_r);
    set_funcs(*ctx);
    (*ctx)->context = context;
    if(params->mask & KADM5_CONFIG_REALM) {
	ret = 0;
	(*ctx)->realm = strdup(params->realm);
	if ((*ctx)->realm == NULL)
	    ret = ENOMEM;
    } else
	ret = krb5_get_default_realm((*ctx)->context, &(*ctx)->realm);
    if (ret) {
	free(*ctx);
	return ret;
    }
    if(params->mask & KADM5_CONFIG_ADMIN_SERVER)
	(*ctx)->admin_server = strdup(params->admin_server);
    else {
	char **hostlist;

	ret = krb5_get_krb_admin_hst (context, &(*ctx)->realm, &hostlist);
	if (ret) {
	    free((*ctx)->realm);
	    free(*ctx);
	    return ret;
	}
	(*ctx)->admin_server = strdup(*hostlist);
	krb5_free_krbhst (context, hostlist);
    }

    if ((*ctx)->admin_server == NULL) {
	free((*ctx)->realm);
	free(*ctx);
	return ENOMEM;
    }
    colon = strchr ((*ctx)->admin_server, ':');
    if (colon != NULL)
	*colon++ = '\0';

    (*ctx)->kadmind_port = 0;

    if(params->mask & KADM5_CONFIG_KADMIND_PORT)
	(*ctx)->kadmind_port = params->kadmind_port;
    else if (colon != NULL) {
	char *end;

	(*ctx)->kadmind_port = htons(strtol (colon, &end, 0));
    }
    if ((*ctx)->kadmind_port == 0)
	(*ctx)->kadmind_port = krb5_getportbyname (context, "kerberos-adm",
						   "tcp", 749);
    return 0;
}

static krb5_error_code
get_kadm_ticket(krb5_context context,
		krb5_ccache id,
		krb5_principal client,
		const char *server_name)
{
    krb5_error_code ret;
    krb5_creds in, *out;

    memset(&in, 0, sizeof(in));
    in.client = client;
    ret = krb5_parse_name(context, server_name, &in.server);
    if(ret)
	return ret;
    ret = krb5_get_credentials(context, 0, id, &in, &out);
    if(ret == 0)
	krb5_free_creds(context, out);
    krb5_free_principal(context, in.server);
    return ret;
}

static krb5_error_code
get_new_cache(krb5_context context,
	      krb5_principal client,
	      const char *password,
	      krb5_prompter_fct prompter,
	      const char *keytab,
	      const char *server_name,
	      krb5_ccache *ret_cache)
{
    krb5_error_code ret;
    krb5_creds cred;
    krb5_get_init_creds_opt *opt;
    krb5_ccache id;

    ret = krb5_get_init_creds_opt_alloc (context, &opt);
    if (ret)
	return ret;

    krb5_get_init_creds_opt_set_default_flags(context, "kadmin",
					      krb5_principal_get_realm(context,
								       client),
					      opt);


    krb5_get_init_creds_opt_set_forwardable (opt, FALSE);
    krb5_get_init_creds_opt_set_proxiable (opt, FALSE);

    if(password == NULL && prompter == NULL) {
	krb5_keytab kt;
	if(keytab == NULL)
	    ret = krb5_kt_default(context, &kt);
	else
	    ret = krb5_kt_resolve(context, keytab, &kt);
	if(ret) {
	    krb5_get_init_creds_opt_free(context, opt);
	    return ret;
	}
	ret = krb5_get_init_creds_keytab (context,
					  &cred,
					  client,
					  kt,
					  0,
					  server_name,
					  opt);
	krb5_kt_close(context, kt);
    } else {
	ret = krb5_get_init_creds_password (context,
					    &cred,
					    client,
					    password,
					    prompter,
					    NULL,
					    0,
					    server_name,
					    opt);
    }
    krb5_get_init_creds_opt_free(context, opt);
    switch(ret){
    case 0:
	break;
    case KRB5_LIBOS_PWDINTR:	/* don't print anything if it was just C-c:ed */
    case KRB5KRB_AP_ERR_BAD_INTEGRITY:
    case KRB5KRB_AP_ERR_MODIFIED:
	return KADM5_BAD_PASSWORD;
    default:
	return ret;
    }
    ret = krb5_cc_new_unique(context, krb5_cc_type_memory, NULL, &id);
    if(ret)
	return ret;
    ret = krb5_cc_initialize (context, id, cred.client);
    if (ret)
	return ret;
    ret = krb5_cc_store_cred (context, id, &cred);
    if (ret)
	return ret;
    krb5_free_cred_contents (context, &cred);
    *ret_cache = id;
    return 0;
}

/*
 * Check the credential cache `id´ to figure out what principal to use
 * when talking to the kadmind. If there is a initial kadmin/admin@
 * credential in the cache, use that client principal. Otherwise, use
 * the client principals first component and add /admin to the
 * principal.
 */

static krb5_error_code
get_cache_principal(krb5_context context,
		    krb5_ccache *id,
		    krb5_principal *client)
{
    krb5_error_code ret;
    const char *name, *inst;
    krb5_principal p1, p2;

    ret = krb5_cc_default(context, id);
    if(ret) {
	*id = NULL;
	return ret;
    }

    ret = krb5_cc_get_principal(context, *id, &p1);
    if(ret) {
	krb5_cc_close(context, *id);
	*id = NULL;
	return ret;
    }

    ret = krb5_make_principal(context, &p2, NULL,
			      "kadmin", "admin", NULL);
    if (ret) {
	krb5_cc_close(context, *id);
	*id = NULL;
	krb5_free_principal(context, p1);
	return ret;
    }

    {
	krb5_creds in, *out;
	krb5_kdc_flags flags;

	flags.i = 0;
	memset(&in, 0, sizeof(in));

	in.client = p1;
	in.server = p2;

	/* check for initial ticket kadmin/admin */
	ret = krb5_get_credentials_with_flags(context, KRB5_GC_CACHED, flags,
					      *id, &in, &out);
	krb5_free_principal(context, p2);
	if (ret == 0) {
	    if (out->flags.b.initial) {
		*client = p1;
		krb5_free_creds(context, out);
		return 0;
	    }
	    krb5_free_creds(context, out);
	}
    }
    krb5_cc_close(context, *id);
    *id = NULL;

    name = krb5_principal_get_comp_string(context, p1, 0);
    inst = krb5_principal_get_comp_string(context, p1, 1);
    if(inst == NULL || strcmp(inst, "admin") != 0) {
	ret = krb5_make_principal(context, &p2, NULL, name, "admin", NULL);
	krb5_free_principal(context, p1);
	if(ret != 0)
	    return ret;

	*client = p2;
	return 0;
    }

    *client = p1;

    return 0;
}

krb5_error_code
_kadm5_c_get_cred_cache(krb5_context context,
			const char *client_name,
			const char *server_name,
			const char *password,
			krb5_prompter_fct prompter,
			const char *keytab,
			krb5_ccache ccache,
			krb5_ccache *ret_cache)
{
    krb5_error_code ret;
    krb5_ccache id = NULL;
    krb5_principal default_client = NULL, client = NULL;

    /* treat empty password as NULL */
    if(password && *password == '\0')
	password = NULL;
    if(server_name == NULL)
	server_name = KADM5_ADMIN_SERVICE;

    if(client_name != NULL) {
	ret = krb5_parse_name(context, client_name, &client);
	if(ret)
	    return ret;
    }

    if(ccache != NULL) {
	id = ccache;
	ret = krb5_cc_get_principal(context, id, &client);
	if(ret)
	    return ret;
    } else {
	/* get principal from default cache, ok if this doesn't work */

	ret = get_cache_principal(context, &id, &default_client);
	if (ret) {
	    /*
	     * No client was specified by the caller and we cannot
	     * determine the client from a credentials cache.
	     */
	    const char *user;

	    user = get_default_username ();

	    if(user == NULL) {
		krb5_set_error_message(context, KADM5_FAILURE, "Unable to find local user name");
		return KADM5_FAILURE;
	    }
	    ret = krb5_make_principal(context, &default_client,
				      NULL, user, "admin", NULL);
	    if(ret)
		return ret;
	}
    }


    /*
     * No client was specified by the caller, but we have a client
     * from the default credentials cache.
     */
    if (client == NULL && default_client != NULL)
	client = default_client;


    if(id && client && (default_client == NULL ||
	      krb5_principal_compare(context, client, default_client) != 0)) {
	ret = get_kadm_ticket(context, id, client, server_name);
	if(ret == 0) {
	    *ret_cache = id;
	    krb5_free_principal(context, default_client);
	    if (default_client != client)
		krb5_free_principal(context, client);
	    return 0;
	}
	if(ccache != NULL)
	    /* couldn't get ticket from cache */
	    return -1;
    }
    /* get creds via AS request */
    if(id && (id != ccache))
	krb5_cc_close(context, id);
    if (client != default_client)
	krb5_free_principal(context, default_client);

    ret = get_new_cache(context, client, password, prompter, keytab,
			server_name, ret_cache);
    krb5_free_principal(context, client);
    return ret;
}

static kadm5_ret_t
kadm_connect(kadm5_client_context *ctx)
{
    kadm5_ret_t ret;
    krb5_principal server;
    krb5_ccache cc;
    rk_socket_t s = rk_INVALID_SOCKET;
    struct addrinfo *ai, *a;
    struct addrinfo hints;
    int error;
    char portstr[NI_MAXSERV];
    char *hostname, *slash;
    char *service_name;
    krb5_context context = ctx->context;

    memset (&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    snprintf (portstr, sizeof(portstr), "%u", ntohs(ctx->kadmind_port));

    hostname = ctx->admin_server;
    slash = strchr (hostname, '/');
    if (slash != NULL)
	hostname = slash + 1;

    error = getaddrinfo (hostname, portstr, &hints, &ai);
    if (error) {
	krb5_clear_error_message(context);
	return KADM5_BAD_SERVER_NAME;
    }

    for (a = ai; a != NULL; a = a->ai_next) {
	s = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
	if (s < 0)
	    continue;
	if (connect (s, a->ai_addr, a->ai_addrlen) < 0) {
	    krb5_clear_error_message(context);
	    krb5_warn (context, errno, "connect(%s)", hostname);
	    rk_closesocket (s);
	    continue;
	}
	break;
    }
    if (a == NULL) {
	freeaddrinfo (ai);
	krb5_clear_error_message(context);
	krb5_warnx (context, "failed to contact %s", hostname);
	return KADM5_FAILURE;
    }
    ret = _kadm5_c_get_cred_cache(context,
				  ctx->client_name,
				  ctx->service_name,
				  NULL, ctx->prompter, ctx->keytab,
				  ctx->ccache, &cc);

    if(ret) {
	freeaddrinfo (ai);
	rk_closesocket(s);
	return ret;
    }

    if (ctx->realm)
	asprintf(&service_name, "%s@%s", KADM5_ADMIN_SERVICE, ctx->realm);
    else
	asprintf(&service_name, "%s", KADM5_ADMIN_SERVICE);

    if (service_name == NULL) {
	freeaddrinfo (ai);
	rk_closesocket(s);
	krb5_clear_error_message(context);
	return ENOMEM;
    }

    ret = krb5_parse_name(context, service_name, &server);
    free(service_name);
    if(ret) {
	freeaddrinfo (ai);
	if(ctx->ccache == NULL)
	    krb5_cc_close(context, cc);
	rk_closesocket(s);
	return ret;
    }
    ctx->ac = NULL;

    ret = krb5_sendauth(context, &ctx->ac, &s,
			KADMIN_APPL_VERSION, NULL,
			server, AP_OPTS_MUTUAL_REQUIRED,
			NULL, NULL, cc, NULL, NULL, NULL);
    if(ret == 0) {
	krb5_data params;
	kadm5_config_params p;
	memset(&p, 0, sizeof(p));
	if(ctx->realm) {
	    p.mask |= KADM5_CONFIG_REALM;
	    p.realm = ctx->realm;
	}
	ret = _kadm5_marshal_params(context, &p, &params);

	ret = krb5_write_priv_message(context, ctx->ac, &s, &params);
	krb5_data_free(&params);
	if(ret) {
	    freeaddrinfo (ai);
	    rk_closesocket(s);
	    if(ctx->ccache == NULL)
		krb5_cc_close(context, cc);
	    return ret;
	}
    } else if(ret == KRB5_SENDAUTH_BADAPPLVERS) {
	rk_closesocket(s);

	s = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
	if (s < 0) {
	    freeaddrinfo (ai);
	    krb5_clear_error_message(context);
	    return errno;
	}
	if (connect (s, a->ai_addr, a->ai_addrlen) < 0) {
	    rk_closesocket (s);
	    freeaddrinfo (ai);
	    krb5_clear_error_message(context);
	    return errno;
	}
	ret = krb5_sendauth(context, &ctx->ac, &s,
			    KADMIN_OLD_APPL_VERSION, NULL,
			    server, AP_OPTS_MUTUAL_REQUIRED,
			    NULL, NULL, cc, NULL, NULL, NULL);
    }
    freeaddrinfo (ai);
    if(ret) {
	rk_closesocket(s);
	return ret;
    }

    krb5_free_principal(context, server);
    if(ctx->ccache == NULL)
	krb5_cc_close(context, cc);
    ctx->sock = s;

    return 0;
}

kadm5_ret_t
_kadm5_connect(void *handle)
{
    kadm5_client_context *ctx = handle;
    if(ctx->sock == -1)
	return kadm_connect(ctx);
    return 0;
}

static kadm5_ret_t
kadm5_c_init_with_context(krb5_context context,
			  const char *client_name,
			  const char *password,
			  krb5_prompter_fct prompter,
			  const char *keytab,
			  krb5_ccache ccache,
			  const char *service_name,
			  kadm5_config_params *realm_params,
			  unsigned long struct_version,
			  unsigned long api_version,
			  void **server_handle)
{
    kadm5_ret_t ret;
    kadm5_client_context *ctx;
    krb5_ccache cc;

    ret = _kadm5_c_init_context(&ctx, realm_params, context);
    if(ret)
	return ret;

    if(password != NULL && *password != '\0') {
	ret = _kadm5_c_get_cred_cache(context,
				      client_name,
				      service_name,
				      password, prompter, keytab, ccache, &cc);
	if(ret)
	    return ret; /* XXX */
	ccache = cc;
    }


    if (client_name != NULL)
	ctx->client_name = strdup(client_name);
    else
	ctx->client_name = NULL;
    if (service_name != NULL)
	ctx->service_name = strdup(service_name);
    else
	ctx->service_name = NULL;
    ctx->prompter = prompter;
    ctx->keytab = keytab;
    ctx->ccache = ccache;
    /* maybe we should copy the params here */
    ctx->sock = -1;

    *server_handle = ctx;
    return 0;
}

static kadm5_ret_t
init_context(const char *client_name,
	     const char *password,
	     krb5_prompter_fct prompter,
	     const char *keytab,
	     krb5_ccache ccache,
	     const char *service_name,
	     kadm5_config_params *realm_params,
	     unsigned long struct_version,
	     unsigned long api_version,
	     void **server_handle)
{
    krb5_context context;
    kadm5_ret_t ret;
    kadm5_server_context *ctx;

    ret = krb5_init_context(&context);
    if (ret)
	return ret;
    ret = kadm5_c_init_with_context(context,
				    client_name,
				    password,
				    prompter,
				    keytab,
				    ccache,
				    service_name,
				    realm_params,
				    struct_version,
				    api_version,
				    server_handle);
    if(ret){
	krb5_free_context(context);
	return ret;
    }
    ctx = *server_handle;
    ctx->my_context = 1;
    return 0;
}

kadm5_ret_t
kadm5_c_init_with_password_ctx(krb5_context context,
			       const char *client_name,
			       const char *password,
			       const char *service_name,
			       kadm5_config_params *realm_params,
			       unsigned long struct_version,
			       unsigned long api_version,
			       void **server_handle)
{
    return kadm5_c_init_with_context(context,
				     client_name,
				     password,
				     krb5_prompter_posix,
				     NULL,
				     NULL,
				     service_name,
				     realm_params,
				     struct_version,
				     api_version,
				     server_handle);
}

kadm5_ret_t
kadm5_c_init_with_password(const char *client_name,
			   const char *password,
			   const char *service_name,
			   kadm5_config_params *realm_params,
			   unsigned long struct_version,
			   unsigned long api_version,
			   void **server_handle)
{
    return init_context(client_name,
			password,
			krb5_prompter_posix,
			NULL,
			NULL,
			service_name,
			realm_params,
			struct_version,
			api_version,
			server_handle);
}

kadm5_ret_t
kadm5_c_init_with_skey_ctx(krb5_context context,
			   const char *client_name,
			   const char *keytab,
			   const char *service_name,
			   kadm5_config_params *realm_params,
			   unsigned long struct_version,
			   unsigned long api_version,
			   void **server_handle)
{
    return kadm5_c_init_with_context(context,
				     client_name,
				     NULL,
				     NULL,
				     keytab,
				     NULL,
				     service_name,
				     realm_params,
				     struct_version,
				     api_version,
				     server_handle);
}


kadm5_ret_t
kadm5_c_init_with_skey(const char *client_name,
		     const char *keytab,
		     const char *service_name,
		     kadm5_config_params *realm_params,
		     unsigned long struct_version,
		     unsigned long api_version,
		     void **server_handle)
{
    return init_context(client_name,
			NULL,
			NULL,
			keytab,
			NULL,
			service_name,
			realm_params,
			struct_version,
			api_version,
			server_handle);
}

kadm5_ret_t
kadm5_c_init_with_creds_ctx(krb5_context context,
			    const char *client_name,
			    krb5_ccache ccache,
			    const char *service_name,
			    kadm5_config_params *realm_params,
			    unsigned long struct_version,
			    unsigned long api_version,
			    void **server_handle)
{
    return kadm5_c_init_with_context(context,
				     client_name,
				     NULL,
				     NULL,
				     NULL,
				     ccache,
				     service_name,
				     realm_params,
				     struct_version,
				     api_version,
				     server_handle);
}

kadm5_ret_t
kadm5_c_init_with_creds(const char *client_name,
			krb5_ccache ccache,
			const char *service_name,
			kadm5_config_params *realm_params,
			unsigned long struct_version,
			unsigned long api_version,
			void **server_handle)
{
    return init_context(client_name,
			NULL,
			NULL,
			NULL,
			ccache,
			service_name,
			realm_params,
			struct_version,
			api_version,
			server_handle);
}

#if 0
kadm5_ret_t
kadm5_init(char *client_name, char *pass,
	   char *service_name,
	   kadm5_config_params *realm_params,
	   unsigned long struct_version,
	   unsigned long api_version,
	   void **server_handle)
{
}
#endif

