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

#include <drm/drm_bridge.h>
#include <drm/drm_client.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_sysfs.h>

#include "drm_crtc_helper_internal.h"

/**
 * DOC: output probing helper overview
 *
 * This library provides some helper code for output probing. It provides an
 * implementation of the core &drm_connector_funcs.fill_modes interface with
 * drm_helper_probe_single_connector_modes().
 *
 * It also provides support for polling connectors with a work item and for
 * generic hotplug interrupt handling where the driver doesn't or cannot keep
 * track of a per-connector hpd interrupt.
 *
 * This helper library can be used independently of the modeset helper library.
 * Drivers can also overwrite different parts e.g. use their own hotplug
 * handling code to avoid probing unrelated outputs.
 *
 * The probe helpers share the function table structures with other display
 * helper libraries. See &struct drm_connector_helper_funcs for the details.
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

static int
drm_mode_validate_pipeline(struct drm_display_mode *mode,
			   struct drm_connector *connector,
			   struct drm_modeset_acquire_ctx *ctx,
			   enum drm_mode_status *status)
{
	struct drm_device *dev = connector->dev;
	struct drm_encoder *encoder;
	int ret;

	/* Step 1: Validate against connector */
	ret = drm_connector_mode_valid(connector, mode, ctx, status);
	if (ret || *status != MODE_OK)
		return ret;

	/* Step 2: Validate against encoders and crtcs */
	drm_connector_for_each_possible_encoder(connector, encoder) {
		struct drm_bridge *bridge;
		struct drm_crtc *crtc;

		*status = drm_encoder_mode_valid(encoder, mode);
		if (*status != MODE_OK) {
			/* No point in continuing for crtc check as this encoder
			 * will not accept the mode anyway. If all encoders
			 * reject the mode then, at exit, ret will not be
			 * MODE_OK. */
			continue;
		}

		bridge = drm_bridge_chain_get_first_bridge(encoder);
		*status = drm_bridge_chain_mode_valid(bridge,
						      &connector->display_info,
						      mode);
		if (*status != MODE_OK) {
			/* There is also no point in continuing for crtc check
			 * here. */
			continue;
		}

		drm_for_each_crtc(crtc, dev) {
			if (!drm_encoder_crtc_ok(encoder, crtc))
				continue;

			*status = drm_crtc_mode_valid(crtc, mode);
			if (*status == MODE_OK) {
				/* If we get to this point there is at least
				 * one combination of encoder+crtc that works
				 * for this mode. Lets return now. */
				return 0;
			}
		}
	}

	return 0;
}

static int drm_helper_probe_add_cmdline_mode(struct drm_connector *connector)
{
	struct drm_cmdline_mode *cmdline_mode;
	struct drm_display_mode *mode;

	cmdline_mode = &connector->cmdline_mode;
	if (!cmdline_mode->specified)
		return 0;

	/* Only add a GTF mode if we find no matching probed modes */
	list_for_each_entry(mode, &connector->probed_modes, head) {
		if (mode->hdisplay != cmdline_mode->xres ||
		    mode->vdisplay != cmdline_mode->yres)
			continue;

		if (cmdline_mode->refresh_specified) {
			/* The probed mode's vrefresh is set until later */
			if (drm_mode_vrefresh(mode) != cmdline_mode->refresh)
				continue;
		}

		/* Mark the matching mode as being preferred by the user */
		mode->type |= DRM_MODE_TYPE_USERDEF;
		return 0;
	}

	mode = drm_mode_create_from_cmdline_mode(connector->dev,
						 cmdline_mode);
	if (mode == NULL)
		return 0;

	drm_mode_probed_add(connector, mode);
	return 1;
}

enum drm_mode_status drm_crtc_mode_valid(struct drm_crtc *crtc,
					 const struct drm_display_mode *mode)
{
	const struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;

	if (!crtc_funcs || !crtc_funcs->mode_valid)
		return MODE_OK;

	return crtc_funcs->mode_valid(crtc, mode);
}

