/*
 *  PS3 address space management.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/memory_hotplug.h>

#include <asm/lmb.h>
#include <asm/udbg.h>
#include <asm/ps3.h>
#include <asm/lv1call.h>

#include "platform.h"

#if defined(DEBUG)
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...) do{if(0)printk(fmt);}while(0)
#endif

enum {
#if defined(CONFIG_PS3_USE_LPAR_ADDR)
	USE_LPAR_ADDR = 1,
#else
	USE_LPAR_ADDR = 0,
#endif
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

static unsigned long make_page_sizes(unsigned long a, unsigned long b)
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
 */

struct mem_region {
	unsigned long base;
	unsigned long size;
	unsigned long offset;
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
 * @r1: hotplug memory region(s)
 *
 * ps3 addresses
 * virt_addr: a cpu 'translated' effective address
 * phys_addr: an address in what Linux thinks is the physical address space
 * lpar_addr: an address in the HV virtual address space
 * bus_addr: an io controller 'translated' address on a device bus
 */

struct map {
	unsigned long total;
	unsigned long vas_id;
	unsigned long htab_size;
	struct mem_region rm;
	struct mem_region r1;
};

#define debug_dump_map(x) _debug_dump_map(x, __func__, __LINE__)
static void _debug_dump_map(const struct map* m, const char* func, int line)
{
	DBG("%s:%d: map.total     = %lxh\n", func, line, m->total);
	DBG("%s:%d: map.rm.size   = %lxh\n", func, line, m->rm.size);
	DBG("%s:%d: map.vas_id    = %lu\n", func, line, m->vas_id);
	DBG("%s:%d: map.htab_size = %lxh\n", func, line, m->htab_size);
	DBG("%s:%d: map.r1.base   = %lxh\n", func, line, m->r1.base);
	DBG("%s:%d: map.r1.offset = %lxh\n", func, line, m->r1.offset);
	DBG("%s:%d: map.r1.size   = %lxh\n", func, line, m->r1.size);
}

static struct map map;

/**
 * ps3_mm_phys_to_lpar - translate a linux physical address to lpar address
 * @phys_addr: linux physical address
 */

unsigned long ps3_mm_phys_to_lpar(unsigned long phys_addr)
{
	BUG_ON(is_kernel_addr(phys_addr));
	if (USE_LPAR_ADDR)
		return phys_addr;
	else
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
	unsigned long start_address;
	unsigned long size;
	unsigned long access_right;
	unsigned long max_page_size;
	unsigned long flags;

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
		DBG("%s:%d: bad max_page_size %lxh\n", __func__, __LINE__,
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
 */

void ps3_mm_vas_destroy(void)
{
	if (map.vas_id) {
		lv1_select_virtual_address_space(0);
		lv1_destruct_virtual_address_space(map.vas_id);
		map.vas_id = 0;
	}
}

/*============================================================================*/
/* memory hotplug routines                                                    */
/*============================================================================*/

/**
 * ps3_mm_region_create - create a memory region in the vas
 * @r: pointer to a struct mem_region to accept initialized values
 * @size: requested region size
 *
 * This implementation creates the region with the vas large page size.
 * @size is rounded down to a multiple of the vas large page size.
 */

int ps3_mm_region_create(struct mem_region *r, unsigned long size)
{
	int result;
	unsigned long muid;

	r->size = _ALIGN_DOWN(size, 1 << PAGE_SHIFT_16M);

	DBG("%s:%d requested  %lxh\n", __func__, __LINE__, size);
	DBG("%s:%d actual     %lxh\n", __func__, __LINE__, r->size);
	DBG("%s:%d difference %lxh (%luMB)\n", __func__, __LINE__,
		(unsigned long)(size - r->size),
		(size - r->size) / 1024 / 1024);

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

void ps3_mm_region_destroy(struct mem_region *r)
{
	if (r->base) {
		lv1_release_memory(r->base);
		r->size = r->base = r->offset = 0;
		map.total = map.rm.size;
	}
}

/**
 * ps3_mm_add_memory - hot add memory
 */

static int __init ps3_mm_add_memory(void)
{
	int result;
	unsigned long start_addr;
	unsigned long start_pfn;
	unsigned long nr_pages;

	BUG_ON(!mem_init_done);

	start_addr = USE_LPAR_ADDR ? map.r1.base : map.rm.size;
	start_pfn = start_addr >> PAGE_SHIFT;
	nr_pages = (map.r1.size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	DBG("%s:%d: start_addr %lxh, start_pfn %lxh, nr_pages %lxh\n",
		__func__, __LINE__, start_addr, start_pfn, nr_pages);

	result = add_memory(0, start_addr, map.r1.size);

	if (result) {
		DBG("%s:%d: add_memory failed: (%d)\n",
			__func__, __LINE__, result);
		return result;
	}

	result = online_pages(start_pfn, nr_pages);

	if (result)
		DBG("%s:%d: online_pages failed: (%d)\n",
			__func__, __LINE__, result);

	return result;
}

core_initcall(ps3_mm_add_memory);

/*============================================================================*/
/* dma routines                                                               */
/*============================================================================*/

/**
 * dma_lpar_to_bus - Translate an lpar address to ioc mapped bus address.
 * @r: pointer to dma region structure
 * @lpar_addr: HV lpar address
 */

static unsigned long dma_lpar_to_bus(struct ps3_dma_region *r,
	unsigned long lpar_addr)
{
	BUG_ON(lpar_addr >= map.r1.base + map.r1.size);
	return r->bus_addr + (lpar_addr <= map.rm.size ? lpar_addr
		: lpar_addr - map.r1.offset);
}

#define dma_dump_region(_a) _dma_dump_region(_a, __func__, __LINE__)
static void _dma_dump_region(const struct ps3_dma_region *r, const char* func,
	int line)
{
	DBG("%s:%d: dev        %u:%u\n", func, line, r->did.bus_id,
		r->did.dev_id);
	DBG("%s:%d: page_size  %u\n", func, line, r->page_size);
	DBG("%s:%d: bus_addr   %lxh\n", func, line, r->bus_addr);
	DBG("%s:%d: len        %lxh\n", func, line, r->len);
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
	DBG("%s:%d: r.dev        %u:%u\n", func, line,
		c->region->did.bus_id, c->region->did.dev_id);
	DBG("%s:%d: r.bus_addr   %lxh\n", func, line, c->region->bus_addr);
	DBG("%s:%d: r.page_size  %u\n", func, line, c->region->page_size);
	DBG("%s:%d: r.len        %lxh\n", func, line, c->region->len);
	DBG("%s:%d: c.lpar_addr  %lxh\n", func, line, c->lpar_addr);
	DBG("%s:%d: c.bus_addr   %lxh\n", func, line, c->bus_addr);
	DBG("%s:%d: c.len        %lxh\n", func, line, c->len);
}

static struct dma_chunk * dma_find_chunk(struct ps3_dma_region *r,
	unsigned long bus_addr, unsigned long len)
{
	struct dma_chunk *c;
	unsigned long aligned_bus = _ALIGN_DOWN(bus_addr, 1 << r->page_size);
	unsigned long aligned_len = _ALIGN_UP(len, 1 << r->page_size);

	list_for_each_entry(c, &r->chunk_list.head, link) {
		/* intersection */
		if (aligned_bus >= c->bus_addr
			&& aligned_bus < c->bus_addr + c->len
			&& aligned_bus + aligned_len <= c->bus_addr + c->len) {
			return c;
		}
		/* below */
		if (aligned_bus + aligned_len <= c->bus_addr) {
			continue;
		}
		/* above */
		if (aligned_bus >= c->bus_addr + c->len) {
			continue;
		}

		/* we don't handle the multi-chunk case for now */

		dma_dump_chunk(c);
		BUG();
	}
	return NULL;
}

static int dma_free_chunk(struct dma_chunk *c)
{
	int result = 0;

	if (c->bus_addr) {
		result = lv1_unmap_device_dma_region(c->region->did.bus_id,
			c->region->did.dev_id, c->bus_addr, c->len);
		BUG_ON(result);
	}

	kfree(c);
	return result;
}

/**
 * dma_map_pages - Maps dma pages into the io controller bus address space.
 * @r: Pointer to a struct ps3_dma_region.
 * @phys_addr: Starting physical address of the area to map.
 * @len: Length in bytes of the area to map.
 * c_out: A pointer to receive an allocated struct dma_chunk for this area.
 *
 * This is the lowest level dma mapping routine, and is the one that will
 * make the HV call to add the pages into the io controller address space.
 */

static int dma_map_pages(struct ps3_dma_region *r, unsigned long phys_addr,
	unsigned long len, struct dma_chunk **c_out)
{
	int result;
	struct dma_chunk *c;

	c = kzalloc(sizeof(struct dma_chunk), GFP_ATOMIC);

	if (!c) {
		result = -ENOMEM;
		goto fail_alloc;
	}

	c->region = r;
	c->lpar_addr = ps3_mm_phys_to_lpar(phys_addr);
	c->bus_addr = dma_lpar_to_bus(r, c->lpar_addr);
	c->len = len;

	result = lv1_map_device_dma_region(c->region->did.bus_id,
		c->region->did.dev_id, c->lpar_addr, c->bus_addr, c->len,
		0xf800000000000000UL);

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

/**
 * dma_region_create - Create a device dma region.
 * @r: Pointer to a struct ps3_dma_region.
 *
 * This is the lowest level dma region create routine, and is the one that
 * will make the HV call to create the region.
 */

static int dma_region_create(struct ps3_dma_region* r)
{
	int result;

	r->len = _ALIGN_UP(map.total, 1 << r->page_size);
	INIT_LIST_HEAD(&r->chunk_list.head);
	spin_lock_init(&r->chunk_list.lock);

	result = lv1_allocate_device_dma_region(r->did.bus_id, r->did.dev_id,
		r->len, r->page_size, r->region_type, &r->bus_addr);

	dma_dump_region(r);

	if (result) {
		DBG("%s:%d: lv1_allocate_device_dma_region failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		r->len = r->bus_addr = 0;
	}

	return result;
}

/**
 * dma_region_free - Free a device dma region.
 * @r: Pointer to a struct ps3_dma_region.
 *
 * This is the lowest level dma region free routine, and is the one that
 * will make the HV call to free the region.
 */

static int dma_region_free(struct ps3_dma_region* r)
{
	int result;
	struct dma_chunk *c;
	struct dma_chunk *tmp;

	list_for_each_entry_safe(c, tmp, &r->chunk_list.head, link) {
		list_del(&c->link);
		dma_free_chunk(c);
	}

	result = lv1_free_device_dma_region(r->did.bus_id, r->did.dev_id,
		r->bus_addr);

	if (result)
		DBG("%s:%d: lv1_free_device_dma_region failed: %s\n",
			__func__, __LINE__, ps3_result(result));

	r->len = r->bus_addr = 0;

	return result;
}

/**
 * dma_map_area - Map an area of memory into a device dma region.
 * @r: Pointer to a struct ps3_dma_region.
 * @virt_addr: Starting virtual address of the area to map.
 * @len: Length in bytes of the area to map.
 * @bus_addr: A pointer to return the starting ioc bus address of the area to
 * map.
 *
 * This is the common dma mapping routine.
 */

static int dma_map_area(struct ps3_dma_region *r, unsigned long virt_addr,
	unsigned long len, unsigned long *bus_addr)
{
	int result;
	unsigned long flags;
	struct dma_chunk *c;
	unsigned long phys_addr = is_kernel_addr(virt_addr) ? __pa(virt_addr)
		: virt_addr;

	*bus_addr = dma_lpar_to_bus(r, ps3_mm_phys_to_lpar(phys_addr));

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
		DBG("%s:%d bus_addr  %lxh (%lxh)\n", __func__, __LINE__,
		*bus_addr, len);
	}

	spin_lock_irqsave(&r->chunk_list.lock, flags);
	c = dma_find_chunk(r, *bus_addr, len);

	if (c) {
		c->usage_count++;
		spin_unlock_irqrestore(&r->chunk_list.lock, flags);
		return 0;
	}

	result = dma_map_pages(r, _ALIGN_DOWN(phys_addr, 1 << r->page_size),
		_ALIGN_UP(len, 1 << r->page_size), &c);

	if (result) {
		*bus_addr = 0;
		DBG("%s:%d: dma_map_pages failed (%d)\n",
			__func__, __LINE__, result);
		spin_unlock_irqrestore(&r->chunk_list.lock, flags);
		return result;
	}

	c->usage_count = 1;

	spin_unlock_irqrestore(&r->chunk_list.lock, flags);
	return result;
}

/**
 * dma_unmap_area - Unmap an area of memory from a device dma region.
 * @r: Pointer to a struct ps3_dma_region.
 * @bus_addr: The starting ioc bus address of the area to unmap.
 * @len: Length in bytes of the area to unmap.
 *
 * This is the common dma unmap routine.
 */

int dma_unmap_area(struct ps3_dma_region *r, unsigned long bus_addr,
	unsigned long len)
{
	unsigned long flags;
	struct dma_chunk *c;

	spin_lock_irqsave(&r->chunk_list.lock, flags);
	c = dma_find_chunk(r, bus_addr, len);

	if (!c) {
		unsigned long aligned_bus = _ALIGN_DOWN(bus_addr,
			1 << r->page_size);
		unsigned long aligned_len = _ALIGN_UP(len, 1 << r->page_size);
		DBG("%s:%d: not found: bus_addr %lxh\n",
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
		dma_free_chunk(c);
	}

	spin_unlock_irqrestore(&r->chunk_list.lock, flags);
	return 0;
}

/**
 * dma_region_create_linear - Setup a linear dma maping for a device.
 * @r: Pointer to a struct ps3_dma_region.
 *
 * This routine creates an HV dma region for the device and maps all available
 * ram into the io controller bus address space.
 */

static int dma_region_create_linear(struct ps3_dma_region *r)
{
	int result;
	unsigned long tmp;

	/* force 16M dma pages for linear mapping */

	if (r->page_size != PS3_DMA_16M) {
		pr_info("%s:%d: forcing 16M pages for linear map\n",
			__func__, __LINE__);
		r->page_size = PS3_DMA_16M;
	}

	result = dma_region_create(r);
	BUG_ON(result);

	result = dma_map_area(r, map.rm.base, map.rm.size, &tmp);
	BUG_ON(result);

	if (USE_LPAR_ADDR)
		result = dma_map_area(r, map.r1.base, map.r1.size,
			&tmp);
	else
		result = dma_map_area(r, map.rm.size, map.r1.size,
			&tmp);

	BUG_ON(result);

	return result;
}

/**
 * dma_region_free_linear - Free a linear dma mapping for a device.
 * @r: Pointer to a struct ps3_dma_region.
 *
 * This routine will unmap all mapped areas and free the HV dma region.
 */

static int dma_region_free_linear(struct ps3_dma_region *r)
{
	int result;

	result = dma_unmap_area(r, dma_lpar_to_bus(r, 0), map.rm.size);
	BUG_ON(result);

	result = dma_unmap_area(r, dma_lpar_to_bus(r, map.r1.base),
		map.r1.size);
	BUG_ON(result);

	result = dma_region_free(r);
	BUG_ON(result);

	return result;
}

/**
 * dma_map_area_linear - Map an area of memory into a device dma region.
 * @r: Pointer to a struct ps3_dma_region.
 * @virt_addr: Starting virtual address of the area to map.
 * @len: Length in bytes of the area to map.
 * @bus_addr: A pointer to return the starting ioc bus address of the area to
 * map.
 *
 * This routine just returns the coresponding bus address.  Actual mapping
 * occurs in dma_region_create_linear().
 */

static int dma_map_area_linear(struct ps3_dma_region *r,
	unsigned long virt_addr, unsigned long len, unsigned long *bus_addr)
{
	unsigned long phys_addr = is_kernel_addr(virt_addr) ? __pa(virt_addr)
		: virt_addr;
	*bus_addr = dma_lpar_to_bus(r, ps3_mm_phys_to_lpar(phys_addr));
	return 0;
}

/**
 * dma_unmap_area_linear - Unmap an area of memory from a device dma region.
 * @r: Pointer to a struct ps3_dma_region.
 * @bus_addr: The starting ioc bus address of the area to unmap.
 * @len: Length in bytes of the area to unmap.
 *
 * This routine does nothing.  Unmapping occurs in dma_region_free_linear().
 */

static int dma_unmap_area_linear(struct ps3_dma_region *r,
	unsigned long bus_addr, unsigned long len)
{
	return 0;
}

int ps3_dma_region_create(struct ps3_dma_region *r)
{
	return (USE_DYNAMIC_DMA)
		? dma_region_create(r)
		: dma_region_create_linear(r);
}

int ps3_dma_region_free(struct ps3_dma_region *r)
{
	return (USE_DYNAMIC_DMA)
		? dma_region_free(r)
		: dma_region_free_linear(r);
}

int ps3_dma_map(struct ps3_dma_region *r, unsigned long virt_addr,
	unsigned long len, unsigned long *bus_addr)
{
	return (USE_DYNAMIC_DMA)
		? dma_map_area(r, virt_addr, len, bus_addr)
		: dma_map_area_linear(r, virt_addr, len, bus_addr);
}

int ps3_dma_unmap(struct ps3_dma_region *r, unsigned long bus_addr,
	unsigned long len)
{
	return (USE_DYNAMIC_DMA) ? dma_unmap_area(r, bus_addr, len)
		: dma_unmap_area_linear(r, bus_addr, len);
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

	lmb_add(map.rm.base, map.rm.size);
	lmb_analyze();

	/* arrange to do this in ps3_mm_add_memory */
	ps3_mm_region_create(&map.r1, map.total - map.rm.size);

	DBG(" <- %s:%d\n", __func__, __LINE__);
}

/**
 * ps3_mm_shutdown - final cleanup of address space
 */

void ps3_mm_shutdown(void)
{
	ps3_mm_region_destroy(&map.r1);
	map.total = map.rm.size;
}
