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

#include <linux/pm_runtime.h>
#include <linux/vga_switcheroo.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic.h>

#include "nouveau_reg.h"
#include "nouveau_drv.h"
#include "dispnv04/hw.h"
#include "dispnv50/disp.h"
#include "nouveau_acpi.h"

#include "nouveau_display.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"

#include <nvif/class.h>
#include <nvif/if0011.h>

struct drm_display_mode *
nouveau_conn_native_mode(struct drm_connector *connector)
{
	const struct drm_connector_helper_funcs *helper = connector->helper_private;
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode, *largest = NULL;
	int high_w = 0, high_h = 0, high_v = 0;

	list_for_each_entry(mode, &connector->probed_modes, head) {
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
		    drm_mode_vrefresh(mode) < high_v)
			continue;

		high_w = mode->hdisplay;
		high_h = mode->vdisplay;
		high_v = drm_mode_vrefresh(mode);
		largest = mode;
	}

	NV_DEBUG(drm, "native mode from largest: %dx%d@%d\n",
		      high_w, high_h, high_v);
	return largest ? drm_mode_duplicate(dev, largest) : NULL;
}

int
nouveau_conn_atomic_get_property(struct drm_connector *connector,
				 const struct drm_connector_state *state,
				 struct drm_property *property, u64 *val)
{
	struct nouveau_conn_atom *asyc = nouveau_conn_atom(state);
	struct nouveau_display *disp = nouveau_display(connector->dev);
	struct drm_device *dev = connector->dev;

	if (property == dev->mode_config.scaling_mode_property)
		*val = asyc->scaler.mode;
	else if (property == disp->underscan_property)
		*val = asyc->scaler.underscan.mode;
	else if (property == disp->underscan_hborder_property)
		*val = asyc->scaler.underscan.hborder;
	else if (property == disp->underscan_vborder_property)
		*val = asyc->scaler.underscan.vborder;
	else if (property == disp->dithering_mode)
		*val = asyc->dither.mode;
	else if (property == disp->dithering_depth)
		*val = asyc->dither.depth;
	else if (property == disp->vibrant_hue_property)
		*val = asyc->procamp.vibrant_hue;
	else if (property == disp->color_vibrance_property)
		*val = asyc->procamp.color_vibrance;
	else
		return -EINVAL;

	return 0;
}

