// SPDX-License-Identifier: GPL-2.0
/*
 * Test cases for the drm_framebuffer functions
 *
 * Copyright (c) 2022 Maíra Canal <mairacanal@riseup.net>
 */

#include <kunit/device.h>
#include <kunit/test.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_mode.h>
#include <drm/drm_framebuffer.h>
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
	bool buffer_freed;
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
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(drm_framebuffer_create, drm_framebuffer_create_cases,
		  drm_framebuffer_test_to_desc);

/* Tries to create a framebuffer with modifiers without drm_device supporting it */
static void drm_test_framebuffer_modifiers_not_supported(struct kunit *test)
{
	struct drm_framebuffer_test_priv *priv = test->priv;
	struct drm_device *dev = &priv->dev;
	struct drm_framebuffer *fb;

	/* A valid cmd with modifier */
	struct drm_mode_fb_cmd2 cmd = {
		.width = MAX_WIDTH, .height = MAX_HEIGHT,
		.pixel_format = DRM_FORMAT_ABGR8888, .handles = { 1, 0, 0 },
		.offsets = { UINT_MAX / 2, 0, 0 }, .pitches = { 4 * MAX_WIDTH, 0, 0 },
		.flags = DRM_MODE_FB_MODIFIERS,
	};

	priv->buffer_created = false;
	dev->mode_config.fb_modifiers_not_supported = 1;

	fb = drm_internal_framebuffer_create(dev, &cmd, NULL);
	KUNIT_EXPECT_EQ(test, priv->buffer_created, false);
	KUNIT_EXPECT_EQ(test, PTR_ERR(fb), -EINVAL);
}

/* Parameters for testing drm_framebuffer_check_src_coords function */
struct drm_framebuffer_check_src_coords_case {
	const char *name;
	const int expect;
	const unsigned int fb_size;
	const uint32_t src_x;
	const uint32_t src_y;

	/* Deltas to be applied on source */
	const uint32_t dsrc_w;
	const uint32_t dsrc_h;
};

static const struct drm_framebuffer_check_src_coords_case
drm_framebuffer_check_src_coords_cases[] = {
	{ .name = "Success: source fits into fb",
	  .expect = 0,
	},
	{ .name = "Fail: overflowing fb with x-axis coordinate",
	  .expect = -ENOSPC, .src_x = 1, .fb_size = UINT_MAX,
	},
	{ .name = "Fail: overflowing fb with y-axis coordinate",
	  .expect = -ENOSPC, .src_y = 1, .fb_size = UINT_MAX,
	},
	{ .name = "Fail: overflowing fb with source width",
	  .expect = -ENOSPC, .dsrc_w = 1, .fb_size = UINT_MAX - 1,
	},
	{ .name = "Fail: overflowing fb with source height",
	  .expect = -ENOSPC, .dsrc_h = 1, .fb_size = UINT_MAX - 1,
	},
};

static void drm_test_framebuffer_check_src_coords(struct kunit *test)
{
	const struct drm_framebuffer_check_src_coords_case *params = test->param_value;
	const uint32_t src_x = params->src_x;
	const uint32_t src_y = params->src_y;
	const uint32_t src_w = (params->fb_size << 16) + params->dsrc_w;
	const uint32_t src_h = (params->fb_size << 16) + params->dsrc_h;
	const struct drm_framebuffer fb = {
		.width  = params->fb_size,
		.height = params->fb_size
	};
	int ret;

	ret = drm_framebuffer_check_src_coords(src_x, src_y, src_w, src_h, &fb);
	KUNIT_EXPECT_EQ(test, ret, params->expect);
}

