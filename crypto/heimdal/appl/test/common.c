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

static int help_flag;
static int version_flag;
static char *port_str;
static char *keytab_str;
krb5_keytab keytab;
char *service = SERVICE;
char *mech = "krb5";
int fork_flag;
char *password = NULL;

static struct getargs args[] = {
    { "port", 'p', arg_string, &port_str, "port to listen to", "port" },
    { "service", 's', arg_string, &service, "service to use", "service" },
    { "keytab", 'k', arg_string, &keytab_str, "keytab to use", "keytab" },
    { "mech", 'm', arg_string, &mech, "gssapi mech to use", "mech" },
    { "password", 'P', arg_string, &password, "password to use", "password" },
    { "fork", 'f', arg_flag, &fork_flag, "do fork" },
    { "help", 'h', arg_flag, &help_flag },
    { "version", 0, arg_flag, &version_flag }
};

static int num_args = sizeof(args) / sizeof(args[0]);

static void
server_usage(int code, struct getargs *args, int num_args)
{
    arg_printusage(args, num_args, NULL, "");
    exit(code);
}

static void
client_usage(int code, struct getargs *args, int num_args)
{
    arg_printusage(args, num_args, NULL, "host");
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

int
server_setup(krb5_context *context, int argc, char **argv)
{
    int port = common_setup(context, &argc, argv, server_usage);
    krb5_error_code ret;

    if(argv[argc] != NULL)
	server_usage(1, args, num_args);
    if (keytab_str != NULL)
	ret = krb5_kt_resolve (*context, keytab_str, &keytab);
    else
	ret = krb5_kt_default (*context, &keytab);
    if (ret)
	krb5_err (*context, 1, ret, "krb5_kt_resolve/default");
    return port;
}

int
client_setup(krb5_context *context, int *argc, char **argv)
{
    int optind = *argc;
    int port = common_setup(context, &optind, argv, client_usage);
    if(*argc - optind != 1)
	client_usage(1, args, num_args);
    *argc = optind;
    return port;
}

int
client_doit (const char *hostname, int port, const char *service,
	     int (*func)(int, const char *hostname, const char *service))
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
	errx (1, "%s: %s", hostname, gai_strerror(error));
	return -1;
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
	return (*func) (s, hostname, service);
    }
    warnx ("failed to contact %s", hostname);
    freeaddrinfo (ai);
    return 1;
}
