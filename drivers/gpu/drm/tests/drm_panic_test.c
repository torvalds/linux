// SPDX-License-Identifier: GPL-2.0 or MIT
/*
 * Copyright (c) 2025 Red Hat.
 * Author: Jocelyn Falempe <jfalempe@redhat.com>
 *
 * KUNIT tests for drm panic
 */

#include <drm/drm_fourcc.h>
#include <drm/drm_panic.h>

#include <kunit/test.h>

#include <linux/units.h>
#include <linux/vmalloc.h>

/* Check the framebuffer color only if the panic colors are the default */
#if (CONFIG_DRM_PANIC_BACKGROUND_COLOR == 0 && \
	CONFIG_DRM_PANIC_FOREGROUND_COLOR == 0xffffff)

static void drm_panic_check_color_byte(struct kunit *test, u8 b)
{
	KUNIT_EXPECT_TRUE(test, (b == 0 || b == 0xff));
}
#else
static void drm_panic_check_color_byte(struct kunit *test, u8 b) {}
#endif

struct drm_test_mode {
	const int width;
	const int height;
	const u32 format;
	void (*draw_screen)(struct drm_scanout_buffer *sb);
	const char *fname;
};

/*
 * Run all tests for the 3 panic screens: user, kmsg and qr_code
 */
#define DRM_TEST_MODE_LIST(func) \
	DRM_PANIC_TEST_MODE(1024, 768, DRM_FORMAT_XRGB8888, func) \
	DRM_PANIC_TEST_MODE(300, 200, DRM_FORMAT_XRGB8888, func) \
	DRM_PANIC_TEST_MODE(1920, 1080, DRM_FORMAT_XRGB8888, func) \
	DRM_PANIC_TEST_MODE(1024, 768, DRM_FORMAT_RGB565, func) \
	DRM_PANIC_TEST_MODE(1024, 768, DRM_FORMAT_RGB888, func) \

#define DRM_PANIC_TEST_MODE(w, h, f, name) { \
	.width = w, \
	.height = h, \
	.format = f, \
	.draw_screen = draw_panic_screen_##name, \
	.fname = #name, \
	}, \

static const struct drm_test_mode drm_test_modes_cases[] = {
	DRM_TEST_MODE_LIST(user)
	DRM_TEST_MODE_LIST(kmsg)
#if IS_ENABLED(CONFIG_DRM_PANIC_SCREEN_QR_CODE)
	DRM_TEST_MODE_LIST(qr_code)
#endif
};

#undef DRM_PANIC_TEST_MODE

static int drm_test_panic_init(struct kunit *test)
{
	struct drm_scanout_buffer *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);

	test->priv = priv;

	drm_panic_set_description("Kunit testing");

	return 0;
}

/*
 * Test drawing the panic screen, using a memory mapped framebuffer
 * Set the whole buffer to 0xa5, and then check that all pixels have been
 * written.
 */
static void drm_test_panic_screen_user_map(struct kunit *test)
{
	struct drm_scanout_buffer *sb = test->priv;
	const struct drm_test_mode *params = test->param_value;
	char *fb;
	int fb_size;
	int i;

	sb->format = drm_format_info(params->format);
	fb_size = params->width * params->height * sb->format->cpp[0];

	fb = vmalloc(fb_size);
	KUNIT_ASSERT_NOT_NULL(test, fb);

	memset(fb, 0xa5, fb_size);

	iosys_map_set_vaddr(&sb->map[0], fb);
	sb->width = params->width;
	sb->height = params->height;
	sb->pitch[0] = params->width * sb->format->cpp[0];

	params->draw_screen(sb);

	for (i = 0; i < fb_size; i++)
		drm_panic_check_color_byte(test, fb[i]);

	vfree(fb);
}

/*
 * Test drawing the panic screen, using a list of pages framebuffer
 * Set the whole buffer to 0xa5, and then check that all pixels have been
 * written.
 */
