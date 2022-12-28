// SPDX-License-Identifier: MIT
/*
 * Copyright 2020 Noralf Trønnes
 */

#include <linux/backlight.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_file.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/gud.h>

#include "gud_internal.h"

struct gud_connector {
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct backlight_device *backlight;
	struct work_struct backlight_work;

	/* Supported properties */
	u16 *properties;
	unsigned int num_properties;

	/* Initial gadget tv state if applicable, applied on state reset */
	struct drm_tv_connector_state initial_tv_state;

	/*
	 * Initial gadget backlight brightness if applicable, applied on state reset.
	 * The value -ENODEV is used to signal no backlight.
	 */
	int initial_brightness;
};

static inline struct gud_connector *to_gud_connector(struct drm_connector *connector)
{
	return container_of(connector, struct gud_connector, connector);
}

static void gud_conn_err(struct drm_connector *connector, const char *msg, int ret)
{
	dev_err(connector->dev->dev, "%s: %s (ret=%d)\n", connector->name, msg, ret);
}

/*
 * Use a worker to avoid taking kms locks inside the backlight lock.
 * Other display drivers use backlight within their kms locks.
 * This avoids inconsistent locking rules, which would upset lockdep.
 */
static void gud_connector_backlight_update_status_work(struct work_struct *work)
{
	struct gud_connector *gconn = container_of(work, struct gud_connector, backlight_work);
	struct drm_connector *connector = &gconn->connector;
	struct drm_connector_state *connector_state;
	struct drm_device *drm = connector->dev;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_atomic_state *state;
	int idx, ret;

	if (!drm_dev_enter(drm, &idx))
		return;

	state = drm_atomic_state_alloc(drm);
	if (!state) {
		ret = -ENOMEM;
		goto exit;
	}

	drm_modeset_acquire_init(&ctx, 0);
	state->acquire_ctx = &ctx;
retry:
	connector_state = drm_atomic_get_connector_state(state, connector);
	if (IS_ERR(connector_state)) {
		ret = PTR_ERR(connector_state);
		goto out;
	}

	/* Reuse tv.brightness to avoid having to subclass */
	connector_state->tv.brightness = gconn->backlight->props.brightness;

	ret = drm_atomic_commit(state);
out:
	if (ret == -EDEADLK) {
		drm_atomic_state_clear(state);
		drm_modeset_backoff(&ctx);
		goto retry;
	}

	drm_atomic_state_put(state);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
exit:
	drm_dev_exit(idx);

	if (ret)
		dev_err(drm->dev, "Failed to update backlight, err=%d\n", ret);
}

static int gud_connector_backlight_update_status(struct backlight_device *bd)
{
	struct drm_connector *connector = bl_get_data(bd);
	struct gud_connector *gconn = to_gud_connector(connector);

	/* The USB timeout is 5 seconds so use system_long_wq for worst case scenario */
	queue_work(system_long_wq, &gconn->backlight_work);

	return 0;
}

static const struct backlight_ops gud_connector_backlight_ops = {
	.update_status	= gud_connector_backlight_update_status,
};

static int gud_connector_backlight_register(struct gud_connector *gconn)
{
	struct drm_connector *connector = &gconn->connector;
	struct backlight_device *bd;
	const char *name;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.scale = BACKLIGHT_SCALE_NON_LINEAR,
		.max_brightness = 100,
		.brightness = gconn->initial_brightness,
	};

	name = kasprintf(GFP_KERNEL, "card%d-%s-backlight",
			 connector->dev->primary->index, connector->name);
	if (!name)
		return -ENOMEM;

	bd = backlight_device_register(name, connector->kdev, connector,
				       &gud_connector_backlight_ops, &props);
	kfree(name);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	gconn->backlight = bd;

	return 0;
}

static int gud_connector_detect(struct drm_connector *connector,
				struct drm_modeset_acquire_ctx *ctx, bool force)
{
	struct gud_device *gdrm = to_gud_device(connector->dev);
	int idx, ret;
	u8 status;

