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

#include <linux/apple-gmux.h>
#include <linux/backlight.h>
#include <linux/idr.h>

#include "nouveau_drv.h"
#include "nouveau_reg.h"
#include "nouveau_encoder.h"
#include "nouveau_connector.h"

static struct ida bl_ida;
#define BL_NAME_SIZE 15 // 12 for name + 2 for digits + 1 for '\0'

static bool
nouveau_get_backlight_name(char backlight_name[BL_NAME_SIZE],
			   struct nouveau_backlight *bl)
{
	const int nb = ida_alloc_max(&bl_ida, 99, GFP_KERNEL);

	if (nb < 0)
		return false;
	if (nb > 0)
		snprintf(backlight_name, BL_NAME_SIZE, "nv_backlight%d", nb);
	else
		snprintf(backlight_name, BL_NAME_SIZE, "nv_backlight");
	bl->id = nb;
	return true;
}

static int
nv40_get_intensity(struct backlight_device *bd)
{
	struct nouveau_encoder *nv_encoder = bl_get_data(bd);
	struct nouveau_drm *drm = nouveau_drm(nv_encoder->base.base.dev);
	struct nvif_object *device = &drm->client.device.object;
	int val = (nvif_rd32(device, NV40_PMC_BACKLIGHT) &
		   NV40_PMC_BACKLIGHT_MASK) >> 16;

	return val;
}

static int
nv40_set_intensity(struct backlight_device *bd)
{
	struct nouveau_encoder *nv_encoder = bl_get_data(bd);
	struct nouveau_drm *drm = nouveau_drm(nv_encoder->base.base.dev);
	struct nvif_object *device = &drm->client.device.object;
	int val = bd->props.brightness;
	int reg = nvif_rd32(device, NV40_PMC_BACKLIGHT);

	nvif_wr32(device, NV40_PMC_BACKLIGHT,
		  (val << 16) | (reg & ~NV40_PMC_BACKLIGHT_MASK));

	return 0;
}

static const struct backlight_ops nv40_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = nv40_get_intensity,
	.update_status = nv40_set_intensity,
};

static int
nv40_backlight_init(struct nouveau_encoder *encoder,
		    struct backlight_properties *props,
		    const struct backlight_ops **ops)
{
	struct nouveau_drm *drm = nouveau_drm(encoder->base.base.dev);
	struct nvif_object *device = &drm->client.device.object;

	if (!(nvif_rd32(device, NV40_PMC_BACKLIGHT) & NV40_PMC_BACKLIGHT_MASK))
		return -ENODEV;

	props->max_brightness = 31;
	*ops = &nv40_bl_ops;
	return 0;
}

static int
nv50_get_intensity(struct backlight_device *bd)
{
	struct nouveau_encoder *nv_encoder = bl_get_data(bd);
	struct nouveau_drm *drm = nouveau_drm(nv_encoder->base.base.dev);
	struct nvif_object *device = &drm->client.device.object;
	int or = ffs(nv_encoder->dcb->or) - 1;
	u32 div = 1025;
	u32 val;

	val  = nvif_rd32(device, NV50_PDISP_SOR_PWM_CTL(or));
	val &= NV50_PDISP_SOR_PWM_CTL_VAL;
	return ((val * 100) + (div / 2)) / div;
}

static int
nv50_set_intensity(struct backlight_device *bd)
{
	struct nouveau_encoder *nv_encoder = bl_get_data(bd);
	struct nouveau_drm *drm = nouveau_drm(nv_encoder->base.base.dev);
	struct nvif_object *device = &drm->client.device.object;
	int or = ffs(nv_encoder->dcb->or) - 1;
	u32 div = 1025;
	u32 val = (bd->props.brightness * div) / 100;

	nvif_wr32(device, NV50_PDISP_SOR_PWM_CTL(or),
		  NV50_PDISP_SOR_PWM_CTL_NEW | val);
	return 0;
}

static const struct backlight_ops nv50_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = nv50_get_intensity,
	.update_status = nv50_set_intensity,
};

/*
 * eDP brightness callbacks need to happen under lock, since we need to
 * enable/disable the backlight ourselves for modesets
 */
