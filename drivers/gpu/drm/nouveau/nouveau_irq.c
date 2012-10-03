/*
 * Copyright 2012 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */

#include <subdev/mc.h>

#include "nouveau_drm.h"
#include "nouveau_irq.h"
#include "nv50_display.h"

void
nouveau_irq_preinstall(struct drm_device *dev)
{
	nv_wr32(nouveau_dev(dev), 0x000140, 0x00000000);
}

int
nouveau_irq_postinstall(struct drm_device *dev)
{
	nv_wr32(nouveau_dev(dev), 0x000140, 0x00000001);
	return 0;
}

void
nouveau_irq_uninstall(struct drm_device *dev)
{
	nv_wr32(nouveau_dev(dev), 0x000140, 0x00000000);
}

irqreturn_t
nouveau_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = arg;
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_mc *pmc = nouveau_mc(device);
	u32 stat;

	stat = nv_rd32(device, 0x000100);
	if (stat == 0 || stat == ~0)
		return IRQ_NONE;

	nv_subdev(pmc)->intr(nv_subdev(pmc));

	if (device->card_type >= NV_D0) {
		if (nv_rd32(device, 0x000100) & 0x04000000)
			nvd0_display_intr(dev);
	} else
	if (device->card_type >= NV_50) {
		if (nv_rd32(device, 0x000100) & 0x04000000)
			nv50_display_intr(dev);
	}

	return IRQ_HANDLED;
}

int
nouveau_irq_init(struct drm_device *dev)
{
	return drm_irq_install(dev);
}

void
nouveau_irq_fini(struct drm_device *dev)
{
	drm_irq_uninstall(dev);
}
