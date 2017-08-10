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

#ifndef __MDP5_PIPE_H__
#define __MDP5_PIPE_H__

/* TODO: Add SSPP_MAX in mdp5.xml.h */
#define SSPP_MAX	(SSPP_CURSOR1 + 1)

/* represents a hw pipe, which is dynamically assigned to a plane */
struct mdp5_hw_pipe {
	int idx;

	const char *name;
	enum mdp5_pipe pipe;

	uint32_t reg_offset;
	uint32_t caps;

	uint32_t flush_mask;      /* used to commit pipe registers */

	/* number of smp blocks per plane, ie:
	 *   nblks_y | (nblks_u << 8) | (nblks_v << 16)
	 */
	uint32_t blkcfg;
};

/* global atomic state of assignment between pipes and planes: */
struct mdp5_hw_pipe_state {
	struct drm_plane *hwpipe_to_plane[SSPP_MAX];
};

struct mdp5_hw_pipe *__must_check
mdp5_pipe_assign(struct drm_atomic_state *s, struct drm_plane *plane,
		uint32_t caps, uint32_t blkcfg);
void mdp5_pipe_release(struct drm_atomic_state *s, struct mdp5_hw_pipe *hwpipe);

struct mdp5_hw_pipe *mdp5_pipe_init(enum mdp5_pipe pipe,
		uint32_t reg_offset, uint32_t caps);
void mdp5_pipe_destroy(struct mdp5_hw_pipe *hwpipe);

#endif /* __MDP5_PIPE_H__ */
