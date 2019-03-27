/*-
 * Copyright (c) 2002-2003 Networks Associates Technology, Inc.
 * Copyright (c) 2004-2017 Dag-Erling Smørgrav
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
 * $OpenPAM: pam_putenv.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"

/*
 * XSSO 4.2.1
 * XSSO 6 page 56
 *
 * Set the value of an environment variable
 */

int
pam_putenv(pam_handle_t *pamh,
	const char *namevalue)
{
	char **env, *p;
	size_t env_size;
	int i;

	ENTER();

	/* sanity checks */
	if ((p = strchr(namevalue, '=')) == NULL) {
		errno = EINVAL;
		RETURNC(PAM_SYSTEM_ERR);
	}

	/* see if the variable is already in the environment */
	if ((i = openpam_findenv(pamh, namevalue, p - namevalue)) >= 0) {
		if ((p = strdup(namevalue)) == NULL)
			RETURNC(PAM_BUF_ERR);
		FREE(pamh->env[i]);
		pamh->env[i] = p;
		RETURNC(PAM_SUCCESS);
	}

	/* grow the environment list if necessary */
	if (pamh->env_count == pamh->env_size) {
		env_size = pamh->env_size * 2 + 1;
		env = realloc(pamh->env, sizeof(char *) * env_size);
		if (env == NULL)
			RETURNC(PAM_BUF_ERR);
		pamh->env = env;
		pamh->env_size = env_size;
	}

	/* add the variable at the end */
	if ((pamh->env[pamh->env_count] = strdup(namevalue)) == NULL)
		RETURNC(PAM_BUF_ERR);
	++pamh->env_count;
	RETURNC(PAM_SUCCESS);
}

/*
 * Error codes:
 *
 *	PAM_SYSTEM_ERR
 *	PAM_BUF_ERR
 */

/**
 * The =pam_putenv function sets an environment variable.
 * Its semantics are similar to those of =putenv, but it modifies the PAM
 * context's environment list instead of the application's.
 *
 * >pam_getenv
 * >pam_getenvlist
 * >pam_setenv
 */
