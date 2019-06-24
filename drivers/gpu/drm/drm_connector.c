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

#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_utils.h>
#include <drm/drm_print.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>

#include <linux/uaccess.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

/**
 * DOC: overview
 *
 * In DRM connectors are the general abstraction for display sinks, and include
 * als fixed panels or anything else that can display pixels in some form. As
 * opposed to all other KMS objects representing hardware (like CRTC, encoder or
 * plane abstractions) connectors can be hotplugged and unplugged at runtime.
 * Hence they are reference-counted using drm_connector_get() and
 * drm_connector_put().
 *
 * KMS driver must create, initialize, register and attach at a &struct
 * drm_connector for each such sink. The instance is created as other KMS
 * objects and initialized by setting the following fields. The connector is
 * initialized with a call to drm_connector_init() with a pointer to the
 * &struct drm_connector_funcs and a connector type, and then exposed to
 * userspace with a call to drm_connector_register().
 *
 * Connectors must be attached to an encoder to be used. For devices that map
 * connectors to encoders 1:1, the connector should be attached at
 * initialization time with a call to drm_connector_attach_encoder(). The
 * driver must also set the &drm_connector.encoder field to point to the
 * attached encoder.
 *
 * For connectors which are not fixed (like built-in panels) the driver needs to
 * support hotplug notifications. The simplest way to do that is by using the
 * probe helpers, see drm_kms_helper_poll_init() for connectors which don't have
 * hardware support for hotplug interrupts. Connectors with hardware hotplug
 * support can instead use e.g. drm_helper_hpd_irq_event().
 */

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
	{ DRM_MODE_CONNECTOR_WRITEBACK, "Writeback" },
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
 * drm_connector_get_cmdline_mode - reads the user's cmdline mode
 * @connector: connector to quwery
 *
 * The kernel supports per-connector configuration of its consoles through
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
		DRM_INFO("forcing %s connector %s\n", connector->name,
			 drm_get_connector_force_name(mode->force));
		connector->force = mode->force;
	}

	DRM_DEBUG_KMS("cmdline mode for connector %s %s %dx%d@%dHz%s%s%s\n",
		      connector->name, mode->name,
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

void drm_connector_free_work_fn(struct work_struct *work)
{
	struct drm_connector *connector, *n;
	struct drm_device *dev =
		container_of(work, struct drm_device, mode_config.connector_free_work);
	struct drm_mode_config *config = &dev->mode_config;
	unsigned long flags;
	struct llist_node *freed;

	spin_lock_irqsave(&config->connector_list_lock, flags);
	freed = llist_del_all(&config->connector_free_list);
	spin_unlock_irqrestore(&config->connector_list_lock, flags);

	llist_for_each_entry_safe(connector, n, freed, free_node) {
		drm_mode_object_unregister(dev, &connector->base);
		connector->funcs->destroy(connector);
	}
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

	WARN_ON(drm_drv_uses_atomic_modeset(dev) &&
		(!funcs->atomic_destroy_state ||
		 !funcs->atomic_duplicate_state));

	ret = __drm_mode_object_add(dev, &connector->base,
				    DRM_MODE_OBJECT_CONNECTOR,
				    false, drm_connector_free);
	if (ret)
		return ret;

	connector->base.properties = &connector->properties;
	connector->dev = dev;
	connector->funcs = funcs;

	/* connector index is used with 32bit bitmasks */
	ret = ida_simple_get(&config->connector_ida, 0, 32, GFP_KERNEL);
	if (ret < 0) {
		DRM_DEBUG_KMS("Failed to allocate %s connector index: %d\n",
			      drm_connector_enum_list[connector_type].name,
			      ret);
		goto out_put;
	}
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
	mutex_init(&connector->mutex);
	connector->edid_blob_ptr = NULL;
	connector->tile_blob_ptr = NULL;
	connector->status = connector_status_unknown;
	connector->display_info.panel_orientation =
		DRM_MODE_PANEL_ORIENTATION_UNKNOWN;

	drm_connector_get_cmdline_mode(connector);

	/* We should add connectors at the end to avoid upsetting the connector
	 * index too much. */
	spin_lock_irq(&config->connector_list_lock);
	list_add_tail(&connector->head, &config->connector_list);
	config->num_connector++;
	spin_unlock_irq(&config->connector_list_lock);

	if (connector_type != DRM_MODE_CONNECTOR_VIRTUAL &&
	    connector_type != DRM_MODE_CONNECTOR_WRITEBACK)
		drm_connector_attach_edid_property(connector);

	drm_object_attach_property(&connector->base,
				      config->dpms_property, 0);

	drm_object_attach_property(&connector->base,
				   config->link_status_property,
				   0);

	drm_object_attach_property(&connector->base,
				   config->non_desktop_property,
				   0);
	drm_object_attach_property(&connector->base,
				   config->tile_property,
				   0);

	if (drm_core_check_feature(dev, DRIVER_ATOMIC)) {
		drm_object_attach_property(&connector->base, config->prop_crtc_id, 0);
	}

	connector->debugfs_entry = NULL;
out_put_type_id:
	if (ret)
		ida_simple_remove(connector_ida, connector->connector_type_id);
out_put_id:
	if (ret)
		ida_simple_remove(&config->connector_ida, connector->index);
out_put:
	if (ret)
		drm_mode_object_unregister(dev, &connector->base);

	return ret;
}
EXPORT_SYMBOL(drm_connector_init);

/**
 * drm_connector_attach_edid_property - attach edid property.
 * @connector: the connector
 *
 * Some connector types like DRM_MODE_CONNECTOR_VIRTUAL do not get a
 * edid property attached by default.  This function can be used to
 * explicitly enable the edid property in these cases.
 */
void drm_connector_attach_edid_property(struct drm_connector *connector)
{
	struct drm_mode_config *config = &connector->dev->mode_config;

	drm_object_attach_property(&connector->base,
				   config->edid_property,
				   0);
}
EXPORT_SYMBOL(drm_connector_attach_edid_property);

/**
 * drm_connector_attach_encoder - attach a connector to an encoder
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
int drm_connector_attach_encoder(struct drm_connector *connector,
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

	for (i = 0; i < ARRAY_SIZE(connector->encoder_ids); i++) {
		if (connector->encoder_ids[i] == 0) {
			connector->encoder_ids[i] = encoder->base.id;
			return 0;
		}
	}
	return -ENOMEM;
}
EXPORT_SYMBOL(drm_connector_attach_encoder);

/**
 * drm_connector_has_possible_encoder - check if the connector and encoder are assosicated with each other
 * @connector: the connector
 * @encoder: the encoder
 *
 * Returns:
 * True if @encoder is one of the possible encoders for @connector.
 */
bool drm_connector_has_possible_encoder(struct drm_connector *connector,
					struct drm_encoder *encoder)
{
	struct drm_encoder *enc;
	int i;

	drm_connector_for_each_possible_encoder(connector, enc, i) {
		if (enc == encoder)
			return true;
	}

	return false;
}
EXPORT_SYMBOL(drm_connector_has_possible_encoder);

static void drm_mode_remove(struct drm_connector *connector,
			    struct drm_display_mode *mode)
{
	list_del(&mode->head);
	drm_mode_destroy(connector->dev, mode);
}

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
	if (WARN_ON(connector->registration_state ==
		    DRM_CONNECTOR_REGISTERED))
		drm_connector_unregister(connector);

	if (connector->tile_group) {
		drm_mode_put_tile_group(dev, connector->tile_group);
		connector->tile_group = NULL;
	}

	list_for_each_entry_safe(mode, t, &connector->probed_modes, head)
		drm_mode_remove(connector, mode);

	list_for_each_entry_safe(mode, t, &connector->modes, head)
		drm_mode_remove(connector, mode);

	ida_simple_remove(&drm_connector_enum_list[connector->connector_type].ida,
			  connector->connector_type_id);

	ida_simple_remove(&dev->mode_config.connector_ida,
			  connector->index);

	kfree(connector->display_info.bus_formats);
	drm_mode_object_unregister(dev, &connector->base);
	kfree(connector->name);
	connector->name = NULL;
	spin_lock_irq(&dev->mode_config.connector_list_lock);
	list_del(&connector->head);
	dev->mode_config.num_connector--;
	spin_unlock_irq(&dev->mode_config.connector_list_lock);

	WARN_ON(connector->state && !connector->funcs->atomic_destroy_state);
	if (connector->state && connector->funcs->atomic_destroy_state)
		connector->funcs->atomic_destroy_state(connector,
						       connector->state);

	mutex_destroy(&connector->mutex);

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
	int ret = 0;

	if (!connector->dev->registered)
		return 0;

	mutex_lock(&connector->mutex);
	if (connector->registration_state != DRM_CONNECTOR_INITIALIZING)
		goto unlock;

	ret = drm_sysfs_connector_add(connector);
	if (ret)
		goto unlock;

	drm_debugfs_connector_add(connector);

	if (connector->funcs->late_register) {
		ret = connector->funcs->late_register(connector);
		if (ret)
			goto err_debugfs;
	}

	drm_mode_object_register(connector->dev, &connector->base);

	connector->registration_state = DRM_CONNECTOR_REGISTERED;
	goto unlock;

err_debugfs:
	drm_debugfs_connector_remove(connector);
	drm_sysfs_connector_remove(connector);
unlock:
	mutex_unlock(&connector->mutex);
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
	mutex_lock(&connector->mutex);
	if (connector->registration_state != DRM_CONNECTOR_REGISTERED) {
		mutex_unlock(&connector->mutex);
		return;
	}

	if (connector->funcs->early_unregister)
		connector->funcs->early_unregister(connector);

	drm_sysfs_connector_remove(connector);
	drm_debugfs_connector_remove(connector);

	connector->registration_state = DRM_CONNECTOR_UNREGISTERED;
	mutex_unlock(&connector->mutex);
}
EXPORT_SYMBOL(drm_connector_unregister);

