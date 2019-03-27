/*
 * Copyright (c) 1997 - 2002 Kungliga Tekniska Högskolan
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
char krb5_tkfile[MAXPATHLEN];

static int help_flag;
static int version_flag;
static char *port_str;
char *service = KF_SERVICE;
int do_inetd = 0;
static char *regpag_str=NULL;

static struct getargs args[] = {
    { "port", 'p', arg_string, &port_str, "port to listen to", "port" },
    { "inetd",'i',arg_flag, &do_inetd,
       "Not started from inetd", NULL },
    { "regpag",'R',arg_string,&regpag_str,"path to regpag binary","regpag"},
    { "help", 'h', arg_flag, &help_flag },
    { "version", 0, arg_flag, &version_flag }
};

static int num_args = sizeof(args) / sizeof(args[0]);

static void
usage(int code, struct getargs *args, int num_args)
{
    arg_printusage(args, num_args, NULL, "");
    exit(code);
}

static int
server_setup(krb5_context *context, int argc, char **argv)
{
    int port = 0;
    int local_argc;

    local_argc = krb5_program_setup(context, argc, argv, args, num_args, usage);

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
	port = krb5_getportbyname (*context, KF_PORT_NAME, "tcp", KF_PORT_NUM);

    if(argv[local_argc] != NULL)
        usage(1, args, num_args);

    return port;
}

static int protocol_version;

static krb5_boolean
kfd_match_version(const void *arg, const char *version)
{
    if(strcmp(version, KF_VERSION_1) == 0) {
	protocol_version = 1;
	return TRUE;
    } else if (strlen(version) == 4 &&
	       version[0] == '0' &&
	       version[1] == '.' &&
	       (version[2] == '4' || version[2] == '3') &&
	       islower((unsigned char)version[3])) {
	protocol_version = 0;
	return TRUE;
    }
    return FALSE;
}

static int
proto (int sock, const char *service)
{
    krb5_auth_context auth_context;
    krb5_error_code status;
    krb5_principal server;
    krb5_ticket *ticket;
    char *name;
    char ret_string[10];
    char hostname[MAXHOSTNAMELEN];
    krb5_data data;
    krb5_data remotename;
    krb5_data tk_file;
    krb5_ccache ccache;
    char ccname[MAXPATHLEN];
    struct passwd *pwd;

    status = krb5_auth_con_init (context, &auth_context);
    if (status)
	krb5_err(context, 1, status, "krb5_auth_con_init");

    status = krb5_auth_con_setaddrs_from_fd (context,
					     auth_context,
					     &sock);
    if (status)
	krb5_err(context, 1, status, "krb5_auth_con_setaddr");

    if(gethostname (hostname, sizeof(hostname)) < 0)
	krb5_err(context, 1, errno, "gethostname");

    status = krb5_sname_to_principal (context,
				      hostname,
				      service,
				      KRB5_NT_SRV_HST,
				      &server);
    if (status)
	krb5_err(context, 1, status, "krb5_sname_to_principal");

    status = krb5_recvauth_match_version (context,
					  &auth_context,
					  &sock,
					  kfd_match_version,
					  NULL,
					  server,
					  0,
					  NULL,
					  &ticket);
    if (status)
	krb5_err(context, 1, status, "krb5_recvauth");

    status = krb5_unparse_name (context,
				ticket->client,
				&name);
    if (status)
	krb5_err(context, 1, status, "krb5_unparse_name");

    if(protocol_version == 0) {
	data.data = "old clnt"; /* XXX old clients only had room for
                                   10 bytes of message, and also
                                   didn't show it to the user */
	data.length = strlen(data.data) + 1;
	krb5_write_message(context, &sock, &data);
	sleep(2); /* XXX give client time to finish */
	krb5_errx(context, 1, "old client; exiting");
    }

    status=krb5_read_priv_message (context, auth_context,
				   &sock, &remotename);
    if (status)
	krb5_err(context, 1, status, "krb5_read_message");
    status=krb5_read_priv_message (context, auth_context,
				   &sock, &tk_file);
    if (status)
	krb5_err(context, 1, status, "krb5_read_message");

    krb5_data_zero (&data);

    if(((char*)remotename.data)[remotename.length-1] != '\0')
	krb5_errx(context, 1, "unterminated received");
    if(((char*)tk_file.data)[tk_file.length-1] != '\0')
	krb5_errx(context, 1, "unterminated received");

    status = krb5_read_priv_message(context, auth_context, &sock, &data);

    if (status) {
	krb5_err(context, 1, errno, "krb5_read_priv_message");
	goto out;
    }

    pwd = getpwnam ((char *)(remotename.data));
    if (pwd == NULL) {
	status=1;
	krb5_warnx(context, "getpwnam: %s failed",(char *)(remotename.data));
	goto out;
    }

    if(!krb5_kuserok (context,
		      ticket->client,
		      (char *)(remotename.data))) {
	status=1;
	krb5_warnx(context, "krb5_kuserok: permission denied");
	goto out;
    }

    if (setgid(pwd->pw_gid) < 0) {
	krb5_warn(context, errno, "setgid");
	goto out;
    }
    if (setuid(pwd->pw_uid) < 0) {
	krb5_warn(context, errno, "setuid");
	goto out;
    }

    if (tk_file.length != 1)
	snprintf (ccname, sizeof(ccname), "%s", (char *)(tk_file.data));
    else
	snprintf (ccname, sizeof(ccname), "FILE:/tmp/krb5cc_%lu",
		  (unsigned long)pwd->pw_uid);

    status = krb5_cc_resolve (context, ccname, &ccache);
    if (status) {
	krb5_warn(context, status, "krb5_cc_resolve");
        goto out;
    }
    status = krb5_cc_initialize (context, ccache, ticket->client);
    if (status) {
	krb5_warn(context, status, "krb5_cc_initialize");
        goto out;
    }
    status = krb5_rd_cred2 (context, auth_context, ccache, &data);
    krb5_cc_close (context, ccache);
    if (status) {
	krb5_warn(context, status, "krb5_rd_cred");
        goto out;

    }
    strlcpy(krb5_tkfile,ccname,sizeof(krb5_tkfile));
    krb5_warnx(context, "%s forwarded ticket to %s,%s",
	       name,
	       (char *)(remotename.data),ccname);
  out:
    if (status) {
	strlcpy(ret_string, "no", sizeof(ret_string));
	krb5_warnx(context, "failed");
    } else  {
	strlcpy(ret_string, "ok", sizeof(ret_string));
    }

    krb5_data_free (&tk_file);
    krb5_data_free (&remotename);
    krb5_data_free (&data);
    free(name);

    data.data = ret_string;
    data.length = strlen(ret_string) + 1;
    status = krb5_write_priv_message(context, auth_context, &sock, &data);
    krb5_auth_con_free(context, auth_context);

    return status;
}

static int
doit (int port, const char *service)
{
    if (do_inetd)
	mini_inetd(port, NULL);
    return proto (STDIN_FILENO, service);
}

int
main(int argc, char **argv)
{
    int port;
    int ret;
    krb5_log_facility *fac;

    setprogname (argv[0]);
    roken_openlog (argv[0], LOG_ODELAY | LOG_PID,LOG_AUTH);
    port = server_setup(&context, argc, argv);
    ret = krb5_openlog(context, "kfd", &fac);
    if(ret) krb5_err(context, 1, ret, "krb5_openlog");
    ret = krb5_set_warn_dest(context, fac);
    if(ret) krb5_err(context, 1, ret, "krb5_set_warn_dest");

    ret = doit (port, service);
    closelog();
    if (ret == 0 && regpag_str != NULL)
        ret = execl(regpag_str, "regpag", "-t", krb5_tkfile, "-r", NULL);
    return ret;
}