enum drm_mode_status drm_encoder_mode_valid(struct drm_encoder *encoder,
					    const struct drm_display_mode *mode)
{
	const struct drm_encoder_helper_funcs *encoder_funcs =
		encoder->helper_private;

	if (!encoder_funcs || !encoder_funcs->mode_valid)
		return MODE_OK;

	return encoder_funcs->mode_valid(encoder, mode);
}

int
drm_connector_mode_valid(struct drm_connector *connector,
			 struct drm_display_mode *mode,
			 struct drm_modeset_acquire_ctx *ctx,
			 enum drm_mode_status *status)
{
	const struct drm_connector_helper_funcs *connector_funcs =
		connector->helper_private;
	int ret = 0;

	if (!connector_funcs)
		*status = MODE_OK;
	else if (connector_funcs->mode_valid_ctx)
		ret = connector_funcs->mode_valid_ctx(connector, mode, ctx,
						      status);
	else if (connector_funcs->mode_valid)
		*status = connector_funcs->mode_valid(connector, mode);
	else
		*status = MODE_OK;

	return ret;
}

#define DRM_OUTPUT_POLL_PERIOD (10*HZ)
/**
 * drm_kms_helper_poll_enable - re-enable output polling.
 * @dev: drm_device
 *
 * This function re-enables the output polling work, after it has been
 * temporarily disabled using drm_kms_helper_poll_disable(), for example over
 * suspend/resume.
 *
 * Drivers can call this helper from their device resume implementation. It is
 * not an error to call this even when output polling isn't enabled.
 *
 * If device polling was never initialized before, this call will trigger a
 * warning and return.
 *
 * Note that calls to enable and disable polling must be strictly ordered, which
 * is automatically the case when they're only call from suspend/resume
 * callbacks.
 */
void drm_kms_helper_poll_enable(struct drm_device *dev)
{
	bool poll = false;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	unsigned long delay = DRM_OUTPUT_POLL_PERIOD;

	if (drm_WARN_ON_ONCE(dev, !dev->mode_config.poll_enabled) ||
	    !drm_kms_helper_poll || dev->mode_config.poll_running)
		return;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->polled & (DRM_CONNECTOR_POLL_CONNECT |
					 DRM_CONNECTOR_POLL_DISCONNECT))
			poll = true;
	}
	drm_connector_list_iter_end(&conn_iter);

	if (dev->mode_config.delayed_event) {
		/*
		 * FIXME:
		 *
		 * Use short (1s) delay to handle the initial delayed event.
		 * This delay should not be needed, but Optimus/nouveau will
		 * fail in a mysterious way if the delayed event is handled as
		 * soon as possible like it is done in
		 * drm_helper_probe_single_connector_modes() in case the poll
		 * was enabled before.
		 */
		poll = true;
		delay = HZ;
	}

	if (poll)
		schedule_delayed_work(&dev->mode_config.output_poll_work, delay);
}
EXPORT_SYMBOL(drm_kms_helper_poll_enable);

static enum drm_connector_status
drm_helper_probe_detect_ctx(struct drm_connector *connector, bool force)
{
	const struct drm_connector_helper_funcs *funcs = connector->helper_private;
	struct drm_modeset_acquire_ctx ctx;
	int ret;

	drm_modeset_acquire_init(&ctx, 0);

retry:
	ret = drm_modeset_lock(&connector->dev->mode_config.connection_mutex, &ctx);
	if (!ret) {
		if (funcs->detect_ctx)
			ret = funcs->detect_ctx(connector, &ctx, force);
		else if (connector->funcs->detect)
			ret = connector->funcs->detect(connector, force);
		else
			ret = connector_status_connected;
	}

	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry;
	}

	if (WARN_ON(ret < 0))
		ret = connector_status_unknown;

	if (ret != connector->status)
		connector->epoch_counter += 1;

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;
}

/**
 * drm_helper_probe_detect - probe connector status
 * @connector: connector to probe
 * @ctx: acquire_ctx, or NULL to let this function handle locking.
 * @force: Whether destructive probe operations should be performed.
 *
 * This function calls the detect callbacks of the connector.
 * This function returns &drm_connector_status, or
 * if @ctx is set, it might also return -EDEADLK.
 */
