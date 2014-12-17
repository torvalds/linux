/*
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 *
 * DRM core CRTC related functions
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
 *
 * Authors:
 *      Keith Packard
 *	Eric Anholt <eric@anholt.net>
 *      Dave Airlie <airlied@linux.ie>
 *      Jesse Barnes <jesse.barnes@intel.com>
 */

#include <linux/export.h>
#include <linux/moduleparam.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_edid.h>

/**
 * DOC: output probing helper overview
 *
 * This library provides some helper code for output probing. It provides an
 * implementation of the core connector->fill_modes interface with
 * drm_helper_probe_single_connector_modes.
 *
 * It also provides support for polling connectors with a work item and for
 * generic hotplug interrupt handling where the driver doesn't or cannot keep
 * track of a per-connector hpd interrupt.
 *
 * This helper library can be used independently of the modeset helper library.
 * Drivers can also overwrite different parts e.g. use their own hotplug
 * handling code to avoid probing unrelated outputs.
 */

static bool drm_kms_helper_poll = true;
module_param_named(poll, drm_kms_helper_poll, bool, 0600);

static enum drm_mode_status
drm_mode_validate_flag(const struct drm_display_mode *mode,
		       int flags)
{
	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) &&
	    !(flags & DRM_MODE_FLAG_INTERLACE))
		return MODE_NO_INTERLACE;

	if ((mode->flags & DRM_MODE_FLAG_DBLSCAN) &&
	    !(flags & DRM_MODE_FLAG_DBLSCAN))
		return MODE_NO_DBLESCAN;

	if ((mode->flags & DRM_MODE_FLAG_3D_MASK) &&
	    !(flags & DRM_MODE_FLAG_3D_MASK))
		return MODE_NO_STEREO;

	return MODE_OK;
}

static int drm_helper_probe_add_cmdline_mode(struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	if (!connector->cmdline_mode.specified)
		return 0;

	mode = drm_mode_create_from_cmdline_mode(connector->dev,
						 &connector->cmdline_mode);
	if (mode == NULL)
		return 0;

	drm_mode_probed_add(connector, mode);
	return 1;
}

static int drm_helper_probe_single_connector_modes_merge_bits(struct drm_connector *connector,
							      uint32_t maxX, uint32_t maxY, bool merge_type_bits)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;
	struct drm_connector_helper_funcs *connector_funcs =
		connector->helper_private;
	int count = 0;
	int mode_flags = 0;
	bool verbose_prune = true;

	WARN_ON(!mutex_is_locked(&dev->mode_config.mutex));

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n", connector->base.id,
			connector->name);
	/* set all modes to the unverified state */
	list_for_each_entry(mode, &connector->modes, head)
		mode->status = MODE_UNVERIFIED;

	if (connector->force) {
		if (connector->force == DRM_FORCE_ON ||
		    connector->force == DRM_FORCE_ON_DIGITAL)
			connector->status = connector_status_connected;
		else
			connector->status = connector_status_disconnected;
		if (connector->funcs->force)
			connector->funcs->force(connector);
	} else {
		connector->status = connector->funcs->detect(connector, true);
	}

	/* Re-enable polling in case the global poll config changed. */
	if (drm_kms_helper_poll != dev->mode_config.poll_running)
		drm_kms_helper_poll_enable(dev);

	dev->mode_config.poll_running = drm_kms_helper_poll;

	if (connector->status == connector_status_disconnected) {
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] disconnected\n",
			connector->base.id, connector->name);
		drm_mode_connector_update_edid_property(connector, NULL);
		verbose_prune = false;
		goto prune;
	}

#ifdef CONFIG_DRM_LOAD_EDID_FIRMWARE
	count = drm_load_edid_firmware(connector);
	if (count == 0)
#endif
	{
		if (connector->override_edid) {
			struct edid *edid = (struct edid *) connector->edid_blob_ptr->data;

			count = drm_add_edid_modes(connector, edid);
		} else
			count = (*connector_funcs->get_modes)(connector);
	}

	if (count == 0 && connector->status == connector_status_connected)
		count = drm_add_modes_noedid(connector, 1024, 768);
	count += drm_helper_probe_add_cmdline_mode(connector);
	if (count == 0)
		goto prune;

	drm_mode_connector_list_update(connector, merge_type_bits);

	if (connector->interlace_allowed)
		mode_flags |= DRM_MODE_FLAG_INTERLACE;
	if (connector->doublescan_allowed)
		mode_flags |= DRM_MODE_FLAG_DBLSCAN;
	if (connector->stereo_allowed)
		mode_flags |= DRM_MODE_FLAG_3D_MASK;

	list_for_each_entry(mode, &connector->modes, head) {
		mode->status = drm_mode_validate_size(mode, maxX, maxY);

		if (mode->status == MODE_OK)
			mode->status = drm_mode_validate_flag(mode, mode_flags);

		if (mode->status == MODE_OK && connector_funcs->mode_valid)
			mode->status = connector_funcs->mode_valid(connector,
								   mode);
	}

