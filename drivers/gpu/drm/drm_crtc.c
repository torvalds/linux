/*
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2008 Red Hat Inc.
 *
 * DRM core CRTC related functions
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
 *      Keith Packard
 *	Eric Anholt <eric@anholt.net>
 *      Dave Airlie <airlied@linux.ie>
 *      Jesse Barnes <jesse.barnes@intel.com>
 */
#include <linux/ctype.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_modeset_lock.h>
#include <drm/drm_atomic.h>
#include <drm/drm_auth.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

static struct drm_framebuffer *
internal_framebuffer_create(struct drm_device *dev,
			    const struct drm_mode_fb_cmd2 *r,
			    struct drm_file *file_priv);

/* Avoid boilerplate.  I'm tired of typing. */
#define DRM_ENUM_NAME_FN(fnname, list)				\
	const char *fnname(int val)				\
	{							\
		int i;						\
		for (i = 0; i < ARRAY_SIZE(list); i++) {	\
			if (list[i].type == val)		\
				return list[i].name;		\
		}						\
		return "(unknown)";				\
	}

/*
 * Global properties
 */
static const struct drm_prop_enum_list drm_dpms_enum_list[] = {
	{ DRM_MODE_DPMS_ON, "On" },
	{ DRM_MODE_DPMS_STANDBY, "Standby" },
	{ DRM_MODE_DPMS_SUSPEND, "Suspend" },
	{ DRM_MODE_DPMS_OFF, "Off" }
};

DRM_ENUM_NAME_FN(drm_get_dpms_name, drm_dpms_enum_list)

static const struct drm_prop_enum_list drm_plane_type_enum_list[] = {
	{ DRM_PLANE_TYPE_OVERLAY, "Overlay" },
	{ DRM_PLANE_TYPE_PRIMARY, "Primary" },
	{ DRM_PLANE_TYPE_CURSOR, "Cursor" },
};

/*
 * Optional properties
 */
static const struct drm_prop_enum_list drm_scaling_mode_enum_list[] = {
	{ DRM_MODE_SCALE_NONE, "None" },
	{ DRM_MODE_SCALE_FULLSCREEN, "Full" },
	{ DRM_MODE_SCALE_CENTER, "Center" },
	{ DRM_MODE_SCALE_ASPECT, "Full aspect" },
};

static const struct drm_prop_enum_list drm_aspect_ratio_enum_list[] = {
	{ DRM_MODE_PICTURE_ASPECT_NONE, "Automatic" },
	{ DRM_MODE_PICTURE_ASPECT_4_3, "4:3" },
	{ DRM_MODE_PICTURE_ASPECT_16_9, "16:9" },
};

/*
 * Non-global properties, but "required" for certain connectors.
 */
static const struct drm_prop_enum_list drm_dvi_i_select_enum_list[] = {
	{ DRM_MODE_SUBCONNECTOR_Automatic, "Automatic" }, /* DVI-I and TV-out */
	{ DRM_MODE_SUBCONNECTOR_DVID,      "DVI-D"     }, /* DVI-I  */
	{ DRM_MODE_SUBCONNECTOR_DVIA,      "DVI-A"     }, /* DVI-I  */
};

DRM_ENUM_NAME_FN(drm_get_dvi_i_select_name, drm_dvi_i_select_enum_list)

static const struct drm_prop_enum_list drm_dvi_i_subconnector_enum_list[] = {
	{ DRM_MODE_SUBCONNECTOR_Unknown,   "Unknown"   }, /* DVI-I and TV-out */
	{ DRM_MODE_SUBCONNECTOR_DVID,      "DVI-D"     }, /* DVI-I  */
	{ DRM_MODE_SUBCONNECTOR_DVIA,      "DVI-A"     }, /* DVI-I  */
};

DRM_ENUM_NAME_FN(drm_get_dvi_i_subconnector_name,
		 drm_dvi_i_subconnector_enum_list)

static const struct drm_prop_enum_list drm_tv_select_enum_list[] = {
	{ DRM_MODE_SUBCONNECTOR_Automatic, "Automatic" }, /* DVI-I and TV-out */
	{ DRM_MODE_SUBCONNECTOR_Composite, "Composite" }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_SVIDEO,    "SVIDEO"    }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_Component, "Component" }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_SCART,     "SCART"     }, /* TV-out */
};

DRM_ENUM_NAME_FN(drm_get_tv_select_name, drm_tv_select_enum_list)

static const struct drm_prop_enum_list drm_tv_subconnector_enum_list[] = {
	{ DRM_MODE_SUBCONNECTOR_Unknown,   "Unknown"   }, /* DVI-I and TV-out */
	{ DRM_MODE_SUBCONNECTOR_Composite, "Composite" }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_SVIDEO,    "SVIDEO"    }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_Component, "Component" }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_SCART,     "SCART"     }, /* TV-out */
};

DRM_ENUM_NAME_FN(drm_get_tv_subconnector_name,
		 drm_tv_subconnector_enum_list)

struct drm_conn_prop_enum_list {
	int type;
	const char *name;
	struct ida ida;
};

/*
 * Connector and encoder types.
 */
static struct drm_conn_prop_enum_list drm_connector_enum_list[] = {
	{ DRM_MODE_CONNECTOR_Unknown, "Unknown" },
	{ DRM_MODE_CONNECTOR_VGA, "VGA" },
	{ DRM_MODE_CONNECTOR_DVII, "DVI-I" },
	{ DRM_MODE_CONNECTOR_DVID, "DVI-D" },
	{ DRM_MODE_CONNECTOR_DVIA, "DVI-A" },
	{ DRM_MODE_CONNECTOR_Composite, "Composite" },
	{ DRM_MODE_CONNECTOR_SVIDEO, "SVIDEO" },
	{ DRM_MODE_CONNECTOR_LVDS, "LVDS" },
	{ DRM_MODE_CONNECTOR_Component, "Component" },
	{ DRM_MODE_CONNECTOR_9PinDIN, "DIN" },
	{ DRM_MODE_CONNECTOR_DisplayPort, "DP" },
	{ DRM_MODE_CONNECTOR_HDMIA, "HDMI-A" },
	{ DRM_MODE_CONNECTOR_HDMIB, "HDMI-B" },
	{ DRM_MODE_CONNECTOR_TV, "TV" },
	{ DRM_MODE_CONNECTOR_eDP, "eDP" },
	{ DRM_MODE_CONNECTOR_VIRTUAL, "Virtual" },
	{ DRM_MODE_CONNECTOR_DSI, "DSI" },
	{ DRM_MODE_CONNECTOR_DPI, "DPI" },
};

static const struct drm_prop_enum_list drm_encoder_enum_list[] = {
	{ DRM_MODE_ENCODER_NONE, "None" },
	{ DRM_MODE_ENCODER_DAC, "DAC" },
	{ DRM_MODE_ENCODER_TMDS, "TMDS" },
	{ DRM_MODE_ENCODER_LVDS, "LVDS" },
	{ DRM_MODE_ENCODER_TVDAC, "TV" },
	{ DRM_MODE_ENCODER_VIRTUAL, "Virtual" },
	{ DRM_MODE_ENCODER_DSI, "DSI" },
	{ DRM_MODE_ENCODER_DPMST, "DP MST" },
	{ DRM_MODE_ENCODER_DPI, "DPI" },
};

static const struct drm_prop_enum_list drm_subpixel_enum_list[] = {
	{ SubPixelUnknown, "Unknown" },
	{ SubPixelHorizontalRGB, "Horizontal RGB" },
	{ SubPixelHorizontalBGR, "Horizontal BGR" },
	{ SubPixelVerticalRGB, "Vertical RGB" },
	{ SubPixelVerticalBGR, "Vertical BGR" },
	{ SubPixelNone, "None" },
};

void drm_connector_ida_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(drm_connector_enum_list); i++)
		ida_init(&drm_connector_enum_list[i].ida);
}

void drm_connector_ida_destroy(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(drm_connector_enum_list); i++)
		ida_destroy(&drm_connector_enum_list[i].ida);
}

/**
 * drm_get_connector_status_name - return a string for connector status
 * @status: connector status to compute name of
 *
 * In contrast to the other drm_get_*_name functions this one here returns a
 * const pointer and hence is threadsafe.
 */
const char *drm_get_connector_status_name(enum drm_connector_status status)
{
	if (status == connector_status_connected)
		return "connected";
	else if (status == connector_status_disconnected)
		return "disconnected";
	else
		return "unknown";
}
EXPORT_SYMBOL(drm_get_connector_status_name);

/**
 * drm_get_subpixel_order_name - return a string for a given subpixel enum
 * @order: enum of subpixel_order
 *
 * Note you could abuse this and return something out of bounds, but that
 * would be a caller error.  No unscrubbed user data should make it here.
 */
const char *drm_get_subpixel_order_name(enum subpixel_order order)
{
	return drm_subpixel_enum_list[order].name;
}
EXPORT_SYMBOL(drm_get_subpixel_order_name);

/*
 * Internal function to assign a slot in the object idr and optionally
 * register the object into the idr.
 */
static int drm_mode_object_get_reg(struct drm_device *dev,
				   struct drm_mode_object *obj,
				   uint32_t obj_type,
				   bool register_obj,
				   void (*obj_free_cb)(struct kref *kref))
{
	int ret;

	mutex_lock(&dev->mode_config.idr_mutex);
	ret = idr_alloc(&dev->mode_config.crtc_idr, register_obj ? obj : NULL, 1, 0, GFP_KERNEL);
	if (ret >= 0) {
		/*
		 * Set up the object linking under the protection of the idr
		 * lock so that other users can't see inconsistent state.
		 */
		obj->id = ret;
		obj->type = obj_type;
		if (obj_free_cb) {
			obj->free_cb = obj_free_cb;
			kref_init(&obj->refcount);
		}
	}
	mutex_unlock(&dev->mode_config.idr_mutex);

	return ret < 0 ? ret : 0;
}

/**
 * drm_mode_object_get - allocate a new modeset identifier
 * @dev: DRM device
 * @obj: object pointer, used to generate unique ID
 * @obj_type: object type
 *
 * Create a unique identifier based on @ptr in @dev's identifier space.  Used
 * for tracking modes, CRTCs and connectors. Note that despite the _get postfix
 * modeset identifiers are _not_ reference counted. Hence don't use this for
 * reference counted modeset objects like framebuffers.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_mode_object_get(struct drm_device *dev,
			struct drm_mode_object *obj, uint32_t obj_type)
{
	return drm_mode_object_get_reg(dev, obj, obj_type, true, NULL);
}

static void drm_mode_object_register(struct drm_device *dev,
				     struct drm_mode_object *obj)
{
	mutex_lock(&dev->mode_config.idr_mutex);
	idr_replace(&dev->mode_config.crtc_idr, obj, obj->id);
	mutex_unlock(&dev->mode_config.idr_mutex);
}

/**
 * drm_mode_object_unregister - free a modeset identifer
 * @dev: DRM device
 * @object: object to free
 *
 * Free @id from @dev's unique identifier pool.
 * This function can be called multiple times, and guards against
 * multiple removals.
 * These modeset identifiers are _not_ reference counted. Hence don't use this
 * for reference counted modeset objects like framebuffers.
 */
void drm_mode_object_unregister(struct drm_device *dev,
			 struct drm_mode_object *object)
{
	mutex_lock(&dev->mode_config.idr_mutex);
	if (object->id) {
		idr_remove(&dev->mode_config.crtc_idr, object->id);
		object->id = 0;
	}
	mutex_unlock(&dev->mode_config.idr_mutex);
}

static struct drm_mode_object *_object_find(struct drm_device *dev,
		uint32_t id, uint32_t type)
{
	struct drm_mode_object *obj = NULL;

	mutex_lock(&dev->mode_config.idr_mutex);
	obj = idr_find(&dev->mode_config.crtc_idr, id);
	if (obj && type != DRM_MODE_OBJECT_ANY && obj->type != type)
		obj = NULL;
	if (obj && obj->id != id)
		obj = NULL;

	if (obj && obj->free_cb) {
		if (!kref_get_unless_zero(&obj->refcount))
			obj = NULL;
	}
	mutex_unlock(&dev->mode_config.idr_mutex);

	return obj;
}

/**
 * drm_mode_object_find - look up a drm object with static lifetime
 * @dev: drm device
 * @id: id of the mode object
 * @type: type of the mode object
 *
 * This function is used to look up a modeset object. It will acquire a
 * reference for reference counted objects. This reference must be dropped again
 * by callind drm_mode_object_unreference().
 */
struct drm_mode_object *drm_mode_object_find(struct drm_device *dev,
		uint32_t id, uint32_t type)
{
	struct drm_mode_object *obj = NULL;

	obj = _object_find(dev, id, type);
	return obj;
}
EXPORT_SYMBOL(drm_mode_object_find);

/**
 * drm_mode_object_unreference - decr the object refcnt
 * @obj: mode_object
 *
 * This functions decrements the object's refcount if it is a refcounted modeset
 * object. It is a no-op on any other object. This is used to drop references
 * acquired with drm_mode_object_reference().
 */
void drm_mode_object_unreference(struct drm_mode_object *obj)
{
	if (obj->free_cb) {
		DRM_DEBUG("OBJ ID: %d (%d)\n", obj->id, atomic_read(&obj->refcount.refcount));
		kref_put(&obj->refcount, obj->free_cb);
	}
}
EXPORT_SYMBOL(drm_mode_object_unreference);

/**
 * drm_mode_object_reference - incr the object refcnt
 * @obj: mode_object
 *
 * This functions increments the object's refcount if it is a refcounted modeset
 * object. It is a no-op on any other object. References should be dropped again
 * by calling drm_mode_object_unreference().
 */
void drm_mode_object_reference(struct drm_mode_object *obj)
{
	if (obj->free_cb) {
		DRM_DEBUG("OBJ ID: %d (%d)\n", obj->id, atomic_read(&obj->refcount.refcount));
		kref_get(&obj->refcount);
	}
}
EXPORT_SYMBOL(drm_mode_object_reference);

/**
 * drm_crtc_force_disable - Forcibly turn off a CRTC
 * @crtc: CRTC to turn off
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_crtc_force_disable(struct drm_crtc *crtc)
{
	struct drm_mode_set set = {
		.crtc = crtc,
	};

	return drm_mode_set_config_internal(&set);
}
EXPORT_SYMBOL(drm_crtc_force_disable);

/**
 * drm_crtc_force_disable_all - Forcibly turn off all enabled CRTCs
 * @dev: DRM device whose CRTCs to turn off
 *
 * Drivers may want to call this on unload to ensure that all displays are
 * unlit and the GPU is in a consistent, low power state. Takes modeset locks.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_crtc_force_disable_all(struct drm_device *dev)
{
	struct drm_crtc *crtc;
	int ret = 0;

	drm_modeset_lock_all(dev);
	drm_for_each_crtc(crtc, dev)
		if (crtc->enabled) {
			ret = drm_crtc_force_disable(crtc);
			if (ret)
				goto out;
		}
out:
	drm_modeset_unlock_all(dev);
	return ret;
}
EXPORT_SYMBOL(drm_crtc_force_disable_all);

static void drm_framebuffer_free(struct kref *kref)
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

	obj = _object_find(dev, id, DRM_MODE_OBJECT_FB);
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

DEFINE_WW_CLASS(crtc_ww_class);

static unsigned int drm_num_crtcs(struct drm_device *dev)
{
	unsigned int num = 0;
	struct drm_crtc *tmp;

	drm_for_each_crtc(tmp, dev) {
		num++;
	}

	return num;
}

static int drm_crtc_register_all(struct drm_device *dev)
{
	struct drm_crtc *crtc;
	int ret = 0;

	drm_for_each_crtc(crtc, dev) {
		if (crtc->funcs->late_register)
			ret = crtc->funcs->late_register(crtc);
		if (ret)
			return ret;
	}

	return 0;
}

static void drm_crtc_unregister_all(struct drm_device *dev)
{
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, dev) {
		if (crtc->funcs->early_unregister)
			crtc->funcs->early_unregister(crtc);
	}
}

/**
 * drm_crtc_init_with_planes - Initialise a new CRTC object with
 *    specified primary and cursor planes.
 * @dev: DRM device
 * @crtc: CRTC object to init
 * @primary: Primary plane for CRTC
 * @cursor: Cursor plane for CRTC
 * @funcs: callbacks for the new CRTC
 * @name: printf style format string for the CRTC name, or NULL for default name
 *
 * Inits a new object created as base part of a driver crtc object.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_crtc_init_with_planes(struct drm_device *dev, struct drm_crtc *crtc,
			      struct drm_plane *primary,
			      struct drm_plane *cursor,
			      const struct drm_crtc_funcs *funcs,
			      const char *name, ...)
{
	struct drm_mode_config *config = &dev->mode_config;
	int ret;

	WARN_ON(primary && primary->type != DRM_PLANE_TYPE_PRIMARY);
	WARN_ON(cursor && cursor->type != DRM_PLANE_TYPE_CURSOR);

	crtc->dev = dev;
	crtc->funcs = funcs;

	INIT_LIST_HEAD(&crtc->commit_list);
	spin_lock_init(&crtc->commit_lock);

	drm_modeset_lock_init(&crtc->mutex);
	ret = drm_mode_object_get(dev, &crtc->base, DRM_MODE_OBJECT_CRTC);
	if (ret)
		return ret;

	if (name) {
		va_list ap;

		va_start(ap, name);
		crtc->name = kvasprintf(GFP_KERNEL, name, ap);
		va_end(ap);
	} else {
		crtc->name = kasprintf(GFP_KERNEL, "crtc-%d",
				       drm_num_crtcs(dev));
	}
	if (!crtc->name) {
		drm_mode_object_unregister(dev, &crtc->base);
		return -ENOMEM;
	}

	crtc->base.properties = &crtc->properties;

	list_add_tail(&crtc->head, &config->crtc_list);
	crtc->index = config->num_crtc++;

	crtc->primary = primary;
	crtc->cursor = cursor;
	if (primary)
		primary->possible_crtcs = 1 << drm_crtc_index(crtc);
	if (cursor)
		cursor->possible_crtcs = 1 << drm_crtc_index(crtc);

	if (drm_core_check_feature(dev, DRIVER_ATOMIC)) {
		drm_object_attach_property(&crtc->base, config->prop_active, 0);
		drm_object_attach_property(&crtc->base, config->prop_mode_id, 0);
	}

	return 0;
}
EXPORT_SYMBOL(drm_crtc_init_with_planes);

/**
 * drm_crtc_cleanup - Clean up the core crtc usage
 * @crtc: CRTC to cleanup
 *
 * This function cleans up @crtc and removes it from the DRM mode setting
 * core. Note that the function does *not* free the crtc structure itself,
 * this is the responsibility of the caller.
 */