int
drm_helper_probe_detect(struct drm_connector *connector,
			struct drm_modeset_acquire_ctx *ctx,
			bool force)
{
	const struct drm_connector_helper_funcs *funcs = connector->helper_private;
	struct drm_device *dev = connector->dev;
	int ret;

	if (!ctx)
		return drm_helper_probe_detect_ctx(connector, force);

	ret = drm_modeset_lock(&dev->mode_config.connection_mutex, ctx);
	if (ret)
		return ret;

	if (funcs->detect_ctx)
		ret = funcs->detect_ctx(connector, ctx, force);
	else if (connector->funcs->detect)
		ret = connector->funcs->detect(connector, force);
	else
		ret = connector_status_connected;

	if (ret != connector->status)
		connector->epoch_counter += 1;

	return ret;
}
EXPORT_SYMBOL(drm_helper_probe_detect);

static int drm_helper_probe_get_modes(struct drm_connector *connector)
{
	const struct drm_connector_helper_funcs *connector_funcs =
		connector->helper_private;
	int count;

	count = connector_funcs->get_modes(connector);

	/* The .get_modes() callback should not return negative values. */
	if (count < 0) {
		drm_err(connector->dev, ".get_modes() returned %pe\n",
			ERR_PTR(count));
		count = 0;
	}

	/*
	 * Fallback for when DDC probe failed in drm_get_edid() and thus skipped
	 * override/firmware EDID.
	 */
	if (count == 0 && connector->status == connector_status_connected)
		count = drm_add_override_edid_modes(connector);

	return count;
}

static int __drm_helper_update_and_validate(struct drm_connector *connector,
					    uint32_t maxX, uint32_t maxY,
					    struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;
	int mode_flags = 0;
	int ret;

	drm_connector_list_update(connector);

	if (connector->interlace_allowed)
		mode_flags |= DRM_MODE_FLAG_INTERLACE;
	if (connector->doublescan_allowed)
		mode_flags |= DRM_MODE_FLAG_DBLSCAN;
	if (connector->stereo_allowed)
		mode_flags |= DRM_MODE_FLAG_3D_MASK;

	list_for_each_entry(mode, &connector->modes, head) {
		if (mode->status != MODE_OK)
			continue;

		mode->status = drm_mode_validate_driver(dev, mode);
		if (mode->status != MODE_OK)
			continue;

		mode->status = drm_mode_validate_size(mode, maxX, maxY);
		if (mode->status != MODE_OK)
			continue;

		mode->status = drm_mode_validate_flag(mode, mode_flags);
		if (mode->status != MODE_OK)
			continue;

		ret = drm_mode_validate_pipeline(mode, connector, ctx,
						 &mode->status);
		if (ret) {
			drm_dbg_kms(dev,
				    "drm_mode_validate_pipeline failed: %d\n",
				    ret);

			if (drm_WARN_ON_ONCE(dev, ret != -EDEADLK))
				mode->status = MODE_ERROR;
			else
				return -EDEADLK;
		}

		if (mode->status != MODE_OK)
			continue;
		mode->status = drm_mode_validate_ycbcr420(mode, connector);
	}

	return 0;
}