static int
nv50_edp_get_brightness(struct backlight_device *bd)
{
	struct drm_connector *connector = dev_get_drvdata(bd->dev.parent);
	struct drm_device *dev = connector->dev;
	struct drm_crtc *crtc;
	struct drm_modeset_acquire_ctx ctx;
	int ret = 0;

	drm_modeset_acquire_init(&ctx, 0);

retry:
	ret = drm_modeset_lock(&dev->mode_config.connection_mutex, &ctx);
	if (ret == -EDEADLK)
		goto deadlock;
	else if (ret < 0)
		goto out;

	crtc = connector->state->crtc;
	if (!crtc)
		goto out;

	ret = drm_modeset_lock(&crtc->mutex, &ctx);
	if (ret == -EDEADLK)
		goto deadlock;
	else if (ret < 0)
		goto out;

	if (!crtc->state->active)
		goto out;

	ret = bd->props.brightness;
out:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	return ret;
deadlock:
	drm_modeset_backoff(&ctx);
	goto retry;
}

static int
nv50_edp_set_brightness(struct backlight_device *bd)
{
	struct drm_connector *connector = dev_get_drvdata(bd->dev.parent);
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct drm_device *dev = connector->dev;
	struct drm_crtc *crtc;
	struct drm_dp_aux *aux = &nv_connector->aux;
	struct nouveau_backlight *nv_bl = nv_connector->backlight;
	struct drm_modeset_acquire_ctx ctx;
	int ret = 0;

	drm_modeset_acquire_init(&ctx, 0);
retry:
	ret = drm_modeset_lock(&dev->mode_config.connection_mutex, &ctx);
	if (ret == -EDEADLK)
		goto deadlock;
	else if (ret < 0)
		goto out;

	crtc = connector->state->crtc;
	if (!crtc)
		goto out;

	ret = drm_modeset_lock(&crtc->mutex, &ctx);
	if (ret == -EDEADLK)
		goto deadlock;
	else if (ret < 0)
		goto out;

	if (crtc->state->active)
		ret = drm_edp_backlight_set_level(aux, &nv_bl->edp_info, bd->props.brightness);

out:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	return ret;
deadlock:
	drm_modeset_backoff(&ctx);
	goto retry;
}

static const struct backlight_ops nv50_edp_bl_ops = {
	.get_brightness = nv50_edp_get_brightness,
	.update_status = nv50_edp_set_brightness,
};