void drm_connector_unregister_all(struct drm_device *dev)
{
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter)
		drm_connector_unregister(connector);
	drm_connector_list_iter_end(&conn_iter);
}

int drm_connector_register_all(struct drm_device *dev)
{
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	int ret = 0;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		ret = drm_connector_register(connector);
		if (ret)
			break;
	}
	drm_connector_list_iter_end(&conn_iter);

	if (ret)
		drm_connector_unregister_all(dev);
	return ret;
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
 * drm_get_connector_force_name - return a string for connector force
 * @force: connector force to get name of
 *
 * Returns: const pointer to name.
 */
const char *drm_get_connector_force_name(enum drm_connector_force force)
{
	switch (force) {
	case DRM_FORCE_UNSPECIFIED:
		return "unspecified";
	case DRM_FORCE_OFF:
		return "off";
	case DRM_FORCE_ON:
		return "on";
	case DRM_FORCE_ON_DIGITAL:
		return "digital";
	default:
		return "unknown";
	}
}

#ifdef CONFIG_LOCKDEP
static struct lockdep_map connector_list_iter_dep_map = {
	.name = "drm_connector_list_iter"
};
#endif

/**
 * drm_connector_list_iter_begin - initialize a connector_list iterator
 * @dev: DRM device
 * @iter: connector_list iterator
 *
 * Sets @iter up to walk the &drm_mode_config.connector_list of @dev. @iter
 * must always be cleaned up again by calling drm_connector_list_iter_end().
 * Iteration itself happens using drm_connector_list_iter_next() or
 * drm_for_each_connector_iter().
 */
void drm_connector_list_iter_begin(struct drm_device *dev,
				   struct drm_connector_list_iter *iter)
{
	iter->dev = dev;
	iter->conn = NULL;
	lock_acquire_shared_recursive(&connector_list_iter_dep_map, 0, 1, NULL, _RET_IP_);
}
EXPORT_SYMBOL(drm_connector_list_iter_begin);

/*
 * Extra-safe connector put function that works in any context. Should only be
 * used from the connector_iter functions, where we never really expect to
 * actually release the connector when dropping our final reference.
 */
static void
__drm_connector_put_safe(struct drm_connector *conn)
{
	struct drm_mode_config *config = &conn->dev->mode_config;

	lockdep_assert_held(&config->connector_list_lock);

	if (!refcount_dec_and_test(&conn->base.refcount.refcount))
		return;

	llist_add(&conn->free_node, &config->connector_free_list);
	schedule_work(&config->connector_free_work);
}

/**
 * drm_connector_list_iter_next - return next connector
 * @iter: connector_list iterator
 *
 * Returns the next connector for @iter, or NULL when the list walk has
 * completed.
 */
struct drm_connector *
drm_connector_list_iter_next(struct drm_connector_list_iter *iter)
{
	struct drm_connector *old_conn = iter->conn;
	struct drm_mode_config *config = &iter->dev->mode_config;
	struct list_head *lhead;
	unsigned long flags;

	spin_lock_irqsave(&config->connector_list_lock, flags);
	lhead = old_conn ? &old_conn->head : &config->connector_list;

	do {
		if (lhead->next == &config->connector_list) {
			iter->conn = NULL;
			break;
		}

		lhead = lhead->next;
		iter->conn = list_entry(lhead, struct drm_connector, head);

		/* loop until it's not a zombie connector */
	} while (!kref_get_unless_zero(&iter->conn->base.refcount));

	if (old_conn)
		__drm_connector_put_safe(old_conn);
	spin_unlock_irqrestore(&config->connector_list_lock, flags);

	return iter->conn;
}
EXPORT_SYMBOL(drm_connector_list_iter_next);

/**
 * drm_connector_list_iter_end - tear down a connector_list iterator
 * @iter: connector_list iterator
 *
 * Tears down @iter and releases any resources (like &drm_connector references)
 * acquired while walking the list. This must always be called, both when the
 * iteration completes fully or when it was aborted without walking the entire
 * list.
 */