void drm_crtc_cleanup(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;

	/* Note that the crtc_list is considered to be static; should we
	 * remove the drm_crtc at runtime we would have to decrement all
	 * the indices on the drm_crtc after us in the crtc_list.
	 */

	kfree(crtc->gamma_store);
	crtc->gamma_store = NULL;

	drm_modeset_lock_fini(&crtc->mutex);

	drm_mode_object_unregister(dev, &crtc->base);
	list_del(&crtc->head);
	dev->mode_config.num_crtc--;

	WARN_ON(crtc->state && !crtc->funcs->atomic_destroy_state);
	if (crtc->state && crtc->funcs->atomic_destroy_state)
		crtc->funcs->atomic_destroy_state(crtc, crtc->state);

	kfree(crtc->name);

	memset(crtc, 0, sizeof(*crtc));
}
EXPORT_SYMBOL(drm_crtc_cleanup);

/*
 * drm_mode_remove - remove and free a mode
 * @connector: connector list to modify
 * @mode: mode to remove
 *
 * Remove @mode from @connector's mode list, then free it.
 */
static void drm_mode_remove(struct drm_connector *connector,
			    struct drm_display_mode *mode)
{
	list_del(&mode->head);
	drm_mode_destroy(connector->dev, mode);
}

/**
 * drm_display_info_set_bus_formats - set the supported bus formats
 * @info: display info to store bus formats in
 * @formats: array containing the supported bus formats
 * @num_formats: the number of entries in the fmts array
 *
 * Store the supported bus formats in display info structure.
 * See MEDIA_BUS_FMT_* definitions in include/uapi/linux/media-bus-format.h for
 * a full list of available formats.
 */
int drm_display_info_set_bus_formats(struct drm_display_info *info,
				     const u32 *formats,
				     unsigned int num_formats)
{
	u32 *fmts = NULL;

	if (!formats && num_formats)
		return -EINVAL;

	if (formats && num_formats) {
		fmts = kmemdup(formats, sizeof(*formats) * num_formats,
			       GFP_KERNEL);
		if (!fmts)
			return -ENOMEM;
	}

	kfree(info->bus_formats);
	info->bus_formats = fmts;
	info->num_bus_formats = num_formats;

	return 0;
}
EXPORT_SYMBOL(drm_display_info_set_bus_formats);

/**
 * drm_connector_get_cmdline_mode - reads the user's cmdline mode
 * @connector: connector to quwery
 *
 * The kernel supports per-connector configration of its consoles through
 * use of the video= parameter. This function parses that option and
 * extracts the user's specified mode (or enable/disable status) for a
 * particular connector. This is typically only used during the early fbdev
 * setup.
 */
static void drm_connector_get_cmdline_mode(struct drm_connector *connector)
{
	struct drm_cmdline_mode *mode = &connector->cmdline_mode;
	char *option = NULL;

	if (fb_get_options(connector->name, &option))
		return;

	if (!drm_mode_parse_command_line_for_connector(option,
						       connector,
						       mode))
		return;

	if (mode->force) {
		const char *s;

		switch (mode->force) {
		case DRM_FORCE_OFF:
			s = "OFF";
			break;
		case DRM_FORCE_ON_DIGITAL:
			s = "ON - dig";
			break;
		default:
		case DRM_FORCE_ON:
			s = "ON";
			break;
		}

		DRM_INFO("forcing %s connector %s\n", connector->name, s);
		connector->force = mode->force;
	}

	DRM_DEBUG_KMS("cmdline mode for connector %s %dx%d@%dHz%s%s%s\n",
		      connector->name,
		      mode->xres, mode->yres,
		      mode->refresh_specified ? mode->refresh : 60,
		      mode->rb ? " reduced blanking" : "",
		      mode->margins ? " with margins" : "",
		      mode->interlace ?  " interlaced" : "");
}

static void drm_connector_free(struct kref *kref)
{
	struct drm_connector *connector =
		container_of(kref, struct drm_connector, base.refcount);
	struct drm_device *dev = connector->dev;

	drm_mode_object_unregister(dev, &connector->base);
	connector->funcs->destroy(connector);
}

/**
 * drm_connector_init - Init a preallocated connector
 * @dev: DRM device
 * @connector: the connector to init
 * @funcs: callbacks for this connector
 * @connector_type: user visible type of the connector
 *
 * Initialises a preallocated connector. Connectors should be
 * subclassed as part of driver connector objects.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_connector_init(struct drm_device *dev,
		       struct drm_connector *connector,
		       const struct drm_connector_funcs *funcs,
		       int connector_type)
{
	struct drm_mode_config *config = &dev->mode_config;
	int ret;
	struct ida *connector_ida =
		&drm_connector_enum_list[connector_type].ida;

	drm_modeset_lock_all(dev);

	ret = drm_mode_object_get_reg(dev, &connector->base,
				      DRM_MODE_OBJECT_CONNECTOR,
				      false, drm_connector_free);
	if (ret)
		goto out_unlock;

	connector->base.properties = &connector->properties;
	connector->dev = dev;
	connector->funcs = funcs;

	ret = ida_simple_get(&config->connector_ida, 0, 0, GFP_KERNEL);
	if (ret < 0)
		goto out_put;
	connector->index = ret;
	ret = 0;

	connector->connector_type = connector_type;
	connector->connector_type_id =
		ida_simple_get(connector_ida, 1, 0, GFP_KERNEL);
	if (connector->connector_type_id < 0) {
		ret = connector->connector_type_id;
		goto out_put_id;
	}
	connector->name =
		kasprintf(GFP_KERNEL, "%s-%d",
			  drm_connector_enum_list[connector_type].name,
			  connector->connector_type_id);
	if (!connector->name) {
		ret = -ENOMEM;
		goto out_put_type_id;
	}

	INIT_LIST_HEAD(&connector->probed_modes);
	INIT_LIST_HEAD(&connector->modes);
	connector->edid_blob_ptr = NULL;
	connector->status = connector_status_unknown;

	drm_connector_get_cmdline_mode(connector);

	/* We should add connectors at the end to avoid upsetting the connector
	 * index too much. */
	list_add_tail(&connector->head, &config->connector_list);
	config->num_connector++;

	if (connector_type != DRM_MODE_CONNECTOR_VIRTUAL)
		drm_object_attach_property(&connector->base,
					      config->edid_property,
					      0);

	drm_object_attach_property(&connector->base,
				      config->dpms_property, 0);

	if (drm_core_check_feature(dev, DRIVER_ATOMIC)) {
		drm_object_attach_property(&connector->base, config->prop_crtc_id, 0);
	}

	connector->debugfs_entry = NULL;
out_put_type_id:
	if (ret)
		ida_remove(connector_ida, connector->connector_type_id);
out_put_id:
	if (ret)
		ida_remove(&config->connector_ida, connector->index);
out_put:
	if (ret)
		drm_mode_object_unregister(dev, &connector->base);

out_unlock:
	drm_modeset_unlock_all(dev);

	return ret;
}
EXPORT_SYMBOL(drm_connector_init);

/**
 * drm_connector_cleanup - cleans up an initialised connector
 * @connector: connector to cleanup
 *
 * Cleans up the connector but doesn't free the object.
 */
void drm_connector_cleanup(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode, *t;

	/* The connector should have been removed from userspace long before
	 * it is finally destroyed.
	 */
	if (WARN_ON(connector->registered))
		drm_connector_unregister(connector);

	if (connector->tile_group) {
		drm_mode_put_tile_group(dev, connector->tile_group);
		connector->tile_group = NULL;
	}

	list_for_each_entry_safe(mode, t, &connector->probed_modes, head)
		drm_mode_remove(connector, mode);

	list_for_each_entry_safe(mode, t, &connector->modes, head)
		drm_mode_remove(connector, mode);

	ida_remove(&drm_connector_enum_list[connector->connector_type].ida,
		   connector->connector_type_id);

	ida_remove(&dev->mode_config.connector_ida,
		   connector->index);

	kfree(connector->display_info.bus_formats);
	drm_mode_object_unregister(dev, &connector->base);
	kfree(connector->name);
	connector->name = NULL;
	list_del(&connector->head);
	dev->mode_config.num_connector--;

	WARN_ON(connector->state && !connector->funcs->atomic_destroy_state);
	if (connector->state && connector->funcs->atomic_destroy_state)
		connector->funcs->atomic_destroy_state(connector,
						       connector->state);

	memset(connector, 0, sizeof(*connector));
}
EXPORT_SYMBOL(drm_connector_cleanup);

/**
 * drm_connector_register - register a connector
 * @connector: the connector to register
 *
 * Register userspace interfaces for a connector
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_connector_register(struct drm_connector *connector)
{
	int ret;

	if (connector->registered)
		return 0;

	ret = drm_sysfs_connector_add(connector);
	if (ret)
		return ret;

	ret = drm_debugfs_connector_add(connector);
	if (ret) {
		goto err_sysfs;
	}

	if (connector->funcs->late_register) {
		ret = connector->funcs->late_register(connector);
		if (ret)
			goto err_debugfs;
	}

	drm_mode_object_register(connector->dev, &connector->base);

	connector->registered = true;
	return 0;

err_debugfs:
	drm_debugfs_connector_remove(connector);
err_sysfs:
	drm_sysfs_connector_remove(connector);
	return ret;
}
EXPORT_SYMBOL(drm_connector_register);

/**
 * drm_connector_unregister - unregister a connector
 * @connector: the connector to unregister
 *
 * Unregister userspace interfaces for a connector
 */
void drm_connector_unregister(struct drm_connector *connector)
{
	if (!connector->registered)
		return;

	if (connector->funcs->early_unregister)
		connector->funcs->early_unregister(connector);

	drm_sysfs_connector_remove(connector);
	drm_debugfs_connector_remove(connector);

	connector->registered = false;
}
EXPORT_SYMBOL(drm_connector_unregister);

static void drm_connector_unregister_all(struct drm_device *dev)
{
	struct drm_connector *connector;

	/* FIXME: taking the mode config mutex ends up in a clash with sysfs */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		drm_connector_unregister(connector);
}

static int drm_connector_register_all(struct drm_device *dev)
{
	struct drm_connector *connector;
	int ret;

	/* FIXME: taking the mode config mutex ends up in a clash with
	 * fbcon/backlight registration */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		ret = drm_connector_register(connector);
		if (ret)
			goto err;
	}

	return 0;

err:
	mutex_unlock(&dev->mode_config.mutex);
	drm_connector_unregister_all(dev);
	return ret;
}

static int drm_encoder_register_all(struct drm_device *dev)
{
	struct drm_encoder *encoder;
	int ret = 0;

	drm_for_each_encoder(encoder, dev) {
		if (encoder->funcs->late_register)
			ret = encoder->funcs->late_register(encoder);
		if (ret)
			return ret;
	}

	return 0;
}

static void drm_encoder_unregister_all(struct drm_device *dev)
{
	struct drm_encoder *encoder;

	drm_for_each_encoder(encoder, dev) {
		if (encoder->funcs->early_unregister)
			encoder->funcs->early_unregister(encoder);
	}
}

/**
 * drm_encoder_init - Init a preallocated encoder
 * @dev: drm device
 * @encoder: the encoder to init
 * @funcs: callbacks for this encoder
 * @encoder_type: user visible type of the encoder
 * @name: printf style format string for the encoder name, or NULL for default name
 *
 * Initialises a preallocated encoder. Encoder should be
 * subclassed as part of driver encoder objects.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_encoder_init(struct drm_device *dev,
		      struct drm_encoder *encoder,
		      const struct drm_encoder_funcs *funcs,
		      int encoder_type, const char *name, ...)
{
	int ret;

	drm_modeset_lock_all(dev);

	ret = drm_mode_object_get(dev, &encoder->base, DRM_MODE_OBJECT_ENCODER);
	if (ret)
		goto out_unlock;

	encoder->dev = dev;
	encoder->encoder_type = encoder_type;
	encoder->funcs = funcs;
	if (name) {
		va_list ap;

		va_start(ap, name);
		encoder->name = kvasprintf(GFP_KERNEL, name, ap);
		va_end(ap);
	} else {
		encoder->name = kasprintf(GFP_KERNEL, "%s-%d",
					  drm_encoder_enum_list[encoder_type].name,
					  encoder->base.id);
	}
	if (!encoder->name) {
		ret = -ENOMEM;
		goto out_put;
	}

	list_add_tail(&encoder->head, &dev->mode_config.encoder_list);
	encoder->index = dev->mode_config.num_encoder++;

out_put:
	if (ret)
		drm_mode_object_unregister(dev, &encoder->base);

out_unlock:
	drm_modeset_unlock_all(dev);

	return ret;
}
EXPORT_SYMBOL(drm_encoder_init);

/**
 * drm_encoder_cleanup - cleans up an initialised encoder
 * @encoder: encoder to cleanup
 *
 * Cleans up the encoder but doesn't free the object.
 */
void drm_encoder_cleanup(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;

	/* Note that the encoder_list is considered to be static; should we
	 * remove the drm_encoder at runtime we would have to decrement all
	 * the indices on the drm_encoder after us in the encoder_list.
	 */

	drm_modeset_lock_all(dev);
	drm_mode_object_unregister(dev, &encoder->base);
	kfree(encoder->name);
	list_del(&encoder->head);
	dev->mode_config.num_encoder--;
	drm_modeset_unlock_all(dev);

	memset(encoder, 0, sizeof(*encoder));
}
EXPORT_SYMBOL(drm_encoder_cleanup);

static unsigned int drm_num_planes(struct drm_device *dev)
{
	unsigned int num = 0;
	struct drm_plane *tmp;

	drm_for_each_plane(tmp, dev) {
		num++;
	}

	return num;
}

/**
 * drm_universal_plane_init - Initialize a new universal plane object
 * @dev: DRM device
 * @plane: plane object to init
 * @possible_crtcs: bitmask of possible CRTCs
 * @funcs: callbacks for the new plane
 * @formats: array of supported formats (DRM_FORMAT\_\*)
 * @format_count: number of elements in @formats
 * @type: type of plane (overlay, primary, cursor)
 * @name: printf style format string for the plane name, or NULL for default name
 *
 * Initializes a plane object of type @type.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_universal_plane_init(struct drm_device *dev, struct drm_plane *plane,
			     unsigned long possible_crtcs,
			     const struct drm_plane_funcs *funcs,
			     const uint32_t *formats, unsigned int format_count,
			     enum drm_plane_type type,
			     const char *name, ...)
{
	struct drm_mode_config *config = &dev->mode_config;
	int ret;

	ret = drm_mode_object_get(dev, &plane->base, DRM_MODE_OBJECT_PLANE);
	if (ret)
		return ret;

	drm_modeset_lock_init(&plane->mutex);

	plane->base.properties = &plane->properties;
	plane->dev = dev;
	plane->funcs = funcs;
	plane->format_types = kmalloc_array(format_count, sizeof(uint32_t),
					    GFP_KERNEL);
	if (!plane->format_types) {
		DRM_DEBUG_KMS("out of memory when allocating plane\n");
		drm_mode_object_unregister(dev, &plane->base);
		return -ENOMEM;
	}

	if (name) {
		va_list ap;

		va_start(ap, name);
		plane->name = kvasprintf(GFP_KERNEL, name, ap);
		va_end(ap);
	} else {
		plane->name = kasprintf(GFP_KERNEL, "plane-%d",
					drm_num_planes(dev));
	}
	if (!plane->name) {
		kfree(plane->format_types);
		drm_mode_object_unregister(dev, &plane->base);
		return -ENOMEM;
	}

	memcpy(plane->format_types, formats, format_count * sizeof(uint32_t));
	plane->format_count = format_count;
	plane->possible_crtcs = possible_crtcs;
	plane->type = type;

	list_add_tail(&plane->head, &config->plane_list);
	plane->index = config->num_total_plane++;
	if (plane->type == DRM_PLANE_TYPE_OVERLAY)
		config->num_overlay_plane++;

	drm_object_attach_property(&plane->base,
				   config->plane_type_property,
				   plane->type);

	if (drm_core_check_feature(dev, DRIVER_ATOMIC)) {
		drm_object_attach_property(&plane->base, config->prop_fb_id, 0);
		drm_object_attach_property(&plane->base, config->prop_crtc_id, 0);
		drm_object_attach_property(&plane->base, config->prop_crtc_x, 0);
		drm_object_attach_property(&plane->base, config->prop_crtc_y, 0);
		drm_object_attach_property(&plane->base, config->prop_crtc_w, 0);
		drm_object_attach_property(&plane->base, config->prop_crtc_h, 0);
		drm_object_attach_property(&plane->base, config->prop_src_x, 0);
		drm_object_attach_property(&plane->base, config->prop_src_y, 0);
		drm_object_attach_property(&plane->base, config->prop_src_w, 0);
		drm_object_attach_property(&plane->base, config->prop_src_h, 0);
	}

	return 0;
}
EXPORT_SYMBOL(drm_universal_plane_init);

static int drm_plane_register_all(struct drm_device *dev)
{
	struct drm_plane *plane;
	int ret = 0;

	drm_for_each_plane(plane, dev) {
		if (plane->funcs->late_register)
			ret = plane->funcs->late_register(plane);
		if (ret)
			return ret;
	}

	return 0;
}

static void drm_plane_unregister_all(struct drm_device *dev)
{
	struct drm_plane *plane;

	drm_for_each_plane(plane, dev) {
		if (plane->funcs->early_unregister)
			plane->funcs->early_unregister(plane);
	}
}

/**
 * drm_plane_init - Initialize a legacy plane
 * @dev: DRM device
 * @plane: plane object to init
 * @possible_crtcs: bitmask of possible CRTCs
 * @funcs: callbacks for the new plane
 * @formats: array of supported formats (DRM_FORMAT\_\*)
 * @format_count: number of elements in @formats
 * @is_primary: plane type (primary vs overlay)
 *
 * Legacy API to initialize a DRM plane.
 *
 * New drivers should call drm_universal_plane_init() instead.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_plane_init(struct drm_device *dev, struct drm_plane *plane,
		   unsigned long possible_crtcs,
		   const struct drm_plane_funcs *funcs,
		   const uint32_t *formats, unsigned int format_count,
		   bool is_primary)
{
	enum drm_plane_type type;

	type = is_primary ? DRM_PLANE_TYPE_PRIMARY : DRM_PLANE_TYPE_OVERLAY;
	return drm_universal_plane_init(dev, plane, possible_crtcs, funcs,
					formats, format_count, type, NULL);
}
EXPORT_SYMBOL(drm_plane_init);

/**
 * drm_plane_cleanup - Clean up the core plane usage
 * @plane: plane to cleanup
 *
 * This function cleans up @plane and removes it from the DRM mode setting
 * core. Note that the function does *not* free the plane structure itself,
 * this is the responsibility of the caller.
 */
