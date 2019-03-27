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
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>
#include <gssapi/gssapi_spnego.h>
#include "gss_common.h"
RCSID("$Id$");

static int
process_it(int sock,
	   gss_ctx_id_t context_hdl,
	   gss_name_t client_name
	   )
{
    OM_uint32 maj_stat, min_stat;
    gss_buffer_desc real_input_token, real_output_token;
    gss_buffer_t input_token = &real_input_token,
	output_token = &real_output_token;
    gss_name_t server_name;
    int conf_flag;

    print_gss_name("User is", client_name);

    maj_stat = gss_inquire_context(&min_stat,
				   context_hdl,
				   NULL,
				   &server_name,
				   NULL,
				   NULL,
				   NULL,
				   NULL,
				   NULL);
    if (GSS_ERROR(maj_stat))
	gss_err (1, min_stat, "gss_inquire_context");

    print_gss_name("Server is", server_name);

    maj_stat = gss_release_name(&min_stat, &server_name);
    if (GSS_ERROR(maj_stat))
	gss_err (1, min_stat, "gss_release_name");

    /* gss_verify_mic */

    read_token (sock, input_token);
    read_token (sock, output_token);

    maj_stat = gss_verify_mic (&min_stat,
			       context_hdl,
			       input_token,
			       output_token,
			       NULL);
    if (GSS_ERROR(maj_stat))
	gss_err (1, min_stat, "gss_verify_mic");

    fprintf (stderr, "gss_verify_mic: %.*s\n", (int)input_token->length,
	    (char *)input_token->value);

    gss_release_buffer (&min_stat, input_token);
    gss_release_buffer (&min_stat, output_token);

    /* gss_unwrap */

    read_token (sock, input_token);

    maj_stat = gss_unwrap (&min_stat,
			   context_hdl,
			   input_token,
			   output_token,
			   &conf_flag,
			   NULL);
    if(GSS_ERROR(maj_stat))
	gss_err (1, min_stat, "gss_unwrap");

    fprintf (stderr, "gss_unwrap: %.*s %s\n", (int)output_token->length,
	    (char *)output_token->value,
	     conf_flag ? "CONF" : "INT");

    gss_release_buffer (&min_stat, input_token);
    gss_release_buffer (&min_stat, output_token);

    read_token (sock, input_token);

    maj_stat = gss_unwrap (&min_stat,
			   context_hdl,
			   input_token,
			   output_token,
			   &conf_flag,
			   NULL);
    if(GSS_ERROR(maj_stat))
	gss_err (1, min_stat, "gss_unwrap");

    fprintf (stderr, "gss_unwrap: %.*s %s\n", (int)output_token->length,
	     (char *)output_token->value,
	     conf_flag ? "CONF" : "INT");

    gss_release_buffer (&min_stat, input_token);
    gss_release_buffer (&min_stat, output_token);

    return 0;
}