/**
 * drm_helper_probe_single_connector_modes - get complete set of display modes
 * @connector: connector to probe
 * @maxX: max width for modes
 * @maxY: max height for modes
 *
 * Based on the helper callbacks implemented by @connector in struct
 * &drm_connector_helper_funcs try to detect all valid modes.  Modes will first
 * be added to the connector's probed_modes list, then culled (based on validity
 * and the @maxX, @maxY parameters) and put into the normal modes list.
 *
 * Intended to be used as a generic implementation of the
 * &drm_connector_funcs.fill_modes() vfunc for drivers that use the CRTC helpers
 * for output mode filtering and detection.
 *
 * The basic procedure is as follows
 *
 * 1. All modes currently on the connector's modes list are marked as stale
 *
 * 2. New modes are added to the connector's probed_modes list with
 *    drm_mode_probed_add(). New modes start their life with status as OK.
 *    Modes are added from a single source using the following priority order.
 *
 *    - &drm_connector_helper_funcs.get_modes vfunc
 *    - if the connector status is connector_status_connected, standard
 *      VESA DMT modes up to 1024x768 are automatically added
 *      (drm_add_modes_noedid())
 *
 *    Finally modes specified via the kernel command line (video=...) are
 *    added in addition to what the earlier probes produced
 *    (drm_helper_probe_add_cmdline_mode()). These modes are generated
 *    using the VESA GTF/CVT formulas.
 *
 * 3. Modes are moved from the probed_modes list to the modes list. Potential
 *    duplicates are merged together (see drm_connector_list_update()).
 *    After this step the probed_modes list will be empty again.
 *
 * 4. Any non-stale mode on the modes list then undergoes validation
 *
 *    - drm_mode_validate_basic() performs basic sanity checks
 *    - drm_mode_validate_size() filters out modes larger than @maxX and @maxY
 *      (if specified)
 *    - drm_mode_validate_flag() checks the modes against basic connector
 *      capabilities (interlace_allowed,doublescan_allowed,stereo_allowed)
 *    - the optional &drm_connector_helper_funcs.mode_valid or
 *      &drm_connector_helper_funcs.mode_valid_ctx helpers can perform driver
 *      and/or sink specific checks
 *    - the optional &drm_crtc_helper_funcs.mode_valid,
 *      &drm_bridge_funcs.mode_valid and &drm_encoder_helper_funcs.mode_valid
 *      helpers can perform driver and/or source specific checks which are also
 *      enforced by the modeset/atomic helpers
 *
 * 5. Any mode whose status is not OK is pruned from the connector's modes list,
 *    accompanied by a debug message indicating the reason for the mode's
 *    rejection (see drm_mode_prune_invalid()).
 *
 * Returns:
 * The number of modes found on @connector.
 */
int drm_helper_probe_single_connector_modes(struct drm_connector *connector,
					    uint32_t maxX, uint32_t maxY)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;
	int count = 0, ret;
	enum drm_connector_status old_status;
	struct drm_modeset_acquire_ctx ctx;

	WARN_ON(!mutex_is_locked(&dev->mode_config.mutex));

	drm_modeset_acquire_init(&ctx, 0);

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n", connector->base.id,
			connector->name);

retry:
	ret = drm_modeset_lock(&dev->mode_config.connection_mutex, &ctx);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry;
	} else
		WARN_ON(ret < 0);

	/* set all old modes to the stale state */
	list_for_each_entry(mode, &connector->modes, head)
		mode->status = MODE_STALE;

	old_status = connector->status;

	if (connector->force) {
		if (connector->force == DRM_FORCE_ON ||
		    connector->force == DRM_FORCE_ON_DIGITAL)
			connector->status = connector_status_connected;
		else
			connector->status = connector_status_disconnected;
		if (connector->funcs->force)
			connector->funcs->force(connector);
	} else {
		ret = drm_helper_probe_detect(connector, &ctx, true);

		if (ret == -EDEADLK) {
			drm_modeset_backoff(&ctx);
			goto retry;
		} else if (WARN(ret < 0, "Invalid return value %i for connector detection\n", ret))
			ret = connector_status_unknown;

		connector->status = ret;
	}

	/*
	 * Normally either the driver's hpd code or the poll loop should
	 * pick up any changes and fire the hotplug event. But if
	 * userspace sneaks in a probe, we might miss a change. Hence
	 * check here, and if anything changed start the hotplug code.
	 */
	if (old_status != connector->status) {
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] status updated from %s to %s\n",
			      connector->base.id,
			      connector->name,
			      drm_get_connector_status_name(old_status),
			      drm_get_connector_status_name(connector->status));

		/*
		 * The hotplug event code might call into the fb
		 * helpers, and so expects that we do not hold any
		 * locks. Fire up the poll struct instead, it will
		 * disable itself again.
		 */
		dev->mode_config.delayed_event = true;
		if (dev->mode_config.poll_enabled)
			mod_delayed_work(system_wq,
					 &dev->mode_config.output_poll_work,
					 0);
	}

	/* Re-enable polling in case the global poll config changed. */
	if (dev->mode_config.poll_enabled &&
	    (drm_kms_helper_poll != dev->mode_config.poll_running))
		drm_kms_helper_poll_enable(dev);

	dev->mode_config.poll_running = drm_kms_helper_poll;

	if (connector->status == connector_status_disconnected) {
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] disconnected\n",
			connector->base.id, connector->name);
		drm_connector_update_edid_property(connector, NULL);
		drm_mode_prune_invalid(dev, &connector->modes, false);
		goto exit;
	}

	count = drm_helper_probe_get_modes(connector);

	if (count == 0 && (connector->status == connector_status_connected ||
			   connector->status == connector_status_unknown)) {
		count = drm_add_modes_noedid(connector, 1024, 768);

		/*
		 * Section 4.2.2.6 (EDID Corruption Detection) of the DP 1.4a
		 * Link CTS specifies that 640x480 (the official "failsafe"
		 * mode) needs to be the default if there's no EDID.
		 */
		if (connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort)
			drm_set_preferred_mode(connector, 640, 480);
	}
	count += drm_helper_probe_add_cmdline_mode(connector);
	if (count != 0) {
		ret = __drm_helper_update_and_validate(connector, maxX, maxY, &ctx);
		if (ret == -EDEADLK) {
			drm_modeset_backoff(&ctx);
			goto retry;
		}
	}

	drm_mode_prune_invalid(dev, &connector->modes, true);

	/*
	 * Displayport spec section 5.2.1.2 ("Video Timing Format") says that
	 * all detachable sinks shall support 640x480 @60Hz as a fail safe
	 * mode. If all modes were pruned, perhaps because they need more
	 * lanes or a higher pixel clock than available, at least try to add
	 * in 640x480.
	 */
	if (list_empty(&connector->modes) &&
	    connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort) {
		count = drm_add_modes_noedid(connector, 640, 480);
		ret = __drm_helper_update_and_validate(connector, maxX, maxY, &ctx);
		if (ret == -EDEADLK) {
			drm_modeset_backoff(&ctx);
			goto retry;
		}
		drm_mode_prune_invalid(dev, &connector->modes, true);
	}