void drm_connector_list_iter_end(struct drm_connector_list_iter *iter)
{
	struct drm_mode_config *config = &iter->dev->mode_config;
	unsigned long flags;

	iter->dev = NULL;
	if (iter->conn) {
		spin_lock_irqsave(&config->connector_list_lock, flags);
		__drm_connector_put_safe(iter->conn);
		spin_unlock_irqrestore(&config->connector_list_lock, flags);
	}
	lock_release(&connector_list_iter_dep_map, 0, _RET_IP_);
}
EXPORT_SYMBOL(drm_connector_list_iter_end);

static const struct drm_prop_enum_list drm_subpixel_enum_list[] = {
	{ SubPixelUnknown, "Unknown" },
	{ SubPixelHorizontalRGB, "Horizontal RGB" },
	{ SubPixelHorizontalBGR, "Horizontal BGR" },
	{ SubPixelVerticalRGB, "Vertical RGB" },
	{ SubPixelVerticalBGR, "Vertical BGR" },
	{ SubPixelNone, "None" },
};

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

static const struct drm_prop_enum_list drm_dpms_enum_list[] = {
	{ DRM_MODE_DPMS_ON, "On" },
	{ DRM_MODE_DPMS_STANDBY, "Standby" },
	{ DRM_MODE_DPMS_SUSPEND, "Suspend" },
	{ DRM_MODE_DPMS_OFF, "Off" }
};
DRM_ENUM_NAME_FN(drm_get_dpms_name, drm_dpms_enum_list)

static const struct drm_prop_enum_list drm_link_status_enum_list[] = {
	{ DRM_MODE_LINK_STATUS_GOOD, "Good" },
	{ DRM_MODE_LINK_STATUS_BAD, "Bad" },
};

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

/* Optional connector properties. */
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

static const struct drm_prop_enum_list drm_content_type_enum_list[] = {
	{ DRM_MODE_CONTENT_TYPE_NO_DATA, "No Data" },
	{ DRM_MODE_CONTENT_TYPE_GRAPHICS, "Graphics" },
	{ DRM_MODE_CONTENT_TYPE_PHOTO, "Photo" },
	{ DRM_MODE_CONTENT_TYPE_CINEMA, "Cinema" },
	{ DRM_MODE_CONTENT_TYPE_GAME, "Game" },
};

static const struct drm_prop_enum_list drm_panel_orientation_enum_list[] = {
	{ DRM_MODE_PANEL_ORIENTATION_NORMAL,	"Normal"	},
	{ DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP,	"Upside Down"	},
	{ DRM_MODE_PANEL_ORIENTATION_LEFT_UP,	"Left Side Up"	},
	{ DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,	"Right Side Up"	},
};

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

static const struct drm_prop_enum_list hdmi_colorspaces[] = {
	/* For Default case, driver will set the colorspace */
	{ DRM_MODE_COLORIMETRY_DEFAULT, "Default" },
	/* Standard Definition Colorimetry based on CEA 861 */
	{ DRM_MODE_COLORIMETRY_SMPTE_170M_YCC, "SMPTE_170M_YCC" },
	{ DRM_MODE_COLORIMETRY_BT709_YCC, "BT709_YCC" },
	/* Standard Definition Colorimetry based on IEC 61966-2-4 */
	{ DRM_MODE_COLORIMETRY_XVYCC_601, "XVYCC_601" },
	/* High Definition Colorimetry based on IEC 61966-2-4 */
	{ DRM_MODE_COLORIMETRY_XVYCC_709, "XVYCC_709" },
	/* Colorimetry based on IEC 61966-2-1/Amendment 1 */
	{ DRM_MODE_COLORIMETRY_SYCC_601, "SYCC_601" },
	/* Colorimetry based on IEC 61966-2-5 [33] */
	{ DRM_MODE_COLORIMETRY_OPYCC_601, "opYCC_601" },
	/* Colorimetry based on IEC 61966-2-5 */
	{ DRM_MODE_COLORIMETRY_OPRGB, "opRGB" },
	/* Colorimetry based on ITU-R BT.2020 */
	{ DRM_MODE_COLORIMETRY_BT2020_CYCC, "BT2020_CYCC" },
	/* Colorimetry based on ITU-R BT.2020 */
	{ DRM_MODE_COLORIMETRY_BT2020_RGB, "BT2020_RGB" },
	/* Colorimetry based on ITU-R BT.2020 */
	{ DRM_MODE_COLORIMETRY_BT2020_YCC, "BT2020_YCC" },
	/* Added as part of Additional Colorimetry Extension in 861.G */
	{ DRM_MODE_COLORIMETRY_DCI_P3_RGB_D65, "DCI-P3_RGB_D65" },
	{ DRM_MODE_COLORIMETRY_DCI_P3_RGB_THEATER, "DCI-P3_RGB_Theater" },
};

