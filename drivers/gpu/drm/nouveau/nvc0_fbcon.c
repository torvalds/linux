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

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_ramht.h"
#include "nouveau_fbcon.h"
#include "nouveau_mm.h"

int
nvc0_fbcon_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct drm_device *dev = nfbdev->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channel;
	int ret;

	ret = RING_SPACE(chan, rect->rop == ROP_COPY ? 7 : 11);
	if (ret)
		return ret;

	if (rect->rop != ROP_COPY) {
		BEGIN_NVC0(chan, NvSub2D, 0x02ac, 1);
		OUT_RING  (chan, 1);
	}
	BEGIN_NVC0(chan, NvSub2D, 0x0588, 1);
	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR)
		OUT_RING  (chan, ((uint32_t *)info->pseudo_palette)[rect->color]);
	else
		OUT_RING  (chan, rect->color);
	BEGIN_NVC0(chan, NvSub2D, 0x0600, 4);
	OUT_RING  (chan, rect->dx);
	OUT_RING  (chan, rect->dy);
	OUT_RING  (chan, rect->dx + rect->width);
	OUT_RING  (chan, rect->dy + rect->height);
	if (rect->rop != ROP_COPY) {
		BEGIN_NVC0(chan, NvSub2D, 0x02ac, 1);
		OUT_RING  (chan, 3);
	}
	FIRE_RING(chan);
	return 0;
}

int
nvc0_fbcon_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct drm_device *dev = nfbdev->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channel;
	int ret;

	ret = RING_SPACE(chan, 12);
	if (ret)
		return ret;

	BEGIN_NVC0(chan, NvSub2D, 0x0110, 1);
	OUT_RING  (chan, 0);
	BEGIN_NVC0(chan, NvSub2D, 0x08b0, 4);
	OUT_RING  (chan, region->dx);
	OUT_RING  (chan, region->dy);
	OUT_RING  (chan, region->width);
	OUT_RING  (chan, region->height);
	BEGIN_NVC0(chan, NvSub2D, 0x08d0, 4);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, region->sx);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, region->sy);
	FIRE_RING(chan);
	return 0;
}

int
nvc0_fbcon_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct drm_device *dev = nfbdev->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channel;
	uint32_t width, dwords, *data = (uint32_t *)image->data;
	uint32_t mask = ~(~0 >> (32 - info->var.bits_per_pixel));
	uint32_t *palette = info->pseudo_palette;
	int ret;

	if (image->depth != 1)
		return -ENODEV;

	ret = RING_SPACE(chan, 11);
	if (ret)
		return ret;

	width = ALIGN(image->width, 32);
	dwords = (width * image->height) >> 5;

	BEGIN_NVC0(chan, NvSub2D, 0x0814, 2);
	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		OUT_RING  (chan, palette[image->bg_color] | mask);
		OUT_RING  (chan, palette[image->fg_color] | mask);
	} else {
		OUT_RING  (chan, image->bg_color);
		OUT_RING  (chan, image->fg_color);
	}
	BEGIN_NVC0(chan, NvSub2D, 0x0838, 2);
	OUT_RING  (chan, image->width);
	OUT_RING  (chan, image->height);
	BEGIN_NVC0(chan, NvSub2D, 0x0850, 4);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, image->dx);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, image->dy);

	while (dwords) {
		int push = dwords > 2047 ? 2047 : dwords;

		ret = RING_SPACE(chan, push + 1);
		if (ret)
			return ret;

		dwords -= push;

		BEGIN_NIC0(chan, NvSub2D, 0x0860, push);
		OUT_RINGp(chan, data, push);
		data += push;
	}

	FIRE_RING(chan);
	return 0;
}

int
nvc0_fbcon_accel_init(struct fb_info *info)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct drm_device *dev = nfbdev->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channel;
	struct nouveau_framebuffer *fb = &nfbdev->nouveau_fb;
	int ret, format;

	ret = nouveau_gpuobj_gr_new(chan, 0x902d, 0x902d);
	if (ret)
		return ret;

	switch (info->var.bits_per_pixel) {
	case 8:
		format = 0xf3;
		break;
	case 15:
		format = 0xf8;
		break;
	case 16:
		format = 0xe8;
		break;
	case 32:
		switch (info->var.transp.length) {
		case 0: /* depth 24 */
		case 8: /* depth 32, just use 24.. */
			format = 0xe6;
			break;
		case 2: /* depth 30 */
			format = 0xd1;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	ret = RING_SPACE(chan, 60);
	if (ret) {
		WARN_ON(1);
		nouveau_fbcon_gpu_lockup(info);
		return ret;
	}

	BEGIN_NVC0(chan, NvSub2D, 0x0000, 1);
	OUT_RING  (chan, 0x0000902d);
	BEGIN_NVC0(chan, NvSub2D, 0x0104, 2);
	OUT_RING  (chan, upper_32_bits(chan->notifier_vma.offset));
	OUT_RING  (chan, lower_32_bits(chan->notifier_vma.offset));
	BEGIN_NVC0(chan, NvSub2D, 0x0290, 1);
	OUT_RING  (chan, 0);
	BEGIN_NVC0(chan, NvSub2D, 0x0888, 1);
	OUT_RING  (chan, 1);
	BEGIN_NVC0(chan, NvSub2D, 0x02ac, 1);
	OUT_RING  (chan, 3);
	BEGIN_NVC0(chan, NvSub2D, 0x02a0, 1);
	OUT_RING  (chan, 0x55);
	BEGIN_NVC0(chan, NvSub2D, 0x08c0, 4);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, 1);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, 1);
	BEGIN_NVC0(chan, NvSub2D, 0x0580, 2);
	OUT_RING  (chan, 4);
	OUT_RING  (chan, format);
	BEGIN_NVC0(chan, NvSub2D, 0x02e8, 2);
	OUT_RING  (chan, 2);
	OUT_RING  (chan, 1);

	BEGIN_NVC0(chan, NvSub2D, 0x0804, 1);
	OUT_RING  (chan, format);
	BEGIN_NVC0(chan, NvSub2D, 0x0800, 1);
	OUT_RING  (chan, 1);
	BEGIN_NVC0(chan, NvSub2D, 0x0808, 3);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, 1);
	BEGIN_NVC0(chan, NvSub2D, 0x081c, 1);
	OUT_RING  (chan, 1);
	BEGIN_NVC0(chan, NvSub2D, 0x0840, 4);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, 1);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, 1);
	BEGIN_NVC0(chan, NvSub2D, 0x0200, 10);
	OUT_RING  (chan, format);
	OUT_RING  (chan, 1);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, 1);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, info->fix.line_length);
	OUT_RING  (chan, info->var.xres_virtual);
	OUT_RING  (chan, info->var.yres_virtual);
	OUT_RING  (chan, upper_32_bits(fb->vma.offset));
	OUT_RING  (chan, lower_32_bits(fb->vma.offset));
	BEGIN_NVC0(chan, NvSub2D, 0x0230, 10);
	OUT_RING  (chan, format);
	OUT_RING  (chan, 1);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, 1);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, info->fix.line_length);
	OUT_RING  (chan, info->var.xres_virtual);
	OUT_RING  (chan, info->var.yres_virtual);
	OUT_RING  (chan, upper_32_bits(fb->vma.offset));
	OUT_RING  (chan, lower_32_bits(fb->vma.offset));
	FIRE_RING (chan);

	return 0;
}

