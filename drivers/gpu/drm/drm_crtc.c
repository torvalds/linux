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
#include <linux/list.h>
#include "drm.h"
#include "drmP.h"
#include "drm_crtc.h"

struct drm_prop_enum_list {
	int type;
	char *name;
};

/* Avoid boilerplate.  I'm tired of typing. */
#define DRM_ENUM_NAME_FN(fnname, list)				\
	char *fnname(int val)					\
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
static struct drm_prop_enum_list drm_dpms_enum_list[] =
{	{ DRM_MODE_DPMS_ON, "On" },
	{ DRM_MODE_DPMS_STANDBY, "Standby" },
	{ DRM_MODE_DPMS_SUSPEND, "Suspend" },
	{ DRM_MODE_DPMS_OFF, "Off" }
};

DRM_ENUM_NAME_FN(drm_get_dpms_name, drm_dpms_enum_list)

/*
 * Optional properties
 */
static struct drm_prop_enum_list drm_scaling_mode_enum_list[] =
{
	{ DRM_MODE_SCALE_NON_GPU, "Non-GPU" },
	{ DRM_MODE_SCALE_FULLSCREEN, "Fullscreen" },
	{ DRM_MODE_SCALE_NO_SCALE, "No scale" },
	{ DRM_MODE_SCALE_ASPECT, "Aspect" },
};

static struct drm_prop_enum_list drm_dithering_mode_enum_list[] =
{
	{ DRM_MODE_DITHERING_OFF, "Off" },
	{ DRM_MODE_DITHERING_ON, "On" },
};

/*
 * Non-global properties, but "required" for certain connectors.
 */
static struct drm_prop_enum_list drm_dvi_i_select_enum_list[] =
{
	{ DRM_MODE_SUBCONNECTOR_Automatic, "Automatic" }, /* DVI-I and TV-out */
	{ DRM_MODE_SUBCONNECTOR_DVID,      "DVI-D"     }, /* DVI-I  */
	{ DRM_MODE_SUBCONNECTOR_DVIA,      "DVI-A"     }, /* DVI-I  */
};

DRM_ENUM_NAME_FN(drm_get_dvi_i_select_name, drm_dvi_i_select_enum_list)

static struct drm_prop_enum_list drm_dvi_i_subconnector_enum_list[] =
{
	{ DRM_MODE_SUBCONNECTOR_Unknown,   "Unknown"   }, /* DVI-I and TV-out */
	{ DRM_MODE_SUBCONNECTOR_DVID,      "DVI-D"     }, /* DVI-I  */
	{ DRM_MODE_SUBCONNECTOR_DVIA,      "DVI-A"     }, /* DVI-I  */
};

DRM_ENUM_NAME_FN(drm_get_dvi_i_subconnector_name,
		 drm_dvi_i_subconnector_enum_list)

static struct drm_prop_enum_list drm_tv_select_enum_list[] =
{
	{ DRM_MODE_SUBCONNECTOR_Automatic, "Automatic" }, /* DVI-I and TV-out */
	{ DRM_MODE_SUBCONNECTOR_Composite, "Composite" }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_SVIDEO,    "SVIDEO"    }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_Component, "Component" }, /* TV-out */
};

DRM_ENUM_NAME_FN(drm_get_tv_select_name, drm_tv_select_enum_list)

static struct drm_prop_enum_list drm_tv_subconnector_enum_list[] =
{
	{ DRM_MODE_SUBCONNECTOR_Unknown,   "Unknown"   }, /* DVI-I and TV-out */
	{ DRM_MODE_SUBCONNECTOR_Composite, "Composite" }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_SVIDEO,    "SVIDEO"    }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_Component, "Component" }, /* TV-out */
};

DRM_ENUM_NAME_FN(drm_get_tv_subconnector_name,
		 drm_tv_subconnector_enum_list)

struct drm_conn_prop_enum_list {
	int type;
	char *name;
	int count;
};

/*
 * Connector and encoder types.
 */
static struct drm_conn_prop_enum_list drm_connector_enum_list[] =
{	{ DRM_MODE_CONNECTOR_Unknown, "Unknown", 0 },
	{ DRM_MODE_CONNECTOR_VGA, "VGA", 0 },
	{ DRM_MODE_CONNECTOR_DVII, "DVI-I", 0 },
	{ DRM_MODE_CONNECTOR_DVID, "DVI-D", 0 },
	{ DRM_MODE_CONNECTOR_DVIA, "DVI-A", 0 },
	{ DRM_MODE_CONNECTOR_Composite, "Composite", 0 },
	{ DRM_MODE_CONNECTOR_SVIDEO, "SVIDEO", 0 },
	{ DRM_MODE_CONNECTOR_LVDS, "LVDS", 0 },
	{ DRM_MODE_CONNECTOR_Component, "Component", 0 },
	{ DRM_MODE_CONNECTOR_9PinDIN, "9-pin DIN", 0 },
	{ DRM_MODE_CONNECTOR_DisplayPort, "DisplayPort", 0 },
	{ DRM_MODE_CONNECTOR_HDMIA, "HDMI Type A", 0 },
	{ DRM_MODE_CONNECTOR_HDMIB, "HDMI Type B", 0 },
};

static struct drm_prop_enum_list drm_encoder_enum_list[] =
{	{ DRM_MODE_ENCODER_NONE, "None" },
	{ DRM_MODE_ENCODER_DAC, "DAC" },
	{ DRM_MODE_ENCODER_TMDS, "TMDS" },
	{ DRM_MODE_ENCODER_LVDS, "LVDS" },
	{ DRM_MODE_ENCODER_TVDAC, "TV" },
};

char *drm_get_encoder_name(struct drm_encoder *encoder)
{
	static char buf[32];

	snprintf(buf, 32, "%s-%d",
		 drm_encoder_enum_list[encoder->encoder_type].name,
		 encoder->base.id);
	return buf;
}

char *drm_get_connector_name(struct drm_connector *connector)
{
	static char buf[32];

	snprintf(buf, 32, "%s-%d",
		 drm_connector_enum_list[connector->connector_type].name,
		 connector->connector_type_id);
	return buf;
}
EXPORT_SYMBOL(drm_get_connector_name);

char *drm_get_connector_status_name(enum drm_connector_status status)
{
	if (status == connector_status_connected)
		return "connected";
	else if (status == connector_status_disconnected)
		return "disconnected";
	else
		return "unknown";
}

/**
 * drm_mode_object_get - allocate a new identifier
 * @dev: DRM device
 * @ptr: object pointer, used to generate unique ID
 * @type: object type
 *
 * LOCKING:
 *
 * Create a unique identifier based on @ptr in @dev's identifier space.  Used
 * for tracking modes, CRTCs and connectors.
 *
 * RETURNS:
 * New unique (relative to other objects in @dev) integer identifier for the
 * object.
 */
static int drm_mode_object_get(struct drm_device *dev,
			       struct drm_mode_object *obj, uint32_t obj_type)
{
	int new_id = 0;
	int ret;

again:
	if (idr_pre_get(&dev->mode_config.crtc_idr, GFP_KERNEL) == 0) {
		DRM_ERROR("Ran out memory getting a mode number\n");
		return -EINVAL;
	}

	mutex_lock(&dev->mode_config.idr_mutex);
	ret = idr_get_new_above(&dev->mode_config.crtc_idr, obj, 1, &new_id);
	mutex_unlock(&dev->mode_config.idr_mutex);
	if (ret == -EAGAIN)
		goto again;

	obj->id = new_id;
	obj->type = obj_type;
	return 0;
}

/**
 * drm_mode_object_put - free an identifer
 * @dev: DRM device
 * @id: ID to free
 *
 * LOCKING:
 * Caller must hold DRM mode_config lock.
 *
 * Free @id from @dev's unique identifier pool.
 */
static void drm_mode_object_put(struct drm_device *dev,
				struct drm_mode_object *object)
{
	mutex_lock(&dev->mode_config.idr_mutex);
	idr_remove(&dev->mode_config.crtc_idr, object->id);
	mutex_unlock(&dev->mode_config.idr_mutex);
}

