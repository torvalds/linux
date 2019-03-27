/*
 * Copyright (c) 1997 - 2006 Kungliga Tekniska Högskolan
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

#include "login_locl.h"
#ifdef HAVE_CAPABILITY_H
#include <capability.h>
#endif
#ifdef HAVE_SYS_CAPABILITY_H
#include <sys/capability.h>
#endif
#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif

RCSID("$Id$");

static int login_timeout = 60;

static int
start_login_process(void)
{
    char *prog, *argv0;
    prog = login_conf_get_string("login_program");
    if(prog == NULL)
	return 0;
    argv0 = strrchr(prog, '/');

    if(argv0)
	argv0++;
    else
	argv0 = prog;

    return simple_execle(prog, argv0, NULL, env);
}

static int
start_logout_process(void)
{
    char *prog, *argv0;
    pid_t pid;

    prog = login_conf_get_string("logout_program");
    if(prog == NULL)
	return 0;
    argv0 = strrchr(prog, '/');

    if(argv0)
	argv0++;
    else
	argv0 = prog;

    pid = fork();
    if(pid == 0) {
	/* avoid getting signals sent to the shell */
	setpgid(0, getpid());
	return 0;
    }
    if(pid == -1)
	err(1, "fork");
    /* wait for the real login process to exit */
#ifdef HAVE_SETPROCTITLE
    setproctitle("waitpid %d", pid);
#endif
    while(1) {
	int status;
	int ret;
	ret = waitpid(pid, &status, 0);
	if(ret > 0) {
	    if(WIFEXITED(status) || WIFSIGNALED(status)) {
		execle(prog, argv0, NULL, env);
		err(1, "exec %s", prog);
	    }
	} else if(ret < 0)
	    err(1, "waitpid");
    }
}

static void
exec_shell(const char *shell, int fallback)
{
    char *sh;
    const char *p;

    extend_env(NULL);
    if(start_login_process() < 0)
	warn("login process");
    start_logout_process();

    p = strrchr(shell, '/');
    if(p)
	p++;
    else
	p = shell;
    if (asprintf(&sh, "-%s", p) == -1)
	errx(1, "Out of memory");
    execle(shell, sh, NULL, env);
    if(fallback){
	warnx("Can't exec %s, trying %s",
	      shell, _PATH_BSHELL);
	execle(_PATH_BSHELL, "-sh", NULL, env);
	err(1, "%s", _PATH_BSHELL);
    }
    err(1, "%s", shell);
}

static enum { NONE = 0, AUTH_KRB5 = 2, AUTH_OTP = 3 } auth;

#ifdef OTP
static OtpContext otp_ctx;

static int
otp_verify(struct passwd *pwd, const char *password)
{
   return (otp_verify_user (&otp_ctx, password));
}
#endif /* OTP */


static int pag_set = 0;

#ifdef KRB5
static krb5_context context;
static krb5_ccache  id, id2;

static int
krb5_verify(struct passwd *pwd, const char *password)
{
    krb5_error_code ret;
    krb5_principal princ;

    ret = krb5_parse_name(context, pwd->pw_name, &princ);
    if(ret)
	return 1;
    ret = krb5_cc_new_unique(context, krb5_cc_type_memory, NULL, &id);
    if(ret) {
	krb5_free_principal(context, princ);
	return 1;
    }
    ret = krb5_verify_user_lrealm(context,
				  princ,
				  id,
				  password,
				  1,
				  NULL);
    krb5_free_principal(context, princ);
    return ret;
}

static int
krb5_start_session (const struct passwd *pwd)
{
    krb5_error_code ret;
    char residual[64];

    /* copy credentials to file cache */
    snprintf(residual, sizeof(residual), "FILE:/tmp/krb5cc_%u",
	     (unsigned)pwd->pw_uid);
    krb5_cc_resolve(context, residual, &id2);
    ret = krb5_cc_copy_cache(context, id, id2);
    if (ret == 0)
	add_env("KRB5CCNAME", residual);
    else {
	krb5_cc_destroy (context, id2);
	return ret;
    }
    krb5_cc_close(context, id2);
    krb5_cc_destroy(context, id);
    return 0;
}

