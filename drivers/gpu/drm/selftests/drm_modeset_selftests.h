/* SPDX-License-Identifier: GPL-2.0 */
/* List each unit test as selftest(name, function)
 *
 * The name is used as both an enum and expanded as igt__name to create
 * a module parameter. It must be unique and legal for a C identifier.
 *
 * Tests are executed in order by igt/drm_selftests_helper
 */
selftest(check_plane_state, igt_check_plane_state)
selftest(check_drm_format_block_width, igt_check_drm_format_block_width)
selftest(check_drm_format_block_height, igt_check_drm_format_block_height)
selftest(check_drm_format_min_pitch, igt_check_drm_format_min_pitch)
selftest(check_drm_framebuffer_create, igt_check_drm_framebuffer_create)
