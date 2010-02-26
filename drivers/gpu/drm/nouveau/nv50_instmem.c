/*
 * Copyright (C) 2007 Ben Skeggs.
 *
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

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"

struct nv50_instmem_priv {
	uint32_t save1700[5]; /* 0x1700->0x1710 */

	struct nouveau_gpuobj_ref *pramin_pt;
	struct nouveau_gpuobj_ref *pramin_bar;
	struct nouveau_gpuobj_ref *fb_bar;

	bool last_access_wr;
};

#define NV50_INSTMEM_PAGE_SHIFT 12
#define NV50_INSTMEM_PAGE_SIZE  (1 << NV50_INSTMEM_PAGE_SHIFT)
#define NV50_INSTMEM_PT_SIZE(a)	(((a) >> 12) << 3)

/*NOTE: - Assumes 0x1700 already covers the correct MiB of PRAMIN
 */
#define BAR0_WI32(g, o, v) do {                                   \
	uint32_t offset;                                          \
	if ((g)->im_backing) {                                    \
		offset = (g)->im_backing_start;                   \
	} else {                                                  \
		offset  = chan->ramin->gpuobj->im_backing_start;  \
		offset += (g)->im_pramin->start;                  \
	}                                                         \
	offset += (o);                                            \
	nv_wr32(dev, NV_RAMIN + (offset & 0xfffff), (v));              \
} while (0)

