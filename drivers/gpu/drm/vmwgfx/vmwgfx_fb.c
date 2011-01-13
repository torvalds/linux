/**************************************************************************
 *
 * Copyright © 2007 David Airlie
 * Copyright © 2009 VMware, Inc., Palo Alto, CA., USA
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

#include "drmP.h"
#include "vmwgfx_drv.h"

#include "ttm/ttm_placement.h"

#define VMW_DIRTY_DELAY (HZ / 30)

struct vmw_fb_par {
	struct vmw_private *vmw_priv;

	void *vmalloc;

	struct vmw_dma_buffer *vmw_bo;
	struct ttm_bo_kmap_obj map;

	u32 pseudo_palette[17];

	unsigned depth;
	unsigned bpp;

	unsigned max_width;
	unsigned max_height;

	void *bo_ptr;
	unsigned bo_size;
	bool bo_iowrite;

	struct {
		spinlock_t lock;
		bool active;
		unsigned x1;
		unsigned y1;
		unsigned x2;
		unsigned y2;
	} dirty;
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

	switch (par->depth) {
	case 24:
	case 32:
		pal[regno] = ((red & 0xff00) << 8) |
			      (green & 0xff00) |
			     ((blue  & 0xff00) >> 8);
		break;
	default:
		DRM_ERROR("Bad depth %u, bpp %u.\n", par->depth, par->bpp);
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

	if (!(vmw_priv->capabilities & SVGA_CAP_DISPLAY_TOPOLOGY) &&
	    (var->xoffset != 0 || var->yoffset != 0)) {
		DRM_ERROR("Can not handle panning without display topology\n");
		return -EINVAL;
	}

	if ((var->xoffset + var->xres) > par->max_width ||
	    (var->yoffset + var->yres) > par->max_height) {
		DRM_ERROR("Requested geom can not fit in framebuffer\n");
		return -EINVAL;
	}

	if (!vmw_kms_validate_mode_vram(vmw_priv,
					info->fix.line_length,
					var->yoffset + var->yres)) {
		DRM_ERROR("Requested geom can not fit in framebuffer\n");
		return -EINVAL;
	}

	return 0;
}

static int vmw_fb_set_par(struct fb_info *info)
{
	struct vmw_fb_par *par = info->par;
	struct vmw_private *vmw_priv = par->vmw_priv;

	vmw_kms_write_svga(vmw_priv, info->var.xres, info->var.yres,
			   info->fix.line_length,
			   par->bpp, par->depth);
	if (vmw_priv->capabilities & SVGA_CAP_DISPLAY_TOPOLOGY) {
		/* TODO check if pitch and offset changes */
		vmw_write(vmw_priv, SVGA_REG_NUM_GUEST_DISPLAYS, 1);
		vmw_write(vmw_priv, SVGA_REG_DISPLAY_ID, 0);
		vmw_write(vmw_priv, SVGA_REG_DISPLAY_IS_PRIMARY, true);
		vmw_write(vmw_priv, SVGA_REG_DISPLAY_POSITION_X, info->var.xoffset);
		vmw_write(vmw_priv, SVGA_REG_DISPLAY_POSITION_Y, info->var.yoffset);
		vmw_write(vmw_priv, SVGA_REG_DISPLAY_WIDTH, info->var.xres);
		vmw_write(vmw_priv, SVGA_REG_DISPLAY_HEIGHT, info->var.yres);
		vmw_write(vmw_priv, SVGA_REG_DISPLAY_ID, SVGA_ID_INVALID);
	}

	/* This is really helpful since if this fails the user
	 * can probably not see anything on the screen.
	 */
	WARN_ON(vmw_read(vmw_priv, SVGA_REG_FB_OFFSET) != 0);

	return 0;
}

static int vmw_fb_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	return 0;
}

static int vmw_fb_blank(int blank, struct fb_info *info)
{
	return 0;
}

/*
 * Dirty code
 */

