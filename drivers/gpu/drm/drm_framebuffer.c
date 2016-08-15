/*
 * Copyright (c) 2016 Intel Corporation
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
 */

#include <linux/export.h>
#include <drm/drmP.h>
#include <drm/drm_auth.h>
#include <drm/drm_framebuffer.h>

#include "drm_crtc_internal.h"

/**
 * drm_mode_addfb - add an FB to the graphics configuration
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Add a new FB to the specified CRTC, given a user request. This is the
 * original addfb ioctl which only supported RGB formats.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_addfb(struct drm_device *dev,
		   void *data, struct drm_file *file_priv)
{
	struct drm_mode_fb_cmd *or = data;
	struct drm_mode_fb_cmd2 r = {};
	int ret;

	/* convert to new format and call new ioctl */
	r.fb_id = or->fb_id;
	r.width = or->width;
	r.height = or->height;
	r.pitches[0] = or->pitch;
	r.pixel_format = drm_mode_legacy_fb_format(or->bpp, or->depth);
	r.handles[0] = or->handle;

	ret = drm_mode_addfb2(dev, &r, file_priv);
	if (ret)
		return ret;

	or->fb_id = r.fb_id;

	return 0;
}

static int format_check(const struct drm_mode_fb_cmd2 *r)
{
	uint32_t format = r->pixel_format & ~DRM_FORMAT_BIG_ENDIAN;
	char *format_name;

	switch (format) {
	case DRM_FORMAT_C8:
	case DRM_FORMAT_RGB332:
	case DRM_FORMAT_BGR233:
	case DRM_FORMAT_XRGB4444:
	case DRM_FORMAT_XBGR4444:
	case DRM_FORMAT_RGBX4444:
	case DRM_FORMAT_BGRX4444:
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_ABGR4444:
	case DRM_FORMAT_RGBA4444:
	case DRM_FORMAT_BGRA4444:
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_RGBX5551:
	case DRM_FORMAT_BGRX5551:
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_BGRA5551:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_RGBX1010102:
	case DRM_FORMAT_BGRX1010102:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_BGRA1010102:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_AYUV:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV42:
	case DRM_FORMAT_YUV410:
	case DRM_FORMAT_YVU410:
	case DRM_FORMAT_YUV411:
	case DRM_FORMAT_YVU411:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YUV444:
	case DRM_FORMAT_YVU444:
		return 0;
	default:
		format_name = drm_get_format_name(r->pixel_format);
		DRM_DEBUG_KMS("invalid pixel format %s\n", format_name);
		kfree(format_name);
		return -EINVAL;
	}
}

static int framebuffer_check(const struct drm_mode_fb_cmd2 *r)
{
	int ret, hsub, vsub, num_planes, i;

	ret = format_check(r);
	if (ret) {
		char *format_name = drm_get_format_name(r->pixel_format);
		DRM_DEBUG_KMS("bad framebuffer format %s\n", format_name);
		kfree(format_name);
		return ret;
	}

	hsub = drm_format_horz_chroma_subsampling(r->pixel_format);
	vsub = drm_format_vert_chroma_subsampling(r->pixel_format);
	num_planes = drm_format_num_planes(r->pixel_format);

	if (r->width == 0 || r->width % hsub) {
		DRM_DEBUG_KMS("bad framebuffer width %u\n", r->width);
		return -EINVAL;
	}

	if (r->height == 0 || r->height % vsub) {
		DRM_DEBUG_KMS("bad framebuffer height %u\n", r->height);
		return -EINVAL;
	}

	for (i = 0; i < num_planes; i++) {
		unsigned int width = r->width / (i != 0 ? hsub : 1);
		unsigned int height = r->height / (i != 0 ? vsub : 1);
		unsigned int cpp = drm_format_plane_cpp(r->pixel_format, i);

		if (!r->handles[i]) {
			DRM_DEBUG_KMS("no buffer object handle for plane %d\n", i);
			return -EINVAL;
		}

		if ((uint64_t) width * cpp > UINT_MAX)
			return -ERANGE;

		if ((uint64_t) height * r->pitches[i] + r->offsets[i] > UINT_MAX)
			return -ERANGE;

		if (r->pitches[i] < width * cpp) {
			DRM_DEBUG_KMS("bad pitch %u for plane %d\n", r->pitches[i], i);
			return -EINVAL;
		}

		if (r->modifier[i] && !(r->flags & DRM_MODE_FB_MODIFIERS)) {
			DRM_DEBUG_KMS("bad fb modifier %llu for plane %d\n",
				      r->modifier[i], i);
			return -EINVAL;
		}

		/* modifier specific checks: */
		switch (r->modifier[i]) {
		case DRM_FORMAT_MOD_SAMSUNG_64_32_TILE:
			/* NOTE: the pitch restriction may be lifted later if it turns
			 * out that no hw has this restriction:
			 */
			if (r->pixel_format != DRM_FORMAT_NV12 ||
					width % 128 || height % 32 ||
					r->pitches[i] % 128) {
				DRM_DEBUG_KMS("bad modifier data for plane %d\n", i);
				return -EINVAL;
			}
			break;

		default:
			break;
		}
	}

	for (i = num_planes; i < 4; i++) {
		if (r->modifier[i]) {
			DRM_DEBUG_KMS("non-zero modifier for unused plane %d\n", i);
			return -EINVAL;
		}

		/* Pre-FB_MODIFIERS userspace didn't clear the structs properly. */
		if (!(r->flags & DRM_MODE_FB_MODIFIERS))
			continue;

		if (r->handles[i]) {
			DRM_DEBUG_KMS("buffer object handle for unused plane %d\n", i);
			return -EINVAL;
		}

		if (r->pitches[i]) {
			DRM_DEBUG_KMS("non-zero pitch for unused plane %d\n", i);
			return -EINVAL;
		}

		if (r->offsets[i]) {
			DRM_DEBUG_KMS("non-zero offset for unused plane %d\n", i);
			return -EINVAL;
		}
	}

	return 0;
}

