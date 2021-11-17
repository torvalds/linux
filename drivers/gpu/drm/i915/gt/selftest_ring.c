// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2020 Intel Corporation
 */

static struct intel_ring *mock_ring(unsigned long sz)
{
	struct intel_ring *ring;

	ring = kzalloc(sizeof(*ring) + sz, GFP_KERNEL);
	if (!ring)
		return NULL;

	kref_init(&ring->ref);
	ring->size = sz;
	ring->wrap = BITS_PER_TYPE(ring->size) - ilog2(sz);
	ring->effective_size = sz;
	ring->vaddr = (void *)(ring + 1);
	atomic_set(&ring->pin_count, 1);

	intel_ring_update_space(ring);

	return ring;
}

static void mock_ring_free(struct intel_ring *ring)
{
	kfree(ring);
}

static int check_ring_direction(struct intel_ring *ring,
				u32 next, u32 prev,
				int expected)
{
	int result;

	result = intel_ring_direction(ring, next, prev);
	if (result < 0)
		result = -1;
	else if (result > 0)
		result = 1;

	if (result != expected) {
		pr_err("intel_ring_direction(%u, %u):%d != %d\n",
		       next, prev, result, expected);
		return -EINVAL;
	}

	return 0;
}

static int check_ring_step(struct intel_ring *ring, u32 x, u32 step)
{
	u32 prev = x, next = intel_ring_wrap(ring, x + step);
	int err = 0;

	err |= check_ring_direction(ring, next, next,  0);
	err |= check_ring_direction(ring, prev, prev,  0);
	err |= check_ring_direction(ring, next, prev,  1);
	err |= check_ring_direction(ring, prev, next, -1);

	return err;
}

static int check_ring_offset(struct intel_ring *ring, u32 x, u32 step)
{
	int err = 0;

	err |= check_ring_step(ring, x, step);
	err |= check_ring_step(ring, intel_ring_wrap(ring, x + 1), step);
	err |= check_ring_step(ring, intel_ring_wrap(ring, x - 1), step);

	return err;
}

static int igt_ring_direction(void *dummy)
{
	struct intel_ring *ring;
	unsigned int half = 2048;
	int step, err = 0;

	ring = mock_ring(2 * half);
	if (!ring)
		return -ENOMEM;

	GEM_BUG_ON(ring->size != 2 * half);

	/* Precision of wrap detection is limited to ring->size / 2 */
	for (step = 1; step < half; step <<= 1) {
		err |= check_ring_offset(ring, 0, step);
		err |= check_ring_offset(ring, half, step);
	}
	err |= check_ring_step(ring, 0, half - 64);

	/* And check unwrapped handling for good measure */
	err |= check_ring_offset(ring, 0, 2 * half + 64);
	err |= check_ring_offset(ring, 3 * half, 1);

	mock_ring_free(ring);
	return err;
}

int intel_ring_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_ring_direction),
	};

	return i915_subtests(tests, NULL);
}
