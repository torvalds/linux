/*-
 * Copyright (c) 2011 Dag-Erling Smørgrav
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
 * $OpenPAM: openpam_subst.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <security/pam_appl.h>

#include "openpam_impl.h"

#define subst_char(ch) do {			\
	int ch_ = (ch);				\
	if (buf && len < *bufsize)		\
		*buf++ = ch_;			\
	++len;					\
} while (0)

#define subst_string(s) do {			\
	const char *s_ = (s);			\
	while (*s_)				\
		subst_char(*s_++);		\
} while (0)

#define subst_item(i) do {			\
	int i_ = (i);				\
	const void *p_;				\
	ret = pam_get_item(pamh, i_, &p_);	\
	if (ret == PAM_SUCCESS && p_ != NULL)	\
		subst_string(p_);		\
} while (0)

/*
 * OpenPAM internal
 *
 * Substitute PAM item values in a string
 */

int
openpam_subst(const pam_handle_t *pamh,
    char *buf, size_t *bufsize, const char *template)
{
	size_t len;
	int ret;

	ENTERS(template);
	if (template == NULL)
		template = "(null)";

	len = 1; /* initialize to 1 for terminating NUL */
	ret = PAM_SUCCESS;
	while (*template && ret == PAM_SUCCESS) {
		if (template[0] == '%') {
			++template;
			switch (*template) {
			case 's':
				subst_item(PAM_SERVICE);
				break;
			case 't':
				subst_item(PAM_TTY);
				break;
			case 'h':
				subst_item(PAM_HOST);
				break;
			case 'u':
				subst_item(PAM_USER);
				break;
			case 'H':
				subst_item(PAM_RHOST);
				break;
			case 'U':
				subst_item(PAM_RUSER);
				break;
			case '\0':
				subst_char('%');
				break;
			default:
				subst_char('%');
				subst_char(*template);
			}
			++template;
		} else {
			subst_char(*template++);
		}
	}
	if (buf)
		*buf = '\0';
	if (ret == PAM_SUCCESS) {
		if (len > *bufsize)
			ret = PAM_TRY_AGAIN;
		*bufsize = len;
	}
	RETURNC(ret);
}

/*
 * Error codes:
 *
 *	=pam_get_item
 *	!PAM_SYMBOL_ERR
 *	PAM_TRY_AGAIN
 */

/**
 * The =openpam_subst function expands a string, substituting PAM item
 * values for all occurrences of specific substitution codes.
 * The =template argument points to the initial string.
 * The result is stored in the buffer pointed to by the =buf argument; the
 * =bufsize argument specifies the size of that buffer.
 * The actual size of the resulting string, including the terminating NUL
 * character, is stored in the location pointed to by the =bufsize
 * argument.
 *
 * If =buf is NULL, or if the buffer is too small to hold the expanded
 * string, =bufsize is updated to reflect the amount of space required to
 * hold the entire string, and =openpam_subst returns =PAM_TRY_AGAIN.
 *
 * If =openpam_subst fails for any other reason, the =bufsize argument is
 * untouched, but part of the buffer may still have been overwritten.
 *
 * Substitution codes are introduced by a percent character and correspond
 * to PAM items:
 *
 *	%H:
 *		Replaced by the current value of the =PAM_RHOST item.
 *	%h:
 *		Replaced by the current value of the =PAM_HOST item.
 *	%s:
 *		Replaced by the current value of the =PAM_SERVICE item.
 *	%t:
 *		Replaced by the current value of the =PAM_TTY item.
 *	%U:
 *		Replaced by the current value of the =PAM_RUSER item.
 *	%u:
 *		Replaced by the current value of the =PAM_USER item.
 *
 * >pam_get_authtok
 * >pam_get_item
 * >pam_get_user
 *
 * AUTHOR DES
 */