struct drm_framebuffer *
drm_internal_framebuffer_create(struct drm_device *dev,
				const struct drm_mode_fb_cmd2 *r,
				struct drm_file *file_priv)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_framebuffer *fb;
	int ret;

	if (r->flags & ~(DRM_MODE_FB_INTERLACED | DRM_MODE_FB_MODIFIERS)) {
		DRM_DEBUG_KMS("bad framebuffer flags 0x%08x\n", r->flags);
		return ERR_PTR(-EINVAL);
	}

	if ((config->min_width > r->width) || (r->width > config->max_width)) {
		DRM_DEBUG_KMS("bad framebuffer width %d, should be >= %d && <= %d\n",
			  r->width, config->min_width, config->max_width);
		return ERR_PTR(-EINVAL);
	}
	if ((config->min_height > r->height) || (r->height > config->max_height)) {
		DRM_DEBUG_KMS("bad framebuffer height %d, should be >= %d && <= %d\n",
			  r->height, config->min_height, config->max_height);
		return ERR_PTR(-EINVAL);
	}

	if (r->flags & DRM_MODE_FB_MODIFIERS &&
	    !dev->mode_config.allow_fb_modifiers) {
		DRM_DEBUG_KMS("driver does not support fb modifiers\n");
		return ERR_PTR(-EINVAL);
	}

	ret = framebuffer_check(r);
	if (ret)
		return ERR_PTR(ret);

	fb = dev->mode_config.funcs->fb_create(dev, file_priv, r);
	if (IS_ERR(fb)) {
		DRM_DEBUG_KMS("could not create framebuffer\n");
		return fb;
	}

	return fb;
}

/**
 * drm_mode_addfb2 - add an FB to the graphics configuration
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Add a new FB to the specified CRTC, given a user request with format. This is
 * the 2nd version of the addfb ioctl, which supports multi-planar framebuffers
 * and uses fourcc codes as pixel format specifiers.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_addfb2(struct drm_device *dev,
		    void *data, struct drm_file *file_priv)
{
	struct drm_mode_fb_cmd2 *r = data;
	struct drm_framebuffer *fb;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	fb = drm_internal_framebuffer_create(dev, r, file_priv);
	if (IS_ERR(fb))
		return PTR_ERR(fb);

	DRM_DEBUG_KMS("[FB:%d]\n", fb->base.id);
	r->fb_id = fb->base.id;

	/* Transfer ownership to the filp for reaping on close */
	mutex_lock(&file_priv->fbs_lock);
	list_add(&fb->filp_head, &file_priv->fbs);
	mutex_unlock(&file_priv->fbs_lock);

	return 0;
}

struct drm_mode_rmfb_work {
	struct work_struct work;
	struct list_head fbs;
};

static void drm_mode_rmfb_work_fn(struct work_struct *w)
{
	struct drm_mode_rmfb_work *arg = container_of(w, typeof(*arg), work);

	while (!list_empty(&arg->fbs)) {
		struct drm_framebuffer *fb =
			list_first_entry(&arg->fbs, typeof(*fb), filp_head);

		list_del_init(&fb->filp_head);
		drm_framebuffer_remove(fb);
	}
}

