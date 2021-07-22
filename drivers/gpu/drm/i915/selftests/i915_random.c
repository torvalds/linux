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
#include "i915_utils.h"

u64 i915_prandom_u64_state(struct rnd_state *rnd)
{
	u64 x;

	x = prandom_u32_state(rnd);
	x <<= 32;
	x |= prandom_u32_state(rnd);

	return x;
}

void i915_prandom_shuffle(void *arr, size_t elsz, size_t count,
			  struct rnd_state *state)
{
	char stack[128];

	if (WARN_ON(elsz > sizeof(stack) || count > U32_MAX))
		return;

	if (!elsz || !count)
		return;

	/* Fisher-Yates shuffle courtesy of Knuth */
	while (--count) {
		size_t swp;

		swp = i915_prandom_u32_max_state(count + 1, state);
		if (swp == count)
			continue;

		memcpy(stack, arr + count * elsz, elsz);
		memcpy(arr + count * elsz, arr + swp * elsz, elsz);
		memcpy(arr + swp * elsz, stack, elsz);
	}
}

void i915_random_reorder(unsigned int *order, unsigned int count,
			 struct rnd_state *state)
{
	i915_prandom_shuffle(order, sizeof(*order), count, state);
}

unsigned int *i915_random_order(unsigned int count, struct rnd_state *state)
{
	unsigned int *order, i;

	order = kmalloc_array(count, sizeof(*order),
			      GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
	if (!order)
		return order;

	for (i = 0; i < count; i++)
		order[i] = i;

	i915_random_reorder(order, count, state);
	return order;
}

u64 igt_random_offset(struct rnd_state *state,
		      u64 start, u64 end,
		      u64 len, u64 align)
{
	u64 range, addr;

	BUG_ON(range_overflows(start, len, end));
	BUG_ON(round_up(start, align) > round_down(end - len, align));

	range = round_down(end - len, align) - round_up(start, align);
	if (range) {
		addr = i915_prandom_u64_state(state);
		div64_u64_rem(addr, range, &addr);
		start += addr;
	}

	return round_up(start, align);
}