	if (!drm_dev_enter(connector->dev, &idx))
		return connector_status_disconnected;

	if (force) {
		ret = gud_usb_set(gdrm, GUD_REQ_SET_CONNECTOR_FORCE_DETECT,
				  connector->index, NULL, 0);
		if (ret) {
			ret = connector_status_unknown;
			goto exit;
		}
	}

	ret = gud_usb_get_u8(gdrm, GUD_REQ_GET_CONNECTOR_STATUS, connector->index, &status);
	if (ret) {
		ret = connector_status_unknown;
		goto exit;
	}

	switch (status & GUD_CONNECTOR_STATUS_CONNECTED_MASK) {
	case GUD_CONNECTOR_STATUS_DISCONNECTED:
		ret = connector_status_disconnected;
		break;
	case GUD_CONNECTOR_STATUS_CONNECTED:
		ret = connector_status_connected;
		break;
	default:
		ret = connector_status_unknown;
		break;
	}

	if (status & GUD_CONNECTOR_STATUS_CHANGED)
		connector->epoch_counter += 1;
exit:
	drm_dev_exit(idx);

	return ret;
}

struct gud_connector_get_edid_ctx {
	void *buf;
	size_t len;
	bool edid_override;
};

static int gud_connector_get_edid_block(void *data, u8 *buf, unsigned int block, size_t len)
{
	struct gud_connector_get_edid_ctx *ctx = data;
	size_t start = block * EDID_LENGTH;

	ctx->edid_override = false;

	if (start + len > ctx->len)
		return -1;

	memcpy(buf, ctx->buf + start, len);

	return 0;
}

static int gud_connector_get_modes(struct drm_connector *connector)
{
	struct gud_device *gdrm = to_gud_device(connector->dev);
	struct gud_display_mode_req *reqmodes = NULL;
	struct gud_connector_get_edid_ctx edid_ctx;
	unsigned int i, num_modes = 0;
	struct edid *edid = NULL;
	int idx, ret;

	if (!drm_dev_enter(connector->dev, &idx))
		return 0;

	edid_ctx.edid_override = true;
	edid_ctx.buf = kmalloc(GUD_CONNECTOR_MAX_EDID_LEN, GFP_KERNEL);
	if (!edid_ctx.buf)
		goto out;

	ret = gud_usb_get(gdrm, GUD_REQ_GET_CONNECTOR_EDID, connector->index,
			  edid_ctx.buf, GUD_CONNECTOR_MAX_EDID_LEN);
	if (ret > 0 && ret % EDID_LENGTH) {
		gud_conn_err(connector, "Invalid EDID size", ret);
	} else if (ret > 0) {
		edid_ctx.len = ret;
		edid = drm_do_get_edid(connector, gud_connector_get_edid_block, &edid_ctx);
	}

	kfree(edid_ctx.buf);
	drm_connector_update_edid_property(connector, edid);

	if (edid && edid_ctx.edid_override)
		goto out;

	reqmodes = kmalloc_array(GUD_CONNECTOR_MAX_NUM_MODES, sizeof(*reqmodes), GFP_KERNEL);
	if (!reqmodes)
		goto out;

	ret = gud_usb_get(gdrm, GUD_REQ_GET_CONNECTOR_MODES, connector->index,
			  reqmodes, GUD_CONNECTOR_MAX_NUM_MODES * sizeof(*reqmodes));
	if (ret <= 0)
		goto out;
	if (ret % sizeof(*reqmodes)) {
		gud_conn_err(connector, "Invalid display mode array size", ret);
		goto out;
	}

	num_modes = ret / sizeof(*reqmodes);

	for (i = 0; i < num_modes; i++) {
		struct drm_display_mode *mode;

		mode = drm_mode_create(connector->dev);
		if (!mode) {
			num_modes = i;
			goto out;
		}

		gud_to_display_mode(mode, &reqmodes[i]);
		drm_mode_probed_add(connector, mode);
	}
out:
	if (!num_modes)
		num_modes = drm_add_edid_modes(connector, edid);

	kfree(reqmodes);
	kfree(edid);
	drm_dev_exit(idx);

	return num_modes;
}

