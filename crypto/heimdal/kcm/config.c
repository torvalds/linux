/*
 * Copyright (c) 2005, PADL Software Pty Ltd.
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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
 * 3. Neither the name of PADL Software nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PADL SOFTWARE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL PADL SOFTWARE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "kcm_locl.h"
#include <getarg.h>
#include <parse_bytes.h>

static const char *config_file;	/* location of kcm config file */

size_t max_request = 0;		/* maximal size of a request */
char *socket_path = NULL;
char *door_path = NULL;

static char *max_request_str;	/* `max_request' as a string */

#ifdef SUPPORT_DETACH
int detach_from_console = -1;
#define DETACH_IS_DEFAULT FALSE
#endif

static const char *system_cache_name = NULL;
static const char *system_keytab = NULL;
static const char *system_principal = NULL;
static const char *system_server = NULL;
static const char *system_perms = NULL;
static const char *system_user = NULL;
static const char *system_group = NULL;

static const char *renew_life = NULL;
static const char *ticket_life = NULL;

int launchd_flag = 0;
int disallow_getting_krbtgt = 0;
int name_constraints = -1;

static int help_flag;
static int version_flag;

static struct getargs args[] = {
    {
	"cache-name",	0,	arg_string,	&system_cache_name,
	"system cache name", "cachename"
    },
    {
	"config-file",	'c',	arg_string,	&config_file,
	"location of config file",	"file"
    },
    {
	"group",	'g',	arg_string,	&system_group,
	"system cache group",	"group"
    },
    {
	"max-request",	0,	arg_string, &max_request,
	"max size for a kcm-request", "size"
    },
    {
	"launchd",	0,	arg_flag, &launchd_flag,
	"when in use by launchd"
    },
#ifdef SUPPORT_DETACH
#if DETACH_IS_DEFAULT
    {
	"detach",       'D',      arg_negative_flag, &detach_from_console,
	"don't detach from console"
    },
#else
    {
	"detach",       0 ,      arg_flag, &detach_from_console,
	"detach from console"
    },
#endif
#endif
    {	"help",		'h',	arg_flag,   &help_flag },
    {
	"system-principal",	'k',	arg_string,	&system_principal,
	"system principal name",	"principal"
    },
    {
	"lifetime",	'l', arg_string, &ticket_life,
	"lifetime of system tickets", "time"
    },
    {
	"mode",		'm', arg_string, &system_perms,
	"octal mode of system cache", "mode"
    },
    {
	"name-constraints",	'n', arg_negative_flag, &name_constraints,
	"disable credentials cache name constraints"
    },
    {
	"disallow-getting-krbtgt", 0, arg_flag, &disallow_getting_krbtgt,
	"disable fetching krbtgt from the cache"
    },
    {
	"renewable-life",	'r', arg_string, &renew_life,
    	"renewable lifetime of system tickets", "time"
    },
    {
	"socket-path",		's', arg_string, &socket_path,
    	"path to kcm domain socket", "path"
    },
#ifdef HAVE_DOOR_CREATE
    {
	"door-path",		's', arg_string, &door_path,
    	"path to kcm door", "path"
    },
#endif
    {
	"server",		'S', arg_string, &system_server,
    	"server to get system ticket for", "principal"
    },
    {
	"keytab",	't',	arg_string,	&system_keytab,
	"system keytab name",	"keytab"
    },
    {
	"user",		'u',	arg_string,	&system_user,
	"system cache owner",	"user"
    },
    {	"version",	'v',	arg_flag,   &version_flag }
};

static int num_args = sizeof(args) / sizeof(args[0]);

static void
usage(int ret)
{
    arg_printusage (args, num_args, NULL, "");
    exit (ret);
}

static int parse_owners(kcm_ccache ccache)
{
    uid_t uid = 0;
    gid_t gid = 0;
    struct passwd *pw;
    struct group *gr;
    int uid_p = 0;
    int gid_p = 0;

    if (system_user != NULL) {
	if (isdigit((unsigned char)system_user[0])) {
	    pw = getpwuid(atoi(system_user));
	} else {
	    pw = getpwnam(system_user);
	}
	if (pw == NULL) {
	    return errno;
	}

	system_user = strdup(pw->pw_name);
	if (system_user == NULL) {
	    return ENOMEM;
	}

	uid = pw->pw_uid; uid_p = 1;
	gid = pw->pw_gid; gid_p = 1;
    }

    if (system_group != NULL) {
	if (isdigit((unsigned char)system_group[0])) {
	    gr = getgrgid(atoi(system_group));
	} else {
	    gr = getgrnam(system_group);
	}
	if (gr == NULL) {
	    return errno;
	}

	gid = gr->gr_gid; gid_p = 1;
    }

    if (uid_p)
	ccache->uid = uid;
    else
	ccache->uid = 0; /* geteuid() XXX */

    if (gid_p)
	ccache->gid = gid;
    else
	ccache->gid = 0; /* getegid() XXX */

    return 0;
}