prune:
	drm_mode_prune_invalid(dev, &connector->modes, verbose_prune);

	if (list_empty(&connector->modes))
		return 0;

	list_for_each_entry(mode, &connector->modes, head)
		mode->vrefresh = drm_mode_vrefresh(mode);

	drm_mode_sort(&connector->modes);

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s] probed modes :\n", connector->base.id,
			connector->name);
	list_for_each_entry(mode, &connector->modes, head) {
		drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V);
		drm_mode_debug_printmodeline(mode);
	}

	return count;
}

/**
 * drm_helper_probe_single_connector_modes - get complete set of display modes
 * @connector: connector to probe
 * @maxX: max width for modes
 * @maxY: max height for modes
 *
 * Based on the helper callbacks implemented by @connector try to detect all
 * valid modes.  Modes will first be added to the connector's probed_modes list,
 * then culled (based on validity and the @maxX, @maxY parameters) and put into
 * the normal modes list.
 *
 * Intended to be use as a generic implementation of the ->fill_modes()
 * @connector vfunc for drivers that use the crtc helpers for output mode
 * filtering and detection.
 *
 * Returns:
 * The number of modes found on @connector.
 */
int drm_helper_probe_single_connector_modes(struct drm_connector *connector,
					    uint32_t maxX, uint32_t maxY)
{
	return drm_helper_probe_single_connector_modes_merge_bits(connector, maxX, maxY, true);
}
EXPORT_SYMBOL(drm_helper_probe_single_connector_modes);

/**
 * drm_helper_probe_single_connector_modes_nomerge - get complete set of display modes
 * @connector: connector to probe
 * @maxX: max width for modes
 * @maxY: max height for modes
 *
 * This operates like drm_hehlper_probe_single_connector_modes except it
 * replaces the mode bits instead of merging them for preferred modes.
 */
int drm_helper_probe_single_connector_modes_nomerge(struct drm_connector *connector,
					    uint32_t maxX, uint32_t maxY)
{
	return drm_helper_probe_single_connector_modes_merge_bits(connector, maxX, maxY, false);
}
EXPORT_SYMBOL(drm_helper_probe_single_connector_modes_nomerge);

/**
 * drm_kms_helper_hotplug_event - fire off KMS hotplug events
 * @dev: drm_device whose connector state changed
 *
 * This function fires off the uevent for userspace and also calls the
 * output_poll_changed function, which is most commonly used to inform the fbdev
 * emulation code and allow it to update the fbcon output configuration.
 *
 * Drivers should call this from their hotplug handling code when a change is
 * detected. Note that this function does not do any output detection of its
 * own, like drm_helper_hpd_irq_event() does - this is assumed to be done by the
 * driver already.
 *
 * This function must be called from process context with no mode
 * setting locks held.
 */
void drm_kms_helper_hotplug_event(struct drm_device *dev)
{
	/* send a uevent + call fbdev */
	drm_sysfs_hotplug_event(dev);
	if (dev->mode_config.funcs->output_poll_changed)
		dev->mode_config.funcs->output_poll_changed(dev);
}
EXPORT_SYMBOL(drm_kms_helper_hotplug_event);

#define DRM_OUTPUT_POLL_PERIOD (10*HZ)
static void output_poll_execute(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct drm_device *dev = container_of(delayed_work, struct drm_device, mode_config.output_poll_work);
	struct drm_connector *connector;
	enum drm_connector_status old_status;
	bool repoll = false, changed = false;

	if (!drm_kms_helper_poll)
		return;

	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {

		/* Ignore forced connectors. */
		if (connector->force)
			continue;

		/* Ignore HPD capable connectors and connectors where we don't
		 * want any hotplug detection at all for polling. */
		if (!connector->polled || connector->polled == DRM_CONNECTOR_POLL_HPD)
			continue;

		repoll = true;

		old_status = connector->status;
		/* if we are connected and don't want to poll for disconnect
		   skip it */
		if (old_status == connector_status_connected &&
		    !(connector->polled & DRM_CONNECTOR_POLL_DISCONNECT))
			continue;

		connector->status = connector->funcs->detect(connector, false);
		if (old_status != connector->status) {
			const char *old, *new;

			old = drm_get_connector_status_name(old_status);
			new = drm_get_connector_status_name(connector->status);

			DRM_DEBUG_KMS("[CONNECTOR:%d:%s] "
				      "status updated from %s to %s\n",
				      connector->base.id,
				      connector->name,
				      old, new);

			changed = true;
		}
	}

	mutex_unlock(&dev->mode_config.mutex);

	if (changed)
		drm_kms_helper_hotplug_event(dev);

	if (repoll)
		schedule_delayed_work(delayed_work, DRM_OUTPUT_POLL_PERIOD);
}

