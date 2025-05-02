/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * FacetimeHD camera driver
 *
 * Copyright (C) 2015 Sven Schnelle <svens@stackframe.org>
 *
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/printk.h>
#include "fthd_drv.h"
#include "fthd_isp.h"
#include "fthd_hw.h"
#include "fthd_buffer.h"

#define GET_IOMMU_PAGES(_x) (((_x) + 4095)/4096)

struct buf_ctx {
	struct fthd_plane plane[4];
	struct isp_mem_obj *isphdr;
};

static int iommu_allocator_init(struct fthd_private *dev_priv)
{
        dev_priv->iommu = kzalloc(sizeof(struct resource), GFP_KERNEL);
	if (!dev_priv->iommu)
	    return -ENOMEM;

	dev_priv->iommu->start = 0;
	dev_priv->iommu->end = 4095;
	return 0;
}

struct iommu_obj *iommu_allocate_sgtable(struct fthd_private *dev_priv, struct sg_table *sgtable)
{
	struct iommu_obj *obj;
	struct resource *root = dev_priv->iommu;
	struct scatterlist *sg;
	int ret, i, pos;
	int total_len = 0, dma_length;
	dma_addr_t dma_addr;
	
	for(i = 0; i < sgtable->nents; i++)
		total_len += sg_dma_len(sgtable->sgl + i);
	
	if (!total_len)
		return NULL;

	total_len += 4095;
	total_len /= 4096;
	
	obj = kzalloc(sizeof(struct iommu_obj), GFP_KERNEL);
	if (!obj)
		return NULL;

	obj->base.name = "S2 IOMMU";
	ret = allocate_resource(root, &obj->base, total_len, root->start, root->end,
				1, NULL, NULL);
	if (ret) {
		dev_err(&dev_priv->pdev->dev,
			"Failed to allocate resource (size: %d, start: %Ld, end: %Ld)\n",
			total_len, root->start, root->end);
		kfree(obj);
		obj = NULL;
		return NULL;
	}

	obj->offset = obj->base.start - root->start;
	obj->size = total_len;

	pos = 0x9000 + obj->offset * 4;
	for(i = 0; i < sgtable->nents; i++) {
		sg = sgtable->sgl + i;
		WARN_ON(sg->offset);
		dma_addr = sg_dma_address(sg);
		WARN_ON(dma_addr & 0xfff);
		dma_addr >>= 12;
		
		for(dma_length = 0; dma_length < sg_dma_len(sg); dma_length += 0x1000) {
		  //			pr_debug("IOMMU %08x -> %08llx (dma length %d)\n", pos, dma_addr, dma_length);
			FTHD_S2_REG_WRITE(dma_addr++, pos);
			pos += 4;
		}
	}

	pr_debug("allocated %d pages @ %p / offset %d\n", obj->size, obj, obj->offset);
	return obj;
}

void iommu_free(struct fthd_private *dev_priv, struct iommu_obj *obj)
{
	int i;
	pr_debug("freeing %p\n", obj);

	if (!obj)
		return;
	
 	for (i = obj->offset; i < obj->offset + obj->size; i++)
		FTHD_S2_REG_WRITE(0, 0x9000 + i * 4);

	release_resource(&obj->base);
	kfree(obj);
	obj = NULL;
}

static void iommu_allocator_destroy(struct fthd_private *dev_priv)
{
	kfree(dev_priv->iommu);
}

int fthd_buffer_init(struct fthd_private *dev_priv)
{
	int i;
	for(i = 0; i < 0x1000; i++)
		FTHD_S2_REG_WRITE(0, 0x9000 + i * 4);

	return iommu_allocator_init(dev_priv);
}

void fthd_buffer_exit(struct fthd_private *dev_priv)
{
	iommu_allocator_destroy(dev_priv);
}
