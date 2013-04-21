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
 * drm_helper_move_panel_connectors_to_head() - move panels to the front in the
 * 						connector list
 * @dev: drm device to operate on
 *
 * Some userspace presumes that the first connected connector is the main
 * display, where it's supposed to display e.g. the login screen. For
 * laptops, this should be the main panel. Use this function to sort all
 * (eDP/LVDS) panels to the front of the connector list, instead of
 * painstakingly trying to initialize them in the right order.
 */
void drm_helper_move_panel_connectors_to_head(struct drm_device *dev)
{
	struct drm_connector *connector, *tmp;
	struct list_head panel_list;

	INIT_LIST_HEAD(&panel_list);

	list_for_each_entry_safe(connector, tmp,
				 &dev->mode_config.connector_list, head) {
		if (connector->connector_type == DRM_MODE_CONNECTOR_LVDS ||
		    connector->connector_type == DRM_MODE_CONNECTOR_eDP)
			list_move_tail(&connector->head, &panel_list);
	}

	list_splice(&panel_list, &dev->mode_config.connector_list);
}
EXPORT_SYMBOL(drm_helper_move_panel_connectors_to_head);

static bool drm_kms_helper_poll = true;
module_param_named(poll, drm_kms_helper_poll, bool, 0600);

static void drm_mode_validate_flag(struct drm_connector *connector,
				   int flags)
{
	struct drm_display_mode *mode;

	if (flags == (DRM_MODE_FLAG_DBLSCAN | DRM_MODE_FLAG_INTERLACE))
		return;

	list_for_each_entry(mode, &connector->modes, head) {
		if ((mode->flags & DRM_MODE_FLAG_INTERLACE) &&
				!(flags & DRM_MODE_FLAG_INTERLACE))
			mode->status = MODE_NO_INTERLACE;
		if ((mode->flags & DRM_MODE_FLAG_DBLSCAN) &&
				!(flags & DRM_MODE_FLAG_DBLSCAN))
			mode->status = MODE_NO_DBLESCAN;
	}

	return;
}

/**
 * drm_helper_probe_single_connector_modes - get complete set of display modes
 * @connector: connector to probe
 * @maxX: max width for modes
 * @maxY: max height for modes
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Based on the helper callbacks implemented by @connector try to detect all
 * valid modes.  Modes will first be added to the connector's probed_modes list,
 * then culled (based on validity and the @maxX, @maxY parameters) and put into
 * the normal modes list.
 *
 * Intended to be use as a generic implementation of the ->probe() @connector
 * callback for drivers that use the crtc helpers for output mode filtering and
 * detection.
 *
 * RETURNS:
 * Number of modes found on @connector.
 */
int drm_helper_probe_single_connector_modes(struct drm_connector *connector,
					    uint32_t maxX, uint32_t maxY)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;
	struct drm_connector_helper_funcs *connector_funcs =
		connector->helper_private;
	int count = 0;
	int mode_flags = 0;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n", connector->base.id,
			drm_get_connector_name(connector));
	/* set all modes to the unverified state */
	list_for_each_entry(mode, &connector->modes, head)
		mode->status = MODE_UNVERIFIED;

	if (connector->force) {
		if (connector->force == DRM_FORCE_ON)
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
			connector->base.id, drm_get_connector_name(connector));
		drm_mode_connector_update_edid_property(connector, NULL);
		goto prune;
	}

#ifdef CONFIG_DRM_LOAD_EDID_FIRMWARE
	count = drm_load_edid_firmware(connector);
	if (count == 0)
#endif
		count = (*connector_funcs->get_modes)(connector);

	if (count == 0 && connector->status == connector_status_connected)
		count = drm_add_modes_noedid(connector, 1024, 768);
	if (count == 0)
		goto prune;

	drm_mode_connector_list_update(connector);

	if (maxX && maxY)
		drm_mode_validate_size(dev, &connector->modes, maxX,
				       maxY, 0);

	if (connector->interlace_allowed)
		mode_flags |= DRM_MODE_FLAG_INTERLACE;
	if (connector->doublescan_allowed)
		mode_flags |= DRM_MODE_FLAG_DBLSCAN;
	drm_mode_validate_flag(connector, mode_flags);

	list_for_each_entry(mode, &connector->modes, head) {
		if (mode->status == MODE_OK)
			mode->status = connector_funcs->mode_valid(connector,
								   mode);
	}