static void
krb5_finish (void)
{
    krb5_free_context(context);
}

static void
krb5_get_afs_tokens (const struct passwd *pwd)
{
    char cell[64];
    char *pw_dir;
    krb5_error_code ret;

    if (!k_hasafs ())
	return;

    ret = krb5_cc_default(context, &id2);

    if (ret == 0) {
	pw_dir = pwd->pw_dir;

	if (!pag_set) {
	    k_setpag();
	    pag_set = 1;
	}

	if(k_afs_cell_of_file(pw_dir, cell, sizeof(cell)) == 0)
	    krb5_afslog_uid_home (context, id2,
				  cell, NULL, pwd->pw_uid, pwd->pw_dir);
	krb5_afslog_uid_home (context, id2, NULL, NULL,
			      pwd->pw_uid, pwd->pw_dir);
	krb5_cc_close (context, id2);
    }
}

#endif /* KRB5 */

static int f_flag;
static int p_flag;
#if 0
static int r_flag;
#endif
static int version_flag;
static int help_flag;
static char *remote_host;
static char *auth_level = NULL;

struct getargs args[] = {
    { NULL, 'a', arg_string,    &auth_level,    "authentication mode" },
#if 0
    { NULL, 'd' },
#endif
    { NULL, 'f', arg_flag,	&f_flag,	"pre-authenticated" },
    { NULL, 'h', arg_string,	&remote_host,	"remote host", "hostname" },
    { NULL, 'p', arg_flag,	&p_flag,	"don't purge environment" },
#if 0
    { NULL, 'r', arg_flag,	&r_flag,	"rlogin protocol" },
#endif
    { "version", 0,  arg_flag,	&version_flag },
    { "help",	 0,  arg_flag,&help_flag, }
};

int nargs = sizeof(args) / sizeof(args[0]);

static void
update_utmp(const char *username, const char *hostname,
	    char *tty, char *ttyn)
{
    /*
     * Update the utmp files, both BSD and SYSV style.
     */
    if (utmpx_login(tty, username, hostname) != 0 && !f_flag) {
	printf("No utmpx entry.  You must exec \"login\" from the "
	       "lowest level shell.\n");
	exit(1);
    }
    utmp_login(ttyn, username, hostname);
}

static void
checknologin(void)
{
    FILE *f;
    char buf[1024];

    f = fopen(_PATH_NOLOGIN, "r");
    if(f == NULL)
	return;
    while(fgets(buf, sizeof(buf), f))
	fputs(buf, stdout);
    fclose(f);
    exit(0);
}

/* print contents of a file */
static void
show_file(const char *file)
{
    FILE *f;
    char buf[BUFSIZ];
    if((f = fopen(file, "r")) == NULL)
	return;
    while (fgets(buf, sizeof(buf), f))
	fputs(buf, stdout);
    fclose(f);
}

/*
 * Actually log in the user.  `pwd' contains all the relevant
 * information about the user.  `ttyn' is the complete name of the tty
 * and `tty' the short name.
 */