void drm_plane_cleanup(struct drm_plane *plane)
{
	struct drm_device *dev = plane->dev;

	drm_modeset_lock_all(dev);
	kfree(plane->format_types);
	drm_mode_object_unregister(dev, &plane->base);

	BUG_ON(list_empty(&plane->head));

	/* Note that the plane_list is considered to be static; should we
	 * remove the drm_plane at runtime we would have to decrement all
	 * the indices on the drm_plane after us in the plane_list.
	 */

	list_del(&plane->head);
	dev->mode_config.num_total_plane--;
	if (plane->type == DRM_PLANE_TYPE_OVERLAY)
		dev->mode_config.num_overlay_plane--;
	drm_modeset_unlock_all(dev);

	WARN_ON(plane->state && !plane->funcs->atomic_destroy_state);
	if (plane->state && plane->funcs->atomic_destroy_state)
		plane->funcs->atomic_destroy_state(plane, plane->state);

	kfree(plane->name);

	memset(plane, 0, sizeof(*plane));
}
EXPORT_SYMBOL(drm_plane_cleanup);

/**
 * drm_plane_from_index - find the registered plane at an index
 * @dev: DRM device
 * @idx: index of registered plane to find for
 *
 * Given a plane index, return the registered plane from DRM device's
 * list of planes with matching index.
 */
struct drm_plane *
drm_plane_from_index(struct drm_device *dev, int idx)
{
	struct drm_plane *plane;

	drm_for_each_plane(plane, dev)
		if (idx == plane->index)
			return plane;

	return NULL;
}
EXPORT_SYMBOL(drm_plane_from_index);

/**
 * drm_plane_force_disable - Forcibly disable a plane
 * @plane: plane to disable
 *
 * Forces the plane to be disabled.
 *
 * Used when the plane's current framebuffer is destroyed,
 * and when restoring fbdev mode.
 */
void drm_plane_force_disable(struct drm_plane *plane)
{
	int ret;

	if (!plane->fb)
		return;

	plane->old_fb = plane->fb;
	ret = plane->funcs->disable_plane(plane);
	if (ret) {
		DRM_ERROR("failed to disable plane with busy fb\n");
		plane->old_fb = NULL;
		return;
	}
	/* disconnect the plane from the fb and crtc: */
	drm_framebuffer_unreference(plane->old_fb);
	plane->old_fb = NULL;
	plane->fb = NULL;
	plane->crtc = NULL;
}
EXPORT_SYMBOL(drm_plane_force_disable);

int drm_modeset_register_all(struct drm_device *dev)
{
	int ret;

	ret = drm_plane_register_all(dev);
	if (ret)
		goto err_plane;

	ret = drm_crtc_register_all(dev);
	if  (ret)
		goto err_crtc;

	ret = drm_encoder_register_all(dev);
	if (ret)
		goto err_encoder;

	ret = drm_connector_register_all(dev);
	if (ret)
		goto err_connector;

	return 0;

err_connector:
	drm_encoder_unregister_all(dev);
err_encoder:
	drm_crtc_unregister_all(dev);
err_crtc:
	drm_plane_unregister_all(dev);
err_plane:
	return ret;
}

void drm_modeset_unregister_all(struct drm_device *dev)
{
	drm_connector_unregister_all(dev);
	drm_encoder_unregister_all(dev);
	drm_crtc_unregister_all(dev);
	drm_plane_unregister_all(dev);
}

