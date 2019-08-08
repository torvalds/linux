/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * Intel SCIF driver.
 */
#ifndef SCIF_MAP_H
#define SCIF_MAP_H

#include "../bus/scif_bus.h"

static __always_inline void *
scif_alloc_coherent(dma_addr_t *dma_handle,
		    struct scif_dev *scifdev, size_t size,
		    gfp_t gfp)
{
	void *va;

	if (scifdev_self(scifdev)) {
		va = kmalloc(size, gfp);
		if (va)
			*dma_handle = virt_to_phys(va);
	} else {
		va = dma_alloc_coherent(&scifdev->sdev->dev,
					size, dma_handle, gfp);
		if (va && scifdev_is_p2p(scifdev))
			*dma_handle = *dma_handle + scifdev->base_addr;
	}
	return va;
}

static __always_inline void
scif_free_coherent(void *va, dma_addr_t local,
		   struct scif_dev *scifdev, size_t size)
{
	if (scifdev_self(scifdev)) {
		kfree(va);
	} else {
		if (scifdev_is_p2p(scifdev) && local > scifdev->base_addr)
			local = local - scifdev->base_addr;
		dma_free_coherent(&scifdev->sdev->dev,
				  size, va, local);
	}
}

static __always_inline int
scif_map_single(dma_addr_t *dma_handle,
		void *local, struct scif_dev *scifdev, size_t size)
{
	int err = 0;

	if (scifdev_self(scifdev)) {
		*dma_handle = virt_to_phys((local));
	} else {
		*dma_handle = dma_map_single(&scifdev->sdev->dev,
					     local, size, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(&scifdev->sdev->dev, *dma_handle))
			err = -ENOMEM;
		else if (scifdev_is_p2p(scifdev))
			*dma_handle = *dma_handle + scifdev->base_addr;
	}
	if (err)
		*dma_handle = 0;
	return err;
}

static __always_inline void
scif_unmap_single(dma_addr_t local, struct scif_dev *scifdev,
		  size_t size)
{
	if (!scifdev_self(scifdev)) {
		if (scifdev_is_p2p(scifdev))
			local = local - scifdev->base_addr;
		dma_unmap_single(&scifdev->sdev->dev, local,
				 size, DMA_BIDIRECTIONAL);
	}
}

static __always_inline void *
scif_ioremap(dma_addr_t phys, size_t size, struct scif_dev *scifdev)
{
	void *out_virt;
	struct scif_hw_dev *sdev = scifdev->sdev;

	if (scifdev_self(scifdev))
		out_virt = phys_to_virt(phys);
	else
		out_virt = (void __force *)
			   sdev->hw_ops->remap(sdev, phys, size);
	return out_virt;
}

static __always_inline void
scif_iounmap(void *virt, size_t len, struct scif_dev *scifdev)
{
	if (!scifdev_self(scifdev)) {
		struct scif_hw_dev *sdev = scifdev->sdev;

		sdev->hw_ops->unmap(sdev, (void __force __iomem *)virt);
	}
}

static __always_inline int
scif_map_page(dma_addr_t *dma_handle, struct page *page,
	      struct scif_dev *scifdev)
{
	int err = 0;

	if (scifdev_self(scifdev)) {
		*dma_handle = page_to_phys(page);
	} else {
		struct scif_hw_dev *sdev = scifdev->sdev;
		*dma_handle = dma_map_page(&sdev->dev,
					   page, 0x0, PAGE_SIZE,
					   DMA_BIDIRECTIONAL);
		if (dma_mapping_error(&sdev->dev, *dma_handle))
			err = -ENOMEM;
		else if (scifdev_is_p2p(scifdev))
			*dma_handle = *dma_handle + scifdev->base_addr;
	}
	if (err)
		*dma_handle = 0;
	return err;
}
#endif  /* SCIF_MAP_H */
