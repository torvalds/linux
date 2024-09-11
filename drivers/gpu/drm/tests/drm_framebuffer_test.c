// SPDX-License-Identifier: GPL-2.0
/*
 * Test cases for the drm_framebuffer functions
 *
 * Copyright (c) 2022 Maíra Canal <mairacanal@riseup.net>
 */

#include <kunit/test.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_print.h>

#include "../drm_crtc_internal.h"

#define MIN_WIDTH 4
#define MAX_WIDTH 4096
#define MIN_HEIGHT 4
#define MAX_HEIGHT 4096

#define DRM_MODE_FB_INVALID BIT(2)

struct drm_framebuffer_test {
	int buffer_created;
	struct drm_mode_fb_cmd2 cmd;
	const char *name;
};

static const struct drm_framebuffer_test drm_framebuffer_create_cases[] = {
{ .buffer_created = 1, .name = "ABGR8888 normal sizes",
	.cmd = { .width = 600, .height = 600, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .pitches = { 4 * 600, 0, 0 },
	}
},
{ .buffer_created = 1, .name = "ABGR8888 max sizes",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .pitches = { 4 * MAX_WIDTH, 0, 0 },
	}
},
{ .buffer_created = 1, .name = "ABGR8888 pitch greater than min required",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .pitches = { 4 * MAX_WIDTH + 1, 0, 0 },
	}
},
{ .buffer_created = 0, .name = "ABGR8888 pitch less than min required",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .pitches = { 4 * MAX_WIDTH - 1, 0, 0 },
	}
},
{ .buffer_created = 0, .name = "ABGR8888 Invalid width",
	.cmd = { .width = MAX_WIDTH + 1, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .pitches = { 4 * (MAX_WIDTH + 1), 0, 0 },
	}
},
{ .buffer_created = 0, .name = "ABGR8888 Invalid buffer handle",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 0, 0, 0 }, .pitches = { 4 * MAX_WIDTH, 0, 0 },
	}
},
{ .buffer_created = 0, .name = "No pixel format",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = 0,
		 .handles = { 1, 0, 0 }, .pitches = { 4 * MAX_WIDTH, 0, 0 },
	}
},
{ .buffer_created = 0, .name = "ABGR8888 Width 0",
	.cmd = { .width = 0, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .pitches = { 4 * MAX_WIDTH, 0, 0 },
	}
},
{ .buffer_created = 0, .name = "ABGR8888 Height 0",
	.cmd = { .width = MAX_WIDTH, .height = 0, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .pitches = { 4 * MAX_WIDTH, 0, 0 },
	}
},
{ .buffer_created = 0, .name = "ABGR8888 Out of bound height * pitch combination",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .offsets = { UINT_MAX - 1, 0, 0 },
		 .pitches = { 4 * MAX_WIDTH, 0, 0 },
	}
},
{ .buffer_created = 1, .name = "ABGR8888 Large buffer offset",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .offsets = { UINT_MAX / 2, 0, 0 },
		 .pitches = { 4 * MAX_WIDTH, 0, 0 },
	}
},

/*
 * All entries in members that represents per-plane values (@modifier, @handles,
 * @pitches and @offsets) must be zero when unused.
 */
{ .buffer_created = 0, .name = "ABGR8888 Buffer offset for inexistent plane",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .offsets = { UINT_MAX / 2, UINT_MAX / 2, 0 },
		 .pitches = { 4 * MAX_WIDTH, 0, 0 }, .flags = DRM_MODE_FB_MODIFIERS,
	}
},

