/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright © 2018 Intel Corporation
 */

#include "../i915_selftest.h"

static int intel_mmio_bases_check(void *arg)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(intel_engines); i++) {
		const struct engine_info *info = &intel_engines[i];
		u8 prev = U8_MAX;

		for (j = 0; j < MAX_MMIO_BASES; j++) {
			u8 gen = info->mmio_bases[j].gen;
			u32 base = info->mmio_bases[j].base;

			if (gen >= prev) {
				pr_err("%s(%s, class:%d, instance:%d): mmio base for gen %x is before the one for gen %x\n",
				       __func__,
				       intel_engine_class_repr(info->class),
				       info->class, info->instance,
				       prev, gen);
				return -EINVAL;
			}

			if (gen == 0)
				break;

			if (!base) {
				pr_err("%s(%s, class:%d, instance:%d): invalid mmio base (%x) for gen %x at entry %u\n",
				       __func__,
				       intel_engine_class_repr(info->class),
				       info->class, info->instance,
				       base, gen, j);
				return -EINVAL;
			}

			prev = gen;
		}

		pr_debug("%s: min gen supported for %s%d is %d\n",
			 __func__,
			 intel_engine_class_repr(info->class),
			 info->instance,
			 prev);
	}

	return 0;
}

int intel_engine_cs_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(intel_mmio_bases_check),
	};

	return i915_subtests(tests, NULL);
}
