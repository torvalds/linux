// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>

#include <asm/io.h>
#include <asm/delay.h>

#include "vs_dc_mmu.h"

static bool mmu_construct;

int _allocate_memory(u32 bytes, void **memory)
{
	void *mem = NULL;

	if (bytes == 0 || memory == NULL) {
		pr_err("%s has invalid arguments.\n", __func__);
		return -EINVAL;
	}

	if (bytes > PAGE_SIZE)
		mem = vmalloc(bytes);
	else
		mem = kmalloc(bytes, GFP_KERNEL);


	if (!mem) {
		pr_err("%s out of memory.\n", __func__);
		return -ENOMEM;
	}

	memset((u8 *)mem, 0, bytes);
	*memory = mem;

	return 0;
}

static int _create_mutex(void **mutex)
{
	int ret = 0;

	if (mutex == NULL)
		return -EINVAL;

	ret = _allocate_memory(sizeof(struct mutex), mutex);
	if (ret)
		return ret;

	mutex_init(*(struct mutex **)mutex);

	return 0;
}

static int _acquire_mutex(void *mutex, u32 timeout)
{
	if (mutex == NULL) {
		pr_err("%s has invalid argument.\n", __func__);
		return -EINVAL;
	}

	if (timeout == DC_INFINITE) {
		mutex_lock(mutex);
		return 0;
	}

	for (;;) {
		/* Try to acquire the mutex. */
		if (mutex_trylock(mutex)) {
			/* Success. */
			return 0;
		}

		if (timeout-- == 0)
			break;

		/* Wait for 1 millisecond. */
		udelay(1000);
	}

	return -ETIMEDOUT;
}

static int _release_mutex(void *mutex)
{
	if (mutex == NULL) {
		pr_err("%s has invalid argument.\n", __func__);
		return -EINVAL;
	}

	mutex_unlock(mutex);

	return 0;
}

static u32 _mtlb_offset(u32 address)
{
	return (address & MMU_MTLB_MASK) >> MMU_MTLB_SHIFT;
}

static u32 _stlb_offset(u32 address)
{
	return (address & MMU_STLB_4K_MASK) >> MMU_STLB_4K_SHIFT;
}

static u32 _address_to_index(dc_mmu_pt mmu, u32 address)
{
	return _mtlb_offset(address) * MMU_STLB_4K_ENTRY_NUM + _stlb_offset(address);
}

static u32 _set_page(u32 page_address, u32 page_address_ext, bool writable)
{
	u32 entry = page_address
		/* AddressExt */
		| (page_address_ext << 4)
		/* Ignore exception */
		| (0 << 1)
		/* Present */
		| (1 << 0);

	if (writable) {
		/* writable */
		entry |= (1 << 2);
	}

	return entry;
}

static void _write_page_entry(u32 *page_entry, u32 entry_value)
{
	*page_entry = entry_value;
}

static u32 _read_page_entry(u32 *page_entry)
{
	return *page_entry;
}