void *drm_mode_object_find(struct drm_device *dev, uint32_t id, uint32_t type)
{
	struct drm_mode_object *obj = NULL;

	mutex_lock(&dev->mode_config.idr_mutex);
	obj = idr_find(&dev->mode_config.crtc_idr, id);
	if (!obj || (obj->type != type) || (obj->id != id))
		obj = NULL;
	mutex_unlock(&dev->mode_config.idr_mutex);

	return obj;
}
EXPORT_SYMBOL(drm_mode_object_find);

/**
 * drm_crtc_from_fb - find the CRTC structure associated with an fb
 * @dev: DRM device
 * @fb: framebuffer in question
 *
 * LOCKING:
 * Caller must hold mode_config lock.
 *
 * Find CRTC in the mode_config structure that matches @fb.
 *
 * RETURNS:
 * Pointer to the CRTC or NULL if it wasn't found.
 */
struct drm_crtc *drm_crtc_from_fb(struct drm_device *dev,
				  struct drm_framebuffer *fb)
{
	struct drm_crtc *crtc;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (crtc->fb == fb)
			return crtc;
	}
	return NULL;
}

/**
 * drm_framebuffer_init - initialize a framebuffer
 * @dev: DRM device
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Allocates an ID for the framebuffer's parent mode object, sets its mode
 * functions & device file and adds it to the master fd list.
 *
 * RETURNS:
 * Zero on success, error code on falure.
 */
int drm_framebuffer_init(struct drm_device *dev, struct drm_framebuffer *fb,
			 const struct drm_framebuffer_funcs *funcs)
{
	int ret;

	ret = drm_mode_object_get(dev, &fb->base, DRM_MODE_OBJECT_FB);
	if (ret) {
		return ret;
	}

	fb->dev = dev;
	fb->funcs = funcs;
	dev->mode_config.num_fb++;
	list_add(&fb->head, &dev->mode_config.fb_list);

	return 0;
}
EXPORT_SYMBOL(drm_framebuffer_init);

/**
 * drm_framebuffer_cleanup - remove a framebuffer object
 * @fb: framebuffer to remove
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Scans all the CRTCs in @dev's mode_config.  If they're using @fb, removes
 * it, setting it to NULL.
 */
void drm_framebuffer_cleanup(struct drm_framebuffer *fb)
{
	struct drm_device *dev = fb->dev;
	struct drm_crtc *crtc;

	/* remove from any CRTC */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (crtc->fb == fb)
			crtc->fb = NULL;
	}

	drm_mode_object_put(dev, &fb->base);
	list_del(&fb->head);
	dev->mode_config.num_fb--;
}
EXPORT_SYMBOL(drm_framebuffer_cleanup);

/**
 * drm_crtc_init - Initialise a new CRTC object
 * @dev: DRM device
 * @crtc: CRTC object to init
 * @funcs: callbacks for the new CRTC
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Inits a new object created as base part of an driver crtc object.
 */
void drm_crtc_init(struct drm_device *dev, struct drm_crtc *crtc,
		   const struct drm_crtc_funcs *funcs)
{
	crtc->dev = dev;
	crtc->funcs = funcs;

	mutex_lock(&dev->mode_config.mutex);
	drm_mode_object_get(dev, &crtc->base, DRM_MODE_OBJECT_CRTC);

	list_add_tail(&crtc->head, &dev->mode_config.crtc_list);
	dev->mode_config.num_crtc++;
	mutex_unlock(&dev->mode_config.mutex);
}
EXPORT_SYMBOL(drm_crtc_init);

/**
 * drm_crtc_cleanup - Cleans up the core crtc usage.
 * @crtc: CRTC to cleanup
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Cleanup @crtc. Removes from drm modesetting space
 * does NOT free object, caller does that.
 */
void drm_crtc_cleanup(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;

	if (crtc->gamma_store) {
		kfree(crtc->gamma_store);
		crtc->gamma_store = NULL;
	}

	drm_mode_object_put(dev, &crtc->base);
	list_del(&crtc->head);
	dev->mode_config.num_crtc--;
}
EXPORT_SYMBOL(drm_crtc_cleanup);

/**
 * drm_mode_probed_add - add a mode to a connector's probed mode list
 * @connector: connector the new mode
 * @mode: mode data
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Add @mode to @connector's mode list for later use.
 */
void drm_mode_probed_add(struct drm_connector *connector,
			 struct drm_display_mode *mode)
{
	list_add(&mode->head, &connector->probed_modes);
}
EXPORT_SYMBOL(drm_mode_probed_add);

/**
 * drm_mode_remove - remove and free a mode
 * @connector: connector list to modify
 * @mode: mode to remove
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Remove @mode from @connector's mode list, then free it.
 */
void drm_mode_remove(struct drm_connector *connector,
		     struct drm_display_mode *mode)
{
	list_del(&mode->head);
	kfree(mode);
}
EXPORT_SYMBOL(drm_mode_remove);

/**
 * drm_connector_init - Init a preallocated connector
 * @dev: DRM device
 * @connector: the connector to init
 * @funcs: callbacks for this connector
 * @name: user visible name of the connector
 *
 * LOCKING:
 * Caller must hold @dev's mode_config lock.
 *
 * Initialises a preallocated connector. Connectors should be
 * subclassed as part of driver connector objects.
 */
void drm_connector_init(struct drm_device *dev,
		     struct drm_connector *connector,
		     const struct drm_connector_funcs *funcs,
		     int connector_type)
{
	mutex_lock(&dev->mode_config.mutex);

	connector->dev = dev;
	connector->funcs = funcs;
	drm_mode_object_get(dev, &connector->base, DRM_MODE_OBJECT_CONNECTOR);
	connector->connector_type = connector_type;
	connector->connector_type_id =
		++drm_connector_enum_list[connector_type].count; /* TODO */
	INIT_LIST_HEAD(&connector->user_modes);
	INIT_LIST_HEAD(&connector->probed_modes);
	INIT_LIST_HEAD(&connector->modes);
	connector->edid_blob_ptr = NULL;

	list_add_tail(&connector->head, &dev->mode_config.connector_list);
	dev->mode_config.num_connector++;

	drm_connector_attach_property(connector,
				      dev->mode_config.edid_property, 0);

	drm_connector_attach_property(connector,
				      dev->mode_config.dpms_property, 0);

	mutex_unlock(&dev->mode_config.mutex);
}
EXPORT_SYMBOL(drm_connector_init);

/**
 * drm_connector_cleanup - cleans up an initialised connector
 * @connector: connector to cleanup
 *
 * LOCKING:
 * Caller must hold @dev's mode_config lock.
 *
 * Cleans up the connector but doesn't free the object.
 */
void drm_connector_cleanup(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode, *t;

	list_for_each_entry_safe(mode, t, &connector->probed_modes, head)
		drm_mode_remove(connector, mode);

	list_for_each_entry_safe(mode, t, &connector->modes, head)
		drm_mode_remove(connector, mode);

	list_for_each_entry_safe(mode, t, &connector->user_modes, head)
		drm_mode_remove(connector, mode);

	mutex_lock(&dev->mode_config.mutex);
	drm_mode_object_put(dev, &connector->base);
	list_del(&connector->head);
	mutex_unlock(&dev->mode_config.mutex);
}
EXPORT_SYMBOL(drm_connector_cleanup);

void drm_encoder_init(struct drm_device *dev,
		      struct drm_encoder *encoder,
		      const struct drm_encoder_funcs *funcs,
		      int encoder_type)
{
	mutex_lock(&dev->mode_config.mutex);

	encoder->dev = dev;

	drm_mode_object_get(dev, &encoder->base, DRM_MODE_OBJECT_ENCODER);
	encoder->encoder_type = encoder_type;
	encoder->funcs = funcs;

	list_add_tail(&encoder->head, &dev->mode_config.encoder_list);
	dev->mode_config.num_encoder++;

	mutex_unlock(&dev->mode_config.mutex);
}
EXPORT_SYMBOL(drm_encoder_init);

void drm_encoder_cleanup(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	mutex_lock(&dev->mode_config.mutex);
	drm_mode_object_put(dev, &encoder->base);
	list_del(&encoder->head);
	mutex_unlock(&dev->mode_config.mutex);
}
EXPORT_SYMBOL(drm_encoder_cleanup);

