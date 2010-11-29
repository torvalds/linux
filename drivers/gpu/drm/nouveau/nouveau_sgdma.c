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
	unsigned nr_pages;

	unsigned pte_start;
	bool bound;
};

static int
nouveau_sgdma_populate(struct ttm_backend *be, unsigned long num_pages,
		       struct page **pages, struct page *dummy_read_page)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)be;
	struct drm_device *dev = nvbe->dev;

	NV_DEBUG(nvbe->dev, "num_pages = %ld\n", num_pages);

	if (nvbe->pages)
		return -EINVAL;

	nvbe->pages = kmalloc(sizeof(dma_addr_t) * num_pages, GFP_KERNEL);
	if (!nvbe->pages)
		return -ENOMEM;

	nvbe->nr_pages = 0;
	while (num_pages--) {
		nvbe->pages[nvbe->nr_pages] =
			pci_map_page(dev->pdev, pages[nvbe->nr_pages], 0,
				     PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
		if (pci_dma_mapping_error(dev->pdev,
					  nvbe->pages[nvbe->nr_pages])) {
			be->func->clear(be);
			return -EFAULT;
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
			pci_unmap_page(dev->pdev, nvbe->pages[nvbe->nr_pages],
				       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
		}
		kfree(nvbe->pages);
		nvbe->pages = NULL;
		nvbe->nr_pages = 0;
	}
}

static inline unsigned
nouveau_sgdma_pte(struct drm_device *dev, uint64_t offset)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned pte = (offset >> NV_CTXDMA_PAGE_SHIFT);

	if (dev_priv->card_type < NV_50)
		return pte + 2;

	return pte << 1;
}

static int
nouveau_sgdma_bind(struct ttm_backend *be, struct ttm_mem_reg *mem)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)be;
	struct drm_device *dev = nvbe->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj = dev_priv->gart_info.sg_ctxdma;
	unsigned i, j, pte;

	NV_DEBUG(dev, "pg=0x%lx\n", mem->start);

	pte = nouveau_sgdma_pte(nvbe->dev, mem->start << PAGE_SHIFT);
	nvbe->pte_start = pte;
	for (i = 0; i < nvbe->nr_pages; i++) {
		dma_addr_t dma_offset = nvbe->pages[i];
		uint32_t offset_l = lower_32_bits(dma_offset);
		uint32_t offset_h = upper_32_bits(dma_offset);

		for (j = 0; j < PAGE_SIZE / NV_CTXDMA_PAGE_SIZE; j++) {
			if (dev_priv->card_type < NV_50) {
				nv_wo32(gpuobj, (pte * 4) + 0, offset_l | 3);
				pte += 1;
			} else {
				nv_wo32(gpuobj, (pte * 4) + 0, offset_l | 0x21);
				nv_wo32(gpuobj, (pte * 4) + 4, offset_h & 0xff);
				pte += 2;
			}

			dma_offset += NV_CTXDMA_PAGE_SIZE;
		}
	}
	dev_priv->engine.instmem.flush(nvbe->dev);

	if (dev_priv->card_type == NV_50) {
		dev_priv->engine.fifo.tlb_flush(dev);
		dev_priv->engine.graph.tlb_flush(dev);
	}

	nvbe->bound = true;
	return 0;
}

static int
nouveau_sgdma_unbind(struct ttm_backend *be)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)be;
	struct drm_device *dev = nvbe->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj = dev_priv->gart_info.sg_ctxdma;
	unsigned i, j, pte;

	NV_DEBUG(dev, "\n");

	if (!nvbe->bound)
		return 0;

	pte = nvbe->pte_start;
	for (i = 0; i < nvbe->nr_pages; i++) {
		dma_addr_t dma_offset = dev_priv->gart_info.sg_dummy_bus;

		for (j = 0; j < PAGE_SIZE / NV_CTXDMA_PAGE_SIZE; j++) {
			if (dev_priv->card_type < NV_50) {
				nv_wo32(gpuobj, (pte * 4) + 0, dma_offset | 3);
				pte += 1;
			} else {
				nv_wo32(gpuobj, (pte * 4) + 0, 0x00000000);
				nv_wo32(gpuobj, (pte * 4) + 4, 0x00000000);
				pte += 2;
			}

			dma_offset += NV_CTXDMA_PAGE_SIZE;
		}
	}
	dev_priv->engine.instmem.flush(nvbe->dev);

	if (dev_priv->card_type == NV_50) {
		dev_priv->engine.fifo.tlb_flush(dev);
		dev_priv->engine.graph.tlb_flush(dev);
	}

	nvbe->bound = false;
	return 0;
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

static struct ttm_backend_func nouveau_sgdma_backend = {
	.populate		= nouveau_sgdma_populate,
	.clear			= nouveau_sgdma_clear,
	.bind			= nouveau_sgdma_bind,
	.unbind			= nouveau_sgdma_unbind,
	.destroy		= nouveau_sgdma_destroy
};