int _allocate_stlb(dc_mmu_stlb_pt *stlb)
{
	dc_mmu_stlb_pt stlb_t = NULL;
	void *mem = NULL;

	mem = kzalloc(sizeof(dc_mmu_stlb), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	stlb_t = (dc_mmu_stlb_pt)mem;

	stlb_t->size = MMU_STLB_4K_SIZE;

	*stlb = stlb_t;

	return 0;
}

int _allocate_all_stlb(struct device *dev, dc_mmu_stlb_pt *stlb)
{
	dc_mmu_stlb_pt stlb_t = NULL;
	void *mem = NULL;
	void *cookie = NULL;
	dma_addr_t dma_addr;
	size_t size;

	mem = kzalloc(sizeof(dc_mmu_stlb), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	stlb_t = (dc_mmu_stlb_pt)mem;

	stlb_t->size = MMU_STLB_4K_SIZE * MMU_MTLB_ENTRY_NUM;
	size = PAGE_ALIGN(stlb_t->size);

	cookie = dma_alloc_wc(dev, size, &dma_addr, GFP_KERNEL);
	if (!cookie) {
		dev_err(dev, "Failed to alloc stlb buffer.\n");
		return -ENOMEM;
	}

	stlb_t->logical = cookie;
	stlb_t->physBase = (u64)dma_addr;
	memset(stlb_t->logical, 0, size);

	*stlb = stlb_t;

	return 0;
}

int _setup_process_address_space(struct device *dev, dc_mmu_pt mmu)
{
	u32 *map = NULL;
	u32 free, i;
	u32 dynamic_mapping_entries, address;
	dc_mmu_stlb_pt all_stlb;
	int ret = 0;

	dynamic_mapping_entries = MMU_MTLB_ENTRY_NUM;
	mmu->dynamic_mapping_start = 0;
	mmu->page_table_size = dynamic_mapping_entries * MMU_STLB_4K_SIZE;

	mmu->page_table_entries = mmu->page_table_size / sizeof(u32);

	ret = _allocate_memory(mmu->page_table_size,
			(void **)&mmu->map_logical);
	if (ret) {
		pr_err("Failed to alloc mmu map buffer.\n");
		return ret;
	}

	map = mmu->map_logical;

	/* Initialize free area*/
	free = mmu->page_table_entries;
	_write_page_entry(map, (free << 8) | DC_MMU_FREE);
	_write_page_entry(map + 1, ~0U);

	mmu->heap_list	= 0;
	mmu->free_nodes = false;

	ret = _allocate_all_stlb(dev, &all_stlb);
	if (ret)
		return ret;

	for (i = 0; i < dynamic_mapping_entries; i++) {
		dc_mmu_stlb_pt stlb;
		dc_mmu_stlb_pt *stlbs = (dc_mmu_stlb_pt *)mmu->stlbs;

		ret = _allocate_stlb(&stlb);
		if (ret)
			return ret;

		stlb->physBase = all_stlb->physBase + i * MMU_STLB_4K_SIZE;
		stlb->logical = all_stlb->logical + i * MMU_STLB_4K_SIZE / sizeof(u32);

		stlbs[i] = stlb;
	}

	address = (u32)all_stlb->physBase;

	ret = _acquire_mutex(mmu->page_table_mutex, DC_INFINITE);
	if (ret)
		return ret;

	for (i = mmu->dynamic_mapping_start;
		 i < mmu->dynamic_mapping_start + dynamic_mapping_entries;
		 i++) {
		u32 mtlb_entry;

		mtlb_entry = address
			   | MMU_MTLB_4K_PAGE
			   | MMU_MTLB_PRESENT;

		address += MMU_STLB_4K_SIZE;

		/* Insert Slave TLB address to Master TLB entry.*/
		_write_page_entry(mmu->mtlb_logical + i, mtlb_entry);
	}

	_release_mutex(mmu->page_table_mutex);

	return 0;
}

/* MMU Construct */
int dc_mmu_construct(struct device *dev, dc_mmu_pt *mmu)
{
	dc_mmu_pt mmu_t = NULL;
	void *mem = NULL;
	void *cookie = NULL, *cookie_safe = NULL;
	dma_addr_t dma_addr, dma_addr_safe;
	u32 size = 0;
	int ret = 0;

	if (mmu_construct)
		return 0;

	mem = kzalloc(sizeof(dc_mmu), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	mmu_t = (dc_mmu_pt)mem;
	mmu_t->mtlb_bytes = MMU_MTLB_SIZE;
	size = PAGE_ALIGN(mmu_t->mtlb_bytes);

	/* Allocate MTLB */
	cookie = dma_alloc_wc(dev, size, &dma_addr, GFP_KERNEL);
	if (!cookie) {
		dev_err(dev, "Failed to alloc mtlb buffer.\n");
		return -ENOMEM;
	}

	mmu_t->mtlb_logical = cookie;
	mmu_t->mtlb_physical = (u64)dma_addr;
	memset(mmu_t->mtlb_logical, 0, size);

	size = MMU_MTLB_ENTRY_NUM * sizeof(dc_mmu_stlb_pt);

	ret = _allocate_memory(size, &mmu_t->stlbs);
	if (ret)
		return ret;

	ret = _create_mutex(&mmu_t->page_table_mutex);
	if (ret)
		return ret;

	mmu_t->mode = MMU_MODE_1K;

	ret = _setup_process_address_space(dev, mmu_t);
	if (ret)
		return ret;

	/* Allocate safe page */
	cookie_safe = dma_alloc_wc(dev, 4096, &dma_addr_safe, GFP_KERNEL);
	if (!cookie_safe) {
		dev_err(dev, "Failed to alloc safe page.\n");
		return -ENOMEM;
	}

	mmu_t->safe_page_logical = cookie_safe;
	mmu_t->safe_page_physical = (u64)dma_addr_safe;
	memset(mmu_t->safe_page_logical, 0, size);

	*mmu = mmu_t;
	mmu_construct = true;

	return 0;
}

int dc_mmu_get_page_entry(dc_mmu_pt mmu, u32 address, u32 **page_table)
{
	dc_mmu_stlb_pt stlb;
	dc_mmu_stlb_pt *stlbs = (dc_mmu_stlb_pt *)mmu->stlbs;
	u32 mtlb_offset = _mtlb_offset(address);
	u32 stlb_offset = _stlb_offset(address);

	stlb = stlbs[mtlb_offset - mmu->dynamic_mapping_start];
	if (stlb == NULL) {
		pr_err("BUG: invalid stlb,	mmu=%p stlbs=%p  mtlb_offset=0x%x %s(%d)\n",
			mmu, stlbs, mtlb_offset, __func__, __LINE__);
		return -ENXIO;
	}

	*page_table = &stlb->logical[stlb_offset];

	return 0;
}

int _link(dc_mmu_pt mmu, u32 index, u32 node)
{
	if (index >= mmu->page_table_entries) {
		mmu->heap_list = node;
	} else {
		u32 *map = mmu->map_logical;

		switch (DC_ENTRY_TYPE(_read_page_entry(&map[index]))) {
		case DC_MMU_SINGLE:
			/* Previous is a single node, link to it*/
			_write_page_entry(&map[index], (node << 8) | DC_MMU_SINGLE);
			break;
		case DC_MMU_FREE:
			/* Link to FREE TYPE node */
			_write_page_entry(&map[index + 1], node);
			break;
		default:
			pr_err("MMU table corrupted at index %u!", index);
			return -EINVAL;
		}
	}

	return 0;
}

int _add_free(dc_mmu_pt mmu, u32 index, u32 node, u32 count)
{
	u32 *map = mmu->map_logical;

	if (count == 1) {
		/* Initialize a single page node */
		_write_page_entry(map + node, DC_SINGLE_PAGE_NODE_INITIALIZE | DC_MMU_SINGLE);
	} else {
		/* Initialize the FREE node*/
		_write_page_entry(map + node, (count << 8) | DC_MMU_FREE);
		_write_page_entry(map + node + 1, ~0U);
	}

	return _link(mmu, index, node);
}

/* Collect free nodes */
int _collect(dc_mmu_pt mmu)
{
	u32 *map = mmu->map_logical;
	u32 count = 0, start = 0, i = 0;
	u32 previous = ~0U;
	int ret = 0;

	mmu->heap_list = ~0U;
	mmu->free_nodes = false;

	/* Walk the entire page table */
	for (i = 0; i < mmu->page_table_entries; i++) {
		switch (DC_ENTRY_TYPE(_read_page_entry(&map[i]))) {
		case DC_MMU_SINGLE:
			if (count++ == 0) {
				/* Set new start node */
				start = i;
			}
			break;
		case DC_MMU_FREE:
			if (count == 0) {
				/* Set new start node */
				start = i;
			}

			count += _read_page_entry(&map[i]) >> 8;
			/* Advance the index of the page table */
			i += (_read_page_entry(&map[i]) >> 8) - 1;
			break;
		case DC_MMU_USED:
			/* Meet used node, start to collect */
			if (count > 0) {
				/* Add free node to list*/
				ret = _add_free(mmu, previous, start, count);
				if (ret)
					return ret;
				/* Reset previous unused node index */
				previous = start;
				count = 0;
			}
			break;
		default:
			pr_err("MMU page table corrupted at index %u!", i);
			return -EINVAL;
		}
	}

	/* If left node is an open node. */
	if (count > 0) {
		ret = _add_free(mmu, previous, start, count);
		if (ret)
			return ret;
	}

	return 0;
}

int _fill_page_table(u32 *page_table, u32 page_count, u32 entry_value)
{
	u32 i;

	for (i = 0; i < page_count; i++)
		_write_page_entry(page_table + i, entry_value);

	return 0;
}

int dc_mmu_allocate_pages(dc_mmu_pt mmu, u32 page_count, u32 *address)
{
	bool got = false, acquired = false;
	u32 *map;
	u32 index = 0, vaddr, left;
	u32 previous = ~0U;
	u32 mtlb_offset, stlb_offset;
	int ret = 0;

	if (page_count == 0 || page_count > mmu->page_table_entries) {
		pr_err("%s has invalid arguments.\n", __func__);
		return -EINVAL;
	}

	_acquire_mutex(mmu->page_table_mutex, DC_INFINITE);
	acquired = true;

	for (map = mmu->map_logical; !got;) {
		for (index = mmu->heap_list; !got && (index < mmu->page_table_entries);) {
			switch (DC_ENTRY_TYPE(_read_page_entry(&map[index]))) {
			case DC_MMU_SINGLE:
				if (page_count == 1) {
					got = true;
				} else {
					/* Move to next node */
					previous = index;
					index = _read_page_entry(&map[index]) >> 8;
				}
				break;
			case DC_MMU_FREE:
				if (page_count <= (_read_page_entry(&map[index]) >> 8)) {
					got = true;
				} else {
					/* Move to next node */
					previous = index;
					index = _read_page_entry(&map[index + 1]);
				}
				break;
			default:
				/* Only link SINGLE and FREE node */
				pr_err("MMU table corrupted at index %u!", index);
				ret = -EINVAL;
				goto OnError;
			}
		}

		/* If out of index */
		if (index >= mmu->page_table_entries) {
			if (mmu->free_nodes) {
				/* Collect the free node */
				ret = _collect(mmu);
				if (ret)
					goto OnError;
			} else {
				ret = -ENODATA;
				goto OnError;
			}
		}
	}

	switch (DC_ENTRY_TYPE(_read_page_entry(&map[index]))) {
	case DC_MMU_SINGLE:
		/* Unlink single node from node list */
		ret = _link(mmu, previous, _read_page_entry(&map[index]) >> 8);
		if (ret)
			goto OnError;
		break;

	case DC_MMU_FREE:
		left = (_read_page_entry(&map[index]) >> 8) - page_count;
		switch (left) {
		case 0:
			/* Unlink the entire FREE type node */
			ret = _link(mmu, previous, _read_page_entry(&map[index + 1]));
			if (ret)
				goto OnError;
			break;
		case 1:
			/* Keep the map[index] as a single node,
			 * mark the left as used
			 */
			_write_page_entry(&map[index],
				(_read_page_entry(&map[index + 1]) << 8) |
				DC_MMU_SINGLE);
			index++;
			break;
		default:
			/* FREE type node left */
			_write_page_entry(&map[index],
					  (left << 8) | DC_MMU_FREE);
			index += left;
			break;
		}
		break;
	default:
		/* Only link SINGLE and FREE node */
		pr_err("MMU table corrupted at index %u!", index);
		ret = -EINVAL;
		goto OnError;
	}

	/* Mark node as used */
	ret = _fill_page_table(&map[index], page_count, DC_MMU_USED);
	if (ret)
		goto OnError;

	_release_mutex(mmu->page_table_mutex);

	mtlb_offset = index / MMU_STLB_4K_ENTRY_NUM + mmu->dynamic_mapping_start;
	stlb_offset = index % MMU_STLB_4K_ENTRY_NUM;

	vaddr = (mtlb_offset << MMU_MTLB_SHIFT) | (stlb_offset << MMU_STLB_4K_SHIFT);

	if (address != NULL)
		*address = vaddr;

	return 0;

OnError:
	if (acquired)
		_release_mutex(mmu->page_table_mutex);

	return ret;
}

int dc_mmu_free_pages(dc_mmu_pt mmu, u32 address, u32 page_count)
{
	u32 *node;

	if (page_count == 0)
		return -EINVAL;

	node = mmu->map_logical + _address_to_index(mmu, address);

	_acquire_mutex(mmu->page_table_mutex, DC_INFINITE);

	if (page_count == 1) {
		/* Mark the Single page node free */
		_write_page_entry(node, DC_SINGLE_PAGE_NODE_INITIALIZE | DC_MMU_SINGLE);
	} else {
		 /* Mark the FREE type node free */
		_write_page_entry(node, (page_count << 8) | DC_MMU_FREE);
		_write_page_entry(node + 1, ~0U);
	}

	mmu->free_nodes = true;

	_release_mutex(mmu->page_table_mutex);

	return 0;
}

int dc_mmu_set_page(dc_mmu_pt mmu, u64 page_address, u32 *page_entry)
{
	u32 address_ext;
	u32 address;

	if (page_entry == NULL || (page_address & 0xFFF))
		return -EINVAL;

	/* [31:0]. */
	address = (u32)(page_address & 0xFFFFFFFF);
	/* [39:32]. */
	address_ext = (u32)((page_address >> 32) & 0xFF);

	_write_page_entry(page_entry, _set_page(address, address_ext, true));

	return 0;
}

int dc_mmu_map_memory(dc_mmu_pt mmu, u64 physical, u32 page_count,
			  u32 *address, bool continuous, bool security)
{
	u32 virutal_address, i = 0;
	u32 mtlb_num, mtlb_entry, mtlb_offset;
	bool allocated = false;
	int ret = 0;

	ret = dc_mmu_allocate_pages(mmu, page_count, &virutal_address);
	if (ret)
		goto OnError;

	*address = virutal_address;
	allocated = true;

	/*Fill mtlb security bit*/
	mtlb_num = _mtlb_offset(virutal_address + page_count * MMU_PAGE_4K_SIZE - 1) -
		_mtlb_offset(virutal_address) + 1;
	mtlb_offset = _mtlb_offset(virutal_address);
	mtlb_entry = mmu->mtlb_logical[mtlb_offset];

	for (i = 0; i < mtlb_num ; i++) {
		mtlb_entry = mmu->mtlb_logical[mtlb_offset + i];
		if (security) {
			mtlb_entry = mtlb_entry
					   | MMU_MTLB_SECURITY
					   | MMU_MTLB_EXCEPTION;
			_write_page_entry(&mmu->mtlb_logical[mtlb_offset + i], mtlb_entry);
		} else {
			mtlb_entry = mtlb_entry & (~MMU_MTLB_SECURITY);
			_write_page_entry(&mmu->mtlb_logical[mtlb_offset + i], mtlb_entry);
		}
	}

	/* Fill in page table */
	for (i = 0; i < page_count; i++) {
		u64 page_phy;
		u32 *page_entry;
		struct page **pages;

		if (continuous == true) {
			page_phy = physical + i * MMU_PAGE_4K_SIZE;
		} else {
			pages = (struct page **)physical;
			page_phy = page_to_phys(pages[i]);
		}

		ret = dc_mmu_get_page_entry(mmu, virutal_address, &page_entry);
		if (ret)
			goto OnError;

		/* Write the page address to the page entry */
		ret = dc_mmu_set_page(mmu, page_phy, page_entry);
		if (ret)
			goto OnError;

		/* Get next page */
		virutal_address += MMU_PAGE_4K_SIZE;
	}

	return 0;

OnError:
	if (allocated)
		dc_mmu_free_pages(mmu, virutal_address, page_count);
	pr_info("%s fail!\n", __func__);

	return ret;
}

int dc_mmu_unmap_memory(dc_mmu_pt mmu, u32 gpu_address, u32 page_count)
{
	return dc_mmu_free_pages(mmu, gpu_address, page_count);
}