int
nouveau_conn_atomic_set_property(struct drm_connector *connector,
				 struct drm_connector_state *state,
				 struct drm_property *property, u64 val)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_conn_atom *asyc = nouveau_conn_atom(state);
	struct nouveau_display *disp = nouveau_display(dev);

	if (property == dev->mode_config.scaling_mode_property) {
		switch (val) {
		case DRM_MODE_SCALE_NONE:
			/* We allow 'None' for EDID modes, even on a fixed
			 * panel (some exist with support for lower refresh
			 * rates, which people might want to use for power-
			 * saving purposes).
			 *
			 * Non-EDID modes will force the use of GPU scaling
			 * to the native mode regardless of this setting.
			 */
			switch (connector->connector_type) {
			case DRM_MODE_CONNECTOR_LVDS:
			case DRM_MODE_CONNECTOR_eDP:
				/* ... except prior to G80, where the code
				 * doesn't support such things.
				 */
				if (disp->disp.object.oclass < NV50_DISP)
					return -EINVAL;
				break;
			default:
				break;
			}
			break;
		case DRM_MODE_SCALE_FULLSCREEN:
		case DRM_MODE_SCALE_CENTER:
		case DRM_MODE_SCALE_ASPECT:
			break;
		default:
			return -EINVAL;
		}

		if (asyc->scaler.mode != val) {
			asyc->scaler.mode = val;
			asyc->set.scaler = true;
		}
	} else
	if (property == disp->underscan_property) {
		if (asyc->scaler.underscan.mode != val) {
			asyc->scaler.underscan.mode = val;
			asyc->set.scaler = true;
		}
	} else
	if (property == disp->underscan_hborder_property) {
		if (asyc->scaler.underscan.hborder != val) {
			asyc->scaler.underscan.hborder = val;
			asyc->set.scaler = true;
		}
	} else
	if (property == disp->underscan_vborder_property) {
		if (asyc->scaler.underscan.vborder != val) {
			asyc->scaler.underscan.vborder = val;
			asyc->set.scaler = true;
		}
	} else
	if (property == disp->dithering_mode) {
		if (asyc->dither.mode != val) {
			asyc->dither.mode = val;
			asyc->set.dither = true;
		}
	} else
	if (property == disp->dithering_depth) {
		if (asyc->dither.mode != val) {
			asyc->dither.depth = val;
			asyc->set.dither = true;
		}
	} else
	if (property == disp->vibrant_hue_property) {
		if (asyc->procamp.vibrant_hue != val) {
			asyc->procamp.vibrant_hue = val;
			asyc->set.procamp = true;
		}
	} else
	if (property == disp->color_vibrance_property) {
		if (asyc->procamp.color_vibrance != val) {
			asyc->procamp.color_vibrance = val;
			asyc->set.procamp = true;
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

void
nouveau_conn_atomic_destroy_state(struct drm_connector *connector,
				  struct drm_connector_state *state)
{
	struct nouveau_conn_atom *asyc = nouveau_conn_atom(state);
	__drm_atomic_helper_connector_destroy_state(&asyc->state);
	kfree(asyc);
}

struct drm_connector_state *
nouveau_conn_atomic_duplicate_state(struct drm_connector *connector)
{
	struct nouveau_conn_atom *armc = nouveau_conn_atom(connector->state);
	struct nouveau_conn_atom *asyc;
	if (!(asyc = kmalloc(sizeof(*asyc), GFP_KERNEL)))
		return NULL;
	__drm_atomic_helper_connector_duplicate_state(connector, &asyc->state);
	asyc->dither = armc->dither;
	asyc->scaler = armc->scaler;
	asyc->procamp = armc->procamp;
	asyc->set.mask = 0;
	return &asyc->state;
}

void
nouveau_conn_reset(struct drm_connector *connector)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_conn_atom *asyc;

	if (drm_drv_uses_atomic_modeset(connector->dev)) {
		if (WARN_ON(!(asyc = kzalloc(sizeof(*asyc), GFP_KERNEL))))
			return;

		if (connector->state)
			nouveau_conn_atomic_destroy_state(connector,
							  connector->state);

		__drm_atomic_helper_connector_reset(connector, &asyc->state);
	} else {
		asyc = &nv_connector->properties_state;
	}

	asyc->dither.mode = DITHERING_MODE_AUTO;
	asyc->dither.depth = DITHERING_DEPTH_AUTO;
	asyc->scaler.mode = DRM_MODE_SCALE_NONE;
	asyc->scaler.underscan.mode = UNDERSCAN_OFF;
	asyc->procamp.color_vibrance = 150;
	asyc->procamp.vibrant_hue = 90;

	if (nouveau_display(connector->dev)->disp.object.oclass < NV50_DISP) {
		switch (connector->connector_type) {
		case DRM_MODE_CONNECTOR_LVDS:
			/* See note in nouveau_conn_atomic_set_property(). */
			asyc->scaler.mode = DRM_MODE_SCALE_FULLSCREEN;
			break;
		default:
			break;
		}
	}
}

void
nouveau_conn_attach_properties(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_display *disp = nouveau_display(dev);
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_conn_atom *armc;

	if (drm_drv_uses_atomic_modeset(connector->dev))
		armc = nouveau_conn_atom(connector->state);
	else
		armc = &nv_connector->properties_state;

	/* Init DVI-I specific properties. */
	if (connector->connector_type == DRM_MODE_CONNECTOR_DVII)
		drm_object_attach_property(&connector->base, dev->mode_config.
					   dvi_i_subconnector_property, 0);

	/* Add overscan compensation options to digital outputs. */
	if (disp->underscan_property &&
	    (connector->connector_type == DRM_MODE_CONNECTOR_DVID ||
	     connector->connector_type == DRM_MODE_CONNECTOR_DVII ||
	     connector->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	     connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort)) {
		drm_object_attach_property(&connector->base,
					   disp->underscan_property,
					   UNDERSCAN_OFF);
		drm_object_attach_property(&connector->base,
					   disp->underscan_hborder_property, 0);
		drm_object_attach_property(&connector->base,
					   disp->underscan_vborder_property, 0);
	}

	/* Add hue and saturation options. */
	if (disp->vibrant_hue_property)
		drm_object_attach_property(&connector->base,
					   disp->vibrant_hue_property,
					   armc->procamp.vibrant_hue);
	if (disp->color_vibrance_property)
		drm_object_attach_property(&connector->base,
					   disp->color_vibrance_property,
					   armc->procamp.color_vibrance);

	/* Scaling mode property. */
	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_TV:
		break;
	case DRM_MODE_CONNECTOR_VGA:
		if (disp->disp.object.oclass < NV50_DISP)
			break; /* Can only scale on DFPs. */
		fallthrough;
	default:
		drm_object_attach_property(&connector->base, dev->mode_config.
					   scaling_mode_property,
					   armc->scaler.mode);
		break;
	}

	/* Dithering properties. */
	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_TV:
	case DRM_MODE_CONNECTOR_VGA:
		break;
	default:
		if (disp->dithering_mode) {
			drm_object_attach_property(&connector->base,
						   disp->dithering_mode,
						   armc->dither.mode);
		}
		if (disp->dithering_depth) {
			drm_object_attach_property(&connector->base,
						   disp->dithering_depth,
						   armc->dither.depth);
		}
		break;
	}
}

