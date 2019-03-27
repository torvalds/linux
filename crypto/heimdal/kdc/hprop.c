/*
 * Copyright (c) 1997 - 2005 Kungliga Tekniska Högskolan
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

#define KRB5_DEPRECATED /* uses v4 functions that will die */

#include "hprop.h"

static int version_flag;
static int help_flag;
static const char *ktname = HPROP_KEYTAB;
static const char *database;
static char *mkeyfile;
static int to_stdout;
static int verbose_flag;
static int encrypt_flag;
static int decrypt_flag;
static hdb_master_key mkey5;

static char *source_type;

static char *local_realm=NULL;

static int
open_socket(krb5_context context, const char *hostname, const char *port)
{
    struct addrinfo *ai, *a;
    struct addrinfo hints;
    int error;

    memset (&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    error = getaddrinfo (hostname, port, &hints, &ai);
    if (error) {
	warnx ("%s: %s", hostname, gai_strerror(error));
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
	return s;
    }
    warnx ("failed to contact %s", hostname);
    freeaddrinfo (ai);
    return -1;
}

krb5_error_code
v5_prop(krb5_context context, HDB *db, hdb_entry_ex *entry, void *appdata)
{
    krb5_error_code ret;
    struct prop_data *pd = appdata;
    krb5_data data;

    if(encrypt_flag) {
	ret = hdb_seal_keys_mkey(context, &entry->entry, mkey5);
	if (ret) {
	    krb5_warn(context, ret, "hdb_seal_keys_mkey");
	    return ret;
	}
    }
    if(decrypt_flag) {
	ret = hdb_unseal_keys_mkey(context, &entry->entry, mkey5);
	if (ret) {
	    krb5_warn(context, ret, "hdb_unseal_keys_mkey");
	    return ret;
	}
    }

    ret = hdb_entry2value(context, &entry->entry, &data);
    if(ret) {
	krb5_warn(context, ret, "hdb_entry2value");
	return ret;
    }

    if(to_stdout)
	ret = krb5_write_message(context, &pd->sock, &data);
    else
	ret = krb5_write_priv_message(context, pd->auth_context,
				      &pd->sock, &data);
    krb5_data_free(&data);
    return ret;
}

struct getargs args[] = {
    { "master-key", 'm', arg_string, &mkeyfile, "v5 master key file", "file" },
    { "database", 'd',	arg_string, rk_UNCONST(&database), "database", "file" },
    { "source",   0,	arg_string, &source_type, "type of database to read",
      "heimdal"
      "|mit-dump"
    },

    { "keytab",   'k',	arg_string, rk_UNCONST(&ktname),
      "keytab to use for authentication", "keytab" },
    { "v5-realm", 'R',  arg_string, &local_realm, "v5 realm to use", NULL },
    { "decrypt",  'D',  arg_flag,   &decrypt_flag,   "decrypt keys", NULL },
    { "encrypt",  'E',  arg_flag,   &encrypt_flag,   "encrypt keys", NULL },
    { "stdout",	  'n',  arg_flag,   &to_stdout, "dump to stdout", NULL },
    { "verbose",  'v',	arg_flag, &verbose_flag, NULL, NULL },
    { "version",   0,	arg_flag, &version_flag, NULL, NULL },
    { "help",     'h',	arg_flag, &help_flag, NULL, NULL }
};

static int num_args = sizeof(args) / sizeof(args[0]);

static void
usage(int ret)
{
    arg_printusage (args, num_args, NULL, "[host[:port]] ...");
    exit (ret);
}

static void
get_creds(krb5_context context, krb5_ccache *cache)
{
    krb5_keytab keytab;
    krb5_principal client;
    krb5_error_code ret;
    krb5_get_init_creds_opt *init_opts;
    krb5_preauthtype preauth = KRB5_PADATA_ENC_TIMESTAMP;
    krb5_creds creds;

    ret = krb5_kt_register(context, &hdb_kt_ops);
    if(ret) krb5_err(context, 1, ret, "krb5_kt_register");

    ret = krb5_kt_resolve(context, ktname, &keytab);
    if(ret) krb5_err(context, 1, ret, "krb5_kt_resolve");

    ret = krb5_make_principal(context, &client, NULL,
			      "kadmin", HPROP_NAME, NULL);
    if(ret) krb5_err(context, 1, ret, "krb5_make_principal");

    ret = krb5_get_init_creds_opt_alloc(context, &init_opts);
    if(ret) krb5_err(context, 1, ret, "krb5_get_init_creds_opt_alloc");
    krb5_get_init_creds_opt_set_preauth_list(init_opts, &preauth, 1);

    ret = krb5_get_init_creds_keytab(context, &creds, client, keytab, 0, NULL, init_opts);
    if(ret) krb5_err(context, 1, ret, "krb5_get_init_creds");

    krb5_get_init_creds_opt_free(context, init_opts);

    ret = krb5_kt_close(context, keytab);
    if(ret) krb5_err(context, 1, ret, "krb5_kt_close");

    ret = krb5_cc_new_unique(context, krb5_cc_type_memory, NULL, cache);
    if(ret) krb5_err(context, 1, ret, "krb5_cc_new_unique");

    ret = krb5_cc_initialize(context, *cache, client);
    if(ret) krb5_err(context, 1, ret, "krb5_cc_initialize");

    krb5_free_principal(context, client);

    ret = krb5_cc_store_cred(context, *cache, &creds);
    if(ret) krb5_err(context, 1, ret, "krb5_cc_store_cred");

    krb5_free_cred_contents(context, &creds);
}

enum hprop_source {
    HPROP_HEIMDAL = 1,
    HPROP_MIT_DUMP
};

struct {
    int type;
    const char *name;
} types[] = {
    { HPROP_HEIMDAL,	"heimdal" },
    { HPROP_MIT_DUMP,	"mit-dump" }
};

static int
parse_source_type(const char *s)
{
    size_t i;
    for(i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
	if(strstr(types[i].name, s) == types[i].name)
	    return types[i].type;
    }
    return 0;
}

static int
iterate (krb5_context context,
	 const char *database_name,
	 HDB *db,
	 int type,
	 struct prop_data *pd)
{
    int ret;

    switch(type) {
    case HPROP_MIT_DUMP:
	ret = mit_prop_dump(pd, database_name);
	if (ret)
	    krb5_warn(context, ret, "mit_prop_dump");
	break;
    case HPROP_HEIMDAL:
	ret = hdb_foreach(context, db, HDB_F_DECRYPT, v5_prop, pd);
	if(ret)
	    krb5_warn(context, ret, "hdb_foreach");
	break;
    default:
	krb5_errx(context, 1, "unknown prop type: %d", type);
    }
    return ret;
}

static int
dump_database (krb5_context context, int type,
	       const char *database_name, HDB *db)
{
    krb5_error_code ret;
    struct prop_data pd;
    krb5_data data;

    pd.context      = context;
    pd.auth_context = NULL;
    pd.sock         = STDOUT_FILENO;

    ret = iterate (context, database_name, db, type, &pd);
    if (ret)
	krb5_errx(context, 1, "iterate failure");
    krb5_data_zero (&data);
    ret = krb5_write_message (context, &pd.sock, &data);
    if (ret)
	krb5_err(context, 1, ret, "krb5_write_message");

    return 0;
}

static int
propagate_database (krb5_context context, int type,
		    const char *database_name,
		    HDB *db, krb5_ccache ccache,
		    int optidx, int argc, char **argv)
{
    krb5_principal server;
    krb5_error_code ret;
    int i, failed = 0;

    for(i = optidx; i < argc; i++){
	krb5_auth_context auth_context;
	int fd;
	struct prop_data pd;
	krb5_data data;

	char *port, portstr[NI_MAXSERV];
	char *host = argv[i];

	port = strchr(host, ':');
	if(port == NULL) {
	    snprintf(portstr, sizeof(portstr), "%u",
		     ntohs(krb5_getportbyname (context, "hprop", "tcp",
					       HPROP_PORT)));
	    port = portstr;
	} else
	    *port++ = '\0';

	fd = open_socket(context, host, port);
	if(fd < 0) {
	    failed++;
	    krb5_warn (context, errno, "connect %s", host);
	    continue;
	}

	ret = krb5_sname_to_principal(context, argv[i],
				      HPROP_NAME, KRB5_NT_SRV_HST, &server);
	if(ret) {
	    failed++;
	    krb5_warn(context, ret, "krb5_sname_to_principal(%s)", host);
	    close(fd);
	    continue;
	}

        if (local_realm) {
            krb5_realm my_realm;
            krb5_get_default_realm(context,&my_realm);
            krb5_principal_set_realm(context,server,my_realm);
	    krb5_xfree(my_realm);
        }

	auth_context = NULL;
	ret = krb5_sendauth(context,
			    &auth_context,
			    &fd,
			    HPROP_VERSION,
			    NULL,
			    server,
			    AP_OPTS_MUTUAL_REQUIRED | AP_OPTS_USE_SUBKEY,
			    NULL, /* in_data */
			    NULL, /* in_creds */
			    ccache,
			    NULL,
			    NULL,
			    NULL);

	krb5_free_principal(context, server);

	if(ret) {
	    failed++;
	    krb5_warn(context, ret, "krb5_sendauth (%s)", host);
	    close(fd);
	    goto next_host;
	}

	pd.context      = context;
	pd.auth_context = auth_context;
	pd.sock         = fd;

	ret = iterate (context, database_name, db, type, &pd);
	if (ret) {
	    krb5_warnx(context, "iterate to host %s failed", host);
	    failed++;
	    goto next_host;
	}

	krb5_data_zero (&data);
	ret = krb5_write_priv_message(context, auth_context, &fd, &data);
	if(ret) {
	    krb5_warn(context, ret, "krb5_write_priv_message");
	    failed++;
	    goto next_host;
	}

	ret = krb5_read_priv_message(context, auth_context, &fd, &data);
	if(ret) {
	    krb5_warn(context, ret, "krb5_read_priv_message: %s", host);
	    failed++;
	    goto next_host;
	} else
	    krb5_data_free (&data);

    next_host:
	krb5_auth_con_free(context, auth_context);
	close(fd);
    }
    if (failed)
	return 1;
    return 0;
}

int
main(int argc, char **argv)
{
    krb5_error_code ret;
    krb5_context context;
    krb5_ccache ccache = NULL;
    HDB *db = NULL;
    int optidx = 0;

    int type, exit_code;

    setprogname(argv[0]);

    if(getarg(args, num_args, argc, argv, &optidx))
	usage(1);

    if(help_flag)
	usage(0);

    if(version_flag){
	print_version(NULL);
	exit(0);
    }

    ret = krb5_init_context(&context);
    if(ret)
	exit(1);

    /* We may be reading an old database encrypted with a DES master key. */
    ret = krb5_allow_weak_crypto(context, 1);
    if(ret)
        krb5_err(context, 1, ret, "krb5_allow_weak_crypto");

    if(local_realm)
	krb5_set_default_realm(context, local_realm);

    if(encrypt_flag && decrypt_flag)
	krb5_errx(context, 1,
		  "only one of `--encrypt' and `--decrypt' is meaningful");

    if(source_type != NULL) {
	type = parse_source_type(source_type);
	if(type == 0)
	    krb5_errx(context, 1, "unknown source type `%s'", source_type);
    } else
	type = HPROP_HEIMDAL;

    if(!to_stdout)
	get_creds(context, &ccache);

    if(decrypt_flag || encrypt_flag) {
	ret = hdb_read_master_key(context, mkeyfile, &mkey5);
	if(ret && ret != ENOENT)
	    krb5_err(context, 1, ret, "hdb_read_master_key");
	if(ret)
	    krb5_errx(context, 1, "No master key file found");
    }

    switch(type) {
    case HPROP_MIT_DUMP:
	if (database == NULL)
	    krb5_errx(context, 1, "no dump file specified");
	break;
    case HPROP_HEIMDAL:
	ret = hdb_create (context, &db, database);
	if(ret)
	    krb5_err(context, 1, ret, "hdb_create: %s", database);
	ret = db->hdb_open(context, db, O_RDONLY, 0);
	if(ret)
	    krb5_err(context, 1, ret, "db->hdb_open");
	break;
    default:
	krb5_errx(context, 1, "unknown dump type `%d'", type);
	break;
    }

    if (to_stdout)
	exit_code = dump_database (context, type, database, db);
    else
	exit_code = propagate_database (context, type, database,
					db, ccache, optidx, argc, argv);

    if(ccache != NULL)
	krb5_cc_destroy(context, ccache);

    if(db != NULL)
	(*db->hdb_destroy)(context, db);

    krb5_free_context(context);
    return exit_code;
}
