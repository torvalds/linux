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
 * $OpenPAM: pam_get_item.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/param.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"

/*
 * XSSO 4.2.1
 * XSSO 6 page 46
 *
 * Get PAM information
 */

int
pam_get_item(const pam_handle_t *pamh,
	int item_type,
	const void **item)
{

	ENTERI(item_type);
	switch (item_type) {
	case PAM_SERVICE:
	case PAM_USER:
	case PAM_AUTHTOK:
	case PAM_OLDAUTHTOK:
	case PAM_TTY:
	case PAM_RHOST:
	case PAM_RUSER:
	case PAM_CONV:
	case PAM_USER_PROMPT:
	case PAM_REPOSITORY:
	case PAM_AUTHTOK_PROMPT:
	case PAM_OLDAUTHTOK_PROMPT:
	case PAM_HOST:
		*item = pamh->item[item_type];
		RETURNC(PAM_SUCCESS);
	default:
		RETURNC(PAM_BAD_ITEM);
	}
}

/*
 * Error codes:
 *
 *	PAM_BAD_ITEM
 */

/**
 * The =pam_get_item function stores a pointer to the item specified by
 * the =item_type argument in the location pointed to by the =item
 * argument.
 * The item is retrieved from the PAM context specified by the =pamh
 * argument.
 * If =pam_get_item fails, the =item argument is untouched.
 *
 * The following item types are recognized:
 *
 *	=PAM_SERVICE:
 *		The name of the requesting service.
 *	=PAM_USER:
 *		The name of the user the application is trying to
 *		authenticate.
 *	=PAM_TTY:
 *		The name of the current terminal.
 *	=PAM_RHOST:
 *		The name of the applicant's host.
 *	=PAM_CONV:
 *		A =struct pam_conv describing the current conversation
 *		function.
 *	=PAM_AUTHTOK:
 *		The current authentication token.
 *	=PAM_OLDAUTHTOK:
 *		The expired authentication token.
 *	=PAM_RUSER:
 *		The name of the applicant.
 *	=PAM_USER_PROMPT:
 *		The prompt to use when asking the applicant for a user
 *		name to authenticate as.
 *	=PAM_AUTHTOK_PROMPT:
 *		The prompt to use when asking the applicant for an
 *		authentication token.
 *	=PAM_OLDAUTHTOK_PROMPT:
 *		The prompt to use when asking the applicant for an
 *		expired authentication token prior to changing it.
 *	=PAM_HOST:
 *		The name of the host the application runs on.
 *
 * See =pam_start for a description of =struct pam_conv.
 *
 * >pam_set_item
 */