MODULE_PARM_DESC(tv_disable, "Disable TV-out detection");
int nouveau_tv_disable = 0;
module_param_named(tv_disable, nouveau_tv_disable, int, 0400);

MODULE_PARM_DESC(ignorelid, "Ignore ACPI lid status");
int nouveau_ignorelid = 0;
module_param_named(ignorelid, nouveau_ignorelid, int, 0400);

MODULE_PARM_DESC(duallink, "Allow dual-link TMDS (default: enabled)");
int nouveau_duallink = 1;
module_param_named(duallink, nouveau_duallink, int, 0400);

MODULE_PARM_DESC(hdmimhz, "Force a maximum HDMI pixel clock (in MHz)");
int nouveau_hdmimhz = 0;
module_param_named(hdmimhz, nouveau_hdmimhz, int, 0400);

struct nouveau_encoder *
find_encoder(struct drm_connector *connector, int type)
{
	struct nouveau_encoder *nv_encoder;
	struct drm_encoder *enc;

	drm_connector_for_each_possible_encoder(connector, enc) {
		nv_encoder = nouveau_encoder(enc);

		if (type == DCB_OUTPUT_ANY ||
		    (nv_encoder->dcb && nv_encoder->dcb->type == type))
			return nv_encoder;
	}

	return NULL;
}

static void
nouveau_connector_destroy(struct drm_connector *connector)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	nvif_event_dtor(&nv_connector->irq);
	nvif_event_dtor(&nv_connector->hpd);
	kfree(nv_connector->edid);
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	if (nv_connector->aux.transfer)
		drm_dp_cec_unregister_connector(&nv_connector->aux);
	nvif_conn_dtor(&nv_connector->conn);
	kfree(connector);
}

static struct nouveau_encoder *
nouveau_connector_ddc_detect(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct nouveau_connector *conn = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder = NULL, *found = NULL;
	struct drm_encoder *encoder;
	int ret;
	bool switcheroo_ddc = false;

	drm_connector_for_each_possible_encoder(connector, encoder) {
		nv_encoder = nouveau_encoder(encoder);

		if (nvif_object_constructed(&nv_encoder->outp.object)) {
			enum nvif_outp_detect_status status;

			if (nv_encoder->dcb->type == DCB_OUTPUT_DP) {
				ret = nouveau_dp_detect(conn, nv_encoder);
				if (ret == NOUVEAU_DP_MST)
					return NULL;
				if (ret != NOUVEAU_DP_SST)
					continue;

				return nv_encoder;
			} else {
				status = nvif_outp_detect(&nv_encoder->outp);
				switch (status) {
				case PRESENT:
					return nv_encoder;
				case NOT_PRESENT:
					continue;
				case UNKNOWN:
					break;
				default:
					WARN_ON(1);
					break;
				}
			}
		}

		if (!nv_encoder->i2c)
			continue;

		if (nv_encoder->dcb->type == DCB_OUTPUT_LVDS) {
			switcheroo_ddc = !!(vga_switcheroo_handler_flags() &
					    VGA_SWITCHEROO_CAN_SWITCH_DDC);
		}

		if (switcheroo_ddc)
			vga_switcheroo_lock_ddc(pdev);
		if (nvkm_probe_i2c(nv_encoder->i2c, 0x50))
			found = nv_encoder;
		if (switcheroo_ddc)
			vga_switcheroo_unlock_ddc(pdev);

		if (found)
			break;
	}

	return found;
}

