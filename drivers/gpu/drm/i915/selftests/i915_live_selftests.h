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
 * compiled using #if IS_ENABLED(CONFIG_DRM_I915_SELFTEST).
 *
 * Tests are executed in order by igt/i915_selftest
 */
selftest(sanitycheck, i915_live_sanitycheck) /* keep first (igt selfcheck) */
selftest(uncore, intel_uncore_live_selftests)
selftest(workarounds, intel_workarounds_live_selftests)
selftest(gt_engines, intel_engine_live_selftests)
selftest(gt_timelines, intel_timeline_live_selftests)
selftest(gt_contexts, intel_context_live_selftests)
selftest(gt_lrc, intel_lrc_live_selftests)
selftest(gt_mocs, intel_mocs_live_selftests)
selftest(gt_pm, intel_gt_pm_live_selftests)
selftest(gt_heartbeat, intel_heartbeat_live_selftests)
selftest(requests, i915_request_live_selftests)
selftest(active, i915_active_live_selftests)
selftest(objects, i915_gem_object_live_selftests)
selftest(mman, i915_gem_mman_live_selftests)
selftest(dmabuf, i915_gem_dmabuf_live_selftests)
selftest(vma, i915_vma_live_selftests)
selftest(coherency, i915_gem_coherency_live_selftests)
selftest(gtt, i915_gem_gtt_live_selftests)
selftest(gem, i915_gem_live_selftests)
selftest(evict, i915_gem_evict_live_selftests)
selftest(hugepages, i915_gem_huge_page_live_selftests)
selftest(gem_contexts, i915_gem_context_live_selftests)
selftest(gem_execbuf, i915_gem_execbuffer_live_selftests)
selftest(blt, i915_gem_object_blt_live_selftests)
selftest(client, i915_gem_client_blt_live_selftests)
selftest(reset, intel_reset_live_selftests)
selftest(memory_region, intel_memory_region_live_selftests)
selftest(hangcheck, intel_hangcheck_live_selftests)
selftest(execlists, intel_execlists_live_selftests)
selftest(ring_submission, intel_ring_submission_live_selftests)
selftest(perf, i915_perf_live_selftests)
/* Here be dragons: keep last to run last! */
selftest(late_gt_pm, intel_gt_pm_late_selftests)
