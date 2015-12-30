/*
 * Copyright (C) 2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/**
 * DOC: VC4 plane module
 *
 * Each DRM plane is a layer of pixels being scanned out by the HVS.
 *
 * At atomic modeset check time, we compute the HVS display element
 * state that would be necessary for displaying the plane (giving us a
 * chance to figure out if a plane configuration is invalid), then at
 * atomic flush time the CRTC will ask us to write our element state
 * into the region of the HVS that it has allocated for us.
 */

#include "vc4_drv.h"
#include "vc4_regs.h"
#include "drm_atomic_helper.h"
#include "drm_fb_cma_helper.h"
#include "drm_plane_helper.h"

struct vc4_plane_state {
	struct drm_plane_state base;
	/* System memory copy of the display list for this element, computed
	 * at atomic_check time.
	 */
	u32 *dlist;
	u32 dlist_size; /* Number of dwords allocated for the display list */
	u32 dlist_count; /* Number of used dwords in the display list. */

	/* Offset in the dlist to various words, for pageflip or
	 * cursor updates.
	 */
	u32 pos0_offset;
	u32 pos2_offset;
	u32 ptr0_offset;

	/* Offset where the plane's dlist was last stored in the
	 * hardware at vc4_crtc_atomic_flush() time.
	 */
	u32 __iomem *hw_dlist;

	/* Clipped coordinates of the plane on the display. */
	int crtc_x, crtc_y, crtc_w, crtc_h;

	/* Offset to start scanning out from the start of the plane's
	 * BO.
	 */
	u32 offset;
};

static inline struct vc4_plane_state *
to_vc4_plane_state(struct drm_plane_state *state)
{
	return (struct vc4_plane_state *)state;
}

static const struct hvs_format {
	u32 drm; /* DRM_FORMAT_* */
	u32 hvs; /* HVS_FORMAT_* */
	u32 pixel_order;
	bool has_alpha;
} hvs_formats[] = {
	{
		.drm = DRM_FORMAT_XRGB8888, .hvs = HVS_PIXEL_FORMAT_RGBA8888,
		.pixel_order = HVS_PIXEL_ORDER_ABGR, .has_alpha = false,
	},
	{
		.drm = DRM_FORMAT_ARGB8888, .hvs = HVS_PIXEL_FORMAT_RGBA8888,
		.pixel_order = HVS_PIXEL_ORDER_ABGR, .has_alpha = true,
	},
};

static const struct hvs_format *vc4_get_hvs_format(u32 drm_format)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(hvs_formats); i++) {
		if (hvs_formats[i].drm == drm_format)
			return &hvs_formats[i];
	}

	return NULL;
}

static bool plane_enabled(struct drm_plane_state *state)
{
	return state->fb && state->crtc;
}

static struct drm_plane_state *vc4_plane_duplicate_state(struct drm_plane *plane)
{
	struct vc4_plane_state *vc4_state;

	if (WARN_ON(!plane->state))
		return NULL;

	vc4_state = kmemdup(plane->state, sizeof(*vc4_state), GFP_KERNEL);
	if (!vc4_state)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &vc4_state->base);

	if (vc4_state->dlist) {
		vc4_state->dlist = kmemdup(vc4_state->dlist,
					   vc4_state->dlist_count * 4,
					   GFP_KERNEL);
		if (!vc4_state->dlist) {
			kfree(vc4_state);
			return NULL;
		}
		vc4_state->dlist_size = vc4_state->dlist_count;
	}

	return &vc4_state->base;
}

static void vc4_plane_destroy_state(struct drm_plane *plane,
				    struct drm_plane_state *state)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);

	kfree(vc4_state->dlist);
	__drm_atomic_helper_plane_destroy_state(plane, &vc4_state->base);
	kfree(state);
}

/* Called during init to allocate the plane's atomic state. */
static void vc4_plane_reset(struct drm_plane *plane)
{
	struct vc4_plane_state *vc4_state;

	WARN_ON(plane->state);

	vc4_state = kzalloc(sizeof(*vc4_state), GFP_KERNEL);
	if (!vc4_state)
		return;

	plane->state = &vc4_state->base;
	vc4_state->base.plane = plane;
}

