// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/slab.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_edid.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/display/drm_hdmi_audio_helper.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_hdmi_state_helper.h>

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
	/**
	 * @bridge_hdmi:
	 *
	 * The bridge in the chain that implements necessary support for the
	 * HDMI connector infrastructure, if any (see &DRM_BRIDGE_OP_HDMI).
	 */
	struct drm_bridge *bridge_hdmi;
	/**
	 * @bridge_hdmi_audio:
	 *
	 * The bridge in the chain that implements necessary support for the
	 * HDMI Audio infrastructure, if any (see &DRM_BRIDGE_OP_HDMI_AUDIO).
	 */
	struct drm_bridge *bridge_hdmi_audio;
	/**
	 * @bridge_dp_audio:
	 *
	 * The bridge in the chain that implements necessary support for the
	 * DisplayPort Audio infrastructure, if any (see
	 * &DRM_BRIDGE_OP_DP_AUDIO).
	 */
	struct drm_bridge *bridge_dp_audio;
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

static void drm_bridge_connector_handle_hpd(struct drm_bridge_connector *drm_bridge_connector,
					    enum drm_connector_status status)
{
	struct drm_connector *connector = &drm_bridge_connector->base;
	struct drm_device *dev = connector->dev;

	mutex_lock(&dev->mode_config.mutex);
	connector->status = status;
	mutex_unlock(&dev->mode_config.mutex);

	drm_bridge_connector_hpd_notify(connector, status);

	drm_kms_helper_connector_hotplug_event(connector);
}

static void drm_bridge_connector_hpd_cb(void *cb_data,
					enum drm_connector_status status)
{
	drm_bridge_connector_handle_hpd(cb_data, status);
}

static void drm_bridge_connector_oob_hotplug_event(struct drm_connector *connector,
						   enum drm_connector_status status)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);

	drm_bridge_connector_handle_hpd(bridge_connector, status);
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
	struct drm_bridge *hdmi = bridge_connector->bridge_hdmi;
	enum drm_connector_status status;

	if (detect) {
		status = detect->funcs->detect(detect);

		if (hdmi)
			drm_atomic_helper_connector_hdmi_hotplug(connector, status);

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

static void drm_bridge_connector_force(struct drm_connector *connector)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *hdmi = bridge_connector->bridge_hdmi;

	if (hdmi)
		drm_atomic_helper_connector_hdmi_force(connector);
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

static void drm_bridge_connector_reset(struct drm_connector *connector)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);

	drm_atomic_helper_connector_reset(connector);
	if (bridge_connector->bridge_hdmi)
		__drm_atomic_helper_connector_hdmi_reset(connector,
							 connector->state);
}

static const struct drm_connector_funcs drm_bridge_connector_funcs = {
	.reset = drm_bridge_connector_reset,
	.detect = drm_bridge_connector_detect,
	.force = drm_bridge_connector_force,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.debugfs_init = drm_bridge_connector_debugfs_init,
	.oob_hotplug_event = drm_bridge_connector_oob_hotplug_event,
};

/* -----------------------------------------------------------------------------
 * Bridge Connector Helper Functions
 */

static int drm_bridge_connector_get_modes_edid(struct drm_connector *connector,
					       struct drm_bridge *bridge)
{
	enum drm_connector_status status;
	const struct drm_edid *drm_edid;
	int n;

	status = drm_bridge_connector_detect(connector, false);
	if (status != connector_status_connected)
		goto no_edid;

	drm_edid = drm_bridge_edid_read(bridge, connector);
	if (!drm_edid_valid(drm_edid)) {
		drm_edid_free(drm_edid);
		goto no_edid;
	}

	drm_edid_connector_update(connector, drm_edid);
	n = drm_edid_connector_add_modes(connector);

	drm_edid_free(drm_edid);
	return n;

no_edid:
	drm_edid_connector_update(connector, NULL);
	return 0;
}

