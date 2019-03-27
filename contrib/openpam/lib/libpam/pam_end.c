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
 * $OpenPAM: pam_end.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"

/*
 * XSSO 4.2.1
 * XSSO 6 page 42
 *
 * Terminate the PAM transaction
 */

int
pam_end(pam_handle_t *pamh,
	int status)
{
	pam_data_t *dp;
	int i;

	ENTER();
	if (pamh == NULL)
		RETURNC(PAM_BAD_HANDLE);

	/* clear module data */
	while ((dp = pamh->module_data) != NULL) {
		if (dp->cleanup)
			(dp->cleanup)(pamh, dp->data, status);
		pamh->module_data = dp->next;
		FREE(dp->name);
		FREE(dp);
	}

	/* clear environment */
	while (pamh->env_count) {
		--pamh->env_count;
		FREE(pamh->env[pamh->env_count]);
	}
	FREE(pamh->env);

	/* clear chains */
	openpam_clear_chains(pamh->chains);

	/* clear items */
	for (i = 0; i < PAM_NUM_ITEMS; ++i)
		pam_set_item(pamh, i, NULL);

	FREE(pamh);

	RETURNC(PAM_SUCCESS);
}

/*
 * Error codes:
 *
 *	PAM_BAD_HANDLE
 */

/**
 * The =pam_end function terminates a PAM transaction and destroys the
 * corresponding PAM context, releasing all resources allocated to it.
 *
 * The =status argument should be set to the error code returned by the
 * last API call before the call to =pam_end.
 */
