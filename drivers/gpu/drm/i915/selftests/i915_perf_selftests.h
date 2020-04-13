/* SPDX-License-Identifier: GPL-2.0 */

#ifndef selftest
#define selftest(x, y)
#endif

/*
 * List each unit test as selftest(name, function)
 *
 * The name is used as both an enum and expanded as subtest__name to create
 * a module parameter. It must be unique and legal for a C identifier.
 *
 * The function should be of type int function(void). It may be conditionally
 * compiled using #if IS_ENABLED(DRM_I915_SELFTEST).
 *
 * Tests are executed in order by igt/i915_selftest
 */
selftest(engine_cs, intel_engine_cs_perf_selftests)
selftest(blt, i915_gem_object_blt_perf_selftests)
selftest(region, intel_memory_region_perf_selftests)
