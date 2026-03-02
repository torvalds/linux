/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __GPU_RANDOM_H__
#define __GPU_RANDOM_H__

/* This is a temporary home for a couple of utility functions that should
 * be transposed to lib/ at the earliest convenience.
 */

#include <linux/prandom.h>

#define GPU_RND_STATE_INITIALIZER(seed__) ({				\
	struct rnd_state state__;					\
	prandom_seed_state(&state__, (seed__));				\
	state__;							\
})

#define GPU_RND_STATE(name__, seed__) \
	struct rnd_state name__ = GPU_RND_STATE_INITIALIZER(seed__)

unsigned int *gpu_random_order(unsigned int count,
			       struct rnd_state *state);
void gpu_random_reorder(unsigned int *order,
			unsigned int count,
			struct rnd_state *state);
u32 gpu_prandom_u32_max_state(u32 ep_ro,
			      struct rnd_state *state);

#endif /* !__GPU_RANDOM_H__ */