static int drm_bridge_connector_get_modes(struct drm_connector *connector)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *bridge;

	/*
	 * If there is a HDMI bridge, EDID has been updated as a part of
	 * the .detect(). Just update the modes here.
	 */
	bridge = bridge_connector->bridge_hdmi;
	if (bridge)
		return drm_edid_connector_add_modes(connector);

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

static enum drm_mode_status
drm_bridge_connector_mode_valid(struct drm_connector *connector,
				const struct drm_display_mode *mode)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);

	if (bridge_connector->bridge_hdmi)
		return drm_hdmi_connector_mode_valid(connector, mode);

	return MODE_OK;
}

static int drm_bridge_connector_atomic_check(struct drm_connector *connector,
					     struct drm_atomic_state *state)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);

	if (bridge_connector->bridge_hdmi)
		return drm_atomic_helper_connector_hdmi_check(connector, state);

	return 0;
}

static const struct drm_connector_helper_funcs drm_bridge_connector_helper_funcs = {
	.get_modes = drm_bridge_connector_get_modes,
	.mode_valid = drm_bridge_connector_mode_valid,
	.enable_hpd = drm_bridge_connector_enable_hpd,
	.disable_hpd = drm_bridge_connector_disable_hpd,
	.atomic_check = drm_bridge_connector_atomic_check,
};

static enum drm_mode_status
drm_bridge_connector_tmds_char_rate_valid(const struct drm_connector *connector,
					  const struct drm_display_mode *mode,
					  unsigned long long tmds_rate)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *bridge;

	bridge = bridge_connector->bridge_hdmi;
	if (!bridge)
		return MODE_ERROR;

	if (bridge->funcs->hdmi_tmds_char_rate_valid)
		return bridge->funcs->hdmi_tmds_char_rate_valid(bridge, mode, tmds_rate);
	else
		return MODE_OK;
}

static int drm_bridge_connector_clear_infoframe(struct drm_connector *connector,
						enum hdmi_infoframe_type type)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *bridge;

	bridge = bridge_connector->bridge_hdmi;
	if (!bridge)
		return -EINVAL;

	return bridge->funcs->hdmi_clear_infoframe(bridge, type);
}

static int drm_bridge_connector_write_infoframe(struct drm_connector *connector,
						enum hdmi_infoframe_type type,
						const u8 *buffer, size_t len)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *bridge;

	bridge = bridge_connector->bridge_hdmi;
	if (!bridge)
		return -EINVAL;

	return bridge->funcs->hdmi_write_infoframe(bridge, type, buffer, len);
}

static const struct drm_edid *
drm_bridge_connector_read_edid(struct drm_connector *connector)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *bridge;

	bridge = bridge_connector->bridge_edid;
	if (!bridge)
		return NULL;

	return drm_bridge_edid_read(bridge, connector);
}

static const struct drm_connector_hdmi_funcs drm_bridge_connector_hdmi_funcs = {
	.tmds_char_rate_valid = drm_bridge_connector_tmds_char_rate_valid,
	.clear_infoframe = drm_bridge_connector_clear_infoframe,
	.write_infoframe = drm_bridge_connector_write_infoframe,
	.read_edid = drm_bridge_connector_read_edid,
};

static int drm_bridge_connector_audio_startup(struct drm_connector *connector)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *bridge;

	if (bridge_connector->bridge_hdmi_audio) {
		bridge = bridge_connector->bridge_hdmi_audio;

		if (!bridge->funcs->hdmi_audio_startup)
			return 0;

		return bridge->funcs->hdmi_audio_startup(connector, bridge);
	}

	if (bridge_connector->bridge_dp_audio) {
		bridge = bridge_connector->bridge_dp_audio;

		if (!bridge->funcs->dp_audio_startup)
			return 0;

		return bridge->funcs->dp_audio_startup(connector, bridge);
	}

	return -EINVAL;
}