static int gud_connector_atomic_check(struct drm_connector *connector,
				      struct drm_atomic_state *state)
{
	struct drm_connector_state *new_state;
	struct drm_crtc_state *new_crtc_state;
	struct drm_connector_state *old_state;

	new_state = drm_atomic_get_new_connector_state(state, connector);
	if (!new_state->crtc)
		return 0;

	old_state = drm_atomic_get_old_connector_state(state, connector);
	new_crtc_state = drm_atomic_get_new_crtc_state(state, new_state->crtc);

	if (old_state->tv.margins.left != new_state->tv.margins.left ||
	    old_state->tv.margins.right != new_state->tv.margins.right ||
	    old_state->tv.margins.top != new_state->tv.margins.top ||
	    old_state->tv.margins.bottom != new_state->tv.margins.bottom ||
	    old_state->tv.mode != new_state->tv.mode ||
	    old_state->tv.brightness != new_state->tv.brightness ||
	    old_state->tv.contrast != new_state->tv.contrast ||
	    old_state->tv.flicker_reduction != new_state->tv.flicker_reduction ||
	    old_state->tv.overscan != new_state->tv.overscan ||
	    old_state->tv.saturation != new_state->tv.saturation ||
	    old_state->tv.hue != new_state->tv.hue)
		new_crtc_state->connectors_changed = true;

	return 0;
}

static const struct drm_connector_helper_funcs gud_connector_helper_funcs = {
	.detect_ctx = gud_connector_detect,
	.get_modes = gud_connector_get_modes,
	.atomic_check = gud_connector_atomic_check,
};

static int gud_connector_late_register(struct drm_connector *connector)
{
	struct gud_connector *gconn = to_gud_connector(connector);

	if (gconn->initial_brightness < 0)
		return 0;

	return gud_connector_backlight_register(gconn);
}

static void gud_connector_early_unregister(struct drm_connector *connector)
{
	struct gud_connector *gconn = to_gud_connector(connector);

	backlight_device_unregister(gconn->backlight);
	cancel_work_sync(&gconn->backlight_work);
}

static void gud_connector_destroy(struct drm_connector *connector)
{
	struct gud_connector *gconn = to_gud_connector(connector);

	drm_connector_cleanup(connector);
	kfree(gconn->properties);
	kfree(gconn);
}

static void gud_connector_reset(struct drm_connector *connector)
{
	struct gud_connector *gconn = to_gud_connector(connector);

	drm_atomic_helper_connector_reset(connector);
	connector->state->tv = gconn->initial_tv_state;
	/* Set margins from command line */
	drm_atomic_helper_connector_tv_margins_reset(connector);
	if (gconn->initial_brightness >= 0)
		connector->state->tv.brightness = gconn->initial_brightness;
}

static const struct drm_connector_funcs gud_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.late_register = gud_connector_late_register,
	.early_unregister = gud_connector_early_unregister,
	.destroy = gud_connector_destroy,
	.reset = gud_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/*
 * The tv.mode property is shared among the connectors and its enum names are
 * driver specific. This means that if more than one connector uses tv.mode,
 * the enum names has to be the same.
 */
static int gud_connector_add_tv_mode(struct gud_device *gdrm, struct drm_connector *connector)
{
	size_t buf_len = GUD_CONNECTOR_TV_MODE_MAX_NUM * GUD_CONNECTOR_TV_MODE_NAME_LEN;
	const char *modes[GUD_CONNECTOR_TV_MODE_MAX_NUM];
	unsigned int i, num_modes;
	char *buf;
	int ret;

	buf = kmalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = gud_usb_get(gdrm, GUD_REQ_GET_CONNECTOR_TV_MODE_VALUES,
			  connector->index, buf, buf_len);
	if (ret < 0)
		goto free;
	if (!ret || ret % GUD_CONNECTOR_TV_MODE_NAME_LEN) {
		ret = -EIO;
		goto free;
	}

	num_modes = ret / GUD_CONNECTOR_TV_MODE_NAME_LEN;
	for (i = 0; i < num_modes; i++)
		modes[i] = &buf[i * GUD_CONNECTOR_TV_MODE_NAME_LEN];

	ret = drm_mode_create_tv_properties(connector->dev, num_modes, modes);
free:
	kfree(buf);
	if (ret < 0)
		gud_conn_err(connector, "Failed to add TV modes", ret);

	return ret;
}