prune:
	drm_mode_prune_invalid(dev, &connector->modes, true);

	if (list_empty(&connector->modes))
		return 0;

	drm_mode_sort(&connector->modes);

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s] probed modes :\n", connector->base.id,
			drm_get_connector_name(connector));
	list_for_each_entry(mode, &connector->modes, head) {
		mode->vrefresh = drm_mode_vrefresh(mode);

		drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V);
		drm_mode_debug_printmodeline(mode);
	}

	return count;
}
EXPORT_SYMBOL(drm_helper_probe_single_connector_modes);

/**
 * drm_helper_encoder_in_use - check if a given encoder is in use
 * @encoder: encoder to check
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Walk @encoders's DRM device's mode_config and see if it's in use.
 *
 * RETURNS:
 * True if @encoder is part of the mode_config, false otherwise.
 */
bool drm_helper_encoder_in_use(struct drm_encoder *encoder)
{
	struct drm_connector *connector;
	struct drm_device *dev = encoder->dev;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		if (connector->encoder == encoder)
			return true;
	return false;
}
EXPORT_SYMBOL(drm_helper_encoder_in_use);

/**
 * drm_helper_crtc_in_use - check if a given CRTC is in a mode_config
 * @crtc: CRTC to check
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Walk @crtc's DRM device's mode_config and see if it's in use.
 *
 * RETURNS:
 * True if @crtc is part of the mode_config, false otherwise.
 */
bool drm_helper_crtc_in_use(struct drm_crtc *crtc)
{
	struct drm_encoder *encoder;
	struct drm_device *dev = crtc->dev;
	/* FIXME: Locking around list access? */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head)
		if (encoder->crtc == crtc && drm_helper_encoder_in_use(encoder))
			return true;
	return false;
}
EXPORT_SYMBOL(drm_helper_crtc_in_use);

static void
drm_encoder_disable(struct drm_encoder *encoder)
{
	struct drm_encoder_helper_funcs *encoder_funcs = encoder->helper_private;

	if (encoder_funcs->disable)
		(*encoder_funcs->disable)(encoder);
	else
		(*encoder_funcs->dpms)(encoder, DRM_MODE_DPMS_OFF);
}

/**
 * drm_helper_disable_unused_functions - disable unused objects
 * @dev: DRM device
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * If an connector or CRTC isn't part of @dev's mode_config, it can be disabled
 * by calling its dpms function, which should power it off.
 */
void drm_helper_disable_unused_functions(struct drm_device *dev)
{
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct drm_crtc *crtc;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (!connector->encoder)
			continue;
		if (connector->status == connector_status_disconnected)
			connector->encoder = NULL;
	}

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (!drm_helper_encoder_in_use(encoder)) {
			drm_encoder_disable(encoder);
			/* disconnector encoder from any connector */
			encoder->crtc = NULL;
		}
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
		crtc->enabled = drm_helper_crtc_in_use(crtc);
		if (!crtc->enabled) {
			if (crtc_funcs->disable)
				(*crtc_funcs->disable)(crtc);
			else
				(*crtc_funcs->dpms)(crtc, DRM_MODE_DPMS_OFF);
			crtc->fb = NULL;
		}
	}
}
EXPORT_SYMBOL(drm_helper_disable_unused_functions);

/**
 * drm_encoder_crtc_ok - can a given crtc drive a given encoder?
 * @encoder: encoder to test
 * @crtc: crtc to test
 *
 * Return false if @encoder can't be driven by @crtc, true otherwise.
 */
static bool drm_encoder_crtc_ok(struct drm_encoder *encoder,
				struct drm_crtc *crtc)
{
	struct drm_device *dev;
	struct drm_crtc *tmp;
	int crtc_mask = 1;

	WARN(!crtc, "checking null crtc?\n");

	dev = crtc->dev;

	list_for_each_entry(tmp, &dev->mode_config.crtc_list, head) {
		if (tmp == crtc)
			break;
		crtc_mask <<= 1;
	}

	if (encoder->possible_crtcs & crtc_mask)
		return true;
	return false;
}

/*
 * Check the CRTC we're going to map each output to vs. its current
 * CRTC.  If they don't match, we have to disable the output and the CRTC
 * since the driver will have to re-route things.
 */