static void
check_src_coords_test_to_desc(const struct drm_framebuffer_check_src_coords_case *t,
			      char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(check_src_coords, drm_framebuffer_check_src_coords_cases,
		  check_src_coords_test_to_desc);

/*
 * Test if drm_framebuffer_cleanup() really pops out the framebuffer object
 * from device's fb_list and decrement the number of framebuffers for that
 * device, which is the only things it does.
 */
static void drm_test_framebuffer_cleanup(struct kunit *test)
{
	struct drm_framebuffer_test_priv *priv = test->priv;
	struct drm_device *dev = &priv->dev;
	struct list_head *fb_list = &dev->mode_config.fb_list;
	struct drm_format_info format = { };
	struct drm_framebuffer fb1 = { .dev = dev, .format = &format };
	struct drm_framebuffer fb2 = { .dev = dev, .format = &format };

	/* This will result on [fb_list] -> fb2 -> fb1 */
	drm_framebuffer_init(dev, &fb1, NULL);
	drm_framebuffer_init(dev, &fb2, NULL);

	drm_framebuffer_cleanup(&fb1);

	/* Now fb2 is the only one element on fb_list */
	KUNIT_ASSERT_TRUE(test, list_is_singular(&fb2.head));
	KUNIT_ASSERT_EQ(test, dev->mode_config.num_fb, 1);

	drm_framebuffer_cleanup(&fb2);

	/* Now fb_list is empty */
	KUNIT_ASSERT_TRUE(test, list_empty(fb_list));
	KUNIT_ASSERT_EQ(test, dev->mode_config.num_fb, 0);
}

/*
 * Initialize a framebuffer, lookup its id and test if the returned reference
 * matches.
 */
static void drm_test_framebuffer_lookup(struct kunit *test)
{
	struct drm_framebuffer_test_priv *priv = test->priv;
	struct drm_device *dev = &priv->dev;
	struct drm_format_info format = { };
	struct drm_framebuffer expected_fb = { .dev = dev, .format = &format };
	struct drm_framebuffer *returned_fb;
	uint32_t id = 0;
	int ret;

	ret = drm_framebuffer_init(dev, &expected_fb, NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	id = expected_fb.base.id;

	/* Looking for expected_fb */
	returned_fb = drm_framebuffer_lookup(dev, NULL, id);
	KUNIT_EXPECT_PTR_EQ(test, returned_fb, &expected_fb);
	drm_framebuffer_put(returned_fb);

	drm_framebuffer_cleanup(&expected_fb);
}

/* Try to lookup an id that is not linked to a framebuffer */
static void drm_test_framebuffer_lookup_inexistent(struct kunit *test)
{
	struct drm_framebuffer_test_priv *priv = test->priv;
	struct drm_device *dev = &priv->dev;
	struct drm_framebuffer *fb;
	uint32_t id = 0;

	/* Looking for an inexistent framebuffer */
	fb = drm_framebuffer_lookup(dev, NULL, id);
	KUNIT_EXPECT_NULL(test, fb);
}

/*
 * Test if drm_framebuffer_init initializes the framebuffer successfully,
 * asserting that its modeset object struct and its refcount are correctly
 * set and that strictly one framebuffer is initialized.
 */
static void drm_test_framebuffer_init(struct kunit *test)
{
	struct drm_framebuffer_test_priv *priv = test->priv;
	struct drm_device *dev = &priv->dev;
	struct drm_format_info format = { };
	struct drm_framebuffer fb1 = { .dev = dev, .format = &format };
	struct drm_framebuffer_funcs funcs = { };
	int ret;

	ret = drm_framebuffer_init(dev, &fb1, &funcs);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* Check if fb->funcs is actually set to the drm_framebuffer_funcs passed on */
	KUNIT_EXPECT_PTR_EQ(test, fb1.funcs, &funcs);

	/* The fb->comm must be set to the current running process */
	KUNIT_EXPECT_STREQ(test, fb1.comm, current->comm);

	/* The fb->base must be successfully initialized */
	KUNIT_EXPECT_NE(test, fb1.base.id, 0);
	KUNIT_EXPECT_EQ(test, fb1.base.type, DRM_MODE_OBJECT_FB);
	KUNIT_EXPECT_EQ(test, kref_read(&fb1.base.refcount), 1);
	KUNIT_EXPECT_PTR_EQ(test, fb1.base.free_cb, &drm_framebuffer_free);

	/* There must be just that one fb initialized */
	KUNIT_EXPECT_EQ(test, dev->mode_config.num_fb, 1);
	KUNIT_EXPECT_PTR_EQ(test, dev->mode_config.fb_list.prev, &fb1.head);
	KUNIT_EXPECT_PTR_EQ(test, dev->mode_config.fb_list.next, &fb1.head);

	drm_framebuffer_cleanup(&fb1);
}

/* Try to init a framebuffer without setting its format */
static void drm_test_framebuffer_init_bad_format(struct kunit *test)
{
	struct drm_framebuffer_test_priv *priv = test->priv;
	struct drm_device *dev = &priv->dev;
	struct drm_framebuffer fb1 = { .dev = dev, .format = NULL };
	struct drm_framebuffer_funcs funcs = { };
	int ret;

	/* Fails if fb.format isn't set */
	ret = drm_framebuffer_init(dev, &fb1, &funcs);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/*
 * Test calling drm_framebuffer_init() passing a framebuffer linked to a
 * different drm_device parent from the one passed on the first argument, which
 * must fail.
 */
static void drm_test_framebuffer_init_dev_mismatch(struct kunit *test)
{
	struct drm_framebuffer_test_priv *priv = test->priv;
	struct drm_device *right_dev = &priv->dev;
	struct drm_device *wrong_dev;
	struct device *wrong_dev_parent;
	struct drm_format_info format = { };
	struct drm_framebuffer fb1 = { .dev = right_dev, .format = &format };
	struct drm_framebuffer_funcs funcs = { };
	int ret;

	wrong_dev_parent = kunit_device_register(test, "drm-kunit-wrong-device-mock");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, wrong_dev_parent);

	wrong_dev = __drm_kunit_helper_alloc_drm_device(test, wrong_dev_parent,
							sizeof(struct drm_device),
							0, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, wrong_dev);

	/* Fails if fb->dev doesn't point to the drm_device passed on first arg */
	ret = drm_framebuffer_init(wrong_dev, &fb1, &funcs);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void destroy_free_mock(struct drm_framebuffer *fb)
{
	struct drm_framebuffer_test_priv *priv = container_of(fb->dev, typeof(*priv), dev);

	priv->buffer_freed = true;
}

static struct drm_framebuffer_funcs framebuffer_funcs_free_mock = {
	.destroy = destroy_free_mock,
};

/*
 * In summary, the drm_framebuffer_free() function must implicitly call
 * fb->funcs->destroy() and garantee that the framebufer object is unregistered
 * from the drm_device idr pool.
 */
static void drm_test_framebuffer_free(struct kunit *test)
{
	struct drm_framebuffer_test_priv *priv = test->priv;
	struct drm_device *dev = &priv->dev;
	struct drm_mode_object *obj;
	struct drm_framebuffer fb = {
		.dev = dev,
		.funcs = &framebuffer_funcs_free_mock,
	};
	int id, ret;

	priv->buffer_freed = false;

	/*
	 * Mock	a framebuffer that was not unregistered	at the moment of the
	 * drm_framebuffer_free() call.
	 */
	ret = drm_mode_object_add(dev, &fb.base, DRM_MODE_OBJECT_FB);
	KUNIT_ASSERT_EQ(test, ret, 0);
	id = fb.base.id;

	drm_framebuffer_free(&fb.base.refcount);

	/* The framebuffer object must be unregistered */
	obj = drm_mode_object_find(dev, NULL, id, DRM_MODE_OBJECT_FB);
	KUNIT_EXPECT_PTR_EQ(test, obj, NULL);
	KUNIT_EXPECT_EQ(test, fb.base.id, 0);

	/* Test if fb->funcs->destroy() was called */
	KUNIT_EXPECT_EQ(test, priv->buffer_freed, true);
}

static struct kunit_case drm_framebuffer_tests[] = {
	KUNIT_CASE_PARAM(drm_test_framebuffer_check_src_coords, check_src_coords_gen_params),
	KUNIT_CASE(drm_test_framebuffer_cleanup),
	KUNIT_CASE_PARAM(drm_test_framebuffer_create, drm_framebuffer_create_gen_params),
	KUNIT_CASE(drm_test_framebuffer_free),
	KUNIT_CASE(drm_test_framebuffer_init),
	KUNIT_CASE(drm_test_framebuffer_init_bad_format),
	KUNIT_CASE(drm_test_framebuffer_init_dev_mismatch),
	KUNIT_CASE(drm_test_framebuffer_lookup),
	KUNIT_CASE(drm_test_framebuffer_lookup_inexistent),
	KUNIT_CASE(drm_test_framebuffer_modifiers_not_supported),
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
