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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_client_event.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

/**
 * DOC: aux kms helpers
 *
 * This helper library contains various one-off functions which don't really fit
 * anywhere else in the DRM modeset helper library.
 */

/**
 * drm_helper_move_panel_connectors_to_head() - move panels to the front in the
 * 						connector list
 * @dev: drm device to operate on
 *
 * Some userspace presumes that the first connected connector is the main
 * display, where it's supposed to display e.g. the login screen. For
 * laptops, this should be the main panel. Use this function to sort all
 * (eDP/LVDS/DSI) panels to the front of the connector list, instead of
 * painstakingly trying to initialize them in the right order.
 */
void drm_helper_move_panel_connectors_to_head(struct drm_device *dev)
{
	struct drm_connector *connector, *tmp;
	struct list_head panel_list;

	INIT_LIST_HEAD(&panel_list);

	spin_lock_irq(&dev->mode_config.connector_list_lock);
	list_for_each_entry_safe(connector, tmp,
				 &dev->mode_config.connector_list, head) {
		if (connector->connector_type == DRM_MODE_CONNECTOR_LVDS ||
		    connector->connector_type == DRM_MODE_CONNECTOR_eDP ||
		    connector->connector_type == DRM_MODE_CONNECTOR_DSI)
			list_move_tail(&connector->head, &panel_list);
	}

	list_splice(&panel_list, &dev->mode_config.connector_list);
	spin_unlock_irq(&dev->mode_config.connector_list_lock);
}
EXPORT_SYMBOL(drm_helper_move_panel_connectors_to_head);

/**
 * drm_helper_mode_fill_fb_struct - fill out framebuffer metadata
 * @dev: DRM device
 * @fb: drm_framebuffer object to fill out
 * @info: pixel format information
 * @mode_cmd: metadata from the userspace fb creation request
 *
 * This helper can be used in a drivers fb_create callback to pre-fill the fb's
 * metadata fields.
 */
void drm_helper_mode_fill_fb_struct(struct drm_device *dev,
				    struct drm_framebuffer *fb,
				    const struct drm_format_info *info,
				    const struct drm_mode_fb_cmd2 *mode_cmd)
{
	int i;

	fb->dev = dev;
	fb->format = info;
	fb->width = mode_cmd->width;
	fb->height = mode_cmd->height;
	for (i = 0; i < 4; i++) {
		fb->pitches[i] = mode_cmd->pitches[i];
		fb->offsets[i] = mode_cmd->offsets[i];
	}
	fb->modifier = mode_cmd->modifier[0];
	fb->flags = mode_cmd->flags;
}
EXPORT_SYMBOL(drm_helper_mode_fill_fb_struct);

/*
 * This is the minimal list of formats that seem to be safe for modeset use
 * with all current DRM drivers.  Most hardware can actually support more
 * formats than this and drivers may specify a more accurate list when
 * creating the primary plane.
 */
static const uint32_t safe_modeset_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static const struct drm_plane_funcs primary_plane_funcs = {
	DRM_PLANE_NON_ATOMIC_FUNCS,
};

/**
 * drm_crtc_init - Legacy CRTC initialization function
 * @dev: DRM device
 * @crtc: CRTC object to init
 * @funcs: callbacks for the new CRTC
 *
 * Initialize a CRTC object with a default helper-provided primary plane and no
 * cursor plane.
 *
 * Note that we make some assumptions about hardware limitations that may not be
 * true for all hardware:
 *
 * 1. Primary plane cannot be repositioned.
 * 2. Primary plane cannot be scaled.
 * 3. Primary plane must cover the entire CRTC.
 * 4. Subpixel positioning is not supported.
 * 5. The primary plane must always be on if the CRTC is enabled.
 *
 * This is purely a backwards compatibility helper for old drivers. Drivers
 * should instead implement their own primary plane. Atomic drivers must do so.
 * Drivers with the above hardware restriction can look into using &struct
 * drm_simple_display_pipe, which encapsulates the above limitations into a nice
 * interface.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_crtc_init(struct drm_device *dev, struct drm_crtc *crtc,
		  const struct drm_crtc_funcs *funcs)
{
	struct drm_plane *primary;
	int ret;

	/* possible_crtc's will be filled in later by crtc_init */
	primary = __drm_universal_plane_alloc(dev, sizeof(*primary), 0, 0,
					      &primary_plane_funcs,
					      safe_modeset_formats,
					      ARRAY_SIZE(safe_modeset_formats),
					      NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (IS_ERR(primary))
		return PTR_ERR(primary);

	/*
	 * Remove the format_default field from drm_plane when dropping
	 * this helper.
	 */
	primary->format_default = true;

	ret = drm_crtc_init_with_planes(dev, crtc, primary, NULL, funcs, NULL);
	if (ret)
		goto err_drm_plane_cleanup;

	return 0;

err_drm_plane_cleanup:
	drm_plane_cleanup(primary);
	kfree(primary);
	return ret;
}
EXPORT_SYMBOL(drm_crtc_init);

/**
 * drm_mode_config_helper_suspend - Modeset suspend helper
 * @dev: DRM device
 *
 * This helper function takes care of suspending the modeset side. It disables
 * output polling if initialized, suspends fbdev if used and finally calls
 * drm_atomic_helper_suspend().
 * If suspending fails, fbdev and polling is re-enabled.
 *
 * Returns:
 * Zero on success, negative error code on error.
 *
 * See also:
 * drm_kms_helper_poll_disable() and drm_client_dev_suspend().
 */
int drm_mode_config_helper_suspend(struct drm_device *dev)
{
	struct drm_atomic_state *state;

	if (!dev)
		return 0;
	/*
	 * Don't disable polling if it was never initialized
	 */
	if (dev->mode_config.poll_enabled)
		drm_kms_helper_poll_disable(dev);

	drm_client_dev_suspend(dev, false);
	state = drm_atomic_helper_suspend(dev);
	if (IS_ERR(state)) {
		drm_client_dev_resume(dev, false);

		/*
		 * Don't enable polling if it was never initialized
		 */
		if (dev->mode_config.poll_enabled)
			drm_kms_helper_poll_enable(dev);

		return PTR_ERR(state);
	}

	dev->mode_config.suspend_state = state;

	return 0;
}
EXPORT_SYMBOL(drm_mode_config_helper_suspend);

/**
 * drm_mode_config_helper_resume - Modeset resume helper
 * @dev: DRM device
 *
 * This helper function takes care of resuming the modeset side. It calls
 * drm_atomic_helper_resume(), resumes fbdev if used and enables output polling
 * if initiaized.
 *
 * Returns:
 * Zero on success, negative error code on error.
 *
 * See also:
 * drm_client_dev_resume() and drm_kms_helper_poll_enable().
 */
int drm_mode_config_helper_resume(struct drm_device *dev)
{
	int ret;

	if (!dev)
		return 0;

	if (WARN_ON(!dev->mode_config.suspend_state))
		return -EINVAL;

	ret = drm_atomic_helper_resume(dev, dev->mode_config.suspend_state);
	if (ret)
		DRM_ERROR("Failed to resume (%d)\n", ret);
	dev->mode_config.suspend_state = NULL;

	drm_client_dev_resume(dev, false);

	/*
	 * Don't enable polling if it is not initialized
	 */
	if (dev->mode_config.poll_enabled)
		drm_kms_helper_poll_enable(dev);

	return ret;
}
EXPORT_SYMBOL(drm_mode_config_helper_resume);