static void
drm_crtc_prepare_encoders(struct drm_device *dev)
{
	struct drm_encoder_helper_funcs *encoder_funcs;
	struct drm_encoder *encoder;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		encoder_funcs = encoder->helper_private;
		/* Disable unused encoders */
		if (encoder->crtc == NULL)
			drm_encoder_disable(encoder);
		/* Disable encoders whose CRTC is about to change */
		if (encoder_funcs->get_crtc &&
		    encoder->crtc != (*encoder_funcs->get_crtc)(encoder))
			drm_encoder_disable(encoder);
	}
}

/**
 * drm_crtc_helper_set_mode - internal helper to set a mode
 * @crtc: CRTC to program
 * @mode: mode to use
 * @x: horizontal offset into the surface
 * @y: vertical offset into the surface
 * @old_fb: old framebuffer, for cleanup
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Try to set @mode on @crtc.  Give @crtc and its associated connectors a chance
 * to fixup or reject the mode prior to trying to set it. This is an internal
 * helper that drivers could e.g. use to update properties that require the
 * entire output pipe to be disabled and re-enabled in a new configuration. For
 * example for changing whether audio is enabled on a hdmi link or for changing
 * panel fitter or dither attributes. It is also called by the
 * drm_crtc_helper_set_config() helper function to drive the mode setting
 * sequence.
 *
 * RETURNS:
 * True if the mode was set successfully, or false otherwise.
 */
bool drm_crtc_helper_set_mode(struct drm_crtc *crtc,
			      struct drm_display_mode *mode,
			      int x, int y,
			      struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct drm_display_mode *adjusted_mode, saved_mode, saved_hwmode;
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	struct drm_encoder_helper_funcs *encoder_funcs;
	int saved_x, saved_y;
	struct drm_encoder *encoder;
	bool ret = true;

	crtc->enabled = drm_helper_crtc_in_use(crtc);
	if (!crtc->enabled)
		return true;

	adjusted_mode = drm_mode_duplicate(dev, mode);
	if (!adjusted_mode)
		return false;

	saved_hwmode = crtc->hwmode;
	saved_mode = crtc->mode;
	saved_x = crtc->x;
	saved_y = crtc->y;

	/* Update crtc values up front so the driver can rely on them for mode
	 * setting.
	 */
	crtc->mode = *mode;
	crtc->x = x;
	crtc->y = y;

	/* Pass our mode to the connectors and the CRTC to give them a chance to
	 * adjust it according to limitations or connector properties, and also
	 * a chance to reject the mode entirely.
	 */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {

		if (encoder->crtc != crtc)
			continue;
		encoder_funcs = encoder->helper_private;
		if (!(ret = encoder_funcs->mode_fixup(encoder, mode,
						      adjusted_mode))) {
			DRM_DEBUG_KMS("Encoder fixup failed\n");
			goto done;
		}
	}

	if (!(ret = crtc_funcs->mode_fixup(crtc, mode, adjusted_mode))) {
		DRM_DEBUG_KMS("CRTC fixup failed\n");
		goto done;
	}
	DRM_DEBUG_KMS("[CRTC:%d]\n", crtc->base.id);

	/* Prepare the encoders and CRTCs before setting the mode. */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {

		if (encoder->crtc != crtc)
			continue;
		encoder_funcs = encoder->helper_private;
		/* Disable the encoders as the first thing we do. */
		encoder_funcs->prepare(encoder);
	}

	drm_crtc_prepare_encoders(dev);

	crtc_funcs->prepare(crtc);

	/* Set up the DPLL and any encoders state that needs to adjust or depend
	 * on the DPLL.
	 */
	ret = !crtc_funcs->mode_set(crtc, mode, adjusted_mode, x, y, old_fb);
	if (!ret)
	    goto done;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {

		if (encoder->crtc != crtc)
			continue;

		DRM_DEBUG_KMS("[ENCODER:%d:%s] set [MODE:%d:%s]\n",
			encoder->base.id, drm_get_encoder_name(encoder),
			mode->base.id, mode->name);
		encoder_funcs = encoder->helper_private;
		encoder_funcs->mode_set(encoder, mode, adjusted_mode);
	}

	/* Now enable the clocks, plane, pipe, and connectors that we set up. */
	crtc_funcs->commit(crtc);

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {

		if (encoder->crtc != crtc)
			continue;

		encoder_funcs = encoder->helper_private;
		encoder_funcs->commit(encoder);

	}

	/* Store real post-adjustment hardware mode. */
	crtc->hwmode = *adjusted_mode;

	/* Calculate and store various constants which
	 * are later needed by vblank and swap-completion
	 * timestamping. They are derived from true hwmode.
	 */
	drm_calc_timestamping_constants(crtc);

	/* FIXME: add subpixel order */
done:
	drm_mode_destroy(dev, adjusted_mode);
	if (!ret) {
		crtc->hwmode = saved_hwmode;
		crtc->mode = saved_mode;
		crtc->x = saved_x;
		crtc->y = saved_y;
	}

	return ret;
}
EXPORT_SYMBOL(drm_crtc_helper_set_mode);


