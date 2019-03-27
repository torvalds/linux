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
 * $OpenPAM: pam_vprompt.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"

/*
 * OpenPAM extension
 *
 * Call the conversation function
 */

int
pam_vprompt(const pam_handle_t *pamh,
	int style,
	char **resp,
	const char *fmt,
	va_list ap)
{
	char msgbuf[PAM_MAX_MSG_SIZE];
	struct pam_message msg;
	const struct pam_message *msgp;
	struct pam_response *rsp;
	const struct pam_conv *conv;
	const void *convp;
	int r;

	ENTER();
	r = pam_get_item(pamh, PAM_CONV, &convp);
	if (r != PAM_SUCCESS)
		RETURNC(r);
	conv = convp;
	if (conv == NULL || conv->conv == NULL) {
		openpam_log(PAM_LOG_ERROR, "no conversation function");
		RETURNC(PAM_SYSTEM_ERR);
	}
	vsnprintf(msgbuf, PAM_MAX_MSG_SIZE, fmt, ap);
	msg.msg_style = style;
	msg.msg = msgbuf;
	msgp = &msg;
	rsp = NULL;
	r = (conv->conv)(1, &msgp, &rsp, conv->appdata_ptr);
	*resp = rsp == NULL ? NULL : rsp->resp;
	FREE(rsp);
	RETURNC(r);
}

/*
 * Error codes:
 *
 *     !PAM_SYMBOL_ERR
 *	PAM_SYSTEM_ERR
 *	PAM_BUF_ERR
 *	PAM_CONV_ERR
 */

/**
 * The =pam_vprompt function constructs a string from the =fmt and =ap
 * arguments using =vsnprintf, and passes it to the given PAM context's
 * conversation function.
 *
 * The =style argument specifies the type of interaction requested, and
 * must be one of the following:
 *
 *	=PAM_PROMPT_ECHO_OFF:
 *		Display the message and obtain the user's response without
 *		displaying it.
 *	=PAM_PROMPT_ECHO_ON:
 *		Display the message and obtain the user's response.
 *	=PAM_ERROR_MSG:
 *		Display the message as an error message, and do not wait
 *		for a response.
 *	=PAM_TEXT_INFO:
 *		Display the message as an informational message, and do
 *		not wait for a response.
 *
 * A pointer to the response, or =NULL if the conversation function did
 * not return one, is stored in the location pointed to by the =resp
 * argument.
 *
 * The message and response should not exceed =PAM_MAX_MSG_SIZE or
 * =PAM_MAX_RESP_SIZE, respectively.
 * If they do, they may be truncated.
 *
 * >pam_error
 * >pam_info
 * >pam_prompt
 * >pam_verror
 * >pam_vinfo
 */
