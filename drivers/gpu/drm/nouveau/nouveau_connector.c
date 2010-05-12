/*
 * Copyright (C) 2008 Maarten Maathuis.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <acpi/button.h>

#include "drmP.h"
#include "drm_edid.h"
#include "drm_crtc_helper.h"

#include "nouveau_reg.h"
#include "nouveau_drv.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"
#include "nouveau_connector.h"
#include "nouveau_hw.h"

static inline struct drm_encoder_slave_funcs *
get_slave_funcs(struct nouveau_encoder *enc)
{
	return to_encoder_slave(to_drm_encoder(enc))->slave_funcs;
}

static struct nouveau_encoder *
find_encoder_by_type(struct drm_connector *connector, int type)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_encoder *nv_encoder;
	struct drm_mode_object *obj;
	int i, id;

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		id = connector->encoder_ids[i];
		if (!id)
			break;

		obj = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_ENCODER);
		if (!obj)
			continue;
		nv_encoder = nouveau_encoder(obj_to_encoder(obj));

		if (type == OUTPUT_ANY || nv_encoder->dcb->type == type)
			return nv_encoder;
	}

	return NULL;
}

struct nouveau_connector *
nouveau_encoder_connector_get(struct nouveau_encoder *encoder)
{
	struct drm_device *dev = to_drm_encoder(encoder)->dev;
	struct drm_connector *drm_connector;

	list_for_each_entry(drm_connector, &dev->mode_config.connector_list, head) {
		if (drm_connector->encoder == to_drm_encoder(encoder))
			return nouveau_connector(drm_connector);
	}

	return NULL;
}


static void
nouveau_connector_destroy(struct drm_connector *drm_connector)
{
	struct nouveau_connector *nv_connector =
		nouveau_connector(drm_connector);
	struct drm_device *dev;

	if (!nv_connector)
		return;

	dev = nv_connector->base.dev;
	NV_DEBUG_KMS(dev, "\n");

	kfree(nv_connector->edid);
	drm_sysfs_connector_remove(drm_connector);
	drm_connector_cleanup(drm_connector);
	kfree(drm_connector);
}

static void
nouveau_connector_ddc_prepare(struct drm_connector *connector, int *flags)
{
	struct drm_nouveau_private *dev_priv = connector->dev->dev_private;

	if (dev_priv->card_type >= NV_50)
		return;

	*flags = 0;
	if (NVLockVgaCrtcs(dev_priv->dev, false))
		*flags |= 1;
	if (nv_heads_tied(dev_priv->dev))
		*flags |= 2;

	if (*flags & 2)
		NVSetOwner(dev_priv->dev, 0); /* necessary? */
}

static void
nouveau_connector_ddc_finish(struct drm_connector *connector, int flags)
{
	struct drm_nouveau_private *dev_priv = connector->dev->dev_private;

	if (dev_priv->card_type >= NV_50)
		return;

	if (flags & 2)
		NVSetOwner(dev_priv->dev, 4);
	if (flags & 1)
		NVLockVgaCrtcs(dev_priv->dev, true);
}

static struct nouveau_i2c_chan *
nouveau_connector_ddc_detect(struct drm_connector *connector,
			     struct nouveau_encoder **pnv_encoder)
{
	struct drm_device *dev = connector->dev;
	uint8_t out_buf[] = { 0x0, 0x0}, buf[2];
	int ret, flags, i;

	struct i2c_msg msgs[] = {
		{
			.addr = 0x50,
			.flags = 0,
			.len = 1,
			.buf = out_buf,
		},
		{
			.addr = 0x50,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = buf,
		}
	};

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		struct nouveau_i2c_chan *i2c = NULL;
		struct nouveau_encoder *nv_encoder;
		struct drm_mode_object *obj;
		int id;

		id = connector->encoder_ids[i];
		if (!id)
			break;