/**
 * drm_mode_create - create a new display mode
 * @dev: DRM device
 *
 * LOCKING:
 * Caller must hold DRM mode_config lock.
 *
 * Create a new drm_display_mode, give it an ID, and return it.
 *
 * RETURNS:
 * Pointer to new mode on success, NULL on error.
 */
struct drm_display_mode *drm_mode_create(struct drm_device *dev)
{
	struct drm_display_mode *nmode;

	nmode = kzalloc(sizeof(struct drm_display_mode), GFP_KERNEL);
	if (!nmode)
		return NULL;

	drm_mode_object_get(dev, &nmode->base, DRM_MODE_OBJECT_MODE);
	return nmode;
}
EXPORT_SYMBOL(drm_mode_create);

/**
 * drm_mode_destroy - remove a mode
 * @dev: DRM device
 * @mode: mode to remove
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Free @mode's unique identifier, then free it.
 */
void drm_mode_destroy(struct drm_device *dev, struct drm_display_mode *mode)
{
	drm_mode_object_put(dev, &mode->base);

	kfree(mode);
}
EXPORT_SYMBOL(drm_mode_destroy);

static int drm_mode_create_standard_connector_properties(struct drm_device *dev)
{
	struct drm_property *edid;
	struct drm_property *dpms;
	int i;

	/*
	 * Standard properties (apply to all connectors)
	 */
	edid = drm_property_create(dev, DRM_MODE_PROP_BLOB |
				   DRM_MODE_PROP_IMMUTABLE,
				   "EDID", 0);
	dev->mode_config.edid_property = edid;

	dpms = drm_property_create(dev, DRM_MODE_PROP_ENUM,
				   "DPMS", ARRAY_SIZE(drm_dpms_enum_list));
	for (i = 0; i < ARRAY_SIZE(drm_dpms_enum_list); i++)
		drm_property_add_enum(dpms, i, drm_dpms_enum_list[i].type,
				      drm_dpms_enum_list[i].name);
	dev->mode_config.dpms_property = dpms;

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
	int i;

	if (dev->mode_config.dvi_i_select_subconnector_property)
		return 0;

	dvi_i_selector =
		drm_property_create(dev, DRM_MODE_PROP_ENUM,
				    "select subconnector",
				    ARRAY_SIZE(drm_dvi_i_select_enum_list));
	for (i = 0; i < ARRAY_SIZE(drm_dvi_i_select_enum_list); i++)
		drm_property_add_enum(dvi_i_selector, i,
				      drm_dvi_i_select_enum_list[i].type,
				      drm_dvi_i_select_enum_list[i].name);
	dev->mode_config.dvi_i_select_subconnector_property = dvi_i_selector;

	dvi_i_subconnector =
		drm_property_create(dev, DRM_MODE_PROP_ENUM |
				    DRM_MODE_PROP_IMMUTABLE,
				    "subconnector",
				    ARRAY_SIZE(drm_dvi_i_subconnector_enum_list));
	for (i = 0; i < ARRAY_SIZE(drm_dvi_i_subconnector_enum_list); i++)
		drm_property_add_enum(dvi_i_subconnector, i,
				      drm_dvi_i_subconnector_enum_list[i].type,
				      drm_dvi_i_subconnector_enum_list[i].name);
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
int drm_mode_create_tv_properties(struct drm_device *dev, int num_modes,
				  char *modes[])
{
	struct drm_property *tv_selector;
	struct drm_property *tv_subconnector;
	int i;

	if (dev->mode_config.tv_select_subconnector_property)
		return 0;

	/*
	 * Basic connector properties
	 */
	tv_selector = drm_property_create(dev, DRM_MODE_PROP_ENUM,
					  "select subconnector",
					  ARRAY_SIZE(drm_tv_select_enum_list));
	for (i = 0; i < ARRAY_SIZE(drm_tv_select_enum_list); i++)
		drm_property_add_enum(tv_selector, i,
				      drm_tv_select_enum_list[i].type,
				      drm_tv_select_enum_list[i].name);
	dev->mode_config.tv_select_subconnector_property = tv_selector;

	tv_subconnector =
		drm_property_create(dev, DRM_MODE_PROP_ENUM |
				    DRM_MODE_PROP_IMMUTABLE, "subconnector",
				    ARRAY_SIZE(drm_tv_subconnector_enum_list));
	for (i = 0; i < ARRAY_SIZE(drm_tv_subconnector_enum_list); i++)
		drm_property_add_enum(tv_subconnector, i,
				      drm_tv_subconnector_enum_list[i].type,
				      drm_tv_subconnector_enum_list[i].name);
	dev->mode_config.tv_subconnector_property = tv_subconnector;

	/*
	 * Other, TV specific properties: margins & TV modes.
	 */
	dev->mode_config.tv_left_margin_property =
		drm_property_create(dev, DRM_MODE_PROP_RANGE,
				    "left margin", 2);
	dev->mode_config.tv_left_margin_property->values[0] = 0;
	dev->mode_config.tv_left_margin_property->values[1] = 100;

	dev->mode_config.tv_right_margin_property =
		drm_property_create(dev, DRM_MODE_PROP_RANGE,
				    "right margin", 2);
	dev->mode_config.tv_right_margin_property->values[0] = 0;
	dev->mode_config.tv_right_margin_property->values[1] = 100;

	dev->mode_config.tv_top_margin_property =
		drm_property_create(dev, DRM_MODE_PROP_RANGE,
				    "top margin", 2);
	dev->mode_config.tv_top_margin_property->values[0] = 0;
	dev->mode_config.tv_top_margin_property->values[1] = 100;

	dev->mode_config.tv_bottom_margin_property =
		drm_property_create(dev, DRM_MODE_PROP_RANGE,
				    "bottom margin", 2);
	dev->mode_config.tv_bottom_margin_property->values[0] = 0;
	dev->mode_config.tv_bottom_margin_property->values[1] = 100;

	dev->mode_config.tv_mode_property =
		drm_property_create(dev, DRM_MODE_PROP_ENUM,
				    "mode", num_modes);
	for (i = 0; i < num_modes; i++)
		drm_property_add_enum(dev->mode_config.tv_mode_property, i,
				      i, modes[i]);

	return 0;
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
	int i;

	if (dev->mode_config.scaling_mode_property)
		return 0;

	scaling_mode =
		drm_property_create(dev, DRM_MODE_PROP_ENUM, "scaling mode",
				    ARRAY_SIZE(drm_scaling_mode_enum_list));
	for (i = 0; i < ARRAY_SIZE(drm_scaling_mode_enum_list); i++)
		drm_property_add_enum(scaling_mode, i,
				      drm_scaling_mode_enum_list[i].type,
				      drm_scaling_mode_enum_list[i].name);

	dev->mode_config.scaling_mode_property = scaling_mode;

	return 0;
}
EXPORT_SYMBOL(drm_mode_create_scaling_mode_property);

/**
 * drm_mode_create_dithering_property - create dithering property
 * @dev: DRM device
 *
 * Called by a driver the first time it's needed, must be attached to desired
 * connectors.
 */
int drm_mode_create_dithering_property(struct drm_device *dev)
{
	struct drm_property *dithering_mode;
	int i;

	if (dev->mode_config.dithering_mode_property)
		return 0;

	dithering_mode =
		drm_property_create(dev, DRM_MODE_PROP_ENUM, "dithering",
				    ARRAY_SIZE(drm_dithering_mode_enum_list));
	for (i = 0; i < ARRAY_SIZE(drm_dithering_mode_enum_list); i++)
		drm_property_add_enum(dithering_mode, i,
				      drm_dithering_mode_enum_list[i].type,
				      drm_dithering_mode_enum_list[i].name);
	dev->mode_config.dithering_mode_property = dithering_mode;

	return 0;
}
EXPORT_SYMBOL(drm_mode_create_dithering_property);

/**
 * drm_mode_config_init - initialize DRM mode_configuration structure
 * @dev: DRM device
 *
 * LOCKING:
 * None, should happen single threaded at init time.
 *
 * Initialize @dev's mode_config structure, used for tracking the graphics
 * configuration of @dev.
 */
void drm_mode_config_init(struct drm_device *dev)
{
	mutex_init(&dev->mode_config.mutex);
	mutex_init(&dev->mode_config.idr_mutex);
	INIT_LIST_HEAD(&dev->mode_config.fb_list);
	INIT_LIST_HEAD(&dev->mode_config.fb_kernel_list);
	INIT_LIST_HEAD(&dev->mode_config.crtc_list);
	INIT_LIST_HEAD(&dev->mode_config.connector_list);
	INIT_LIST_HEAD(&dev->mode_config.encoder_list);
	INIT_LIST_HEAD(&dev->mode_config.property_list);
	INIT_LIST_HEAD(&dev->mode_config.property_blob_list);
	idr_init(&dev->mode_config.crtc_idr);

	mutex_lock(&dev->mode_config.mutex);
	drm_mode_create_standard_connector_properties(dev);
	mutex_unlock(&dev->mode_config.mutex);

	/* Just to be sure */
	dev->mode_config.num_fb = 0;
	dev->mode_config.num_connector = 0;
	dev->mode_config.num_crtc = 0;
	dev->mode_config.num_encoder = 0;
}
EXPORT_SYMBOL(drm_mode_config_init);

int drm_mode_group_init(struct drm_device *dev, struct drm_mode_group *group)
{
	uint32_t total_objects = 0;

	total_objects += dev->mode_config.num_crtc;
	total_objects += dev->mode_config.num_connector;
	total_objects += dev->mode_config.num_encoder;

	if (total_objects == 0)
		return -EINVAL;

	group->id_list = kzalloc(total_objects * sizeof(uint32_t), GFP_KERNEL);
	if (!group->id_list)
		return -ENOMEM;

	group->num_crtcs = 0;
	group->num_connectors = 0;
	group->num_encoders = 0;
	return 0;
}

int drm_mode_group_init_legacy_group(struct drm_device *dev,
				     struct drm_mode_group *group)
{
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int ret;

	if ((ret = drm_mode_group_init(dev, group)))
		return ret;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		group->id_list[group->num_crtcs++] = crtc->base.id;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head)
		group->id_list[group->num_crtcs + group->num_encoders++] =
		encoder->base.id;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		group->id_list[group->num_crtcs + group->num_encoders +
			       group->num_connectors++] = connector->base.id;

	return 0;
}

