/*
 * Copyright (c) 1999 - 2008 Kungliga Tekniska Högskolan
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
 * 3. Neither the name of KTH nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY KTH AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KTH OR ITS CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include <config.h>

RCSID("$Id$");

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <syslog.h>

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

#ifdef HAVE_SHADOW_H
#include <shadow.h>
#endif

#include <pwd.h>
#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif

#include "crypto-headers.h"
#ifdef KRB5
#include <krb5.h>
#endif
#include <kafs.h>
#include <err.h>
#include <roken.h>
#include <getarg.h>

#include "supaths.h"

#if !HAVE_DECL_ENVIRON
extern char **environ;
#endif

int kerberos_flag = 1;
int csh_f_flag;
int full_login;
int env_flag;
char *kerberos_instance = "root";
int help_flag;
int version_flag;
char *cmd;
char tkfile[256];

struct getargs args[] = {
    { "kerberos", 'K', arg_negative_flag, &kerberos_flag,
      "don't use kerberos" },
    { NULL,	  'f', arg_flag,	  &csh_f_flag,
      "don't read .cshrc" },
    { "full",	  'l', arg_flag,          &full_login,
      "simulate full login" },
    { NULL,	  'm', arg_flag,          &env_flag,
      "leave environment unmodified" },
    { "instance", 'i', arg_string,        &kerberos_instance,
      "root instance to use" },
    { "command",  'c', arg_string,        &cmd,
      "command to execute" },
    { "help", 	  'h', arg_flag,          &help_flag },
    { "version",  0,   arg_flag,          &version_flag },
};


static void
usage (int ret)
{
    arg_printusage (args,
		    sizeof(args)/sizeof(*args),
		    NULL,
		    "[login [shell arguments]]");
    exit (ret);
}

static void
free_info(struct passwd *p)
{
    free (p->pw_name);
    free (p->pw_passwd);
    free (p->pw_dir);
    free (p->pw_shell);
    free (p);
}

static struct passwd*
dup_info(const struct passwd *pwd)
{
    struct passwd *info;

    info = malloc(sizeof(*info));
    if(info == NULL)
	return NULL;
    info->pw_name = strdup(pwd->pw_name);
    info->pw_passwd = strdup(pwd->pw_passwd);
    info->pw_uid = pwd->pw_uid;
    info->pw_gid = pwd->pw_gid;
    info->pw_dir = strdup(pwd->pw_dir);
    info->pw_shell = strdup(pwd->pw_shell);
    if(info->pw_name == NULL || info->pw_passwd == NULL ||
       info->pw_dir == NULL || info->pw_shell == NULL) {
	free_info (info);
	return NULL;
    }
    return info;
}

#ifdef KRB5
static krb5_context context;
static krb5_ccache ccache;

static int
krb5_verify(const struct passwd *login_info,
	    const struct passwd *su_info,
	    const char *kerberos_instance)
{
    krb5_error_code ret;
    krb5_principal p;
    krb5_realm *realms, *r;
    char *login_name = NULL;
    int user_ok = 0;

#if defined(HAVE_GETLOGIN) && !defined(POSIX_GETLOGIN)
    login_name = getlogin();
#endif
    ret = krb5_init_context (&context);
    if (ret) {
#if 0
	warnx("krb5_init_context failed: %d", ret);
#endif
	return 1;
    }

    ret = krb5_get_default_realms(context, &realms);
    if (ret)
	return 1;

    /* Check all local realms */
    for (r = realms; *r != NULL && !user_ok; r++) {

	if (login_name == NULL || strcmp (login_name, "root") == 0)
	    login_name = login_info->pw_name;
	if (strcmp (su_info->pw_name, "root") == 0)
	    ret = krb5_make_principal(context, &p, *r,
				      login_name,
				      kerberos_instance,
				      NULL);
	else
	    ret = krb5_make_principal(context, &p, *r,
				      su_info->pw_name,
				      NULL);
	if (ret) {
	    krb5_free_host_realm(context, realms);
	    return 1;
	}

	/* if we are su-ing too root, check with krb5_kuserok */
	if (su_info->pw_uid == 0 && !krb5_kuserok(context, p, su_info->pw_name))
	    continue;

	ret = krb5_cc_new_unique(context, krb5_cc_type_memory, NULL, &ccache);
	if(ret) {
	    krb5_free_host_realm(context, realms);
	    krb5_free_principal (context, p);
	    return 1;
	}
  	ret = krb5_verify_user(context, p, ccache, NULL, TRUE, NULL);
	krb5_free_principal (context, p);
	switch (ret) {
	case 0:
	    user_ok = 1;
	    break;
	case KRB5_LIBOS_PWDINTR :
	    krb5_cc_destroy(context, ccache);
	    break;
	case KRB5KRB_AP_ERR_BAD_INTEGRITY:
	case KRB5KRB_AP_ERR_MODIFIED:
	    krb5_cc_destroy(context, ccache);
	    krb5_warnx(context, "Password incorrect");
	    break;
	default :
	    krb5_cc_destroy(context, ccache);
	    krb5_warn(context, ret, "krb5_verify_user");
	    break;
	}
    }
    krb5_free_host_realm(context, realms);
    if (!user_ok)
	return 1;
    return 0;
}

