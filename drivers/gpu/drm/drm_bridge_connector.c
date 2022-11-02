// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_edid.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>

/**
 * DOC: overview
 *
 * The DRM bridge connector helper object provides a DRM connector
 * implementation that wraps a chain of &struct drm_bridge. The connector
 * operations are fully implemented based on the operations of the bridges in
 * the chain, and don't require any intervention from the display controller
 * driver at runtime.
 *
 * To use the helper, display controller drivers create a bridge connector with
 * a call to drm_bridge_connector_init(). This associates the newly created
 * connector with the chain of bridges passed to the function and registers it
 * with the DRM device. At that point the connector becomes fully usable, no
 * further operation is needed.
 *
 * The DRM bridge connector operations are implemented based on the operations
 * provided by the bridges in the chain. Each connector operation is delegated
 * to the bridge closest to the connector (at the end of the chain) that
 * provides the relevant functionality.
 *
 * To make use of this helper, all bridges in the chain shall report bridge
 * operation flags (&drm_bridge->ops) and bridge output type
 * (&drm_bridge->type), as well as the DRM_BRIDGE_ATTACH_NO_CONNECTOR attach
 * flag (none of the bridges shall create a DRM connector directly).
 */

/**
 * struct drm_bridge_connector - A connector backed by a chain of bridges
 */
struct drm_bridge_connector {
	/**
	 * @base: The base DRM connector
	 */
	struct drm_connector base;
	/**
	 * @encoder:
	 *
	 * The encoder at the start of the bridges chain.
	 */
	struct drm_encoder *encoder;
	/**
	 * @bridge_edid:
	 *
	 * The last bridge in the chain (closest to the connector) that provides
	 * EDID read support, if any (see &DRM_BRIDGE_OP_EDID).
	 */
	struct drm_bridge *bridge_edid;
	/**
	 * @bridge_hpd:
	 *
	 * The last bridge in the chain (closest to the connector) that provides
	 * hot-plug detection notification, if any (see &DRM_BRIDGE_OP_HPD).
	 */
	struct drm_bridge *bridge_hpd;
	/**
	 * @bridge_detect:
	 *
	 * The last bridge in the chain (closest to the connector) that provides
	 * connector detection, if any (see &DRM_BRIDGE_OP_DETECT).
	 */
	struct drm_bridge *bridge_detect;
	/**
	 * @bridge_modes:
	 *
	 * The last bridge in the chain (closest to the connector) that provides
	 * connector modes detection, if any (see &DRM_BRIDGE_OP_MODES).
	 */
	struct drm_bridge *bridge_modes;
};

#define to_drm_bridge_connector(x) \
	container_of(x, struct drm_bridge_connector, base)

/* -----------------------------------------------------------------------------
 * Bridge Connector Hot-Plug Handling
 */

static void drm_bridge_connector_hpd_notify(struct drm_connector *connector,
					    enum drm_connector_status status)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *bridge;

	/* Notify all bridges in the pipeline of hotplug events. */
	drm_for_each_bridge_in_chain(bridge_connector->encoder, bridge) {
		if (bridge->funcs->hpd_notify)
			bridge->funcs->hpd_notify(bridge, status);
	}
}

static void drm_bridge_connector_hpd_cb(void *cb_data,
					enum drm_connector_status status)
{
	struct drm_bridge_connector *drm_bridge_connector = cb_data;
	struct drm_connector *connector = &drm_bridge_connector->base;
	struct drm_device *dev = connector->dev;
	enum drm_connector_status old_status;

	mutex_lock(&dev->mode_config.mutex);
	old_status = connector->status;
	connector->status = status;
	mutex_unlock(&dev->mode_config.mutex);

	if (old_status == status)
		return;

	drm_bridge_connector_hpd_notify(connector, status);

	drm_kms_helper_hotplug_event(dev);
}

static void drm_bridge_connector_enable_hpd(struct drm_connector *connector)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *hpd = bridge_connector->bridge_hpd;

	if (hpd)
		drm_bridge_hpd_enable(hpd, drm_bridge_connector_hpd_cb,
				      bridge_connector);
}

static void drm_bridge_connector_disable_hpd(struct drm_connector *connector)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *hpd = bridge_connector->bridge_hpd;

	if (hpd)
		drm_bridge_hpd_disable(hpd);
}

/* -----------------------------------------------------------------------------
 * Bridge Connector Functions
 */

static enum drm_connector_status
drm_bridge_connector_detect(struct drm_connector *connector, bool force)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *detect = bridge_connector->bridge_detect;
	enum drm_connector_status status;

	if (detect) {
		status = detect->funcs->detect(detect);

		drm_bridge_connector_hpd_notify(connector, status);
	} else {
		switch (connector->connector_type) {
		case DRM_MODE_CONNECTOR_DPI:
		case DRM_MODE_CONNECTOR_LVDS:
		case DRM_MODE_CONNECTOR_DSI:
		case DRM_MODE_CONNECTOR_eDP:
			status = connector_status_connected;
			break;
		default:
			status = connector_status_unknown;
			break;
		}
	}

	return status;
}

static void drm_bridge_connector_destroy(struct drm_connector *connector)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);

	if (bridge_connector->bridge_hpd) {
		struct drm_bridge *hpd = bridge_connector->bridge_hpd;

		drm_bridge_hpd_disable(hpd);
	}

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);

	kfree(bridge_connector);
}

static void drm_bridge_connector_debugfs_init(struct drm_connector *connector,
					      struct dentry *root)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_encoder *encoder = bridge_connector->encoder;
	struct drm_bridge *bridge;

	list_for_each_entry(bridge, &encoder->bridge_chain, chain_node) {
		if (bridge->funcs->debugfs_init)
			bridge->funcs->debugfs_init(bridge, root);
	}
}

