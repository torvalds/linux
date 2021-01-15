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

#include <linux/dma-buf-map.h>

#include <drm/drm_fourcc.h>

#include "qxl_drv.h"
#include "qxl_object.h"

static int alloc_clips(struct qxl_device *qdev,
		       struct qxl_release *release,
		       unsigned int num_clips,
		       struct qxl_bo **clips_bo)
{
	int size = sizeof(struct qxl_clip_rects) + sizeof(struct qxl_rect) * num_clips;

	return qxl_alloc_bo_reserved(qdev, release, size, clips_bo);
}

/* returns a pointer to the already allocated qxl_rect array inside
 * the qxl_clip_rects. This is *not* the same as the memory allocated
 * on the device, it is offset to qxl_clip_rects.chunk.data */
static struct qxl_rect *drawable_set_clipping(struct qxl_device *qdev,
					      unsigned int num_clips,
					      struct qxl_bo *clips_bo)
{
	struct dma_buf_map map;
	struct qxl_clip_rects *dev_clips;
	int ret;

	ret = qxl_bo_kmap(clips_bo, &map);
	if (ret)
		return NULL;
	dev_clips = map.vaddr; /* TODO: Use mapping abstraction properly */

	dev_clips->num_rects = num_clips;
	dev_clips->chunk.next_chunk = 0;
	dev_clips->chunk.prev_chunk = 0;
	dev_clips->chunk.data_size = sizeof(struct qxl_rect) * num_clips;
	return (struct qxl_rect *)dev_clips->chunk.data;
}

static int
alloc_drawable(struct qxl_device *qdev, struct qxl_release **release)
{
	return qxl_alloc_release_reserved(qdev, sizeof(struct qxl_drawable),
					  QXL_RELEASE_DRAWABLE, release, NULL);
}

static void
free_drawable(struct qxl_device *qdev, struct qxl_release *release)
{
	qxl_release_free(qdev, release);
}

/* release needs to be reserved at this point */
static int
make_drawable(struct qxl_device *qdev, int surface, uint8_t type,
	      const struct qxl_rect *rect,
	      struct qxl_release *release)
{
	struct qxl_drawable *drawable;
	int i;

	drawable = (struct qxl_drawable *)qxl_release_map(qdev, release);
	if (!drawable)
		return -ENOMEM;

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
	qxl_release_unmap(qdev, release, &drawable->release_info);
	return 0;
}

/* push a draw command using the given clipping rectangles as
 * the sources from the shadow framebuffer.
 *
 * Right now implementing with a single draw and a clip list. Clip
 * lists are known to be a problem performance wise, this can be solved
 * by treating them differently in the server.
 */
void qxl_draw_dirty_fb(struct qxl_device *qdev,
		       struct drm_framebuffer *fb,
		       struct qxl_bo *bo,
		       unsigned int flags, unsigned int color,
		       struct drm_clip_rect *clips,
		       unsigned int num_clips, int inc,
		       uint32_t dumb_shadow_offset)
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
	int stride = fb->pitches[0];
	/* depth is not actually interesting, we don't mask with it */
	int depth = fb->format->cpp[0] * 8;
	struct dma_buf_map surface_map;
	uint8_t *surface_base;
	struct qxl_release *release;
	struct qxl_bo *clips_bo;
	struct qxl_drm_image *dimage;
	int ret;

	ret = alloc_drawable(qdev, &release);
	if (ret)
		return;

	clips->x1 += dumb_shadow_offset;
	clips->x2 += dumb_shadow_offset;

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

	ret = alloc_clips(qdev, release, num_clips, &clips_bo);
	if (ret)
		goto out_free_drawable;

	ret = qxl_image_alloc_objects(qdev, release,
				      &dimage,
				      height, stride);
	if (ret)
		goto out_free_clips;

	/* do a reservation run over all the objects we just allocated */
	ret = qxl_release_reserve_list(release, true);
	if (ret)
		goto out_free_image;

	drawable_rect.left = left;
	drawable_rect.right = right;
	drawable_rect.top = top;
	drawable_rect.bottom = bottom;

	ret = make_drawable(qdev, 0, QXL_DRAW_COPY, &drawable_rect,
			    release);
	if (ret)
		goto out_release_backoff;

	ret = qxl_bo_kmap(bo, &surface_map);
	if (ret)
		goto out_release_backoff;
	surface_base = surface_map.vaddr; /* TODO: Use mapping abstraction properly */

	ret = qxl_image_init(qdev, release, dimage, surface_base,
			     left - dumb_shadow_offset,
			     top, width, height, depth, stride);
	qxl_bo_kunmap(bo);
	if (ret)
		goto out_release_backoff;

	rects = drawable_set_clipping(qdev, num_clips, clips_bo);
	if (!rects) {
		ret = -EINVAL;
		goto out_release_backoff;
	}
	drawable = (struct qxl_drawable *)qxl_release_map(qdev, release);

	drawable->clip.type = SPICE_CLIP_TYPE_RECTS;
	drawable->clip.data = qxl_bo_physical_address(qdev,
						      clips_bo, 0);

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

	drawable->u.copy.src_bitmap = qxl_bo_physical_address(qdev, dimage->bo, 0);
	qxl_release_unmap(qdev, release, &drawable->release_info);

	clips_ptr = clips;
	for (i = 0; i < num_clips; i++, clips_ptr += inc) {
		rects[i].left   = clips_ptr->x1;
		rects[i].right  = clips_ptr->x2;
		rects[i].top    = clips_ptr->y1;
		rects[i].bottom = clips_ptr->y2;
	}
	qxl_bo_kunmap(clips_bo);

	qxl_release_fence_buffer_objects(release);
	qxl_push_command_ring_release(qdev, release, QXL_CMD_DRAW, false);

out_release_backoff:
	if (ret)
		qxl_release_backoff_reserve_list(release);
out_free_image:
	qxl_image_free_objects(qdev, dimage);
out_free_clips:
	qxl_bo_unref(&clips_bo);
out_free_drawable:
	/* only free drawable on error */
	if (ret)
		free_drawable(qdev, release);

}
