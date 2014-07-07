/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "mdp5_kms.h"
#include "mdp5_smp.h"


/* SMP - Shared Memory Pool
 *
 * These are shared between all the clients, where each plane in a
 * scanout buffer is a SMP client.  Ie. scanout of 3 plane I420 on
 * pipe VIG0 => 3 clients: VIG0_Y, VIG0_CB, VIG0_CR.
 *
 * Based on the size of the attached scanout buffer, a certain # of
 * blocks must be allocated to that client out of the shared pool.
 *
 * For each block, it can be either free, or pending/in-use by a
 * client.  The updates happen in three steps:
 *
 *  1) mdp5_smp_request():
 *     When plane scanout is setup, calculate required number of
 *     blocks needed per client, and request.  Blocks not inuse or
 *     pending by any other client are added to client's pending
 *     set.
 *
 *  2) mdp5_smp_configure():
 *     As hw is programmed, before FLUSH, MDP5_SMP_ALLOC registers
 *     are configured for the union(pending, inuse)
 *
 *  3) mdp5_smp_commit():
 *     After next vblank, copy pending -> inuse.  Optionally update
 *     MDP5_SMP_ALLOC registers if there are newly unused blocks
 *
 * On the next vblank after changes have been committed to hw, the
 * client's pending blocks become it's in-use blocks (and no-longer
 * in-use blocks become available to other clients).
 *
 * btw, hurray for confusing overloaded acronyms!  :-/
 *
 * NOTE: for atomic modeset/pageflip NONBLOCK operations, step #1
 * should happen at (or before)? atomic->check().  And we'd need
 * an API to discard previous requests if update is aborted or
 * (test-only).
 *
 * TODO would perhaps be nice to have debugfs to dump out kernel
 * inuse and pending state of all clients..
 */

static DEFINE_SPINLOCK(smp_lock);


/* step #1: update # of blocks pending for the client: */
int mdp5_smp_request(struct mdp5_kms *mdp5_kms,
		enum mdp5_client_id cid, int nblks)
{
	struct mdp5_client_smp_state *ps = &mdp5_kms->smp_client_state[cid];
	int i, ret, avail, cur_nblks, cnt = mdp5_kms->smp_blk_cnt;
	unsigned long flags;

	spin_lock_irqsave(&smp_lock, flags);

	avail = cnt - bitmap_weight(mdp5_kms->smp_state, cnt);
	if (nblks > avail) {
		ret = -ENOSPC;
		goto fail;
	}

	cur_nblks = bitmap_weight(ps->pending, cnt);
	if (nblks > cur_nblks) {
		/* grow the existing pending reservation: */
		for (i = cur_nblks; i < nblks; i++) {
			int blk = find_first_zero_bit(mdp5_kms->smp_state, cnt);
			set_bit(blk, ps->pending);
			set_bit(blk, mdp5_kms->smp_state);
		}
	} else {
		/* shrink the existing pending reservation: */
		for (i = cur_nblks; i > nblks; i--) {
			int blk = find_first_bit(ps->pending, cnt);
			clear_bit(blk, ps->pending);
			/* don't clear in global smp_state until _commit() */
		}
	}

fail:
	spin_unlock_irqrestore(&smp_lock, flags);
	return 0;
}

static void update_smp_state(struct mdp5_kms *mdp5_kms,
		enum mdp5_client_id cid, mdp5_smp_state_t *assigned)
{
	int cnt = mdp5_kms->smp_blk_cnt;
	uint32_t blk, val;

	for_each_set_bit(blk, *assigned, cnt) {
		int idx = blk / 3;
		int fld = blk % 3;

		val = mdp5_read(mdp5_kms, REG_MDP5_SMP_ALLOC_W_REG(idx));

		switch (fld) {
		case 0:
			val &= ~MDP5_SMP_ALLOC_W_REG_CLIENT0__MASK;
			val |= MDP5_SMP_ALLOC_W_REG_CLIENT0(cid);
			break;
		case 1:
			val &= ~MDP5_SMP_ALLOC_W_REG_CLIENT1__MASK;
			val |= MDP5_SMP_ALLOC_W_REG_CLIENT1(cid);
			break;
		case 2:
			val &= ~MDP5_SMP_ALLOC_W_REG_CLIENT2__MASK;
			val |= MDP5_SMP_ALLOC_W_REG_CLIENT2(cid);
			break;
		}

		mdp5_write(mdp5_kms, REG_MDP5_SMP_ALLOC_W_REG(idx), val);
		mdp5_write(mdp5_kms, REG_MDP5_SMP_ALLOC_R_REG(idx), val);
	}
}

/* step #2: configure hw for union(pending, inuse): */
void mdp5_smp_configure(struct mdp5_kms *mdp5_kms, enum mdp5_client_id cid)
{
	struct mdp5_client_smp_state *ps = &mdp5_kms->smp_client_state[cid];
	int cnt = mdp5_kms->smp_blk_cnt;
	mdp5_smp_state_t assigned;

	bitmap_or(assigned, ps->inuse, ps->pending, cnt);
	update_smp_state(mdp5_kms, cid, &assigned);
}

/* step #3: after vblank, copy pending -> inuse: */
void mdp5_smp_commit(struct mdp5_kms *mdp5_kms, enum mdp5_client_id cid)
{
	struct mdp5_client_smp_state *ps = &mdp5_kms->smp_client_state[cid];
	int cnt = mdp5_kms->smp_blk_cnt;
	mdp5_smp_state_t released;

	/*
	 * Figure out if there are any blocks we where previously
	 * using, which can be released and made available to other
	 * clients:
	 */
	if (bitmap_andnot(released, ps->inuse, ps->pending, cnt)) {
		unsigned long flags;

		spin_lock_irqsave(&smp_lock, flags);
		/* clear released blocks: */
		bitmap_andnot(mdp5_kms->smp_state, mdp5_kms->smp_state,
				released, cnt);
		spin_unlock_irqrestore(&smp_lock, flags);

		update_smp_state(mdp5_kms, CID_UNUSED, &released);
	}

	bitmap_copy(ps->inuse, ps->pending, cnt);
}
