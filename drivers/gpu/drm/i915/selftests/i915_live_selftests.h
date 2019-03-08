/* SPDX-License-Identifier: GPL-2.0 */
/* List each unit test as selftest(name, function)
 *
 * The name is used as both an enum and expanded as subtest__name to create
 * a module parameter. It must be unique and legal for a C identifier.
 *
 * The function should be of type int function(void). It may be conditionally
 * compiled using #if IS_ENABLED(DRM_I915_SELFTEST).
 *
 * Tests are executed in order by igt/drv_selftest
 */
selftest(sanitycheck, i915_live_sanitycheck) /* keep first (igt selfcheck) */
selftest(uncore, intel_uncore_live_selftests)
selftest(workarounds, intel_workarounds_live_selftests)
selftest(timelines, i915_timeline_live_selftests)
selftest(requests, i915_request_live_selftests)
selftest(active, i915_active_live_selftests)
selftest(objects, i915_gem_object_live_selftests)
selftest(dmabuf, i915_gem_dmabuf_live_selftests)
selftest(coherency, i915_gem_coherency_live_selftests)
selftest(gtt, i915_gem_gtt_live_selftests)
selftest(gem, i915_gem_live_selftests)
selftest(evict, i915_gem_evict_live_selftests)
selftest(hugepages, i915_gem_huge_page_live_selftests)
selftest(contexts, i915_gem_context_live_selftests)
selftest(hangcheck, intel_hangcheck_live_selftests)
selftest(execlists, intel_execlists_live_selftests)
selftest(guc, intel_guc_live_selftest)