static int drm_mode_create_standard_properties(struct drm_device *dev)
{
	struct drm_property *prop;

	/*
	 * Standard properties (apply to all connectors)
	 */
	prop = drm_property_create(dev, DRM_MODE_PROP_BLOB |
				   DRM_MODE_PROP_IMMUTABLE,
				   "EDID", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.edid_property = prop;

	prop = drm_property_create_enum(dev, 0,
				   "DPMS", drm_dpms_enum_list,
				   ARRAY_SIZE(drm_dpms_enum_list));
	if (!prop)
		return -ENOMEM;
	dev->mode_config.dpms_property = prop;

	prop = drm_property_create(dev,
				   DRM_MODE_PROP_BLOB |
				   DRM_MODE_PROP_IMMUTABLE,
				   "PATH", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.path_property = prop;

	prop = drm_property_create(dev,
				   DRM_MODE_PROP_BLOB |
				   DRM_MODE_PROP_IMMUTABLE,
				   "TILE", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.tile_property = prop;

	prop = drm_property_create_enum(dev, DRM_MODE_PROP_IMMUTABLE,
					"type", drm_plane_type_enum_list,
					ARRAY_SIZE(drm_plane_type_enum_list));
	if (!prop)
		return -ENOMEM;
	dev->mode_config.plane_type_property = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
			"SRC_X", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_src_x = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
			"SRC_Y", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_src_y = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
			"SRC_W", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_src_w = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
			"SRC_H", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_src_h = prop;

	prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
			"CRTC_X", INT_MIN, INT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_crtc_x = prop;

	prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
			"CRTC_Y", INT_MIN, INT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_crtc_y = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
			"CRTC_W", 0, INT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_crtc_w = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
			"CRTC_H", 0, INT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_crtc_h = prop;

	prop = drm_property_create_object(dev, DRM_MODE_PROP_ATOMIC,
			"FB_ID", DRM_MODE_OBJECT_FB);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_fb_id = prop;

	prop = drm_property_create_object(dev, DRM_MODE_PROP_ATOMIC,
			"CRTC_ID", DRM_MODE_OBJECT_CRTC);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_crtc_id = prop;

	prop = drm_property_create_bool(dev, DRM_MODE_PROP_ATOMIC,
			"ACTIVE");
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_active = prop;

	prop = drm_property_create(dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"MODE_ID", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_mode_id = prop;

	prop = drm_property_create(dev,
			DRM_MODE_PROP_BLOB,
			"DEGAMMA_LUT", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.degamma_lut_property = prop;

	prop = drm_property_create_range(dev,
			DRM_MODE_PROP_IMMUTABLE,
			"DEGAMMA_LUT_SIZE", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.degamma_lut_size_property = prop;

	prop = drm_property_create(dev,
			DRM_MODE_PROP_BLOB,
			"CTM", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.ctm_property = prop;

	prop = drm_property_create(dev,
			DRM_MODE_PROP_BLOB,
			"GAMMA_LUT", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.gamma_lut_property = prop;

	prop = drm_property_create_range(dev,
			DRM_MODE_PROP_IMMUTABLE,
			"GAMMA_LUT_SIZE", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.gamma_lut_size_property = prop;

	return 0;
}

/**
 * drm_mode_create_dvi_i_properties - create DVI-I specific connector properties
 * @dev: DRM device
 *
 * Called by a driver the first time a DVI-I connector is made.
 */
int drm_mode_create_dvi_i_properties(struct drm_device *dev)
{
	struct drm_property *dvi_i_selector;
	struct drm_property *dvi_i_subconnector;

	if (dev->mode_config.dvi_i_select_subconnector_property)
		return 0;

	dvi_i_selector =
		drm_property_create_enum(dev, 0,
				    "select subconnector",
				    drm_dvi_i_select_enum_list,
				    ARRAY_SIZE(drm_dvi_i_select_enum_list));
	dev->mode_config.dvi_i_select_subconnector_property = dvi_i_selector;

	dvi_i_subconnector = drm_property_create_enum(dev, DRM_MODE_PROP_IMMUTABLE,
				    "subconnector",
				    drm_dvi_i_subconnector_enum_list,
				    ARRAY_SIZE(drm_dvi_i_subconnector_enum_list));
	dev->mode_config.dvi_i_subconnector_property = dvi_i_subconnector;

	return 0;
}
EXPORT_SYMBOL(drm_mode_create_dvi_i_properties);

/**
 * drm_create_tv_properties - create TV specific connector properties
 * @dev: DRM device
 * @num_modes: number of different TV formats (modes) supported
 * @modes: array of pointers to strings containing name of each format
 *
 * Called by a driver's TV initialization routine, this function creates
 * the TV specific connector properties for a given device.  Caller is
 * responsible for allocating a list of format names and passing them to
 * this routine.
 */
int drm_mode_create_tv_properties(struct drm_device *dev,
				  unsigned int num_modes,
				  const char * const modes[])
{
	struct drm_property *tv_selector;
	struct drm_property *tv_subconnector;
	unsigned int i;

	if (dev->mode_config.tv_select_subconnector_property)
		return 0;

	/*
	 * Basic connector properties
	 */
	tv_selector = drm_property_create_enum(dev, 0,
					  "select subconnector",
					  drm_tv_select_enum_list,
					  ARRAY_SIZE(drm_tv_select_enum_list));
	if (!tv_selector)
		goto nomem;

	dev->mode_config.tv_select_subconnector_property = tv_selector;

	tv_subconnector =
		drm_property_create_enum(dev, DRM_MODE_PROP_IMMUTABLE,
				    "subconnector",
				    drm_tv_subconnector_enum_list,
				    ARRAY_SIZE(drm_tv_subconnector_enum_list));
	if (!tv_subconnector)
		goto nomem;
	dev->mode_config.tv_subconnector_property = tv_subconnector;

	/*
	 * Other, TV specific properties: margins & TV modes.
	 */
	dev->mode_config.tv_left_margin_property =
		drm_property_create_range(dev, 0, "left margin", 0, 100);
	if (!dev->mode_config.tv_left_margin_property)
		goto nomem;

	dev->mode_config.tv_right_margin_property =
		drm_property_create_range(dev, 0, "right margin", 0, 100);
	if (!dev->mode_config.tv_right_margin_property)
		goto nomem;

	dev->mode_config.tv_top_margin_property =
		drm_property_create_range(dev, 0, "top margin", 0, 100);
	if (!dev->mode_config.tv_top_margin_property)
		goto nomem;

	dev->mode_config.tv_bottom_margin_property =
		drm_property_create_range(dev, 0, "bottom margin", 0, 100);
	if (!dev->mode_config.tv_bottom_margin_property)
		goto nomem;

	dev->mode_config.tv_mode_property =
		drm_property_create(dev, DRM_MODE_PROP_ENUM,
				    "mode", num_modes);
	if (!dev->mode_config.tv_mode_property)
		goto nomem;

	for (i = 0; i < num_modes; i++)
		drm_property_add_enum(dev->mode_config.tv_mode_property, i,
				      i, modes[i]);

	dev->mode_config.tv_brightness_property =
		drm_property_create_range(dev, 0, "brightness", 0, 100);
	if (!dev->mode_config.tv_brightness_property)
		goto nomem;

	dev->mode_config.tv_contrast_property =
		drm_property_create_range(dev, 0, "contrast", 0, 100);
	if (!dev->mode_config.tv_contrast_property)
		goto nomem;

	dev->mode_config.tv_flicker_reduction_property =
		drm_property_create_range(dev, 0, "flicker reduction", 0, 100);
	if (!dev->mode_config.tv_flicker_reduction_property)
		goto nomem;

	dev->mode_config.tv_overscan_property =
		drm_property_create_range(dev, 0, "overscan", 0, 100);
	if (!dev->mode_config.tv_overscan_property)
		goto nomem;

	dev->mode_config.tv_saturation_property =
		drm_property_create_range(dev, 0, "saturation", 0, 100);
	if (!dev->mode_config.tv_saturation_property)
		goto nomem;

	dev->mode_config.tv_hue_property =
		drm_property_create_range(dev, 0, "hue", 0, 100);
	if (!dev->mode_config.tv_hue_property)
		goto nomem;

	return 0;
nomem:
	return -ENOMEM;
}
EXPORT_SYMBOL(drm_mode_create_tv_properties);

/**
 * drm_mode_create_scaling_mode_property - create scaling mode property
 * @dev: DRM device
 *
 * Called by a driver the first time it's needed, must be attached to desired
 * connectors.
 */
int drm_mode_create_scaling_mode_property(struct drm_device *dev)
{
	struct drm_property *scaling_mode;

	if (dev->mode_config.scaling_mode_property)
		return 0;

	scaling_mode =
		drm_property_create_enum(dev, 0, "scaling mode",
				drm_scaling_mode_enum_list,
				    ARRAY_SIZE(drm_scaling_mode_enum_list));

	dev->mode_config.scaling_mode_property = scaling_mode;

	return 0;
}
EXPORT_SYMBOL(drm_mode_create_scaling_mode_property);

/**
 * drm_mode_create_aspect_ratio_property - create aspect ratio property
 * @dev: DRM device
 *
 * Called by a driver the first time it's needed, must be attached to desired
 * connectors.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_create_aspect_ratio_property(struct drm_device *dev)
{
	if (dev->mode_config.aspect_ratio_property)
		return 0;

	dev->mode_config.aspect_ratio_property =
		drm_property_create_enum(dev, 0, "aspect ratio",
				drm_aspect_ratio_enum_list,
				ARRAY_SIZE(drm_aspect_ratio_enum_list));

	if (dev->mode_config.aspect_ratio_property == NULL)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(drm_mode_create_aspect_ratio_property);

/**
 * drm_mode_create_suggested_offset_properties - create suggests offset properties
 * @dev: DRM device
 *
 * Create the the suggested x/y offset property for connectors.
 */
int drm_mode_create_suggested_offset_properties(struct drm_device *dev)
{
	if (dev->mode_config.suggested_x_property && dev->mode_config.suggested_y_property)
		return 0;

	dev->mode_config.suggested_x_property =
		drm_property_create_range(dev, DRM_MODE_PROP_IMMUTABLE, "suggested X", 0, 0xffffffff);

	dev->mode_config.suggested_y_property =
		drm_property_create_range(dev, DRM_MODE_PROP_IMMUTABLE, "suggested Y", 0, 0xffffffff);

	if (dev->mode_config.suggested_x_property == NULL ||
	    dev->mode_config.suggested_y_property == NULL)
		return -ENOMEM;
	return 0;
}
EXPORT_SYMBOL(drm_mode_create_suggested_offset_properties);

/**
 * drm_mode_getresources - get graphics configuration
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Construct a set of configuration description structures and return
 * them to the user, including CRTC, connector and framebuffer configuration.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_getresources(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_mode_card_res *card_res = data;
	struct list_head *lh;
	struct drm_framebuffer *fb;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	int ret = 0;
	int connector_count = 0;
	int crtc_count = 0;
	int fb_count = 0;
	int encoder_count = 0;
	int copied = 0;
	uint32_t __user *fb_id;
	uint32_t __user *crtc_id;
	uint32_t __user *connector_id;
	uint32_t __user *encoder_id;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;


	mutex_lock(&file_priv->fbs_lock);
	/*
	 * For the non-control nodes we need to limit the list of resources
	 * by IDs in the group list for this node
	 */
	list_for_each(lh, &file_priv->fbs)
		fb_count++;

	/* handle this in 4 parts */
	/* FBs */
	if (card_res->count_fbs >= fb_count) {
		copied = 0;
		fb_id = (uint32_t __user *)(unsigned long)card_res->fb_id_ptr;
		list_for_each_entry(fb, &file_priv->fbs, filp_head) {
			if (put_user(fb->base.id, fb_id + copied)) {
				mutex_unlock(&file_priv->fbs_lock);
				return -EFAULT;
			}
			copied++;
		}
	}
	card_res->count_fbs = fb_count;
	mutex_unlock(&file_priv->fbs_lock);

	/* mode_config.mutex protects the connector list against e.g. DP MST
	 * connector hot-adding. CRTC/Plane lists are invariant. */
	mutex_lock(&dev->mode_config.mutex);
	drm_for_each_crtc(crtc, dev)
		crtc_count++;

	drm_for_each_connector(connector, dev)
		connector_count++;

	drm_for_each_encoder(encoder, dev)
		encoder_count++;

	card_res->max_height = dev->mode_config.max_height;
	card_res->min_height = dev->mode_config.min_height;
	card_res->max_width = dev->mode_config.max_width;
	card_res->min_width = dev->mode_config.min_width;

	/* CRTCs */
	if (card_res->count_crtcs >= crtc_count) {
		copied = 0;
		crtc_id = (uint32_t __user *)(unsigned long)card_res->crtc_id_ptr;
		drm_for_each_crtc(crtc, dev) {
			if (put_user(crtc->base.id, crtc_id + copied)) {
				ret = -EFAULT;
				goto out;
			}
			copied++;
		}
	}
	card_res->count_crtcs = crtc_count;

	/* Encoders */
	if (card_res->count_encoders >= encoder_count) {
		copied = 0;
		encoder_id = (uint32_t __user *)(unsigned long)card_res->encoder_id_ptr;
		drm_for_each_encoder(encoder, dev) {
			if (put_user(encoder->base.id, encoder_id +
				     copied)) {
				ret = -EFAULT;
				goto out;
			}
			copied++;
		}
	}
	card_res->count_encoders = encoder_count;

	/* Connectors */
	if (card_res->count_connectors >= connector_count) {
		copied = 0;
		connector_id = (uint32_t __user *)(unsigned long)card_res->connector_id_ptr;
		drm_for_each_connector(connector, dev) {
			if (put_user(connector->base.id,
				     connector_id + copied)) {
				ret = -EFAULT;
				goto out;
			}
			copied++;
		}
	}
	card_res->count_connectors = connector_count;

out:
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}

/**
 * drm_mode_getcrtc - get CRTC configuration
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Construct a CRTC configuration structure to return to the user.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_getcrtc(struct drm_device *dev,
		     void *data, struct drm_file *file_priv)
{
	struct drm_mode_crtc *crtc_resp = data;
	struct drm_crtc *crtc;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	crtc = drm_crtc_find(dev, crtc_resp->crtc_id);
	if (!crtc)
		return -ENOENT;

	drm_modeset_lock_crtc(crtc, crtc->primary);
	crtc_resp->gamma_size = crtc->gamma_size;
	if (crtc->primary->fb)
		crtc_resp->fb_id = crtc->primary->fb->base.id;
	else
		crtc_resp->fb_id = 0;

	if (crtc->state) {
		crtc_resp->x = crtc->primary->state->src_x >> 16;
		crtc_resp->y = crtc->primary->state->src_y >> 16;
		if (crtc->state->enable) {
			drm_mode_convert_to_umode(&crtc_resp->mode, &crtc->state->mode);
			crtc_resp->mode_valid = 1;

		} else {
			crtc_resp->mode_valid = 0;
		}
	} else {
		crtc_resp->x = crtc->x;
		crtc_resp->y = crtc->y;
		if (crtc->enabled) {
			drm_mode_convert_to_umode(&crtc_resp->mode, &crtc->mode);
			crtc_resp->mode_valid = 1;

		} else {
			crtc_resp->mode_valid = 0;
		}
	}
	drm_modeset_unlock_crtc(crtc);

	return 0;
}

static bool drm_mode_expose_to_userspace(const struct drm_display_mode *mode,
					 const struct drm_file *file_priv)
{
	/*
	 * If user-space hasn't configured the driver to expose the stereo 3D
	 * modes, don't expose them.
	 */
	if (!file_priv->stereo_allowed && drm_mode_is_stereo(mode))
		return false;

	return true;
}

static struct drm_encoder *drm_connector_get_encoder(struct drm_connector *connector)
{
	/* For atomic drivers only state objects are synchronously updated and
	 * protected by modeset locks, so check those first. */
	if (connector->state)
		return connector->state->best_encoder;
	return connector->encoder;
}

/* helper for getconnector and getproperties ioctls */
static int get_properties(struct drm_mode_object *obj, bool atomic,
		uint32_t __user *prop_ptr, uint64_t __user *prop_values,
		uint32_t *arg_count_props)
{
	int props_count;
	int i, ret, copied;

	props_count = obj->properties->count;
	if (!atomic)
		props_count -= obj->properties->atomic_count;

	if ((*arg_count_props >= props_count) && props_count) {
		for (i = 0, copied = 0; copied < props_count; i++) {
			struct drm_property *prop = obj->properties->properties[i];
			uint64_t val;

			if ((prop->flags & DRM_MODE_PROP_ATOMIC) && !atomic)
				continue;

			ret = drm_object_property_get_value(obj, prop, &val);
			if (ret)
				return ret;

			if (put_user(prop->base.id, prop_ptr + copied))
				return -EFAULT;

			if (put_user(val, prop_values + copied))
				return -EFAULT;

			copied++;
		}
	}
	*arg_count_props = props_count;

	return 0;
}

/**
 * drm_mode_getconnector - get connector configuration
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Construct a connector configuration structure to return to the user.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_getconnector(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_mode_get_connector *out_resp = data;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_display_mode *mode;
	int mode_count = 0;
	int encoders_count = 0;
	int ret = 0;
	int copied = 0;
	int i;
	struct drm_mode_modeinfo u_mode;
	struct drm_mode_modeinfo __user *mode_ptr;
	uint32_t __user *encoder_ptr;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	memset(&u_mode, 0, sizeof(struct drm_mode_modeinfo));

	mutex_lock(&dev->mode_config.mutex);

	connector = drm_connector_lookup(dev, out_resp->connector_id);
	if (!connector) {
		ret = -ENOENT;
		goto out_unlock;
	}

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++)
		if (connector->encoder_ids[i] != 0)
			encoders_count++;

	if (out_resp->count_modes == 0) {
		connector->funcs->fill_modes(connector,
					     dev->mode_config.max_width,
					     dev->mode_config.max_height);
	}

	/* delayed so we get modes regardless of pre-fill_modes state */
	list_for_each_entry(mode, &connector->modes, head)
		if (drm_mode_expose_to_userspace(mode, file_priv))
			mode_count++;

	out_resp->connector_id = connector->base.id;
	out_resp->connector_type = connector->connector_type;
	out_resp->connector_type_id = connector->connector_type_id;
	out_resp->mm_width = connector->display_info.width_mm;
	out_resp->mm_height = connector->display_info.height_mm;
	out_resp->subpixel = connector->display_info.subpixel_order;
	out_resp->connection = connector->status;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	encoder = drm_connector_get_encoder(connector);
	if (encoder)
		out_resp->encoder_id = encoder->base.id;
	else
		out_resp->encoder_id = 0;

	/*
	 * This ioctl is called twice, once to determine how much space is
	 * needed, and the 2nd time to fill it.
	 */
	if ((out_resp->count_modes >= mode_count) && mode_count) {
		copied = 0;
		mode_ptr = (struct drm_mode_modeinfo __user *)(unsigned long)out_resp->modes_ptr;
		list_for_each_entry(mode, &connector->modes, head) {
			if (!drm_mode_expose_to_userspace(mode, file_priv))
				continue;

			drm_mode_convert_to_umode(&u_mode, mode);
			if (copy_to_user(mode_ptr + copied,
					 &u_mode, sizeof(u_mode))) {
				ret = -EFAULT;
				goto out;
			}
			copied++;
		}
	}
	out_resp->count_modes = mode_count;

	ret = get_properties(&connector->base, file_priv->atomic,
			(uint32_t __user *)(unsigned long)(out_resp->props_ptr),
			(uint64_t __user *)(unsigned long)(out_resp->prop_values_ptr),
			&out_resp->count_props);
	if (ret)
		goto out;

	if ((out_resp->count_encoders >= encoders_count) && encoders_count) {
		copied = 0;
		encoder_ptr = (uint32_t __user *)(unsigned long)(out_resp->encoders_ptr);
		for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
			if (connector->encoder_ids[i] != 0) {
				if (put_user(connector->encoder_ids[i],
					     encoder_ptr + copied)) {
					ret = -EFAULT;
					goto out;
				}
				copied++;
			}
		}
	}
	out_resp->count_encoders = encoders_count;

out:
	drm_modeset_unlock(&dev->mode_config.connection_mutex);

	drm_connector_unreference(connector);
out_unlock:
	mutex_unlock(&dev->mode_config.mutex);

	return ret;
}

static struct drm_crtc *drm_encoder_get_crtc(struct drm_encoder *encoder)
{
	struct drm_connector *connector;
	struct drm_device *dev = encoder->dev;
	bool uses_atomic = false;

	/* For atomic drivers only state objects are synchronously updated and
	 * protected by modeset locks, so check those first. */
	drm_for_each_connector(connector, dev) {
		if (!connector->state)
			continue;

		uses_atomic = true;

		if (connector->state->best_encoder != encoder)
			continue;

		return connector->state->crtc;
	}

	/* Don't return stale data (e.g. pending async disable). */
	if (uses_atomic)
		return NULL;

	return encoder->crtc;
}

/**
 * drm_mode_getencoder - get encoder configuration
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Construct a encoder configuration structure to return to the user.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_getencoder(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_mode_get_encoder *enc_resp = data;
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	encoder = drm_encoder_find(dev, enc_resp->encoder_id);
	if (!encoder)
		return -ENOENT;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	crtc = drm_encoder_get_crtc(encoder);
	if (crtc)
		enc_resp->crtc_id = crtc->base.id;
	else
		enc_resp->crtc_id = 0;
	drm_modeset_unlock(&dev->mode_config.connection_mutex);

	enc_resp->encoder_type = encoder->encoder_type;
	enc_resp->encoder_id = encoder->base.id;
	enc_resp->possible_crtcs = encoder->possible_crtcs;
	enc_resp->possible_clones = encoder->possible_clones;

	return 0;
}

/**
 * drm_mode_getplane_res - enumerate all plane resources
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * Construct a list of plane ids to return to the user.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_getplane_res(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_mode_get_plane_res *plane_resp = data;
	struct drm_mode_config *config;
	struct drm_plane *plane;
	uint32_t __user *plane_ptr;
	int copied = 0;
	unsigned num_planes;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	config = &dev->mode_config;

	if (file_priv->universal_planes)
		num_planes = config->num_total_plane;
	else
		num_planes = config->num_overlay_plane;

	/*
	 * This ioctl is called twice, once to determine how much space is
	 * needed, and the 2nd time to fill it.
	 */
	if (num_planes &&
	    (plane_resp->count_planes >= num_planes)) {
		plane_ptr = (uint32_t __user *)(unsigned long)plane_resp->plane_id_ptr;

		/* Plane lists are invariant, no locking needed. */
		drm_for_each_plane(plane, dev) {
			/*
			 * Unless userspace set the 'universal planes'
			 * capability bit, only advertise overlays.
			 */
			if (plane->type != DRM_PLANE_TYPE_OVERLAY &&
			    !file_priv->universal_planes)
				continue;

			if (put_user(plane->base.id, plane_ptr + copied))
				return -EFAULT;
			copied++;
		}
	}
	plane_resp->count_planes = num_planes;

	return 0;
}

/**
 * drm_mode_getplane - get plane configuration
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * Construct a plane configuration structure to return to the user.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_getplane(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_mode_get_plane *plane_resp = data;
	struct drm_plane *plane;
	uint32_t __user *format_ptr;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	plane = drm_plane_find(dev, plane_resp->plane_id);
	if (!plane)
		return -ENOENT;

	drm_modeset_lock(&plane->mutex, NULL);
	if (plane->crtc)
		plane_resp->crtc_id = plane->crtc->base.id;
	else
		plane_resp->crtc_id = 0;

	if (plane->fb)
		plane_resp->fb_id = plane->fb->base.id;
	else
		plane_resp->fb_id = 0;
	drm_modeset_unlock(&plane->mutex);

	plane_resp->plane_id = plane->base.id;
	plane_resp->possible_crtcs = plane->possible_crtcs;
	plane_resp->gamma_size = 0;

	/*
	 * This ioctl is called twice, once to determine how much space is
	 * needed, and the 2nd time to fill it.
	 */
	if (plane->format_count &&
	    (plane_resp->count_format_types >= plane->format_count)) {
		format_ptr = (uint32_t __user *)(unsigned long)plane_resp->format_type_ptr;
		if (copy_to_user(format_ptr,
				 plane->format_types,
				 sizeof(uint32_t) * plane->format_count)) {
			return -EFAULT;
		}
	}
	plane_resp->count_format_types = plane->format_count;

	return 0;
}

/**
 * drm_plane_check_pixel_format - Check if the plane supports the pixel format
 * @plane: plane to check for format support
 * @format: the pixel format
 *
 * Returns:
 * Zero of @plane has @format in its list of supported pixel formats, -EINVAL
 * otherwise.
 */
int drm_plane_check_pixel_format(const struct drm_plane *plane, u32 format)
{
	unsigned int i;

	for (i = 0; i < plane->format_count; i++) {
		if (format == plane->format_types[i])
			return 0;
	}

	return -EINVAL;
}

static int check_src_coords(uint32_t src_x, uint32_t src_y,
			    uint32_t src_w, uint32_t src_h,
			    const struct drm_framebuffer *fb)
{
	unsigned int fb_width, fb_height;

	fb_width = fb->width << 16;
	fb_height = fb->height << 16;

	/* Make sure source coordinates are inside the fb. */
	if (src_w > fb_width ||
	    src_x > fb_width - src_w ||
	    src_h > fb_height ||
	    src_y > fb_height - src_h) {
		DRM_DEBUG_KMS("Invalid source coordinates "
			      "%u.%06ux%u.%06u+%u.%06u+%u.%06u\n",
			      src_w >> 16, ((src_w & 0xffff) * 15625) >> 10,
			      src_h >> 16, ((src_h & 0xffff) * 15625) >> 10,
			      src_x >> 16, ((src_x & 0xffff) * 15625) >> 10,
			      src_y >> 16, ((src_y & 0xffff) * 15625) >> 10);
		return -ENOSPC;
	}

	return 0;
}

/*
 * setplane_internal - setplane handler for internal callers
 *
 * Note that we assume an extra reference has already been taken on fb.  If the
 * update fails, this reference will be dropped before return; if it succeeds,
 * the previous framebuffer (if any) will be unreferenced instead.
 *
 * src_{x,y,w,h} are provided in 16.16 fixed point format
 */
static int __setplane_internal(struct drm_plane *plane,
			       struct drm_crtc *crtc,
			       struct drm_framebuffer *fb,
			       int32_t crtc_x, int32_t crtc_y,
			       uint32_t crtc_w, uint32_t crtc_h,
			       /* src_{x,y,w,h} values are 16.16 fixed point */
			       uint32_t src_x, uint32_t src_y,
			       uint32_t src_w, uint32_t src_h)
{
	int ret = 0;

	/* No fb means shut it down */
	if (!fb) {
		plane->old_fb = plane->fb;
		ret = plane->funcs->disable_plane(plane);
		if (!ret) {
			plane->crtc = NULL;
			plane->fb = NULL;
		} else {
			plane->old_fb = NULL;
		}
		goto out;
	}

	/* Check whether this plane is usable on this CRTC */
	if (!(plane->possible_crtcs & drm_crtc_mask(crtc))) {
		DRM_DEBUG_KMS("Invalid crtc for plane\n");
		ret = -EINVAL;
		goto out;
	}

	/* Check whether this plane supports the fb pixel format. */
	ret = drm_plane_check_pixel_format(plane, fb->pixel_format);
	if (ret) {
		char *format_name = drm_get_format_name(fb->pixel_format);
		DRM_DEBUG_KMS("Invalid pixel format %s\n", format_name);
		kfree(format_name);
		goto out;
	}

	/* Give drivers some help against integer overflows */
	if (crtc_w > INT_MAX ||
	    crtc_x > INT_MAX - (int32_t) crtc_w ||
	    crtc_h > INT_MAX ||
	    crtc_y > INT_MAX - (int32_t) crtc_h) {
		DRM_DEBUG_KMS("Invalid CRTC coordinates %ux%u+%d+%d\n",
			      crtc_w, crtc_h, crtc_x, crtc_y);
		ret = -ERANGE;
		goto out;
	}

	ret = check_src_coords(src_x, src_y, src_w, src_h, fb);
	if (ret)
		goto out;

	plane->old_fb = plane->fb;
	ret = plane->funcs->update_plane(plane, crtc, fb,
					 crtc_x, crtc_y, crtc_w, crtc_h,
					 src_x, src_y, src_w, src_h);
	if (!ret) {
		plane->crtc = crtc;
		plane->fb = fb;
		fb = NULL;
	} else {
		plane->old_fb = NULL;
	}

out:
	if (fb)
		drm_framebuffer_unreference(fb);
	if (plane->old_fb)
		drm_framebuffer_unreference(plane->old_fb);
	plane->old_fb = NULL;

	return ret;
}

static int setplane_internal(struct drm_plane *plane,
			     struct drm_crtc *crtc,
			     struct drm_framebuffer *fb,
			     int32_t crtc_x, int32_t crtc_y,
			     uint32_t crtc_w, uint32_t crtc_h,
			     /* src_{x,y,w,h} values are 16.16 fixed point */
			     uint32_t src_x, uint32_t src_y,
			     uint32_t src_w, uint32_t src_h)
{
	int ret;

	drm_modeset_lock_all(plane->dev);
	ret = __setplane_internal(plane, crtc, fb,
				  crtc_x, crtc_y, crtc_w, crtc_h,
				  src_x, src_y, src_w, src_h);
	drm_modeset_unlock_all(plane->dev);

	return ret;
}

/**
 * drm_mode_setplane - configure a plane's configuration
 * @dev: DRM device
 * @data: ioctl data*
 * @file_priv: DRM file info
 *
 * Set plane configuration, including placement, fb, scaling, and other factors.
 * Or pass a NULL fb to disable (planes may be disabled without providing a
 * valid crtc).
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_setplane(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_mode_set_plane *plane_req = data;
	struct drm_plane *plane;
	struct drm_crtc *crtc = NULL;
	struct drm_framebuffer *fb = NULL;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	/*
	 * First, find the plane, crtc, and fb objects.  If not available,
	 * we don't bother to call the driver.
	 */
	plane = drm_plane_find(dev, plane_req->plane_id);
	if (!plane) {
		DRM_DEBUG_KMS("Unknown plane ID %d\n",
			      plane_req->plane_id);
		return -ENOENT;
	}

	if (plane_req->fb_id) {
		fb = drm_framebuffer_lookup(dev, plane_req->fb_id);
		if (!fb) {
			DRM_DEBUG_KMS("Unknown framebuffer ID %d\n",
				      plane_req->fb_id);
			return -ENOENT;
		}

		crtc = drm_crtc_find(dev, plane_req->crtc_id);
		if (!crtc) {
			DRM_DEBUG_KMS("Unknown crtc ID %d\n",
				      plane_req->crtc_id);
			return -ENOENT;
		}
	}

	/*
	 * setplane_internal will take care of deref'ing either the old or new
	 * framebuffer depending on success.
	 */
	return setplane_internal(plane, crtc, fb,
				 plane_req->crtc_x, plane_req->crtc_y,
				 plane_req->crtc_w, plane_req->crtc_h,
				 plane_req->src_x, plane_req->src_y,
				 plane_req->src_w, plane_req->src_h);
}

/**
 * drm_mode_set_config_internal - helper to call ->set_config
 * @set: modeset config to set
 *
 * This is a little helper to wrap internal calls to the ->set_config driver
 * interface. The only thing it adds is correct refcounting dance.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_set_config_internal(struct drm_mode_set *set)
{
	struct drm_crtc *crtc = set->crtc;
	struct drm_framebuffer *fb;
	struct drm_crtc *tmp;
	int ret;

	/*
	 * NOTE: ->set_config can also disable other crtcs (if we steal all
	 * connectors from it), hence we need to refcount the fbs across all
	 * crtcs. Atomic modeset will have saner semantics ...
	 */
	drm_for_each_crtc(tmp, crtc->dev)
		tmp->primary->old_fb = tmp->primary->fb;

	fb = set->fb;

	ret = crtc->funcs->set_config(set);
	if (ret == 0) {
		crtc->primary->crtc = crtc;
		crtc->primary->fb = fb;
	}

	drm_for_each_crtc(tmp, crtc->dev) {
		if (tmp->primary->fb)
			drm_framebuffer_reference(tmp->primary->fb);
		if (tmp->primary->old_fb)
			drm_framebuffer_unreference(tmp->primary->old_fb);
		tmp->primary->old_fb = NULL;
	}

	return ret;
}
EXPORT_SYMBOL(drm_mode_set_config_internal);

/**
 * drm_crtc_get_hv_timing - Fetches hdisplay/vdisplay for given mode
 * @mode: mode to query
 * @hdisplay: hdisplay value to fill in
 * @vdisplay: vdisplay value to fill in
 *
 * The vdisplay value will be doubled if the specified mode is a stereo mode of
 * the appropriate layout.
 */
void drm_crtc_get_hv_timing(const struct drm_display_mode *mode,
			    int *hdisplay, int *vdisplay)
{
	struct drm_display_mode adjusted;

	drm_mode_copy(&adjusted, mode);
	drm_mode_set_crtcinfo(&adjusted, CRTC_STEREO_DOUBLE_ONLY);
	*hdisplay = adjusted.crtc_hdisplay;
	*vdisplay = adjusted.crtc_vdisplay;
}
EXPORT_SYMBOL(drm_crtc_get_hv_timing);

/**
 * drm_crtc_check_viewport - Checks that a framebuffer is big enough for the
 *     CRTC viewport
 * @crtc: CRTC that framebuffer will be displayed on
 * @x: x panning
 * @y: y panning
 * @mode: mode that framebuffer will be displayed under
 * @fb: framebuffer to check size of
 */
int drm_crtc_check_viewport(const struct drm_crtc *crtc,
			    int x, int y,
			    const struct drm_display_mode *mode,
			    const struct drm_framebuffer *fb)

{
	int hdisplay, vdisplay;

	drm_crtc_get_hv_timing(mode, &hdisplay, &vdisplay);

	if (crtc->state &&
	    crtc->primary->state->rotation & (DRM_ROTATE_90 |
					      DRM_ROTATE_270))
		swap(hdisplay, vdisplay);

	return check_src_coords(x << 16, y << 16,
				hdisplay << 16, vdisplay << 16, fb);
}
EXPORT_SYMBOL(drm_crtc_check_viewport);

/**
 * drm_mode_setcrtc - set CRTC configuration
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Build a new CRTC configuration based on user request.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_setcrtc(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_mode_crtc *crtc_req = data;
	struct drm_crtc *crtc;
	struct drm_connector **connector_set = NULL, *connector;
	struct drm_framebuffer *fb = NULL;
	struct drm_display_mode *mode = NULL;
	struct drm_mode_set set;
	uint32_t __user *set_connectors_ptr;
	int ret;
	int i;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	/*
	 * Universal plane src offsets are only 16.16, prevent havoc for
	 * drivers using universal plane code internally.
	 */
	if (crtc_req->x & 0xffff0000 || crtc_req->y & 0xffff0000)
		return -ERANGE;

	drm_modeset_lock_all(dev);
	crtc = drm_crtc_find(dev, crtc_req->crtc_id);
	if (!crtc) {
		DRM_DEBUG_KMS("Unknown CRTC ID %d\n", crtc_req->crtc_id);
		ret = -ENOENT;
		goto out;
	}
	DRM_DEBUG_KMS("[CRTC:%d:%s]\n", crtc->base.id, crtc->name);

	if (crtc_req->mode_valid) {
		/* If we have a mode we need a framebuffer. */
		/* If we pass -1, set the mode with the currently bound fb */
		if (crtc_req->fb_id == -1) {
			if (!crtc->primary->fb) {
				DRM_DEBUG_KMS("CRTC doesn't have current FB\n");
				ret = -EINVAL;
				goto out;
			}
			fb = crtc->primary->fb;
			/* Make refcounting symmetric with the lookup path. */
			drm_framebuffer_reference(fb);
		} else {
			fb = drm_framebuffer_lookup(dev, crtc_req->fb_id);
			if (!fb) {
				DRM_DEBUG_KMS("Unknown FB ID%d\n",
						crtc_req->fb_id);
				ret = -ENOENT;
				goto out;
			}
		}

		mode = drm_mode_create(dev);
		if (!mode) {
			ret = -ENOMEM;
			goto out;
		}

		ret = drm_mode_convert_umode(mode, &crtc_req->mode);
		if (ret) {
			DRM_DEBUG_KMS("Invalid mode\n");
			goto out;
		}

		/*
		 * Check whether the primary plane supports the fb pixel format.
		 * Drivers not implementing the universal planes API use a
		 * default formats list provided by the DRM core which doesn't
		 * match real hardware capabilities. Skip the check in that
		 * case.
		 */
		if (!crtc->primary->format_default) {
			ret = drm_plane_check_pixel_format(crtc->primary,
							   fb->pixel_format);
			if (ret) {
				char *format_name = drm_get_format_name(fb->pixel_format);
				DRM_DEBUG_KMS("Invalid pixel format %s\n", format_name);
				kfree(format_name);
				goto out;
			}
		}

		ret = drm_crtc_check_viewport(crtc, crtc_req->x, crtc_req->y,
					      mode, fb);
		if (ret)
			goto out;

	}

	if (crtc_req->count_connectors == 0 && mode) {
		DRM_DEBUG_KMS("Count connectors is 0 but mode set\n");
		ret = -EINVAL;
		goto out;
	}

	if (crtc_req->count_connectors > 0 && (!mode || !fb)) {
		DRM_DEBUG_KMS("Count connectors is %d but no mode or fb set\n",
			  crtc_req->count_connectors);
		ret = -EINVAL;
		goto out;
	}

	if (crtc_req->count_connectors > 0) {
		u32 out_id;

		/* Avoid unbounded kernel memory allocation */
		if (crtc_req->count_connectors > config->num_connector) {
			ret = -EINVAL;
			goto out;
		}

		connector_set = kmalloc_array(crtc_req->count_connectors,
					      sizeof(struct drm_connector *),
					      GFP_KERNEL);
		if (!connector_set) {
			ret = -ENOMEM;
			goto out;
		}

		for (i = 0; i < crtc_req->count_connectors; i++) {
			connector_set[i] = NULL;
			set_connectors_ptr = (uint32_t __user *)(unsigned long)crtc_req->set_connectors_ptr;
			if (get_user(out_id, &set_connectors_ptr[i])) {
				ret = -EFAULT;
				goto out;
			}

			connector = drm_connector_lookup(dev, out_id);
			if (!connector) {
				DRM_DEBUG_KMS("Connector id %d unknown\n",
						out_id);
				ret = -ENOENT;
				goto out;
			}
			DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
					connector->base.id,
					connector->name);

			connector_set[i] = connector;
		}
	}

	set.crtc = crtc;
	set.x = crtc_req->x;
	set.y = crtc_req->y;
	set.mode = mode;
	set.connectors = connector_set;
	set.num_connectors = crtc_req->count_connectors;
	set.fb = fb;
	ret = drm_mode_set_config_internal(&set);

