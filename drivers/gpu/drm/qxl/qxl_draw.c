/*
 * Copyright 2011 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "qxl_drv.h"
#include "qxl_object.h"

/* returns a pointer to the already allocated qxl_rect array inside
 * the qxl_clip_rects. This is *not* the same as the memory allocated
 * on the device, it is offset to qxl_clip_rects.chunk.data */
static struct qxl_rect *drawable_set_clipping(struct qxl_device *qdev,
					      struct qxl_drawable *drawable,
					      unsigned num_clips,
					      struct qxl_bo **clips_bo,
					      struct qxl_release *release)
{
	struct qxl_clip_rects *dev_clips;
	int ret;
	int size = sizeof(*dev_clips) + sizeof(struct qxl_rect) * num_clips;
	ret = qxl_alloc_bo_reserved(qdev, size, clips_bo);
	if (ret)
		return NULL;

	ret = qxl_bo_kmap(*clips_bo, (void **)&dev_clips);
	if (ret) {
		qxl_bo_unref(clips_bo);
		return NULL;
	}
	dev_clips->num_rects = num_clips;
	dev_clips->chunk.next_chunk = 0;
	dev_clips->chunk.prev_chunk = 0;
	dev_clips->chunk.data_size = sizeof(struct qxl_rect) * num_clips;
	return (struct qxl_rect *)dev_clips->chunk.data;
}

static int
make_drawable(struct qxl_device *qdev, int surface, uint8_t type,
	      const struct qxl_rect *rect,
	      struct qxl_release **release)
{
	struct qxl_drawable *drawable;
	int i, ret;

	ret = qxl_alloc_release_reserved(qdev, sizeof(*drawable),
					 QXL_RELEASE_DRAWABLE, release,
					 NULL);
	if (ret)
		return ret;

	drawable = (struct qxl_drawable *)qxl_release_map(qdev, *release);
	drawable->type = type;

	drawable->surface_id = surface;		/* Only primary for now */
	drawable->effect = QXL_EFFECT_OPAQUE;
	drawable->self_bitmap = 0;
	drawable->self_bitmap_area.top = 0;
	drawable->self_bitmap_area.left = 0;
	drawable->self_bitmap_area.bottom = 0;
	drawable->self_bitmap_area.right = 0;
	/* FIXME: add clipping */
	drawable->clip.type = SPICE_CLIP_TYPE_NONE;

	/*
	 * surfaces_dest[i] should apparently be filled out with the
	 * surfaces that we depend on, and surface_rects should be
	 * filled with the rectangles of those surfaces that we
	 * are going to use.
	 */
	for (i = 0; i < 3; ++i)
		drawable->surfaces_dest[i] = -1;

	if (rect)
		drawable->bbox = *rect;

	drawable->mm_time = qdev->rom->mm_clock;
	qxl_release_unmap(qdev, *release, &drawable->release_info);
	return 0;
}

static int qxl_palette_create_1bit(struct qxl_bo **palette_bo,
				   const struct qxl_fb_image *qxl_fb_image)
{
	struct qxl_device *qdev = qxl_fb_image->qdev;
	const struct fb_image *fb_image = &qxl_fb_image->fb_image;
	uint32_t visual = qxl_fb_image->visual;
	const uint32_t *pseudo_palette = qxl_fb_image->pseudo_palette;
	struct qxl_palette *pal;
	int ret;
	uint32_t fgcolor, bgcolor;
	static uint64_t unique; /* we make no attempt to actually set this
				 * correctly globaly, since that would require
				 * tracking all of our palettes. */

	ret = qxl_alloc_bo_reserved(qdev,
				    sizeof(struct qxl_palette) + sizeof(uint32_t) * 2,
				    palette_bo);

	ret = qxl_bo_kmap(*palette_bo, (void **)&pal);
	pal->num_ents = 2;
	pal->unique = unique++;
	if (visual == FB_VISUAL_TRUECOLOR || visual == FB_VISUAL_DIRECTCOLOR) {
		/* NB: this is the only used branch currently. */
		fgcolor = pseudo_palette[fb_image->fg_color];
		bgcolor = pseudo_palette[fb_image->bg_color];
	} else {
		fgcolor = fb_image->fg_color;
		bgcolor = fb_image->bg_color;
	}
	pal->ents[0] = bgcolor;
	pal->ents[1] = fgcolor;
	qxl_bo_kunmap(*palette_bo);
	return 0;
}

