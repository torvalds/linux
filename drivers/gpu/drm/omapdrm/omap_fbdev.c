/*
 * drivers/gpu/drm/omapdrm/omap_fbdev.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "omap_drv.h"

#include "drm_crtc.h"
#include "drm_fb_helper.h"

MODULE_PARM_DESC(ywrap, "Enable ywrap scrolling (omap44xx and later, default 'y')");
static bool ywrap_enabled = true;
module_param_named(ywrap, ywrap_enabled, bool, 0644);

/*
 * fbdev funcs, to implement legacy fbdev interface on top of drm driver
 */

#define to_omap_fbdev(x) container_of(x, struct omap_fbdev, base)

struct omap_fbdev {
	struct drm_fb_helper base;
	struct drm_framebuffer *fb;
	struct drm_gem_object *bo;
	bool ywrap_enabled;

	/* for deferred dmm roll when getting called in atomic ctx */
	struct work_struct work;
};

static void omap_fbdev_flush(struct fb_info *fbi, int x, int y, int w, int h);
static struct drm_fb_helper *get_fb(struct fb_info *fbi);

static ssize_t omap_fbdev_write(struct fb_info *fbi, const char __user *buf,
		size_t count, loff_t *ppos)
{
	ssize_t res;

	res = fb_sys_write(fbi, buf, count, ppos);
	omap_fbdev_flush(fbi, 0, 0, fbi->var.xres, fbi->var.yres);

	return res;
}

static void omap_fbdev_fillrect(struct fb_info *fbi,
		const struct fb_fillrect *rect)
{
	sys_fillrect(fbi, rect);
	omap_fbdev_flush(fbi, rect->dx, rect->dy, rect->width, rect->height);
}

static void omap_fbdev_copyarea(struct fb_info *fbi,
		const struct fb_copyarea *area)
{
	sys_copyarea(fbi, area);
	omap_fbdev_flush(fbi, area->dx, area->dy, area->width, area->height);
}

static void omap_fbdev_imageblit(struct fb_info *fbi,
		const struct fb_image *image)
{
	sys_imageblit(fbi, image);
	omap_fbdev_flush(fbi, image->dx, image->dy,
				image->width, image->height);
}

static void pan_worker(struct work_struct *work)
{
	struct omap_fbdev *fbdev = container_of(work, struct omap_fbdev, work);
	struct fb_info *fbi = fbdev->base.fbdev;
	int npages;

	/* DMM roll shifts in 4K pages: */
	npages = fbi->fix.line_length >> PAGE_SHIFT;
	omap_gem_roll(fbdev->bo, fbi->var.yoffset * npages);
}

static int omap_fbdev_pan_display(struct fb_var_screeninfo *var,
		struct fb_info *fbi)
{
	struct drm_fb_helper *helper = get_fb(fbi);
	struct omap_fbdev *fbdev = to_omap_fbdev(helper);

	if (!helper)
		goto fallback;

	if (!fbdev->ywrap_enabled)
		goto fallback;

	if (drm_can_sleep()) {
		pan_worker(&fbdev->work);
	} else {
		struct omap_drm_private *priv = helper->dev->dev_private;
		queue_work(priv->wq, &fbdev->work);
	}

	return 0;

fallback:
	return drm_fb_helper_pan_display(var, fbi);
}

static struct fb_ops omap_fb_ops = {
	.owner = THIS_MODULE,

	/* Note: to properly handle manual update displays, we wrap the
	 * basic fbdev ops which write to the framebuffer
	 */
	.fb_read = fb_sys_read,
	.fb_write = omap_fbdev_write,
	.fb_fillrect = omap_fbdev_fillrect,
	.fb_copyarea = omap_fbdev_copyarea,
	.fb_imageblit = omap_fbdev_imageblit,

	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_pan_display = omap_fbdev_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
};

static int omap_fbdev_create(struct drm_fb_helper *helper,
		struct drm_fb_helper_surface_size *sizes)
{
	struct omap_fbdev *fbdev = to_omap_fbdev(helper);
	struct drm_device *dev = helper->dev;
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_framebuffer *fb = NULL;
	union omap_gem_size gsize;
	struct fb_info *fbi = NULL;
	struct drm_mode_fb_cmd2 mode_cmd = {0};
	dma_addr_t paddr;
	int ret;

	/* only doing ARGB32 since this is what is needed to alpha-blend
	 * with video overlays:
	 */
	sizes->surface_bpp = 32;
	sizes->surface_depth = 32;

