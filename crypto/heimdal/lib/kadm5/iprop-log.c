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

#include "iprop.h"
#include <sl.h>
#include <parse_time.h>
#include "iprop-commands.h"

RCSID("$Id$");

static krb5_context context;

static kadm5_server_context *
get_kadmin_context(const char *config_file, char *realm)
{
    kadm5_config_params conf;
    krb5_error_code ret;
    void *kadm_handle;
    char **files;

    if (config_file == NULL) {
	char *file;
	asprintf(&file, "%s/kdc.conf", hdb_db_dir(context));
	if (file == NULL)
	    errx(1, "out of memory");
	config_file = file;
    }

    ret = krb5_prepend_config_files_default(config_file, &files);
    if (ret)
	krb5_err(context, 1, ret, "getting configuration files");

    ret = krb5_set_config_files(context, files);
    krb5_free_config_files(files);
    if (ret)
	krb5_err(context, 1, ret, "reading configuration files");

    memset(&conf, 0, sizeof(conf));
    if(realm) {
	conf.mask |= KADM5_CONFIG_REALM;
	conf.realm = realm;
    }

    ret = kadm5_init_with_password_ctx (context,
					KADM5_ADMIN_SERVICE,
					NULL,
					KADM5_ADMIN_SERVICE,
					&conf, 0, 0,
					&kadm_handle);
    if (ret)
	krb5_err (context, 1, ret, "kadm5_init_with_password_ctx");

    return (kadm5_server_context *)kadm_handle;
}

/*
 * dump log
 */

static const char *op_names[] = {
    "get",
    "delete",
    "create",
    "rename",
    "chpass",
    "modify",
    "randkey",
    "get_privs",
    "get_princs",
    "chpass_with_key",
    "nop"
};

