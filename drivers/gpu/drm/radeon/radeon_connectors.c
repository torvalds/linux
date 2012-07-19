/*
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 */
#include "drmP.h"
#include "drm_edid.h"
#include "drm_crtc_helper.h"
#include "drm_fb_helper.h"
#include "radeon_drm.h"
#include "radeon.h"
#include "atom.h"

extern void
radeon_combios_connected_scratch_regs(struct drm_connector *connector,
				      struct drm_encoder *encoder,
				      bool connected);
extern void
radeon_atombios_connected_scratch_regs(struct drm_connector *connector,
				       struct drm_encoder *encoder,
				       bool connected);

extern void
radeon_legacy_backlight_init(struct radeon_encoder *radeon_encoder,
			     struct drm_connector *drm_connector);

bool radeon_connector_encoder_is_dp_bridge(struct drm_connector *connector);

void radeon_connector_hotplug(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);

	/* bail if the connector does not have hpd pin, e.g.,
	 * VGA, TV, etc.
	 */
	if (radeon_connector->hpd.hpd == RADEON_HPD_NONE)
		return;

	radeon_hpd_set_polarity(rdev, radeon_connector->hpd.hpd);

	/* if the connector is already off, don't turn it back on */
	if (connector->dpms != DRM_MODE_DPMS_ON)
		return;

	/* just deal with DP (not eDP) here. */
	if (connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort) {
		struct radeon_connector_atom_dig *dig_connector =
			radeon_connector->con_priv;

		/* if existing sink type was not DP no need to retrain */
		if (dig_connector->dp_sink_type != CONNECTOR_OBJECT_ID_DISPLAYPORT)
			return;

		/* first get sink type as it may be reset after (un)plug */
		dig_connector->dp_sink_type = radeon_dp_getsinktype(radeon_connector);
		/* don't do anything if sink is not display port, i.e.,
		 * passive dp->(dvi|hdmi) adaptor
		 */
		if (dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_DISPLAYPORT) {
			int saved_dpms = connector->dpms;
			/* Only turn off the display if it's physically disconnected */
			if (!radeon_hpd_sense(rdev, radeon_connector->hpd.hpd))
				drm_helper_connector_dpms(connector, DRM_MODE_DPMS_OFF);
			else if (radeon_dp_needs_link_train(radeon_connector))
				drm_helper_connector_dpms(connector, DRM_MODE_DPMS_ON);
			connector->dpms = saved_dpms;
		}
	}
}

static void radeon_property_change_mode(struct drm_encoder *encoder)
{
	struct drm_crtc *crtc = encoder->crtc;

	if (crtc && crtc->enabled) {
		drm_crtc_helper_set_mode(crtc, &crtc->mode,
					 crtc->x, crtc->y, crtc->fb);
	}
}
static void
radeon_connector_update_scratch_regs(struct drm_connector *connector, enum drm_connector_status status)
{
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct drm_encoder *best_encoder = NULL;
	struct drm_encoder *encoder = NULL;
	struct drm_connector_helper_funcs *connector_funcs = connector->helper_private;
	struct drm_mode_object *obj;
	bool connected;
	int i;

	best_encoder = connector_funcs->best_encoder(connector);

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		if (connector->encoder_ids[i] == 0)
			break;

		obj = drm_mode_object_find(connector->dev,
					   connector->encoder_ids[i],
					   DRM_MODE_OBJECT_ENCODER);
		if (!obj)
			continue;

		encoder = obj_to_encoder(obj);

		if ((encoder == best_encoder) && (status == connector_status_connected))
			connected = true;
		else
			connected = false;

		if (rdev->is_atom_bios)
			radeon_atombios_connected_scratch_regs(connector, encoder, connected);
		else
			radeon_combios_connected_scratch_regs(connector, encoder, connected);

	}
}

struct drm_encoder *radeon_find_encoder(struct drm_connector *connector, int encoder_type)
{
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;
	int i;

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		if (connector->encoder_ids[i] == 0)
			break;

		obj = drm_mode_object_find(connector->dev, connector->encoder_ids[i], DRM_MODE_OBJECT_ENCODER);
		if (!obj)
			continue;

		encoder = obj_to_encoder(obj);
		if (encoder->encoder_type == encoder_type)
			return encoder;
	}
	return NULL;
}

struct drm_encoder *radeon_best_single_encoder(struct drm_connector *connector)
{
	int enc_id = connector->encoder_ids[0];
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;

	/* pick the encoder ids */
	if (enc_id) {
		obj = drm_mode_object_find(connector->dev, enc_id, DRM_MODE_OBJECT_ENCODER);
		if (!obj)
			return NULL;
		encoder = obj_to_encoder(obj);
		return encoder;
	}
	return NULL;
}

/*
 * radeon_connector_analog_encoder_conflict_solve
 * - search for other connectors sharing this encoder
 *   if priority is true, then set them disconnected if this is connected
 *   if priority is false, set us disconnected if they are connected
 */
static enum drm_connector_status
radeon_connector_analog_encoder_conflict_solve(struct drm_connector *connector,
					       struct drm_encoder *encoder,
					       enum drm_connector_status current_status,
					       bool priority)
{
	struct drm_device *dev = connector->dev;
	struct drm_connector *conflict;
	struct radeon_connector *radeon_conflict;
	int i;

	list_for_each_entry(conflict, &dev->mode_config.connector_list, head) {
		if (conflict == connector)
			continue;

		radeon_conflict = to_radeon_connector(conflict);
		for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
			if (conflict->encoder_ids[i] == 0)
				break;

			/* if the IDs match */
			if (conflict->encoder_ids[i] == encoder->base.id) {
				if (conflict->status != connector_status_connected)
					continue;

				if (radeon_conflict->use_digital)
					continue;

				if (priority == true) {
					DRM_DEBUG_KMS("1: conflicting encoders switching off %s\n", drm_get_connector_name(conflict));
					DRM_DEBUG_KMS("in favor of %s\n", drm_get_connector_name(connector));
					conflict->status = connector_status_disconnected;
					radeon_connector_update_scratch_regs(conflict, connector_status_disconnected);
				} else {
					DRM_DEBUG_KMS("2: conflicting encoders switching off %s\n", drm_get_connector_name(connector));
					DRM_DEBUG_KMS("in favor of %s\n", drm_get_connector_name(conflict));
					current_status = connector_status_disconnected;
				}
				break;
			}
		}
	}
	return current_status;

}

static struct drm_display_mode *radeon_fp_native_mode(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_display_mode *mode = NULL;
	struct drm_display_mode *native_mode = &radeon_encoder->native_mode;

	if (native_mode->hdisplay != 0 &&
	    native_mode->vdisplay != 0 &&
	    native_mode->clock != 0) {
		mode = drm_mode_duplicate(dev, native_mode);
		mode->type = DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER;
		drm_mode_set_name(mode);

		DRM_DEBUG_KMS("Adding native panel mode %s\n", mode->name);
	} else if (native_mode->hdisplay != 0 &&
		   native_mode->vdisplay != 0) {
		/* mac laptops without an edid */
		/* Note that this is not necessarily the exact panel mode,
		 * but an approximation based on the cvt formula.  For these
		 * systems we should ideally read the mode info out of the
		 * registers or add a mode table, but this works and is much
		 * simpler.
		 */
		mode = drm_cvt_mode(dev, native_mode->hdisplay, native_mode->vdisplay, 60, true, false, false);
		mode->type = DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER;
		DRM_DEBUG_KMS("Adding cvt approximation of native panel mode %s\n", mode->name);
	}
	return mode;
}

static void radeon_add_common_modes(struct drm_encoder *encoder, struct drm_connector *connector)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_display_mode *mode = NULL;
	struct drm_display_mode *native_mode = &radeon_encoder->native_mode;
	int i;
	struct mode_size {
		int w;
		int h;
	} common_modes[17] = {
		{ 640,  480},
		{ 720,  480},
		{ 800,  600},
		{ 848,  480},
		{1024,  768},
		{1152,  768},
		{1280,  720},
		{1280,  800},
		{1280,  854},
		{1280,  960},
		{1280, 1024},
		{1440,  900},
		{1400, 1050},
		{1680, 1050},
		{1600, 1200},
		{1920, 1080},
		{1920, 1200}
	};

	for (i = 0; i < 17; i++) {
		if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT)) {
			if (common_modes[i].w > 1024 ||
			    common_modes[i].h > 768)
				continue;
		}
		if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
			if (common_modes[i].w > native_mode->hdisplay ||
			    common_modes[i].h > native_mode->vdisplay ||
			    (common_modes[i].w == native_mode->hdisplay &&
			     common_modes[i].h == native_mode->vdisplay))
				continue;
		}
		if (common_modes[i].w < 320 || common_modes[i].h < 200)
			continue;

		mode = drm_cvt_mode(dev, common_modes[i].w, common_modes[i].h, 60, false, false, false);
		drm_mode_probed_add(connector, mode);
	}
}

