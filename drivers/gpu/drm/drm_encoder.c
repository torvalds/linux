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
#include <drm/drm_encoder.h>

#include "drm_crtc_internal.h"

/**
 * DOC: overview
 *
 * Encoders represent the connecting element between the CRTC (as the overall
 * pixel pipeline, represented by &struct drm_crtc) and the connectors (as the
 * generic sink entity, represented by &struct drm_connector). An encoder takes
 * pixel data from a CRTC and converts it to a format suitable for any attached
 * connector. Encoders are objects exposed to userspace, originally to allow
 * userspace to infer cloning and connector/CRTC restrictions. Unfortunately
 * almost all drivers get this wrong, making the uabi pretty much useless. On
 * top of that the exposed restrictions are too simple for today's hardware, and
 * the recommended way to infer restrictions is by using the
 * DRM_MODE_ATOMIC_TEST_ONLY flag for the atomic IOCTL.
 *
 * Otherwise encoders aren't used in the uapi at all (any modeset request from
 * userspace directly connects a connector with a CRTC), drivers are therefore
 * free to use them however they wish. Modeset helper libraries make strong use
 * of encoders to facilitate code sharing. But for more complex settings it is
 * usually better to move shared code into a separate &drm_bridge. Compared to
 * encoders, bridges also have the benefit of being purely an internal
 * abstraction since they are not exposed to userspace at all.
 *
 * Encoders are initialized with drm_encoder_init() and cleaned up using
 * drm_encoder_cleanup().
 */
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

int drm_encoder_register_all(struct drm_device *dev)
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

void drm_encoder_unregister_all(struct drm_device *dev)
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
 * Initialises a preallocated encoder. Encoder should be subclassed as part of
 * driver encoder objects. At driver unload time drm_encoder_cleanup() should be
 * called from the driver's &drm_encoder_funcs.destroy hook.
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

	ret = drm_mode_object_add(dev, &encoder->base, DRM_MODE_OBJECT_ENCODER);
	if (ret)
		return ret;

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

	if (encoder->bridge) {
		struct drm_bridge *bridge = encoder->bridge;
		struct drm_bridge *next;

		while (bridge) {
			next = bridge->next;
			drm_bridge_detach(bridge);
			bridge = next;
		}
	}

	drm_mode_object_unregister(dev, &encoder->base);
	kfree(encoder->name);
	list_del(&encoder->head);
	dev->mode_config.num_encoder--;

	memset(encoder, 0, sizeof(*encoder));
}
EXPORT_SYMBOL(drm_encoder_cleanup);

static struct drm_crtc *drm_encoder_get_crtc(struct drm_encoder *encoder)
{
	struct drm_connector *connector;
	struct drm_device *dev = encoder->dev;
	bool uses_atomic = false;
	struct drm_connector_list_iter conn_iter;

	/* For atomic drivers only state objects are synchronously updated and
	 * protected by modeset locks, so check those first. */
	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (!connector->state)
			continue;

		uses_atomic = true;

		if (connector->state->best_encoder != encoder)
			continue;

		drm_connector_list_iter_end(&conn_iter);
		return connector->state->crtc;
	}
	drm_connector_list_iter_end(&conn_iter);

	/* Don't return stale data (e.g. pending async disable). */
	if (uses_atomic)
		return NULL;

	return encoder->crtc;
}

int drm_mode_getencoder(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_mode_get_encoder *enc_resp = data;
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	encoder = drm_encoder_find(dev, file_priv, enc_resp->encoder_id);
	if (!encoder)
		return -ENOENT;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	crtc = drm_encoder_get_crtc(encoder);
	if (crtc && drm_lease_held(file_priv, crtc->base.id))
		enc_resp->crtc_id = crtc->base.id;
	else
		enc_resp->crtc_id = 0;
	drm_modeset_unlock(&dev->mode_config.connection_mutex);

	enc_resp->encoder_type = encoder->encoder_type;
	enc_resp->encoder_id = encoder->base.id;
	enc_resp->possible_crtcs = drm_lease_filter_crtcs(file_priv,
							  encoder->possible_crtcs);
	enc_resp->possible_clones = encoder->possible_clones;

	return 0;
}