{ .buffer_created = 0, .name = "ABGR8888 Invalid flag",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .offsets = { UINT_MAX / 2, 0, 0 },
		 .pitches = { 4 * MAX_WIDTH, 0, 0 }, .flags = DRM_MODE_FB_INVALID,
	}
},
{ .buffer_created = 1, .name = "ABGR8888 Set DRM_MODE_FB_MODIFIERS without modifiers",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .offsets = { UINT_MAX / 2, 0, 0 },
		 .pitches = { 4 * MAX_WIDTH, 0, 0 }, .flags = DRM_MODE_FB_MODIFIERS,
	}
},
{ .buffer_created = 1, .name = "ABGR8888 Valid buffer modifier",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .offsets = { UINT_MAX / 2, 0, 0 },
		 .pitches = { 4 * MAX_WIDTH, 0, 0 }, .flags = DRM_MODE_FB_MODIFIERS,
		 .modifier = { AFBC_FORMAT_MOD_YTR, 0, 0 },
	}
},
{ .buffer_created = 0,
	.name = "ABGR8888 Invalid buffer modifier(DRM_FORMAT_MOD_SAMSUNG_64_32_TILE)",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .offsets = { UINT_MAX / 2, 0, 0 },
		 .pitches = { 4 * MAX_WIDTH, 0, 0 }, .flags = DRM_MODE_FB_MODIFIERS,
		 .modifier = { DRM_FORMAT_MOD_SAMSUNG_64_32_TILE, 0, 0 },
	}
},
{ .buffer_created = 1, .name = "ABGR8888 Extra pitches without DRM_MODE_FB_MODIFIERS",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .offsets = { UINT_MAX / 2, 0, 0 },
		 .pitches = { 4 * MAX_WIDTH, 4 * MAX_WIDTH, 0 },
	}
},
{ .buffer_created = 0, .name = "ABGR8888 Extra pitches with DRM_MODE_FB_MODIFIERS",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_ABGR8888,
		 .handles = { 1, 0, 0 }, .flags = DRM_MODE_FB_MODIFIERS,
		 .pitches = { 4 * MAX_WIDTH, 4 * MAX_WIDTH, 0 },
	}
},
{ .buffer_created = 1, .name = "NV12 Normal sizes",
	.cmd = { .width = 600, .height = 600, .pixel_format = DRM_FORMAT_NV12,
		 .handles = { 1, 1, 0 }, .pitches = { 600, 600, 0 },
	}
},
{ .buffer_created = 1, .name = "NV12 Max sizes",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_NV12,
		 .handles = { 1, 1, 0 }, .pitches = { MAX_WIDTH, MAX_WIDTH, 0 },
	}
},
{ .buffer_created = 0, .name = "NV12 Invalid pitch",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_NV12,
		 .handles = { 1, 1, 0 }, .pitches = { MAX_WIDTH, MAX_WIDTH - 1, 0 },
	}
},
{ .buffer_created = 0, .name = "NV12 Invalid modifier/missing DRM_MODE_FB_MODIFIERS flag",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_NV12,
		 .handles = { 1, 1, 0 }, .modifier = { DRM_FORMAT_MOD_SAMSUNG_64_32_TILE, 0, 0 },
		 .pitches = { MAX_WIDTH, MAX_WIDTH, 0 },
	}
},
{ .buffer_created = 0, .name = "NV12 different  modifier per-plane",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_NV12,
		 .handles = { 1, 1, 0 }, .flags = DRM_MODE_FB_MODIFIERS,
		 .modifier = { DRM_FORMAT_MOD_SAMSUNG_64_32_TILE, 0, 0 },
		 .pitches = { MAX_WIDTH, MAX_WIDTH, 0 },
	}
},
{ .buffer_created = 1, .name = "NV12 with DRM_FORMAT_MOD_SAMSUNG_64_32_TILE",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_NV12,
		 .handles = { 1, 1, 0 }, .flags = DRM_MODE_FB_MODIFIERS,
		 .modifier = { DRM_FORMAT_MOD_SAMSUNG_64_32_TILE,
			 DRM_FORMAT_MOD_SAMSUNG_64_32_TILE, 0 },
		 .pitches = { MAX_WIDTH, MAX_WIDTH, 0 },
	}
},
{ .buffer_created = 0, .name = "NV12 Valid modifiers without DRM_MODE_FB_MODIFIERS",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_NV12,
		 .handles = { 1, 1, 0 }, .modifier = { DRM_FORMAT_MOD_SAMSUNG_64_32_TILE,
						       DRM_FORMAT_MOD_SAMSUNG_64_32_TILE, 0 },
		 .pitches = { MAX_WIDTH, MAX_WIDTH, 0 },
	}
},
{ .buffer_created = 0, .name = "NV12 Modifier for inexistent plane",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_NV12,
		 .handles = { 1, 1, 0 }, .flags = DRM_MODE_FB_MODIFIERS,
		 .modifier = { DRM_FORMAT_MOD_SAMSUNG_64_32_TILE, DRM_FORMAT_MOD_SAMSUNG_64_32_TILE,
			       DRM_FORMAT_MOD_SAMSUNG_64_32_TILE },
		 .pitches = { MAX_WIDTH, MAX_WIDTH, 0 },
	}
},
{ .buffer_created = 0, .name = "NV12 Handle for inexistent plane",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_NV12,
		 .handles = { 1, 1, 1 }, .flags = DRM_MODE_FB_MODIFIERS,
		 .pitches = { MAX_WIDTH, MAX_WIDTH, 0 },
	}
},
{ .buffer_created = 1, .name = "NV12 Handle for inexistent plane without DRM_MODE_FB_MODIFIERS",
	.cmd = { .width = 600, .height = 600, .pixel_format = DRM_FORMAT_NV12,
		 .handles = { 1, 1, 1 }, .pitches = { 600, 600, 600 },
	}
},
{ .buffer_created = 1, .name = "YVU420 DRM_MODE_FB_MODIFIERS set without modifier",
	.cmd = { .width = 600, .height = 600, .pixel_format = DRM_FORMAT_YVU420,
		 .handles = { 1, 1, 1 }, .flags = DRM_MODE_FB_MODIFIERS,
		 .pitches = { 600, 300, 300 },
	}
},
{ .buffer_created = 1, .name = "YVU420 Normal sizes",
	.cmd = { .width = 600, .height = 600, .pixel_format = DRM_FORMAT_YVU420,
		 .handles = { 1, 1, 1 }, .pitches = { 600, 300, 300 },
	}
},
{ .buffer_created = 1, .name = "YVU420 Max sizes",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_YVU420,
		 .handles = { 1, 1, 1 }, .pitches = { MAX_WIDTH, DIV_ROUND_UP(MAX_WIDTH, 2),
						      DIV_ROUND_UP(MAX_WIDTH, 2) },
	}
},
{ .buffer_created = 0, .name = "YVU420 Invalid pitch",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_YVU420,
		 .handles = { 1, 1, 1 }, .pitches = { MAX_WIDTH, DIV_ROUND_UP(MAX_WIDTH, 2) - 1,
						      DIV_ROUND_UP(MAX_WIDTH, 2) },
	}
},
{ .buffer_created = 1, .name = "YVU420 Different pitches",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_YVU420,
		 .handles = { 1, 1, 1 }, .pitches = { MAX_WIDTH, DIV_ROUND_UP(MAX_WIDTH, 2) + 1,
						      DIV_ROUND_UP(MAX_WIDTH, 2) + 7 },
	}
},
{ .buffer_created = 1, .name = "YVU420 Different buffer offsets/pitches",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_YVU420,
		 .handles = { 1, 1, 1 }, .offsets = { MAX_WIDTH, MAX_WIDTH  +
			 MAX_WIDTH * MAX_HEIGHT, MAX_WIDTH  + 2 * MAX_WIDTH * MAX_HEIGHT },
		 .pitches = { MAX_WIDTH, DIV_ROUND_UP(MAX_WIDTH, 2) + 1,
			 DIV_ROUND_UP(MAX_WIDTH, 2) + 7 },
	}
},
{ .buffer_created = 0,
	.name = "YVU420 Modifier set just for plane 0, without DRM_MODE_FB_MODIFIERS",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_YVU420,
		 .handles = { 1, 1, 1 }, .modifier = { AFBC_FORMAT_MOD_SPARSE, 0, 0 },
		 .pitches = { MAX_WIDTH, DIV_ROUND_UP(MAX_WIDTH, 2), DIV_ROUND_UP(MAX_WIDTH, 2) },
	}
},
{ .buffer_created = 0,
	.name = "YVU420 Modifier set just for planes 0, 1, without DRM_MODE_FB_MODIFIERS",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_YVU420,
		 .handles = { 1, 1, 1 },
		 .modifier = { AFBC_FORMAT_MOD_SPARSE, AFBC_FORMAT_MOD_SPARSE, 0 },
		 .pitches = { MAX_WIDTH, DIV_ROUND_UP(MAX_WIDTH, 2), DIV_ROUND_UP(MAX_WIDTH, 2) },
	}
},
{ .buffer_created = 0,
	.name = "YVU420 Modifier set just for plane 0, 1, with DRM_MODE_FB_MODIFIERS",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_YVU420,
		 .handles = { 1, 1, 1 }, .flags = DRM_MODE_FB_MODIFIERS,
		 .modifier = { AFBC_FORMAT_MOD_SPARSE, AFBC_FORMAT_MOD_SPARSE, 0 },
		 .pitches = { MAX_WIDTH, DIV_ROUND_UP(MAX_WIDTH, 2), DIV_ROUND_UP(MAX_WIDTH, 2) },
	}
},
{ .buffer_created = 1, .name = "YVU420 Valid modifier",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_YVU420,
		 .handles = { 1, 1, 1 }, .flags = DRM_MODE_FB_MODIFIERS,
		 .modifier = { AFBC_FORMAT_MOD_SPARSE, AFBC_FORMAT_MOD_SPARSE,
			 AFBC_FORMAT_MOD_SPARSE },
		 .pitches = { MAX_WIDTH, DIV_ROUND_UP(MAX_WIDTH, 2), DIV_ROUND_UP(MAX_WIDTH, 2) },
	}
},
{ .buffer_created = 0, .name = "YVU420 Different modifiers per plane",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_YVU420,
		 .handles = { 1, 1, 1 }, .flags = DRM_MODE_FB_MODIFIERS,
		 .modifier = { AFBC_FORMAT_MOD_SPARSE, AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_YTR,
			       AFBC_FORMAT_MOD_SPARSE },
		 .pitches = { MAX_WIDTH, DIV_ROUND_UP(MAX_WIDTH, 2), DIV_ROUND_UP(MAX_WIDTH, 2) },
	}
},
{ .buffer_created = 0, .name = "YVU420 Modifier for inexistent plane",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_YVU420,
		 .handles = { 1, 1, 1 }, .flags = DRM_MODE_FB_MODIFIERS,
		 .modifier = { AFBC_FORMAT_MOD_SPARSE, AFBC_FORMAT_MOD_SPARSE,
			 AFBC_FORMAT_MOD_SPARSE, AFBC_FORMAT_MOD_SPARSE },
		 .pitches = { MAX_WIDTH, DIV_ROUND_UP(MAX_WIDTH, 2), DIV_ROUND_UP(MAX_WIDTH, 2) },
	}
},
{ .buffer_created = 0, .name = "YUV420_10BIT Invalid modifier(DRM_FORMAT_MOD_LINEAR)",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_YUV420_10BIT,
		 .handles = { 1, 0, 0 }, .flags = DRM_MODE_FB_MODIFIERS,
		 .modifier = { DRM_FORMAT_MOD_LINEAR, 0, 0 },
		 .pitches = { MAX_WIDTH, 0, 0 },
	}
},
{ .buffer_created = 1, .name = "X0L2 Normal sizes",
	.cmd = { .width = 600, .height = 600, .pixel_format = DRM_FORMAT_X0L2,
		 .handles = { 1, 0, 0 }, .pitches = { 1200, 0, 0 }
	}
},
{ .buffer_created = 1, .name = "X0L2 Max sizes",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_X0L2,
		 .handles = { 1, 0, 0 }, .pitches = { 2 * MAX_WIDTH, 0, 0 }
	}
},
{ .buffer_created = 0, .name = "X0L2 Invalid pitch",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_X0L2,
		 .handles = { 1, 0, 0 }, .pitches = { 2 * MAX_WIDTH - 1, 0, 0 }
	}
},
{ .buffer_created = 1, .name = "X0L2 Pitch greater than minimum required",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_X0L2,
		 .handles = { 1, 0, 0 }, .pitches = { 2 * MAX_WIDTH + 1, 0, 0 }
	}
},
{ .buffer_created = 0, .name = "X0L2 Handle for inexistent plane",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_X0L2,
		 .handles = { 1, 1, 0 }, .flags = DRM_MODE_FB_MODIFIERS,
		 .pitches = { 2 * MAX_WIDTH + 1, 0, 0 }
	}
},
{ .buffer_created = 1,
	.name = "X0L2 Offset for inexistent plane, without DRM_MODE_FB_MODIFIERS set",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_X0L2,
		 .handles = { 1, 0, 0 }, .offsets = { 0, 0, 3 },
		 .pitches = { 2 * MAX_WIDTH + 1, 0, 0 }
	}
},
{ .buffer_created = 0, .name = "X0L2 Modifier without DRM_MODE_FB_MODIFIERS set",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_X0L2,
		 .handles = { 1, 0, 0 }, .pitches = { 2 * MAX_WIDTH + 1, 0, 0 },
		 .modifier = { AFBC_FORMAT_MOD_SPARSE, 0, 0 },
	}
},
{ .buffer_created = 1, .name = "X0L2 Valid modifier",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT, .pixel_format = DRM_FORMAT_X0L2,
		 .handles = { 1, 0, 0 }, .pitches = { 2 * MAX_WIDTH + 1, 0, 0 },
		 .modifier = { AFBC_FORMAT_MOD_SPARSE, 0, 0 }, .flags = DRM_MODE_FB_MODIFIERS,
	}
},
{ .buffer_created = 0, .name = "X0L2 Modifier for inexistent plane",
	.cmd = { .width = MAX_WIDTH, .height = MAX_HEIGHT,
		 .pixel_format = DRM_FORMAT_X0L2, .handles = { 1, 0, 0 },
		 .pitches = { 2 * MAX_WIDTH + 1, 0, 0 },
		 .modifier = { AFBC_FORMAT_MOD_SPARSE, AFBC_FORMAT_MOD_SPARSE, 0 },
		 .flags = DRM_MODE_FB_MODIFIERS,
	}
},
};

