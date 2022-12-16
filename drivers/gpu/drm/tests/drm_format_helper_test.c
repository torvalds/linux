// SPDX-License-Identifier: GPL-2.0+

#include <kunit/test.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_mode.h>
#include <drm/drm_print.h>
#include <drm/drm_rect.h>

#include "../drm_crtc_internal.h"

#define TEST_BUF_SIZE 50

struct convert_to_gray8_result {
	unsigned int dst_pitch;
	const u8 expected[TEST_BUF_SIZE];
};

struct convert_to_rgb332_result {
	unsigned int dst_pitch;
	const u8 expected[TEST_BUF_SIZE];
};

struct convert_to_rgb565_result {
	unsigned int dst_pitch;
	const u16 expected[TEST_BUF_SIZE];
	const u16 expected_swab[TEST_BUF_SIZE];
};

struct convert_to_rgb888_result {
	unsigned int dst_pitch;
	const u8 expected[TEST_BUF_SIZE];
};

struct convert_to_xrgb2101010_result {
	unsigned int dst_pitch;
	const u32 expected[TEST_BUF_SIZE];
};

struct convert_xrgb8888_case {
	const char *name;
	unsigned int pitch;
	struct drm_rect clip;
	const u32 xrgb8888[TEST_BUF_SIZE];
	struct convert_to_gray8_result gray8_result;
	struct convert_to_rgb332_result rgb332_result;
	struct convert_to_rgb565_result rgb565_result;
	struct convert_to_rgb888_result rgb888_result;
	struct convert_to_xrgb2101010_result xrgb2101010_result;
};

