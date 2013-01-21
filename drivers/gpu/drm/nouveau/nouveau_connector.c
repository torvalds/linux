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

#include <drm/drmP.h>
#include <drm/drm_edid.h>
#include <drm/drm_crtc_helper.h>

#include "nouveau_reg.h"
#include "nouveau_drm.h"
#include "nouveau_hw.h"
#include "nouveau_acpi.h"

#include "nouveau_display.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"

#include <subdev/i2c.h>
#include <subdev/gpio.h>

MODULE_PARM_DESC(tv_disable, "Disable TV-out detection");
static int nouveau_tv_disable = 0;
module_param_named(tv_disable, nouveau_tv_disable, int, 0400);

MODULE_PARM_DESC(ignorelid, "Ignore ACPI lid status");
static int nouveau_ignorelid = 0;
module_param_named(ignorelid, nouveau_ignorelid, int, 0400);

MODULE_PARM_DESC(duallink, "Allow dual-link TMDS (default: enabled)");
static int nouveau_duallink = 1;
module_param_named(duallink, nouveau_duallink, int, 0400);

static void nouveau_connector_hotplug(void *, int);

struct nouveau_encoder *
find_encoder(struct drm_connector *connector, int type)
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

		if (type == DCB_OUTPUT_ANY || nv_encoder->dcb->type == type)
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
nouveau_connector_destroy(struct drm_connector *connector)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_gpio *gpio;
	struct nouveau_drm *drm;
	struct drm_device *dev;

	if (!nv_connector)
		return;

	dev  = nv_connector->base.dev;
	drm  = nouveau_drm(dev);
	gpio = nouveau_gpio(drm->device);

	if (gpio && nv_connector->hpd != DCB_GPIO_UNUSED) {
		gpio->isr_del(gpio, 0, nv_connector->hpd, 0xff,
			      nouveau_connector_hotplug, connector);
	}

	kfree(nv_connector->edid);
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static struct nouveau_i2c_port *
nouveau_connector_ddc_detect(struct drm_connector *connector,
			     struct nouveau_encoder **pnv_encoder)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_gpio *gpio = nouveau_gpio(drm->device);
	struct nouveau_i2c *i2c = nouveau_i2c(drm->device);
	struct nouveau_i2c_port *port = NULL;
	int i, panel = -ENODEV;

	/* eDP panels need powering on by us (if the VBIOS doesn't default it
	 * to on) before doing any AUX channel transactions.  LVDS panel power
	 * is handled by the SOR itself, and not required for LVDS DDC.
	 */
	if (nv_connector->type == DCB_CONNECTOR_eDP) {
		panel = gpio->get(gpio, 0, DCB_GPIO_PANEL_POWER, 0xff);
		if (panel == 0) {
			gpio->set(gpio, 0, DCB_GPIO_PANEL_POWER, 0xff, 1);
			msleep(300);
		}
	}

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
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
			port = i2c->find(i2c, nv_encoder->dcb->i2c_index);
		if (port && nv_probe_i2c(port, 0x50)) {
			*pnv_encoder = nv_encoder;
			break;
		}

		port = NULL;
	}

	/* eDP panel not detected, restore panel power GPIO to previous
	 * state to avoid confusing the SOR for other output types.
	 */
	if (!port && panel == 0)
		gpio->set(gpio, 0, DCB_GPIO_PANEL_POWER, 0xff, panel);

	return port;
}

static struct nouveau_encoder *
nouveau_connector_of_detect(struct drm_connector *connector)
{
#ifdef __powerpc__
	struct drm_device *dev = connector->dev;
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder;
	struct device_node *cn, *dn = pci_device_to_OF_node(dev->pdev);

	if (!dn ||
	    !((nv_encoder = find_encoder(connector, DCB_OUTPUT_TMDS)) ||
	      (nv_encoder = find_encoder(connector, DCB_OUTPUT_ANALOG))))
		return NULL;

	for_each_child_of_node(dn, cn) {
		const char *name = of_get_property(cn, "name", NULL);
		const void *edid = of_get_property(cn, "EDID", NULL);
		int idx = name ? name[strlen(name) - 1] - 'A' : 0;

		if (nv_encoder->dcb->i2c_index == idx && edid) {
			nv_connector->edid =
				kmemdup(edid, EDID_LENGTH, GFP_KERNEL);
			of_node_put(cn);
			return nv_encoder;
		}
	}
#endif
	return NULL;
}