exit:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	if (list_empty(&connector->modes))
		return 0;

	drm_mode_sort(&connector->modes);

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s] probed modes :\n", connector->base.id,
			connector->name);
	list_for_each_entry(mode, &connector->modes, head) {
		drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V);
		drm_mode_debug_printmodeline(mode);
	}

	return count;
}
EXPORT_SYMBOL(drm_helper_probe_single_connector_modes);

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
 *
 * If only a single connector has changed, consider calling
 * drm_kms_helper_connector_hotplug_event() instead.
 */
void drm_kms_helper_hotplug_event(struct drm_device *dev)
{
	/* send a uevent + call fbdev */
	drm_sysfs_hotplug_event(dev);
	if (dev->mode_config.funcs->output_poll_changed)
		dev->mode_config.funcs->output_poll_changed(dev);

	drm_client_dev_hotplug(dev);
}
EXPORT_SYMBOL(drm_kms_helper_hotplug_event);

/**
 * drm_kms_helper_connector_hotplug_event - fire off a KMS connector hotplug event
 * @connector: drm_connector which has changed
 *
 * This is the same as drm_kms_helper_hotplug_event(), except it fires a more
 * fine-grained uevent for a single connector.
 */
void drm_kms_helper_connector_hotplug_event(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	/* send a uevent + call fbdev */
	drm_sysfs_connector_hotplug_event(connector);
	if (dev->mode_config.funcs->output_poll_changed)
		dev->mode_config.funcs->output_poll_changed(dev);

	drm_client_dev_hotplug(dev);
}
EXPORT_SYMBOL(drm_kms_helper_connector_hotplug_event);

