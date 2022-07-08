/* SPDX-License-Identifier: GPL-2.0 */
/* List each unit test as selftest(name, function)
 *
 * The name is used as both an enum and expanded as igt__name to create
 * a module parameter. It must be unique and legal for a C identifier.
 *
 * Tests are executed in order by igt/drm_selftests_helper
 */
selftest(drm_rect_clip_scaled_div_by_zero, igt_drm_rect_clip_scaled_div_by_zero)
selftest(drm_rect_clip_scaled_not_clipped, igt_drm_rect_clip_scaled_not_clipped)
selftest(drm_rect_clip_scaled_clipped, igt_drm_rect_clip_scaled_clipped)
selftest(drm_rect_clip_scaled_signed_vs_unsigned, igt_drm_rect_clip_scaled_signed_vs_unsigned)
selftest(check_plane_state, igt_check_plane_state)
selftest(check_drm_format_block_width, igt_check_drm_format_block_width)
selftest(check_drm_format_block_height, igt_check_drm_format_block_height)
selftest(check_drm_format_min_pitch, igt_check_drm_format_min_pitch)
selftest(check_drm_framebuffer_create, igt_check_drm_framebuffer_create)
selftest(dp_mst_calc_pbn_mode, igt_dp_mst_calc_pbn_mode)
selftest(dp_mst_sideband_msg_req_decode, igt_dp_mst_sideband_msg_req_decode)