int
nv50_instmem_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan;
	uint32_t c_offset, c_size, c_ramfc, c_vmpd, c_base, pt_size;
	struct nv50_instmem_priv *priv;
	int ret, i;
	uint32_t v, save_nv001700;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_priv->engine.instmem.priv = priv;

	/* Save state, will restore at takedown. */
	for (i = 0x1700; i <= 0x1710; i += 4)
		priv->save1700[(i-0x1700)/4] = nv_rd32(dev, i);

	if (dev_priv->chipset == 0xaa || dev_priv->chipset == 0xac)
		dev_priv->vram_sys_base = nv_rd32(dev, 0x100e10) << 12;
	else
		dev_priv->vram_sys_base = 0;

	/* Reserve the last MiB of VRAM, we should probably try to avoid
	 * setting up the below tables over the top of the VBIOS image at
	 * some point.
	 */
	dev_priv->ramin_rsvd_vram = 1 << 20;
	c_offset = nouveau_mem_fb_amount(dev) - dev_priv->ramin_rsvd_vram;
	c_size   = 128 << 10;
	c_vmpd   = ((dev_priv->chipset & 0xf0) == 0x50) ? 0x1400 : 0x200;
	c_ramfc  = ((dev_priv->chipset & 0xf0) == 0x50) ? 0x0 : 0x20;
	c_base   = c_vmpd + 0x4000;
	pt_size  = NV50_INSTMEM_PT_SIZE(dev_priv->ramin_size);

	NV_DEBUG(dev, " Rsvd VRAM base: 0x%08x\n", c_offset);
	NV_DEBUG(dev, "    VBIOS image: 0x%08x\n",
				(nv_rd32(dev, 0x619f04) & ~0xff) << 8);
	NV_DEBUG(dev, "  Aperture size: %d MiB\n", dev_priv->ramin_size >> 20);
	NV_DEBUG(dev, "        PT size: %d KiB\n", pt_size >> 10);

	/* Determine VM layout, we need to do this first to make sure
	 * we allocate enough memory for all the page tables.
	 */
	dev_priv->vm_gart_base = roundup(NV50_VM_BLOCK, NV50_VM_BLOCK);
	dev_priv->vm_gart_size = NV50_VM_BLOCK;

	dev_priv->vm_vram_base = dev_priv->vm_gart_base + dev_priv->vm_gart_size;
	dev_priv->vm_vram_size = nouveau_mem_fb_amount(dev);
	if (dev_priv->vm_vram_size > NV50_VM_MAX_VRAM)
		dev_priv->vm_vram_size = NV50_VM_MAX_VRAM;
	dev_priv->vm_vram_size = roundup(dev_priv->vm_vram_size, NV50_VM_BLOCK);
	dev_priv->vm_vram_pt_nr = dev_priv->vm_vram_size / NV50_VM_BLOCK;

	dev_priv->vm_end = dev_priv->vm_vram_base + dev_priv->vm_vram_size;

	NV_DEBUG(dev, "NV50VM: GART 0x%016llx-0x%016llx\n",
		 dev_priv->vm_gart_base,
		 dev_priv->vm_gart_base + dev_priv->vm_gart_size - 1);
	NV_DEBUG(dev, "NV50VM: VRAM 0x%016llx-0x%016llx\n",
		 dev_priv->vm_vram_base,
		 dev_priv->vm_vram_base + dev_priv->vm_vram_size - 1);

	c_size += dev_priv->vm_vram_pt_nr * (NV50_VM_BLOCK / 65536 * 8);

	/* Map BAR0 PRAMIN aperture over the memory we want to use */
	save_nv001700 = nv_rd32(dev, NV50_PUNK_BAR0_PRAMIN);
	nv_wr32(dev, NV50_PUNK_BAR0_PRAMIN, (c_offset >> 16));

	/* Create a fake channel, and use it as our "dummy" channels 0/127.
	 * The main reason for creating a channel is so we can use the gpuobj
	 * code.  However, it's probably worth noting that NVIDIA also setup
	 * their channels 0/127 with the same values they configure here.
	 * So, there may be some other reason for doing this.
	 *
	 * Have to create the entire channel manually, as the real channel
	 * creation code assumes we have PRAMIN access, and we don't until
	 * we're done here.
	 */
	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;
	chan->id = 0;
	chan->dev = dev;
	chan->file_priv = (struct drm_file *)-2;
	dev_priv->fifos[0] = dev_priv->fifos[127] = chan;

	/* Channel's PRAMIN object + heap */
	ret = nouveau_gpuobj_new_fake(dev, 0, c_offset, c_size, 0,
							NULL, &chan->ramin);
	if (ret)
		return ret;

	if (nouveau_mem_init_heap(&chan->ramin_heap, c_base, c_size - c_base))
		return -ENOMEM;

	/* RAMFC + zero channel's PRAMIN up to start of VM pagedir */
	ret = nouveau_gpuobj_new_fake(dev, c_ramfc, c_offset + c_ramfc,
						0x4000, 0, NULL, &chan->ramfc);
	if (ret)
		return ret;

	for (i = 0; i < c_vmpd; i += 4)
		BAR0_WI32(chan->ramin->gpuobj, i, 0);

	/* VM page directory */
	ret = nouveau_gpuobj_new_fake(dev, c_vmpd, c_offset + c_vmpd,
					   0x4000, 0, &chan->vm_pd, NULL);
	if (ret)
		return ret;
	for (i = 0; i < 0x4000; i += 8) {
		BAR0_WI32(chan->vm_pd, i + 0x00, 0x00000000);
		BAR0_WI32(chan->vm_pd, i + 0x04, 0x00000000);
	}

	/* PRAMIN page table, cheat and map into VM at 0x0000000000.
	 * We map the entire fake channel into the start of the PRAMIN BAR
	 */
	ret = nouveau_gpuobj_new_ref(dev, chan, NULL, 0, pt_size, 0x1000,
				     0, &priv->pramin_pt);
	if (ret)
		return ret;

	v = c_offset | 1;
	if (dev_priv->vram_sys_base) {
		v += dev_priv->vram_sys_base;
		v |= 0x30;
	}

	i = 0;
	while (v < dev_priv->vram_sys_base + c_offset + c_size) {
		BAR0_WI32(priv->pramin_pt->gpuobj, i + 0, v);
		BAR0_WI32(priv->pramin_pt->gpuobj, i + 4, 0x00000000);
		v += 0x1000;
		i += 8;
	}

	while (i < pt_size) {
		BAR0_WI32(priv->pramin_pt->gpuobj, i + 0, 0x00000000);
		BAR0_WI32(priv->pramin_pt->gpuobj, i + 4, 0x00000000);
		i += 8;
	}

	BAR0_WI32(chan->vm_pd, 0x00, priv->pramin_pt->instance | 0x63);
	BAR0_WI32(chan->vm_pd, 0x04, 0x00000000);

	/* VRAM page table(s), mapped into VM at +1GiB  */
	for (i = 0; i < dev_priv->vm_vram_pt_nr; i++) {
		ret = nouveau_gpuobj_new_ref(dev, chan, NULL, 0,
					     NV50_VM_BLOCK/65536*8, 0, 0,
					     &chan->vm_vram_pt[i]);
		if (ret) {
			NV_ERROR(dev, "Error creating VRAM page tables: %d\n",
									ret);
			dev_priv->vm_vram_pt_nr = i;
			return ret;
		}
		dev_priv->vm_vram_pt[i] = chan->vm_vram_pt[i]->gpuobj;

		for (v = 0; v < dev_priv->vm_vram_pt[i]->im_pramin->size;
								v += 4)
			BAR0_WI32(dev_priv->vm_vram_pt[i], v, 0);

		BAR0_WI32(chan->vm_pd, 0x10 + (i*8),
			  chan->vm_vram_pt[i]->instance | 0x61);
		BAR0_WI32(chan->vm_pd, 0x14 + (i*8), 0);
	}

	/* DMA object for PRAMIN BAR */
	ret = nouveau_gpuobj_new_ref(dev, chan, chan, 0, 6*4, 16, 0,
							&priv->pramin_bar);
	if (ret)
		return ret;
	BAR0_WI32(priv->pramin_bar->gpuobj, 0x00, 0x7fc00000);
	BAR0_WI32(priv->pramin_bar->gpuobj, 0x04, dev_priv->ramin_size - 1);
	BAR0_WI32(priv->pramin_bar->gpuobj, 0x08, 0x00000000);
	BAR0_WI32(priv->pramin_bar->gpuobj, 0x0c, 0x00000000);
	BAR0_WI32(priv->pramin_bar->gpuobj, 0x10, 0x00000000);
	BAR0_WI32(priv->pramin_bar->gpuobj, 0x14, 0x00000000);

	/* DMA object for FB BAR */
	ret = nouveau_gpuobj_new_ref(dev, chan, chan, 0, 6*4, 16, 0,
							&priv->fb_bar);
	if (ret)
		return ret;
	BAR0_WI32(priv->fb_bar->gpuobj, 0x00, 0x7fc00000);
	BAR0_WI32(priv->fb_bar->gpuobj, 0x04, 0x40000000 +
					      drm_get_resource_len(dev, 1) - 1);
	BAR0_WI32(priv->fb_bar->gpuobj, 0x08, 0x40000000);
	BAR0_WI32(priv->fb_bar->gpuobj, 0x0c, 0x00000000);
	BAR0_WI32(priv->fb_bar->gpuobj, 0x10, 0x00000000);
	BAR0_WI32(priv->fb_bar->gpuobj, 0x14, 0x00000000);

	/* Poke the relevant regs, and pray it works :) */
	nv_wr32(dev, NV50_PUNK_BAR_CFG_BASE, (chan->ramin->instance >> 12));
	nv_wr32(dev, NV50_PUNK_UNK1710, 0);
	nv_wr32(dev, NV50_PUNK_BAR_CFG_BASE, (chan->ramin->instance >> 12) |
					 NV50_PUNK_BAR_CFG_BASE_VALID);
	nv_wr32(dev, NV50_PUNK_BAR1_CTXDMA, (priv->fb_bar->instance >> 4) |
					NV50_PUNK_BAR1_CTXDMA_VALID);
	nv_wr32(dev, NV50_PUNK_BAR3_CTXDMA, (priv->pramin_bar->instance >> 4) |
					NV50_PUNK_BAR3_CTXDMA_VALID);

	for (i = 0; i < 8; i++)
		nv_wr32(dev, 0x1900 + (i*4), 0);

	/* Assume that praying isn't enough, check that we can re-read the
	 * entire fake channel back from the PRAMIN BAR */
	dev_priv->engine.instmem.prepare_access(dev, false);
	for (i = 0; i < c_size; i += 4) {
		if (nv_rd32(dev, NV_RAMIN + i) != nv_ri32(dev, i)) {
			NV_ERROR(dev, "Error reading back PRAMIN at 0x%08x\n",
									i);
			dev_priv->engine.instmem.finish_access(dev);
			return -EINVAL;
		}
	}
	dev_priv->engine.instmem.finish_access(dev);

	nv_wr32(dev, NV50_PUNK_BAR0_PRAMIN, save_nv001700);

	/* Global PRAMIN heap */
	if (nouveau_mem_init_heap(&dev_priv->ramin_heap,
				  c_size, dev_priv->ramin_size - c_size)) {
		dev_priv->ramin_heap = NULL;
		NV_ERROR(dev, "Failed to init RAMIN heap\n");
	}

	/*XXX: incorrect, but needed to make hash func "work" */
	dev_priv->ramht_offset = 0x10000;
	dev_priv->ramht_bits   = 9;
	dev_priv->ramht_size   = (1 << dev_priv->ramht_bits);
	return 0;
}

