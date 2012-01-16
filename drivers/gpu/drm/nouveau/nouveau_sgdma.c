#include "drmP.h"
#include "nouveau_drv.h"
#include <linux/pagemap.h>
#include <linux/slab.h>

#define NV_CTXDMA_PAGE_SHIFT 12
#define NV_CTXDMA_PAGE_SIZE  (1 << NV_CTXDMA_PAGE_SHIFT)
#define NV_CTXDMA_PAGE_MASK  (NV_CTXDMA_PAGE_SIZE - 1)

struct nouveau_sgdma_be {
	/* this has to be the first field so populate/unpopulated in
	 * nouve_bo.c works properly, otherwise have to move them here
	 */
	struct ttm_dma_tt ttm;
	struct drm_device *dev;
	u64 offset;
};

static void
nouveau_sgdma_destroy(struct ttm_tt *ttm)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)ttm;

	if (ttm) {
		NV_DEBUG(nvbe->dev, "\n");
		ttm_dma_tt_fini(&nvbe->ttm);
		kfree(nvbe);
	}
}

static int
nv04_sgdma_bind(struct ttm_tt *ttm, struct ttm_mem_reg *mem)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)ttm;
	struct drm_device *dev = nvbe->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj = dev_priv->gart_info.sg_ctxdma;
	unsigned i, j, pte;

	NV_DEBUG(dev, "pg=0x%lx\n", mem->start);

	nvbe->offset = mem->start << PAGE_SHIFT;
	pte = (nvbe->offset >> NV_CTXDMA_PAGE_SHIFT) + 2;
	for (i = 0; i < ttm->num_pages; i++) {
		dma_addr_t dma_offset = nvbe->ttm.dma_address[i];
		uint32_t offset_l = lower_32_bits(dma_offset);

		for (j = 0; j < PAGE_SIZE / NV_CTXDMA_PAGE_SIZE; j++, pte++) {
			nv_wo32(gpuobj, (pte * 4) + 0, offset_l | 3);
			offset_l += NV_CTXDMA_PAGE_SIZE;
		}
	}

	return 0;
}

static int
nv04_sgdma_unbind(struct ttm_tt *ttm)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)ttm;
	struct drm_device *dev = nvbe->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj = dev_priv->gart_info.sg_ctxdma;
	unsigned i, j, pte;

	NV_DEBUG(dev, "\n");

	if (ttm->state != tt_bound)
		return 0;

	pte = (nvbe->offset >> NV_CTXDMA_PAGE_SHIFT) + 2;
	for (i = 0; i < ttm->num_pages; i++) {
		for (j = 0; j < PAGE_SIZE / NV_CTXDMA_PAGE_SIZE; j++, pte++)
			nv_wo32(gpuobj, (pte * 4) + 0, 0x00000000);
	}

	return 0;
}

static struct ttm_backend_func nv04_sgdma_backend = {
	.bind			= nv04_sgdma_bind,
	.unbind			= nv04_sgdma_unbind,
	.destroy		= nouveau_sgdma_destroy
};

static void
nv41_sgdma_flush(struct nouveau_sgdma_be *nvbe)
{
	struct drm_device *dev = nvbe->dev;

	nv_wr32(dev, 0x100810, 0x00000022);
	if (!nv_wait(dev, 0x100810, 0x00000100, 0x00000100))
		NV_ERROR(dev, "vm flush timeout: 0x%08x\n",
			 nv_rd32(dev, 0x100810));
	nv_wr32(dev, 0x100810, 0x00000000);
}

static int
nv41_sgdma_bind(struct ttm_tt *ttm, struct ttm_mem_reg *mem)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)ttm;
	struct drm_nouveau_private *dev_priv = nvbe->dev->dev_private;
	struct nouveau_gpuobj *pgt = dev_priv->gart_info.sg_ctxdma;
	dma_addr_t *list = nvbe->ttm.dma_address;
	u32 pte = mem->start << 2;
	u32 cnt = ttm->num_pages;

	nvbe->offset = mem->start << PAGE_SHIFT;

	while (cnt--) {
		nv_wo32(pgt, pte, (*list++ >> 7) | 1);
		pte += 4;
	}

	nv41_sgdma_flush(nvbe);
	return 0;
}

