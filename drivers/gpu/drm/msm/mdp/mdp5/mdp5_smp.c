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


struct mdp5_smp {
	struct drm_device *dev;

	uint8_t reserved[MAX_CLIENTS]; /* fixed MMBs allocation per client */

	int blk_cnt;
	int blk_size;
};

static inline
struct mdp5_kms *get_kms(struct mdp5_smp *smp)
{
	struct msm_drm_private *priv = smp->dev->dev_private;

	return to_mdp5_kms(to_mdp_kms(priv->kms));
}

static inline u32 pipe2client(enum mdp5_pipe pipe, int plane)
{
#define CID_UNUSED	0

	if (WARN_ON(plane >= pipe2nclients(pipe)))
		return CID_UNUSED;

	/*
	 * Note on SMP clients:
	 * For ViG pipes, fetch Y/Cr/Cb-components clients are always
	 * consecutive, and in that order.
	 *
	 * e.g.:
	 * if mdp5_cfg->smp.clients[SSPP_VIG0] = N,
	 *	Y  plane's client ID is N
	 *	Cr plane's client ID is N + 1
	 *	Cb plane's client ID is N + 2
	 */

	return mdp5_cfg->smp.clients[pipe] + plane;
}

/* allocate blocks for the specified request: */
static int smp_request_block(struct mdp5_smp *smp,
		struct mdp5_smp_state *state,
		u32 cid, int nblks)
{
	void *cs = state->client_state[cid];
	int i, avail, cnt = smp->blk_cnt;
	uint8_t reserved;

	/* we shouldn't be requesting blocks for an in-use client: */
	WARN_ON(bitmap_weight(cs, cnt) > 0);

	reserved = smp->reserved[cid];

	if (reserved) {
		nblks = max(0, nblks - reserved);
		DBG("%d MMBs allocated (%d reserved)", nblks, reserved);
	}

	avail = cnt - bitmap_weight(state->state, cnt);
	if (nblks > avail) {
		dev_err(smp->dev->dev, "out of blks (req=%d > avail=%d)\n",
				nblks, avail);
		return -ENOSPC;
	}

	for (i = 0; i < nblks; i++) {
		int blk = find_first_zero_bit(state->state, cnt);
		set_bit(blk, cs);
		set_bit(blk, state->state);
	}

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
uint32_t mdp5_smp_calculate(struct mdp5_smp *smp,
		const struct mdp_format *format,
		u32 width, bool hdecim)
{
	struct mdp5_kms *mdp5_kms = get_kms(smp);
	int rev = mdp5_cfg_get_hw_rev(mdp5_kms->cfg);
	int i, hsub, nplanes, nlines;
	u32 fmt = format->base.pixel_format;
	uint32_t blkcfg = 0;

	nplanes = drm_format_num_planes(fmt);
	hsub = drm_format_horz_chroma_subsampling(fmt);

	/* different if BWC (compressed framebuffer?) enabled: */
	nlines = 2;

	/* Newer MDPs have split/packing logic, which fetches sub-sampled
	 * U and V components (splits them from Y if necessary) and packs
	 * them together, writes to SMP using a single client.
	 */
	if ((rev > 0) && (format->chroma_sample > CHROMA_FULL)) {
		fmt = DRM_FORMAT_NV24;
		nplanes = 2;

		/* if decimation is enabled, HW decimates less on the
		 * sub sampled chroma components
		 */
		if (hdecim && (hsub > 1))
			hsub = 1;
	}

	for (i = 0; i < nplanes; i++) {
		int n, fetch_stride, cpp;

		cpp = drm_format_plane_cpp(fmt, i);
		fetch_stride = width * cpp / (i ? hsub : 1);

		n = DIV_ROUND_UP(fetch_stride * nlines, smp->blk_size);

		/* for hw rev v1.00 */
		if (rev == 0)
			n = roundup_pow_of_two(n);

		blkcfg |= (n << (8 * i));
	}

	return blkcfg;
}

int mdp5_smp_assign(struct mdp5_smp *smp, struct mdp5_smp_state *state,
		enum mdp5_pipe pipe, uint32_t blkcfg)
{
	struct mdp5_kms *mdp5_kms = get_kms(smp);
	struct drm_device *dev = mdp5_kms->dev;
	int i, ret;

	for (i = 0; i < pipe2nclients(pipe); i++) {
		u32 cid = pipe2client(pipe, i);
		int n = blkcfg & 0xff;

		if (!n)
			continue;

		DBG("%s[%d]: request %d SMP blocks", pipe2name(pipe), i, n);
		ret = smp_request_block(smp, state, cid, n);
		if (ret) {
			dev_err(dev->dev, "Cannot allocate %d SMP blocks: %d\n",
					n, ret);
			return ret;
		}

		blkcfg >>= 8;
	}

	state->assigned |= (1 << pipe);

	return 0;
}

/* Release SMP blocks for all clients of the pipe */
void mdp5_smp_release(struct mdp5_smp *smp, struct mdp5_smp_state *state,
		enum mdp5_pipe pipe)
{
	int i;
	int cnt = smp->blk_cnt;

	for (i = 0; i < pipe2nclients(pipe); i++) {
		u32 cid = pipe2client(pipe, i);
		void *cs = state->client_state[cid];

		/* update global state: */
		bitmap_andnot(state->state, state->state, cs, cnt);

		/* clear client's state */
		bitmap_zero(cs, cnt);
	}

	state->released |= (1 << pipe);
}

/* NOTE: SMP_ALLOC_* regs are *not* double buffered, so release has to
 * happen after scanout completes.
 */
static unsigned update_smp_state(struct mdp5_smp *smp,
		u32 cid, mdp5_smp_state_t *assigned)
{
	struct mdp5_kms *mdp5_kms = get_kms(smp);
	int cnt = smp->blk_cnt;
	unsigned nblks = 0;
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

		nblks++;
	}

	return nblks;
}