void
nv50_instmem_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_instmem_priv *priv = dev_priv->engine.instmem.priv;
	struct nouveau_channel *chan = dev_priv->fifos[0];
	int i;

	NV_DEBUG(dev, "\n");

	if (!priv)
		return;

	/* Restore state from before init */
	for (i = 0x1700; i <= 0x1710; i += 4)
		nv_wr32(dev, i, priv->save1700[(i - 0x1700) / 4]);

	nouveau_gpuobj_ref_del(dev, &priv->fb_bar);
	nouveau_gpuobj_ref_del(dev, &priv->pramin_bar);
	nouveau_gpuobj_ref_del(dev, &priv->pramin_pt);

	/* Destroy dummy channel */
	if (chan) {
		for (i = 0; i < dev_priv->vm_vram_pt_nr; i++) {
			nouveau_gpuobj_ref_del(dev, &chan->vm_vram_pt[i]);
			dev_priv->vm_vram_pt[i] = NULL;
		}
		dev_priv->vm_vram_pt_nr = 0;

		nouveau_gpuobj_del(dev, &chan->vm_pd);
		nouveau_gpuobj_ref_del(dev, &chan->ramfc);
		nouveau_gpuobj_ref_del(dev, &chan->ramin);
		nouveau_mem_takedown(&chan->ramin_heap);

		dev_priv->fifos[0] = dev_priv->fifos[127] = NULL;
		kfree(chan);
	}

	dev_priv->engine.instmem.priv = NULL;
	kfree(priv);
}

