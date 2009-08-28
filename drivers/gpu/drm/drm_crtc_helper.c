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

#include "drmP.h"
#include "drm_crtc.h"
#include "drm_crtc_helper.h"

/*
 * Detailed mode info for 800x600@60Hz
 */
static struct drm_display_mode std_modes[] = {
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DEFAULT, 40000, 800, 840,
		   968, 1056, 0, 600, 601, 605, 628, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
};

static void drm_mode_validate_flag(struct drm_connector *connector,
				   int flags)
{
	struct drm_display_mode *mode, *t;

	if (flags == (DRM_MODE_FLAG_DBLSCAN | DRM_MODE_FLAG_INTERLACE))
		return;

	list_for_each_entry_safe(mode, t, &connector->modes, head) {
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
 * drm_helper_probe_connector_modes - get complete set of display modes
 * @dev: DRM device
 * @maxX: max width for modes
 * @maxY: max height for modes
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Based on @dev's mode_config layout, scan all the connectors and try to detect
 * modes on them.  Modes will first be added to the connector's probed_modes
 * list, then culled (based on validity and the @maxX, @maxY parameters) and
 * put into the normal modes list.
 *
 * Intended to be used either at bootup time or when major configuration
 * changes have occurred.
 *
 * FIXME: take into account monitor limits
 *
 * RETURNS:
 * Number of modes found on @connector.
 */
int drm_helper_probe_single_connector_modes(struct drm_connector *connector,
					    uint32_t maxX, uint32_t maxY)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode, *t;
	struct drm_connector_helper_funcs *connector_funcs =
		connector->helper_private;
	int count = 0;
	int mode_flags = 0;

	DRM_DEBUG("%s\n", drm_get_connector_name(connector));
	/* set all modes to the unverified state */
	list_for_each_entry_safe(mode, t, &connector->modes, head)
		mode->status = MODE_UNVERIFIED;

	connector->status = connector->funcs->detect(connector);

	if (connector->status == connector_status_disconnected) {
		DRM_DEBUG("%s is disconnected\n",
			  drm_get_connector_name(connector));
		/* TODO set EDID to NULL */
		return 0;
	}

	count = (*connector_funcs->get_modes)(connector);
	if (!count)
		return 0;

	drm_mode_connector_list_update(connector);

	if (maxX && maxY)
		drm_mode_validate_size(dev, &connector->modes, maxX,
				       maxY, 0);

	if (connector->interlace_allowed)
		mode_flags |= DRM_MODE_FLAG_INTERLACE;
	if (connector->doublescan_allowed)
		mode_flags |= DRM_MODE_FLAG_DBLSCAN;
	drm_mode_validate_flag(connector, mode_flags);

	list_for_each_entry_safe(mode, t, &connector->modes, head) {
		if (mode->status == MODE_OK)
			mode->status = connector_funcs->mode_valid(connector,
								   mode);
	}


	drm_mode_prune_invalid(dev, &connector->modes, true);

	if (list_empty(&connector->modes))
		return 0;

	drm_mode_sort(&connector->modes);

	DRM_DEBUG("Probed modes for %s\n", drm_get_connector_name(connector));
	list_for_each_entry_safe(mode, t, &connector->modes, head) {
		mode->vrefresh = drm_mode_vrefresh(mode);

		drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V);
		drm_mode_debug_printmodeline(mode);
	}

	return count;
}
EXPORT_SYMBOL(drm_helper_probe_single_connector_modes);

int drm_helper_probe_connector_modes(struct drm_device *dev, uint32_t maxX,
				      uint32_t maxY)
{
	struct drm_connector *connector;
	int count = 0;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		count += drm_helper_probe_single_connector_modes(connector,
								 maxX, maxY);
	}

	return count;
}
EXPORT_SYMBOL(drm_helper_probe_connector_modes);

static void drm_helper_add_std_modes(struct drm_device *dev,
				     struct drm_connector *connector)
{
	struct drm_display_mode *mode, *t;
	int i;

	for (i = 0; i < ARRAY_SIZE(std_modes); i++) {
		struct drm_display_mode *stdmode;

		/*
		 * When no valid EDID modes are available we end up
		 * here and bailed in the past, now we add some standard
		 * modes and move on.
		 */
		stdmode = drm_mode_duplicate(dev, &std_modes[i]);
		drm_mode_probed_add(connector, stdmode);
		drm_mode_list_concat(&connector->probed_modes,
				     &connector->modes);

		DRM_DEBUG("Adding mode %s to %s\n", stdmode->name,
			  drm_get_connector_name(connector));
	}
	drm_mode_sort(&connector->modes);