static int
krb5_start_session(void)
{
    krb5_ccache ccache2;
    char *cc_name;
    int ret;

    ret = krb5_cc_new_unique(context, krb5_cc_type_file, NULL, &ccache2);
    if (ret) {
	krb5_cc_destroy(context, ccache);
	return 1;
    }

    ret = krb5_cc_copy_cache(context, ccache, ccache2);
    if (ret) {
	krb5_cc_destroy(context, ccache);
	krb5_cc_destroy(context, ccache2);
	return 1;
    }

    ret = asprintf(&cc_name, "%s:%s", krb5_cc_get_type(context, ccache2),
		   krb5_cc_get_name(context, ccache2));
    if (ret == -1) {
	krb5_cc_destroy(context, ccache);
	krb5_cc_destroy(context, ccache2);
	errx(1, "malloc - out of memory");
    }
    esetenv("KRB5CCNAME", cc_name, 1);

    /* convert creds? */
    if(k_hasafs()) {
	if (k_setpag() == 0)
	    krb5_afslog(context, ccache2, NULL, NULL);
    }

    krb5_cc_close(context, ccache2);
    krb5_cc_destroy(context, ccache);
    return 0;
}
#endif


#define GROUP_MEMBER		0
#define GROUP_MISSING		1
#define GROUP_EMPTY		2
#define GROUP_NOT_MEMBER	3

static int
group_member_p(const char *group, const char *user)
{
    struct group *g;
    int i;
    g = getgrnam(group);
    if(g == NULL)
	return GROUP_MISSING;
    if(g->gr_mem[0] == NULL)
	return GROUP_EMPTY;
    for(i = 0; g->gr_mem[i] != NULL; i++)
	if(strcmp(user, g->gr_mem[i]) == 0)
	    return GROUP_MEMBER;
    return GROUP_NOT_MEMBER;
}

static int
verify_unix(struct passwd *login, struct passwd *su)
{
    char prompt[128];
    char pw_buf[1024];
    char *pw;
    int r;
    if(su->pw_passwd != NULL && *su->pw_passwd != '\0') {
	snprintf(prompt, sizeof(prompt), "%s's password: ", su->pw_name);
	r = UI_UTIL_read_pw_string(pw_buf, sizeof(pw_buf), prompt, 0);
	if(r != 0)
	    exit(0);
	pw = crypt(pw_buf, su->pw_passwd);
	memset(pw_buf, 0, sizeof(pw_buf));
	if(strcmp(pw, su->pw_passwd) != 0) {
	    syslog (LOG_ERR | LOG_AUTH, "%s to %s: incorrect password",
		    login->pw_name, su->pw_name);
	    return 1;
	}
    }
    /* if su:ing to root, check membership of group wheel or root; if
       that group doesn't exist, or is empty, allow anyone to su
       root */
    if(su->pw_uid == 0) {
#ifndef ROOT_GROUP
#define ROOT_GROUP "wheel"
#endif
	int gs = group_member_p(ROOT_GROUP, login->pw_name);
	if(gs == GROUP_NOT_MEMBER) {
	    syslog (LOG_ERR | LOG_AUTH, "%s to %s: not in group %s",
		    login->pw_name, su->pw_name, ROOT_GROUP);
	    return 1;
	}
	return 0;
    }
    return 0;
}

