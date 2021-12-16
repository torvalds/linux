// SPDX-License-Identifier: GPL-2.0-only
/*
 *  PS3 address space management.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 */

#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/memblock.h>
#include <linux/slab.h>

#include <asm/cell-regs.h>
#include <asm/firmware.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/lv1call.h>
#include <asm/setup.h>

#include "platform.h"

#if defined(DEBUG)
#define DBG udbg_printf
#else
#define DBG pr_devel
#endif

enum {
#if defined(CONFIG_PS3_DYNAMIC_DMA)
	USE_DYNAMIC_DMA = 1,
#else
	USE_DYNAMIC_DMA = 0,
#endif
};

enum {
	PAGE_SHIFT_4K = 12U,
	PAGE_SHIFT_64K = 16U,
	PAGE_SHIFT_16M = 24U,
};

static unsigned long __init make_page_sizes(unsigned long a, unsigned long b)
{
	return (a << 56) | (b << 48);
}

enum {
	ALLOCATE_MEMORY_TRY_ALT_UNIT = 0X04,
	ALLOCATE_MEMORY_ADDR_ZERO = 0X08,
};

/* valid htab sizes are {18,19,20} = 256K, 512K, 1M */

enum {
	HTAB_SIZE_MAX = 20U, /* HV limit of 1MB */
	HTAB_SIZE_MIN = 18U, /* CPU limit of 256KB */
};

/*============================================================================*/
/* virtual address space routines                                             */
/*============================================================================*/

/**
 * struct mem_region - memory region structure
 * @base: base address
 * @size: size in bytes
 * @offset: difference between base and rm.size
 * @destroy: flag if region should be destroyed upon shutdown
 */

struct mem_region {
	u64 base;
	u64 size;
	unsigned long offset;
	int destroy;
};

/**
 * struct map - address space state variables holder
 * @total: total memory available as reported by HV
 * @vas_id - HV virtual address space id
 * @htab_size: htab size in bytes
 *
 * The HV virtual address space (vas) allows for hotplug memory regions.
 * Memory regions can be created and destroyed in the vas at runtime.
 * @rm: real mode (bootmem) region
 * @r1: highmem region(s)
 *
 * ps3 addresses
 * virt_addr: a cpu 'translated' effective address
 * phys_addr: an address in what Linux thinks is the physical address space
 * lpar_addr: an address in the HV virtual address space
 * bus_addr: an io controller 'translated' address on a device bus
 */

struct map {
	u64 total;
	u64 vas_id;
	u64 htab_size;
	struct mem_region rm;
	struct mem_region r1;
};

#define debug_dump_map(x) _debug_dump_map(x, __func__, __LINE__)
static void __maybe_unused _debug_dump_map(const struct map *m,
	const char *func, int line)
{
	DBG("%s:%d: map.total     = %llxh\n", func, line, m->total);
	DBG("%s:%d: map.rm.size   = %llxh\n", func, line, m->rm.size);
	DBG("%s:%d: map.vas_id    = %llu\n", func, line, m->vas_id);
	DBG("%s:%d: map.htab_size = %llxh\n", func, line, m->htab_size);
	DBG("%s:%d: map.r1.base   = %llxh\n", func, line, m->r1.base);
	DBG("%s:%d: map.r1.offset = %lxh\n", func, line, m->r1.offset);
	DBG("%s:%d: map.r1.size   = %llxh\n", func, line, m->r1.size);
}

static struct map map;

/**
 * ps3_mm_phys_to_lpar - translate a linux physical address to lpar address
 * @phys_addr: linux physical address
 */

unsigned long ps3_mm_phys_to_lpar(unsigned long phys_addr)
{
	BUG_ON(is_kernel_addr(phys_addr));
	return (phys_addr < map.rm.size || phys_addr >= map.total)
		? phys_addr : phys_addr + map.r1.offset;
}

EXPORT_SYMBOL(ps3_mm_phys_to_lpar);

/**
 * ps3_mm_vas_create - create the virtual address space
 */