static int
nva3_get_intensity(struct backlight_device *bd)
{
	struct nouveau_encoder *nv_encoder = bl_get_data(bd);
	struct nouveau_drm *drm = nouveau_drm(nv_encoder->base.base.dev);
	struct nvif_object *device = &drm->client.device.object;
	int or = ffs(nv_encoder->dcb->or) - 1;
	u32 div, val;

	div  = nvif_rd32(device, NV50_PDISP_SOR_PWM_DIV(or));
	val  = nvif_rd32(device, NV50_PDISP_SOR_PWM_CTL(or));
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
	struct nvif_object *device = &drm->client.device.object;
	int or = ffs(nv_encoder->dcb->or) - 1;
	u32 div, val;

	div = nvif_rd32(device, NV50_PDISP_SOR_PWM_DIV(or));
	val = (bd->props.brightness * div) / 100;
	if (div) {
		nvif_wr32(device, NV50_PDISP_SOR_PWM_CTL(or),
			  val |
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

/* FIXME: perform backlight probing for eDP _before_ this, this only gets called after connector
 * registration which happens after the initial modeset
 */
static int
nv50_backlight_init(struct nouveau_backlight *bl,
		    struct nouveau_connector *nv_conn,
		    struct nouveau_encoder *nv_encoder,
		    struct backlight_properties *props,
		    const struct backlight_ops **ops)
{
	struct nouveau_drm *drm = nouveau_drm(nv_encoder->base.base.dev);
	struct nvif_object *device = &drm->client.device.object;

	if (!nvif_rd32(device, NV50_PDISP_SOR_PWM_CTL(ffs(nv_encoder->dcb->or) - 1)) ||
	    nv_conn->base.status != connector_status_connected)
		return -ENODEV;

	if (nv_conn->type == DCB_CONNECTOR_eDP) {
		int ret;
		u16 current_level;
		u8 edp_dpcd[EDP_DISPLAY_CTL_CAP_SIZE];
		u8 current_mode;

		ret = drm_dp_dpcd_read(&nv_conn->aux, DP_EDP_DPCD_REV, edp_dpcd,
				       EDP_DISPLAY_CTL_CAP_SIZE);
		if (ret < 0)
			return ret;

		if (drm_edp_backlight_supported(edp_dpcd)) {
			NV_DEBUG(drm, "DPCD backlight controls supported on %s\n",
				 nv_conn->base.name);

			ret = drm_edp_backlight_init(&nv_conn->aux, &bl->edp_info, 0, edp_dpcd,
						     &current_level, &current_mode);
			if (ret < 0)
				return ret;

			ret = drm_edp_backlight_enable(&nv_conn->aux, &bl->edp_info, current_level);
			if (ret < 0) {
				NV_ERROR(drm, "Failed to enable backlight on %s: %d\n",
					 nv_conn->base.name, ret);
				return ret;
			}

			*ops = &nv50_edp_bl_ops;
			props->brightness = current_level;
			props->max_brightness = bl->edp_info.max;
			bl->uses_dpcd = true;
			return 0;
		}
	}

	if (drm->client.device.info.chipset <= 0xa0 ||
	    drm->client.device.info.chipset == 0xaa ||
	    drm->client.device.info.chipset == 0xac)
		*ops = &nv50_bl_ops;
	else
		*ops = &nva3_bl_ops;

	props->max_brightness = 100;

	return 0;
}

int
nouveau_backlight_init(struct drm_connector *connector)
{
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nouveau_backlight *bl;
	struct nouveau_encoder *nv_encoder = NULL;
	struct nvif_device *device = &drm->client.device;
	char backlight_name[BL_NAME_SIZE];
	struct backlight_properties props = {0};
	const struct backlight_ops *ops;
	int ret;

	if (apple_gmux_present()) {
		NV_INFO_ONCE(drm, "Apple GMUX detected: not registering Nouveau backlight interface\n");
		return 0;
	}

	if (connector->connector_type == DRM_MODE_CONNECTOR_LVDS)
		nv_encoder = find_encoder(connector, DCB_OUTPUT_LVDS);
	else if (connector->connector_type == DRM_MODE_CONNECTOR_eDP)
		nv_encoder = find_encoder(connector, DCB_OUTPUT_DP);
	else
		return 0;

	if (!nv_encoder)
		return 0;

	bl = kzalloc(sizeof(*bl), GFP_KERNEL);
	if (!bl)
		return -ENOMEM;

	switch (device->info.family) {
	case NV_DEVICE_INFO_V0_CURIE:
		ret = nv40_backlight_init(nv_encoder, &props, &ops);
		break;
	case NV_DEVICE_INFO_V0_TESLA:
	case NV_DEVICE_INFO_V0_FERMI:
	case NV_DEVICE_INFO_V0_KEPLER:
	case NV_DEVICE_INFO_V0_MAXWELL:
	case NV_DEVICE_INFO_V0_PASCAL:
	case NV_DEVICE_INFO_V0_VOLTA:
	case NV_DEVICE_INFO_V0_TURING:
	case NV_DEVICE_INFO_V0_AMPERE: //XXX: not confirmed
		ret = nv50_backlight_init(bl, nouveau_connector(connector),
					  nv_encoder, &props, &ops);
		break;
	default:
		ret = 0;
		goto fail_alloc;
	}

	if (ret) {
		if (ret == -ENODEV)
			ret = 0;
		goto fail_alloc;
	}

	if (!nouveau_get_backlight_name(backlight_name, bl)) {
		NV_ERROR(drm, "Failed to retrieve a unique name for the backlight interface\n");
		goto fail_alloc;
	}

	props.type = BACKLIGHT_RAW;
	bl->dev = backlight_device_register(backlight_name, connector->kdev,
					    nv_encoder, ops, &props);
	if (IS_ERR(bl->dev)) {
		if (bl->id >= 0)
			ida_free(&bl_ida, bl->id);
		ret = PTR_ERR(bl->dev);
		goto fail_alloc;
	}

	nouveau_connector(connector)->backlight = bl;
	if (!bl->dev->props.brightness)
		bl->dev->props.brightness =
			bl->dev->ops->get_brightness(bl->dev);
	backlight_update_status(bl->dev);

	return 0;

fail_alloc:
	kfree(bl);
	return ret;
}

void
nouveau_backlight_fini(struct drm_connector *connector)
{
	struct nouveau_connector *nv_conn = nouveau_connector(connector);
	struct nouveau_backlight *bl = nv_conn->backlight;

	if (!bl)
		return;

	if (bl->id >= 0)
		ida_free(&bl_ida, bl->id);

	backlight_device_unregister(bl->dev);
	nv_conn->backlight = NULL;
	kfree(bl);
}

void
nouveau_backlight_ctor(void)
{
	ida_init(&bl_ida);
}

void
nouveau_backlight_dtor(void)
{
	ida_destroy(&bl_ida);
}