static int
nv41_sgdma_unbind(struct ttm_tt *ttm)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)ttm;
	struct drm_nouveau_private *dev_priv = nvbe->dev->dev_private;
	struct nouveau_gpuobj *pgt = dev_priv->gart_info.sg_ctxdma;
	u32 pte = (nvbe->offset >> 12) << 2;
	u32 cnt = ttm->num_pages;

	while (cnt--) {
		nv_wo32(pgt, pte, 0x00000000);
		pte += 4;
	}

	nv41_sgdma_flush(nvbe);
	return 0;
}

static struct ttm_backend_func nv41_sgdma_backend = {
	.bind			= nv41_sgdma_bind,
	.unbind			= nv41_sgdma_unbind,
	.destroy		= nouveau_sgdma_destroy
};

static void
nv44_sgdma_flush(struct ttm_tt *ttm)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)ttm;
	struct drm_device *dev = nvbe->dev;

	nv_wr32(dev, 0x100814, (ttm->num_pages - 1) << 12);
	nv_wr32(dev, 0x100808, nvbe->offset | 0x20);
	if (!nv_wait(dev, 0x100808, 0x00000001, 0x00000001))
		NV_ERROR(dev, "gart flush timeout: 0x%08x\n",
			 nv_rd32(dev, 0x100808));
	nv_wr32(dev, 0x100808, 0x00000000);
}

static void
nv44_sgdma_fill(struct nouveau_gpuobj *pgt, dma_addr_t *list, u32 base, u32 cnt)
{
	struct drm_nouveau_private *dev_priv = pgt->dev->dev_private;
	dma_addr_t dummy = dev_priv->gart_info.dummy.addr;
	u32 pte, tmp[4];

	pte   = base >> 2;
	base &= ~0x0000000f;

	tmp[0] = nv_ro32(pgt, base + 0x0);
	tmp[1] = nv_ro32(pgt, base + 0x4);
	tmp[2] = nv_ro32(pgt, base + 0x8);
	tmp[3] = nv_ro32(pgt, base + 0xc);
	while (cnt--) {
		u32 addr = list ? (*list++ >> 12) : (dummy >> 12);
		switch (pte++ & 0x3) {
		case 0:
			tmp[0] &= ~0x07ffffff;
			tmp[0] |= addr;
			break;
		case 1:
			tmp[0] &= ~0xf8000000;
			tmp[0] |= addr << 27;
			tmp[1] &= ~0x003fffff;
			tmp[1] |= addr >> 5;
			break;
		case 2:
			tmp[1] &= ~0xffc00000;
			tmp[1] |= addr << 22;
			tmp[2] &= ~0x0001ffff;
			tmp[2] |= addr >> 10;
			break;
		case 3:
			tmp[2] &= ~0xfffe0000;
			tmp[2] |= addr << 17;
			tmp[3] &= ~0x00000fff;
			tmp[3] |= addr >> 15;
			break;
		}
	}

	tmp[3] |= 0x40000000;

	nv_wo32(pgt, base + 0x0, tmp[0]);
	nv_wo32(pgt, base + 0x4, tmp[1]);
	nv_wo32(pgt, base + 0x8, tmp[2]);
	nv_wo32(pgt, base + 0xc, tmp[3]);
}

