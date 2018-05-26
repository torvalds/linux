/*
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
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

#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>

#include "omap_drv.h"

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

static struct drm_fb_helper *get_fb(struct fb_info *fbi);

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

	.fb_check_var	= drm_fb_helper_check_var,
	.fb_set_par	= drm_fb_helper_set_par,
	.fb_setcmap	= drm_fb_helper_setcmap,
	.fb_blank	= drm_fb_helper_blank,
	.fb_pan_display = omap_fbdev_pan_display,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
	.fb_ioctl	= drm_fb_helper_ioctl,

	.fb_read = drm_fb_helper_sys_read,
	.fb_write = drm_fb_helper_sys_write,
	.fb_fillrect = drm_fb_helper_sys_fillrect,
	.fb_copyarea = drm_fb_helper_sys_copyarea,
	.fb_imageblit = drm_fb_helper_sys_imageblit,
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
	dma_addr_t dma_addr;
	int ret;

	sizes->surface_bpp = 32;
	sizes->surface_depth = 24;

	DBG("create fbdev: %dx%d@%d (%dx%d)", sizes->surface_width,
			sizes->surface_height, sizes->surface_bpp,
			sizes->fb_width, sizes->fb_height);

	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
			sizes->surface_depth);

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	mode_cmd.pitches[0] =
			DIV_ROUND_UP(mode_cmd.width * sizes->surface_bpp, 8);

	fbdev->ywrap_enabled = priv->has_dmm && ywrap_enabled;
	if (fbdev->ywrap_enabled) {
		/* need to align pitch to page size if using DMM scrolling */
		mode_cmd.pitches[0] = PAGE_ALIGN(mode_cmd.pitches[0]);
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
		drm_gem_object_unreference_unlocked(fbdev->bo);
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
	ret = omap_gem_pin(fbdev->bo, &dma_addr);
	if (ret) {
		dev_err(dev->dev, "could not pin framebuffer\n");
		ret = -ENOMEM;
		goto fail;
	}

	fbi = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(fbi)) {
		dev_err(dev->dev, "failed to allocate fb info\n");
		ret = PTR_ERR(fbi);
		goto fail;
	}

	DBG("fbi=%p, dev=%p", fbi, dev);

	fbdev->fb = fb;
	helper->fb = fb;

	fbi->par = helper;
	fbi->fbops = &omap_fb_ops;

	strcpy(fbi->fix.id, MODULE_NAME);

	drm_fb_helper_fill_fix(fbi, fb->pitches[0], fb->format->depth);
	drm_fb_helper_fill_var(fbi, helper, sizes->fb_width, sizes->fb_height);

	dev->mode_config.fb_base = dma_addr;

	fbi->screen_buffer = omap_gem_vaddr(fbdev->bo);
	fbi->screen_size = fbdev->bo->size;
	fbi->fix.smem_start = dma_addr;
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

	return 0;

fail:

	if (ret) {
		if (fb)
			drm_framebuffer_remove(fb);
	}

	return ret;
}

static const struct drm_fb_helper_funcs omap_fb_helper_funcs = {
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

/* initialize fbdev helper */
void omap_fbdev_init(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_fbdev *fbdev = NULL;
	struct drm_fb_helper *helper;
	int ret = 0;

	if (!priv->num_crtcs || !priv->num_connectors)
		return;

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev)
		goto fail;

	INIT_WORK(&fbdev->work, pan_worker);

	helper = &fbdev->base;

	drm_fb_helper_prepare(dev, helper, &omap_fb_helper_funcs);

	ret = drm_fb_helper_init(dev, helper, priv->num_connectors);
	if (ret)
		goto fail;

	ret = drm_fb_helper_single_add_all_connectors(helper);
	if (ret)
		goto fini;

	ret = drm_fb_helper_initial_config(helper, 32);
	if (ret)
		goto fini;

	priv->fbdev = helper;

	return;

fini:
	drm_fb_helper_fini(helper);
fail:
	kfree(fbdev);

	dev_warn(dev->dev, "omap_fbdev_init failed\n");
}

void omap_fbdev_fini(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_fb_helper *helper = priv->fbdev;
	struct omap_fbdev *fbdev;

	DBG();

	if (!helper)
		return;

	drm_fb_helper_unregister_fbi(helper);

	drm_fb_helper_fini(helper);

	fbdev = to_omap_fbdev(helper);

	/* unpin the GEM object pinned in omap_fbdev_create() */
	if (fbdev->bo)
		omap_gem_unpin(fbdev->bo);

	/* this will free the backing object */
	if (fbdev->fb)
		drm_framebuffer_remove(fbdev->fb);

	kfree(fbdev);

	priv->fbdev = NULL;
}
