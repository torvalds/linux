/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/lnet/lib-me.c
 *
 * Match Entry management routines
 */

#define DEBUG_SUBSYSTEM S_LNET

#include "../../include/linux/lnet/lib-lnet.h"

/**
 * Create and attach a match entry to the match list of \a portal. The new
 * ME is empty, i.e. not associated with a memory descriptor. LNetMDAttach()
 * can be used to attach a MD to an empty ME.
 *
 * \param portal The portal table index where the ME should be attached.
 * \param match_id Specifies the match criteria for the process ID of
 * the requester. The constants LNET_PID_ANY and LNET_NID_ANY can be
 * used to wildcard either of the identifiers in the lnet_process_id_t
 * structure.
 * \param match_bits,ignore_bits Specify the match criteria to apply
 * to the match bits in the incoming request. The ignore bits are used
 * to mask out insignificant bits in the incoming match bits. The resulting
 * bits are then compared to the ME's match bits to determine if the
 * incoming request meets the match criteria.
 * \param unlink Indicates whether the ME should be unlinked when the memory
 * descriptor associated with it is unlinked (Note that the check for
 * unlinking a ME only occurs when the memory descriptor is unlinked.).
 * Valid values are LNET_RETAIN and LNET_UNLINK.
 * \param pos Indicates whether the new ME should be prepended or
 * appended to the match list. Allowed constants: LNET_INS_BEFORE,
 * LNET_INS_AFTER.
 * \param handle On successful returns, a handle to the newly created ME
 * object is saved here. This handle can be used later in LNetMEInsert(),
 * LNetMEUnlink(), or LNetMDAttach() functions.
 *
 * \retval 0       On success.
 * \retval -EINVAL If \a portal is invalid.
 * \retval -ENOMEM If new ME object cannot be allocated.
 */
int
LNetMEAttach(unsigned int portal,
	     lnet_process_id_t match_id,
	     __u64 match_bits, __u64 ignore_bits,
	     lnet_unlink_t unlink, lnet_ins_pos_t pos,
	     lnet_handle_me_t *handle)
{
	struct lnet_match_table *mtable;
	struct lnet_me *me;
	struct list_head *head;

	LASSERT(the_lnet.ln_refcount > 0);

	if ((int)portal >= the_lnet.ln_nportals)
		return -EINVAL;

	mtable = lnet_mt_of_attach(portal, match_id,
				   match_bits, ignore_bits, pos);
	if (!mtable) /* can't match portal type */
		return -EPERM;

	me = lnet_me_alloc();
	if (!me)
		return -ENOMEM;

	lnet_res_lock(mtable->mt_cpt);

	me->me_portal = portal;
	me->me_match_id = match_id;
	me->me_match_bits = match_bits;
	me->me_ignore_bits = ignore_bits;
	me->me_unlink = unlink;
	me->me_md = NULL;

	lnet_res_lh_initialize(the_lnet.ln_me_containers[mtable->mt_cpt],
			       &me->me_lh);
	if (ignore_bits)
		head = &mtable->mt_mhash[LNET_MT_HASH_IGNORE];
	else
		head = lnet_mt_match_head(mtable, match_id, match_bits);

	me->me_pos = head - &mtable->mt_mhash[0];
	if (pos == LNET_INS_AFTER || pos == LNET_INS_LOCAL)
		list_add_tail(&me->me_list, head);
	else
		list_add(&me->me_list, head);

	lnet_me2handle(handle, me);

	lnet_res_unlock(mtable->mt_cpt);
	return 0;
}
EXPORT_SYMBOL(LNetMEAttach);

/**
 * Create and a match entry and insert it before or after the ME pointed to by
 * \a current_meh. The new ME is empty, i.e. not associated with a memory
 * descriptor. LNetMDAttach() can be used to attach a MD to an empty ME.
 *
 * This function is identical to LNetMEAttach() except for the position
 * where the new ME is inserted.
 *
 * \param current_meh A handle for a ME. The new ME will be inserted
 * immediately before or immediately after this ME.
 * \param match_id,match_bits,ignore_bits,unlink,pos,handle See the discussion
 * for LNetMEAttach().
 *
 * \retval 0       On success.
 * \retval -ENOMEM If new ME object cannot be allocated.
 * \retval -ENOENT If \a current_meh does not point to a valid match entry.
 */
