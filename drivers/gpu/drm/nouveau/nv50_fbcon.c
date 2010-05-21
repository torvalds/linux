#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_fbcon.h"

void
nv50_fbcon_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct nouveau_fbcon_par *par = info->par;
	struct drm_device *dev = par->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channel;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (!(info->flags & FBINFO_HWACCEL_DISABLED) &&
	     RING_SPACE(chan, rect->rop == ROP_COPY ? 7 : 11)) {
		nouveau_fbcon_gpu_lockup(info);
	}

	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_fillrect(info, rect);
		return;
	}

	if (rect->rop != ROP_COPY) {
		BEGIN_RING(chan, NvSub2D, 0x02ac, 1);
		OUT_RING(chan, 1);
	}
	BEGIN_RING(chan, NvSub2D, 0x0588, 1);
	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR)
		OUT_RING(chan, ((uint32_t *)info->pseudo_palette)[rect->color]);
	else
		OUT_RING(chan, rect->color);
	BEGIN_RING(chan, NvSub2D, 0x0600, 4);
	OUT_RING(chan, rect->dx);
	OUT_RING(chan, rect->dy);
	OUT_RING(chan, rect->dx + rect->width);
	OUT_RING(chan, rect->dy + rect->height);
	if (rect->rop != ROP_COPY) {
		BEGIN_RING(chan, NvSub2D, 0x02ac, 1);
		OUT_RING(chan, 3);
	}
	FIRE_RING(chan);
}

void
nv50_fbcon_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	struct nouveau_fbcon_par *par = info->par;
	struct drm_device *dev = par->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channel;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (!(info->flags & FBINFO_HWACCEL_DISABLED) && RING_SPACE(chan, 12)) {
		nouveau_fbcon_gpu_lockup(info);
	}

	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_copyarea(info, region);
		return;
	}

	BEGIN_RING(chan, NvSub2D, 0x0110, 1);
	OUT_RING(chan, 0);
	BEGIN_RING(chan, NvSub2D, 0x08b0, 4);
	OUT_RING(chan, region->dx);
	OUT_RING(chan, region->dy);
	OUT_RING(chan, region->width);
	OUT_RING(chan, region->height);
	BEGIN_RING(chan, NvSub2D, 0x08d0, 4);
	OUT_RING(chan, 0);
	OUT_RING(chan, region->sx);
	OUT_RING(chan, 0);
	OUT_RING(chan, region->sy);
	FIRE_RING(chan);
}

void
nv50_fbcon_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct nouveau_fbcon_par *par = info->par;
	struct drm_device *dev = par->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channel;
	uint32_t width, dwords, *data = (uint32_t *)image->data;
	uint32_t mask = ~(~0 >> (32 - info->var.bits_per_pixel));
	uint32_t *palette = info->pseudo_palette;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (image->depth != 1) {
		cfb_imageblit(info, image);
		return;
	}

	if (!(info->flags & FBINFO_HWACCEL_DISABLED) && RING_SPACE(chan, 11)) {
		nouveau_fbcon_gpu_lockup(info);
	}

	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_imageblit(info, image);
		return;
	}

	width = ALIGN(image->width, 32);
	dwords = (width * image->height) >> 5;

	BEGIN_RING(chan, NvSub2D, 0x0814, 2);
	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		OUT_RING(chan, palette[image->bg_color] | mask);
		OUT_RING(chan, palette[image->fg_color] | mask);
	} else {
		OUT_RING(chan, image->bg_color);
		OUT_RING(chan, image->fg_color);
	}
	BEGIN_RING(chan, NvSub2D, 0x0838, 2);
	OUT_RING(chan, image->width);
	OUT_RING(chan, image->height);
	BEGIN_RING(chan, NvSub2D, 0x0850, 4);
	OUT_RING(chan, 0);
	OUT_RING(chan, image->dx);
	OUT_RING(chan, 0);
	OUT_RING(chan, image->dy);

	while (dwords) {
		int push = dwords > 2047 ? 2047 : dwords;

		if (RING_SPACE(chan, push + 1)) {
			nouveau_fbcon_gpu_lockup(info);
			cfb_imageblit(info, image);
			return;
		}

		dwords -= push;

		BEGIN_RING(chan, NvSub2D, 0x40000860, push);
		OUT_RINGp(chan, data, push);
		data += push;
	}

	FIRE_RING(chan);
}