static void drm_test_panic_screen_user_page(struct kunit *test)
{
	struct drm_scanout_buffer *sb = test->priv;
	const struct drm_test_mode *params = test->param_value;
	int fb_size, p, i, npages;
	struct page **pages;
	u8 *vaddr;

	sb->format = drm_format_info(params->format);
	fb_size = params->width * params->height * sb->format->cpp[0];
	npages = DIV_ROUND_UP(fb_size, PAGE_SIZE);

	pages = kmalloc_array(npages, sizeof(struct page *), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pages);

	for (p = 0; p < npages; p++) {
		pages[p] = alloc_page(GFP_KERNEL);
		if (!pages[p]) {
			npages = p - 1;
			KUNIT_FAIL(test, "Can't allocate page\n");
			goto free_pages;
		}
		vaddr = kmap_local_page(pages[p]);
		memset(vaddr, 0xa5, PAGE_SIZE);
		kunmap_local(vaddr);
	}
	sb->pages = pages;
	sb->width = params->width;
	sb->height = params->height;
	sb->pitch[0] = params->width * sb->format->cpp[0];

	params->draw_screen(sb);

	for (p = 0; p < npages; p++) {
		int bytes_in_page = (p == npages - 1) ? fb_size - p * PAGE_SIZE : PAGE_SIZE;

		vaddr = kmap_local_page(pages[p]);
		for (i = 0; i < bytes_in_page; i++)
			drm_panic_check_color_byte(test, vaddr[i]);

		kunmap_local(vaddr);
	}

free_pages:
	for (p = 0; p < npages; p++)
		__free_page(pages[p]);
	kfree(pages);
}

static void drm_test_panic_set_pixel(struct drm_scanout_buffer *sb,
				     unsigned int x,
				     unsigned int y,
				     u32 color)
{
	struct kunit *test = (struct kunit *)sb->private;

	KUNIT_ASSERT_TRUE(test, x < sb->width && y < sb->height);
}

/*
 * Test drawing the panic screen, using the set_pixel callback
 * Check that all calls to set_pixel() are within the framebuffer
 */
static void drm_test_panic_screen_user_set_pixel(struct kunit *test)
{
	struct drm_scanout_buffer *sb = test->priv;
	const struct drm_test_mode *params = test->param_value;

	sb->format = drm_format_info(params->format);
	sb->set_pixel = drm_test_panic_set_pixel;
	sb->width = params->width;
	sb->height = params->height;
	sb->private = test;

	params->draw_screen(sb);
}

static void drm_test_panic_desc(const struct drm_test_mode *t, char *desc)
{
	sprintf(desc, "Panic screen %s, mode: %d x %d \t%p4cc",
		t->fname, t->width, t->height, &t->format);
}

KUNIT_ARRAY_PARAM(drm_test_panic_screen_user_map, drm_test_modes_cases, drm_test_panic_desc);
KUNIT_ARRAY_PARAM(drm_test_panic_screen_user_page, drm_test_modes_cases, drm_test_panic_desc);
KUNIT_ARRAY_PARAM(drm_test_panic_screen_user_set_pixel, drm_test_modes_cases, drm_test_panic_desc);

static struct kunit_case drm_panic_screen_user_test[] = {
	KUNIT_CASE_PARAM(drm_test_panic_screen_user_map,
			 drm_test_panic_screen_user_map_gen_params),
	KUNIT_CASE_PARAM(drm_test_panic_screen_user_page,
			 drm_test_panic_screen_user_page_gen_params),
	KUNIT_CASE_PARAM(drm_test_panic_screen_user_set_pixel,
			 drm_test_panic_screen_user_set_pixel_gen_params),
	{ }
};

static struct kunit_suite drm_panic_suite = {
	.name = "drm_panic",
	.init = drm_test_panic_init,
	.test_cases = drm_panic_screen_user_test,
};

kunit_test_suite(drm_panic_suite);