static int
drm_crtc_helper_disable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_connector *connector;
	struct drm_encoder *encoder;

	/* Decouple all encoders and their attached connectors from this crtc */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->crtc != crtc)
			continue;

		list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
			if (connector->encoder != encoder)
				continue;

			connector->encoder = NULL;
		}
	}

	drm_helper_disable_unused_functions(dev);
	return 0;
}

/**
 * drm_crtc_helper_set_config - set a new config from userspace
 * @set: mode set configuration
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Setup a new configuration, provided by the upper layers (either an ioctl call
 * from userspace or internally e.g. from the fbdev suppport code) in @set, and
 * enable it. This is the main helper functions for drivers that implement
 * kernel mode setting with the crtc helper functions and the assorted
 * ->prepare(), ->modeset() and ->commit() helper callbacks.
 *
 * RETURNS:
 * Returns 0 on success, -ERRNO on failure.
 */
int drm_crtc_helper_set_config(struct drm_mode_set *set)
{
	struct drm_device *dev;
	struct drm_crtc *save_crtcs, *new_crtc, *crtc;
	struct drm_encoder *save_encoders, *new_encoder, *encoder;
	struct drm_framebuffer *old_fb = NULL;
	bool mode_changed = false; /* if true do a full mode set */
	bool fb_changed = false; /* if true and !mode_changed just do a flip */
	struct drm_connector *save_connectors, *connector;
	int count = 0, ro, fail = 0;
	struct drm_crtc_helper_funcs *crtc_funcs;
	struct drm_mode_set save_set;
	int ret;
	int i;

	DRM_DEBUG_KMS("\n");

	if (!set)
		return -EINVAL;

	if (!set->crtc)
		return -EINVAL;

	if (!set->crtc->helper_private)
		return -EINVAL;

	crtc_funcs = set->crtc->helper_private;

	if (!set->mode)
		set->fb = NULL;

	if (set->fb) {
		DRM_DEBUG_KMS("[CRTC:%d] [FB:%d] #connectors=%d (x y) (%i %i)\n",
				set->crtc->base.id, set->fb->base.id,
				(int)set->num_connectors, set->x, set->y);
	} else {
		DRM_DEBUG_KMS("[CRTC:%d] [NOFB]\n", set->crtc->base.id);
		return drm_crtc_helper_disable(set->crtc);
	}

	dev = set->crtc->dev;

	/* Allocate space for the backup of all (non-pointer) crtc, encoder and
	 * connector data. */
	save_crtcs = kzalloc(dev->mode_config.num_crtc *
			     sizeof(struct drm_crtc), GFP_KERNEL);
	if (!save_crtcs)
		return -ENOMEM;

	save_encoders = kzalloc(dev->mode_config.num_encoder *
				sizeof(struct drm_encoder), GFP_KERNEL);
	if (!save_encoders) {
		kfree(save_crtcs);
		return -ENOMEM;
	}

	save_connectors = kzalloc(dev->mode_config.num_connector *
				sizeof(struct drm_connector), GFP_KERNEL);
	if (!save_connectors) {
		kfree(save_crtcs);
		kfree(save_encoders);
		return -ENOMEM;
	}

	/* Copy data. Note that driver private data is not affected.
	 * Should anything bad happen only the expected state is
	 * restored, not the drivers personal bookkeeping.
	 */
	count = 0;
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		save_crtcs[count++] = *crtc;
	}

	count = 0;
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		save_encoders[count++] = *encoder;
	}

	count = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		save_connectors[count++] = *connector;
	}

	save_set.crtc = set->crtc;
	save_set.mode = &set->crtc->mode;
	save_set.x = set->crtc->x;
	save_set.y = set->crtc->y;
	save_set.fb = set->crtc->fb;

	/* We should be able to check here if the fb has the same properties
	 * and then just flip_or_move it */
	if (set->crtc->fb != set->fb) {
		/* If we have no fb then treat it as a full mode set */
		if (set->crtc->fb == NULL) {
			DRM_DEBUG_KMS("crtc has no fb, full mode set\n");
			mode_changed = true;
		} else if (set->fb == NULL) {
			mode_changed = true;
		} else if (set->fb->depth != set->crtc->fb->depth) {
			mode_changed = true;
		} else if (set->fb->bits_per_pixel !=
			   set->crtc->fb->bits_per_pixel) {
			mode_changed = true;
		} else if (set->fb->pixel_format !=
			   set->crtc->fb->pixel_format) {
			mode_changed = true;
		} else
			fb_changed = true;
	}

	if (set->x != set->crtc->x || set->y != set->crtc->y)
		fb_changed = true;

	if (set->mode && !drm_mode_equal(set->mode, &set->crtc->mode)) {
		DRM_DEBUG_KMS("modes are different, full mode set\n");
		drm_mode_debug_printmodeline(&set->crtc->mode);
		drm_mode_debug_printmodeline(set->mode);
		mode_changed = true;
	}

	/* a) traverse passed in connector list and get encoders for them */
	count = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct drm_connector_helper_funcs *connector_funcs =
			connector->helper_private;
		new_encoder = connector->encoder;
		for (ro = 0; ro < set->num_connectors; ro++) {
			if (set->connectors[ro] == connector) {
				new_encoder = connector_funcs->best_encoder(connector);
				/* if we can't get an encoder for a connector
				   we are setting now - then fail */
				if (new_encoder == NULL)
					/* don't break so fail path works correct */
					fail = 1;
				break;
			}
		}

		if (new_encoder != connector->encoder) {
			DRM_DEBUG_KMS("encoder changed, full mode switch\n");
			mode_changed = true;
			/* If the encoder is reused for another connector, then
			 * the appropriate crtc will be set later.
			 */
			if (connector->encoder)
				connector->encoder->crtc = NULL;
			connector->encoder = new_encoder;
		}
	}

	if (fail) {
		ret = -EINVAL;
		goto fail;
	}

	count = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (!connector->encoder)
			continue;

		if (connector->encoder->crtc == set->crtc)
			new_crtc = NULL;
		else
			new_crtc = connector->encoder->crtc;

		for (ro = 0; ro < set->num_connectors; ro++) {
			if (set->connectors[ro] == connector)
				new_crtc = set->crtc;
		}

		/* Make sure the new CRTC will work with the encoder */
		if (new_crtc &&
		    !drm_encoder_crtc_ok(connector->encoder, new_crtc)) {
			ret = -EINVAL;
			goto fail;
		}
		if (new_crtc != connector->encoder->crtc) {
			DRM_DEBUG_KMS("crtc changed, full mode switch\n");
			mode_changed = true;
			connector->encoder->crtc = new_crtc;
		}
		if (new_crtc) {
			DRM_DEBUG_KMS("[CONNECTOR:%d:%s] to [CRTC:%d]\n",
				connector->base.id, drm_get_connector_name(connector),
				new_crtc->base.id);
		} else {
			DRM_DEBUG_KMS("[CONNECTOR:%d:%s] to [NOCRTC]\n",
				connector->base.id, drm_get_connector_name(connector));
		}
	}

	/* mode_set_base is not a required function */
	if (fb_changed && !crtc_funcs->mode_set_base)
		mode_changed = true;

	if (mode_changed) {
		set->crtc->enabled = drm_helper_crtc_in_use(set->crtc);
		if (set->crtc->enabled) {
			DRM_DEBUG_KMS("attempting to set mode from"
					" userspace\n");
			drm_mode_debug_printmodeline(set->mode);
			old_fb = set->crtc->fb;
			set->crtc->fb = set->fb;
			if (!drm_crtc_helper_set_mode(set->crtc, set->mode,
						      set->x, set->y,
						      old_fb)) {
				DRM_ERROR("failed to set mode on [CRTC:%d]\n",
					  set->crtc->base.id);
				set->crtc->fb = old_fb;
				ret = -EINVAL;
				goto fail;
			}
			DRM_DEBUG_KMS("Setting connector DPMS state to on\n");
			for (i = 0; i < set->num_connectors; i++) {
				DRM_DEBUG_KMS("\t[CONNECTOR:%d:%s] set DPMS on\n", set->connectors[i]->base.id,
					      drm_get_connector_name(set->connectors[i]));
				set->connectors[i]->funcs->dpms(set->connectors[i], DRM_MODE_DPMS_ON);
			}
		}
		drm_helper_disable_unused_functions(dev);
	} else if (fb_changed) {
		set->crtc->x = set->x;
		set->crtc->y = set->y;

		old_fb = set->crtc->fb;
		if (set->crtc->fb != set->fb)
			set->crtc->fb = set->fb;
		ret = crtc_funcs->mode_set_base(set->crtc,
						set->x, set->y, old_fb);
		if (ret != 0) {
			set->crtc->fb = old_fb;
			goto fail;
		}
	}

	kfree(save_connectors);
	kfree(save_encoders);
	kfree(save_crtcs);
	return 0;

