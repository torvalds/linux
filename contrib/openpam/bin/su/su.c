/*-
 * Copyright (c) 2002-2003 Networks Associates Technology, Inc.
 * Copyright (c) 2004-2011 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by ThinkSec AS and
 * Network Associates Laboratories, the Security Research Division of
 * Network Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
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
 * $OpenPAM: su.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/param.h>
#include <sys/wait.h>

#include <err.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <security/pam_appl.h>
#include <security/openpam.h>	/* for openpam_ttyconv() */

extern char **environ;

static pam_handle_t *pamh;
static struct pam_conv pamc;

static void
usage(void)
{

	fprintf(stderr, "usage: su [login [args]]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	char hostname[MAXHOSTNAMELEN];
	const char *user, *tty;
	const void *item;
	char **args, **pam_envlist, **pam_env;
	struct passwd *pwd;
	int o, pam_err, status;
	pid_t pid;

	while ((o = getopt(argc, argv, "")) != -1)
		switch (o) {
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc > 0) {
		user = *argv;
		--argc;
		++argv;
	} else {
		user = "root";
	}

	/* initialize PAM */
	pamc.conv = &openpam_ttyconv;
	pam_start("su", user, &pamc, &pamh);

	/* set some items */
	gethostname(hostname, sizeof(hostname));
	if ((pam_err = pam_set_item(pamh, PAM_RHOST, hostname)) != PAM_SUCCESS)
		goto pamerr;
	user = getlogin();
	if ((pam_err = pam_set_item(pamh, PAM_RUSER, user)) != PAM_SUCCESS)
		goto pamerr;
	tty = ttyname(STDERR_FILENO);
	if ((pam_err = pam_set_item(pamh, PAM_TTY, tty)) != PAM_SUCCESS)
		goto pamerr;

	/* authenticate the applicant */
	if ((pam_err = pam_authenticate(pamh, 0)) != PAM_SUCCESS)
		goto pamerr;
	if ((pam_err = pam_acct_mgmt(pamh, 0)) == PAM_NEW_AUTHTOK_REQD)
		pam_err = pam_chauthtok(pamh, PAM_CHANGE_EXPIRED_AUTHTOK);
	if (pam_err != PAM_SUCCESS)
		goto pamerr;

	/* establish the requested credentials */
	if ((pam_err = pam_setcred(pamh, PAM_ESTABLISH_CRED)) != PAM_SUCCESS)
		goto pamerr;

	/* authentication succeeded; open a session */
	if ((pam_err = pam_open_session(pamh, 0)) != PAM_SUCCESS)
		goto pamerr;

	/* get mapped user name; PAM may have changed it */
	pam_err = pam_get_item(pamh, PAM_USER, &item);
	if (pam_err != PAM_SUCCESS || (pwd = getpwnam(user = item)) == NULL)
		goto pamerr;

	/* export PAM environment */
	if ((pam_envlist = pam_getenvlist(pamh)) != NULL) {
		for (pam_env = pam_envlist; *pam_env != NULL; ++pam_env) {
			putenv(*pam_env);
			free(*pam_env);
		}
		free(pam_envlist);
	}

	/* build argument list */
	if ((args = calloc(argc + 2, sizeof *args)) == NULL) {
		warn("calloc()");
		goto err;
	}
	*args = pwd->pw_shell;
	memcpy(args + 1, argv, argc * sizeof *args);

	/* fork and exec */
	switch ((pid = fork())) {
	case -1:
		warn("fork()");
		goto err;
	case 0:
		/* child: give up privs and start a shell */

		/* set uid and groups */
		if (initgroups(pwd->pw_name, pwd->pw_gid) == -1) {
			warn("initgroups()");
			_exit(1);
		}
		if (setgid(pwd->pw_gid) == -1) {
			warn("setgid()");
			_exit(1);
		}
		if (setuid(pwd->pw_uid) == -1) {
			warn("setuid()");
			_exit(1);
		}
		execve(*args, args, environ);
		warn("execve()");
		_exit(1);
	default:
		/* parent: wait for child to exit */
		waitpid(pid, &status, 0);

		/* close the session and release PAM resources */
		pam_err = pam_close_session(pamh, 0);
		pam_end(pamh, pam_err);

		exit(WEXITSTATUS(status));
	}

pamerr:
	fprintf(stderr, "Sorry\n");
err:
	pam_end(pamh, pam_err);
	exit(1);
}