static void
print_entry(kadm5_server_context *server_context,
	    uint32_t ver,
	    time_t timestamp,
	    enum kadm_ops op,
	    uint32_t len,
	    krb5_storage *sp,
	    void *ctx)
{
    char t[256];
    int32_t mask;
    hdb_entry ent;
    krb5_principal source;
    char *name1, *name2;
    krb5_data data;
    krb5_context scontext = server_context->context;

    off_t end = krb5_storage_seek(sp, 0, SEEK_CUR) + len;

    krb5_error_code ret;

    strftime(t, sizeof(t), "%Y-%m-%d %H:%M:%S", localtime(&timestamp));

    if((int)op < (int)kadm_get || (int)op > (int)kadm_nop) {
	printf("unknown op: %d\n", op);
	krb5_storage_seek(sp, end, SEEK_SET);
	return;
    }

    printf ("%s: ver = %u, timestamp = %s, len = %u\n",
	    op_names[op], ver, t, len);
    switch(op) {
    case kadm_delete:
	krb5_ret_principal(sp, &source);
	krb5_unparse_name(scontext, source, &name1);
	printf("    %s\n", name1);
	free(name1);
	krb5_free_principal(scontext, source);
	break;
    case kadm_rename:
	ret = krb5_data_alloc(&data, len);
	if (ret)
	    krb5_err (scontext, 1, ret, "kadm_rename: data alloc: %d", len);
	krb5_ret_principal(sp, &source);
	krb5_storage_read(sp, data.data, data.length);
	hdb_value2entry(scontext, &data, &ent);
	krb5_unparse_name(scontext, source, &name1);
	krb5_unparse_name(scontext, ent.principal, &name2);
	printf("    %s -> %s\n", name1, name2);
	free(name1);
	free(name2);
	krb5_free_principal(scontext, source);
	free_hdb_entry(&ent);
	break;
    case kadm_create:
	ret = krb5_data_alloc(&data, len);
	if (ret)
	    krb5_err (scontext, 1, ret, "kadm_create: data alloc: %d", len);
	krb5_storage_read(sp, data.data, data.length);
	ret = hdb_value2entry(scontext, &data, &ent);
	if(ret)
	    abort();
	mask = ~0;
	goto foo;
    case kadm_modify:
	ret = krb5_data_alloc(&data, len);
	if (ret)
	    krb5_err (scontext, 1, ret, "kadm_modify: data alloc: %d", len);
	krb5_ret_int32(sp, &mask);
	krb5_storage_read(sp, data.data, data.length);
	ret = hdb_value2entry(scontext, &data, &ent);
	if(ret)
	    abort();
    foo:
	if(ent.principal /* mask & KADM5_PRINCIPAL */) {
	    krb5_unparse_name(scontext, ent.principal, &name1);
	    printf("    principal = %s\n", name1);
	    free(name1);
	}
	if(mask & KADM5_PRINC_EXPIRE_TIME) {
	    if(ent.valid_end == NULL) {
		strlcpy(t, "never", sizeof(t));
	    } else {
		strftime(t, sizeof(t), "%Y-%m-%d %H:%M:%S",
			 localtime(ent.valid_end));
	    }
	    printf("    expires = %s\n", t);
	}
	if(mask & KADM5_PW_EXPIRATION) {
	    if(ent.pw_end == NULL) {
		strlcpy(t, "never", sizeof(t));
	    } else {
		strftime(t, sizeof(t), "%Y-%m-%d %H:%M:%S",
			 localtime(ent.pw_end));
	    }
	    printf("    password exp = %s\n", t);
	}
	if(mask & KADM5_LAST_PWD_CHANGE) {
	}
	if(mask & KADM5_ATTRIBUTES) {
	    unparse_flags(HDBFlags2int(ent.flags),
			  asn1_HDBFlags_units(), t, sizeof(t));
	    printf("    attributes = %s\n", t);
	}
	if(mask & KADM5_MAX_LIFE) {
	    if(ent.max_life == NULL)
		strlcpy(t, "for ever", sizeof(t));
	    else
		unparse_time(*ent.max_life, t, sizeof(t));
	    printf("    max life = %s\n", t);
	}
	if(mask & KADM5_MAX_RLIFE) {
	    if(ent.max_renew == NULL)
		strlcpy(t, "for ever", sizeof(t));
	    else
		unparse_time(*ent.max_renew, t, sizeof(t));
	    printf("    max rlife = %s\n", t);
	}
	if(mask & KADM5_MOD_TIME) {
	    printf("    mod time\n");
	}
	if(mask & KADM5_MOD_NAME) {
	    printf("    mod name\n");
	}
	if(mask & KADM5_KVNO) {
	    printf("    kvno = %d\n", ent.kvno);
	}
	if(mask & KADM5_MKVNO) {
	    printf("    mkvno\n");
	}
	if(mask & KADM5_AUX_ATTRIBUTES) {
	    printf("    aux attributes\n");
	}
	if(mask & KADM5_POLICY) {
	    printf("    policy\n");
	}
	if(mask & KADM5_POLICY_CLR) {
	    printf("    mod time\n");
	}
	if(mask & KADM5_LAST_SUCCESS) {
	    printf("    last success\n");
	}
	if(mask & KADM5_LAST_FAILED) {
	    printf("    last failed\n");
	}
	if(mask & KADM5_FAIL_AUTH_COUNT) {
	    printf("    fail auth count\n");
	}
	if(mask & KADM5_KEY_DATA) {
	    printf("    key data\n");
	}
	if(mask & KADM5_TL_DATA) {
	    printf("    tl data\n");
	}
	free_hdb_entry(&ent);
	break;
    case kadm_nop :
	break;
    default:
	abort();
    }
    krb5_storage_seek(sp, end, SEEK_SET);
}

int
iprop_dump(struct dump_options *opt, int argc, char **argv)
{
    kadm5_server_context *server_context;
    krb5_error_code ret;

    server_context = get_kadmin_context(opt->config_file_string,
					opt->realm_string);

    ret = kadm5_log_init (server_context);
    if (ret)
	krb5_err (context, 1, ret, "kadm5_log_init");

    ret = kadm5_log_foreach (server_context, print_entry, NULL);
    if(ret)
	krb5_warn(context, ret, "kadm5_log_foreach");

    ret = kadm5_log_end (server_context);
    if (ret)
	krb5_warn(context, ret, "kadm5_log_end");
    return 0;
}

int
iprop_truncate(struct truncate_options *opt, int argc, char **argv)
{
    kadm5_server_context *server_context;
    krb5_error_code ret;

    server_context = get_kadmin_context(opt->config_file_string,
					opt->realm_string);

    ret = kadm5_log_truncate (server_context);
    if (ret)
	krb5_err (context, 1, ret, "kadm5_log_truncate");

    return 0;
}

int
last_version(struct last_version_options *opt, int argc, char **argv)
{
    kadm5_server_context *server_context;
    krb5_error_code ret;
    uint32_t version;

    server_context = get_kadmin_context(opt->config_file_string,
					opt->realm_string);

    ret = kadm5_log_init (server_context);
    if (ret)
	krb5_err (context, 1, ret, "kadm5_log_init");

    ret = kadm5_log_get_version (server_context, &version);
    if (ret)
	krb5_err (context, 1, ret, "kadm5_log_get_version");

    ret = kadm5_log_end (server_context);
    if (ret)
	krb5_warn(context, ret, "kadm5_log_end");

    printf("version: %lu\n", (unsigned long)version);

    return 0;
}