void mdp5_smp_prepare_commit(struct mdp5_smp *smp, struct mdp5_smp_state *state)
{
	enum mdp5_pipe pipe;

	for_each_set_bit(pipe, &state->assigned, sizeof(state->assigned) * 8) {
		unsigned i, nblks = 0;

		for (i = 0; i < pipe2nclients(pipe); i++) {
			u32 cid = pipe2client(pipe, i);
			void *cs = state->client_state[cid];

			nblks += update_smp_state(smp, cid, cs);

			DBG("assign %s:%u, %u blks",
				pipe2name(pipe), i, nblks);
		}

		set_fifo_thresholds(smp, pipe, nblks);
	}

	state->assigned = 0;
}

void mdp5_smp_complete_commit(struct mdp5_smp *smp, struct mdp5_smp_state *state)
{
	enum mdp5_pipe pipe;

	for_each_set_bit(pipe, &state->released, sizeof(state->released) * 8) {
		DBG("release %s", pipe2name(pipe));
		set_fifo_thresholds(smp, pipe, 0);
	}

	state->released = 0;
}

void mdp5_smp_destroy(struct mdp5_smp *smp)
{
	kfree(smp);
}

struct mdp5_smp *mdp5_smp_init(struct mdp5_kms *mdp5_kms, const struct mdp5_smp_block *cfg)
{
	struct mdp5_smp_state *state = &mdp5_kms->state->smp;
	struct mdp5_smp *smp = NULL;
	int ret;

	smp = kzalloc(sizeof(*smp), GFP_KERNEL);
	if (unlikely(!smp)) {
		ret = -ENOMEM;
		goto fail;
	}

	smp->dev = mdp5_kms->dev;
	smp->blk_cnt = cfg->mmb_count;
	smp->blk_size = cfg->mmb_size;

	/* statically tied MMBs cannot be re-allocated: */
	bitmap_copy(state->state, cfg->reserved_state, smp->blk_cnt);
	memcpy(smp->reserved, cfg->reserved, sizeof(smp->reserved));

	return smp;
fail:
	if (smp)
		mdp5_smp_destroy(smp);

	return ERR_PTR(ret);
}