/*
 * This struct is intended to provide a way to mocked functions communicate
 * with the outer test when it can't be achieved by using its return value. In
 * this way, the functions that receive the mocked drm_device, for example, can
 * grab a reference to this and actually return something to be used on some
 * expectation.
 */
struct drm_framebuffer_test_priv {
	struct drm_device dev;
	bool buffer_created;
};

static struct drm_framebuffer *fb_create_mock(struct drm_device *dev,
					      struct drm_file *file_priv,
					      const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_framebuffer_test_priv *priv = container_of(dev, typeof(*priv), dev);

	priv->buffer_created = true;
	return ERR_PTR(-EINVAL);
}

static struct drm_mode_config_funcs mock_config_funcs = {
	.fb_create = fb_create_mock,
};

static int drm_framebuffer_test_init(struct kunit *test)
{
	struct device *parent;
	struct drm_framebuffer_test_priv *priv;
	struct drm_device *dev;

	parent = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	priv = drm_kunit_helper_alloc_drm_device(test, parent, typeof(*priv),
						 dev, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);
	dev = &priv->dev;

	dev->mode_config.min_width = MIN_WIDTH;
	dev->mode_config.max_width = MAX_WIDTH;
	dev->mode_config.min_height = MIN_HEIGHT;
	dev->mode_config.max_height = MAX_HEIGHT;
	dev->mode_config.funcs = &mock_config_funcs;

	test->priv = priv;
	return 0;
}

