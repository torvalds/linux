/*
 * Copyright (c) 2006-2009 Red Hat Inc.
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 *
 * DRM framebuffer helper functions
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 *
 * Authors:
 *      Dave Airlie <airlied@linux.ie>
 *      Jesse Barnes <jesse.barnes@intel.com>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/console.h>
#include <linux/dma-buf.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysrq.h>
#include <linux/vmalloc.h>

#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "drm_crtc_helper_internal.h"
#include "drm_internal.h"

static bool drm_fbdev_emulation = true;
module_param_named(fbdev_emulation, drm_fbdev_emulation, bool, 0600);
MODULE_PARM_DESC(fbdev_emulation,
		 "Enable legacy fbdev emulation [default=true]");

static int drm_fbdev_overalloc = CONFIG_DRM_FBDEV_OVERALLOC;
module_param(drm_fbdev_overalloc, int, 0444);
MODULE_PARM_DESC(drm_fbdev_overalloc,
		 "Overallocation of the fbdev buffer (%) [default="
		 __MODULE_STRING(CONFIG_DRM_FBDEV_OVERALLOC) "]");

/*
 * In order to keep user-space compatibility, we want in certain use-cases
 * to keep leaking the fbdev physical address to the user-space program
 * handling the fbdev buffer.
 * This is a bad habit essentially kept into closed source opengl driver
 * that should really be moved into open-source upstream projects instead
 * of using legacy physical addresses in user space to communicate with
 * other out-of-tree kernel modules.
 *
 * This module_param *should* be removed as soon as possible and be
 * considered as a broken and legacy behaviour from a modern fbdev device.
 */
#if IS_ENABLED(CONFIG_DRM_FBDEV_LEAK_PHYS_SMEM)
static bool drm_leak_fbdev_smem = false;
module_param_unsafe(drm_leak_fbdev_smem, bool, 0600);
MODULE_PARM_DESC(drm_leak_fbdev_smem,
		 "Allow unsafe leaking fbdev physical smem address [default=false]");
#endif

static LIST_HEAD(kernel_fb_helper_list);
static DEFINE_MUTEX(kernel_fb_helper_lock);

/**
 * DOC: fbdev helpers
 *
 * The fb helper functions are useful to provide an fbdev on top of a drm kernel
 * mode setting driver. They can be used mostly independently from the crtc
 * helper functions used by many drivers to implement the kernel mode setting
 * interfaces.
 *
 * Drivers that support a dumb buffer with a virtual address and mmap support,
 * should try out the generic fbdev emulation using drm_fbdev_generic_setup().
 * It will automatically set up deferred I/O if the driver requires a shadow
 * buffer.
 *
 * At runtime drivers should restore the fbdev console by using
 * drm_fb_helper_lastclose() as their &drm_driver.lastclose callback.
 * They should also notify the fb helper code from updates to the output
 * configuration by using drm_fb_helper_output_poll_changed() as their
 * &drm_mode_config_funcs.output_poll_changed callback.
 *
 * For suspend/resume consider using drm_mode_config_helper_suspend() and
 * drm_mode_config_helper_resume() which takes care of fbdev as well.
 *
 * All other functions exported by the fb helper library can be used to
 * implement the fbdev driver interface by the driver.
 *
 * It is possible, though perhaps somewhat tricky, to implement race-free
 * hotplug detection using the fbdev helpers. The drm_fb_helper_prepare()
 * helper must be called first to initialize the minimum required to make
 * hotplug detection work. Drivers also need to make sure to properly set up
 * the &drm_mode_config.funcs member. After calling drm_kms_helper_poll_init()
 * it is safe to enable interrupts and start processing hotplug events. At the
 * same time, drivers should initialize all modeset objects such as CRTCs,
 * encoders and connectors. To finish up the fbdev helper initialization, the
 * drm_fb_helper_init() function is called. To probe for all attached displays
 * and set up an initial configuration using the detected hardware, drivers
 * should call drm_fb_helper_initial_config().
 *
 * If &drm_framebuffer_funcs.dirty is set, the
 * drm_fb_helper_{cfb,sys}_{write,fillrect,copyarea,imageblit} functions will
 * accumulate changes and schedule &drm_fb_helper.dirty_work to run right
 * away. This worker then calls the dirty() function ensuring that it will
 * always run in process context since the fb_*() function could be running in
 * atomic context. If drm_fb_helper_deferred_io() is used as the deferred_io
 * callback it will also schedule dirty_work with the damage collected from the
 * mmap page writes.
 *
 * Deferred I/O is not compatible with SHMEM. Such drivers should request an
 * fbdev shadow buffer and call drm_fbdev_generic_setup() instead.
 */

static void drm_fb_helper_restore_lut_atomic(struct drm_crtc *crtc)
{
	uint16_t *r_base, *g_base, *b_base;

	if (crtc->funcs->gamma_set == NULL)
		return;

	r_base = crtc->gamma_store;
	g_base = r_base + crtc->gamma_size;
	b_base = g_base + crtc->gamma_size;

	crtc->funcs->gamma_set(crtc, r_base, g_base, b_base,
			       crtc->gamma_size, NULL);
}

/**
 * drm_fb_helper_debug_enter - implementation for &fb_ops.fb_debug_enter
 * @info: fbdev registered by the helper
 */
int drm_fb_helper_debug_enter(struct fb_info *info)
{
	struct drm_fb_helper *helper = info->par;
	const struct drm_crtc_helper_funcs *funcs;
	struct drm_mode_set *mode_set;

	list_for_each_entry(helper, &kernel_fb_helper_list, kernel_fb_list) {
		mutex_lock(&helper->client.modeset_mutex);
		drm_client_for_each_modeset(mode_set, &helper->client) {
			if (!mode_set->crtc->enabled)
				continue;

			funcs =	mode_set->crtc->helper_private;
			if (funcs->mode_set_base_atomic == NULL)
				continue;

			if (drm_drv_uses_atomic_modeset(mode_set->crtc->dev))
				continue;

			funcs->mode_set_base_atomic(mode_set->crtc,
						    mode_set->fb,
						    mode_set->x,
						    mode_set->y,
						    ENTER_ATOMIC_MODE_SET);
		}
		mutex_unlock(&helper->client.modeset_mutex);
	}

	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_debug_enter);

/**
 * drm_fb_helper_debug_leave - implementation for &fb_ops.fb_debug_leave
 * @info: fbdev registered by the helper
 */
int drm_fb_helper_debug_leave(struct fb_info *info)
{
	struct drm_fb_helper *helper = info->par;
	struct drm_client_dev *client = &helper->client;
	struct drm_device *dev = helper->dev;
	struct drm_crtc *crtc;
	const struct drm_crtc_helper_funcs *funcs;
	struct drm_mode_set *mode_set;
	struct drm_framebuffer *fb;

	mutex_lock(&client->modeset_mutex);
	drm_client_for_each_modeset(mode_set, client) {
		crtc = mode_set->crtc;
		if (drm_drv_uses_atomic_modeset(crtc->dev))
			continue;

		funcs = crtc->helper_private;
		fb = crtc->primary->fb;

		if (!crtc->enabled)
			continue;

		if (!fb) {
			drm_err(dev, "no fb to restore?\n");
			continue;
		}

		if (funcs->mode_set_base_atomic == NULL)
			continue;

		drm_fb_helper_restore_lut_atomic(mode_set->crtc);
		funcs->mode_set_base_atomic(mode_set->crtc, fb, crtc->x,
					    crtc->y, LEAVE_ATOMIC_MODE_SET);
	}
	mutex_unlock(&client->modeset_mutex);

	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_debug_leave);

static int
__drm_fb_helper_restore_fbdev_mode_unlocked(struct drm_fb_helper *fb_helper,
					    bool force)
{
	bool do_delayed;
	int ret;

	if (!drm_fbdev_emulation || !fb_helper)
		return -ENODEV;

	if (READ_ONCE(fb_helper->deferred_setup))
		return 0;

	mutex_lock(&fb_helper->lock);
	if (force) {
		/*
		 * Yes this is the _locked version which expects the master lock
		 * to be held. But for forced restores we're intentionally
		 * racing here, see drm_fb_helper_set_par().
		 */
		ret = drm_client_modeset_commit_locked(&fb_helper->client);
	} else {
		ret = drm_client_modeset_commit(&fb_helper->client);
	}

	do_delayed = fb_helper->delayed_hotplug;
	if (do_delayed)
		fb_helper->delayed_hotplug = false;
	mutex_unlock(&fb_helper->lock);

	if (do_delayed)
		drm_fb_helper_hotplug_event(fb_helper);

	return ret;
}

/**
 * drm_fb_helper_restore_fbdev_mode_unlocked - restore fbdev configuration
 * @fb_helper: driver-allocated fbdev helper, can be NULL
 *
 * This should be called from driver's drm &drm_driver.lastclose callback
 * when implementing an fbcon on top of kms using this helper. This ensures that
 * the user isn't greeted with a black screen when e.g. X dies.
 *
 * RETURNS:
 * Zero if everything went ok, negative error code otherwise.
 */
int drm_fb_helper_restore_fbdev_mode_unlocked(struct drm_fb_helper *fb_helper)
{
	return __drm_fb_helper_restore_fbdev_mode_unlocked(fb_helper, false);
}
EXPORT_SYMBOL(drm_fb_helper_restore_fbdev_mode_unlocked);

#ifdef CONFIG_MAGIC_SYSRQ
/* emergency restore, don't bother with error reporting */
static void drm_fb_helper_restore_work_fn(struct work_struct *ignored)
{
	struct drm_fb_helper *helper;

	mutex_lock(&kernel_fb_helper_lock);
	list_for_each_entry(helper, &kernel_fb_helper_list, kernel_fb_list) {
		struct drm_device *dev = helper->dev;

		if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
			continue;

		mutex_lock(&helper->lock);
		drm_client_modeset_commit_locked(&helper->client);
		mutex_unlock(&helper->lock);
	}
	mutex_unlock(&kernel_fb_helper_lock);
}

static DECLARE_WORK(drm_fb_helper_restore_work, drm_fb_helper_restore_work_fn);

static void drm_fb_helper_sysrq(int dummy1)
{
	schedule_work(&drm_fb_helper_restore_work);
}

static const struct sysrq_key_op sysrq_drm_fb_helper_restore_op = {
	.handler = drm_fb_helper_sysrq,
	.help_msg = "force-fb(v)",
	.action_msg = "Restore framebuffer console",
};
#else
static const struct sysrq_key_op sysrq_drm_fb_helper_restore_op = { };
#endif