/**
 * drm_mode_config_cleanup - free up DRM mode_config info
 * @dev: DRM device
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Free up all the connectors and CRTCs associated with this DRM device, then
 * free up the framebuffers and associated buffer objects.
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

	list_for_each_entry_safe(fb, fbt, &dev->mode_config.fb_list, head) {
		fb->funcs->destroy(fb);
	}

	list_for_each_entry_safe(crtc, ct, &dev->mode_config.crtc_list, head) {
		crtc->funcs->destroy(crtc);
	}

}
EXPORT_SYMBOL(drm_mode_config_cleanup);

/**
 * drm_crtc_convert_to_umode - convert a drm_display_mode into a modeinfo
 * @out: drm_mode_modeinfo struct to return to the user
 * @in: drm_display_mode to use
 *
 * LOCKING:
 * None.
 *
 * Convert a drm_display_mode into a drm_mode_modeinfo structure to return to
 * the user.
 */
void drm_crtc_convert_to_umode(struct drm_mode_modeinfo *out,
			       struct drm_display_mode *in)
{
	out->clock = in->clock;
	out->hdisplay = in->hdisplay;
	out->hsync_start = in->hsync_start;
	out->hsync_end = in->hsync_end;
	out->htotal = in->htotal;
	out->hskew = in->hskew;
	out->vdisplay = in->vdisplay;
	out->vsync_start = in->vsync_start;
	out->vsync_end = in->vsync_end;
	out->vtotal = in->vtotal;
	out->vscan = in->vscan;
	out->vrefresh = in->vrefresh;
	out->flags = in->flags;
	out->type = in->type;
	strncpy(out->name, in->name, DRM_DISPLAY_MODE_LEN);
	out->name[DRM_DISPLAY_MODE_LEN-1] = 0;
}

/**
 * drm_crtc_convert_to_umode - convert a modeinfo into a drm_display_mode
 * @out: drm_display_mode to return to the user
 * @in: drm_mode_modeinfo to use
 *
 * LOCKING:
 * None.
 *
 * Convert a drm_mode_modeinfo into a drm_display_mode structure to return to
 * the caller.
 */
void drm_crtc_convert_umode(struct drm_display_mode *out,
			    struct drm_mode_modeinfo *in)
{
	out->clock = in->clock;
	out->hdisplay = in->hdisplay;
	out->hsync_start = in->hsync_start;
	out->hsync_end = in->hsync_end;
	out->htotal = in->htotal;
	out->hskew = in->hskew;
	out->vdisplay = in->vdisplay;
	out->vsync_start = in->vsync_start;
	out->vsync_end = in->vsync_end;
	out->vtotal = in->vtotal;
	out->vscan = in->vscan;
	out->vrefresh = in->vrefresh;
	out->flags = in->flags;
	out->type = in->type;
	strncpy(out->name, in->name, DRM_DISPLAY_MODE_LEN);
	out->name[DRM_DISPLAY_MODE_LEN-1] = 0;
}

/**
 * drm_mode_getresources - get graphics configuration
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Construct a set of configuration description structures and return
 * them to the user, including CRTC, connector and framebuffer configuration.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
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
	int copied = 0, i;
	uint32_t __user *fb_id;
	uint32_t __user *crtc_id;
	uint32_t __user *connector_id;
	uint32_t __user *encoder_id;
	struct drm_mode_group *mode_group;

	mutex_lock(&dev->mode_config.mutex);

	/*
	 * For the non-control nodes we need to limit the list of resources
	 * by IDs in the group list for this node
	 */
	list_for_each(lh, &file_priv->fbs)
		fb_count++;

	mode_group = &file_priv->master->minor->mode_group;
	if (file_priv->master->minor->type == DRM_MINOR_CONTROL) {

		list_for_each(lh, &dev->mode_config.crtc_list)
			crtc_count++;

		list_for_each(lh, &dev->mode_config.connector_list)
			connector_count++;

		list_for_each(lh, &dev->mode_config.encoder_list)
			encoder_count++;
	} else {

		crtc_count = mode_group->num_crtcs;
		connector_count = mode_group->num_connectors;
		encoder_count = mode_group->num_encoders;
	}

	card_res->max_height = dev->mode_config.max_height;
	card_res->min_height = dev->mode_config.min_height;
	card_res->max_width = dev->mode_config.max_width;
	card_res->min_width = dev->mode_config.min_width;

	/* handle this in 4 parts */
	/* FBs */
	if (card_res->count_fbs >= fb_count) {
		copied = 0;
		fb_id = (uint32_t __user *)(unsigned long)card_res->fb_id_ptr;
		list_for_each_entry(fb, &file_priv->fbs, head) {
			if (put_user(fb->base.id, fb_id + copied)) {
				ret = -EFAULT;
				goto out;
			}
			copied++;
		}
	}
	card_res->count_fbs = fb_count;

	/* CRTCs */
	if (card_res->count_crtcs >= crtc_count) {
		copied = 0;
		crtc_id = (uint32_t __user *)(unsigned long)card_res->crtc_id_ptr;
		if (file_priv->master->minor->type == DRM_MINOR_CONTROL) {
			list_for_each_entry(crtc, &dev->mode_config.crtc_list,
					    head) {
				DRM_DEBUG("CRTC ID is %d\n", crtc->base.id);
				if (put_user(crtc->base.id, crtc_id + copied)) {
					ret = -EFAULT;
					goto out;
				}
				copied++;
			}
		} else {
			for (i = 0; i < mode_group->num_crtcs; i++) {
				if (put_user(mode_group->id_list[i],
					     crtc_id + copied)) {
					ret = -EFAULT;
					goto out;
				}
				copied++;
			}
		}
	}
	card_res->count_crtcs = crtc_count;

	/* Encoders */
	if (card_res->count_encoders >= encoder_count) {
		copied = 0;
		encoder_id = (uint32_t __user *)(unsigned long)card_res->encoder_id_ptr;
		if (file_priv->master->minor->type == DRM_MINOR_CONTROL) {
			list_for_each_entry(encoder,
					    &dev->mode_config.encoder_list,
					    head) {
				DRM_DEBUG("ENCODER ID is %d\n",
					  encoder->base.id);
				if (put_user(encoder->base.id, encoder_id +
					     copied)) {
					ret = -EFAULT;
					goto out;
				}
				copied++;
			}
		} else {
			for (i = mode_group->num_crtcs; i < mode_group->num_crtcs + mode_group->num_encoders; i++) {
				if (put_user(mode_group->id_list[i],
					     encoder_id + copied)) {
					ret = -EFAULT;
					goto out;
				}
				copied++;
			}

		}
	}
	card_res->count_encoders = encoder_count;

	/* Connectors */
	if (card_res->count_connectors >= connector_count) {
		copied = 0;
		connector_id = (uint32_t __user *)(unsigned long)card_res->connector_id_ptr;
		if (file_priv->master->minor->type == DRM_MINOR_CONTROL) {
			list_for_each_entry(connector,
					    &dev->mode_config.connector_list,
					    head) {
				DRM_DEBUG("CONNECTOR ID is %d\n",
					  connector->base.id);
				if (put_user(connector->base.id,
					     connector_id + copied)) {
					ret = -EFAULT;
					goto out;
				}
				copied++;
			}
		} else {
			int start = mode_group->num_crtcs +
				mode_group->num_encoders;
			for (i = start; i < start + mode_group->num_connectors; i++) {
				if (put_user(mode_group->id_list[i],
					     connector_id + copied)) {
					ret = -EFAULT;
					goto out;
				}
				copied++;
			}
		}
	}
	card_res->count_connectors = connector_count;

	DRM_DEBUG("Counted %d %d %d\n", card_res->count_crtcs,
		  card_res->count_connectors, card_res->count_encoders);