int radeon_connector_set_property(struct drm_connector *connector, struct drm_property *property,
				  uint64_t val)
{
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct drm_encoder *encoder;
	struct radeon_encoder *radeon_encoder;

	if (property == rdev->mode_info.coherent_mode_property) {
		struct radeon_encoder_atom_dig *dig;
		bool new_coherent_mode;

		/* need to find digital encoder on connector */
		encoder = radeon_find_encoder(connector, DRM_MODE_ENCODER_TMDS);
		if (!encoder)
			return 0;

		radeon_encoder = to_radeon_encoder(encoder);

		if (!radeon_encoder->enc_priv)
			return 0;

		dig = radeon_encoder->enc_priv;
		new_coherent_mode = val ? true : false;
		if (dig->coherent_mode != new_coherent_mode) {
			dig->coherent_mode = new_coherent_mode;
			radeon_property_change_mode(&radeon_encoder->base);
		}
	}

	if (property == rdev->mode_info.underscan_property) {
		/* need to find digital encoder on connector */
		encoder = radeon_find_encoder(connector, DRM_MODE_ENCODER_TMDS);
		if (!encoder)
			return 0;

		radeon_encoder = to_radeon_encoder(encoder);

		if (radeon_encoder->underscan_type != val) {
			radeon_encoder->underscan_type = val;
			radeon_property_change_mode(&radeon_encoder->base);
		}
	}

	if (property == rdev->mode_info.underscan_hborder_property) {
		/* need to find digital encoder on connector */
		encoder = radeon_find_encoder(connector, DRM_MODE_ENCODER_TMDS);
		if (!encoder)
			return 0;

		radeon_encoder = to_radeon_encoder(encoder);

		if (radeon_encoder->underscan_hborder != val) {
			radeon_encoder->underscan_hborder = val;
			radeon_property_change_mode(&radeon_encoder->base);
		}
	}

	if (property == rdev->mode_info.underscan_vborder_property) {
		/* need to find digital encoder on connector */
		encoder = radeon_find_encoder(connector, DRM_MODE_ENCODER_TMDS);
		if (!encoder)
			return 0;

		radeon_encoder = to_radeon_encoder(encoder);

		if (radeon_encoder->underscan_vborder != val) {
			radeon_encoder->underscan_vborder = val;
			radeon_property_change_mode(&radeon_encoder->base);
		}
	}

	if (property == rdev->mode_info.tv_std_property) {
		encoder = radeon_find_encoder(connector, DRM_MODE_ENCODER_TVDAC);
		if (!encoder) {
			encoder = radeon_find_encoder(connector, DRM_MODE_ENCODER_DAC);
		}

		if (!encoder)
			return 0;

		radeon_encoder = to_radeon_encoder(encoder);
		if (!radeon_encoder->enc_priv)
			return 0;
		if (ASIC_IS_AVIVO(rdev) || radeon_r4xx_atom) {
			struct radeon_encoder_atom_dac *dac_int;
			dac_int = radeon_encoder->enc_priv;
			dac_int->tv_std = val;
		} else {
			struct radeon_encoder_tv_dac *dac_int;
			dac_int = radeon_encoder->enc_priv;
			dac_int->tv_std = val;
		}
		radeon_property_change_mode(&radeon_encoder->base);
	}

	if (property == rdev->mode_info.load_detect_property) {
		struct radeon_connector *radeon_connector =
			to_radeon_connector(connector);

		if (val == 0)
			radeon_connector->dac_load_detect = false;
		else
			radeon_connector->dac_load_detect = true;
	}

	if (property == rdev->mode_info.tmds_pll_property) {
		struct radeon_encoder_int_tmds *tmds = NULL;
		bool ret = false;
		/* need to find digital encoder on connector */
		encoder = radeon_find_encoder(connector, DRM_MODE_ENCODER_TMDS);
		if (!encoder)
			return 0;

		radeon_encoder = to_radeon_encoder(encoder);

		tmds = radeon_encoder->enc_priv;
		if (!tmds)
			return 0;

		if (val == 0) {
			if (rdev->is_atom_bios)
				ret = radeon_atombios_get_tmds_info(radeon_encoder, tmds);
			else
				ret = radeon_legacy_get_tmds_info_from_combios(radeon_encoder, tmds);
		}
		if (val == 1 || ret == false) {
			radeon_legacy_get_tmds_info_from_table(radeon_encoder, tmds);
		}
		radeon_property_change_mode(&radeon_encoder->base);
	}

	return 0;
}

/*
 * Some integrated ATI Radeon chipset implementations (e. g.
 * Asus M2A-VM HDMI) may indicate the availability of a DDC,
 * even when there's no monitor connected. For these connectors
 * following DDC probe extension will be applied: check also for the
 * availability of EDID with at least a correct EDID header. Only then,
 * DDC is assumed to be available. This prevents drm_get_edid() and
 * drm_edid_block_valid() from periodically dumping data and kernel
 * errors into the logs and onto the terminal.
 */
static bool radeon_connector_needs_extended_probe(struct radeon_device *dev,
				     uint32_t supported_device,
				     int connector_type)
{
	/* Asus M2A-VM HDMI board sends data to i2c bus even,
	 * if HDMI add-on card is not plugged in or HDMI is disabled in
	 * BIOS. Valid DDC can only be assumed, if also a valid EDID header
	 * can be retrieved via i2c bus during DDC probe */
	if ((dev->pdev->device == 0x791e) &&
	    (dev->pdev->subsystem_vendor == 0x1043) &&
	    (dev->pdev->subsystem_device == 0x826d)) {
		if ((connector_type == DRM_MODE_CONNECTOR_HDMIA) &&
		    (supported_device == ATOM_DEVICE_DFP2_SUPPORT))
			return true;
	}
	/* ECS A740GM-M with ATI RADEON 2100 sends data to i2c bus
	 * for a DVI connector that is not implemented */
	if ((dev->pdev->device == 0x796e) &&
	    (dev->pdev->subsystem_vendor == 0x1019) &&
	    (dev->pdev->subsystem_device == 0x2615)) {
		if ((connector_type == DRM_MODE_CONNECTOR_DVID) &&
		    (supported_device == ATOM_DEVICE_DFP2_SUPPORT))
			return true;
	}
	/* TOSHIBA Satellite L300D with ATI Mobility Radeon x1100
	 * (RS690M) sends data to i2c bus for a HDMI connector that
	 * is not implemented */
	if ((dev->pdev->device == 0x791f) &&
	    (dev->pdev->subsystem_vendor == 0x1179) &&
	    (dev->pdev->subsystem_device == 0xff68)) {
		if ((connector_type == DRM_MODE_CONNECTOR_HDMIA) &&
		    (supported_device == ATOM_DEVICE_DFP2_SUPPORT))
			return true;
	}

	/* Default: no EDID header probe required for DDC probing */
	return false;
}

static void radeon_fixup_lvds_native_mode(struct drm_encoder *encoder,
					  struct drm_connector *connector)
{
	struct radeon_encoder *radeon_encoder =	to_radeon_encoder(encoder);
	struct drm_display_mode *native_mode = &radeon_encoder->native_mode;

	/* Try to get native mode details from EDID if necessary */
	if (!native_mode->clock) {
		struct drm_display_mode *t, *mode;

		list_for_each_entry_safe(mode, t, &connector->probed_modes, head) {
			if (mode->hdisplay == native_mode->hdisplay &&
			    mode->vdisplay == native_mode->vdisplay) {
				*native_mode = *mode;
				drm_mode_set_crtcinfo(native_mode, CRTC_INTERLACE_HALVE_V);
				DRM_DEBUG_KMS("Determined LVDS native mode details from EDID\n");
				break;
			}
		}
	}
	if (!native_mode->clock) {
		DRM_DEBUG_KMS("No LVDS native mode details, disabling RMX\n");
		radeon_encoder->rmx_type = RMX_OFF;
	}
}

static int radeon_lvds_get_modes(struct drm_connector *connector)
{
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct drm_encoder *encoder;
	int ret = 0;
	struct drm_display_mode *mode;

	if (radeon_connector->ddc_bus) {
		ret = radeon_ddc_get_modes(radeon_connector);
		if (ret > 0) {
			encoder = radeon_best_single_encoder(connector);
			if (encoder) {
				radeon_fixup_lvds_native_mode(encoder, connector);
				/* add scaled modes */
				radeon_add_common_modes(encoder, connector);
			}
			return ret;
		}
	}

	encoder = radeon_best_single_encoder(connector);
	if (!encoder)
		return 0;

	/* we have no EDID modes */
	mode = radeon_fp_native_mode(encoder);
	if (mode) {
		ret = 1;
		drm_mode_probed_add(connector, mode);
		/* add the width/height from vbios tables if available */
		connector->display_info.width_mm = mode->width_mm;
		connector->display_info.height_mm = mode->height_mm;
		/* add scaled modes */
		radeon_add_common_modes(encoder, connector);
	}

	return ret;
}

