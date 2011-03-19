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
#include <linux/acpi.h>

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"
#include "nouveau_reg.h"

static int nv40_get_intensity(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(bd);
	int val = (nv_rd32(dev, NV40_PMC_BACKLIGHT) & NV40_PMC_BACKLIGHT_MASK)
									>> 16;

	return val;
}

static int nv40_set_intensity(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(bd);
	int val = bd->props.brightness;
	int reg = nv_rd32(dev, NV40_PMC_BACKLIGHT);

	nv_wr32(dev, NV40_PMC_BACKLIGHT,
		 (val << 16) | (reg & ~NV40_PMC_BACKLIGHT_MASK));

	return 0;
}

static const struct backlight_ops nv40_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = nv40_get_intensity,
	.update_status = nv40_set_intensity,
};

static int nv50_get_intensity(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(bd);

	return nv_rd32(dev, NV50_PDISPLAY_SOR_BACKLIGHT);
}

static int nv50_set_intensity(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(bd);
	int val = bd->props.brightness;

	nv_wr32(dev, NV50_PDISPLAY_SOR_BACKLIGHT,
		val | NV50_PDISPLAY_SOR_BACKLIGHT_ENABLE);
	return 0;
}

static const struct backlight_ops nv50_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = nv50_get_intensity,
	.update_status = nv50_set_intensity,
};

static int nouveau_nv40_backlight_init(struct drm_device *dev)
{
	struct backlight_properties props;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct backlight_device *bd;

	if (!(nv_rd32(dev, NV40_PMC_BACKLIGHT) & NV40_PMC_BACKLIGHT_MASK))
		return 0;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = 31;
	bd = backlight_device_register("nv_backlight", &dev->pdev->dev, dev,
				       &nv40_bl_ops, &props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	dev_priv->backlight = bd;
	bd->props.brightness = nv40_get_intensity(bd);
	backlight_update_status(bd);

	return 0;
}

static int nouveau_nv50_backlight_init(struct drm_device *dev)
{
	struct backlight_properties props;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct backlight_device *bd;

	if (!nv_rd32(dev, NV50_PDISPLAY_SOR_BACKLIGHT))
		return 0;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = 1025;
	bd = backlight_device_register("nv_backlight", &dev->pdev->dev, dev,
				       &nv50_bl_ops, &props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	dev_priv->backlight = bd;
	bd->props.brightness = nv50_get_intensity(bd);
	backlight_update_status(bd);
	return 0;
}

int nouveau_backlight_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

#ifdef CONFIG_ACPI
	if (acpi_video_backlight_support()) {
		NV_INFO(dev, "ACPI backlight interface available, "
			     "not registering our own\n");
		return 0;
	}
#endif

	switch (dev_priv->card_type) {
	case NV_40:
		return nouveau_nv40_backlight_init(dev);
	case NV_50:
		return nouveau_nv50_backlight_init(dev);
	default:
		break;
	}

	return 0;
}

void nouveau_backlight_exit(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->backlight) {
		backlight_device_unregister(dev_priv->backlight);
		dev_priv->backlight = NULL;
	}
}