static int
nv44_sgdma_bind(struct ttm_tt *ttm, struct ttm_mem_reg *mem)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)ttm;
	struct drm_nouveau_private *dev_priv = nvbe->dev->dev_private;
	struct nouveau_gpuobj *pgt = dev_priv->gart_info.sg_ctxdma;
	dma_addr_t *list = nvbe->ttm.dma_address;
	u32 pte = mem->start << 2, tmp[4];
	u32 cnt = ttm->num_pages;
	int i;

	nvbe->offset = mem->start << PAGE_SHIFT;

	if (pte & 0x0000000c) {
		u32  max = 4 - ((pte >> 2) & 0x3);
		u32 part = (cnt > max) ? max : cnt;
		nv44_sgdma_fill(pgt, list, pte, part);
		pte  += (part << 2);
		list += part;
		cnt  -= part;
	}

	while (cnt >= 4) {
		for (i = 0; i < 4; i++)
			tmp[i] = *list++ >> 12;
		nv_wo32(pgt, pte + 0x0, tmp[0] >>  0 | tmp[1] << 27);
		nv_wo32(pgt, pte + 0x4, tmp[1] >>  5 | tmp[2] << 22);
		nv_wo32(pgt, pte + 0x8, tmp[2] >> 10 | tmp[3] << 17);
		nv_wo32(pgt, pte + 0xc, tmp[3] >> 15 | 0x40000000);
		pte  += 0x10;
		cnt  -= 4;
	}

	if (cnt)
		nv44_sgdma_fill(pgt, list, pte, cnt);

	nv44_sgdma_flush(ttm);
	return 0;
}

static int
nv44_sgdma_unbind(struct ttm_tt *ttm)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)ttm;
	struct drm_nouveau_private *dev_priv = nvbe->dev->dev_private;
	struct nouveau_gpuobj *pgt = dev_priv->gart_info.sg_ctxdma;
	u32 pte = (nvbe->offset >> 12) << 2;
	u32 cnt = ttm->num_pages;

	if (pte & 0x0000000c) {
		u32  max = 4 - ((pte >> 2) & 0x3);
		u32 part = (cnt > max) ? max : cnt;
		nv44_sgdma_fill(pgt, NULL, pte, part);
		pte  += (part << 2);
		cnt  -= part;
	}

	while (cnt >= 4) {
		nv_wo32(pgt, pte + 0x0, 0x00000000);
		nv_wo32(pgt, pte + 0x4, 0x00000000);
		nv_wo32(pgt, pte + 0x8, 0x00000000);
		nv_wo32(pgt, pte + 0xc, 0x00000000);
		pte  += 0x10;
		cnt  -= 4;
	}

	if (cnt)
		nv44_sgdma_fill(pgt, NULL, pte, cnt);

	nv44_sgdma_flush(ttm);
	return 0;
}

static struct ttm_backend_func nv44_sgdma_backend = {
	.bind			= nv44_sgdma_bind,
	.unbind			= nv44_sgdma_unbind,
	.destroy		= nouveau_sgdma_destroy
};

static int
nv50_sgdma_bind(struct ttm_tt *ttm, struct ttm_mem_reg *mem)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)ttm;
	struct nouveau_mem *node = mem->mm_node;

	/* noop: bound in move_notify() */
	node->pages = nvbe->ttm.dma_address;
	return 0;
}

static int
nv50_sgdma_unbind(struct ttm_tt *ttm)
{
	/* noop: unbound in move_notify() */
	return 0;
}

static struct ttm_backend_func nv50_sgdma_backend = {
	.bind			= nv50_sgdma_bind,
	.unbind			= nv50_sgdma_unbind,
	.destroy		= nouveau_sgdma_destroy
};

struct ttm_tt *
nouveau_sgdma_create_ttm(struct ttm_bo_device *bdev,
			 unsigned long size, uint32_t page_flags,
			 struct page *dummy_read_page)
{
	struct drm_nouveau_private *dev_priv = nouveau_bdev(bdev);
	struct drm_device *dev = dev_priv->dev;
	struct nouveau_sgdma_be *nvbe;

	nvbe = kzalloc(sizeof(*nvbe), GFP_KERNEL);
	if (!nvbe)
		return NULL;

	nvbe->dev = dev;
	nvbe->ttm.ttm.func = dev_priv->gart_info.func;

	if (ttm_dma_tt_init(&nvbe->ttm, bdev, size, page_flags, dummy_read_page)) {
		kfree(nvbe);
		return NULL;
	}
	return &nvbe->ttm.ttm;
}