		obj = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_ENCODER);
		if (!obj)
			continue;
		nv_encoder = nouveau_encoder(obj_to_encoder(obj));

		if (nv_encoder->dcb->i2c_index < 0xf)
			i2c = nouveau_i2c_find(dev, nv_encoder->dcb->i2c_index);
		if (!i2c)
			continue;

		nouveau_connector_ddc_prepare(connector, &flags);
		ret = i2c_transfer(&i2c->adapter, msgs, 2);
		nouveau_connector_ddc_finish(connector, flags);

		if (ret == 2) {
			*pnv_encoder = nv_encoder;
			return i2c;
		}
	}

	return NULL;
}

static void
nouveau_connector_set_encoder(struct drm_connector *connector,
			      struct nouveau_encoder *nv_encoder)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct drm_nouveau_private *dev_priv = connector->dev->dev_private;
	struct drm_device *dev = connector->dev;

	if (nv_connector->detected_encoder == nv_encoder)
		return;
	nv_connector->detected_encoder = nv_encoder;

	if (nv_encoder->dcb->type == OUTPUT_LVDS ||
	    nv_encoder->dcb->type == OUTPUT_TMDS) {
		connector->doublescan_allowed = false;
		connector->interlace_allowed = false;
	} else {
		connector->doublescan_allowed = true;
		if (dev_priv->card_type == NV_20 ||
		   (dev_priv->card_type == NV_10 &&
		    (dev->pci_device & 0x0ff0) != 0x0100 &&
		    (dev->pci_device & 0x0ff0) != 0x0150))
			/* HW is broken */
			connector->interlace_allowed = false;
		else
			connector->interlace_allowed = true;
	}

	if (nv_connector->dcb->type == DCB_CONNECTOR_DVI_I) {
		drm_connector_property_set_value(connector,
			dev->mode_config.dvi_i_subconnector_property,
			nv_encoder->dcb->type == OUTPUT_TMDS ?
			DRM_MODE_SUBCONNECTOR_DVID :
			DRM_MODE_SUBCONNECTOR_DVIA);
	}
}

static enum drm_connector_status
nouveau_connector_detect(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder = NULL;
	struct nouveau_i2c_chan *i2c;
	int type, flags;

	if (nv_connector->dcb->type == DCB_CONNECTOR_LVDS)
		nv_encoder = find_encoder_by_type(connector, OUTPUT_LVDS);
	if (nv_encoder && nv_connector->native_mode) {
		unsigned status = connector_status_connected;

#ifdef CONFIG_ACPI
		if (!nouveau_ignorelid && !acpi_lid_open())
			status = connector_status_unknown;
#endif
		nouveau_connector_set_encoder(connector, nv_encoder);
		return status;
	}

	/* Cleanup the previous EDID block. */
	if (nv_connector->edid) {
		drm_mode_connector_update_edid_property(connector, NULL);
		kfree(nv_connector->edid);
		nv_connector->edid = NULL;
	}

	i2c = nouveau_connector_ddc_detect(connector, &nv_encoder);
	if (i2c) {
		nouveau_connector_ddc_prepare(connector, &flags);
		nv_connector->edid = drm_get_edid(connector, &i2c->adapter);
		nouveau_connector_ddc_finish(connector, flags);
		drm_mode_connector_update_edid_property(connector,
							nv_connector->edid);
		if (!nv_connector->edid) {
			NV_ERROR(dev, "DDC responded, but no EDID for %s\n",
				 drm_get_connector_name(connector));
			goto detect_analog;
		}

		if (nv_encoder->dcb->type == OUTPUT_DP &&
		    !nouveau_dp_detect(to_drm_encoder(nv_encoder))) {
			NV_ERROR(dev, "Detected %s, but failed init\n",
				 drm_get_connector_name(connector));
			return connector_status_disconnected;
		}

		/* Override encoder type for DVI-I based on whether EDID
		 * says the display is digital or analog, both use the
		 * same i2c channel so the value returned from ddc_detect
		 * isn't necessarily correct.
		 */
		if (nv_connector->dcb->type == DCB_CONNECTOR_DVI_I) {
			if (nv_connector->edid->input & DRM_EDID_INPUT_DIGITAL)
				type = OUTPUT_TMDS;
			else
				type = OUTPUT_ANALOG;

			nv_encoder = find_encoder_by_type(connector, type);
			if (!nv_encoder) {
				NV_ERROR(dev, "Detected %d encoder on %s, "
					      "but no object!\n", type,
					 drm_get_connector_name(connector));
				return connector_status_disconnected;
			}
		}

		nouveau_connector_set_encoder(connector, nv_encoder);
		return connector_status_connected;
	}

detect_analog:
	nv_encoder = find_encoder_by_type(connector, OUTPUT_ANALOG);
	if (!nv_encoder && !nouveau_tv_disable)
		nv_encoder = find_encoder_by_type(connector, OUTPUT_TV);
	if (nv_encoder) {
		struct drm_encoder *encoder = to_drm_encoder(nv_encoder);
		struct drm_encoder_helper_funcs *helper =
						encoder->helper_private;

		if (helper->detect(encoder, connector) ==
						connector_status_connected) {
			nouveau_connector_set_encoder(connector, nv_encoder);
			return connector_status_connected;
		}

	}

	return connector_status_disconnected;
}

