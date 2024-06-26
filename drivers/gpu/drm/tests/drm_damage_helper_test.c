// SPDX-License-Identifier: GPL-2.0
/*
 * Test case for drm_damage_helper functions
 *
 * Copyright (c) 2022 Maíra Canal <mairacanal@riseup.net>
 */

#include <kunit/test.h>

#include <drm/drm_damage_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_plane.h>
#include <drm/drm_drv.h>

struct drm_damage_mock {
	struct drm_driver driver;
	struct drm_device device;
	struct drm_object_properties obj_props;
	struct drm_plane plane;
	struct drm_property prop;
	struct drm_framebuffer fb;
	struct drm_plane_state state;
	struct drm_plane_state old_state;
};

static int drm_damage_helper_init(struct kunit *test)
{
	struct drm_damage_mock *mock;

	mock = kunit_kzalloc(test, sizeof(*mock), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mock);

	mock->fb.width = 2048;
	mock->fb.height = 2048;

	mock->state.crtc = ZERO_SIZE_PTR;
	mock->state.fb = &mock->fb;
	mock->state.visible = true;

	mock->old_state.plane = &mock->plane;
	mock->state.plane = &mock->plane;

	/* just enough so that drm_plane_enable_fb_damage_clips() works */
	mock->device.driver = &mock->driver;
	mock->device.mode_config.prop_fb_damage_clips = &mock->prop;
	mock->plane.dev = &mock->device;
	mock->obj_props.count = 0;
	mock->plane.base.properties = &mock->obj_props;
	mock->prop.base.id = 1; /* 0 is an invalid id */
	mock->prop.dev = &mock->device;

	drm_plane_enable_fb_damage_clips(&mock->plane);

	test->priv = mock;

	return 0;
}

static void set_plane_src(struct drm_plane_state *state, int x1, int y1, int x2,
			  int y2)
{
	state->src_x = x1;
	state->src_y = y1;
	state->src_w = x2 - x1;
	state->src_h = y2 - y1;

	state->src.x1 = x1;
	state->src.y1 = y1;
	state->src.x2 = x2;
	state->src.y2 = y2;
}

static void set_damage_clip(struct drm_mode_rect *r, int x1, int y1, int x2,
			    int y2)
{
	r->x1 = x1;
	r->y1 = y1;
	r->x2 = x2;
	r->y2 = y2;
}

static void set_damage_blob(struct drm_property_blob *damage_blob,
			    struct drm_mode_rect *r, u32 size)
{
	damage_blob->length = size;
	damage_blob->data = r;
}

static void set_plane_damage(struct drm_plane_state *state,
			     struct drm_property_blob *damage_blob)
{
	state->fb_damage_clips = damage_blob;
}

static void check_damage_clip(struct kunit *test, struct drm_rect *r,
			      int x1, int y1, int x2, int y2)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_plane_state state = mock->state;

	/*
	 * Round down x1/y1 and round up x2/y2. This is because damage is not in
	 * 16.16 fixed point so to catch all pixels.
	 */
	int src_x1 = state.src.x1 >> 16;
	int src_y1 = state.src.y1 >> 16;
	int src_x2 = (state.src.x2 >> 16) + !!(state.src.x2 & 0xFFFF);
	int src_y2 = (state.src.y2 >> 16) + !!(state.src.y2 & 0xFFFF);

	if (x1 >= x2 || y1 >= y2)
		KUNIT_FAIL(test, "Cannot have damage clip with no dimension.");
	if (x1 < src_x1 || y1 < src_y1 || x2 > src_x2 || y2 > src_y2)
		KUNIT_FAIL(test, "Damage cannot be outside rounded plane src.");
	if (r->x1 != x1 || r->y1 != y1 || r->x2 != x2 || r->y2 != y2)
		KUNIT_FAIL(test, "Damage = %d %d %d %d, want = %d %d %d %d",
			   r->x1, r->y1, r->x2, r->y2, x1, y1, x2, y2);
}

static void drm_test_damage_iter_no_damage(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect clip;
	u32 num_hits = 0;

	/* Plane src same as fb size. */
	set_plane_src(&mock->old_state, 0, 0, mock->fb.width << 16, mock->fb.height << 16);
	set_plane_src(&mock->state, 0, 0, mock->fb.width << 16, mock->fb.height << 16);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 1, "Should return plane src as damage.");
	check_damage_clip(test, &clip, 0, 0, 2048, 2048);
}

