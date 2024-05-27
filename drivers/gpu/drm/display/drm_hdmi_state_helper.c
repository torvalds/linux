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
	return 0;
}
EXPORT_SYMBOL(drm_atomic_helper_connector_hdmi_check);