out:
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}

/**
 * drm_mode_getcrtc - get CRTC configuration
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Caller? (FIXME)
 *
 * Construct a CRTC configuration structure to return to the user.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_getcrtc(struct drm_device *dev,
		     void *data, struct drm_file *file_priv)
{
	struct drm_mode_crtc *crtc_resp = data;
	struct drm_crtc *crtc;
	struct drm_mode_object *obj;
	int ret = 0;

	mutex_lock(&dev->mode_config.mutex);

	obj = drm_mode_object_find(dev, crtc_resp->crtc_id,
				   DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		ret = -EINVAL;
		goto out;
	}
	crtc = obj_to_crtc(obj);

	crtc_resp->x = crtc->x;
	crtc_resp->y = crtc->y;
	crtc_resp->gamma_size = crtc->gamma_size;
	if (crtc->fb)
		crtc_resp->fb_id = crtc->fb->base.id;
	else
		crtc_resp->fb_id = 0;

	if (crtc->enabled) {

		drm_crtc_convert_to_umode(&crtc_resp->mode, &crtc->mode);
		crtc_resp->mode_valid = 1;

	} else {
		crtc_resp->mode_valid = 0;
	}

out:
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}

/**
 * drm_mode_getconnector - get connector configuration
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Caller? (FIXME)
 *
 * Construct a connector configuration structure to return to the user.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_getconnector(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_mode_get_connector *out_resp = data;
	struct drm_mode_object *obj;
	struct drm_connector *connector;
	struct drm_display_mode *mode;
	int mode_count = 0;
	int props_count = 0;
	int encoders_count = 0;
	int ret = 0;
	int copied = 0;
	int i;
	struct drm_mode_modeinfo u_mode;
	struct drm_mode_modeinfo __user *mode_ptr;
	uint32_t __user *prop_ptr;
	uint64_t __user *prop_values;
	uint32_t __user *encoder_ptr;

	memset(&u_mode, 0, sizeof(struct drm_mode_modeinfo));

	DRM_DEBUG("connector id %d:\n", out_resp->connector_id);

	mutex_lock(&dev->mode_config.mutex);

	obj = drm_mode_object_find(dev, out_resp->connector_id,
				   DRM_MODE_OBJECT_CONNECTOR);
	if (!obj) {
		ret = -EINVAL;
		goto out;
	}
	connector = obj_to_connector(obj);

	for (i = 0; i < DRM_CONNECTOR_MAX_PROPERTY; i++) {
		if (connector->property_ids[i] != 0) {
			props_count++;
		}
	}

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		if (connector->encoder_ids[i] != 0) {
			encoders_count++;
		}
	}

	if (out_resp->count_modes == 0) {
		connector->funcs->fill_modes(connector,
					     dev->mode_config.max_width,
					     dev->mode_config.max_height);
	}

	/* delayed so we get modes regardless of pre-fill_modes state */
	list_for_each_entry(mode, &connector->modes, head)
		mode_count++;

	out_resp->connector_id = connector->base.id;
	out_resp->connector_type = connector->connector_type;
	out_resp->connector_type_id = connector->connector_type_id;
	out_resp->mm_width = connector->display_info.width_mm;
	out_resp->mm_height = connector->display_info.height_mm;
	out_resp->subpixel = connector->display_info.subpixel_order;
	out_resp->connection = connector->status;
	if (connector->encoder)
		out_resp->encoder_id = connector->encoder->base.id;
	else
		out_resp->encoder_id = 0;

	/*
	 * This ioctl is called twice, once to determine how much space is
	 * needed, and the 2nd time to fill it.
	 */
	if ((out_resp->count_modes >= mode_count) && mode_count) {
		copied = 0;
		mode_ptr = (struct drm_mode_modeinfo *)(unsigned long)out_resp->modes_ptr;
		list_for_each_entry(mode, &connector->modes, head) {
			drm_crtc_convert_to_umode(&u_mode, mode);
			if (copy_to_user(mode_ptr + copied,
					 &u_mode, sizeof(u_mode))) {
				ret = -EFAULT;
				goto out;
			}
			copied++;
		}
	}
	out_resp->count_modes = mode_count;

	if ((out_resp->count_props >= props_count) && props_count) {
		copied = 0;
		prop_ptr = (uint32_t *)(unsigned long)(out_resp->props_ptr);
		prop_values = (uint64_t *)(unsigned long)(out_resp->prop_values_ptr);
		for (i = 0; i < DRM_CONNECTOR_MAX_PROPERTY; i++) {
			if (connector->property_ids[i] != 0) {
				if (put_user(connector->property_ids[i],
					     prop_ptr + copied)) {
					ret = -EFAULT;
					goto out;
				}

				if (put_user(connector->property_values[i],
					     prop_values + copied)) {
					ret = -EFAULT;
					goto out;
				}
				copied++;
			}
		}
	}
	out_resp->count_props = props_count;

	if ((out_resp->count_encoders >= encoders_count) && encoders_count) {
		copied = 0;
		encoder_ptr = (uint32_t *)(unsigned long)(out_resp->encoders_ptr);
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
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}

int drm_mode_getencoder(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_mode_get_encoder *enc_resp = data;
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;
	int ret = 0;

	mutex_lock(&dev->mode_config.mutex);
	obj = drm_mode_object_find(dev, enc_resp->encoder_id,
				   DRM_MODE_OBJECT_ENCODER);
	if (!obj) {
		ret = -EINVAL;
		goto out;
	}
	encoder = obj_to_encoder(obj);

	if (encoder->crtc)
		enc_resp->crtc_id = encoder->crtc->base.id;
	else
		enc_resp->crtc_id = 0;
	enc_resp->encoder_type = encoder->encoder_type;
	enc_resp->encoder_id = encoder->base.id;
	enc_resp->possible_crtcs = encoder->possible_crtcs;
	enc_resp->possible_clones = encoder->possible_clones;

out:
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}