/*
 * Replay log
 */

int start_version = -1;
int end_version = -1;

static void
apply_entry(kadm5_server_context *server_context,
	    uint32_t ver,
	    time_t timestamp,
	    enum kadm_ops op,
	    uint32_t len,
	    krb5_storage *sp,
	    void *ctx)
{
    struct replay_options *opt = ctx;
    krb5_error_code ret;

    if((opt->start_version_integer != -1 && ver < (uint32_t)opt->start_version_integer) ||
       (opt->end_version_integer != -1 && ver > (uint32_t)opt->end_version_integer)) {
	/* XXX skip this entry */
	krb5_storage_seek(sp, len, SEEK_CUR);
	return;
    }
    printf ("ver %u... ", ver);
    fflush (stdout);

    ret = kadm5_log_replay (server_context,
			    op, ver, len, sp);
    if (ret)
	krb5_warn (server_context->context, ret, "kadm5_log_replay");

    printf ("done\n");
}

int
iprop_replay(struct replay_options *opt, int argc, char **argv)
{
    kadm5_server_context *server_context;
    krb5_error_code ret;

    server_context = get_kadmin_context(opt->config_file_string,
					opt->realm_string);

    ret = server_context->db->hdb_open(context,
				       server_context->db,
				       O_RDWR | O_CREAT, 0600);
    if (ret)
	krb5_err (context, 1, ret, "db->open");

    ret = kadm5_log_init (server_context);
    if (ret)
	krb5_err (context, 1, ret, "kadm5_log_init");

    ret = kadm5_log_foreach (server_context, apply_entry, opt);
    if(ret)
	krb5_warn(context, ret, "kadm5_log_foreach");
    ret = kadm5_log_end (server_context);
    if (ret)
	krb5_warn(context, ret, "kadm5_log_end");
    ret = server_context->db->hdb_close (context, server_context->db);
    if (ret)
	krb5_err (context, 1, ret, "db->close");

    return 0;
}

static int help_flag;
static int version_flag;

static struct getargs args[] = {
    { "version", 	0,	arg_flag, 	&version_flag,
      NULL,		NULL
    },
    { "help", 	'h', 	arg_flag, 	&help_flag,
      NULL, NULL
    }
};

static int num_args = sizeof(args) / sizeof(args[0]);

int
help(void *opt, int argc, char **argv)
{
    if(argc == 0) {
	sl_help(commands, 1, argv - 1 /* XXX */);
    } else {
	SL_cmd *c = sl_match (commands, argv[0], 0);
 	if(c == NULL) {
	    fprintf (stderr, "No such command: %s. "
		     "Try \"help\" for a list of commands\n",
		     argv[0]);
	} else {
	    if(c->func) {
		static char shelp[] = "--help";
		char *fake[3];
		fake[0] = argv[0];
		fake[1] = shelp;
		fake[2] = NULL;
		(*c->func)(2, fake);
		fprintf(stderr, "\n");
	    }
	    if(c->help && *c->help)
		fprintf (stderr, "%s\n", c->help);
	    if((++c)->name && c->func == NULL) {
		int f = 0;
		fprintf (stderr, "Synonyms:");
		while (c->name && c->func == NULL) {
		    fprintf (stderr, "%s%s", f ? ", " : " ", (c++)->name);
		    f = 1;
		}
		fprintf (stderr, "\n");
	    }
	}
    }
    return 0;
}

static void
usage(int status)
{
    arg_printusage(args, num_args, NULL, "command");
    exit(status);
}

int
main(int argc, char **argv)
{
    int optidx = 0;
    krb5_error_code ret;

    setprogname(argv[0]);

    if(getarg(args, num_args, argc, argv, &optidx))
	usage(1);
    if(help_flag)
	usage(0);
    if(version_flag) {
	print_version(NULL);
	exit(0);
    }
    argc -= optidx;
    argv += optidx;
    if(argc == 0)
	usage(1);

    ret = krb5_init_context(&context);
    if (ret)
	errx(1, "krb5_init_context failed with: %d\n", ret);

    ret = sl_command(commands, argc, argv);
    if(ret == -1)
	warnx ("unrecognized command: %s", argv[0]);
    return ret;
}
