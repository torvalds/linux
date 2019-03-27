/*-
 * Copyright (c) 2011 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $OpenPAM: pamtest.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <err.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <security/pam_appl.h>
#include <security/openpam.h>	/* for openpam_ttyconv() */

/* OpenPAM internals */
extern const char *pam_item_name[PAM_NUM_ITEMS];
extern int openpam_debug;

static pam_handle_t *pamh;
static struct pam_conv pamc;

static int silent;
static int verbose;

static void pt_verbose(const char *, ...)
	OPENPAM_FORMAT ((__printf__, 1, 2));
static void pt_error(int, const char *, ...)
	OPENPAM_FORMAT ((__printf__, 2, 3));

/*
 * Print an information message if -v was specified at least once
 */
static void
pt_verbose(const char *fmt, ...)
{
	va_list ap;

	if (verbose) {
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
		fprintf(stderr, "\n");
	}
}

/*
 * Print an error message
 */
static void
pt_error(int e, const char *fmt, ...)
{
	va_list ap;

	if (e == PAM_SUCCESS && !verbose)
		return;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, ": %s\n", pam_strerror(NULL, e));
}

/*
 * Wrapper for pam_start(3)
 */
static int
pt_start(const char *service, const char *user)
{
	int pame;

	pamc.conv = &openpam_ttyconv;
	pt_verbose("pam_start(%s, %s)", service, user);
	if ((pame = pam_start(service, user, &pamc, &pamh)) != PAM_SUCCESS)
		pt_error(pame, "pam_start(%s)", service);
	return (pame);
}

/*
 * Wrapper for pam_authenticate(3)
 */
static int
pt_authenticate(int flags)
{
	int pame;

	flags |= silent;
	pt_verbose("pam_authenticate()");
	if ((pame = pam_authenticate(pamh, flags)) != PAM_SUCCESS)
		pt_error(pame, "pam_authenticate()");
	return (pame);
}

/*
 * Wrapper for pam_acct_mgmt(3)
 */
static int
pt_acct_mgmt(int flags)
{
	int pame;

	flags |= silent;
	pt_verbose("pam_acct_mgmt()");
	if ((pame = pam_acct_mgmt(pamh, flags)) != PAM_SUCCESS)
		pt_error(pame, "pam_acct_mgmt()");
	return (pame);
}

/*
 * Wrapper for pam_chauthtok(3)
 */
static int
pt_chauthtok(int flags)
{
	int pame;

	flags |= silent;
	pt_verbose("pam_chauthtok()");
	if ((pame = pam_chauthtok(pamh, flags)) != PAM_SUCCESS)
		pt_error(pame, "pam_chauthtok()");
	return (pame);
}

/*
 * Wrapper for pam_setcred(3)
 */
static int
pt_setcred(int flags)
{
	int pame;

	flags |= silent;
	pt_verbose("pam_setcred()");
	if ((pame = pam_setcred(pamh, flags)) != PAM_SUCCESS)
		pt_error(pame, "pam_setcred()");
	return (pame);
}

/*
 * Wrapper for pam_open_session(3)
 */
static int
pt_open_session(int flags)
{
	int pame;

	flags |= silent;
	pt_verbose("pam_open_session()");
	if ((pame = pam_open_session(pamh, flags)) != PAM_SUCCESS)
		pt_error(pame, "pam_open_session()");
	return (pame);
}

/*
 * Wrapper for pam_close_session(3)
 */
static int
pt_close_session(int flags)
{
	int pame;

	flags |= silent;
	pt_verbose("pam_close_session()");
	if ((pame = pam_close_session(pamh, flags)) != PAM_SUCCESS)
		pt_error(pame, "pam_close_session()");
	return (pame);
}

/*
 * Wrapper for pam_set_item(3)
 */
static int
pt_set_item(int item, const char *p)
{
	int pame;

	switch (item) {
	case PAM_SERVICE:
	case PAM_USER:
	case PAM_AUTHTOK:
	case PAM_OLDAUTHTOK:
	case PAM_TTY:
	case PAM_RHOST:
	case PAM_RUSER:
	case PAM_USER_PROMPT:
	case PAM_AUTHTOK_PROMPT:
	case PAM_OLDAUTHTOK_PROMPT:
	case PAM_HOST:
		pt_verbose("setting %s to %s", pam_item_name[item], p);
		break;
	default:
		pt_verbose("setting %s", pam_item_name[item]);
		break;
	}
	if ((pame = pam_set_item(pamh, item, p)) != PAM_SUCCESS)
		pt_error(pame, "pam_set_item(%s)", pam_item_name[item]);
	return (pame);
}

/*
 * Wrapper for pam_end(3)
 */
static int
pt_end(int pame)
{

	if (pamh != NULL && (pame = pam_end(pamh, pame)) != PAM_SUCCESS)
		/* can't happen */
		pt_error(pame, "pam_end()");
	return (pame);
}

/*
 * Retrieve and list the PAM environment variables
 */
static int
pt_listenv(void)
{
	char **pam_envlist, **pam_env;

	if ((pam_envlist = pam_getenvlist(pamh)) == NULL ||
	    *pam_envlist == NULL) {
		pt_verbose("no environment variables.");
	} else {
		pt_verbose("environment variables:");
		for (pam_env = pam_envlist; *pam_env != NULL; ++pam_env) {
			printf(" %s\n", *pam_env);
			free(*pam_env);
		}
	}
	free(pam_envlist);
	return (PAM_SUCCESS);
}

/*
 * Print usage string and exit
 */
static void
usage(void)
{

	fprintf(stderr, "usage: pamtest %s service command ...\n",
	    "[-dkMPsv] [-H rhost] [-h host] [-t tty] [-U ruser] [-u user]");
	exit(1);
}