	DRM_DEBUG("Added std modes on %s\n", drm_get_connector_name(connector));
	list_for_each_entry_safe(mode, t, &connector->modes, head) {
		mode->vrefresh = drm_mode_vrefresh(mode);

		drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V);
		drm_mode_debug_printmodeline(mode);
	}
}

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

/**
 * drm_disable_unused_functions - disable unused objects
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
	struct drm_encoder_helper_funcs *encoder_funcs;
	struct drm_crtc *crtc;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		encoder_funcs = encoder->helper_private;
		if (!drm_helper_encoder_in_use(encoder))
			(*encoder_funcs->dpms)(encoder, DRM_MODE_DPMS_OFF);
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
		crtc->enabled = drm_helper_crtc_in_use(crtc);
		if (!crtc->enabled) {
			crtc_funcs->dpms(crtc, DRM_MODE_DPMS_OFF);
			crtc->fb = NULL;
		}
	}
}
EXPORT_SYMBOL(drm_helper_disable_unused_functions);

static struct drm_display_mode *drm_has_preferred_mode(struct drm_connector *connector, int width, int height)
{
	struct drm_display_mode *mode;

	list_for_each_entry(mode, &connector->modes, head) {
		if (drm_mode_width(mode) > width ||
		    drm_mode_height(mode) > height)
			continue;
		if (mode->type & DRM_MODE_TYPE_PREFERRED)
			return mode;
	}
	return NULL;
}

static bool drm_connector_enabled(struct drm_connector *connector, bool strict)
{
	bool enable;

	if (strict) {
		enable = connector->status == connector_status_connected;
	} else {
		enable = connector->status != connector_status_disconnected;
	}
	return enable;
}

static void drm_enable_connectors(struct drm_device *dev, bool *enabled)
{
	bool any_enabled = false;
	struct drm_connector *connector;
	int i = 0;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		enabled[i] = drm_connector_enabled(connector, true);
		DRM_DEBUG("connector %d enabled? %s\n", connector->base.id,
			  enabled[i] ? "yes" : "no");
		any_enabled |= enabled[i];
		i++;
	}

	if (any_enabled)
		return;

	i = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		enabled[i] = drm_connector_enabled(connector, false);
		i++;
	}
}

static bool drm_target_preferred(struct drm_device *dev,
				 struct drm_display_mode **modes,
				 bool *enabled, int width, int height)
{
	struct drm_connector *connector;
	int i = 0;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {

		if (enabled[i] == false) {
			i++;
			continue;
		}

		DRM_DEBUG("looking for preferred mode on connector %d\n",
			  connector->base.id);

		modes[i] = drm_has_preferred_mode(connector, width, height);
		/* No preferred modes, pick one off the list */
		if (!modes[i] && !list_empty(&connector->modes)) {
			list_for_each_entry(modes[i], &connector->modes, head)
				break;
		}
		DRM_DEBUG("found mode %s\n", modes[i] ? modes[i]->name :
			  "none");
		i++;
	}
	return true;
}

static int drm_pick_crtcs(struct drm_device *dev,
			  struct drm_crtc **best_crtcs,
			  struct drm_display_mode **modes,
			  int n, int width, int height)
{
	int c, o;
	struct drm_connector *connector;
	struct drm_connector_helper_funcs *connector_funcs;
	struct drm_encoder *encoder;
	struct drm_crtc *best_crtc;
	int my_score, best_score, score;
	struct drm_crtc **crtcs, *crtc;

	if (n == dev->mode_config.num_connector)
		return 0;
	c = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (c == n)
			break;
		c++;
	}

	best_crtcs[n] = NULL;
	best_crtc = NULL;
	best_score = drm_pick_crtcs(dev, best_crtcs, modes, n+1, width, height);
	if (modes[n] == NULL)
		return best_score;

	crtcs = kmalloc(dev->mode_config.num_connector *
			sizeof(struct drm_crtc *), GFP_KERNEL);
	if (!crtcs)
		return best_score;

	my_score = 1;
	if (connector->status == connector_status_connected)
		my_score++;
	if (drm_has_preferred_mode(connector, width, height))
		my_score++;

	connector_funcs = connector->helper_private;
	encoder = connector_funcs->best_encoder(connector);
	if (!encoder)
		goto out;

	connector->encoder = encoder;

	/* select a crtc for this connector and then attempt to configure
	   remaining connectors */
	c = 0;
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {

		if ((connector->encoder->possible_crtcs & (1 << c)) == 0) {
			c++;
			continue;
		}

		for (o = 0; o < n; o++)
			if (best_crtcs[o] == crtc)
				break;

		if (o < n) {
			/* ignore cloning for now */
			c++;
			continue;
		}

		crtcs[n] = crtc;
		memcpy(crtcs, best_crtcs, n * sizeof(struct drm_crtc *));
		score = my_score + drm_pick_crtcs(dev, crtcs, modes, n + 1,
						  width, height);
		if (score > best_score) {
			best_crtc = crtc;
			best_score = score;
			memcpy(best_crtcs, crtcs,
			       dev->mode_config.num_connector *
			       sizeof(struct drm_crtc *));
		}
		c++;
	}