static void output_poll_execute(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct drm_device *dev = container_of(delayed_work, struct drm_device, mode_config.output_poll_work);
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	enum drm_connector_status old_status;
	bool repoll = false, changed;
	u64 old_epoch_counter;

	if (!dev->mode_config.poll_enabled)
		return;

	/* Pick up any changes detected by the probe functions. */
	changed = dev->mode_config.delayed_event;
	dev->mode_config.delayed_event = false;

	if (!drm_kms_helper_poll)
		goto out;

	if (!mutex_trylock(&dev->mode_config.mutex)) {
		repoll = true;
		goto out;
	}

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		/* Ignore forced connectors. */
		if (connector->force)
			continue;

		/* Ignore HPD capable connectors and connectors where we don't
		 * want any hotplug detection at all for polling. */
		if (!connector->polled || connector->polled == DRM_CONNECTOR_POLL_HPD)
			continue;

		old_status = connector->status;
		/* if we are connected and don't want to poll for disconnect
		   skip it */
		if (old_status == connector_status_connected &&
		    !(connector->polled & DRM_CONNECTOR_POLL_DISCONNECT))
			continue;

		repoll = true;

		old_epoch_counter = connector->epoch_counter;
		connector->status = drm_helper_probe_detect(connector, NULL, false);
		if (old_epoch_counter != connector->epoch_counter) {
			const char *old, *new;

			/*
			 * The poll work sets force=false when calling detect so
			 * that drivers can avoid to do disruptive tests (e.g.
			 * when load detect cycles could cause flickering on
			 * other, running displays). This bears the risk that we
			 * flip-flop between unknown here in the poll work and
			 * the real state when userspace forces a full detect
			 * call after receiving a hotplug event due to this
			 * change.
			 *
			 * Hence clamp an unknown detect status to the old
			 * value.
			 */
			if (connector->status == connector_status_unknown) {
				connector->status = old_status;
				continue;
			}

			old = drm_get_connector_status_name(old_status);
			new = drm_get_connector_status_name(connector->status);

			DRM_DEBUG_KMS("[CONNECTOR:%d:%s] "
				      "status updated from %s to %s\n",
				      connector->base.id,
				      connector->name,
				      old, new);
			DRM_DEBUG_KMS("[CONNECTOR:%d:%s] epoch counter %llu -> %llu\n",
				      connector->base.id, connector->name,
				      old_epoch_counter, connector->epoch_counter);

			changed = true;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	mutex_unlock(&dev->mode_config.mutex);

out:
	if (changed)
		drm_kms_helper_hotplug_event(dev);

	if (repoll)
		schedule_delayed_work(delayed_work, DRM_OUTPUT_POLL_PERIOD);
}

/**
 * drm_kms_helper_is_poll_worker - is %current task an output poll worker?
 *
 * Determine if %current task is an output poll worker.  This can be used
 * to select distinct code paths for output polling versus other contexts.
 *
 * One use case is to avoid a deadlock between the output poll worker and
 * the autosuspend worker wherein the latter waits for polling to finish
 * upon calling drm_kms_helper_poll_disable(), while the former waits for
 * runtime suspend to finish upon calling pm_runtime_get_sync() in a
 * connector ->detect hook.
 */
bool drm_kms_helper_is_poll_worker(void)
{
	struct work_struct *work = current_work();

	return work && work->func == output_poll_execute;
}
EXPORT_SYMBOL(drm_kms_helper_is_poll_worker);

/**
 * drm_kms_helper_poll_disable - disable output polling
 * @dev: drm_device
 *
 * This function disables the output polling work.
 *
 * Drivers can call this helper from their device suspend implementation. It is
 * not an error to call this even when output polling isn't enabled or already
 * disabled. Polling is re-enabled by calling drm_kms_helper_poll_enable().
 *
 * If however, the polling was never initialized, this call will trigger a
 * warning and return
 *
 * Note that calls to enable and disable polling must be strictly ordered, which
 * is automatically the case when they're only call from suspend/resume
 * callbacks.
 */
void drm_kms_helper_poll_disable(struct drm_device *dev)
{
	if (drm_WARN_ON(dev, !dev->mode_config.poll_enabled))
		return;

	cancel_delayed_work_sync(&dev->mode_config.output_poll_work);
}
EXPORT_SYMBOL(drm_kms_helper_poll_disable);

/**
 * drm_kms_helper_poll_init - initialize and enable output polling
 * @dev: drm_device
 *
 * This function initializes and then also enables output polling support for
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
	if (!dev->mode_config.poll_enabled)
		return;

	dev->mode_config.poll_enabled = false;
	cancel_delayed_work_sync(&dev->mode_config.output_poll_work);
}
EXPORT_SYMBOL(drm_kms_helper_poll_fini);

static bool check_connector_changed(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	enum drm_connector_status old_status;
	u64 old_epoch_counter;

	/* Only handle HPD capable connectors. */
	drm_WARN_ON(dev, !(connector->polled & DRM_CONNECTOR_POLL_HPD));

	drm_WARN_ON(dev, !mutex_is_locked(&dev->mode_config.mutex));

	old_status = connector->status;
	old_epoch_counter = connector->epoch_counter;
	connector->status = drm_helper_probe_detect(connector, NULL, false);

	if (old_epoch_counter == connector->epoch_counter) {
		drm_dbg_kms(dev, "[CONNECTOR:%d:%s] Same epoch counter %llu\n",
			    connector->base.id,
			    connector->name,
			    connector->epoch_counter);

		return false;
	}

	drm_dbg_kms(dev, "[CONNECTOR:%d:%s] status updated from %s to %s\n",
		    connector->base.id,
		    connector->name,
		    drm_get_connector_status_name(old_status),
		    drm_get_connector_status_name(connector->status));

	drm_dbg_kms(dev, "[CONNECTOR:%d:%s] Changed epoch counter %llu => %llu\n",
		    connector->base.id,
		    connector->name,
		    old_epoch_counter,
		    connector->epoch_counter);

	return true;
}

