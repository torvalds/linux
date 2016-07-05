/**************************************************************************
 *
 * Copyright © 2007 David Airlie
 * Copyright © 2009-2015 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include <linux/export.h>

#include <drm/drmP.h>
#include "vmwgfx_drv.h"
#include "vmwgfx_kms.h"

#include <drm/ttm/ttm_placement.h>

#define VMW_DIRTY_DELAY (HZ / 30)

struct vmw_fb_par {
	struct vmw_private *vmw_priv;

	void *vmalloc;

	struct mutex bo_mutex;
	struct vmw_dma_buffer *vmw_bo;
	struct ttm_bo_kmap_obj map;
	void *bo_ptr;
	unsigned bo_size;
	struct drm_framebuffer *set_fb;
	struct drm_display_mode *set_mode;
	u32 fb_x;
	u32 fb_y;
	bool bo_iowrite;

	u32 pseudo_palette[17];

	unsigned max_width;
	unsigned max_height;

	struct {
		spinlock_t lock;
		bool active;
		unsigned x1;
		unsigned y1;
		unsigned x2;
		unsigned y2;
	} dirty;

	struct drm_crtc *crtc;
	struct drm_connector *con;
	struct delayed_work local_work;
};

static int vmw_fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			    unsigned blue, unsigned transp,
			    struct fb_info *info)
{
	struct vmw_fb_par *par = info->par;
	u32 *pal = par->pseudo_palette;

	if (regno > 15) {
		DRM_ERROR("Bad regno %u.\n", regno);
		return 1;
	}

	switch (par->set_fb->depth) {
	case 24:
	case 32:
		pal[regno] = ((red & 0xff00) << 8) |
			      (green & 0xff00) |
			     ((blue  & 0xff00) >> 8);
		break;
	default:
		DRM_ERROR("Bad depth %u, bpp %u.\n", par->set_fb->depth,
			  par->set_fb->bits_per_pixel);
		return 1;
	}

	return 0;
}

static int vmw_fb_check_var(struct fb_var_screeninfo *var,
			    struct fb_info *info)
{
	int depth = var->bits_per_pixel;
	struct vmw_fb_par *par = info->par;
	struct vmw_private *vmw_priv = par->vmw_priv;

	switch (var->bits_per_pixel) {
	case 32:
		depth = (var->transp.length > 0) ? 32 : 24;
		break;
	default:
		DRM_ERROR("Bad bpp %u.\n", var->bits_per_pixel);
		return -EINVAL;
	}

	switch (depth) {
	case 24:
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->transp.length = 0;
		var->transp.offset = 0;
		break;
	case 32:
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->transp.length = 8;
		var->transp.offset = 24;
		break;
	default:
		DRM_ERROR("Bad depth %u.\n", depth);
		return -EINVAL;
	}

	if ((var->xoffset + var->xres) > par->max_width ||
	    (var->yoffset + var->yres) > par->max_height) {
		DRM_ERROR("Requested geom can not fit in framebuffer\n");
		return -EINVAL;
	}

	if (!vmw_kms_validate_mode_vram(vmw_priv,
					var->xres * var->bits_per_pixel/8,
					var->yoffset + var->yres)) {
		DRM_ERROR("Requested geom can not fit in framebuffer\n");
		return -EINVAL;
	}

	return 0;
}

static int vmw_fb_blank(int blank, struct fb_info *info)
{
	return 0;
}

/*
 * Dirty code
 */

