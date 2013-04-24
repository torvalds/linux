/*
 * Copyright (C) 2011-2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <drm/drm_rect.h>

/**
 * drm_rect_intersect - intersect two rectangles
 * @r1: first rectangle
 * @r2: second rectangle
 *
 * Calculate the intersection of rectangles @r1 and @r2.
 * @r1 will be overwritten with the intersection.
 *
 * RETURNS:
 * %true if rectangle @r1 is still visible after the operation,
 * %false otherwise.
 */
bool drm_rect_intersect(struct drm_rect *r1, const struct drm_rect *r2)
{
	r1->x1 = max(r1->x1, r2->x1);
	r1->y1 = max(r1->y1, r2->y1);
	r1->x2 = min(r1->x2, r2->x2);
	r1->y2 = min(r1->y2, r2->y2);

	return drm_rect_visible(r1);
}
EXPORT_SYMBOL(drm_rect_intersect);

/**
 * drm_rect_clip_scaled - perform a scaled clip operation
 * @src: source window rectangle
 * @dst: destination window rectangle
 * @clip: clip rectangle
 * @hscale: horizontal scaling factor
 * @vscale: vertical scaling factor
 *
 * Clip rectangle @dst by rectangle @clip. Clip rectangle @src by the
 * same amounts multiplied by @hscale and @vscale.
 *
 * RETURNS:
 * %true if rectangle @dst is still visible after being clipped,
 * %false otherwise
 */
bool drm_rect_clip_scaled(struct drm_rect *src, struct drm_rect *dst,
			  const struct drm_rect *clip,
			  int hscale, int vscale)
{
	int diff;

	diff = clip->x1 - dst->x1;
	if (diff > 0) {
		int64_t tmp = src->x1 + (int64_t) diff * hscale;
		src->x1 = clamp_t(int64_t, tmp, INT_MIN, INT_MAX);
	}
	diff = clip->y1 - dst->y1;
	if (diff > 0) {
		int64_t tmp = src->y1 + (int64_t) diff * vscale;
		src->y1 = clamp_t(int64_t, tmp, INT_MIN, INT_MAX);
	}
	diff = dst->x2 - clip->x2;
	if (diff > 0) {
		int64_t tmp = src->x2 - (int64_t) diff * hscale;
		src->x2 = clamp_t(int64_t, tmp, INT_MIN, INT_MAX);
	}
	diff = dst->y2 - clip->y2;
	if (diff > 0) {
		int64_t tmp = src->y2 - (int64_t) diff * vscale;
		src->y2 = clamp_t(int64_t, tmp, INT_MIN, INT_MAX);
	}

	return drm_rect_intersect(dst, clip);
}
EXPORT_SYMBOL(drm_rect_clip_scaled);
