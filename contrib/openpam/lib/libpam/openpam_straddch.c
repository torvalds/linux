/*-
 * Copyright (c) 2012 Dag-Erling Smørgrav
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
 * $OpenPAM: openpam_straddch.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <stdlib.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"

#define MIN_STR_SIZE	32

/*
 * OpenPAM extension
 *
 * Add a character to a string, expanding the buffer if needed.
 */

int
openpam_straddch(char **str, size_t *size, size_t *len, int ch)
{
	size_t tmpsize;
	char *tmpstr;

	if (*str == NULL) {
		/* initial allocation */
		tmpsize = MIN_STR_SIZE;
		if ((tmpstr = malloc(tmpsize)) == NULL) {
			openpam_log(PAM_LOG_ERROR, "malloc(): %m");
			errno = ENOMEM;
			return (-1);
		}
		*str = tmpstr;
		*size = tmpsize;
		*len = 0;
	} else if (ch != 0 && *len + 1 >= *size) {
		/* additional space required */
		tmpsize = *size * 2;
		if ((tmpstr = realloc(*str, tmpsize)) == NULL) {
			openpam_log(PAM_LOG_ERROR, "realloc(): %m");
			errno = ENOMEM;
			return (-1);
		}
		*size = tmpsize;
		*str = tmpstr;
	}
	if (ch != 0) {
		(*str)[*len] = ch;
		++*len;
	}
	(*str)[*len] = '\0';
	return (0);
}

/**
 * The =openpam_straddch function appends a character to a dynamically
 * allocated NUL-terminated buffer, reallocating the buffer as needed.
 *
 * The =str argument points to a variable containing either a pointer to
 * an existing buffer or =NULL.
 * If the value of the variable pointed to by =str is =NULL, a new buffer
 * is allocated.
 *
 * The =size and =len argument point to variables used to hold the size
 * of the buffer and the length of the string it contains, respectively.
 *
 * The final argument, =ch, is the character that should be appended to
 * the string.  If =ch is 0, nothing is appended, but a new buffer is
 * still allocated if =str is NULL.  This can be used to "bootstrap" the
 * string.
 *
 * If a new buffer is allocated or an existing buffer is reallocated to
 * make room for the additional character, =str and =size are updated
 * accordingly.
 *
 * The =openpam_straddch function ensures that the buffer is always
 * NUL-terminated.
 *
 * If the =openpam_straddch function is successful, it increments the
 * integer variable pointed to by =len (unless =ch was 0) and returns 0.
 * Otherwise, it leaves the variables pointed to by =str, =size and =len
 * unmodified, sets :errno to =ENOMEM and returns -1.
 *
 * AUTHOR DES
 */