static void vmw_fb_dirty_flush(struct work_struct *work)
{
	struct vmw_fb_par *par = container_of(work, struct vmw_fb_par,
					      local_work.work);
	struct vmw_private *vmw_priv = par->vmw_priv;
	struct fb_info *info = vmw_priv->fb_info;
	unsigned long irq_flags;
	s32 dst_x1, dst_x2, dst_y1, dst_y2, w, h;
	u32 cpp, max_x, max_y;
	struct drm_clip_rect clip;
	struct drm_framebuffer *cur_fb;
	u8 *src_ptr, *dst_ptr;

	if (vmw_priv->suspended)
		return;

	mutex_lock(&par->bo_mutex);
	cur_fb = par->set_fb;
	if (!cur_fb)
		goto out_unlock;

	spin_lock_irqsave(&par->dirty.lock, irq_flags);
	if (!par->dirty.active) {
		spin_unlock_irqrestore(&par->dirty.lock, irq_flags);
		goto out_unlock;
	}

	/*
	 * Handle panning when copying from vmalloc to framebuffer.
	 * Clip dirty area to framebuffer.
	 */
	cpp = (cur_fb->bits_per_pixel + 7) / 8;
	max_x = par->fb_x + cur_fb->width;
	max_y = par->fb_y + cur_fb->height;

	dst_x1 = par->dirty.x1 - par->fb_x;
	dst_y1 = par->dirty.y1 - par->fb_y;
	dst_x1 = max_t(s32, dst_x1, 0);
	dst_y1 = max_t(s32, dst_y1, 0);

	dst_x2 = par->dirty.x2 - par->fb_x;
	dst_y2 = par->dirty.y2 - par->fb_y;
	dst_x2 = min_t(s32, dst_x2, max_x);
	dst_y2 = min_t(s32, dst_y2, max_y);
	w = dst_x2 - dst_x1;
	h = dst_y2 - dst_y1;
	w = max_t(s32, 0, w);
	h = max_t(s32, 0, h);

	par->dirty.x1 = par->dirty.x2 = 0;
	par->dirty.y1 = par->dirty.y2 = 0;
	spin_unlock_irqrestore(&par->dirty.lock, irq_flags);

	if (w && h) {
		dst_ptr = (u8 *)par->bo_ptr  +
			(dst_y1 * par->set_fb->pitches[0] + dst_x1 * cpp);
		src_ptr = (u8 *)par->vmalloc +
			((dst_y1 + par->fb_y) * info->fix.line_length +
			 (dst_x1 + par->fb_x) * cpp);

		while (h-- > 0) {
			memcpy(dst_ptr, src_ptr, w*cpp);
			dst_ptr += par->set_fb->pitches[0];
			src_ptr += info->fix.line_length;
		}

		clip.x1 = dst_x1;
		clip.x2 = dst_x2;
		clip.y1 = dst_y1;
		clip.y2 = dst_y2;

		WARN_ON_ONCE(par->set_fb->funcs->dirty(cur_fb, NULL, 0, 0,
						       &clip, 1));
		vmw_fifo_flush(vmw_priv, false);
	}
out_unlock:
	mutex_unlock(&par->bo_mutex);
}

static void vmw_fb_dirty_mark(struct vmw_fb_par *par,
			      unsigned x1, unsigned y1,
			      unsigned width, unsigned height)
{
	unsigned long flags;
	unsigned x2 = x1 + width;
	unsigned y2 = y1 + height;

	spin_lock_irqsave(&par->dirty.lock, flags);
	if (par->dirty.x1 == par->dirty.x2) {
		par->dirty.x1 = x1;
		par->dirty.y1 = y1;
		par->dirty.x2 = x2;
		par->dirty.y2 = y2;
		/* if we are active start the dirty work
		 * we share the work with the defio system */
		if (par->dirty.active)
			schedule_delayed_work(&par->local_work,
					      VMW_DIRTY_DELAY);
	} else {
		if (x1 < par->dirty.x1)
			par->dirty.x1 = x1;
		if (y1 < par->dirty.y1)
			par->dirty.y1 = y1;
		if (x2 > par->dirty.x2)
			par->dirty.x2 = x2;
		if (y2 > par->dirty.y2)
			par->dirty.y2 = y2;
	}
	spin_unlock_irqrestore(&par->dirty.lock, flags);
}

static int vmw_fb_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct vmw_fb_par *par = info->par;

	if ((var->xoffset + var->xres) > var->xres_virtual ||
	    (var->yoffset + var->yres) > var->yres_virtual) {
		DRM_ERROR("Requested panning can not fit in framebuffer\n");
		return -EINVAL;
	}

	mutex_lock(&par->bo_mutex);
	par->fb_x = var->xoffset;
	par->fb_y = var->yoffset;
	if (par->set_fb)
		vmw_fb_dirty_mark(par, par->fb_x, par->fb_y, par->set_fb->width,
				  par->set_fb->height);
	mutex_unlock(&par->bo_mutex);

	return 0;
}