void qxl_draw_opaque_fb(const struct qxl_fb_image *qxl_fb_image,
			int stride /* filled in if 0 */)
{
	struct qxl_device *qdev = qxl_fb_image->qdev;
	struct qxl_drawable *drawable;
	struct qxl_rect rect;
	const struct fb_image *fb_image = &qxl_fb_image->fb_image;
	int x = fb_image->dx;
	int y = fb_image->dy;
	int width = fb_image->width;
	int height = fb_image->height;
	const char *src = fb_image->data;
	int depth = fb_image->depth;
	struct qxl_release *release;
	struct qxl_bo *image_bo;
	struct qxl_image *image;
	int ret;

	if (stride == 0)
		stride = depth * width / 8;

	rect.left = x;
	rect.right = x + width;
	rect.top = y;
	rect.bottom = y + height;

	ret = make_drawable(qdev, 0, QXL_DRAW_COPY, &rect, &release);
	if (ret)
		return;

	ret = qxl_image_create(qdev, release, &image_bo,
			       (const uint8_t *)src, 0, 0,
			       width, height, depth, stride);
	if (ret) {
		qxl_release_unreserve(qdev, release);
		qxl_release_free(qdev, release);
		return;
	}

	if (depth == 1) {
		struct qxl_bo *palette_bo;
		void *ptr;
		ret = qxl_palette_create_1bit(&palette_bo, qxl_fb_image);
		qxl_release_add_res(qdev, release, palette_bo);

		ptr = qxl_bo_kmap_atomic_page(qdev, image_bo, 0);
		image = ptr;
		image->u.bitmap.palette =
			qxl_bo_physical_address(qdev, palette_bo, 0);
		qxl_bo_kunmap_atomic_page(qdev, image_bo, ptr);
		qxl_bo_unreserve(palette_bo);
		qxl_bo_unref(&palette_bo);
	}

	drawable = (struct qxl_drawable *)qxl_release_map(qdev, release);

	drawable->u.copy.src_area.top = 0;
	drawable->u.copy.src_area.bottom = height;
	drawable->u.copy.src_area.left = 0;
	drawable->u.copy.src_area.right = width;

	drawable->u.copy.rop_descriptor = SPICE_ROPD_OP_PUT;
	drawable->u.copy.scale_mode = 0;
	drawable->u.copy.mask.flags = 0;
	drawable->u.copy.mask.pos.x = 0;
	drawable->u.copy.mask.pos.y = 0;
	drawable->u.copy.mask.bitmap = 0;

	drawable->u.copy.src_bitmap =
		qxl_bo_physical_address(qdev, image_bo, 0);
	qxl_release_unmap(qdev, release, &drawable->release_info);

	qxl_release_add_res(qdev, release, image_bo);
	qxl_bo_unreserve(image_bo);
	qxl_bo_unref(&image_bo);

	qxl_fence_releaseable(qdev, release);
	qxl_push_command_ring_release(qdev, release, QXL_CMD_DRAW, false);
	qxl_release_unreserve(qdev, release);
}

/* push a draw command using the given clipping rectangles as
 * the sources from the shadow framebuffer.
 *
 * Right now implementing with a single draw and a clip list. Clip
 * lists are known to be a problem performance wise, this can be solved
 * by treating them differently in the server.
 */