fail:
	/* Restore all previous data. */
	count = 0;
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		*crtc = save_crtcs[count++];
	}

	count = 0;
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		*encoder = save_encoders[count++];
	}

	count = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		*connector = save_connectors[count++];
	}

	/* Try to restore the config */
	if (mode_changed &&
	    !drm_crtc_helper_set_mode(save_set.crtc, save_set.mode, save_set.x,
				      save_set.y, save_set.fb))
		DRM_ERROR("failed to restore config after modeset failure\n");

	kfree(save_connectors);
	kfree(save_encoders);
	kfree(save_crtcs);
	return ret;
}
EXPORT_SYMBOL(drm_crtc_helper_set_config);

static int drm_helper_choose_encoder_dpms(struct drm_encoder *encoder)
{
	int dpms = DRM_MODE_DPMS_OFF;
	struct drm_connector *connector;
	struct drm_device *dev = encoder->dev;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		if (connector->encoder == encoder)
			if (connector->dpms < dpms)
				dpms = connector->dpms;
	return dpms;
}

static int drm_helper_choose_crtc_dpms(struct drm_crtc *crtc)
{
	int dpms = DRM_MODE_DPMS_OFF;
	struct drm_connector *connector;
	struct drm_device *dev = crtc->dev;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		if (connector->encoder && connector->encoder->crtc == crtc)
			if (connector->dpms < dpms)
				dpms = connector->dpms;
	return dpms;
}

