/*
 * Copyright (c) 2006 Darren Tucker.  All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "includes.h"

#include <stdarg.h>
#include <unistd.h>

#include "log.h"
#include "misc.h"
#include "servconf.h"
#include "sshkey.h"
#include "hostfile.h"
#include "auth.h"
#include "auth-pam.h"
#include "platform.h"

#include "openbsd-compat/openbsd-compat.h"

extern int use_privsep;
extern ServerOptions options;

void
platform_pre_listen(void)
{
#ifdef LINUX_OOM_ADJUST
	/* Adjust out-of-memory killer so listening process is not killed */
	oom_adjust_setup();
#endif
}

void
platform_pre_fork(void)
{
#ifdef USE_SOLARIS_PROCESS_CONTRACTS
	solaris_contract_pre_fork();
#endif
}

void
platform_pre_restart(void)
{
#ifdef LINUX_OOM_ADJUST
	oom_adjust_restore();
#endif
}

void
platform_post_fork_parent(pid_t child_pid)
{
#ifdef USE_SOLARIS_PROCESS_CONTRACTS
	solaris_contract_post_fork_parent(child_pid);
#endif
}

void
platform_post_fork_child(void)
{
#ifdef USE_SOLARIS_PROCESS_CONTRACTS
	solaris_contract_post_fork_child();
#endif
#ifdef LINUX_OOM_ADJUST
	oom_adjust_restore();
#endif
}

/* return 1 if we are running with privilege to swap UIDs, 0 otherwise */
int
platform_privileged_uidswap(void)
{
#ifdef HAVE_CYGWIN
	/* uid 0 is not special on Cygwin so always try */
	return 1;
#else
	return (getuid() == 0 || geteuid() == 0);
#endif
}

/*
 * This gets called before switching UIDs, and is called even when sshd is
 * not running as root.
 */
void
platform_setusercontext(struct passwd *pw)
{
#ifdef WITH_SELINUX
	/* Cache selinux status for later use */
	(void)ssh_selinux_enabled();
#endif

#ifdef USE_SOLARIS_PROJECTS
	/*
	 * If solaris projects were detected, set the default now, unless
	 * we are using PAM in which case it is the responsibility of the
	 * PAM stack.
	 */
	if (!options.use_pam && (getuid() == 0 || geteuid() == 0))
		solaris_set_default_project(pw);
#endif

#if defined(HAVE_LOGIN_CAP) && defined (__bsdi__)
	if (getuid() == 0 || geteuid() == 0)
		setpgid(0, 0);
# endif

#if defined(HAVE_LOGIN_CAP) && defined(USE_PAM)
	/*
	 * If we have both LOGIN_CAP and PAM, we want to establish creds
	 * before calling setusercontext (in session.c:do_setusercontext).
	 */
	if (getuid() == 0 || geteuid() == 0) {
		if (options.use_pam) {
			do_pam_setcred(use_privsep);
		}
	}
# endif /* USE_PAM */

#if !defined(HAVE_LOGIN_CAP) && defined(HAVE_GETLUID) && defined(HAVE_SETLUID)
	if (getuid() == 0 || geteuid() == 0) {
		/* Sets login uid for accounting */
		if (getluid() == -1 && setluid(pw->pw_uid) == -1)
			error("setluid: %s", strerror(errno));
	}
#endif
}

/*
 * This gets called after we've established the user's groups, and is only
 * called if sshd is running as root.
 */
void
platform_setusercontext_post_groups(struct passwd *pw)
{
#if !defined(HAVE_LOGIN_CAP) && defined(USE_PAM)
	/*
	 * PAM credentials may take the form of supplementary groups.
	 * These will have been wiped by the above initgroups() call.
	 * Reestablish them here.
	 */
	if (options.use_pam) {
		do_pam_setcred(use_privsep);
	}
#endif /* USE_PAM */

#if !defined(HAVE_LOGIN_CAP) && (defined(WITH_IRIX_PROJECT) || \
    defined(WITH_IRIX_JOBS) || defined(WITH_IRIX_ARRAY))
	irix_setusercontext(pw);
#endif /* defined(WITH_IRIX_PROJECT) || defined(WITH_IRIX_JOBS) || defined(WITH_IRIX_ARRAY) */

#ifdef _AIX
	aix_usrinfo(pw);
#endif /* _AIX */

#ifdef HAVE_SETPCRED
	/*
	 * If we have a chroot directory, we set all creds except real
	 * uid which we will need for chroot.  If we don't have a
	 * chroot directory, we don't override anything.
	 */
	{
		char **creds = NULL, *chroot_creds[] =
		    { "REAL_USER=root", NULL };

		if (options.chroot_directory != NULL &&
		    strcasecmp(options.chroot_directory, "none") != 0)
			creds = chroot_creds;

		if (setpcred(pw->pw_name, creds) == -1)
			fatal("Failed to set process credentials");
	}
#endif /* HAVE_SETPCRED */
#ifdef WITH_SELINUX
	ssh_selinux_setup_exec_context(pw->pw_name);
#endif
}

char *
platform_krb5_get_principal_name(const char *pw_name)
{
#ifdef USE_AIX_KRB_NAME
	return aix_krb5_get_principal_name(pw_name);
#else
	return NULL;
#endif
}