static void vmw_deferred_io(struct fb_info *info,
			    struct list_head *pagelist)
{
	struct vmw_fb_par *par = info->par;
	unsigned long start, end, min, max;
	unsigned long flags;
	struct page *page;
	int y1, y2;

	min = ULONG_MAX;
	max = 0;
	list_for_each_entry(page, pagelist, lru) {
		start = page->index << PAGE_SHIFT;
		end = start + PAGE_SIZE - 1;
		min = min(min, start);
		max = max(max, end);
	}

	if (min < max) {
		y1 = min / info->fix.line_length;
		y2 = (max / info->fix.line_length) + 1;

		spin_lock_irqsave(&par->dirty.lock, flags);
		par->dirty.x1 = 0;
		par->dirty.y1 = y1;
		par->dirty.x2 = info->var.xres;
		par->dirty.y2 = y2;
		spin_unlock_irqrestore(&par->dirty.lock, flags);

		/*
		 * Since we've already waited on this work once, try to
		 * execute asap.
		 */
		cancel_delayed_work(&par->local_work);
		schedule_delayed_work(&par->local_work, 0);
	}
};

static struct fb_deferred_io vmw_defio = {
	.delay		= VMW_DIRTY_DELAY,
	.deferred_io	= vmw_deferred_io,
};

/*
 * Draw code
 */

static void vmw_fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	cfb_fillrect(info, rect);
	vmw_fb_dirty_mark(info->par, rect->dx, rect->dy,
			  rect->width, rect->height);
}

static void vmw_fb_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	cfb_copyarea(info, region);
	vmw_fb_dirty_mark(info->par, region->dx, region->dy,
			  region->width, region->height);
}

static void vmw_fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	cfb_imageblit(info, image);
	vmw_fb_dirty_mark(info->par, image->dx, image->dy,
			  image->width, image->height);
}

/*
 * Bring up code
 */

static int vmw_fb_create_bo(struct vmw_private *vmw_priv,
			    size_t size, struct vmw_dma_buffer **out)
{
	struct vmw_dma_buffer *vmw_bo;
	int ret;

	(void) ttm_write_lock(&vmw_priv->reservation_sem, false);

	vmw_bo = kmalloc(sizeof(*vmw_bo), GFP_KERNEL);
	if (!vmw_bo) {
		ret = -ENOMEM;
		goto err_unlock;
	}

	ret = vmw_dmabuf_init(vmw_priv, vmw_bo, size,
			      &vmw_sys_placement,
			      false,
			      &vmw_dmabuf_bo_free);
	if (unlikely(ret != 0))
		goto err_unlock; /* init frees the buffer on failure */

	*out = vmw_bo;
	ttm_write_unlock(&vmw_priv->reservation_sem);

	return 0;

err_unlock:
	ttm_write_unlock(&vmw_priv->reservation_sem);
	return ret;
}

static int vmw_fb_compute_depth(struct fb_var_screeninfo *var,
				int *depth)
{
	switch (var->bits_per_pixel) {
	case 32:
		*depth = (var->transp.length > 0) ? 32 : 24;
		break;
	default:
		DRM_ERROR("Bad bpp %u.\n", var->bits_per_pixel);
		return -EINVAL;
	}

	return 0;
}

static int vmw_fb_kms_detach(struct vmw_fb_par *par,
			     bool detach_bo,
			     bool unref_bo)
{
	struct drm_framebuffer *cur_fb = par->set_fb;
	int ret;

	/* Detach the KMS framebuffer from crtcs */
	if (par->set_mode) {
		struct drm_mode_set set;

		set.crtc = par->crtc;
		set.x = 0;
		set.y = 0;
		set.mode = NULL;
		set.fb = NULL;
		set.num_connectors = 1;
		set.connectors = &par->con;
		ret = drm_mode_set_config_internal(&set);
		if (ret) {
			DRM_ERROR("Could not unset a mode.\n");
			return ret;
		}
		drm_mode_destroy(par->vmw_priv->dev, par->set_mode);
		par->set_mode = NULL;
	}

	if (cur_fb) {
		drm_framebuffer_unreference(cur_fb);
		par->set_fb = NULL;
	}

	if (par->vmw_bo && detach_bo) {
		if (par->bo_ptr) {
			ttm_bo_kunmap(&par->map);
			par->bo_ptr = NULL;
		}
		if (unref_bo)
			vmw_dmabuf_unreference(&par->vmw_bo);
		else
			vmw_dmabuf_unpin(par->vmw_priv, par->vmw_bo, false);
	}

	return 0;
}