static struct drm_property *
gud_connector_property_lookup(struct drm_connector *connector, u16 prop)
{
	struct drm_mode_config *config = &connector->dev->mode_config;

	switch (prop) {
	case GUD_PROPERTY_TV_LEFT_MARGIN:
		return config->tv_left_margin_property;
	case GUD_PROPERTY_TV_RIGHT_MARGIN:
		return config->tv_right_margin_property;
	case GUD_PROPERTY_TV_TOP_MARGIN:
		return config->tv_top_margin_property;
	case GUD_PROPERTY_TV_BOTTOM_MARGIN:
		return config->tv_bottom_margin_property;
	case GUD_PROPERTY_TV_MODE:
		return config->tv_mode_property;
	case GUD_PROPERTY_TV_BRIGHTNESS:
		return config->tv_brightness_property;
	case GUD_PROPERTY_TV_CONTRAST:
		return config->tv_contrast_property;
	case GUD_PROPERTY_TV_FLICKER_REDUCTION:
		return config->tv_flicker_reduction_property;
	case GUD_PROPERTY_TV_OVERSCAN:
		return config->tv_overscan_property;
	case GUD_PROPERTY_TV_SATURATION:
		return config->tv_saturation_property;
	case GUD_PROPERTY_TV_HUE:
		return config->tv_hue_property;
	default:
		return ERR_PTR(-EINVAL);
	}
}

static unsigned int *gud_connector_tv_state_val(u16 prop, struct drm_tv_connector_state *state)
{
	switch (prop) {
	case GUD_PROPERTY_TV_LEFT_MARGIN:
		return &state->margins.left;
	case GUD_PROPERTY_TV_RIGHT_MARGIN:
		return &state->margins.right;
	case GUD_PROPERTY_TV_TOP_MARGIN:
		return &state->margins.top;
	case GUD_PROPERTY_TV_BOTTOM_MARGIN:
		return &state->margins.bottom;
	case GUD_PROPERTY_TV_MODE:
		return &state->mode;
	case GUD_PROPERTY_TV_BRIGHTNESS:
		return &state->brightness;
	case GUD_PROPERTY_TV_CONTRAST:
		return &state->contrast;
	case GUD_PROPERTY_TV_FLICKER_REDUCTION:
		return &state->flicker_reduction;
	case GUD_PROPERTY_TV_OVERSCAN:
		return &state->overscan;
	case GUD_PROPERTY_TV_SATURATION:
		return &state->saturation;
	case GUD_PROPERTY_TV_HUE:
		return &state->hue;
	default:
		return ERR_PTR(-EINVAL);
	}
}

