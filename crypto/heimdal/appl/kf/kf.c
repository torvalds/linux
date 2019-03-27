/*
 * Copyright (c) 1997 - 2000, 2002 Kungliga Tekniska Högskolan
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

#include "kf_locl.h"
RCSID("$Id$");

krb5_context context;
static int help_flag;
static int version_flag;
static char *port_str;
const char *service     = KF_SERVICE;
const char *remote_name = NULL;
int forwardable   = 0;
const char *ccache_name = NULL;

static struct getargs args[] = {
    { "port", 'p', arg_string, &port_str, "port to connect to", "port" },
    { "login", 'l',arg_string, &remote_name,"remote login name","login"},
    { "ccache", 'c',arg_string, &ccache_name, "remote cred cache","ccache"},
    { "forwardable",'F',arg_flag,&forwardable,
       "Forward forwardable credentials", NULL },
    { "forwardable",'G',arg_negative_flag,&forwardable,
       "Don't forward forwardable credentials", NULL },
    { "help", 'h', arg_flag, &help_flag },
    { "version", 0, arg_flag, &version_flag }
};

static int num_args = sizeof(args) / sizeof(args[0]);

static void
usage(int code, struct getargs *args, int num_args)
{
    arg_printusage(args, num_args, NULL, "hosts");
    exit(code);
}

static int
client_setup(krb5_context *context, int *argc, char **argv)
{
    int optind = 0;
    int port = 0;
    int status;

    setprogname (argv[0]);

    status = krb5_init_context (context);
    if (status)
	errx(1, "krb5_init_context failed: %d", status);

    forwardable = krb5_config_get_bool (*context, NULL,
					"libdefaults",
					"forwardable",
					NULL);

    if (getarg (args, num_args, *argc, argv, &optind))
	usage(1, args, num_args);

    if(help_flag)
	usage (0, args, num_args);
    if(version_flag) {
	print_version(NULL);
	exit(0);
    }

    if(port_str) {
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
	port = krb5_getportbyname (*context, KF_PORT_NAME, "tcp", KF_PORT_NUM);

    if(*argc - optind < 1)
        usage(1, args, num_args);
    *argc = optind;

    return port;
}

/*
 * forward creds to `hostname'/`service' over `sock'
 * return 0 iff OK
 */