out:
	if (fb)
		drm_framebuffer_unreference(fb);

	if (connector_set) {
		for (i = 0; i < crtc_req->count_connectors; i++) {
			if (connector_set[i])
				drm_connector_unreference(connector_set[i]);
		}
	}
	kfree(connector_set);
	drm_mode_destroy(dev, mode);
	drm_modeset_unlock_all(dev);
	return ret;
}

/**
 * drm_mode_cursor_universal - translate legacy cursor ioctl call into a
 *     universal plane handler call
 * @crtc: crtc to update cursor for
 * @req: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Legacy cursor ioctl's work directly with driver buffer handles.  To
 * translate legacy ioctl calls into universal plane handler calls, we need to
 * wrap the native buffer handle in a drm_framebuffer.
 *
 * Note that we assume any handle passed to the legacy ioctls was a 32-bit ARGB
 * buffer with a pitch of 4*width; the universal plane interface should be used
 * directly in cases where the hardware can support other buffer settings and
 * userspace wants to make use of these capabilities.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
static int drm_mode_cursor_universal(struct drm_crtc *crtc,
				     struct drm_mode_cursor2 *req,
				     struct drm_file *file_priv)
{
	struct drm_device *dev = crtc->dev;
	struct drm_framebuffer *fb = NULL;
	struct drm_mode_fb_cmd2 fbreq = {
		.width = req->width,
		.height = req->height,
		.pixel_format = DRM_FORMAT_ARGB8888,
		.pitches = { req->width * 4 },
		.handles = { req->handle },
	};
	int32_t crtc_x, crtc_y;
	uint32_t crtc_w = 0, crtc_h = 0;
	uint32_t src_w = 0, src_h = 0;
	int ret = 0;

	BUG_ON(!crtc->cursor);
	WARN_ON(crtc->cursor->crtc != crtc && crtc->cursor->crtc != NULL);

	/*
	 * Obtain fb we'll be using (either new or existing) and take an extra
	 * reference to it if fb != null.  setplane will take care of dropping
	 * the reference if the plane update fails.
	 */
	if (req->flags & DRM_MODE_CURSOR_BO) {
		if (req->handle) {
			fb = internal_framebuffer_create(dev, &fbreq, file_priv);
			if (IS_ERR(fb)) {
				DRM_DEBUG_KMS("failed to wrap cursor buffer in drm framebuffer\n");
				return PTR_ERR(fb);
			}
			fb->hot_x = req->hot_x;
			fb->hot_y = req->hot_y;
		} else {
			fb = NULL;
		}
	} else {
		fb = crtc->cursor->fb;
		if (fb)
			drm_framebuffer_reference(fb);
	}

	if (req->flags & DRM_MODE_CURSOR_MOVE) {
		crtc_x = req->x;
		crtc_y = req->y;
	} else {
		crtc_x = crtc->cursor_x;
		crtc_y = crtc->cursor_y;
	}

	if (fb) {
		crtc_w = fb->width;
		crtc_h = fb->height;
		src_w = fb->width << 16;
		src_h = fb->height << 16;
	}

	/*
	 * setplane_internal will take care of deref'ing either the old or new
	 * framebuffer depending on success.
	 */
	ret = __setplane_internal(crtc->cursor, crtc, fb,
				crtc_x, crtc_y, crtc_w, crtc_h,
				0, 0, src_w, src_h);

	/* Update successful; save new cursor position, if necessary */
	if (ret == 0 && req->flags & DRM_MODE_CURSOR_MOVE) {
		crtc->cursor_x = req->x;
		crtc->cursor_y = req->y;
	}

	return ret;
}

static int drm_mode_cursor_common(struct drm_device *dev,
				  struct drm_mode_cursor2 *req,
				  struct drm_file *file_priv)
{
	struct drm_crtc *crtc;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	if (!req->flags || (~DRM_MODE_CURSOR_FLAGS & req->flags))
		return -EINVAL;

	crtc = drm_crtc_find(dev, req->crtc_id);
	if (!crtc) {
		DRM_DEBUG_KMS("Unknown CRTC ID %d\n", req->crtc_id);
		return -ENOENT;
	}

	/*
	 * If this crtc has a universal cursor plane, call that plane's update
	 * handler rather than using legacy cursor handlers.
	 */
	drm_modeset_lock_crtc(crtc, crtc->cursor);
	if (crtc->cursor) {
		ret = drm_mode_cursor_universal(crtc, req, file_priv);
		goto out;
	}

	if (req->flags & DRM_MODE_CURSOR_BO) {
		if (!crtc->funcs->cursor_set && !crtc->funcs->cursor_set2) {
			ret = -ENXIO;
			goto out;
		}
		/* Turns off the cursor if handle is 0 */
		if (crtc->funcs->cursor_set2)
			ret = crtc->funcs->cursor_set2(crtc, file_priv, req->handle,
						      req->width, req->height, req->hot_x, req->hot_y);
		else
			ret = crtc->funcs->cursor_set(crtc, file_priv, req->handle,
						      req->width, req->height);
	}

	if (req->flags & DRM_MODE_CURSOR_MOVE) {
		if (crtc->funcs->cursor_move) {
			ret = crtc->funcs->cursor_move(crtc, req->x, req->y);
		} else {
			ret = -EFAULT;
			goto out;
		}
	}
out:
	drm_modeset_unlock_crtc(crtc);

	return ret;

}


/**
 * drm_mode_cursor_ioctl - set CRTC's cursor configuration
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Set the cursor configuration based on user request.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_cursor_ioctl(struct drm_device *dev,
			  void *data, struct drm_file *file_priv)
{
	struct drm_mode_cursor *req = data;
	struct drm_mode_cursor2 new_req;

	memcpy(&new_req, req, sizeof(struct drm_mode_cursor));
	new_req.hot_x = new_req.hot_y = 0;

	return drm_mode_cursor_common(dev, &new_req, file_priv);
}

/**
 * drm_mode_cursor2_ioctl - set CRTC's cursor configuration
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Set the cursor configuration based on user request. This implements the 2nd
 * version of the cursor ioctl, which allows userspace to additionally specify
 * the hotspot of the pointer.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_cursor2_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv)
{
	struct drm_mode_cursor2 *req = data;

	return drm_mode_cursor_common(dev, req, file_priv);
}

/**
 * drm_mode_legacy_fb_format - compute drm fourcc code from legacy description
 * @bpp: bits per pixels
 * @depth: bit depth per pixel
 *
 * Computes a drm fourcc pixel format code for the given @bpp/@depth values.
 * Useful in fbdev emulation code, since that deals in those values.
 */
uint32_t drm_mode_legacy_fb_format(uint32_t bpp, uint32_t depth)
{
	uint32_t fmt;

	switch (bpp) {
	case 8:
		fmt = DRM_FORMAT_C8;
		break;
	case 16:
		if (depth == 15)
			fmt = DRM_FORMAT_XRGB1555;
		else
			fmt = DRM_FORMAT_RGB565;
		break;
	case 24:
		fmt = DRM_FORMAT_RGB888;
		break;
	case 32:
		if (depth == 24)
			fmt = DRM_FORMAT_XRGB8888;
		else if (depth == 30)
			fmt = DRM_FORMAT_XRGB2101010;
		else
			fmt = DRM_FORMAT_ARGB8888;
		break;
	default:
		DRM_ERROR("bad bpp, assuming x8r8g8b8 pixel format\n");
		fmt = DRM_FORMAT_XRGB8888;
		break;
	}

	return fmt;
}
EXPORT_SYMBOL(drm_mode_legacy_fb_format);

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

static struct drm_framebuffer *
internal_framebuffer_create(struct drm_device *dev,
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

	fb = internal_framebuffer_create(dev, r, file_priv);
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

static bool drm_property_type_valid(struct drm_property *property)
{
	if (property->flags & DRM_MODE_PROP_EXTENDED_TYPE)
		return !(property->flags & DRM_MODE_PROP_LEGACY_TYPE);
	return !!(property->flags & DRM_MODE_PROP_LEGACY_TYPE);
}

/**
 * drm_property_create - create a new property type
 * @dev: drm device
 * @flags: flags specifying the property type
 * @name: name of the property
 * @num_values: number of pre-defined values
 *
 * This creates a new generic drm property which can then be attached to a drm
 * object with drm_object_attach_property. The returned property object must be
 * freed with drm_property_destroy.
 *
 * Note that the DRM core keeps a per-device list of properties and that, if
 * drm_mode_config_cleanup() is called, it will destroy all properties created
 * by the driver.
 *
 * Returns:
 * A pointer to the newly created property on success, NULL on failure.
 */
struct drm_property *drm_property_create(struct drm_device *dev, int flags,
					 const char *name, int num_values)
{
	struct drm_property *property = NULL;
	int ret;

	property = kzalloc(sizeof(struct drm_property), GFP_KERNEL);
	if (!property)
		return NULL;

	property->dev = dev;

	if (num_values) {
		property->values = kcalloc(num_values, sizeof(uint64_t),
					   GFP_KERNEL);
		if (!property->values)
			goto fail;
	}

	ret = drm_mode_object_get(dev, &property->base, DRM_MODE_OBJECT_PROPERTY);
	if (ret)
		goto fail;

	property->flags = flags;
	property->num_values = num_values;
	INIT_LIST_HEAD(&property->enum_list);

	if (name) {
		strncpy(property->name, name, DRM_PROP_NAME_LEN);
		property->name[DRM_PROP_NAME_LEN-1] = '\0';
	}

	list_add_tail(&property->head, &dev->mode_config.property_list);

	WARN_ON(!drm_property_type_valid(property));

	return property;
fail:
	kfree(property->values);
	kfree(property);
	return NULL;
}
EXPORT_SYMBOL(drm_property_create);

/**
 * drm_property_create_enum - create a new enumeration property type
 * @dev: drm device
 * @flags: flags specifying the property type
 * @name: name of the property
 * @props: enumeration lists with property values
 * @num_values: number of pre-defined values
 *
 * This creates a new generic drm property which can then be attached to a drm
 * object with drm_object_attach_property. The returned property object must be
 * freed with drm_property_destroy.
 *
 * Userspace is only allowed to set one of the predefined values for enumeration
 * properties.
 *
 * Returns:
 * A pointer to the newly created property on success, NULL on failure.
 */
struct drm_property *drm_property_create_enum(struct drm_device *dev, int flags,
					 const char *name,
					 const struct drm_prop_enum_list *props,
					 int num_values)
{
	struct drm_property *property;
	int i, ret;

	flags |= DRM_MODE_PROP_ENUM;

	property = drm_property_create(dev, flags, name, num_values);
	if (!property)
		return NULL;

	for (i = 0; i < num_values; i++) {
		ret = drm_property_add_enum(property, i,
				      props[i].type,
				      props[i].name);
		if (ret) {
			drm_property_destroy(dev, property);
			return NULL;
		}
	}

	return property;
}
EXPORT_SYMBOL(drm_property_create_enum);

/**
 * drm_property_create_bitmask - create a new bitmask property type
 * @dev: drm device
 * @flags: flags specifying the property type
 * @name: name of the property
 * @props: enumeration lists with property bitflags
 * @num_props: size of the @props array
 * @supported_bits: bitmask of all supported enumeration values
 *
 * This creates a new bitmask drm property which can then be attached to a drm
 * object with drm_object_attach_property. The returned property object must be
 * freed with drm_property_destroy.
 *
 * Compared to plain enumeration properties userspace is allowed to set any
 * or'ed together combination of the predefined property bitflag values
 *
 * Returns:
 * A pointer to the newly created property on success, NULL on failure.
 */
struct drm_property *drm_property_create_bitmask(struct drm_device *dev,
					 int flags, const char *name,
					 const struct drm_prop_enum_list *props,
					 int num_props,
					 uint64_t supported_bits)
{
	struct drm_property *property;
	int i, ret, index = 0;
	int num_values = hweight64(supported_bits);

	flags |= DRM_MODE_PROP_BITMASK;

	property = drm_property_create(dev, flags, name, num_values);
	if (!property)
		return NULL;
	for (i = 0; i < num_props; i++) {
		if (!(supported_bits & (1ULL << props[i].type)))
			continue;

		if (WARN_ON(index >= num_values)) {
			drm_property_destroy(dev, property);
			return NULL;
		}

		ret = drm_property_add_enum(property, index++,
				      props[i].type,
				      props[i].name);
		if (ret) {
			drm_property_destroy(dev, property);
			return NULL;
		}
	}

	return property;
}
EXPORT_SYMBOL(drm_property_create_bitmask);

static struct drm_property *property_create_range(struct drm_device *dev,
					 int flags, const char *name,
					 uint64_t min, uint64_t max)
{
	struct drm_property *property;

	property = drm_property_create(dev, flags, name, 2);
	if (!property)
		return NULL;

	property->values[0] = min;
	property->values[1] = max;

	return property;
}

/**
 * drm_property_create_range - create a new unsigned ranged property type
 * @dev: drm device
 * @flags: flags specifying the property type
 * @name: name of the property
 * @min: minimum value of the property
 * @max: maximum value of the property
 *
 * This creates a new generic drm property which can then be attached to a drm
 * object with drm_object_attach_property. The returned property object must be
 * freed with drm_property_destroy.
 *
 * Userspace is allowed to set any unsigned integer value in the (min, max)
 * range inclusive.
 *
 * Returns:
 * A pointer to the newly created property on success, NULL on failure.
 */
struct drm_property *drm_property_create_range(struct drm_device *dev, int flags,
					 const char *name,
					 uint64_t min, uint64_t max)
{
	return property_create_range(dev, DRM_MODE_PROP_RANGE | flags,
			name, min, max);
}
EXPORT_SYMBOL(drm_property_create_range);

/**
 * drm_property_create_signed_range - create a new signed ranged property type
 * @dev: drm device
 * @flags: flags specifying the property type
 * @name: name of the property
 * @min: minimum value of the property
 * @max: maximum value of the property
 *
 * This creates a new generic drm property which can then be attached to a drm
 * object with drm_object_attach_property. The returned property object must be
 * freed with drm_property_destroy.
 *
 * Userspace is allowed to set any signed integer value in the (min, max)
 * range inclusive.
 *
 * Returns:
 * A pointer to the newly created property on success, NULL on failure.
 */
struct drm_property *drm_property_create_signed_range(struct drm_device *dev,
					 int flags, const char *name,
					 int64_t min, int64_t max)
{
	return property_create_range(dev, DRM_MODE_PROP_SIGNED_RANGE | flags,
			name, I642U64(min), I642U64(max));
}
EXPORT_SYMBOL(drm_property_create_signed_range);

/**
 * drm_property_create_object - create a new object property type
 * @dev: drm device
 * @flags: flags specifying the property type
 * @name: name of the property
 * @type: object type from DRM_MODE_OBJECT_* defines
 *
 * This creates a new generic drm property which can then be attached to a drm
 * object with drm_object_attach_property. The returned property object must be
 * freed with drm_property_destroy.
 *
 * Userspace is only allowed to set this to any property value of the given
 * @type. Only useful for atomic properties, which is enforced.
 *
 * Returns:
 * A pointer to the newly created property on success, NULL on failure.
 */
struct drm_property *drm_property_create_object(struct drm_device *dev,
					 int flags, const char *name, uint32_t type)
{
	struct drm_property *property;

	flags |= DRM_MODE_PROP_OBJECT;

	if (WARN_ON(!(flags & DRM_MODE_PROP_ATOMIC)))
		return NULL;

	property = drm_property_create(dev, flags, name, 1);
	if (!property)
		return NULL;

	property->values[0] = type;

	return property;
}
EXPORT_SYMBOL(drm_property_create_object);

/**
 * drm_property_create_bool - create a new boolean property type
 * @dev: drm device
 * @flags: flags specifying the property type
 * @name: name of the property
 *
 * This creates a new generic drm property which can then be attached to a drm
 * object with drm_object_attach_property. The returned property object must be
 * freed with drm_property_destroy.
 *
 * This is implemented as a ranged property with only {0, 1} as valid values.
 *
 * Returns:
 * A pointer to the newly created property on success, NULL on failure.
 */
struct drm_property *drm_property_create_bool(struct drm_device *dev, int flags,
					 const char *name)
{
	return drm_property_create_range(dev, flags, name, 0, 1);
}
EXPORT_SYMBOL(drm_property_create_bool);

/**
 * drm_property_add_enum - add a possible value to an enumeration property
 * @property: enumeration property to change
 * @index: index of the new enumeration
 * @value: value of the new enumeration
 * @name: symbolic name of the new enumeration
 *
 * This functions adds enumerations to a property.
 *
 * It's use is deprecated, drivers should use one of the more specific helpers
 * to directly create the property with all enumerations already attached.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_property_add_enum(struct drm_property *property, int index,
			  uint64_t value, const char *name)
{
	struct drm_property_enum *prop_enum;

	if (!(drm_property_type_is(property, DRM_MODE_PROP_ENUM) ||
			drm_property_type_is(property, DRM_MODE_PROP_BITMASK)))
		return -EINVAL;

	/*
	 * Bitmask enum properties have the additional constraint of values
	 * from 0 to 63
	 */
	if (drm_property_type_is(property, DRM_MODE_PROP_BITMASK) &&
			(value > 63))
		return -EINVAL;

	if (!list_empty(&property->enum_list)) {
		list_for_each_entry(prop_enum, &property->enum_list, head) {
			if (prop_enum->value == value) {
				strncpy(prop_enum->name, name, DRM_PROP_NAME_LEN);
				prop_enum->name[DRM_PROP_NAME_LEN-1] = '\0';
				return 0;
			}
		}
	}

	prop_enum = kzalloc(sizeof(struct drm_property_enum), GFP_KERNEL);
	if (!prop_enum)
		return -ENOMEM;

	strncpy(prop_enum->name, name, DRM_PROP_NAME_LEN);
	prop_enum->name[DRM_PROP_NAME_LEN-1] = '\0';
	prop_enum->value = value;

	property->values[index] = value;
	list_add_tail(&prop_enum->head, &property->enum_list);
	return 0;
}
EXPORT_SYMBOL(drm_property_add_enum);