int
nv50_instmem_suspend(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->fifos[0];
	struct nouveau_gpuobj *ramin = chan->ramin->gpuobj;
	int i;

	ramin->im_backing_suspend = vmalloc(ramin->im_pramin->size);
	if (!ramin->im_backing_suspend)
		return -ENOMEM;

	for (i = 0; i < ramin->im_pramin->size; i += 4)
		ramin->im_backing_suspend[i/4] = nv_ri32(dev, i);
	return 0;
}

void
nv50_instmem_resume(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_instmem_priv *priv = dev_priv->engine.instmem.priv;
	struct nouveau_channel *chan = dev_priv->fifos[0];
	struct nouveau_gpuobj *ramin = chan->ramin->gpuobj;
	int i;

	nv_wr32(dev, NV50_PUNK_BAR0_PRAMIN, (ramin->im_backing_start >> 16));
	for (i = 0; i < ramin->im_pramin->size; i += 4)
		BAR0_WI32(ramin, i, ramin->im_backing_suspend[i/4]);
	vfree(ramin->im_backing_suspend);
	ramin->im_backing_suspend = NULL;

	/* Poke the relevant regs, and pray it works :) */
	nv_wr32(dev, NV50_PUNK_BAR_CFG_BASE, (chan->ramin->instance >> 12));
	nv_wr32(dev, NV50_PUNK_UNK1710, 0);
	nv_wr32(dev, NV50_PUNK_BAR_CFG_BASE, (chan->ramin->instance >> 12) |
					 NV50_PUNK_BAR_CFG_BASE_VALID);
	nv_wr32(dev, NV50_PUNK_BAR1_CTXDMA, (priv->fb_bar->instance >> 4) |
					NV50_PUNK_BAR1_CTXDMA_VALID);
	nv_wr32(dev, NV50_PUNK_BAR3_CTXDMA, (priv->pramin_bar->instance >> 4) |
					NV50_PUNK_BAR3_CTXDMA_VALID);

	for (i = 0; i < 8; i++)
		nv_wr32(dev, 0x1900 + (i*4), 0);
}

int
nv50_instmem_populate(struct drm_device *dev, struct nouveau_gpuobj *gpuobj,
		      uint32_t *sz)
{
	int ret;

	if (gpuobj->im_backing)
		return -EINVAL;

	*sz = (*sz + (NV50_INSTMEM_PAGE_SIZE-1)) & ~(NV50_INSTMEM_PAGE_SIZE-1);
	if (*sz == 0)
		return -EINVAL;

	ret = nouveau_bo_new(dev, NULL, *sz, 0, TTM_PL_FLAG_VRAM, 0, 0x0000,
			     true, false, &gpuobj->im_backing);
	if (ret) {
		NV_ERROR(dev, "error getting PRAMIN backing pages: %d\n", ret);
		return ret;
	}

	ret = nouveau_bo_pin(gpuobj->im_backing, TTM_PL_FLAG_VRAM);
	if (ret) {
		NV_ERROR(dev, "error pinning PRAMIN backing VRAM: %d\n", ret);
		nouveau_bo_ref(NULL, &gpuobj->im_backing);
		return ret;
	}

	gpuobj->im_backing_start = gpuobj->im_backing->bo.mem.mm_node->start;
	gpuobj->im_backing_start <<= PAGE_SHIFT;

	return 0;
}

