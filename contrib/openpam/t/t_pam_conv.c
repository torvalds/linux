/*-
 * Copyright (c) 2015-2017 Dag-Erling Smørgrav
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
 * $OpenPAM: t_pam_conv.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cryb/test.h>

#include <security/pam_appl.h>
#include <security/openpam.h>

#include "openpam_impl.h"
#include "openpam_asprintf.h"

#include "t_pam_conv.h"

/*
 * Conversation function
 *
 * The appdata argument points to a struct t_pam_conv_script which
 * contains both the expected messages and the desired responses.  If
 * script.responses is NULL, t_pam_conv() will return PAM_CONV_ERR.  If an
 * error occurs (incorrect number of messages, messages don't match script
 * etc.), script.comment will be set to point to a malloc()ed string
 * describing the error.  Otherwise, t_pam_conv() will return to its
 * caller a malloc()ed copy of script.responses.
 */
int
t_pam_conv(int nm, const struct pam_message **msgs,
    struct pam_response **respsp, void *ad)
{
	struct t_pam_conv_script *s = ad;
	struct pam_response *resps;
	int i;

	/* check message count */
	if (nm != s->nmsg) {
		asprintf(&s->comment, "expected %d messages, got %d",
		    s->nmsg, nm);
		return (PAM_CONV_ERR);
	}
	if (nm <= 0 || nm > PAM_MAX_NUM_MSG) {
		/* since the previous test passed, this is intentional! */
		s->comment = NULL;
		return (PAM_CONV_ERR);
	}

	/* check each message and provide the sed answer */
	if ((resps = calloc(nm, sizeof *resps)) == NULL)
		goto enomem;
	for (i = 0; i < nm; ++i) {
		if (msgs[i]->msg_style != s->msgs[i].msg_style) {
			asprintf(&s->comment,
			    "message %d expected style %d got %d", i,
			    s->msgs[i].msg_style, msgs[i]->msg_style);
			goto fail;
		}
		if (strcmp(msgs[i]->msg, s->msgs[i].msg) != 0) {
			asprintf(&s->comment,
			    "message %d expected \"%s\" got \"%s\"", i,
			    s->msgs[i].msg, msgs[i]->msg);
			goto fail;
		}
		switch (msgs[i]->msg_style) {
		case PAM_PROMPT_ECHO_OFF:
			t_printv("[PAM_PROMPT_ECHO_OFF] %s\n", msgs[i]->msg);
			break;
		case PAM_PROMPT_ECHO_ON:
			t_printv("[PAM_PROMPT_ECHO_ON] %s\n", msgs[i]->msg);
			break;
		case PAM_ERROR_MSG:
			t_printv("[PAM_ERROR_MSG] %s\n", msgs[i]->msg);
			break;
		case PAM_TEXT_INFO:
			t_printv("[PAM_TEXT_INFO] %s\n", msgs[i]->msg);
			break;
		default:
			asprintf(&s->comment, "invalid message style %d",
			    msgs[i]->msg_style);
			goto fail;
		}
		/* copy the response, if there is one */
		if (s->resps[i].resp != NULL &&
		    (resps[i].resp = strdup(s->resps[i].resp)) == NULL)
			goto enomem;
		resps[i].resp_retcode = s->resps[i].resp_retcode;
	}
	s->comment = NULL;
	*respsp = resps;
	return (PAM_SUCCESS);
enomem:
	asprintf(&s->comment, "%s", strerror(ENOMEM));
fail:
	for (i = 0; i < nm; ++i)
		free(resps[i].resp);
	free(resps);
	return (PAM_CONV_ERR);
}