/**
 * drm_connector_helper_hpd_irq_event - hotplug processing
 * @connector: drm_connector
 *
 * Drivers can use this helper function to run a detect cycle on a connector
 * which has the DRM_CONNECTOR_POLL_HPD flag set in its &polled member.
 *
 * This helper function is useful for drivers which can track hotplug
 * interrupts for a single connector. Drivers that want to send a
 * hotplug event for all connectors or can't track hotplug interrupts
 * per connector need to use drm_helper_hpd_irq_event().
 *
 * This function must be called from process context with no mode
 * setting locks held.
 *
 * Note that a connector can be both polled and probed from the hotplug
 * handler, in case the hotplug interrupt is known to be unreliable.
 *
 * Returns:
 * A boolean indicating whether the connector status changed or not
 */
bool drm_connector_helper_hpd_irq_event(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	bool changed;

	mutex_lock(&dev->mode_config.mutex);
	changed = check_connector_changed(connector);
	mutex_unlock(&dev->mode_config.mutex);

	if (changed) {
		drm_kms_helper_connector_hotplug_event(connector);
		drm_dbg_kms(dev, "[CONNECTOR:%d:%s] Sent hotplug event\n",
			    connector->base.id,
			    connector->name);
	}

	return changed;
}
EXPORT_SYMBOL(drm_connector_helper_hpd_irq_event);

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
 * which have a more fine-grained detect logic can use
 * drm_connector_helper_hpd_irq_event(). Alternatively, they should bypass this
 * code and directly call drm_kms_helper_hotplug_event() in case the connector
 * state changed.
 *
 * This function must be called from process context with no mode
 * setting locks held.
 *
 * Note that a connector can be both polled and probed from the hotplug handler,
 * in case the hotplug interrupt is known to be unreliable.
 *
 * Returns:
 * A boolean indicating whether the connector status changed or not
 */
bool drm_helper_hpd_irq_event(struct drm_device *dev)
{
	struct drm_connector *connector, *first_changed_connector = NULL;
	struct drm_connector_list_iter conn_iter;
	int changed = 0;

	if (!dev->mode_config.poll_enabled)
		return false;

	mutex_lock(&dev->mode_config.mutex);
	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		/* Only handle HPD capable connectors. */
		if (!(connector->polled & DRM_CONNECTOR_POLL_HPD))
			continue;

		if (check_connector_changed(connector)) {
			if (!first_changed_connector) {
				drm_connector_get(connector);
				first_changed_connector = connector;
			}

			changed++;
		}
	}
	drm_connector_list_iter_end(&conn_iter);
	mutex_unlock(&dev->mode_config.mutex);

	if (changed == 1)
		drm_kms_helper_connector_hotplug_event(first_changed_connector);
	else if (changed > 0)
		drm_kms_helper_hotplug_event(dev);

	if (first_changed_connector)
		drm_connector_put(first_changed_connector);

	return changed;
}
EXPORT_SYMBOL(drm_helper_hpd_irq_event);

