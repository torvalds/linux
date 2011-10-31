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
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

struct nvc0_fb_priv {
	struct page *r100c10_page;
	dma_addr_t r100c10;
};

static inline void
nvc0_mfb_subp_isr(struct drm_device *dev, int unit, int subp)
{
	u32 subp_base = 0x141000 + (unit * 0x2000) + (subp * 0x400);
	u32 stat = nv_rd32(dev, subp_base + 0x020);

	if (stat) {
		NV_INFO(dev, "PMFB%d_SUBP%d: 0x%08x\n", unit, subp, stat);
		nv_wr32(dev, subp_base + 0x020, stat);
	}
}

static void
nvc0_mfb_isr(struct drm_device *dev)
{
	u32 units = nv_rd32(dev, 0x00017c);
	while (units) {
		u32 subp, unit = ffs(units) - 1;
		for (subp = 0; subp < 2; subp++)
			nvc0_mfb_subp_isr(dev, unit, subp);
		units &= ~(1 << unit);
	}
}

static void
nvc0_fb_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fb_engine *pfb = &dev_priv->engine.fb;
	struct nvc0_fb_priv *priv = pfb->priv;

	nouveau_irq_unregister(dev, 25);

	if (priv->r100c10_page) {
		pci_unmap_page(dev->pdev, priv->r100c10, PAGE_SIZE,
			       PCI_DMA_BIDIRECTIONAL);
		__free_page(priv->r100c10_page);
	}

	kfree(priv);
	pfb->priv = NULL;
}

static int
nvc0_fb_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fb_engine *pfb = &dev_priv->engine.fb;
	struct nvc0_fb_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	pfb->priv = priv;

	priv->r100c10_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!priv->r100c10_page) {
		nvc0_fb_destroy(dev);
		return -ENOMEM;
	}

	priv->r100c10 = pci_map_page(dev->pdev, priv->r100c10_page, 0,
				     PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	if (pci_dma_mapping_error(dev->pdev, priv->r100c10)) {
		nvc0_fb_destroy(dev);
		return -EFAULT;
	}

	nouveau_irq_register(dev, 25, nvc0_mfb_isr);
	return 0;
}

int
nvc0_fb_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_fb_priv *priv;
	int ret;

	if (!dev_priv->engine.fb.priv) {
		ret = nvc0_fb_create(dev);
		if (ret)
			return ret;
	}
	priv = dev_priv->engine.fb.priv;

	nv_wr32(dev, 0x100c10, priv->r100c10 >> 8);
	return 0;
}

void
nvc0_fb_takedown(struct drm_device *dev)
{
	nvc0_fb_destroy(dev);
}