static void drm_fb_helper_dpms(struct fb_info *info, int dpms_mode)
{
	struct drm_fb_helper *fb_helper = info->par;

	mutex_lock(&fb_helper->lock);
	drm_client_modeset_dpms(&fb_helper->client, dpms_mode);
	mutex_unlock(&fb_helper->lock);
}

/**
 * drm_fb_helper_blank - implementation for &fb_ops.fb_blank
 * @blank: desired blanking state
 * @info: fbdev registered by the helper
 */
int drm_fb_helper_blank(int blank, struct fb_info *info)
{
	if (oops_in_progress)
		return -EBUSY;

	switch (blank) {
	/* Display: On; HSync: On, VSync: On */
	case FB_BLANK_UNBLANK:
		drm_fb_helper_dpms(info, DRM_MODE_DPMS_ON);
		break;
	/* Display: Off; HSync: On, VSync: On */
	case FB_BLANK_NORMAL:
		drm_fb_helper_dpms(info, DRM_MODE_DPMS_STANDBY);
		break;
	/* Display: Off; HSync: Off, VSync: On */
	case FB_BLANK_HSYNC_SUSPEND:
		drm_fb_helper_dpms(info, DRM_MODE_DPMS_STANDBY);
		break;
	/* Display: Off; HSync: On, VSync: Off */
	case FB_BLANK_VSYNC_SUSPEND:
		drm_fb_helper_dpms(info, DRM_MODE_DPMS_SUSPEND);
		break;
	/* Display: Off; HSync: Off, VSync: Off */
	case FB_BLANK_POWERDOWN:
		drm_fb_helper_dpms(info, DRM_MODE_DPMS_OFF);
		break;
	}
	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_blank);

static void drm_fb_helper_resume_worker(struct work_struct *work)
{
	struct drm_fb_helper *helper = container_of(work, struct drm_fb_helper,
						    resume_work);

	console_lock();
	fb_set_suspend(helper->fbdev, 0);
	console_unlock();
}

static void drm_fb_helper_damage_blit_real(struct drm_fb_helper *fb_helper,
					   struct drm_clip_rect *clip,
					   struct iosys_map *dst)
{
	struct drm_framebuffer *fb = fb_helper->fb;
	unsigned int cpp = fb->format->cpp[0];
	size_t offset = clip->y1 * fb->pitches[0] + clip->x1 * cpp;
	void *src = fb_helper->fbdev->screen_buffer + offset;
	size_t len = (clip->x2 - clip->x1) * cpp;
	unsigned int y;

	iosys_map_incr(dst, offset); /* go to first pixel within clip rect */

	for (y = clip->y1; y < clip->y2; y++) {
		iosys_map_memcpy_to(dst, 0, src, len);
		iosys_map_incr(dst, fb->pitches[0]);
		src += fb->pitches[0];
	}
}

static int drm_fb_helper_damage_blit(struct drm_fb_helper *fb_helper,
				     struct drm_clip_rect *clip)
{
	struct drm_client_buffer *buffer = fb_helper->buffer;
	struct iosys_map map, dst;
	int ret;

	/*
	 * We have to pin the client buffer to its current location while
	 * flushing the shadow buffer. In the general case, concurrent
	 * modesetting operations could try to move the buffer and would
	 * fail. The modeset has to be serialized by acquiring the reservation
	 * object of the underlying BO here.
	 *
	 * For fbdev emulation, we only have to protect against fbdev modeset
	 * operations. Nothing else will involve the client buffer's BO. So it
	 * is sufficient to acquire struct drm_fb_helper.lock here.
	 */
	mutex_lock(&fb_helper->lock);

	ret = drm_client_buffer_vmap(buffer, &map);
	if (ret)
		goto out;

	dst = map;
	drm_fb_helper_damage_blit_real(fb_helper, clip, &dst);

	drm_client_buffer_vunmap(buffer);

out:
	mutex_unlock(&fb_helper->lock);

	return ret;
}

static void drm_fb_helper_damage_work(struct work_struct *work)
{
	struct drm_fb_helper *helper = container_of(work, struct drm_fb_helper,
						    damage_work);
	struct drm_device *dev = helper->dev;
	struct drm_clip_rect *clip = &helper->damage_clip;
	struct drm_clip_rect clip_copy;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&helper->damage_lock, flags);
	clip_copy = *clip;
	clip->x1 = clip->y1 = ~0;
	clip->x2 = clip->y2 = 0;
	spin_unlock_irqrestore(&helper->damage_lock, flags);

	/* Call damage handlers only if necessary */
	if (!(clip_copy.x1 < clip_copy.x2 && clip_copy.y1 < clip_copy.y2))
		return;

	if (helper->buffer) {
		ret = drm_fb_helper_damage_blit(helper, &clip_copy);
		if (drm_WARN_ONCE(dev, ret, "Damage blitter failed: ret=%d\n", ret))
			goto err;
	}

	if (helper->fb->funcs->dirty) {
		ret = helper->fb->funcs->dirty(helper->fb, NULL, 0, 0, &clip_copy, 1);
		if (drm_WARN_ONCE(dev, ret, "Dirty helper failed: ret=%d\n", ret))
			goto err;
	}

	return;

err:
	/*
	 * Restore damage clip rectangle on errors. The next run
	 * of the damage worker will perform the update.
	 */
	spin_lock_irqsave(&helper->damage_lock, flags);
	clip->x1 = min_t(u32, clip->x1, clip_copy.x1);
	clip->y1 = min_t(u32, clip->y1, clip_copy.y1);
	clip->x2 = max_t(u32, clip->x2, clip_copy.x2);
	clip->y2 = max_t(u32, clip->y2, clip_copy.y2);
	spin_unlock_irqrestore(&helper->damage_lock, flags);
}

/**
 * drm_fb_helper_prepare - setup a drm_fb_helper structure
 * @dev: DRM device
 * @helper: driver-allocated fbdev helper structure to set up
 * @funcs: pointer to structure of functions associate with this helper
 *
 * Sets up the bare minimum to make the framebuffer helper usable. This is
 * useful to implement race-free initialization of the polling helpers.
 */
void drm_fb_helper_prepare(struct drm_device *dev, struct drm_fb_helper *helper,
			   const struct drm_fb_helper_funcs *funcs)
{
	INIT_LIST_HEAD(&helper->kernel_fb_list);
	spin_lock_init(&helper->damage_lock);
	INIT_WORK(&helper->resume_work, drm_fb_helper_resume_worker);
	INIT_WORK(&helper->damage_work, drm_fb_helper_damage_work);
	helper->damage_clip.x1 = helper->damage_clip.y1 = ~0;
	mutex_init(&helper->lock);
	helper->funcs = funcs;
	helper->dev = dev;
}
EXPORT_SYMBOL(drm_fb_helper_prepare);

/**
 * drm_fb_helper_init - initialize a &struct drm_fb_helper
 * @dev: drm device
 * @fb_helper: driver-allocated fbdev helper structure to initialize
 *
 * This allocates the structures for the fbdev helper with the given limits.
 * Note that this won't yet touch the hardware (through the driver interfaces)
 * nor register the fbdev. This is only done in drm_fb_helper_initial_config()
 * to allow driver writes more control over the exact init sequence.
 *
 * Drivers must call drm_fb_helper_prepare() before calling this function.
 *
 * RETURNS:
 * Zero if everything went ok, nonzero otherwise.
 */
int drm_fb_helper_init(struct drm_device *dev,
		       struct drm_fb_helper *fb_helper)
{
	int ret;

	if (!drm_fbdev_emulation) {
		dev->fb_helper = fb_helper;
		return 0;
	}

	/*
	 * If this is not the generic fbdev client, initialize a drm_client
	 * without callbacks so we can use the modesets.
	 */
	if (!fb_helper->client.funcs) {
		ret = drm_client_init(dev, &fb_helper->client, "drm_fb_helper", NULL);
		if (ret)
			return ret;
	}

	dev->fb_helper = fb_helper;

	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_init);

/**
 * drm_fb_helper_alloc_fbi - allocate fb_info and some of its members
 * @fb_helper: driver-allocated fbdev helper
 *
 * A helper to alloc fb_info and the members cmap and apertures. Called
 * by the driver within the fb_probe fb_helper callback function. Drivers do not
 * need to release the allocated fb_info structure themselves, this is
 * automatically done when calling drm_fb_helper_fini().
 *
 * RETURNS:
 * fb_info pointer if things went okay, pointer containing error code
 * otherwise
 */
struct fb_info *drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper)
{
	struct device *dev = fb_helper->dev->dev;
	struct fb_info *info;
	int ret;

	info = framebuffer_alloc(0, dev);
	if (!info)
		return ERR_PTR(-ENOMEM);

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret)
		goto err_release;

	/*
	 * TODO: We really should be smarter here and alloc an aperture
	 * for each IORESOURCE_MEM resource helper->dev->dev has and also
	 * init the ranges of the appertures based on the resources.
	 * Note some drivers currently count on there being only 1 empty
	 * aperture and fill this themselves, these will need to be dealt
	 * with somehow when fixing this.
	 */
	info->apertures = alloc_apertures(1);
	if (!info->apertures) {
		ret = -ENOMEM;
		goto err_free_cmap;
	}

	fb_helper->fbdev = info;
	info->skip_vt_switch = true;

	return info;

err_free_cmap:
	fb_dealloc_cmap(&info->cmap);
err_release:
	framebuffer_release(info);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(drm_fb_helper_alloc_fbi);

/**
 * drm_fb_helper_unregister_fbi - unregister fb_info framebuffer device
 * @fb_helper: driver-allocated fbdev helper, can be NULL
 *
 * A wrapper around unregister_framebuffer, to release the fb_info
 * framebuffer device. This must be called before releasing all resources for
 * @fb_helper by calling drm_fb_helper_fini().
 */
void drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper)
{
	if (fb_helper && fb_helper->fbdev)
		unregister_framebuffer(fb_helper->fbdev);
}
EXPORT_SYMBOL(drm_fb_helper_unregister_fbi);

/**
 * drm_fb_helper_fini - finialize a &struct drm_fb_helper
 * @fb_helper: driver-allocated fbdev helper, can be NULL
 *
 * This cleans up all remaining resources associated with @fb_helper.
 */