static int vmw_fb_kms_framebuffer(struct fb_info *info)
{
	struct drm_mode_fb_cmd mode_cmd;
	struct vmw_fb_par *par = info->par;
	struct fb_var_screeninfo *var = &info->var;
	struct drm_framebuffer *cur_fb;
	struct vmw_framebuffer *vfb;
	int ret = 0;
	size_t new_bo_size;

	ret = vmw_fb_compute_depth(var, &mode_cmd.depth);
	if (ret)
		return ret;

	mode_cmd.width = var->xres;
	mode_cmd.height = var->yres;
	mode_cmd.bpp = var->bits_per_pixel;
	mode_cmd.pitch = ((mode_cmd.bpp + 7) / 8) * mode_cmd.width;

	cur_fb = par->set_fb;
	if (cur_fb && cur_fb->width == mode_cmd.width &&
	    cur_fb->height == mode_cmd.height &&
	    cur_fb->bits_per_pixel == mode_cmd.bpp &&
	    cur_fb->depth == mode_cmd.depth &&
	    cur_fb->pitches[0] == mode_cmd.pitch)
		return 0;

	/* Need new buffer object ? */
	new_bo_size = (size_t) mode_cmd.pitch * (size_t) mode_cmd.height;
	ret = vmw_fb_kms_detach(par,
				par->bo_size < new_bo_size ||
				par->bo_size > 2*new_bo_size,
				true);
	if (ret)
		return ret;

	if (!par->vmw_bo) {
		ret = vmw_fb_create_bo(par->vmw_priv, new_bo_size,
				       &par->vmw_bo);
		if (ret) {
			DRM_ERROR("Failed creating a buffer object for "
				  "fbdev.\n");
			return ret;
		}
		par->bo_size = new_bo_size;
	}

	vfb = vmw_kms_new_framebuffer(par->vmw_priv, par->vmw_bo, NULL,
				      true, &mode_cmd);
	if (IS_ERR(vfb))
		return PTR_ERR(vfb);

	par->set_fb = &vfb->base;

	if (!par->bo_ptr) {
		/*
		 * Pin before mapping. Since we don't know in what placement
		 * to pin, call into KMS to do it for us.
		 */
		ret = vfb->pin(vfb);
		if (ret) {
			DRM_ERROR("Could not pin the fbdev framebuffer.\n");
			return ret;
		}

		ret = ttm_bo_kmap(&par->vmw_bo->base, 0,
				  par->vmw_bo->base.num_pages, &par->map);
		if (ret) {
			vfb->unpin(vfb);
			DRM_ERROR("Could not map the fbdev framebuffer.\n");
			return ret;
		}

		par->bo_ptr = ttm_kmap_obj_virtual(&par->map, &par->bo_iowrite);
	}

	return 0;
}