/**
 * drm_mode_setcrtc - set CRTC configuration
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Caller? (FIXME)
 *
 * Build a new CRTC configuration based on user request.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_setcrtc(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_mode_crtc *crtc_req = data;
	struct drm_mode_object *obj;
	struct drm_crtc *crtc, *crtcfb;
	struct drm_connector **connector_set = NULL, *connector;
	struct drm_framebuffer *fb = NULL;
	struct drm_display_mode *mode = NULL;
	struct drm_mode_set set;
	uint32_t __user *set_connectors_ptr;
	int ret = 0;
	int i;

	mutex_lock(&dev->mode_config.mutex);
	obj = drm_mode_object_find(dev, crtc_req->crtc_id,
				   DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		DRM_DEBUG("Unknown CRTC ID %d\n", crtc_req->crtc_id);
		ret = -EINVAL;
		goto out;
	}
	crtc = obj_to_crtc(obj);

	if (crtc_req->mode_valid) {
		/* If we have a mode we need a framebuffer. */
		/* If we pass -1, set the mode with the currently bound fb */
		if (crtc_req->fb_id == -1) {
			list_for_each_entry(crtcfb,
					    &dev->mode_config.crtc_list, head) {
				if (crtcfb == crtc) {
					DRM_DEBUG("Using current fb for setmode\n");
					fb = crtc->fb;
				}
			}
		} else {
			obj = drm_mode_object_find(dev, crtc_req->fb_id,
						   DRM_MODE_OBJECT_FB);
			if (!obj) {
				DRM_DEBUG("Unknown FB ID%d\n", crtc_req->fb_id);
				ret = -EINVAL;
				goto out;
			}
			fb = obj_to_fb(obj);
		}

		mode = drm_mode_create(dev);
		drm_crtc_convert_umode(mode, &crtc_req->mode);
		drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V);
	}

	if (crtc_req->count_connectors == 0 && mode) {
		DRM_DEBUG("Count connectors is 0 but mode set\n");
		ret = -EINVAL;
		goto out;
	}

	if (crtc_req->count_connectors > 0 && !mode && !fb) {
		DRM_DEBUG("Count connectors is %d but no mode or fb set\n",
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

		connector_set = kmalloc(crtc_req->count_connectors *
					sizeof(struct drm_connector *),
					GFP_KERNEL);
		if (!connector_set) {
			ret = -ENOMEM;
			goto out;
		}

		for (i = 0; i < crtc_req->count_connectors; i++) {
			set_connectors_ptr = (uint32_t *)(unsigned long)crtc_req->set_connectors_ptr;
			if (get_user(out_id, &set_connectors_ptr[i])) {
				ret = -EFAULT;
				goto out;
			}

			obj = drm_mode_object_find(dev, out_id,
						   DRM_MODE_OBJECT_CONNECTOR);
			if (!obj) {
				DRM_DEBUG("Connector id %d unknown\n", out_id);
				ret = -EINVAL;
				goto out;
			}
			connector = obj_to_connector(obj);

			connector_set[i] = connector;
		}
	}

	set.crtc = crtc;
	set.x = crtc_req->x;
	set.y = crtc_req->y;
	set.mode = mode;
	set.connectors = connector_set;
	set.num_connectors = crtc_req->count_connectors;
	set.fb =fb;
	ret = crtc->funcs->set_config(&set);

out:
	kfree(connector_set);
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}

int drm_mode_cursor_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv)
{
	struct drm_mode_cursor *req = data;
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	int ret = 0;

	DRM_DEBUG("\n");

	if (!req->flags) {
		DRM_ERROR("no operation set\n");
		return -EINVAL;
	}

	mutex_lock(&dev->mode_config.mutex);
	obj = drm_mode_object_find(dev, req->crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		DRM_DEBUG("Unknown CRTC ID %d\n", req->crtc_id);
		ret = -EINVAL;
		goto out;
	}
	crtc = obj_to_crtc(obj);

	if (req->flags & DRM_MODE_CURSOR_BO) {
		if (!crtc->funcs->cursor_set) {
			DRM_ERROR("crtc does not support cursor\n");
			ret = -ENXIO;
			goto out;
		}
		/* Turns off the cursor if handle is 0 */
		ret = crtc->funcs->cursor_set(crtc, file_priv, req->handle,
					      req->width, req->height);
	}

	if (req->flags & DRM_MODE_CURSOR_MOVE) {
		if (crtc->funcs->cursor_move) {
			ret = crtc->funcs->cursor_move(crtc, req->x, req->y);
		} else {
			DRM_ERROR("crtc does not support cursor\n");
			ret = -EFAULT;
			goto out;
		}
	}
out:
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}

/**
 * drm_mode_addfb - add an FB to the graphics configuration
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Add a new FB to the specified CRTC, given a user request.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_addfb(struct drm_device *dev,
		   void *data, struct drm_file *file_priv)
{
	struct drm_mode_fb_cmd *r = data;
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_framebuffer *fb;
	int ret = 0;

	if ((config->min_width > r->width) || (r->width > config->max_width)) {
		DRM_ERROR("mode new framebuffer width not within limits\n");
		return -EINVAL;
	}
	if ((config->min_height > r->height) || (r->height > config->max_height)) {
		DRM_ERROR("mode new framebuffer height not within limits\n");
		return -EINVAL;
	}

	mutex_lock(&dev->mode_config.mutex);

	/* TODO check buffer is sufficently large */
	/* TODO setup destructor callback */

	fb = dev->mode_config.funcs->fb_create(dev, file_priv, r);
	if (!fb) {
		DRM_ERROR("could not create framebuffer\n");
		ret = -EINVAL;
		goto out;
	}

	r->fb_id = fb->base.id;
	list_add(&fb->filp_head, &file_priv->fbs);

out:
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}

/**
 * drm_mode_rmfb - remove an FB from the configuration
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Remove the FB specified by the user.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_rmfb(struct drm_device *dev,
		   void *data, struct drm_file *file_priv)
{
	struct drm_mode_object *obj;
	struct drm_framebuffer *fb = NULL;
	struct drm_framebuffer *fbl = NULL;
	uint32_t *id = data;
	int ret = 0;
	int found = 0;

	mutex_lock(&dev->mode_config.mutex);
	obj = drm_mode_object_find(dev, *id, DRM_MODE_OBJECT_FB);
	/* TODO check that we realy get a framebuffer back. */
	if (!obj) {
		DRM_ERROR("mode invalid framebuffer id\n");
		ret = -EINVAL;
		goto out;
	}
	fb = obj_to_fb(obj);

	list_for_each_entry(fbl, &file_priv->fbs, filp_head)
		if (fb == fbl)
			found = 1;

	if (!found) {
		DRM_ERROR("tried to remove a fb that we didn't own\n");
		ret = -EINVAL;
		goto out;
	}

	/* TODO release all crtc connected to the framebuffer */
	/* TODO unhock the destructor from the buffer object */

	list_del(&fb->filp_head);
	fb->funcs->destroy(fb);

out:
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}

/**
 * drm_mode_getfb - get FB info
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Caller? (FIXME)
 *
 * Lookup the FB given its ID and return info about it.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_getfb(struct drm_device *dev,
		   void *data, struct drm_file *file_priv)
{
	struct drm_mode_fb_cmd *r = data;
	struct drm_mode_object *obj;
	struct drm_framebuffer *fb;
	int ret = 0;

	mutex_lock(&dev->mode_config.mutex);
	obj = drm_mode_object_find(dev, r->fb_id, DRM_MODE_OBJECT_FB);
	if (!obj) {
		DRM_ERROR("invalid framebuffer id\n");
		ret = -EINVAL;
		goto out;
	}
	fb = obj_to_fb(obj);

	r->height = fb->height;
	r->width = fb->width;
	r->depth = fb->depth;
	r->bpp = fb->bits_per_pixel;
	r->pitch = fb->pitch;
	fb->funcs->create_handle(fb, file_priv, &r->handle);

out:
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}

/**
 * drm_fb_release - remove and free the FBs on this file
 * @filp: file * from the ioctl
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Destroy all the FBs associated with @filp.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
void drm_fb_release(struct file *filp)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;
	struct drm_framebuffer *fb, *tfb;

	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry_safe(fb, tfb, &priv->fbs, filp_head) {
		list_del(&fb->filp_head);
		fb->funcs->destroy(fb);
	}
	mutex_unlock(&dev->mode_config.mutex);
}

/**
 * drm_mode_attachmode - add a mode to the user mode list
 * @dev: DRM device
 * @connector: connector to add the mode to
 * @mode: mode to add
 *
 * Add @mode to @connector's user mode list.
 */
static int drm_mode_attachmode(struct drm_device *dev,
			       struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	int ret = 0;

	list_add_tail(&mode->head, &connector->user_modes);
	return ret;
}