void drm_fb_helper_fini(struct drm_fb_helper *fb_helper)
{
	struct fb_info *info;

	if (!fb_helper)
		return;

	fb_helper->dev->fb_helper = NULL;

	if (!drm_fbdev_emulation)
		return;

	cancel_work_sync(&fb_helper->resume_work);
	cancel_work_sync(&fb_helper->damage_work);

	info = fb_helper->fbdev;
	if (info) {
		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}
	fb_helper->fbdev = NULL;

	mutex_lock(&kernel_fb_helper_lock);
	if (!list_empty(&fb_helper->kernel_fb_list)) {
		list_del(&fb_helper->kernel_fb_list);
		if (list_empty(&kernel_fb_helper_list))
			unregister_sysrq_key('v', &sysrq_drm_fb_helper_restore_op);
	}
	mutex_unlock(&kernel_fb_helper_lock);

	mutex_destroy(&fb_helper->lock);

	if (!fb_helper->client.funcs)
		drm_client_release(&fb_helper->client);
}
EXPORT_SYMBOL(drm_fb_helper_fini);

static bool drm_fbdev_use_shadow_fb(struct drm_fb_helper *fb_helper)
{
	struct drm_device *dev = fb_helper->dev;
	struct drm_framebuffer *fb = fb_helper->fb;

	return dev->mode_config.prefer_shadow_fbdev ||
	       dev->mode_config.prefer_shadow ||
	       fb->funcs->dirty;
}

static void drm_fb_helper_damage(struct fb_info *info, u32 x, u32 y,
				 u32 width, u32 height)
{
	struct drm_fb_helper *helper = info->par;
	struct drm_clip_rect *clip = &helper->damage_clip;
	unsigned long flags;

	if (!drm_fbdev_use_shadow_fb(helper))
		return;

	spin_lock_irqsave(&helper->damage_lock, flags);
	clip->x1 = min_t(u32, clip->x1, x);
	clip->y1 = min_t(u32, clip->y1, y);
	clip->x2 = max_t(u32, clip->x2, x + width);
	clip->y2 = max_t(u32, clip->y2, y + height);
	spin_unlock_irqrestore(&helper->damage_lock, flags);

	schedule_work(&helper->damage_work);
}

/* Convert memory region into area of scanlines and pixels per scanline */
static void drm_fb_helper_memory_range_to_clip(struct fb_info *info, off_t off, size_t len,
					       struct drm_rect *clip)
{
	off_t end = off + len;
	u32 x1 = 0;
	u32 y1 = off / info->fix.line_length;
	u32 x2 = info->var.xres;
	u32 y2 = DIV_ROUND_UP(end, info->fix.line_length);

	if ((y2 - y1) == 1) {
		/*
		 * We've only written to a single scanline. Try to reduce
		 * the number of horizontal pixels that need an update.
		 */
		off_t bit_off = (off % info->fix.line_length) * 8;
		off_t bit_end = (end % info->fix.line_length) * 8;

		x1 = bit_off / info->var.bits_per_pixel;
		x2 = DIV_ROUND_UP(bit_end, info->var.bits_per_pixel);
	}

	drm_rect_init(clip, x1, y1, x2 - x1, y2 - y1);
}

/**
 * drm_fb_helper_deferred_io() - fbdev deferred_io callback function
 * @info: fb_info struct pointer
 * @pagelist: list of mmap framebuffer pages that have to be flushed
 *
 * This function is used as the &fb_deferred_io.deferred_io
 * callback function for flushing the fbdev mmap writes.
 */
void drm_fb_helper_deferred_io(struct fb_info *info,
			       struct list_head *pagelist)
{
	unsigned long start, end, min, max;
	struct page *page;
	struct drm_rect damage_area;

	min = ULONG_MAX;
	max = 0;
	list_for_each_entry(page, pagelist, lru) {
		start = page->index << PAGE_SHIFT;
		end = start + PAGE_SIZE;
		min = min(min, start);
		max = max(max, end);
	}
	if (min >= max)
		return;

	drm_fb_helper_memory_range_to_clip(info, min, max - min, &damage_area);
	drm_fb_helper_damage(info, damage_area.x1, damage_area.y1,
			     drm_rect_width(&damage_area),
			     drm_rect_height(&damage_area));
}
EXPORT_SYMBOL(drm_fb_helper_deferred_io);

/**
 * drm_fb_helper_sys_read - wrapper around fb_sys_read
 * @info: fb_info struct pointer
 * @buf: userspace buffer to read from framebuffer memory
 * @count: number of bytes to read from framebuffer memory
 * @ppos: read offset within framebuffer memory
 *
 * A wrapper around fb_sys_read implemented by fbdev core
 */
ssize_t drm_fb_helper_sys_read(struct fb_info *info, char __user *buf,
			       size_t count, loff_t *ppos)
{
	return fb_sys_read(info, buf, count, ppos);
}
EXPORT_SYMBOL(drm_fb_helper_sys_read);

/**
 * drm_fb_helper_sys_write - wrapper around fb_sys_write
 * @info: fb_info struct pointer
 * @buf: userspace buffer to write to framebuffer memory
 * @count: number of bytes to write to framebuffer memory
 * @ppos: write offset within framebuffer memory
 *
 * A wrapper around fb_sys_write implemented by fbdev core
 */
ssize_t drm_fb_helper_sys_write(struct fb_info *info, const char __user *buf,
				size_t count, loff_t *ppos)
{
	loff_t pos = *ppos;
	ssize_t ret;
	struct drm_rect damage_area;

	ret = fb_sys_write(info, buf, count, ppos);
	if (ret <= 0)
		return ret;

	drm_fb_helper_memory_range_to_clip(info, pos, ret, &damage_area);
	drm_fb_helper_damage(info, damage_area.x1, damage_area.y1,
			     drm_rect_width(&damage_area),
			     drm_rect_height(&damage_area));

	return ret;
}
EXPORT_SYMBOL(drm_fb_helper_sys_write);

/**
 * drm_fb_helper_sys_fillrect - wrapper around sys_fillrect
 * @info: fbdev registered by the helper
 * @rect: info about rectangle to fill
 *
 * A wrapper around sys_fillrect implemented by fbdev core
 */
void drm_fb_helper_sys_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect)
{
	sys_fillrect(info, rect);
	drm_fb_helper_damage(info, rect->dx, rect->dy, rect->width, rect->height);
}
EXPORT_SYMBOL(drm_fb_helper_sys_fillrect);

/**
 * drm_fb_helper_sys_copyarea - wrapper around sys_copyarea
 * @info: fbdev registered by the helper
 * @area: info about area to copy
 *
 * A wrapper around sys_copyarea implemented by fbdev core
 */
void drm_fb_helper_sys_copyarea(struct fb_info *info,
				const struct fb_copyarea *area)
{
	sys_copyarea(info, area);
	drm_fb_helper_damage(info, area->dx, area->dy, area->width, area->height);
}
EXPORT_SYMBOL(drm_fb_helper_sys_copyarea);

/**
 * drm_fb_helper_sys_imageblit - wrapper around sys_imageblit
 * @info: fbdev registered by the helper
 * @image: info about image to blit
 *
 * A wrapper around sys_imageblit implemented by fbdev core
 */
void drm_fb_helper_sys_imageblit(struct fb_info *info,
				 const struct fb_image *image)
{
	sys_imageblit(info, image);
	drm_fb_helper_damage(info, image->dx, image->dy, image->width, image->height);
}
EXPORT_SYMBOL(drm_fb_helper_sys_imageblit);

/**
 * drm_fb_helper_cfb_fillrect - wrapper around cfb_fillrect
 * @info: fbdev registered by the helper
 * @rect: info about rectangle to fill
 *
 * A wrapper around cfb_fillrect implemented by fbdev core
 */
void drm_fb_helper_cfb_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect)
{
	cfb_fillrect(info, rect);
	drm_fb_helper_damage(info, rect->dx, rect->dy, rect->width, rect->height);
}
EXPORT_SYMBOL(drm_fb_helper_cfb_fillrect);

/**
 * drm_fb_helper_cfb_copyarea - wrapper around cfb_copyarea
 * @info: fbdev registered by the helper
 * @area: info about area to copy
 *
 * A wrapper around cfb_copyarea implemented by fbdev core
 */
void drm_fb_helper_cfb_copyarea(struct fb_info *info,
				const struct fb_copyarea *area)
{
	cfb_copyarea(info, area);
	drm_fb_helper_damage(info, area->dx, area->dy, area->width, area->height);
}
EXPORT_SYMBOL(drm_fb_helper_cfb_copyarea);

/**
 * drm_fb_helper_cfb_imageblit - wrapper around cfb_imageblit
 * @info: fbdev registered by the helper
 * @image: info about image to blit
 *
 * A wrapper around cfb_imageblit implemented by fbdev core
 */
void drm_fb_helper_cfb_imageblit(struct fb_info *info,
				 const struct fb_image *image)
{
	cfb_imageblit(info, image);
	drm_fb_helper_damage(info, image->dx, image->dy, image->width, image->height);
}
EXPORT_SYMBOL(drm_fb_helper_cfb_imageblit);

/**
 * drm_fb_helper_set_suspend - wrapper around fb_set_suspend
 * @fb_helper: driver-allocated fbdev helper, can be NULL
 * @suspend: whether to suspend or resume
 *
 * A wrapper around fb_set_suspend implemented by fbdev core.
 * Use drm_fb_helper_set_suspend_unlocked() if you don't need to take
 * the lock yourself
 */
void drm_fb_helper_set_suspend(struct drm_fb_helper *fb_helper, bool suspend)
{
	if (fb_helper && fb_helper->fbdev)
		fb_set_suspend(fb_helper->fbdev, suspend);
}
EXPORT_SYMBOL(drm_fb_helper_set_suspend);

/**
 * drm_fb_helper_set_suspend_unlocked - wrapper around fb_set_suspend that also
 *                                      takes the console lock
 * @fb_helper: driver-allocated fbdev helper, can be NULL
 * @suspend: whether to suspend or resume
 *
 * A wrapper around fb_set_suspend() that takes the console lock. If the lock
 * isn't available on resume, a worker is tasked with waiting for the lock
 * to become available. The console lock can be pretty contented on resume
 * due to all the printk activity.
 *
 * This function can be called multiple times with the same state since
 * &fb_info.state is checked to see if fbdev is running or not before locking.
 *
 * Use drm_fb_helper_set_suspend() if you need to take the lock yourself.
 */
