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

#include <linux/dma-mapping.h>
#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"

#define MEM_SYNC 0xe0000001
#define MEM_VRAM 0xe0010000

struct nvd0_display {
	struct nouveau_gpuobj *mem;
	struct {
		dma_addr_t handle;
		u32 *ptr;
	} evo[1];
};

static struct nvd0_display *
nvd0_display(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	return dev_priv->engine.display.priv;
}

static int
evo_icmd(struct drm_device *dev, int id, u32 mthd, u32 data)
{
	int ret = 0;
	nv_mask(dev, 0x610700 + (id * 0x10), 0x00000001, 0x00000001);
	nv_wr32(dev, 0x610704 + (id * 0x10), data);
	nv_mask(dev, 0x610704 + (id * 0x10), 0x80000ffc, 0x80000000 | mthd);
	if (!nv_wait(dev, 0x610704 + (id * 0x10), 0x80000000, 0x00000000))
		ret = -EBUSY;
	nv_mask(dev, 0x610700 + (id * 0x10), 0x00000001, 0x00000000);
	return ret;
}

static u32 *
evo_wait(struct drm_device *dev, int id, int nr)
{
	struct nvd0_display *disp = nvd0_display(dev);
	u32 put = nv_rd32(dev, 0x640000 + (id * 0x1000)) / 4;

	if (put + nr >= (PAGE_SIZE / 4)) {
		disp->evo[id].ptr[put] = 0x20000000;

		nv_wr32(dev, 0x640000 + (id * 0x1000), 0x00000000);
		if (!nv_wait(dev, 0x640004 + (id * 0x1000), ~0, 0x00000000)) {
			NV_ERROR(dev, "evo %d dma stalled\n", id);
			return NULL;
		}

		put = 0;
	}

	return disp->evo[id].ptr + put;
}

static void
evo_kick(u32 *push, struct drm_device *dev, int id)
{
	struct nvd0_display *disp = nvd0_display(dev);
	nv_wr32(dev, 0x640000 + (id * 0x1000), (push - disp->evo[id].ptr) << 2);
}

#define evo_mthd(p,m,s) *((p)++) = (((s) << 18) | (m))
#define evo_data(p,d)   *((p)++) = (d)

/******************************************************************************
 * DAC
 *****************************************************************************/

/******************************************************************************
 * SOR
 *****************************************************************************/

/******************************************************************************
 * IRQ
 *****************************************************************************/
static void
nvd0_display_intr(struct drm_device *dev)
{
	u32 intr = nv_rd32(dev, 0x610088);

	if (intr & 0x00000002) {
		u32 stat = nv_rd32(dev, 0x61009c);
		int chid = ffs(stat) - 1;
		if (chid >= 0) {
			u32 mthd = nv_rd32(dev, 0x6101f0 + (chid * 12));
			u32 data = nv_rd32(dev, 0x6101f4 + (chid * 12));
			u32 unkn = nv_rd32(dev, 0x6101f8 + (chid * 12));

			NV_INFO(dev, "EvoCh: chid %d mthd 0x%04x data 0x%08x "
				     "0x%08x 0x%08x\n",
				chid, (mthd & 0x0000ffc), data, mthd, unkn);
			nv_wr32(dev, 0x61009c, (1 << chid));
			nv_wr32(dev, 0x6101f0 + (chid * 12), 0x90000000);
		}

		intr &= ~0x00000002;
	}

	if (intr & 0x01000000) {
		u32 stat = nv_rd32(dev, 0x6100bc);
		nv_wr32(dev, 0x6100bc, stat);
		intr &= ~0x01000000;
	}

	if (intr & 0x02000000) {
		u32 stat = nv_rd32(dev, 0x6108bc);
		nv_wr32(dev, 0x6108bc, stat);
		intr &= ~0x02000000;
	}

	if (intr)
		NV_INFO(dev, "PDISP: unknown intr 0x%08x\n", intr);
}

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
	u32 *push;
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
	nv_wr32(dev, 0x610494, (disp->evo[0].handle >> 8) | 3);
	nv_wr32(dev, 0x610498, 0x00010000);
	nv_wr32(dev, 0x61049c, 0x00000001);
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

	push = evo_wait(dev, 0, 32);
	if (!push)
		return -EBUSY;
	evo_mthd(push, 0x0088, 1);
	evo_data(push, MEM_SYNC);
	evo_mthd(push, 0x0084, 1);
	evo_data(push, 0x00000000);
	evo_mthd(push, 0x0084, 1);
	evo_data(push, 0x80000000);
	evo_mthd(push, 0x008c, 1);
	evo_data(push, 0x00000000);
	evo_kick(push, dev, 0);

	return 0;
}

void
nvd0_display_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvd0_display *disp = nvd0_display(dev);
	struct pci_dev *pdev = dev->pdev;

	nvd0_display_fini(dev);

	pci_free_consistent(pdev, PAGE_SIZE, disp->evo[0].ptr, disp->evo[0].handle);
	nouveau_gpuobj_ref(NULL, &disp->mem);
	nouveau_irq_unregister(dev, 26);

	dev_priv->engine.display.priv = NULL;
	kfree(disp);
}

int
nvd0_display_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	struct pci_dev *pdev = dev->pdev;
	struct nvd0_display *disp;
	int ret;

	disp = kzalloc(sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;
	dev_priv->engine.display.priv = disp;

	/* setup interrupt handling */
	nouveau_irq_register(dev, 26, nvd0_display_intr);

	/* hash table and dma objects for the memory areas we care about */
	ret = nouveau_gpuobj_new(dev, NULL, 0x4000, 0x10000,
				 NVOBJ_FLAG_ZERO_ALLOC, &disp->mem);
	if (ret)
		goto out;

	nv_wo32(disp->mem, 0x1000, 0x00000049);
	nv_wo32(disp->mem, 0x1004, (disp->mem->vinst + 0x2000) >> 8);
	nv_wo32(disp->mem, 0x1008, (disp->mem->vinst + 0x2fff) >> 8);
	nv_wo32(disp->mem, 0x100c, 0x00000000);
	nv_wo32(disp->mem, 0x1010, 0x00000000);
	nv_wo32(disp->mem, 0x1014, 0x00000000);
	nv_wo32(disp->mem, 0x0000, MEM_SYNC);
	nv_wo32(disp->mem, 0x0004, (0x1000 << 9) | 0x00000001);

	nv_wo32(disp->mem, 0x1020, 0x00000009);
	nv_wo32(disp->mem, 0x1024, 0x00000000);
	nv_wo32(disp->mem, 0x1028, (dev_priv->vram_size - 1) >> 8);
	nv_wo32(disp->mem, 0x102c, 0x00000000);
	nv_wo32(disp->mem, 0x1030, 0x00000000);
	nv_wo32(disp->mem, 0x1034, 0x00000000);
	nv_wo32(disp->mem, 0x0008, MEM_VRAM);
	nv_wo32(disp->mem, 0x000c, (0x1020 << 9) | 0x00000001);

	pinstmem->flush(dev);

	/* push buffers for evo channels */
	disp->evo[0].ptr =
		pci_alloc_consistent(pdev, PAGE_SIZE, &disp->evo[0].handle);
	if (!disp->evo[0].ptr) {
		ret = -ENOMEM;
		goto out;
	}

	ret = nvd0_display_init(dev);
	if (ret)
		goto out;

out:
	if (ret)
		nvd0_display_destroy(dev);
	return ret;
}