static void vmw_fb_dirty_flush(struct vmw_fb_par *par)
{
	struct vmw_private *vmw_priv = par->vmw_priv;
	struct fb_info *info = vmw_priv->fb_info;
	int stride = (info->fix.line_length / 4);
	int *src = (int *)info->screen_base;
	__le32 __iomem *vram_mem = par->bo_ptr;
	unsigned long flags;
	unsigned x, y, w, h;
	int i, k;
	struct {
		uint32_t header;
		SVGAFifoCmdUpdate body;
	} *cmd;

	if (vmw_priv->suspended)
		return;

	spin_lock_irqsave(&par->dirty.lock, flags);
	if (!par->dirty.active) {
		spin_unlock_irqrestore(&par->dirty.lock, flags);
		return;
	}
	x = par->dirty.x1;
	y = par->dirty.y1;
	w = min(par->dirty.x2, info->var.xres) - x;
	h = min(par->dirty.y2, info->var.yres) - y;
	par->dirty.x1 = par->dirty.x2 = 0;
	par->dirty.y1 = par->dirty.y2 = 0;
	spin_unlock_irqrestore(&par->dirty.lock, flags);

	for (i = y * stride; i < info->fix.smem_len / 4; i += stride) {
		for (k = i+x; k < i+x+w && k < info->fix.smem_len / 4; k++)
			iowrite32(src[k], vram_mem + k);
	}

#if 0
	DRM_INFO("%s, (%u, %u) (%ux%u)\n", __func__, x, y, w, h);
#endif

	cmd = vmw_fifo_reserve(vmw_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Fifo reserve failed.\n");
		return;
	}

	cmd->header = cpu_to_le32(SVGA_CMD_UPDATE);
	cmd->body.x = cpu_to_le32(x);
	cmd->body.y = cpu_to_le32(y);
	cmd->body.width = cpu_to_le32(w);
	cmd->body.height = cpu_to_le32(h);
	vmw_fifo_commit(vmw_priv, sizeof(*cmd));
}

static void vmw_fb_dirty_mark(struct vmw_fb_par *par,
			      unsigned x1, unsigned y1,
			      unsigned width, unsigned height)
{
	struct fb_info *info = par->vmw_priv->fb_info;
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
			schedule_delayed_work(&info->deferred_work, VMW_DIRTY_DELAY);
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
	}

	vmw_fb_dirty_flush(par);
};

struct fb_deferred_io vmw_defio = {
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

static int vmw_fb_create_bo(struct vmw_private *vmw_priv,
			    size_t size, struct vmw_dma_buffer **out)
{
	struct vmw_dma_buffer *vmw_bo;
	struct ttm_placement ne_placement = vmw_vram_ne_placement;
	int ret;

	ne_placement.lpfn = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	/* interuptable? */
	ret = ttm_write_lock(&vmw_priv->fbdev_master.lock, false);
	if (unlikely(ret != 0))
		return ret;

	vmw_bo = kmalloc(sizeof(*vmw_bo), GFP_KERNEL);
	if (!vmw_bo)
		goto err_unlock;

	ret = vmw_dmabuf_init(vmw_priv, vmw_bo, size,
			      &ne_placement,
			      false,
			      &vmw_dmabuf_bo_free);
	if (unlikely(ret != 0))
		goto err_unlock; /* init frees the buffer on failure */

	*out = vmw_bo;

	ttm_write_unlock(&vmw_priv->fbdev_master.lock);

	return 0;

err_unlock:
	ttm_write_unlock(&vmw_priv->fbdev_master.lock);
	return ret;
}

int vmw_fb_init(struct vmw_private *vmw_priv)
{
	struct device *device = &vmw_priv->dev->pdev->dev;
	struct vmw_fb_par *par;
	struct fb_info *info;
	unsigned initial_width, initial_height;
	unsigned fb_width, fb_height;
	unsigned fb_bbp, fb_depth, fb_offset, fb_pitch, fb_size;
	int ret;

	/* XXX These shouldn't be hardcoded. */
	initial_width = 800;
	initial_height = 600;

	fb_bbp = 32;
	fb_depth = 24;

	/* XXX As shouldn't these be as well. */
	fb_width = min(vmw_priv->fb_max_width, (unsigned)2048);
	fb_height = min(vmw_priv->fb_max_height, (unsigned)2048);

	initial_width = min(fb_width, initial_width);
	initial_height = min(fb_height, initial_height);

	fb_pitch = fb_width * fb_bbp / 8;
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
	par->vmw_priv = vmw_priv;
	par->depth = fb_depth;
	par->bpp = fb_bbp;
	par->vmalloc = NULL;
	par->max_width = fb_width;
	par->max_height = fb_height;

	/*
	 * Create buffers and alloc memory
	 */
	par->vmalloc = vmalloc(fb_size);
	if (unlikely(par->vmalloc == NULL)) {
		ret = -ENOMEM;
		goto err_free;
	}

	ret = vmw_fb_create_bo(vmw_priv, fb_size, &par->vmw_bo);
	if (unlikely(ret != 0))
		goto err_free;

	ret = ttm_bo_kmap(&par->vmw_bo->base,
			  0,
			  par->vmw_bo->base.num_pages,
			  &par->map);
	if (unlikely(ret != 0))
		goto err_unref;
	par->bo_ptr = ttm_kmap_obj_virtual(&par->map, &par->bo_iowrite);
	par->bo_size = fb_size;

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

	info->fix.mmio_start = 0;
	info->fix.mmio_len = 0;

	info->pseudo_palette = par->pseudo_palette;
	info->screen_base = par->vmalloc;
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
	info->var.bits_per_pixel = par->bpp;
	info->var.xoffset = 0;
	info->var.yoffset = 0;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.height = -1;
	info->var.width = -1;

	info->var.xres = initial_width;
	info->var.yres = initial_height;

#if 0
	info->pixmap.size = 64*1024;
	info->pixmap.buf_align = 8;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;
	info->pixmap.scan_align = 1;
#else
	info->pixmap.size = 0;
	info->pixmap.buf_align = 8;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;
	info->pixmap.scan_align = 1;
#endif

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
	info->fbdefio = &vmw_defio;
	fb_deferred_io_init(info);

	ret = register_framebuffer(info);
	if (unlikely(ret != 0))
		goto err_defio;

	return 0;

err_defio:
	fb_deferred_io_cleanup(info);
err_aper:
	ttm_bo_kunmap(&par->map);
err_unref:
	ttm_bo_unref((struct ttm_buffer_object **)&par->vmw_bo);
err_free:
	vfree(par->vmalloc);
	framebuffer_release(info);
	vmw_priv->fb_info = NULL;

	return ret;
}

int vmw_fb_close(struct vmw_private *vmw_priv)
{
	struct fb_info *info;
	struct vmw_fb_par *par;
	struct ttm_buffer_object *bo;

	if (!vmw_priv->fb_info)
		return 0;

	info = vmw_priv->fb_info;
	par = info->par;
	bo = &par->vmw_bo->base;
	par->vmw_bo = NULL;

	/* ??? order */
	fb_deferred_io_cleanup(info);
	unregister_framebuffer(info);

	ttm_bo_kunmap(&par->map);
	ttm_bo_unref(&bo);

	vfree(par->vmalloc);
	framebuffer_release(info);

	return 0;
}

int vmw_dmabuf_from_vram(struct vmw_private *vmw_priv,
			 struct vmw_dma_buffer *vmw_bo)
{
	struct ttm_buffer_object *bo = &vmw_bo->base;
	int ret = 0;

