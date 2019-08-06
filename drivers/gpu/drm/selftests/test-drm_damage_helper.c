// SPDX-License-Identifier: GPL-2.0
/*
 * Test case for drm_damage_helper functions
 */

#define pr_fmt(fmt) "drm_damage_helper: " fmt

#include <drm/drm_damage_helper.h>

#include "test-drm_modeset_common.h"

static void set_plane_src(struct drm_plane_state *state, int x1, int y1, int x2,
			  int y2)
{
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
			    struct drm_mode_rect *r, uint32_t size)
{
	damage_blob->length = size;
	damage_blob->data = r;
}

static void set_plane_damage(struct drm_plane_state *state,
			     struct drm_property_blob *damage_blob)
{
	state->fb_damage_clips = damage_blob;
}

static bool check_damage_clip(struct drm_plane_state *state, struct drm_rect *r,
			      int x1, int y1, int x2, int y2)
{
	/*
	 * Round down x1/y1 and round up x2/y2. This is because damage is not in
	 * 16.16 fixed point so to catch all pixels.
	 */
	int src_x1 = state->src.x1 >> 16;
	int src_y1 = state->src.y1 >> 16;
	int src_x2 = (state->src.x2 >> 16) + !!(state->src.x2 & 0xFFFF);
	int src_y2 = (state->src.y2 >> 16) + !!(state->src.y2 & 0xFFFF);

	if (x1 >= x2 || y1 >= y2) {
		pr_err("Cannot have damage clip with no dimension.\n");
		return false;
	}

	if (x1 < src_x1 || y1 < src_y1 || x2 > src_x2 || y2 > src_y2) {
		pr_err("Damage cannot be outside rounded plane src.\n");
		return false;
	}

	if (r->x1 != x1 || r->y1 != y1 || r->x2 != x2 || r->y2 != y2) {
		pr_err("Damage = %d %d %d %d\n", r->x1, r->y1, r->x2, r->y2);
		return false;
	}

	return true;
}

int igt_damage_iter_no_damage(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	/* Plane src same as fb size. */
	set_plane_src(&old_state, 0, 0, fb.width << 16, fb.height << 16);
	set_plane_src(&state, 0, 0, fb.width << 16, fb.height << 16);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 1, "Should return plane src as damage.");
	FAIL_ON(!check_damage_clip(&state, &clip, 0, 0, 2048, 2048));

	return 0;
}

int igt_damage_iter_no_damage_fractional_src(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	/* Plane src has fractional part. */
	set_plane_src(&old_state, 0x3fffe, 0x3fffe,
		      0x3fffe + (1024 << 16), 0x3fffe + (768 << 16));
	set_plane_src(&state, 0x3fffe, 0x3fffe,
		      0x3fffe + (1024 << 16), 0x3fffe + (768 << 16));
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 1, "Should return rounded off plane src as damage.");
	FAIL_ON(!check_damage_clip(&state, &clip, 3, 3, 1028, 772));

	return 0;
}

int igt_damage_iter_no_damage_src_moved(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	/* Plane src moved since old plane state. */
	set_plane_src(&old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&state, 10 << 16, 10 << 16,
		      (10 + 1024) << 16, (10 + 768) << 16);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 1, "Should return plane src as damage.");
	FAIL_ON(!check_damage_clip(&state, &clip, 10, 10, 1034, 778));

	return 0;
}

int igt_damage_iter_no_damage_fractional_src_moved(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	/* Plane src has fractional part and it moved since old plane state. */
	set_plane_src(&old_state, 0x3fffe, 0x3fffe,
		      0x3fffe + (1024 << 16), 0x3fffe + (768 << 16));
	set_plane_src(&state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 1, "Should return plane src as damage.");
	FAIL_ON(!check_damage_clip(&state, &clip, 4, 4, 1029, 773));

	return 0;
}

int igt_damage_iter_no_damage_not_visible(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = false,
	};

	set_plane_src(&old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&state, 0, 0, 1024 << 16, 768 << 16);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 0, "Should have no damage.");

	return 0;
}

int igt_damage_iter_no_damage_no_crtc(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = 0,
		.fb = &fb,
	};

	set_plane_src(&old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&state, 0, 0, 1024 << 16, 768 << 16);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 0, "Should have no damage.");

	return 0;
}