static int
proto (int sock, const char *service)
{
    struct sockaddr_in remote, local;
    socklen_t addrlen;
    gss_ctx_id_t context_hdl = GSS_C_NO_CONTEXT;
    gss_buffer_desc real_input_token, real_output_token;
    gss_buffer_t input_token = &real_input_token,
	output_token = &real_output_token;
    OM_uint32 maj_stat, min_stat;
    gss_name_t client_name;
    struct gss_channel_bindings_struct input_chan_bindings;
    gss_cred_id_t delegated_cred_handle = NULL;
    krb5_ccache ccache;
    u_char init_buf[4];
    u_char acct_buf[4];
    gss_OID mech_oid;
    char *mech, *p;

    addrlen = sizeof(local);
    if (getsockname (sock, (struct sockaddr *)&local, &addrlen) < 0
	|| addrlen != sizeof(local))
	err (1, "getsockname)");

    addrlen = sizeof(remote);
    if (getpeername (sock, (struct sockaddr *)&remote, &addrlen) < 0
	|| addrlen != sizeof(remote))
	err (1, "getpeername");

    input_chan_bindings.initiator_addrtype = GSS_C_AF_INET;
    input_chan_bindings.initiator_address.length = 4;
    init_buf[0] = (remote.sin_addr.s_addr >> 24) & 0xFF;
    init_buf[1] = (remote.sin_addr.s_addr >> 16) & 0xFF;
    init_buf[2] = (remote.sin_addr.s_addr >>  8) & 0xFF;
    init_buf[3] = (remote.sin_addr.s_addr >>  0) & 0xFF;

    input_chan_bindings.initiator_address.value = init_buf;
    input_chan_bindings.acceptor_addrtype = GSS_C_AF_INET;

    input_chan_bindings.acceptor_address.length = 4;
    acct_buf[0] = (local.sin_addr.s_addr >> 24) & 0xFF;
    acct_buf[1] = (local.sin_addr.s_addr >> 16) & 0xFF;
    acct_buf[2] = (local.sin_addr.s_addr >>  8) & 0xFF;
    acct_buf[3] = (local.sin_addr.s_addr >>  0) & 0xFF;
    input_chan_bindings.acceptor_address.value = acct_buf;
    input_chan_bindings.application_data.value = emalloc(4);
#if 0
    * (unsigned short *)input_chan_bindings.application_data.value =
                          remote.sin_port;
    * ((unsigned short *)input_chan_bindings.application_data.value + 1) =
                          local.sin_port;
    input_chan_bindings.application_data.length = 4;
#else
    input_chan_bindings.application_data.length = 0;
    input_chan_bindings.application_data.value = NULL;
#endif

    delegated_cred_handle = GSS_C_NO_CREDENTIAL;

    do {
	read_token (sock, input_token);
	maj_stat =
	    gss_accept_sec_context (&min_stat,
				    &context_hdl,
				    GSS_C_NO_CREDENTIAL,
				    input_token,
				    &input_chan_bindings,
				    &client_name,
				    &mech_oid,
				    output_token,
				    NULL,
				    NULL,
				    &delegated_cred_handle);
	if(GSS_ERROR(maj_stat))
	    gss_err (1, min_stat, "gss_accept_sec_context");
	if (output_token->length != 0)
	    write_token (sock, output_token);
	if (GSS_ERROR(maj_stat)) {
	    if (context_hdl != GSS_C_NO_CONTEXT)
		gss_delete_sec_context (&min_stat,
					&context_hdl,
					GSS_C_NO_BUFFER);
	    break;
	}
    } while(maj_stat & GSS_S_CONTINUE_NEEDED);

    p = (char *)mech_oid->elements;
    if (mech_oid->length == GSS_KRB5_MECHANISM->length
	&& memcmp(p, GSS_KRB5_MECHANISM->elements, mech_oid->length) == 0)
	mech = "Kerberos 5";
    else if (mech_oid->length == GSS_SPNEGO_MECHANISM->length
	&& memcmp(p, GSS_SPNEGO_MECHANISM->elements, mech_oid->length) == 0)
	mech = "SPNEGO"; /* XXX Silly, wont show up */
    else
	mech = "Unknown";

    printf("Using mech: %s\n", mech);

    if (delegated_cred_handle != GSS_C_NO_CREDENTIAL) {
       krb5_context context;

       printf("Delegated cred found\n");

       maj_stat = krb5_init_context(&context);
       maj_stat = krb5_cc_resolve(context, "FILE:/tmp/krb5cc_test", &ccache);
       maj_stat = gss_krb5_copy_ccache(&min_stat,
				       delegated_cred_handle,
				       ccache);
       if (maj_stat == 0) {
	   krb5_principal p;
	   maj_stat = krb5_cc_get_principal(context, ccache, &p);
	   if (maj_stat == 0) {
	       char *name;
	       maj_stat = krb5_unparse_name(context, p, &name);
	       if (maj_stat == 0) {
		   printf("Delegated user is: `%s'\n", name);
		   free(name);
	       }
	       krb5_free_principal(context, p);
	   }
       }
       krb5_cc_close(context, ccache);
       gss_release_cred(&min_stat, &delegated_cred_handle);
    }

    if (fork_flag) {
	pid_t pid;
	int pipefd[2];

	if (pipe (pipefd) < 0)
	    err (1, "pipe");

	pid = fork ();
	if (pid < 0)
	    err (1, "fork");
	if (pid != 0) {
	    gss_buffer_desc buf;

	    maj_stat = gss_export_sec_context (&min_stat,
					       &context_hdl,
					       &buf);
	    if (GSS_ERROR(maj_stat))
		gss_err (1, min_stat, "gss_export_sec_context");
	    write_token (pipefd[1], &buf);
	    exit (0);
	} else {
	    gss_ctx_id_t context_hdl;
	    gss_buffer_desc buf;

	    close (pipefd[1]);
	    read_token (pipefd[0], &buf);
	    close (pipefd[0]);
	    maj_stat = gss_import_sec_context (&min_stat, &buf, &context_hdl);
	    if (GSS_ERROR(maj_stat))
		gss_err (1, min_stat, "gss_import_sec_context");
	    gss_release_buffer (&min_stat, &buf);
	    return process_it (sock, context_hdl, client_name);
	}
    } else {
	return process_it (sock, context_hdl, client_name);
    }
}

static int
doit (int port, const char *service)
{
    int sock, sock2;
    struct sockaddr_in my_addr;
    int one = 1;
    int ret;

    sock = socket (AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
	err (1, "socket");

    memset (&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family      = AF_INET;
    my_addr.sin_port        = port;
    my_addr.sin_addr.s_addr = INADDR_ANY;

    if (setsockopt (sock, SOL_SOCKET, SO_REUSEADDR,
		    (void *)&one, sizeof(one)) < 0)
	warn ("setsockopt SO_REUSEADDR");

    if (bind (sock, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0)
	err (1, "bind");

    while (1) {
        if (listen (sock, 1) < 0)
	    err (1, "listen");

        sock2 = accept (sock, NULL, NULL);
        if (sock2 < 0)
	    err (1, "accept");

        ret = proto (sock2, service);
    }
    return ret;
}

int
main(int argc, char **argv)
{
    krb5_context context = NULL; /* XXX */
    int port = server_setup(&context, argc, argv);
    return doit (port, service);
}