static int radeon_lvds_mode_valid(struct drm_connector *connector,
				  struct drm_display_mode *mode)
{
	struct drm_encoder *encoder = radeon_best_single_encoder(connector);

	if ((mode->hdisplay < 320) || (mode->vdisplay < 240))
		return MODE_PANEL;

	if (encoder) {
		struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
		struct drm_display_mode *native_mode = &radeon_encoder->native_mode;

		/* AVIVO hardware supports downscaling modes larger than the panel
		 * to the panel size, but I'm not sure this is desirable.
		 */
		if ((mode->hdisplay > native_mode->hdisplay) ||
		    (mode->vdisplay > native_mode->vdisplay))
			return MODE_PANEL;

		/* if scaling is disabled, block non-native modes */
		if (radeon_encoder->rmx_type == RMX_OFF) {
			if ((mode->hdisplay != native_mode->hdisplay) ||
			    (mode->vdisplay != native_mode->vdisplay))
				return MODE_PANEL;
		}
	}

	return MODE_OK;
}

static enum drm_connector_status
radeon_lvds_detect(struct drm_connector *connector, bool force)
{
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct drm_encoder *encoder = radeon_best_single_encoder(connector);
	enum drm_connector_status ret = connector_status_disconnected;

	if (encoder) {
		struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
		struct drm_display_mode *native_mode = &radeon_encoder->native_mode;

		/* check if panel is valid */
		if (native_mode->hdisplay >= 320 && native_mode->vdisplay >= 240)
			ret = connector_status_connected;

	}

	/* check for edid as well */
	if (radeon_connector->edid)
		ret = connector_status_connected;
	else {
		if (radeon_connector->ddc_bus) {
			radeon_connector->edid = drm_get_edid(&radeon_connector->base,
							      &radeon_connector->ddc_bus->adapter);
			if (radeon_connector->edid)
				ret = connector_status_connected;
		}
	}
	/* check acpi lid status ??? */

	radeon_connector_update_scratch_regs(connector, ret);
	return ret;
}

static void radeon_connector_destroy(struct drm_connector *connector)
{
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);

	if (radeon_connector->edid)
		kfree(radeon_connector->edid);
	kfree(radeon_connector->con_priv);
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static int radeon_lvds_set_property(struct drm_connector *connector,
				    struct drm_property *property,
				    uint64_t value)
{
	struct drm_device *dev = connector->dev;
	struct radeon_encoder *radeon_encoder;
	enum radeon_rmx_type rmx_type;

	DRM_DEBUG_KMS("\n");
	if (property != dev->mode_config.scaling_mode_property)
		return 0;

	if (connector->encoder)
		radeon_encoder = to_radeon_encoder(connector->encoder);
	else {
		struct drm_connector_helper_funcs *connector_funcs = connector->helper_private;
		radeon_encoder = to_radeon_encoder(connector_funcs->best_encoder(connector));
	}

	switch (value) {
	case DRM_MODE_SCALE_NONE: rmx_type = RMX_OFF; break;
	case DRM_MODE_SCALE_CENTER: rmx_type = RMX_CENTER; break;
	case DRM_MODE_SCALE_ASPECT: rmx_type = RMX_ASPECT; break;
	default:
	case DRM_MODE_SCALE_FULLSCREEN: rmx_type = RMX_FULL; break;
	}
	if (radeon_encoder->rmx_type == rmx_type)
		return 0;

	radeon_encoder->rmx_type = rmx_type;

	radeon_property_change_mode(&radeon_encoder->base);
	return 0;
}


struct drm_connector_helper_funcs radeon_lvds_connector_helper_funcs = {
	.get_modes = radeon_lvds_get_modes,
	.mode_valid = radeon_lvds_mode_valid,
	.best_encoder = radeon_best_single_encoder,
};

struct drm_connector_funcs radeon_lvds_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = radeon_lvds_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = radeon_connector_destroy,
	.set_property = radeon_lvds_set_property,
};

static int radeon_vga_get_modes(struct drm_connector *connector)
{
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	int ret;

	ret = radeon_ddc_get_modes(radeon_connector);

	return ret;
}

static int radeon_vga_mode_valid(struct drm_connector *connector,
				  struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;

	/* XXX check mode bandwidth */

	if ((mode->clock / 10) > rdev->clock.max_pixel_clock)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static enum drm_connector_status
radeon_vga_detect(struct drm_connector *connector, bool force)
{
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct drm_encoder *encoder;
	struct drm_encoder_helper_funcs *encoder_funcs;
	bool dret = false;
	enum drm_connector_status ret = connector_status_disconnected;

	encoder = radeon_best_single_encoder(connector);
	if (!encoder)
		ret = connector_status_disconnected;

	if (radeon_connector->ddc_bus)
		dret = radeon_ddc_probe(radeon_connector,
					radeon_connector->requires_extended_probe);
	if (dret) {
		radeon_connector->detected_by_load = false;
		if (radeon_connector->edid) {
			kfree(radeon_connector->edid);
			radeon_connector->edid = NULL;
		}
		radeon_connector->edid = drm_get_edid(&radeon_connector->base, &radeon_connector->ddc_bus->adapter);

		if (!radeon_connector->edid) {
			DRM_ERROR("%s: probed a monitor but no|invalid EDID\n",
					drm_get_connector_name(connector));
			ret = connector_status_connected;
		} else {
			radeon_connector->use_digital = !!(radeon_connector->edid->input & DRM_EDID_INPUT_DIGITAL);

			/* some oems have boards with separate digital and analog connectors
			 * with a shared ddc line (often vga + hdmi)
			 */
			if (radeon_connector->use_digital && radeon_connector->shared_ddc) {
				kfree(radeon_connector->edid);
				radeon_connector->edid = NULL;
				ret = connector_status_disconnected;
			} else
				ret = connector_status_connected;
		}
	} else {

		/* if we aren't forcing don't do destructive polling */
		if (!force) {
			/* only return the previous status if we last
			 * detected a monitor via load.
			 */
			if (radeon_connector->detected_by_load)
				return connector->status;
			else
				return ret;
		}

		if (radeon_connector->dac_load_detect && encoder) {
			encoder_funcs = encoder->helper_private;
			ret = encoder_funcs->detect(encoder, connector);
			if (ret != connector_status_disconnected)
				radeon_connector->detected_by_load = true;
		}
	}

	if (ret == connector_status_connected)
		ret = radeon_connector_analog_encoder_conflict_solve(connector, encoder, ret, true);

	/* RN50 and some RV100 asics in servers often have a hardcoded EDID in the
	 * vbios to deal with KVMs. If we have one and are not able to detect a monitor
	 * by other means, assume the CRT is connected and use that EDID.
	 */
	if ((!rdev->is_atom_bios) &&
	    (ret == connector_status_disconnected) &&
	    rdev->mode_info.bios_hardcoded_edid_size) {
		ret = connector_status_connected;
	}

	radeon_connector_update_scratch_regs(connector, ret);
	return ret;
}

struct drm_connector_helper_funcs radeon_vga_connector_helper_funcs = {
	.get_modes = radeon_vga_get_modes,
	.mode_valid = radeon_vga_mode_valid,
	.best_encoder = radeon_best_single_encoder,
};

struct drm_connector_funcs radeon_vga_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = radeon_vga_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = radeon_connector_destroy,
	.set_property = radeon_connector_set_property,
};

static int radeon_tv_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct drm_display_mode *tv_mode;
	struct drm_encoder *encoder;

	encoder = radeon_best_single_encoder(connector);
	if (!encoder)
		return 0;

	/* avivo chips can scale any mode */
	if (rdev->family >= CHIP_RS600)
		/* add scaled modes */
		radeon_add_common_modes(encoder, connector);
	else {
		/* only 800x600 is supported right now on pre-avivo chips */
		tv_mode = drm_cvt_mode(dev, 800, 600, 60, false, false, false);
		tv_mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, tv_mode);
	}
	return 1;
}

static int radeon_tv_mode_valid(struct drm_connector *connector,
				struct drm_display_mode *mode)
{
	if ((mode->hdisplay > 1024) || (mode->vdisplay > 768))
		return MODE_CLOCK_RANGE;
	return MODE_OK;
}

static enum drm_connector_status
radeon_tv_detect(struct drm_connector *connector, bool force)
{
	struct drm_encoder *encoder;
	struct drm_encoder_helper_funcs *encoder_funcs;
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	enum drm_connector_status ret = connector_status_disconnected;

	if (!radeon_connector->dac_load_detect)
		return ret;

	encoder = radeon_best_single_encoder(connector);
	if (!encoder)
		ret = connector_status_disconnected;
	else {
		encoder_funcs = encoder->helper_private;
		ret = encoder_funcs->detect(encoder, connector);
	}
	if (ret == connector_status_connected)
		ret = radeon_connector_analog_encoder_conflict_solve(connector, encoder, ret, false);
	radeon_connector_update_scratch_regs(connector, ret);
	return ret;
}

