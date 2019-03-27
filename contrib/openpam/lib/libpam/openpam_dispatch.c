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
 * $OpenPAM: openpam_dispatch.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/param.h>

#include <stdint.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"

#if !defined(OPENPAM_RELAX_CHECKS)
static void openpam_check_error_code(int, int);
#else
#define openpam_check_error_code(a, b)
#endif /* !defined(OPENPAM_RELAX_CHECKS) */

/*
 * OpenPAM internal
 *
 * Execute a module chain
 */

int
openpam_dispatch(pam_handle_t *pamh,
	int primitive,
	int flags)
{
	pam_chain_t *chain;
	int err, fail, nsuccess, r;
	int debug;

	ENTER();

	/* prevent recursion */
	if (pamh->current != NULL) {
		openpam_log(PAM_LOG_ERROR,
		    "%s() called while %s::%s() is in progress",
		    pam_func_name[primitive],
		    pamh->current->module->path,
		    pam_sm_func_name[pamh->primitive]);
		RETURNC(PAM_ABORT);
	}

	/* pick a chain */
	switch (primitive) {
	case PAM_SM_AUTHENTICATE:
	case PAM_SM_SETCRED:
		chain = pamh->chains[PAM_AUTH];
		break;
	case PAM_SM_ACCT_MGMT:
		chain = pamh->chains[PAM_ACCOUNT];
		break;
	case PAM_SM_OPEN_SESSION:
	case PAM_SM_CLOSE_SESSION:
		chain = pamh->chains[PAM_SESSION];
		break;
	case PAM_SM_CHAUTHTOK:
		chain = pamh->chains[PAM_PASSWORD];
		break;
	default:
		RETURNC(PAM_SYSTEM_ERR);
	}

	/* execute */
	err = PAM_SUCCESS;
	fail = nsuccess = 0;
	for (; chain != NULL; chain = chain->next) {
		if (chain->module->func[primitive] == NULL) {
			openpam_log(PAM_LOG_ERROR, "%s: no %s()",
			    chain->module->path, pam_sm_func_name[primitive]);
			r = PAM_SYMBOL_ERR;
		} else {
			pamh->primitive = primitive;
			pamh->current = chain;
			debug = (openpam_get_option(pamh, "debug") != NULL);
			if (debug)
				++openpam_debug;
			openpam_log(PAM_LOG_LIBDEBUG, "calling %s() in %s",
			    pam_sm_func_name[primitive], chain->module->path);
			r = (chain->module->func[primitive])(pamh, flags,
			    chain->optc, (const char **)(intptr_t)chain->optv);
			pamh->current = NULL;
			openpam_log(PAM_LOG_LIBDEBUG, "%s: %s(): %s",
			    chain->module->path, pam_sm_func_name[primitive],
			    pam_strerror(pamh, r));
			if (debug)
				--openpam_debug;
		}

		if (r == PAM_IGNORE)
			continue;
		if (r == PAM_SUCCESS) {
			++nsuccess;
			/*
			 * For pam_setcred() and pam_chauthtok() with the
			 * PAM_PRELIM_CHECK flag, treat "sufficient" as
			 * "optional".
			 */
			if ((chain->flag == PAM_SUFFICIENT ||
			    chain->flag == PAM_BINDING) && !fail &&
			    primitive != PAM_SM_SETCRED &&
			    !(primitive == PAM_SM_CHAUTHTOK &&
				(flags & PAM_PRELIM_CHECK)))
				break;
			continue;
		}

		openpam_check_error_code(primitive, r);

		/*
		 * Record the return code from the first module to
		 * fail.  If a required module fails, record the
		 * return code from the first required module to fail.
		 */
		if (err == PAM_SUCCESS)
			err = r;
		if ((chain->flag == PAM_REQUIRED ||
		    chain->flag == PAM_BINDING) && !fail) {
			openpam_log(PAM_LOG_LIBDEBUG, "required module failed");
			fail = 1;
			err = r;
		}

		/*
		 * If a requisite module fails, terminate the chain
		 * immediately.
		 */
		if (chain->flag == PAM_REQUISITE) {
			openpam_log(PAM_LOG_LIBDEBUG, "requisite module failed");
			fail = 1;
			break;
		}
	}

	if (!fail && err != PAM_NEW_AUTHTOK_REQD)
		err = PAM_SUCCESS;

	/*
	 * Require the chain to be non-empty, and at least one module
	 * in the chain to be successful, so that we don't fail open.
	 */
	if (err == PAM_SUCCESS && nsuccess < 1) {
		openpam_log(PAM_LOG_ERROR,
		    "all modules were unsuccessful for %s()",
		    pam_sm_func_name[primitive]);
		err = PAM_SYSTEM_ERR;
	}

	RETURNC(err);
}

#if !defined(OPENPAM_RELAX_CHECKS)
static void
openpam_check_error_code(int primitive, int r)
{
	/* common error codes */
	if (r == PAM_SUCCESS ||
	    r == PAM_SYSTEM_ERR ||
	    r == PAM_SERVICE_ERR ||
	    r == PAM_BUF_ERR ||
	    r == PAM_CONV_ERR ||
	    r == PAM_PERM_DENIED ||
	    r == PAM_ABORT)
		return;

	/* specific error codes */
	switch (primitive) {
	case PAM_SM_AUTHENTICATE:
		if (r == PAM_AUTH_ERR ||
		    r == PAM_CRED_INSUFFICIENT ||
		    r == PAM_AUTHINFO_UNAVAIL ||
		    r == PAM_USER_UNKNOWN ||
		    r == PAM_MAXTRIES)
			return;
		break;
	case PAM_SM_SETCRED:
		if (r == PAM_CRED_UNAVAIL ||
		    r == PAM_CRED_EXPIRED ||
		    r == PAM_USER_UNKNOWN ||
		    r == PAM_CRED_ERR)
			return;
		break;
	case PAM_SM_ACCT_MGMT:
		if (r == PAM_USER_UNKNOWN ||
		    r == PAM_AUTH_ERR ||
		    r == PAM_NEW_AUTHTOK_REQD ||
		    r == PAM_ACCT_EXPIRED)
			return;
		break;
	case PAM_SM_OPEN_SESSION:
	case PAM_SM_CLOSE_SESSION:
		if (r == PAM_SESSION_ERR)
			return;
		break;
	case PAM_SM_CHAUTHTOK:
		if (r == PAM_PERM_DENIED ||
		    r == PAM_AUTHTOK_ERR ||
		    r == PAM_AUTHTOK_RECOVERY_ERR ||
		    r == PAM_AUTHTOK_LOCK_BUSY ||
		    r == PAM_AUTHTOK_DISABLE_AGING ||
		    r == PAM_TRY_AGAIN)
			return;
		break;
	}

	openpam_log(PAM_LOG_ERROR, "%s(): unexpected return value %d",
	    pam_sm_func_name[primitive], r);
}
#endif /* !defined(OPENPAM_RELAX_CHECKS) */

/*
 * NODOC
 *
 * Error codes:
 */
