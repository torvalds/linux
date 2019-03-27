/*
 * Copyright (c) 1995 - 2005 Kungliga Tekniska Högskolan
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

RCSID("$Id$");

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#include <time.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#ifdef KRB5
#include <krb5.h>
#endif
#include <kafs.h>

#include <err.h>
#include <roken.h>
#include <getarg.h>

#ifndef TKT_ROOT
#define TKT_ROOT "/tmp/tkt"
#endif

static int help_flag;
static int version_flag;
static int c_flag;
#ifdef KRB5
static char *typename_arg;
#endif

struct getargs getargs[] = {
    { NULL,	'c', arg_flag, &c_flag },
#ifdef KRB5
    { "cache-type", 0,  arg_string, &typename_arg },
#endif
    { "version", 0,  arg_flag, &version_flag },
    { "help",	'h', arg_flag, &help_flag },
};

static int num_args = sizeof(getargs) / sizeof(getargs[0]);

static void
usage(int ecode)
{
    arg_printusage(getargs, num_args, NULL, "command [args...]");
    exit(ecode);
}

/*
 * Run command with a new ticket file / credentials cache / token
 */

int
main(int argc, char **argv)
{
    int f;
    char tf[1024];
    char *p;

    char *path;
    char **args;
    unsigned int i;
    int optind = 0;

    setprogname(argv[0]);
    if(getarg(getargs, num_args, argc, argv, &optind))
	usage(1);
    if(help_flag)
	usage(0);
    if(version_flag) {
	print_version(NULL);
	exit(0);
    }

    argc -= optind;
    argv += optind;

#ifdef KRB5
    {
	krb5_error_code ret;
	krb5_context context;
	krb5_ccache id;
	const char *name;

	ret = krb5_init_context(&context);
	if (ret) /* XXX should this really call exit ? */
	    errx(1, "no kerberos 5 support");

	ret = krb5_cc_new_unique(context, typename_arg, NULL, &id);
	if (ret)
	    krb5_err(context, 1, ret, "Failed generating credential cache");

	name = krb5_cc_get_name(context, id);
	if (name == NULL)
	    krb5_errx(context, 1, "Generated credential cache have no name");

	snprintf(tf, sizeof(tf), "%s:%s", krb5_cc_get_type(context, id), name);

	ret = krb5_cc_close(context, id);
	if (ret)
	    krb5_err(context, 1, ret, "Failed closing credential cache");

	krb5_free_context(context);

	esetenv("KRB5CCNAME", tf, 1);
    }
#endif

    snprintf (tf, sizeof(tf), "%s_XXXXXX", TKT_ROOT);
    f = mkstemp (tf);
    if (f < 0)
	err(1, "mkstemp failed");
    close (f);
    unlink (tf);
    esetenv("KRBTKFILE", tf, 1);

    i = 0;

    args = (char **) malloc((argc + 10)*sizeof(char *));
    if (args == NULL)
	errx (1, "Out of memory allocating %lu bytes",
	      (unsigned long)((argc + 10)*sizeof(char *)));

    if(*argv == NULL) {
	path = getenv("SHELL");
	if(path == NULL){
	    struct passwd *pw = k_getpwuid(geteuid());
	    if (pw == NULL)
		errx(1, "no such user: %d", (int)geteuid());
	    path = strdup(pw->pw_shell);
	}
    } else {
	path = strdup(*argv++);
    }
    if (path == NULL)
	errx (1, "Out of memory copying path");

    p=strrchr(path, '/');
    if(p)
	args[i] = strdup(p+1);
    else
	args[i] = strdup(path);

    if (args[i++] == NULL)
	errx (1, "Out of memory copying arguments");

    while(*argv)
	args[i++] = *argv++;

    args[i++] = NULL;

    if(k_hasafs())
	k_setpag();

    unsetenv("PAGPID");
    execvp(path, args);
    if (errno == ENOENT || c_flag) {
	char **sh_args = malloc ((i + 2) * sizeof(char *));
	unsigned int j;

	if (sh_args == NULL)
	    errx (1, "Out of memory copying sh arguments");
	for (j = 1; j < i; ++j)
	    sh_args[j + 2] = args[j];
	sh_args[0] = "sh";
	sh_args[1] = "-c";
	sh_args[2] = path;
	execv ("/bin/sh", sh_args);
    }
    err (1, "execvp");
}