static void drm_test_damage_iter_no_damage_fractional_src(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect clip;
	u32 num_hits = 0;

	/* Plane src has fractional part. */
	set_plane_src(&mock->old_state, 0x3fffe, 0x3fffe,
		      0x3fffe + (1024 << 16), 0x3fffe + (768 << 16));
	set_plane_src(&mock->state, 0x3fffe, 0x3fffe,
		      0x3fffe + (1024 << 16), 0x3fffe + (768 << 16));
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 1,
			    "Should return rounded off plane src as damage.");
	check_damage_clip(test, &clip, 3, 3, 1028, 772);
}

static void drm_test_damage_iter_no_damage_src_moved(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect clip;
	u32 num_hits = 0;

	/* Plane src moved since old plane state. */
	set_plane_src(&mock->old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&mock->state, 10 << 16, 10 << 16,
		      (10 + 1024) << 16, (10 + 768) << 16);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 1, "Should return plane src as damage.");
	check_damage_clip(test, &clip, 10, 10, 1034, 778);
}

static void drm_test_damage_iter_no_damage_fractional_src_moved(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect clip;
	u32 num_hits = 0;

	/* Plane src has fractional part and it moved since old plane state. */
	set_plane_src(&mock->old_state, 0x3fffe, 0x3fffe,
		      0x3fffe + (1024 << 16), 0x3fffe + (768 << 16));
	set_plane_src(&mock->state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 1, "Should return plane src as damage.");
	check_damage_clip(test, &clip, 4, 4, 1029, 773);
}

static void drm_test_damage_iter_no_damage_not_visible(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect clip;
	u32 num_hits = 0;

	mock->state.visible = false;

	set_plane_src(&mock->old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&mock->state, 0, 0, 1024 << 16, 768 << 16);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 0, "Should have no damage.");
}

static void drm_test_damage_iter_no_damage_no_crtc(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect clip;
	u32 num_hits = 0;

	mock->state.crtc = NULL;

	set_plane_src(&mock->old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&mock->state, 0, 0, 1024 << 16, 768 << 16);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 0, "Should have no damage.");
}

static void drm_test_damage_iter_no_damage_no_fb(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect clip;
	u32 num_hits = 0;

	mock->state.fb = NULL;

	set_plane_src(&mock->old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&mock->state, 0, 0, 1024 << 16, 768 << 16);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 0, "Should have no damage.");
}

static void drm_test_damage_iter_simple_damage(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	u32 num_hits = 0;

	set_plane_src(&mock->old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&mock->state, 0, 0, 1024 << 16, 768 << 16);
	/* Damage set to plane src */
	set_damage_clip(&damage, 0, 0, 1024, 768);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&mock->state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 1, "Should return damage when set.");
	check_damage_clip(test, &clip, 0, 0, 1024, 768);
}

static void drm_test_damage_iter_single_damage(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	u32 num_hits = 0;

	set_plane_src(&mock->old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&mock->state, 0, 0, 1024 << 16, 768 << 16);
	set_damage_clip(&damage, 256, 192, 768, 576);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&mock->state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 1, "Should return damage when set.");
	check_damage_clip(test, &clip, 256, 192, 768, 576);
}

static void drm_test_damage_iter_single_damage_intersect_src(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	u32 num_hits = 0;

	set_plane_src(&mock->old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&mock->state, 0, 0, 1024 << 16, 768 << 16);
	/* Damage intersect with plane src. */
	set_damage_clip(&damage, 256, 192, 1360, 768);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&mock->state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 1, "Should return damage clipped to src.");
	check_damage_clip(test, &clip, 256, 192, 1024, 768);
}

static void drm_test_damage_iter_single_damage_outside_src(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	u32 num_hits = 0;

	set_plane_src(&mock->old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&mock->state, 0, 0, 1024 << 16, 768 << 16);
	/* Damage clip outside plane src */
	set_damage_clip(&damage, 1360, 1360, 1380, 1380);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&mock->state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 0, "Should have no damage.");
}