static struct nouveau_encoder *
nouveau_connector_of_detect(struct drm_connector *connector)
{
#ifdef __powerpc__
	struct drm_device *dev = connector->dev;
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct device_node *cn, *dn = pci_device_to_OF_node(pdev);

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
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	if (nv_connector->detected_encoder == nv_encoder)
		return;
	nv_connector->detected_encoder = nv_encoder;

	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_TESLA) {
		if (nv_encoder->dcb->type == DCB_OUTPUT_DP)
			connector->interlace_allowed =
				nv_encoder->caps.dp_interlace;
		else
			connector->interlace_allowed =
				drm->client.device.info.family < NV_DEVICE_INFO_V0_VOLTA;
		connector->doublescan_allowed = true;
	} else
	if (nv_encoder->dcb->type == DCB_OUTPUT_LVDS ||
	    nv_encoder->dcb->type == DCB_OUTPUT_TMDS) {
		connector->doublescan_allowed = false;
		connector->interlace_allowed = false;
	} else {
		connector->doublescan_allowed = true;
		if (drm->client.device.info.family == NV_DEVICE_INFO_V0_KELVIN ||
		    (drm->client.device.info.family == NV_DEVICE_INFO_V0_CELSIUS &&
		     (pdev->device & 0x0ff0) != 0x0100 &&
		     (pdev->device & 0x0ff0) != 0x0150))
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

static void
nouveau_connector_set_edid(struct nouveau_connector *nv_connector,
			   struct edid *edid)
{
	if (nv_connector->edid != edid) {
		struct edid *old_edid = nv_connector->edid;

		drm_connector_update_edid_property(&nv_connector->base, edid);
		kfree(old_edid);
		nv_connector->edid = edid;
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
	int type;
	int ret;
	enum drm_connector_status conn_status = connector_status_disconnected;

	/* Outputs are only polled while runtime active, so resuming the
	 * device here is unnecessary (and would deadlock upon runtime suspend
	 * because it waits for polling to finish). We do however, want to
	 * prevent the autosuspend timer from elapsing during this operation
	 * if possible.
	 */
	if (drm_kms_helper_is_poll_worker()) {
		pm_runtime_get_noresume(dev->dev);
	} else {
		ret = pm_runtime_get_sync(dev->dev);
		if (ret < 0 && ret != -EACCES) {
			pm_runtime_put_autosuspend(dev->dev);
			nouveau_connector_set_edid(nv_connector, NULL);
			return conn_status;
		}
	}

	nv_encoder = nouveau_connector_ddc_detect(connector);
	if (nv_encoder) {
		struct edid *new_edid = NULL;

		if (nv_encoder->i2c) {
			if ((vga_switcheroo_handler_flags() & VGA_SWITCHEROO_CAN_SWITCH_DDC) &&
			    nv_connector->type == DCB_CONNECTOR_LVDS)
				new_edid = drm_get_edid_switcheroo(connector, nv_encoder->i2c);
			else
				new_edid = drm_get_edid(connector, nv_encoder->i2c);
		} else {
			ret = nvif_outp_edid_get(&nv_encoder->outp, (u8 **)&new_edid);
			if (ret < 0)
				return connector_status_disconnected;
		}

		nouveau_connector_set_edid(nv_connector, new_edid);
		if (!nv_connector->edid) {
			NV_ERROR(drm, "DDC responded, but no EDID for %s\n",
				 connector->name);
			goto detect_analog;
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
		conn_status = connector_status_connected;

		if (nv_encoder->dcb->type == DCB_OUTPUT_DP)
			drm_dp_cec_set_edid(&nv_connector->aux, nv_connector->edid);

		goto out;
	} else {
		nouveau_connector_set_edid(nv_connector, NULL);
	}

	nv_encoder = nouveau_connector_of_detect(connector);
	if (nv_encoder) {
		nouveau_connector_set_encoder(connector, nv_encoder);
		conn_status = connector_status_connected;
		goto out;
	}

detect_analog:
	nv_encoder = find_encoder(connector, DCB_OUTPUT_ANALOG);
	if (!nv_encoder && !nouveau_tv_disable)
		nv_encoder = find_encoder(connector, DCB_OUTPUT_TV);
	if (nv_encoder && force) {
		struct drm_encoder *encoder = to_drm_encoder(nv_encoder);
		const struct drm_encoder_helper_funcs *helper =
						encoder->helper_private;

		if (helper->detect(encoder, connector) ==
						connector_status_connected) {
			nouveau_connector_set_encoder(connector, nv_encoder);
			conn_status = connector_status_connected;
			goto out;
		}
	}

 out:
	if (!nv_connector->edid)
		drm_dp_cec_unset_edid(&nv_connector->aux);

	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return conn_status;
}

static enum drm_connector_status
nouveau_connector_detect_lvds(struct drm_connector *connector, bool force)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder = NULL;
	struct edid *edid = NULL;
	enum drm_connector_status status = connector_status_disconnected;

	nv_encoder = find_encoder(connector, DCB_OUTPUT_LVDS);
	if (!nv_encoder)
		goto out;

	/* Try retrieving EDID via DDC */
	if (!drm->vbios.fp_no_ddc) {
		status = nouveau_connector_detect(connector, force);
		if (status == connector_status_connected) {
			edid = nv_connector->edid;
			goto out;
		}
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
		edid = nouveau_acpi_edid(dev, connector);
		if (edid) {
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
		edid = (struct edid *)nouveau_bios_embedded_edid(dev);
		if (edid) {
			edid = kmemdup(edid, EDID_LENGTH, GFP_KERNEL);
			if (edid)
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

	nouveau_connector_set_edid(nv_connector, edid);
	if (nv_encoder)
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
			 connector->name);
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
	struct nouveau_conn_atom *asyc = &nv_connector->properties_state;
	struct drm_encoder *encoder = to_drm_encoder(nv_encoder);
	int ret;

	ret = connector->funcs->atomic_set_property(&nv_connector->base,
						    &asyc->state,
						    property, value);
	if (ret) {
		if (nv_encoder && nv_encoder->dcb->type == DCB_OUTPUT_TV)
			return get_slave_funcs(encoder)->set_property(
				encoder, connector, property, value);
		return ret;
	}

	nv_connector->scaling_mode = asyc->scaler.mode;
	nv_connector->dithering_mode = asyc->dither.mode;

	if (connector->encoder && connector->encoder->crtc) {
		ret = drm_crtc_helper_set_mode(connector->encoder->crtc,
					      &connector->encoder->crtc->mode,
					       connector->encoder->crtc->x,
					       connector->encoder->crtc->y,
					       NULL);
		if (!ret)
			return -EINVAL;
	}

	return 0;
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
		    mode->vdisplay <= native->vdisplay &&
		    (mode->hdisplay != native->hdisplay ||
		     mode->vdisplay != native->vdisplay)) {
			m = drm_cvt_mode(dev, mode->hdisplay, mode->vdisplay,
					 drm_mode_vrefresh(native), false,
					 false, false);
			if (!m)
				continue;

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
nouveau_connector_late_register(struct drm_connector *connector)
{
	int ret;

	ret = nouveau_backlight_init(connector);
	if (ret)
		return ret;

	if (connector->connector_type == DRM_MODE_CONNECTOR_eDP ||
	    connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort) {
		ret = drm_dp_aux_register(&nouveau_connector(connector)->aux);
		if (ret)
			goto backlight_fini;
	}

	return 0;
backlight_fini:
	nouveau_backlight_fini(connector);
	return ret;
}

static void
nouveau_connector_early_unregister(struct drm_connector *connector)
{
	if (connector->connector_type == DRM_MODE_CONNECTOR_eDP ||
	    connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort)
		drm_dp_aux_unregister(&nouveau_connector(connector)->aux);

	nouveau_backlight_fini(connector);
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
		nv_connector->native_mode = nouveau_conn_native_mode(connector);
	if (ret == 0 && nv_connector->native_mode) {
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(dev, nv_connector->native_mode);
		if (!mode)
			return 0;

		drm_mode_probed_add(connector, mode);
		ret = 1;
	}

	/* Determine LVDS colour depth, must happen after determining
	 * "native" mode as some VBIOS tables require us to use the
	 * pixel clock as part of the lookup...
	 */
	if (connector->connector_type == DRM_MODE_CONNECTOR_LVDS && nv_connector->native_mode)
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
	struct nouveau_encoder *nv_encoder = nv_connector->detected_encoder;
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct dcb_output *dcb = nv_connector->detected_encoder->dcb;
	struct drm_display_info *info = NULL;
	unsigned duallink_scale =
		nouveau_duallink && nv_encoder->dcb->duallink_possible ? 2 : 1;

	if (drm_detect_hdmi_monitor(nv_connector->edid)) {
		info = &nv_connector->base.display_info;
		duallink_scale = 1;
	}

	if (info) {
		if (nouveau_hdmimhz > 0)
			return nouveau_hdmimhz * 1000;
		/* Note: these limits are conservative, some Fermi's
		 * can do 297 MHz. Unclear how this can be determined.
		 */
		if (drm->client.device.info.chipset >= 0x120) {
			const int max_tmds_clock =
				info->hdmi.scdc.scrambling.supported ?
				594000 : 340000;
			return info->max_tmds_clock ?
				min(info->max_tmds_clock, max_tmds_clock) :
				max_tmds_clock;
		}
		if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_KEPLER)
			return 297000;
		if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_FERMI)
			return 225000;
	}

	if (dcb->location != DCB_LOC_ON_CHIP ||
	    drm->client.device.info.chipset >= 0x46)
		return 165000 * duallink_scale;
	else if (drm->client.device.info.chipset >= 0x40)
		return 155000 * duallink_scale;
	else if (drm->client.device.info.chipset >= 0x18)
		return 135000 * duallink_scale;
	else
		return 112000 * duallink_scale;
}

static enum drm_mode_status
nouveau_connector_mode_valid(struct drm_connector *connector,
			     struct drm_display_mode *mode)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder = nv_connector->detected_encoder;
	struct drm_encoder *encoder = to_drm_encoder(nv_encoder);
	unsigned int min_clock = 25000, max_clock = min_clock, clock = mode->clock;

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
		break;
	case DCB_OUTPUT_ANALOG:
		max_clock = nv_encoder->dcb->crtconf.maxfreq;
		if (!max_clock)
			max_clock = 350000;
		break;
	case DCB_OUTPUT_TV:
		return get_slave_funcs(encoder)->mode_valid(encoder, mode);
	case DCB_OUTPUT_DP:
		return nv50_dp_mode_valid(nv_encoder, mode, NULL);
	default:
		BUG();
		return MODE_BAD;
	}

	if ((mode->flags & DRM_MODE_FLAG_3D_MASK) == DRM_MODE_FLAG_3D_FRAME_PACKING)
		clock *= 2;

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

static int
nouveau_connector_atomic_check(struct drm_connector *connector, struct drm_atomic_state *state)
{
	struct nouveau_connector *nv_conn = nouveau_connector(connector);
	struct drm_connector_state *conn_state =
		drm_atomic_get_new_connector_state(state, connector);

	if (!nv_conn->dp_encoder || !nv_conn->dp_encoder->dp.mstm)
		return 0;

	return drm_dp_mst_root_conn_atomic_check(conn_state, &nv_conn->dp_encoder->dp.mstm->mgr);
}

static const struct drm_connector_helper_funcs
nouveau_connector_helper_funcs = {
	.get_modes = nouveau_connector_get_modes,
	.mode_valid = nouveau_connector_mode_valid,
	.best_encoder = nouveau_connector_best_encoder,
	.atomic_check = nouveau_connector_atomic_check,
};

static const struct drm_connector_funcs
nouveau_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.reset = nouveau_conn_reset,
	.detect = nouveau_connector_detect,
	.force = nouveau_connector_force,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = nouveau_connector_set_property,
	.destroy = nouveau_connector_destroy,
	.atomic_duplicate_state = nouveau_conn_atomic_duplicate_state,
	.atomic_destroy_state = nouveau_conn_atomic_destroy_state,
	.atomic_set_property = nouveau_conn_atomic_set_property,
	.atomic_get_property = nouveau_conn_atomic_get_property,
	.late_register = nouveau_connector_late_register,
	.early_unregister = nouveau_connector_early_unregister,
};

static const struct drm_connector_funcs
nouveau_connector_funcs_lvds = {
	.dpms = drm_helper_connector_dpms,
	.reset = nouveau_conn_reset,
	.detect = nouveau_connector_detect_lvds,
	.force = nouveau_connector_force,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = nouveau_connector_set_property,
	.destroy = nouveau_connector_destroy,
	.atomic_duplicate_state = nouveau_conn_atomic_duplicate_state,
	.atomic_destroy_state = nouveau_conn_atomic_destroy_state,
	.atomic_set_property = nouveau_conn_atomic_set_property,
	.atomic_get_property = nouveau_conn_atomic_get_property,
	.late_register = nouveau_connector_late_register,
	.early_unregister = nouveau_connector_early_unregister,
};

void
nouveau_connector_hpd(struct nouveau_connector *nv_connector, u64 bits)
{
	struct nouveau_drm *drm = nouveau_drm(nv_connector->base.dev);
	u32 mask = drm_connector_mask(&nv_connector->base);
	unsigned long flags;

	spin_lock_irqsave(&drm->hpd_lock, flags);
	if (!(drm->hpd_pending & mask)) {
		nv_connector->hpd_pending |= bits;
		drm->hpd_pending |= mask;
		schedule_work(&drm->hpd_work);
	}
	spin_unlock_irqrestore(&drm->hpd_lock, flags);
}

static int
nouveau_connector_irq(struct nvif_event *event, void *repv, u32 repc)
{
	struct nouveau_connector *nv_connector = container_of(event, typeof(*nv_connector), irq);

	schedule_work(&nv_connector->irq_work);
	return NVIF_EVENT_KEEP;
}

static int
nouveau_connector_hotplug(struct nvif_event *event, void *repv, u32 repc)
{
	struct nouveau_connector *nv_connector = container_of(event, typeof(*nv_connector), hpd);
	struct nvif_conn_event_v0 *rep = repv;

	nouveau_connector_hpd(nv_connector, rep->types);
	return NVIF_EVENT_KEEP;
}

static ssize_t
nouveau_connector_aux_xfer(struct drm_dp_aux *obj, struct drm_dp_aux_msg *msg)
{
	struct nouveau_connector *nv_connector =
		container_of(obj, typeof(*nv_connector), aux);
	struct nouveau_encoder *nv_encoder;
	u8 size = msg->size;
	int ret;

	nv_encoder = find_encoder(&nv_connector->base, DCB_OUTPUT_DP);
	if (!nv_encoder)
		return -ENODEV;
	if (WARN_ON(msg->size > 16))
		return -E2BIG;

	ret = nvif_outp_dp_aux_xfer(&nv_encoder->outp,
				    msg->request, &size, msg->address, msg->buffer);
	if (ret >= 0) {
		msg->reply = ret;
		return size;
	}

	return ret;
}

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
	case DCB_CONNECTOR_DP       :
	case DCB_CONNECTOR_mDP      :
	case DCB_CONNECTOR_USB_C    : return DRM_MODE_CONNECTOR_DisplayPort;
	case DCB_CONNECTOR_eDP      : return DRM_MODE_CONNECTOR_eDP;
	case DCB_CONNECTOR_HDMI_0   :
	case DCB_CONNECTOR_HDMI_1   :
	case DCB_CONNECTOR_HDMI_C   : return DRM_MODE_CONNECTOR_HDMIA;
	case DCB_CONNECTOR_WFD	    : return DRM_MODE_CONNECTOR_VIRTUAL;
	default:
		break;
	}

	return DRM_MODE_CONNECTOR_Unknown;
}