/**
 * drm_mode_rmfb - remove an FB from the configuration
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Remove the FB specified by the user.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_rmfb(struct drm_device *dev,
		   void *data, struct drm_file *file_priv)
{
	struct drm_framebuffer *fb = NULL;
	struct drm_framebuffer *fbl = NULL;
	uint32_t *id = data;
	int found = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	fb = drm_framebuffer_lookup(dev, *id);
	if (!fb)
		return -ENOENT;

	mutex_lock(&file_priv->fbs_lock);
	list_for_each_entry(fbl, &file_priv->fbs, filp_head)
		if (fb == fbl)
			found = 1;
	if (!found) {
		mutex_unlock(&file_priv->fbs_lock);
		goto fail_unref;
	}

	list_del_init(&fb->filp_head);
	mutex_unlock(&file_priv->fbs_lock);

	/* drop the reference we picked up in framebuffer lookup */
	drm_framebuffer_unreference(fb);

	/*
	 * we now own the reference that was stored in the fbs list
	 *
	 * drm_framebuffer_remove may fail with -EINTR on pending signals,
	 * so run this in a separate stack as there's no way to correctly
	 * handle this after the fb is already removed from the lookup table.
	 */
	if (drm_framebuffer_read_refcount(fb) > 1) {
		struct drm_mode_rmfb_work arg;

		INIT_WORK_ONSTACK(&arg.work, drm_mode_rmfb_work_fn);
		INIT_LIST_HEAD(&arg.fbs);
		list_add_tail(&fb->filp_head, &arg.fbs);

		schedule_work(&arg.work);
		flush_work(&arg.work);
		destroy_work_on_stack(&arg.work);
	} else
		drm_framebuffer_unreference(fb);

	return 0;

fail_unref:
	drm_framebuffer_unreference(fb);
	return -ENOENT;
}

/**
 * drm_mode_getfb - get FB info
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Lookup the FB given its ID and return info about it.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_getfb(struct drm_device *dev,
		   void *data, struct drm_file *file_priv)
{
	struct drm_mode_fb_cmd *r = data;
	struct drm_framebuffer *fb;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	fb = drm_framebuffer_lookup(dev, r->fb_id);
	if (!fb)
		return -ENOENT;

	r->height = fb->height;
	r->width = fb->width;
	r->depth = fb->depth;
	r->bpp = fb->bits_per_pixel;
	r->pitch = fb->pitches[0];
	if (fb->funcs->create_handle) {
		if (drm_is_current_master(file_priv) || capable(CAP_SYS_ADMIN) ||
		    drm_is_control_client(file_priv)) {
			ret = fb->funcs->create_handle(fb, file_priv,
						       &r->handle);
		} else {
			/* GET_FB() is an unprivileged ioctl so we must not
			 * return a buffer-handle to non-master processes! For
			 * backwards-compatibility reasons, we cannot make
			 * GET_FB() privileged, so just return an invalid handle
			 * for non-masters. */
			r->handle = 0;
			ret = 0;
		}
	} else {
		ret = -ENODEV;
	}

	drm_framebuffer_unreference(fb);

	return ret;
}

/**
 * drm_mode_dirtyfb_ioctl - flush frontbuffer rendering on an FB
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Lookup the FB and flush out the damaged area supplied by userspace as a clip
 * rectangle list. Generic userspace which does frontbuffer rendering must call
 * this ioctl to flush out the changes on manual-update display outputs, e.g.
 * usb display-link, mipi manual update panels or edp panel self refresh modes.
 *
 * Modesetting drivers which always update the frontbuffer do not need to
 * implement the corresponding ->dirty framebuffer callback.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_dirtyfb_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv)
{
	struct drm_clip_rect __user *clips_ptr;
	struct drm_clip_rect *clips = NULL;
	struct drm_mode_fb_dirty_cmd *r = data;
	struct drm_framebuffer *fb;
	unsigned flags;
	int num_clips;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	fb = drm_framebuffer_lookup(dev, r->fb_id);
	if (!fb)
		return -ENOENT;

	num_clips = r->num_clips;
	clips_ptr = (struct drm_clip_rect __user *)(unsigned long)r->clips_ptr;

	if (!num_clips != !clips_ptr) {
		ret = -EINVAL;
		goto out_err1;
	}

	flags = DRM_MODE_FB_DIRTY_FLAGS & r->flags;

	/* If userspace annotates copy, clips must come in pairs */
	if (flags & DRM_MODE_FB_DIRTY_ANNOTATE_COPY && (num_clips % 2)) {
		ret = -EINVAL;
		goto out_err1;
	}

	if (num_clips && clips_ptr) {
		if (num_clips < 0 || num_clips > DRM_MODE_FB_DIRTY_MAX_CLIPS) {
			ret = -EINVAL;
			goto out_err1;
		}
		clips = kcalloc(num_clips, sizeof(*clips), GFP_KERNEL);
		if (!clips) {
			ret = -ENOMEM;
			goto out_err1;
		}

		ret = copy_from_user(clips, clips_ptr,
				     num_clips * sizeof(*clips));
		if (ret) {
			ret = -EFAULT;
			goto out_err2;
		}
	}

	if (fb->funcs->dirty) {
		ret = fb->funcs->dirty(fb, file_priv, flags, r->color,
				       clips, num_clips);
	} else {
		ret = -ENOSYS;
	}