int igt_damage_iter_no_damage_no_fb(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = 0,
	};

	set_plane_src(&old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&state, 0, 0, 1024 << 16, 768 << 16);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 0, "Should have no damage.");

	return 0;
}

int igt_damage_iter_simple_damage(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	set_plane_src(&old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&state, 0, 0, 1024 << 16, 768 << 16);
	/* Damage set to plane src */
	set_damage_clip(&damage, 0, 0, 1024, 768);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 1, "Should return damage when set.");
	FAIL_ON(!check_damage_clip(&state, &clip, 0, 0, 1024, 768));

	return 0;
}

int igt_damage_iter_single_damage(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	set_plane_src(&old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&state, 0, 0, 1024 << 16, 768 << 16);
	set_damage_clip(&damage, 256, 192, 768, 576);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 1, "Should return damage when set.");
	FAIL_ON(!check_damage_clip(&state, &clip, 256, 192, 768, 576));

	return 0;
}

int igt_damage_iter_single_damage_intersect_src(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	set_plane_src(&old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&state, 0, 0, 1024 << 16, 768 << 16);
	/* Damage intersect with plane src. */
	set_damage_clip(&damage, 256, 192, 1360, 768);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 1, "Should return damage clipped to src.");
	FAIL_ON(!check_damage_clip(&state, &clip, 256, 192, 1024, 768));

	return 0;
}

int igt_damage_iter_single_damage_outside_src(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	set_plane_src(&old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&state, 0, 0, 1024 << 16, 768 << 16);
	/* Damage clip outside plane src */
	set_damage_clip(&damage, 1360, 1360, 1380, 1380);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 0, "Should have no damage.");

	return 0;
}