static void vc4_dlist_write(struct vc4_plane_state *vc4_state, u32 val)
{
	if (vc4_state->dlist_count == vc4_state->dlist_size) {
		u32 new_size = max(4u, vc4_state->dlist_count * 2);
		u32 *new_dlist = kmalloc(new_size * 4, GFP_KERNEL);

		if (!new_dlist)
			return;
		memcpy(new_dlist, vc4_state->dlist, vc4_state->dlist_count * 4);

		kfree(vc4_state->dlist);
		vc4_state->dlist = new_dlist;
		vc4_state->dlist_size = new_size;
	}

	vc4_state->dlist[vc4_state->dlist_count++] = val;
}

static int vc4_plane_setup_clipping_and_scaling(struct drm_plane_state *state)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	struct drm_framebuffer *fb = state->fb;

	vc4_state->offset = fb->offsets[0];

	vc4_state->crtc_x = state->crtc_x;
	vc4_state->crtc_y = state->crtc_y;
	vc4_state->crtc_w = state->crtc_w;
	vc4_state->crtc_h = state->crtc_h;

	if (state->crtc_w << 16 != state->src_w ||
	    state->crtc_h << 16 != state->src_h) {
		/* We don't support scaling yet, which involves
		 * allocating the LBM memory for scaling temporary
		 * storage, and putting filter kernels in the HVS
		 * context.
		 */
		return -EINVAL;
	}

	if (vc4_state->crtc_x < 0) {
		vc4_state->offset += (drm_format_plane_cpp(fb->pixel_format,
							   0) *
				      -vc4_state->crtc_x);
		vc4_state->crtc_w += vc4_state->crtc_x;
		vc4_state->crtc_x = 0;
	}

	if (vc4_state->crtc_y < 0) {
		vc4_state->offset += fb->pitches[0] * -vc4_state->crtc_y;
		vc4_state->crtc_h += vc4_state->crtc_y;
		vc4_state->crtc_y = 0;
	}

	return 0;
}


/* Writes out a full display list for an active plane to the plane's
 * private dlist state.
 */
static int vc4_plane_mode_set(struct drm_plane *plane,
			      struct drm_plane_state *state)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	struct drm_gem_cma_object *bo = drm_fb_cma_get_gem_obj(fb, 0);
	u32 ctl0_offset = vc4_state->dlist_count;
	const struct hvs_format *format = vc4_get_hvs_format(fb->pixel_format);
	int ret;

	ret = vc4_plane_setup_clipping_and_scaling(state);
	if (ret)
		return ret;

	vc4_dlist_write(vc4_state,
			SCALER_CTL0_VALID |
			(format->pixel_order << SCALER_CTL0_ORDER_SHIFT) |
			(format->hvs << SCALER_CTL0_PIXEL_FORMAT_SHIFT) |
			SCALER_CTL0_UNITY);

	/* Position Word 0: Image Positions and Alpha Value */
	vc4_state->pos0_offset = vc4_state->dlist_count;
	vc4_dlist_write(vc4_state,
			VC4_SET_FIELD(0xff, SCALER_POS0_FIXED_ALPHA) |
			VC4_SET_FIELD(vc4_state->crtc_x, SCALER_POS0_START_X) |
			VC4_SET_FIELD(vc4_state->crtc_y, SCALER_POS0_START_Y));

	/* Position Word 1: Scaled Image Dimensions.
	 * Skipped due to SCALER_CTL0_UNITY scaling.
	 */

	/* Position Word 2: Source Image Size, Alpha Mode */
	vc4_state->pos2_offset = vc4_state->dlist_count;
	vc4_dlist_write(vc4_state,
			VC4_SET_FIELD(format->has_alpha ?
				      SCALER_POS2_ALPHA_MODE_PIPELINE :
				      SCALER_POS2_ALPHA_MODE_FIXED,
				      SCALER_POS2_ALPHA_MODE) |
			VC4_SET_FIELD(vc4_state->crtc_w, SCALER_POS2_WIDTH) |
			VC4_SET_FIELD(vc4_state->crtc_h, SCALER_POS2_HEIGHT));

	/* Position Word 3: Context.  Written by the HVS. */
	vc4_dlist_write(vc4_state, 0xc0c0c0c0);

	/* Pointer Word 0: RGB / Y Pointer */
	vc4_state->ptr0_offset = vc4_state->dlist_count;
	vc4_dlist_write(vc4_state, bo->paddr + vc4_state->offset);

	/* Pointer Context Word 0: Written by the HVS */
	vc4_dlist_write(vc4_state, 0xc0c0c0c0);

	/* Pitch word 0: Pointer 0 Pitch */
	vc4_dlist_write(vc4_state,
			VC4_SET_FIELD(fb->pitches[0], SCALER_SRC_PITCH));

	vc4_state->dlist[ctl0_offset] |=
		VC4_SET_FIELD(vc4_state->dlist_count, SCALER_CTL0_SIZE);

	return 0;
}

