/*
 *	Functions to handle I2O memory
 *
 *	Pulled from the inlines in i2o headers and uninlined
 *
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation; either version 2 of the License, or (at your
 *	option) any later version.
 */

#include <linux/module.h>
#include <linux/i2o.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "core.h"

/* Protects our 32/64bit mask switching */
static DEFINE_MUTEX(mem_lock);

/**
 *	i2o_sg_tablesize - Calculate the maximum number of elements in a SGL
 *	@c: I2O controller for which the calculation should be done
 *	@body_size: maximum body size used for message in 32-bit words.
 *
 *	Return the maximum number of SG elements in a SG list.
 */
u16 i2o_sg_tablesize(struct i2o_controller *c, u16 body_size)
{
	i2o_status_block *sb = c->status_block.virt;
	u16 sg_count =
	    (sb->inbound_frame_size - sizeof(struct i2o_message) / 4) -
	    body_size;

	if (c->pae_support) {
		/*
		 * for 64-bit a SG attribute element must be added and each
		 * SG element needs 12 bytes instead of 8.
		 */
		sg_count -= 2;
		sg_count /= 3;
	} else
		sg_count /= 2;

	if (c->short_req && (sg_count > 8))
		sg_count = 8;

	return sg_count;
}
EXPORT_SYMBOL_GPL(i2o_sg_tablesize);


/**
 *	i2o_dma_map_single - Map pointer to controller and fill in I2O message.
 *	@c: I2O controller
 *	@ptr: pointer to the data which should be mapped
 *	@size: size of data in bytes
 *	@direction: DMA_TO_DEVICE / DMA_FROM_DEVICE
 *	@sg_ptr: pointer to the SG list inside the I2O message
 *
 *	This function does all necessary DMA handling and also writes the I2O
 *	SGL elements into the I2O message. For details on DMA handling see also
 *	dma_map_single(). The pointer sg_ptr will only be set to the end of the
 *	SG list if the allocation was successful.
 *
 *	Returns DMA address which must be checked for failures using
 *	dma_mapping_error().
 */
dma_addr_t i2o_dma_map_single(struct i2o_controller *c, void *ptr,
					    size_t size,
					    enum dma_data_direction direction,
					    u32 ** sg_ptr)
{
	u32 sg_flags;
	u32 *mptr = *sg_ptr;
	dma_addr_t dma_addr;

	switch (direction) {
	case DMA_TO_DEVICE:
		sg_flags = 0xd4000000;
		break;
	case DMA_FROM_DEVICE:
		sg_flags = 0xd0000000;
		break;
	default:
		return 0;
	}

	dma_addr = dma_map_single(&c->pdev->dev, ptr, size, direction);
	if (!dma_mapping_error(&c->pdev->dev, dma_addr)) {
#ifdef CONFIG_I2O_EXT_ADAPTEC_DMA64
		if ((sizeof(dma_addr_t) > 4) && c->pae_support) {
			*mptr++ = cpu_to_le32(0x7C020002);
			*mptr++ = cpu_to_le32(PAGE_SIZE);
		}
#endif

		*mptr++ = cpu_to_le32(sg_flags | size);
		*mptr++ = cpu_to_le32(i2o_dma_low(dma_addr));
#ifdef CONFIG_I2O_EXT_ADAPTEC_DMA64
		if ((sizeof(dma_addr_t) > 4) && c->pae_support)
			*mptr++ = cpu_to_le32(i2o_dma_high(dma_addr));
#endif
		*sg_ptr = mptr;
	}
	return dma_addr;
}
EXPORT_SYMBOL_GPL(i2o_dma_map_single);

/**
 *	i2o_dma_map_sg - Map a SG List to controller and fill in I2O message.
 *	@c: I2O controller
 *	@sg: SG list to be mapped
 *	@sg_count: number of elements in the SG list
 *	@direction: DMA_TO_DEVICE / DMA_FROM_DEVICE
 *	@sg_ptr: pointer to the SG list inside the I2O message
 *
 *	This function does all necessary DMA handling and also writes the I2O
 *	SGL elements into the I2O message. For details on DMA handling see also
 *	dma_map_sg(). The pointer sg_ptr will only be set to the end of the SG
 *	list if the allocation was successful.
 *
 *	Returns 0 on failure or 1 on success.
 */
int i2o_dma_map_sg(struct i2o_controller *c, struct scatterlist *sg,
	    int sg_count, enum dma_data_direction direction, u32 ** sg_ptr)
{
	u32 sg_flags;
	u32 *mptr = *sg_ptr;

	switch (direction) {
	case DMA_TO_DEVICE:
		sg_flags = 0x14000000;
		break;
	case DMA_FROM_DEVICE:
		sg_flags = 0x10000000;
		break;
	default:
		return 0;
	}

	sg_count = dma_map_sg(&c->pdev->dev, sg, sg_count, direction);
	if (!sg_count)
		return 0;

#ifdef CONFIG_I2O_EXT_ADAPTEC_DMA64
	if ((sizeof(dma_addr_t) > 4) && c->pae_support) {
		*mptr++ = cpu_to_le32(0x7C020002);
		*mptr++ = cpu_to_le32(PAGE_SIZE);
	}
#endif

	while (sg_count-- > 0) {
		if (!sg_count)
			sg_flags |= 0xC0000000;
		*mptr++ = cpu_to_le32(sg_flags | sg_dma_len(sg));
		*mptr++ = cpu_to_le32(i2o_dma_low(sg_dma_address(sg)));
#ifdef CONFIG_I2O_EXT_ADAPTEC_DMA64
		if ((sizeof(dma_addr_t) > 4) && c->pae_support)
			*mptr++ = cpu_to_le32(i2o_dma_high(sg_dma_address(sg)));
#endif
		sg = sg_next(sg);
	}
	*sg_ptr = mptr;

	return 1;
}
EXPORT_SYMBOL_GPL(i2o_dma_map_sg);