static void
nouveau_connector_set_encoder(struct drm_connector *connector,
			      struct nouveau_encoder *nv_encoder)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct drm_device *dev = connector->dev;

	if (nv_connector->detected_encoder == nv_encoder)
		return;
	nv_connector->detected_encoder = nv_encoder;

	if (nv_device(drm->device)->card_type >= NV_50) {
		connector->interlace_allowed = true;
		connector->doublescan_allowed = true;
	} else
	if (nv_encoder->dcb->type == DCB_OUTPUT_LVDS ||
	    nv_encoder->dcb->type == DCB_OUTPUT_TMDS) {
		connector->doublescan_allowed = false;
		connector->interlace_allowed = false;
	} else {
		connector->doublescan_allowed = true;
		if (nv_device(drm->device)->card_type == NV_20 ||
		   (nv_device(drm->device)->card_type == NV_10 &&
		    (dev->pci_device & 0x0ff0) != 0x0100 &&
		    (dev->pci_device & 0x0ff0) != 0x0150))
			/* HW is broken */
			connector->interlace_allowed = false;
		else
			connector->interlace_allowed = true;
	}

	if (nv_connector->type == DCB_CONNECTOR_DVI_I) {
		drm_object_property_set_value(&connector->base,
			dev->mode_config.dvi_i_subconnector_property,
			nv_encoder->dcb->type == DCB_OUTPUT_TMDS ?
			DRM_MODE_SUBCONNECTOR_DVID :
			DRM_MODE_SUBCONNECTOR_DVIA);
	}
}

static enum drm_connector_status
nouveau_connector_detect(struct drm_connector *connector, bool force)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder = NULL;
	struct nouveau_encoder *nv_partner;
	struct nouveau_i2c_port *i2c;
	int type;

	/* Cleanup the previous EDID block. */
	if (nv_connector->edid) {
		drm_mode_connector_update_edid_property(connector, NULL);
		kfree(nv_connector->edid);
		nv_connector->edid = NULL;
	}

	i2c = nouveau_connector_ddc_detect(connector, &nv_encoder);
	if (i2c) {
		nv_connector->edid = drm_get_edid(connector, &i2c->adapter);
		drm_mode_connector_update_edid_property(connector,
							nv_connector->edid);
		if (!nv_connector->edid) {
			NV_ERROR(drm, "DDC responded, but no EDID for %s\n",
				 drm_get_connector_name(connector));
			goto detect_analog;
		}

		if (nv_encoder->dcb->type == DCB_OUTPUT_DP &&
		    !nouveau_dp_detect(to_drm_encoder(nv_encoder))) {
			NV_ERROR(drm, "Detected %s, but failed init\n",
				 drm_get_connector_name(connector));
			return connector_status_disconnected;
		}

		/* Override encoder type for DVI-I based on whether EDID
		 * says the display is digital or analog, both use the
		 * same i2c channel so the value returned from ddc_detect
		 * isn't necessarily correct.
		 */
		nv_partner = NULL;
		if (nv_encoder->dcb->type == DCB_OUTPUT_TMDS)
			nv_partner = find_encoder(connector, DCB_OUTPUT_ANALOG);
		if (nv_encoder->dcb->type == DCB_OUTPUT_ANALOG)
			nv_partner = find_encoder(connector, DCB_OUTPUT_TMDS);

		if (nv_partner && ((nv_encoder->dcb->type == DCB_OUTPUT_ANALOG &&
				    nv_partner->dcb->type == DCB_OUTPUT_TMDS) ||
				   (nv_encoder->dcb->type == DCB_OUTPUT_TMDS &&
				    nv_partner->dcb->type == DCB_OUTPUT_ANALOG))) {
			if (nv_connector->edid->input & DRM_EDID_INPUT_DIGITAL)
				type = DCB_OUTPUT_TMDS;
			else
				type = DCB_OUTPUT_ANALOG;

			nv_encoder = find_encoder(connector, type);
		}

		nouveau_connector_set_encoder(connector, nv_encoder);
		return connector_status_connected;
	}

	nv_encoder = nouveau_connector_of_detect(connector);
	if (nv_encoder) {
		nouveau_connector_set_encoder(connector, nv_encoder);
		return connector_status_connected;
	}

