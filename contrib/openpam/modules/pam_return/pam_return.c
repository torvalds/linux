/*-
 * Copyright (c) 2015 Dag-Erling Smørgrav
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
 * $OpenPAM: pam_return.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/param.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <security/pam_modules.h>
#include <security/openpam.h>

#include "openpam_impl.h"

static int
pam_return(pam_handle_t *pamh, int flags,
	int argc, const char *argv[])
{
	const char *errname;
	char *e;
	long errcode;

	(void)flags;
	(void)argc;
	(void)argv;
	if ((errname = openpam_get_option(pamh, "error")) == NULL ||
	    errname[0] == '\0') {
		openpam_log(PAM_LOG_ERROR, "missing error parameter");
		return (PAM_SYSTEM_ERR);
	}
	/* is it a number? */
	errcode = strtol(errname, &e, 10);
	if (e != NULL && *e == '\0') {
		/* yep, check range */
		if (errcode >= INT_MIN && errcode <= INT_MAX)
			return (errcode);
	} else {
		/* nope, look it up */
		for (errcode = 0; errcode < PAM_NUM_ERRORS; ++errcode)
			if (strcmp(errname, pam_err_name[errcode]) == 0)
				return (errcode);
	}
	openpam_log(PAM_LOG_ERROR, "invalid error code '%s'", errname);
	return (PAM_SYSTEM_ERR);
}

PAM_EXTERN int
pam_sm_authenticate(pam_handle_t *pamh, int flags,
	int argc, const char *argv[])
{

	return (pam_return(pamh, flags, argc, argv));
}

PAM_EXTERN int
pam_sm_setcred(pam_handle_t *pamh, int flags,
	int argc, const char *argv[])
{

	return (pam_return(pamh, flags, argc, argv));
}

PAM_EXTERN int
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags,
	int argc, const char *argv[])
{

	return (pam_return(pamh, flags, argc, argv));
}

PAM_EXTERN int
pam_sm_open_session(pam_handle_t *pamh, int flags,
	int argc, const char *argv[])
{

	return (pam_return(pamh, flags, argc, argv));
}

PAM_EXTERN int
pam_sm_close_session(pam_handle_t *pamh, int flags,
	int argc, const char *argv[])
{

	return (pam_return(pamh, flags, argc, argv));
}

PAM_EXTERN int
pam_sm_chauthtok(pam_handle_t *pamh, int flags,
	int argc, const char *argv[])
{

	return (pam_return(pamh, flags, argc, argv));
}

PAM_MODULE_ENTRY("pam_return");
