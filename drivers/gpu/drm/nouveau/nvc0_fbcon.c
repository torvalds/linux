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
		PUSH_NVIM(push, NV902D, 0x02ac, 1);
	}

	PUSH_NVSQ(push, NV902D, 0x0588, colour);
	PUSH_NVSQ(push, NV902D, 0x0600, rect->dx,
				0x0604, rect->dy,
				0x0608, rect->dx + rect->width,
				0x060c, rect->dy + rect->height);

	if (rect->rop != ROP_COPY) {
		PUSH_NVIM(push, NV902D, 0x02ac, 3);
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

	PUSH_NVSQ(push, NV902D, 0x0814, bg,
				0x0818, fg);
	PUSH_NVSQ(push, NV902D, 0x0838, image->width,
				0x083c, image->height);
	PUSH_NVSQ(push, NV902D, 0x0850, 0,
				0x0854, image->dx,
				0x0858, 0,
				0x085c, image->dy);

	dwords = ALIGN(ALIGN(image->width, 8) * image->height, 32) >> 5;
	while (dwords) {
		int count = dwords > 2047 ? 2047 : dwords;

		ret = PUSH_WAIT(push, count + 1);
		if (ret)
			return ret;

		dwords -= count;

		PUSH_NVNI(push, NV902D, 0x0860, data, count);
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

	ret = PUSH_WAIT(push, 52);
	if (ret) {
		WARN_ON(1);
		nouveau_fbcon_gpu_lockup(info);
		return ret;
	}

	PUSH_NVSQ(push, NV902D, 0x0000, nfbdev->twod.handle);

	PUSH_NVSQ(push, NV902D, 0x0200, format,
				0x0204, 1);
	PUSH_NVSQ(push, NV902D, 0x0214, info->fix.line_length,
				0x0218, info->var.xres_virtual,
				0x021c, info->var.yres_virtual,
				0x0220, upper_32_bits(nfbdev->vma->addr),
				0x0224, lower_32_bits(nfbdev->vma->addr));

	PUSH_NVSQ(push, NV902D, 0x0230, format,
				0x0234, 1);
	PUSH_NVSQ(push, NV902D, 0x0244, info->fix.line_length,
				0x0248, info->var.xres_virtual,
				0x024c, info->var.yres_virtual,
				0x0250, upper_32_bits(nfbdev->vma->addr),
				0x0254, lower_32_bits(nfbdev->vma->addr));

	PUSH_NVIM(push, NV902D, 0x0290, 0);
	PUSH_NVIM(push, NV902D, 0x02a0, 0x55);
	PUSH_NVIM(push, NV902D, 0x02ac, 3);
	PUSH_NVSQ(push, NV902D, 0x02e8, 2,
				0x02ec, 1);

	PUSH_NVSQ(push, NV902D, 0X0580, 4,
				0x0584, format);

	PUSH_NVSQ(push, NV902D, 0x0800, 1,
				0x0804, format,
				0x0808, 0,
				0x080c, 0,
				0x0810, 1);
	PUSH_NVIM(push, NV902D, 0x081c, 1);
	PUSH_NVSQ(push, NV902D, 0x0840, 0,
				0x0844, 1,
				0x0848, 0,
				0x084c, 1);

	PUSH_NVIM(push, NV902D, 0x0888, 1);
	PUSH_NVSQ(push, NV902D, 0x08c0, 0,
				0x08c4, 1,
				0x08c8, 0,
				0x08cc, 1);

	PUSH_KICK(push);
	return 0;
}