static void
nouveau_connector_force(struct drm_connector *connector)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder;
	int type;

	if (nv_connector->dcb->type == DCB_CONNECTOR_DVI_I) {
		if (connector->force == DRM_FORCE_ON_DIGITAL)
			type = OUTPUT_TMDS;
		else
			type = OUTPUT_ANALOG;
	} else
		type = OUTPUT_ANY;

	nv_encoder = find_encoder_by_type(connector, type);
	if (!nv_encoder) {
		NV_ERROR(connector->dev, "can't find encoder to force %s on!\n",
			 drm_get_connector_name(connector));
		connector->status = connector_status_disconnected;
		return;
	}

	nouveau_connector_set_encoder(connector, nv_encoder);
}

static int
nouveau_connector_set_property(struct drm_connector *connector,
			       struct drm_property *property, uint64_t value)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder = nv_connector->detected_encoder;
	struct drm_device *dev = connector->dev;
	int ret;

	/* Scaling mode */
	if (property == dev->mode_config.scaling_mode_property) {
		struct nouveau_crtc *nv_crtc = NULL;
		bool modeset = false;

		switch (value) {
		case DRM_MODE_SCALE_NONE:
		case DRM_MODE_SCALE_FULLSCREEN:
		case DRM_MODE_SCALE_CENTER:
		case DRM_MODE_SCALE_ASPECT:
			break;
		default:
			return -EINVAL;
		}

		/* LVDS always needs gpu scaling */
		if (nv_connector->dcb->type == DCB_CONNECTOR_LVDS &&
		    value == DRM_MODE_SCALE_NONE)
			return -EINVAL;

		/* Changing between GPU and panel scaling requires a full
		 * modeset
		 */
		if ((nv_connector->scaling_mode == DRM_MODE_SCALE_NONE) ||
		    (value == DRM_MODE_SCALE_NONE))
			modeset = true;
		nv_connector->scaling_mode = value;

		if (connector->encoder && connector->encoder->crtc)
			nv_crtc = nouveau_crtc(connector->encoder->crtc);
		if (!nv_crtc)
			return 0;

		if (modeset || !nv_crtc->set_scale) {
			ret = drm_crtc_helper_set_mode(&nv_crtc->base,
							&nv_crtc->base.mode,
							nv_crtc->base.x,
							nv_crtc->base.y, NULL);
			if (!ret)
				return -EINVAL;
		} else {
			ret = nv_crtc->set_scale(nv_crtc, value, true);
			if (ret)
				return ret;
		}

		return 0;
	}

	/* Dithering */
	if (property == dev->mode_config.dithering_mode_property) {
		struct nouveau_crtc *nv_crtc = NULL;

		if (value == DRM_MODE_DITHERING_ON)
			nv_connector->use_dithering = true;
		else
			nv_connector->use_dithering = false;

		if (connector->encoder && connector->encoder->crtc)
			nv_crtc = nouveau_crtc(connector->encoder->crtc);

		if (!nv_crtc || !nv_crtc->set_dither)
			return 0;

		return nv_crtc->set_dither(nv_crtc, nv_connector->use_dithering,
					   true);
	}

	if (nv_encoder && nv_encoder->dcb->type == OUTPUT_TV)
		return get_slave_funcs(nv_encoder)->
			set_property(to_drm_encoder(nv_encoder), connector, property, value);

	return -EINVAL;
}

