/*
 * Copyright (c) 1997 - 2004 Kungliga Tekniska Högskolan
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

#include "kadmin_locl.h"
#include "kadmin-commands.h"
#include <sl.h>

static char *config_file;
static char *keyfile;
int local_flag;
static int ad_flag;
static int help_flag;
static int version_flag;
static char *realm;
static char *admin_server;
static int server_port = 0;
static char *client_name;
static char *keytab;
static char *check_library  = NULL;
static char *check_function = NULL;
static getarg_strings policy_libraries = { 0, NULL };

static struct getargs args[] = {
    {	"principal", 	'p',	arg_string,	&client_name,
	"principal to authenticate as", NULL },
    {   "keytab",	'K',	arg_string,	&keytab,
   	"keytab for authentication principal", NULL },
    {
	"config-file",	'c',	arg_string,	&config_file,
	"location of config file",	"file"
    },
    {
	"key-file",	'k',	arg_string, &keyfile,
	"location of master key file", "file"
    },
    {
	"realm",	'r',	arg_string,   &realm,
	"realm to use", "realm"
    },
    {
	"admin-server",	'a',	arg_string,   &admin_server,
	"server to contact", "host"
    },
    {
	"server-port",	's',	arg_integer,   &server_port,
	"port to use", "port number"
    },
    {	"ad", 		0, arg_flag, &ad_flag, "active directory admin mode",
	NULL },
#ifdef HAVE_DLOPEN
    { "check-library", 0, arg_string, &check_library,
      "library to load password check function from", "library" },
    { "check-function", 0, arg_string, &check_function,
      "password check function to load", "function" },
    { "policy-libraries", 0, arg_strings, &policy_libraries,
      "password check function to load", "function" },
#endif
    {	"local", 'l', arg_flag, &local_flag, "local admin mode", NULL },
    {	"help",		'h',	arg_flag,   &help_flag, NULL, NULL },
    {	"version",	'v',	arg_flag,   &version_flag, NULL, NULL }
};

static int num_args = sizeof(args) / sizeof(args[0]);


krb5_context context;
void *kadm_handle;

int
help(void *opt, int argc, char **argv)
{
    sl_slc_help(commands, argc, argv);
    return 0;
}

static int exit_seen = 0;

int
exit_kadmin (void *opt, int argc, char **argv)
{
    exit_seen = 1;
    return 0;
}

static void
usage(int ret)
{
    arg_printusage (args, num_args, NULL, "[command]");
    exit (ret);
}

int
get_privs(void *opt, int argc, char **argv)
{
    uint32_t privs;
    char str[128];
    kadm5_ret_t ret;

    ret = kadm5_get_privs(kadm_handle, &privs);
    if(ret)
	krb5_warn(context, ret, "kadm5_get_privs");
    else{
	ret =_kadm5_privs_to_string(privs, str, sizeof(str));
	if (ret == 0)
	    printf("%s\n", str);
	else
	    printf("privs: 0x%x\n", (unsigned int)privs);
    }
    return 0;
}

int
main(int argc, char **argv)
{
    krb5_error_code ret;
    char **files;
    kadm5_config_params conf;
    int optidx = 0;
    int exit_status = 0;

    setprogname(argv[0]);

    ret = krb5_init_context(&context);
    if (ret)
	errx (1, "krb5_init_context failed: %d", ret);

    if(getarg(args, num_args, argc, argv, &optidx))
	usage(1);

    if (help_flag)
	usage (0);

    if (version_flag) {
	print_version(NULL);
	exit(0);
    }

    argc -= optidx;
    argv += optidx;

    if (config_file == NULL) {
	asprintf(&config_file, "%s/kdc.conf", hdb_db_dir(context));
	if (config_file == NULL)
	    errx(1, "out of memory");
    }

    ret = krb5_prepend_config_files_default(config_file, &files);
    if (ret)
	krb5_err(context, 1, ret, "getting configuration files");

    ret = krb5_set_config_files(context, files);
    krb5_free_config_files(files);
    if(ret)
	krb5_err(context, 1, ret, "reading configuration files");

    memset(&conf, 0, sizeof(conf));
    if(realm) {
	krb5_set_default_realm(context, realm); /* XXX should be fixed
						   some other way */
	conf.realm = realm;
	conf.mask |= KADM5_CONFIG_REALM;
    }

    if (admin_server) {
	conf.admin_server = admin_server;
	conf.mask |= KADM5_CONFIG_ADMIN_SERVER;
    }

    if (server_port) {
	conf.kadmind_port = htons(server_port);
	conf.mask |= KADM5_CONFIG_KADMIND_PORT;
    }

    if (keyfile) {
	conf.stash_file = keyfile;
	conf.mask |= KADM5_CONFIG_STASH_FILE;
    }

    if(local_flag) {
	int i;

	kadm5_setup_passwd_quality_check (context,
					  check_library, check_function);

	for (i = 0; i < policy_libraries.num_strings; i++) {
	    ret = kadm5_add_passwd_quality_verifier(context,
						    policy_libraries.strings[i]);
	    if (ret)
		krb5_err(context, 1, ret, "kadm5_add_passwd_quality_verifier");
	}
	ret = kadm5_add_passwd_quality_verifier(context, NULL);
	if (ret)
	    krb5_err(context, 1, ret, "kadm5_add_passwd_quality_verifier");

	ret = kadm5_s_init_with_password_ctx(context,
					     KADM5_ADMIN_SERVICE,
					     NULL,
					     KADM5_ADMIN_SERVICE,
					     &conf, 0, 0,
					     &kadm_handle);
    } else if (ad_flag) {
	if (client_name == NULL)
	    krb5_errx(context, 1, "keytab mode require principal name");
	ret = kadm5_ad_init_with_password_ctx(context,
					      client_name,
					      NULL,
					      KADM5_ADMIN_SERVICE,
					      &conf, 0, 0,
					      &kadm_handle);
    } else if (keytab) {
	if (client_name == NULL)
	    krb5_errx(context, 1, "keytab mode require principal name");
        ret = kadm5_c_init_with_skey_ctx(context,
					 client_name,
					 keytab,
					 KADM5_ADMIN_SERVICE,
                                         &conf, 0, 0,
                                         &kadm_handle);
    } else
	ret = kadm5_c_init_with_password_ctx(context,
					     client_name,
					     NULL,
					     KADM5_ADMIN_SERVICE,
					     &conf, 0, 0,
					     &kadm_handle);

    if(ret)
	krb5_err(context, 1, ret, "kadm5_init_with_password");

    signal(SIGINT, SIG_IGN); /* ignore signals for now, the sl command
                                parser will handle SIGINT its own way;
                                we should really take care of this in
                                each function, f.i `get' might be
                                interruptable, but not `create' */
    if (argc != 0) {
	ret = sl_command (commands, argc, argv);
	if(ret == -1)
	    krb5_warnx (context, "unrecognized command: %s", argv[0]);
	else if (ret == -2)
	    ret = 0;
	if(ret != 0)
	    exit_status = 1;
    } else {
	while(!exit_seen) {
	    ret = sl_command_loop(commands, "kadmin> ", NULL);
	    if (ret == -2)
		exit_seen = 1;
	    else if (ret != 0)
		exit_status = 1;
	}
    }

    kadm5_destroy(kadm_handle);
    krb5_free_context(context);
    return exit_status;
}
