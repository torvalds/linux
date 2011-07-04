/*
 * Copyright 2011 Red Hat Inc.
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

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"

struct nvd0_display {
	struct nouveau_gpuobj *mem;
};

static struct nvd0_display *
nvd0_display(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	return dev_priv->engine.display.priv;
}

/******************************************************************************
 * DAC
 *****************************************************************************/

/******************************************************************************
 * SOR
 *****************************************************************************/

/******************************************************************************
 * IRQ
 *****************************************************************************/

/******************************************************************************
 * Init
 *****************************************************************************/
static void
nvd0_display_fini(struct drm_device *dev)
{
	int i;

	/* fini cursors */
	for (i = 14; i >= 13; i--) {
		if (!(nv_rd32(dev, 0x610490 + (i * 0x10)) & 0x00000001))
			continue;

		nv_mask(dev, 0x610490 + (i * 0x10), 0x00000001, 0x00000000);
		nv_wait(dev, 0x610490 + (i * 0x10), 0x00010000, 0x00000000);
		nv_mask(dev, 0x610090, 1 << i, 0x00000000);
		nv_mask(dev, 0x6100a0, 1 << i, 0x00000000);
	}

	/* fini master */
	if (nv_rd32(dev, 0x610490) & 0x00000010) {
		nv_mask(dev, 0x610490, 0x00000010, 0x00000000);
		nv_mask(dev, 0x610490, 0x00000003, 0x00000000);
		nv_wait(dev, 0x610490, 0x80000000, 0x00000000);
		nv_mask(dev, 0x610090, 0x00000001, 0x00000000);
		nv_mask(dev, 0x6100a0, 0x00000001, 0x00000000);
	}
}

int
nvd0_display_init(struct drm_device *dev)
{
	struct nvd0_display *disp = nvd0_display(dev);
	int i;

	if (nv_rd32(dev, 0x6100ac) & 0x00000100) {
		nv_wr32(dev, 0x6100ac, 0x00000100);
		nv_mask(dev, 0x6194e8, 0x00000001, 0x00000000);
		if (!nv_wait(dev, 0x6194e8, 0x00000002, 0x00000000)) {
			NV_ERROR(dev, "PDISP: 0x6194e8 0x%08x\n",
				 nv_rd32(dev, 0x6194e8));
			return -EBUSY;
		}
	}

	nv_wr32(dev, 0x610010, (disp->mem->vinst >> 8) | 9);

	/* init master */
	nv_wr32(dev, 0x610494, ((disp->mem->vinst + 0x1000) >> 8) | 1);
	nv_wr32(dev, 0x610498, 0x00010000);
	nv_wr32(dev, 0x61049c, 0x00000000);
	nv_mask(dev, 0x610490, 0x00000010, 0x00000010);
	nv_wr32(dev, 0x640000, 0x00000000);
	nv_wr32(dev, 0x610490, 0x01000013);
	if (!nv_wait(dev, 0x610490, 0x80000000, 0x00000000)) {
		NV_ERROR(dev, "PDISP: master 0x%08x\n",
			 nv_rd32(dev, 0x610490));
		return -EBUSY;
	}
	nv_mask(dev, 0x610090, 0x00000001, 0x00000001);
	nv_mask(dev, 0x6100a0, 0x00000001, 0x00000001);

	/* init cursors */
	for (i = 13; i <= 14; i++) {
		nv_wr32(dev, 0x610490 + (i * 0x10), 0x00000001);
		if (!nv_wait(dev, 0x610490 + (i * 0x10), 0x00010000, 0x00010000)) {
			NV_ERROR(dev, "PDISP: curs%d 0x%08x\n", i,
				 nv_rd32(dev, 0x610490 + (i * 0x10)));
			return -EBUSY;
		}

		nv_mask(dev, 0x610090, 1 << i, 1 << i);
		nv_mask(dev, 0x6100a0, 1 << i, 1 << i);
	}

	return 0;
}

void
nvd0_display_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvd0_display *disp = nvd0_display(dev);

	nvd0_display_fini(dev);

	dev_priv->engine.display.priv = NULL;
	nouveau_gpuobj_ref(NULL, &disp->mem);
	kfree(disp);
}

int
nvd0_display_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvd0_display *disp;
	int ret;

	disp = kzalloc(sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;
	dev_priv->engine.display.priv = disp;

	ret = nouveau_gpuobj_new(dev, NULL, 8 * 1024, 0x1000, 0, &disp->mem);
	if (ret)
		goto out;

	ret = nvd0_display_init(dev);
	if (ret)
		goto out;

out:
	if (ret)
		nvd0_display_destroy(dev);
	return ret;
}