static const struct drm_connector_funcs drm_bridge_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.detect = drm_bridge_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_bridge_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.debugfs_init = drm_bridge_connector_debugfs_init,
};

/* -----------------------------------------------------------------------------
 * Bridge Connector Helper Functions
 */

static int drm_bridge_connector_get_modes_edid(struct drm_connector *connector,
					       struct drm_bridge *bridge)
{
	enum drm_connector_status status;
	struct edid *edid;
	int n;

	status = drm_bridge_connector_detect(connector, false);
	if (status != connector_status_connected)
		goto no_edid;

	edid = bridge->funcs->get_edid(bridge, connector);
	if (!drm_edid_is_valid(edid)) {
		kfree(edid);
		goto no_edid;
	}

	drm_connector_update_edid_property(connector, edid);
	n = drm_add_edid_modes(connector, edid);

	kfree(edid);
	return n;

no_edid:
	drm_connector_update_edid_property(connector, NULL);
	return 0;
}

static int drm_bridge_connector_get_modes(struct drm_connector *connector)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *bridge;

	/*
	 * If display exposes EDID, then we parse that in the normal way to
	 * build table of supported modes.
	 */
	bridge = bridge_connector->bridge_edid;
	if (bridge)
		return drm_bridge_connector_get_modes_edid(connector, bridge);

	/*
	 * Otherwise if the display pipeline reports modes (e.g. with a fixed
	 * resolution panel or an analog TV output), query it.
	 */
	bridge = bridge_connector->bridge_modes;
	if (bridge)
		return bridge->funcs->get_modes(bridge, connector);

	/*
	 * We can't retrieve modes, which can happen for instance for a DVI or
	 * VGA output with the DDC bus unconnected. The KMS core will add the
	 * default modes.
	 */
	return 0;
}

static const struct drm_connector_helper_funcs drm_bridge_connector_helper_funcs = {
	.get_modes = drm_bridge_connector_get_modes,
	/* No need for .mode_valid(), the bridges are checked by the core. */
	.enable_hpd = drm_bridge_connector_enable_hpd,
	.disable_hpd = drm_bridge_connector_disable_hpd,
};

/* -----------------------------------------------------------------------------
 * Bridge Connector Initialisation
 */

/**
 * drm_bridge_connector_init - Initialise a connector for a chain of bridges
 * @drm: the DRM device
 * @encoder: the encoder where the bridge chain starts
 *
 * Allocate, initialise and register a &drm_bridge_connector with the @drm
 * device. The connector is associated with a chain of bridges that starts at
 * the @encoder. All bridges in the chain shall report bridge operation flags
 * (&drm_bridge->ops) and bridge output type (&drm_bridge->type), and none of
 * them may create a DRM connector directly.
 *
 * Returns a pointer to the new connector on success, or a negative error
 * pointer otherwise.
 */
struct drm_connector *drm_bridge_connector_init(struct drm_device *drm,
						struct drm_encoder *encoder)
{
	struct drm_bridge_connector *bridge_connector;
	struct drm_connector *connector;
	struct i2c_adapter *ddc = NULL;
	struct drm_bridge *bridge, *panel_bridge = NULL;
	int connector_type;

	bridge_connector = kzalloc(sizeof(*bridge_connector), GFP_KERNEL);
	if (!bridge_connector)
		return ERR_PTR(-ENOMEM);

	bridge_connector->encoder = encoder;

	/*
	 * TODO: Handle doublescan_allowed, stereo_allowed and
	 * ycbcr_420_allowed.
	 */
	connector = &bridge_connector->base;
	connector->interlace_allowed = true;

	/*
	 * Initialise connector status handling. First locate the furthest
	 * bridges in the pipeline that support HPD and output detection. Then
	 * initialise the connector polling mode, using HPD if available and
	 * falling back to polling if supported. If neither HPD nor output
	 * detection are available, we don't support hotplug detection at all.
	 */
	connector_type = DRM_MODE_CONNECTOR_Unknown;
	drm_for_each_bridge_in_chain(encoder, bridge) {
		if (!bridge->interlace_allowed)
			connector->interlace_allowed = false;

		if (bridge->ops & DRM_BRIDGE_OP_EDID)
			bridge_connector->bridge_edid = bridge;
		if (bridge->ops & DRM_BRIDGE_OP_HPD)
			bridge_connector->bridge_hpd = bridge;
		if (bridge->ops & DRM_BRIDGE_OP_DETECT)
			bridge_connector->bridge_detect = bridge;
		if (bridge->ops & DRM_BRIDGE_OP_MODES)
			bridge_connector->bridge_modes = bridge;

		if (!drm_bridge_get_next_bridge(bridge))
			connector_type = bridge->type;

		if (bridge->ddc)
			ddc = bridge->ddc;

		if (drm_bridge_is_panel(bridge))
			panel_bridge = bridge;
	}

	if (connector_type == DRM_MODE_CONNECTOR_Unknown) {
		kfree(bridge_connector);
		return ERR_PTR(-EINVAL);
	}

	drm_connector_init_with_ddc(drm, connector, &drm_bridge_connector_funcs,
				    connector_type, ddc);
	drm_connector_helper_add(connector, &drm_bridge_connector_helper_funcs);

	if (bridge_connector->bridge_hpd)
		connector->polled = DRM_CONNECTOR_POLL_HPD;
	else if (bridge_connector->bridge_detect)
		connector->polled = DRM_CONNECTOR_POLL_CONNECT
				  | DRM_CONNECTOR_POLL_DISCONNECT;

	if (panel_bridge)
		drm_panel_bridge_set_orientation(connector, panel_bridge);

	return connector;
}
EXPORT_SYMBOL_GPL(drm_bridge_connector_init);
