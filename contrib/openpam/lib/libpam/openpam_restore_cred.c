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
 * $OpenPAM: openpam_restore_cred.c 938 2017-04-30 21:34:42Z des $
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
 * Restore credentials
 */

int
openpam_restore_cred(pam_handle_t *pamh)
{
	const struct pam_saved_cred *scred;
	const void *scredp;
	int r;

	ENTER();
	r = pam_get_data(pamh, PAM_SAVED_CRED, &scredp);
	if (r != PAM_SUCCESS)
		RETURNC(r);
	if (scredp == NULL)
		RETURNC(PAM_SYSTEM_ERR);
	scred = scredp;
	if (scred->euid != geteuid()) {
		if (seteuid(scred->euid) < 0 ||
		    setgroups(scred->ngroups, scred->groups) < 0 ||
		    setegid(scred->egid) < 0)
			RETURNC(PAM_SYSTEM_ERR);
	}
	pam_set_data(pamh, PAM_SAVED_CRED, NULL, NULL);
	RETURNC(PAM_SUCCESS);
}

/*
 * Error codes:
 *
 *	=pam_get_data
 *	PAM_SYSTEM_ERR
 */

/**
 * The =openpam_restore_cred function restores the credentials saved by
 * =openpam_borrow_cred.
 *
 * >setegid 2
 * >seteuid 2
 * >setgroups 2
 */