/**
 * drm_helper_connector_dpms() - connector dpms helper implementation
 * @connector: affected connector
 * @mode: DPMS mode
 *
 * This is the main helper function provided by the crtc helper framework for
 * implementing the DPMS connector attribute. It computes the new desired DPMS
 * state for all encoders and crtcs in the output mesh and calls the ->dpms()
 * callback provided by the driver appropriately.
 */
void drm_helper_connector_dpms(struct drm_connector *connector, int mode)
{
	struct drm_encoder *encoder = connector->encoder;
	struct drm_crtc *crtc = encoder ? encoder->crtc : NULL;
	int old_dpms;

	if (mode == connector->dpms)
		return;

	old_dpms = connector->dpms;
	connector->dpms = mode;

	/* from off to on, do crtc then encoder */
	if (mode < old_dpms) {
		if (crtc) {
			struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
			if (crtc_funcs->dpms)
				(*crtc_funcs->dpms) (crtc,
						     drm_helper_choose_crtc_dpms(crtc));
		}
		if (encoder) {
			struct drm_encoder_helper_funcs *encoder_funcs = encoder->helper_private;
			if (encoder_funcs->dpms)
				(*encoder_funcs->dpms) (encoder,
							drm_helper_choose_encoder_dpms(encoder));
		}
	}

	/* from on to off, do encoder then crtc */
	if (mode > old_dpms) {
		if (encoder) {
			struct drm_encoder_helper_funcs *encoder_funcs = encoder->helper_private;
			if (encoder_funcs->dpms)
				(*encoder_funcs->dpms) (encoder,
							drm_helper_choose_encoder_dpms(encoder));
		}
		if (crtc) {
			struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
			if (crtc_funcs->dpms)
				(*crtc_funcs->dpms) (crtc,
						     drm_helper_choose_crtc_dpms(crtc));
		}
	}

	return;
}
EXPORT_SYMBOL(drm_helper_connector_dpms);

