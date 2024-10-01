// SPDX-License-Identifier: GPL-2.0+

#include <kunit/test.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_mode.h>
#include <drm/drm_print.h>
#include <drm/drm_rect.h>

#include "../drm_crtc_internal.h"

#define TEST_BUF_SIZE 50

#define TEST_USE_DEFAULT_PITCH 0

static unsigned char fmtcnv_state_mem[PAGE_SIZE];
static struct drm_format_conv_state fmtcnv_state =
	DRM_FORMAT_CONV_STATE_INIT_PREALLOCATED(fmtcnv_state_mem, sizeof(fmtcnv_state_mem));

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

struct convert_to_xrgb1555_result {
	unsigned int dst_pitch;
	const u16 expected[TEST_BUF_SIZE];
};

struct convert_to_argb1555_result {
	unsigned int dst_pitch;
	const u16 expected[TEST_BUF_SIZE];
};

struct convert_to_rgba5551_result {
	unsigned int dst_pitch;
	const u16 expected[TEST_BUF_SIZE];
};

struct convert_to_rgb888_result {
	unsigned int dst_pitch;
	const u8 expected[TEST_BUF_SIZE];
};

struct convert_to_argb8888_result {
	unsigned int dst_pitch;
	const u32 expected[TEST_BUF_SIZE];
};

struct convert_to_xrgb2101010_result {
	unsigned int dst_pitch;
	const u32 expected[TEST_BUF_SIZE];
};

struct convert_to_argb2101010_result {
	unsigned int dst_pitch;
	const u32 expected[TEST_BUF_SIZE];
};

struct convert_to_mono_result {
	unsigned int dst_pitch;
	const u8 expected[TEST_BUF_SIZE];
};

struct fb_swab_result {
	unsigned int dst_pitch;
	const u32 expected[TEST_BUF_SIZE];
};

struct convert_to_xbgr8888_result {
	unsigned int dst_pitch;
	const u32 expected[TEST_BUF_SIZE];
};

struct convert_to_abgr8888_result {
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
	struct convert_to_xrgb1555_result xrgb1555_result;
	struct convert_to_argb1555_result argb1555_result;
	struct convert_to_rgba5551_result rgba5551_result;
	struct convert_to_rgb888_result rgb888_result;
	struct convert_to_argb8888_result argb8888_result;
	struct convert_to_xrgb2101010_result xrgb2101010_result;
	struct convert_to_argb2101010_result argb2101010_result;
	struct convert_to_mono_result mono_result;
	struct fb_swab_result swab_result;
	struct convert_to_xbgr8888_result xbgr8888_result;
	struct convert_to_abgr8888_result abgr8888_result;
};