struct drm_connector_helper_funcs radeon_tv_connector_helper_funcs = {
	.get_modes = radeon_tv_get_modes,
	.mode_valid = radeon_tv_mode_valid,
	.best_encoder = radeon_best_single_encoder,
};

struct drm_connector_funcs radeon_tv_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = radeon_tv_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = radeon_connector_destroy,
	.set_property = radeon_connector_set_property,
};

static int radeon_dvi_get_modes(struct drm_connector *connector)
{
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	int ret;

	ret = radeon_ddc_get_modes(radeon_connector);
	return ret;
}

/*
 * DVI is complicated
 * Do a DDC probe, if DDC probe passes, get the full EDID so
 * we can do analog/digital monitor detection at this point.
 * If the monitor is an analog monitor or we got no DDC,
 * we need to find the DAC encoder object for this connector.
 * If we got no DDC, we do load detection on the DAC encoder object.
 * If we got analog DDC or load detection passes on the DAC encoder
 * we have to check if this analog encoder is shared with anyone else (TV)
 * if its shared we have to set the other connector to disconnected.
 */
static enum drm_connector_status
radeon_dvi_detect(struct drm_connector *connector, bool force)
{
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct drm_encoder *encoder = NULL;
	struct drm_encoder_helper_funcs *encoder_funcs;
	struct drm_mode_object *obj;
	int i;
	enum drm_connector_status ret = connector_status_disconnected;
	bool dret = false;

	if (radeon_connector->ddc_bus)
		dret = radeon_ddc_probe(radeon_connector,
					radeon_connector->requires_extended_probe);
	if (dret) {
		radeon_connector->detected_by_load = false;
		if (radeon_connector->edid) {
			kfree(radeon_connector->edid);
			radeon_connector->edid = NULL;
		}
		radeon_connector->edid = drm_get_edid(&radeon_connector->base, &radeon_connector->ddc_bus->adapter);

		if (!radeon_connector->edid) {
			DRM_ERROR("%s: probed a monitor but no|invalid EDID\n",
					drm_get_connector_name(connector));
			/* rs690 seems to have a problem with connectors not existing and always
			 * return a block of 0's. If we see this just stop polling on this output */
			if ((rdev->family == CHIP_RS690 || rdev->family == CHIP_RS740) && radeon_connector->base.null_edid_counter) {
				ret = connector_status_disconnected;
				DRM_ERROR("%s: detected RS690 floating bus bug, stopping ddc detect\n", drm_get_connector_name(connector));
				radeon_connector->ddc_bus = NULL;
			}
		} else {
			radeon_connector->use_digital = !!(radeon_connector->edid->input & DRM_EDID_INPUT_DIGITAL);

			/* some oems have boards with separate digital and analog connectors
			 * with a shared ddc line (often vga + hdmi)
			 */
			if ((!radeon_connector->use_digital) && radeon_connector->shared_ddc) {
				kfree(radeon_connector->edid);
				radeon_connector->edid = NULL;
				ret = connector_status_disconnected;
			} else
				ret = connector_status_connected;

			/* This gets complicated.  We have boards with VGA + HDMI with a
			 * shared DDC line and we have boards with DVI-D + HDMI with a shared
			 * DDC line.  The latter is more complex because with DVI<->HDMI adapters
			 * you don't really know what's connected to which port as both are digital.
			 */
			if (radeon_connector->shared_ddc && (ret == connector_status_connected)) {
				struct drm_connector *list_connector;
				struct radeon_connector *list_radeon_connector;
				list_for_each_entry(list_connector, &dev->mode_config.connector_list, head) {
					if (connector == list_connector)
						continue;
					list_radeon_connector = to_radeon_connector(list_connector);
					if (list_radeon_connector->shared_ddc &&
					    (list_radeon_connector->ddc_bus->rec.i2c_id ==
					     radeon_connector->ddc_bus->rec.i2c_id)) {
						/* cases where both connectors are digital */
						if (list_connector->connector_type != DRM_MODE_CONNECTOR_VGA) {
							/* hpd is our only option in this case */
							if (!radeon_hpd_sense(rdev, radeon_connector->hpd.hpd)) {
								kfree(radeon_connector->edid);
								radeon_connector->edid = NULL;
								ret = connector_status_disconnected;
							}
						}
					}
				}
			}
		}
	}

	if ((ret == connector_status_connected) && (radeon_connector->use_digital == true))
		goto out;

	/* DVI-D and HDMI-A are digital only */
	if ((connector->connector_type == DRM_MODE_CONNECTOR_DVID) ||
	    (connector->connector_type == DRM_MODE_CONNECTOR_HDMIA))
		goto out;

	/* if we aren't forcing don't do destructive polling */
	if (!force) {
		/* only return the previous status if we last
		 * detected a monitor via load.
		 */
		if (radeon_connector->detected_by_load)
			ret = connector->status;
		goto out;
	}

	/* find analog encoder */
	if (radeon_connector->dac_load_detect) {
		for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
			if (connector->encoder_ids[i] == 0)
				break;

			obj = drm_mode_object_find(connector->dev,
						   connector->encoder_ids[i],
						   DRM_MODE_OBJECT_ENCODER);
			if (!obj)
				continue;

			encoder = obj_to_encoder(obj);

			if (encoder->encoder_type != DRM_MODE_ENCODER_DAC &&
			    encoder->encoder_type != DRM_MODE_ENCODER_TVDAC)
				continue;

			encoder_funcs = encoder->helper_private;
			if (encoder_funcs->detect) {
				if (ret != connector_status_connected) {
					ret = encoder_funcs->detect(encoder, connector);
					if (ret == connector_status_connected) {
						radeon_connector->use_digital = false;
					}
					if (ret != connector_status_disconnected)
						radeon_connector->detected_by_load = true;
				}
				break;
			}
		}
	}

	if ((ret == connector_status_connected) && (radeon_connector->use_digital == false) &&
	    encoder) {
		ret = radeon_connector_analog_encoder_conflict_solve(connector, encoder, ret, true);
	}

	/* RN50 and some RV100 asics in servers often have a hardcoded EDID in the
	 * vbios to deal with KVMs. If we have one and are not able to detect a monitor
	 * by other means, assume the DFP is connected and use that EDID.  In most
	 * cases the DVI port is actually a virtual KVM port connected to the service
	 * processor.
	 */
out:
	if ((!rdev->is_atom_bios) &&
	    (ret == connector_status_disconnected) &&
	    rdev->mode_info.bios_hardcoded_edid_size) {
		radeon_connector->use_digital = true;
		ret = connector_status_connected;
	}

	/* updated in get modes as well since we need to know if it's analog or digital */
	radeon_connector_update_scratch_regs(connector, ret);
	return ret;
}

/* okay need to be smart in here about which encoder to pick */
struct drm_encoder *radeon_dvi_encoder(struct drm_connector *connector)
{
	int enc_id = connector->encoder_ids[0];
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;
	int i;
	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		if (connector->encoder_ids[i] == 0)
			break;

		obj = drm_mode_object_find(connector->dev, connector->encoder_ids[i], DRM_MODE_OBJECT_ENCODER);
		if (!obj)
			continue;

		encoder = obj_to_encoder(obj);

		if (radeon_connector->use_digital == true) {
			if (encoder->encoder_type == DRM_MODE_ENCODER_TMDS)
				return encoder;
		} else {
			if (encoder->encoder_type == DRM_MODE_ENCODER_DAC ||
			    encoder->encoder_type == DRM_MODE_ENCODER_TVDAC)
				return encoder;
		}
	}

	/* see if we have a default encoder  TODO */

	/* then check use digitial */
	/* pick the first one */
	if (enc_id) {
		obj = drm_mode_object_find(connector->dev, enc_id, DRM_MODE_OBJECT_ENCODER);
		if (!obj)
			return NULL;
		encoder = obj_to_encoder(obj);
		return encoder;
	}
	return NULL;
}

static void radeon_dvi_force(struct drm_connector *connector)
{
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	if (connector->force == DRM_FORCE_ON)
		radeon_connector->use_digital = false;
	if (connector->force == DRM_FORCE_ON_DIGITAL)
		radeon_connector->use_digital = true;
}

static int radeon_dvi_mode_valid(struct drm_connector *connector,
				  struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);

	/* XXX check mode bandwidth */

	/* clocks over 135 MHz have heat issues with DVI on RV100 */
	if (radeon_connector->use_digital &&
	    (rdev->family == CHIP_RV100) &&
	    (mode->clock > 135000))
		return MODE_CLOCK_HIGH;

	if (radeon_connector->use_digital && (mode->clock > 165000)) {
		if ((radeon_connector->connector_object_id == CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_I) ||
		    (radeon_connector->connector_object_id == CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_D) ||
		    (radeon_connector->connector_object_id == CONNECTOR_OBJECT_ID_HDMI_TYPE_B))
			return MODE_OK;
		else if (radeon_connector->connector_object_id == CONNECTOR_OBJECT_ID_HDMI_TYPE_A) {
			if (ASIC_IS_DCE3(rdev)) {
				/* HDMI 1.3+ supports max clock of 340 Mhz */
				if (mode->clock > 340000)
					return MODE_CLOCK_HIGH;
				else
					return MODE_OK;
			} else
				return MODE_CLOCK_HIGH;
		} else
			return MODE_CLOCK_HIGH;
	}

	/* check against the max pixel clock */
	if ((mode->clock / 10) > rdev->clock.max_pixel_clock)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