int drm_helper_mode_fill_fb_struct(struct drm_framebuffer *fb,
				   struct drm_mode_fb_cmd2 *mode_cmd)
{
	int i;

	fb->width = mode_cmd->width;
	fb->height = mode_cmd->height;
	for (i = 0; i < 4; i++) {
		fb->pitches[i] = mode_cmd->pitches[i];
		fb->offsets[i] = mode_cmd->offsets[i];
	}
	drm_fb_get_bpp_depth(mode_cmd->pixel_format, &fb->depth,
				    &fb->bits_per_pixel);
	fb->pixel_format = mode_cmd->pixel_format;

	return 0;
}
EXPORT_SYMBOL(drm_helper_mode_fill_fb_struct);

int drm_helper_resume_force_mode(struct drm_device *dev)
{
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	struct drm_encoder_helper_funcs *encoder_funcs;
	struct drm_crtc_helper_funcs *crtc_funcs;
	int ret;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {

		if (!crtc->enabled)
			continue;

		ret = drm_crtc_helper_set_mode(crtc, &crtc->mode,
					       crtc->x, crtc->y, crtc->fb);

		if (ret == false)
			DRM_ERROR("failed to set mode on crtc %p\n", crtc);

		/* Turn off outputs that were already powered off */
		if (drm_helper_choose_crtc_dpms(crtc)) {
			list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {

				if(encoder->crtc != crtc)
					continue;

				encoder_funcs = encoder->helper_private;
				if (encoder_funcs->dpms)
					(*encoder_funcs->dpms) (encoder,
								drm_helper_choose_encoder_dpms(encoder));
			}

			crtc_funcs = crtc->helper_private;
			if (crtc_funcs->dpms)
				(*crtc_funcs->dpms) (crtc,
						     drm_helper_choose_crtc_dpms(crtc));
		}
	}
	/* disable the unused connectors while restoring the modesetting */
	drm_helper_disable_unused_functions(dev);
	return 0;
}
EXPORT_SYMBOL(drm_helper_resume_force_mode);

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
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] status updated from %d to %d\n",
			      connector->base.id,
			      drm_get_connector_name(connector),
			      old_status, connector->status);
		if (old_status != connector->status)
			changed = true;
	}

	mutex_unlock(&dev->mode_config.mutex);

	if (changed)
		drm_kms_helper_hotplug_event(dev);

	if (repoll)
		schedule_delayed_work(delayed_work, DRM_OUTPUT_POLL_PERIOD);
}

void drm_kms_helper_poll_disable(struct drm_device *dev)
{
	if (!dev->mode_config.poll_enabled)
		return;
	cancel_delayed_work_sync(&dev->mode_config.output_poll_work);
}
EXPORT_SYMBOL(drm_kms_helper_poll_disable);

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

void drm_kms_helper_poll_init(struct drm_device *dev)
{
	INIT_DELAYED_WORK(&dev->mode_config.output_poll_work, output_poll_execute);
	dev->mode_config.poll_enabled = true;

	drm_kms_helper_poll_enable(dev);
}
EXPORT_SYMBOL(drm_kms_helper_poll_init);

void drm_kms_helper_poll_fini(struct drm_device *dev)
{
	drm_kms_helper_poll_disable(dev);
}
EXPORT_SYMBOL(drm_kms_helper_poll_fini);

void drm_helper_hpd_irq_event(struct drm_device *dev)
{
	struct drm_connector *connector;
	enum drm_connector_status old_status;
	bool changed = false;

	if (!dev->mode_config.poll_enabled)
		return;

	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {

		/* Only handle HPD capable connectors. */
		if (!(connector->polled & DRM_CONNECTOR_POLL_HPD))
			continue;

		old_status = connector->status;

		connector->status = connector->funcs->detect(connector, false);
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] status updated from %d to %d\n",
			      connector->base.id,
			      drm_get_connector_name(connector),
			      old_status, connector->status);
		if (old_status != connector->status)
			changed = true;
	}

	mutex_unlock(&dev->mode_config.mutex);

	if (changed)
		drm_kms_helper_hotplug_event(dev);
}
EXPORT_SYMBOL(drm_helper_hpd_irq_event);