struct ttm_backend *
nouveau_sgdma_init_ttm(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_sgdma_be *nvbe;

	if (!dev_priv->gart_info.sg_ctxdma)
		return NULL;

	nvbe = kzalloc(sizeof(*nvbe), GFP_KERNEL);
	if (!nvbe)
		return NULL;

	nvbe->dev = dev;

	nvbe->backend.func	= &nouveau_sgdma_backend;

	return &nvbe->backend;
}

int
nouveau_sgdma_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pci_dev *pdev = dev->pdev;
	struct nouveau_gpuobj *gpuobj = NULL;
	uint32_t aper_size, obj_size;
	int i, ret;

	if (dev_priv->card_type < NV_50) {
		if(dev_priv->ramin_rsvd_vram < 2 * 1024 * 1024)
			aper_size = 64 * 1024 * 1024;
		else
			aper_size = 512 * 1024 * 1024;

		obj_size  = (aper_size >> NV_CTXDMA_PAGE_SHIFT) * 4;
		obj_size += 8; /* ctxdma header */
	} else {
		/* 1 entire VM page table */
		aper_size = (512 * 1024 * 1024);
		obj_size  = (aper_size >> NV_CTXDMA_PAGE_SHIFT) * 8;
	}

	ret = nouveau_gpuobj_new(dev, NULL, obj_size, 16,
				      NVOBJ_FLAG_ZERO_ALLOC |
				      NVOBJ_FLAG_ZERO_FREE, &gpuobj);
	if (ret) {
		NV_ERROR(dev, "Error creating sgdma object: %d\n", ret);
		return ret;
	}

	dev_priv->gart_info.sg_dummy_page =
		alloc_page(GFP_KERNEL|__GFP_DMA32|__GFP_ZERO);
	if (!dev_priv->gart_info.sg_dummy_page) {
		nouveau_gpuobj_ref(NULL, &gpuobj);
		return -ENOMEM;
	}

	set_bit(PG_locked, &dev_priv->gart_info.sg_dummy_page->flags);
	dev_priv->gart_info.sg_dummy_bus =
		pci_map_page(pdev, dev_priv->gart_info.sg_dummy_page, 0,
			     PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	if (pci_dma_mapping_error(pdev, dev_priv->gart_info.sg_dummy_bus)) {
		nouveau_gpuobj_ref(NULL, &gpuobj);
		return -EFAULT;
	}

	if (dev_priv->card_type < NV_50) {
		/* special case, allocated from global instmem heap so
		 * cinst is invalid, we use it on all channels though so
		 * cinst needs to be valid, set it the same as pinst
		 */
		gpuobj->cinst = gpuobj->pinst;

		/* Maybe use NV_DMA_TARGET_AGP for PCIE? NVIDIA do this, and
		 * confirmed to work on c51.  Perhaps means NV_DMA_TARGET_PCIE
		 * on those cards? */
		nv_wo32(gpuobj, 0, NV_CLASS_DMA_IN_MEMORY |
				   (1 << 12) /* PT present */ |
				   (0 << 13) /* PT *not* linear */ |
				   (NV_DMA_ACCESS_RW  << 14) |
				   (NV_DMA_TARGET_PCI << 16));
		nv_wo32(gpuobj, 4, aper_size - 1);
		for (i = 2; i < 2 + (aper_size >> 12); i++) {
			nv_wo32(gpuobj, i * 4,
				dev_priv->gart_info.sg_dummy_bus | 3);
		}
	} else {
		for (i = 0; i < obj_size; i += 8) {
			nv_wo32(gpuobj, i + 0, 0x00000000);
			nv_wo32(gpuobj, i + 4, 0x00000000);
		}
	}
	dev_priv->engine.instmem.flush(dev);

	dev_priv->gart_info.type      = NOUVEAU_GART_SGDMA;
	dev_priv->gart_info.aper_base = 0;
	dev_priv->gart_info.aper_size = aper_size;
	dev_priv->gart_info.sg_ctxdma = gpuobj;
	return 0;
}

void
nouveau_sgdma_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->gart_info.sg_dummy_page) {
		pci_unmap_page(dev->pdev, dev_priv->gart_info.sg_dummy_bus,
			       NV_CTXDMA_PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
		unlock_page(dev_priv->gart_info.sg_dummy_page);
		__free_page(dev_priv->gart_info.sg_dummy_page);
		dev_priv->gart_info.sg_dummy_page = NULL;
		dev_priv->gart_info.sg_dummy_bus = 0;
	}

	nouveau_gpuobj_ref(NULL, &dev_priv->gart_info.sg_ctxdma);
}

int
nouveau_sgdma_get_page(struct drm_device *dev, uint32_t offset, uint32_t *page)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj = dev_priv->gart_info.sg_ctxdma;
	int pte;

	pte = (offset >> NV_CTXDMA_PAGE_SHIFT) << 2;
	if (dev_priv->card_type < NV_50) {
		*page = nv_ro32(gpuobj, (pte + 8)) & ~NV_CTXDMA_PAGE_MASK;
		return 0;
	}

	NV_ERROR(dev, "Unimplemented on NV50\n");
	return -EINVAL;
}
