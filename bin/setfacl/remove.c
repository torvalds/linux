/*-
 * Copyright (c) 2001 Chris D. Faulhaber
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
#include <stdio.h>
#include <string.h>

#include "setfacl.h"

/*
 * remove ACL entries from an ACL
 */
int
remove_acl(acl_t acl, acl_t *prev_acl, const char *filename)
{
	acl_entry_t	entry;
	acl_t		acl_new;
	acl_tag_t	tag;
	int		carried_error, entry_id, acl_brand, prev_acl_brand;

	carried_error = 0;

	acl_get_brand_np(acl, &acl_brand);
	acl_get_brand_np(*prev_acl, &prev_acl_brand);

	if (branding_mismatch(acl_brand, prev_acl_brand)) {
		warnx("%s: branding mismatch; existing ACL is %s, "
		    "entry to be removed is %s", filename,
		    brand_name(prev_acl_brand), brand_name(acl_brand));
		return (-1);
	}

	carried_error = 0;

	acl_new = acl_dup(*prev_acl);
	if (acl_new == NULL)
		err(1, "%s: acl_dup() failed", filename);

	tag = ACL_UNDEFINED_TAG;

	/* find and delete the entry */
	entry_id = ACL_FIRST_ENTRY;
	while (acl_get_entry(acl, entry_id, &entry) == 1) {
		entry_id = ACL_NEXT_ENTRY;
		if (acl_get_tag_type(entry, &tag) == -1)
			err(1, "%s: acl_get_tag_type() failed", filename);
		if (tag == ACL_MASK)
			have_mask = true;
		if (acl_delete_entry(acl_new, entry) == -1) {
			carried_error++;
			warnx("%s: cannot remove non-existent ACL entry",
			    filename);
		}
	}

	acl_free(*prev_acl);
	*prev_acl = acl_new;

	if (carried_error)
		return (-1);

	return (0);
}

int
remove_by_number(uint entry_number, acl_t *prev_acl, const char *filename)
{
	acl_entry_t	entry;
	acl_t		acl_new;
	acl_tag_t	tag;
	int		carried_error, entry_id;
	uint		i;

	carried_error = 0;

	acl_new = acl_dup(*prev_acl);
	if (acl_new == NULL)
		err(1, "%s: acl_dup() failed", filename);

	tag = ACL_UNDEFINED_TAG;

	/*
	 * Find out whether we're removing the mask entry,
	 * to behave the same as the routine above.
	 *
	 * XXX: Is this loop actually needed?
	 */
	entry_id = ACL_FIRST_ENTRY;
	i = 0;
	while (acl_get_entry(acl_new, entry_id, &entry) == 1) {
		entry_id = ACL_NEXT_ENTRY;
		if (i != entry_number)
			continue;
		if (acl_get_tag_type(entry, &tag) == -1)
			err(1, "%s: acl_get_tag_type() failed", filename);
		if (tag == ACL_MASK)
			have_mask = true;
	}

	if (acl_delete_entry_np(acl_new, entry_number) == -1) {
		carried_error++;
		warn("%s: acl_delete_entry_np() failed", filename);
	}

	acl_free(*prev_acl);
	*prev_acl = acl_new;

	if (carried_error)
		return (-1);

	return (0);
}

/*
 * remove default entries
 */
int
remove_default(acl_t *prev_acl, const char *filename)
{

	acl_free(*prev_acl);
	*prev_acl = acl_init(ACL_MAX_ENTRIES);
	if (*prev_acl == NULL)
		err(1, "%s: acl_init() failed", filename);

	return (0);
}

/*
 * remove extended entries
 */
void
remove_ext(acl_t *prev_acl, const char *filename)
{
	acl_t acl_new;

	acl_new = acl_strip_np(*prev_acl, !n_flag);
	if (acl_new == NULL)
		err(1, "%s: acl_strip_np() failed", filename);

	acl_free(*prev_acl);
	*prev_acl = acl_new;
}