detect_analog:
	nv_encoder = find_encoder(connector, DCB_OUTPUT_ANALOG);
	if (!nv_encoder && !nouveau_tv_disable)
		nv_encoder = find_encoder(connector, DCB_OUTPUT_TV);
	if (nv_encoder && force) {
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

static enum drm_connector_status
nouveau_connector_detect_lvds(struct drm_connector *connector, bool force)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder = NULL;
	enum drm_connector_status status = connector_status_disconnected;

	/* Cleanup the previous EDID block. */
	if (nv_connector->edid) {
		drm_mode_connector_update_edid_property(connector, NULL);
		kfree(nv_connector->edid);
		nv_connector->edid = NULL;
	}

	nv_encoder = find_encoder(connector, DCB_OUTPUT_LVDS);
	if (!nv_encoder)
		return connector_status_disconnected;

	/* Try retrieving EDID via DDC */
	if (!drm->vbios.fp_no_ddc) {
		status = nouveau_connector_detect(connector, force);
		if (status == connector_status_connected)
			goto out;
	}

	/* On some laptops (Sony, i'm looking at you) there appears to
	 * be no direct way of accessing the panel's EDID.  The only
	 * option available to us appears to be to ask ACPI for help..
	 *
	 * It's important this check's before trying straps, one of the
	 * said manufacturer's laptops are configured in such a way
	 * the nouveau decides an entry in the VBIOS FP mode table is
	 * valid - it's not (rh#613284)
	 */
	if (nv_encoder->dcb->lvdsconf.use_acpi_for_edid) {
		if ((nv_connector->edid = nouveau_acpi_edid(dev, connector))) {
			status = connector_status_connected;
			goto out;
		}
	}

	/* If no EDID found above, and the VBIOS indicates a hardcoded
	 * modeline is avalilable for the panel, set it as the panel's
	 * native mode and exit.
	 */
	if (nouveau_bios_fp_mode(dev, NULL) && (drm->vbios.fp_no_ddc ||
	    nv_encoder->dcb->lvdsconf.use_straps_for_mode)) {
		status = connector_status_connected;
		goto out;
	}

	/* Still nothing, some VBIOS images have a hardcoded EDID block
	 * stored for the panel stored in them.
	 */
	if (!drm->vbios.fp_no_ddc) {
		struct edid *edid =
			(struct edid *)nouveau_bios_embedded_edid(dev);
		if (edid) {
			nv_connector->edid = kmalloc(EDID_LENGTH, GFP_KERNEL);
			*(nv_connector->edid) = *edid;
			status = connector_status_connected;
		}
	}

out:
#if defined(CONFIG_ACPI_BUTTON) || \
	(defined(CONFIG_ACPI_BUTTON_MODULE) && defined(MODULE))
	if (status == connector_status_connected &&
	    !nouveau_ignorelid && !acpi_lid_open())
		status = connector_status_unknown;
#endif

	drm_mode_connector_update_edid_property(connector, nv_connector->edid);
	nouveau_connector_set_encoder(connector, nv_encoder);
	return status;
}