static int vmw_fb_set_par(struct fb_info *info)
{
	struct vmw_fb_par *par = info->par;
	struct vmw_private *vmw_priv = par->vmw_priv;
	struct drm_mode_set set;
	struct fb_var_screeninfo *var = &info->var;
	struct drm_display_mode new_mode = { DRM_MODE("fb_mode",
		DRM_MODE_TYPE_DRIVER,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC)
	};
	struct drm_display_mode *old_mode;
	struct drm_display_mode *mode;
	int ret;

	old_mode = par->set_mode;
	mode = drm_mode_duplicate(vmw_priv->dev, &new_mode);
	if (!mode) {
		DRM_ERROR("Could not create new fb mode.\n");
		return -ENOMEM;
	}

	mode->hdisplay = var->xres;
	mode->vdisplay = var->yres;
	vmw_guess_mode_timing(mode);

	if (old_mode && drm_mode_equal(old_mode, mode)) {
		drm_mode_destroy(vmw_priv->dev, mode);
		mode = old_mode;
		old_mode = NULL;
	} else if (!vmw_kms_validate_mode_vram(vmw_priv,
					mode->hdisplay *
					DIV_ROUND_UP(var->bits_per_pixel, 8),
					mode->vdisplay)) {
		drm_mode_destroy(vmw_priv->dev, mode);
		return -EINVAL;
	}

	mutex_lock(&par->bo_mutex);
	drm_modeset_lock_all(vmw_priv->dev);
	ret = vmw_fb_kms_framebuffer(info);
	if (ret)
		goto out_unlock;

	par->fb_x = var->xoffset;
	par->fb_y = var->yoffset;

	set.crtc = par->crtc;
	set.x = 0;
	set.y = 0;
	set.mode = mode;
	set.fb = par->set_fb;
	set.num_connectors = 1;
	set.connectors = &par->con;

	ret = drm_mode_set_config_internal(&set);
	if (ret)
		goto out_unlock;

	vmw_fb_dirty_mark(par, par->fb_x, par->fb_y,
			  par->set_fb->width, par->set_fb->height);

	/* If there already was stuff dirty we wont
	 * schedule a new work, so lets do it now */

	schedule_delayed_work(&par->local_work, 0);

out_unlock:
	if (old_mode)
		drm_mode_destroy(vmw_priv->dev, old_mode);
	par->set_mode = mode;

	drm_modeset_unlock_all(vmw_priv->dev);
	mutex_unlock(&par->bo_mutex);

	return ret;
}


static struct fb_ops vmw_fb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = vmw_fb_check_var,
	.fb_set_par = vmw_fb_set_par,
	.fb_setcolreg = vmw_fb_setcolreg,
	.fb_fillrect = vmw_fb_fillrect,
	.fb_copyarea = vmw_fb_copyarea,
	.fb_imageblit = vmw_fb_imageblit,
	.fb_pan_display = vmw_fb_pan_display,
	.fb_blank = vmw_fb_blank,
};