out_err2:
	kfree(clips);
out_err1:
	drm_framebuffer_unreference(fb);

	return ret;
}

/**
 * drm_fb_release - remove and free the FBs on this file
 * @priv: drm file for the ioctl
 *
 * Destroy all the FBs associated with @filp.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
void drm_fb_release(struct drm_file *priv)
{
	struct drm_framebuffer *fb, *tfb;
	struct drm_mode_rmfb_work arg;

	INIT_LIST_HEAD(&arg.fbs);

	/*
	 * When the file gets released that means no one else can access the fb
	 * list any more, so no need to grab fpriv->fbs_lock. And we need to
	 * avoid upsetting lockdep since the universal cursor code adds a
	 * framebuffer while holding mutex locks.
	 *
	 * Note that a real deadlock between fpriv->fbs_lock and the modeset
	 * locks is impossible here since no one else but this function can get
	 * at it any more.
	 */
	list_for_each_entry_safe(fb, tfb, &priv->fbs, filp_head) {
		if (drm_framebuffer_read_refcount(fb) > 1) {
			list_move_tail(&fb->filp_head, &arg.fbs);
		} else {
			list_del_init(&fb->filp_head);

			/* This drops the fpriv->fbs reference. */
			drm_framebuffer_unreference(fb);
		}
	}

	if (!list_empty(&arg.fbs)) {
		INIT_WORK_ONSTACK(&arg.work, drm_mode_rmfb_work_fn);

		schedule_work(&arg.work);
		flush_work(&arg.work);
		destroy_work_on_stack(&arg.work);
	}
}

void drm_framebuffer_free(struct kref *kref)
{
	struct drm_framebuffer *fb =
			container_of(kref, struct drm_framebuffer, base.refcount);
	struct drm_device *dev = fb->dev;

	/*
	 * The lookup idr holds a weak reference, which has not necessarily been
	 * removed at this point. Check for that.
	 */
	drm_mode_object_unregister(dev, &fb->base);

	fb->funcs->destroy(fb);
}

/**
 * drm_framebuffer_init - initialize a framebuffer
 * @dev: DRM device
 * @fb: framebuffer to be initialized
 * @funcs: ... with these functions
 *
 * Allocates an ID for the framebuffer's parent mode object, sets its mode
 * functions & device file and adds it to the master fd list.
 *
 * IMPORTANT:
 * This functions publishes the fb and makes it available for concurrent access
 * by other users. Which means by this point the fb _must_ be fully set up -
 * since all the fb attributes are invariant over its lifetime, no further
 * locking but only correct reference counting is required.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_framebuffer_init(struct drm_device *dev, struct drm_framebuffer *fb,
			 const struct drm_framebuffer_funcs *funcs)
{
	int ret;

	INIT_LIST_HEAD(&fb->filp_head);
	fb->dev = dev;
	fb->funcs = funcs;

	ret = drm_mode_object_get_reg(dev, &fb->base, DRM_MODE_OBJECT_FB,
				      false, drm_framebuffer_free);
	if (ret)
		goto out;

	mutex_lock(&dev->mode_config.fb_lock);
	dev->mode_config.num_fb++;
	list_add(&fb->head, &dev->mode_config.fb_list);
	mutex_unlock(&dev->mode_config.fb_lock);

	drm_mode_object_register(dev, &fb->base);
out:
	return ret;
}
EXPORT_SYMBOL(drm_framebuffer_init);

/**
 * drm_framebuffer_lookup - look up a drm framebuffer and grab a reference
 * @dev: drm device
 * @id: id of the fb object
 *
 * If successful, this grabs an additional reference to the framebuffer -
 * callers need to make sure to eventually unreference the returned framebuffer
 * again, using @drm_framebuffer_unreference.
 */