static void drm_test_framebuffer_create(struct kunit *test)
{
	const struct drm_framebuffer_test *params = test->param_value;
	struct drm_framebuffer_test_priv *priv = test->priv;
	struct drm_device *dev = &priv->dev;

	priv->buffer_created = false;
	drm_internal_framebuffer_create(dev, &params->cmd, NULL);
	KUNIT_EXPECT_EQ(test, params->buffer_created, priv->buffer_created);
}

static void drm_framebuffer_test_to_desc(const struct drm_framebuffer_test *t, char *desc)
{
	strcpy(desc, t->name);
}

KUNIT_ARRAY_PARAM(drm_framebuffer_create, drm_framebuffer_create_cases,
		  drm_framebuffer_test_to_desc);

static struct kunit_case drm_framebuffer_tests[] = {
	KUNIT_CASE_PARAM(drm_test_framebuffer_create, drm_framebuffer_create_gen_params),
	{ }
};

static struct kunit_suite drm_framebuffer_test_suite = {
	.name = "drm_framebuffer",
	.init = drm_framebuffer_test_init,
	.test_cases = drm_framebuffer_tests,
};

kunit_test_suite(drm_framebuffer_test_suite);

MODULE_DESCRIPTION("Test cases for the drm_framebuffer functions");
MODULE_LICENSE("GPL");
