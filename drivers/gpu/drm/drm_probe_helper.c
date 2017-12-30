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
#include <drm/drm_modeset_helper_vtables.h>

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

static enum drm_mode_status
drm_mode_validate_pipeline(struct drm_display_mode *mode,
			    struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	uint32_t *ids = connector->encoder_ids;
	enum drm_mode_status ret = MODE_OK;
	unsigned int i;

	/* Step 1: Validate against connector */
	ret = drm_connector_mode_valid(connector, mode);
	if (ret != MODE_OK)
		return ret;

	/* Step 2: Validate against encoders and crtcs */
	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		struct drm_encoder *encoder = drm_encoder_find(dev, NULL, ids[i]);
		struct drm_crtc *crtc;

		if (!encoder)
			continue;

		ret = drm_encoder_mode_valid(encoder, mode);
		if (ret != MODE_OK) {
			/* No point in continuing for crtc check as this encoder
			 * will not accept the mode anyway. If all encoders
			 * reject the mode then, at exit, ret will not be
			 * MODE_OK. */
			continue;
		}

		ret = drm_bridge_mode_valid(encoder->bridge, mode);
		if (ret != MODE_OK) {
			/* There is also no point in continuing for crtc check
			 * here. */
			continue;
		}

		drm_for_each_crtc(crtc, dev) {
			if (!drm_encoder_crtc_ok(encoder, crtc))
				continue;

			ret = drm_crtc_mode_valid(crtc, mode);
			if (ret == MODE_OK) {
				/* If we get to this point there is at least
				 * one combination of encoder+crtc that works
				 * for this mode. Lets return now. */
				return ret;
			}
		}
	}

	return ret;
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

enum drm_mode_status drm_connector_mode_valid(struct drm_connector *connector,
					      struct drm_display_mode *mode)
{
	const struct drm_connector_helper_funcs *connector_funcs =
		connector->helper_private;

	if (!connector_funcs || !connector_funcs->mode_valid)
		return MODE_OK;

	return connector_funcs->mode_valid(connector, mode);
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
 * an error to call this when the output polling support has not yet been set
 * up.
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

	if (!dev->mode_config.poll_enabled || !drm_kms_helper_poll)
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
		return funcs->detect_ctx(connector, ctx, force);
	else if (connector->funcs->detect)
		return connector->funcs->detect(connector, force);
	else
		return connector_status_connected;
}
EXPORT_SYMBOL(drm_helper_probe_detect);

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
 *    duplicates are merged together (see drm_mode_connector_list_update()).
 *    After this step the probed_modes list will be empty again.
 *
 * 4. Any non-stale mode on the modes list then undergoes validation
 *
 *    - drm_mode_validate_basic() performs basic sanity checks
 *    - drm_mode_validate_size() filters out modes larger than @maxX and @maxY
 *      (if specified)
 *    - drm_mode_validate_flag() checks the modes against basic connector
 *      capabilities (interlace_allowed,doublescan_allowed,stereo_allowed)
 *    - the optional &drm_connector_helper_funcs.mode_valid helper can perform
 *      driver and/or sink specific checks
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
	const struct drm_connector_helper_funcs *connector_funcs =
		connector->helper_private;
	int count = 0, ret;
	int mode_flags = 0;
	bool verbose_prune = true;
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
			schedule_delayed_work(&dev->mode_config.output_poll_work,
					      0);
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

	count = (*connector_funcs->get_modes)(connector);

	if (count == 0 && connector->status == connector_status_connected)
		count = drm_add_modes_noedid(connector, 1024, 768);
	count += drm_helper_probe_add_cmdline_mode(connector);
	if (count == 0)
		goto prune;

	drm_mode_connector_list_update(connector);

	if (connector->interlace_allowed)
		mode_flags |= DRM_MODE_FLAG_INTERLACE;
	if (connector->doublescan_allowed)
		mode_flags |= DRM_MODE_FLAG_DBLSCAN;
	if (connector->stereo_allowed)
		mode_flags |= DRM_MODE_FLAG_3D_MASK;

	list_for_each_entry(mode, &connector->modes, head) {
		if (mode->status == MODE_OK)
			mode->status = drm_mode_validate_basic(mode);

		if (mode->status == MODE_OK)
			mode->status = drm_mode_validate_size(mode, maxX, maxY);

		if (mode->status == MODE_OK)
			mode->status = drm_mode_validate_flag(mode, mode_flags);

		if (mode->status == MODE_OK)
			mode->status = drm_mode_validate_pipeline(mode,
								  connector);

		if (mode->status == MODE_OK)
			mode->status = drm_mode_validate_ycbcr420(mode,
								  connector);
	}

prune:
	drm_mode_prune_invalid(dev, &connector->modes, verbose_prune);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

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
 */
void drm_kms_helper_hotplug_event(struct drm_device *dev)
{
	/* send a uevent + call fbdev */
	drm_sysfs_hotplug_event(dev);
	if (dev->mode_config.funcs->output_poll_changed)
		dev->mode_config.funcs->output_poll_changed(dev);
}
EXPORT_SYMBOL(drm_kms_helper_hotplug_event);

static void output_poll_execute(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct drm_device *dev = container_of(delayed_work, struct drm_device, mode_config.output_poll_work);
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	enum drm_connector_status old_status;
	bool repoll = false, changed;

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

		connector->status = drm_helper_probe_detect(connector, NULL, false);
		if (old_status != connector->status) {
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
 * drm_kms_helper_poll_disable - disable output polling
 * @dev: drm_device
 *
 * This function disables the output polling work.
 *
 * Drivers can call this helper from their device suspend implementation. It is
 * not an error to call this even when output polling isn't enabled or already
 * disabled. Polling is re-enabled by calling drm_kms_helper_poll_enable().
 *
 * Note that calls to enable and disable polling must be strictly ordered, which
 * is automatically the case when they're only call from suspend/resume
 * callbacks.
 */
void drm_kms_helper_poll_disable(struct drm_device *dev)
{
	if (!dev->mode_config.poll_enabled)
		return;
	cancel_delayed_work_sync(&dev->mode_config.output_poll_work);
}
EXPORT_SYMBOL(drm_kms_helper_poll_disable);

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
	struct drm_connector_list_iter conn_iter;
	enum drm_connector_status old_status;
	bool changed = false;

	if (!dev->mode_config.poll_enabled)
		return false;

	mutex_lock(&dev->mode_config.mutex);
	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		/* Only handle HPD capable connectors. */
		if (!(connector->polled & DRM_CONNECTOR_POLL_HPD))
			continue;

		old_status = connector->status;

		connector->status = drm_helper_probe_detect(connector, NULL, false);
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] status updated from %s to %s\n",
			      connector->base.id,
			      connector->name,
			      drm_get_connector_status_name(old_status),
			      drm_get_connector_status_name(connector->status));
		if (old_status != connector->status)
			changed = true;
	}
	drm_connector_list_iter_end(&conn_iter);
	mutex_unlock(&dev->mode_config.mutex);

	if (changed)
		drm_kms_helper_hotplug_event(dev);

	return changed;
}
EXPORT_SYMBOL(drm_helper_hpd_irq_event);