int igt_damage_iter_single_damage_fractional_src(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	/* Plane src has fractional part. */
	set_plane_src(&old_state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	set_plane_src(&state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	set_damage_clip(&damage, 10, 10, 256, 330);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 1, "Should return damage when set.");
	FAIL_ON(!check_damage_clip(&state, &clip, 10, 10, 256, 330));

	return 0;
}

int igt_damage_iter_single_damage_intersect_fractional_src(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	/* Plane src has fractional part. */
	set_plane_src(&old_state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	set_plane_src(&state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	/* Damage intersect with plane src. */
	set_damage_clip(&damage, 10, 1, 1360, 330);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 1, "Should return damage clipped to rounded off src.");
	FAIL_ON(!check_damage_clip(&state, &clip, 10, 4, 1029, 330));

	return 0;
}

int igt_damage_iter_single_damage_outside_fractional_src(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	/* Plane src has fractional part. */
	set_plane_src(&old_state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	set_plane_src(&state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	/* Damage clip outside plane src */
	set_damage_clip(&damage, 1360, 1360, 1380, 1380);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 0, "Should have no damage.");

	return 0;
}

int igt_damage_iter_single_damage_src_moved(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	/* Plane src moved since old plane state. */
	set_plane_src(&old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&state, 10 << 16, 10 << 16,
		      (10 + 1024) << 16, (10 + 768) << 16);
	set_damage_clip(&damage, 20, 30, 256, 256);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 1, "Should return plane src as damage.");
	FAIL_ON(!check_damage_clip(&state, &clip, 10, 10, 1034, 778));

	return 0;
}

int igt_damage_iter_single_damage_fractional_src_moved(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage;
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	/* Plane src with fractional part moved since old plane state. */
	set_plane_src(&old_state, 0x3fffe, 0x3fffe,
		      0x3fffe + (1024 << 16), 0x3fffe + (768 << 16));
	set_plane_src(&state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	/* Damage intersect with plane src. */
	set_damage_clip(&damage, 20, 30, 1360, 256);
	set_damage_blob(&damage_blob, &damage, sizeof(damage));
	set_plane_damage(&state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 1, "Should return rounded off plane src as damage.");
	FAIL_ON(!check_damage_clip(&state, &clip, 4, 4, 1029, 773));

	return 0;
}

int igt_damage_iter_damage(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage[2];
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	set_plane_src(&old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&state, 0, 0, 1024 << 16, 768 << 16);
	/* 2 damage clips. */
	set_damage_clip(&damage[0], 20, 30, 200, 180);
	set_damage_clip(&damage[1], 240, 200, 280, 250);
	set_damage_blob(&damage_blob, &damage[0], sizeof(damage));
	set_plane_damage(&state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip) {
		if (num_hits == 0)
			FAIL_ON(!check_damage_clip(&state, &clip, 20, 30, 200, 180));
		if (num_hits == 1)
			FAIL_ON(!check_damage_clip(&state, &clip, 240, 200, 280, 250));
		num_hits++;
	}

	FAIL(num_hits != 2, "Should return damage when set.");

	return 0;
}

int igt_damage_iter_damage_one_intersect(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage[2];
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	set_plane_src(&old_state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	set_plane_src(&state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	/* 2 damage clips, one intersect plane src. */
	set_damage_clip(&damage[0], 20, 30, 200, 180);
	set_damage_clip(&damage[1], 2, 2, 1360, 1360);
	set_damage_blob(&damage_blob, &damage[0], sizeof(damage));
	set_plane_damage(&state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip) {
		if (num_hits == 0)
			FAIL_ON(!check_damage_clip(&state, &clip, 20, 30, 200, 180));
		if (num_hits == 1)
			FAIL_ON(!check_damage_clip(&state, &clip, 4, 4, 1029, 773));
		num_hits++;
	}

	FAIL(num_hits != 2, "Should return damage when set.");

	return 0;
}

int igt_damage_iter_damage_one_outside(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage[2];
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	set_plane_src(&old_state, 0, 0, 1024 << 16, 768 << 16);
	set_plane_src(&state, 0, 0, 1024 << 16, 768 << 16);
	/* 2 damage clips, one outside plane src. */
	set_damage_clip(&damage[0], 1360, 1360, 1380, 1380);
	set_damage_clip(&damage[1], 240, 200, 280, 250);
	set_damage_blob(&damage_blob, &damage[0], sizeof(damage));
	set_plane_damage(&state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 1, "Should return damage when set.");
	FAIL_ON(!check_damage_clip(&state, &clip, 240, 200, 280, 250));

	return 0;
}

int igt_damage_iter_damage_src_moved(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage[2];
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = true,
	};

	set_plane_src(&old_state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	set_plane_src(&state, 0x3fffe, 0x3fffe,
		      0x3fffe + (1024 << 16), 0x3fffe + (768 << 16));
	/* 2 damage clips, one outside plane src. */
	set_damage_clip(&damage[0], 1360, 1360, 1380, 1380);
	set_damage_clip(&damage[1], 240, 200, 280, 250);
	set_damage_blob(&damage_blob, &damage[0], sizeof(damage));
	set_plane_damage(&state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 1, "Should return round off plane src as damage.");
	FAIL_ON(!check_damage_clip(&state, &clip, 3, 3, 1028, 772));

	return 0;
}

int igt_damage_iter_damage_not_visible(void *ignored)
{
	struct drm_atomic_helper_damage_iter iter;
	struct drm_plane_state old_state;
	struct drm_property_blob damage_blob;
	struct drm_mode_rect damage[2];
	struct drm_rect clip;
	uint32_t num_hits = 0;

	struct drm_framebuffer fb = {
		.width = 2048,
		.height = 2048
	};

	struct drm_plane_state state = {
		.crtc = ZERO_SIZE_PTR,
		.fb = &fb,
		.visible = false,
	};

	set_plane_src(&old_state, 0x40002, 0x40002,
		      0x40002 + (1024 << 16), 0x40002 + (768 << 16));
	set_plane_src(&state, 0x3fffe, 0x3fffe,
		      0x3fffe + (1024 << 16), 0x3fffe + (768 << 16));
	/* 2 damage clips, one outside plane src. */
	set_damage_clip(&damage[0], 1360, 1360, 1380, 1380);
	set_damage_clip(&damage[1], 240, 200, 280, 250);
	set_damage_blob(&damage_blob, &damage[0], sizeof(damage));
	set_plane_damage(&state, &damage_blob);
	drm_atomic_helper_damage_iter_init(&iter, &old_state, &state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	FAIL(num_hits != 0, "Should not return any damage.");

	return 0;
}
