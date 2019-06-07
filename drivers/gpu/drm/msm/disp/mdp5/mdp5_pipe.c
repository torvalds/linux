/*
 * Copyright (C) 2016 Red Hat
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

int mdp5_pipe_assign(struct drm_atomic_state *s, struct drm_plane *plane,
		     uint32_t caps, uint32_t blkcfg,
		     struct mdp5_hw_pipe **hwpipe,
		     struct mdp5_hw_pipe **r_hwpipe)
{
	struct msm_drm_private *priv = s->dev->dev_private;
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(priv->kms));
	struct mdp5_global_state *new_global_state, *old_global_state;
	struct mdp5_hw_pipe_state *old_state, *new_state;
	int i, j;

	new_global_state = mdp5_get_global_state(s);
	if (IS_ERR(new_global_state))
		return PTR_ERR(new_global_state);

	/* grab old_state after mdp5_get_global_state(), since now we hold lock: */
	old_global_state = mdp5_get_existing_global_state(mdp5_kms);

	old_state = &old_global_state->hwpipe;
	new_state = &new_global_state->hwpipe;

	for (i = 0; i < mdp5_kms->num_hwpipes; i++) {
		struct mdp5_hw_pipe *cur = mdp5_kms->hwpipes[i];

		/* skip if already in-use.. check both new and old state,
		 * since we cannot immediately re-use a pipe that is
		 * released in the current update in some cases:
		 *  (1) mdp5 can have SMP (non-double-buffered)
		 *  (2) hw pipe previously assigned to different CRTC
		 *      (vblanks might not be aligned)
		 */
		if (new_state->hwpipe_to_plane[cur->idx] ||
				old_state->hwpipe_to_plane[cur->idx])
			continue;

		/* skip if doesn't support some required caps: */
		if (caps & ~cur->caps)
			continue;

		/*
		 * don't assign a cursor pipe to a plane that isn't going to
		 * be used as a cursor
		 */
		if (cur->caps & MDP_PIPE_CAP_CURSOR &&
				plane->type != DRM_PLANE_TYPE_CURSOR)
			continue;

		/* possible candidate, take the one with the
		 * fewest unneeded caps bits set:
		 */
		if (!(*hwpipe) || (hweight_long(cur->caps & ~caps) <
				   hweight_long((*hwpipe)->caps & ~caps))) {
			bool r_found = false;

			if (r_hwpipe) {
				for (j = i + 1; j < mdp5_kms->num_hwpipes;
				     j++) {
					struct mdp5_hw_pipe *r_cur =
							mdp5_kms->hwpipes[j];

					/* reject different types of hwpipes */
					if (r_cur->caps != cur->caps)
						continue;

					/* respect priority, eg. VIG0 > VIG1 */
					if (cur->pipe > r_cur->pipe)
						continue;

					*r_hwpipe = r_cur;
					r_found = true;
					break;
				}
			}

			if (!r_hwpipe || r_found)
				*hwpipe = cur;
		}
	}

	if (!(*hwpipe))
		return -ENOMEM;

	if (r_hwpipe && !(*r_hwpipe))
		return -ENOMEM;

	if (mdp5_kms->smp) {
		int ret;

		/* We don't support SMP and 2 hwpipes/plane together */
		WARN_ON(r_hwpipe);

		DBG("%s: alloc SMP blocks", (*hwpipe)->name);
		ret = mdp5_smp_assign(mdp5_kms->smp, &new_global_state->smp,
				(*hwpipe)->pipe, blkcfg);
		if (ret)
			return -ENOMEM;

		(*hwpipe)->blkcfg = blkcfg;
	}

	DBG("%s: assign to plane %s for caps %x",
			(*hwpipe)->name, plane->name, caps);
	new_state->hwpipe_to_plane[(*hwpipe)->idx] = plane;

	if (r_hwpipe) {
		DBG("%s: assign to right of plane %s for caps %x",
		    (*r_hwpipe)->name, plane->name, caps);
		new_state->hwpipe_to_plane[(*r_hwpipe)->idx] = plane;
	}

	return 0;
}

void mdp5_pipe_release(struct drm_atomic_state *s, struct mdp5_hw_pipe *hwpipe)
{
	struct msm_drm_private *priv = s->dev->dev_private;
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(priv->kms));
	struct mdp5_global_state *state = mdp5_get_global_state(s);
	struct mdp5_hw_pipe_state *new_state = &state->hwpipe;

	if (!hwpipe)
		return;

	if (WARN_ON(!new_state->hwpipe_to_plane[hwpipe->idx]))
		return;

	DBG("%s: release from plane %s", hwpipe->name,
		new_state->hwpipe_to_plane[hwpipe->idx]->name);

	if (mdp5_kms->smp) {
		DBG("%s: free SMP blocks", hwpipe->name);
		mdp5_smp_release(mdp5_kms->smp, &state->smp, hwpipe->pipe);
	}

	new_state->hwpipe_to_plane[hwpipe->idx] = NULL;
}

void mdp5_pipe_destroy(struct mdp5_hw_pipe *hwpipe)
{
	kfree(hwpipe);
}

struct mdp5_hw_pipe *mdp5_pipe_init(enum mdp5_pipe pipe,
		uint32_t reg_offset, uint32_t caps)
{
	struct mdp5_hw_pipe *hwpipe;

	hwpipe = kzalloc(sizeof(*hwpipe), GFP_KERNEL);
	if (!hwpipe)
		return ERR_PTR(-ENOMEM);

	hwpipe->name = pipe2name(pipe);
	hwpipe->pipe = pipe;
	hwpipe->reg_offset = reg_offset;
	hwpipe->caps = caps;
	hwpipe->flush_mask = mdp_ctl_flush_mask_pipe(pipe);

	return hwpipe;
}