struct drm_connector_helper_funcs radeon_dvi_connector_helper_funcs = {
	.get_modes = radeon_dvi_get_modes,
	.mode_valid = radeon_dvi_mode_valid,
	.best_encoder = radeon_dvi_encoder,
};

struct drm_connector_funcs radeon_dvi_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = radeon_dvi_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = radeon_connector_set_property,
	.destroy = radeon_connector_destroy,
	.force = radeon_dvi_force,
};

static void radeon_dp_connector_destroy(struct drm_connector *connector)
{
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct radeon_connector_atom_dig *radeon_dig_connector = radeon_connector->con_priv;

	if (radeon_connector->edid)
		kfree(radeon_connector->edid);
	if (radeon_dig_connector->dp_i2c_bus)
		radeon_i2c_destroy(radeon_dig_connector->dp_i2c_bus);
	kfree(radeon_connector->con_priv);
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static int radeon_dp_get_modes(struct drm_connector *connector)
{
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct radeon_connector_atom_dig *radeon_dig_connector = radeon_connector->con_priv;
	struct drm_encoder *encoder = radeon_best_single_encoder(connector);
	int ret;

	if ((connector->connector_type == DRM_MODE_CONNECTOR_eDP) ||
	    (connector->connector_type == DRM_MODE_CONNECTOR_LVDS)) {
		struct drm_display_mode *mode;

		if (!radeon_dig_connector->edp_on)
			atombios_set_edp_panel_power(connector,
						     ATOM_TRANSMITTER_ACTION_POWER_ON);
		ret = radeon_ddc_get_modes(radeon_connector);
		if (!radeon_dig_connector->edp_on)
			atombios_set_edp_panel_power(connector,
						     ATOM_TRANSMITTER_ACTION_POWER_OFF);

		if (ret > 0) {
			if (encoder) {
				radeon_fixup_lvds_native_mode(encoder, connector);
				/* add scaled modes */
				radeon_add_common_modes(encoder, connector);
			}
			return ret;
		}

		encoder = radeon_best_single_encoder(connector);
		if (!encoder)
			return 0;

		/* we have no EDID modes */
		mode = radeon_fp_native_mode(encoder);
		if (mode) {
			ret = 1;
			drm_mode_probed_add(connector, mode);
			/* add the width/height from vbios tables if available */
			connector->display_info.width_mm = mode->width_mm;
			connector->display_info.height_mm = mode->height_mm;
			/* add scaled modes */
			radeon_add_common_modes(encoder, connector);
		}
	} else {
		/* need to setup ddc on the bridge */
		if (radeon_connector_encoder_is_dp_bridge(connector)) {
			if (encoder)
				radeon_atom_ext_encoder_setup_ddc(encoder);
		}
		ret = radeon_ddc_get_modes(radeon_connector);
	}

	return ret;
}

bool radeon_connector_encoder_is_dp_bridge(struct drm_connector *connector)
{
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;
	struct radeon_encoder *radeon_encoder;
	int i;
	bool found = false;

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		if (connector->encoder_ids[i] == 0)
			break;

		obj = drm_mode_object_find(connector->dev, connector->encoder_ids[i], DRM_MODE_OBJECT_ENCODER);
		if (!obj)
			continue;

		encoder = obj_to_encoder(obj);
		radeon_encoder = to_radeon_encoder(encoder);

		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_TRAVIS:
		case ENCODER_OBJECT_ID_NUTMEG:
			found = true;
			break;
		default:
			break;
		}
	}

	return found;
}

bool radeon_connector_encoder_is_hbr2(struct drm_connector *connector)
{
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;
	struct radeon_encoder *radeon_encoder;
	int i;
	bool found = false;

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		if (connector->encoder_ids[i] == 0)
			break;

		obj = drm_mode_object_find(connector->dev, connector->encoder_ids[i], DRM_MODE_OBJECT_ENCODER);
		if (!obj)
			continue;

		encoder = obj_to_encoder(obj);
		radeon_encoder = to_radeon_encoder(encoder);
		if (radeon_encoder->caps & ATOM_ENCODER_CAP_RECORD_HBR2)
			found = true;
	}

	return found;
}

bool radeon_connector_is_dp12_capable(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;

	if (ASIC_IS_DCE5(rdev) &&
	    (rdev->clock.dp_extclk >= 53900) &&
	    radeon_connector_encoder_is_hbr2(connector)) {
		return true;
	}

	return false;
}

static enum drm_connector_status
radeon_dp_detect(struct drm_connector *connector, bool force)
{
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	enum drm_connector_status ret = connector_status_disconnected;
	struct radeon_connector_atom_dig *radeon_dig_connector = radeon_connector->con_priv;
	struct drm_encoder *encoder = radeon_best_single_encoder(connector);

	if (radeon_connector->edid) {
		kfree(radeon_connector->edid);
		radeon_connector->edid = NULL;
	}

	if ((connector->connector_type == DRM_MODE_CONNECTOR_eDP) ||
	    (connector->connector_type == DRM_MODE_CONNECTOR_LVDS)) {
		if (encoder) {
			struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
			struct drm_display_mode *native_mode = &radeon_encoder->native_mode;

			/* check if panel is valid */
			if (native_mode->hdisplay >= 320 && native_mode->vdisplay >= 240)
				ret = connector_status_connected;
		}
		/* eDP is always DP */
		radeon_dig_connector->dp_sink_type = CONNECTOR_OBJECT_ID_DISPLAYPORT;
		if (!radeon_dig_connector->edp_on)
			atombios_set_edp_panel_power(connector,
						     ATOM_TRANSMITTER_ACTION_POWER_ON);
		if (radeon_dp_getdpcd(radeon_connector))
			ret = connector_status_connected;
		if (!radeon_dig_connector->edp_on)
			atombios_set_edp_panel_power(connector,
						     ATOM_TRANSMITTER_ACTION_POWER_OFF);
	} else {
		/* need to setup ddc on the bridge */
		if (radeon_connector_encoder_is_dp_bridge(connector)) {
			if (encoder)
				radeon_atom_ext_encoder_setup_ddc(encoder);
		}
		radeon_dig_connector->dp_sink_type = radeon_dp_getsinktype(radeon_connector);
		if (radeon_hpd_sense(rdev, radeon_connector->hpd.hpd)) {
			ret = connector_status_connected;
			if (radeon_dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_DISPLAYPORT)
				radeon_dp_getdpcd(radeon_connector);
		} else {
			if (radeon_dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_DISPLAYPORT) {
				if (radeon_dp_getdpcd(radeon_connector))
					ret = connector_status_connected;
			} else {
				if (radeon_ddc_probe(radeon_connector,
						     radeon_connector->requires_extended_probe))
					ret = connector_status_connected;
			}
		}

		if ((ret == connector_status_disconnected) &&
		    radeon_connector->dac_load_detect) {
			struct drm_encoder *encoder = radeon_best_single_encoder(connector);
			struct drm_encoder_helper_funcs *encoder_funcs;
			if (encoder) {
				encoder_funcs = encoder->helper_private;
				ret = encoder_funcs->detect(encoder, connector);
			}
		}
	}

	radeon_connector_update_scratch_regs(connector, ret);
	return ret;
}

static int radeon_dp_mode_valid(struct drm_connector *connector,
				  struct drm_display_mode *mode)
{
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct radeon_connector_atom_dig *radeon_dig_connector = radeon_connector->con_priv;

	/* XXX check mode bandwidth */

	if ((connector->connector_type == DRM_MODE_CONNECTOR_eDP) ||
	    (connector->connector_type == DRM_MODE_CONNECTOR_LVDS)) {
		struct drm_encoder *encoder = radeon_best_single_encoder(connector);

		if ((mode->hdisplay < 320) || (mode->vdisplay < 240))
			return MODE_PANEL;

		if (encoder) {
			struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
			struct drm_display_mode *native_mode = &radeon_encoder->native_mode;

			/* AVIVO hardware supports downscaling modes larger than the panel
			 * to the panel size, but I'm not sure this is desirable.
			 */
			if ((mode->hdisplay > native_mode->hdisplay) ||
			    (mode->vdisplay > native_mode->vdisplay))
				return MODE_PANEL;

			/* if scaling is disabled, block non-native modes */
			if (radeon_encoder->rmx_type == RMX_OFF) {
				if ((mode->hdisplay != native_mode->hdisplay) ||
				    (mode->vdisplay != native_mode->vdisplay))
					return MODE_PANEL;
			}
		}
		return MODE_OK;
	} else {
		if ((radeon_dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_DISPLAYPORT) ||
		    (radeon_dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_eDP))
			return radeon_dp_mode_valid_helper(connector, mode);
		else
			return MODE_OK;
	}
}

