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
 * $OpenPAM: pam_constants.h 938 2017-04-30 21:34:42Z des $
 */

#ifndef SECURITY_PAM_CONSTANTS_H_INCLUDED
#define SECURITY_PAM_CONSTANTS_H_INCLUDED

#include <security/openpam_version.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * XSSO 5.2
 */
enum {
	PAM_SUCCESS			=   0,
	PAM_OPEN_ERR			=   1,
	PAM_SYMBOL_ERR			=   2,
	PAM_SERVICE_ERR			=   3,
	PAM_SYSTEM_ERR			=   4,
	PAM_BUF_ERR			=   5,
	PAM_CONV_ERR			=   6,
	PAM_PERM_DENIED			=   7,
	PAM_MAXTRIES			=   8,
	PAM_AUTH_ERR			=   9,
	PAM_NEW_AUTHTOK_REQD		=  10,
	PAM_CRED_INSUFFICIENT		=  11,
	PAM_AUTHINFO_UNAVAIL		=  12,
	PAM_USER_UNKNOWN		=  13,
	PAM_CRED_UNAVAIL		=  14,
	PAM_CRED_EXPIRED		=  15,
	PAM_CRED_ERR			=  16,
	PAM_ACCT_EXPIRED		=  17,
	PAM_AUTHTOK_EXPIRED		=  18,
	PAM_SESSION_ERR			=  19,
	PAM_AUTHTOK_ERR			=  20,
	PAM_AUTHTOK_RECOVERY_ERR	=  21,
	PAM_AUTHTOK_LOCK_BUSY		=  22,
	PAM_AUTHTOK_DISABLE_AGING	=  23,
	PAM_NO_MODULE_DATA		=  24,
	PAM_IGNORE			=  25,
	PAM_ABORT			=  26,
	PAM_TRY_AGAIN			=  27,
	PAM_MODULE_UNKNOWN		=  28,
	PAM_DOMAIN_UNKNOWN		=  29,
	PAM_BAD_HANDLE			=  30,		/* OpenPAM extension */
	PAM_BAD_ITEM			=  31,		/* OpenPAM extension */
	PAM_BAD_FEATURE			=  32,		/* OpenPAM extension */
	PAM_BAD_CONSTANT		=  33,		/* OpenPAM extension */
	PAM_NUM_ERRORS					/* OpenPAM extension */
};

/*
 * XSSO 5.3
 */
enum {
	PAM_PROMPT_ECHO_OFF		=   1,
	PAM_PROMPT_ECHO_ON		=   2,
	PAM_ERROR_MSG			=   3,
	PAM_TEXT_INFO			=   4,
	PAM_MAX_NUM_MSG			=  32,
	PAM_MAX_MSG_SIZE		= 512,
	PAM_MAX_RESP_SIZE		= 512
};

/*
 * XSSO 5.4
 */
enum {
	/* some compilers promote 0x8000000 to long */
	PAM_SILENT			= (-0x7fffffff - 1),
	PAM_DISALLOW_NULL_AUTHTOK	= 0x1,
	PAM_ESTABLISH_CRED		= 0x1,
	PAM_DELETE_CRED			= 0x2,
	PAM_REINITIALIZE_CRED		= 0x4,
	PAM_REFRESH_CRED		= 0x8,
	PAM_PRELIM_CHECK		= 0x1,
	PAM_UPDATE_AUTHTOK		= 0x2,
	PAM_CHANGE_EXPIRED_AUTHTOK	= 0x4
};

/*
 * XSSO 5.5
 */
enum {
	PAM_SERVICE			=   1,
	PAM_USER			=   2,
	PAM_TTY				=   3,
	PAM_RHOST			=   4,
	PAM_CONV			=   5,
	PAM_AUTHTOK			=   6,
	PAM_OLDAUTHTOK			=   7,
	PAM_RUSER			=   8,
	PAM_USER_PROMPT			=   9,
	PAM_REPOSITORY			=  10,
	PAM_AUTHTOK_PROMPT		=  11,		/* OpenPAM extension */
	PAM_OLDAUTHTOK_PROMPT		=  12,		/* OpenPAM extension */
	PAM_HOST			=  13,		/* OpenPAM extension */
	PAM_NUM_ITEMS					/* OpenPAM extension */
};

#ifdef __cplusplus
}
#endif

#endif /* !SECURITY_PAM_CONSTANTS_H_INCLUDED */