static int gud_connector_add_properties(struct gud_device *gdrm, struct gud_connector *gconn)
{
	struct drm_connector *connector = &gconn->connector;
	struct drm_device *drm = &gdrm->drm;
	struct gud_property_req *properties;
	unsigned int i, num_properties;
	int ret;

	properties = kcalloc(GUD_CONNECTOR_PROPERTIES_MAX_NUM, sizeof(*properties), GFP_KERNEL);
	if (!properties)
		return -ENOMEM;

	ret = gud_usb_get(gdrm, GUD_REQ_GET_CONNECTOR_PROPERTIES, connector->index,
			  properties, GUD_CONNECTOR_PROPERTIES_MAX_NUM * sizeof(*properties));
	if (ret <= 0)
		goto out;
	if (ret % sizeof(*properties)) {
		ret = -EIO;
		goto out;
	}

	num_properties = ret / sizeof(*properties);
	ret = 0;

	gconn->properties = kcalloc(num_properties, sizeof(*gconn->properties), GFP_KERNEL);
	if (!gconn->properties) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_properties; i++) {
		u16 prop = le16_to_cpu(properties[i].prop);
		u64 val = le64_to_cpu(properties[i].val);
		struct drm_property *property;
		unsigned int *state_val;

		drm_dbg(drm, "property: %u = %llu(0x%llx)\n", prop, val, val);

		switch (prop) {
		case GUD_PROPERTY_TV_LEFT_MARGIN:
			fallthrough;
		case GUD_PROPERTY_TV_RIGHT_MARGIN:
			fallthrough;
		case GUD_PROPERTY_TV_TOP_MARGIN:
			fallthrough;
		case GUD_PROPERTY_TV_BOTTOM_MARGIN:
			ret = drm_mode_create_tv_margin_properties(drm);
			if (ret)
				goto out;
			break;
		case GUD_PROPERTY_TV_MODE:
			ret = gud_connector_add_tv_mode(gdrm, connector);
			if (ret)
				goto out;
			break;
		case GUD_PROPERTY_TV_BRIGHTNESS:
			fallthrough;
		case GUD_PROPERTY_TV_CONTRAST:
			fallthrough;
		case GUD_PROPERTY_TV_FLICKER_REDUCTION:
			fallthrough;
		case GUD_PROPERTY_TV_OVERSCAN:
			fallthrough;
		case GUD_PROPERTY_TV_SATURATION:
			fallthrough;
		case GUD_PROPERTY_TV_HUE:
			/* This is a no-op if already added. */
			ret = drm_mode_create_tv_properties(drm, 0, NULL);
			if (ret)
				goto out;
			break;
		case GUD_PROPERTY_BACKLIGHT_BRIGHTNESS:
			if (val > 100) {
				ret = -EINVAL;
				goto out;
			}
			gconn->initial_brightness = val;
			break;
		default:
			/* New ones might show up in future devices, skip those we don't know. */
			drm_dbg(drm, "Ignoring unknown property: %u\n", prop);
			continue;
		}

		gconn->properties[gconn->num_properties++] = prop;

		if (prop == GUD_PROPERTY_BACKLIGHT_BRIGHTNESS)
			continue; /* not a DRM property */

		property = gud_connector_property_lookup(connector, prop);
		if (WARN_ON(IS_ERR(property)))
			continue;

		state_val = gud_connector_tv_state_val(prop, &gconn->initial_tv_state);
		if (WARN_ON(IS_ERR(state_val)))
			continue;

		*state_val = val;
		drm_object_attach_property(&connector->base, property, 0);
	}
out:
	kfree(properties);

	return ret;
}

int gud_connector_fill_properties(struct drm_connector_state *connector_state,
				  struct gud_property_req *properties)
{
	struct gud_connector *gconn = to_gud_connector(connector_state->connector);
	unsigned int i;

	for (i = 0; i < gconn->num_properties; i++) {
		u16 prop = gconn->properties[i];
		u64 val;

		if (prop == GUD_PROPERTY_BACKLIGHT_BRIGHTNESS) {
			val = connector_state->tv.brightness;
		} else {
			unsigned int *state_val;

			state_val = gud_connector_tv_state_val(prop, &connector_state->tv);
			if (WARN_ON_ONCE(IS_ERR(state_val)))
				return PTR_ERR(state_val);

			val = *state_val;
		}

		properties[i].prop = cpu_to_le16(prop);
		properties[i].val = cpu_to_le64(val);
	}

	return gconn->num_properties;
}

static int gud_connector_create(struct gud_device *gdrm, unsigned int index,
				struct gud_connector_descriptor_req *desc)
{
	struct drm_device *drm = &gdrm->drm;
	struct gud_connector *gconn;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	int ret, connector_type;
	u32 flags;