struct drm_connector_helper_funcs radeon_dp_connector_helper_funcs = {
	.get_modes = radeon_dp_get_modes,
	.mode_valid = radeon_dp_mode_valid,
	.best_encoder = radeon_dvi_encoder,
};

struct drm_connector_funcs radeon_dp_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = radeon_dp_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = radeon_connector_set_property,
	.destroy = radeon_dp_connector_destroy,
	.force = radeon_dvi_force,
};

void
radeon_add_atom_connector(struct drm_device *dev,
			  uint32_t connector_id,
			  uint32_t supported_device,
			  int connector_type,
			  struct radeon_i2c_bus_rec *i2c_bus,
			  uint32_t igp_lane_info,
			  uint16_t connector_object_id,
			  struct radeon_hpd *hpd,
			  struct radeon_router *router)
{
	struct radeon_device *rdev = dev->dev_private;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;
	struct radeon_connector_atom_dig *radeon_dig_connector;
	struct drm_encoder *encoder;
	struct radeon_encoder *radeon_encoder;
	uint32_t subpixel_order = SubPixelNone;
	bool shared_ddc = false;
	bool is_dp_bridge = false;

	if (connector_type == DRM_MODE_CONNECTOR_Unknown)
		return;

	/* if the user selected tv=0 don't try and add the connector */
	if (((connector_type == DRM_MODE_CONNECTOR_SVIDEO) ||
	     (connector_type == DRM_MODE_CONNECTOR_Composite) ||
	     (connector_type == DRM_MODE_CONNECTOR_9PinDIN)) &&
	    (radeon_tv == 0))
		return;

