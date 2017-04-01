/* List each unit test as selftest(name, function)
 *
 * The name is used as both an enum and expanded as igt__name to create
 * a module parameter. It must be unique and legal for a C identifier.
 *
 * Tests are executed in order by igt/drm_mm
 */
selftest(sanitycheck, igt_sanitycheck) /* keep first (selfcheck for igt) */
selftest(init, igt_init)
selftest(debug, igt_debug)
selftest(reserve, igt_reserve)
selftest(insert, igt_insert)
selftest(replace, igt_replace)
selftest(insert_range, igt_insert_range)
selftest(align, igt_align)
selftest(align32, igt_align32)
selftest(align64, igt_align64)
selftest(evict, igt_evict)
selftest(evict_range, igt_evict_range)
selftest(bottomup, igt_bottomup)
selftest(topdown, igt_topdown)
selftest(color, igt_color)
selftest(color_evict, igt_color_evict)
selftest(color_evict_range, igt_color_evict_range)