/**
 * drm_kms_helper_poll_disable - disable output polling
 * @dev: drm_device
 *
 * This function disables the output polling work.
 *
 * Drivers can call this helper from their device suspend implementation. It is
 * not an error to call this even when output polling isn't enabled or arlready
 * disabled.
 */
void drm_kms_helper_poll_disable(struct drm_device *dev)
{
	if (!dev->mode_config.poll_enabled)
		return;
	cancel_delayed_work_sync(&dev->mode_config.output_poll_work);
}
EXPORT_SYMBOL(drm_kms_helper_poll_disable);

/**
 * drm_kms_helper_poll_enable - re-enable output polling.
 * @dev: drm_device
 *
 * This function re-enables the output polling work.
 *
 * Drivers can call this helper from their device resume implementation. It is
 * an error to call this when the output polling support has not yet been set
 * up.
 */
void drm_kms_helper_poll_enable(struct drm_device *dev)
{
	bool poll = false;
	struct drm_connector *connector;

	if (!dev->mode_config.poll_enabled || !drm_kms_helper_poll)
		return;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->polled & (DRM_CONNECTOR_POLL_CONNECT |
					 DRM_CONNECTOR_POLL_DISCONNECT))
			poll = true;
	}

	if (poll)
		schedule_delayed_work(&dev->mode_config.output_poll_work, DRM_OUTPUT_POLL_PERIOD);
}
EXPORT_SYMBOL(drm_kms_helper_poll_enable);

/**
 * drm_kms_helper_poll_init - initialize and enable output polling
 * @dev: drm_device
 *
 * This function intializes and then also enables output polling support for
 * @dev. Drivers which do not have reliable hotplug support in hardware can use
 * this helper infrastructure to regularly poll such connectors for changes in
 * their connection state.
 *
 * Drivers can control which connectors are polled by setting the
 * DRM_CONNECTOR_POLL_CONNECT and DRM_CONNECTOR_POLL_DISCONNECT flags. On
 * connectors where probing live outputs can result in visual distortion drivers
 * should not set the DRM_CONNECTOR_POLL_DISCONNECT flag to avoid this.
 * Connectors which have no flag or only DRM_CONNECTOR_POLL_HPD set are
 * completely ignored by the polling logic.
 *
 * Note that a connector can be both polled and probed from the hotplug handler,
 * in case the hotplug interrupt is known to be unreliable.
 */
void drm_kms_helper_poll_init(struct drm_device *dev)
{
	INIT_DELAYED_WORK(&dev->mode_config.output_poll_work, output_poll_execute);
	dev->mode_config.poll_enabled = true;

	drm_kms_helper_poll_enable(dev);
}
EXPORT_SYMBOL(drm_kms_helper_poll_init);

/**
 * drm_kms_helper_poll_fini - disable output polling and clean it up
 * @dev: drm_device
 */
void drm_kms_helper_poll_fini(struct drm_device *dev)
{
	drm_kms_helper_poll_disable(dev);
}
EXPORT_SYMBOL(drm_kms_helper_poll_fini);

/**
 * drm_helper_hpd_irq_event - hotplug processing
 * @dev: drm_device
 *
 * Drivers can use this helper function to run a detect cycle on all connectors
 * which have the DRM_CONNECTOR_POLL_HPD flag set in their &polled member. All
 * other connectors are ignored, which is useful to avoid reprobing fixed
 * panels.
 *
 * This helper function is useful for drivers which can't or don't track hotplug
 * interrupts for each connector.
 *
 * Drivers which support hotplug interrupts for each connector individually and
 * which have a more fine-grained detect logic should bypass this code and
 * directly call drm_kms_helper_hotplug_event() in case the connector state
 * changed.
 *
 * This function must be called from process context with no mode
 * setting locks held.
 *
 * Note that a connector can be both polled and probed from the hotplug handler,
 * in case the hotplug interrupt is known to be unreliable.
 */
bool drm_helper_hpd_irq_event(struct drm_device *dev)
{
	struct drm_connector *connector;
	enum drm_connector_status old_status;
	bool changed = false;

	if (!dev->mode_config.poll_enabled)
		return false;

	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {

		/* Only handle HPD capable connectors. */
		if (!(connector->polled & DRM_CONNECTOR_POLL_HPD))
			continue;

		old_status = connector->status;

		connector->status = connector->funcs->detect(connector, false);
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] status updated from %s to %s\n",
			      connector->base.id,
			      connector->name,
			      drm_get_connector_status_name(old_status),
			      drm_get_connector_status_name(connector->status));
		if (old_status != connector->status)
			changed = true;
	}

	mutex_unlock(&dev->mode_config.mutex);

	if (changed)
		drm_kms_helper_hotplug_event(dev);

	return changed;
}
EXPORT_SYMBOL(drm_helper_hpd_irq_event);