void __init ps3_mm_vas_create(unsigned long* htab_size)
{
	int result;
	u64 start_address;
	u64 size;
	u64 access_right;
	u64 max_page_size;
	u64 flags;

	result = lv1_query_logical_partition_address_region_info(0,
		&start_address, &size, &access_right, &max_page_size,
		&flags);

	if (result) {
		DBG("%s:%d: lv1_query_logical_partition_address_region_info "
			"failed: %s\n", __func__, __LINE__,
			ps3_result(result));
		goto fail;
	}

	if (max_page_size < PAGE_SHIFT_16M) {
		DBG("%s:%d: bad max_page_size %llxh\n", __func__, __LINE__,
			max_page_size);
		goto fail;
	}

	BUILD_BUG_ON(CONFIG_PS3_HTAB_SIZE > HTAB_SIZE_MAX);
	BUILD_BUG_ON(CONFIG_PS3_HTAB_SIZE < HTAB_SIZE_MIN);

	result = lv1_construct_virtual_address_space(CONFIG_PS3_HTAB_SIZE,
			2, make_page_sizes(PAGE_SHIFT_16M, PAGE_SHIFT_64K),
			&map.vas_id, &map.htab_size);

	if (result) {
		DBG("%s:%d: lv1_construct_virtual_address_space failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		goto fail;
	}

	result = lv1_select_virtual_address_space(map.vas_id);

	if (result) {
		DBG("%s:%d: lv1_select_virtual_address_space failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		goto fail;
	}

	*htab_size = map.htab_size;

	debug_dump_map(&map);

	return;

fail:
	panic("ps3_mm_vas_create failed");
}

/**
 * ps3_mm_vas_destroy -
 *
 * called during kexec sequence with MMU off.
 */

notrace void ps3_mm_vas_destroy(void)
{
	int result;

	if (map.vas_id) {
		result = lv1_select_virtual_address_space(0);
		result += lv1_destruct_virtual_address_space(map.vas_id);

		if (result) {
			lv1_panic(0);
		}

		map.vas_id = 0;
	}
}

static int __init ps3_mm_get_repository_highmem(struct mem_region *r)
{
	int result;

	/* Assume a single highmem region. */

	result = ps3_repository_read_highmem_info(0, &r->base, &r->size);

	if (result)
		goto zero_region;

	if (!r->base || !r->size) {
		result = -1;
		goto zero_region;
	}

	r->offset = r->base - map.rm.size;

	DBG("%s:%d: Found high region in repository: %llxh %llxh\n",
	    __func__, __LINE__, r->base, r->size);

	return 0;

zero_region:
	DBG("%s:%d: No high region in repository.\n", __func__, __LINE__);

	r->size = r->base = r->offset = 0;
	return result;
}

static int ps3_mm_set_repository_highmem(const struct mem_region *r)
{
	/* Assume a single highmem region. */

	return r ? ps3_repository_write_highmem_info(0, r->base, r->size) :
		ps3_repository_write_highmem_info(0, 0, 0);
}

/**
 * ps3_mm_region_create - create a memory region in the vas
 * @r: pointer to a struct mem_region to accept initialized values
 * @size: requested region size
 *
 * This implementation creates the region with the vas large page size.
 * @size is rounded down to a multiple of the vas large page size.
 */

static int ps3_mm_region_create(struct mem_region *r, unsigned long size)
{
	int result;
	u64 muid;

	r->size = ALIGN_DOWN(size, 1 << PAGE_SHIFT_16M);

	DBG("%s:%d requested  %lxh\n", __func__, __LINE__, size);
	DBG("%s:%d actual     %llxh\n", __func__, __LINE__, r->size);
	DBG("%s:%d difference %llxh (%lluMB)\n", __func__, __LINE__,
		size - r->size, (size - r->size) / 1024 / 1024);

	if (r->size == 0) {
		DBG("%s:%d: size == 0\n", __func__, __LINE__);
		result = -1;
		goto zero_region;
	}

	result = lv1_allocate_memory(r->size, PAGE_SHIFT_16M, 0,
		ALLOCATE_MEMORY_TRY_ALT_UNIT, &r->base, &muid);

	if (result || r->base < map.rm.size) {
		DBG("%s:%d: lv1_allocate_memory failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		goto zero_region;
	}

	r->destroy = 1;
	r->offset = r->base - map.rm.size;
	return result;

zero_region:
	r->size = r->base = r->offset = 0;
	return result;
}

/**
 * ps3_mm_region_destroy - destroy a memory region
 * @r: pointer to struct mem_region
 */

static void ps3_mm_region_destroy(struct mem_region *r)
{
	int result;

	if (!r->destroy) {
		return;
	}

	if (r->base) {
		result = lv1_release_memory(r->base);

		if (result) {
			lv1_panic(0);
		}

		r->size = r->base = r->offset = 0;
		map.total = map.rm.size;
	}

	ps3_mm_set_repository_highmem(NULL);
}

/*============================================================================*/
/* dma routines                                                               */
/*============================================================================*/

/**
 * dma_sb_lpar_to_bus - Translate an lpar address to ioc mapped bus address.
 * @r: pointer to dma region structure
 * @lpar_addr: HV lpar address
 */

static unsigned long dma_sb_lpar_to_bus(struct ps3_dma_region *r,
	unsigned long lpar_addr)
{
	if (lpar_addr >= map.rm.size)
		lpar_addr -= map.r1.offset;
	BUG_ON(lpar_addr < r->offset);
	BUG_ON(lpar_addr >= r->offset + r->len);
	return r->bus_addr + lpar_addr - r->offset;
}

#define dma_dump_region(_a) _dma_dump_region(_a, __func__, __LINE__)
static void  __maybe_unused _dma_dump_region(const struct ps3_dma_region *r,
	const char *func, int line)
{
	DBG("%s:%d: dev        %llu:%llu\n", func, line, r->dev->bus_id,
		r->dev->dev_id);
	DBG("%s:%d: page_size  %u\n", func, line, r->page_size);
	DBG("%s:%d: bus_addr   %lxh\n", func, line, r->bus_addr);
	DBG("%s:%d: len        %lxh\n", func, line, r->len);
	DBG("%s:%d: offset     %lxh\n", func, line, r->offset);
}

  /**
 * dma_chunk - A chunk of dma pages mapped by the io controller.
 * @region - The dma region that owns this chunk.
 * @lpar_addr: Starting lpar address of the area to map.
 * @bus_addr: Starting ioc bus address of the area to map.
 * @len: Length in bytes of the area to map.
 * @link: A struct list_head used with struct ps3_dma_region.chunk_list, the
 * list of all chuncks owned by the region.
 *
 * This implementation uses a very simple dma page manager
 * based on the dma_chunk structure.  This scheme assumes
 * that all drivers use very well behaved dma ops.
 */

struct dma_chunk {
	struct ps3_dma_region *region;
	unsigned long lpar_addr;
	unsigned long bus_addr;
	unsigned long len;
	struct list_head link;
	unsigned int usage_count;
};

#define dma_dump_chunk(_a) _dma_dump_chunk(_a, __func__, __LINE__)
static void _dma_dump_chunk (const struct dma_chunk* c, const char* func,
	int line)
{
	DBG("%s:%d: r.dev        %llu:%llu\n", func, line,
		c->region->dev->bus_id, c->region->dev->dev_id);
	DBG("%s:%d: r.bus_addr   %lxh\n", func, line, c->region->bus_addr);
	DBG("%s:%d: r.page_size  %u\n", func, line, c->region->page_size);
	DBG("%s:%d: r.len        %lxh\n", func, line, c->region->len);
	DBG("%s:%d: r.offset     %lxh\n", func, line, c->region->offset);
	DBG("%s:%d: c.lpar_addr  %lxh\n", func, line, c->lpar_addr);
	DBG("%s:%d: c.bus_addr   %lxh\n", func, line, c->bus_addr);
	DBG("%s:%d: c.len        %lxh\n", func, line, c->len);
}

static struct dma_chunk * dma_find_chunk(struct ps3_dma_region *r,
	unsigned long bus_addr, unsigned long len)
{
	struct dma_chunk *c;
	unsigned long aligned_bus = ALIGN_DOWN(bus_addr, 1 << r->page_size);
	unsigned long aligned_len = ALIGN(len+bus_addr-aligned_bus,
					      1 << r->page_size);

	list_for_each_entry(c, &r->chunk_list.head, link) {
		/* intersection */
		if (aligned_bus >= c->bus_addr &&
		    aligned_bus + aligned_len <= c->bus_addr + c->len)
			return c;

		/* below */
		if (aligned_bus + aligned_len <= c->bus_addr)
			continue;

		/* above */
		if (aligned_bus >= c->bus_addr + c->len)
			continue;

		/* we don't handle the multi-chunk case for now */
		dma_dump_chunk(c);
		BUG();
	}
	return NULL;
}

static struct dma_chunk *dma_find_chunk_lpar(struct ps3_dma_region *r,
	unsigned long lpar_addr, unsigned long len)
{
	struct dma_chunk *c;
	unsigned long aligned_lpar = ALIGN_DOWN(lpar_addr, 1 << r->page_size);
	unsigned long aligned_len = ALIGN(len + lpar_addr - aligned_lpar,
					      1 << r->page_size);

	list_for_each_entry(c, &r->chunk_list.head, link) {
		/* intersection */
		if (c->lpar_addr <= aligned_lpar &&
		    aligned_lpar < c->lpar_addr + c->len) {
			if (aligned_lpar + aligned_len <= c->lpar_addr + c->len)
				return c;
			else {
				dma_dump_chunk(c);
				BUG();
			}
		}
		/* below */
		if (aligned_lpar + aligned_len <= c->lpar_addr) {
			continue;
		}
		/* above */
		if (c->lpar_addr + c->len <= aligned_lpar) {
			continue;
		}
	}
	return NULL;
}

static int dma_sb_free_chunk(struct dma_chunk *c)
{
	int result = 0;

	if (c->bus_addr) {
		result = lv1_unmap_device_dma_region(c->region->dev->bus_id,
			c->region->dev->dev_id, c->bus_addr, c->len);
		BUG_ON(result);
	}

	kfree(c);
	return result;
}

static int dma_ioc0_free_chunk(struct dma_chunk *c)
{
	int result = 0;
	int iopage;
	unsigned long offset;
	struct ps3_dma_region *r = c->region;

	DBG("%s:start\n", __func__);
	for (iopage = 0; iopage < (c->len >> r->page_size); iopage++) {
		offset = (1 << r->page_size) * iopage;
		/* put INVALID entry */
		result = lv1_put_iopte(0,
				       c->bus_addr + offset,
				       c->lpar_addr + offset,
				       r->ioid,
				       0);
		DBG("%s: bus=%#lx, lpar=%#lx, ioid=%d\n", __func__,
		    c->bus_addr + offset,
		    c->lpar_addr + offset,
		    r->ioid);

		if (result) {
			DBG("%s:%d: lv1_put_iopte failed: %s\n", __func__,
			    __LINE__, ps3_result(result));
		}
	}
	kfree(c);
	DBG("%s:end\n", __func__);
	return result;
}

/**
 * dma_sb_map_pages - Maps dma pages into the io controller bus address space.
 * @r: Pointer to a struct ps3_dma_region.
 * @phys_addr: Starting physical address of the area to map.
 * @len: Length in bytes of the area to map.
 * c_out: A pointer to receive an allocated struct dma_chunk for this area.
 *
 * This is the lowest level dma mapping routine, and is the one that will
 * make the HV call to add the pages into the io controller address space.
 */

static int dma_sb_map_pages(struct ps3_dma_region *r, unsigned long phys_addr,
	    unsigned long len, struct dma_chunk **c_out, u64 iopte_flag)
{
	int result;
	struct dma_chunk *c;

	c = kzalloc(sizeof(*c), GFP_ATOMIC);
	if (!c) {
		result = -ENOMEM;
		goto fail_alloc;
	}

	c->region = r;
	c->lpar_addr = ps3_mm_phys_to_lpar(phys_addr);
	c->bus_addr = dma_sb_lpar_to_bus(r, c->lpar_addr);
	c->len = len;

	BUG_ON(iopte_flag != 0xf800000000000000UL);
	result = lv1_map_device_dma_region(c->region->dev->bus_id,
					   c->region->dev->dev_id, c->lpar_addr,
					   c->bus_addr, c->len, iopte_flag);
	if (result) {
		DBG("%s:%d: lv1_map_device_dma_region failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		goto fail_map;
	}

	list_add(&c->link, &r->chunk_list.head);

	*c_out = c;
	return 0;

fail_map:
	kfree(c);
fail_alloc:
	*c_out = NULL;
	DBG(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

static int dma_ioc0_map_pages(struct ps3_dma_region *r, unsigned long phys_addr,
			      unsigned long len, struct dma_chunk **c_out,
			      u64 iopte_flag)
{
	int result;
	struct dma_chunk *c, *last;
	int iopage, pages;
	unsigned long offset;

	DBG(KERN_ERR "%s: phy=%#lx, lpar%#lx, len=%#lx\n", __func__,
	    phys_addr, ps3_mm_phys_to_lpar(phys_addr), len);
	c = kzalloc(sizeof(*c), GFP_ATOMIC);
	if (!c) {
		result = -ENOMEM;
		goto fail_alloc;
	}

	c->region = r;
	c->len = len;
	c->lpar_addr = ps3_mm_phys_to_lpar(phys_addr);
	/* allocate IO address */
	if (list_empty(&r->chunk_list.head)) {
		/* first one */
		c->bus_addr = r->bus_addr;
	} else {
		/* derive from last bus addr*/
		last  = list_entry(r->chunk_list.head.next,
				   struct dma_chunk, link);
		c->bus_addr = last->bus_addr + last->len;
		DBG("%s: last bus=%#lx, len=%#lx\n", __func__,
		    last->bus_addr, last->len);
	}

	/* FIXME: check whether length exceeds region size */

	/* build ioptes for the area */
	pages = len >> r->page_size;
	DBG("%s: pgsize=%#x len=%#lx pages=%#x iopteflag=%#llx\n", __func__,
	    r->page_size, r->len, pages, iopte_flag);
	for (iopage = 0; iopage < pages; iopage++) {
		offset = (1 << r->page_size) * iopage;
		result = lv1_put_iopte(0,
				       c->bus_addr + offset,
				       c->lpar_addr + offset,
				       r->ioid,
				       iopte_flag);
		if (result) {
			pr_warn("%s:%d: lv1_put_iopte failed: %s\n",
				__func__, __LINE__, ps3_result(result));
			goto fail_map;
		}
		DBG("%s: pg=%d bus=%#lx, lpar=%#lx, ioid=%#x\n", __func__,
		    iopage, c->bus_addr + offset, c->lpar_addr + offset,
		    r->ioid);
	}

	/* be sure that last allocated one is inserted at head */
	list_add(&c->link, &r->chunk_list.head);

	*c_out = c;
	DBG("%s: end\n", __func__);
	return 0;

fail_map:
	for (iopage--; 0 <= iopage; iopage--) {
		lv1_put_iopte(0,
			      c->bus_addr + offset,
			      c->lpar_addr + offset,
			      r->ioid,
			      0);
	}
	kfree(c);
fail_alloc:
	*c_out = NULL;
	return result;
}

/**
 * dma_sb_region_create - Create a device dma region.
 * @r: Pointer to a struct ps3_dma_region.
 *
 * This is the lowest level dma region create routine, and is the one that
 * will make the HV call to create the region.
 */

static int dma_sb_region_create(struct ps3_dma_region *r)
{
	int result;
	u64 bus_addr;

	DBG(" -> %s:%d:\n", __func__, __LINE__);

	BUG_ON(!r);

	if (!r->dev->bus_id) {
		pr_info("%s:%d: %llu:%llu no dma\n", __func__, __LINE__,
			r->dev->bus_id, r->dev->dev_id);
		return 0;
	}

	DBG("%s:%u: len = 0x%lx, page_size = %u, offset = 0x%lx\n", __func__,
	    __LINE__, r->len, r->page_size, r->offset);

	BUG_ON(!r->len);
	BUG_ON(!r->page_size);
	BUG_ON(!r->region_ops);

	INIT_LIST_HEAD(&r->chunk_list.head);
	spin_lock_init(&r->chunk_list.lock);

	result = lv1_allocate_device_dma_region(r->dev->bus_id, r->dev->dev_id,
		roundup_pow_of_two(r->len), r->page_size, r->region_type,
		&bus_addr);
	r->bus_addr = bus_addr;

	if (result) {
		DBG("%s:%d: lv1_allocate_device_dma_region failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		r->len = r->bus_addr = 0;
	}

	return result;
}

static int dma_ioc0_region_create(struct ps3_dma_region *r)
{
	int result;
	u64 bus_addr;

	INIT_LIST_HEAD(&r->chunk_list.head);
	spin_lock_init(&r->chunk_list.lock);

	result = lv1_allocate_io_segment(0,
					 r->len,
					 r->page_size,
					 &bus_addr);
	r->bus_addr = bus_addr;
	if (result) {
		DBG("%s:%d: lv1_allocate_io_segment failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		r->len = r->bus_addr = 0;
	}
	DBG("%s: len=%#lx, pg=%d, bus=%#lx\n", __func__,
	    r->len, r->page_size, r->bus_addr);
	return result;
}

/**
 * dma_region_free - Free a device dma region.
 * @r: Pointer to a struct ps3_dma_region.
 *
 * This is the lowest level dma region free routine, and is the one that
 * will make the HV call to free the region.
 */

static int dma_sb_region_free(struct ps3_dma_region *r)
{
	int result;
	struct dma_chunk *c;
	struct dma_chunk *tmp;

	BUG_ON(!r);

	if (!r->dev->bus_id) {
		pr_info("%s:%d: %llu:%llu no dma\n", __func__, __LINE__,
			r->dev->bus_id, r->dev->dev_id);
		return 0;
	}

	list_for_each_entry_safe(c, tmp, &r->chunk_list.head, link) {
		list_del(&c->link);
		dma_sb_free_chunk(c);
	}

	result = lv1_free_device_dma_region(r->dev->bus_id, r->dev->dev_id,
		r->bus_addr);

	if (result)
		DBG("%s:%d: lv1_free_device_dma_region failed: %s\n",
			__func__, __LINE__, ps3_result(result));

	r->bus_addr = 0;

	return result;
}

static int dma_ioc0_region_free(struct ps3_dma_region *r)
{
	int result;
	struct dma_chunk *c, *n;

	DBG("%s: start\n", __func__);
	list_for_each_entry_safe(c, n, &r->chunk_list.head, link) {
		list_del(&c->link);
		dma_ioc0_free_chunk(c);
	}

	result = lv1_release_io_segment(0, r->bus_addr);

	if (result)
		DBG("%s:%d: lv1_free_device_dma_region failed: %s\n",
			__func__, __LINE__, ps3_result(result));

	r->bus_addr = 0;
	DBG("%s: end\n", __func__);

	return result;
}

/**
 * dma_sb_map_area - Map an area of memory into a device dma region.
 * @r: Pointer to a struct ps3_dma_region.
 * @virt_addr: Starting virtual address of the area to map.
 * @len: Length in bytes of the area to map.
 * @bus_addr: A pointer to return the starting ioc bus address of the area to
 * map.
 *
 * This is the common dma mapping routine.
 */

static int dma_sb_map_area(struct ps3_dma_region *r, unsigned long virt_addr,
	   unsigned long len, dma_addr_t *bus_addr,
	   u64 iopte_flag)
{
	int result;
	unsigned long flags;
	struct dma_chunk *c;
	unsigned long phys_addr = is_kernel_addr(virt_addr) ? __pa(virt_addr)
		: virt_addr;
	unsigned long aligned_phys = ALIGN_DOWN(phys_addr, 1 << r->page_size);
	unsigned long aligned_len = ALIGN(len + phys_addr - aligned_phys,
					      1 << r->page_size);
	*bus_addr = dma_sb_lpar_to_bus(r, ps3_mm_phys_to_lpar(phys_addr));

	if (!USE_DYNAMIC_DMA) {
		unsigned long lpar_addr = ps3_mm_phys_to_lpar(phys_addr);
		DBG(" -> %s:%d\n", __func__, __LINE__);
		DBG("%s:%d virt_addr %lxh\n", __func__, __LINE__,
			virt_addr);
		DBG("%s:%d phys_addr %lxh\n", __func__, __LINE__,
			phys_addr);
		DBG("%s:%d lpar_addr %lxh\n", __func__, __LINE__,
			lpar_addr);
		DBG("%s:%d len       %lxh\n", __func__, __LINE__, len);
		DBG("%s:%d bus_addr  %llxh (%lxh)\n", __func__, __LINE__,
		*bus_addr, len);
	}

	spin_lock_irqsave(&r->chunk_list.lock, flags);
	c = dma_find_chunk(r, *bus_addr, len);

	if (c) {
		DBG("%s:%d: reusing mapped chunk", __func__, __LINE__);
		dma_dump_chunk(c);
		c->usage_count++;
		spin_unlock_irqrestore(&r->chunk_list.lock, flags);
		return 0;
	}

	result = dma_sb_map_pages(r, aligned_phys, aligned_len, &c, iopte_flag);

	if (result) {
		*bus_addr = 0;
		DBG("%s:%d: dma_sb_map_pages failed (%d)\n",
			__func__, __LINE__, result);
		spin_unlock_irqrestore(&r->chunk_list.lock, flags);
		return result;
	}

	c->usage_count = 1;

	spin_unlock_irqrestore(&r->chunk_list.lock, flags);
	return result;
}

static int dma_ioc0_map_area(struct ps3_dma_region *r, unsigned long virt_addr,
	     unsigned long len, dma_addr_t *bus_addr,
	     u64 iopte_flag)
{
	int result;
	unsigned long flags;
	struct dma_chunk *c;
	unsigned long phys_addr = is_kernel_addr(virt_addr) ? __pa(virt_addr)
		: virt_addr;
	unsigned long aligned_phys = ALIGN_DOWN(phys_addr, 1 << r->page_size);
	unsigned long aligned_len = ALIGN(len + phys_addr - aligned_phys,
					      1 << r->page_size);

	DBG(KERN_ERR "%s: vaddr=%#lx, len=%#lx\n", __func__,
	    virt_addr, len);
	DBG(KERN_ERR "%s: ph=%#lx a_ph=%#lx a_l=%#lx\n", __func__,
	    phys_addr, aligned_phys, aligned_len);

	spin_lock_irqsave(&r->chunk_list.lock, flags);
	c = dma_find_chunk_lpar(r, ps3_mm_phys_to_lpar(phys_addr), len);

	if (c) {
		/* FIXME */
		BUG();
		*bus_addr = c->bus_addr + phys_addr - aligned_phys;
		c->usage_count++;
		spin_unlock_irqrestore(&r->chunk_list.lock, flags);
		return 0;
	}

	result = dma_ioc0_map_pages(r, aligned_phys, aligned_len, &c,
				    iopte_flag);

	if (result) {
		*bus_addr = 0;
		DBG("%s:%d: dma_ioc0_map_pages failed (%d)\n",
			__func__, __LINE__, result);
		spin_unlock_irqrestore(&r->chunk_list.lock, flags);
		return result;
	}
	*bus_addr = c->bus_addr + phys_addr - aligned_phys;
	DBG("%s: va=%#lx pa=%#lx a_pa=%#lx bus=%#llx\n", __func__,
	    virt_addr, phys_addr, aligned_phys, *bus_addr);
	c->usage_count = 1;

	spin_unlock_irqrestore(&r->chunk_list.lock, flags);
	return result;
}

/**
 * dma_sb_unmap_area - Unmap an area of memory from a device dma region.
 * @r: Pointer to a struct ps3_dma_region.
 * @bus_addr: The starting ioc bus address of the area to unmap.
 * @len: Length in bytes of the area to unmap.
 *
 * This is the common dma unmap routine.
 */

static int dma_sb_unmap_area(struct ps3_dma_region *r, dma_addr_t bus_addr,
	unsigned long len)
{
	unsigned long flags;
	struct dma_chunk *c;

	spin_lock_irqsave(&r->chunk_list.lock, flags);
	c = dma_find_chunk(r, bus_addr, len);

	if (!c) {
		unsigned long aligned_bus = ALIGN_DOWN(bus_addr,
			1 << r->page_size);
		unsigned long aligned_len = ALIGN(len + bus_addr
			- aligned_bus, 1 << r->page_size);
		DBG("%s:%d: not found: bus_addr %llxh\n",
			__func__, __LINE__, bus_addr);
		DBG("%s:%d: not found: len %lxh\n",
			__func__, __LINE__, len);
		DBG("%s:%d: not found: aligned_bus %lxh\n",
			__func__, __LINE__, aligned_bus);
		DBG("%s:%d: not found: aligned_len %lxh\n",
			__func__, __LINE__, aligned_len);
		BUG();
	}

	c->usage_count--;

	if (!c->usage_count) {
		list_del(&c->link);
		dma_sb_free_chunk(c);
	}

	spin_unlock_irqrestore(&r->chunk_list.lock, flags);
	return 0;
}

static int dma_ioc0_unmap_area(struct ps3_dma_region *r,
			dma_addr_t bus_addr, unsigned long len)
{
	unsigned long flags;
	struct dma_chunk *c;

	DBG("%s: start a=%#llx l=%#lx\n", __func__, bus_addr, len);
	spin_lock_irqsave(&r->chunk_list.lock, flags);
	c = dma_find_chunk(r, bus_addr, len);

	if (!c) {
		unsigned long aligned_bus = ALIGN_DOWN(bus_addr,
							1 << r->page_size);
		unsigned long aligned_len = ALIGN(len + bus_addr
						      - aligned_bus,
						      1 << r->page_size);
		DBG("%s:%d: not found: bus_addr %llxh\n",
		    __func__, __LINE__, bus_addr);
		DBG("%s:%d: not found: len %lxh\n",
		    __func__, __LINE__, len);
		DBG("%s:%d: not found: aligned_bus %lxh\n",
		    __func__, __LINE__, aligned_bus);
		DBG("%s:%d: not found: aligned_len %lxh\n",
		    __func__, __LINE__, aligned_len);
		BUG();
	}

	c->usage_count--;

	if (!c->usage_count) {
		list_del(&c->link);
		dma_ioc0_free_chunk(c);
	}

	spin_unlock_irqrestore(&r->chunk_list.lock, flags);
	DBG("%s: end\n", __func__);
	return 0;
}

/**
 * dma_sb_region_create_linear - Setup a linear dma mapping for a device.
 * @r: Pointer to a struct ps3_dma_region.
 *
 * This routine creates an HV dma region for the device and maps all available
 * ram into the io controller bus address space.
 */

static int dma_sb_region_create_linear(struct ps3_dma_region *r)
{
	int result;
	unsigned long virt_addr, len;
	dma_addr_t tmp;

	if (r->len > 16*1024*1024) {	/* FIXME: need proper fix */
		/* force 16M dma pages for linear mapping */
		if (r->page_size != PS3_DMA_16M) {
			pr_info("%s:%d: forcing 16M pages for linear map\n",
				__func__, __LINE__);
			r->page_size = PS3_DMA_16M;
			r->len = ALIGN(r->len, 1 << r->page_size);
		}
	}

	result = dma_sb_region_create(r);
	BUG_ON(result);

	if (r->offset < map.rm.size) {
		/* Map (part of) 1st RAM chunk */
		virt_addr = map.rm.base + r->offset;
		len = map.rm.size - r->offset;
		if (len > r->len)
			len = r->len;
		result = dma_sb_map_area(r, virt_addr, len, &tmp,
			CBE_IOPTE_PP_W | CBE_IOPTE_PP_R | CBE_IOPTE_SO_RW |
			CBE_IOPTE_M);
		BUG_ON(result);
	}

	if (r->offset + r->len > map.rm.size) {
		/* Map (part of) 2nd RAM chunk */
		virt_addr = map.rm.size;
		len = r->len;
		if (r->offset >= map.rm.size)
			virt_addr += r->offset - map.rm.size;
		else
			len -= map.rm.size - r->offset;
		result = dma_sb_map_area(r, virt_addr, len, &tmp,
			CBE_IOPTE_PP_W | CBE_IOPTE_PP_R | CBE_IOPTE_SO_RW |
			CBE_IOPTE_M);
		BUG_ON(result);
	}

	return result;
}

/**
 * dma_sb_region_free_linear - Free a linear dma mapping for a device.
 * @r: Pointer to a struct ps3_dma_region.
 *
 * This routine will unmap all mapped areas and free the HV dma region.
 */

static int dma_sb_region_free_linear(struct ps3_dma_region *r)
{
	int result;
	dma_addr_t bus_addr;
	unsigned long len, lpar_addr;

	if (r->offset < map.rm.size) {
		/* Unmap (part of) 1st RAM chunk */
		lpar_addr = map.rm.base + r->offset;
		len = map.rm.size - r->offset;
		if (len > r->len)
			len = r->len;
		bus_addr = dma_sb_lpar_to_bus(r, lpar_addr);
		result = dma_sb_unmap_area(r, bus_addr, len);
		BUG_ON(result);
	}

	if (r->offset + r->len > map.rm.size) {
		/* Unmap (part of) 2nd RAM chunk */
		lpar_addr = map.r1.base;
		len = r->len;
		if (r->offset >= map.rm.size)
			lpar_addr += r->offset - map.rm.size;
		else
			len -= map.rm.size - r->offset;
		bus_addr = dma_sb_lpar_to_bus(r, lpar_addr);
		result = dma_sb_unmap_area(r, bus_addr, len);
		BUG_ON(result);
	}

	result = dma_sb_region_free(r);
	BUG_ON(result);

	return result;
}

/**
 * dma_sb_map_area_linear - Map an area of memory into a device dma region.
 * @r: Pointer to a struct ps3_dma_region.
 * @virt_addr: Starting virtual address of the area to map.
 * @len: Length in bytes of the area to map.
 * @bus_addr: A pointer to return the starting ioc bus address of the area to
 * map.
 *
 * This routine just returns the corresponding bus address.  Actual mapping
 * occurs in dma_region_create_linear().
 */

static int dma_sb_map_area_linear(struct ps3_dma_region *r,
	unsigned long virt_addr, unsigned long len, dma_addr_t *bus_addr,
	u64 iopte_flag)
{
	unsigned long phys_addr = is_kernel_addr(virt_addr) ? __pa(virt_addr)
		: virt_addr;
	*bus_addr = dma_sb_lpar_to_bus(r, ps3_mm_phys_to_lpar(phys_addr));
	return 0;
}

/**
 * dma_unmap_area_linear - Unmap an area of memory from a device dma region.
 * @r: Pointer to a struct ps3_dma_region.
 * @bus_addr: The starting ioc bus address of the area to unmap.
 * @len: Length in bytes of the area to unmap.
 *
 * This routine does nothing.  Unmapping occurs in dma_sb_region_free_linear().
 */

static int dma_sb_unmap_area_linear(struct ps3_dma_region *r,
	dma_addr_t bus_addr, unsigned long len)
{
	return 0;
};

static const struct ps3_dma_region_ops ps3_dma_sb_region_ops =  {
	.create = dma_sb_region_create,
	.free = dma_sb_region_free,
	.map = dma_sb_map_area,
	.unmap = dma_sb_unmap_area
};

static const struct ps3_dma_region_ops ps3_dma_sb_region_linear_ops = {
	.create = dma_sb_region_create_linear,
	.free = dma_sb_region_free_linear,
	.map = dma_sb_map_area_linear,
	.unmap = dma_sb_unmap_area_linear
};

static const struct ps3_dma_region_ops ps3_dma_ioc0_region_ops = {
	.create = dma_ioc0_region_create,
	.free = dma_ioc0_region_free,
	.map = dma_ioc0_map_area,
	.unmap = dma_ioc0_unmap_area
};

int ps3_dma_region_init(struct ps3_system_bus_device *dev,
	struct ps3_dma_region *r, enum ps3_dma_page_size page_size,
	enum ps3_dma_region_type region_type, void *addr, unsigned long len)
{
	unsigned long lpar_addr;
	int result;

	lpar_addr = addr ? ps3_mm_phys_to_lpar(__pa(addr)) : 0;

	r->dev = dev;
	r->page_size = page_size;
	r->region_type = region_type;
	r->offset = lpar_addr;
	if (r->offset >= map.rm.size)
		r->offset -= map.r1.offset;
	r->len = len ? len : ALIGN(map.total, 1 << r->page_size);

	dev->core.dma_mask = &r->dma_mask;

	result = dma_set_mask_and_coherent(&dev->core, DMA_BIT_MASK(32));

	if (result < 0) {
		dev_err(&dev->core, "%s:%d: dma_set_mask_and_coherent failed: %d\n",
			__func__, __LINE__, result);
		return result;
	}

	switch (dev->dev_type) {
	case PS3_DEVICE_TYPE_SB:
		r->region_ops =  (USE_DYNAMIC_DMA)
			? &ps3_dma_sb_region_ops
			: &ps3_dma_sb_region_linear_ops;
		break;
	case PS3_DEVICE_TYPE_IOC0:
		r->region_ops = &ps3_dma_ioc0_region_ops;
		break;
	default:
		BUG();
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(ps3_dma_region_init);

int ps3_dma_region_create(struct ps3_dma_region *r)
{
	BUG_ON(!r);
	BUG_ON(!r->region_ops);
	BUG_ON(!r->region_ops->create);
	return r->region_ops->create(r);
}
EXPORT_SYMBOL(ps3_dma_region_create);

int ps3_dma_region_free(struct ps3_dma_region *r)
{
	BUG_ON(!r);
	BUG_ON(!r->region_ops);
	BUG_ON(!r->region_ops->free);
	return r->region_ops->free(r);
}
EXPORT_SYMBOL(ps3_dma_region_free);

int ps3_dma_map(struct ps3_dma_region *r, unsigned long virt_addr,
	unsigned long len, dma_addr_t *bus_addr,
	u64 iopte_flag)
{
	return r->region_ops->map(r, virt_addr, len, bus_addr, iopte_flag);
}

int ps3_dma_unmap(struct ps3_dma_region *r, dma_addr_t bus_addr,
	unsigned long len)
{
	return r->region_ops->unmap(r, bus_addr, len);
}

/*============================================================================*/
/* system startup routines                                                    */
/*============================================================================*/

/**
 * ps3_mm_init - initialize the address space state variables
 */

void __init ps3_mm_init(void)
{
	int result;

	DBG(" -> %s:%d\n", __func__, __LINE__);

	result = ps3_repository_read_mm_info(&map.rm.base, &map.rm.size,
		&map.total);

	if (result)
		panic("ps3_repository_read_mm_info() failed");

	map.rm.offset = map.rm.base;
	map.vas_id = map.htab_size = 0;

	/* this implementation assumes map.rm.base is zero */

	BUG_ON(map.rm.base);
	BUG_ON(!map.rm.size);

	/* Check if we got the highmem region from an earlier boot step */

	if (ps3_mm_get_repository_highmem(&map.r1)) {
		result = ps3_mm_region_create(&map.r1, map.total - map.rm.size);

		if (!result)
			ps3_mm_set_repository_highmem(&map.r1);
	}

	/* correct map.total for the real total amount of memory we use */
	map.total = map.rm.size + map.r1.size;

	if (!map.r1.size) {
		DBG("%s:%d: No highmem region found\n", __func__, __LINE__);
	} else {
		DBG("%s:%d: Adding highmem region: %llxh %llxh\n",
			__func__, __LINE__, map.rm.size,
			map.total - map.rm.size);
		memblock_add(map.rm.size, map.total - map.rm.size);
	}

	DBG(" <- %s:%d\n", __func__, __LINE__);
}

/**
 * ps3_mm_shutdown - final cleanup of address space
 *
 * called during kexec sequence with MMU off.
 */

notrace void ps3_mm_shutdown(void)
{
	ps3_mm_region_destroy(&map.r1);
}
