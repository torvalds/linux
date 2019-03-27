/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <ctf_impl.h>

/*
 * Simple doubly-linked list append routine.  This implementation assumes that
 * each list element contains an embedded ctf_list_t as the first member.
 * An additional ctf_list_t is used to store the head (l_next) and tail
 * (l_prev) pointers.  The current head and tail list elements have their
 * previous and next pointers set to NULL, respectively.
 */
void
ctf_list_append(ctf_list_t *lp, void *new)
{
	ctf_list_t *p = lp->l_prev;	/* p = tail list element */
	ctf_list_t *q = new;		/* q = new list element */

	lp->l_prev = q;
	q->l_prev = p;
	q->l_next = NULL;

	if (p != NULL)
		p->l_next = q;
	else
		lp->l_next = q;
}

/*
 * Prepend the specified existing element to the given ctf_list_t.  The
 * existing pointer should be pointing at a struct with embedded ctf_list_t.
 */
void
ctf_list_prepend(ctf_list_t *lp, void *new)
{
	ctf_list_t *p = new;		/* p = new list element */
	ctf_list_t *q = lp->l_next;	/* q = head list element */

	lp->l_next = p;
	p->l_prev = NULL;
	p->l_next = q;

	if (q != NULL)
		q->l_prev = p;
	else
		lp->l_prev = p;
}

/*
 * Delete the specified existing element from the given ctf_list_t.  The
 * existing pointer should be pointing at a struct with embedded ctf_list_t.
 */
void
ctf_list_delete(ctf_list_t *lp, void *existing)
{
	ctf_list_t *p = existing;

	if (p->l_prev != NULL)
		p->l_prev->l_next = p->l_next;
	else
		lp->l_next = p->l_next;

	if (p->l_next != NULL)
		p->l_next->l_prev = p->l_prev;
	else
		lp->l_prev = p->l_prev;
}

/*
 * Convert an encoded CTF string name into a pointer to a C string by looking
 * up the appropriate string table buffer and then adding the offset.
 */
const char *
ctf_strraw(ctf_file_t *fp, uint_t name)
{
	ctf_strs_t *ctsp = &fp->ctf_str[CTF_NAME_STID(name)];

	if (ctsp->cts_strs != NULL && CTF_NAME_OFFSET(name) < ctsp->cts_len)
		return (ctsp->cts_strs + CTF_NAME_OFFSET(name));

	/* string table not loaded or corrupt offset */
	return (NULL);
}

const char *
ctf_strptr(ctf_file_t *fp, uint_t name)
{
	const char *s = ctf_strraw(fp, name);
	return (s != NULL ? s : "(?)");
}

/*
 * Same strdup(3C), but use ctf_alloc() to do the memory allocation.
 */
char *
ctf_strdup(const char *s1)
{
	char *s2 = ctf_alloc(strlen(s1) + 1);

	if (s2 != NULL)
		(void) strcpy(s2, s1);

	return (s2);
}

/*
 * Store the specified error code into errp if it is non-NULL, and then
 * return NULL for the benefit of the caller.
 */
ctf_file_t *
ctf_set_open_errno(int *errp, int error)
{
	if (errp != NULL)
		*errp = error;
	return (NULL);
}

/*
 * Store the specified error code into the CTF container, and then return
 * CTF_ERR for the benefit of the caller.
 */
long
ctf_set_errno(ctf_file_t *fp, int err)
{
	fp->ctf_errno = err;
	return (CTF_ERR);
}
