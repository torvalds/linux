/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
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
 * In some hw, some blocks are statically allocated for certain pipes
 * and CANNOT be re-allocated (eg: MMB0 and MMB1 both tied to RGB0).
 *
 * For each block that can be dynamically allocated, it can be either
 * free, or pending/in-use by a client. The updates happen in three steps:
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

struct mdp5_smp {
	struct drm_device *dev;

	int blk_cnt;
	int blk_size;

	spinlock_t state_lock;
	mdp5_smp_state_t state; /* to track smp allocation amongst pipes: */

	struct mdp5_client_smp_state client_state[CID_MAX];
};

static inline
struct mdp5_kms *get_kms(struct mdp5_smp *smp)
{
	struct msm_drm_private *priv = smp->dev->dev_private;

	return to_mdp5_kms(to_mdp_kms(priv->kms));
}

static inline enum mdp5_client_id pipe2client(enum mdp5_pipe pipe, int plane)
{
	WARN_ON(plane >= pipe2nclients(pipe));
	switch (pipe) {
	case SSPP_VIG0: return CID_VIG0_Y + plane;
	case SSPP_VIG1: return CID_VIG1_Y + plane;
	case SSPP_VIG2: return CID_VIG2_Y + plane;
	case SSPP_RGB0: return CID_RGB0;
	case SSPP_RGB1: return CID_RGB1;
	case SSPP_RGB2: return CID_RGB2;
	case SSPP_DMA0: return CID_DMA0_Y + plane;
	case SSPP_DMA1: return CID_DMA1_Y + plane;
	case SSPP_VIG3: return CID_VIG3_Y + plane;
	case SSPP_RGB3: return CID_RGB3;
	default:        return CID_UNUSED;
	}
}

/* step #1: update # of blocks pending for the client: */
static int smp_request_block(struct mdp5_smp *smp,
		enum mdp5_client_id cid, int nblks)
{
	struct mdp5_kms *mdp5_kms = get_kms(smp);
	const struct mdp5_cfg_hw *hw_cfg;
	struct mdp5_client_smp_state *ps = &smp->client_state[cid];
	int i, ret, avail, cur_nblks, cnt = smp->blk_cnt;
	int reserved;
	unsigned long flags;

	hw_cfg = mdp5_cfg_get_hw_config(mdp5_kms->cfg);
	reserved = hw_cfg->smp.reserved[cid];

	spin_lock_irqsave(&smp->state_lock, flags);

	if (reserved) {
		nblks = max(0, nblks - reserved);
		DBG("%d MMBs allocated (%d reserved)", nblks, reserved);
	}

	avail = cnt - bitmap_weight(smp->state, cnt);
	if (nblks > avail) {
		dev_err(mdp5_kms->dev->dev, "out of blks (req=%d > avail=%d)\n",
				nblks, avail);
		ret = -ENOSPC;
		goto fail;
	}

	cur_nblks = bitmap_weight(ps->pending, cnt);
	if (nblks > cur_nblks) {
		/* grow the existing pending reservation: */
		for (i = cur_nblks; i < nblks; i++) {
			int blk = find_first_zero_bit(smp->state, cnt);
			set_bit(blk, ps->pending);
			set_bit(blk, smp->state);
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
	spin_unlock_irqrestore(&smp->state_lock, flags);
	return 0;
}

static void set_fifo_thresholds(struct mdp5_smp *smp,
		enum mdp5_pipe pipe, int nblks)
{
	struct mdp5_kms *mdp5_kms = get_kms(smp);
	u32 smp_entries_per_blk = smp->blk_size / (128 / BITS_PER_BYTE);
	u32 val;

	/* 1/4 of SMP pool that is being fetched */
	val = (nblks * smp_entries_per_blk) / 4;

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_REQPRIO_FIFO_WM_0(pipe), val * 1);
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_REQPRIO_FIFO_WM_1(pipe), val * 2);
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_REQPRIO_FIFO_WM_2(pipe), val * 3);
}

/*
 * NOTE: looks like if horizontal decimation is used (if we supported that)
 * then the width used to calculate SMP block requirements is the post-
 * decimated width.  Ie. SMP buffering sits downstream of decimation (which
 * presumably happens during the dma from scanout buffer).
 */
