/* SPDX-License-Identifier: GPL-2.0 */
/* List each unit test as selftest(name, function)
 *
 * The name is used as both an enum and expanded as igt__name to create
 * a module parameter. It must be unique and legal for a C identifier.
 *
 * Tests are executed in order by igt/drm_selftests_helper
 */
selftest(check_drm_framebuffer_create, igt_check_drm_framebuffer_create)
selftest(dp_mst_calc_pbn_mode, igt_dp_mst_calc_pbn_mode)
selftest(dp_mst_sideband_msg_req_decode, igt_dp_mst_sideband_msg_req_decode)
