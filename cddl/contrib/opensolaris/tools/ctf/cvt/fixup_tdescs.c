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
 * Workarounds for stabs generation bugs in the compiler and general needed
 * fixups.
 */

#include <stdio.h>
#include <strings.h>

#include "ctf_headers.h"
#include "ctftools.h"
#include "hash.h"
#include "memory.h"

/*
 * Due to 4432619, the 6.1 compiler will sometimes incorrectly generate pointer
 * stabs.  Given a struct foo, and a corresponding typedef struct foo foo_t.
 * In some cases, when faced with a pointer to a foo_t, the compiler will
 * sometimes generate a stab that describes a pointer to a struct foo.
 * Regardless of correctness, this breaks merges, as it occurs inconsistently
 * by file.  The following two routines know how to recognize and repair foo_t *
 * and foo_t ** bugs in a specific set of cases.  There is no general way to
 * solve this problem without a fix to the compiler.  In general, cases should
 * only be added to these routines to fix merging problems in genunix.
 */
static void
fix_ptrptr_to_struct(tdata_t *td)
{
	const char *strs[2] = { "as", "fdbuffer" };
	const char *mems[2] = { "a_objectdir", "fd_shadow" };
	const char *acts[2] = { "vnode", "page" };
	const char *tgts[2] = { "vnode_t", "page_t" };
	tdesc_t *str;
	tdesc_t *act, *tgt;
	tdesc_t *p1, *p2;
	mlist_t *ml;
	int i;

	for (i = 0; i < (int) (sizeof (strs) / sizeof (strs[0])); i++) {
		if (!(str = lookupname(strs[i])) || str->t_type != STRUCT)
			continue;

		for (ml = str->t_members; ml; ml = ml->ml_next) {
			if (streq(ml->ml_name, mems[i]))
				break;
		}
		if (!ml)
			continue;

		if (ml->ml_type->t_type != POINTER || ml->ml_type->t_name ||
		    ml->ml_type->t_tdesc->t_type != POINTER ||
		    ml->ml_type->t_tdesc->t_name)
			continue;

		act = ml->ml_type->t_tdesc->t_tdesc;
		if (act->t_type != STRUCT || !streq(act->t_name, acts[i]))
			continue;

		if (!(tgt = lookupname(tgts[i])) || tgt->t_type != TYPEDEF)
			continue;

		/* We have an instance of the bug */
		p2 = xcalloc(sizeof (*p2));
		p2->t_type = POINTER;
		p2->t_id = td->td_nextid++;
		p2->t_tdesc = tgt;

		p1 = xcalloc(sizeof (*p1));
		p1->t_type = POINTER;
		p1->t_id = td->td_nextid++;
		p1->t_tdesc = p2;

		ml->ml_type = p1;

		debug(3, "Fixed %s->%s => ptrptr struct %s bug\n",
		    strs[i], mems[i], acts[i]);
	}
}

static void
fix_ptr_to_struct(tdata_t *td)
{
	const char *strs[2] = { "vmem", "id_space" };
	const char *mems[2] = { NULL, "is_vmem" };
	tdesc_t *ptr = NULL;
	tdesc_t *str, *vmt;
	mlist_t *ml;
	int i;

	if ((vmt = lookupname("vmem_t")) == NULL || vmt->t_type != TYPEDEF)
		return;

	for (i = 0; i < (int) (sizeof (strs) / sizeof (strs[0])); i++) {
		if (!(str = lookupname(strs[i])) || str->t_type != STRUCT)
			continue;

		for (ml = str->t_members; ml; ml = ml->ml_next) {
			if (mems[i] && !streq(ml->ml_name, mems[i]))
				continue;

			if (ml->ml_type->t_type != POINTER ||
			    ml->ml_type->t_name ||
			    (ml->ml_type->t_tdesc->t_type != STRUCT &&
			    ml->ml_type->t_tdesc->t_type != FORWARD) ||
			    !streq(ml->ml_type->t_tdesc->t_name, "vmem"))
				continue;

			debug(3, "Fixed %s->%s => ptr struct vmem bug\n",
			    strs[i], ml->ml_name);

			if (!ptr) {
				ptr = xcalloc(sizeof (*ptr));
				ptr->t_type = POINTER;
				ptr->t_id = td->td_nextid++;
				ptr->t_tdesc = vmt;
			}

			ml->ml_type = ptr;
		}
	}
}

/*
 * Fix stabs generation bugs.  These routines must be run before the
 * post-conversion merge
 */
