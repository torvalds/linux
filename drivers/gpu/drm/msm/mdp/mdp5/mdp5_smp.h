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

#ifndef __MDP5_SMP_H__
#define __MDP5_SMP_H__

#include "msm_drv.h"

struct mdp5_client_smp_state {
	mdp5_smp_state_t inuse;
	mdp5_smp_state_t configured;
	mdp5_smp_state_t pending;
};

struct mdp5_kms;
struct mdp5_smp;

/*
 * SMP module prototypes:
 * mdp5_smp_init() returns a SMP @handler,
 * which is then used to call the other mdp5_smp_*(handler, ...) functions.
 */

struct mdp5_smp *mdp5_smp_init(struct drm_device *dev, const struct mdp5_smp_block *cfg);
void  mdp5_smp_destroy(struct mdp5_smp *smp);

int  mdp5_smp_request(struct mdp5_smp *smp, enum mdp5_pipe pipe, u32 fmt, u32 width);
void mdp5_smp_configure(struct mdp5_smp *smp, enum mdp5_pipe pipe);
void mdp5_smp_commit(struct mdp5_smp *smp, enum mdp5_pipe pipe);
void mdp5_smp_release(struct mdp5_smp *smp, enum mdp5_pipe pipe);

#endif /* __MDP5_SMP_H__ */