/*
 * Handle an option that takes an int argument and can be used only once
 */
static void
opt_num_once(int opt, long *num, const char *arg)
{
	char *end;
	long l;

	l = strtol(arg, &end, 0);
	if (end == optarg || *end != '\0') {
		fprintf(stderr,
		    "The -%c option expects a numeric argument\n", opt);
		usage();
	}
	*num = l;
}

/*
 * Handle an option that takes a string argument and can be used only once
 */
static void
opt_str_once(int opt, const char **p, const char *arg)
{

	if (*p != NULL) {
		fprintf(stderr, "The -%c option can only be used once\n", opt);
		usage();
	}
	*p = arg;
}

/*
 * Entry point
 */
int
main(int argc, char *argv[])
{
	char hostname[1024];
	const char *rhost = NULL;
	const char *host = NULL;
	const char *ruser = NULL;
	const char *user = NULL;
	const char *service = NULL;
	const char *tty = NULL;
	long timeout = 0;
	int keepatit = 0;
	int pame;
	int opt;

	while ((opt = getopt(argc, argv, "dH:h:kMPsT:t:U:u:v")) != -1)
		switch (opt) {
		case 'd':
			openpam_debug++;
			break;
		case 'H':
			opt_str_once(opt, &rhost, optarg);
			break;
		case 'h':
			opt_str_once(opt, &host, optarg);
			break;
		case 'k':
			keepatit = 1;
			break;
		case 'M':
			openpam_set_feature(OPENPAM_RESTRICT_MODULE_NAME, 0);
			openpam_set_feature(OPENPAM_VERIFY_MODULE_FILE, 0);
			break;
		case 'P':
			openpam_set_feature(OPENPAM_RESTRICT_SERVICE_NAME, 0);
			openpam_set_feature(OPENPAM_VERIFY_POLICY_FILE, 0);
			break;
		case 's':
			silent = PAM_SILENT;
			break;
		case 'T':
			opt_num_once(opt, &timeout, optarg);
			if (timeout < 0 || timeout > INT_MAX) {
				fprintf(stderr,
				    "Invalid conversation timeout\n");
				usage();
			}
			openpam_ttyconv_timeout = (int)timeout;
			break;
		case 't':
			opt_str_once(opt, &tty, optarg);
			break;
		case 'U':
			opt_str_once(opt, &ruser, optarg);
			break;
		case 'u':
			opt_str_once(opt, &user, optarg);
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	service = *argv;
	--argc;
	++argv;

	/* defaults */
	if (service == NULL)
		service = "pamtest";
	if (rhost == NULL) {
		if (gethostname(hostname, sizeof(hostname)) == -1)
			err(1, "gethostname()");
		rhost = hostname;
	}
	if (tty == NULL)
		tty = ttyname(STDERR_FILENO);
	if (user == NULL)
		user = getlogin();
	if (ruser == NULL)
		ruser = user;

	/* initialize PAM */
	if ((pame = pt_start(service, user)) != PAM_SUCCESS)
		goto end;

	/*
	 * pam_start(3) sets this to the machine's hostname, but we allow
	 * the user to override it.
	 */
	if (host != NULL)
		if ((pame = pt_set_item(PAM_HOST, host)) != PAM_SUCCESS)
		    goto end;

	/*
	 * The remote host / user / tty are usually set by the
	 * application.
	 */
	if ((pame = pt_set_item(PAM_RHOST, rhost)) != PAM_SUCCESS ||
	    (pame = pt_set_item(PAM_RUSER, ruser)) != PAM_SUCCESS ||
	    (pame = pt_set_item(PAM_TTY, tty)) != PAM_SUCCESS)
		goto end;

	while (argc > 0) {
		if (strcmp(*argv, "listenv") == 0 ||
		    strcmp(*argv, "env") == 0) {
			pame = pt_listenv();
		} else if (strcmp(*argv, "authenticate") == 0 ||
		    strcmp(*argv, "auth") == 0) {
			pame = pt_authenticate(0);
		} else if (strcmp(*argv, "acct_mgmt") == 0 ||
		    strcmp(*argv, "account") == 0) {
			pame = pt_acct_mgmt(0);
		} else if (strcmp(*argv, "chauthtok") == 0 ||
		    strcmp(*argv, "change") == 0) {
			pame = pt_chauthtok(PAM_CHANGE_EXPIRED_AUTHTOK);
		} else if (strcmp(*argv, "forcechauthtok") == 0 ||
		    strcmp(*argv, "forcechange") == 0) {
			pame = pt_chauthtok(0);
		} else if (strcmp(*argv, "setcred") == 0 ||
		    strcmp(*argv, "establish_cred") == 0) {
			pame = pt_setcred(PAM_ESTABLISH_CRED);
		} else if (strcmp(*argv, "open_session") == 0 ||
		    strcmp(*argv, "open") == 0) {
			pame = pt_open_session(0);
		} else if (strcmp(*argv, "close_session") == 0 ||
		    strcmp(*argv, "close") == 0) {
			pame = pt_close_session(0);
		} else if (strcmp(*argv, "unsetcred") == 0 ||
		    strcmp(*argv, "delete_cred") == 0) {
			pame = pt_setcred(PAM_DELETE_CRED);
		} else {
			warnx("unknown primitive: %s", *argv);
			pame = PAM_SYSTEM_ERR;
		}
		if (pame != PAM_SUCCESS && !keepatit) {
			warnx("test aborted");
			break;
		}
		--argc;
		++argv;
	}

end:
	(void)pt_end(pame);
	exit(pame == PAM_SUCCESS ? 0 : 1);
}
