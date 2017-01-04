#ifndef __DRM_RANDOM_H__
#define __DRM_RANDOM_H__

/* This is a temporary home for a couple of utility functions that should
 * be transposed to lib/ at the earliest convenience.
 */

#include <linux/random.h>

#define DRM_RND_STATE_INITIALIZER(seed__) ({				\
	struct rnd_state state__;					\
	prandom_seed_state(&state__, (seed__));				\
	state__;							\
})

#define DRM_RND_STATE(name__, seed__) \
	struct rnd_state name__ = DRM_RND_STATE_INITIALIZER(seed__)

unsigned int *drm_random_order(unsigned int count,
			       struct rnd_state *state);
void drm_random_reorder(unsigned int *order,
			unsigned int count,
			struct rnd_state *state);

#endif /* !__DRM_RANDOM_H__ */
