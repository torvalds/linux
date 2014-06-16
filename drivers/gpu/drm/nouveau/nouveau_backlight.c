/*
 * Copyright (C) 2009 Red Hat <mjg@redhat.com>
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

/*
 * Authors:
 *  Matthew Garrett <mjg@redhat.com>
 *
 * Register locations derived from NVClock by Roderick Colenbrander
 */

#include <linux/backlight.h>

#include "nouveau_drm.h"
#include "nouveau_reg.h"
#include "nouveau_encoder.h"

static int
nv40_get_intensity(struct backlight_device *bd)
{
	struct nouveau_drm *drm = bl_get_data(bd);
	struct nouveau_device *device = nv_device(drm->device);
	int val = (nv_rd32(device, NV40_PMC_BACKLIGHT) &
				   NV40_PMC_BACKLIGHT_MASK) >> 16;

	return val;
}

static int
nv40_set_intensity(struct backlight_device *bd)
{
	struct nouveau_drm *drm = bl_get_data(bd);
	struct nouveau_device *device = nv_device(drm->device);
	int val = bd->props.brightness;
	int reg = nv_rd32(device, NV40_PMC_BACKLIGHT);

	nv_wr32(device, NV40_PMC_BACKLIGHT,
		 (val << 16) | (reg & ~NV40_PMC_BACKLIGHT_MASK));

	return 0;
}

static const struct backlight_ops nv40_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = nv40_get_intensity,
	.update_status = nv40_set_intensity,
};

static int
nv40_backlight_init(struct drm_connector *connector)
{
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nouveau_device *device = nv_device(drm->device);
	struct backlight_properties props;
	struct backlight_device *bd;

	if (!(nv_rd32(device, NV40_PMC_BACKLIGHT) & NV40_PMC_BACKLIGHT_MASK))
		return 0;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = 31;
	bd = backlight_device_register("nv_backlight", connector->kdev, drm,
				       &nv40_bl_ops, &props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);
	drm->backlight = bd;
	bd->props.brightness = nv40_get_intensity(bd);
	backlight_update_status(bd);

	return 0;
}

static int
nv50_get_intensity(struct backlight_device *bd)
{
	struct nouveau_encoder *nv_encoder = bl_get_data(bd);
	struct nouveau_drm *drm = nouveau_drm(nv_encoder->base.base.dev);
	struct nouveau_device *device = nv_device(drm->device);
	int or = nv_encoder->or;
	u32 div = 1025;
	u32 val;

	val  = nv_rd32(device, NV50_PDISP_SOR_PWM_CTL(or));
	val &= NV50_PDISP_SOR_PWM_CTL_VAL;
	return ((val * 100) + (div / 2)) / div;
}

static int
nv50_set_intensity(struct backlight_device *bd)
{
	struct nouveau_encoder *nv_encoder = bl_get_data(bd);
	struct nouveau_drm *drm = nouveau_drm(nv_encoder->base.base.dev);
	struct nouveau_device *device = nv_device(drm->device);
	int or = nv_encoder->or;
	u32 div = 1025;
	u32 val = (bd->props.brightness * div) / 100;

	nv_wr32(device, NV50_PDISP_SOR_PWM_CTL(or),
			NV50_PDISP_SOR_PWM_CTL_NEW | val);
	return 0;
}

static const struct backlight_ops nv50_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = nv50_get_intensity,
	.update_status = nv50_set_intensity,
};

static int
nva3_get_intensity(struct backlight_device *bd)
{
	struct nouveau_encoder *nv_encoder = bl_get_data(bd);
	struct nouveau_drm *drm = nouveau_drm(nv_encoder->base.base.dev);
	struct nouveau_device *device = nv_device(drm->device);
	int or = nv_encoder->or;
	u32 div, val;

	div  = nv_rd32(device, NV50_PDISP_SOR_PWM_DIV(or));
	val  = nv_rd32(device, NV50_PDISP_SOR_PWM_CTL(or));
	val &= NVA3_PDISP_SOR_PWM_CTL_VAL;
	if (div && div >= val)
		return ((val * 100) + (div / 2)) / div;

	return 100;
}

static int
nva3_set_intensity(struct backlight_device *bd)
{
	struct nouveau_encoder *nv_encoder = bl_get_data(bd);
	struct nouveau_drm *drm = nouveau_drm(nv_encoder->base.base.dev);
	struct nouveau_device *device = nv_device(drm->device);
	int or = nv_encoder->or;
	u32 div, val;

	div = nv_rd32(device, NV50_PDISP_SOR_PWM_DIV(or));
	val = (bd->props.brightness * div) / 100;
	if (div) {
		nv_wr32(device, NV50_PDISP_SOR_PWM_CTL(or), val |
				NV50_PDISP_SOR_PWM_CTL_NEW |
				NVA3_PDISP_SOR_PWM_CTL_UNK);
		return 0;
	}

	return -EINVAL;
}

static const struct backlight_ops nva3_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = nva3_get_intensity,
	.update_status = nva3_set_intensity,
};

static int
nv50_backlight_init(struct drm_connector *connector)
{
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nouveau_device *device = nv_device(drm->device);
	struct nouveau_encoder *nv_encoder;
	struct backlight_properties props;
	struct backlight_device *bd;
	const struct backlight_ops *ops;

	nv_encoder = find_encoder(connector, DCB_OUTPUT_LVDS);
	if (!nv_encoder) {
		nv_encoder = find_encoder(connector, DCB_OUTPUT_DP);
		if (!nv_encoder)
			return -ENODEV;
	}

	if (!nv_rd32(device, NV50_PDISP_SOR_PWM_CTL(nv_encoder->or)))
		return 0;

	if (device->chipset <= 0xa0 ||
	    device->chipset == 0xaa ||
	    device->chipset == 0xac)
		ops = &nv50_bl_ops;
	else
		ops = &nva3_bl_ops;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = 100;
	bd = backlight_device_register("nv_backlight", connector->kdev,
				       nv_encoder, ops, &props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	drm->backlight = bd;
	bd->props.brightness = bd->ops->get_brightness(bd);
	backlight_update_status(bd);
	return 0;
}

int
nouveau_backlight_init(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_device *device = nv_device(drm->device);
	struct drm_connector *connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->connector_type != DRM_MODE_CONNECTOR_LVDS &&
		    connector->connector_type != DRM_MODE_CONNECTOR_eDP)
			continue;

		switch (device->card_type) {
		case NV_40:
			return nv40_backlight_init(connector);
		case NV_50:
		case NV_C0:
		case NV_D0:
		case NV_E0:
			return nv50_backlight_init(connector);
		default:
			break;
		}
	}


	return 0;
}

void
nouveau_backlight_exit(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (drm->backlight) {
		backlight_device_unregister(drm->backlight);
		drm->backlight = NULL;
	}
}
