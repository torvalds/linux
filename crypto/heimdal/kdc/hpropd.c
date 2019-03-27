/*
 * Copyright (c) 1997-2006 Kungliga Tekniska Högskolan
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

#include "hprop.h"

static int inetd_flag = -1;
static int help_flag;
static int version_flag;
static int print_dump;
static const char *database;
static int from_stdin;
static char *local_realm;
static char *ktname = NULL;

struct getargs args[] = {
    { "database", 'd', arg_string, rk_UNCONST(&database), "database", "file" },
    { "stdin",    'n', arg_flag, &from_stdin, "read from stdin", NULL },
    { "print",	    0, arg_flag, &print_dump, "print dump to stdout", NULL },
#ifdef SUPPORT_INETD
    { "inetd",	   'i',	arg_negative_flag,	&inetd_flag,
      "Not started from inetd", NULL },
#endif
    { "keytab",   'k',	arg_string, &ktname,	"keytab to use for authentication", "keytab" },
    { "realm",   'r',	arg_string, &local_realm, "realm to use", NULL },
    { "version",    0, arg_flag, &version_flag, NULL, NULL },
    { "help",    'h',  arg_flag, &help_flag, NULL, NULL}
};

static int num_args = sizeof(args) / sizeof(args[0]);
static char unparseable_name[] = "unparseable name";

static void
usage(int ret)
{
    arg_printusage (args, num_args, NULL, "");
    exit (ret);
}

int
main(int argc, char **argv)
{
    krb5_error_code ret;
    krb5_context context;
    krb5_auth_context ac = NULL;
    krb5_principal c1, c2;
    krb5_authenticator authent;
    krb5_keytab keytab;
    krb5_socket_t sock = rk_INVALID_SOCKET;
    HDB *db = NULL;
    int optidx = 0;
    char *tmp_db;
    krb5_log_facility *fac;
    int nprincs;

    setprogname(argv[0]);

    ret = krb5_init_context(&context);
    if(ret)
	exit(1);

    ret = krb5_openlog(context, "hpropd", &fac);
    if(ret)
	errx(1, "krb5_openlog");
    krb5_set_warn_dest(context, fac);

    if(getarg(args, num_args, argc, argv, &optidx))
	usage(1);

    if(local_realm != NULL)
	krb5_set_default_realm(context, local_realm);

    if(help_flag)
	usage(0);
    if(version_flag) {
	print_version(NULL);
	exit(0);
    }

    argc -= optidx;
    argv += optidx;

    if (argc != 0)
	usage(1);

    if (database == NULL)
	database = hdb_default_db(context);

    if(from_stdin) {
	sock = STDIN_FILENO;
    } else {
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t sin_len = sizeof(ss);
	char addr_name[256];
	krb5_ticket *ticket;
	char *server;

	sock = STDIN_FILENO;
#ifdef SUPPORT_INETD
	if (inetd_flag == -1) {
	    if (getpeername (sock, sa, &sin_len) < 0) {
		inetd_flag = 0;
	    } else {
		inetd_flag = 1;
	    }
	}
#else
	inetd_flag = 0;
#endif
	if (!inetd_flag) {
	    mini_inetd (krb5_getportbyname (context, "hprop", "tcp",
					    HPROP_PORT), &sock);
	}
	sin_len = sizeof(ss);
	if(getpeername(sock, sa, &sin_len) < 0)
	    krb5_err(context, 1, errno, "getpeername");

	if (inet_ntop(sa->sa_family,
		      socket_get_address (sa),
		      addr_name,
		      sizeof(addr_name)) == NULL)
	    strlcpy (addr_name, "unknown address",
		     sizeof(addr_name));

	krb5_log(context, fac, 0, "Connection from %s", addr_name);

	ret = krb5_kt_register(context, &hdb_kt_ops);
	if(ret)
	    krb5_err(context, 1, ret, "krb5_kt_register");

	if (ktname != NULL) {
	    ret = krb5_kt_resolve(context, ktname, &keytab);
	    if (ret)
		krb5_err (context, 1, ret, "krb5_kt_resolve %s", ktname);
	} else {
	    ret = krb5_kt_default (context, &keytab);
	    if (ret)
		krb5_err (context, 1, ret, "krb5_kt_default");
	}

	ret = krb5_recvauth(context, &ac, &sock, HPROP_VERSION, NULL,
			    0, keytab, &ticket);
	if(ret)
	    krb5_err(context, 1, ret, "krb5_recvauth");

	ret = krb5_unparse_name(context, ticket->server, &server);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_unparse_name");
	if (strncmp(server, "hprop/", 5) != 0)
	    krb5_errx(context, 1, "ticket not for hprop (%s)", server);

	free(server);
	krb5_free_ticket (context, ticket);

	ret = krb5_auth_con_getauthenticator(context, ac, &authent);
	if(ret)
	    krb5_err(context, 1, ret, "krb5_auth_con_getauthenticator");

	ret = krb5_make_principal(context, &c1, NULL, "kadmin", "hprop", NULL);
	if(ret)
	    krb5_err(context, 1, ret, "krb5_make_principal");
	_krb5_principalname2krb5_principal(context, &c2,
					   authent->cname, authent->crealm);
	if(!krb5_principal_compare(context, c1, c2)) {
	    char *s;
	    ret = krb5_unparse_name(context, c2, &s);
	    if (ret)
		s = unparseable_name;
	    krb5_errx(context, 1, "Unauthorized connection from %s", s);
	}
	krb5_free_principal(context, c1);
	krb5_free_principal(context, c2);

	ret = krb5_kt_close(context, keytab);
	if(ret)
	    krb5_err(context, 1, ret, "krb5_kt_close");
    }

    if(!print_dump) {
	asprintf(&tmp_db, "%s~", database);

	ret = hdb_create(context, &db, tmp_db);
	if(ret)
	    krb5_err(context, 1, ret, "hdb_create(%s)", tmp_db);
	ret = db->hdb_open(context, db, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if(ret)
	    krb5_err(context, 1, ret, "hdb_open(%s)", tmp_db);
    }

    nprincs = 0;
    while(1){
	krb5_data data;
	hdb_entry_ex entry;

	if(from_stdin) {
	    ret = krb5_read_message(context, &sock, &data);
	    if(ret != 0 && ret != HEIM_ERR_EOF)
		krb5_err(context, 1, ret, "krb5_read_message");
	} else {
	    ret = krb5_read_priv_message(context, ac, &sock, &data);
	    if(ret)
		krb5_err(context, 1, ret, "krb5_read_priv_message");
	}

	if(ret == HEIM_ERR_EOF || data.length == 0) {
	    if(!from_stdin) {
		data.data = NULL;
		data.length = 0;
		krb5_write_priv_message(context, ac, &sock, &data);
	    }
	    if(!print_dump) {
		ret = db->hdb_close(context, db);
		if(ret)
		    krb5_err(context, 1, ret, "db_close");
		ret = db->hdb_rename(context, db, database);
		if(ret)
		    krb5_err(context, 1, ret, "db_rename");
	    }
	    break;
	}
	memset(&entry, 0, sizeof(entry));
	ret = hdb_value2entry(context, &data, &entry.entry);
	krb5_data_free(&data);
	if(ret)
	    krb5_err(context, 1, ret, "hdb_value2entry");
	if(print_dump)
	    hdb_print_entry(context, db, &entry, stdout);
	else {
	    ret = db->hdb_store(context, db, 0, &entry);
	    if(ret == HDB_ERR_EXISTS) {
		char *s;
		ret = krb5_unparse_name(context, entry.entry.principal, &s);
		if (ret)
		    s = strdup(unparseable_name);
		krb5_warnx(context, "Entry exists: %s", s);
		free(s);
	    } else if(ret)
		krb5_err(context, 1, ret, "db_store");
	    else
		nprincs++;
	}
	hdb_free_entry(context, &entry);
    }
    if (!print_dump)
	krb5_log(context, fac, 0, "Received %d principals", nprincs);

    if (inetd_flag == 0)
	rk_closesocket(sock);

    exit(0);
}