static int
proto (int sock, const char *hostname, const char *service,
       char *message, size_t len)
{
    krb5_auth_context auth_context;
    krb5_error_code status;
    krb5_principal server;
    krb5_data data;
    krb5_data data_send;

    krb5_ccache     ccache;
    krb5_creds      creds;
    krb5_kdc_flags  flags;
    krb5_principal  principal;

    status = krb5_auth_con_init (context, &auth_context);
    if (status) {
	krb5_warn (context, status, "krb5_auth_con_init");
	return 1;
    }

    status = krb5_auth_con_setaddrs_from_fd (context,
					     auth_context,
					     &sock);
    if (status) {
	krb5_auth_con_free(context, auth_context);
	krb5_warn (context, status, "krb5_auth_con_setaddr");
	return 1;
    }

    status = krb5_sname_to_principal (context,
				      hostname,
				      service,
				      KRB5_NT_SRV_HST,
				      &server);
    if (status) {
	krb5_auth_con_free(context, auth_context);
	krb5_warn (context, status, "krb5_sname_to_principal");
	return 1;
    }

    status = krb5_sendauth (context,
			    &auth_context,
			    &sock,
			    KF_VERSION_1,
			    NULL,
			    server,
			    AP_OPTS_MUTUAL_REQUIRED | AP_OPTS_USE_SUBKEY,
			    NULL,
			    NULL,
			    NULL,
			    NULL,
			    NULL,
			    NULL);
    if (status) {
	krb5_auth_con_free(context, auth_context);
	krb5_warn(context, status, "krb5_sendauth");
	return 1;
    }

    if (ccache_name == NULL)
	ccache_name = "";

    data_send.data   = (void *)remote_name;
    data_send.length = strlen(remote_name) + 1;
    status = krb5_write_priv_message(context, auth_context, &sock, &data_send);
    if (status) {
	krb5_auth_con_free(context, auth_context);
	krb5_warn (context, status, "krb5_write_message");
	return 1;
    }
    data_send.data   = (void *)ccache_name;
    data_send.length = strlen(ccache_name)+1;
    status = krb5_write_priv_message(context, auth_context, &sock, &data_send);
    if (status) {
	krb5_auth_con_free(context, auth_context);
	krb5_warn (context, status, "krb5_write_message");
	return 1;
    }

    memset (&creds, 0, sizeof(creds));

    status = krb5_cc_default (context, &ccache);
    if (status) {
	krb5_auth_con_free(context, auth_context);
	krb5_warn (context, status, "krb5_cc_default");
	return 1;
    }

    status = krb5_cc_get_principal (context, ccache, &principal);
    if (status) {
	krb5_auth_con_free(context, auth_context);
	krb5_warn (context, status, "krb5_cc_get_principal");
	return 1;
    }

    creds.client = principal;

    status = krb5_make_principal (context,
				  &creds.server,
				  principal->realm,
				  KRB5_TGS_NAME,
				  principal->realm,
				  NULL);

    if (status) {
	krb5_auth_con_free(context, auth_context);
	krb5_warn (context, status, "krb5_make_principal");
	return 1;
    }

    creds.times.endtime = 0;

    flags.i = 0;
    flags.b.forwarded   = 1;
    flags.b.forwardable = forwardable;

    status = krb5_get_forwarded_creds (context,
				       auth_context,
				       ccache,
				       flags.i,
				       hostname,
				       &creds,
				       &data);
    if (status) {
	krb5_auth_con_free(context, auth_context);
	krb5_warn (context, status, "krb5_get_forwarded_creds");
	return 1;
    }

    status = krb5_write_priv_message(context, auth_context, &sock, &data);

    if (status) {
	krb5_auth_con_free(context, auth_context);
	krb5_warn (context, status, "krb5_mk_priv");
	return 1;
    }

    krb5_data_free (&data);

    status = krb5_read_priv_message(context, auth_context, &sock, &data);
    krb5_auth_con_free(context, auth_context);
    if (status) {
	krb5_warn (context, status, "krb5_mk_priv");
	return 1;
    }
    if(data.length >= len) {
	krb5_warnx (context, "returned string is too long, truncating");
	memcpy(message, data.data, len);
	message[len - 1] = '\0';
    } else {
	memcpy(message, data.data, data.length);
	message[data.length] = '\0';
    }
    krb5_data_free (&data);

    return(strcmp(message, "ok"));
}

static int
doit (const char *hostname, int port, const char *service,
      char *message, size_t len)
{
    struct addrinfo *ai, *a;
    struct addrinfo hints;
    int error;
    char portstr[NI_MAXSERV];

    memset (&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    snprintf (portstr, sizeof(portstr), "%u", ntohs(port));

    error = getaddrinfo (hostname, portstr, &hints, &ai);
    if (error) {
	errx (1, "getaddrinfo(%s): %s", hostname, gai_strerror(error));
    }

    for (a = ai; a != NULL; a = a->ai_next) {
	int s;

	s = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
	if (s < 0)
	    continue;
	if (connect (s, a->ai_addr, a->ai_addrlen) < 0) {
	    warn ("connect(%s)", hostname);
	    close (s);
	    continue;
	}
	freeaddrinfo (ai);
	return proto (s, hostname, service, message, len);
    }
    warnx ("failed to contact %s", hostname);
    freeaddrinfo (ai);
    return 1;
}

int
main(int argc, char **argv)
{
    int argcc,port,i;
    int ret=0;

    argcc = argc;
    port = client_setup(&context, &argcc, argv);

    if (remote_name == NULL) {
	remote_name = get_default_username ();
	if (remote_name == NULL)
	    errx (1, "who are you?");
    }

    for (i = argcc;i < argc; i++) {
	char message[128];
	ret = doit (argv[i], port, service, message, sizeof(message));
	if(ret == 0)
	    warnx ("%s: ok", argv[i]);
	else
	    warnx ("%s: failed: %s", argv[i], message);
    }
    return(ret);
}