void drm_fb_helper_set_suspend_unlocked(struct drm_fb_helper *fb_helper,
					bool suspend)
{
	if (!fb_helper || !fb_helper->fbdev)
		return;

	/* make sure there's no pending/ongoing resume */
	flush_work(&fb_helper->resume_work);

	if (suspend) {
		if (fb_helper->fbdev->state != FBINFO_STATE_RUNNING)
			return;

		console_lock();

	} else {
		if (fb_helper->fbdev->state == FBINFO_STATE_RUNNING)
			return;

		if (!console_trylock()) {
			schedule_work(&fb_helper->resume_work);
			return;
		}
	}

	fb_set_suspend(fb_helper->fbdev, suspend);
	console_unlock();
}
EXPORT_SYMBOL(drm_fb_helper_set_suspend_unlocked);

static int setcmap_pseudo_palette(struct fb_cmap *cmap, struct fb_info *info)
{
	u32 *palette = (u32 *)info->pseudo_palette;
	int i;

	if (cmap->start + cmap->len > 16)
		return -EINVAL;

	for (i = 0; i < cmap->len; ++i) {
		u16 red = cmap->red[i];
		u16 green = cmap->green[i];
		u16 blue = cmap->blue[i];
		u32 value;

		red >>= 16 - info->var.red.length;
		green >>= 16 - info->var.green.length;
		blue >>= 16 - info->var.blue.length;
		value = (red << info->var.red.offset) |
			(green << info->var.green.offset) |
			(blue << info->var.blue.offset);
		if (info->var.transp.length > 0) {
			u32 mask = (1 << info->var.transp.length) - 1;

			mask <<= info->var.transp.offset;
			value |= mask;
		}
		palette[cmap->start + i] = value;
	}

	return 0;
}

static int setcmap_legacy(struct fb_cmap *cmap, struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_mode_set *modeset;
	struct drm_crtc *crtc;
	u16 *r, *g, *b;
	int ret = 0;

	drm_modeset_lock_all(fb_helper->dev);
	drm_client_for_each_modeset(modeset, &fb_helper->client) {
		crtc = modeset->crtc;
		if (!crtc->funcs->gamma_set || !crtc->gamma_size) {
			ret = -EINVAL;
			goto out;
		}

		if (cmap->start + cmap->len > crtc->gamma_size) {
			ret = -EINVAL;
			goto out;
		}

		r = crtc->gamma_store;
		g = r + crtc->gamma_size;
		b = g + crtc->gamma_size;

		memcpy(r + cmap->start, cmap->red, cmap->len * sizeof(*r));
		memcpy(g + cmap->start, cmap->green, cmap->len * sizeof(*g));
		memcpy(b + cmap->start, cmap->blue, cmap->len * sizeof(*b));

		ret = crtc->funcs->gamma_set(crtc, r, g, b,
					     crtc->gamma_size, NULL);
		if (ret)
			goto out;
	}
out:
	drm_modeset_unlock_all(fb_helper->dev);

	return ret;
}

static struct drm_property_blob *setcmap_new_gamma_lut(struct drm_crtc *crtc,
						       struct fb_cmap *cmap)
{
	struct drm_device *dev = crtc->dev;
	struct drm_property_blob *gamma_lut;
	struct drm_color_lut *lut;
	int size = crtc->gamma_size;
	int i;

	if (!size || cmap->start + cmap->len > size)
		return ERR_PTR(-EINVAL);

	gamma_lut = drm_property_create_blob(dev, sizeof(*lut) * size, NULL);
	if (IS_ERR(gamma_lut))
		return gamma_lut;

	lut = gamma_lut->data;
	if (cmap->start || cmap->len != size) {
		u16 *r = crtc->gamma_store;
		u16 *g = r + crtc->gamma_size;
		u16 *b = g + crtc->gamma_size;

		for (i = 0; i < cmap->start; i++) {
			lut[i].red = r[i];
			lut[i].green = g[i];
			lut[i].blue = b[i];
		}
		for (i = cmap->start + cmap->len; i < size; i++) {
			lut[i].red = r[i];
			lut[i].green = g[i];
			lut[i].blue = b[i];
		}
	}

	for (i = 0; i < cmap->len; i++) {
		lut[cmap->start + i].red = cmap->red[i];
		lut[cmap->start + i].green = cmap->green[i];
		lut[cmap->start + i].blue = cmap->blue[i];
	}

	return gamma_lut;
}

static int setcmap_atomic(struct fb_cmap *cmap, struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_device *dev = fb_helper->dev;
	struct drm_property_blob *gamma_lut = NULL;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_crtc_state *crtc_state;
	struct drm_atomic_state *state;
	struct drm_mode_set *modeset;
	struct drm_crtc *crtc;
	u16 *r, *g, *b;
	bool replaced;
	int ret = 0;

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_atomic_state_alloc(dev);
	if (!state) {
		ret = -ENOMEM;
		goto out_ctx;
	}

	state->acquire_ctx = &ctx;
retry:
	drm_client_for_each_modeset(modeset, &fb_helper->client) {
		crtc = modeset->crtc;

		if (!gamma_lut)
			gamma_lut = setcmap_new_gamma_lut(crtc, cmap);
		if (IS_ERR(gamma_lut)) {
			ret = PTR_ERR(gamma_lut);
			gamma_lut = NULL;
			goto out_state;
		}

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state)) {
			ret = PTR_ERR(crtc_state);
			goto out_state;
		}

		/*
		 * FIXME: This always uses gamma_lut. Some HW have only
		 * degamma_lut, in which case we should reset gamma_lut and set
		 * degamma_lut. See drm_crtc_legacy_gamma_set().
		 */
		replaced  = drm_property_replace_blob(&crtc_state->degamma_lut,
						      NULL);
		replaced |= drm_property_replace_blob(&crtc_state->ctm, NULL);
		replaced |= drm_property_replace_blob(&crtc_state->gamma_lut,
						      gamma_lut);
		crtc_state->color_mgmt_changed |= replaced;
	}

	ret = drm_atomic_commit(state);
	if (ret)
		goto out_state;

	drm_client_for_each_modeset(modeset, &fb_helper->client) {
		crtc = modeset->crtc;

		r = crtc->gamma_store;
		g = r + crtc->gamma_size;
		b = g + crtc->gamma_size;

		memcpy(r + cmap->start, cmap->red, cmap->len * sizeof(*r));
		memcpy(g + cmap->start, cmap->green, cmap->len * sizeof(*g));
		memcpy(b + cmap->start, cmap->blue, cmap->len * sizeof(*b));
	}

out_state:
	if (ret == -EDEADLK)
		goto backoff;

	drm_property_blob_put(gamma_lut);
	drm_atomic_state_put(state);
out_ctx:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;

backoff:
	drm_atomic_state_clear(state);
	drm_modeset_backoff(&ctx);
	goto retry;
}

/**
 * drm_fb_helper_setcmap - implementation for &fb_ops.fb_setcmap
 * @cmap: cmap to set
 * @info: fbdev registered by the helper
 */
int drm_fb_helper_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_device *dev = fb_helper->dev;
	int ret;

	if (oops_in_progress)
		return -EBUSY;

	mutex_lock(&fb_helper->lock);

	if (!drm_master_internal_acquire(dev)) {
		ret = -EBUSY;
		goto unlock;
	}

	mutex_lock(&fb_helper->client.modeset_mutex);
	if (info->fix.visual == FB_VISUAL_TRUECOLOR)
		ret = setcmap_pseudo_palette(cmap, info);
	else if (drm_drv_uses_atomic_modeset(fb_helper->dev))
		ret = setcmap_atomic(cmap, info);
	else
		ret = setcmap_legacy(cmap, info);
	mutex_unlock(&fb_helper->client.modeset_mutex);

	drm_master_internal_release(dev);
unlock:
	mutex_unlock(&fb_helper->lock);

	return ret;
}
EXPORT_SYMBOL(drm_fb_helper_setcmap);

/**
 * drm_fb_helper_ioctl - legacy ioctl implementation
 * @info: fbdev registered by the helper
 * @cmd: ioctl command
 * @arg: ioctl argument
 *
 * A helper to implement the standard fbdev ioctl. Only
 * FBIO_WAITFORVSYNC is implemented for now.
 */
int drm_fb_helper_ioctl(struct fb_info *info, unsigned int cmd,
			unsigned long arg)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_device *dev = fb_helper->dev;
	struct drm_crtc *crtc;
	int ret = 0;

	mutex_lock(&fb_helper->lock);
	if (!drm_master_internal_acquire(dev)) {
		ret = -EBUSY;
		goto unlock;
	}

	switch (cmd) {
	case FBIO_WAITFORVSYNC:
		/*
		 * Only consider the first CRTC.
		 *
		 * This ioctl is supposed to take the CRTC number as
		 * an argument, but in fbdev times, what that number
		 * was supposed to be was quite unclear, different
		 * drivers were passing that argument differently
		 * (some by reference, some by value), and most of the
		 * userspace applications were just hardcoding 0 as an
		 * argument.
		 *
		 * The first CRTC should be the integrated panel on
		 * most drivers, so this is the best choice we can
		 * make. If we're not smart enough here, one should
		 * just consider switch the userspace to KMS.
		 */
		crtc = fb_helper->client.modesets[0].crtc;

		/*
		 * Only wait for a vblank event if the CRTC is
		 * enabled, otherwise just don't do anythintg,
		 * not even report an error.
		 */
		ret = drm_crtc_vblank_get(crtc);
		if (!ret) {
			drm_crtc_wait_one_vblank(crtc);
			drm_crtc_vblank_put(crtc);
		}

		ret = 0;
		break;
	default:
		ret = -ENOTTY;
	}

	drm_master_internal_release(dev);
unlock:
	mutex_unlock(&fb_helper->lock);
	return ret;
}
EXPORT_SYMBOL(drm_fb_helper_ioctl);

static bool drm_fb_pixel_format_equal(const struct fb_var_screeninfo *var_1,
				      const struct fb_var_screeninfo *var_2)
{
	return var_1->bits_per_pixel == var_2->bits_per_pixel &&
	       var_1->grayscale == var_2->grayscale &&
	       var_1->red.offset == var_2->red.offset &&
	       var_1->red.length == var_2->red.length &&
	       var_1->red.msb_right == var_2->red.msb_right &&
	       var_1->green.offset == var_2->green.offset &&
	       var_1->green.length == var_2->green.length &&
	       var_1->green.msb_right == var_2->green.msb_right &&
	       var_1->blue.offset == var_2->blue.offset &&
	       var_1->blue.length == var_2->blue.length &&
	       var_1->blue.msb_right == var_2->blue.msb_right &&
	       var_1->transp.offset == var_2->transp.offset &&
	       var_1->transp.length == var_2->transp.length &&
	       var_1->transp.msb_right == var_2->transp.msb_right;
}