static struct drm_display_mode *
nouveau_connector_native_mode(struct nouveau_connector *connector)
{
	struct drm_device *dev = connector->base.dev;
	struct drm_display_mode *mode, *largest = NULL;
	int high_w = 0, high_h = 0, high_v = 0;

	/* Use preferred mode if there is one.. */
	list_for_each_entry(mode, &connector->base.probed_modes, head) {
		if (mode->type & DRM_MODE_TYPE_PREFERRED) {
			NV_DEBUG_KMS(dev, "native mode from preferred\n");
			return drm_mode_duplicate(dev, mode);
		}
	}

	/* Otherwise, take the resolution with the largest width, then height,
	 * then vertical refresh
	 */
	list_for_each_entry(mode, &connector->base.probed_modes, head) {
		if (mode->hdisplay < high_w)
			continue;

		if (mode->hdisplay == high_w && mode->vdisplay < high_h)
			continue;

		if (mode->hdisplay == high_w && mode->vdisplay == high_h &&
		    mode->vrefresh < high_v)
			continue;

		high_w = mode->hdisplay;
		high_h = mode->vdisplay;
		high_v = mode->vrefresh;
		largest = mode;
	}

	NV_DEBUG_KMS(dev, "native mode from largest: %dx%d@%d\n",
		      high_w, high_h, high_v);
	return largest ? drm_mode_duplicate(dev, largest) : NULL;
}

struct moderec {
	int hdisplay;
	int vdisplay;
};

static struct moderec scaler_modes[] = {
	{ 1920, 1200 },
	{ 1920, 1080 },
	{ 1680, 1050 },
	{ 1600, 1200 },
	{ 1400, 1050 },
	{ 1280, 1024 },
	{ 1280, 960 },
	{ 1152, 864 },
	{ 1024, 768 },
	{ 800, 600 },
	{ 720, 400 },
	{ 640, 480 },
	{ 640, 400 },
	{ 640, 350 },
	{}
};

static int
nouveau_connector_scaler_modes_add(struct drm_connector *connector)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct drm_display_mode *native = nv_connector->native_mode, *m;
	struct drm_device *dev = connector->dev;
	struct moderec *mode = &scaler_modes[0];
	int modes = 0;

	if (!native)
		return 0;

	while (mode->hdisplay) {
		if (mode->hdisplay <= native->hdisplay &&
		    mode->vdisplay <= native->vdisplay) {
			m = drm_cvt_mode(dev, mode->hdisplay, mode->vdisplay,
					 drm_mode_vrefresh(native), false,
					 false, false);
			if (!m)
				continue;

			m->type |= DRM_MODE_TYPE_DRIVER;

			drm_mode_probed_add(connector, m);
			modes++;
		}

		mode++;
	}

	return modes;
}