	ret = ttm_bo_reserve(bo, false, false, false, 0);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_bo_validate(bo, &vmw_sys_placement, false, false, false);
	ttm_bo_unreserve(bo);

	return ret;
}

int vmw_dmabuf_to_start_of_vram(struct vmw_private *vmw_priv,
				struct vmw_dma_buffer *vmw_bo)
{
	struct ttm_buffer_object *bo = &vmw_bo->base;
	struct ttm_placement ne_placement = vmw_vram_ne_placement;
	int ret = 0;

	ne_placement.lpfn = bo->num_pages;

	/* interuptable? */
	ret = ttm_write_lock(&vmw_priv->active_master->lock, false);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_bo_reserve(bo, false, false, false, 0);
	if (unlikely(ret != 0))
		goto err_unlock;

	if (bo->mem.mem_type == TTM_PL_VRAM &&
	    bo->mem.start < bo->num_pages &&
	    bo->mem.start > 0)
		(void) ttm_bo_validate(bo, &vmw_sys_placement, false,
				       false, false);

	ret = ttm_bo_validate(bo, &ne_placement, false, false, false);

	/* Could probably bug on */
	WARN_ON(bo->offset != 0);

	ttm_bo_unreserve(bo);
err_unlock:
	ttm_write_unlock(&vmw_priv->active_master->lock);

	return ret;
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

	flush_delayed_work_sync(&info->deferred_work);

	par->bo_ptr = NULL;
	ttm_bo_kunmap(&par->map);

	vmw_dmabuf_from_vram(vmw_priv, par->vmw_bo);

	return 0;
}

int vmw_fb_on(struct vmw_private *vmw_priv)
{
	struct fb_info *info;
	struct vmw_fb_par *par;
	unsigned long flags;
	bool dummy;
	int ret;

	if (!vmw_priv->fb_info)
		return -EINVAL;

	info = vmw_priv->fb_info;
	par = info->par;

	/* we are already active */
	if (par->bo_ptr != NULL)
		return 0;

	/* Make sure that all overlays are stoped when we take over */
	vmw_overlay_stop_all(vmw_priv);

	ret = vmw_dmabuf_to_start_of_vram(vmw_priv, par->vmw_bo);
	if (unlikely(ret != 0)) {
		DRM_ERROR("could not move buffer to start of VRAM\n");
		goto err_no_buffer;
	}

	ret = ttm_bo_kmap(&par->vmw_bo->base,
			  0,
			  par->vmw_bo->base.num_pages,
			  &par->map);
	BUG_ON(ret != 0);
	par->bo_ptr = ttm_kmap_obj_virtual(&par->map, &dummy);

	spin_lock_irqsave(&par->dirty.lock, flags);
	par->dirty.active = true;
	spin_unlock_irqrestore(&par->dirty.lock, flags);

err_no_buffer:
	vmw_fb_set_par(info);

	vmw_fb_dirty_mark(par, 0, 0, info->var.xres, info->var.yres);

	/* If there already was stuff dirty we wont
	 * schedule a new work, so lets do it now */
	schedule_delayed_work(&info->deferred_work, 0);

	return 0;
}