static void
do_login(const struct passwd *pwd, char *tty, char *ttyn)
{
#ifdef HAVE_GETSPNAM
    struct spwd *sp;
#endif
    int rootlogin = (pwd->pw_uid == 0);
    gid_t tty_gid;
    struct group *gr;
    const char *home_dir;
    int i;

    if(!rootlogin)
	checknologin();

#ifdef HAVE_GETSPNAM
    sp = getspnam(pwd->pw_name);
#endif

    update_utmp(pwd->pw_name, remote_host ? remote_host : "",
		tty, ttyn);

    gr = getgrnam ("tty");
    if (gr != NULL)
	tty_gid = gr->gr_gid;
    else
	tty_gid = pwd->pw_gid;

    if (chown (ttyn, pwd->pw_uid, tty_gid) < 0) {
	warn("chown %s", ttyn);
	if (rootlogin == 0)
	    exit (1);
    }

    if (chmod (ttyn, S_IRUSR | S_IWUSR | S_IWGRP) < 0) {
	warn("chmod %s", ttyn);
	if (rootlogin == 0)
	    exit (1);
    }

#ifdef HAVE_SETLOGIN
    if(setlogin(pwd->pw_name)){
	warn("setlogin(%s)", pwd->pw_name);
	if(rootlogin == 0)
	    exit(1);
    }
#endif
    if(rootlogin == 0) {
	const char *file = login_conf_get_string("limits");
	if(file == NULL)
	    file = _PATH_LIMITS_CONF;

	read_limits_conf(file, pwd);
    }

#ifdef HAVE_SETPCRED
    if (setpcred (pwd->pw_name, NULL) == -1)
	warn("setpcred(%s)", pwd->pw_name);
#endif /* HAVE_SETPCRED */
#ifdef HAVE_INITGROUPS
    if(initgroups(pwd->pw_name, pwd->pw_gid)){
	warn("initgroups(%s, %u)", pwd->pw_name, (unsigned)pwd->pw_gid);
	if(rootlogin == 0)
	    exit(1);
    }
#endif
    if(do_osfc2_magic(pwd->pw_uid))
	exit(1);
    if(setgid(pwd->pw_gid)){
	warn("setgid(%u)", (unsigned)pwd->pw_gid);
	if(rootlogin == 0)
	    exit(1);
    }
    if(setuid(pwd->pw_uid) || (pwd->pw_uid != 0 && setuid(0) == 0)) {
	warn("setuid(%u)", (unsigned)pwd->pw_uid);
	if(rootlogin == 0)
	    exit(1);
    }

    /* make sure signals are set to default actions, apparently some
       OS:es like to ignore SIGINT, which is not very convenient */

    for (i = 1; i < NSIG; ++i)
	signal(i, SIG_DFL);

    /* all kinds of different magic */

#ifdef HAVE_GETSPNAM
    check_shadow(pwd, sp);
#endif

#if defined(HAVE_GETUDBNAM) && defined(HAVE_SETLIM)
    {
	struct udb *udb;
	long t;
	const long maxcpu = 46116860184; /* some random constant */
	udb = getudbnam(pwd->pw_name);
	if(udb == UDB_NULL)
	    errx(1, "Failed to get UDB entry.");
	t = udb->ue_pcpulim[UDBRC_INTER];
	if(t == 0 || t > maxcpu)
	    t = CPUUNLIM;
	else
	    t *= 100 * CLOCKS_PER_SEC;

	if(limit(C_PROC, 0, L_CPU, t) < 0)
	    warn("limit C_PROC");

	t = udb->ue_jcpulim[UDBRC_INTER];
	if(t == 0 || t > maxcpu)
	    t = CPUUNLIM;
	else
	    t *= 100 * CLOCKS_PER_SEC;

	if(limit(C_JOBPROCS, 0, L_CPU, t) < 0)
	    warn("limit C_JOBPROCS");

	nice(udb->ue_nice[UDBRC_INTER]);
    }
#endif
#if defined(HAVE_SGI_GETCAPABILITYBYNAME) && defined(HAVE_CAP_SET_PROC)
	/* XXX SGI capability hack IRIX 6.x (x >= 0?) has something
	   called capabilities, that allow you to give away
	   permissions (such as chown) to specific processes. From 6.5
	   this is default on, and the default capability set seems to
	   not always be the empty set. The problem is that the
	   runtime linker refuses to do just about anything if the
	   process has *any* capabilities set, so we have to remove
	   them here (unless otherwise instructed by /etc/capability).
	   In IRIX < 6.5, these functions was called sgi_cap_setproc,
	   etc, but we ignore this fact (it works anyway). */
	{
	    struct user_cap *ucap = sgi_getcapabilitybyname(pwd->pw_name);
	    cap_t cap;
	    if(ucap == NULL)
		cap = cap_from_text("all=");
	    else
		cap = cap_from_text(ucap->ca_default);
	    if(cap == NULL)
		err(1, "cap_from_text");
	    if(cap_set_proc(cap) < 0)
		err(1, "cap_set_proc");
	    cap_free(cap);
	    free(ucap);
	}
#endif
    home_dir = pwd->pw_dir;
    if (chdir(home_dir) < 0) {
	fprintf(stderr, "No home directory \"%s\"!\n", pwd->pw_dir);
	if (chdir("/"))
	    exit(0);
	home_dir = "/";
	fprintf(stderr, "Logging in with home = \"/\".\n");
    }
#ifdef KRB5
    if (auth == AUTH_KRB5) {
	krb5_start_session (pwd);
    }

    krb5_get_afs_tokens (pwd);

    krb5_finish ();
#endif /* KRB5 */

    add_env("PATH", _PATH_DEFPATH);

    {
	const char *str = login_conf_get_string("environment");
	char buf[MAXPATHLEN];

	if(str == NULL) {
	    login_read_env(_PATH_ETC_ENVIRONMENT);
	} else {
	    while(strsep_copy(&str, ",", buf, sizeof(buf)) != -1) {
		if(buf[0] == '\0')
		    continue;
		login_read_env(buf);
	    }
	}
    }
    {
	const char *str = login_conf_get_string("motd");
	char buf[MAXPATHLEN];

	if(str != NULL) {
	    while(strsep_copy(&str, ",", buf, sizeof(buf)) != -1) {
		if(buf[0] == '\0')
		    continue;
		show_file(buf);
	    }
	} else {
	    str = login_conf_get_string("welcome");
	    if(str != NULL)
		show_file(str);
	}
    }
    add_env("HOME", home_dir);
    add_env("USER", pwd->pw_name);
    add_env("LOGNAME", pwd->pw_name);
    add_env("SHELL", pwd->pw_shell);
    exec_shell(pwd->pw_shell, rootlogin);
}