static struct convert_xrgb8888_case convert_xrgb8888_cases[] = {
	{
		.name = "single_pixel_source_buffer",
		.pitch = 1 * 4,
		.clip = DRM_RECT_INIT(0, 0, 1, 1),
		.xrgb8888 = { 0x01FF0000 },
		.gray8_result = {
			.dst_pitch = 0,
			.expected = { 0x4C },
		},
		.rgb332_result = {
			.dst_pitch = 0,
			.expected = { 0xE0 },
		},
		.rgb565_result = {
			.dst_pitch = 0,
			.expected = { 0xF800 },
			.expected_swab = { 0x00F8 },
		},
		.rgb888_result = {
			.dst_pitch = 0,
			.expected = { 0x00, 0x00, 0xFF },
		},
		.xrgb2101010_result = {
			.dst_pitch = 0,
			.expected = { 0x3FF00000 },
		},
	},
	{
		.name = "single_pixel_clip_rectangle",
		.pitch = 2 * 4,
		.clip = DRM_RECT_INIT(1, 1, 1, 1),
		.xrgb8888 = {
			0x00000000, 0x00000000,
			0x00000000, 0x10FF0000,
		},
		.gray8_result = {
			.dst_pitch = 0,
			.expected = { 0x4C },
		},
		.rgb332_result = {
			.dst_pitch = 0,
			.expected = { 0xE0 },
		},
		.rgb565_result = {
			.dst_pitch = 0,
			.expected = { 0xF800 },
			.expected_swab = { 0x00F8 },
		},
		.rgb888_result = {
			.dst_pitch = 0,
			.expected = { 0x00, 0x00, 0xFF },
		},
		.xrgb2101010_result = {
			.dst_pitch = 0,
			.expected = { 0x3FF00000 },
		},
	},
	{
		/* Well known colors: White, black, red, green, blue, magenta,
		 * yellow and cyan. Different values for the X in XRGB8888 to
		 * make sure it is ignored. Partial clip area.
		 */
		.name = "well_known_colors",
		.pitch = 4 * 4,
		.clip = DRM_RECT_INIT(1, 1, 2, 4),
		.xrgb8888 = {
			0x00000000, 0x00000000, 0x00000000, 0x00000000,
			0x00000000, 0x11FFFFFF, 0x22000000, 0x00000000,
			0x00000000, 0x33FF0000, 0x4400FF00, 0x00000000,
			0x00000000, 0x550000FF, 0x66FF00FF, 0x00000000,
			0x00000000, 0x77FFFF00, 0x8800FFFF, 0x00000000,
		},
		.gray8_result = {
			.dst_pitch = 0,
			.expected = {
				0xFF, 0x00,
				0x4C, 0x99,
				0x19, 0x66,
				0xE5, 0xB2,
			},
		},
		.rgb332_result = {
			.dst_pitch = 0,
			.expected = {
				0xFF, 0x00,
				0xE0, 0x1C,
				0x03, 0xE3,
				0xFC, 0x1F,
			},
		},
		.rgb565_result = {
			.dst_pitch = 0,
			.expected = {
				0xFFFF, 0x0000,
				0xF800, 0x07E0,
				0x001F, 0xF81F,
				0xFFE0, 0x07FF,
			},
			.expected_swab = {
				0xFFFF, 0x0000,
				0x00F8, 0xE007,
				0x1F00, 0x1FF8,
				0xE0FF, 0xFF07,
			},
		},
		.rgb888_result = {
			.dst_pitch = 0,
			.expected = {
				0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
				0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00,
				0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF,
				0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
			},
		},
		.xrgb2101010_result = {
			.dst_pitch = 0,
			.expected = {
				0x3FFFFFFF, 0x00000000,
				0x3FF00000, 0x000FFC00,
				0x000003FF, 0x3FF003FF,
				0x3FFFFC00, 0x000FFFFF,
			},
		},
	},
	{
		/* Randomly picked colors. Full buffer within the clip area. */
		.name = "destination_pitch",
		.pitch = 3 * 4,
		.clip = DRM_RECT_INIT(0, 0, 3, 3),
		.xrgb8888 = {
			0xA10E449C, 0xB1114D05, 0xC1A80303,
			0xD16C7073, 0xA20E449C, 0xB2114D05,
			0xC2A80303, 0xD26C7073, 0xA30E449C,
		},
		.gray8_result = {
			.dst_pitch = 5,
			.expected = {
				0x3C, 0x33, 0x34, 0x00, 0x00,
				0x6F, 0x3C, 0x33, 0x00, 0x00,
				0x34, 0x6F, 0x3C, 0x00, 0x00,
			},
		},
		.rgb332_result = {
			.dst_pitch = 5,
			.expected = {
				0x0A, 0x08, 0xA0, 0x00, 0x00,
				0x6D, 0x0A, 0x08, 0x00, 0x00,
				0xA0, 0x6D, 0x0A, 0x00, 0x00,
			},
		},
		.rgb565_result = {
			.dst_pitch = 10,
			.expected = {
				0x0A33, 0x1260, 0xA800, 0x0000, 0x0000,
				0x6B8E, 0x0A33, 0x1260, 0x0000, 0x0000,
				0xA800, 0x6B8E, 0x0A33, 0x0000, 0x0000,
			},
			.expected_swab = {
				0x330A, 0x6012, 0x00A8, 0x0000, 0x0000,
				0x8E6B, 0x330A, 0x6012, 0x0000, 0x0000,
				0x00A8, 0x8E6B, 0x330A, 0x0000, 0x0000,
			},
		},
		.rgb888_result = {
			.dst_pitch = 15,
			.expected = {
				0x9C, 0x44, 0x0E, 0x05, 0x4D, 0x11, 0x03, 0x03, 0xA8,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x73, 0x70, 0x6C, 0x9C, 0x44, 0x0E, 0x05, 0x4D, 0x11,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x03, 0x03, 0xA8, 0x73, 0x70, 0x6C, 0x9C, 0x44, 0x0E,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			},
		},
		.xrgb2101010_result = {
			.dst_pitch = 20,
			.expected = {
				0x03844672, 0x0444D414, 0x2A20300C, 0x00000000, 0x00000000,
				0x1B1705CD, 0x03844672, 0x0444D414, 0x00000000, 0x00000000,
				0x2A20300C, 0x1B1705CD, 0x03844672, 0x00000000, 0x00000000,
			},
		},
	},
};