static int
nouveau_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder = nv_connector->detected_encoder;
	int ret = 0;

	/* If we're not LVDS, destroy the previous native mode, the attached
	 * monitor could have changed.
	 */
	if (nv_connector->dcb->type != DCB_CONNECTOR_LVDS &&
	    nv_connector->native_mode) {
		drm_mode_destroy(dev, nv_connector->native_mode);
		nv_connector->native_mode = NULL;
	}

	if (nv_connector->edid)
		ret = drm_add_edid_modes(connector, nv_connector->edid);

	/* Find the native mode if this is a digital panel, if we didn't
	 * find any modes through DDC previously add the native mode to
	 * the list of modes.
	 */
	if (!nv_connector->native_mode)
		nv_connector->native_mode =
			nouveau_connector_native_mode(nv_connector);
	if (ret == 0 && nv_connector->native_mode) {
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(dev, nv_connector->native_mode);
		drm_mode_probed_add(connector, mode);
		ret = 1;
	}

	if (nv_encoder->dcb->type == OUTPUT_TV)
		ret = get_slave_funcs(nv_encoder)->
			get_modes(to_drm_encoder(nv_encoder), connector);

	if (nv_encoder->dcb->type == OUTPUT_LVDS)
		ret += nouveau_connector_scaler_modes_add(connector);

	return ret;
}

static int
nouveau_connector_mode_valid(struct drm_connector *connector,
			     struct drm_display_mode *mode)
{
	struct drm_nouveau_private *dev_priv = connector->dev->dev_private;
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder = nv_connector->detected_encoder;
	unsigned min_clock = 25000, max_clock = min_clock;
	unsigned clock = mode->clock;

	switch (nv_encoder->dcb->type) {
	case OUTPUT_LVDS:
		BUG_ON(!nv_connector->native_mode);
		if (mode->hdisplay > nv_connector->native_mode->hdisplay ||
		    mode->vdisplay > nv_connector->native_mode->vdisplay)
			return MODE_PANEL;

		min_clock = 0;
		max_clock = 400000;
		break;
	case OUTPUT_TMDS:
		if ((dev_priv->card_type >= NV_50 && !nouveau_duallink) ||
		    (dev_priv->card_type < NV_50 &&
		     !nv_encoder->dcb->duallink_possible))
			max_clock = 165000;
		else
			max_clock = 330000;
		break;
	case OUTPUT_ANALOG:
		max_clock = nv_encoder->dcb->crtconf.maxfreq;
		if (!max_clock)
			max_clock = 350000;
		break;
	case OUTPUT_TV:
		return get_slave_funcs(nv_encoder)->
			mode_valid(to_drm_encoder(nv_encoder), mode);
	case OUTPUT_DP:
		if (nv_encoder->dp.link_bw == DP_LINK_BW_2_7)
			max_clock = nv_encoder->dp.link_nr * 270000;
		else
			max_clock = nv_encoder->dp.link_nr * 162000;

		clock *= 3;
		break;
	default:
		BUG_ON(1);
		return MODE_BAD;
	}

	if (clock < min_clock)
		return MODE_CLOCK_LOW;

	if (clock > max_clock)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static struct drm_encoder *
nouveau_connector_best_encoder(struct drm_connector *connector)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);

	if (nv_connector->detected_encoder)
		return to_drm_encoder(nv_connector->detected_encoder);

	return NULL;
}

static const struct drm_connector_helper_funcs
nouveau_connector_helper_funcs = {
	.get_modes = nouveau_connector_get_modes,
	.mode_valid = nouveau_connector_mode_valid,
	.best_encoder = nouveau_connector_best_encoder,
};

static const struct drm_connector_funcs
nouveau_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.save = NULL,
	.restore = NULL,
	.detect = nouveau_connector_detect,
	.destroy = nouveau_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = nouveau_connector_set_property,
	.force = nouveau_connector_force
};