int
main(int argc, char **argv)
{
    int i, optind = 0;
    char *su_user;
    struct passwd *su_info;
    struct passwd *login_info;

    struct passwd *pwd;

    char *shell;

    int ok = 0;

    setprogname (argv[0]);

    if(getarg(args, sizeof(args) / sizeof(args[0]), argc, argv, &optind))
	usage(1);

    for (i=0; i < optind; i++)
      if (strcmp(argv[i], "-") == 0) {
	 full_login = 1;
	 break;
      }

    if(help_flag)
	usage(0);
    if(version_flag) {
	print_version(NULL);
	exit(0);
    }
    if(optind >= argc)
	su_user = "root";
    else
	su_user = argv[optind++];

    if (!issuid() && getuid() != 0)
	warnx("Not setuid and you are not root, expect this to fail");

    pwd = k_getpwnam(su_user);
    if(pwd == NULL)
	errx (1, "unknown login %s", su_user);
    if (pwd->pw_uid == 0 && strcmp ("root", su_user) != 0) {
	syslog (LOG_ALERT, "NIS attack, user %s has uid 0", su_user);
	errx (1, "unknown login %s", su_user);
    }
    su_info = dup_info(pwd);
    if (su_info == NULL)
	errx (1, "malloc: out of memory");

	pwd = getpwuid(getuid());
    if(pwd == NULL)
	errx(1, "who are you?");
    login_info = dup_info(pwd);
    if (login_info == NULL)
	errx (1, "malloc: out of memory");
    if(env_flag)
	shell = login_info->pw_shell;
    else
	shell = su_info->pw_shell;
    if(shell == NULL || *shell == '\0')
	shell = _PATH_BSHELL;


#ifdef KRB5
    if(kerberos_flag && ok == 0 &&
       krb5_verify(login_info, su_info, kerberos_instance) == 0)
	ok = 5;
#endif

    if(ok == 0 && login_info->pw_uid && verify_unix(login_info, su_info) != 0) {
	printf("Sorry!\n");
	exit(1);
    }

#ifdef HAVE_GETSPNAM
   {  struct spwd *sp;
      long    today;

    sp = getspnam(su_info->pw_name);
    if (sp != NULL) {
	today = time(0)/(24L * 60 * 60);
	if (sp->sp_expire > 0) {
	    if (today >= sp->sp_expire) {
		if (login_info->pw_uid)
		    errx(1,"Your account has expired.");
		else
		    printf("Your account has expired.");
            }
            else if (sp->sp_expire - today < 14)
                printf("Your account will expire in %d days.\n",
		       (int)(sp->sp_expire - today));
	}
	if (sp->sp_max > 0) {
	    if (today >= sp->sp_lstchg + sp->sp_max) {
		if (login_info->pw_uid)
		    errx(1,"Your password has expired. Choose a new one.");
		else
		    printf("Your password has expired. Choose a new one.");
	    }
	    else if (today >= sp->sp_lstchg + sp->sp_max - sp->sp_warn)
		printf("Your account will expire in %d days.\n",
		       (int)(sp->sp_lstchg + sp->sp_max -today));
	}
    }
    }
#endif
    {
	char *tty = ttyname (STDERR_FILENO);
	syslog (LOG_NOTICE | LOG_AUTH, tty ? "%s to %s on %s" : "%s to %s",
		login_info->pw_name, su_info->pw_name, tty);
    }


    if(!env_flag) {
	if(full_login) {
	    char *t = getenv ("TERM");
	    char **newenv = NULL;
	    int i, j;

	    i = read_environment(_PATH_ETC_ENVIRONMENT, &newenv);

	    environ = malloc ((10 + i) * sizeof (char *));
	    if (environ == NULL)
		err (1, "malloc");
	    environ[0] = NULL;

	    for (j = 0; j < i; j++) {
		char *p = strchr(newenv[j], '=');
		if (p == NULL)
		    errx(1, "enviroment '%s' missing '='", newenv[j]);
		*p++ = 0;
		esetenv (newenv[j], p, 1);
	    }
	    free(newenv);

	    esetenv ("PATH", _PATH_DEFPATH, 1);
	    if (t)
		esetenv ("TERM", t, 1);
	    if (chdir (su_info->pw_dir) < 0)
		errx (1, "no directory");
	}
	if (full_login || su_info->pw_uid)
	    esetenv ("USER", su_info->pw_name, 1);
	esetenv("HOME", su_info->pw_dir, 1);
	esetenv("SHELL", shell, 1);
    }

    {
	int i;
	char **args;
	char *p;

	p = strrchr(shell, '/');
	if(p)
	    p++;
	else
	    p = shell;

	if (strcmp(p, "csh") != 0)
	    csh_f_flag = 0;

        args = malloc(((cmd ? 2 : 0) + 1 + argc - optind + 1 + csh_f_flag) * sizeof(*args));
	if (args == NULL)
	    err (1, "malloc");
	i = 0;
	if(full_login) {
	    if (asprintf(&args[i++], "-%s", p) == -1)
		errx (1, "malloc");
	} else
	    args[i++] = p;
	if (cmd) {
	   args[i++] = "-c";
	   args[i++] = cmd;
	}

	if (csh_f_flag)
	    args[i++] = "-f";

	for (argv += optind; *argv; ++argv)
	   args[i++] = *argv;
	args[i] = NULL;

	if(setgid(su_info->pw_gid) < 0)
	    err(1, "setgid");
	if (initgroups (su_info->pw_name, su_info->pw_gid) < 0)
	    err (1, "initgroups");
	if(setuid(su_info->pw_uid) < 0
	   || (su_info->pw_uid != 0 && setuid(0) == 0))
	    err(1, "setuid");

#ifdef KRB5
        if (ok == 5)
           krb5_start_session();
#endif
	execve(shell, args, environ);
    }

    exit(1);
}
