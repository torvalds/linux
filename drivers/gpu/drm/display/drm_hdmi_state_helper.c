// SPDX-License-Identifier: MIT

#include <drm/drm_atomic.h>
#include <drm/drm_connector.h>

#include <drm/display/drm_hdmi_helper.h>
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

static const struct drm_display_mode *
connector_state_get_mode(const struct drm_connector_state *conn_state)
{
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;

	state = conn_state->state;
	if (!state)
		return NULL;

	crtc = conn_state->crtc;
	if (!crtc)
		return NULL;

	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	if (!crtc_state)
		return NULL;

	return &crtc_state->mode;
}

static enum drm_mode_status
hdmi_clock_valid(const struct drm_connector *connector,
		 const struct drm_display_mode *mode,
		 unsigned long long clock)
{
	const struct drm_display_info *info = &connector->display_info;

	if (info->max_tmds_clock && clock > info->max_tmds_clock * 1000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static int
hdmi_compute_clock(const struct drm_connector *connector,
		   struct drm_connector_state *conn_state,
		   const struct drm_display_mode *mode,
		   unsigned int bpc, enum hdmi_colorspace fmt)
{
	enum drm_mode_status status;
	unsigned long long clock;

	clock = drm_hdmi_compute_mode_clock(mode, bpc, fmt);
	if (!clock)
		return -EINVAL;

	status = hdmi_clock_valid(connector, mode, clock);
	if (status != MODE_OK)
		return -EINVAL;

	conn_state->hdmi.tmds_char_rate = clock;

	return 0;
}

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
	const struct drm_display_mode *mode =
		connector_state_get_mode(new_conn_state);
	int ret;

	ret = hdmi_compute_clock(connector, new_conn_state, mode,
				 new_conn_state->hdmi.output_bpc,
				 new_conn_state->hdmi.output_format);
	if (ret)
		return ret;

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
