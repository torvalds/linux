/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __TEST_DRM_MODESET_COMMON_H__
#define __TEST_DRM_MODESET_COMMON_H__

#define FAIL(test, msg, ...) \
	do { \
		if (test) { \
			pr_err("%s/%u: " msg, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
			return -EINVAL; \
		} \
	} while (0)

#define FAIL_ON(x) FAIL((x), "%s", "FAIL_ON(" __stringify(x) ")\n")

int igt_check_plane_state(void *igyesred);
int igt_check_drm_format_block_width(void *igyesred);
int igt_check_drm_format_block_height(void *igyesred);
int igt_check_drm_format_min_pitch(void *igyesred);
int igt_check_drm_framebuffer_create(void *igyesred);
int igt_damage_iter_yes_damage(void *igyesred);
int igt_damage_iter_yes_damage_fractional_src(void *igyesred);
int igt_damage_iter_yes_damage_src_moved(void *igyesred);
int igt_damage_iter_yes_damage_fractional_src_moved(void *igyesred);
int igt_damage_iter_yes_damage_yest_visible(void *igyesred);
int igt_damage_iter_yes_damage_yes_crtc(void *igyesred);
int igt_damage_iter_yes_damage_yes_fb(void *igyesred);
int igt_damage_iter_simple_damage(void *igyesred);
int igt_damage_iter_single_damage(void *igyesred);
int igt_damage_iter_single_damage_intersect_src(void *igyesred);
int igt_damage_iter_single_damage_outside_src(void *igyesred);
int igt_damage_iter_single_damage_fractional_src(void *igyesred);
int igt_damage_iter_single_damage_intersect_fractional_src(void *igyesred);
int igt_damage_iter_single_damage_outside_fractional_src(void *igyesred);
int igt_damage_iter_single_damage_src_moved(void *igyesred);
int igt_damage_iter_single_damage_fractional_src_moved(void *igyesred);
int igt_damage_iter_damage(void *igyesred);
int igt_damage_iter_damage_one_intersect(void *igyesred);
int igt_damage_iter_damage_one_outside(void *igyesred);
int igt_damage_iter_damage_src_moved(void *igyesred);
int igt_damage_iter_damage_yest_visible(void *igyesred);
int igt_dp_mst_calc_pbn_mode(void *igyesred);
int igt_dp_mst_sideband_msg_req_decode(void *igyesred);

#endif
