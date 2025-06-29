// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/export.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_helper.h>
#include <drm/drm_modeset_lock.h>

/**
 * drm_bridge_helper_reset_crtc - Reset the pipeline feeding a bridge
 * @bridge: DRM bridge to reset
 * @ctx: lock acquisition context
 *
 * Reset a @bridge pipeline. It will power-cycle all active components
 * between the CRTC and connector that bridge is connected to.
 *
 * As it relies on drm_atomic_helper_reset_crtc(), the same limitations
 * apply.
 *
 * Returns:
 *
 * 0 on success or a negative error code on failure. If the error
 * returned is EDEADLK, the whole atomic sequence must be restarted.
 */
int drm_bridge_helper_reset_crtc(struct drm_bridge *bridge,
				 struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_connector *connector;
	struct drm_encoder *encoder = bridge->encoder;
	struct drm_device *dev = encoder->dev;
	struct drm_crtc *crtc;
	int ret;

	ret = drm_modeset_lock(&dev->mode_config.connection_mutex, ctx);
	if (ret)
		return ret;

	connector = drm_atomic_get_connector_for_encoder(encoder, ctx);
	if (IS_ERR(connector)) {
		ret = PTR_ERR(connector);
		goto out;
	}

	if (!connector->state) {
		ret = -EINVAL;
		goto out;
	}

	crtc = connector->state->crtc;
	ret = drm_atomic_helper_reset_crtc(crtc, ctx);
	if (ret)
		goto out;

out:
	drm_modeset_unlock(&dev->mode_config.connection_mutex);
	return ret;
}
EXPORT_SYMBOL(drm_bridge_helper_reset_crtc);