static int drm_bridge_connector_audio_prepare(struct drm_connector *connector,
					      struct hdmi_codec_daifmt *fmt,
					      struct hdmi_codec_params *hparms)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *bridge;

	if (bridge_connector->bridge_hdmi_audio) {
		bridge = bridge_connector->bridge_hdmi_audio;

		return bridge->funcs->hdmi_audio_prepare(connector, bridge, fmt, hparms);
	}

	if (bridge_connector->bridge_dp_audio) {
		bridge = bridge_connector->bridge_dp_audio;

		return bridge->funcs->dp_audio_prepare(connector, bridge, fmt, hparms);
	}

	return -EINVAL;
}

static void drm_bridge_connector_audio_shutdown(struct drm_connector *connector)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *bridge;

	if (bridge_connector->bridge_hdmi_audio) {
		bridge = bridge_connector->bridge_hdmi_audio;
		bridge->funcs->hdmi_audio_shutdown(connector, bridge);
	}

	if (bridge_connector->bridge_dp_audio) {
		bridge = bridge_connector->bridge_dp_audio;
		bridge->funcs->dp_audio_shutdown(connector, bridge);
	}
}

static int drm_bridge_connector_audio_mute_stream(struct drm_connector *connector,
						  bool enable, int direction)
{
	struct drm_bridge_connector *bridge_connector =
		to_drm_bridge_connector(connector);
	struct drm_bridge *bridge;

	if (bridge_connector->bridge_hdmi_audio) {
		bridge = bridge_connector->bridge_hdmi_audio;

		if (!bridge->funcs->hdmi_audio_mute_stream)
			return -ENOTSUPP;

		return bridge->funcs->hdmi_audio_mute_stream(connector, bridge,
							     enable, direction);
	}

	if (bridge_connector->bridge_dp_audio) {
		bridge = bridge_connector->bridge_dp_audio;

		if (!bridge->funcs->dp_audio_mute_stream)
			return -ENOTSUPP;

		return bridge->funcs->dp_audio_mute_stream(connector, bridge,
							   enable, direction);
	}

	return -EINVAL;
}

