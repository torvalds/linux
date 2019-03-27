/*
 * Copyright (c) 1997 - 2000 Kungliga Tekniska Högskolan
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

RCSID("$Id$");


static kadm5_ret_t
kadm5_s_init_with_context(krb5_context context,
			  const char *client_name,
			  const char *service_name,
			  kadm5_config_params *realm_params,
			  unsigned long struct_version,
			  unsigned long api_version,
			  void **server_handle)
{
    kadm5_ret_t ret;
    kadm5_server_context *ctx;
    ret = _kadm5_s_init_context(&ctx, realm_params, context);
    if(ret)
	return ret;

    assert(ctx->config.dbname != NULL);
    assert(ctx->config.stash_file != NULL);
    assert(ctx->config.acl_file != NULL);
    assert(ctx->log_context.log_file != NULL);
#ifndef NO_UNIX_SOCKETS
    assert(ctx->log_context.socket_name.sun_path[0] != '\0');
#else
    assert(ctx->log_context.socket_info != NULL);
#endif

    ret = hdb_create(ctx->context, &ctx->db, ctx->config.dbname);
    if(ret)
	return ret;
    ret = hdb_set_master_keyfile (ctx->context,
				  ctx->db, ctx->config.stash_file);
    if(ret)
	return ret;

    ctx->log_context.log_fd   = -1;

#ifndef NO_UNIX_SOCKETS
    ctx->log_context.socket_fd = socket (AF_UNIX, SOCK_DGRAM, 0);
#else
    ctx->log_context.socket_fd = socket (ctx->log_context.socket_info->ai_family,
					 ctx->log_context.socket_info->ai_socktype,
					 ctx->log_context.socket_info->ai_protocol);
#endif

    ret = krb5_parse_name(ctx->context, client_name, &ctx->caller);
    if(ret)
	return ret;

    ret = _kadm5_acl_init(ctx);
    if(ret)
	return ret;

    *server_handle = ctx;
    return 0;
}

kadm5_ret_t
kadm5_s_init_with_password_ctx(krb5_context context,
			       const char *client_name,
			       const char *password,
			       const char *service_name,
			       kadm5_config_params *realm_params,
			       unsigned long struct_version,
			       unsigned long api_version,
			       void **server_handle)
{
    return kadm5_s_init_with_context(context,
				     client_name,
				     service_name,
				     realm_params,
				     struct_version,
				     api_version,
				     server_handle);
}

kadm5_ret_t
kadm5_s_init_with_password(const char *client_name,
			   const char *password,
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
    ret = kadm5_s_init_with_password_ctx(context,
					 client_name,
					 password,
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
kadm5_s_init_with_skey_ctx(krb5_context context,
			   const char *client_name,
			   const char *keytab,
			   const char *service_name,
			   kadm5_config_params *realm_params,
			   unsigned long struct_version,
			   unsigned long api_version,
			   void **server_handle)
{
    return kadm5_s_init_with_context(context,
				     client_name,
				     service_name,
				     realm_params,
				     struct_version,
				     api_version,
				     server_handle);
}

kadm5_ret_t
kadm5_s_init_with_skey(const char *client_name,
		       const char *keytab,
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
    ret = kadm5_s_init_with_skey_ctx(context,
				     client_name,
				     keytab,
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
kadm5_s_init_with_creds_ctx(krb5_context context,
			    const char *client_name,
			    krb5_ccache ccache,
			    const char *service_name,
			    kadm5_config_params *realm_params,
			    unsigned long struct_version,
			    unsigned long api_version,
			    void **server_handle)
{
    return kadm5_s_init_with_context(context,
				     client_name,
				     service_name,
				     realm_params,
				     struct_version,
				     api_version,
				     server_handle);
}

kadm5_ret_t
kadm5_s_init_with_creds(const char *client_name,
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
    ret = kadm5_s_init_with_creds_ctx(context,
				      client_name,
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