int mdp5_smp_request(struct mdp5_smp *smp, enum mdp5_pipe pipe, u32 fmt, u32 width)
{
	struct mdp5_kms *mdp5_kms = get_kms(smp);
	struct drm_device *dev = mdp5_kms->dev;
	int rev = mdp5_cfg_get_hw_rev(mdp5_kms->cfg);
	int i, hsub, nplanes, nlines, nblks, ret;

	nplanes = drm_format_num_planes(fmt);
	hsub = drm_format_horz_chroma_subsampling(fmt);

	/* different if BWC (compressed framebuffer?) enabled: */
	nlines = 2;

	for (i = 0, nblks = 0; i < nplanes; i++) {
		int n, fetch_stride, cpp;

		cpp = drm_format_plane_cpp(fmt, i);
		fetch_stride = width * cpp / (i ? hsub : 1);

		n = DIV_ROUND_UP(fetch_stride * nlines, smp->blk_size);

		/* for hw rev v1.00 */
		if (rev == 0)
			n = roundup_pow_of_two(n);

		DBG("%s[%d]: request %d SMP blocks", pipe2name(pipe), i, n);
		ret = smp_request_block(smp, pipe2client(pipe, i), n);
		if (ret) {
			dev_err(dev->dev, "Cannot allocate %d SMP blocks: %d\n",
					n, ret);
			return ret;
		}

		nblks += n;
	}

	set_fifo_thresholds(smp, pipe, nblks);

	return 0;
}

/* Release SMP blocks for all clients of the pipe */
void mdp5_smp_release(struct mdp5_smp *smp, enum mdp5_pipe pipe)
{
	int i, nblks;

	for (i = 0, nblks = 0; i < pipe2nclients(pipe); i++)
		smp_request_block(smp, pipe2client(pipe, i), 0);
	set_fifo_thresholds(smp, pipe, 0);
}

static void update_smp_state(struct mdp5_smp *smp,
		enum mdp5_client_id cid, mdp5_smp_state_t *assigned)
{
	struct mdp5_kms *mdp5_kms = get_kms(smp);
	int cnt = smp->blk_cnt;
	u32 blk, val;

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
void mdp5_smp_configure(struct mdp5_smp *smp, enum mdp5_pipe pipe)
{
	int cnt = smp->blk_cnt;
	mdp5_smp_state_t assigned;
	int i;

	for (i = 0; i < pipe2nclients(pipe); i++) {
		enum mdp5_client_id cid = pipe2client(pipe, i);
		struct mdp5_client_smp_state *ps = &smp->client_state[cid];

		bitmap_or(assigned, ps->inuse, ps->pending, cnt);
		update_smp_state(smp, cid, &assigned);
	}
}

/* step #3: after vblank, copy pending -> inuse: */
void mdp5_smp_commit(struct mdp5_smp *smp, enum mdp5_pipe pipe)
{
	int cnt = smp->blk_cnt;
	mdp5_smp_state_t released;
	int i;

	for (i = 0; i < pipe2nclients(pipe); i++) {
		enum mdp5_client_id cid = pipe2client(pipe, i);
		struct mdp5_client_smp_state *ps = &smp->client_state[cid];

		/*
		 * Figure out if there are any blocks we where previously
		 * using, which can be released and made available to other
		 * clients:
		 */
		if (bitmap_andnot(released, ps->inuse, ps->pending, cnt)) {
			unsigned long flags;

			spin_lock_irqsave(&smp->state_lock, flags);
			/* clear released blocks: */
			bitmap_andnot(smp->state, smp->state, released, cnt);
			spin_unlock_irqrestore(&smp->state_lock, flags);

			update_smp_state(smp, CID_UNUSED, &released);
		}

		bitmap_copy(ps->inuse, ps->pending, cnt);
	}
}

void mdp5_smp_destroy(struct mdp5_smp *smp)
{
	kfree(smp);
}

struct mdp5_smp *mdp5_smp_init(struct drm_device *dev, const struct mdp5_smp_block *cfg)
{
	struct mdp5_smp *smp = NULL;
	int ret;

	smp = kzalloc(sizeof(*smp), GFP_KERNEL);
	if (unlikely(!smp)) {
		ret = -ENOMEM;
		goto fail;
	}

	smp->dev = dev;
	smp->blk_cnt = cfg->mmb_count;
	smp->blk_size = cfg->mmb_size;

	/* statically tied MMBs cannot be re-allocated: */
	bitmap_copy(smp->state, cfg->reserved_state, smp->blk_cnt);
	spin_lock_init(&smp->state_lock);

	return smp;
fail:
	if (smp)
		mdp5_smp_destroy(smp);

	return ERR_PTR(ret);
}