int
LNetMEInsert(lnet_handle_me_t current_meh,
	     lnet_process_id_t match_id,
	     __u64 match_bits, __u64 ignore_bits,
	     lnet_unlink_t unlink, lnet_ins_pos_t pos,
	     lnet_handle_me_t *handle)
{
	struct lnet_me *current_me;
	struct lnet_me *new_me;
	struct lnet_portal *ptl;
	int cpt;

	LASSERT(the_lnet.ln_refcount > 0);

	if (pos == LNET_INS_LOCAL)
		return -EPERM;

	new_me = lnet_me_alloc();
	if (!new_me)
		return -ENOMEM;

	cpt = lnet_cpt_of_cookie(current_meh.cookie);

	lnet_res_lock(cpt);

	current_me = lnet_handle2me(&current_meh);
	if (!current_me) {
		lnet_me_free(new_me);

		lnet_res_unlock(cpt);
		return -ENOENT;
	}

	LASSERT(current_me->me_portal < the_lnet.ln_nportals);

	ptl = the_lnet.ln_portals[current_me->me_portal];
	if (lnet_ptl_is_unique(ptl)) {
		/* nosense to insertion on unique portal */
		lnet_me_free(new_me);
		lnet_res_unlock(cpt);
		return -EPERM;
	}

	new_me->me_pos = current_me->me_pos;
	new_me->me_portal = current_me->me_portal;
	new_me->me_match_id = match_id;
	new_me->me_match_bits = match_bits;
	new_me->me_ignore_bits = ignore_bits;
	new_me->me_unlink = unlink;
	new_me->me_md = NULL;

	lnet_res_lh_initialize(the_lnet.ln_me_containers[cpt], &new_me->me_lh);

	if (pos == LNET_INS_AFTER)
		list_add(&new_me->me_list, &current_me->me_list);
	else
		list_add_tail(&new_me->me_list, &current_me->me_list);

	lnet_me2handle(handle, new_me);

	lnet_res_unlock(cpt);

	return 0;
}
EXPORT_SYMBOL(LNetMEInsert);

/**
 * Unlink a match entry from its match list.
 *
 * This operation also releases any resources associated with the ME. If a
 * memory descriptor is attached to the ME, then it will be unlinked as well
 * and an unlink event will be generated. It is an error to use the ME handle
 * after calling LNetMEUnlink().
 *
 * \param meh A handle for the ME to be unlinked.
 *
 * \retval 0       On success.
 * \retval -ENOENT If \a meh does not point to a valid ME.
 * \see LNetMDUnlink() for the discussion on delivering unlink event.
 */
int
LNetMEUnlink(lnet_handle_me_t meh)
{
	lnet_me_t *me;
	lnet_libmd_t *md;
	lnet_event_t ev;
	int cpt;

	LASSERT(the_lnet.ln_refcount > 0);

	cpt = lnet_cpt_of_cookie(meh.cookie);
	lnet_res_lock(cpt);

	me = lnet_handle2me(&meh);
	if (!me) {
		lnet_res_unlock(cpt);
		return -ENOENT;
	}

	md = me->me_md;
	if (md) {
		md->md_flags |= LNET_MD_FLAG_ABORTED;
		if (md->md_eq && !md->md_refcount) {
			lnet_build_unlink_event(md, &ev);
			lnet_eq_enqueue_event(md->md_eq, &ev);
		}
	}

	lnet_me_unlink(me);

	lnet_res_unlock(cpt);
	return 0;
}
EXPORT_SYMBOL(LNetMEUnlink);

/* call with lnet_res_lock please */
void
lnet_me_unlink(lnet_me_t *me)
{
	list_del(&me->me_list);

	if (me->me_md) {
		lnet_libmd_t *md = me->me_md;

		/* detach MD from portal of this ME */
		lnet_ptl_detach_md(me, md);
		lnet_md_unlink(md);
	}

	lnet_res_lh_invalidate(&me->me_lh);
	lnet_me_free(me);
}

#if 0
static void
lib_me_dump(lnet_me_t *me)
{
	CWARN("Match Entry %p (%#llx)\n", me,
	      me->me_lh.lh_cookie);

	CWARN("\tMatch/Ignore\t= %016lx / %016lx\n",
	      me->me_match_bits, me->me_ignore_bits);

	CWARN("\tMD\t= %p\n", me->md);
	CWARN("\tprev\t= %p\n",
	      list_entry(me->me_list.prev, lnet_me_t, me_list));
	CWARN("\tnext\t= %p\n",
	      list_entry(me->me_list.next, lnet_me_t, me_list));
}
#endif
