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
 * $OpenPAM: openpam_borrow_cred.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/param.h>

#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <unistd.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"
#include "openpam_cred.h"

/*
 * OpenPAM extension
 *
 * Temporarily borrow user credentials
 */

int
openpam_borrow_cred(pam_handle_t *pamh,
	const struct passwd *pwd)
{
	struct pam_saved_cred *scred;
	const void *scredp;
	int r;

	ENTERI(pwd->pw_uid);
	r = pam_get_data(pamh, PAM_SAVED_CRED, &scredp);
	if (r == PAM_SUCCESS && scredp != NULL) {
		openpam_log(PAM_LOG_LIBDEBUG,
		    "already operating under borrowed credentials");
		RETURNC(PAM_SYSTEM_ERR);
	}
	if (geteuid() != 0 && geteuid() != pwd->pw_uid) {
		openpam_log(PAM_LOG_LIBDEBUG, "called with non-zero euid: %d",
		    (int)geteuid());
		RETURNC(PAM_PERM_DENIED);
	}
	scred = calloc(1, sizeof *scred);
	if (scred == NULL)
		RETURNC(PAM_BUF_ERR);
	scred->euid = geteuid();
	scred->egid = getegid();
	r = getgroups(NGROUPS_MAX, scred->groups);
	if (r < 0) {
		FREE(scred);
		RETURNC(PAM_SYSTEM_ERR);
	}
	scred->ngroups = r;
	r = pam_set_data(pamh, PAM_SAVED_CRED, scred, &openpam_free_data);
	if (r != PAM_SUCCESS) {
		FREE(scred);
		RETURNC(r);
	}
	if (geteuid() == pwd->pw_uid)
		RETURNC(PAM_SUCCESS);
	if (initgroups(pwd->pw_name, pwd->pw_gid) < 0 ||
	      setegid(pwd->pw_gid) < 0 || seteuid(pwd->pw_uid) < 0) {
		openpam_restore_cred(pamh);
		RETURNC(PAM_SYSTEM_ERR);
	}
	RETURNC(PAM_SUCCESS);
}

/*
 * Error codes:
 *
 *	=pam_set_data
 *	PAM_SYSTEM_ERR
 *	PAM_BUF_ERR
 *	PAM_PERM_DENIED
 */

/**
 * The =openpam_borrow_cred function saves the current credentials and
 * switches to those of the user specified by its =pwd argument.
 * The affected credentials are the effective UID, the effective GID, and
 * the group access list.
 * The original credentials can be restored using =openpam_restore_cred.
 *
 * >setegid 2
 * >seteuid 2
 * >setgroups 2
 */
