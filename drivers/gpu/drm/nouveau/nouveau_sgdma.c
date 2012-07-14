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
	struct nouveau_mem *node;
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
	struct nouveau_mem *node = mem->mm_node;
	u64 size = mem->num_pages << 12;

	if (ttm->sg) {
		node->sg = ttm->sg;
		nouveau_vm_map_sg_table(&node->vma[0], 0, size, node);
	} else {
		node->pages = nvbe->ttm.dma_address;
		nouveau_vm_map_sg(&node->vma[0], 0, size, node);
	}

	nvbe->node = node;
	return 0;
}

static int
nv04_sgdma_unbind(struct ttm_tt *ttm)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)ttm;
	nouveau_vm_unmap(&nvbe->node->vma[0]);
	return 0;
}

static struct ttm_backend_func nv04_sgdma_backend = {
	.bind			= nv04_sgdma_bind,
	.unbind			= nv04_sgdma_unbind,
	.destroy		= nouveau_sgdma_destroy
};

static int
nv50_sgdma_bind(struct ttm_tt *ttm, struct ttm_mem_reg *mem)
{
	struct nouveau_sgdma_be *nvbe = (struct nouveau_sgdma_be *)ttm;
	struct nouveau_mem *node = mem->mm_node;

	/* noop: bound in move_notify() */
	if (ttm->sg) {
		node->sg = ttm->sg;
	} else
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
	u32 aper_size;

	if (dev_priv->card_type >= NV_50)
		aper_size = 512 * 1024 * 1024;
	else
		aper_size = 128 * 1024 * 1024;

	if (dev_priv->card_type >= NV_50) {
		dev_priv->gart_info.aper_base = 0;
		dev_priv->gart_info.aper_size = aper_size;
		dev_priv->gart_info.type = NOUVEAU_GART_HW;
		dev_priv->gart_info.func = &nv50_sgdma_backend;
	} else {
		dev_priv->gart_info.aper_base = 0;
		dev_priv->gart_info.aper_size = aper_size;
		dev_priv->gart_info.type = NOUVEAU_GART_PDMA;
		dev_priv->gart_info.func = &nv04_sgdma_backend;
		dev_priv->gart_info.sg_ctxdma = nv04vm_refdma(dev);
	}

	return 0;
}

void
nouveau_sgdma_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	nouveau_gpuobj_ref(NULL, &dev_priv->gart_info.sg_ctxdma);
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
