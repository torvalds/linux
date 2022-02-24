/* SPDX-License-Identifier: GPL-2.0 */
/* List each unit test as selftest(name, function)
 *
 * The name is used as both an enum and expanded as igt__name to create
 * a module parameter. It must be unique and legal for a C identifier.
 *
 * Tests are executed in order by igt/drm_buddy
 */
selftest(sanitycheck, igt_sanitycheck) /* keep first (selfcheck for igt) */
selftest(buddy_alloc_limit, igt_buddy_alloc_limit)
selftest(buddy_alloc_range, igt_buddy_alloc_range)
selftest(buddy_alloc_optimistic, igt_buddy_alloc_optimistic)
selftest(buddy_alloc_pessimistic, igt_buddy_alloc_pessimistic)
selftest(buddy_alloc_smoke, igt_buddy_alloc_smoke)
selftest(buddy_alloc_pathological, igt_buddy_alloc_pathological)