static const char *
kcm_system_config_get_string(const char *string)
{
    return krb5_config_get_string(kcm_context, NULL, "kcm",
				  "system_ccache", string, NULL);
}

static krb5_error_code
ccache_init_system(void)
{
    kcm_ccache ccache;
    krb5_error_code ret;

    if (system_cache_name == NULL)
	system_cache_name = kcm_system_config_get_string("cc_name");

    ret = kcm_ccache_new(kcm_context,
			 system_cache_name ? system_cache_name : "SYSTEM",
			 &ccache);
    if (ret)
	return ret;

    ccache->flags |= KCM_FLAGS_OWNER_IS_SYSTEM;
    ccache->flags |= KCM_FLAGS_USE_KEYTAB;

    ret = parse_owners(ccache);
    if (ret)
	return ret;

    ret = krb5_parse_name(kcm_context, system_principal, &ccache->client);
    if (ret) {
	kcm_release_ccache(kcm_context, ccache);
	return ret;
    }

    if (system_server == NULL)
	system_server = kcm_system_config_get_string("server");

    if (system_server != NULL) {
	ret = krb5_parse_name(kcm_context, system_server, &ccache->server);
	if (ret) {
	    kcm_release_ccache(kcm_context, ccache);
	    return ret;
	}
    }

    if (system_keytab == NULL)
	system_keytab = kcm_system_config_get_string("keytab_name");

    if (system_keytab != NULL) {
	ret = krb5_kt_resolve(kcm_context, system_keytab, &ccache->key.keytab);
    } else {
	ret = krb5_kt_default(kcm_context, &ccache->key.keytab);
    }
    if (ret) {
	kcm_release_ccache(kcm_context, ccache);
	return ret;
    }

    if (renew_life == NULL)
	renew_life = kcm_system_config_get_string("renew_life");

    if (renew_life == NULL)
	renew_life = "1 month";

    if (renew_life != NULL) {
	ccache->renew_life = parse_time(renew_life, "s");
	if (ccache->renew_life < 0) {
	    kcm_release_ccache(kcm_context, ccache);
	    return EINVAL;
	}
    }

    if (ticket_life == NULL)
	ticket_life = kcm_system_config_get_string("ticket_life");

    if (ticket_life != NULL) {
	ccache->tkt_life = parse_time(ticket_life, "s");
	if (ccache->tkt_life < 0) {
	    kcm_release_ccache(kcm_context, ccache);
	    return EINVAL;
	}
    }

    if (system_perms == NULL)
	system_perms = kcm_system_config_get_string("mode");

    if (system_perms != NULL) {
	int mode;

	if (sscanf(system_perms, "%o", &mode) != 1)
	    return EINVAL;

	ccache->mode = mode;
    }

    if (disallow_getting_krbtgt == -1) {
	disallow_getting_krbtgt =
	    krb5_config_get_bool_default(kcm_context, NULL, FALSE, "kcm",
					 "disallow-getting-krbtgt", NULL);
    }

    /* enqueue default actions for credentials cache */
    ret = kcm_ccache_enqueue_default(kcm_context, ccache, NULL);

    kcm_release_ccache(kcm_context, ccache); /* retained by event queue */

    return ret;
}

void
kcm_configure(int argc, char **argv)
{
    krb5_error_code ret;
    int optind = 0;
    const char *p;

    while(getarg(args, num_args, argc, argv, &optind))
	warnx("error at argument `%s'", argv[optind]);

    if(help_flag)
	usage (0);

    if (version_flag) {
	print_version(NULL);
	exit(0);
    }

    argc -= optind;
    argv += optind;

    if (argc != 0)
	usage(1);

    {
	char **files;

	if(config_file == NULL)
	    config_file = _PATH_KCM_CONF;

	ret = krb5_prepend_config_files_default(config_file, &files);
	if (ret)
	    krb5_err(kcm_context, 1, ret, "getting configuration files");

	ret = krb5_set_config_files(kcm_context, files);
	krb5_free_config_files(files);
	if(ret)
	    krb5_err(kcm_context, 1, ret, "reading configuration files");
    }

    if(max_request_str)
	max_request = parse_bytes(max_request_str, NULL);

    if(max_request == 0){
	p = krb5_config_get_string (kcm_context,
				    NULL,
				    "kcm",
				    "max-request",
				    NULL);
	if(p)
	    max_request = parse_bytes(p, NULL);
    }

    if (system_principal == NULL) {
	system_principal = kcm_system_config_get_string("principal");
    }

    if (system_principal != NULL) {
	ret = ccache_init_system();
	if (ret)
	    krb5_err(kcm_context, 1, ret, "initializing system ccache");
    }

#ifdef SUPPORT_DETACH
    if(detach_from_console == -1)
	detach_from_console = krb5_config_get_bool_default(kcm_context, NULL,
							   DETACH_IS_DEFAULT,
							   "kcm",
							   "detach", NULL);
#endif
    kcm_openlog();
    if(max_request == 0)
	max_request = 64 * 1024;
}

