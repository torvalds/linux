/*
 * Copyright 2009 Ben Skeggs
 * Copyright 2008 Stuart Bennett
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_fbcon.h"

int
nv04_fbcon_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct nouveau_drm *drm = nouveau_drm(nfbdev->helper.dev);
	struct nouveau_channel *chan = drm->channel;
	int ret;

	ret = RING_SPACE(chan, 4);
	if (ret)
		return ret;

	BEGIN_NV04(chan, NvSubImageBlit, 0x0300, 3);
	OUT_RING(chan, (region->sy << 16) | region->sx);
	OUT_RING(chan, (region->dy << 16) | region->dx);
	OUT_RING(chan, (region->height << 16) | region->width);
	FIRE_RING(chan);
	return 0;
}

int
nv04_fbcon_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct nouveau_drm *drm = nouveau_drm(nfbdev->helper.dev);
	struct nouveau_channel *chan = drm->channel;
	int ret;

	ret = RING_SPACE(chan, 7);
	if (ret)
		return ret;

	BEGIN_NV04(chan, NvSubGdiRect, 0x02fc, 1);
	OUT_RING(chan, (rect->rop != ROP_COPY) ? 1 : 3);
	BEGIN_NV04(chan, NvSubGdiRect, 0x03fc, 1);
	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR)
		OUT_RING(chan, ((uint32_t *)info->pseudo_palette)[rect->color]);
	else
		OUT_RING(chan, rect->color);
	BEGIN_NV04(chan, NvSubGdiRect, 0x0400, 2);
	OUT_RING(chan, (rect->dx << 16) | rect->dy);
	OUT_RING(chan, (rect->width << 16) | rect->height);
	FIRE_RING(chan);
	return 0;
}

int
nv04_fbcon_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct nouveau_drm *drm = nouveau_drm(nfbdev->helper.dev);
	struct nouveau_channel *chan = drm->channel;
	uint32_t fg;
	uint32_t bg;
	uint32_t dsize;
	uint32_t *data = (uint32_t *)image->data;
	int ret;

	if (image->depth != 1)
		return -ENODEV;

	ret = RING_SPACE(chan, 8);
	if (ret)
		return ret;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		fg = ((uint32_t *) info->pseudo_palette)[image->fg_color];
		bg = ((uint32_t *) info->pseudo_palette)[image->bg_color];
	} else {
		fg = image->fg_color;
		bg = image->bg_color;
	}

	BEGIN_NV04(chan, NvSubGdiRect, 0x0be4, 7);
	OUT_RING(chan, (image->dy << 16) | (image->dx & 0xffff));
	OUT_RING(chan, ((image->dy + image->height) << 16) |
			 ((image->dx + image->width) & 0xffff));
	OUT_RING(chan, bg);
	OUT_RING(chan, fg);
	OUT_RING(chan, (image->height << 16) | ALIGN(image->width, 8));
	OUT_RING(chan, (image->height << 16) | image->width);
	OUT_RING(chan, (image->dy << 16) | (image->dx & 0xffff));

	dsize = ALIGN(ALIGN(image->width, 8) * image->height, 32) >> 5;
	while (dsize) {
		int iter_len = dsize > 128 ? 128 : dsize;

		ret = RING_SPACE(chan, iter_len + 1);
		if (ret)
			return ret;

		BEGIN_NV04(chan, NvSubGdiRect, 0x0c00, iter_len);
		OUT_RINGp(chan, data, iter_len);
		data += iter_len;
		dsize -= iter_len;
	}

	FIRE_RING(chan);
	return 0;
}

int
nv04_fbcon_accel_init(struct fb_info *info)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct drm_device *dev = nfbdev->helper.dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_channel *chan = drm->channel;
	struct nvif_device *device = &drm->device;
	int surface_fmt, pattern_fmt, rect_fmt;
	int ret;

	switch (info->var.bits_per_pixel) {
	case 8:
		surface_fmt = 1;
		pattern_fmt = 3;
		rect_fmt = 3;
		break;
	case 16:
		surface_fmt = 4;
		pattern_fmt = 1;
		rect_fmt = 1;
		break;
	case 32:
		switch (info->var.transp.length) {
		case 0: /* depth 24 */
		case 8: /* depth 32 */
			break;
		default:
			return -EINVAL;
		}

		surface_fmt = 6;
		pattern_fmt = 3;
		rect_fmt = 3;
		break;
	default:
		return -EINVAL;
	}

	ret = nvif_object_init(&chan->user, 0x0062,
			       device->info.family >= NV_DEVICE_INFO_V0_CELSIUS ?
			       0x0062 : 0x0042, NULL, 0, &nfbdev->surf2d);
	if (ret)
		return ret;

	ret = nvif_object_init(&chan->user, 0x0019, 0x0019, NULL, 0,
			       &nfbdev->clip);
	if (ret)
		return ret;

	ret = nvif_object_init(&chan->user, 0x0043, 0x0043, NULL, 0,
			       &nfbdev->rop);
	if (ret)
		return ret;

	ret = nvif_object_init(&chan->user, 0x0044, 0x0044, NULL, 0,
			       &nfbdev->patt);
	if (ret)
		return ret;

	ret = nvif_object_init(&chan->user, 0x004a, 0x004a, NULL, 0,
			       &nfbdev->gdi);
	if (ret)
		return ret;

	ret = nvif_object_init(&chan->user, 0x005f,
			       device->info.chipset >= 0x11 ? 0x009f : 0x005f,
			       NULL, 0, &nfbdev->blit);
	if (ret)
		return ret;

	if (RING_SPACE(chan, 49 + (device->info.chipset >= 0x11 ? 4 : 0))) {
		nouveau_fbcon_gpu_lockup(info);
		return 0;
	}

	BEGIN_NV04(chan, NvSubCtxSurf2D, 0x0000, 1);
	OUT_RING(chan, nfbdev->surf2d.handle);
	BEGIN_NV04(chan, NvSubCtxSurf2D, 0x0184, 2);
	OUT_RING(chan, chan->vram.handle);
	OUT_RING(chan, chan->vram.handle);
	BEGIN_NV04(chan, NvSubCtxSurf2D, 0x0300, 4);
	OUT_RING(chan, surface_fmt);
	OUT_RING(chan, info->fix.line_length | (info->fix.line_length << 16));
	OUT_RING(chan, info->fix.smem_start - dev->mode_config.fb_base);
	OUT_RING(chan, info->fix.smem_start - dev->mode_config.fb_base);

	BEGIN_NV04(chan, NvSubCtxSurf2D, 0x0000, 1);
	OUT_RING(chan, nfbdev->rop.handle);
	BEGIN_NV04(chan, NvSubCtxSurf2D, 0x0300, 1);
	OUT_RING(chan, 0x55);

	BEGIN_NV04(chan, NvSubCtxSurf2D, 0x0000, 1);
	OUT_RING(chan, nfbdev->patt.handle);
	BEGIN_NV04(chan, NvSubCtxSurf2D, 0x0300, 8);
	OUT_RING(chan, pattern_fmt);