static void drm_fb_helper_fill_pixel_fmt(struct fb_var_screeninfo *var,
					 u8 depth)
{
	switch (depth) {
	case 8:
		var->red.offset = 0;
		var->green.offset = 0;
		var->blue.offset = 0;
		var->red.length = 8; /* 8bit DAC */
		var->green.length = 8;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 15:
		var->red.offset = 10;
		var->green.offset = 5;
		var->blue.offset = 0;
		var->red.length = 5;
		var->green.length = 5;
		var->blue.length = 5;
		var->transp.offset = 15;
		var->transp.length = 1;
		break;
	case 16:
		var->red.offset = 11;
		var->green.offset = 5;
		var->blue.offset = 0;
		var->red.length = 5;
		var->green.length = 6;
		var->blue.length = 5;
		var->transp.offset = 0;
		break;
	case 24:
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 32:
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		break;
	default:
		break;
	}
}

/**
 * drm_fb_helper_check_var - implementation for &fb_ops.fb_check_var
 * @var: screeninfo to check
 * @info: fbdev registered by the helper
 */
int drm_fb_helper_check_var(struct fb_var_screeninfo *var,
			    struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_framebuffer *fb = fb_helper->fb;
	struct drm_device *dev = fb_helper->dev;

	if (in_dbg_master())
		return -EINVAL;

	if (var->pixclock != 0) {
		drm_dbg_kms(dev, "fbdev emulation doesn't support changing the pixel clock, value of pixclock is ignored\n");
		var->pixclock = 0;
	}

	if ((drm_format_info_block_width(fb->format, 0) > 1) ||
	    (drm_format_info_block_height(fb->format, 0) > 1))
		return -EINVAL;

	/*
	 * Changes struct fb_var_screeninfo are currently not pushed back
	 * to KMS, hence fail if different settings are requested.
	 */
	if (var->bits_per_pixel > fb->format->cpp[0] * 8 ||
	    var->xres > fb->width || var->yres > fb->height ||
	    var->xres_virtual > fb->width || var->yres_virtual > fb->height) {
		drm_dbg_kms(dev, "fb requested width/height/bpp can't fit in current fb "
			  "request %dx%d-%d (virtual %dx%d) > %dx%d-%d\n",
			  var->xres, var->yres, var->bits_per_pixel,
			  var->xres_virtual, var->yres_virtual,
			  fb->width, fb->height, fb->format->cpp[0] * 8);
		return -EINVAL;
	}

	/*
	 * Workaround for SDL 1.2, which is known to be setting all pixel format
	 * fields values to zero in some cases. We treat this situation as a
	 * kind of "use some reasonable autodetected values".
	 */
	if (!var->red.offset     && !var->green.offset    &&
	    !var->blue.offset    && !var->transp.offset   &&
	    !var->red.length     && !var->green.length    &&
	    !var->blue.length    && !var->transp.length   &&
	    !var->red.msb_right  && !var->green.msb_right &&
	    !var->blue.msb_right && !var->transp.msb_right) {
		drm_fb_helper_fill_pixel_fmt(var, fb->format->depth);
	}

	/*
	 * Likewise, bits_per_pixel should be rounded up to a supported value.
	 */
	var->bits_per_pixel = fb->format->cpp[0] * 8;

	/*
	 * drm fbdev emulation doesn't support changing the pixel format at all,
	 * so reject all pixel format changing requests.
	 */
	if (!drm_fb_pixel_format_equal(var, &info->var)) {
		drm_dbg_kms(dev, "fbdev emulation doesn't support changing the pixel format\n");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_check_var);

/**
 * drm_fb_helper_set_par - implementation for &fb_ops.fb_set_par
 * @info: fbdev registered by the helper
 *
 * This will let fbcon do the mode init and is called at initialization time by
 * the fbdev core when registering the driver, and later on through the hotplug
 * callback.
 */
int drm_fb_helper_set_par(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct fb_var_screeninfo *var = &info->var;
	bool force;

	if (oops_in_progress)
		return -EBUSY;

	if (var->pixclock != 0) {
		drm_err(fb_helper->dev, "PIXEL CLOCK SET\n");
		return -EINVAL;
	}

	/*
	 * Normally we want to make sure that a kms master takes precedence over
	 * fbdev, to avoid fbdev flickering and occasionally stealing the
	 * display status. But Xorg first sets the vt back to text mode using
	 * the KDSET IOCTL with KD_TEXT, and only after that drops the master
	 * status when exiting.
	 *
	 * In the past this was caught by drm_fb_helper_lastclose(), but on
	 * modern systems where logind always keeps a drm fd open to orchestrate
	 * the vt switching, this doesn't work.
	 *
	 * To not break the userspace ABI we have this special case here, which
	 * is only used for the above case. Everything else uses the normal
	 * commit function, which ensures that we never steal the display from
	 * an active drm master.
	 */
	force = var->activate & FB_ACTIVATE_KD_TEXT;

	__drm_fb_helper_restore_fbdev_mode_unlocked(fb_helper, force);

	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_set_par);

static void pan_set(struct drm_fb_helper *fb_helper, int x, int y)
{
	struct drm_mode_set *mode_set;

	mutex_lock(&fb_helper->client.modeset_mutex);
	drm_client_for_each_modeset(mode_set, &fb_helper->client) {
		mode_set->x = x;
		mode_set->y = y;
	}
	mutex_unlock(&fb_helper->client.modeset_mutex);
}

static int pan_display_atomic(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	int ret;

	pan_set(fb_helper, var->xoffset, var->yoffset);

	ret = drm_client_modeset_commit_locked(&fb_helper->client);
	if (!ret) {
		info->var.xoffset = var->xoffset;
		info->var.yoffset = var->yoffset;
	} else
		pan_set(fb_helper, info->var.xoffset, info->var.yoffset);

	return ret;
}

static int pan_display_legacy(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_client_dev *client = &fb_helper->client;
	struct drm_mode_set *modeset;
	int ret = 0;

	mutex_lock(&client->modeset_mutex);
	drm_modeset_lock_all(fb_helper->dev);
	drm_client_for_each_modeset(modeset, client) {
		modeset->x = var->xoffset;
		modeset->y = var->yoffset;

		if (modeset->num_connectors) {
			ret = drm_mode_set_config_internal(modeset);
			if (!ret) {
				info->var.xoffset = var->xoffset;
				info->var.yoffset = var->yoffset;
			}
		}
	}
	drm_modeset_unlock_all(fb_helper->dev);
	mutex_unlock(&client->modeset_mutex);

	return ret;
}

/**
 * drm_fb_helper_pan_display - implementation for &fb_ops.fb_pan_display
 * @var: updated screen information
 * @info: fbdev registered by the helper
 */
int drm_fb_helper_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_device *dev = fb_helper->dev;
	int ret;

	if (oops_in_progress)
		return -EBUSY;

	mutex_lock(&fb_helper->lock);
	if (!drm_master_internal_acquire(dev)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (drm_drv_uses_atomic_modeset(dev))
		ret = pan_display_atomic(var, info);
	else
		ret = pan_display_legacy(var, info);

	drm_master_internal_release(dev);
unlock:
	mutex_unlock(&fb_helper->lock);

	return ret;
}
EXPORT_SYMBOL(drm_fb_helper_pan_display);

/*
 * Allocates the backing storage and sets up the fbdev info structure through
 * the ->fb_probe callback.
 */
static int drm_fb_helper_single_fb_probe(struct drm_fb_helper *fb_helper,
					 int preferred_bpp)
{
	struct drm_client_dev *client = &fb_helper->client;
	struct drm_device *dev = fb_helper->dev;
	struct drm_mode_config *config = &dev->mode_config;
	int ret = 0;
	int crtc_count = 0;
	struct drm_connector_list_iter conn_iter;
	struct drm_fb_helper_surface_size sizes;
	struct drm_connector *connector;
	struct drm_mode_set *mode_set;
	int best_depth = 0;

	memset(&sizes, 0, sizeof(struct drm_fb_helper_surface_size));
	sizes.surface_depth = 24;
	sizes.surface_bpp = 32;
	sizes.fb_width = (u32)-1;
	sizes.fb_height = (u32)-1;

	/*
	 * If driver picks 8 or 16 by default use that for both depth/bpp
	 * to begin with
	 */
	if (preferred_bpp != sizes.surface_bpp)
		sizes.surface_depth = sizes.surface_bpp = preferred_bpp;

	drm_connector_list_iter_begin(fb_helper->dev, &conn_iter);
	drm_client_for_each_connector_iter(connector, &conn_iter) {
		struct drm_cmdline_mode *cmdline_mode;

		cmdline_mode = &connector->cmdline_mode;

		if (cmdline_mode->bpp_specified) {
			switch (cmdline_mode->bpp) {
			case 8:
				sizes.surface_depth = sizes.surface_bpp = 8;
				break;
			case 15:
				sizes.surface_depth = 15;
				sizes.surface_bpp = 16;
				break;
			case 16:
				sizes.surface_depth = sizes.surface_bpp = 16;
				break;
			case 24:
				sizes.surface_depth = sizes.surface_bpp = 24;
				break;
			case 32:
				sizes.surface_depth = 24;
				sizes.surface_bpp = 32;
				break;
			}
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	/*
	 * If we run into a situation where, for example, the primary plane
	 * supports RGBA5551 (16 bpp, depth 15) but not RGB565 (16 bpp, depth
	 * 16) we need to scale down the depth of the sizes we request.
	 */
	mutex_lock(&client->modeset_mutex);
	drm_client_for_each_modeset(mode_set, client) {
		struct drm_crtc *crtc = mode_set->crtc;
		struct drm_plane *plane = crtc->primary;
		int j;

		drm_dbg_kms(dev, "test CRTC %u primary plane\n", drm_crtc_index(crtc));

		for (j = 0; j < plane->format_count; j++) {
			const struct drm_format_info *fmt;

			fmt = drm_format_info(plane->format_types[j]);

			/*
			 * Do not consider YUV or other complicated formats
			 * for framebuffers. This means only legacy formats
			 * are supported (fmt->depth is a legacy field) but
			 * the framebuffer emulation can only deal with such
			 * formats, specifically RGB/BGA formats.
			 */
			if (fmt->depth == 0)
				continue;

			/* We found a perfect fit, great */
			if (fmt->depth == sizes.surface_depth) {
				best_depth = fmt->depth;
				break;
			}

			/* Skip depths above what we're looking for */
			if (fmt->depth > sizes.surface_depth)
				continue;

			/* Best depth found so far */
			if (fmt->depth > best_depth)
				best_depth = fmt->depth;
		}
	}
	if (sizes.surface_depth != best_depth && best_depth) {
		drm_info(dev, "requested bpp %d, scaled depth down to %d",
			 sizes.surface_bpp, best_depth);
		sizes.surface_depth = best_depth;
	}

	/* first up get a count of crtcs now in use and new min/maxes width/heights */
	crtc_count = 0;
	drm_client_for_each_modeset(mode_set, client) {
		struct drm_display_mode *desired_mode;
		int x, y, j;
		/* in case of tile group, are we the last tile vert or horiz?
		 * If no tile group you are always the last one both vertically
		 * and horizontally
		 */
		bool lastv = true, lasth = true;

		desired_mode = mode_set->mode;

		if (!desired_mode)
			continue;

		crtc_count++;

		x = mode_set->x;
		y = mode_set->y;

		sizes.surface_width  = max_t(u32, desired_mode->hdisplay + x, sizes.surface_width);
		sizes.surface_height = max_t(u32, desired_mode->vdisplay + y, sizes.surface_height);

		for (j = 0; j < mode_set->num_connectors; j++) {
			struct drm_connector *connector = mode_set->connectors[j];

			if (connector->has_tile &&
			    desired_mode->hdisplay == connector->tile_h_size &&
			    desired_mode->vdisplay == connector->tile_v_size) {
				lasth = (connector->tile_h_loc == (connector->num_h_tile - 1));
				lastv = (connector->tile_v_loc == (connector->num_v_tile - 1));
				/* cloning to multiple tiles is just crazy-talk, so: */
				break;
			}
		}

		if (lasth)
			sizes.fb_width  = min_t(u32, desired_mode->hdisplay + x, sizes.fb_width);
		if (lastv)
			sizes.fb_height = min_t(u32, desired_mode->vdisplay + y, sizes.fb_height);
	}
	mutex_unlock(&client->modeset_mutex);

	if (crtc_count == 0 || sizes.fb_width == -1 || sizes.fb_height == -1) {
		drm_info(dev, "Cannot find any crtc or sizes\n");

		/* First time: disable all crtc's.. */
		if (!fb_helper->deferred_setup)
			drm_client_modeset_commit(client);
		return -EAGAIN;
	}

	/* Handle our overallocation */
	sizes.surface_height *= drm_fbdev_overalloc;
	sizes.surface_height /= 100;
	if (sizes.surface_height > config->max_height) {
		drm_dbg_kms(dev, "Fbdev over-allocation too large; clamping height to %d\n",
			    config->max_height);
		sizes.surface_height = config->max_height;
	}

	/* push down into drivers */
	ret = (*fb_helper->funcs->fb_probe)(fb_helper, &sizes);
	if (ret < 0)
		return ret;

	strcpy(fb_helper->fb->comm, "[fbcon]");
	return 0;
}

static void drm_fb_helper_fill_fix(struct fb_info *info, uint32_t pitch,
				   uint32_t depth)
{
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = depth == 8 ? FB_VISUAL_PSEUDOCOLOR :
		FB_VISUAL_TRUECOLOR;
	info->fix.mmio_start = 0;
	info->fix.mmio_len = 0;
	info->fix.type_aux = 0;
	info->fix.xpanstep = 1; /* doing it in hw */
	info->fix.ypanstep = 1; /* doing it in hw */
	info->fix.ywrapstep = 0;
	info->fix.accel = FB_ACCEL_NONE;

	info->fix.line_length = pitch;
}

static void drm_fb_helper_fill_var(struct fb_info *info,
				   struct drm_fb_helper *fb_helper,
				   uint32_t fb_width, uint32_t fb_height)
{
	struct drm_framebuffer *fb = fb_helper->fb;

	WARN_ON((drm_format_info_block_width(fb->format, 0) > 1) ||
		(drm_format_info_block_height(fb->format, 0) > 1));
	info->pseudo_palette = fb_helper->pseudo_palette;
	info->var.xres_virtual = fb->width;
	info->var.yres_virtual = fb->height;
	info->var.bits_per_pixel = fb->format->cpp[0] * 8;
	info->var.accel_flags = FB_ACCELF_TEXT;
	info->var.xoffset = 0;
	info->var.yoffset = 0;
	info->var.activate = FB_ACTIVATE_NOW;

	drm_fb_helper_fill_pixel_fmt(&info->var, fb->format->depth);

	info->var.xres = fb_width;
	info->var.yres = fb_height;
}

/**
 * drm_fb_helper_fill_info - initializes fbdev information
 * @info: fbdev instance to set up
 * @fb_helper: fb helper instance to use as template
 * @sizes: describes fbdev size and scanout surface size
 *
 * Sets up the variable and fixed fbdev metainformation from the given fb helper
 * instance and the drm framebuffer allocated in &drm_fb_helper.fb.
 *
 * Drivers should call this (or their equivalent setup code) from their
 * &drm_fb_helper_funcs.fb_probe callback after having allocated the fbdev
 * backing storage framebuffer.
 */
void drm_fb_helper_fill_info(struct fb_info *info,
			     struct drm_fb_helper *fb_helper,
			     struct drm_fb_helper_surface_size *sizes)
{
	struct drm_framebuffer *fb = fb_helper->fb;

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->format->depth);
	drm_fb_helper_fill_var(info, fb_helper,
			       sizes->fb_width, sizes->fb_height);

	info->par = fb_helper;
	/*
	 * The DRM drivers fbdev emulation device name can be confusing if the
	 * driver name also has a "drm" suffix on it. Leading to names such as
	 * "simpledrmdrmfb" in /proc/fb. Unfortunately, it's an uAPI and can't
	 * be changed due user-space tools (e.g: pm-utils) matching against it.
	 */
	snprintf(info->fix.id, sizeof(info->fix.id), "%sdrmfb",
		 fb_helper->dev->driver->name);

}
EXPORT_SYMBOL(drm_fb_helper_fill_info);

/*
 * This is a continuation of drm_setup_crtcs() that sets up anything related
 * to the framebuffer. During initialization, drm_setup_crtcs() is called before
 * the framebuffer has been allocated (fb_helper->fb and fb_helper->fbdev).
 * So, any setup that touches those fields needs to be done here instead of in
 * drm_setup_crtcs().
 */
static void drm_setup_crtcs_fb(struct drm_fb_helper *fb_helper)
{
	struct drm_client_dev *client = &fb_helper->client;
	struct drm_connector_list_iter conn_iter;
	struct fb_info *info = fb_helper->fbdev;
	unsigned int rotation, sw_rotations = 0;
	struct drm_connector *connector;
	struct drm_mode_set *modeset;

	mutex_lock(&client->modeset_mutex);
	drm_client_for_each_modeset(modeset, client) {
		if (!modeset->num_connectors)
			continue;

		modeset->fb = fb_helper->fb;

		if (drm_client_rotation(modeset, &rotation))
			/* Rotating in hardware, fbcon should not rotate */
			sw_rotations |= DRM_MODE_ROTATE_0;
		else
			sw_rotations |= rotation;
	}
	mutex_unlock(&client->modeset_mutex);

	drm_connector_list_iter_begin(fb_helper->dev, &conn_iter);
	drm_client_for_each_connector_iter(connector, &conn_iter) {

		/* use first connected connector for the physical dimensions */
		if (connector->status == connector_status_connected) {
			info->var.width = connector->display_info.width_mm;
			info->var.height = connector->display_info.height_mm;
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	switch (sw_rotations) {
	case DRM_MODE_ROTATE_0:
		info->fbcon_rotate_hint = FB_ROTATE_UR;
		break;
	case DRM_MODE_ROTATE_90:
		info->fbcon_rotate_hint = FB_ROTATE_CCW;
		break;
	case DRM_MODE_ROTATE_180:
		info->fbcon_rotate_hint = FB_ROTATE_UD;
		break;
	case DRM_MODE_ROTATE_270:
		info->fbcon_rotate_hint = FB_ROTATE_CW;
		break;
	default:
		/*
		 * Multiple bits are set / multiple rotations requested
		 * fbcon cannot handle separate rotation settings per
		 * output, so fallback to unrotated.
		 */
		info->fbcon_rotate_hint = FB_ROTATE_UR;
	}
}

/* Note: Drops fb_helper->lock before returning. */
static int
__drm_fb_helper_initial_config_and_unlock(struct drm_fb_helper *fb_helper,
					  int bpp_sel)
{
	struct drm_device *dev = fb_helper->dev;
	struct fb_info *info;
	unsigned int width, height;
	int ret;

	width = dev->mode_config.max_width;
	height = dev->mode_config.max_height;

	drm_client_modeset_probe(&fb_helper->client, width, height);
	ret = drm_fb_helper_single_fb_probe(fb_helper, bpp_sel);
	if (ret < 0) {
		if (ret == -EAGAIN) {
			fb_helper->preferred_bpp = bpp_sel;
			fb_helper->deferred_setup = true;
			ret = 0;
		}
		mutex_unlock(&fb_helper->lock);

		return ret;
	}
	drm_setup_crtcs_fb(fb_helper);

	fb_helper->deferred_setup = false;

	info = fb_helper->fbdev;
	info->var.pixclock = 0;
	/* Shamelessly allow physical address leaking to userspace */
#if IS_ENABLED(CONFIG_DRM_FBDEV_LEAK_PHYS_SMEM)
	if (!drm_leak_fbdev_smem)
#endif
		/* don't leak any physical addresses to userspace */
		info->flags |= FBINFO_HIDE_SMEM_START;

	/* Need to drop locks to avoid recursive deadlock in
	 * register_framebuffer. This is ok because the only thing left to do is
	 * register the fbdev emulation instance in kernel_fb_helper_list. */
	mutex_unlock(&fb_helper->lock);

	ret = register_framebuffer(info);
	if (ret < 0)
		return ret;

	drm_info(dev, "fb%d: %s frame buffer device\n",
		 info->node, info->fix.id);

	mutex_lock(&kernel_fb_helper_lock);
	if (list_empty(&kernel_fb_helper_list))
		register_sysrq_key('v', &sysrq_drm_fb_helper_restore_op);

	list_add(&fb_helper->kernel_fb_list, &kernel_fb_helper_list);
	mutex_unlock(&kernel_fb_helper_lock);

	return 0;
}

/**
 * drm_fb_helper_initial_config - setup a sane initial connector configuration
 * @fb_helper: fb_helper device struct
 * @bpp_sel: bpp value to use for the framebuffer configuration
 *
 * Scans the CRTCs and connectors and tries to put together an initial setup.
 * At the moment, this is a cloned configuration across all heads with
 * a new framebuffer object as the backing store.
 *
 * Note that this also registers the fbdev and so allows userspace to call into
 * the driver through the fbdev interfaces.
 *
 * This function will call down into the &drm_fb_helper_funcs.fb_probe callback
 * to let the driver allocate and initialize the fbdev info structure and the
 * drm framebuffer used to back the fbdev. drm_fb_helper_fill_info() is provided
 * as a helper to setup simple default values for the fbdev info structure.
 *
 * HANG DEBUGGING:
 *
 * When you have fbcon support built-in or already loaded, this function will do
 * a full modeset to setup the fbdev console. Due to locking misdesign in the
 * VT/fbdev subsystem that entire modeset sequence has to be done while holding
 * console_lock. Until console_unlock is called no dmesg lines will be sent out
 * to consoles, not even serial console. This means when your driver crashes,
 * you will see absolutely nothing else but a system stuck in this function,
 * with no further output. Any kind of printk() you place within your own driver
 * or in the drm core modeset code will also never show up.
 *
 * Standard debug practice is to run the fbcon setup without taking the
 * console_lock as a hack, to be able to see backtraces and crashes on the
 * serial line. This can be done by setting the fb.lockless_register_fb=1 kernel
 * cmdline option.
 *
 * The other option is to just disable fbdev emulation since very likely the
 * first modeset from userspace will crash in the same way, and is even easier
 * to debug. This can be done by setting the drm_kms_helper.fbdev_emulation=0
 * kernel cmdline option.
 *
 * RETURNS:
 * Zero if everything went ok, nonzero otherwise.
 */
int drm_fb_helper_initial_config(struct drm_fb_helper *fb_helper, int bpp_sel)
{
	int ret;

	if (!drm_fbdev_emulation)
		return 0;

	mutex_lock(&fb_helper->lock);
	ret = __drm_fb_helper_initial_config_and_unlock(fb_helper, bpp_sel);

	return ret;
}
EXPORT_SYMBOL(drm_fb_helper_initial_config);

/**
 * drm_fb_helper_hotplug_event - respond to a hotplug notification by
 *                               probing all the outputs attached to the fb
 * @fb_helper: driver-allocated fbdev helper, can be NULL
 *
 * Scan the connectors attached to the fb_helper and try to put together a
 * setup after notification of a change in output configuration.
 *
 * Called at runtime, takes the mode config locks to be able to check/change the
 * modeset configuration. Must be run from process context (which usually means
 * either the output polling work or a work item launched from the driver's
 * hotplug interrupt).
 *
 * Note that drivers may call this even before calling
 * drm_fb_helper_initial_config but only after drm_fb_helper_init. This allows
 * for a race-free fbcon setup and will make sure that the fbdev emulation will
 * not miss any hotplug events.
 *
 * RETURNS:
 * 0 on success and a non-zero error code otherwise.
 */
int drm_fb_helper_hotplug_event(struct drm_fb_helper *fb_helper)
{
	int err = 0;

	if (!drm_fbdev_emulation || !fb_helper)
		return 0;

	mutex_lock(&fb_helper->lock);
	if (fb_helper->deferred_setup) {
		err = __drm_fb_helper_initial_config_and_unlock(fb_helper,
				fb_helper->preferred_bpp);
		return err;
	}

	if (!fb_helper->fb || !drm_master_internal_acquire(fb_helper->dev)) {
		fb_helper->delayed_hotplug = true;
		mutex_unlock(&fb_helper->lock);
		return err;
	}

	drm_master_internal_release(fb_helper->dev);

	drm_dbg_kms(fb_helper->dev, "\n");

	drm_client_modeset_probe(&fb_helper->client, fb_helper->fb->width, fb_helper->fb->height);
	drm_setup_crtcs_fb(fb_helper);
	mutex_unlock(&fb_helper->lock);

	drm_fb_helper_set_par(fb_helper->fbdev);

	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_hotplug_event);

/**
 * drm_fb_helper_lastclose - DRM driver lastclose helper for fbdev emulation
 * @dev: DRM device
 *
 * This function can be used as the &drm_driver->lastclose callback for drivers
 * that only need to call drm_fb_helper_restore_fbdev_mode_unlocked().
 */
void drm_fb_helper_lastclose(struct drm_device *dev)
{
	drm_fb_helper_restore_fbdev_mode_unlocked(dev->fb_helper);
}
EXPORT_SYMBOL(drm_fb_helper_lastclose);

/**
 * drm_fb_helper_output_poll_changed - DRM mode config \.output_poll_changed
 *                                     helper for fbdev emulation
 * @dev: DRM device
 *
 * This function can be used as the
 * &drm_mode_config_funcs.output_poll_changed callback for drivers that only
 * need to call drm_fb_helper_hotplug_event().
 */
void drm_fb_helper_output_poll_changed(struct drm_device *dev)
{
	drm_fb_helper_hotplug_event(dev->fb_helper);
}
EXPORT_SYMBOL(drm_fb_helper_output_poll_changed);

/* @user: 1=userspace, 0=fbcon */
static int drm_fbdev_fb_open(struct fb_info *info, int user)
{
	struct drm_fb_helper *fb_helper = info->par;

	/* No need to take a ref for fbcon because it unbinds on unregister */
	if (user && !try_module_get(fb_helper->dev->driver->fops->owner))
		return -ENODEV;

	return 0;
}

static int drm_fbdev_fb_release(struct fb_info *info, int user)
{
	struct drm_fb_helper *fb_helper = info->par;

	if (user)
		module_put(fb_helper->dev->driver->fops->owner);

	return 0;
}

static void drm_fbdev_cleanup(struct drm_fb_helper *fb_helper)
{
	struct fb_info *fbi = fb_helper->fbdev;
	void *shadow = NULL;

	if (!fb_helper->dev)
		return;

	if (fbi) {
		if (fbi->fbdefio)
			fb_deferred_io_cleanup(fbi);
		if (drm_fbdev_use_shadow_fb(fb_helper))
			shadow = fbi->screen_buffer;
	}

	drm_fb_helper_fini(fb_helper);

	if (shadow)
		vfree(shadow);
	else if (fb_helper->buffer)
		drm_client_buffer_vunmap(fb_helper->buffer);

	drm_client_framebuffer_delete(fb_helper->buffer);
}

static void drm_fbdev_release(struct drm_fb_helper *fb_helper)
{
	drm_fbdev_cleanup(fb_helper);
	drm_client_release(&fb_helper->client);
	kfree(fb_helper);
}

/*
 * fb_ops.fb_destroy is called by the last put_fb_info() call at the end of
 * unregister_framebuffer() or fb_release().
 */
static void drm_fbdev_fb_destroy(struct fb_info *info)
{
	drm_fbdev_release(info->par);
}

static int drm_fbdev_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct drm_fb_helper *fb_helper = info->par;

	if (fb_helper->dev->driver->gem_prime_mmap)
		return fb_helper->dev->driver->gem_prime_mmap(fb_helper->buffer->gem, vma);
	else
		return -ENODEV;
}

static bool drm_fbdev_use_iomem(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_client_buffer *buffer = fb_helper->buffer;

	return !drm_fbdev_use_shadow_fb(fb_helper) && buffer->map.is_iomem;
}

static ssize_t fb_read_screen_base(struct fb_info *info, char __user *buf, size_t count,
				   loff_t pos)
{
	const char __iomem *src = info->screen_base + pos;
	size_t alloc_size = min_t(size_t, count, PAGE_SIZE);
	ssize_t ret = 0;
	int err = 0;
	char *tmp;

	tmp = kmalloc(alloc_size, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	while (count) {
		size_t c = min_t(size_t, count, alloc_size);

		memcpy_fromio(tmp, src, c);
		if (copy_to_user(buf, tmp, c)) {
			err = -EFAULT;
			break;
		}

		src += c;
		buf += c;
		ret += c;
		count -= c;
	}

	kfree(tmp);

	return ret ? ret : err;
}

static ssize_t fb_read_screen_buffer(struct fb_info *info, char __user *buf, size_t count,
				     loff_t pos)
{
	const char *src = info->screen_buffer + pos;

	if (copy_to_user(buf, src, count))
		return -EFAULT;

	return count;
}

static ssize_t drm_fbdev_fb_read(struct fb_info *info, char __user *buf,
				 size_t count, loff_t *ppos)
{
	loff_t pos = *ppos;
	size_t total_size;
	ssize_t ret;

	if (info->screen_size)
		total_size = info->screen_size;
	else
		total_size = info->fix.smem_len;

	if (pos >= total_size)
		return 0;
	if (count >= total_size)
		count = total_size;
	if (total_size - count < pos)
		count = total_size - pos;

	if (drm_fbdev_use_iomem(info))
		ret = fb_read_screen_base(info, buf, count, pos);
	else
		ret = fb_read_screen_buffer(info, buf, count, pos);

	if (ret > 0)
		*ppos += ret;

	return ret;
}

static ssize_t fb_write_screen_base(struct fb_info *info, const char __user *buf, size_t count,
				    loff_t pos)
{
	char __iomem *dst = info->screen_base + pos;
	size_t alloc_size = min_t(size_t, count, PAGE_SIZE);
	ssize_t ret = 0;
	int err = 0;
	u8 *tmp;

	tmp = kmalloc(alloc_size, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	while (count) {
		size_t c = min_t(size_t, count, alloc_size);

		if (copy_from_user(tmp, buf, c)) {
			err = -EFAULT;
			break;
		}
		memcpy_toio(dst, tmp, c);

		dst += c;
		buf += c;
		ret += c;
		count -= c;
	}

	kfree(tmp);

	return ret ? ret : err;
}

static ssize_t fb_write_screen_buffer(struct fb_info *info, const char __user *buf, size_t count,
				      loff_t pos)
{
	char *dst = info->screen_buffer + pos;

	if (copy_from_user(dst, buf, count))
		return -EFAULT;

	return count;
}

static ssize_t drm_fbdev_fb_write(struct fb_info *info, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	loff_t pos = *ppos;
	size_t total_size;
	ssize_t ret;
	struct drm_rect damage_area;
	int err = 0;

	if (info->screen_size)
		total_size = info->screen_size;
	else
		total_size = info->fix.smem_len;

	if (pos > total_size)
		return -EFBIG;
	if (count > total_size) {
		err = -EFBIG;
		count = total_size;
	}
	if (total_size - count < pos) {
		if (!err)
			err = -ENOSPC;
		count = total_size - pos;
	}

	/*
	 * Copy to framebuffer even if we already logged an error. Emulates
	 * the behavior of the original fbdev implementation.
	 */
	if (drm_fbdev_use_iomem(info))
		ret = fb_write_screen_base(info, buf, count, pos);
	else
		ret = fb_write_screen_buffer(info, buf, count, pos);

	if (ret < 0)
		return ret; /* return last error, if any */
	else if (!ret)
		return err; /* return previous error, if any */

	*ppos += ret;

	drm_fb_helper_memory_range_to_clip(info, pos, ret, &damage_area);
	drm_fb_helper_damage(info, damage_area.x1, damage_area.y1,
			     drm_rect_width(&damage_area),
			     drm_rect_height(&damage_area));

	return ret;
}

static void drm_fbdev_fb_fillrect(struct fb_info *info,
				  const struct fb_fillrect *rect)
{
	if (drm_fbdev_use_iomem(info))
		drm_fb_helper_cfb_fillrect(info, rect);
	else
		drm_fb_helper_sys_fillrect(info, rect);
}

static void drm_fbdev_fb_copyarea(struct fb_info *info,
				  const struct fb_copyarea *area)
{
	if (drm_fbdev_use_iomem(info))
		drm_fb_helper_cfb_copyarea(info, area);
	else
		drm_fb_helper_sys_copyarea(info, area);
}

static void drm_fbdev_fb_imageblit(struct fb_info *info,
				   const struct fb_image *image)
{
	if (drm_fbdev_use_iomem(info))
		drm_fb_helper_cfb_imageblit(info, image);
	else
		drm_fb_helper_sys_imageblit(info, image);
}

static const struct fb_ops drm_fbdev_fb_ops = {
	.owner		= THIS_MODULE,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_open	= drm_fbdev_fb_open,
	.fb_release	= drm_fbdev_fb_release,
	.fb_destroy	= drm_fbdev_fb_destroy,
	.fb_mmap	= drm_fbdev_fb_mmap,
	.fb_read	= drm_fbdev_fb_read,
	.fb_write	= drm_fbdev_fb_write,
	.fb_fillrect	= drm_fbdev_fb_fillrect,
	.fb_copyarea	= drm_fbdev_fb_copyarea,
	.fb_imageblit	= drm_fbdev_fb_imageblit,
};

static struct fb_deferred_io drm_fbdev_defio = {
	.delay		= HZ / 20,
	.deferred_io	= drm_fb_helper_deferred_io,
};

/*
 * This function uses the client API to create a framebuffer backed by a dumb buffer.
 *
 * The _sys_ versions are used for &fb_ops.fb_read, fb_write, fb_fillrect,
 * fb_copyarea, fb_imageblit.
 */
static int drm_fb_helper_generic_probe(struct drm_fb_helper *fb_helper,
				       struct drm_fb_helper_surface_size *sizes)
{
	struct drm_client_dev *client = &fb_helper->client;
	struct drm_device *dev = fb_helper->dev;
	struct drm_client_buffer *buffer;
	struct drm_framebuffer *fb;
	struct fb_info *fbi;
	u32 format;
	struct iosys_map map;
	int ret;

	drm_dbg_kms(dev, "surface width(%d), height(%d) and bpp(%d)\n",
		    sizes->surface_width, sizes->surface_height,
		    sizes->surface_bpp);

	format = drm_mode_legacy_fb_format(sizes->surface_bpp, sizes->surface_depth);
	buffer = drm_client_framebuffer_create(client, sizes->surface_width,
					       sizes->surface_height, format);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	fb_helper->buffer = buffer;
	fb_helper->fb = buffer->fb;
	fb = buffer->fb;

	fbi = drm_fb_helper_alloc_fbi(fb_helper);
	if (IS_ERR(fbi))
		return PTR_ERR(fbi);

	fbi->fbops = &drm_fbdev_fb_ops;
	fbi->screen_size = sizes->surface_height * fb->pitches[0];
	fbi->fix.smem_len = fbi->screen_size;
	fbi->flags = FBINFO_DEFAULT;

	drm_fb_helper_fill_info(fbi, fb_helper, sizes);

	if (drm_fbdev_use_shadow_fb(fb_helper)) {
		fbi->screen_buffer = vzalloc(fbi->screen_size);
		if (!fbi->screen_buffer)
			return -ENOMEM;
		fbi->flags |= FBINFO_VIRTFB | FBINFO_READS_FAST;

		fbi->fbdefio = &drm_fbdev_defio;
		fb_deferred_io_init(fbi);
	} else {
		/* buffer is mapped for HW framebuffer */
		ret = drm_client_buffer_vmap(fb_helper->buffer, &map);
		if (ret)
			return ret;
		if (map.is_iomem) {
			fbi->screen_base = map.vaddr_iomem;
		} else {
			fbi->screen_buffer = map.vaddr;
			fbi->flags |= FBINFO_VIRTFB;
		}

		/*
		 * Shamelessly leak the physical address to user-space. As
		 * page_to_phys() is undefined for I/O memory, warn in this
		 * case.
		 */
#if IS_ENABLED(CONFIG_DRM_FBDEV_LEAK_PHYS_SMEM)
		if (drm_leak_fbdev_smem && fbi->fix.smem_start == 0 &&
		    !drm_WARN_ON_ONCE(dev, map.is_iomem))
			fbi->fix.smem_start =
				page_to_phys(virt_to_page(fbi->screen_buffer));
#endif
	}

	return 0;
}

static const struct drm_fb_helper_funcs drm_fb_helper_generic_funcs = {
	.fb_probe = drm_fb_helper_generic_probe,
};

static void drm_fbdev_client_unregister(struct drm_client_dev *client)
{
	struct drm_fb_helper *fb_helper = drm_fb_helper_from_client(client);

	if (fb_helper->fbdev)
		/* drm_fbdev_fb_destroy() takes care of cleanup */
		drm_fb_helper_unregister_fbi(fb_helper);
	else
		drm_fbdev_release(fb_helper);
}

static int drm_fbdev_client_restore(struct drm_client_dev *client)
{
	drm_fb_helper_lastclose(client->dev);

	return 0;
}

static int drm_fbdev_client_hotplug(struct drm_client_dev *client)
{
	struct drm_fb_helper *fb_helper = drm_fb_helper_from_client(client);
	struct drm_device *dev = client->dev;
	int ret;

	/* Setup is not retried if it has failed */
	if (!fb_helper->dev && fb_helper->funcs)
		return 0;

	if (dev->fb_helper)
		return drm_fb_helper_hotplug_event(dev->fb_helper);

	if (!dev->mode_config.num_connector) {
		drm_dbg_kms(dev, "No connectors found, will not create framebuffer!\n");
		return 0;
	}

	drm_fb_helper_prepare(dev, fb_helper, &drm_fb_helper_generic_funcs);

	ret = drm_fb_helper_init(dev, fb_helper);
	if (ret)
		goto err;

	if (!drm_drv_uses_atomic_modeset(dev))
		drm_helper_disable_unused_functions(dev);

	ret = drm_fb_helper_initial_config(fb_helper, fb_helper->preferred_bpp);
	if (ret)
		goto err_cleanup;

	return 0;

err_cleanup:
	drm_fbdev_cleanup(fb_helper);
err:
	fb_helper->dev = NULL;
	fb_helper->fbdev = NULL;

	drm_err(dev, "fbdev: Failed to setup generic emulation (ret=%d)\n", ret);

	return ret;
}

static const struct drm_client_funcs drm_fbdev_client_funcs = {
	.owner		= THIS_MODULE,
	.unregister	= drm_fbdev_client_unregister,
	.restore	= drm_fbdev_client_restore,
	.hotplug	= drm_fbdev_client_hotplug,
};

/**
 * drm_fbdev_generic_setup() - Setup generic fbdev emulation
 * @dev: DRM device
 * @preferred_bpp: Preferred bits per pixel for the device.
 *                 @dev->mode_config.preferred_depth is used if this is zero.
 *
 * This function sets up generic fbdev emulation for drivers that supports
 * dumb buffers with a virtual address and that can be mmap'ed.
 * drm_fbdev_generic_setup() shall be called after the DRM driver registered
 * the new DRM device with drm_dev_register().
 *
 * Restore, hotplug events and teardown are all taken care of. Drivers that do
 * suspend/resume need to call drm_fb_helper_set_suspend_unlocked() themselves.
 * Simple drivers might use drm_mode_config_helper_suspend().
 *
 * Drivers that set the dirty callback on their framebuffer will get a shadow
 * fbdev buffer that is blitted onto the real buffer. This is done in order to
 * make deferred I/O work with all kinds of buffers. A shadow buffer can be
 * requested explicitly by setting struct drm_mode_config.prefer_shadow or
 * struct drm_mode_config.prefer_shadow_fbdev to true beforehand. This is
 * required to use generic fbdev emulation with SHMEM helpers.
 *
 * This function is safe to call even when there are no connectors present.
 * Setup will be retried on the next hotplug event.
 *
 * The fbdev is destroyed by drm_dev_unregister().
 */
void drm_fbdev_generic_setup(struct drm_device *dev,
			     unsigned int preferred_bpp)
{
	struct drm_fb_helper *fb_helper;
	int ret;

	drm_WARN(dev, !dev->registered, "Device has not been registered.\n");
	drm_WARN(dev, dev->fb_helper, "fb_helper is already set!\n");

	if (!drm_fbdev_emulation)
		return;

	fb_helper = kzalloc(sizeof(*fb_helper), GFP_KERNEL);
	if (!fb_helper) {
		drm_err(dev, "Failed to allocate fb_helper\n");
		return;
	}

	ret = drm_client_init(dev, &fb_helper->client, "fbdev", &drm_fbdev_client_funcs);
	if (ret) {
		kfree(fb_helper);
		drm_err(dev, "Failed to register client: %d\n", ret);
		return;
	}

	/*
	 * FIXME: This mixes up depth with bpp, which results in a glorious
	 * mess, resulting in some drivers picking wrong fbdev defaults and
	 * others wrong preferred_depth defaults.
	 */
	if (!preferred_bpp)
		preferred_bpp = dev->mode_config.preferred_depth;
	if (!preferred_bpp)
		preferred_bpp = 32;
	fb_helper->preferred_bpp = preferred_bpp;

	ret = drm_fbdev_client_hotplug(&fb_helper->client);
	if (ret)
		drm_dbg_kms(dev, "client hotplug ret=%d\n", ret);

	drm_client_register(&fb_helper->client);
}
EXPORT_SYMBOL(drm_fbdev_generic_setup);
