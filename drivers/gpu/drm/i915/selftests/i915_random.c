/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "i915_random.h"

u64 i915_prandom_u64_state(struct rnd_state *rnd)
{
	u64 x;

	x = prandom_u32_state(rnd);
	x <<= 32;
	x |= prandom_u32_state(rnd);

	return x;
}

static inline u32 i915_prandom_u32_max_state(u32 ep_ro, struct rnd_state *state)
{
	return upper_32_bits((u64)prandom_u32_state(state) * ep_ro);
}

void i915_random_reorder(unsigned int *order, unsigned int count,
			 struct rnd_state *state)
{
	unsigned int i, j;

	for (i = 0; i < count; i++) {
		BUILD_BUG_ON(sizeof(unsigned int) > sizeof(u32));
		j = i915_prandom_u32_max_state(count, state);
		swap(order[i], order[j]);
	}
}

unsigned int *i915_random_order(unsigned int count, struct rnd_state *state)
{
	unsigned int *order, i;

	order = kmalloc_array(count, sizeof(*order), GFP_KERNEL);
	if (!order)
		return order;

	for (i = 0; i < count; i++)
		order[i] = i;

	i915_random_reorder(order, count, state);
	return order;
}
