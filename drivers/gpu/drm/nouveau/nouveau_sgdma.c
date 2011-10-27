#include "drmP.h"
#include "nouveau_drv.h"
#include <linux/pagemap.h>
#include <linux/slab.h>

#define NV_CTXDMA_PAGE_SHIFT 12
#define NV_CTXDMA_PAGE_SIZE  (1 << NV_CTXDMA_PAGE_SHIFT)
#define NV_CTXDMA_PAGE_MASK  (NV_CTXDMA_PAGE_SIZE - 1)

struct nouveau_sgdma_be {
	struct ttm_backend backend;
	struct drm_device *dev;

	dma_addr_t *pages;
	bool *ttm_alloced;
	unsigned nr_pages;

	u64 offset;
	bool bound;
};

static int
nouveau_sgdma_populate(struct ttm_backend *be, unsigned long num_pages,
		       struct page **pages, struct page *dummy_read_page,
		       dma_addr_t *dma_addrs)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)be;
	struct drm_device *dev = nvbe->dev;

	NV_DEBUG(nvbe->dev, "num_pages = %ld\n", num_pages);

	if (nvbe->pages)
		return -EINVAL;

	nvbe->pages = kmalloc(sizeof(dma_addr_t) * num_pages, GFP_KERNEL);
	if (!nvbe->pages)
		return -ENOMEM;

	nvbe->ttm_alloced = kmalloc(sizeof(bool) * num_pages, GFP_KERNEL);
	if (!nvbe->ttm_alloced) {
		kfree(nvbe->pages);
		nvbe->pages = NULL;
		return -ENOMEM;
	}

	nvbe->nr_pages = 0;
	while (num_pages--) {
		/* this code path isn't called and is incorrect anyways */
		if (0) { /*dma_addrs[nvbe->nr_pages] != DMA_ERROR_CODE)*/
			nvbe->pages[nvbe->nr_pages] =
					dma_addrs[nvbe->nr_pages];
		 	nvbe->ttm_alloced[nvbe->nr_pages] = true;
		} else {
			nvbe->pages[nvbe->nr_pages] =
				pci_map_page(dev->pdev, pages[nvbe->nr_pages], 0,
				     PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
			if (pci_dma_mapping_error(dev->pdev,
						  nvbe->pages[nvbe->nr_pages])) {
				be->func->clear(be);
				return -EFAULT;
			}
			nvbe->ttm_alloced[nvbe->nr_pages] = false;
		}

		nvbe->nr_pages++;
	}

	return 0;
}

static void
nouveau_sgdma_clear(struct ttm_backend *be)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)be;
	struct drm_device *dev;

	if (nvbe && nvbe->pages) {
		dev = nvbe->dev;
		NV_DEBUG(dev, "\n");

		if (nvbe->bound)
			be->func->unbind(be);

		while (nvbe->nr_pages--) {
			if (!nvbe->ttm_alloced[nvbe->nr_pages])
				pci_unmap_page(dev->pdev, nvbe->pages[nvbe->nr_pages],
				       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
		}
		kfree(nvbe->pages);
		kfree(nvbe->ttm_alloced);
		nvbe->pages = NULL;
		nvbe->ttm_alloced = NULL;
		nvbe->nr_pages = 0;
	}
}

static void
nouveau_sgdma_destroy(struct ttm_backend *be)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)be;

	if (be) {
		NV_DEBUG(nvbe->dev, "\n");

		if (nvbe) {
			if (nvbe->pages)
				be->func->clear(be);
			kfree(nvbe);
		}
	}
}

static int
nv04_sgdma_bind(struct ttm_backend *be, struct ttm_mem_reg *mem)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)be;
	struct drm_device *dev = nvbe->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj = dev_priv->gart_info.sg_ctxdma;
	unsigned i, j, pte;

	NV_DEBUG(dev, "pg=0x%lx\n", mem->start);

	nvbe->offset = mem->start << PAGE_SHIFT;
	pte = (nvbe->offset >> NV_CTXDMA_PAGE_SHIFT) + 2;
	for (i = 0; i < nvbe->nr_pages; i++) {
		dma_addr_t dma_offset = nvbe->pages[i];
		uint32_t offset_l = lower_32_bits(dma_offset);

		for (j = 0; j < PAGE_SIZE / NV_CTXDMA_PAGE_SIZE; j++, pte++) {
			nv_wo32(gpuobj, (pte * 4) + 0, offset_l | 3);
			dma_offset += NV_CTXDMA_PAGE_SIZE;
		}
	}

	nvbe->bound = true;
	return 0;
}

static int
nv04_sgdma_unbind(struct ttm_backend *be)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)be;
	struct drm_device *dev = nvbe->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj = dev_priv->gart_info.sg_ctxdma;
	unsigned i, j, pte;

	NV_DEBUG(dev, "\n");

	if (!nvbe->bound)
		return 0;

	pte = (nvbe->offset >> NV_CTXDMA_PAGE_SHIFT) + 2;
	for (i = 0; i < nvbe->nr_pages; i++) {
		for (j = 0; j < PAGE_SIZE / NV_CTXDMA_PAGE_SIZE; j++, pte++)
			nv_wo32(gpuobj, (pte * 4) + 0, 0x00000000);
	}

	nvbe->bound = false;
	return 0;
}

