/*
 * Test cases for the drm_kms_helper functions
 */

#define pr_fmt(fmt) "drm_kms_helper: " fmt

#include <linux/module.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_modes.h>

#define TESTS "drm_helper_selftests.h"
#include "drm_selftest.h"

#define FAIL(test, msg, ...) \
	do { \
		if (test) { \
			pr_err("%s/%u: " msg, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
			return -EINVAL; \
		} \
	} while (0)

#define FAIL_ON(x) FAIL((x), "%s", "FAIL_ON(" __stringify(x) ")\n")

static void set_src(struct drm_plane_state *plane_state,
		    unsigned src_x, unsigned src_y,
		    unsigned src_w, unsigned src_h)
{
	plane_state->src_x = src_x;
	plane_state->src_y = src_y;
	plane_state->src_w = src_w;
	plane_state->src_h = src_h;
}

static bool check_src_eq(struct drm_plane_state *plane_state,
			 unsigned src_x, unsigned src_y,
			 unsigned src_w, unsigned src_h)
{
	if (plane_state->src.x1 < 0) {
		pr_err("src x coordinate %x should never be below 0.\n", plane_state->src.x1);
		drm_rect_debug_print("src: ", &plane_state->src, true);
		return false;
	}
	if (plane_state->src.y1 < 0) {
		pr_err("src y coordinate %x should never be below 0.\n", plane_state->src.y1);
		drm_rect_debug_print("src: ", &plane_state->src, true);
		return false;
	}

	if (plane_state->src.x1 != src_x ||
	    plane_state->src.y1 != src_y ||
	    drm_rect_width(&plane_state->src) != src_w ||
	    drm_rect_height(&plane_state->src) != src_h) {
		drm_rect_debug_print("src: ", &plane_state->src, true);
		return false;
	}

	return true;
}

static void set_crtc(struct drm_plane_state *plane_state,
		     int crtc_x, int crtc_y,
		     unsigned crtc_w, unsigned crtc_h)
{
	plane_state->crtc_x = crtc_x;
	plane_state->crtc_y = crtc_y;
	plane_state->crtc_w = crtc_w;
	plane_state->crtc_h = crtc_h;
}

static bool check_crtc_eq(struct drm_plane_state *plane_state,
			  int crtc_x, int crtc_y,
			  unsigned crtc_w, unsigned crtc_h)
{
	if (plane_state->dst.x1 != crtc_x ||
	    plane_state->dst.y1 != crtc_y ||
	    drm_rect_width(&plane_state->dst) != crtc_w ||
	    drm_rect_height(&plane_state->dst) != crtc_h) {
		drm_rect_debug_print("dst: ", &plane_state->dst, false);

		return false;
	}

	return true;
}

static int igt_check_plane_state(void *ignored)
{
	int ret;

	const struct drm_crtc_state crtc_state = {
		.crtc = ZERO_SIZE_PTR,
		.enable = true,
		.active = true,
		.mode = {
			DRM_MODE("1024x768", 0, 65000, 1024, 1048,
				1184, 1344, 0, 768, 771, 777, 806, 0,
				DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC)
		},
	};
	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};
	struct drm_plane_state plane_state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.rotation = DRM_MODE_ROTATE_0
	};

	/* Simple clipping, no scaling. */
	set_src(&plane_state, 0, 0, fb.width << 16, fb.height << 16);
	set_crtc(&plane_state, 0, 0, fb.width, fb.height);
	ret = drm_atomic_helper_check_plane_state(&plane_state, &crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  false, false);
	FAIL(ret < 0, "Simple clipping check should pass\n");
	FAIL_ON(!plane_state.visible);
	FAIL_ON(!check_src_eq(&plane_state, 0, 0, 1024 << 16, 768 << 16));
	FAIL_ON(!check_crtc_eq(&plane_state, 0, 0, 1024, 768));

	/* Rotated clipping + reflection, no scaling. */
	plane_state.rotation = DRM_MODE_ROTATE_90 | DRM_MODE_REFLECT_X;
	ret = drm_atomic_helper_check_plane_state(&plane_state, &crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  false, false);
	FAIL(ret < 0, "Rotated clipping check should pass\n");
	FAIL_ON(!plane_state.visible);
	FAIL_ON(!check_src_eq(&plane_state, 0, 0, 768 << 16, 1024 << 16));
	FAIL_ON(!check_crtc_eq(&plane_state, 0, 0, 1024, 768));
	plane_state.rotation = DRM_MODE_ROTATE_0;

	/* Check whether positioning works correctly. */
	set_src(&plane_state, 0, 0, 1023 << 16, 767 << 16);
	set_crtc(&plane_state, 0, 0, 1023, 767);
	ret = drm_atomic_helper_check_plane_state(&plane_state, &crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  false, false);
	FAIL(!ret, "Should not be able to position on the crtc with can_position=false\n");

	ret = drm_atomic_helper_check_plane_state(&plane_state, &crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  true, false);
	FAIL(ret < 0, "Simple positioning should work\n");
	FAIL_ON(!plane_state.visible);
	FAIL_ON(!check_src_eq(&plane_state, 0, 0, 1023 << 16, 767 << 16));
	FAIL_ON(!check_crtc_eq(&plane_state, 0, 0, 1023, 767));

	/* Simple scaling tests. */
	set_src(&plane_state, 0, 0, 512 << 16, 384 << 16);
	set_crtc(&plane_state, 0, 0, 1024, 768);
	ret = drm_atomic_helper_check_plane_state(&plane_state, &crtc_state,
						  0x8001,
						  DRM_PLANE_HELPER_NO_SCALING,
						  false, false);
	FAIL(!ret, "Upscaling out of range should fail.\n");
	ret = drm_atomic_helper_check_plane_state(&plane_state, &crtc_state,
						  0x8000,
						  DRM_PLANE_HELPER_NO_SCALING,
						  false, false);
	FAIL(ret < 0, "Upscaling exactly 2x should work\n");
	FAIL_ON(!plane_state.visible);
	FAIL_ON(!check_src_eq(&plane_state, 0, 0, 512 << 16, 384 << 16));
	FAIL_ON(!check_crtc_eq(&plane_state, 0, 0, 1024, 768));

	set_src(&plane_state, 0, 0, 2048 << 16, 1536 << 16);
	ret = drm_atomic_helper_check_plane_state(&plane_state, &crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  0x1ffff, false, false);
	FAIL(!ret, "Downscaling out of range should fail.\n");
	ret = drm_atomic_helper_check_plane_state(&plane_state, &crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  0x20000, false, false);
	FAIL(ret < 0, "Should succeed with exact scaling limit\n");
	FAIL_ON(!plane_state.visible);
	FAIL_ON(!check_src_eq(&plane_state, 0, 0, 2048 << 16, 1536 << 16));
	FAIL_ON(!check_crtc_eq(&plane_state, 0, 0, 1024, 768));

	/* Testing rounding errors. */
	set_src(&plane_state, 0, 0, 0x40001, 0x40001);
	set_crtc(&plane_state, 1022, 766, 4, 4);
	ret = drm_atomic_helper_check_plane_state(&plane_state, &crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  0x10001,
						  true, false);
	FAIL(ret < 0, "Should succeed by clipping to exact multiple");
	FAIL_ON(!plane_state.visible);
	FAIL_ON(!check_src_eq(&plane_state, 0, 0, 2 << 16, 2 << 16));
	FAIL_ON(!check_crtc_eq(&plane_state, 1022, 766, 2, 2));

	set_src(&plane_state, 0x20001, 0x20001, 0x4040001, 0x3040001);
	set_crtc(&plane_state, -2, -2, 1028, 772);
	ret = drm_atomic_helper_check_plane_state(&plane_state, &crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  0x10001,
						  false, false);
	FAIL(ret < 0, "Should succeed by clipping to exact multiple");
	FAIL_ON(!plane_state.visible);
	FAIL_ON(!check_src_eq(&plane_state, 0x40002, 0x40002, 1024 << 16, 768 << 16));
	FAIL_ON(!check_crtc_eq(&plane_state, 0, 0, 1024, 768));

	set_src(&plane_state, 0, 0, 0x3ffff, 0x3ffff);
	set_crtc(&plane_state, 1022, 766, 4, 4);
	ret = drm_atomic_helper_check_plane_state(&plane_state, &crtc_state,
						  0xffff,
						  DRM_PLANE_HELPER_NO_SCALING,
						  true, false);
	FAIL(ret < 0, "Should succeed by clipping to exact multiple");
	FAIL_ON(!plane_state.visible);
	/* Should not be rounded to 0x20001, which would be upscaling. */
	FAIL_ON(!check_src_eq(&plane_state, 0, 0, 2 << 16, 2 << 16));
	FAIL_ON(!check_crtc_eq(&plane_state, 1022, 766, 2, 2));

	set_src(&plane_state, 0x1ffff, 0x1ffff, 0x403ffff, 0x303ffff);
	set_crtc(&plane_state, -2, -2, 1028, 772);
	ret = drm_atomic_helper_check_plane_state(&plane_state, &crtc_state,
						  0xffff,
						  DRM_PLANE_HELPER_NO_SCALING,
						  false, false);
	FAIL(ret < 0, "Should succeed by clipping to exact multiple");
	FAIL_ON(!plane_state.visible);
	FAIL_ON(!check_src_eq(&plane_state, 0x3fffe, 0x3fffe, 1024 << 16, 768 << 16));
	FAIL_ON(!check_crtc_eq(&plane_state, 0, 0, 1024, 768));

	return 0;
}

#include "drm_selftest.c"

static int __init test_drm_helper_init(void)
{
	int err;

	err = run_selftests(selftests, ARRAY_SIZE(selftests), NULL);

	return err > 0 ? 0 : err;
}

module_init(test_drm_helper_init);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
