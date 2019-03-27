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
 * Routines used to traverse tdesc trees, invoking user-supplied callbacks
 * as the tree is traversed.
 */

#include <stdio.h>
#include <assert.h>

#include "ctftools.h"
#include "traverse.h"
#include "memory.h"

static int (*tddescenders[])(tdesc_t *, tdtrav_data_t *);
static tdtrav_cb_f tdnops[];

void
tdtrav_init(tdtrav_data_t *tdtd, int *vgenp, tdtrav_cb_f *firstops,
    tdtrav_cb_f *preops, tdtrav_cb_f *postops, void *private)
{
	tdtd->vgen = ++(*vgenp);
	tdtd->firstops = firstops ? firstops : tdnops;
	tdtd->preops = preops ? preops : tdnops;
	tdtd->postops = postops ? postops : tdnops;
	tdtd->private = private;
}

static int
tdtrav_plain(tdesc_t *this, tdtrav_data_t *tdtd)
{
	return (tdtraverse(this->t_tdesc, &this->t_tdesc, tdtd));
}

static int
tdtrav_func(tdesc_t *this, tdtrav_data_t *tdtd)
{
	fndef_t *fn = this->t_fndef;
	int i, rc;

	if ((rc = tdtraverse(fn->fn_ret, &fn->fn_ret, tdtd)) < 0)
		return (rc);

	for (i = 0; i < (int) fn->fn_nargs; i++) {
		if ((rc = tdtraverse(fn->fn_args[i], &fn->fn_args[i],
		    tdtd)) < 0)
			return (rc);
	}

	return (0);
}

static int
tdtrav_array(tdesc_t *this, tdtrav_data_t *tdtd)
{
	ardef_t *ardef = this->t_ardef;
	int rc;

	if ((rc = tdtraverse(ardef->ad_contents, &ardef->ad_contents,
	    tdtd)) < 0)
		return (rc);

	return (tdtraverse(ardef->ad_idxtype, &ardef->ad_idxtype, tdtd));
}

static int
tdtrav_su(tdesc_t *this, tdtrav_data_t *tdtd)
{
	mlist_t *ml;
	int rc = 0;

	for (ml = this->t_members; ml; ml = ml->ml_next) {
		if ((rc = tdtraverse(ml->ml_type, &ml->ml_type, tdtd)) < 0)
			return (rc);
	}

	return (rc);
}

/*ARGSUSED*/
int
tdtrav_assert(tdesc_t *node __unused, tdesc_t **nodep __unused, void *private __unused)
{
	assert(1 == 0);

	return (-1);
}

static tdtrav_cb_f tdnops[] = {
	NULL,
	NULL,			/* intrinsic */
	NULL,			/* pointer */
	NULL,			/* array */
	NULL,			/* function */
	NULL,			/* struct */
	NULL,			/* union */
	NULL,			/* enum */
	NULL,			/* forward */
	NULL,			/* typedef */
	NULL,			/* typedef_unres */
	NULL,			/* volatile */
	NULL,			/* const */
	NULL			/* restrict */
};

static int (*tddescenders[])(tdesc_t *, tdtrav_data_t *) = {
	NULL,
	NULL,			/* intrinsic */
	tdtrav_plain,		/* pointer */
	tdtrav_array,		/* array */
	tdtrav_func,		/* function */
	tdtrav_su,		/* struct */
	tdtrav_su,		/* union */
	NULL,			/* enum */
	NULL,			/* forward */
	tdtrav_plain,		/* typedef */
	NULL,			/* typedef_unres */
	tdtrav_plain,		/* volatile */
	tdtrav_plain,		/* const */
	tdtrav_plain		/* restrict */
};

int
tdtraverse(tdesc_t *this, tdesc_t **thisp, tdtrav_data_t *tdtd)
{
	tdtrav_cb_f travcb;
	int (*descender)(tdesc_t *, tdtrav_data_t *);
	int descend = 1;
	int rc;

	if ((travcb = tdtd->firstops[this->t_type]) != NULL) {
		if ((rc = travcb(this, thisp, tdtd->private)) < 0)
			return (rc);
		else if (rc == 0)
			descend = 0;
	}

	if (this->t_vgen == tdtd->vgen)
		return (1);
	this->t_vgen = tdtd->vgen;

	if (descend && (travcb = tdtd->preops[this->t_type]) != NULL) {
		if ((rc = travcb(this, thisp, tdtd->private)) < 0)
			return (rc);
		else if (rc == 0)
			descend = 0;
	}

	if (descend) {
		if ((descender = tddescenders[this->t_type]) != NULL &&
		    (rc = descender(this, tdtd)) < 0)
			return (rc);

		if ((travcb = tdtd->postops[this->t_type]) != NULL &&
		    (rc = travcb(this, thisp, tdtd->private)) < 0)
			return (rc);
	}

	return (1);
}

int
iitraverse_td(void *arg1, void *arg2)
{
	iidesc_t *ii = arg1;
	tdtrav_data_t *tdtd = arg2;
	int i, rc;

	if ((rc = tdtraverse(ii->ii_dtype, &ii->ii_dtype, tdtd)) < 0)
		return (rc);

	for (i = 0; i < ii->ii_nargs; i++) {
		if ((rc = tdtraverse(ii->ii_args[i], &ii->ii_args[i],
		    tdtd)) < 0)
			return (rc);
	}

	return (1);
}

int
iitraverse(iidesc_t *ii, int *vgenp, tdtrav_cb_f *firstops, tdtrav_cb_f *preops,
    tdtrav_cb_f *postops, void *private)
{
	tdtrav_data_t tdtd;

	tdtrav_init(&tdtd, vgenp, firstops, preops, postops, private);

	return (iitraverse_td(ii, &tdtd));
}

int
iitraverse_hash(hash_t *iihash, int *vgenp, tdtrav_cb_f *firstops,
    tdtrav_cb_f *preops, tdtrav_cb_f *postops, void *private)
{
	tdtrav_data_t tdtd;

	tdtrav_init(&tdtd, vgenp, firstops, preops, postops, private);

	return (hash_iter(iihash, iitraverse_td, &tdtd));
}
