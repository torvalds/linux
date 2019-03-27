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
 * $OpenPAM: pam_getenvlist.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"

/*
 * XSSO 4.2.1
 * XSSO 6 page 45
 *
 * Returns a list of all the PAM environment variables
 */

char **
pam_getenvlist(pam_handle_t *pamh)
{
	char **envlist;
	int i;

	ENTER();
	envlist = malloc(sizeof(char *) * (pamh->env_count + 1));
	if (envlist == NULL) {
		openpam_log(PAM_LOG_ERROR, "%s",
		    pam_err_text[PAM_BUF_ERR]);
		RETURNP(NULL);
	}
	for (i = 0; i < pamh->env_count; ++i) {
		if ((envlist[i] = strdup(pamh->env[i])) == NULL) {
			while (i) {
				--i;
				FREE(envlist[i]);
			}
			FREE(envlist);
			openpam_log(PAM_LOG_ERROR, "%s",
			    pam_err_text[PAM_BUF_ERR]);
			RETURNP(NULL);
		}
	}
	envlist[i] = NULL;
	RETURNP(envlist);
}

/**
 * The =pam_getenvlist function returns a copy of the given PAM context's
 * environment list as a pointer to an array of strings.
 * The last element in the array is =NULL.
 * The pointer is suitable for assignment to {Va environ}.
 *
 * The array and the strings it lists are allocated using =malloc, and
 * should be released using =free after use:
 *
 *     char **envlist, **env;
 *
 *     envlist = environ;
 *     environ = pam_getenvlist(pamh);
 *     \/\* do something nifty \*\/
 *     for (env = environ; *env != NULL; env++)
 *         free(*env);
 *     free(environ);
 *     environ = envlist;
 *
 * >environ 7
 * >pam_getenv
 * >pam_putenv
 * >pam_setenv
 */