out:
	kfree(crtcs);
	return best_score;
}

static void drm_setup_crtcs(struct drm_device *dev)
{
	struct drm_crtc **crtcs;
	struct drm_display_mode **modes;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	bool *enabled;
	int width, height;
	int i, ret;

	DRM_DEBUG("\n");

	width = dev->mode_config.max_width;
	height = dev->mode_config.max_height;

	/* clean out all the encoder/crtc combos */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		encoder->crtc = NULL;
	}

	crtcs = kcalloc(dev->mode_config.num_connector,
			sizeof(struct drm_crtc *), GFP_KERNEL);
	modes = kcalloc(dev->mode_config.num_connector,
			sizeof(struct drm_display_mode *), GFP_KERNEL);
	enabled = kcalloc(dev->mode_config.num_connector,
			  sizeof(bool), GFP_KERNEL);

	drm_enable_connectors(dev, enabled);

	ret = drm_target_preferred(dev, modes, enabled, width, height);
	if (!ret)
		DRM_ERROR("Unable to find initial modes\n");

	DRM_DEBUG("picking CRTCs for %dx%d config\n", width, height);

	drm_pick_crtcs(dev, crtcs, modes, 0, width, height);

	i = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct drm_display_mode *mode = modes[i];
		struct drm_crtc *crtc = crtcs[i];

		if (connector->encoder == NULL) {
			i++;
			continue;
		}

		if (mode && crtc) {
			DRM_DEBUG("desired mode %s set on crtc %d\n",
				  mode->name, crtc->base.id);
			crtc->desired_mode = mode;
			connector->encoder->crtc = crtc;
		} else
			connector->encoder->crtc = NULL;
		i++;
	}

	kfree(crtcs);
	kfree(modes);
	kfree(enabled);
}

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

	WARN(!crtc, "checking null crtc?");

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
			(*encoder_funcs->dpms)(encoder, DRM_MODE_DPMS_OFF);
		/* Disable encoders whose CRTC is about to change */
		if (encoder_funcs->get_crtc &&
		    encoder->crtc != (*encoder_funcs->get_crtc)(encoder))
			(*encoder_funcs->dpms)(encoder, DRM_MODE_DPMS_OFF);
	}
}

/**
 * drm_crtc_set_mode - set a mode
 * @crtc: CRTC to program
 * @mode: mode to use
 * @x: width of mode
 * @y: height of mode
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Try to set @mode on @crtc.  Give @crtc and its associated connectors a chance
 * to fixup or reject the mode prior to trying to set it.
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
	struct drm_display_mode *adjusted_mode, saved_mode;
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	struct drm_encoder_helper_funcs *encoder_funcs;
	int saved_x, saved_y;
	struct drm_encoder *encoder;
	bool ret = true;

	adjusted_mode = drm_mode_duplicate(dev, mode);

	crtc->enabled = drm_helper_crtc_in_use(crtc);

	if (!crtc->enabled)
		return true;

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
			goto done;
		}
	}

	if (!(ret = crtc_funcs->mode_fixup(crtc, mode, adjusted_mode))) {
		goto done;
	}

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

		DRM_INFO("%s: set mode %s %x\n", drm_get_encoder_name(encoder),
			 mode->name, mode->base.id);
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

	/* XXX free adjustedmode */
	drm_mode_destroy(dev, adjusted_mode);
	/* FIXME: add subpixel order */
done:
	if (!ret) {
		crtc->mode = saved_mode;
		crtc->x = saved_x;
		crtc->y = saved_y;
	}

	return ret;
}
EXPORT_SYMBOL(drm_crtc_helper_set_mode);


/**
 * drm_crtc_helper_set_config - set a new config from userspace
 * @crtc: CRTC to setup
 * @crtc_info: user provided configuration
 * @new_mode: new mode to set
 * @connector_set: set of connectors for the new config
 * @fb: new framebuffer
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Setup a new configuration, provided by the user in @crtc_info, and enable
 * it.
 *
 * RETURNS:
 * Zero. (FIXME)
 */