static int
check_password(struct passwd *pwd, const char *password)
{
    if(pwd->pw_passwd == NULL)
	return 1;
    if(pwd->pw_passwd[0] == '\0'){
#ifdef ALLOW_NULL_PASSWORD
	return password[0] != '\0';
#else
	return 1;
#endif
    }
    if(strcmp(pwd->pw_passwd, crypt(password, pwd->pw_passwd)) == 0)
	return 0;
#ifdef KRB5
    if(krb5_verify(pwd, password) == 0) {
	auth = AUTH_KRB5;
	return 0;
    }
#endif
#ifdef OTP
    if (otp_verify (pwd, password) == 0) {
       auth = AUTH_OTP;
       return 0;
    }
#endif
    return 1;
}

static void
usage(int status)
{
    arg_printusage(args, nargs, NULL, "[username]");
    exit(status);
}

static RETSIGTYPE
sig_handler(int sig)
{
    if (sig == SIGALRM)
         fprintf(stderr, "Login timed out after %d seconds\n",
                login_timeout);
      else
         fprintf(stderr, "Login received signal, exiting\n");
    exit(0);
}

int
main(int argc, char **argv)
{
    int max_tries = 5;
    int try;

    char username[32];
    int optidx = 0;

    int ask = 1;
    struct sigaction sa;

    setprogname(argv[0]);

#ifdef KRB5
    {
	krb5_error_code ret;

	ret = krb5_init_context(&context);
	if (ret)
	    errx (1, "krb5_init_context failed: %d", ret);
    }
#endif

    openlog("login", LOG_ODELAY | LOG_PID, LOG_AUTH);

    if (getarg (args, sizeof(args) / sizeof(args[0]), argc, argv,
		&optidx))
	usage (1);
    argc -= optidx;
    argv += optidx;

    if(help_flag)
	usage(0);
    if (version_flag) {
	print_version (NULL);
	return 0;
    }

    if (geteuid() != 0)
	errx(1, "only root may use login, use su");

    /* Default tty settings. */
    stty_default();

    if(p_flag)
	copy_env();
    else {
	/* this set of variables is always preserved by BSD login */
	if(getenv("TERM"))
	    add_env("TERM", getenv("TERM"));
	if(getenv("TZ"))
	    add_env("TZ", getenv("TZ"));
    }

    if(*argv){
	if(strchr(*argv, '=') == NULL && strcmp(*argv, "-") != 0){
	    strlcpy (username, *argv, sizeof(username));
	    ask = 0;
	}
    }

#if defined(DCE) && defined(AIX)
    esetenv("AUTHSTATE", "DCE", 1);
#endif

    /* XXX should we care about environment on the command line? */

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);
    alarm(login_timeout);

    for(try = 0; try < max_tries; try++){
	struct passwd *pwd;
	char password[128];
	int ret;
	char ttname[32];
	char *tty, *ttyn;
        char prompt[128];
#ifdef OTP
        char otp_str[256];
#endif

	if(ask){
	    f_flag = 0;
#if 0
	    r_flag = 0;
#endif
	    ret = read_string("login: ", username, sizeof(username), 1);
	    if(ret == -3)
		exit(0);
	    if(ret == -2)
		sig_handler(0); /* exit */
	}
        pwd = k_getpwnam(username);
#ifdef ALLOW_NULL_PASSWORD
        if (pwd != NULL && (pwd->pw_passwd[0] == '\0')) {
            strcpy(password,"");
        }
        else
#endif

        {
#ifdef OTP
           if(auth_level && strcmp(auth_level, "otp") == 0 &&
                 otp_challenge(&otp_ctx, username,
                            otp_str, sizeof(otp_str)) == 0)
                 snprintf (prompt, sizeof(prompt), "%s's %s Password: ",
                            username, otp_str);
            else
#endif
                 strncpy(prompt, "Password: ", sizeof(prompt));

	    if (f_flag == 0) {
	       ret = read_string(prompt, password, sizeof(password), 0);
               if (ret == -3) {
                  ask = 1;
                  continue;
               }
               if (ret == -2)
                  sig_handler(0);
            }
         }

	if(pwd == NULL){
	    fprintf(stderr, "Login incorrect.\n");
	    ask = 1;
	    continue;
	}

	if(f_flag == 0 && check_password(pwd, password)){
	    fprintf(stderr, "Login incorrect.\n");
            ask = 1;
	    continue;
	}
	ttyn = ttyname(STDIN_FILENO);
	if(ttyn == NULL){
	    snprintf(ttname, sizeof(ttname), "%s??", _PATH_TTY);
	    ttyn = ttname;
	}
	if (strncmp (ttyn, _PATH_DEV, strlen(_PATH_DEV)) == 0)
	    tty = ttyn + strlen(_PATH_DEV);
	else
	    tty = ttyn;

	if (login_access (pwd, remote_host ? remote_host : tty) == 0) {
	    fprintf(stderr, "Permission denied\n");
	    if (remote_host)
		syslog(LOG_NOTICE, "%s LOGIN REFUSED FROM %s",
		       pwd->pw_name, remote_host);
	    else
		syslog(LOG_NOTICE, "%s LOGIN REFUSED ON %s",
		       pwd->pw_name, tty);
	    exit (1);
	} else {
	    if (remote_host)
		syslog(LOG_NOTICE, "%s LOGIN ACCEPTED FROM %s ppid=%d",
		       pwd->pw_name, remote_host, (int) getppid());
	    else
		syslog(LOG_NOTICE, "%s LOGIN ACCEPTED ON %s ppid=%d",
		       pwd->pw_name, tty, (int) getppid());
	}
        alarm(0);
	do_login(pwd, tty, ttyn);
    }
    exit(1);
}