int vmw_fb_init(struct vmw_private *vmw_priv)
{
	struct device *device = &vmw_priv->dev->pdev->dev;
	struct vmw_fb_par *par;
	struct fb_info *info;
	unsigned fb_width, fb_height;
	unsigned fb_bpp, fb_depth, fb_offset, fb_pitch, fb_size;
	struct drm_display_mode *init_mode;
	int ret;

	fb_bpp = 32;
	fb_depth = 24;

	/* XXX As shouldn't these be as well. */
	fb_width = min(vmw_priv->fb_max_width, (unsigned)2048);
	fb_height = min(vmw_priv->fb_max_height, (unsigned)2048);

	fb_pitch = fb_width * fb_bpp / 8;
	fb_size = fb_pitch * fb_height;
	fb_offset = vmw_read(vmw_priv, SVGA_REG_FB_OFFSET);

	info = framebuffer_alloc(sizeof(*par), device);
	if (!info)
		return -ENOMEM;

	/*
	 * Par
	 */
	vmw_priv->fb_info = info;
	par = info->par;
	memset(par, 0, sizeof(*par));
	INIT_DELAYED_WORK(&par->local_work, &vmw_fb_dirty_flush);
	par->vmw_priv = vmw_priv;
	par->vmalloc = NULL;
	par->max_width = fb_width;
	par->max_height = fb_height;

	drm_modeset_lock_all(vmw_priv->dev);
	ret = vmw_kms_fbdev_init_data(vmw_priv, 0, par->max_width,
				      par->max_height, &par->con,
				      &par->crtc, &init_mode);
	if (ret) {
		drm_modeset_unlock_all(vmw_priv->dev);
		goto err_kms;
	}

	info->var.xres = init_mode->hdisplay;
	info->var.yres = init_mode->vdisplay;
	drm_modeset_unlock_all(vmw_priv->dev);

	/*
	 * Create buffers and alloc memory
	 */
	par->vmalloc = vzalloc(fb_size);
	if (unlikely(par->vmalloc == NULL)) {
		ret = -ENOMEM;
		goto err_free;
	}

	/*
	 * Fixed and var
	 */
	strcpy(info->fix.id, "svgadrmfb");
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.type_aux = 0;
	info->fix.xpanstep = 1; /* doing it in hw */
	info->fix.ypanstep = 1; /* doing it in hw */
	info->fix.ywrapstep = 0;
	info->fix.accel = FB_ACCEL_NONE;
	info->fix.line_length = fb_pitch;

	info->fix.smem_start = 0;
	info->fix.smem_len = fb_size;

	info->pseudo_palette = par->pseudo_palette;
	info->screen_base = (char __iomem *)par->vmalloc;
	info->screen_size = fb_size;

	info->flags = FBINFO_DEFAULT;
	info->fbops = &vmw_fb_ops;

	/* 24 depth per default */
	info->var.red.offset = 16;
	info->var.green.offset = 8;
	info->var.blue.offset = 0;
	info->var.red.length = 8;
	info->var.green.length = 8;
	info->var.blue.length = 8;
	info->var.transp.offset = 0;
	info->var.transp.length = 0;

	info->var.xres_virtual = fb_width;
	info->var.yres_virtual = fb_height;
	info->var.bits_per_pixel = fb_bpp;
	info->var.xoffset = 0;
	info->var.yoffset = 0;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.height = -1;
	info->var.width = -1;

	/* Use default scratch pixmap (info->pixmap.flags = FB_PIXMAP_SYSTEM) */
	info->apertures = alloc_apertures(1);
	if (!info->apertures) {
		ret = -ENOMEM;
		goto err_aper;
	}
	info->apertures->ranges[0].base = vmw_priv->vram_start;
	info->apertures->ranges[0].size = vmw_priv->vram_size;

	/*
	 * Dirty & Deferred IO
	 */
	par->dirty.x1 = par->dirty.x2 = 0;
	par->dirty.y1 = par->dirty.y2 = 0;
	par->dirty.active = true;
	spin_lock_init(&par->dirty.lock);
	mutex_init(&par->bo_mutex);
	info->fbdefio = &vmw_defio;
	fb_deferred_io_init(info);

	ret = register_framebuffer(info);
	if (unlikely(ret != 0))
		goto err_defio;

	vmw_fb_set_par(info);

	return 0;

err_defio:
	fb_deferred_io_cleanup(info);
err_aper:
err_free:
	vfree(par->vmalloc);
err_kms:
	framebuffer_release(info);
	vmw_priv->fb_info = NULL;

	return ret;
}

int vmw_fb_close(struct vmw_private *vmw_priv)
{
	struct fb_info *info;
	struct vmw_fb_par *par;

	if (!vmw_priv->fb_info)
		return 0;

	info = vmw_priv->fb_info;
	par = info->par;

	/* ??? order */
	fb_deferred_io_cleanup(info);
	cancel_delayed_work_sync(&par->local_work);
	unregister_framebuffer(info);

	(void) vmw_fb_kms_detach(par, true, true);

	vfree(par->vmalloc);
	framebuffer_release(info);

	return 0;
}

int vmw_fb_off(struct vmw_private *vmw_priv)
{
	struct fb_info *info;
	struct vmw_fb_par *par;
	unsigned long flags;

	if (!vmw_priv->fb_info)
		return -EINVAL;

	info = vmw_priv->fb_info;
	par = info->par;

	spin_lock_irqsave(&par->dirty.lock, flags);
	par->dirty.active = false;
	spin_unlock_irqrestore(&par->dirty.lock, flags);

	flush_delayed_work(&info->deferred_work);
	flush_delayed_work(&par->local_work);

	mutex_lock(&par->bo_mutex);
	(void) vmw_fb_kms_detach(par, true, false);
	mutex_unlock(&par->bo_mutex);

	return 0;
}

int vmw_fb_on(struct vmw_private *vmw_priv)
{
	struct fb_info *info;
	struct vmw_fb_par *par;
	unsigned long flags;

	if (!vmw_priv->fb_info)
		return -EINVAL;

	info = vmw_priv->fb_info;
	par = info->par;

	vmw_fb_set_par(info);
	spin_lock_irqsave(&par->dirty.lock, flags);
	par->dirty.active = true;
	spin_unlock_irqrestore(&par->dirty.lock, flags);
 
	return 0;
}
