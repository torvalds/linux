/*
 * Copyright 2010 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
#define NVIF_DEBUG_PRINT_DISABLE
#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_fbcon.h"
#include "nouveau_vmm.h"

#include <nvif/push906f.h>

#include <nvhw/class/cl902d.h>

int
nvc0_fbcon_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct nouveau_drm *drm = nouveau_drm(nfbdev->helper.dev);
	struct nouveau_channel *chan = drm->channel;
	struct nvif_push *push = chan->chan.push;
	u32 colour;
	int ret;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR)
		colour = ((uint32_t *)info->pseudo_palette)[rect->color];
	else
		colour = rect->color;

	ret = PUSH_WAIT(push, rect->rop == ROP_COPY ? 7 : 9);
	if (ret)
		return ret;

	if (rect->rop != ROP_COPY) {
		PUSH_IMMD(push, NV902D, SET_OPERATION,
			  NVDEF(NV902D, SET_OPERATION, V, ROP_AND));
	}

	PUSH_MTHD(push, NV902D, SET_RENDER_SOLID_PRIM_COLOR, colour);

	PUSH_MTHD(push, NV902D, RENDER_SOLID_PRIM_POINT_SET_X(0), rect->dx,
				RENDER_SOLID_PRIM_POINT_Y(0), rect->dy,
				RENDER_SOLID_PRIM_POINT_SET_X(1), rect->dx + rect->width,
				RENDER_SOLID_PRIM_POINT_Y(1), rect->dy + rect->height);

	if (rect->rop != ROP_COPY) {
		PUSH_IMMD(push, NV902D, SET_OPERATION,
			  NVDEF(NV902D, SET_OPERATION, V, SRCCOPY));
	}

	PUSH_KICK(push);
	return 0;
}

int
nvc0_fbcon_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct nouveau_drm *drm = nouveau_drm(nfbdev->helper.dev);
	struct nouveau_channel *chan = drm->channel;
	struct nvif_push *push = chan->chan.push;
	int ret;

	ret = PUSH_WAIT(push, 11);
	if (ret)
		return ret;

	PUSH_IMMD(push, NV902D, WAIT_FOR_IDLE, 0);

	PUSH_MTHD(push, NV902D, SET_PIXELS_FROM_MEMORY_DST_X0, region->dx,
				SET_PIXELS_FROM_MEMORY_DST_Y0, region->dy,
				SET_PIXELS_FROM_MEMORY_DST_WIDTH, region->width,
				SET_PIXELS_FROM_MEMORY_DST_HEIGHT, region->height);

	PUSH_MTHD(push, NV902D, SET_PIXELS_FROM_MEMORY_SRC_X0_FRAC, 0,
				SET_PIXELS_FROM_MEMORY_SRC_X0_INT, region->sx,
				SET_PIXELS_FROM_MEMORY_SRC_Y0_FRAC, 0,
				PIXELS_FROM_MEMORY_SRC_Y0_INT, region->sy);
	PUSH_KICK(push);
	return 0;
}

int
nvc0_fbcon_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct nouveau_drm *drm = nouveau_drm(nfbdev->helper.dev);
	struct nouveau_channel *chan = drm->channel;
	struct nvif_push *push = chan->chan.push;
	uint32_t dwords, *data = (uint32_t *)image->data;
	uint32_t mask = ~(~0 >> (32 - info->var.bits_per_pixel));
	uint32_t *palette = info->pseudo_palette, bg, fg;
	int ret;

	if (image->depth != 1)
		return -ENODEV;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		bg = palette[image->bg_color] | mask;
		fg = palette[image->fg_color] | mask;
	} else {
		bg = image->bg_color;
		fg = image->fg_color;
	}

	ret = PUSH_WAIT(push, 11);
	if (ret)
		return ret;

	PUSH_MTHD(push, NV902D, SET_PIXELS_FROM_CPU_COLOR0, bg,
				SET_PIXELS_FROM_CPU_COLOR1, fg);

	PUSH_MTHD(push, NV902D, SET_PIXELS_FROM_CPU_SRC_WIDTH, image->width,
				SET_PIXELS_FROM_CPU_SRC_HEIGHT, image->height);

	PUSH_MTHD(push, NV902D, SET_PIXELS_FROM_CPU_DST_X0_FRAC, 0,
				SET_PIXELS_FROM_CPU_DST_X0_INT, image->dx,
				SET_PIXELS_FROM_CPU_DST_Y0_FRAC, 0,
				SET_PIXELS_FROM_CPU_DST_Y0_INT, image->dy);

	dwords = ALIGN(ALIGN(image->width, 8) * image->height, 32) >> 5;
	while (dwords) {
		int count = dwords > 2047 ? 2047 : dwords;

		ret = PUSH_WAIT(push, count + 1);
		if (ret)
			return ret;

		dwords -= count;

		PUSH_NINC(push, NV902D, PIXELS_FROM_CPU_DATA, data, count);
		data += count;
	}

	PUSH_KICK(push);
	return 0;
}

int
nvc0_fbcon_accel_init(struct fb_info *info)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct drm_device *dev = nfbdev->helper.dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_channel *chan = drm->channel;
	struct nvif_push *push = chan->chan.push;
	int ret, format;

	ret = nvif_object_ctor(&chan->user, "fbconTwoD", 0x902d, 0x902d,
			       NULL, 0, &nfbdev->twod);
	if (ret)
		return ret;

	switch (info->var.bits_per_pixel) {
	case 8:
		format = NV902D_SET_DST_FORMAT_V_Y8;
		break;
	case 15:
		format = NV902D_SET_DST_FORMAT_V_X1R5G5B5;
		break;
	case 16:
		format = NV902D_SET_DST_FORMAT_V_R5G6B5;
		break;
	case 32:
		switch (info->var.transp.length) {
		case 0: /* depth 24 */
		case 8: /* depth 32, just use 24.. */
			format = NV902D_SET_DST_FORMAT_V_X8R8G8B8;
			break;
		case 2: /* depth 30 */
			format = NV902D_SET_DST_FORMAT_V_A2B10G10R10;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	ret = PUSH_WAIT(push, 52);
	if (ret) {
		WARN_ON(1);
		nouveau_fbcon_gpu_lockup(info);
		return ret;
	}

	PUSH_MTHD(push, NV902D, SET_OBJECT, nfbdev->twod.handle);

	PUSH_MTHD(push, NV902D, SET_DST_FORMAT,
		  NVVAL(NV902D, SET_DST_FORMAT, V, format),

				SET_DST_MEMORY_LAYOUT,
		  NVDEF(NV902D, SET_DST_MEMORY_LAYOUT, V, PITCH));

	PUSH_MTHD(push, NV902D, SET_DST_PITCH, info->fix.line_length,
				SET_DST_WIDTH, info->var.xres_virtual,
				SET_DST_HEIGHT, info->var.yres_virtual,

				SET_DST_OFFSET_UPPER,
		  NVVAL(NV902D, SET_DST_OFFSET_UPPER, V, upper_32_bits(nfbdev->vma->addr)),

				SET_DST_OFFSET_LOWER,
		  NVVAL(NV902D, SET_DST_OFFSET_LOWER, V, lower_32_bits(nfbdev->vma->addr)));

	PUSH_MTHD(push, NV902D, SET_SRC_FORMAT,
		  NVVAL(NV902D, SET_SRC_FORMAT, V, format),

				SET_SRC_MEMORY_LAYOUT,
		  NVDEF(NV902D, SET_SRC_MEMORY_LAYOUT, V, PITCH));

	PUSH_MTHD(push, NV902D, SET_SRC_PITCH, info->fix.line_length,
				SET_SRC_WIDTH, info->var.xres_virtual,
				SET_SRC_HEIGHT, info->var.yres_virtual,

				SET_SRC_OFFSET_UPPER,
		  NVVAL(NV902D, SET_SRC_OFFSET_UPPER, V, upper_32_bits(nfbdev->vma->addr)),

				SET_SRC_OFFSET_LOWER,
		  NVVAL(NV902D, SET_SRC_OFFSET_LOWER, V, lower_32_bits(nfbdev->vma->addr)));

	PUSH_IMMD(push, NV902D, SET_CLIP_ENABLE,
		  NVDEF(NV902D, SET_CLIP_ENABLE, V, FALSE));

	PUSH_IMMD(push, NV902D, SET_ROP,
		  NVVAL(NV902D, SET_ROP, V, 0x55));

	PUSH_IMMD(push, NV902D, SET_OPERATION,
		  NVDEF(NV902D, SET_OPERATION, V, SRCCOPY));

	PUSH_MTHD(push, NV902D, SET_MONOCHROME_PATTERN_COLOR_FORMAT,
		  NVDEF(NV902D, SET_MONOCHROME_PATTERN_COLOR_FORMAT, V, A8R8G8B8),

				SET_MONOCHROME_PATTERN_FORMAT,
		  NVDEF(NV902D, SET_MONOCHROME_PATTERN_FORMAT, V, LE_M1));

	PUSH_MTHD(push, NV902D, RENDER_SOLID_PRIM_MODE,
		  NVDEF(NV902D, RENDER_SOLID_PRIM_MODE, V, RECTS),

				SET_RENDER_SOLID_PRIM_COLOR_FORMAT,
		  NVVAL(NV902D, SET_RENDER_SOLID_PRIM_COLOR_FORMAT, V, format));

	PUSH_MTHD(push, NV902D, SET_PIXELS_FROM_CPU_DATA_TYPE,
		  NVDEF(NV902D, SET_PIXELS_FROM_CPU_DATA_TYPE, V, INDEX),

				SET_PIXELS_FROM_CPU_COLOR_FORMAT,
		  NVVAL(NV902D, SET_PIXELS_FROM_CPU_COLOR_FORMAT, V, format),

				SET_PIXELS_FROM_CPU_INDEX_FORMAT,
		  NVDEF(NV902D, SET_PIXELS_FROM_CPU_INDEX_FORMAT, V, I1),

				SET_PIXELS_FROM_CPU_MONO_FORMAT,
		  NVDEF(NV902D, SET_PIXELS_FROM_CPU_MONO_FORMAT, V, CGA6_M1),

				SET_PIXELS_FROM_CPU_WRAP,
		  NVDEF(NV902D, SET_PIXELS_FROM_CPU_WRAP, V, WRAP_BYTE));

	PUSH_IMMD(push, NV902D, SET_PIXELS_FROM_CPU_MONO_OPACITY,
		  NVDEF(NV902D, SET_PIXELS_FROM_CPU_MONO_OPACITY, V, OPAQUE));

	PUSH_MTHD(push, NV902D, SET_PIXELS_FROM_CPU_DX_DU_FRAC, 0,
				SET_PIXELS_FROM_CPU_DX_DU_INT, 1,
				SET_PIXELS_FROM_CPU_DY_DV_FRAC, 0,
				SET_PIXELS_FROM_CPU_DY_DV_INT, 1);

	PUSH_IMMD(push, NV902D, SET_PIXELS_FROM_MEMORY_SAFE_OVERLAP,
		  NVDEF(NV902D, SET_PIXELS_FROM_MEMORY_SAFE_OVERLAP, V, TRUE));

	PUSH_MTHD(push, NV902D, SET_PIXELS_FROM_MEMORY_DU_DX_FRAC, 0,
				SET_PIXELS_FROM_MEMORY_DU_DX_INT, 1,
				SET_PIXELS_FROM_MEMORY_DV_DY_FRAC, 0,
				SET_PIXELS_FROM_MEMORY_DV_DY_INT, 1);
	PUSH_KICK(push);
	return 0;
}

