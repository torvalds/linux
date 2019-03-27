/*
 * Copyright (c) 1997 Kungliga Tekniska Högskolan
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

kadm5_ret_t
kadm5_init_with_password(const char *client_name,
			 const char *password,
			 const char *service_name,
			 kadm5_config_params *realm_params,
			 unsigned long struct_version,
			 unsigned long api_version,
			 void **server_handle)
{
    return kadm5_c_init_with_password(client_name,
				      password,
				      service_name,
				      realm_params,
				      struct_version,
				      api_version,
				      server_handle);
}

kadm5_ret_t
kadm5_init_with_password_ctx(krb5_context context,
			     const char *client_name,
			     const char *password,
			     const char *service_name,
			     kadm5_config_params *realm_params,
			     unsigned long struct_version,
			     unsigned long api_version,
			     void **server_handle)
{
    return kadm5_c_init_with_password_ctx(context,
					  client_name,
					  password,
					  service_name,
					  realm_params,
					  struct_version,
					  api_version,
					  server_handle);
}

kadm5_ret_t
kadm5_init_with_skey(const char *client_name,
		     const char *keytab,
		     const char *service_name,
		     kadm5_config_params *realm_params,
		     unsigned long struct_version,
		     unsigned long api_version,
		     void **server_handle)
{
    return kadm5_c_init_with_skey(client_name,
				  keytab,
				  service_name,
				  realm_params,
				  struct_version,
				  api_version,
				  server_handle);
}

kadm5_ret_t
kadm5_init_with_skey_ctx(krb5_context context,
			 const char *client_name,
			 const char *keytab,
			 const char *service_name,
			 kadm5_config_params *realm_params,
			 unsigned long struct_version,
			 unsigned long api_version,
			 void **server_handle)
{
    return kadm5_c_init_with_skey_ctx(context,
				      client_name,
				      keytab,
				      service_name,
				      realm_params,
				      struct_version,
				      api_version,
				      server_handle);
}

kadm5_ret_t
kadm5_init_with_creds(const char *client_name,
		      krb5_ccache ccache,
		      const char *service_name,
		      kadm5_config_params *realm_params,
		      unsigned long struct_version,
		      unsigned long api_version,
		      void **server_handle)
{
    return kadm5_c_init_with_creds(client_name,
				   ccache,
				   service_name,
				   realm_params,
				   struct_version,
				   api_version,
				   server_handle);
}

kadm5_ret_t
kadm5_init_with_creds_ctx(krb5_context context,
			  const char *client_name,
			  krb5_ccache ccache,
			  const char *service_name,
			  kadm5_config_params *realm_params,
			  unsigned long struct_version,
			  unsigned long api_version,
			  void **server_handle)
{
    return kadm5_c_init_with_creds_ctx(context,
				       client_name,
				       ccache,
				       service_name,
				       realm_params,
				       struct_version,
				       api_version,
				       server_handle);
}
