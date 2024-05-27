// SPDX-License-Identifier: MIT

#include <drm/drm_atomic.h>
#include <drm/drm_connector.h>

#include <drm/display/drm_hdmi_state_helper.h>

/**
 * __drm_atomic_helper_connector_hdmi_reset() - Initializes all HDMI @drm_connector_state resources
 * @connector: DRM connector
 * @new_conn_state: connector state to reset
 *
 * Initializes all HDMI resources from a @drm_connector_state without
 * actually allocating it. This is useful for HDMI drivers, in
 * combination with __drm_atomic_helper_connector_reset() or
 * drm_atomic_helper_connector_reset().
 */
void __drm_atomic_helper_connector_hdmi_reset(struct drm_connector *connector,
					      struct drm_connector_state *new_conn_state)
{
	unsigned int max_bpc = connector->max_bpc;

	new_conn_state->max_bpc = max_bpc;
	new_conn_state->max_requested_bpc = max_bpc;
}
EXPORT_SYMBOL(__drm_atomic_helper_connector_hdmi_reset);

/**
 * drm_atomic_helper_connector_hdmi_check() - Helper to check HDMI connector atomic state
 * @connector: DRM Connector
 * @state: the DRM State object
 *
 * Provides a default connector state check handler for HDMI connectors.
 * Checks that a desired connector update is valid, and updates various
 * fields of derived state.
 *
 * RETURNS:
 * Zero on success, or an errno code otherwise.
 */
int drm_atomic_helper_connector_hdmi_check(struct drm_connector *connector,
					   struct drm_atomic_state *state)
{
	struct drm_connector_state *old_conn_state =
		drm_atomic_get_old_connector_state(state, connector);
	struct drm_connector_state *new_conn_state =
		drm_atomic_get_new_connector_state(state, connector);

	if (old_conn_state->hdmi.output_bpc != new_conn_state->hdmi.output_bpc ||
	    old_conn_state->hdmi.output_format != new_conn_state->hdmi.output_format) {
		struct drm_crtc *crtc = new_conn_state->crtc;
		struct drm_crtc_state *crtc_state;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		crtc_state->mode_changed = true;
	}

	return 0;
}
EXPORT_SYMBOL(drm_atomic_helper_connector_hdmi_check);