/*
 * conversion_buf_size - Return the destination buffer size required to convert
 * between formats.
 * @dst_format: destination buffer pixel format (DRM_FORMAT_*)
 * @dst_pitch: Number of bytes between two consecutive scanlines within dst
 * @clip: Clip rectangle area to convert
 *
 * Returns:
 * The size of the destination buffer or negative value on error.
 */
static size_t conversion_buf_size(u32 dst_format, unsigned int dst_pitch,
				  const struct drm_rect *clip)
{
	const struct drm_format_info *dst_fi = drm_format_info(dst_format);

	if (!dst_fi)
		return -EINVAL;

	if (!dst_pitch)
		dst_pitch = drm_rect_width(clip) * dst_fi->cpp[0];

	return dst_pitch * drm_rect_height(clip);
}

static u32 *le32buf_to_cpu(struct kunit *test, const u32 *buf, size_t buf_size)
{
	u32 *dst = NULL;
	int n;

	dst = kunit_kzalloc(test, sizeof(*dst) * buf_size, GFP_KERNEL);
	if (!dst)
		return NULL;

	for (n = 0; n < buf_size; n++)
		dst[n] = le32_to_cpu((__force __le32)buf[n]);

	return dst;
}

static void convert_xrgb8888_case_desc(struct convert_xrgb8888_case *t,
				       char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(convert_xrgb8888, convert_xrgb8888_cases,
		  convert_xrgb8888_case_desc);

static void drm_test_fb_xrgb8888_to_gray8(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_gray8_result *result = &params->gray8_result;
	size_t dst_size;
	__u8 *buf = NULL;
	__u32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_R8, result->dst_pitch,
				       &params->clip);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = le32buf_to_cpu(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	drm_fb_xrgb8888_to_gray8(&dst, &result->dst_pitch, &src, &fb, &params->clip);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_rgb332(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_rgb332_result *result = &params->rgb332_result;
	size_t dst_size;
	__u8 *buf = NULL;
	__u32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_RGB332, result->dst_pitch,
				       &params->clip);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = le32buf_to_cpu(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	drm_fb_xrgb8888_to_rgb332(&dst, &result->dst_pitch, &src, &fb, &params->clip);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_rgb565(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_rgb565_result *result = &params->rgb565_result;
	size_t dst_size;
	__u16 *buf = NULL;
	__u32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_RGB565, result->dst_pitch,
				       &params->clip);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = le32buf_to_cpu(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	drm_fb_xrgb8888_to_rgb565(&dst, &result->dst_pitch, &src, &fb, &params->clip, false);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);

	drm_fb_xrgb8888_to_rgb565(&dst, &result->dst_pitch, &src, &fb, &params->clip, true);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected_swab, dst_size);
}

static void drm_test_fb_xrgb8888_to_rgb888(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_rgb888_result *result = &params->rgb888_result;
	size_t dst_size;
	__u8 *buf = NULL;
	__u32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_RGB888, result->dst_pitch,
				       &params->clip);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = le32buf_to_cpu(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	drm_fb_xrgb8888_to_rgb888(&dst, &result->dst_pitch, &src, &fb, &params->clip);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_xrgb2101010(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_xrgb2101010_result *result = &params->xrgb2101010_result;
	size_t dst_size;
	__u32 *buf = NULL;
	__u32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_XRGB2101010,
				       result->dst_pitch, &params->clip);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = le32buf_to_cpu(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	drm_fb_xrgb8888_to_xrgb2101010(&dst, &result->dst_pitch, &src, &fb, &params->clip);
	buf = le32buf_to_cpu(test, buf, dst_size / sizeof(u32));
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static struct kunit_case drm_format_helper_test_cases[] = {
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_gray8, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_rgb332, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_rgb565, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_rgb888, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_xrgb2101010, convert_xrgb8888_gen_params),
	{}
};

static struct kunit_suite drm_format_helper_test_suite = {
	.name = "drm_format_helper_test",
	.test_cases = drm_format_helper_test_cases,
};

kunit_test_suite(drm_format_helper_test_suite);

MODULE_DESCRIPTION("KUnit tests for the drm_format_helper APIs");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("José Expósito <jose.exposito89@gmail.com>");