	/* see if we already added it */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		radeon_connector = to_radeon_connector(connector);
		if (radeon_connector->connector_id == connector_id) {
			radeon_connector->devices |= supported_device;
			return;
		}
		if (radeon_connector->ddc_bus && i2c_bus->valid) {
			if (radeon_connector->ddc_bus->rec.i2c_id == i2c_bus->i2c_id) {
				radeon_connector->shared_ddc = true;
				shared_ddc = true;
			}
			if (radeon_connector->router_bus && router->ddc_valid &&
			    (radeon_connector->router.router_id == router->router_id)) {
				radeon_connector->shared_ddc = false;
				shared_ddc = false;
			}
		}
	}

	/* check if it's a dp bridge */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		radeon_encoder = to_radeon_encoder(encoder);
		if (radeon_encoder->devices & supported_device) {
			switch (radeon_encoder->encoder_id) {
			case ENCODER_OBJECT_ID_TRAVIS:
			case ENCODER_OBJECT_ID_NUTMEG:
				is_dp_bridge = true;
				break;
			default:
				break;
			}
		}
	}

	radeon_connector = kzalloc(sizeof(struct radeon_connector), GFP_KERNEL);
	if (!radeon_connector)
		return;

	connector = &radeon_connector->base;

	radeon_connector->connector_id = connector_id;
	radeon_connector->devices = supported_device;
	radeon_connector->shared_ddc = shared_ddc;
	radeon_connector->connector_object_id = connector_object_id;
	radeon_connector->hpd = *hpd;
	radeon_connector->requires_extended_probe =
		radeon_connector_needs_extended_probe(rdev, supported_device,
							connector_type);
	radeon_connector->router = *router;
	if (router->ddc_valid || router->cd_valid) {
		radeon_connector->router_bus = radeon_i2c_lookup(rdev, &router->i2c_info);
		if (!radeon_connector->router_bus)
			DRM_ERROR("Failed to assign router i2c bus! Check dmesg for i2c errors.\n");
	}

	if (is_dp_bridge) {
		radeon_dig_connector = kzalloc(sizeof(struct radeon_connector_atom_dig), GFP_KERNEL);
		if (!radeon_dig_connector)
			goto failed;
		radeon_dig_connector->igp_lane_info = igp_lane_info;
		radeon_connector->con_priv = radeon_dig_connector;
		drm_connector_init(dev, &radeon_connector->base, &radeon_dp_connector_funcs, connector_type);
		drm_connector_helper_add(&radeon_connector->base, &radeon_dp_connector_helper_funcs);
		if (i2c_bus->valid) {
			/* add DP i2c bus */
			if (connector_type == DRM_MODE_CONNECTOR_eDP)
				radeon_dig_connector->dp_i2c_bus = radeon_i2c_create_dp(dev, i2c_bus, "eDP-auxch");
			else
				radeon_dig_connector->dp_i2c_bus = radeon_i2c_create_dp(dev, i2c_bus, "DP-auxch");
			if (!radeon_dig_connector->dp_i2c_bus)
				DRM_ERROR("DP: Failed to assign dp ddc bus! Check dmesg for i2c errors.\n");
			radeon_connector->ddc_bus = radeon_i2c_lookup(rdev, i2c_bus);
			if (!radeon_connector->ddc_bus)
				DRM_ERROR("DP: Failed to assign ddc bus! Check dmesg for i2c errors.\n");
		}
		switch (connector_type) {
		case DRM_MODE_CONNECTOR_VGA:
		case DRM_MODE_CONNECTOR_DVIA:
		default:
			connector->interlace_allowed = true;
			connector->doublescan_allowed = true;
			radeon_connector->dac_load_detect = true;
			drm_connector_attach_property(&radeon_connector->base,
						      rdev->mode_info.load_detect_property,
						      1);
			break;
		case DRM_MODE_CONNECTOR_DVII:
		case DRM_MODE_CONNECTOR_DVID:
		case DRM_MODE_CONNECTOR_HDMIA:
		case DRM_MODE_CONNECTOR_HDMIB:
		case DRM_MODE_CONNECTOR_DisplayPort:
			drm_connector_attach_property(&radeon_connector->base,
						      rdev->mode_info.underscan_property,
						      UNDERSCAN_OFF);
			drm_connector_attach_property(&radeon_connector->base,
						      rdev->mode_info.underscan_hborder_property,
						      0);
			drm_connector_attach_property(&radeon_connector->base,
						      rdev->mode_info.underscan_vborder_property,
						      0);
			subpixel_order = SubPixelHorizontalRGB;
			connector->interlace_allowed = true;
			if (connector_type == DRM_MODE_CONNECTOR_HDMIB)
				connector->doublescan_allowed = true;
			else
				connector->doublescan_allowed = false;
			if (connector_type == DRM_MODE_CONNECTOR_DVII) {
				radeon_connector->dac_load_detect = true;
				drm_connector_attach_property(&radeon_connector->base,
							      rdev->mode_info.load_detect_property,
							      1);
			}
			break;
		case DRM_MODE_CONNECTOR_LVDS:
		case DRM_MODE_CONNECTOR_eDP:
			drm_connector_attach_property(&radeon_connector->base,
						      dev->mode_config.scaling_mode_property,
						      DRM_MODE_SCALE_FULLSCREEN);
			subpixel_order = SubPixelHorizontalRGB;
			connector->interlace_allowed = false;
			connector->doublescan_allowed = false;
			break;
		}
	} else {
		switch (connector_type) {
		case DRM_MODE_CONNECTOR_VGA:
			drm_connector_init(dev, &radeon_connector->base, &radeon_vga_connector_funcs, connector_type);
			drm_connector_helper_add(&radeon_connector->base, &radeon_vga_connector_helper_funcs);
			if (i2c_bus->valid) {
				radeon_connector->ddc_bus = radeon_i2c_lookup(rdev, i2c_bus);
				if (!radeon_connector->ddc_bus)
					DRM_ERROR("VGA: Failed to assign ddc bus! Check dmesg for i2c errors.\n");
			}
			radeon_connector->dac_load_detect = true;
			drm_connector_attach_property(&radeon_connector->base,
						      rdev->mode_info.load_detect_property,
						      1);
			/* no HPD on analog connectors */
			radeon_connector->hpd.hpd = RADEON_HPD_NONE;
			connector->polled = DRM_CONNECTOR_POLL_CONNECT;
			connector->interlace_allowed = true;
			connector->doublescan_allowed = true;
			break;
		case DRM_MODE_CONNECTOR_DVIA:
			drm_connector_init(dev, &radeon_connector->base, &radeon_vga_connector_funcs, connector_type);
			drm_connector_helper_add(&radeon_connector->base, &radeon_vga_connector_helper_funcs);
			if (i2c_bus->valid) {
				radeon_connector->ddc_bus = radeon_i2c_lookup(rdev, i2c_bus);
				if (!radeon_connector->ddc_bus)
					DRM_ERROR("DVIA: Failed to assign ddc bus! Check dmesg for i2c errors.\n");
			}
			radeon_connector->dac_load_detect = true;
			drm_connector_attach_property(&radeon_connector->base,
						      rdev->mode_info.load_detect_property,
						      1);
			/* no HPD on analog connectors */
			radeon_connector->hpd.hpd = RADEON_HPD_NONE;
			connector->interlace_allowed = true;
			connector->doublescan_allowed = true;
			break;
		case DRM_MODE_CONNECTOR_DVII:
		case DRM_MODE_CONNECTOR_DVID:
			radeon_dig_connector = kzalloc(sizeof(struct radeon_connector_atom_dig), GFP_KERNEL);
			if (!radeon_dig_connector)
				goto failed;
			radeon_dig_connector->igp_lane_info = igp_lane_info;
			radeon_connector->con_priv = radeon_dig_connector;
			drm_connector_init(dev, &radeon_connector->base, &radeon_dvi_connector_funcs, connector_type);
			drm_connector_helper_add(&radeon_connector->base, &radeon_dvi_connector_helper_funcs);
			if (i2c_bus->valid) {
				radeon_connector->ddc_bus = radeon_i2c_lookup(rdev, i2c_bus);
				if (!radeon_connector->ddc_bus)
					DRM_ERROR("DVI: Failed to assign ddc bus! Check dmesg for i2c errors.\n");
			}
			subpixel_order = SubPixelHorizontalRGB;
			drm_connector_attach_property(&radeon_connector->base,
						      rdev->mode_info.coherent_mode_property,
						      1);
			if (ASIC_IS_AVIVO(rdev)) {
				drm_connector_attach_property(&radeon_connector->base,
							      rdev->mode_info.underscan_property,
							      UNDERSCAN_OFF);
				drm_connector_attach_property(&radeon_connector->base,
							      rdev->mode_info.underscan_hborder_property,
							      0);
				drm_connector_attach_property(&radeon_connector->base,
							      rdev->mode_info.underscan_vborder_property,
							      0);
			}
			if (connector_type == DRM_MODE_CONNECTOR_DVII) {
				radeon_connector->dac_load_detect = true;
				drm_connector_attach_property(&radeon_connector->base,
							      rdev->mode_info.load_detect_property,
							      1);
			}
			connector->interlace_allowed = true;
			if (connector_type == DRM_MODE_CONNECTOR_DVII)
				connector->doublescan_allowed = true;
			else
				connector->doublescan_allowed = false;
			break;
		case DRM_MODE_CONNECTOR_HDMIA:
		case DRM_MODE_CONNECTOR_HDMIB:
			radeon_dig_connector = kzalloc(sizeof(struct radeon_connector_atom_dig), GFP_KERNEL);
			if (!radeon_dig_connector)
				goto failed;
			radeon_dig_connector->igp_lane_info = igp_lane_info;
			radeon_connector->con_priv = radeon_dig_connector;
			drm_connector_init(dev, &radeon_connector->base, &radeon_dvi_connector_funcs, connector_type);
			drm_connector_helper_add(&radeon_connector->base, &radeon_dvi_connector_helper_funcs);
			if (i2c_bus->valid) {
				radeon_connector->ddc_bus = radeon_i2c_lookup(rdev, i2c_bus);
				if (!radeon_connector->ddc_bus)
					DRM_ERROR("HDMI: Failed to assign ddc bus! Check dmesg for i2c errors.\n");
			}
			drm_connector_attach_property(&radeon_connector->base,
						      rdev->mode_info.coherent_mode_property,
						      1);
			if (ASIC_IS_AVIVO(rdev)) {
				drm_connector_attach_property(&radeon_connector->base,
							      rdev->mode_info.underscan_property,
							      UNDERSCAN_OFF);
				drm_connector_attach_property(&radeon_connector->base,
							      rdev->mode_info.underscan_hborder_property,
							      0);
				drm_connector_attach_property(&radeon_connector->base,
							      rdev->mode_info.underscan_vborder_property,
							      0);
			}
			subpixel_order = SubPixelHorizontalRGB;
			connector->interlace_allowed = true;
			if (connector_type == DRM_MODE_CONNECTOR_HDMIB)
				connector->doublescan_allowed = true;
			else
				connector->doublescan_allowed = false;
			break;
		case DRM_MODE_CONNECTOR_DisplayPort:
			radeon_dig_connector = kzalloc(sizeof(struct radeon_connector_atom_dig), GFP_KERNEL);
			if (!radeon_dig_connector)
				goto failed;
			radeon_dig_connector->igp_lane_info = igp_lane_info;
			radeon_connector->con_priv = radeon_dig_connector;
			drm_connector_init(dev, &radeon_connector->base, &radeon_dp_connector_funcs, connector_type);
			drm_connector_helper_add(&radeon_connector->base, &radeon_dp_connector_helper_funcs);
			if (i2c_bus->valid) {
				/* add DP i2c bus */
				radeon_dig_connector->dp_i2c_bus = radeon_i2c_create_dp(dev, i2c_bus, "DP-auxch");
				if (!radeon_dig_connector->dp_i2c_bus)
					DRM_ERROR("DP: Failed to assign dp ddc bus! Check dmesg for i2c errors.\n");
				radeon_connector->ddc_bus = radeon_i2c_lookup(rdev, i2c_bus);
				if (!radeon_connector->ddc_bus)
					DRM_ERROR("DP: Failed to assign ddc bus! Check dmesg for i2c errors.\n");
			}
			subpixel_order = SubPixelHorizontalRGB;
			drm_connector_attach_property(&radeon_connector->base,
						      rdev->mode_info.coherent_mode_property,
						      1);
			if (ASIC_IS_AVIVO(rdev)) {
				drm_connector_attach_property(&radeon_connector->base,
							      rdev->mode_info.underscan_property,
							      UNDERSCAN_OFF);
				drm_connector_attach_property(&radeon_connector->base,
							      rdev->mode_info.underscan_hborder_property,
							      0);
				drm_connector_attach_property(&radeon_connector->base,
							      rdev->mode_info.underscan_vborder_property,
							      0);
			}
			connector->interlace_allowed = true;
			/* in theory with a DP to VGA converter... */
			connector->doublescan_allowed = false;
			break;
		case DRM_MODE_CONNECTOR_eDP:
			radeon_dig_connector = kzalloc(sizeof(struct radeon_connector_atom_dig), GFP_KERNEL);
			if (!radeon_dig_connector)
				goto failed;
			radeon_dig_connector->igp_lane_info = igp_lane_info;
			radeon_connector->con_priv = radeon_dig_connector;
			drm_connector_init(dev, &radeon_connector->base, &radeon_dp_connector_funcs, connector_type);
			drm_connector_helper_add(&radeon_connector->base, &radeon_dp_connector_helper_funcs);
			if (i2c_bus->valid) {
				/* add DP i2c bus */
				radeon_dig_connector->dp_i2c_bus = radeon_i2c_create_dp(dev, i2c_bus, "eDP-auxch");
				if (!radeon_dig_connector->dp_i2c_bus)
					DRM_ERROR("DP: Failed to assign dp ddc bus! Check dmesg for i2c errors.\n");
				radeon_connector->ddc_bus = radeon_i2c_lookup(rdev, i2c_bus);
				if (!radeon_connector->ddc_bus)
					DRM_ERROR("DP: Failed to assign ddc bus! Check dmesg for i2c errors.\n");
			}
			drm_connector_attach_property(&radeon_connector->base,
						      dev->mode_config.scaling_mode_property,
						      DRM_MODE_SCALE_FULLSCREEN);
			subpixel_order = SubPixelHorizontalRGB;
			connector->interlace_allowed = false;
			connector->doublescan_allowed = false;
			break;
		case DRM_MODE_CONNECTOR_SVIDEO:
		case DRM_MODE_CONNECTOR_Composite:
		case DRM_MODE_CONNECTOR_9PinDIN:
			drm_connector_init(dev, &radeon_connector->base, &radeon_tv_connector_funcs, connector_type);
			drm_connector_helper_add(&radeon_connector->base, &radeon_tv_connector_helper_funcs);
			radeon_connector->dac_load_detect = true;
			drm_connector_attach_property(&radeon_connector->base,
						      rdev->mode_info.load_detect_property,
						      1);
			drm_connector_attach_property(&radeon_connector->base,
						      rdev->mode_info.tv_std_property,
						      radeon_atombios_get_tv_info(rdev));
			/* no HPD on analog connectors */
			radeon_connector->hpd.hpd = RADEON_HPD_NONE;
			connector->interlace_allowed = false;
			connector->doublescan_allowed = false;
			break;
		case DRM_MODE_CONNECTOR_LVDS:
			radeon_dig_connector = kzalloc(sizeof(struct radeon_connector_atom_dig), GFP_KERNEL);
			if (!radeon_dig_connector)
				goto failed;
			radeon_dig_connector->igp_lane_info = igp_lane_info;
			radeon_connector->con_priv = radeon_dig_connector;
			drm_connector_init(dev, &radeon_connector->base, &radeon_lvds_connector_funcs, connector_type);
			drm_connector_helper_add(&radeon_connector->base, &radeon_lvds_connector_helper_funcs);
			if (i2c_bus->valid) {
				radeon_connector->ddc_bus = radeon_i2c_lookup(rdev, i2c_bus);
				if (!radeon_connector->ddc_bus)
					DRM_ERROR("LVDS: Failed to assign ddc bus! Check dmesg for i2c errors.\n");
			}
			drm_connector_attach_property(&radeon_connector->base,
						      dev->mode_config.scaling_mode_property,
						      DRM_MODE_SCALE_FULLSCREEN);
			subpixel_order = SubPixelHorizontalRGB;
			connector->interlace_allowed = false;
			connector->doublescan_allowed = false;
			break;
		}
	}

	if (radeon_connector->hpd.hpd == RADEON_HPD_NONE) {
		if (i2c_bus->valid)
			connector->polled = DRM_CONNECTOR_POLL_CONNECT;
	} else
		connector->polled = DRM_CONNECTOR_POLL_HPD;

	connector->display_info.subpixel_order = subpixel_order;
	drm_sysfs_connector_add(connector);
	return;