	gconn = kzalloc(sizeof(*gconn), GFP_KERNEL);
	if (!gconn)
		return -ENOMEM;

	INIT_WORK(&gconn->backlight_work, gud_connector_backlight_update_status_work);
	gconn->initial_brightness = -ENODEV;
	flags = le32_to_cpu(desc->flags);
	connector = &gconn->connector;

	drm_dbg(drm, "Connector: index=%u type=%u flags=0x%x\n", index, desc->connector_type, flags);

	switch (desc->connector_type) {
	case GUD_CONNECTOR_TYPE_PANEL:
		connector_type = DRM_MODE_CONNECTOR_USB;
		break;
	case GUD_CONNECTOR_TYPE_VGA:
		connector_type = DRM_MODE_CONNECTOR_VGA;
		break;
	case GUD_CONNECTOR_TYPE_DVI:
		connector_type = DRM_MODE_CONNECTOR_DVID;
		break;
	case GUD_CONNECTOR_TYPE_COMPOSITE:
		connector_type = DRM_MODE_CONNECTOR_Composite;
		break;
	case GUD_CONNECTOR_TYPE_SVIDEO:
		connector_type = DRM_MODE_CONNECTOR_SVIDEO;
		break;
	case GUD_CONNECTOR_TYPE_COMPONENT:
		connector_type = DRM_MODE_CONNECTOR_Component;
		break;
	case GUD_CONNECTOR_TYPE_DISPLAYPORT:
		connector_type = DRM_MODE_CONNECTOR_DisplayPort;
		break;
	case GUD_CONNECTOR_TYPE_HDMI:
		connector_type = DRM_MODE_CONNECTOR_HDMIA;
		break;
	default: /* future types */
		connector_type = DRM_MODE_CONNECTOR_USB;
		break;
	}

	drm_connector_helper_add(connector, &gud_connector_helper_funcs);
	ret = drm_connector_init(drm, connector, &gud_connector_funcs, connector_type);
	if (ret) {
		kfree(connector);
		return ret;
	}

	if (WARN_ON(connector->index != index))
		return -EINVAL;

	if (flags & GUD_CONNECTOR_FLAGS_POLL_STATUS)
		connector->polled = (DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT);
	if (flags & GUD_CONNECTOR_FLAGS_INTERLACE)
		connector->interlace_allowed = true;
	if (flags & GUD_CONNECTOR_FLAGS_DOUBLESCAN)
		connector->doublescan_allowed = true;

	ret = gud_connector_add_properties(gdrm, gconn);
	if (ret) {
		gud_conn_err(connector, "Failed to add properties", ret);
		return ret;
	}

	/* The first connector is attached to the existing simple pipe encoder */
	if (!connector->index) {
		encoder = &gdrm->pipe.encoder;
	} else {
		encoder = &gconn->encoder;

		ret = drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_NONE);
		if (ret)
			return ret;

		encoder->possible_crtcs = 1;
	}

	return drm_connector_attach_encoder(connector, encoder);
}

int gud_get_connectors(struct gud_device *gdrm)
{
	struct gud_connector_descriptor_req *descs;
	unsigned int i, num_connectors;
	int ret;

	descs = kmalloc_array(GUD_CONNECTORS_MAX_NUM, sizeof(*descs), GFP_KERNEL);
	if (!descs)
		return -ENOMEM;

	ret = gud_usb_get(gdrm, GUD_REQ_GET_CONNECTORS, 0,
			  descs, GUD_CONNECTORS_MAX_NUM * sizeof(*descs));
	if (ret < 0)
		goto free;
	if (!ret || ret % sizeof(*descs)) {
		ret = -EIO;
		goto free;
	}

	num_connectors = ret / sizeof(*descs);

	for (i = 0; i < num_connectors; i++) {
		ret = gud_connector_create(gdrm, i, &descs[i]);
		if (ret)
			goto free;
	}
free:
	kfree(descs);

	return ret;
}
