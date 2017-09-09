/*
 * Copyright 2009-2010 Freescale Semiconductor, Inc.
 *
 * Simple memory allocator abstraction for QorIQ (P1/P2) based Cache-SRAM
 *
 * Author: Vivek Mahajan <vivek.mahajan@freescale.com>
 *
 * This file is derived from the original work done
 * by Sylvain Munaut for the Bestcomm SRAM allocator.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of_platform.h>
#include <asm/pgtable.h>
#include <asm/fsl_85xx_cache_sram.h>

#include "fsl_85xx_cache_ctlr.h"

struct mpc85xx_cache_sram *cache_sram;

void *mpc85xx_cache_sram_alloc(unsigned int size,
				phys_addr_t *phys, unsigned int align)
{
	unsigned long offset;
	unsigned long flags;

	if (unlikely(cache_sram == NULL))
		return NULL;

	if (!size || (size > cache_sram->size) || (align > cache_sram->size)) {
		pr_err("%s(): size(=%x) or align(=%x) zero or too big\n",
			__func__, size, align);
		return NULL;
	}

	if ((align & (align - 1)) || align <= 1) {
		pr_err("%s(): align(=%x) must be power of two and >1\n",
			__func__, align);
		return NULL;
	}

	spin_lock_irqsave(&cache_sram->lock, flags);
	offset = rh_alloc_align(cache_sram->rh, size, align, NULL);
	spin_unlock_irqrestore(&cache_sram->lock, flags);

	if (IS_ERR_VALUE(offset))
		return NULL;

	*phys = cache_sram->base_phys + offset;

	return (unsigned char *)cache_sram->base_virt + offset;
}
EXPORT_SYMBOL(mpc85xx_cache_sram_alloc);

void mpc85xx_cache_sram_free(void *ptr)
{
	unsigned long flags;
	BUG_ON(!ptr);

	spin_lock_irqsave(&cache_sram->lock, flags);
	rh_free(cache_sram->rh, ptr - cache_sram->base_virt);
	spin_unlock_irqrestore(&cache_sram->lock, flags);
}
EXPORT_SYMBOL(mpc85xx_cache_sram_free);

int __init instantiate_cache_sram(struct platform_device *dev,
		struct sram_parameters sram_params)
{
	int ret = 0;

	if (cache_sram) {
		dev_err(&dev->dev, "Already initialized cache-sram\n");
		return -EBUSY;
	}

	cache_sram = kzalloc(sizeof(struct mpc85xx_cache_sram), GFP_KERNEL);
	if (!cache_sram) {
		dev_err(&dev->dev, "Out of memory for cache_sram structure\n");
		return -ENOMEM;
	}

	cache_sram->base_phys = sram_params.sram_offset;
	cache_sram->size = sram_params.sram_size;

	if (!request_mem_region(cache_sram->base_phys, cache_sram->size,
						"fsl_85xx_cache_sram")) {
		dev_err(&dev->dev, "%pOF: request memory failed\n",
				dev->dev.of_node);
		ret = -ENXIO;
		goto out_free;
	}

	cache_sram->base_virt = ioremap_prot(cache_sram->base_phys,
				cache_sram->size, _PAGE_COHERENT | PAGE_KERNEL);
	if (!cache_sram->base_virt) {
		dev_err(&dev->dev, "%pOF: ioremap_prot failed\n",
				dev->dev.of_node);
		ret = -ENOMEM;
		goto out_release;
	}

	cache_sram->rh = rh_create(sizeof(unsigned int));
	if (IS_ERR(cache_sram->rh)) {
		dev_err(&dev->dev, "%pOF: Unable to create remote heap\n",
				dev->dev.of_node);
		ret = PTR_ERR(cache_sram->rh);
		goto out_unmap;
	}

	rh_attach_region(cache_sram->rh, 0, cache_sram->size);
	spin_lock_init(&cache_sram->lock);

	dev_info(&dev->dev, "[base:0x%llx, size:0x%x] configured and loaded\n",
		(unsigned long long)cache_sram->base_phys, cache_sram->size);

	return 0;

out_unmap:
	iounmap(cache_sram->base_virt);

out_release:
	release_mem_region(cache_sram->base_phys, cache_sram->size);

out_free:
	kfree(cache_sram);
	return ret;
}

void remove_cache_sram(struct platform_device *dev)
{
	BUG_ON(!cache_sram);

	rh_detach_region(cache_sram->rh, 0, cache_sram->size);
	rh_destroy(cache_sram->rh);

	iounmap(cache_sram->base_virt);
	release_mem_region(cache_sram->base_phys, cache_sram->size);

	kfree(cache_sram);
	cache_sram = NULL;

	dev_info(&dev->dev, "MPC85xx Cache-SRAM driver unloaded\n");
}