#ifdef __BIG_ENDIAN
	OUT_RING(chan, 2);
#else
	OUT_RING(chan, 1);
#endif
	OUT_RING(chan, 0);
	OUT_RING(chan, 1);
	OUT_RING(chan, ~0);
	OUT_RING(chan, ~0);
	OUT_RING(chan, ~0);
	OUT_RING(chan, ~0);

	BEGIN_NV04(chan, NvSubCtxSurf2D, 0x0000, 1);
	OUT_RING(chan, nfbdev->clip.handle);
	BEGIN_NV04(chan, NvSubCtxSurf2D, 0x0300, 2);
	OUT_RING(chan, 0);
	OUT_RING(chan, (info->var.yres_virtual << 16) | info->var.xres_virtual);

	BEGIN_NV04(chan, NvSubImageBlit, 0x0000, 1);
	OUT_RING(chan, nfbdev->blit.handle);
	BEGIN_NV04(chan, NvSubImageBlit, 0x019c, 1);
	OUT_RING(chan, nfbdev->surf2d.handle);
	BEGIN_NV04(chan, NvSubImageBlit, 0x02fc, 1);
	OUT_RING(chan, 3);
	if (device->info.chipset >= 0x11 /*XXX: oclass == 0x009f*/) {
		BEGIN_NV04(chan, NvSubImageBlit, 0x0120, 3);
		OUT_RING(chan, 0);
		OUT_RING(chan, 1);
		OUT_RING(chan, 2);
	}

	BEGIN_NV04(chan, NvSubGdiRect, 0x0000, 1);
	OUT_RING(chan, nfbdev->gdi.handle);
	BEGIN_NV04(chan, NvSubGdiRect, 0x0198, 1);
	OUT_RING(chan, nfbdev->surf2d.handle);
	BEGIN_NV04(chan, NvSubGdiRect, 0x0188, 2);
	OUT_RING(chan, nfbdev->patt.handle);
	OUT_RING(chan, nfbdev->rop.handle);
	BEGIN_NV04(chan, NvSubGdiRect, 0x0304, 1);
	OUT_RING(chan, 1);
	BEGIN_NV04(chan, NvSubGdiRect, 0x0300, 1);
	OUT_RING(chan, rect_fmt);
	BEGIN_NV04(chan, NvSubGdiRect, 0x02fc, 1);
	OUT_RING(chan, 3);

	FIRE_RING(chan);

	return 0;
}