int drm_crtc_helper_set_config(struct drm_mode_set *set)
{
	struct drm_device *dev;
	struct drm_crtc **save_crtcs, *new_crtc;
	struct drm_encoder **save_encoders, *new_encoder;
	struct drm_framebuffer *old_fb = NULL;
	bool save_enabled;
	bool mode_changed = false; /* if true do a full mode set */
	bool fb_changed = false; /* if true and !mode_changed just do a flip */
	struct drm_connector *connector;
	int count = 0, ro, fail = 0;
	struct drm_crtc_helper_funcs *crtc_funcs;
	int ret = 0;

	DRM_DEBUG("\n");

	if (!set)
		return -EINVAL;

	if (!set->crtc)
		return -EINVAL;

	if (!set->crtc->helper_private)
		return -EINVAL;

	crtc_funcs = set->crtc->helper_private;

	DRM_DEBUG("crtc: %p %d fb: %p connectors: %p num_connectors: %d (x, y) (%i, %i)\n",
		  set->crtc, set->crtc->base.id, set->fb, set->connectors,
		  (int)set->num_connectors, set->x, set->y);

	dev = set->crtc->dev;

	/* save previous config */
	save_enabled = set->crtc->enabled;

	/*
	 * We do mode_config.num_connectors here since we'll look at the
	 * CRTC and encoder associated with each connector later.
	 */
	save_crtcs = kzalloc(dev->mode_config.num_connector *
			     sizeof(struct drm_crtc *), GFP_KERNEL);
	if (!save_crtcs)
		return -ENOMEM;

	save_encoders = kzalloc(dev->mode_config.num_connector *
				sizeof(struct drm_encoders *), GFP_KERNEL);
	if (!save_encoders) {
		kfree(save_crtcs);
		return -ENOMEM;
	}

	/* We should be able to check here if the fb has the same properties
	 * and then just flip_or_move it */
	if (set->crtc->fb != set->fb) {
		/* If we have no fb then treat it as a full mode set */
		if (set->crtc->fb == NULL) {
			DRM_DEBUG("crtc has no fb, full mode set\n");
			mode_changed = true;
		} else if (set->fb == NULL) {
			mode_changed = true;
		} else if ((set->fb->bits_per_pixel !=
			 set->crtc->fb->bits_per_pixel) ||
			 set->fb->depth != set->crtc->fb->depth)
			fb_changed = true;
		else
			fb_changed = true;
	}

	if (set->x != set->crtc->x || set->y != set->crtc->y)
		fb_changed = true;

	if (set->mode && !drm_mode_equal(set->mode, &set->crtc->mode)) {
		DRM_DEBUG("modes are different, full mode set\n");
		drm_mode_debug_printmodeline(&set->crtc->mode);
		drm_mode_debug_printmodeline(set->mode);
		mode_changed = true;
	}

	/* a) traverse passed in connector list and get encoders for them */
	count = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct drm_connector_helper_funcs *connector_funcs =
			connector->helper_private;
		save_encoders[count++] = connector->encoder;
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
			DRM_DEBUG("encoder changed, full mode switch\n");
			mode_changed = true;
			connector->encoder = new_encoder;
		}
	}

	if (fail) {
		ret = -EINVAL;
		goto fail_no_encoder;
	}

	count = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (!connector->encoder)
			continue;

		save_crtcs[count++] = connector->encoder->crtc;

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
			goto fail_set_mode;
		}
		if (new_crtc != connector->encoder->crtc) {
			DRM_DEBUG("crtc changed, full mode switch\n");
			mode_changed = true;
			connector->encoder->crtc = new_crtc;
		}
		DRM_DEBUG("setting connector %d crtc to %p\n",
			  connector->base.id, new_crtc);
	}

	/* mode_set_base is not a required function */
	if (fb_changed && !crtc_funcs->mode_set_base)
		mode_changed = true;

	if (mode_changed) {
		old_fb = set->crtc->fb;
		set->crtc->fb = set->fb;
		set->crtc->enabled = (set->mode != NULL);
		if (set->mode != NULL) {
			DRM_DEBUG("attempting to set mode from userspace\n");
			drm_mode_debug_printmodeline(set->mode);
			if (!drm_crtc_helper_set_mode(set->crtc, set->mode,
						      set->x, set->y,
						      old_fb)) {
				DRM_ERROR("failed to set mode on crtc %p\n",
					  set->crtc);
				ret = -EINVAL;
				goto fail_set_mode;
			}
			/* TODO are these needed? */
			set->crtc->desired_x = set->x;
			set->crtc->desired_y = set->y;
			set->crtc->desired_mode = set->mode;
		}
		drm_helper_disable_unused_functions(dev);
	} else if (fb_changed) {
		old_fb = set->crtc->fb;
		if (set->crtc->fb != set->fb)
			set->crtc->fb = set->fb;
		ret = crtc_funcs->mode_set_base(set->crtc,
						set->x, set->y, old_fb);
		if (ret != 0)
		    goto fail_set_mode;
	}

	kfree(save_encoders);
	kfree(save_crtcs);
	return 0;

