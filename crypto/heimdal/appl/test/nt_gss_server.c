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
#include <krb5.h>
#include "nt_gss_common.h"

RCSID("$Id$");

/*
 * This program tries to act as a server for the sample in `Sample
 * SSPI Code' in Windows 2000 RC1 SDK.
 *
 * use --dump-auth to get a binary dump of the authorization data in the ticket
 */

static int help_flag;
static int version_flag;
static char *port_str;
char *service = SERVICE;
static char *auth_file;

static struct getargs args[] = {
    { "port", 'p', arg_string, &port_str, "port to listen to", "port" },
    { "service", 's', arg_string, &service, "service to use", "service" },
    { "dump-auth", 0, arg_string, &auth_file, "dump authorization data",
      "file" },
    { "help", 'h', arg_flag, &help_flag },
    { "version", 0, arg_flag, &version_flag }
};

static int num_args = sizeof(args) / sizeof(args[0]);

static int
proto (int sock, const char *service)
{
    struct sockaddr_in remote, local;
    socklen_t addrlen;
    gss_ctx_id_t context_hdl = GSS_C_NO_CONTEXT;
    gss_buffer_t input_token, output_token;
    gss_buffer_desc real_input_token, real_output_token;
    OM_uint32 maj_stat, min_stat;
    gss_name_t client_name;
    gss_buffer_desc name_token;

    addrlen = sizeof(local);
    if (getsockname (sock, (struct sockaddr *)&local, &addrlen) < 0
	|| addrlen != sizeof(local))
	err (1, "getsockname)");

    addrlen = sizeof(remote);
    if (getpeername (sock, (struct sockaddr *)&remote, &addrlen) < 0
	|| addrlen != sizeof(remote))
	err (1, "getpeername");

    input_token = &real_input_token;
    output_token = &real_output_token;

    do {
	nt_read_token (sock, input_token);
	maj_stat =
	    gss_accept_sec_context (&min_stat,
				    &context_hdl,
				    GSS_C_NO_CREDENTIAL,
				    input_token,
				    GSS_C_NO_CHANNEL_BINDINGS,
				    &client_name,
				    NULL,
				    output_token,
				    NULL,
				    NULL,
				    NULL);
	if(GSS_ERROR(maj_stat))
	    gss_err (1, min_stat, "gss_accept_sec_context");
	if (output_token->length != 0)
	    nt_write_token (sock, output_token);
	if (GSS_ERROR(maj_stat)) {
	    if (context_hdl != GSS_C_NO_CONTEXT)
		gss_delete_sec_context (&min_stat,
					&context_hdl,
					GSS_C_NO_BUFFER);
	    break;
	}
    } while(maj_stat & GSS_S_CONTINUE_NEEDED);

    if (auth_file != NULL) {
	gss_buffer_desc data;

	maj_stat = gsskrb5_extract_authz_data_from_sec_context(&min_stat,
							       context_hdl,
							       KRB5_AUTHDATA_WIN2K_PAC,
							       &data);
	if (maj_stat == GSS_S_COMPLETE) {
	    rk_dumpdata(auth_file, data.value, data.length);
	    gss_release_buffer(&min_stat, &data);
	}
    }

    maj_stat = gss_display_name (&min_stat,
				 client_name,
				 &name_token,
				 NULL);
    if (GSS_ERROR(maj_stat))
	gss_err (1, min_stat, "gss_display_name");

    fprintf (stderr, "User is `%.*s'\n", (int)name_token.length,
	    (char *)name_token.value);

    /* write something back */

    output_token->value  = strdup ("hejsan");
    output_token->length = strlen (output_token->value) + 1;
    nt_write_token (sock, output_token);

    output_token->value  = strdup ("hoppsan");
    output_token->length = strlen (output_token->value) + 1;
    nt_write_token (sock, output_token);

    return 0;
}

static int
doit (int port, const char *service)
{
    int sock, sock2;
    struct sockaddr_in my_addr;
    int one = 1;

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

    if (listen (sock, 1) < 0)
	err (1, "listen");

    sock2 = accept (sock, NULL, NULL);
    if (sock2 < 0)
	err (1, "accept");

    return proto (sock2, service);
}

static void
usage(int code, struct getargs *args, int num_args)
{
    arg_printusage(args, num_args, NULL, "");
    exit(code);
}

static int
common_setup(krb5_context *context, int *argc, char **argv,
	     void (*usage)(int, struct getargs*, int))
{
    int port = 0;
    *argc = krb5_program_setup(context, *argc, argv, args, num_args, usage);

    if(help_flag)
	(*usage)(0, args, num_args);
    if(version_flag) {
	print_version(NULL);
	exit(0);
    }

    if(port_str){
	struct servent *s = roken_getservbyname(port_str, "tcp");
	if(s)
	    port = s->s_port;
	else {
	    char *ptr;

	    port = strtol (port_str, &ptr, 10);
	    if (port == 0 && ptr == port_str)
		errx (1, "Bad port `%s'", port_str);
	    port = htons(port);
	}
    }

    if (port == 0)
	port = krb5_getportbyname (*context, PORT, "tcp", 4711);

    return port;
}

static int
setup(krb5_context *context, int argc, char **argv)
{
    int port = common_setup(context, &argc, argv, usage);
    if(argv[argc] != NULL)
	usage(1, args, num_args);
    return port;
}

int
main(int argc, char **argv)
{
    krb5_context context = NULL; /* XXX */
    int port = setup(&context, argc, argv);
    return doit (port, service);
}