int drm_mode_attachmode_crtc(struct drm_device *dev, struct drm_crtc *crtc,
			     struct drm_display_mode *mode)
{
	struct drm_connector *connector;
	int ret = 0;
	struct drm_display_mode *dup_mode;
	int need_dup = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (!connector->encoder)
			break;
		if (connector->encoder->crtc == crtc) {
			if (need_dup)
				dup_mode = drm_mode_duplicate(dev, mode);
			else
				dup_mode = mode;
			ret = drm_mode_attachmode(dev, connector, dup_mode);
			if (ret)
				return ret;
			need_dup = 1;
		}
	}
	return 0;
}
EXPORT_SYMBOL(drm_mode_attachmode_crtc);

static int drm_mode_detachmode(struct drm_device *dev,
			       struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	int found = 0;
	int ret = 0;
	struct drm_display_mode *match_mode, *t;

	list_for_each_entry_safe(match_mode, t, &connector->user_modes, head) {
		if (drm_mode_equal(match_mode, mode)) {
			list_del(&match_mode->head);
			drm_mode_destroy(dev, match_mode);
			found = 1;
			break;
		}
	}

	if (!found)
		ret = -EINVAL;

	return ret;
}

int drm_mode_detachmode_crtc(struct drm_device *dev, struct drm_display_mode *mode)
{
	struct drm_connector *connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		drm_mode_detachmode(dev, connector, mode);
	}
	return 0;
}
EXPORT_SYMBOL(drm_mode_detachmode_crtc);

/**
 * drm_fb_attachmode - Attach a user mode to an connector
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * This attaches a user specified mode to an connector.
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_attachmode_ioctl(struct drm_device *dev,
			      void *data, struct drm_file *file_priv)
{
	struct drm_mode_mode_cmd *mode_cmd = data;
	struct drm_connector *connector;
	struct drm_display_mode *mode;
	struct drm_mode_object *obj;
	struct drm_mode_modeinfo *umode = &mode_cmd->mode;
	int ret = 0;

	mutex_lock(&dev->mode_config.mutex);

	obj = drm_mode_object_find(dev, mode_cmd->connector_id, DRM_MODE_OBJECT_CONNECTOR);
	if (!obj) {
		ret = -EINVAL;
		goto out;
	}
	connector = obj_to_connector(obj);

	mode = drm_mode_create(dev);
	if (!mode) {
		ret = -ENOMEM;
		goto out;
	}

	drm_crtc_convert_umode(mode, umode);

	ret = drm_mode_attachmode(dev, connector, mode);
out:
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}


/**
 * drm_fb_detachmode - Detach a user specified mode from an connector
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_detachmode_ioctl(struct drm_device *dev,
			      void *data, struct drm_file *file_priv)
{
	struct drm_mode_object *obj;
	struct drm_mode_mode_cmd *mode_cmd = data;
	struct drm_connector *connector;
	struct drm_display_mode mode;
	struct drm_mode_modeinfo *umode = &mode_cmd->mode;
	int ret = 0;

	mutex_lock(&dev->mode_config.mutex);

	obj = drm_mode_object_find(dev, mode_cmd->connector_id, DRM_MODE_OBJECT_CONNECTOR);
	if (!obj) {
		ret = -EINVAL;
		goto out;
	}
	connector = obj_to_connector(obj);

	drm_crtc_convert_umode(&mode, umode);
	ret = drm_mode_detachmode(dev, connector, &mode);
out:
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}

struct drm_property *drm_property_create(struct drm_device *dev, int flags,
					 const char *name, int num_values)
{
	struct drm_property *property = NULL;

	property = kzalloc(sizeof(struct drm_property), GFP_KERNEL);
	if (!property)
		return NULL;

	if (num_values) {
		property->values = kzalloc(sizeof(uint64_t)*num_values, GFP_KERNEL);
		if (!property->values)
			goto fail;
	}

	drm_mode_object_get(dev, &property->base, DRM_MODE_OBJECT_PROPERTY);
	property->flags = flags;
	property->num_values = num_values;
	INIT_LIST_HEAD(&property->enum_blob_list);

	if (name)
		strncpy(property->name, name, DRM_PROP_NAME_LEN);

	list_add_tail(&property->head, &dev->mode_config.property_list);
	return property;
fail:
	kfree(property);
	return NULL;
}
EXPORT_SYMBOL(drm_property_create);

int drm_property_add_enum(struct drm_property *property, int index,
			  uint64_t value, const char *name)
{
	struct drm_property_enum *prop_enum;

	if (!(property->flags & DRM_MODE_PROP_ENUM))
		return -EINVAL;

	if (!list_empty(&property->enum_blob_list)) {
		list_for_each_entry(prop_enum, &property->enum_blob_list, head) {
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
	list_add_tail(&prop_enum->head, &property->enum_blob_list);
	return 0;
}
EXPORT_SYMBOL(drm_property_add_enum);

void drm_property_destroy(struct drm_device *dev, struct drm_property *property)
{
	struct drm_property_enum *prop_enum, *pt;

	list_for_each_entry_safe(prop_enum, pt, &property->enum_blob_list, head) {
		list_del(&prop_enum->head);
		kfree(prop_enum);
	}

	if (property->num_values)
		kfree(property->values);
	drm_mode_object_put(dev, &property->base);
	list_del(&property->head);
	kfree(property);
}
EXPORT_SYMBOL(drm_property_destroy);

int drm_connector_attach_property(struct drm_connector *connector,
			       struct drm_property *property, uint64_t init_val)
{
	int i;

	for (i = 0; i < DRM_CONNECTOR_MAX_PROPERTY; i++) {
		if (connector->property_ids[i] == 0) {
			connector->property_ids[i] = property->base.id;
			connector->property_values[i] = init_val;
			break;
		}
	}

	if (i == DRM_CONNECTOR_MAX_PROPERTY)
		return -EINVAL;
	return 0;
}
EXPORT_SYMBOL(drm_connector_attach_property);

int drm_connector_property_set_value(struct drm_connector *connector,
				  struct drm_property *property, uint64_t value)
{
	int i;

	for (i = 0; i < DRM_CONNECTOR_MAX_PROPERTY; i++) {
		if (connector->property_ids[i] == property->base.id) {
			connector->property_values[i] = value;
			break;
		}
	}

	if (i == DRM_CONNECTOR_MAX_PROPERTY)
		return -EINVAL;
	return 0;
}
EXPORT_SYMBOL(drm_connector_property_set_value);

int drm_connector_property_get_value(struct drm_connector *connector,
				  struct drm_property *property, uint64_t *val)
{
	int i;

	for (i = 0; i < DRM_CONNECTOR_MAX_PROPERTY; i++) {
		if (connector->property_ids[i] == property->base.id) {
			*val = connector->property_values[i];
			break;
		}
	}

	if (i == DRM_CONNECTOR_MAX_PROPERTY)
		return -EINVAL;
	return 0;
}
EXPORT_SYMBOL(drm_connector_property_get_value);

int drm_mode_getproperty_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv)
{
	struct drm_mode_object *obj;
	struct drm_mode_get_property *out_resp = data;
	struct drm_property *property;
	int enum_count = 0;
	int blob_count = 0;
	int value_count = 0;
	int ret = 0, i;
	int copied;
	struct drm_property_enum *prop_enum;
	struct drm_mode_property_enum __user *enum_ptr;
	struct drm_property_blob *prop_blob;
	uint32_t *blob_id_ptr;
	uint64_t __user *values_ptr;
	uint32_t __user *blob_length_ptr;

	mutex_lock(&dev->mode_config.mutex);
	obj = drm_mode_object_find(dev, out_resp->prop_id, DRM_MODE_OBJECT_PROPERTY);
	if (!obj) {
		ret = -EINVAL;
		goto done;
	}
	property = obj_to_property(obj);

	if (property->flags & DRM_MODE_PROP_ENUM) {
		list_for_each_entry(prop_enum, &property->enum_blob_list, head)
			enum_count++;
	} else if (property->flags & DRM_MODE_PROP_BLOB) {
		list_for_each_entry(prop_blob, &property->enum_blob_list, head)
			blob_count++;
	}

	value_count = property->num_values;

	strncpy(out_resp->name, property->name, DRM_PROP_NAME_LEN);
	out_resp->name[DRM_PROP_NAME_LEN-1] = 0;
	out_resp->flags = property->flags;

	if ((out_resp->count_values >= value_count) && value_count) {
		values_ptr = (uint64_t *)(unsigned long)out_resp->values_ptr;
		for (i = 0; i < value_count; i++) {
			if (copy_to_user(values_ptr + i, &property->values[i], sizeof(uint64_t))) {
				ret = -EFAULT;
				goto done;
			}
		}
	}
	out_resp->count_values = value_count;

	if (property->flags & DRM_MODE_PROP_ENUM) {
		if ((out_resp->count_enum_blobs >= enum_count) && enum_count) {
			copied = 0;
			enum_ptr = (struct drm_mode_property_enum *)(unsigned long)out_resp->enum_blob_ptr;
			list_for_each_entry(prop_enum, &property->enum_blob_list, head) {

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

	if (property->flags & DRM_MODE_PROP_BLOB) {
		if ((out_resp->count_enum_blobs >= blob_count) && blob_count) {
			copied = 0;
			blob_id_ptr = (uint32_t *)(unsigned long)out_resp->enum_blob_ptr;
			blob_length_ptr = (uint32_t *)(unsigned long)out_resp->values_ptr;

			list_for_each_entry(prop_blob, &property->enum_blob_list, head) {
				if (put_user(prop_blob->base.id, blob_id_ptr + copied)) {
					ret = -EFAULT;
					goto done;
				}

				if (put_user(prop_blob->length, blob_length_ptr + copied)) {
					ret = -EFAULT;
					goto done;
				}

				copied++;
			}
		}
		out_resp->count_enum_blobs = blob_count;
	}
done:
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}

static struct drm_property_blob *drm_property_create_blob(struct drm_device *dev, int length,
							  void *data)
{
	struct drm_property_blob *blob;

	if (!length || !data)
		return NULL;

	blob = kzalloc(sizeof(struct drm_property_blob)+length, GFP_KERNEL);
	if (!blob)
		return NULL;

	blob->data = (void *)((char *)blob + sizeof(struct drm_property_blob));
	blob->length = length;

	memcpy(blob->data, data, length);

	drm_mode_object_get(dev, &blob->base, DRM_MODE_OBJECT_BLOB);

	list_add_tail(&blob->head, &dev->mode_config.property_blob_list);
	return blob;
}

static void drm_property_destroy_blob(struct drm_device *dev,
			       struct drm_property_blob *blob)
{
	drm_mode_object_put(dev, &blob->base);
	list_del(&blob->head);
	kfree(blob);
}

int drm_mode_getblob_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv)
{
	struct drm_mode_object *obj;
	struct drm_mode_get_blob *out_resp = data;
	struct drm_property_blob *blob;
	int ret = 0;
	void *blob_ptr;

	mutex_lock(&dev->mode_config.mutex);
	obj = drm_mode_object_find(dev, out_resp->blob_id, DRM_MODE_OBJECT_BLOB);
	if (!obj) {
		ret = -EINVAL;
		goto done;
	}
	blob = obj_to_blob(obj);

	if (out_resp->length == blob->length) {
		blob_ptr = (void *)(unsigned long)out_resp->data;
		if (copy_to_user(blob_ptr, blob->data, blob->length)){
			ret = -EFAULT;
			goto done;
		}
	}
	out_resp->length = blob->length;

done:
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}

int drm_mode_connector_update_edid_property(struct drm_connector *connector,
					    struct edid *edid)
{
	struct drm_device *dev = connector->dev;
	int ret = 0;

	if (connector->edid_blob_ptr)
		drm_property_destroy_blob(dev, connector->edid_blob_ptr);

	/* Delete edid, when there is none. */
	if (!edid) {
		connector->edid_blob_ptr = NULL;
		ret = drm_connector_property_set_value(connector, dev->mode_config.edid_property, 0);
		return ret;
	}

	connector->edid_blob_ptr = drm_property_create_blob(connector->dev, 128, edid);

	ret = drm_connector_property_set_value(connector,
					       dev->mode_config.edid_property,
					       connector->edid_blob_ptr->base.id);

	return ret;
}
EXPORT_SYMBOL(drm_mode_connector_update_edid_property);

