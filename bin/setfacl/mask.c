/*-
 * Copyright (c) 2001-2002 Chris D. Faulhaber
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/acl.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "setfacl.h"

/* set the appropriate mask the given ACL's */
int
set_acl_mask(acl_t *prev_acl, const char *filename)
{
	acl_entry_t entry;
	acl_t acl;
	acl_tag_t tag;
	int entry_id;

	entry = NULL;

	/*
	 * ... if a mask entry is specified, then the permissions of the mask
	 * entry in the resulting ACL shall be set to the permissions in the
	 * specified ACL mask entry.
	 */
	if (have_mask)
		return (0);

	acl = acl_dup(*prev_acl);
	if (acl == NULL)
		err(1, "%s: acl_dup() failed", filename);

	if (!n_flag) {
		/*
		 * If no mask entry is specified and the -n option is not
		 * specified, then the permissions of the resulting ACL mask
		 * entry shall be set to the union of the permissions
		 * associated with all entries which belong to the file group
		 * class in the resulting ACL
		 */
		if (acl_calc_mask(&acl)) {
			warn("%s: acl_calc_mask() failed", filename);
			acl_free(acl);
			return (-1);
		}
	} else {
		/*
		 * If no mask entry is specified and the -n option is
		 * specified, then the permissions of the resulting ACL
		 * mask entry shall remain unchanged ...
		 */

		entry_id = ACL_FIRST_ENTRY;

		while (acl_get_entry(acl, entry_id, &entry) == 1) {
			entry_id = ACL_NEXT_ENTRY;
			if (acl_get_tag_type(entry, &tag) == -1)
				err(1, "%s: acl_get_tag_type() failed",
				    filename);

			if (tag == ACL_MASK) {
				acl_free(acl);
				return (0);
			}
		}

		/*
		 * If no mask entry is specified, the -n option is specified,
		 * and no ACL mask entry exists in the ACL associated with the
		 * file, then write an error message to standard error and
		 * continue with the next file.
		 */
		warnx("%s: warning: no mask entry", filename);
		acl_free(acl);
		return (0);
	}

	acl_free(*prev_acl);
	*prev_acl = acl_dup(acl);
	acl_free(acl);

	return (0);
}
