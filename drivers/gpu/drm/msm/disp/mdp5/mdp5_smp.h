/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __MDP5_SMP_H__
#define __MDP5_SMP_H__

#include <drm/drm_print.h>

#include "msm_drv.h"

/*
 * SMP - Shared Memory Pool:
 *
 * SMP blocks are shared between all the clients, where each plane in
 * a scanout buffer is a SMP client.  Ie. scanout of 3 plane I420 on
 * pipe VIG0 => 3 clients: VIG0_Y, VIG0_CB, VIG0_CR.
 *
 * Based on the size of the attached scanout buffer, a certain # of
 * blocks must be allocated to that client out of the shared pool.
 *
 * In some hw, some blocks are statically allocated for certain pipes
 * and CANNOT be re-allocated (eg: MMB0 and MMB1 both tied to RGB0).
 *
 *
 * Atomic SMP State:
 *
 * On atomic updates that modify SMP configuration, the state is cloned
 * (copied) and modified.  For test-only, or in cases where atomic
 * update fails (or if we hit ww_mutex deadlock/backoff condition) the
 * new state is simply thrown away.
 *
 * Because the SMP registers are not double buffered, updates are a
 * two step process:
 *
 * 1) in _prepare_commit() we configure things (via read-modify-write)
 *    for the newly assigned pipes, so we don't take away blocks
 *    assigned to pipes that are still scanning out
 * 2) in _complete_commit(), after vblank/etc, we clear things for the
 *    released clients, since at that point old pipes are no longer
 *    scanning out.
 */
struct mdp5_smp_state {
	/* global state of what blocks are in use: */
	mdp5_smp_state_t state;

	/* per client state of what blocks they are using: */
	mdp5_smp_state_t client_state[MAX_CLIENTS];

	/* assigned pipes (hw updated at _prepare_commit()): */
	unsigned long assigned;

	/* released pipes (hw updated at _complete_commit()): */
	unsigned long released;
};

struct mdp5_kms;
struct mdp5_smp;

/*
 * SMP module prototypes:
 * mdp5_smp_init() returns a SMP @handler,
 * which is then used to call the other mdp5_smp_*(handler, ...) functions.
 */

struct mdp5_smp *mdp5_smp_init(struct mdp5_kms *mdp5_kms,
		const struct mdp5_smp_block *cfg);

struct mdp5_global_state;
void mdp5_smp_dump(struct mdp5_smp *smp, struct drm_printer *p,
		   struct mdp5_global_state *global_state);

uint32_t mdp5_smp_calculate(struct mdp5_smp *smp,
		const struct mdp_format *format,
		u32 width, bool hdecim);

int mdp5_smp_assign(struct mdp5_smp *smp, struct mdp5_smp_state *state,
		enum mdp5_pipe pipe, uint32_t blkcfg);
void mdp5_smp_release(struct mdp5_smp *smp, struct mdp5_smp_state *state,
		enum mdp5_pipe pipe);

void mdp5_smp_prepare_commit(struct mdp5_smp *smp, struct mdp5_smp_state *state);
void mdp5_smp_complete_commit(struct mdp5_smp *smp, struct mdp5_smp_state *state);

#endif /* __MDP5_SMP_H__ */