void
cvt_fixstabs(tdata_t *td)
{
	fix_ptrptr_to_struct(td);
	fix_ptr_to_struct(td);
}

struct match {
	tdesc_t *m_ret;
	const char *m_name;
};

static int
matching_iidesc(void *arg1, void *arg2)
{
	iidesc_t *iidesc = arg1;
	struct match *match = arg2;
	if (!streq(iidesc->ii_name, match->m_name))
		return (0);

	if (iidesc->ii_type != II_TYPE && iidesc->ii_type != II_SOU)
		return (0);

	match->m_ret = iidesc->ii_dtype;
	return (-1);
}

static tdesc_t *
lookup_tdesc(tdata_t *td, char const *name)
{
	struct match match = { NULL, name };
	iter_iidescs_by_name(td, name, matching_iidesc, &match);
	return (match.m_ret);
}

/*
 * The cpu structure grows, with the addition of a machcpu member, if
 * _MACHDEP is defined.  This means that, for example, the cpu structure
 * in unix is different from the cpu structure in genunix.  As one might
 * expect, this causes merges to fail.  Since everyone indirectly contains
 * a pointer to a CPU structure, the failed merges can cause massive amounts
 * of duplication.  In the case of unix uniquifying against genunix, upwards
 * of 50% of the structures were unmerged due to this problem.  We fix this
 * by adding a cpu_m member.  If machcpu hasn't been defined in our module,
 * we make a forward node for it.
 */
static void
fix_small_cpu_struct(tdata_t *td, size_t ptrsize)
{
	tdesc_t *cput, *cpu;
	tdesc_t *machcpu;
	mlist_t *ml, *lml;
	mlist_t *cpum;
	int foundcpucyc = 0;

	/*
	 * We're going to take the circuitous route finding the cpu structure,
	 * because we want to make sure that we find the right one.  It would
	 * be nice if we could verify the header name too.  DWARF might not
	 * have the cpu_t, so we let this pass.
	 */
	if ((cput = lookup_tdesc(td, "cpu_t")) != NULL) {
		if (cput->t_type != TYPEDEF)
			return;
		cpu = cput->t_tdesc;
	} else {
		cpu = lookup_tdesc(td, "cpu");
	}

	if (cpu == NULL)
		return;

	if (!streq(cpu->t_name, "cpu") || cpu->t_type != STRUCT)
		return;

	for (ml = cpu->t_members, lml = NULL; ml;
	    lml = ml, ml = ml->ml_next) {
		if (strcmp(ml->ml_name, "cpu_cyclic") == 0)
			foundcpucyc = 1;
	}

	if (foundcpucyc == 0 || lml == NULL ||
	    strcmp(lml->ml_name, "cpu_m") == 0)
		return;

	/*
	 * We need to derive the right offset for the fake cpu_m member.  To do
	 * that, we require a special unused member to be the last member
	 * before the 'cpu_m', that we encode knowledge of here.  ABI alignment
	 * on all platforms is such that we only need to add a pointer-size
	 * number of bits to get the right offset for cpu_m.  This would most
	 * likely break if gcc's -malign-double were ever used, but that option
	 * breaks the ABI anyway.
	 */
	if (!streq(lml->ml_name, "cpu_m_pad") &&
	    getenv("CTFCONVERT_PERMISSIVE") == NULL) {
		terminate("last cpu_t member before cpu_m is %s; "
		    "it must be cpu_m_pad.\n", lml->ml_name);
	}

	if ((machcpu = lookup_tdesc(td, "machcpu")) == NULL) {
		machcpu = xcalloc(sizeof (*machcpu));
		machcpu->t_name = xstrdup("machcpu");
		machcpu->t_id = td->td_nextid++;
		machcpu->t_type = FORWARD;
	} else if (machcpu->t_type != STRUCT) {
		return;
	}

	debug(3, "Adding cpu_m machcpu %s to cpu struct\n",
	    (machcpu->t_type == FORWARD ? "forward" : "struct"));

	cpum = xmalloc(sizeof (*cpum));
	cpum->ml_offset = lml->ml_offset + (ptrsize * NBBY);
	cpum->ml_size = 0;
	cpum->ml_name = xstrdup("cpu_m");
	cpum->ml_type = machcpu;
	cpum->ml_next = NULL;

	lml->ml_next = cpum;
}

void
cvt_fixups(tdata_t *td, size_t ptrsize)
{
	fix_small_cpu_struct(td, ptrsize);
}