static const struct drm_connector_hdmi_audio_funcs drm_bridge_connector_hdmi_audio_funcs = {
	.startup = drm_bridge_connector_audio_startup,
	.prepare = drm_bridge_connector_audio_prepare,
	.shutdown = drm_bridge_connector_audio_shutdown,
	.mute_stream = drm_bridge_connector_audio_mute_stream,
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
	unsigned int supported_formats = BIT(HDMI_COLORSPACE_RGB);
	unsigned int max_bpc = 8;
	int connector_type;
	int ret;

	bridge_connector = drmm_kzalloc(drm, sizeof(*bridge_connector), GFP_KERNEL);
	if (!bridge_connector)
		return ERR_PTR(-ENOMEM);

	bridge_connector->encoder = encoder;

	/*
	 * TODO: Handle doublescan_allowed and stereo_allowed.
	 */
	connector = &bridge_connector->base;
	connector->interlace_allowed = true;
	connector->ycbcr_420_allowed = true;

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
		if (!bridge->ycbcr_420_allowed)
			connector->ycbcr_420_allowed = false;

		if (bridge->ops & DRM_BRIDGE_OP_EDID)
			bridge_connector->bridge_edid = bridge;
		if (bridge->ops & DRM_BRIDGE_OP_HPD)
			bridge_connector->bridge_hpd = bridge;
		if (bridge->ops & DRM_BRIDGE_OP_DETECT)
			bridge_connector->bridge_detect = bridge;
		if (bridge->ops & DRM_BRIDGE_OP_MODES)
			bridge_connector->bridge_modes = bridge;
		if (bridge->ops & DRM_BRIDGE_OP_HDMI) {
			if (bridge_connector->bridge_hdmi)
				return ERR_PTR(-EBUSY);
			if (!bridge->funcs->hdmi_write_infoframe ||
			    !bridge->funcs->hdmi_clear_infoframe)
				return ERR_PTR(-EINVAL);

			bridge_connector->bridge_hdmi = bridge;

			if (bridge->supported_formats)
				supported_formats = bridge->supported_formats;
			if (bridge->max_bpc)
				max_bpc = bridge->max_bpc;
		}

		if (bridge->ops & DRM_BRIDGE_OP_HDMI_AUDIO) {
			if (bridge_connector->bridge_hdmi_audio)
				return ERR_PTR(-EBUSY);

			if (bridge_connector->bridge_dp_audio)
				return ERR_PTR(-EBUSY);

			if (!bridge->hdmi_audio_max_i2s_playback_channels &&
			    !bridge->hdmi_audio_spdif_playback)
				return ERR_PTR(-EINVAL);

			if (!bridge->funcs->hdmi_audio_prepare ||
			    !bridge->funcs->hdmi_audio_shutdown)
				return ERR_PTR(-EINVAL);

			bridge_connector->bridge_hdmi_audio = bridge;
		}

		if (bridge->ops & DRM_BRIDGE_OP_DP_AUDIO) {
			if (bridge_connector->bridge_dp_audio)
				return ERR_PTR(-EBUSY);

			if (bridge_connector->bridge_hdmi_audio)
				return ERR_PTR(-EBUSY);

			if (!bridge->hdmi_audio_max_i2s_playback_channels &&
			    !bridge->hdmi_audio_spdif_playback)
				return ERR_PTR(-EINVAL);

			if (!bridge->funcs->dp_audio_prepare ||
			    !bridge->funcs->dp_audio_shutdown)
				return ERR_PTR(-EINVAL);

			bridge_connector->bridge_dp_audio = bridge;
		}

		if (!drm_bridge_get_next_bridge(bridge))
			connector_type = bridge->type;

#ifdef CONFIG_OF
		if (!drm_bridge_get_next_bridge(bridge) &&
		    bridge->of_node)
			connector->fwnode = fwnode_handle_get(of_fwnode_handle(bridge->of_node));
#endif

		if (bridge->ddc)
			ddc = bridge->ddc;

		if (drm_bridge_is_panel(bridge))
			panel_bridge = bridge;
	}

	if (connector_type == DRM_MODE_CONNECTOR_Unknown)
		return ERR_PTR(-EINVAL);

	if (bridge_connector->bridge_hdmi) {
		if (!connector->ycbcr_420_allowed)
			supported_formats &= ~BIT(HDMI_COLORSPACE_YUV420);

		bridge = bridge_connector->bridge_hdmi;

		ret = drmm_connector_hdmi_init(drm, connector,
					       bridge_connector->bridge_hdmi->vendor,
					       bridge_connector->bridge_hdmi->product,
					       &drm_bridge_connector_funcs,
					       &drm_bridge_connector_hdmi_funcs,
					       connector_type, ddc,
					       supported_formats,
					       max_bpc);
		if (ret)
			return ERR_PTR(ret);
	} else {
		ret = drmm_connector_init(drm, connector,
					  &drm_bridge_connector_funcs,
					  connector_type, ddc);
		if (ret)
			return ERR_PTR(ret);
	}

	if (bridge_connector->bridge_hdmi_audio ||
	    bridge_connector->bridge_dp_audio) {
		struct device *dev;
		struct drm_bridge *bridge;

		if (bridge_connector->bridge_hdmi_audio)
			bridge = bridge_connector->bridge_hdmi_audio;
		else
			bridge = bridge_connector->bridge_dp_audio;

		dev = bridge->hdmi_audio_dev;

		ret = drm_connector_hdmi_audio_init(connector, dev,
						    &drm_bridge_connector_hdmi_audio_funcs,
						    bridge->hdmi_audio_max_i2s_playback_channels,
						    bridge->hdmi_audio_spdif_playback,
						    bridge->hdmi_audio_dai_port);
		if (ret)
			return ERR_PTR(ret);
	}

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