struct drm_framebuffer *drm_framebuffer_lookup(struct drm_device *dev,
					       uint32_t id)
{
	struct drm_mode_object *obj;
	struct drm_framebuffer *fb = NULL;

	obj = __drm_mode_object_find(dev, id, DRM_MODE_OBJECT_FB);
	if (obj)
		fb = obj_to_fb(obj);
	return fb;
}
EXPORT_SYMBOL(drm_framebuffer_lookup);

/**
 * drm_framebuffer_unregister_private - unregister a private fb from the lookup idr
 * @fb: fb to unregister
 *
 * Drivers need to call this when cleaning up driver-private framebuffers, e.g.
 * those used for fbdev. Note that the caller must hold a reference of it's own,
 * i.e. the object may not be destroyed through this call (since it'll lead to a
 * locking inversion).
 */
void drm_framebuffer_unregister_private(struct drm_framebuffer *fb)
{
	struct drm_device *dev;

	if (!fb)
		return;

	dev = fb->dev;

	/* Mark fb as reaped and drop idr ref. */
	drm_mode_object_unregister(dev, &fb->base);
}
EXPORT_SYMBOL(drm_framebuffer_unregister_private);

/**
 * drm_framebuffer_cleanup - remove a framebuffer object
 * @fb: framebuffer to remove
 *
 * Cleanup framebuffer. This function is intended to be used from the drivers
 * ->destroy callback. It can also be used to clean up driver private
 * framebuffers embedded into a larger structure.
 *
 * Note that this function does not remove the fb from active usuage - if it is
 * still used anywhere, hilarity can ensue since userspace could call getfb on
 * the id and get back -EINVAL. Obviously no concern at driver unload time.
 *
 * Also, the framebuffer will not be removed from the lookup idr - for
 * user-created framebuffers this will happen in in the rmfb ioctl. For
 * driver-private objects (e.g. for fbdev) drivers need to explicitly call
 * drm_framebuffer_unregister_private.
 */
void drm_framebuffer_cleanup(struct drm_framebuffer *fb)
{
	struct drm_device *dev = fb->dev;

	mutex_lock(&dev->mode_config.fb_lock);
	list_del(&fb->head);
	dev->mode_config.num_fb--;
	mutex_unlock(&dev->mode_config.fb_lock);
}
EXPORT_SYMBOL(drm_framebuffer_cleanup);

/**
 * drm_framebuffer_remove - remove and unreference a framebuffer object
 * @fb: framebuffer to remove
 *
 * Scans all the CRTCs and planes in @dev's mode_config.  If they're
 * using @fb, removes it, setting it to NULL. Then drops the reference to the
 * passed-in framebuffer. Might take the modeset locks.
 *
 * Note that this function optimizes the cleanup away if the caller holds the
 * last reference to the framebuffer. It is also guaranteed to not take the
 * modeset locks in this case.
 */
void drm_framebuffer_remove(struct drm_framebuffer *fb)
{
	struct drm_device *dev;
	struct drm_crtc *crtc;
	struct drm_plane *plane;

	if (!fb)
		return;

	dev = fb->dev;

	WARN_ON(!list_empty(&fb->filp_head));

	/*
	 * drm ABI mandates that we remove any deleted framebuffers from active
	 * useage. But since most sane clients only remove framebuffers they no
	 * longer need, try to optimize this away.
	 *
	 * Since we're holding a reference ourselves, observing a refcount of 1
	 * means that we're the last holder and can skip it. Also, the refcount
	 * can never increase from 1 again, so we don't need any barriers or
	 * locks.
	 *
	 * Note that userspace could try to race with use and instate a new
	 * usage _after_ we've cleared all current ones. End result will be an
	 * in-use fb with fb-id == 0. Userspace is allowed to shoot its own foot
	 * in this manner.
	 */
	if (drm_framebuffer_read_refcount(fb) > 1) {
		drm_modeset_lock_all(dev);
		/* remove from any CRTC */
		drm_for_each_crtc(crtc, dev) {
			if (crtc->primary->fb == fb) {
				/* should turn off the crtc */
				if (drm_crtc_force_disable(crtc))
					DRM_ERROR("failed to reset crtc %p when fb was deleted\n", crtc);
			}
		}

		drm_for_each_plane(plane, dev) {
			if (plane->fb == fb)
				drm_plane_force_disable(plane);
		}
		drm_modeset_unlock_all(dev);
	}

	drm_framebuffer_unreference(fb);
}
EXPORT_SYMBOL(drm_framebuffer_remove);