struct drm_connector *
nouveau_connector_create(struct drm_device *dev, int index)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_display *disp = nouveau_display(dev);
	struct nouveau_connector *nv_connector = NULL;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	int type, ret = 0;
	bool dummy;

	drm_connector_list_iter_begin(dev, &conn_iter);
	nouveau_for_each_non_mst_connector_iter(connector, &conn_iter) {
		nv_connector = nouveau_connector(connector);
		if (nv_connector->index == index) {
			drm_connector_list_iter_end(&conn_iter);
			return connector;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	nv_connector = kzalloc(sizeof(*nv_connector), GFP_KERNEL);
	if (!nv_connector)
		return ERR_PTR(-ENOMEM);

	connector = &nv_connector->base;
	nv_connector->index = index;
	INIT_WORK(&nv_connector->irq_work, nouveau_dp_irq);

	if (disp->disp.conn_mask & BIT(nv_connector->index)) {
		ret = nvif_conn_ctor(&disp->disp, nv_connector->base.name, nv_connector->index,
				     &nv_connector->conn);
		if (ret) {
			kfree(nv_connector);
			return ERR_PTR(ret);
		}

		switch (nv_connector->conn.info.type) {
		case NVIF_CONN_VGA      : type = DCB_CONNECTOR_VGA; break;
		case NVIF_CONN_DVI_I    : type = DCB_CONNECTOR_DVI_I; break;
		case NVIF_CONN_DVI_D    : type = DCB_CONNECTOR_DVI_D; break;
		case NVIF_CONN_LVDS     : type = DCB_CONNECTOR_LVDS; break;
		case NVIF_CONN_LVDS_SPWG: type = DCB_CONNECTOR_LVDS_SPWG; break;
		case NVIF_CONN_DP       : type = DCB_CONNECTOR_DP; break;
		case NVIF_CONN_EDP      : type = DCB_CONNECTOR_eDP; break;
		case NVIF_CONN_HDMI     : type = DCB_CONNECTOR_HDMI_0; break;
		default:
			WARN_ON(1);
			return NULL;
		}

		nv_connector->type = type;
	} else {
		u8 *dcb = olddcb_conn(dev, nv_connector->index);

		if (dcb)
			nv_connector->type = dcb[0];
		else
			nv_connector->type = DCB_CONNECTOR_NONE;

		/* attempt to parse vbios connector type and hotplug gpio */
		if (nv_connector->type != DCB_CONNECTOR_NONE) {
			if (drm_conntype_from_dcb(nv_connector->type) ==
						  DRM_MODE_CONNECTOR_Unknown) {
				NV_WARN(drm, "unknown connector type %02x\n",
					nv_connector->type);
				nv_connector->type = DCB_CONNECTOR_NONE;
			}
		}

		/* no vbios data, or an unknown dcb connector type - attempt to
		 * figure out something suitable ourselves
		 */
		if (nv_connector->type == DCB_CONNECTOR_NONE &&
		    !WARN_ON(drm->client.device.info.family >= NV_DEVICE_INFO_V0_TESLA)) {
			struct dcb_table *dcbt = &drm->vbios.dcb;
			u32 encoders = 0;
			int i;

			for (i = 0; i < dcbt->entries; i++) {
				if (dcbt->entry[i].connector == nv_connector->index)
					encoders |= (1 << dcbt->entry[i].type);
			}

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
	}

	type = drm_conntype_from_dcb(nv_connector->type);
	if (type == DRM_MODE_CONNECTOR_LVDS)
		drm_connector_init(dev, connector, &nouveau_connector_funcs_lvds, type);
	else
		drm_connector_init(dev, connector, &nouveau_connector_funcs, type);

	switch (type) {
	case DRM_MODE_CONNECTOR_LVDS:
		ret = nouveau_bios_parse_lvds_table(dev, 0, &dummy, &dummy);
		if (ret) {
			NV_ERROR(drm, "Error parsing LVDS table, disabling\n");
			kfree(nv_connector);
			return ERR_PTR(ret);
		}

		break;
	case DRM_MODE_CONNECTOR_DisplayPort:
	case DRM_MODE_CONNECTOR_eDP:
		nv_connector->aux.dev = connector->kdev;
		nv_connector->aux.drm_dev = dev;
		nv_connector->aux.transfer = nouveau_connector_aux_xfer;
		nv_connector->aux.name = connector->name;
		drm_dp_aux_init(&nv_connector->aux);
		break;
	default:
		break;
	}

	/* HDMI 3D support */
	if ((disp->disp.object.oclass >= G82_DISP)
	    && ((type == DRM_MODE_CONNECTOR_DisplayPort)
		|| (type == DRM_MODE_CONNECTOR_eDP)
		|| (type == DRM_MODE_CONNECTOR_HDMIA)))
		connector->stereo_allowed = true;

	/* defaults, will get overridden in detect() */
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	drm_connector_helper_add(connector, &nouveau_connector_helper_funcs);
	connector->polled = DRM_CONNECTOR_POLL_CONNECT;

	if (nvif_object_constructed(&nv_connector->conn.object)) {
		ret = nvif_conn_event_ctor(&nv_connector->conn, "kmsHotplug",
					   nouveau_connector_hotplug,
					   NVIF_CONN_EVENT_V0_PLUG | NVIF_CONN_EVENT_V0_UNPLUG,
					   &nv_connector->hpd);
		if (ret == 0)
			connector->polled = DRM_CONNECTOR_POLL_HPD;

		if (nv_connector->aux.transfer) {
			ret = nvif_conn_event_ctor(&nv_connector->conn, "kmsDpIrq",
						   nouveau_connector_irq, NVIF_CONN_EVENT_V0_IRQ,
						   &nv_connector->irq);
			if (ret) {
				nvif_event_dtor(&nv_connector->hpd);
				nvif_conn_dtor(&nv_connector->conn);
				goto drm_conn_err;
			}
		}
	}

	connector->funcs->reset(connector);
	nouveau_conn_attach_properties(connector);

	/* Default scaling mode */
	switch (nv_connector->type) {
	case DCB_CONNECTOR_LVDS:
	case DCB_CONNECTOR_LVDS_SPWG:
	case DCB_CONNECTOR_eDP:
		/* see note in nouveau_connector_set_property() */
		if (disp->disp.object.oclass < NV50_DISP) {
			nv_connector->scaling_mode = DRM_MODE_SCALE_FULLSCREEN;
			break;
		}
		nv_connector->scaling_mode = DRM_MODE_SCALE_NONE;
		break;
	default:
		nv_connector->scaling_mode = DRM_MODE_SCALE_NONE;
		break;
	}

	/* dithering properties */
	switch (nv_connector->type) {
	case DCB_CONNECTOR_TV_0:
	case DCB_CONNECTOR_TV_1:
	case DCB_CONNECTOR_TV_3:
	case DCB_CONNECTOR_VGA:
		break;
	default:
		nv_connector->dithering_mode = DITHERING_MODE_AUTO;
		break;
	}

	switch (type) {
	case DRM_MODE_CONNECTOR_DisplayPort:
		nv_connector->dp_encoder = find_encoder(&nv_connector->base, DCB_OUTPUT_DP);
		fallthrough;
	case DRM_MODE_CONNECTOR_eDP:
		drm_dp_cec_register_connector(&nv_connector->aux, connector);
		break;
	}

	drm_connector_register(connector);
	return connector;

drm_conn_err:
	drm_connector_cleanup(connector);
	kfree(nv_connector);
	return ERR_PTR(ret);
}
