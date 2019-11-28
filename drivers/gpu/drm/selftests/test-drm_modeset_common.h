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

int igt_check_plane_state(void *ignored);
int igt_check_drm_format_block_width(void *ignored);
int igt_check_drm_format_block_height(void *ignored);
int igt_check_drm_format_min_pitch(void *ignored);
int igt_check_drm_framebuffer_create(void *ignored);
int igt_damage_iter_no_damage(void *ignored);
int igt_damage_iter_no_damage_fractional_src(void *ignored);
int igt_damage_iter_no_damage_src_moved(void *ignored);
int igt_damage_iter_no_damage_fractional_src_moved(void *ignored);
int igt_damage_iter_no_damage_not_visible(void *ignored);
int igt_damage_iter_no_damage_no_crtc(void *ignored);
int igt_damage_iter_no_damage_no_fb(void *ignored);
int igt_damage_iter_simple_damage(void *ignored);
int igt_damage_iter_single_damage(void *ignored);
int igt_damage_iter_single_damage_intersect_src(void *ignored);
int igt_damage_iter_single_damage_outside_src(void *ignored);
int igt_damage_iter_single_damage_fractional_src(void *ignored);
int igt_damage_iter_single_damage_intersect_fractional_src(void *ignored);
int igt_damage_iter_single_damage_outside_fractional_src(void *ignored);
int igt_damage_iter_single_damage_src_moved(void *ignored);
int igt_damage_iter_single_damage_fractional_src_moved(void *ignored);
int igt_damage_iter_damage(void *ignored);
int igt_damage_iter_damage_one_intersect(void *ignored);
int igt_damage_iter_damage_one_outside(void *ignored);
int igt_damage_iter_damage_src_moved(void *ignored);
int igt_damage_iter_damage_not_visible(void *ignored);
int igt_dp_mst_calc_pbn_mode(void *ignored);
int igt_dp_mst_sideband_msg_req_decode(void *ignored);

#endif