/**
 *	i2o_dma_alloc - Allocate DMA memory
 *	@dev: struct device pointer to the PCI device of the I2O controller
 *	@addr: i2o_dma struct which should get the DMA buffer
 *	@len: length of the new DMA memory
 *
 *	Allocate a coherent DMA memory and write the pointers into addr.
 *
 *	Returns 0 on success or -ENOMEM on failure.
 */
int i2o_dma_alloc(struct device *dev, struct i2o_dma *addr, size_t len)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int dma_64 = 0;

	mutex_lock(&mem_lock);
	if ((sizeof(dma_addr_t) > 4) && (pdev->dma_mask == DMA_64BIT_MASK)) {
		dma_64 = 1;
		if (pci_set_dma_mask(pdev, DMA_32BIT_MASK)) {
			mutex_unlock(&mem_lock);
			return -ENOMEM;
		}
	}

	addr->virt = dma_alloc_coherent(dev, len, &addr->phys, GFP_KERNEL);

	if ((sizeof(dma_addr_t) > 4) && dma_64)
		if (pci_set_dma_mask(pdev, DMA_64BIT_MASK))
			printk(KERN_WARNING "i2o: unable to set 64-bit DMA");
	mutex_unlock(&mem_lock);

	if (!addr->virt)
		return -ENOMEM;

	memset(addr->virt, 0, len);
	addr->len = len;

	return 0;
}
EXPORT_SYMBOL_GPL(i2o_dma_alloc);


/**
 *	i2o_dma_free - Free DMA memory
 *	@dev: struct device pointer to the PCI device of the I2O controller
 *	@addr: i2o_dma struct which contains the DMA buffer
 *
 *	Free a coherent DMA memory and set virtual address of addr to NULL.
 */
void i2o_dma_free(struct device *dev, struct i2o_dma *addr)
{
	if (addr->virt) {
		if (addr->phys)
			dma_free_coherent(dev, addr->len, addr->virt,
					  addr->phys);
		else
			kfree(addr->virt);
		addr->virt = NULL;
	}
}
EXPORT_SYMBOL_GPL(i2o_dma_free);


/**
 *	i2o_dma_realloc - Realloc DMA memory
 *	@dev: struct device pointer to the PCI device of the I2O controller
 *	@addr: pointer to a i2o_dma struct DMA buffer
 *	@len: new length of memory
 *
 *	If there was something allocated in the addr, free it first. If len > 0
 *	than try to allocate it and write the addresses back to the addr
 *	structure. If len == 0 set the virtual address to NULL.
 *
 *	Returns the 0 on success or negative error code on failure.
 */
int i2o_dma_realloc(struct device *dev, struct i2o_dma *addr, size_t len)
{
	i2o_dma_free(dev, addr);

	if (len)
		return i2o_dma_alloc(dev, addr, len);

	return 0;
}
EXPORT_SYMBOL_GPL(i2o_dma_realloc);

/*
 *	i2o_pool_alloc - Allocate an slab cache and mempool
 *	@mempool: pointer to struct i2o_pool to write data into.
 *	@name: name which is used to identify cache
 *	@size: size of each object
 *	@min_nr: minimum number of objects
 *
 *	First allocates a slab cache with name and size. Then allocates a
 *	mempool which uses the slab cache for allocation and freeing.
 *
 *	Returns 0 on success or negative error code on failure.
 */
int i2o_pool_alloc(struct i2o_pool *pool, const char *name,
				 size_t size, int min_nr)
{
	pool->name = kmalloc(strlen(name) + 1, GFP_KERNEL);
	if (!pool->name)
		goto exit;
	strcpy(pool->name, name);

	pool->slab =
	    kmem_cache_create(pool->name, size, 0, SLAB_HWCACHE_ALIGN, NULL);
	if (!pool->slab)
		goto free_name;

	pool->mempool = mempool_create_slab_pool(min_nr, pool->slab);
	if (!pool->mempool)
		goto free_slab;

	return 0;

free_slab:
	kmem_cache_destroy(pool->slab);

free_name:
	kfree(pool->name);

exit:
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(i2o_pool_alloc);

/*
 *	i2o_pool_free - Free slab cache and mempool again
 *	@mempool: pointer to struct i2o_pool which should be freed
 *
 *	Note that you have to return all objects to the mempool again before
 *	calling i2o_pool_free().
 */
void i2o_pool_free(struct i2o_pool *pool)
{
	mempool_destroy(pool->mempool);
	kmem_cache_destroy(pool->slab);
	kfree(pool->name);
};
EXPORT_SYMBOL_GPL(i2o_pool_free);
