/*
 * Copyright (C) 2017 The Linux Foundation. All rights reserved.
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

struct mdp5_hw_mixer *mdp5_mixer_assign(struct drm_atomic_state *s,
					struct drm_crtc *crtc, uint32_t caps)
{
	struct msm_drm_private *priv = s->dev->dev_private;
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(priv->kms));
	struct mdp5_state *state = mdp5_get_state(s);
	struct mdp5_hw_mixer_state *new_state;
	struct mdp5_hw_mixer *mixer = NULL;
	int i;

	if (IS_ERR(state))
		return ERR_CAST(state);

	new_state = &state->hwmixer;

	for (i = 0; i < mdp5_kms->num_hwmixers; i++) {
		struct mdp5_hw_mixer *cur = mdp5_kms->hwmixers[i];

		/* skip if already in-use */
		if (new_state->hwmixer_to_crtc[cur->idx])
			continue;

		/* skip if doesn't support some required caps: */
		if (caps & ~cur->caps)
			continue;

		if (!mixer)
			mixer = cur;
	}

	if (!mixer)
		return ERR_PTR(-ENOMEM);

	new_state->hwmixer_to_crtc[mixer->idx] = crtc;

	return mixer;
}

void mdp5_mixer_release(struct drm_atomic_state *s, struct mdp5_hw_mixer *mixer)
{
	struct mdp5_state *state = mdp5_get_state(s);
	struct mdp5_hw_mixer_state *new_state = &state->hwmixer;

	if (!mixer)
		return;

	if (WARN_ON(!new_state->hwmixer_to_crtc[mixer->idx]))
		return;

	DBG("%s: release from crtc %s", mixer->name,
	    new_state->hwmixer_to_crtc[mixer->idx]->name);

	new_state->hwmixer_to_crtc[mixer->idx] = NULL;
}

void mdp5_mixer_destroy(struct mdp5_hw_mixer *mixer)
{
	kfree(mixer);
}

static const char * const mixer_names[] = {
	"LM0", "LM1", "LM2", "LM3", "LM4", "LM5",
};

struct mdp5_hw_mixer *mdp5_mixer_init(const struct mdp5_lm_instance *lm)
{
	struct mdp5_hw_mixer *mixer;

	/* ignore WB bound mixers for now */
	if (lm->caps & MDP_LM_CAP_WB)
		return NULL;

	mixer = kzalloc(sizeof(*mixer), GFP_KERNEL);
	if (!mixer)
		return ERR_PTR(-ENOMEM);

	mixer->name = mixer_names[lm->id];
	mixer->lm = lm->id;
	mixer->caps = lm->caps;
	mixer->pp = lm->pp;
	mixer->dspp = lm->dspp;
	mixer->flush_mask = mdp_ctl_flush_mask_lm(lm->id);

	return mixer;
}