/* If a modeset involves changing the setup of a plane, the atomic
 * infrastructure will call this to validate a proposed plane setup.
 * However, if a plane isn't getting updated, this (and the
 * corresponding vc4_plane_atomic_update) won't get called.  Thus, we
 * compute the dlist here and have all active plane dlists get updated
 * in the CRTC's flush.
 */
static int vc4_plane_atomic_check(struct drm_plane *plane,
				  struct drm_plane_state *state)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);

	vc4_state->dlist_count = 0;

	if (plane_enabled(state))
		return vc4_plane_mode_set(plane, state);
	else
		return 0;
}

static void vc4_plane_atomic_update(struct drm_plane *plane,
				    struct drm_plane_state *old_state)
{
	/* No contents here.  Since we don't know where in the CRTC's
	 * dlist we should be stored, our dlist is uploaded to the
	 * hardware with vc4_plane_write_dlist() at CRTC atomic_flush
	 * time.
	 */
}

u32 vc4_plane_write_dlist(struct drm_plane *plane, u32 __iomem *dlist)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(plane->state);
	int i;

	vc4_state->hw_dlist = dlist;

	/* Can't memcpy_toio() because it needs to be 32-bit writes. */
	for (i = 0; i < vc4_state->dlist_count; i++)
		writel(vc4_state->dlist[i], &dlist[i]);

	return vc4_state->dlist_count;
}

u32 vc4_plane_dlist_size(struct drm_plane_state *state)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);

	return vc4_state->dlist_count;
}

/* Updates the plane to immediately (well, once the FIFO needs
 * refilling) scan out from at a new framebuffer.
 */
void vc4_plane_async_set_fb(struct drm_plane *plane, struct drm_framebuffer *fb)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(plane->state);
	struct drm_gem_cma_object *bo = drm_fb_cma_get_gem_obj(fb, 0);
	uint32_t addr;

	/* We're skipping the address adjustment for negative origin,
	 * because this is only called on the primary plane.
	 */
	WARN_ON_ONCE(plane->state->crtc_x < 0 || plane->state->crtc_y < 0);
	addr = bo->paddr + fb->offsets[0];

	/* Write the new address into the hardware immediately.  The
	 * scanout will start from this address as soon as the FIFO
	 * needs to refill with pixels.
	 */
	writel(addr, &vc4_state->hw_dlist[vc4_state->ptr0_offset]);

	/* Also update the CPU-side dlist copy, so that any later
	 * atomic updates that don't do a new modeset on our plane
	 * also use our updated address.
	 */
	vc4_state->dlist[vc4_state->ptr0_offset] = addr;
}

static const struct drm_plane_helper_funcs vc4_plane_helper_funcs = {
	.prepare_fb = NULL,
	.cleanup_fb = NULL,
	.atomic_check = vc4_plane_atomic_check,
	.atomic_update = vc4_plane_atomic_update,
};

