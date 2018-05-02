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

#ifndef __MDP5_LM_H__
#define __MDP5_LM_H__

/* represents a hw Layer Mixer, one (or more) is dynamically assigned to a crtc */
struct mdp5_hw_mixer {
	int idx;

	const char *name;

	int lm;			/* the LM instance # */
	uint32_t caps;
	int pp;
	int dspp;

	uint32_t flush_mask;      /* used to commit LM registers */
};

/* global atomic state of assignment between CRTCs and Layer Mixers: */
struct mdp5_hw_mixer_state {
	struct drm_crtc *hwmixer_to_crtc[8];
};

struct mdp5_hw_mixer *mdp5_mixer_init(const struct mdp5_lm_instance *lm);
void mdp5_mixer_destroy(struct mdp5_hw_mixer *lm);
int mdp5_mixer_assign(struct drm_atomic_state *s, struct drm_crtc *crtc,
		      uint32_t caps, struct mdp5_hw_mixer **mixer,
		      struct mdp5_hw_mixer **r_mixer);
void mdp5_mixer_release(struct drm_atomic_state *s,
			struct mdp5_hw_mixer *mixer);

#endif /* __MDP5_LM_H__ */
