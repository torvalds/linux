/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Routines for manipulating iidesc_t structures
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "ctftools.h"
#include "memory.h"
#include "list.h"
#include "hash.h"

typedef struct iidesc_find {
	iidesc_t *iif_tgt;
	iidesc_t *iif_ret;
} iidesc_find_t;

iidesc_t *
iidesc_new(char *name)
{
	iidesc_t *ii;

	ii = xcalloc(sizeof (iidesc_t));
	if (name)
		ii->ii_name = xstrdup(name);

	return (ii);
}

int
iidesc_hash(int nbuckets, void *arg)
{
	iidesc_t *ii = arg;
	int h = 0;

	if (ii->ii_name)
		return (hash_name(nbuckets, ii->ii_name));

	return (h);
}

static int
iidesc_cmp(void *arg1, void *arg2)
{
	iidesc_t *src = arg1;
	iidesc_find_t *find = arg2;
	iidesc_t *tgt = find->iif_tgt;

	if (src->ii_type != tgt->ii_type ||
	    !streq(src->ii_name, tgt->ii_name))
		return (0);

	find->iif_ret = src;

	return (-1);
}

void
iidesc_add(hash_t *hash, iidesc_t *new)
{
	iidesc_find_t find;

	find.iif_tgt = new;
	find.iif_ret = NULL;

	(void) hash_match(hash, new, iidesc_cmp, &find);

	if (find.iif_ret != NULL) {
		iidesc_t *old = find.iif_ret;
		iidesc_t tmp;
		/* replacing existing one */
		bcopy(old, &tmp, sizeof (tmp));
		bcopy(new, old, sizeof (*old));
		bcopy(&tmp, new, sizeof (*new));

		iidesc_free(new, NULL);
		return;
	}

	hash_add(hash, new);
}

void
iter_iidescs_by_name(tdata_t *td, char const *name,
    int (*func)(void *, void *), void *data)
{
	iidesc_t tmpdesc;
	bzero(&tmpdesc, sizeof(tmpdesc));
	tmpdesc.ii_name = xstrdup(name);
	(void) hash_match(td->td_iihash, &tmpdesc, func, data);
	free(tmpdesc.ii_name);
}

iidesc_t *
iidesc_dup(iidesc_t *src)
{
	iidesc_t *tgt;

	tgt = xmalloc(sizeof (iidesc_t));
	bcopy(src, tgt, sizeof (iidesc_t));

	tgt->ii_name = src->ii_name ? xstrdup(src->ii_name) : NULL;
	tgt->ii_owner = src->ii_owner ? xstrdup(src->ii_owner) : NULL;

	if (tgt->ii_nargs) {
		tgt->ii_args = xmalloc(sizeof (tdesc_t *) * tgt->ii_nargs);
		bcopy(src->ii_args, tgt->ii_args,
		    sizeof (tdesc_t *) * tgt->ii_nargs);
	}

	return (tgt);
}

iidesc_t *
iidesc_dup_rename(iidesc_t *src, char const *name, char const *owner)
{
	iidesc_t *tgt = iidesc_dup(src);
	free(tgt->ii_name);
	free(tgt->ii_owner);

	tgt->ii_name = name ? xstrdup(name) : NULL;
	tgt->ii_owner = owner ? xstrdup(owner) : NULL;

	return (tgt);
}

/*ARGSUSED*/
void
iidesc_free(void *arg, void *private __unused)
{
	iidesc_t *idp = arg;
	if (idp->ii_name)
		free(idp->ii_name);
	if (idp->ii_nargs)
		free(idp->ii_args);
	if (idp->ii_owner)
		free(idp->ii_owner);
	free(idp);
}

int
iidesc_dump(iidesc_t *ii)
{
	printf("type: %d  name %s\n", ii->ii_type,
	    (ii->ii_name ? ii->ii_name : "(anon)"));

	return (0);
}

int
iidesc_count_type(void *data, void *private)
{
	iidesc_t *ii = data;
	iitype_t match = (iitype_t)private;

	return (ii->ii_type == match);
}

void
iidesc_stats(hash_t *ii)
{
	printf("GFun: %5d SFun: %5d GVar: %5d SVar: %5d T %5d SOU: %5d\n",
	    hash_iter(ii, iidesc_count_type, (void *)II_GFUN),
	    hash_iter(ii, iidesc_count_type, (void *)II_SFUN),
	    hash_iter(ii, iidesc_count_type, (void *)II_GVAR),
	    hash_iter(ii, iidesc_count_type, (void *)II_SVAR),
	    hash_iter(ii, iidesc_count_type, (void *)II_TYPE),
	    hash_iter(ii, iidesc_count_type, (void *)II_SOU));
}