static void vc4_plane_destroy(struct drm_plane *plane)
{
	drm_plane_helper_disable(plane);
	drm_plane_cleanup(plane);
}

/* Implements immediate (non-vblank-synced) updates of the cursor
 * position, or falls back to the atomic helper otherwise.
 */
static int
vc4_update_plane(struct drm_plane *plane,
		 struct drm_crtc *crtc,
		 struct drm_framebuffer *fb,
		 int crtc_x, int crtc_y,
		 unsigned int crtc_w, unsigned int crtc_h,
		 uint32_t src_x, uint32_t src_y,
		 uint32_t src_w, uint32_t src_h)
{
	struct drm_plane_state *plane_state;
	struct vc4_plane_state *vc4_state;

	if (plane != crtc->cursor)
		goto out;

	plane_state = plane->state;
	vc4_state = to_vc4_plane_state(plane_state);

	if (!plane_state)
		goto out;

	/* If we're changing the cursor contents, do that in the
	 * normal vblank-synced atomic path.
	 */
	if (fb != plane_state->fb)
		goto out;

	/* No configuring new scaling in the fast path. */
	if (crtc_w != plane_state->crtc_w ||
	    crtc_h != plane_state->crtc_h ||
	    src_w != plane_state->src_w ||
	    src_h != plane_state->src_h) {
		goto out;
	}

	/* Set the cursor's position on the screen.  This is the
	 * expected change from the drm_mode_cursor_universal()
	 * helper.
	 */
	plane_state->crtc_x = crtc_x;
	plane_state->crtc_y = crtc_y;

	/* Allow changing the start position within the cursor BO, if
	 * that matters.
	 */
	plane_state->src_x = src_x;
	plane_state->src_y = src_y;

	/* Update the display list based on the new crtc_x/y. */
	vc4_plane_atomic_check(plane, plane_state);

	/* Note that we can't just call vc4_plane_write_dlist()
	 * because that would smash the context data that the HVS is
	 * currently using.
	 */
	writel(vc4_state->dlist[vc4_state->pos0_offset],
	       &vc4_state->hw_dlist[vc4_state->pos0_offset]);
	writel(vc4_state->dlist[vc4_state->pos2_offset],
	       &vc4_state->hw_dlist[vc4_state->pos2_offset]);
	writel(vc4_state->dlist[vc4_state->ptr0_offset],
	       &vc4_state->hw_dlist[vc4_state->ptr0_offset]);

	return 0;

out:
	return drm_atomic_helper_update_plane(plane, crtc, fb,
					      crtc_x, crtc_y,
					      crtc_w, crtc_h,
					      src_x, src_y,
					      src_w, src_h);
}

static const struct drm_plane_funcs vc4_plane_funcs = {
	.update_plane = vc4_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = vc4_plane_destroy,
	.set_property = NULL,
	.reset = vc4_plane_reset,
	.atomic_duplicate_state = vc4_plane_duplicate_state,
	.atomic_destroy_state = vc4_plane_destroy_state,
};

struct drm_plane *vc4_plane_init(struct drm_device *dev,
				 enum drm_plane_type type)
{
	struct drm_plane *plane = NULL;
	struct vc4_plane *vc4_plane;
	u32 formats[ARRAY_SIZE(hvs_formats)];
	int ret = 0;
	unsigned i;

	vc4_plane = devm_kzalloc(dev->dev, sizeof(*vc4_plane),
				 GFP_KERNEL);
	if (!vc4_plane) {
		ret = -ENOMEM;
		goto fail;
	}

	for (i = 0; i < ARRAY_SIZE(hvs_formats); i++)
		formats[i] = hvs_formats[i].drm;
	plane = &vc4_plane->base;
	ret = drm_universal_plane_init(dev, plane, 0xff,
				       &vc4_plane_funcs,
				       formats, ARRAY_SIZE(formats),
				       type, NULL);

	drm_plane_helper_add(plane, &vc4_plane_helper_funcs);

	return plane;
fail:
	if (plane)
		vc4_plane_destroy(plane);

	return ERR_PTR(ret);
}
