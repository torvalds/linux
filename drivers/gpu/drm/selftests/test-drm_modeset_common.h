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

#endif