fail_set_mode:
	set->crtc->enabled = save_enabled;
	set->crtc->fb = old_fb;
	count = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (!connector->encoder)
			continue;

		connector->encoder->crtc = save_crtcs[count++];
	}
fail_no_encoder:
	kfree(save_crtcs);
	count = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		connector->encoder = save_encoders[count++];
	}
	kfree(save_encoders);
	return ret;
}
EXPORT_SYMBOL(drm_crtc_helper_set_config);

bool drm_helper_plugged_event(struct drm_device *dev)
{
	DRM_DEBUG("\n");

	drm_helper_probe_connector_modes(dev, dev->mode_config.max_width,
					 dev->mode_config.max_height);

	drm_setup_crtcs(dev);

	/* alert the driver fb layer */
	dev->mode_config.funcs->fb_changed(dev);

	/* FIXME: send hotplug event */
	return true;
}
/**
 * drm_initial_config - setup a sane initial connector configuration
 * @dev: DRM device
 *
 * LOCKING:
 * Called at init time, must take mode config lock.
 *
 * Scan the CRTCs and connectors and try to put together an initial setup.
 * At the moment, this is a cloned configuration across all heads with
 * a new framebuffer object as the backing store.
 *
 * RETURNS:
 * Zero if everything went ok, nonzero otherwise.
 */
bool drm_helper_initial_config(struct drm_device *dev)
{
	struct drm_connector *connector;
	int count = 0;

	count = drm_helper_probe_connector_modes(dev,
						 dev->mode_config.max_width,
						 dev->mode_config.max_height);

	/*
	 * None of the available connectors had any modes, so add some
	 * and try to light them up anyway
	 */
	if (!count) {
		DRM_ERROR("connectors have no modes, using standard modes\n");
		list_for_each_entry(connector,
				    &dev->mode_config.connector_list,
				    head)
			drm_helper_add_std_modes(dev, connector);
	}

	drm_setup_crtcs(dev);

	/* alert the driver fb layer */
	dev->mode_config.funcs->fb_changed(dev);

	return 0;
}
EXPORT_SYMBOL(drm_helper_initial_config);

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
 * drm_helper_connector_dpms
 * @connector affected connector
 * @mode DPMS mode
 *
 * Calls the low-level connector DPMS function, then
 * calls appropriate encoder and crtc DPMS functions as well
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

/**
 * drm_hotplug_stage_two
 * @dev DRM device
 * @connector hotpluged connector
 *
 * LOCKING.
 * Caller must hold mode config lock, function might grab struct lock.
 *
 * Stage two of a hotplug.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_helper_hotplug_stage_two(struct drm_device *dev)
{
	drm_helper_plugged_event(dev);

	return 0;
}
EXPORT_SYMBOL(drm_helper_hotplug_stage_two);

int drm_helper_mode_fill_fb_struct(struct drm_framebuffer *fb,
				   struct drm_mode_fb_cmd *mode_cmd)
{
	fb->width = mode_cmd->width;
	fb->height = mode_cmd->height;
	fb->pitch = mode_cmd->pitch;
	fb->bits_per_pixel = mode_cmd->bpp;
	fb->depth = mode_cmd->depth;

	return 0;
}
EXPORT_SYMBOL(drm_helper_mode_fill_fb_struct);

int drm_helper_resume_force_mode(struct drm_device *dev)
{
	struct drm_crtc *crtc;
	int ret;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {

		if (!crtc->enabled)
			continue;

		ret = drm_crtc_helper_set_mode(crtc, &crtc->mode,
					       crtc->x, crtc->y, crtc->fb);

		if (ret == false)
			DRM_ERROR("failed to set mode on crtc %p\n", crtc);
	}
	/* disable the unused connectors while restoring the modesetting */
	drm_helper_disable_unused_functions(dev);
	return 0;
}
EXPORT_SYMBOL(drm_helper_resume_force_mode);
