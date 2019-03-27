/*
 * Copyright (c) 2010 Kungliga Tekniska Högskolan
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

#include "kuser_locl.h"
#include <sl.h>
#include "kcc-commands.h"

krb5_context kcc_context;
static int version_flag;
static int help_flag;

static struct getargs args[] = {
    { "version", 	0,   arg_flag, &version_flag, NULL, NULL },
    { "help",		0,   arg_flag, &help_flag, NULL, NULL }
};

static void
usage(int ret)
{
    arg_printusage_i18n(args,
			sizeof(args)/sizeof(*args),
			N_("Usage: ", ""),
			NULL,
			"command ..",
			getarg_i18n);
    exit (ret);
}

int
help(void *opt, int argc, char **argv)
{
    sl_slc_help(commands, argc, argv);
    return 0;
}

int
kgetcred(struct kgetcred_options *opt, int argc, char **argv)
{
    return 0;
}

/*
 * Wrapper for command line compatiblity
 */

int
kvno(struct kvno_options *opt, int argc, char **argv)
{
    struct kgetcred_options k;
    memset(&k, 0, sizeof(k));

    k.cache_string = opt->cache_string;
    k.enctype_string = opt->enctype_string;

    return kgetcred(&k, argc, argv);
}

static int
command_alias(const char *name)
{
    const char *aliases[] = {
	"kinit", "klist", "kswitch", "kgetcred", "kvno", "kdeltkt",
	"kdestroy", "kcpytkt", NULL
    }, **p = aliases;

    while (*p && strcmp(name, *p) != 0)
	p++;
    return *p != NULL;
}


int
main(int argc, char **argv)
{
    krb5_error_code ret;
    int optidx = 0;
    int exit_status = 0;

    setprogname (argv[0]);

    setlocale (LC_ALL, "");
    bindtextdomain ("heimdal_kuser", HEIMDAL_LOCALEDIR);
    textdomain("heimdal_kuser");

    ret = krb5_init_context(&kcc_context);
    if (ret == KRB5_CONFIG_BADFORMAT)
	errx (1, "krb5_init_context failed to parse configuration file");
    else if (ret)
	errx(1, "krb5_init_context failed: %d", ret);

    /*
     * Support linking of kcc to commands
     */

    if (!command_alias(getprogname())) {

	if (argc == 1) {
	    sl_slc_help(commands, 0, NULL);
	    return 1;
	}

	if(getarg(args, sizeof(args) / sizeof(args[0]), argc, argv, &optidx))
	    usage(1);

	if (help_flag)
	    usage (0);

	if(version_flag) {
	    print_version(NULL);
	    exit(0);
	}

    } else {
	argv[0] = rk_UNCONST(getprogname());
    }

    argc -= optidx;
    argv += optidx;

    if (argc != 0) {
	ret = sl_command(commands, argc, argv);
	if(ret == -1)
	    krb5_warnx(kcc_context, "unrecognized command: %s", argv[0]);
	else if (ret == -2)
	    ret = 0;
	if(ret != 0)
	    exit_status = 1;
    } else {
	sl_slc_help(commands, argc, argv);
	exit_status = 1;
    }

    krb5_free_context(kcc_context);
    return exit_status;
}