/**
 * drm_property_destroy - destroy a drm property
 * @dev: drm device
 * @property: property to destry
 *
 * This function frees a property including any attached resources like
 * enumeration values.
 */
void drm_property_destroy(struct drm_device *dev, struct drm_property *property)
{
	struct drm_property_enum *prop_enum, *pt;

	list_for_each_entry_safe(prop_enum, pt, &property->enum_list, head) {
		list_del(&prop_enum->head);
		kfree(prop_enum);
	}

	if (property->num_values)
		kfree(property->values);
	drm_mode_object_unregister(dev, &property->base);
	list_del(&property->head);
	kfree(property);
}
EXPORT_SYMBOL(drm_property_destroy);

/**
 * drm_object_attach_property - attach a property to a modeset object
 * @obj: drm modeset object
 * @property: property to attach
 * @init_val: initial value of the property
 *
 * This attaches the given property to the modeset object with the given initial
 * value. Currently this function cannot fail since the properties are stored in
 * a statically sized array.
 */
void drm_object_attach_property(struct drm_mode_object *obj,
				struct drm_property *property,
				uint64_t init_val)
{
	int count = obj->properties->count;

	if (count == DRM_OBJECT_MAX_PROPERTY) {
		WARN(1, "Failed to attach object property (type: 0x%x). Please "
			"increase DRM_OBJECT_MAX_PROPERTY by 1 for each time "
			"you see this message on the same object type.\n",
			obj->type);
		return;
	}

	obj->properties->properties[count] = property;
	obj->properties->values[count] = init_val;
	obj->properties->count++;
	if (property->flags & DRM_MODE_PROP_ATOMIC)
		obj->properties->atomic_count++;
}
EXPORT_SYMBOL(drm_object_attach_property);

/**
 * drm_object_property_set_value - set the value of a property
 * @obj: drm mode object to set property value for
 * @property: property to set
 * @val: value the property should be set to
 *
 * This functions sets a given property on a given object. This function only
 * changes the software state of the property, it does not call into the
 * driver's ->set_property callback.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_object_property_set_value(struct drm_mode_object *obj,
				  struct drm_property *property, uint64_t val)
{
	int i;

	for (i = 0; i < obj->properties->count; i++) {
		if (obj->properties->properties[i] == property) {
			obj->properties->values[i] = val;
			return 0;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL(drm_object_property_set_value);

/**
 * drm_object_property_get_value - retrieve the value of a property
 * @obj: drm mode object to get property value from
 * @property: property to retrieve
 * @val: storage for the property value
 *
 * This function retrieves the softare state of the given property for the given
 * property. Since there is no driver callback to retrieve the current property
 * value this might be out of sync with the hardware, depending upon the driver
 * and property.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_object_property_get_value(struct drm_mode_object *obj,
				  struct drm_property *property, uint64_t *val)
{
	int i;

	/* read-only properties bypass atomic mechanism and still store
	 * their value in obj->properties->values[].. mostly to avoid
	 * having to deal w/ EDID and similar props in atomic paths:
	 */
	if (drm_core_check_feature(property->dev, DRIVER_ATOMIC) &&
			!(property->flags & DRM_MODE_PROP_IMMUTABLE))
		return drm_atomic_get_property(obj, property, val);

	for (i = 0; i < obj->properties->count; i++) {
		if (obj->properties->properties[i] == property) {
			*val = obj->properties->values[i];
			return 0;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL(drm_object_property_get_value);

/**
 * drm_mode_getproperty_ioctl - get the property metadata
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * This function retrieves the metadata for a given property, like the different
 * possible values for an enum property or the limits for a range property.
 *
 * Blob properties are special
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_getproperty_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv)
{
	struct drm_mode_get_property *out_resp = data;
	struct drm_property *property;
	int enum_count = 0;
	int value_count = 0;
	int ret = 0, i;
	int copied;
	struct drm_property_enum *prop_enum;
	struct drm_mode_property_enum __user *enum_ptr;
	uint64_t __user *values_ptr;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	drm_modeset_lock_all(dev);
	property = drm_property_find(dev, out_resp->prop_id);
	if (!property) {
		ret = -ENOENT;
		goto done;
	}

	if (drm_property_type_is(property, DRM_MODE_PROP_ENUM) ||
			drm_property_type_is(property, DRM_MODE_PROP_BITMASK)) {
		list_for_each_entry(prop_enum, &property->enum_list, head)
			enum_count++;
	}

	value_count = property->num_values;

	strncpy(out_resp->name, property->name, DRM_PROP_NAME_LEN);
	out_resp->name[DRM_PROP_NAME_LEN-1] = 0;
	out_resp->flags = property->flags;

	if ((out_resp->count_values >= value_count) && value_count) {
		values_ptr = (uint64_t __user *)(unsigned long)out_resp->values_ptr;
		for (i = 0; i < value_count; i++) {
			if (copy_to_user(values_ptr + i, &property->values[i], sizeof(uint64_t))) {
				ret = -EFAULT;
				goto done;
			}
		}
	}
	out_resp->count_values = value_count;

	if (drm_property_type_is(property, DRM_MODE_PROP_ENUM) ||
			drm_property_type_is(property, DRM_MODE_PROP_BITMASK)) {
		if ((out_resp->count_enum_blobs >= enum_count) && enum_count) {
			copied = 0;
			enum_ptr = (struct drm_mode_property_enum __user *)(unsigned long)out_resp->enum_blob_ptr;
			list_for_each_entry(prop_enum, &property->enum_list, head) {

				if (copy_to_user(&enum_ptr[copied].value, &prop_enum->value, sizeof(uint64_t))) {
					ret = -EFAULT;
					goto done;
				}

				if (copy_to_user(&enum_ptr[copied].name,
						 &prop_enum->name, DRM_PROP_NAME_LEN)) {
					ret = -EFAULT;
					goto done;
				}
				copied++;
			}
		}
		out_resp->count_enum_blobs = enum_count;
	}

	/*
	 * NOTE: The idea seems to have been to use this to read all the blob
	 * property values. But nothing ever added them to the corresponding
	 * list, userspace always used the special-purpose get_blob ioctl to
	 * read the value for a blob property. It also doesn't make a lot of
	 * sense to return values here when everything else is just metadata for
	 * the property itself.
	 */
	if (drm_property_type_is(property, DRM_MODE_PROP_BLOB))
		out_resp->count_enum_blobs = 0;
done:
	drm_modeset_unlock_all(dev);
	return ret;
}

static void drm_property_free_blob(struct kref *kref)
{
	struct drm_property_blob *blob =
		container_of(kref, struct drm_property_blob, base.refcount);

	mutex_lock(&blob->dev->mode_config.blob_lock);
	list_del(&blob->head_global);
	mutex_unlock(&blob->dev->mode_config.blob_lock);

	drm_mode_object_unregister(blob->dev, &blob->base);

	kfree(blob);
}

/**
 * drm_property_create_blob - Create new blob property
 *
 * Creates a new blob property for a specified DRM device, optionally
 * copying data.
 *
 * @dev: DRM device to create property for
 * @length: Length to allocate for blob data
 * @data: If specified, copies data into blob
 *
 * Returns:
 * New blob property with a single reference on success, or an ERR_PTR
 * value on failure.
 */
struct drm_property_blob *
drm_property_create_blob(struct drm_device *dev, size_t length,
			 const void *data)
{
	struct drm_property_blob *blob;
	int ret;

	if (!length || length > ULONG_MAX - sizeof(struct drm_property_blob))
		return ERR_PTR(-EINVAL);

	blob = kzalloc(sizeof(struct drm_property_blob)+length, GFP_KERNEL);
	if (!blob)
		return ERR_PTR(-ENOMEM);

	/* This must be explicitly initialised, so we can safely call list_del
	 * on it in the removal handler, even if it isn't in a file list. */
	INIT_LIST_HEAD(&blob->head_file);
	blob->length = length;
	blob->dev = dev;

	if (data)
		memcpy(blob->data, data, length);

	ret = drm_mode_object_get_reg(dev, &blob->base, DRM_MODE_OBJECT_BLOB,
				      true, drm_property_free_blob);
	if (ret) {
		kfree(blob);
		return ERR_PTR(-EINVAL);
	}

	mutex_lock(&dev->mode_config.blob_lock);
	list_add_tail(&blob->head_global,
	              &dev->mode_config.property_blob_list);
	mutex_unlock(&dev->mode_config.blob_lock);

	return blob;
}
EXPORT_SYMBOL(drm_property_create_blob);

/**
 * drm_property_unreference_blob - Unreference a blob property
 *
 * Drop a reference on a blob property. May free the object.
 *
 * @blob: Pointer to blob property
 */
void drm_property_unreference_blob(struct drm_property_blob *blob)
{
	if (!blob)
		return;

	drm_mode_object_unreference(&blob->base);
}
EXPORT_SYMBOL(drm_property_unreference_blob);

/**
 * drm_property_destroy_user_blobs - destroy all blobs created by this client
 * @dev:       DRM device
 * @file_priv: destroy all blobs owned by this file handle
 */
void drm_property_destroy_user_blobs(struct drm_device *dev,
				     struct drm_file *file_priv)
{
	struct drm_property_blob *blob, *bt;

	/*
	 * When the file gets released that means no one else can access the
	 * blob list any more, so no need to grab dev->blob_lock.
	 */
	list_for_each_entry_safe(blob, bt, &file_priv->blobs, head_file) {
		list_del_init(&blob->head_file);
		drm_property_unreference_blob(blob);
	}
}

/**
 * drm_property_reference_blob - Take a reference on an existing property
 *
 * Take a new reference on an existing blob property.
 *
 * @blob: Pointer to blob property
 */
struct drm_property_blob *drm_property_reference_blob(struct drm_property_blob *blob)
{
	drm_mode_object_reference(&blob->base);
	return blob;
}
EXPORT_SYMBOL(drm_property_reference_blob);

/**
 * drm_property_lookup_blob - look up a blob property and take a reference
 * @dev: drm device
 * @id: id of the blob property
 *
 * If successful, this takes an additional reference to the blob property.
 * callers need to make sure to eventually unreference the returned property
 * again, using @drm_property_unreference_blob.
 */
struct drm_property_blob *drm_property_lookup_blob(struct drm_device *dev,
					           uint32_t id)
{
	struct drm_mode_object *obj;
	struct drm_property_blob *blob = NULL;

	obj = _object_find(dev, id, DRM_MODE_OBJECT_BLOB);
	if (obj)
		blob = obj_to_blob(obj);
	return blob;
}
EXPORT_SYMBOL(drm_property_lookup_blob);

/**
 * drm_property_replace_global_blob - atomically replace existing blob property
 * @dev: drm device
 * @replace: location of blob property pointer to be replaced
 * @length: length of data for new blob, or 0 for no data
 * @data: content for new blob, or NULL for no data
 * @obj_holds_id: optional object for property holding blob ID
 * @prop_holds_id: optional property holding blob ID
 * @return 0 on success or error on failure
 *
 * This function will atomically replace a global property in the blob list,
 * optionally updating a property which holds the ID of that property. It is
 * guaranteed to be atomic: no caller will be allowed to see intermediate
 * results, and either the entire operation will succeed and clean up the
 * previous property, or it will fail and the state will be unchanged.
 *
 * If length is 0 or data is NULL, no new blob will be created, and the holding
 * property, if specified, will be set to 0.
 *
 * Access to the replace pointer is assumed to be protected by the caller, e.g.
 * by holding the relevant modesetting object lock for its parent.
 *
 * For example, a drm_connector has a 'PATH' property, which contains the ID
 * of a blob property with the value of the MST path information. Calling this
 * function with replace pointing to the connector's path_blob_ptr, length and
 * data set for the new path information, obj_holds_id set to the connector's
 * base object, and prop_holds_id set to the path property name, will perform
 * a completely atomic update. The access to path_blob_ptr is protected by the
 * caller holding a lock on the connector.
 */
static int drm_property_replace_global_blob(struct drm_device *dev,
                                            struct drm_property_blob **replace,
                                            size_t length,
                                            const void *data,
                                            struct drm_mode_object *obj_holds_id,
                                            struct drm_property *prop_holds_id)
{
	struct drm_property_blob *new_blob = NULL;
	struct drm_property_blob *old_blob = NULL;
	int ret;

	WARN_ON(replace == NULL);

	old_blob = *replace;

	if (length && data) {
		new_blob = drm_property_create_blob(dev, length, data);
		if (IS_ERR(new_blob))
			return PTR_ERR(new_blob);
	}

	/* This does not need to be synchronised with blob_lock, as the
	 * get_properties ioctl locks all modesetting objects, and
	 * obj_holds_id must be locked before calling here, so we cannot
	 * have its value out of sync with the list membership modified
	 * below under blob_lock. */
	if (obj_holds_id) {
		ret = drm_object_property_set_value(obj_holds_id,
						    prop_holds_id,
						    new_blob ?
						        new_blob->base.id : 0);
		if (ret != 0)
			goto err_created;
	}

	drm_property_unreference_blob(old_blob);
	*replace = new_blob;

	return 0;

err_created:
	drm_property_unreference_blob(new_blob);
	return ret;
}

/**
 * drm_mode_getblob_ioctl - get the contents of a blob property value
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * This function retrieves the contents of a blob property. The value stored in
 * an object's blob property is just a normal modeset object id.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_getblob_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv)
{
	struct drm_mode_get_blob *out_resp = data;
	struct drm_property_blob *blob;
	int ret = 0;
	void __user *blob_ptr;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	blob = drm_property_lookup_blob(dev, out_resp->blob_id);
	if (!blob)
		return -ENOENT;

	if (out_resp->length == blob->length) {
		blob_ptr = (void __user *)(unsigned long)out_resp->data;
		if (copy_to_user(blob_ptr, blob->data, blob->length)) {
			ret = -EFAULT;
			goto unref;
		}
	}
	out_resp->length = blob->length;
unref:
	drm_property_unreference_blob(blob);

	return ret;
}

/**
 * drm_mode_createblob_ioctl - create a new blob property
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * This function creates a new blob property with user-defined values. In order
 * to give us sensible validation and checking when creating, rather than at
 * every potential use, we also require a type to be provided upfront.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_createblob_ioctl(struct drm_device *dev,
			      void *data, struct drm_file *file_priv)
{
	struct drm_mode_create_blob *out_resp = data;
	struct drm_property_blob *blob;
	void __user *blob_ptr;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	blob = drm_property_create_blob(dev, out_resp->length, NULL);
	if (IS_ERR(blob))
		return PTR_ERR(blob);

	blob_ptr = (void __user *)(unsigned long)out_resp->data;
	if (copy_from_user(blob->data, blob_ptr, out_resp->length)) {
		ret = -EFAULT;
		goto out_blob;
	}

	/* Dropping the lock between create_blob and our access here is safe
	 * as only the same file_priv can remove the blob; at this point, it is
	 * not associated with any file_priv. */
	mutex_lock(&dev->mode_config.blob_lock);
	out_resp->blob_id = blob->base.id;
	list_add_tail(&blob->head_file, &file_priv->blobs);
	mutex_unlock(&dev->mode_config.blob_lock);

	return 0;

out_blob:
	drm_property_unreference_blob(blob);
	return ret;
}

