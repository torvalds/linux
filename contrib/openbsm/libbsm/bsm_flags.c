/*-
 * Copyright (c) 2004 Apple Inc.
 * Copyright (c) 2006 Robert N. M. Watson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <config/config.h>

#include <bsm/libbsm.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef HAVE_STRLCPY
#include <compat/strlcpy.h>
#endif

static const char	*flagdelim = ",";

/*
 * Convert the character representation of audit values into the au_mask_t
 * field.
 */
int
getauditflagsbin(char *auditstr, au_mask_t *masks)
{
	char class_ent_name[AU_CLASS_NAME_MAX];
	char class_ent_desc[AU_CLASS_DESC_MAX];
	struct au_class_ent c;
	char *tok;
	char sel, sub;
	char *last;

	bzero(&c, sizeof(c));
	bzero(class_ent_name, sizeof(class_ent_name));
	bzero(class_ent_desc, sizeof(class_ent_desc));
	c.ac_name = class_ent_name;
	c.ac_desc = class_ent_desc;

	masks->am_success = 0;
	masks->am_failure = 0;

	tok = strtok_r(auditstr, flagdelim, &last);
	while (tok != NULL) {
		/* Check for the events that should not be audited. */
		if (tok[0] == '^') {
			sub = 1;
			tok++;
		} else
			sub = 0;

		/* Check for the events to be audited for success. */
		if (tok[0] == '+') {
			sel = AU_PRS_SUCCESS;
			tok++;
		} else if (tok[0] == '-') {
			sel = AU_PRS_FAILURE;
			tok++;
		} else
			sel = AU_PRS_BOTH;

		if ((getauclassnam_r(&c, tok)) != NULL) {
			if (sub)
				SUB_FROM_MASK(masks, c.ac_class, sel);
			else
				ADD_TO_MASK(masks, c.ac_class, sel);
		} else {
			errno = EINVAL;
			return (-1);
		}

		/* Get the next class. */
		tok = strtok_r(NULL, flagdelim, &last);
	}
	return (0);
}

/*
 * Convert the au_mask_t fields into a string value.  If verbose is non-zero
 * the long flag names are used else the short (2-character)flag names are
 * used.
 *
 * XXXRW: If bits are specified that are not matched by any class, they are
 * omitted rather than rejected with EINVAL.
 *
 * XXXRW: This is not thread-safe as it relies on atomicity between
 * setauclass() and sequential calls to getauclassent().  This could be
 * fixed by iterating through the bitmask fields rather than iterating
 * through the classes.
 */
int
getauditflagschar(char *auditstr, au_mask_t *masks, int verbose)
{
	char class_ent_name[AU_CLASS_NAME_MAX];
	char class_ent_desc[AU_CLASS_DESC_MAX];
	struct au_class_ent c;
	char *strptr = auditstr;
	u_char sel;

	bzero(&c, sizeof(c));
	bzero(class_ent_name, sizeof(class_ent_name));
	bzero(class_ent_desc, sizeof(class_ent_desc));
	c.ac_name = class_ent_name;
	c.ac_desc = class_ent_desc;

	/*
	 * Enumerate the class entries, check if each is selected in either
	 * the success or failure masks.
	 */
	setauclass();
	while ((getauclassent_r(&c)) != NULL) {
		sel = 0;

		/* Dont do anything for class = no. */
		if (c.ac_class == 0)
			continue;

		sel |= ((c.ac_class & masks->am_success) == c.ac_class) ?
		    AU_PRS_SUCCESS : 0;
		sel |= ((c.ac_class & masks->am_failure) == c.ac_class) ?
		    AU_PRS_FAILURE : 0;

		/*
		 * No prefix should be attached if both success and failure
		 * are selected.
		 */
		if ((sel & AU_PRS_BOTH) == 0) {
			if ((sel & AU_PRS_SUCCESS) != 0) {
				*strptr = '+';
				strptr = strptr + 1;
			} else if ((sel & AU_PRS_FAILURE) != 0) {
				*strptr = '-';
				strptr = strptr + 1;
			}
		}

		if (sel != 0) {
			if (verbose) {
				strlcpy(strptr, c.ac_desc, AU_CLASS_DESC_MAX);
				strptr += strlen(c.ac_desc);
			} else {
				strlcpy(strptr, c.ac_name, AU_CLASS_NAME_MAX);
				strptr += strlen(c.ac_name);
			}
			*strptr = ','; /* delimiter */
			strptr = strptr + 1;
		}
	}

	/* Overwrite the last delimiter with the string terminator. */
	if (strptr != auditstr)
		*(strptr-1) = '\0';

	return (0);
}