static struct ttm_backend_func nv04_sgdma_backend = {
	.populate		= nouveau_sgdma_populate,
	.clear			= nouveau_sgdma_clear,
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
nv41_sgdma_bind(struct ttm_backend *be, struct ttm_mem_reg *mem)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)be;
	struct drm_nouveau_private *dev_priv = nvbe->dev->dev_private;
	struct nouveau_gpuobj *pgt = dev_priv->gart_info.sg_ctxdma;
	dma_addr_t *list = nvbe->pages;
	u32 pte = mem->start << 2;
	u32 cnt = nvbe->nr_pages;

	nvbe->offset = mem->start << PAGE_SHIFT;

	while (cnt--) {
		nv_wo32(pgt, pte, (*list++ >> 7) | 1);
		pte += 4;
	}

	nv41_sgdma_flush(nvbe);
	nvbe->bound = true;
	return 0;
}

static int
nv41_sgdma_unbind(struct ttm_backend *be)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)be;
	struct drm_nouveau_private *dev_priv = nvbe->dev->dev_private;
	struct nouveau_gpuobj *pgt = dev_priv->gart_info.sg_ctxdma;
	u32 pte = (nvbe->offset >> 12) << 2;
	u32 cnt = nvbe->nr_pages;

	while (cnt--) {
		nv_wo32(pgt, pte, 0x00000000);
		pte += 4;
	}

	nv41_sgdma_flush(nvbe);
	nvbe->bound = false;
	return 0;
}

static struct ttm_backend_func nv41_sgdma_backend = {
	.populate		= nouveau_sgdma_populate,
	.clear			= nouveau_sgdma_clear,
	.bind			= nv41_sgdma_bind,
	.unbind			= nv41_sgdma_unbind,
	.destroy		= nouveau_sgdma_destroy
};

static void
nv44_sgdma_flush(struct nouveau_sgdma_be *nvbe)
{
	struct drm_device *dev = nvbe->dev;

	nv_wr32(dev, 0x100814, (nvbe->nr_pages - 1) << 12);
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
nv44_sgdma_bind(struct ttm_backend *be, struct ttm_mem_reg *mem)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)be;
	struct drm_nouveau_private *dev_priv = nvbe->dev->dev_private;
	struct nouveau_gpuobj *pgt = dev_priv->gart_info.sg_ctxdma;
	dma_addr_t *list = nvbe->pages;
	u32 pte = mem->start << 2, tmp[4];
	u32 cnt = nvbe->nr_pages;
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

	nv44_sgdma_flush(nvbe);
	nvbe->bound = true;
	return 0;
}

static int
nv44_sgdma_unbind(struct ttm_backend *be)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)be;
	struct drm_nouveau_private *dev_priv = nvbe->dev->dev_private;
	struct nouveau_gpuobj *pgt = dev_priv->gart_info.sg_ctxdma;
	u32 pte = (nvbe->offset >> 12) << 2;
	u32 cnt = nvbe->nr_pages;

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

	nv44_sgdma_flush(nvbe);
	nvbe->bound = false;
	return 0;
}

static struct ttm_backend_func nv44_sgdma_backend = {
	.populate		= nouveau_sgdma_populate,
	.clear			= nouveau_sgdma_clear,
	.bind			= nv44_sgdma_bind,
	.unbind			= nv44_sgdma_unbind,
	.destroy		= nouveau_sgdma_destroy
};

static int
nv50_sgdma_bind(struct ttm_backend *be, struct ttm_mem_reg *mem)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)be;
	struct nouveau_mem *node = mem->mm_node;
	/* noop: bound in move_notify() */
	node->pages = nvbe->pages;
	nvbe->pages = (dma_addr_t *)node;
	nvbe->bound = true;
	return 0;
}

static int
nv50_sgdma_unbind(struct ttm_backend *be)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)be;
	struct nouveau_mem *node = (struct nouveau_mem *)nvbe->pages;
	/* noop: unbound in move_notify() */
	nvbe->pages = node->pages;
	node->pages = NULL;
	nvbe->bound = false;
	return 0;
}

static struct ttm_backend_func nv50_sgdma_backend = {
	.populate		= nouveau_sgdma_populate,
	.clear			= nouveau_sgdma_clear,
	.bind			= nv50_sgdma_bind,
	.unbind			= nv50_sgdma_unbind,
	.destroy		= nouveau_sgdma_destroy
};

struct ttm_backend *
nouveau_sgdma_init_ttm(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_sgdma_be *nvbe;

	nvbe = kzalloc(sizeof(*nvbe), GFP_KERNEL);
	if (!nvbe)
		return NULL;

	nvbe->dev = dev;

	nvbe->backend.func = dev_priv->gart_info.func;
	return &nvbe->backend;
}

int
nouveau_sgdma_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj = NULL;
	u32 aper_size, align;
	int ret;

	if (dev_priv->card_type >= NV_40 && drm_pci_device_is_pcie(dev))
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
	if (0 && drm_pci_device_is_pcie(dev) &&
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