/**
 * drm_mode_destroyblob_ioctl - destroy a user blob property
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * Destroy an existing user-defined blob property.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_destroyblob_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv)
{
	struct drm_mode_destroy_blob *out_resp = data;
	struct drm_property_blob *blob = NULL, *bt;
	bool found = false;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	blob = drm_property_lookup_blob(dev, out_resp->blob_id);
	if (!blob)
		return -ENOENT;

	mutex_lock(&dev->mode_config.blob_lock);
	/* Ensure the property was actually created by this user. */
	list_for_each_entry(bt, &file_priv->blobs, head_file) {
		if (bt == blob) {
			found = true;
			break;
		}
	}

	if (!found) {
		ret = -EPERM;
		goto err;
	}

	/* We must drop head_file here, because we may not be the last
	 * reference on the blob. */
	list_del_init(&blob->head_file);
	mutex_unlock(&dev->mode_config.blob_lock);

	/* One reference from lookup, and one from the filp. */
	drm_property_unreference_blob(blob);
	drm_property_unreference_blob(blob);

	return 0;

err:
	mutex_unlock(&dev->mode_config.blob_lock);
	drm_property_unreference_blob(blob);

	return ret;
}

/**
 * drm_mode_connector_set_path_property - set tile property on connector
 * @connector: connector to set property on.
 * @path: path to use for property; must not be NULL.
 *
 * This creates a property to expose to userspace to specify a
 * connector path. This is mainly used for DisplayPort MST where
 * connectors have a topology and we want to allow userspace to give
 * them more meaningful names.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_connector_set_path_property(struct drm_connector *connector,
					 const char *path)
{
	struct drm_device *dev = connector->dev;
	int ret;

	ret = drm_property_replace_global_blob(dev,
	                                       &connector->path_blob_ptr,
	                                       strlen(path) + 1,
	                                       path,
	                                       &connector->base,
	                                       dev->mode_config.path_property);
	return ret;
}
EXPORT_SYMBOL(drm_mode_connector_set_path_property);

/**
 * drm_mode_connector_set_tile_property - set tile property on connector
 * @connector: connector to set property on.
 *
 * This looks up the tile information for a connector, and creates a
 * property for userspace to parse if it exists. The property is of
 * the form of 8 integers using ':' as a separator.
 *
 * Returns:
 * Zero on success, errno on failure.
 */
int drm_mode_connector_set_tile_property(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	char tile[256];
	int ret;

	if (!connector->has_tile) {
		ret  = drm_property_replace_global_blob(dev,
		                                        &connector->tile_blob_ptr,
		                                        0,
		                                        NULL,
		                                        &connector->base,
		                                        dev->mode_config.tile_property);
		return ret;
	}

	snprintf(tile, 256, "%d:%d:%d:%d:%d:%d:%d:%d",
		 connector->tile_group->id, connector->tile_is_single_monitor,
		 connector->num_h_tile, connector->num_v_tile,
		 connector->tile_h_loc, connector->tile_v_loc,
		 connector->tile_h_size, connector->tile_v_size);

	ret = drm_property_replace_global_blob(dev,
	                                       &connector->tile_blob_ptr,
	                                       strlen(tile) + 1,
	                                       tile,
	                                       &connector->base,
	                                       dev->mode_config.tile_property);
	return ret;
}
EXPORT_SYMBOL(drm_mode_connector_set_tile_property);

/**
 * drm_mode_connector_update_edid_property - update the edid property of a connector
 * @connector: drm connector
 * @edid: new value of the edid property
 *
 * This function creates a new blob modeset object and assigns its id to the
 * connector's edid property.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_connector_update_edid_property(struct drm_connector *connector,
					    const struct edid *edid)
{
	struct drm_device *dev = connector->dev;
	size_t size = 0;
	int ret;

	/* ignore requests to set edid when overridden */
	if (connector->override_edid)
		return 0;

	if (edid)
		size = EDID_LENGTH * (1 + edid->extensions);

	ret = drm_property_replace_global_blob(dev,
					       &connector->edid_blob_ptr,
	                                       size,
	                                       edid,
	                                       &connector->base,
	                                       dev->mode_config.edid_property);
	return ret;
}
EXPORT_SYMBOL(drm_mode_connector_update_edid_property);

/* Some properties could refer to dynamic refcnt'd objects, or things that
 * need special locking to handle lifetime issues (ie. to ensure the prop
 * value doesn't become invalid part way through the property update due to
 * race).  The value returned by reference via 'obj' should be passed back
 * to drm_property_change_valid_put() after the property is set (and the
 * object to which the property is attached has a chance to take it's own
 * reference).
 */
bool drm_property_change_valid_get(struct drm_property *property,
					 uint64_t value, struct drm_mode_object **ref)
{
	int i;

	if (property->flags & DRM_MODE_PROP_IMMUTABLE)
		return false;

	*ref = NULL;

	if (drm_property_type_is(property, DRM_MODE_PROP_RANGE)) {
		if (value < property->values[0] || value > property->values[1])
			return false;
		return true;
	} else if (drm_property_type_is(property, DRM_MODE_PROP_SIGNED_RANGE)) {
		int64_t svalue = U642I64(value);

		if (svalue < U642I64(property->values[0]) ||
				svalue > U642I64(property->values[1]))
			return false;
		return true;
	} else if (drm_property_type_is(property, DRM_MODE_PROP_BITMASK)) {
		uint64_t valid_mask = 0;

		for (i = 0; i < property->num_values; i++)
			valid_mask |= (1ULL << property->values[i]);
		return !(value & ~valid_mask);
	} else if (drm_property_type_is(property, DRM_MODE_PROP_BLOB)) {
		struct drm_property_blob *blob;

		if (value == 0)
			return true;

		blob = drm_property_lookup_blob(property->dev, value);
		if (blob) {
			*ref = &blob->base;
			return true;
		} else {
			return false;
		}
	} else if (drm_property_type_is(property, DRM_MODE_PROP_OBJECT)) {
		/* a zero value for an object property translates to null: */
		if (value == 0)
			return true;

		*ref = _object_find(property->dev, value, property->values[0]);
		return *ref != NULL;
	}

	for (i = 0; i < property->num_values; i++)
		if (property->values[i] == value)
			return true;
	return false;
}

void drm_property_change_valid_put(struct drm_property *property,
		struct drm_mode_object *ref)
{
	if (!ref)
		return;

	if (drm_property_type_is(property, DRM_MODE_PROP_OBJECT)) {
		drm_mode_object_unreference(ref);
	} else if (drm_property_type_is(property, DRM_MODE_PROP_BLOB))
		drm_property_unreference_blob(obj_to_blob(ref));
}

/**
 * drm_mode_connector_property_set_ioctl - set the current value of a connector property
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * This function sets the current value for a connectors's property. It also
 * calls into a driver's ->set_property callback to update the hardware state
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_connector_property_set_ioctl(struct drm_device *dev,
				       void *data, struct drm_file *file_priv)
{
	struct drm_mode_connector_set_property *conn_set_prop = data;
	struct drm_mode_obj_set_property obj_set_prop = {
		.value = conn_set_prop->value,
		.prop_id = conn_set_prop->prop_id,
		.obj_id = conn_set_prop->connector_id,
		.obj_type = DRM_MODE_OBJECT_CONNECTOR
	};

	/* It does all the locking and checking we need */
	return drm_mode_obj_set_property_ioctl(dev, &obj_set_prop, file_priv);
}

static int drm_mode_connector_set_obj_prop(struct drm_mode_object *obj,
					   struct drm_property *property,
					   uint64_t value)
{
	int ret = -EINVAL;
	struct drm_connector *connector = obj_to_connector(obj);

	/* Do DPMS ourselves */
	if (property == connector->dev->mode_config.dpms_property) {
		ret = (*connector->funcs->dpms)(connector, (int)value);
	} else if (connector->funcs->set_property)
		ret = connector->funcs->set_property(connector, property, value);

	/* store the property value if successful */
	if (!ret)
		drm_object_property_set_value(&connector->base, property, value);
	return ret;
}

static int drm_mode_crtc_set_obj_prop(struct drm_mode_object *obj,
				      struct drm_property *property,
				      uint64_t value)
{
	int ret = -EINVAL;
	struct drm_crtc *crtc = obj_to_crtc(obj);

	if (crtc->funcs->set_property)
		ret = crtc->funcs->set_property(crtc, property, value);
	if (!ret)
		drm_object_property_set_value(obj, property, value);

	return ret;
}

/**
 * drm_mode_plane_set_obj_prop - set the value of a property
 * @plane: drm plane object to set property value for
 * @property: property to set
 * @value: value the property should be set to
 *
 * This functions sets a given property on a given plane object. This function
 * calls the driver's ->set_property callback and changes the software state of
 * the property if the callback succeeds.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_mode_plane_set_obj_prop(struct drm_plane *plane,
				struct drm_property *property,
				uint64_t value)
{
	int ret = -EINVAL;
	struct drm_mode_object *obj = &plane->base;

	if (plane->funcs->set_property)
		ret = plane->funcs->set_property(plane, property, value);
	if (!ret)
		drm_object_property_set_value(obj, property, value);

	return ret;
}
EXPORT_SYMBOL(drm_mode_plane_set_obj_prop);

/**
 * drm_mode_obj_get_properties_ioctl - get the current value of a object's property
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * This function retrieves the current value for an object's property. Compared
 * to the connector specific ioctl this one is extended to also work on crtc and
 * plane objects.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_obj_get_properties_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv)
{
	struct drm_mode_obj_get_properties *arg = data;
	struct drm_mode_object *obj;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	drm_modeset_lock_all(dev);

	obj = drm_mode_object_find(dev, arg->obj_id, arg->obj_type);
	if (!obj) {
		ret = -ENOENT;
		goto out;
	}
	if (!obj->properties) {
		ret = -EINVAL;
		goto out_unref;
	}

	ret = get_properties(obj, file_priv->atomic,
			(uint32_t __user *)(unsigned long)(arg->props_ptr),
			(uint64_t __user *)(unsigned long)(arg->prop_values_ptr),
			&arg->count_props);

out_unref:
	drm_mode_object_unreference(obj);
out:
	drm_modeset_unlock_all(dev);
	return ret;
}

/**
 * drm_mode_obj_set_property_ioctl - set the current value of an object's property
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * This function sets the current value for an object's property. It also calls
 * into a driver's ->set_property callback to update the hardware state.
 * Compared to the connector specific ioctl this one is extended to also work on
 * crtc and plane objects.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_obj_set_property_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv)
{
	struct drm_mode_obj_set_property *arg = data;
	struct drm_mode_object *arg_obj;
	struct drm_mode_object *prop_obj;
	struct drm_property *property;
	int i, ret = -EINVAL;
	struct drm_mode_object *ref;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	drm_modeset_lock_all(dev);

	arg_obj = drm_mode_object_find(dev, arg->obj_id, arg->obj_type);
	if (!arg_obj) {
		ret = -ENOENT;
		goto out;
	}
	if (!arg_obj->properties)
		goto out_unref;

	for (i = 0; i < arg_obj->properties->count; i++)
		if (arg_obj->properties->properties[i]->base.id == arg->prop_id)
			break;

	if (i == arg_obj->properties->count)
		goto out_unref;

	prop_obj = drm_mode_object_find(dev, arg->prop_id,
					DRM_MODE_OBJECT_PROPERTY);
	if (!prop_obj) {
		ret = -ENOENT;
		goto out_unref;
	}
	property = obj_to_property(prop_obj);

	if (!drm_property_change_valid_get(property, arg->value, &ref))
		goto out_unref;

	switch (arg_obj->type) {
	case DRM_MODE_OBJECT_CONNECTOR:
		ret = drm_mode_connector_set_obj_prop(arg_obj, property,
						      arg->value);
		break;
	case DRM_MODE_OBJECT_CRTC:
		ret = drm_mode_crtc_set_obj_prop(arg_obj, property, arg->value);
		break;
	case DRM_MODE_OBJECT_PLANE:
		ret = drm_mode_plane_set_obj_prop(obj_to_plane(arg_obj),
						  property, arg->value);
		break;
	}

	drm_property_change_valid_put(property, ref);

out_unref:
	drm_mode_object_unreference(arg_obj);
out:
	drm_modeset_unlock_all(dev);
	return ret;
}

/**
 * drm_mode_connector_attach_encoder - attach a connector to an encoder
 * @connector: connector to attach
 * @encoder: encoder to attach @connector to
 *
 * This function links up a connector to an encoder. Note that the routing
 * restrictions between encoders and crtcs are exposed to userspace through the
 * possible_clones and possible_crtcs bitmasks.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_connector_attach_encoder(struct drm_connector *connector,
				      struct drm_encoder *encoder)
{
	int i;

	/*
	 * In the past, drivers have attempted to model the static association
	 * of connector to encoder in simple connector/encoder devices using a
	 * direct assignment of connector->encoder = encoder. This connection
	 * is a logical one and the responsibility of the core, so drivers are
	 * expected not to mess with this.
	 *
	 * Note that the error return should've been enough here, but a large
	 * majority of drivers ignores the return value, so add in a big WARN
	 * to get people's attention.
	 */
	if (WARN_ON(connector->encoder))
		return -EINVAL;

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		if (connector->encoder_ids[i] == 0) {
			connector->encoder_ids[i] = encoder->base.id;
			return 0;
		}
	}
	return -ENOMEM;
}
EXPORT_SYMBOL(drm_mode_connector_attach_encoder);

/**
 * drm_mode_crtc_set_gamma_size - set the gamma table size
 * @crtc: CRTC to set the gamma table size for
 * @gamma_size: size of the gamma table
 *
 * Drivers which support gamma tables should set this to the supported gamma
 * table size when initializing the CRTC. Currently the drm core only supports a
 * fixed gamma table size.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_crtc_set_gamma_size(struct drm_crtc *crtc,
				 int gamma_size)
{
	uint16_t *r_base, *g_base, *b_base;
	int i;

	crtc->gamma_size = gamma_size;

	crtc->gamma_store = kcalloc(gamma_size, sizeof(uint16_t) * 3,
				    GFP_KERNEL);
	if (!crtc->gamma_store) {
		crtc->gamma_size = 0;
		return -ENOMEM;
	}

	r_base = crtc->gamma_store;
	g_base = r_base + gamma_size;
	b_base = g_base + gamma_size;
	for (i = 0; i < gamma_size; i++) {
		r_base[i] = i << 8;
		g_base[i] = i << 8;
		b_base[i] = i << 8;
	}


	return 0;
}
EXPORT_SYMBOL(drm_mode_crtc_set_gamma_size);

/**
 * drm_mode_gamma_set_ioctl - set the gamma table
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * Set the gamma table of a CRTC to the one passed in by the user. Userspace can
 * inquire the required gamma table size through drm_mode_gamma_get_ioctl.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_gamma_set_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv)
{
	struct drm_mode_crtc_lut *crtc_lut = data;
	struct drm_crtc *crtc;
	void *r_base, *g_base, *b_base;
	int size;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	drm_modeset_lock_all(dev);
	crtc = drm_crtc_find(dev, crtc_lut->crtc_id);
	if (!crtc) {
		ret = -ENOENT;
		goto out;
	}

	if (crtc->funcs->gamma_set == NULL) {
		ret = -ENOSYS;
		goto out;
	}

	/* memcpy into gamma store */
	if (crtc_lut->gamma_size != crtc->gamma_size) {
		ret = -EINVAL;
		goto out;
	}

	size = crtc_lut->gamma_size * (sizeof(uint16_t));
	r_base = crtc->gamma_store;
	if (copy_from_user(r_base, (void __user *)(unsigned long)crtc_lut->red, size)) {
		ret = -EFAULT;
		goto out;
	}

	g_base = r_base + size;
	if (copy_from_user(g_base, (void __user *)(unsigned long)crtc_lut->green, size)) {
		ret = -EFAULT;
		goto out;
	}

	b_base = g_base + size;
	if (copy_from_user(b_base, (void __user *)(unsigned long)crtc_lut->blue, size)) {
		ret = -EFAULT;
		goto out;
	}

	ret = crtc->funcs->gamma_set(crtc, r_base, g_base, b_base, crtc->gamma_size);

out:
	drm_modeset_unlock_all(dev);
	return ret;

}

/**
 * drm_mode_gamma_get_ioctl - get the gamma table
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * Copy the current gamma table into the storage provided. This also provides
 * the gamma table size the driver expects, which can be used to size the
 * allocated storage.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_gamma_get_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv)
{
	struct drm_mode_crtc_lut *crtc_lut = data;
	struct drm_crtc *crtc;
	void *r_base, *g_base, *b_base;
	int size;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	drm_modeset_lock_all(dev);
	crtc = drm_crtc_find(dev, crtc_lut->crtc_id);
	if (!crtc) {
		ret = -ENOENT;
		goto out;
	}

	/* memcpy into gamma store */
	if (crtc_lut->gamma_size != crtc->gamma_size) {
		ret = -EINVAL;
		goto out;
	}

	size = crtc_lut->gamma_size * (sizeof(uint16_t));
	r_base = crtc->gamma_store;
	if (copy_to_user((void __user *)(unsigned long)crtc_lut->red, r_base, size)) {
		ret = -EFAULT;
		goto out;
	}

	g_base = r_base + size;
	if (copy_to_user((void __user *)(unsigned long)crtc_lut->green, g_base, size)) {
		ret = -EFAULT;
		goto out;
	}

	b_base = g_base + size;
	if (copy_to_user((void __user *)(unsigned long)crtc_lut->blue, b_base, size)) {
		ret = -EFAULT;
		goto out;
	}
out:
	drm_modeset_unlock_all(dev);
	return ret;
}