failed:
	drm_connector_cleanup(connector);
	kfree(connector);
}

void
radeon_add_legacy_connector(struct drm_device *dev,
			    uint32_t connector_id,
			    uint32_t supported_device,
			    int connector_type,
			    struct radeon_i2c_bus_rec *i2c_bus,
			    uint16_t connector_object_id,
			    struct radeon_hpd *hpd)
{
	struct radeon_device *rdev = dev->dev_private;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;
	uint32_t subpixel_order = SubPixelNone;

	if (connector_type == DRM_MODE_CONNECTOR_Unknown)
		return;

	/* if the user selected tv=0 don't try and add the connector */
	if (((connector_type == DRM_MODE_CONNECTOR_SVIDEO) ||
	     (connector_type == DRM_MODE_CONNECTOR_Composite) ||
	     (connector_type == DRM_MODE_CONNECTOR_9PinDIN)) &&
	    (radeon_tv == 0))
		return;

	/* see if we already added it */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		radeon_connector = to_radeon_connector(connector);
		if (radeon_connector->connector_id == connector_id) {
			radeon_connector->devices |= supported_device;
			return;
		}
	}

	radeon_connector = kzalloc(sizeof(struct radeon_connector), GFP_KERNEL);
	if (!radeon_connector)
		return;

	connector = &radeon_connector->base;

	radeon_connector->connector_id = connector_id;
	radeon_connector->devices = supported_device;
	radeon_connector->connector_object_id = connector_object_id;
	radeon_connector->hpd = *hpd;
	radeon_connector->requires_extended_probe =
		radeon_connector_needs_extended_probe(rdev, supported_device,
							connector_type);
	switch (connector_type) {
	case DRM_MODE_CONNECTOR_VGA:
		drm_connector_init(dev, &radeon_connector->base, &radeon_vga_connector_funcs, connector_type);
		drm_connector_helper_add(&radeon_connector->base, &radeon_vga_connector_helper_funcs);
		if (i2c_bus->valid) {
			radeon_connector->ddc_bus = radeon_i2c_lookup(rdev, i2c_bus);
			if (!radeon_connector->ddc_bus)
				DRM_ERROR("VGA: Failed to assign ddc bus! Check dmesg for i2c errors.\n");
		}
		radeon_connector->dac_load_detect = true;
		drm_connector_attach_property(&radeon_connector->base,
					      rdev->mode_info.load_detect_property,
					      1);
		/* no HPD on analog connectors */
		radeon_connector->hpd.hpd = RADEON_HPD_NONE;
		connector->polled = DRM_CONNECTOR_POLL_CONNECT;
		connector->interlace_allowed = true;
		connector->doublescan_allowed = true;
		break;
	case DRM_MODE_CONNECTOR_DVIA:
		drm_connector_init(dev, &radeon_connector->base, &radeon_vga_connector_funcs, connector_type);
		drm_connector_helper_add(&radeon_connector->base, &radeon_vga_connector_helper_funcs);
		if (i2c_bus->valid) {
			radeon_connector->ddc_bus = radeon_i2c_lookup(rdev, i2c_bus);
			if (!radeon_connector->ddc_bus)
				DRM_ERROR("DVIA: Failed to assign ddc bus! Check dmesg for i2c errors.\n");
		}
		radeon_connector->dac_load_detect = true;
		drm_connector_attach_property(&radeon_connector->base,
					      rdev->mode_info.load_detect_property,
					      1);
		/* no HPD on analog connectors */
		radeon_connector->hpd.hpd = RADEON_HPD_NONE;
		connector->interlace_allowed = true;
		connector->doublescan_allowed = true;
		break;
	case DRM_MODE_CONNECTOR_DVII:
	case DRM_MODE_CONNECTOR_DVID:
		drm_connector_init(dev, &radeon_connector->base, &radeon_dvi_connector_funcs, connector_type);
		drm_connector_helper_add(&radeon_connector->base, &radeon_dvi_connector_helper_funcs);
		if (i2c_bus->valid) {
			radeon_connector->ddc_bus = radeon_i2c_lookup(rdev, i2c_bus);
			if (!radeon_connector->ddc_bus)
				DRM_ERROR("DVI: Failed to assign ddc bus! Check dmesg for i2c errors.\n");
		}
		if (connector_type == DRM_MODE_CONNECTOR_DVII) {
			radeon_connector->dac_load_detect = true;
			drm_connector_attach_property(&radeon_connector->base,
						      rdev->mode_info.load_detect_property,
						      1);
		}
		subpixel_order = SubPixelHorizontalRGB;
		connector->interlace_allowed = true;
		if (connector_type == DRM_MODE_CONNECTOR_DVII)
			connector->doublescan_allowed = true;
		else
			connector->doublescan_allowed = false;
		break;
	case DRM_MODE_CONNECTOR_SVIDEO:
	case DRM_MODE_CONNECTOR_Composite:
	case DRM_MODE_CONNECTOR_9PinDIN:
		drm_connector_init(dev, &radeon_connector->base, &radeon_tv_connector_funcs, connector_type);
		drm_connector_helper_add(&radeon_connector->base, &radeon_tv_connector_helper_funcs);
		radeon_connector->dac_load_detect = true;
		/* RS400,RC410,RS480 chipset seems to report a lot
		 * of false positive on load detect, we haven't yet
		 * found a way to make load detect reliable on those
		 * chipset, thus just disable it for TV.
		 */
		if (rdev->family == CHIP_RS400 || rdev->family == CHIP_RS480)
			radeon_connector->dac_load_detect = false;
		drm_connector_attach_property(&radeon_connector->base,
					      rdev->mode_info.load_detect_property,
					      radeon_connector->dac_load_detect);
		drm_connector_attach_property(&radeon_connector->base,
					      rdev->mode_info.tv_std_property,
					      radeon_combios_get_tv_info(rdev));
		/* no HPD on analog connectors */
		radeon_connector->hpd.hpd = RADEON_HPD_NONE;
		connector->interlace_allowed = false;
		connector->doublescan_allowed = false;
		break;
	case DRM_MODE_CONNECTOR_LVDS:
		drm_connector_init(dev, &radeon_connector->base, &radeon_lvds_connector_funcs, connector_type);
		drm_connector_helper_add(&radeon_connector->base, &radeon_lvds_connector_helper_funcs);
		if (i2c_bus->valid) {
			radeon_connector->ddc_bus = radeon_i2c_lookup(rdev, i2c_bus);
			if (!radeon_connector->ddc_bus)
				DRM_ERROR("LVDS: Failed to assign ddc bus! Check dmesg for i2c errors.\n");
		}
		drm_connector_attach_property(&radeon_connector->base,
					      dev->mode_config.scaling_mode_property,
					      DRM_MODE_SCALE_FULLSCREEN);
		subpixel_order = SubPixelHorizontalRGB;
		connector->interlace_allowed = false;
		connector->doublescan_allowed = false;
		break;
	}

	if (radeon_connector->hpd.hpd == RADEON_HPD_NONE) {
		if (i2c_bus->valid)
			connector->polled = DRM_CONNECTOR_POLL_CONNECT;
	} else
		connector->polled = DRM_CONNECTOR_POLL_HPD;
	connector->display_info.subpixel_order = subpixel_order;
	drm_sysfs_connector_add(connector);
	if (connector_type == DRM_MODE_CONNECTOR_LVDS) {
		struct drm_encoder *drm_encoder;

		list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
			struct radeon_encoder *radeon_encoder;

			radeon_encoder = to_radeon_encoder(drm_encoder);
			if (radeon_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_LVDS)
				radeon_legacy_backlight_init(radeon_encoder, connector);
		}
	}
}