static void
nouveau_connector_force(struct drm_connector *connector)
{
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder;
	int type;

	if (nv_connector->type == DCB_CONNECTOR_DVI_I) {
		if (connector->force == DRM_FORCE_ON_DIGITAL)
			type = DCB_OUTPUT_TMDS;
		else
			type = DCB_OUTPUT_ANALOG;
	} else
		type = DCB_OUTPUT_ANY;

	nv_encoder = find_encoder(connector, type);
	if (!nv_encoder) {
		NV_ERROR(drm, "can't find encoder to force %s on!\n",
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
	struct nouveau_display *disp = nouveau_display(connector->dev);
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder = nv_connector->detected_encoder;
	struct drm_encoder *encoder = to_drm_encoder(nv_encoder);
	struct drm_device *dev = connector->dev;
	struct nouveau_crtc *nv_crtc;
	int ret;

	nv_crtc = NULL;
	if (connector->encoder && connector->encoder->crtc)
		nv_crtc = nouveau_crtc(connector->encoder->crtc);

	/* Scaling mode */
	if (property == dev->mode_config.scaling_mode_property) {
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
		if (connector->connector_type == DRM_MODE_CONNECTOR_LVDS &&
		    value == DRM_MODE_SCALE_NONE)
			return -EINVAL;

		/* Changing between GPU and panel scaling requires a full
		 * modeset
		 */
		if ((nv_connector->scaling_mode == DRM_MODE_SCALE_NONE) ||
		    (value == DRM_MODE_SCALE_NONE))
			modeset = true;
		nv_connector->scaling_mode = value;

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
			ret = nv_crtc->set_scale(nv_crtc, true);
			if (ret)
				return ret;
		}

		return 0;
	}

	/* Underscan */
	if (property == disp->underscan_property) {
		if (nv_connector->underscan != value) {
			nv_connector->underscan = value;
			if (!nv_crtc || !nv_crtc->set_scale)
				return 0;

			return nv_crtc->set_scale(nv_crtc, true);
		}

		return 0;
	}

	if (property == disp->underscan_hborder_property) {
		if (nv_connector->underscan_hborder != value) {
			nv_connector->underscan_hborder = value;
			if (!nv_crtc || !nv_crtc->set_scale)
				return 0;

			return nv_crtc->set_scale(nv_crtc, true);
		}

		return 0;
	}

	if (property == disp->underscan_vborder_property) {
		if (nv_connector->underscan_vborder != value) {
			nv_connector->underscan_vborder = value;
			if (!nv_crtc || !nv_crtc->set_scale)
				return 0;

			return nv_crtc->set_scale(nv_crtc, true);
		}

		return 0;
	}

	/* Dithering */
	if (property == disp->dithering_mode) {
		nv_connector->dithering_mode = value;
		if (!nv_crtc || !nv_crtc->set_dither)
			return 0;

		return nv_crtc->set_dither(nv_crtc, true);
	}

	if (property == disp->dithering_depth) {
		nv_connector->dithering_depth = value;
		if (!nv_crtc || !nv_crtc->set_dither)
			return 0;

		return nv_crtc->set_dither(nv_crtc, true);
	}

	if (nv_crtc && nv_crtc->set_color_vibrance) {
		/* Hue */
		if (property == disp->vibrant_hue_property) {
			nv_crtc->vibrant_hue = value - 90;
			return nv_crtc->set_color_vibrance(nv_crtc, true);
		}
		/* Saturation */
		if (property == disp->color_vibrance_property) {
			nv_crtc->color_vibrance = value - 100;
			return nv_crtc->set_color_vibrance(nv_crtc, true);
		}
	}

	if (nv_encoder && nv_encoder->dcb->type == DCB_OUTPUT_TV)
		return get_slave_funcs(encoder)->set_property(
			encoder, connector, property, value);

	return -EINVAL;
}

static struct drm_display_mode *
nouveau_connector_native_mode(struct drm_connector *connector)
{
	struct drm_connector_helper_funcs *helper = connector->helper_private;
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode, *largest = NULL;
	int high_w = 0, high_h = 0, high_v = 0;

	list_for_each_entry(mode, &nv_connector->base.probed_modes, head) {
		mode->vrefresh = drm_mode_vrefresh(mode);
		if (helper->mode_valid(connector, mode) != MODE_OK ||
		    (mode->flags & DRM_MODE_FLAG_INTERLACE))
			continue;

		/* Use preferred mode if there is one.. */
		if (mode->type & DRM_MODE_TYPE_PREFERRED) {
			NV_DEBUG(drm, "native mode from preferred\n");
			return drm_mode_duplicate(dev, mode);
		}

		/* Otherwise, take the resolution with the largest width, then
		 * height, then vertical refresh
		 */
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

	NV_DEBUG(drm, "native mode from largest: %dx%d@%d\n",
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

static void
nouveau_connector_detect_depth(struct drm_connector *connector)
{
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder = nv_connector->detected_encoder;
	struct nvbios *bios = &drm->vbios;
	struct drm_display_mode *mode = nv_connector->native_mode;
	bool duallink;

	/* if the edid is feeling nice enough to provide this info, use it */
	if (nv_connector->edid && connector->display_info.bpc)
		return;

	/* EDID 1.4 is *supposed* to be supported on eDP, but, Apple... */
	if (nv_connector->type == DCB_CONNECTOR_eDP) {
		connector->display_info.bpc = 6;
		return;
	}

	/* we're out of options unless we're LVDS, default to 8bpc */
	if (nv_encoder->dcb->type != DCB_OUTPUT_LVDS) {
		connector->display_info.bpc = 8;
		return;
	}

	connector->display_info.bpc = 6;

	/* LVDS: panel straps */
	if (bios->fp_no_ddc) {
		if (bios->fp.if_is_24bit)
			connector->display_info.bpc = 8;
		return;
	}

	/* LVDS: DDC panel, need to first determine the number of links to
	 * know which if_is_24bit flag to check...
	 */
	if (nv_connector->edid &&
	    nv_connector->type == DCB_CONNECTOR_LVDS_SPWG)
		duallink = ((u8 *)nv_connector->edid)[121] == 2;
	else
		duallink = mode->clock >= bios->fp.duallink_transition_clk;

	if ((!duallink && (bios->fp.strapless_is_24bit & 1)) ||
	    ( duallink && (bios->fp.strapless_is_24bit & 2)))
		connector->display_info.bpc = 8;
}

static int
nouveau_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder = nv_connector->detected_encoder;
	struct drm_encoder *encoder = to_drm_encoder(nv_encoder);
	int ret = 0;

	/* destroy the native mode, the attached monitor could have changed.
	 */
	if (nv_connector->native_mode) {
		drm_mode_destroy(dev, nv_connector->native_mode);
		nv_connector->native_mode = NULL;
	}

	if (nv_connector->edid)
		ret = drm_add_edid_modes(connector, nv_connector->edid);
	else
	if (nv_encoder->dcb->type == DCB_OUTPUT_LVDS &&
	    (nv_encoder->dcb->lvdsconf.use_straps_for_mode ||
	     drm->vbios.fp_no_ddc) && nouveau_bios_fp_mode(dev, NULL)) {
		struct drm_display_mode mode;

		nouveau_bios_fp_mode(dev, &mode);
		nv_connector->native_mode = drm_mode_duplicate(dev, &mode);
	}

	/* Determine display colour depth for everything except LVDS now,
	 * DP requires this before mode_valid() is called.
	 */
	if (connector->connector_type != DRM_MODE_CONNECTOR_LVDS)
		nouveau_connector_detect_depth(connector);

	/* Find the native mode if this is a digital panel, if we didn't
	 * find any modes through DDC previously add the native mode to
	 * the list of modes.
	 */
	if (!nv_connector->native_mode)
		nv_connector->native_mode =
			nouveau_connector_native_mode(connector);
	if (ret == 0 && nv_connector->native_mode) {
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(dev, nv_connector->native_mode);
		drm_mode_probed_add(connector, mode);
		ret = 1;
	}

	/* Determine LVDS colour depth, must happen after determining
	 * "native" mode as some VBIOS tables require us to use the
	 * pixel clock as part of the lookup...
	 */
	if (connector->connector_type == DRM_MODE_CONNECTOR_LVDS)
		nouveau_connector_detect_depth(connector);

	if (nv_encoder->dcb->type == DCB_OUTPUT_TV)
		ret = get_slave_funcs(encoder)->get_modes(encoder, connector);

	if (nv_connector->type == DCB_CONNECTOR_LVDS ||
	    nv_connector->type == DCB_CONNECTOR_LVDS_SPWG ||
	    nv_connector->type == DCB_CONNECTOR_eDP)
		ret += nouveau_connector_scaler_modes_add(connector);

	return ret;
}

static unsigned
get_tmds_link_bandwidth(struct drm_connector *connector)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct dcb_output *dcb = nv_connector->detected_encoder->dcb;

	if (dcb->location != DCB_LOC_ON_CHIP ||
	    nv_device(drm->device)->chipset >= 0x46)
		return 165000;
	else if (nv_device(drm->device)->chipset >= 0x40)
		return 155000;
	else if (nv_device(drm->device)->chipset >= 0x18)
		return 135000;
	else
		return 112000;
}

static int
nouveau_connector_mode_valid(struct drm_connector *connector,
			     struct drm_display_mode *mode)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder = nv_connector->detected_encoder;
	struct drm_encoder *encoder = to_drm_encoder(nv_encoder);
	unsigned min_clock = 25000, max_clock = min_clock;
	unsigned clock = mode->clock;

	switch (nv_encoder->dcb->type) {
	case DCB_OUTPUT_LVDS:
		if (nv_connector->native_mode &&
		    (mode->hdisplay > nv_connector->native_mode->hdisplay ||
		     mode->vdisplay > nv_connector->native_mode->vdisplay))
			return MODE_PANEL;

		min_clock = 0;
		max_clock = 400000;
		break;
	case DCB_OUTPUT_TMDS:
		max_clock = get_tmds_link_bandwidth(connector);
		if (nouveau_duallink && nv_encoder->dcb->duallink_possible)
			max_clock *= 2;
		break;
	case DCB_OUTPUT_ANALOG:
		max_clock = nv_encoder->dcb->crtconf.maxfreq;
		if (!max_clock)
			max_clock = 350000;
		break;
	case DCB_OUTPUT_TV:
		return get_slave_funcs(encoder)->mode_valid(encoder, mode);
	case DCB_OUTPUT_DP:
		max_clock  = nv_encoder->dp.link_nr;
		max_clock *= nv_encoder->dp.link_bw;
		clock = clock * (connector->display_info.bpc * 3) / 10;
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

static const struct drm_connector_funcs
nouveau_connector_funcs_lvds = {
	.dpms = drm_helper_connector_dpms,
	.save = NULL,
	.restore = NULL,
	.detect = nouveau_connector_detect_lvds,
	.destroy = nouveau_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = nouveau_connector_set_property,
	.force = nouveau_connector_force
};

static int
drm_conntype_from_dcb(enum dcb_connector_type dcb)
{
	switch (dcb) {
	case DCB_CONNECTOR_VGA      : return DRM_MODE_CONNECTOR_VGA;
	case DCB_CONNECTOR_TV_0     :
	case DCB_CONNECTOR_TV_1     :
	case DCB_CONNECTOR_TV_3     : return DRM_MODE_CONNECTOR_TV;
	case DCB_CONNECTOR_DMS59_0  :
	case DCB_CONNECTOR_DMS59_1  :
	case DCB_CONNECTOR_DVI_I    : return DRM_MODE_CONNECTOR_DVII;
	case DCB_CONNECTOR_DVI_D    : return DRM_MODE_CONNECTOR_DVID;
	case DCB_CONNECTOR_LVDS     :
	case DCB_CONNECTOR_LVDS_SPWG: return DRM_MODE_CONNECTOR_LVDS;
	case DCB_CONNECTOR_DMS59_DP0:
	case DCB_CONNECTOR_DMS59_DP1:
	case DCB_CONNECTOR_DP       : return DRM_MODE_CONNECTOR_DisplayPort;
	case DCB_CONNECTOR_eDP      : return DRM_MODE_CONNECTOR_eDP;
	case DCB_CONNECTOR_HDMI_0   :
	case DCB_CONNECTOR_HDMI_1   : return DRM_MODE_CONNECTOR_HDMIA;
	default:
		break;
	}

	return DRM_MODE_CONNECTOR_Unknown;
}

struct drm_connector *
nouveau_connector_create(struct drm_device *dev, int index)
{
	const struct drm_connector_funcs *funcs = &nouveau_connector_funcs;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_gpio *gpio = nouveau_gpio(drm->device);
	struct nouveau_display *disp = nouveau_display(dev);
	struct nouveau_connector *nv_connector = NULL;
	struct drm_connector *connector;
	int type, ret = 0;
	bool dummy;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		nv_connector = nouveau_connector(connector);
		if (nv_connector->index == index)
			return connector;
	}

	nv_connector = kzalloc(sizeof(*nv_connector), GFP_KERNEL);
	if (!nv_connector)
		return ERR_PTR(-ENOMEM);

	connector = &nv_connector->base;
	nv_connector->index = index;

	/* attempt to parse vbios connector type and hotplug gpio */
	nv_connector->dcb = olddcb_conn(dev, index);
	if (nv_connector->dcb) {
		static const u8 hpd[16] = {
			0xff, 0x07, 0x08, 0xff, 0xff, 0x51, 0x52, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0x5e, 0x5f, 0x60,
		};

		u32 entry = ROM16(nv_connector->dcb[0]);
		if (olddcb_conntab(dev)[3] >= 4)
			entry |= (u32)ROM16(nv_connector->dcb[2]) << 16;

		nv_connector->hpd = ffs((entry & 0x07033000) >> 12);
		nv_connector->hpd = hpd[nv_connector->hpd];

		nv_connector->type = nv_connector->dcb[0];
		if (drm_conntype_from_dcb(nv_connector->type) ==
					  DRM_MODE_CONNECTOR_Unknown) {
			NV_WARN(drm, "unknown connector type %02x\n",
				nv_connector->type);
			nv_connector->type = DCB_CONNECTOR_NONE;
		}

		/* Gigabyte NX85T */
		if (nv_match_device(dev, 0x0421, 0x1458, 0x344c)) {
			if (nv_connector->type == DCB_CONNECTOR_HDMI_1)
				nv_connector->type = DCB_CONNECTOR_DVI_I;
		}

		/* Gigabyte GV-NX86T512H */
		if (nv_match_device(dev, 0x0402, 0x1458, 0x3455)) {
			if (nv_connector->type == DCB_CONNECTOR_HDMI_1)
				nv_connector->type = DCB_CONNECTOR_DVI_I;
		}
	} else {
		nv_connector->type = DCB_CONNECTOR_NONE;
		nv_connector->hpd = DCB_GPIO_UNUSED;
	}

	/* no vbios data, or an unknown dcb connector type - attempt to
	 * figure out something suitable ourselves
	 */
	if (nv_connector->type == DCB_CONNECTOR_NONE) {
		struct nouveau_drm *drm = nouveau_drm(dev);
		struct dcb_table *dcbt = &drm->vbios.dcb;
		u32 encoders = 0;
		int i;

		for (i = 0; i < dcbt->entries; i++) {
			if (dcbt->entry[i].connector == nv_connector->index)
				encoders |= (1 << dcbt->entry[i].type);
		}

		if (encoders & (1 << DCB_OUTPUT_DP)) {
			if (encoders & (1 << DCB_OUTPUT_TMDS))
				nv_connector->type = DCB_CONNECTOR_DP;
			else
				nv_connector->type = DCB_CONNECTOR_eDP;
		} else
		if (encoders & (1 << DCB_OUTPUT_TMDS)) {
			if (encoders & (1 << DCB_OUTPUT_ANALOG))
				nv_connector->type = DCB_CONNECTOR_DVI_I;
			else
				nv_connector->type = DCB_CONNECTOR_DVI_D;
		} else
		if (encoders & (1 << DCB_OUTPUT_ANALOG)) {
			nv_connector->type = DCB_CONNECTOR_VGA;
		} else
		if (encoders & (1 << DCB_OUTPUT_LVDS)) {
			nv_connector->type = DCB_CONNECTOR_LVDS;
		} else
		if (encoders & (1 << DCB_OUTPUT_TV)) {
			nv_connector->type = DCB_CONNECTOR_TV_0;
		}
	}

	type = drm_conntype_from_dcb(nv_connector->type);
	if (type == DRM_MODE_CONNECTOR_LVDS) {
		ret = nouveau_bios_parse_lvds_table(dev, 0, &dummy, &dummy);
		if (ret) {
			NV_ERROR(drm, "Error parsing LVDS table, disabling\n");
			kfree(nv_connector);
			return ERR_PTR(ret);
		}

		funcs = &nouveau_connector_funcs_lvds;
	} else {
		funcs = &nouveau_connector_funcs;
	}

	/* defaults, will get overridden in detect() */
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	drm_connector_init(dev, connector, funcs, type);
	drm_connector_helper_add(connector, &nouveau_connector_helper_funcs);

	/* Init DVI-I specific properties */
	if (nv_connector->type == DCB_CONNECTOR_DVI_I)
		drm_object_attach_property(&connector->base, dev->mode_config.dvi_i_subconnector_property, 0);

	/* Add overscan compensation options to digital outputs */
	if (disp->underscan_property &&
	    (type == DRM_MODE_CONNECTOR_DVID ||
	     type == DRM_MODE_CONNECTOR_DVII ||
	     type == DRM_MODE_CONNECTOR_HDMIA ||
	     type == DRM_MODE_CONNECTOR_DisplayPort)) {
		drm_object_attach_property(&connector->base,
					      disp->underscan_property,
					      UNDERSCAN_OFF);
		drm_object_attach_property(&connector->base,
					      disp->underscan_hborder_property,
					      0);
		drm_object_attach_property(&connector->base,
					      disp->underscan_vborder_property,
					      0);
	}

	/* Add hue and saturation options */
	if (disp->vibrant_hue_property)
		drm_object_attach_property(&connector->base,
					      disp->vibrant_hue_property,
					      90);
	if (disp->color_vibrance_property)
		drm_object_attach_property(&connector->base,
					      disp->color_vibrance_property,
					      150);

	switch (nv_connector->type) {
	case DCB_CONNECTOR_VGA:
		if (nv_device(drm->device)->card_type >= NV_50) {
			drm_object_attach_property(&connector->base,
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

		drm_object_attach_property(&connector->base,
				dev->mode_config.scaling_mode_property,
				nv_connector->scaling_mode);
		if (disp->dithering_mode) {
			nv_connector->dithering_mode = DITHERING_MODE_AUTO;
			drm_object_attach_property(&connector->base,
						disp->dithering_mode,
						nv_connector->dithering_mode);
		}
		if (disp->dithering_depth) {
			nv_connector->dithering_depth = DITHERING_DEPTH_AUTO;
			drm_object_attach_property(&connector->base,
						disp->dithering_depth,
						nv_connector->dithering_depth);
		}
		break;
	}

	connector->polled = DRM_CONNECTOR_POLL_CONNECT;
	if (gpio && nv_connector->hpd != DCB_GPIO_UNUSED) {
		ret = gpio->isr_add(gpio, 0, nv_connector->hpd, 0xff,
				    nouveau_connector_hotplug, connector);
		if (ret == 0)
			connector->polled = DRM_CONNECTOR_POLL_HPD;
	}

	drm_sysfs_connector_add(connector);
	return connector;
}

static void
nouveau_connector_hotplug(void *data, int plugged)
{
	struct drm_connector *connector = data;
	struct drm_device *dev = connector->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);

	NV_DEBUG(drm, "%splugged %s\n", plugged ? "" : "un",
		 drm_get_connector_name(connector));

	if (plugged)
		drm_helper_connector_dpms(connector, DRM_MODE_DPMS_ON);
	else
		drm_helper_connector_dpms(connector, DRM_MODE_DPMS_OFF);

	drm_helper_hpd_irq_event(dev);
}
