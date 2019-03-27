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

#include "test_locl.h"
RCSID("$Id$");

krb5_context context;

static int
proto (int sock, const char *hostname, const char *service)
{
    struct sockaddr_in remote, local;
    socklen_t addrlen;
    krb5_address remote_addr, local_addr;
    krb5_context context;
    krb5_ccache ccache;
    krb5_auth_context auth_context;
    krb5_error_code status;
    krb5_principal client;
    krb5_data data;
    krb5_data packet;
    krb5_creds mcred, cred;
    krb5_ticket *ticket;

    addrlen = sizeof(local);
    if (getsockname (sock, (struct sockaddr *)&local, &addrlen) < 0
	|| addrlen != sizeof(local))
	err (1, "getsockname(%s)", hostname);

    addrlen = sizeof(remote);
    if (getpeername (sock, (struct sockaddr *)&remote, &addrlen) < 0
	|| addrlen != sizeof(remote))
	err (1, "getpeername(%s)", hostname);

    status = krb5_init_context(&context);
    if (status)
	errx(1, "krb5_init_context failed: %d", status);

    status = krb5_cc_default (context, &ccache);
    if (status)
	krb5_err(context, 1, status, "krb5_cc_default");

    status = krb5_auth_con_init (context, &auth_context);
    if (status)
	krb5_err(context, 1, status, "krb5_auth_con_init");

    local_addr.addr_type = AF_INET;
    local_addr.address.length = sizeof(local.sin_addr);
    local_addr.address.data   = &local.sin_addr;

    remote_addr.addr_type = AF_INET;
    remote_addr.address.length = sizeof(remote.sin_addr);
    remote_addr.address.data   = &remote.sin_addr;

    status = krb5_auth_con_setaddrs (context,
				     auth_context,
				     &local_addr,
				     &remote_addr);
    if (status)
	krb5_err(context, 1, status, "krb5_auth_con_setaddr");

    krb5_cc_clear_mcred(&mcred);

    status = krb5_cc_get_principal(context, ccache, &client);
    if(status)
	krb5_err(context, 1, status, "krb5_cc_get_principal");
    status = krb5_make_principal(context, &mcred.server,
				 krb5_principal_get_realm(context, client),
				 "krbtgt",
				 krb5_principal_get_realm(context, client),
				 NULL);
    if(status)
	krb5_err(context, 1, status, "krb5_make_principal");
    mcred.client = client;

    status = krb5_cc_retrieve_cred(context, ccache, 0, &mcred, &cred);
    if(status)
	krb5_err(context, 1, status, "krb5_cc_retrieve_cred");

    {
	char *client_name;
	krb5_data data;
	status = krb5_unparse_name(context, cred.client, &client_name);
	if(status)
	    krb5_err(context, 1, status, "krb5_unparse_name");
	data.data = client_name;
	data.length = strlen(client_name) + 1;
	status = krb5_write_message(context, &sock, &data);
	if(status)
	    krb5_err(context, 1, status, "krb5_write_message");
	free(client_name);
    }

    status = krb5_write_message(context, &sock, &cred.ticket);
    if(status)
	krb5_err(context, 1, status, "krb5_write_message");

    status = krb5_auth_con_setuserkey(context, auth_context, &cred.session);
    if(status)
	krb5_err(context, 1, status, "krb5_auth_con_setuserkey");

    status = krb5_recvauth(context, &auth_context, &sock,
			   VERSION, client, 0, NULL, &ticket);

    if (status)
	krb5_err(context, 1, status, "krb5_recvauth");

    if (ticket->ticket.authorization_data) {
	AuthorizationData *authz;
	int i;

	printf("Authorization data:\n");

	authz = ticket->ticket.authorization_data;
	for (i = 0; i < authz->len; i++) {
	    printf("\ttype %d, length %lu\n",
		   authz->val[i].ad_type,
		   (unsigned long)authz->val[i].ad_data.length);
	}
    }

    data.data   = "hej";
    data.length = 3;

    krb5_data_zero (&packet);

    status = krb5_mk_safe (context,
			   auth_context,
			   &data,
			   &packet,
			   NULL);
    if (status)
	krb5_err(context, 1, status, "krb5_mk_safe");

    status = krb5_write_message(context, &sock, &packet);
    if(status)
	krb5_err(context, 1, status, "krb5_write_message");

    data.data   = "hemligt";
    data.length = 7;

    krb5_data_free (&packet);

    status = krb5_mk_priv (context,
			   auth_context,
			   &data,
			   &packet,
			   NULL);
    if (status)
	krb5_err(context, 1, status, "krb5_mk_priv");

    status = krb5_write_message(context, &sock, &packet);
    if(status)
	krb5_err(context, 1, status, "krb5_write_message");
    return 0;
}

int
main(int argc, char **argv)
{
    int port = client_setup(&context, &argc, argv);
    return client_doit (argv[argc], port, service, proto);
}