static int
nouveau_connector_create_lvds(struct drm_device *dev,
			      struct drm_connector *connector)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_i2c_chan *i2c = NULL;
	struct nouveau_encoder *nv_encoder;
	struct drm_display_mode native, *mode, *temp;
	bool dummy, if_is_24bit = false;
	int ret, flags;

	nv_encoder = find_encoder_by_type(connector, OUTPUT_LVDS);
	if (!nv_encoder)
		return -ENODEV;

	ret = nouveau_bios_parse_lvds_table(dev, 0, &dummy, &if_is_24bit);
	if (ret) {
		NV_ERROR(dev, "Error parsing LVDS table, disabling LVDS\n");
		return ret;
	}
	nv_connector->use_dithering = !if_is_24bit;

	/* Firstly try getting EDID over DDC, if allowed and I2C channel
	 * is available.
	 */
	if (!dev_priv->vbios.fp_no_ddc && nv_encoder->dcb->i2c_index < 0xf)
		i2c = nouveau_i2c_find(dev, nv_encoder->dcb->i2c_index);

	if (i2c) {
		nouveau_connector_ddc_prepare(connector, &flags);
		nv_connector->edid = drm_get_edid(connector, &i2c->adapter);
		nouveau_connector_ddc_finish(connector, flags);
	}

	/* If no EDID found above, and the VBIOS indicates a hardcoded
	 * modeline is avalilable for the panel, set it as the panel's
	 * native mode and exit.
	 */
	if (!nv_connector->edid && nouveau_bios_fp_mode(dev, &native) &&
	     (nv_encoder->dcb->lvdsconf.use_straps_for_mode ||
	      dev_priv->vbios.fp_no_ddc)) {
		nv_connector->native_mode = drm_mode_duplicate(dev, &native);
		goto out;
	}

	/* Still nothing, some VBIOS images have a hardcoded EDID block
	 * stored for the panel stored in them.
	 */
	if (!nv_connector->edid && !nv_connector->native_mode &&
	    !dev_priv->vbios.fp_no_ddc) {
		struct edid *edid =
			(struct edid *)nouveau_bios_embedded_edid(dev);
		if (edid) {
			nv_connector->edid = kmalloc(EDID_LENGTH, GFP_KERNEL);
			*(nv_connector->edid) = *edid;
		}
	}

	if (!nv_connector->edid)
		goto out;

	/* We didn't find/use a panel mode from the VBIOS, so parse the EDID
	 * block and look for the preferred mode there.
	 */
	ret = drm_add_edid_modes(connector, nv_connector->edid);
	if (ret == 0)
		goto out;
	nv_connector->detected_encoder = nv_encoder;
	nv_connector->native_mode = nouveau_connector_native_mode(nv_connector);
	list_for_each_entry_safe(mode, temp, &connector->probed_modes, head)
		drm_mode_remove(connector, mode);

out:
	if (!nv_connector->native_mode) {
		NV_ERROR(dev, "LVDS present in DCB table, but couldn't "
			      "determine its native mode.  Disabling.\n");
		return -ENODEV;
	}

	drm_mode_connector_update_edid_property(connector, nv_connector->edid);
	return 0;
}