/**
 * drm_crtc_helper_mode_valid_fixed - Validates a display mode
 * @crtc: the crtc
 * @mode: the mode to validate
 * @fixed_mode: the display hardware's mode
 *
 * Returns:
 * MODE_OK on success, or another mode-status code otherwise.
 */
enum drm_mode_status drm_crtc_helper_mode_valid_fixed(struct drm_crtc *crtc,
						      const struct drm_display_mode *mode,
						      const struct drm_display_mode *fixed_mode)
{
	if (mode->hdisplay != fixed_mode->hdisplay && mode->vdisplay != fixed_mode->vdisplay)
		return MODE_ONE_SIZE;
	else if (mode->hdisplay != fixed_mode->hdisplay)
		return MODE_ONE_WIDTH;
	else if (mode->vdisplay != fixed_mode->vdisplay)
		return MODE_ONE_HEIGHT;

	return MODE_OK;
}
EXPORT_SYMBOL(drm_crtc_helper_mode_valid_fixed);

/**
 * drm_connector_helper_get_modes_from_ddc - Updates the connector's EDID
 *                                           property from the connector's
 *                                           DDC channel
 * @connector: The connector
 *
 * Returns:
 * The number of detected display modes.
 *
 * Uses a connector's DDC channel to retrieve EDID data and update the
 * connector's EDID property and display modes. Drivers can use this
 * function to implement struct &drm_connector_helper_funcs.get_modes
 * for connectors with a DDC channel.
 */
int drm_connector_helper_get_modes_from_ddc(struct drm_connector *connector)
{
	struct edid *edid;
	int count = 0;

	if (!connector->ddc)
		return 0;

	edid = drm_get_edid(connector, connector->ddc);

	// clears property if EDID is NULL
	drm_connector_update_edid_property(connector, edid);

	if (edid) {
		count = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}

	return count;
}
EXPORT_SYMBOL(drm_connector_helper_get_modes_from_ddc);

/**
 * drm_connector_helper_get_modes_fixed - Duplicates a display mode for a connector
 * @connector: the connector
 * @fixed_mode: the display hardware's mode
 *
 * This function duplicates a display modes for a connector. Drivers for hardware
 * that only supports a single fixed mode can use this function in their connector's
 * get_modes helper.
 *
 * Returns:
 * The number of created modes.
 */
int drm_connector_helper_get_modes_fixed(struct drm_connector *connector,
					 const struct drm_display_mode *fixed_mode)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(dev, fixed_mode);
	if (!mode) {
		drm_err(dev, "Failed to duplicate mode " DRM_MODE_FMT "\n",
			DRM_MODE_ARG(fixed_mode));
		return 0;
	}

	if (mode->name[0] == '\0')
		drm_mode_set_name(mode);

	mode->type |= DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	if (mode->width_mm)
		connector->display_info.width_mm = mode->width_mm;
	if (mode->height_mm)
		connector->display_info.height_mm = mode->height_mm;

	return 1;
}
EXPORT_SYMBOL(drm_connector_helper_get_modes_fixed);

/**
 * drm_connector_helper_get_modes - Read EDID and update connector.
 * @connector: The connector
 *
 * Read the EDID using drm_edid_read() (which requires that connector->ddc is
 * set), and update the connector using the EDID.
 *
 * This can be used as the "default" connector helper .get_modes() hook if the
 * driver does not need any special processing. This is sets the example what
 * custom .get_modes() hooks should do regarding EDID read and connector update.
 *
 * Returns: Number of modes.
 */
int drm_connector_helper_get_modes(struct drm_connector *connector)
{
	const struct drm_edid *drm_edid;
	int count;

	drm_edid = drm_edid_read(connector);

	/*
	 * Unconditionally update the connector. If the EDID was read
	 * successfully, fill in the connector information derived from the
	 * EDID. Otherwise, if the EDID is NULL, clear the connector
	 * information.
	 */
	count = drm_edid_connector_update(connector, drm_edid);

	drm_edid_free(drm_edid);

	return count;
}
EXPORT_SYMBOL(drm_connector_helper_get_modes);