int drm_mode_connector_property_set_ioctl(struct drm_device *dev,
				       void *data, struct drm_file *file_priv)
{
	struct drm_mode_connector_set_property *out_resp = data;
	struct drm_mode_object *obj;
	struct drm_property *property;
	struct drm_connector *connector;
	int ret = -EINVAL;
	int i;

	mutex_lock(&dev->mode_config.mutex);

	obj = drm_mode_object_find(dev, out_resp->connector_id, DRM_MODE_OBJECT_CONNECTOR);
	if (!obj) {
		goto out;
	}
	connector = obj_to_connector(obj);

	for (i = 0; i < DRM_CONNECTOR_MAX_PROPERTY; i++) {
		if (connector->property_ids[i] == out_resp->prop_id)
			break;
	}

	if (i == DRM_CONNECTOR_MAX_PROPERTY) {
		goto out;
	}

	obj = drm_mode_object_find(dev, out_resp->prop_id, DRM_MODE_OBJECT_PROPERTY);
	if (!obj) {
		goto out;
	}
	property = obj_to_property(obj);

	if (property->flags & DRM_MODE_PROP_IMMUTABLE)
		goto out;

	if (property->flags & DRM_MODE_PROP_RANGE) {
		if (out_resp->value < property->values[0])
			goto out;

		if (out_resp->value > property->values[1])
			goto out;
	} else {
		int found = 0;
		for (i = 0; i < property->num_values; i++) {
			if (property->values[i] == out_resp->value) {
				found = 1;
				break;
			}
		}
		if (!found) {
			goto out;
		}
	}

	if (connector->funcs->set_property)
		ret = connector->funcs->set_property(connector, property, out_resp->value);

	/* store the property value if succesful */
	if (!ret)
		drm_connector_property_set_value(connector, property, out_resp->value);
out:
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}

int drm_mode_connector_attach_encoder(struct drm_connector *connector,
				      struct drm_encoder *encoder)
{
	int i;

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		if (connector->encoder_ids[i] == 0) {
			connector->encoder_ids[i] = encoder->base.id;
			return 0;
		}
	}
	return -ENOMEM;
}
EXPORT_SYMBOL(drm_mode_connector_attach_encoder);

void drm_mode_connector_detach_encoder(struct drm_connector *connector,
				    struct drm_encoder *encoder)
{
	int i;
	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		if (connector->encoder_ids[i] == encoder->base.id) {
			connector->encoder_ids[i] = 0;
			if (connector->encoder == encoder)
				connector->encoder = NULL;
			break;
		}
	}
}
EXPORT_SYMBOL(drm_mode_connector_detach_encoder);

bool drm_mode_crtc_set_gamma_size(struct drm_crtc *crtc,
				  int gamma_size)
{
	crtc->gamma_size = gamma_size;

	crtc->gamma_store = kzalloc(gamma_size * sizeof(uint16_t) * 3, GFP_KERNEL);
	if (!crtc->gamma_store) {
		crtc->gamma_size = 0;
		return false;
	}

	return true;
}
EXPORT_SYMBOL(drm_mode_crtc_set_gamma_size);

int drm_mode_gamma_set_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv)
{
	struct drm_mode_crtc_lut *crtc_lut = data;
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	void *r_base, *g_base, *b_base;
	int size;
	int ret = 0;

	mutex_lock(&dev->mode_config.mutex);
	obj = drm_mode_object_find(dev, crtc_lut->crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		ret = -EINVAL;
		goto out;
	}
	crtc = obj_to_crtc(obj);

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

	crtc->funcs->gamma_set(crtc, r_base, g_base, b_base, crtc->gamma_size);

out:
	mutex_unlock(&dev->mode_config.mutex);
	return ret;

}

int drm_mode_gamma_get_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv)
{
	struct drm_mode_crtc_lut *crtc_lut = data;
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	void *r_base, *g_base, *b_base;
	int size;
	int ret = 0;

	mutex_lock(&dev->mode_config.mutex);
	obj = drm_mode_object_find(dev, crtc_lut->crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		ret = -EINVAL;
		goto out;
	}
	crtc = obj_to_crtc(obj);

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
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}