int
nouveau_sgdma_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj = NULL;
	u32 aper_size, align;
	int ret;

	if (dev_priv->card_type >= NV_40 && pci_is_pcie(dev->pdev))
		aper_size = 512 * 1024 * 1024;
	else
		aper_size = 64 * 1024 * 1024;

	/* Dear NVIDIA, NV44+ would like proper present bits in PTEs for
	 * christmas.  The cards before it have them, the cards after
	 * it have them, why is NV44 so unloved?
	 */
	dev_priv->gart_info.dummy.page = alloc_page(GFP_DMA32 | GFP_KERNEL);
	if (!dev_priv->gart_info.dummy.page)
		return -ENOMEM;

	dev_priv->gart_info.dummy.addr =
		pci_map_page(dev->pdev, dev_priv->gart_info.dummy.page,
			     0, PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	if (pci_dma_mapping_error(dev->pdev, dev_priv->gart_info.dummy.addr)) {
		NV_ERROR(dev, "error mapping dummy page\n");
		__free_page(dev_priv->gart_info.dummy.page);
		dev_priv->gart_info.dummy.page = NULL;
		return -ENOMEM;
	}

	if (dev_priv->card_type >= NV_50) {
		dev_priv->gart_info.aper_base = 0;
		dev_priv->gart_info.aper_size = aper_size;
		dev_priv->gart_info.type = NOUVEAU_GART_HW;
		dev_priv->gart_info.func = &nv50_sgdma_backend;
	} else
	if (0 && pci_is_pcie(dev->pdev) &&
	    dev_priv->chipset > 0x40 && dev_priv->chipset != 0x45) {
		if (nv44_graph_class(dev)) {
			dev_priv->gart_info.func = &nv44_sgdma_backend;
			align = 512 * 1024;
		} else {
			dev_priv->gart_info.func = &nv41_sgdma_backend;
			align = 16;
		}

		ret = nouveau_gpuobj_new(dev, NULL, aper_size / 1024, align,
					 NVOBJ_FLAG_ZERO_ALLOC |
					 NVOBJ_FLAG_ZERO_FREE, &gpuobj);
		if (ret) {
			NV_ERROR(dev, "Error creating sgdma object: %d\n", ret);
			return ret;
		}

		dev_priv->gart_info.sg_ctxdma = gpuobj;
		dev_priv->gart_info.aper_base = 0;
		dev_priv->gart_info.aper_size = aper_size;
		dev_priv->gart_info.type = NOUVEAU_GART_HW;
	} else {
		ret = nouveau_gpuobj_new(dev, NULL, (aper_size / 1024) + 8, 16,
					 NVOBJ_FLAG_ZERO_ALLOC |
					 NVOBJ_FLAG_ZERO_FREE, &gpuobj);
		if (ret) {
			NV_ERROR(dev, "Error creating sgdma object: %d\n", ret);
			return ret;
		}

		nv_wo32(gpuobj, 0, NV_CLASS_DMA_IN_MEMORY |
				   (1 << 12) /* PT present */ |
				   (0 << 13) /* PT *not* linear */ |
				   (0 << 14) /* RW */ |
				   (2 << 16) /* PCI */);
		nv_wo32(gpuobj, 4, aper_size - 1);

		dev_priv->gart_info.sg_ctxdma = gpuobj;
		dev_priv->gart_info.aper_base = 0;
		dev_priv->gart_info.aper_size = aper_size;
		dev_priv->gart_info.type = NOUVEAU_GART_PDMA;
		dev_priv->gart_info.func = &nv04_sgdma_backend;
	}

	return 0;
}

void
nouveau_sgdma_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	nouveau_gpuobj_ref(NULL, &dev_priv->gart_info.sg_ctxdma);

	if (dev_priv->gart_info.dummy.page) {
		pci_unmap_page(dev->pdev, dev_priv->gart_info.dummy.addr,
			       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
		__free_page(dev_priv->gart_info.dummy.page);
		dev_priv->gart_info.dummy.page = NULL;
	}
}

uint32_t
nouveau_sgdma_get_physical(struct drm_device *dev, uint32_t offset)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj = dev_priv->gart_info.sg_ctxdma;
	int pte = (offset >> NV_CTXDMA_PAGE_SHIFT) + 2;

	BUG_ON(dev_priv->card_type >= NV_50);

	return (nv_ro32(gpuobj, 4 * pte) & ~NV_CTXDMA_PAGE_MASK) |
		(offset & NV_CTXDMA_PAGE_MASK);
}