void qxl_draw_dirty_fb(struct qxl_device *qdev,
		       struct qxl_framebuffer *qxl_fb,
		       struct qxl_bo *bo,
		       unsigned flags, unsigned color,
		       struct drm_clip_rect *clips,
		       unsigned num_clips, int inc)
{
	/*
	 * TODO: if flags & DRM_MODE_FB_DIRTY_ANNOTATE_FILL then we should
	 * send a fill command instead, much cheaper.
	 *
	 * See include/drm/drm_mode.h
	 */
	struct drm_clip_rect *clips_ptr;
	int i;
	int left, right, top, bottom;
	int width, height;
	struct qxl_drawable *drawable;
	struct qxl_rect drawable_rect;
	struct qxl_rect *rects;
	int stride = qxl_fb->base.pitches[0];
	/* depth is not actually interesting, we don't mask with it */
	int depth = qxl_fb->base.bits_per_pixel;
	uint8_t *surface_base;
	struct qxl_release *release;
	struct qxl_bo *image_bo;
	struct qxl_bo *clips_bo;
	int ret;

	left = clips->x1;
	right = clips->x2;
	top = clips->y1;
	bottom = clips->y2;

	/* skip the first clip rect */
	for (i = 1, clips_ptr = clips + inc;
	     i < num_clips; i++, clips_ptr += inc) {
		left = min_t(int, left, (int)clips_ptr->x1);
		right = max_t(int, right, (int)clips_ptr->x2);
		top = min_t(int, top, (int)clips_ptr->y1);
		bottom = max_t(int, bottom, (int)clips_ptr->y2);
	}

	width = right - left;
	height = bottom - top;
	drawable_rect.left = left;
	drawable_rect.right = right;
	drawable_rect.top = top;
	drawable_rect.bottom = bottom;
	ret = make_drawable(qdev, 0, QXL_DRAW_COPY, &drawable_rect,
			    &release);
	if (ret)
		return;

	ret = qxl_bo_kmap(bo, (void **)&surface_base);
	if (ret)
		goto out_unref;

	ret = qxl_image_create(qdev, release, &image_bo, surface_base,
			       left, top, width, height, depth, stride);
	qxl_bo_kunmap(bo);
	if (ret)
		goto out_unref;

	rects = drawable_set_clipping(qdev, drawable, num_clips, &clips_bo, release);
	if (!rects) {
		qxl_bo_unref(&image_bo);
		goto out_unref;
	}
	drawable = (struct qxl_drawable *)qxl_release_map(qdev, release);

	drawable->clip.type = SPICE_CLIP_TYPE_RECTS;
	drawable->clip.data = qxl_bo_physical_address(qdev,
						      clips_bo, 0);
	qxl_release_add_res(qdev, release, clips_bo);

	drawable->u.copy.src_area.top = 0;
	drawable->u.copy.src_area.bottom = height;
	drawable->u.copy.src_area.left = 0;
	drawable->u.copy.src_area.right = width;

	drawable->u.copy.rop_descriptor = SPICE_ROPD_OP_PUT;
	drawable->u.copy.scale_mode = 0;
	drawable->u.copy.mask.flags = 0;
	drawable->u.copy.mask.pos.x = 0;
	drawable->u.copy.mask.pos.y = 0;
	drawable->u.copy.mask.bitmap = 0;

	drawable->u.copy.src_bitmap = qxl_bo_physical_address(qdev, image_bo, 0);
	qxl_release_unmap(qdev, release, &drawable->release_info);
	qxl_release_add_res(qdev, release, image_bo);
	qxl_bo_unreserve(image_bo);
	qxl_bo_unref(&image_bo);
	clips_ptr = clips;
	for (i = 0; i < num_clips; i++, clips_ptr += inc) {
		rects[i].left   = clips_ptr->x1;
		rects[i].right  = clips_ptr->x2;
		rects[i].top    = clips_ptr->y1;
		rects[i].bottom = clips_ptr->y2;
	}
	qxl_bo_kunmap(clips_bo);
	qxl_bo_unreserve(clips_bo);
	qxl_bo_unref(&clips_bo);

	qxl_fence_releaseable(qdev, release);
	qxl_push_command_ring_release(qdev, release, QXL_CMD_DRAW, false);
	qxl_release_unreserve(qdev, release);
	return;

out_unref:
	qxl_release_unreserve(qdev, release);
	qxl_release_free(qdev, release);
}

void qxl_draw_copyarea(struct qxl_device *qdev,
		       u32 width, u32 height,
		       u32 sx, u32 sy,
		       u32 dx, u32 dy)
{
	struct qxl_drawable *drawable;
	struct qxl_rect rect;
	struct qxl_release *release;
	int ret;

	rect.left = dx;
	rect.top = dy;
	rect.right = dx + width;
	rect.bottom = dy + height;
	ret = make_drawable(qdev, 0, QXL_COPY_BITS, &rect, &release);
	if (ret)
		return;

	drawable = (struct qxl_drawable *)qxl_release_map(qdev, release);
	drawable->u.copy_bits.src_pos.x = sx;
	drawable->u.copy_bits.src_pos.y = sy;

	qxl_release_unmap(qdev, release, &drawable->release_info);
	qxl_fence_releaseable(qdev, release);
	qxl_push_command_ring_release(qdev, release, QXL_CMD_DRAW, false);
	qxl_release_unreserve(qdev, release);
}

void qxl_draw_fill(struct qxl_draw_fill *qxl_draw_fill_rec)
{
	struct qxl_device *qdev = qxl_draw_fill_rec->qdev;
	struct qxl_rect rect = qxl_draw_fill_rec->rect;
	uint32_t color = qxl_draw_fill_rec->color;
	uint16_t rop = qxl_draw_fill_rec->rop;
	struct qxl_drawable *drawable;
	struct qxl_release *release;
	int ret;

	ret = make_drawable(qdev, 0, QXL_DRAW_FILL, &rect, &release);
	if (ret)
		return;

	drawable = (struct qxl_drawable *)qxl_release_map(qdev, release);
	drawable->u.fill.brush.type = SPICE_BRUSH_TYPE_SOLID;
	drawable->u.fill.brush.u.color = color;
	drawable->u.fill.rop_descriptor = rop;
	drawable->u.fill.mask.flags = 0;
	drawable->u.fill.mask.pos.x = 0;
	drawable->u.fill.mask.pos.y = 0;
	drawable->u.fill.mask.bitmap = 0;

	qxl_release_unmap(qdev, release, &drawable->release_info);
	qxl_fence_releaseable(qdev, release);
	qxl_push_command_ring_release(qdev, release, QXL_CMD_DRAW, false);
	qxl_release_unreserve(qdev, release);
}