static struct convert_xrgb8888_case convert_xrgb8888_cases[] = {
	{
		.name = "single_pixel_source_buffer",
		.pitch = 1 * 4,
		.clip = DRM_RECT_INIT(0, 0, 1, 1),
		.xrgb8888 = { 0x01FF0000 },
		.gray8_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0x4C },
		},
		.rgb332_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0xE0 },
		},
		.rgb565_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0xF800 },
			.expected_swab = { 0x00F8 },
		},
		.xrgb1555_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0x7C00 },
		},
		.argb1555_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0xFC00 },
		},
		.rgba5551_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0xF801 },
		},
		.rgb888_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0x00, 0x00, 0xFF },
		},
		.argb8888_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0xFFFF0000 },
		},
		.xrgb2101010_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0x3FF00000 },
		},
		.argb2101010_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0xFFF00000 },
		},
		.mono_result = {
			.dst_pitch =  TEST_USE_DEFAULT_PITCH,
			.expected = { 0b0 },
		},
		.swab_result = {
			.dst_pitch =  TEST_USE_DEFAULT_PITCH,
			.expected = { 0x0000FF01 },
		},
		.xbgr8888_result = {
			.dst_pitch =  TEST_USE_DEFAULT_PITCH,
			.expected = { 0x010000FF },
		},
		.abgr8888_result = {
			.dst_pitch =  TEST_USE_DEFAULT_PITCH,
			.expected = { 0xFF0000FF },
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
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0x4C },
		},
		.rgb332_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0xE0 },
		},
		.rgb565_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0xF800 },
			.expected_swab = { 0x00F8 },
		},
		.xrgb1555_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0x7C00 },
		},
		.argb1555_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0xFC00 },
		},
		.rgba5551_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0xF801 },
		},
		.rgb888_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0x00, 0x00, 0xFF },
		},
		.argb8888_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0xFFFF0000 },
		},
		.xrgb2101010_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0x3FF00000 },
		},
		.argb2101010_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0xFFF00000 },
		},
		.mono_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = { 0b0 },
		},
		.swab_result = {
			.dst_pitch =  TEST_USE_DEFAULT_PITCH,
			.expected = { 0x0000FF10 },
		},
		.xbgr8888_result = {
			.dst_pitch =  TEST_USE_DEFAULT_PITCH,
			.expected = { 0x100000FF },
		},
		.abgr8888_result = {
			.dst_pitch =  TEST_USE_DEFAULT_PITCH,
			.expected = { 0xFF0000FF },
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
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = {
				0xFF, 0x00,
				0x4C, 0x99,
				0x19, 0x66,
				0xE5, 0xB2,
			},
		},
		.rgb332_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = {
				0xFF, 0x00,
				0xE0, 0x1C,
				0x03, 0xE3,
				0xFC, 0x1F,
			},
		},
		.rgb565_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
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
		.xrgb1555_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = {
				0x7FFF, 0x0000,
				0x7C00, 0x03E0,
				0x001F, 0x7C1F,
				0x7FE0, 0x03FF,
			},
		},
		.argb1555_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = {
				0xFFFF, 0x8000,
				0xFC00, 0x83E0,
				0x801F, 0xFC1F,
				0xFFE0, 0x83FF,
			},
		},
		.rgba5551_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = {
				0xFFFF, 0x0001,
				0xF801, 0x07C1,
				0x003F, 0xF83F,
				0xFFC1, 0x07FF,
			},
		},
		.rgb888_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = {
				0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
				0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00,
				0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF,
				0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
			},
		},
		.argb8888_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = {
				0xFFFFFFFF, 0xFF000000,
				0xFFFF0000, 0xFF00FF00,
				0xFF0000FF, 0xFFFF00FF,
				0xFFFFFF00, 0xFF00FFFF,
			},
		},
		.xrgb2101010_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = {
				0x3FFFFFFF, 0x00000000,
				0x3FF00000, 0x000FFC00,
				0x000003FF, 0x3FF003FF,
				0x3FFFFC00, 0x000FFFFF,
			},
		},
		.argb2101010_result = {
			.dst_pitch = TEST_USE_DEFAULT_PITCH,
			.expected = {
				0xFFFFFFFF, 0xC0000000,
				0xFFF00000, 0xC00FFC00,
				0xC00003FF, 0xFFF003FF,
				0xFFFFFC00, 0xC00FFFFF,
			},
		},
		.mono_result = {
			.dst_pitch =  TEST_USE_DEFAULT_PITCH,
			.expected = {
				0b01,
				0b10,
				0b00,
				0b11,
			},
		},
		.swab_result = {
			.dst_pitch =  TEST_USE_DEFAULT_PITCH,
			.expected = {
				0xFFFFFF11, 0x00000022,
				0x0000FF33, 0x00FF0044,
				0xFF000055, 0xFF00FF66,
				0x00FFFF77, 0xFFFF0088,
			},
		},
		.xbgr8888_result = {
			.dst_pitch =  TEST_USE_DEFAULT_PITCH,
			.expected = {
				0x11FFFFFF, 0x22000000,
				0x330000FF, 0x4400FF00,
				0x55FF0000, 0x66FF00FF,
				0x7700FFFF, 0x88FFFF00,
			},
		},
		.abgr8888_result = {
			.dst_pitch =  TEST_USE_DEFAULT_PITCH,
			.expected = {
				0xFFFFFFFF, 0xFF000000,
				0xFF0000FF, 0xFF00FF00,
				0xFFFF0000, 0xFFFF00FF,
				0xFF00FFFF, 0xFFFFFF00,
			},
		},
	},
	{
		/* Randomly picked colors. Full buffer within the clip area. */
		.name = "destination_pitch",
		.pitch = 3 * 4,
		.clip = DRM_RECT_INIT(0, 0, 3, 3),
		.xrgb8888 = {
			0xA10E449C, 0xB1114D05, 0xC1A8F303,
			0xD16CF073, 0xA20E449C, 0xB2114D05,
			0xC2A80303, 0xD26CF073, 0xA30E449C,
		},
		.gray8_result = {
			.dst_pitch = 5,
			.expected = {
				0x3C, 0x33, 0xC4, 0x00, 0x00,
				0xBB, 0x3C, 0x33, 0x00, 0x00,
				0x34, 0xBB, 0x3C, 0x00, 0x00,
			},
		},
		.rgb332_result = {
			.dst_pitch = 5,
			.expected = {
				0x0A, 0x08, 0xBC, 0x00, 0x00,
				0x7D, 0x0A, 0x08, 0x00, 0x00,
				0xA0, 0x7D, 0x0A, 0x00, 0x00,
			},
		},
		.rgb565_result = {
			.dst_pitch = 10,
			.expected = {
				0x0A33, 0x1260, 0xAF80, 0x0000, 0x0000,
				0x6F8E, 0x0A33, 0x1260, 0x0000, 0x0000,
				0xA800, 0x6F8E, 0x0A33, 0x0000, 0x0000,
			},
			.expected_swab = {
				0x330A, 0x6012, 0x80AF, 0x0000, 0x0000,
				0x8E6F, 0x330A, 0x6012, 0x0000, 0x0000,
				0x00A8, 0x8E6F, 0x330A, 0x0000, 0x0000,
			},
		},
		.xrgb1555_result = {
			.dst_pitch = 10,
			.expected = {
				0x0513, 0x0920, 0x57C0, 0x0000, 0x0000,
				0x37CE, 0x0513, 0x0920, 0x0000, 0x0000,
				0x5400, 0x37CE, 0x0513, 0x0000, 0x0000,
			},
		},
		.argb1555_result = {
			.dst_pitch = 10,
			.expected = {
				0x8513, 0x8920, 0xD7C0, 0x0000, 0x0000,
				0xB7CE, 0x8513, 0x8920, 0x0000, 0x0000,
				0xD400, 0xB7CE, 0x8513, 0x0000, 0x0000,
			},
		},
		.rgba5551_result = {
			.dst_pitch = 10,
			.expected = {
				0x0A27, 0x1241, 0xAF81, 0x0000, 0x0000,
				0x6F9D, 0x0A27, 0x1241, 0x0000, 0x0000,
				0xA801, 0x6F9D, 0x0A27, 0x0000, 0x0000,
			},
		},
		.rgb888_result = {
			.dst_pitch = 15,
			.expected = {
				0x9C, 0x44, 0x0E, 0x05, 0x4D, 0x11, 0x03, 0xF3, 0xA8,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x73, 0xF0, 0x6C, 0x9C, 0x44, 0x0E, 0x05, 0x4D, 0x11,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x03, 0x03, 0xA8, 0x73, 0xF0, 0x6C, 0x9C, 0x44, 0x0E,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			},
		},
		.argb8888_result = {
			.dst_pitch = 20,
			.expected = {
				0xFF0E449C, 0xFF114D05, 0xFFA8F303, 0x00000000, 0x00000000,
				0xFF6CF073, 0xFF0E449C, 0xFF114D05, 0x00000000, 0x00000000,
				0xFFA80303, 0xFF6CF073, 0xFF0E449C, 0x00000000, 0x00000000,
			},
		},
		.xrgb2101010_result = {
			.dst_pitch = 20,
			.expected = {
				0x03844672, 0x0444D414, 0x2A2F3C0C, 0x00000000, 0x00000000,
				0x1B1F0DCD, 0x03844672, 0x0444D414, 0x00000000, 0x00000000,
				0x2A20300C, 0x1B1F0DCD, 0x03844672, 0x00000000, 0x00000000,
			},
		},
		.argb2101010_result = {
			.dst_pitch = 20,
			.expected = {
				0xC3844672, 0xC444D414, 0xEA2F3C0C, 0x00000000, 0x00000000,
				0xDB1F0DCD, 0xC3844672, 0xC444D414, 0x00000000, 0x00000000,
				0xEA20300C, 0xDB1F0DCD, 0xC3844672, 0x00000000, 0x00000000,
			},
		},
		.mono_result = {
			.dst_pitch = 2,
			.expected = {
				0b100, 0b000,
				0b001, 0b000,
				0b010, 0b000,
			},
		},
		.swab_result = {
			.dst_pitch =  20,
			.expected = {
				0x9C440EA1, 0x054D11B1, 0x03F3A8C1, 0x00000000, 0x00000000,
				0x73F06CD1, 0x9C440EA2, 0x054D11B2, 0x00000000, 0x00000000,
				0x0303A8C2, 0x73F06CD2, 0x9C440EA3, 0x00000000, 0x00000000,
			},
		},
		.xbgr8888_result = {
			.dst_pitch =  20,
			.expected = {
				0xA19C440E, 0xB1054D11, 0xC103F3A8, 0x00000000, 0x00000000,
				0xD173F06C, 0xA29C440E, 0xB2054D11, 0x00000000, 0x00000000,
				0xC20303A8, 0xD273F06C, 0xA39C440E, 0x00000000, 0x00000000,
			},
		},
		.abgr8888_result = {
			.dst_pitch =  20,
			.expected = {
				0xFF9C440E, 0xFF054D11, 0xFF03F3A8, 0x00000000, 0x00000000,
				0xFF73F06C, 0xFF9C440E, 0xFF054D11, 0x00000000, 0x00000000,
				0xFF0303A8, 0xFF73F06C, 0xFF9C440E, 0x00000000, 0x00000000,
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
				  const struct drm_rect *clip, int plane)
{
	const struct drm_format_info *dst_fi = drm_format_info(dst_format);

	if (!dst_fi)
		return -EINVAL;

	if (!dst_pitch)
		dst_pitch = drm_format_info_min_pitch(dst_fi, plane, drm_rect_width(clip));

	return dst_pitch * drm_rect_height(clip);
}

static u16 *le16buf_to_cpu(struct kunit *test, const __le16 *buf, size_t buf_size)
{
	u16 *dst = NULL;
	int n;

	dst = kunit_kzalloc(test, sizeof(*dst) * buf_size, GFP_KERNEL);
	if (!dst)
		return NULL;

	for (n = 0; n < buf_size; n++)
		dst[n] = le16_to_cpu(buf[n]);

	return dst;
}

static u32 *le32buf_to_cpu(struct kunit *test, const __le32 *buf, size_t buf_size)
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

static __le32 *cpubuf_to_le32(struct kunit *test, const u32 *buf, size_t buf_size)
{
	__le32 *dst = NULL;
	int n;

	dst = kunit_kzalloc(test, sizeof(*dst) * buf_size, GFP_KERNEL);
	if (!dst)
		return NULL;

	for (n = 0; n < buf_size; n++)
		dst[n] = cpu_to_le32(buf[n]);

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
	u8 *buf = NULL;
	__le32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_R8, result->dst_pitch,
				       &params->clip, 0);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = cpubuf_to_le32(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	const unsigned int *dst_pitch = (result->dst_pitch == TEST_USE_DEFAULT_PITCH) ?
		NULL : &result->dst_pitch;

	drm_fb_xrgb8888_to_gray8(&dst, dst_pitch, &src, &fb, &params->clip, &fmtcnv_state);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_rgb332(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_rgb332_result *result = &params->rgb332_result;
	size_t dst_size;
	u8 *buf = NULL;
	__le32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_RGB332, result->dst_pitch,
				       &params->clip, 0);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = cpubuf_to_le32(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	const unsigned int *dst_pitch = (result->dst_pitch == TEST_USE_DEFAULT_PITCH) ?
		NULL : &result->dst_pitch;

	drm_fb_xrgb8888_to_rgb332(&dst, dst_pitch, &src, &fb, &params->clip, &fmtcnv_state);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_rgb565(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_rgb565_result *result = &params->rgb565_result;
	size_t dst_size;
	u16 *buf = NULL;
	__le32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_RGB565, result->dst_pitch,
				       &params->clip, 0);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = cpubuf_to_le32(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	const unsigned int *dst_pitch = (result->dst_pitch == TEST_USE_DEFAULT_PITCH) ?
		NULL : &result->dst_pitch;

	drm_fb_xrgb8888_to_rgb565(&dst, dst_pitch, &src, &fb, &params->clip,
				  &fmtcnv_state, false);
	buf = le16buf_to_cpu(test, (__force const __le16 *)buf, dst_size / sizeof(__le16));
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);

	buf = dst.vaddr; /* restore original value of buf */
	drm_fb_xrgb8888_to_rgb565(&dst, &result->dst_pitch, &src, &fb, &params->clip,
				  &fmtcnv_state, true);
	buf = le16buf_to_cpu(test, (__force const __le16 *)buf, dst_size / sizeof(__le16));
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected_swab, dst_size);

	buf = dst.vaddr;
	memset(buf, 0, dst_size);

	int blit_result = 0;

	blit_result = drm_fb_blit(&dst, dst_pitch, DRM_FORMAT_RGB565, &src, &fb, &params->clip,
				  &fmtcnv_state);

	buf = le16buf_to_cpu(test, (__force const __le16 *)buf, dst_size / sizeof(__le16));

	KUNIT_EXPECT_FALSE(test, blit_result);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_xrgb1555(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_xrgb1555_result *result = &params->xrgb1555_result;
	size_t dst_size;
	u16 *buf = NULL;
	__le32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_XRGB1555, result->dst_pitch,
				       &params->clip, 0);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = cpubuf_to_le32(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	const unsigned int *dst_pitch = (result->dst_pitch == TEST_USE_DEFAULT_PITCH) ?
		NULL : &result->dst_pitch;

	drm_fb_xrgb8888_to_xrgb1555(&dst, dst_pitch, &src, &fb, &params->clip, &fmtcnv_state);
	buf = le16buf_to_cpu(test, (__force const __le16 *)buf, dst_size / sizeof(__le16));
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);

	buf = dst.vaddr; /* restore original value of buf */
	memset(buf, 0, dst_size);

	int blit_result = 0;

	blit_result = drm_fb_blit(&dst, dst_pitch, DRM_FORMAT_XRGB1555, &src, &fb, &params->clip,
				  &fmtcnv_state);

	buf = le16buf_to_cpu(test, (__force const __le16 *)buf, dst_size / sizeof(__le16));

	KUNIT_EXPECT_FALSE(test, blit_result);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_argb1555(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_argb1555_result *result = &params->argb1555_result;
	size_t dst_size;
	u16 *buf = NULL;
	__le32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_ARGB1555, result->dst_pitch,
				       &params->clip, 0);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = cpubuf_to_le32(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	const unsigned int *dst_pitch = (result->dst_pitch == TEST_USE_DEFAULT_PITCH) ?
		NULL : &result->dst_pitch;

	drm_fb_xrgb8888_to_argb1555(&dst, dst_pitch, &src, &fb, &params->clip, &fmtcnv_state);
	buf = le16buf_to_cpu(test, (__force const __le16 *)buf, dst_size / sizeof(__le16));
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);

	buf = dst.vaddr; /* restore original value of buf */
	memset(buf, 0, dst_size);

	int blit_result = 0;

	blit_result = drm_fb_blit(&dst, dst_pitch, DRM_FORMAT_ARGB1555, &src, &fb, &params->clip,
				  &fmtcnv_state);

	buf = le16buf_to_cpu(test, (__force const __le16 *)buf, dst_size / sizeof(__le16));

	KUNIT_EXPECT_FALSE(test, blit_result);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_rgba5551(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_rgba5551_result *result = &params->rgba5551_result;
	size_t dst_size;
	u16 *buf = NULL;
	__le32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_RGBA5551, result->dst_pitch,
				       &params->clip, 0);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = cpubuf_to_le32(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	const unsigned int *dst_pitch = (result->dst_pitch == TEST_USE_DEFAULT_PITCH) ?
		NULL : &result->dst_pitch;

	drm_fb_xrgb8888_to_rgba5551(&dst, dst_pitch, &src, &fb, &params->clip, &fmtcnv_state);
	buf = le16buf_to_cpu(test, (__force const __le16 *)buf, dst_size / sizeof(__le16));
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);

	buf = dst.vaddr; /* restore original value of buf */
	memset(buf, 0, dst_size);

	int blit_result = 0;

	blit_result = drm_fb_blit(&dst, dst_pitch, DRM_FORMAT_RGBA5551, &src, &fb, &params->clip,
				  &fmtcnv_state);

	buf = le16buf_to_cpu(test, (__force const __le16 *)buf, dst_size / sizeof(__le16));

	KUNIT_EXPECT_FALSE(test, blit_result);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_rgb888(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_rgb888_result *result = &params->rgb888_result;
	size_t dst_size;
	u8 *buf = NULL;
	__le32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_RGB888, result->dst_pitch,
				       &params->clip, 0);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = cpubuf_to_le32(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	/*
	 * RGB888 expected results are already in little-endian
	 * order, so there's no need to convert the test output.
	 */
	const unsigned int *dst_pitch = (result->dst_pitch == TEST_USE_DEFAULT_PITCH) ?
		NULL : &result->dst_pitch;

	drm_fb_xrgb8888_to_rgb888(&dst, dst_pitch, &src, &fb, &params->clip, &fmtcnv_state);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);

	buf = dst.vaddr; /* restore original value of buf */
	memset(buf, 0, dst_size);

	int blit_result = 0;

	blit_result = drm_fb_blit(&dst, dst_pitch, DRM_FORMAT_RGB888, &src, &fb, &params->clip,
				  &fmtcnv_state);

	KUNIT_EXPECT_FALSE(test, blit_result);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_argb8888(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_argb8888_result *result = &params->argb8888_result;
	size_t dst_size;
	u32 *buf = NULL;
	__le32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_ARGB8888,
				       result->dst_pitch, &params->clip, 0);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = cpubuf_to_le32(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	const unsigned int *dst_pitch = (result->dst_pitch == TEST_USE_DEFAULT_PITCH) ?
		NULL : &result->dst_pitch;

	drm_fb_xrgb8888_to_argb8888(&dst, dst_pitch, &src, &fb, &params->clip, &fmtcnv_state);
	buf = le32buf_to_cpu(test, (__force const __le32 *)buf, dst_size / sizeof(u32));
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);

	buf = dst.vaddr; /* restore original value of buf */
	memset(buf, 0, dst_size);

	int blit_result = 0;

	blit_result = drm_fb_blit(&dst, dst_pitch, DRM_FORMAT_ARGB8888, &src, &fb, &params->clip,
				  &fmtcnv_state);

	buf = le32buf_to_cpu(test, (__force const __le32 *)buf, dst_size / sizeof(u32));

	KUNIT_EXPECT_FALSE(test, blit_result);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_xrgb2101010(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_xrgb2101010_result *result = &params->xrgb2101010_result;
	size_t dst_size;
	u32 *buf = NULL;
	__le32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_XRGB2101010,
				       result->dst_pitch, &params->clip, 0);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = cpubuf_to_le32(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	const unsigned int *dst_pitch = (result->dst_pitch == TEST_USE_DEFAULT_PITCH) ?
		NULL : &result->dst_pitch;

	drm_fb_xrgb8888_to_xrgb2101010(&dst, dst_pitch, &src, &fb, &params->clip, &fmtcnv_state);
	buf = le32buf_to_cpu(test, buf, dst_size / sizeof(u32));
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);

	buf = dst.vaddr; /* restore original value of buf */
	memset(buf, 0, dst_size);

	int blit_result = 0;

	blit_result = drm_fb_blit(&dst, dst_pitch, DRM_FORMAT_XRGB2101010, &src, &fb,
				  &params->clip, &fmtcnv_state);

	KUNIT_EXPECT_FALSE(test, blit_result);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_argb2101010(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_argb2101010_result *result = &params->argb2101010_result;
	size_t dst_size;
	u32 *buf = NULL;
	__le32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_ARGB2101010,
				       result->dst_pitch, &params->clip, 0);
	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = cpubuf_to_le32(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	const unsigned int *dst_pitch = (result->dst_pitch == TEST_USE_DEFAULT_PITCH) ?
		NULL : &result->dst_pitch;

	drm_fb_xrgb8888_to_argb2101010(&dst, dst_pitch, &src, &fb, &params->clip, &fmtcnv_state);
	buf = le32buf_to_cpu(test, (__force const __le32 *)buf, dst_size / sizeof(u32));
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);

	buf = dst.vaddr; /* restore original value of buf */
	memset(buf, 0, dst_size);

	int blit_result = 0;

	blit_result = drm_fb_blit(&dst, dst_pitch, DRM_FORMAT_ARGB2101010, &src, &fb,
				  &params->clip, &fmtcnv_state);

	buf = le32buf_to_cpu(test, (__force const __le32 *)buf, dst_size / sizeof(u32));

	KUNIT_EXPECT_FALSE(test, blit_result);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_mono(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_mono_result *result = &params->mono_result;
	size_t dst_size;
	u8 *buf = NULL;
	__le32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_C1, result->dst_pitch, &params->clip, 0);

	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = cpubuf_to_le32(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	const unsigned int *dst_pitch = (result->dst_pitch == TEST_USE_DEFAULT_PITCH) ?
		NULL : &result->dst_pitch;

	drm_fb_xrgb8888_to_mono(&dst, dst_pitch, &src, &fb, &params->clip, &fmtcnv_state);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_swab(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct fb_swab_result *result = &params->swab_result;
	size_t dst_size;
	u32 *buf = NULL;
	__le32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_XRGB8888, result->dst_pitch, &params->clip, 0);

	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = cpubuf_to_le32(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	const unsigned int *dst_pitch = (result->dst_pitch == TEST_USE_DEFAULT_PITCH) ?
		NULL : &result->dst_pitch;

	drm_fb_swab(&dst, dst_pitch, &src, &fb, &params->clip, false, &fmtcnv_state);
	buf = le32buf_to_cpu(test, (__force const __le32 *)buf, dst_size / sizeof(u32));
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);

	buf = dst.vaddr; /* restore original value of buf */
	memset(buf, 0, dst_size);

	int blit_result;

	blit_result = drm_fb_blit(&dst, dst_pitch, DRM_FORMAT_XRGB8888 | DRM_FORMAT_BIG_ENDIAN,
				  &src, &fb, &params->clip, &fmtcnv_state);
	buf = le32buf_to_cpu(test, (__force const __le32 *)buf, dst_size / sizeof(u32));

	KUNIT_EXPECT_FALSE(test, blit_result);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);

	buf = dst.vaddr;
	memset(buf, 0, dst_size);

	blit_result = drm_fb_blit(&dst, dst_pitch, DRM_FORMAT_BGRX8888, &src, &fb, &params->clip,
				  &fmtcnv_state);
	buf = le32buf_to_cpu(test, (__force const __le32 *)buf, dst_size / sizeof(u32));

	KUNIT_EXPECT_FALSE(test, blit_result);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);

	buf = dst.vaddr;
	memset(buf, 0, dst_size);

	struct drm_format_info mock_format = *fb.format;

	mock_format.format |= DRM_FORMAT_BIG_ENDIAN;
	fb.format = &mock_format;

	blit_result = drm_fb_blit(&dst, dst_pitch, DRM_FORMAT_XRGB8888, &src, &fb, &params->clip,
				  &fmtcnv_state);
	buf = le32buf_to_cpu(test, (__force const __le32 *)buf, dst_size / sizeof(u32));

	KUNIT_EXPECT_FALSE(test, blit_result);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_abgr8888(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_abgr8888_result *result = &params->abgr8888_result;
	size_t dst_size;
	u32 *buf = NULL;
	__le32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_XBGR8888, result->dst_pitch, &params->clip, 0);

	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = cpubuf_to_le32(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	const unsigned int *dst_pitch = (result->dst_pitch == TEST_USE_DEFAULT_PITCH) ?
		NULL : &result->dst_pitch;

	int blit_result = 0;

	blit_result = drm_fb_blit(&dst, dst_pitch, DRM_FORMAT_ABGR8888, &src, &fb, &params->clip,
				  &fmtcnv_state);

	buf = le32buf_to_cpu(test, (__force const __le32 *)buf, dst_size / sizeof(u32));

	KUNIT_EXPECT_FALSE(test, blit_result);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

static void drm_test_fb_xrgb8888_to_xbgr8888(struct kunit *test)
{
	const struct convert_xrgb8888_case *params = test->param_value;
	const struct convert_to_xbgr8888_result *result = &params->xbgr8888_result;
	size_t dst_size;
	u32 *buf = NULL;
	__le32 *xrgb8888 = NULL;
	struct iosys_map dst, src;

	struct drm_framebuffer fb = {
		.format = drm_format_info(DRM_FORMAT_XRGB8888),
		.pitches = { params->pitch, 0, 0 },
	};

	dst_size = conversion_buf_size(DRM_FORMAT_XBGR8888, result->dst_pitch, &params->clip, 0);

	KUNIT_ASSERT_GT(test, dst_size, 0);

	buf = kunit_kzalloc(test, dst_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	iosys_map_set_vaddr(&dst, buf);

	xrgb8888 = cpubuf_to_le32(test, params->xrgb8888, TEST_BUF_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xrgb8888);
	iosys_map_set_vaddr(&src, xrgb8888);

	const unsigned int *dst_pitch = (result->dst_pitch == TEST_USE_DEFAULT_PITCH) ?
		NULL : &result->dst_pitch;

	int blit_result = 0;

	blit_result = drm_fb_blit(&dst, dst_pitch, DRM_FORMAT_XBGR8888, &src, &fb, &params->clip,
				  &fmtcnv_state);

	buf = le32buf_to_cpu(test, (__force const __le32 *)buf, dst_size / sizeof(u32));

	KUNIT_EXPECT_FALSE(test, blit_result);
	KUNIT_EXPECT_MEMEQ(test, buf, result->expected, dst_size);
}

struct clip_offset_case {
	const char *name;
	unsigned int pitch;
	u32 format;
	struct drm_rect clip;
	unsigned int expected_offset;
};

static struct clip_offset_case clip_offset_cases[] = {
	{
		.name = "pass through",
		.pitch = TEST_USE_DEFAULT_PITCH,
		.format = DRM_FORMAT_XRGB8888,
		.clip = DRM_RECT_INIT(0, 0, 3, 3),
		.expected_offset = 0
	},
	{
		.name = "horizontal offset",
		.pitch = TEST_USE_DEFAULT_PITCH,
		.format = DRM_FORMAT_XRGB8888,
		.clip = DRM_RECT_INIT(1, 0, 3, 3),
		.expected_offset = 4,
	},
	{
		.name = "vertical offset",
		.pitch = TEST_USE_DEFAULT_PITCH,
		.format = DRM_FORMAT_XRGB8888,
		.clip = DRM_RECT_INIT(0, 1, 3, 3),
		.expected_offset = 12,
	},
	{
		.name = "horizontal and vertical offset",
		.pitch = TEST_USE_DEFAULT_PITCH,
		.format = DRM_FORMAT_XRGB8888,
		.clip = DRM_RECT_INIT(1, 1, 3, 3),
		.expected_offset = 16,
	},
	{
		.name = "horizontal offset (custom pitch)",
		.pitch = 20,
		.format = DRM_FORMAT_XRGB8888,
		.clip = DRM_RECT_INIT(1, 0, 3, 3),
		.expected_offset = 4,
	},
	{
		.name = "vertical offset (custom pitch)",
		.pitch = 20,
		.format = DRM_FORMAT_XRGB8888,
		.clip = DRM_RECT_INIT(0, 1, 3, 3),
		.expected_offset = 20,
	},
	{
		.name = "horizontal and vertical offset (custom pitch)",
		.pitch = 20,
		.format = DRM_FORMAT_XRGB8888,
		.clip = DRM_RECT_INIT(1, 1, 3, 3),
		.expected_offset = 24,
	},
};

static void clip_offset_case_desc(struct clip_offset_case *t, char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(clip_offset, clip_offset_cases, clip_offset_case_desc);

static void drm_test_fb_clip_offset(struct kunit *test)
{
	const struct clip_offset_case *params = test->param_value;
	const struct drm_format_info *format_info = drm_format_info(params->format);

	unsigned int offset;
	unsigned int pitch = params->pitch;

	if (pitch == TEST_USE_DEFAULT_PITCH)
		pitch = drm_format_info_min_pitch(format_info, 0,
						  drm_rect_width(&params->clip));

	/*
	 * Assure that the pitch is not zero, because this will inevitable cause the
	 * wrong expected result
	 */
	KUNIT_ASSERT_NE(test, pitch, 0);

	offset = drm_fb_clip_offset(pitch, format_info, &params->clip);

	KUNIT_EXPECT_EQ(test, offset, params->expected_offset);
}

struct fb_build_fourcc_list_case {
	const char *name;
	u32 native_fourccs[TEST_BUF_SIZE];
	size_t native_fourccs_size;
	u32 expected[TEST_BUF_SIZE];
	size_t expected_fourccs_size;
};

static struct fb_build_fourcc_list_case fb_build_fourcc_list_cases[] = {
	{
		.name = "no native formats",
		.native_fourccs = { },
		.native_fourccs_size = 0,
		.expected = { DRM_FORMAT_XRGB8888 },
		.expected_fourccs_size = 1,
	},
	{
		.name = "XRGB8888 as native format",
		.native_fourccs = { DRM_FORMAT_XRGB8888 },
		.native_fourccs_size = 1,
		.expected = { DRM_FORMAT_XRGB8888 },
		.expected_fourccs_size = 1,
	},
	{
		.name = "remove duplicates",
		.native_fourccs = {
			DRM_FORMAT_XRGB8888,
			DRM_FORMAT_XRGB8888,
			DRM_FORMAT_RGB888,
			DRM_FORMAT_RGB888,
			DRM_FORMAT_RGB888,
			DRM_FORMAT_XRGB8888,
			DRM_FORMAT_RGB888,
			DRM_FORMAT_RGB565,
			DRM_FORMAT_RGB888,
			DRM_FORMAT_XRGB8888,
			DRM_FORMAT_RGB565,
			DRM_FORMAT_RGB565,
			DRM_FORMAT_XRGB8888,
		},
		.native_fourccs_size = 11,
		.expected = {
			DRM_FORMAT_XRGB8888,
			DRM_FORMAT_RGB888,
			DRM_FORMAT_RGB565,
		},
		.expected_fourccs_size = 3,
	},
	{
		.name = "convert alpha formats",
		.native_fourccs = {
			DRM_FORMAT_ARGB1555,
			DRM_FORMAT_ABGR1555,
			DRM_FORMAT_RGBA5551,
			DRM_FORMAT_BGRA5551,
			DRM_FORMAT_ARGB8888,
			DRM_FORMAT_ABGR8888,
			DRM_FORMAT_RGBA8888,
			DRM_FORMAT_BGRA8888,
			DRM_FORMAT_ARGB2101010,
			DRM_FORMAT_ABGR2101010,
			DRM_FORMAT_RGBA1010102,
			DRM_FORMAT_BGRA1010102,
		},
		.native_fourccs_size = 12,
		.expected = {
			DRM_FORMAT_XRGB1555,
			DRM_FORMAT_XBGR1555,
			DRM_FORMAT_RGBX5551,
			DRM_FORMAT_BGRX5551,
			DRM_FORMAT_XRGB8888,
			DRM_FORMAT_XBGR8888,
			DRM_FORMAT_RGBX8888,
			DRM_FORMAT_BGRX8888,
			DRM_FORMAT_XRGB2101010,
			DRM_FORMAT_XBGR2101010,
			DRM_FORMAT_RGBX1010102,
			DRM_FORMAT_BGRX1010102,
		},
		.expected_fourccs_size = 12,
	},
	{
		.name = "random formats",
		.native_fourccs = {
			DRM_FORMAT_Y212,
			DRM_FORMAT_ARGB1555,
			DRM_FORMAT_ABGR16161616F,
			DRM_FORMAT_C8,
			DRM_FORMAT_BGR888,
			DRM_FORMAT_XRGB1555,
			DRM_FORMAT_RGBA5551,
			DRM_FORMAT_BGR565_A8,
			DRM_FORMAT_R10,
			DRM_FORMAT_XYUV8888,
		},
		.native_fourccs_size = 10,
		.expected = {
			DRM_FORMAT_Y212,
			DRM_FORMAT_XRGB1555,
			DRM_FORMAT_ABGR16161616F,
			DRM_FORMAT_C8,
			DRM_FORMAT_BGR888,
			DRM_FORMAT_RGBX5551,
			DRM_FORMAT_BGR565_A8,
			DRM_FORMAT_R10,
			DRM_FORMAT_XYUV8888,
			DRM_FORMAT_XRGB8888,
		},
		.expected_fourccs_size = 10,
	},
};

static void fb_build_fourcc_list_case_desc(struct fb_build_fourcc_list_case *t, char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(fb_build_fourcc_list, fb_build_fourcc_list_cases, fb_build_fourcc_list_case_desc);

static void drm_test_fb_build_fourcc_list(struct kunit *test)
{
	const struct fb_build_fourcc_list_case *params = test->param_value;
	u32 fourccs_out[TEST_BUF_SIZE] = {0};
	size_t nfourccs_out;
	struct drm_device *drm;
	struct device *dev;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev, sizeof(*drm), 0, DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	nfourccs_out = drm_fb_build_fourcc_list(drm, params->native_fourccs,
						params->native_fourccs_size,
						fourccs_out, TEST_BUF_SIZE);

	KUNIT_EXPECT_EQ(test, nfourccs_out, params->expected_fourccs_size);
	KUNIT_EXPECT_MEMEQ(test, fourccs_out, params->expected, TEST_BUF_SIZE);
}

struct fb_memcpy_case {
	const char *name;
	u32 format;
	struct drm_rect clip;
	unsigned int src_pitches[DRM_FORMAT_MAX_PLANES];
	const u32 src[DRM_FORMAT_MAX_PLANES][TEST_BUF_SIZE];
	unsigned int dst_pitches[DRM_FORMAT_MAX_PLANES];
	const u32 expected[DRM_FORMAT_MAX_PLANES][TEST_BUF_SIZE];
};

/* The `src` and `expected` buffers are u32 arrays. To deal with planes that
 * have a cpp != 4 the values are stored together on the same u32 number in a
 * way so the order in memory is correct in a little-endian machine.
 *
 * Because of that, on some occasions, parts of a u32 will not be part of the
 * test, to make this explicit the 0xFF byte is used on those parts.
 */

static struct fb_memcpy_case fb_memcpy_cases[] = {
	{
		.name = "single_pixel_source_buffer",
		.format = DRM_FORMAT_XRGB8888,
		.clip = DRM_RECT_INIT(0, 0, 1, 1),
		.src_pitches = { 1 * 4 },
		.src = {{ 0x01020304 }},
		.dst_pitches = { TEST_USE_DEFAULT_PITCH },
		.expected = {{ 0x01020304 }},
	},
	{
		.name = "single_pixel_source_buffer",
		.format = DRM_FORMAT_XRGB8888_A8,
		.clip = DRM_RECT_INIT(0, 0, 1, 1),
		.src_pitches = { 1 * 4, 1 },
		.src = {
			{ 0x01020304 },
			{ 0xFFFFFF01 },
		},
		.dst_pitches = { TEST_USE_DEFAULT_PITCH },
		.expected = {
			{ 0x01020304 },
			{ 0x00000001 },
		},
	},
	{
		.name = "single_pixel_source_buffer",
		.format = DRM_FORMAT_YUV444,
		.clip = DRM_RECT_INIT(0, 0, 1, 1),
		.src_pitches = { 1, 1, 1 },
		.src = {
			{ 0xFFFFFF01 },
			{ 0xFFFFFF01 },
			{ 0xFFFFFF01 },
		},
		.dst_pitches = { TEST_USE_DEFAULT_PITCH },
		.expected = {
			{ 0x00000001 },
			{ 0x00000001 },
			{ 0x00000001 },
		},
	},
	{
		.name = "single_pixel_clip_rectangle",
		.format = DRM_FORMAT_XBGR8888,
		.clip = DRM_RECT_INIT(1, 1, 1, 1),
		.src_pitches = { 2 * 4 },
		.src = {
			{
				0x00000000, 0x00000000,
				0x00000000, 0x01020304,
			},
		},
		.dst_pitches = { TEST_USE_DEFAULT_PITCH },
		.expected = {
			{ 0x01020304 },
		},
	},
	{
		.name = "single_pixel_clip_rectangle",
		.format = DRM_FORMAT_XRGB8888_A8,
		.clip = DRM_RECT_INIT(1, 1, 1, 1),
		.src_pitches = { 2 * 4, 2 * 1 },
		.src = {
			{
				0x00000000, 0x00000000,
				0x00000000, 0x01020304,
			},
			{ 0x01000000 },
		},
		.dst_pitches = { TEST_USE_DEFAULT_PITCH },
		.expected = {
			{ 0x01020304 },
			{ 0x00000001 },
		},
	},
	{
		.name = "single_pixel_clip_rectangle",
		.format = DRM_FORMAT_YUV444,
		.clip = DRM_RECT_INIT(1, 1, 1, 1),
		.src_pitches = { 2 * 1, 2 * 1, 2 * 1 },
		.src = {
			{ 0x01000000 },
			{ 0x01000000 },
			{ 0x01000000 },
		},
		.dst_pitches = { TEST_USE_DEFAULT_PITCH },
		.expected = {
			{ 0x00000001 },
			{ 0x00000001 },
			{ 0x00000001 },
		},
	},
	{
		.name = "well_known_colors",
		.format = DRM_FORMAT_XBGR8888,
		.clip = DRM_RECT_INIT(1, 1, 2, 4),
		.src_pitches = { 4 * 4 },
		.src = {
			{
				0x00000000, 0x00000000, 0x00000000, 0x00000000,
				0x00000000, 0x11FFFFFF, 0x22000000, 0x00000000,
				0x00000000, 0x33FF0000, 0x4400FF00, 0x00000000,
				0x00000000, 0x550000FF, 0x66FF00FF, 0x00000000,
				0x00000000, 0x77FFFF00, 0x8800FFFF, 0x00000000,
			},
		},
		.dst_pitches = { TEST_USE_DEFAULT_PITCH },
		.expected = {
			{
				0x11FFFFFF, 0x22000000,
				0x33FF0000, 0x4400FF00,
				0x550000FF, 0x66FF00FF,
				0x77FFFF00, 0x8800FFFF,
			},
		},
	},
	{
		.name = "well_known_colors",
		.format = DRM_FORMAT_XRGB8888_A8,
		.clip = DRM_RECT_INIT(1, 1, 2, 4),
		.src_pitches = { 4 * 4, 4 * 1 },
		.src = {
			{
				0x00000000, 0x00000000, 0x00000000, 0x00000000,
				0x00000000, 0xFFFFFFFF, 0xFF000000, 0x00000000,
				0x00000000, 0xFFFF0000, 0xFF00FF00, 0x00000000,
				0x00000000, 0xFF0000FF, 0xFFFF00FF, 0x00000000,
				0x00000000, 0xFFFFFF00, 0xFF00FFFF, 0x00000000,
			},
			{
				0x00000000,
				0x00221100,
				0x00443300,
				0x00665500,
				0x00887700,
			},
		},
		.dst_pitches = { TEST_USE_DEFAULT_PITCH },
		.expected = {
			{
				0xFFFFFFFF, 0xFF000000,
				0xFFFF0000, 0xFF00FF00,
				0xFF0000FF, 0xFFFF00FF,
				0xFFFFFF00, 0xFF00FFFF,
			},
			{
				0x44332211,
				0x88776655,
			},
		},
	},
	{
		.name = "well_known_colors",
		.format = DRM_FORMAT_YUV444,
		.clip = DRM_RECT_INIT(1, 1, 2, 4),
		.src_pitches = { 4 * 1, 4 * 1, 4 * 1 },
		.src = {
			{
				0x00000000,
				0x0000FF00,
				0x00954C00,
				0x00691D00,
				0x00B2E100,
			},
			{
				0x00000000,
				0x00000000,
				0x00BEDE00,
				0x00436500,
				0x00229B00,
			},
			{
				0x00000000,
				0x00000000,
				0x007E9C00,
				0x0083E700,
				0x00641A00,
			},
		},
		.dst_pitches = { TEST_USE_DEFAULT_PITCH },
		.expected = {
			{
				0x954C00FF,
				0xB2E1691D,
			},
			{
				0xBEDE0000,
				0x229B4365,
			},
			{
				0x7E9C0000,
				0x641A83E7,
			},
		},
	},
	{
		.name = "destination_pitch",
		.format = DRM_FORMAT_XBGR8888,
		.clip = DRM_RECT_INIT(0, 0, 3, 3),
		.src_pitches = { 3 * 4 },
		.src = {
			{
				0xA10E449C, 0xB1114D05, 0xC1A8F303,
				0xD16CF073, 0xA20E449C, 0xB2114D05,
				0xC2A80303, 0xD26CF073, 0xA30E449C,
			},
		},
		.dst_pitches = { 5 * 4 },
		.expected = {
			{
				0xA10E449C, 0xB1114D05, 0xC1A8F303, 0x00000000, 0x00000000,
				0xD16CF073, 0xA20E449C, 0xB2114D05, 0x00000000, 0x00000000,
				0xC2A80303, 0xD26CF073, 0xA30E449C, 0x00000000, 0x00000000,
			},
		},
	},
	{
		.name = "destination_pitch",
		.format = DRM_FORMAT_XRGB8888_A8,
		.clip = DRM_RECT_INIT(0, 0, 3, 3),
		.src_pitches = { 3 * 4, 3 * 1 },
		.src = {
			{
				0xFF0E449C, 0xFF114D05, 0xFFA8F303,
				0xFF6CF073, 0xFF0E449C, 0xFF114D05,
				0xFFA80303, 0xFF6CF073, 0xFF0E449C,
			},
			{
				0xB2C1B1A1,
				0xD2A3D1A2,
				0xFFFFFFC2,
			},
		},
		.dst_pitches = { 5 * 4, 5 * 1 },
		.expected = {
			{
				0xFF0E449C, 0xFF114D05, 0xFFA8F303, 0x00000000, 0x00000000,
				0xFF6CF073, 0xFF0E449C, 0xFF114D05, 0x00000000, 0x00000000,
				0xFFA80303, 0xFF6CF073, 0xFF0E449C, 0x00000000, 0x00000000,
			},
			{
				0x00C1B1A1,
				0xD1A2B200,
				0xD2A30000,
				0xFF0000C2,
			},
		},
	},
	{
		.name = "destination_pitch",
		.format = DRM_FORMAT_YUV444,
		.clip = DRM_RECT_INIT(0, 0, 3, 3),
		.src_pitches = { 3 * 1, 3 * 1, 3 * 1 },
		.src = {
			{
				0xBAC1323D,
				0xBA34323D,
				0xFFFFFF3D,
			},
			{
				0xE1ABEC2A,
				0xE1EAEC2A,
				0xFFFFFF2A,
			},
			{
				0xBCEBE4D7,
				0xBC65E4D7,
				0xFFFFFFD7,
			},
		},
		.dst_pitches = { 5 * 1, 5 * 1, 5 * 1 },
		.expected = {
			{
				0x00C1323D,
				0x323DBA00,
				0xBA340000,
				0xFF00003D,
			},
			{
				0x00ABEC2A,
				0xEC2AE100,
				0xE1EA0000,
				0xFF00002A,
			},
			{
				0x00EBE4D7,
				0xE4D7BC00,
				0xBC650000,
				0xFF0000D7,
			},
		},
	},
};

static void fb_memcpy_case_desc(struct fb_memcpy_case *t, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%s: %p4cc", t->name, &t->format);
}

KUNIT_ARRAY_PARAM(fb_memcpy, fb_memcpy_cases, fb_memcpy_case_desc);

static void drm_test_fb_memcpy(struct kunit *test)
{
	const struct fb_memcpy_case *params = test->param_value;
	size_t dst_size[DRM_FORMAT_MAX_PLANES] = { 0 };
	u32 *buf[DRM_FORMAT_MAX_PLANES] = { 0 };
	__le32 *src_cp[DRM_FORMAT_MAX_PLANES] = { 0 };
	__le32 *expected[DRM_FORMAT_MAX_PLANES] = { 0 };
	struct iosys_map dst[DRM_FORMAT_MAX_PLANES];
	struct iosys_map src[DRM_FORMAT_MAX_PLANES];

	struct drm_framebuffer fb = {
		.format = drm_format_info(params->format),
	};

	memcpy(fb.pitches, params->src_pitches, DRM_FORMAT_MAX_PLANES * sizeof(int));

	for (size_t i = 0; i < fb.format->num_planes; i++) {
		dst_size[i] = conversion_buf_size(params->format, params->dst_pitches[i],
						  &params->clip, i);
		KUNIT_ASSERT_GT(test, dst_size[i], 0);

		buf[i] = kunit_kzalloc(test, dst_size[i], GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf[i]);
		iosys_map_set_vaddr(&dst[i], buf[i]);

		src_cp[i] = cpubuf_to_le32(test, params->src[i], TEST_BUF_SIZE);
		iosys_map_set_vaddr(&src[i], src_cp[i]);
	}

	const unsigned int *dst_pitches = params->dst_pitches[0] == TEST_USE_DEFAULT_PITCH ? NULL :
		params->dst_pitches;

	drm_fb_memcpy(dst, dst_pitches, src, &fb, &params->clip);

	for (size_t i = 0; i < fb.format->num_planes; i++) {
		expected[i] = cpubuf_to_le32(test, params->expected[i], TEST_BUF_SIZE);
		KUNIT_EXPECT_MEMEQ_MSG(test, buf[i], expected[i], dst_size[i],
				       "Failed expectation on plane %zu", i);

		memset(buf[i], 0, dst_size[i]);
	}

	int blit_result;

	blit_result = drm_fb_blit(dst, dst_pitches, params->format, src, &fb, &params->clip,
				  &fmtcnv_state);

	KUNIT_EXPECT_FALSE(test, blit_result);
	for (size_t i = 0; i < fb.format->num_planes; i++) {
		expected[i] = cpubuf_to_le32(test, params->expected[i], TEST_BUF_SIZE);
		KUNIT_EXPECT_MEMEQ_MSG(test, buf[i], expected[i], dst_size[i],
				       "Failed expectation on plane %zu", i);
	}
}

static struct kunit_case drm_format_helper_test_cases[] = {
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_gray8, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_rgb332, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_rgb565, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_xrgb1555, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_argb1555, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_rgba5551, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_rgb888, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_argb8888, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_xrgb2101010, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_argb2101010, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_mono, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_swab, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_xbgr8888, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_xrgb8888_to_abgr8888, convert_xrgb8888_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_clip_offset, clip_offset_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_build_fourcc_list, fb_build_fourcc_list_gen_params),
	KUNIT_CASE_PARAM(drm_test_fb_memcpy, fb_memcpy_gen_params),
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