/**
 * DOC: standard connector properties
 *
 * DRM connectors have a few standardized properties:
 *
 * EDID:
 * 	Blob property which contains the current EDID read from the sink. This
 * 	is useful to parse sink identification information like vendor, model
 * 	and serial. Drivers should update this property by calling
 * 	drm_connector_update_edid_property(), usually after having parsed
 * 	the EDID using drm_add_edid_modes(). Userspace cannot change this
 * 	property.
 * DPMS:
 * 	Legacy property for setting the power state of the connector. For atomic
 * 	drivers this is only provided for backwards compatibility with existing
 * 	drivers, it remaps to controlling the "ACTIVE" property on the CRTC the
 * 	connector is linked to. Drivers should never set this property directly,
 * 	it is handled by the DRM core by calling the &drm_connector_funcs.dpms
 * 	callback. For atomic drivers the remapping to the "ACTIVE" property is
 * 	implemented in the DRM core.  This is the only standard connector
 * 	property that userspace can change.
 *
 * 	Note that this property cannot be set through the MODE_ATOMIC ioctl,
 * 	userspace must use "ACTIVE" on the CRTC instead.
 *
 * 	WARNING:
 *
 * 	For userspace also running on legacy drivers the "DPMS" semantics are a
 * 	lot more complicated. First, userspace cannot rely on the "DPMS" value
 * 	returned by the GETCONNECTOR actually reflecting reality, because many
 * 	drivers fail to update it. For atomic drivers this is taken care of in
 * 	drm_atomic_helper_update_legacy_modeset_state().
 *
 * 	The second issue is that the DPMS state is only well-defined when the
 * 	connector is connected to a CRTC. In atomic the DRM core enforces that
 * 	"ACTIVE" is off in such a case, no such checks exists for "DPMS".
 *
 * 	Finally, when enabling an output using the legacy SETCONFIG ioctl then
 * 	"DPMS" is forced to ON. But see above, that might not be reflected in
 * 	the software value on legacy drivers.
 *
 * 	Summarizing: Only set "DPMS" when the connector is known to be enabled,
 * 	assume that a successful SETCONFIG call also sets "DPMS" to on, and
 * 	never read back the value of "DPMS" because it can be incorrect.
 * PATH:
 * 	Connector path property to identify how this sink is physically
 * 	connected. Used by DP MST. This should be set by calling
 * 	drm_connector_set_path_property(), in the case of DP MST with the
 * 	path property the MST manager created. Userspace cannot change this
 * 	property.
 * TILE:
 * 	Connector tile group property to indicate how a set of DRM connector
 * 	compose together into one logical screen. This is used by both high-res
 * 	external screens (often only using a single cable, but exposing multiple
 * 	DP MST sinks), or high-res integrated panels (like dual-link DSI) which
 * 	are not gen-locked. Note that for tiled panels which are genlocked, like
 * 	dual-link LVDS or dual-link DSI, the driver should try to not expose the
 * 	tiling and virtualize both &drm_crtc and &drm_plane if needed. Drivers
 * 	should update this value using drm_connector_set_tile_property().
 * 	Userspace cannot change this property.
 * link-status:
 *      Connector link-status property to indicate the status of link. The
 *      default value of link-status is "GOOD". If something fails during or
 *      after modeset, the kernel driver may set this to "BAD" and issue a
 *      hotplug uevent. Drivers should update this value using
 *      drm_connector_set_link_status_property().
 * non_desktop:
 * 	Indicates the output should be ignored for purposes of displaying a
 * 	standard desktop environment or console. This is most likely because
 * 	the output device is not rectilinear.
 * Content Protection:
 *	This property is used by userspace to request the kernel protect future
 *	content communicated over the link. When requested, kernel will apply
 *	the appropriate means of protection (most often HDCP), and use the
 *	property to tell userspace the protection is active.
 *
 *	Drivers can set this up by calling
 *	drm_connector_attach_content_protection_property() on initialization.
 *
 *	The value of this property can be one of the following:
 *
 *	DRM_MODE_CONTENT_PROTECTION_UNDESIRED = 0
 *		The link is not protected, content is transmitted in the clear.
 *	DRM_MODE_CONTENT_PROTECTION_DESIRED = 1
 *		Userspace has requested content protection, but the link is not
 *		currently protected. When in this state, kernel should enable
 *		Content Protection as soon as possible.
 *	DRM_MODE_CONTENT_PROTECTION_ENABLED = 2
 *		Userspace has requested content protection, and the link is
 *		protected. Only the driver can set the property to this value.
 *		If userspace attempts to set to ENABLED, kernel will return
 *		-EINVAL.
 *
 *	A few guidelines:
 *
 *	- DESIRED state should be preserved until userspace de-asserts it by
 *	  setting the property to UNDESIRED. This means ENABLED should only
 *	  transition to UNDESIRED when the user explicitly requests it.
 *	- If the state is DESIRED, kernel should attempt to re-authenticate the
 *	  link whenever possible. This includes across disable/enable, dpms,
 *	  hotplug, downstream device changes, link status failures, etc..
 *	- Userspace is responsible for polling the property to determine when
 *	  the value transitions from ENABLED to DESIRED. This signifies the link
 *	  is no longer protected and userspace should take appropriate action
 *	  (whatever that might be).
 *
 * HDR_OUTPUT_METADATA:
 *	Connector property to enable userspace to send HDR Metadata to
 *	driver. This metadata is based on the composition and blending
 *	policies decided by user, taking into account the hardware and
 *	sink capabilities. The driver gets this metadata and creates a
 *	Dynamic Range and Mastering Infoframe (DRM) in case of HDMI,
 *	SDP packet (Non-audio INFOFRAME SDP v1.3) for DP. This is then
 *	sent to sink. This notifies the sink of the upcoming frame's Color
 *	Encoding and Luminance parameters.
 *
 *	Userspace first need to detect the HDR capabilities of sink by
 *	reading and parsing the EDID. Details of HDR metadata for HDMI
 *	are added in CTA 861.G spec. For DP , its defined in VESA DP
 *	Standard v1.4. It needs to then get the metadata information
 *	of the video/game/app content which are encoded in HDR (basically
 *	using HDR transfer functions). With this information it needs to
 *	decide on a blending policy and compose the relevant
 *	layers/overlays into a common format. Once this blending is done,
 *	userspace will be aware of the metadata of the composed frame to
 *	be send to sink. It then uses this property to communicate this
 *	metadata to driver which then make a Infoframe packet and sends
 *	to sink based on the type of encoder connected.
 *
 *	Userspace will be responsible to do Tone mapping operation in case:
 *		- Some layers are HDR and others are SDR
 *		- HDR layers luminance is not same as sink
 *
 *	It will even need to do colorspace conversion and get all layers
 *	to one common colorspace for blending. It can use either GL, Media
 *	or display engine to get this done based on the capabilties of the
 *	associated hardware.
 *
 *	Driver expects metadata to be put in &struct hdr_output_metadata
 *	structure from userspace. This is received as blob and stored in
 *	&drm_connector_state.hdr_output_metadata. It parses EDID and saves the
 *	sink metadata in &struct hdr_sink_metadata, as
 *	&drm_connector.hdr_sink_metadata.  Driver uses
 *	drm_hdmi_infoframe_set_hdr_metadata() helper to set the HDR metadata,
 *	hdmi_drm_infoframe_pack() to pack the infoframe as per spec, in case of
 *	HDMI encoder.
 *
 * max bpc:
 *	This range property is used by userspace to limit the bit depth. When
 *	used the driver would limit the bpc in accordance with the valid range
 *	supported by the hardware and sink. Drivers to use the function
 *	drm_connector_attach_max_bpc_property() to create and attach the
 *	property to the connector during initialization.
 *
 * Connectors also have one standardized atomic property:
 *
 * CRTC_ID:
 * 	Mode object ID of the &drm_crtc this connector should be connected to.
 *
 * Connectors for LCD panels may also have one standardized property:
 *
 * panel orientation:
 *	On some devices the LCD panel is mounted in the casing in such a way
 *	that the up/top side of the panel does not match with the top side of
 *	the device. Userspace can use this property to check for this.
 *	Note that input coordinates from touchscreens (input devices with
 *	INPUT_PROP_DIRECT) will still map 1:1 to the actual LCD panel
 *	coordinates, so if userspace rotates the picture to adjust for
 *	the orientation it must also apply the same transformation to the
 *	touchscreen input coordinates. This property is initialized by calling
 *	drm_connector_init_panel_orientation_property().
 *
 * scaling mode:
 *	This property defines how a non-native mode is upscaled to the native
 *	mode of an LCD panel:
 *
 *	None:
 *		No upscaling happens, scaling is left to the panel. Not all
 *		drivers expose this mode.
 *	Full:
 *		The output is upscaled to the full resolution of the panel,
 *		ignoring the aspect ratio.
 *	Center:
 *		No upscaling happens, the output is centered within the native
 *		resolution the panel.
 *	Full aspect:
 *		The output is upscaled to maximize either the width or height
 *		while retaining the aspect ratio.
 *
 *	This property should be set up by calling
 *	drm_connector_attach_scaling_mode_property(). Note that drivers
 *	can also expose this property to external outputs, in which case they
 *	must support "None", which should be the default (since external screens
 *	have a built-in scaler).
 */