void
nv50_instmem_clear(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (gpuobj && gpuobj->im_backing) {
		if (gpuobj->im_bound)
			dev_priv->engine.instmem.unbind(dev, gpuobj);
		nouveau_bo_unpin(gpuobj->im_backing);
		nouveau_bo_ref(NULL, &gpuobj->im_backing);
		gpuobj->im_backing = NULL;
	}
}

int
nv50_instmem_bind(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_instmem_priv *priv = dev_priv->engine.instmem.priv;
	struct nouveau_gpuobj *pramin_pt = priv->pramin_pt->gpuobj;
	uint32_t pte, pte_end;
	uint64_t vram;

	if (!gpuobj->im_backing || !gpuobj->im_pramin || gpuobj->im_bound)
		return -EINVAL;

	NV_DEBUG(dev, "st=0x%0llx sz=0x%0llx\n",
		 gpuobj->im_pramin->start, gpuobj->im_pramin->size);

	pte     = (gpuobj->im_pramin->start >> 12) << 1;
	pte_end = ((gpuobj->im_pramin->size >> 12) << 1) + pte;
	vram    = gpuobj->im_backing_start;

	NV_DEBUG(dev, "pramin=0x%llx, pte=%d, pte_end=%d\n",
		 gpuobj->im_pramin->start, pte, pte_end);
	NV_DEBUG(dev, "first vram page: 0x%08x\n", gpuobj->im_backing_start);

	vram |= 1;
	if (dev_priv->vram_sys_base) {
		vram += dev_priv->vram_sys_base;
		vram |= 0x30;
	}

	dev_priv->engine.instmem.prepare_access(dev, true);
	while (pte < pte_end) {
		nv_wo32(dev, pramin_pt, pte++, lower_32_bits(vram));
		nv_wo32(dev, pramin_pt, pte++, upper_32_bits(vram));
		vram += NV50_INSTMEM_PAGE_SIZE;
	}
	dev_priv->engine.instmem.finish_access(dev);

	nv_wr32(dev, 0x100c80, 0x00040001);
	if (!nv_wait(0x100c80, 0x00000001, 0x00000000)) {
		NV_ERROR(dev, "timeout: (0x100c80 & 1) == 0 (1)\n");
		NV_ERROR(dev, "0x100c80 = 0x%08x\n", nv_rd32(dev, 0x100c80));
		return -EBUSY;
	}

	nv_wr32(dev, 0x100c80, 0x00060001);
	if (!nv_wait(0x100c80, 0x00000001, 0x00000000)) {
		NV_ERROR(dev, "timeout: (0x100c80 & 1) == 0 (2)\n");
		NV_ERROR(dev, "0x100c80 = 0x%08x\n", nv_rd32(dev, 0x100c80));
		return -EBUSY;
	}

	gpuobj->im_bound = 1;
	return 0;
}

int
nv50_instmem_unbind(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_instmem_priv *priv = dev_priv->engine.instmem.priv;
	uint32_t pte, pte_end;

	if (gpuobj->im_bound == 0)
		return -EINVAL;

	pte     = (gpuobj->im_pramin->start >> 12) << 1;
	pte_end = ((gpuobj->im_pramin->size >> 12) << 1) + pte;

	dev_priv->engine.instmem.prepare_access(dev, true);
	while (pte < pte_end) {
		nv_wo32(dev, priv->pramin_pt->gpuobj, pte++, 0x00000000);
		nv_wo32(dev, priv->pramin_pt->gpuobj, pte++, 0x00000000);
	}
	dev_priv->engine.instmem.finish_access(dev);

	gpuobj->im_bound = 0;
	return 0;
}

void
nv50_instmem_prepare_access(struct drm_device *dev, bool write)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_instmem_priv *priv = dev_priv->engine.instmem.priv;

	priv->last_access_wr = write;
}

void
nv50_instmem_finish_access(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_instmem_priv *priv = dev_priv->engine.instmem.priv;

	if (priv->last_access_wr) {
		nv_wr32(dev, 0x070000, 0x00000001);
		if (!nv_wait(0x070000, 0x00000001, 0x00000000))
			NV_ERROR(dev, "PRAMIN flush timeout\n");
	}
}