	DBG("create fbdev: %dx%d@%d (%dx%d)", sizes->surface_width,
			sizes->surface_height, sizes->surface_bpp,
			sizes->fb_width, sizes->fb_height);

	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
			sizes->surface_depth);

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	mode_cmd.pitches[0] = align_pitch(
			mode_cmd.width * ((sizes->surface_bpp + 7) / 8),
			mode_cmd.width, sizes->surface_bpp);

	fbdev->ywrap_enabled = priv->has_dmm && ywrap_enabled;
	if (fbdev->ywrap_enabled) {
		/* need to align pitch to page size if using DMM scrolling */
		mode_cmd.pitches[0] = ALIGN(mode_cmd.pitches[0], PAGE_SIZE);
	}

	/* allocate backing bo */
	gsize = (union omap_gem_size){
		.bytes = PAGE_ALIGN(mode_cmd.pitches[0] * mode_cmd.height),
	};
	DBG("allocating %d bytes for fb %d", gsize.bytes, dev->primary->index);
	fbdev->bo = omap_gem_new(dev, gsize, OMAP_BO_SCANOUT | OMAP_BO_WC);
	if (!fbdev->bo) {
		dev_err(dev->dev, "failed to allocate buffer object\n");
		ret = -ENOMEM;
		goto fail;
	}

	fb = omap_framebuffer_init(dev, &mode_cmd, &fbdev->bo);
	if (IS_ERR(fb)) {
		dev_err(dev->dev, "failed to allocate fb\n");
		/* note: if fb creation failed, we can't rely on fb destroy
		 * to unref the bo:
		 */
		drm_gem_object_unreference(fbdev->bo);
		ret = PTR_ERR(fb);
		goto fail;
	}

	/* note: this keeps the bo pinned.. which is perhaps not ideal,
	 * but is needed as long as we use fb_mmap() to mmap to userspace
	 * (since this happens using fix.smem_start).  Possibly we could
	 * implement our own mmap using GEM mmap support to avoid this
	 * (non-tiled buffer doesn't need to be pinned for fbcon to write
	 * to it).  Then we just need to be sure that we are able to re-
	 * pin it in case of an opps.
	 */
	ret = omap_gem_get_paddr(fbdev->bo, &paddr, true);
	if (ret) {
		dev_err(dev->dev,
			"could not map (paddr)!  Skipping framebuffer alloc\n");
		ret = -ENOMEM;
		goto fail;
	}

	mutex_lock(&dev->struct_mutex);

	fbi = framebuffer_alloc(0, dev->dev);
	if (!fbi) {
		dev_err(dev->dev, "failed to allocate fb info\n");
		ret = -ENOMEM;
		goto fail_unlock;
	}

	DBG("fbi=%p, dev=%p", fbi, dev);

	fbdev->fb = fb;
	helper->fb = fb;
	helper->fbdev = fbi;

	fbi->par = helper;
	fbi->flags = FBINFO_DEFAULT;
	fbi->fbops = &omap_fb_ops;

	strcpy(fbi->fix.id, MODULE_NAME);

	ret = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (ret) {
		ret = -ENOMEM;
		goto fail_unlock;
	}

	drm_fb_helper_fill_fix(fbi, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(fbi, helper, sizes->fb_width, sizes->fb_height);

	dev->mode_config.fb_base = paddr;

	fbi->screen_base = omap_gem_vaddr(fbdev->bo);
	fbi->screen_size = fbdev->bo->size;
	fbi->fix.smem_start = paddr;
	fbi->fix.smem_len = fbdev->bo->size;

	/* if we have DMM, then we can use it for scrolling by just
	 * shuffling pages around in DMM rather than doing sw blit.
	 */
	if (fbdev->ywrap_enabled) {
		DRM_INFO("Enabling DMM ywrap scrolling\n");
		fbi->flags |= FBINFO_HWACCEL_YWRAP | FBINFO_READS_FAST;
		fbi->fix.ywrapstep = 1;
	}


	DBG("par=%p, %dx%d", fbi->par, fbi->var.xres, fbi->var.yres);
	DBG("allocated %dx%d fb", fbdev->fb->width, fbdev->fb->height);

	mutex_unlock(&dev->struct_mutex);

	return 0;

fail_unlock:
	mutex_unlock(&dev->struct_mutex);
fail:

	if (ret) {
		if (fbi)
			framebuffer_release(fbi);
		if (fb) {
			drm_framebuffer_unregister_private(fb);
			drm_framebuffer_remove(fb);
		}
	}

	return ret;
}

static struct drm_fb_helper_funcs omap_fb_helper_funcs = {
	.fb_probe = omap_fbdev_create,
};

static struct drm_fb_helper *get_fb(struct fb_info *fbi)
{
	if (!fbi || strcmp(fbi->fix.id, MODULE_NAME)) {
		/* these are not the fb's you're looking for */
		return NULL;
	}
	return fbi->par;
}

/* flush an area of the framebuffer (in case of manual update display that
 * is not automatically flushed)
 */
static void omap_fbdev_flush(struct fb_info *fbi, int x, int y, int w, int h)
{
	struct drm_fb_helper *helper = get_fb(fbi);

	if (!helper)
		return;

	VERB("flush fbdev: %d,%d %dx%d, fbi=%p", x, y, w, h, fbi);

	omap_framebuffer_flush(helper->fb, x, y, w, h);
}

/* initialize fbdev helper */
struct drm_fb_helper *omap_fbdev_init(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_fbdev *fbdev = NULL;
	struct drm_fb_helper *helper;
	int ret = 0;

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev)
		goto fail;

	INIT_WORK(&fbdev->work, pan_worker);

	helper = &fbdev->base;

	helper->funcs = &omap_fb_helper_funcs;

	ret = drm_fb_helper_init(dev, helper,
			priv->num_crtcs, priv->num_connectors);
	if (ret) {
		dev_err(dev->dev, "could not init fbdev: ret=%d\n", ret);
		goto fail;
	}

	drm_fb_helper_single_add_all_connectors(helper);

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(dev);

	drm_fb_helper_initial_config(helper, 32);

	priv->fbdev = helper;

	return helper;

fail:
	kfree(fbdev);
	return NULL;
}

void omap_fbdev_free(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_fb_helper *helper = priv->fbdev;
	struct omap_fbdev *fbdev;
	struct fb_info *fbi;

	DBG();

	fbi = helper->fbdev;

	/* only cleanup framebuffer if it is present */
	if (fbi) {
		unregister_framebuffer(fbi);
		framebuffer_release(fbi);
	}

	drm_fb_helper_fini(helper);

	fbdev = to_omap_fbdev(priv->fbdev);

	/* this will free the backing object */
	if (fbdev->fb) {
		drm_framebuffer_unregister_private(fbdev->fb);
		drm_framebuffer_remove(fbdev->fb);
	}

	kfree(fbdev);

	priv->fbdev = NULL;
}