/**
 * drm_mode_page_flip_ioctl - schedule an asynchronous fb update
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * This schedules an asynchronous update on a given CRTC, called page flip.
 * Optionally a drm event is generated to signal the completion of the event.
 * Generic drivers cannot assume that a pageflip with changed framebuffer
 * properties (including driver specific metadata like tiling layout) will work,
 * but some drivers support e.g. pixel format changes through the pageflip
 * ioctl.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_page_flip_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv)
{
	struct drm_mode_crtc_page_flip *page_flip = data;
	struct drm_crtc *crtc;
	struct drm_framebuffer *fb = NULL;
	struct drm_pending_vblank_event *e = NULL;
	int ret = -EINVAL;

	if (page_flip->flags & ~DRM_MODE_PAGE_FLIP_FLAGS ||
	    page_flip->reserved != 0)
		return -EINVAL;

	if ((page_flip->flags & DRM_MODE_PAGE_FLIP_ASYNC) && !dev->mode_config.async_page_flip)
		return -EINVAL;

	crtc = drm_crtc_find(dev, page_flip->crtc_id);
	if (!crtc)
		return -ENOENT;

	drm_modeset_lock_crtc(crtc, crtc->primary);
	if (crtc->primary->fb == NULL) {
		/* The framebuffer is currently unbound, presumably
		 * due to a hotplug event, that userspace has not
		 * yet discovered.
		 */
		ret = -EBUSY;
		goto out;
	}

	if (crtc->funcs->page_flip == NULL)
		goto out;

	fb = drm_framebuffer_lookup(dev, page_flip->fb_id);
	if (!fb) {
		ret = -ENOENT;
		goto out;
	}

	if (crtc->state) {
		const struct drm_plane_state *state = crtc->primary->state;

		ret = check_src_coords(state->src_x, state->src_y,
				       state->src_w, state->src_h, fb);
	} else {
		ret = drm_crtc_check_viewport(crtc, crtc->x, crtc->y, &crtc->mode, fb);
	}
	if (ret)
		goto out;

	if (crtc->primary->fb->pixel_format != fb->pixel_format) {
		DRM_DEBUG_KMS("Page flip is not allowed to change frame buffer format.\n");
		ret = -EINVAL;
		goto out;
	}

	if (page_flip->flags & DRM_MODE_PAGE_FLIP_EVENT) {
		e = kzalloc(sizeof *e, GFP_KERNEL);
		if (!e) {
			ret = -ENOMEM;
			goto out;
		}
		e->event.base.type = DRM_EVENT_FLIP_COMPLETE;
		e->event.base.length = sizeof(e->event);
		e->event.user_data = page_flip->user_data;
		ret = drm_event_reserve_init(dev, file_priv, &e->base, &e->event.base);
		if (ret) {
			kfree(e);
			goto out;
		}
	}

	crtc->primary->old_fb = crtc->primary->fb;
	ret = crtc->funcs->page_flip(crtc, fb, e, page_flip->flags);
	if (ret) {
		if (page_flip->flags & DRM_MODE_PAGE_FLIP_EVENT)
			drm_event_cancel_free(dev, &e->base);
		/* Keep the old fb, don't unref it. */
		crtc->primary->old_fb = NULL;
	} else {
		crtc->primary->fb = fb;
		/* Unref only the old framebuffer. */
		fb = NULL;
	}

out:
	if (fb)
		drm_framebuffer_unreference(fb);
	if (crtc->primary->old_fb)
		drm_framebuffer_unreference(crtc->primary->old_fb);
	crtc->primary->old_fb = NULL;
	drm_modeset_unlock_crtc(crtc);

	return ret;
}

/**
 * drm_mode_config_reset - call ->reset callbacks
 * @dev: drm device
 *
 * This functions calls all the crtc's, encoder's and connector's ->reset
 * callback. Drivers can use this in e.g. their driver load or resume code to
 * reset hardware and software state.
 */
void drm_mode_config_reset(struct drm_device *dev)
{
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	struct drm_encoder *encoder;
	struct drm_connector *connector;

	drm_for_each_plane(plane, dev)
		if (plane->funcs->reset)
			plane->funcs->reset(plane);

	drm_for_each_crtc(crtc, dev)
		if (crtc->funcs->reset)
			crtc->funcs->reset(crtc);

	drm_for_each_encoder(encoder, dev)
		if (encoder->funcs->reset)
			encoder->funcs->reset(encoder);

	mutex_lock(&dev->mode_config.mutex);
	drm_for_each_connector(connector, dev)
		if (connector->funcs->reset)
			connector->funcs->reset(connector);
	mutex_unlock(&dev->mode_config.mutex);
}
EXPORT_SYMBOL(drm_mode_config_reset);

/**
 * drm_mode_create_dumb_ioctl - create a dumb backing storage buffer
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * This creates a new dumb buffer in the driver's backing storage manager (GEM,
 * TTM or something else entirely) and returns the resulting buffer handle. This
 * handle can then be wrapped up into a framebuffer modeset object.
 *
 * Note that userspace is not allowed to use such objects for render
 * acceleration - drivers must create their own private ioctls for such a use
 * case.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_create_dumb_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv)
{
	struct drm_mode_create_dumb *args = data;
	u32 cpp, stride, size;

	if (!dev->driver->dumb_create)
		return -ENOSYS;
	if (!args->width || !args->height || !args->bpp)
		return -EINVAL;

	/* overflow checks for 32bit size calculations */
	/* NOTE: DIV_ROUND_UP() can overflow */
	cpp = DIV_ROUND_UP(args->bpp, 8);
	if (!cpp || cpp > 0xffffffffU / args->width)
		return -EINVAL;
	stride = cpp * args->width;
	if (args->height > 0xffffffffU / stride)
		return -EINVAL;

	/* test for wrap-around */
	size = args->height * stride;
	if (PAGE_ALIGN(size) == 0)
		return -EINVAL;

	/*
	 * handle, pitch and size are output parameters. Zero them out to
	 * prevent drivers from accidentally using uninitialized data. Since
	 * not all existing userspace is clearing these fields properly we
	 * cannot reject IOCTL with garbage in them.
	 */
	args->handle = 0;
	args->pitch = 0;
	args->size = 0;

	return dev->driver->dumb_create(file_priv, dev, args);
}

/**
 * drm_mode_mmap_dumb_ioctl - create an mmap offset for a dumb backing storage buffer
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * Allocate an offset in the drm device node's address space to be able to
 * memory map a dumb buffer.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_mmap_dumb_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv)
{
	struct drm_mode_map_dumb *args = data;

	/* call driver ioctl to get mmap offset */
	if (!dev->driver->dumb_map_offset)
		return -ENOSYS;

	return dev->driver->dumb_map_offset(file_priv, dev, args->handle, &args->offset);
}

/**
 * drm_mode_destroy_dumb_ioctl - destroy a dumb backing strage buffer
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * This destroys the userspace handle for the given dumb backing storage buffer.
 * Since buffer objects must be reference counted in the kernel a buffer object
 * won't be immediately freed if a framebuffer modeset object still uses it.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_destroy_dumb_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file_priv)
{
	struct drm_mode_destroy_dumb *args = data;

	if (!dev->driver->dumb_destroy)
		return -ENOSYS;

	return dev->driver->dumb_destroy(file_priv, dev, args->handle);
}

/**
 * drm_rotation_simplify() - Try to simplify the rotation
 * @rotation: Rotation to be simplified
 * @supported_rotations: Supported rotations
 *
 * Attempt to simplify the rotation to a form that is supported.
 * Eg. if the hardware supports everything except DRM_REFLECT_X
 * one could call this function like this:
 *
 * drm_rotation_simplify(rotation, DRM_ROTATE_0 |
 *                       DRM_ROTATE_90 | DRM_ROTATE_180 |
 *                       DRM_ROTATE_270 | DRM_REFLECT_Y);
 *
 * to eliminate the DRM_ROTATE_X flag. Depending on what kind of
 * transforms the hardware supports, this function may not
 * be able to produce a supported transform, so the caller should
 * check the result afterwards.
 */
unsigned int drm_rotation_simplify(unsigned int rotation,
				   unsigned int supported_rotations)
{
	if (rotation & ~supported_rotations) {
		rotation ^= DRM_REFLECT_X | DRM_REFLECT_Y;
		rotation = (rotation & DRM_REFLECT_MASK) |
		           BIT((ffs(rotation & DRM_ROTATE_MASK) + 1) % 4);
	}

	return rotation;
}
EXPORT_SYMBOL(drm_rotation_simplify);

/**
 * drm_mode_config_init - initialize DRM mode_configuration structure
 * @dev: DRM device
 *
 * Initialize @dev's mode_config structure, used for tracking the graphics
 * configuration of @dev.
 *
 * Since this initializes the modeset locks, no locking is possible. Which is no
 * problem, since this should happen single threaded at init time. It is the
 * driver's problem to ensure this guarantee.
 *
 */
void drm_mode_config_init(struct drm_device *dev)
{
	mutex_init(&dev->mode_config.mutex);
	drm_modeset_lock_init(&dev->mode_config.connection_mutex);
	mutex_init(&dev->mode_config.idr_mutex);
	mutex_init(&dev->mode_config.fb_lock);
	mutex_init(&dev->mode_config.blob_lock);
	INIT_LIST_HEAD(&dev->mode_config.fb_list);
	INIT_LIST_HEAD(&dev->mode_config.crtc_list);
	INIT_LIST_HEAD(&dev->mode_config.connector_list);
	INIT_LIST_HEAD(&dev->mode_config.encoder_list);
	INIT_LIST_HEAD(&dev->mode_config.property_list);
	INIT_LIST_HEAD(&dev->mode_config.property_blob_list);
	INIT_LIST_HEAD(&dev->mode_config.plane_list);
	idr_init(&dev->mode_config.crtc_idr);
	idr_init(&dev->mode_config.tile_idr);
	ida_init(&dev->mode_config.connector_ida);

	drm_modeset_lock_all(dev);
	drm_mode_create_standard_properties(dev);
	drm_modeset_unlock_all(dev);

	/* Just to be sure */
	dev->mode_config.num_fb = 0;
	dev->mode_config.num_connector = 0;
	dev->mode_config.num_crtc = 0;
	dev->mode_config.num_encoder = 0;
	dev->mode_config.num_overlay_plane = 0;
	dev->mode_config.num_total_plane = 0;
}
EXPORT_SYMBOL(drm_mode_config_init);

/**
 * drm_mode_config_cleanup - free up DRM mode_config info
 * @dev: DRM device
 *
 * Free up all the connectors and CRTCs associated with this DRM device, then
 * free up the framebuffers and associated buffer objects.
 *
 * Note that since this /should/ happen single-threaded at driver/device
 * teardown time, no locking is required. It's the driver's job to ensure that
 * this guarantee actually holds true.
 *
 * FIXME: cleanup any dangling user buffer objects too
 */
void drm_mode_config_cleanup(struct drm_device *dev)
{
	struct drm_connector *connector, *ot;
	struct drm_crtc *crtc, *ct;
	struct drm_encoder *encoder, *enct;
	struct drm_framebuffer *fb, *fbt;
	struct drm_property *property, *pt;
	struct drm_property_blob *blob, *bt;
	struct drm_plane *plane, *plt;

	list_for_each_entry_safe(encoder, enct, &dev->mode_config.encoder_list,
				 head) {
		encoder->funcs->destroy(encoder);
	}

	list_for_each_entry_safe(connector, ot,
				 &dev->mode_config.connector_list, head) {
		connector->funcs->destroy(connector);
	}

	list_for_each_entry_safe(property, pt, &dev->mode_config.property_list,
				 head) {
		drm_property_destroy(dev, property);
	}

	list_for_each_entry_safe(plane, plt, &dev->mode_config.plane_list,
				 head) {
		plane->funcs->destroy(plane);
	}

	list_for_each_entry_safe(crtc, ct, &dev->mode_config.crtc_list, head) {
		crtc->funcs->destroy(crtc);
	}

	list_for_each_entry_safe(blob, bt, &dev->mode_config.property_blob_list,
				 head_global) {
		drm_property_unreference_blob(blob);
	}

	/*
	 * Single-threaded teardown context, so it's not required to grab the
	 * fb_lock to protect against concurrent fb_list access. Contrary, it
	 * would actually deadlock with the drm_framebuffer_cleanup function.
	 *
	 * Also, if there are any framebuffers left, that's a driver leak now,
	 * so politely WARN about this.
	 */
	WARN_ON(!list_empty(&dev->mode_config.fb_list));
	list_for_each_entry_safe(fb, fbt, &dev->mode_config.fb_list, head) {
		drm_framebuffer_free(&fb->base.refcount);
	}

	ida_destroy(&dev->mode_config.connector_ida);
	idr_destroy(&dev->mode_config.tile_idr);
	idr_destroy(&dev->mode_config.crtc_idr);
	drm_modeset_lock_fini(&dev->mode_config.connection_mutex);
}
EXPORT_SYMBOL(drm_mode_config_cleanup);

struct drm_property *drm_mode_create_rotation_property(struct drm_device *dev,
						       unsigned int supported_rotations)
{
	static const struct drm_prop_enum_list props[] = {
		{ __builtin_ffs(DRM_ROTATE_0) - 1,   "rotate-0" },
		{ __builtin_ffs(DRM_ROTATE_90) - 1,  "rotate-90" },
		{ __builtin_ffs(DRM_ROTATE_180) - 1, "rotate-180" },
		{ __builtin_ffs(DRM_ROTATE_270) - 1, "rotate-270" },
		{ __builtin_ffs(DRM_REFLECT_X) - 1,  "reflect-x" },
		{ __builtin_ffs(DRM_REFLECT_Y) - 1,  "reflect-y" },
	};

	return drm_property_create_bitmask(dev, 0, "rotation",
					   props, ARRAY_SIZE(props),
					   supported_rotations);
}
EXPORT_SYMBOL(drm_mode_create_rotation_property);

/**
 * DOC: Tile group
 *
 * Tile groups are used to represent tiled monitors with a unique
 * integer identifier. Tiled monitors using DisplayID v1.3 have
 * a unique 8-byte handle, we store this in a tile group, so we
 * have a common identifier for all tiles in a monitor group.
 */
static void drm_tile_group_free(struct kref *kref)
{
	struct drm_tile_group *tg = container_of(kref, struct drm_tile_group, refcount);
	struct drm_device *dev = tg->dev;
	mutex_lock(&dev->mode_config.idr_mutex);
	idr_remove(&dev->mode_config.tile_idr, tg->id);
	mutex_unlock(&dev->mode_config.idr_mutex);
	kfree(tg);
}

/**
 * drm_mode_put_tile_group - drop a reference to a tile group.
 * @dev: DRM device
 * @tg: tile group to drop reference to.
 *
 * drop reference to tile group and free if 0.
 */
void drm_mode_put_tile_group(struct drm_device *dev,
			     struct drm_tile_group *tg)
{
	kref_put(&tg->refcount, drm_tile_group_free);
}

/**
 * drm_mode_get_tile_group - get a reference to an existing tile group
 * @dev: DRM device
 * @topology: 8-bytes unique per monitor.
 *
 * Use the unique bytes to get a reference to an existing tile group.
 *
 * RETURNS:
 * tile group or NULL if not found.
 */
struct drm_tile_group *drm_mode_get_tile_group(struct drm_device *dev,
					       char topology[8])
{
	struct drm_tile_group *tg;
	int id;
	mutex_lock(&dev->mode_config.idr_mutex);
	idr_for_each_entry(&dev->mode_config.tile_idr, tg, id) {
		if (!memcmp(tg->group_data, topology, 8)) {
			if (!kref_get_unless_zero(&tg->refcount))
				tg = NULL;
			mutex_unlock(&dev->mode_config.idr_mutex);
			return tg;
		}
	}
	mutex_unlock(&dev->mode_config.idr_mutex);
	return NULL;
}
EXPORT_SYMBOL(drm_mode_get_tile_group);

/**
 * drm_mode_create_tile_group - create a tile group from a displayid description
 * @dev: DRM device
 * @topology: 8-bytes unique per monitor.
 *
 * Create a tile group for the unique monitor, and get a unique
 * identifier for the tile group.
 *
 * RETURNS:
 * new tile group or error.
 */
struct drm_tile_group *drm_mode_create_tile_group(struct drm_device *dev,
						  char topology[8])
{
	struct drm_tile_group *tg;
	int ret;

	tg = kzalloc(sizeof(*tg), GFP_KERNEL);
	if (!tg)
		return ERR_PTR(-ENOMEM);

	kref_init(&tg->refcount);
	memcpy(tg->group_data, topology, 8);
	tg->dev = dev;

	mutex_lock(&dev->mode_config.idr_mutex);
	ret = idr_alloc(&dev->mode_config.tile_idr, tg, 1, 0, GFP_KERNEL);
	if (ret >= 0) {
		tg->id = ret;
	} else {
		kfree(tg);
		tg = ERR_PTR(ret);
	}

	mutex_unlock(&dev->mode_config.idr_mutex);
	return tg;
}
EXPORT_SYMBOL(drm_mode_create_tile_group);

/**
 * drm_crtc_enable_color_mgmt - enable color management properties
 * @crtc: DRM CRTC
 * @degamma_lut_size: the size of the degamma lut (before CSC)
 * @has_ctm: whether to attach ctm_property for CSC matrix
 * @gamma_lut_size: the size of the gamma lut (after CSC)
 *
 * This function lets the driver enable the color correction
 * properties on a CRTC. This includes 3 degamma, csc and gamma
 * properties that userspace can set and 2 size properties to inform
 * the userspace of the lut sizes. Each of the properties are
 * optional. The gamma and degamma properties are only attached if
 * their size is not 0 and ctm_property is only attached if has_ctm is
 * true.
 */
void drm_crtc_enable_color_mgmt(struct drm_crtc *crtc,
				uint degamma_lut_size,
				bool has_ctm,
				uint gamma_lut_size)
{
	struct drm_device *dev = crtc->dev;
	struct drm_mode_config *config = &dev->mode_config;

	if (degamma_lut_size) {
		drm_object_attach_property(&crtc->base,
					   config->degamma_lut_property, 0);
		drm_object_attach_property(&crtc->base,
					   config->degamma_lut_size_property,
					   degamma_lut_size);
	}

	if (has_ctm)
		drm_object_attach_property(&crtc->base,
					   config->ctm_property, 0);

	if (gamma_lut_size) {
		drm_object_attach_property(&crtc->base,
					   config->gamma_lut_property, 0);
		drm_object_attach_property(&crtc->base,
					   config->gamma_lut_size_property,
					   gamma_lut_size);
	}
}
EXPORT_SYMBOL(drm_crtc_enable_color_mgmt);