int
nouveau_connector_create(struct drm_device *dev,
			 struct dcb_connector_table_entry *dcb)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_connector *nv_connector = NULL;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	int ret, type;

	NV_DEBUG_KMS(dev, "\n");

	switch (dcb->type) {
	case DCB_CONNECTOR_NONE:
		return 0;
	case DCB_CONNECTOR_VGA:
		NV_INFO(dev, "Detected a VGA connector\n");
		type = DRM_MODE_CONNECTOR_VGA;
		break;
	case DCB_CONNECTOR_TV_0:
	case DCB_CONNECTOR_TV_1:
	case DCB_CONNECTOR_TV_3:
		NV_INFO(dev, "Detected a TV connector\n");
		type = DRM_MODE_CONNECTOR_TV;
		break;
	case DCB_CONNECTOR_DVI_I:
		NV_INFO(dev, "Detected a DVI-I connector\n");
		type = DRM_MODE_CONNECTOR_DVII;
		break;
	case DCB_CONNECTOR_DVI_D:
		NV_INFO(dev, "Detected a DVI-D connector\n");
		type = DRM_MODE_CONNECTOR_DVID;
		break;
	case DCB_CONNECTOR_HDMI_0:
	case DCB_CONNECTOR_HDMI_1:
		NV_INFO(dev, "Detected a HDMI connector\n");
		type = DRM_MODE_CONNECTOR_HDMIA;
		break;
	case DCB_CONNECTOR_LVDS:
		NV_INFO(dev, "Detected a LVDS connector\n");
		type = DRM_MODE_CONNECTOR_LVDS;
		break;
	case DCB_CONNECTOR_DP:
		NV_INFO(dev, "Detected a DisplayPort connector\n");
		type = DRM_MODE_CONNECTOR_DisplayPort;
		break;
	case DCB_CONNECTOR_eDP:
		NV_INFO(dev, "Detected an eDP connector\n");
		type = DRM_MODE_CONNECTOR_eDP;
		break;
	default:
		NV_ERROR(dev, "unknown connector type: 0x%02x!!\n", dcb->type);
		return -EINVAL;
	}

	nv_connector = kzalloc(sizeof(*nv_connector), GFP_KERNEL);
	if (!nv_connector)
		return -ENOMEM;
	nv_connector->dcb = dcb;
	connector = &nv_connector->base;

	/* defaults, will get overridden in detect() */
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	drm_connector_init(dev, connector, &nouveau_connector_funcs, type);
	drm_connector_helper_add(connector, &nouveau_connector_helper_funcs);

	/* attach encoders */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);

		if (nv_encoder->dcb->connector != dcb->index)
			continue;

		if (get_slave_funcs(nv_encoder))
			get_slave_funcs(nv_encoder)->create_resources(encoder, connector);

		drm_mode_connector_attach_encoder(connector, encoder);
	}

	if (!connector->encoder_ids[0]) {
		NV_WARN(dev, "  no encoders, ignoring\n");
		drm_connector_cleanup(connector);
		kfree(connector);
		return 0;
	}

	/* Init DVI-I specific properties */
	if (dcb->type == DCB_CONNECTOR_DVI_I) {
		drm_mode_create_dvi_i_properties(dev);
		drm_connector_attach_property(connector, dev->mode_config.dvi_i_subconnector_property, 0);
		drm_connector_attach_property(connector, dev->mode_config.dvi_i_select_subconnector_property, 0);
	}

	if (dcb->type != DCB_CONNECTOR_LVDS)
		nv_connector->use_dithering = false;

	switch (dcb->type) {
	case DCB_CONNECTOR_VGA:
		if (dev_priv->card_type >= NV_50) {
			drm_connector_attach_property(connector,
					dev->mode_config.scaling_mode_property,
					nv_connector->scaling_mode);
		}
		/* fall-through */
	case DCB_CONNECTOR_TV_0:
	case DCB_CONNECTOR_TV_1:
	case DCB_CONNECTOR_TV_3:
		nv_connector->scaling_mode = DRM_MODE_SCALE_NONE;
		break;
	default:
		nv_connector->scaling_mode = DRM_MODE_SCALE_FULLSCREEN;

		drm_connector_attach_property(connector,
				dev->mode_config.scaling_mode_property,
				nv_connector->scaling_mode);
		drm_connector_attach_property(connector,
				dev->mode_config.dithering_mode_property,
				nv_connector->use_dithering ?
				DRM_MODE_DITHERING_ON : DRM_MODE_DITHERING_OFF);
		break;
	}

	drm_sysfs_connector_add(connector);

	if (dcb->type == DCB_CONNECTOR_LVDS) {
		ret = nouveau_connector_create_lvds(dev, connector);
		if (ret) {
			connector->funcs->destroy(connector);
			return ret;
		}
	}

	return 0;
}