int drm_connector_create_standard_properties(struct drm_device *dev)
{
	struct drm_property *prop;

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

	prop = drm_property_create_enum(dev, 0, "link-status",
					drm_link_status_enum_list,
					ARRAY_SIZE(drm_link_status_enum_list));
	if (!prop)
		return -ENOMEM;
	dev->mode_config.link_status_property = prop;

	prop = drm_property_create_bool(dev, DRM_MODE_PROP_IMMUTABLE, "non-desktop");
	if (!prop)
		return -ENOMEM;
	dev->mode_config.non_desktop_property = prop;

	prop = drm_property_create(dev, DRM_MODE_PROP_BLOB,
				   "HDR_OUTPUT_METADATA", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.hdr_output_metadata_property = prop;

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
 * DOC: HDMI connector properties
 *
 * content type (HDMI specific):
 *	Indicates content type setting to be used in HDMI infoframes to indicate
 *	content type for the external device, so that it adjusts its display
 *	settings accordingly.
 *
 *	The value of this property can be one of the following:
 *
 *	No Data:
 *		Content type is unknown
 *	Graphics:
 *		Content type is graphics
 *	Photo:
 *		Content type is photo
 *	Cinema:
 *		Content type is cinema
 *	Game:
 *		Content type is game
 *
 *	Drivers can set up this property by calling
 *	drm_connector_attach_content_type_property(). Decoding to
 *	infoframe values is done through drm_hdmi_avi_infoframe_content_type().
 */

/**
 * drm_connector_attach_content_type_property - attach content-type property
 * @connector: connector to attach content type property on.
 *
 * Called by a driver the first time a HDMI connector is made.
 */
int drm_connector_attach_content_type_property(struct drm_connector *connector)
{
	if (!drm_mode_create_content_type_property(connector->dev))
		drm_object_attach_property(&connector->base,
					   connector->dev->mode_config.content_type_property,
					   DRM_MODE_CONTENT_TYPE_NO_DATA);
	return 0;
}
EXPORT_SYMBOL(drm_connector_attach_content_type_property);


/**
 * drm_hdmi_avi_infoframe_content_type() - fill the HDMI AVI infoframe
 *                                         content type information, based
 *                                         on correspondent DRM property.
 * @frame: HDMI AVI infoframe
 * @conn_state: DRM display connector state
 *
 */
void drm_hdmi_avi_infoframe_content_type(struct hdmi_avi_infoframe *frame,
					 const struct drm_connector_state *conn_state)
{
	switch (conn_state->content_type) {
	case DRM_MODE_CONTENT_TYPE_GRAPHICS:
		frame->content_type = HDMI_CONTENT_TYPE_GRAPHICS;
		break;
	case DRM_MODE_CONTENT_TYPE_CINEMA:
		frame->content_type = HDMI_CONTENT_TYPE_CINEMA;
		break;
	case DRM_MODE_CONTENT_TYPE_GAME:
		frame->content_type = HDMI_CONTENT_TYPE_GAME;
		break;
	case DRM_MODE_CONTENT_TYPE_PHOTO:
		frame->content_type = HDMI_CONTENT_TYPE_PHOTO;
		break;
	default:
		/* Graphics is the default(0) */
		frame->content_type = HDMI_CONTENT_TYPE_GRAPHICS;
	}

	frame->itc = conn_state->content_type != DRM_MODE_CONTENT_TYPE_NO_DATA;
}
EXPORT_SYMBOL(drm_hdmi_avi_infoframe_content_type);

/**
 * drm_mode_attach_tv_margin_properties - attach TV connector margin properties
 * @connector: DRM connector
 *
 * Called by a driver when it needs to attach TV margin props to a connector.
 * Typically used on SDTV and HDMI connectors.
 */
void drm_connector_attach_tv_margin_properties(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	drm_object_attach_property(&connector->base,
				   dev->mode_config.tv_left_margin_property,
				   0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.tv_right_margin_property,
				   0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.tv_top_margin_property,
				   0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.tv_bottom_margin_property,
				   0);
}
EXPORT_SYMBOL(drm_connector_attach_tv_margin_properties);

/**
 * drm_mode_create_tv_margin_properties - create TV connector margin properties
 * @dev: DRM device
 *
 * Called by a driver's HDMI connector initialization routine, this function
 * creates the TV margin properties for a given device. No need to call this
 * function for an SDTV connector, it's already called from
 * drm_mode_create_tv_properties().
 */
int drm_mode_create_tv_margin_properties(struct drm_device *dev)
{
	if (dev->mode_config.tv_left_margin_property)
		return 0;

	dev->mode_config.tv_left_margin_property =
		drm_property_create_range(dev, 0, "left margin", 0, 100);
	if (!dev->mode_config.tv_left_margin_property)
		return -ENOMEM;

	dev->mode_config.tv_right_margin_property =
		drm_property_create_range(dev, 0, "right margin", 0, 100);
	if (!dev->mode_config.tv_right_margin_property)
		return -ENOMEM;

	dev->mode_config.tv_top_margin_property =
		drm_property_create_range(dev, 0, "top margin", 0, 100);
	if (!dev->mode_config.tv_top_margin_property)
		return -ENOMEM;

	dev->mode_config.tv_bottom_margin_property =
		drm_property_create_range(dev, 0, "bottom margin", 0, 100);
	if (!dev->mode_config.tv_bottom_margin_property)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(drm_mode_create_tv_margin_properties);

/**
 * drm_mode_create_tv_properties - create TV specific connector properties
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
	if (drm_mode_create_tv_margin_properties(dev))
		goto nomem;

	dev->mode_config.tv_mode_property =
		drm_property_create(dev, DRM_MODE_PROP_ENUM,
				    "mode", num_modes);
	if (!dev->mode_config.tv_mode_property)
		goto nomem;

	for (i = 0; i < num_modes; i++)
		drm_property_add_enum(dev->mode_config.tv_mode_property,
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
 *
 * Atomic drivers should use drm_connector_attach_scaling_mode_property()
 * instead to correctly assign &drm_connector_state.picture_aspect_ratio
 * in the atomic state.
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
 * DOC: Variable refresh properties
 *
 * Variable refresh rate capable displays can dynamically adjust their
 * refresh rate by extending the duration of their vertical front porch
 * until page flip or timeout occurs. This can reduce or remove stuttering
 * and latency in scenarios where the page flip does not align with the
 * vblank interval.
 *
 * An example scenario would be an application flipping at a constant rate
 * of 48Hz on a 60Hz display. The page flip will frequently miss the vblank
 * interval and the same contents will be displayed twice. This can be
 * observed as stuttering for content with motion.
 *
 * If variable refresh rate was active on a display that supported a
 * variable refresh range from 35Hz to 60Hz no stuttering would be observable
 * for the example scenario. The minimum supported variable refresh rate of
 * 35Hz is below the page flip frequency and the vertical front porch can
 * be extended until the page flip occurs. The vblank interval will be
 * directly aligned to the page flip rate.
 *
 * Not all userspace content is suitable for use with variable refresh rate.
 * Large and frequent changes in vertical front porch duration may worsen
 * perceived stuttering for input sensitive applications.
 *
 * Panel brightness will also vary with vertical front porch duration. Some
 * panels may have noticeable differences in brightness between the minimum
 * vertical front porch duration and the maximum vertical front porch duration.
 * Large and frequent changes in vertical front porch duration may produce
 * observable flickering for such panels.
 *
 * Userspace control for variable refresh rate is supported via properties
 * on the &drm_connector and &drm_crtc objects.
 *
 * "vrr_capable":
 *	Optional &drm_connector boolean property that drivers should attach
 *	with drm_connector_attach_vrr_capable_property() on connectors that
 *	could support variable refresh rates. Drivers should update the
 *	property value by calling drm_connector_set_vrr_capable_property().
 *
 *	Absence of the property should indicate absence of support.
 *
 * "VRR_ENABLED":
 *	Default &drm_crtc boolean property that notifies the driver that the
 *	content on the CRTC is suitable for variable refresh rate presentation.
 *	The driver will take this property as a hint to enable variable
 *	refresh rate support if the receiver supports it, ie. if the
 *	"vrr_capable" property is true on the &drm_connector object. The
 *	vertical front porch duration will be extended until page-flip or
 *	timeout when enabled.
 *
 *	The minimum vertical front porch duration is defined as the vertical
 *	front porch duration for the current mode.
 *
 *	The maximum vertical front porch duration is greater than or equal to
 *	the minimum vertical front porch duration. The duration is derived
 *	from the minimum supported variable refresh rate for the connector.
 *
 *	The driver may place further restrictions within these minimum
 *	and maximum bounds.
 */

/**
 * drm_connector_attach_vrr_capable_property - creates the
 * vrr_capable property
 * @connector: connector to create the vrr_capable property on.
 *
 * This is used by atomic drivers to add support for querying
 * variable refresh rate capability for a connector.
 *
 * Returns:
 * Zero on success, negative errono on failure.
 */
int drm_connector_attach_vrr_capable_property(
	struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_property *prop;

	if (!connector->vrr_capable_property) {
		prop = drm_property_create_bool(dev, DRM_MODE_PROP_IMMUTABLE,
			"vrr_capable");
		if (!prop)
			return -ENOMEM;

		connector->vrr_capable_property = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	return 0;
}
EXPORT_SYMBOL(drm_connector_attach_vrr_capable_property);

/**
 * drm_connector_attach_scaling_mode_property - attach atomic scaling mode property
 * @connector: connector to attach scaling mode property on.
 * @scaling_mode_mask: or'ed mask of BIT(%DRM_MODE_SCALE_\*).
 *
 * This is used to add support for scaling mode to atomic drivers.
 * The scaling mode will be set to &drm_connector_state.picture_aspect_ratio
 * and can be used from &drm_connector_helper_funcs->atomic_check for validation.
 *
 * This is the atomic version of drm_mode_create_scaling_mode_property().
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_connector_attach_scaling_mode_property(struct drm_connector *connector,
					       u32 scaling_mode_mask)
{
	struct drm_device *dev = connector->dev;
	struct drm_property *scaling_mode_property;
	int i;
	const unsigned valid_scaling_mode_mask =
		(1U << ARRAY_SIZE(drm_scaling_mode_enum_list)) - 1;

	if (WARN_ON(hweight32(scaling_mode_mask) < 2 ||
		    scaling_mode_mask & ~valid_scaling_mode_mask))
		return -EINVAL;

	scaling_mode_property =
		drm_property_create(dev, DRM_MODE_PROP_ENUM, "scaling mode",
				    hweight32(scaling_mode_mask));

	if (!scaling_mode_property)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(drm_scaling_mode_enum_list); i++) {
		int ret;

		if (!(BIT(i) & scaling_mode_mask))
			continue;

		ret = drm_property_add_enum(scaling_mode_property,
					    drm_scaling_mode_enum_list[i].type,
					    drm_scaling_mode_enum_list[i].name);

		if (ret) {
			drm_property_destroy(dev, scaling_mode_property);

			return ret;
		}
	}

	drm_object_attach_property(&connector->base,
				   scaling_mode_property, 0);

	connector->scaling_mode_property = scaling_mode_property;

	return 0;
}
EXPORT_SYMBOL(drm_connector_attach_scaling_mode_property);

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
 * DOC: standard connector properties
 *
 * Colorspace:
 *     drm_mode_create_colorspace_property - create colorspace property
 *     This property helps select a suitable colorspace based on the sink
 *     capability. Modern sink devices support wider gamut like BT2020.
 *     This helps switch to BT2020 mode if the BT2020 encoded video stream
 *     is being played by the user, same for any other colorspace. Thereby
 *     giving a good visual experience to users.
 *
 *     The expectation from userspace is that it should parse the EDID
 *     and get supported colorspaces. Use this property and switch to the
 *     one supported. Sink supported colorspaces should be retrieved by
 *     userspace from EDID and driver will not explicitly expose them.
 *
 *     Basically the expectation from userspace is:
 *      - Set up CRTC DEGAMMA/CTM/GAMMA to convert to some sink
 *        colorspace
 *      - Set this new property to let the sink know what it
 *        converted the CRTC output to.
 *      - This property is just to inform sink what colorspace
 *        source is trying to drive.
 *
 * Called by a driver the first time it's needed, must be attached to desired
 * connectors.
 */
int drm_mode_create_colorspace_property(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_property *prop;

	if (connector->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector->connector_type == DRM_MODE_CONNECTOR_HDMIB) {
		prop = drm_property_create_enum(dev, DRM_MODE_PROP_ENUM,
						"Colorspace",
						hdmi_colorspaces,
						ARRAY_SIZE(hdmi_colorspaces));
		if (!prop)
			return -ENOMEM;
	} else {
		DRM_DEBUG_KMS("Colorspace property not supported\n");
		return 0;
	}

	connector->colorspace_property = prop;

	return 0;
}
EXPORT_SYMBOL(drm_mode_create_colorspace_property);

/**
 * drm_mode_create_content_type_property - create content type property
 * @dev: DRM device
 *
 * Called by a driver the first time it's needed, must be attached to desired
 * connectors.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_create_content_type_property(struct drm_device *dev)
{
	if (dev->mode_config.content_type_property)
		return 0;

	dev->mode_config.content_type_property =
		drm_property_create_enum(dev, 0, "content type",
					 drm_content_type_enum_list,
					 ARRAY_SIZE(drm_content_type_enum_list));

	if (dev->mode_config.content_type_property == NULL)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(drm_mode_create_content_type_property);

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
 * drm_connector_set_path_property - set tile property on connector
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
int drm_connector_set_path_property(struct drm_connector *connector,
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
EXPORT_SYMBOL(drm_connector_set_path_property);

/**
 * drm_connector_set_tile_property - set tile property on connector
 * @connector: connector to set property on.
 *
 * This looks up the tile information for a connector, and creates a
 * property for userspace to parse if it exists. The property is of
 * the form of 8 integers using ':' as a separator.
 * This is used for dual port tiled displays with DisplayPort SST
 * or DisplayPort MST connectors.
 *
 * Returns:
 * Zero on success, errno on failure.
 */
int drm_connector_set_tile_property(struct drm_connector *connector)
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
EXPORT_SYMBOL(drm_connector_set_tile_property);

/**
 * drm_connector_update_edid_property - update the edid property of a connector
 * @connector: drm connector
 * @edid: new value of the edid property
 *
 * This function creates a new blob modeset object and assigns its id to the
 * connector's edid property.
 * Since we also parse tile information from EDID's displayID block, we also
 * set the connector's tile property here. See drm_connector_set_tile_property()
 * for more details.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_connector_update_edid_property(struct drm_connector *connector,
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

	/* Set the display info, using edid if available, otherwise
	 * reseting the values to defaults. This duplicates the work
	 * done in drm_add_edid_modes, but that function is not
	 * consistently called before this one in all drivers and the
	 * computation is cheap enough that it seems better to
	 * duplicate it rather than attempt to ensure some arbitrary
	 * ordering of calls.
	 */
	if (edid)
		drm_add_display_info(connector, edid);
	else
		drm_reset_display_info(connector);

	drm_object_property_set_value(&connector->base,
				      dev->mode_config.non_desktop_property,
				      connector->display_info.non_desktop);

	ret = drm_property_replace_global_blob(dev,
					       &connector->edid_blob_ptr,
	                                       size,
	                                       edid,
	                                       &connector->base,
	                                       dev->mode_config.edid_property);
	if (ret)
		return ret;
	return drm_connector_set_tile_property(connector);
}
EXPORT_SYMBOL(drm_connector_update_edid_property);

/**
 * drm_connector_set_link_status_property - Set link status property of a connector
 * @connector: drm connector
 * @link_status: new value of link status property (0: Good, 1: Bad)
 *
 * In usual working scenario, this link status property will always be set to
 * "GOOD". If something fails during or after a mode set, the kernel driver
 * may set this link status property to "BAD". The caller then needs to send a
 * hotplug uevent for userspace to re-check the valid modes through
 * GET_CONNECTOR_IOCTL and retry modeset.
 *
 * Note: Drivers cannot rely on userspace to support this property and
 * issue a modeset. As such, they may choose to handle issues (like
 * re-training a link) without userspace's intervention.
 *
 * The reason for adding this property is to handle link training failures, but
 * it is not limited to DP or link training. For example, if we implement
 * asynchronous setcrtc, this property can be used to report any failures in that.
 */
void drm_connector_set_link_status_property(struct drm_connector *connector,
					    uint64_t link_status)
{
	struct drm_device *dev = connector->dev;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	connector->state->link_status = link_status;
	drm_modeset_unlock(&dev->mode_config.connection_mutex);
}
EXPORT_SYMBOL(drm_connector_set_link_status_property);

/**
 * drm_connector_attach_max_bpc_property - attach "max bpc" property
 * @connector: connector to attach max bpc property on.
 * @min: The minimum bit depth supported by the connector.
 * @max: The maximum bit depth supported by the connector.
 *
 * This is used to add support for limiting the bit depth on a connector.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_connector_attach_max_bpc_property(struct drm_connector *connector,
					  int min, int max)
{
	struct drm_device *dev = connector->dev;
	struct drm_property *prop;

	prop = connector->max_bpc_property;
	if (!prop) {
		prop = drm_property_create_range(dev, 0, "max bpc", min, max);
		if (!prop)
			return -ENOMEM;

		connector->max_bpc_property = prop;
	}

	drm_object_attach_property(&connector->base, prop, max);
	connector->state->max_requested_bpc = max;
	connector->state->max_bpc = max;

	return 0;
}
EXPORT_SYMBOL(drm_connector_attach_max_bpc_property);

/**
 * drm_connector_set_vrr_capable_property - sets the variable refresh rate
 * capable property for a connector
 * @connector: drm connector
 * @capable: True if the connector is variable refresh rate capable
 *
 * Should be used by atomic drivers to update the indicated support for
 * variable refresh rate over a connector.
 */
void drm_connector_set_vrr_capable_property(
		struct drm_connector *connector, bool capable)
{
	drm_object_property_set_value(&connector->base,
				      connector->vrr_capable_property,
				      capable);
}
EXPORT_SYMBOL(drm_connector_set_vrr_capable_property);

/**
 * drm_connector_init_panel_orientation_property -
 *	initialize the connecters panel_orientation property
 * @connector: connector for which to init the panel-orientation property.
 * @width: width in pixels of the panel, used for panel quirk detection
 * @height: height in pixels of the panel, used for panel quirk detection
 *
 * This function should only be called for built-in panels, after setting
 * connector->display_info.panel_orientation first (if known).
 *
 * This function will check for platform specific (e.g. DMI based) quirks
 * overriding display_info.panel_orientation first, then if panel_orientation
 * is not DRM_MODE_PANEL_ORIENTATION_UNKNOWN it will attach the
 * "panel orientation" property to the connector.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_connector_init_panel_orientation_property(
	struct drm_connector *connector, int width, int height)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_info *info = &connector->display_info;
	struct drm_property *prop;
	int orientation_quirk;

	orientation_quirk = drm_get_panel_orientation_quirk(width, height);
	if (orientation_quirk != DRM_MODE_PANEL_ORIENTATION_UNKNOWN)
		info->panel_orientation = orientation_quirk;

	if (info->panel_orientation == DRM_MODE_PANEL_ORIENTATION_UNKNOWN)
		return 0;

	prop = dev->mode_config.panel_orientation_property;
	if (!prop) {
		prop = drm_property_create_enum(dev, DRM_MODE_PROP_IMMUTABLE,
				"panel orientation",
				drm_panel_orientation_enum_list,
				ARRAY_SIZE(drm_panel_orientation_enum_list));
		if (!prop)
			return -ENOMEM;

		dev->mode_config.panel_orientation_property = prop;
	}

	drm_object_attach_property(&connector->base, prop,
				   info->panel_orientation);
	return 0;
}
EXPORT_SYMBOL(drm_connector_init_panel_orientation_property);

int drm_connector_set_obj_prop(struct drm_mode_object *obj,
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

	if (!ret)
		drm_object_property_set_value(&connector->base, property, value);
	return ret;
}

int drm_connector_property_set_ioctl(struct drm_device *dev,
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

static struct drm_encoder *drm_connector_get_encoder(struct drm_connector *connector)
{
	/* For atomic drivers only state objects are synchronously updated and
	 * protected by modeset locks, so check those first. */
	if (connector->state)
		return connector->state->best_encoder;
	return connector->encoder;
}

static bool
drm_mode_expose_to_userspace(const struct drm_display_mode *mode,
			     const struct list_head *export_list,
			     const struct drm_file *file_priv)
{
	/*
	 * If user-space hasn't configured the driver to expose the stereo 3D
	 * modes, don't expose them.
	 */
	if (!file_priv->stereo_allowed && drm_mode_is_stereo(mode))
		return false;
	/*
	 * If user-space hasn't configured the driver to expose the modes
	 * with aspect-ratio, don't expose them. However if such a mode
	 * is unique, let it be exposed, but reset the aspect-ratio flags
	 * while preparing the list of user-modes.
	 */
	if (!file_priv->aspect_ratio_allowed) {
		struct drm_display_mode *mode_itr;

		list_for_each_entry(mode_itr, export_list, export_head)
			if (drm_mode_match(mode_itr, mode,
					   DRM_MODE_MATCH_TIMINGS |
					   DRM_MODE_MATCH_CLOCK |
					   DRM_MODE_MATCH_FLAGS |
					   DRM_MODE_MATCH_3D_FLAGS))
				return false;
	}

	return true;
}

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
	LIST_HEAD(export_list);

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	memset(&u_mode, 0, sizeof(struct drm_mode_modeinfo));

	connector = drm_connector_lookup(dev, file_priv, out_resp->connector_id);
	if (!connector)
		return -ENOENT;

	drm_connector_for_each_possible_encoder(connector, encoder, i)
		encoders_count++;

	if ((out_resp->count_encoders >= encoders_count) && encoders_count) {
		copied = 0;
		encoder_ptr = (uint32_t __user *)(unsigned long)(out_resp->encoders_ptr);

		drm_connector_for_each_possible_encoder(connector, encoder, i) {
			if (put_user(encoder->base.id, encoder_ptr + copied)) {
				ret = -EFAULT;
				goto out;
			}
			copied++;
		}
	}
	out_resp->count_encoders = encoders_count;

	out_resp->connector_id = connector->base.id;
	out_resp->connector_type = connector->connector_type;
	out_resp->connector_type_id = connector->connector_type_id;

	mutex_lock(&dev->mode_config.mutex);
	if (out_resp->count_modes == 0) {
		connector->funcs->fill_modes(connector,
					     dev->mode_config.max_width,
					     dev->mode_config.max_height);
	}

	out_resp->mm_width = connector->display_info.width_mm;
	out_resp->mm_height = connector->display_info.height_mm;
	out_resp->subpixel = connector->display_info.subpixel_order;
	out_resp->connection = connector->status;

	/* delayed so we get modes regardless of pre-fill_modes state */
	list_for_each_entry(mode, &connector->modes, head)
		if (drm_mode_expose_to_userspace(mode, &export_list,
						 file_priv)) {
			list_add_tail(&mode->export_head, &export_list);
			mode_count++;
		}

	/*
	 * This ioctl is called twice, once to determine how much space is
	 * needed, and the 2nd time to fill it.
	 * The modes that need to be exposed to the user are maintained in the
	 * 'export_list'. When the ioctl is called first time to determine the,
	 * space, the export_list gets filled, to find the no.of modes. In the
	 * 2nd time, the user modes are filled, one by one from the export_list.
	 */
	if ((out_resp->count_modes >= mode_count) && mode_count) {
		copied = 0;
		mode_ptr = (struct drm_mode_modeinfo __user *)(unsigned long)out_resp->modes_ptr;
		list_for_each_entry(mode, &export_list, export_head) {
			drm_mode_convert_to_umode(&u_mode, mode);
			/*
			 * Reset aspect ratio flags of user-mode, if modes with
			 * aspect-ratio are not supported.
			 */
			if (!file_priv->aspect_ratio_allowed)
				u_mode.flags &= ~DRM_MODE_FLAG_PIC_AR_MASK;
			if (copy_to_user(mode_ptr + copied,
					 &u_mode, sizeof(u_mode))) {
				ret = -EFAULT;
				mutex_unlock(&dev->mode_config.mutex);

				goto out;
			}
			copied++;
		}
	}
	out_resp->count_modes = mode_count;
	mutex_unlock(&dev->mode_config.mutex);

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	encoder = drm_connector_get_encoder(connector);
	if (encoder)
		out_resp->encoder_id = encoder->base.id;
	else
		out_resp->encoder_id = 0;

	/* Only grab properties after probing, to make sure EDID and other
	 * properties reflect the latest status. */
	ret = drm_mode_object_get_properties(&connector->base, file_priv->atomic,
			(uint32_t __user *)(unsigned long)(out_resp->props_ptr),
			(uint64_t __user *)(unsigned long)(out_resp->prop_values_ptr),
			&out_resp->count_props);
	drm_modeset_unlock(&dev->mode_config.connection_mutex);

out:
	drm_connector_put(connector);

	return ret;
}


/**
 * DOC: Tile group
 *
 * Tile groups are used to represent tiled monitors with a unique integer
 * identifier. Tiled monitors using DisplayID v1.3 have a unique 8-byte handle,
 * we store this in a tile group, so we have a common identifier for all tiles
 * in a monitor group. The property is called "TILE". Drivers can manage tile
 * groups using drm_mode_create_tile_group(), drm_mode_put_tile_group() and
 * drm_mode_get_tile_group(). But this is only needed for internal panels where
 * the tile group information is exposed through a non-standard way.
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
EXPORT_SYMBOL(drm_mode_put_tile_group);

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
 * new tile group or NULL.
 */
struct drm_tile_group *drm_mode_create_tile_group(struct drm_device *dev,
						  char topology[8])
{
	struct drm_tile_group *tg;
	int ret;

	tg = kzalloc(sizeof(*tg), GFP_KERNEL);
	if (!tg)
		return NULL;

	kref_init(&tg->refcount);
	memcpy(tg->group_data, topology, 8);
	tg->dev = dev;

	mutex_lock(&dev->mode_config.idr_mutex);
	ret = idr_alloc(&dev->mode_config.tile_idr, tg, 1, 0, GFP_KERNEL);
	if (ret >= 0) {
		tg->id = ret;
	} else {
		kfree(tg);
		tg = NULL;
	}

	mutex_unlock(&dev->mode_config.idr_mutex);
	return tg;
}
EXPORT_SYMBOL(drm_mode_create_tile_group);