int
nv50_fbcon_accel_init(struct fb_info *info)
{
	struct nouveau_fbcon_par *par = info->par;
	struct drm_device *dev = par->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channel;
	struct nouveau_gpuobj *eng2d = NULL;
	uint64_t fb;
	int ret, format;

	fb = info->fix.smem_start - dev_priv->fb_phys + dev_priv->vm_vram_base;

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

	ret = nouveau_gpuobj_gr_new(dev_priv->channel, 0x502d, &eng2d);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_ref_add(dev, dev_priv->channel, Nv2D, eng2d, NULL);
	if (ret)
		return ret;

	ret = RING_SPACE(chan, 59);
	if (ret) {
		nouveau_fbcon_gpu_lockup(info);
		return ret;
	}

	BEGIN_RING(chan, NvSub2D, 0x0000, 1);
	OUT_RING(chan, Nv2D);
	BEGIN_RING(chan, NvSub2D, 0x0180, 4);
	OUT_RING(chan, NvNotify0);
	OUT_RING(chan, chan->vram_handle);
	OUT_RING(chan, chan->vram_handle);
	OUT_RING(chan, chan->vram_handle);
	BEGIN_RING(chan, NvSub2D, 0x0290, 1);
	OUT_RING(chan, 0);
	BEGIN_RING(chan, NvSub2D, 0x0888, 1);
	OUT_RING(chan, 1);
	BEGIN_RING(chan, NvSub2D, 0x02ac, 1);
	OUT_RING(chan, 3);
	BEGIN_RING(chan, NvSub2D, 0x02a0, 1);
	OUT_RING(chan, 0x55);
	BEGIN_RING(chan, NvSub2D, 0x08c0, 4);
	OUT_RING(chan, 0);
	OUT_RING(chan, 1);
	OUT_RING(chan, 0);
	OUT_RING(chan, 1);
	BEGIN_RING(chan, NvSub2D, 0x0580, 2);
	OUT_RING(chan, 4);
	OUT_RING(chan, format);
	BEGIN_RING(chan, NvSub2D, 0x02e8, 2);
	OUT_RING(chan, 2);
	OUT_RING(chan, 1);
	BEGIN_RING(chan, NvSub2D, 0x0804, 1);
	OUT_RING(chan, format);
	BEGIN_RING(chan, NvSub2D, 0x0800, 1);
	OUT_RING(chan, 1);
	BEGIN_RING(chan, NvSub2D, 0x0808, 3);
	OUT_RING(chan, 0);
	OUT_RING(chan, 0);
	OUT_RING(chan, 1);
	BEGIN_RING(chan, NvSub2D, 0x081c, 1);
	OUT_RING(chan, 1);
	BEGIN_RING(chan, NvSub2D, 0x0840, 4);
	OUT_RING(chan, 0);
	OUT_RING(chan, 1);
	OUT_RING(chan, 0);
	OUT_RING(chan, 1);
	BEGIN_RING(chan, NvSub2D, 0x0200, 2);
	OUT_RING(chan, format);
	OUT_RING(chan, 1);
	BEGIN_RING(chan, NvSub2D, 0x0214, 5);
	OUT_RING(chan, info->fix.line_length);
	OUT_RING(chan, info->var.xres_virtual);
	OUT_RING(chan, info->var.yres_virtual);
	OUT_RING(chan, upper_32_bits(fb));
	OUT_RING(chan, lower_32_bits(fb));
	BEGIN_RING(chan, NvSub2D, 0x0230, 2);
	OUT_RING(chan, format);
	OUT_RING(chan, 1);
	BEGIN_RING(chan, NvSub2D, 0x0244, 5);
	OUT_RING(chan, info->fix.line_length);
	OUT_RING(chan, info->var.xres_virtual);
	OUT_RING(chan, info->var.yres_virtual);
	OUT_RING(chan, upper_32_bits(fb));
	OUT_RING(chan, lower_32_bits(fb));

	return 0;
}

