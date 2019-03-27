/*-
 * Copyright (c) 2001-2003 Networks Associates Technology, Inc.
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
 * $OpenPAM: openpam_constants.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <security/pam_appl.h>

#include "openpam_impl.h"

const char *pam_err_name[PAM_NUM_ERRORS] = {
	[PAM_SUCCESS]			 = "PAM_SUCCESS",
	[PAM_OPEN_ERR]			 = "PAM_OPEN_ERR",
	[PAM_SYMBOL_ERR]		 = "PAM_SYMBOL_ERR",
	[PAM_SERVICE_ERR]		 = "PAM_SERVICE_ERR",
	[PAM_SYSTEM_ERR]		 = "PAM_SYSTEM_ERR",
	[PAM_BUF_ERR]			 = "PAM_BUF_ERR",
	[PAM_CONV_ERR]			 = "PAM_CONV_ERR",
	[PAM_PERM_DENIED]		 = "PAM_PERM_DENIED",
	[PAM_MAXTRIES]			 = "PAM_MAXTRIES",
	[PAM_AUTH_ERR]			 = "PAM_AUTH_ERR",
	[PAM_NEW_AUTHTOK_REQD]		 = "PAM_NEW_AUTHTOK_REQD",
	[PAM_CRED_INSUFFICIENT]		 = "PAM_CRED_INSUFFICIENT",
	[PAM_AUTHINFO_UNAVAIL]		 = "PAM_AUTHINFO_UNAVAIL",
	[PAM_USER_UNKNOWN]		 = "PAM_USER_UNKNOWN",
	[PAM_CRED_UNAVAIL]		 = "PAM_CRED_UNAVAIL",
	[PAM_CRED_EXPIRED]		 = "PAM_CRED_EXPIRED",
	[PAM_CRED_ERR]			 = "PAM_CRED_ERR",
	[PAM_ACCT_EXPIRED]		 = "PAM_ACCT_EXPIRED",
	[PAM_AUTHTOK_EXPIRED]		 = "PAM_AUTHTOK_EXPIRED",
	[PAM_SESSION_ERR]		 = "PAM_SESSION_ERR",
	[PAM_AUTHTOK_ERR]		 = "PAM_AUTHTOK_ERR",
	[PAM_AUTHTOK_RECOVERY_ERR]	 = "PAM_AUTHTOK_RECOVERY_ERR",
	[PAM_AUTHTOK_LOCK_BUSY]		 = "PAM_AUTHTOK_LOCK_BUSY",
	[PAM_AUTHTOK_DISABLE_AGING]	 = "PAM_AUTHTOK_DISABLE_AGING",
	[PAM_NO_MODULE_DATA]		 = "PAM_NO_MODULE_DATA",
	[PAM_IGNORE]			 = "PAM_IGNORE",
	[PAM_ABORT]			 = "PAM_ABORT",
	[PAM_TRY_AGAIN]			 = "PAM_TRY_AGAIN",
	[PAM_MODULE_UNKNOWN]		 = "PAM_MODULE_UNKNOWN",
	[PAM_DOMAIN_UNKNOWN]		 = "PAM_DOMAIN_UNKNOWN",
	[PAM_BAD_HANDLE]		 = "PAM_BAD_HANDLE",
	[PAM_BAD_ITEM]			 = "PAM_BAD_ITEM",
	[PAM_BAD_FEATURE]		 = "PAM_BAD_FEATURE",
	[PAM_BAD_CONSTANT]		 = "PAM_BAD_CONSTANT",
};

const char *pam_err_text[PAM_NUM_ERRORS] = {
	[PAM_SUCCESS]			 = "Success",
	[PAM_OPEN_ERR]			 = "Failed to load module",
	[PAM_SYMBOL_ERR]		 = "Invalid symbol",
	[PAM_SERVICE_ERR]		 = "Error in service module",
	[PAM_SYSTEM_ERR]		 = "System error",
	[PAM_BUF_ERR]			 = "Memory buffer error",
	[PAM_CONV_ERR]			 = "Conversation failure",
	[PAM_PERM_DENIED]		 = "Permission denied",
	[PAM_MAXTRIES]			 = "Maximum number of tries exceeded",
	[PAM_AUTH_ERR]			 = "Authentication error",
	[PAM_NEW_AUTHTOK_REQD]		 = "New authentication token required",
	[PAM_CRED_INSUFFICIENT]		 = "Insufficient credentials",
	[PAM_AUTHINFO_UNAVAIL]		 = "Authentication information is unavailable",
	[PAM_USER_UNKNOWN]		 = "Unknown user",
	[PAM_CRED_UNAVAIL]		 = "Failed to retrieve user credentials",
	[PAM_CRED_EXPIRED]		 = "User credentials have expired",
	[PAM_CRED_ERR]			 = "Failed to set user credentials",
	[PAM_ACCT_EXPIRED]		 = "User account has expired",
	[PAM_AUTHTOK_EXPIRED]		 = "Password has expired",
	[PAM_SESSION_ERR]		 = "Session failure",
	[PAM_AUTHTOK_ERR]		 = "Authentication token failure",
	[PAM_AUTHTOK_RECOVERY_ERR]	 = "Failed to recover old authentication token",
	[PAM_AUTHTOK_LOCK_BUSY]		 = "Authentication token lock busy",
	[PAM_AUTHTOK_DISABLE_AGING]	 = "Authentication token aging disabled",
	[PAM_NO_MODULE_DATA]		 = "Module data not found",
	[PAM_IGNORE]			 = "Ignore this module",
	[PAM_ABORT]			 = "General failure",
	[PAM_TRY_AGAIN]			 = "Try again",
	[PAM_MODULE_UNKNOWN]		 = "Unknown module type",
	[PAM_DOMAIN_UNKNOWN]		 = "Unknown authentication domain",
	[PAM_BAD_HANDLE]		 = "Invalid PAM handle",
	[PAM_BAD_ITEM]			 = "Unrecognized or restricted item",
	[PAM_BAD_FEATURE]		 = "Unrecognized or restricted feature",
	[PAM_BAD_CONSTANT]		 = "Invalid constant",
};

const char *pam_item_name[PAM_NUM_ITEMS] = {
	[PAM_SERVICE]		 = "PAM_SERVICE",
	[PAM_USER]		 = "PAM_USER",
	[PAM_TTY]		 = "PAM_TTY",
	[PAM_RHOST]		 = "PAM_RHOST",
	[PAM_CONV]		 = "PAM_CONV",
	[PAM_AUTHTOK]		 = "PAM_AUTHTOK",
	[PAM_OLDAUTHTOK]	 = "PAM_OLDAUTHTOK",
	[PAM_RUSER]		 = "PAM_RUSER",
	[PAM_USER_PROMPT]	 = "PAM_USER_PROMPT",
	[PAM_REPOSITORY]	 = "PAM_REPOSITORY",
	[PAM_AUTHTOK_PROMPT]	 = "PAM_AUTHTOK_PROMPT",
	[PAM_OLDAUTHTOK_PROMPT]	 = "PAM_OLDAUTHTOK_PROMPT",
	[PAM_HOST]		 = "PAM_HOST",
};

const char *pam_facility_name[PAM_NUM_FACILITIES] = {
	[PAM_ACCOUNT]		 = "account",
	[PAM_AUTH]		 = "auth",
	[PAM_PASSWORD]		 = "password",
	[PAM_SESSION]		 = "session",
};

const char *pam_control_flag_name[PAM_NUM_CONTROL_FLAGS] = {
	[PAM_BINDING]		 = "binding",
	[PAM_OPTIONAL]		 = "optional",
	[PAM_REQUIRED]		 = "required",
	[PAM_REQUISITE]		 = "requisite",
	[PAM_SUFFICIENT]	 = "sufficient",
};

const char *pam_func_name[PAM_NUM_PRIMITIVES] = {
	[PAM_SM_AUTHENTICATE]	 = "pam_authenticate",
	[PAM_SM_SETCRED]	 = "pam_setcred",
	[PAM_SM_ACCT_MGMT]	 = "pam_acct_mgmt",
	[PAM_SM_OPEN_SESSION]	 = "pam_open_session",
	[PAM_SM_CLOSE_SESSION]	 = "pam_close_session",
	[PAM_SM_CHAUTHTOK]	 = "pam_chauthtok"
};

const char *pam_sm_func_name[PAM_NUM_PRIMITIVES] = {
	[PAM_SM_AUTHENTICATE]	 = "pam_sm_authenticate",
	[PAM_SM_SETCRED]	 = "pam_sm_setcred",
	[PAM_SM_ACCT_MGMT]	 = "pam_sm_acct_mgmt",
	[PAM_SM_OPEN_SESSION]	 = "pam_sm_open_session",
	[PAM_SM_CLOSE_SESSION]	 = "pam_sm_close_session",
	[PAM_SM_CHAUTHTOK]	 = "pam_sm_chauthtok"
};

const char *openpam_policy_path[] = {
	"/etc/pam.d/",
	"/etc/pam.conf",
	"/usr/local/etc/pam.d/",
	"/usr/local/etc/pam.conf",
	NULL
};

const char *openpam_module_path[] = {
#ifdef OPENPAM_MODULES_DIRECTORY
	OPENPAM_MODULES_DIRECTORY,
#elif COMPAT_32BIT
	"/usr/lib32",
	"/usr/local/lib32",
#else
	"/usr/lib",
	"/usr/local/lib",
#endif
	NULL
};
