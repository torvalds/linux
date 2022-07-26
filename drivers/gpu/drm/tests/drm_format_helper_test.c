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

struct convert_to_rgb332_result {
	unsigned int dst_pitch;
	const u8 expected[TEST_BUF_SIZE];
};

struct convert_xrgb8888_case {
	const char *name;
	unsigned int pitch;
	struct drm_rect clip;
	const u32 xrgb8888[TEST_BUF_SIZE];
	struct convert_to_rgb332_result rgb332_result;
};

static struct convert_xrgb8888_case convert_xrgb8888_cases[] = {
	{
		.name = "single_pixel_source_buffer",
		.pitch = 1 * 4,
		.clip = DRM_RECT_INIT(0, 0, 1, 1),
		.xrgb8888 = { 0x01FF0000 },
		.rgb332_result = {
			.dst_pitch = 0,
			.expected = { 0xE0 },
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
		.rgb332_result = {
			.dst_pitch = 0,
			.expected = { 0xE0 },
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
		.rgb332_result = {
			.dst_pitch = 0,
			.expected = {
				0xFF, 0x00,
				0xE0, 0x1C,
				0x03, 0xE3,
				0xFC, 0x1F,
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
		.rgb332_result = {
			.dst_pitch = 5,
			.expected = {
				0x0A, 0x08, 0xA0, 0x00, 0x00,
				0x6D, 0x0A, 0x08, 0x00, 0x00,
				0xA0, 0x6D, 0x0A, 0x00, 0x00,
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

static void xrgb8888_to_rgb332_test(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_rgb332_result *result = &params->rgb332_result;
	size_t dst_size;
	__u8 *dst = NULL;
	__u32 *src = NULL;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_RGB332, result->dst_pitch,
				       &params->clip);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	dst = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dst);

	src = le32buf_to_cpu(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, src);

	drm_fb_xrgb8888_to_rgb332(dst, result->dst_pitch, src, &fb,
				  &params->clip);
	KUNIT_EXPECT_EQ(test, memcmp(dst, result->expected, dst_size), 0);
}

static struct kunit_case drm_format_helper_test_cases[] = {
	KUNIT_CASE_PARAM(xrgb8888_to_rgb332_test, convert_xrgb8888_gen_params),
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