static void drm_test_damage_iter_single_damage_fractional_src(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	u32 num_hits = 0;

	/* Plane src has fractional part. */
	set_plane_src(&mock->old_state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	set_plane_src(&mock->state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	set_damage_clip(&damage, 10, 10, 256, 330);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&mock->state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 1, "Should return damage when set.");
	check_damage_clip(test, &clip, 10, 10, 256, 330);
}

static void drm_test_damage_iter_single_damage_intersect_fractional_src(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	u32 num_hits = 0;

	/* Plane src has fractional part. */
	set_plane_src(&mock->old_state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	set_plane_src(&mock->state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	/* Damage intersect with plane src. */
	set_damage_clip(&damage, 10, 1, 1360, 330);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&mock->state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 1,
			    "Should return damage clipped to rounded off src.");
	check_damage_clip(test, &clip, 10, 4, 1029, 330);
}

static void drm_test_damage_iter_single_damage_outside_fractional_src(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	u32 num_hits = 0;

	/* Plane src has fractional part. */
	set_plane_src(&mock->old_state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	set_plane_src(&mock->state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	/* Damage clip outside plane src */
	set_damage_clip(&damage, 1360, 1360, 1380, 1380);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&mock->state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 0, "Should have no damage.");
}

static void drm_test_damage_iter_single_damage_src_moved(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	u32 num_hits = 0;

	/* Plane src moved since old plane state. */
	set_plane_src(&mock->old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&mock->state, 10 << 16, 10 << 16,
		      (10 + 1024) << 16, (10 + 768) << 16);
	set_damage_clip(&damage, 20, 30, 256, 256);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&mock->state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 1,
			    "Should return plane src as damage.");
	check_damage_clip(test, &clip, 10, 10, 1034, 778);
}

static void drm_test_damage_iter_single_damage_fractional_src_moved(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	u32 num_hits = 0;

	/* Plane src with fractional part moved since old plane state. */
	set_plane_src(&mock->old_state, 0x3fffe, 0x3fffe,
		      0x3fffe + (1024 << 16), 0x3fffe + (768 << 16));
	set_plane_src(&mock->state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	/* Damage intersect with plane src. */
	set_damage_clip(&damage, 20, 30, 1360, 256);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&mock->state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 1,
			    "Should return rounded off plane as damage.");
	check_damage_clip(test, &clip, 4, 4, 1029, 773);
}

static void drm_test_damage_iter_damage(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage[2];
	struct drm_rect clip;
	u32 num_hits = 0;

	set_plane_src(&mock->old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&mock->state, 0, 0, 1024 << 16, 768 << 16);
	/* 2 damage clips. */
	set_damage_clip(&damage[0], 20, 30, 200, 180);
	set_damage_clip(&damage[1], 240, 200, 280, 250);
	set_damage_blob(&damage_blob, &damage[0], sizeof(damage));
	set_plane_damage(&mock->state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip) {
		if (num_hits == 0)
			check_damage_clip(test, &clip, 20, 30, 200, 180);
		if (num_hits == 1)
			check_damage_clip(test, &clip, 240, 200, 280, 250);
		num_hits++;
	}

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 2, "Should return damage when set.");
}

static void drm_test_damage_iter_damage_one_intersect(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage[2];
	struct drm_rect clip;
	u32 num_hits = 0;

	set_plane_src(&mock->old_state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	set_plane_src(&mock->state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	/* 2 damage clips, one intersect plane src. */
	set_damage_clip(&damage[0], 20, 30, 200, 180);
	set_damage_clip(&damage[1], 2, 2, 1360, 1360);
	set_damage_blob(&damage_blob, &damage[0], sizeof(damage));
	set_plane_damage(&mock->state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip) {
		if (num_hits == 0)
			check_damage_clip(test, &clip, 20, 30, 200, 180);
		if (num_hits == 1)
			check_damage_clip(test, &clip, 4, 4, 1029, 773);
		num_hits++;
	}

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 2, "Should return damage when set.");
}

static void drm_test_damage_iter_damage_one_outside(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage[2];
	struct drm_rect clip;
	u32 num_hits = 0;

	set_plane_src(&mock->old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&mock->state, 0, 0, 1024 << 16, 768 << 16);
	/* 2 damage clips, one outside plane src. */
	set_damage_clip(&damage[0], 1360, 1360, 1380, 1380);
	set_damage_clip(&damage[1], 240, 200, 280, 250);
	set_damage_blob(&damage_blob, &damage[0], sizeof(damage));
	set_plane_damage(&mock->state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 1, "Should return damage when set.");
	check_damage_clip(test, &clip, 240, 200, 280, 250);
}

static void drm_test_damage_iter_damage_src_moved(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage[2];
	struct drm_rect clip;
	u32 num_hits = 0;

	set_plane_src(&mock->old_state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	set_plane_src(&mock->state, 0x3fffe, 0x3fffe,
		      0x3fffe + (1024 << 16), 0x3fffe + (768 << 16));
	/* 2 damage clips, one outside plane src. */
	set_damage_clip(&damage[0], 1360, 1360, 1380, 1380);
	set_damage_clip(&damage[1], 240, 200, 280, 250);
	set_damage_blob(&damage_blob, &damage[0], sizeof(damage));
	set_plane_damage(&mock->state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 1,
			    "Should return round off plane src as damage.");
	check_damage_clip(test, &clip, 3, 3, 1028, 772);
}

static void drm_test_damage_iter_damage_not_visible(struct kunit *test)
{
	struct drm_damage_mock *mock = test->priv;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage[2];
	struct drm_rect clip;
	u32 num_hits = 0;

	mock->state.visible = false;

	set_plane_src(&mock->old_state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	set_plane_src(&mock->state, 0x3fffe, 0x3fffe,
		      0x3fffe + (1024 << 16), 0x3fffe + (768 << 16));
	/* 2 damage clips, one outside plane src. */
	set_damage_clip(&damage[0], 1360, 1360, 1380, 1380);
	set_damage_clip(&damage[1], 240, 200, 280, 250);
	set_damage_blob(&damage_blob, &damage[0], sizeof(damage));
	set_plane_damage(&mock->state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &mock->old_state, &mock->state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	KUNIT_EXPECT_EQ_MSG(test, num_hits, 0, "Should not return any damage.");
}

static struct kunit_case drm_damage_helper_tests[] = {
	KUNIT_CASE(drm_test_damage_iter_no_damage),
	KUNIT_CASE(drm_test_damage_iter_no_damage_fractional_src),
	KUNIT_CASE(drm_test_damage_iter_no_damage_src_moved),
	KUNIT_CASE(drm_test_damage_iter_no_damage_fractional_src_moved),
	KUNIT_CASE(drm_test_damage_iter_no_damage_not_visible),
	KUNIT_CASE(drm_test_damage_iter_no_damage_no_crtc),
	KUNIT_CASE(drm_test_damage_iter_no_damage_no_fb),
	KUNIT_CASE(drm_test_damage_iter_simple_damage),
	KUNIT_CASE(drm_test_damage_iter_single_damage),
	KUNIT_CASE(drm_test_damage_iter_single_damage_intersect_src),
	KUNIT_CASE(drm_test_damage_iter_single_damage_outside_src),
	KUNIT_CASE(drm_test_damage_iter_single_damage_fractional_src),
	KUNIT_CASE(drm_test_damage_iter_single_damage_intersect_fractional_src),
	KUNIT_CASE(drm_test_damage_iter_single_damage_outside_fractional_src),
	KUNIT_CASE(drm_test_damage_iter_single_damage_src_moved),
	KUNIT_CASE(drm_test_damage_iter_single_damage_fractional_src_moved),
	KUNIT_CASE(drm_test_damage_iter_damage),
	KUNIT_CASE(drm_test_damage_iter_damage_one_intersect),
	KUNIT_CASE(drm_test_damage_iter_damage_one_outside),
	KUNIT_CASE(drm_test_damage_iter_damage_src_moved),
	KUNIT_CASE(drm_test_damage_iter_damage_not_visible),
	{ }
};

static struct kunit_suite drm_damage_helper_test_suite = {
	.name = "drm_damage_helper",
	.init = drm_damage_helper_init,
	.test_cases = drm_damage_helper_tests,
};

kunit_test_suite(drm_damage_helper_test_suite);

MODULE_DESCRIPTION("Test case for drm_damage_helper functions");
MODULE_LICENSE("GPL");
