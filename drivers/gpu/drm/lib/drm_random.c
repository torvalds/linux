// SPDX-License-Identifier: GPL-2.0
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "drm_random.h"

u32 drm_prandom_u32_max_state(u32 ep_ro, struct rnd_state *state)
{
	return upper_32_bits((u64)prandom_u32_state(state) * ep_ro);
}
EXPORT_SYMBOL(drm_prandom_u32_max_state);

void drm_random_reorder(unsigned int *order, unsigned int count,
			struct rnd_state *state)
{
	unsigned int i, j;

	for (i = 0; i < count; ++i) {
		BUILD_BUG_ON(sizeof(unsigned int) > sizeof(u32));
		j = drm_prandom_u32_max_state(count, state);
		swap(order[i], order[j]);
	}
}
EXPORT_SYMBOL(drm_random_reorder);

unsigned int *drm_random_order(unsigned int count, struct rnd_state *state)
{
	unsigned int *order, i;

	order = kmalloc_array(count, sizeof(*order), GFP_KERNEL);
	if (!order)
		return order;

	for (i = 0; i < count; i++)
		order[i] = i;

	drm_random_reorder(order, count, state);
	return order;
}
EXPORT_SYMBOL(drm_random_order);
