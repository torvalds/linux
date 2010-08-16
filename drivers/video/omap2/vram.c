/*
 * VRAM manager for OMAP
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*#define DEBUG*/

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/memblock.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/jiffies.h>
#include <linux/module.h>

#include <asm/setup.h>

#include <plat/sram.h>
#include <plat/vram.h>
#include <plat/dma.h>

#ifdef DEBUG
#define DBG(format, ...) pr_debug("VRAM: " format, ## __VA_ARGS__)
#else
#define DBG(format, ...)
#endif

#define OMAP2_SRAM_START		0x40200000
/* Maximum size, in reality this is smaller if SRAM is partially locked. */
#define OMAP2_SRAM_SIZE			0xa0000		/* 640k */

/* postponed regions are used to temporarily store region information at boot
 * time when we cannot yet allocate the region list */
#define MAX_POSTPONED_REGIONS 10

static bool vram_initialized;
static int postponed_cnt;
static struct {
	unsigned long paddr;
	size_t size;
} postponed_regions[MAX_POSTPONED_REGIONS];

struct vram_alloc {
	struct list_head list;
	unsigned long paddr;
	unsigned pages;
};

struct vram_region {
	struct list_head list;
	struct list_head alloc_list;
	unsigned long paddr;
	unsigned pages;
};

static DEFINE_MUTEX(region_mutex);
static LIST_HEAD(region_list);

static inline int region_mem_type(unsigned long paddr)
{
	if (paddr >= OMAP2_SRAM_START &&
	    paddr < OMAP2_SRAM_START + OMAP2_SRAM_SIZE)
		return OMAP_VRAM_MEMTYPE_SRAM;
	else
		return OMAP_VRAM_MEMTYPE_SDRAM;
}

static struct vram_region *omap_vram_create_region(unsigned long paddr,
		unsigned pages)
{
	struct vram_region *rm;

	rm = kzalloc(sizeof(*rm), GFP_KERNEL);

	if (rm) {
		INIT_LIST_HEAD(&rm->alloc_list);
		rm->paddr = paddr;
		rm->pages = pages;
	}

	return rm;
}

#if 0
static void omap_vram_free_region(struct vram_region *vr)
{
	list_del(&vr->list);
	kfree(vr);
}
#endif

static struct vram_alloc *omap_vram_create_allocation(struct vram_region *vr,
		unsigned long paddr, unsigned pages)
{
	struct vram_alloc *va;
	struct vram_alloc *new;

	new = kzalloc(sizeof(*va), GFP_KERNEL);

	if (!new)
		return NULL;

	new->paddr = paddr;
	new->pages = pages;

	list_for_each_entry(va, &vr->alloc_list, list) {
		if (va->paddr > new->paddr)
			break;
	}

	list_add_tail(&new->list, &va->list);

	return new;
}

static void omap_vram_free_allocation(struct vram_alloc *va)
{
	list_del(&va->list);
	kfree(va);
}

int omap_vram_add_region(unsigned long paddr, size_t size)
{
	struct vram_region *rm;
	unsigned pages;

	if (vram_initialized) {
		DBG("adding region paddr %08lx size %d\n",
				paddr, size);

		size &= PAGE_MASK;
		pages = size >> PAGE_SHIFT;

		rm = omap_vram_create_region(paddr, pages);
		if (rm == NULL)
			return -ENOMEM;

		list_add(&rm->list, &region_list);
	} else {
		if (postponed_cnt == MAX_POSTPONED_REGIONS)
			return -ENOMEM;

		postponed_regions[postponed_cnt].paddr = paddr;
		postponed_regions[postponed_cnt].size = size;

		++postponed_cnt;
	}
	return 0;
}

int omap_vram_free(unsigned long paddr, size_t size)
{
	struct vram_region *rm;
	struct vram_alloc *alloc;
	unsigned start, end;

	DBG("free mem paddr %08lx size %d\n", paddr, size);

	size = PAGE_ALIGN(size);

	mutex_lock(&region_mutex);

	list_for_each_entry(rm, &region_list, list) {
		list_for_each_entry(alloc, &rm->alloc_list, list) {
			start = alloc->paddr;
			end = alloc->paddr + (alloc->pages >> PAGE_SHIFT);

			if (start >= paddr && end < paddr + size)
				goto found;
		}
	}

	mutex_unlock(&region_mutex);
	return -EINVAL;

found:
	omap_vram_free_allocation(alloc);

	mutex_unlock(&region_mutex);
	return 0;
}
EXPORT_SYMBOL(omap_vram_free);

static int _omap_vram_reserve(unsigned long paddr, unsigned pages)
{
	struct vram_region *rm;
	struct vram_alloc *alloc;
	size_t size;

	size = pages << PAGE_SHIFT;

	list_for_each_entry(rm, &region_list, list) {
		unsigned long start, end;

		DBG("checking region %lx %d\n", rm->paddr, rm->pages);

		if (region_mem_type(rm->paddr) != region_mem_type(paddr))
			continue;

		start = rm->paddr;
		end = start + (rm->pages << PAGE_SHIFT) - 1;
		if (start > paddr || end < paddr + size - 1)
			continue;

		DBG("block ok, checking allocs\n");

		list_for_each_entry(alloc, &rm->alloc_list, list) {
			end = alloc->paddr - 1;

			if (start <= paddr && end >= paddr + size - 1)
				goto found;

			start = alloc->paddr + (alloc->pages << PAGE_SHIFT);
		}

		end = rm->paddr + (rm->pages << PAGE_SHIFT) - 1;

		if (!(start <= paddr && end >= paddr + size - 1))
			continue;
found:
		DBG("found area start %lx, end %lx\n", start, end);

		if (omap_vram_create_allocation(rm, paddr, pages) == NULL)
			return -ENOMEM;

		return 0;
	}

	return -ENOMEM;
}

int omap_vram_reserve(unsigned long paddr, size_t size)
{
	unsigned pages;
	int r;

	DBG("reserve mem paddr %08lx size %d\n", paddr, size);

	size = PAGE_ALIGN(size);
	pages = size >> PAGE_SHIFT;

	mutex_lock(&region_mutex);

	r = _omap_vram_reserve(paddr, pages);

	mutex_unlock(&region_mutex);

	return r;
}
EXPORT_SYMBOL(omap_vram_reserve);

static void _omap_vram_dma_cb(int lch, u16 ch_status, void *data)
{
	struct completion *compl = data;
	complete(compl);
}

static int _omap_vram_clear(u32 paddr, unsigned pages)
{
	struct completion compl;
	unsigned elem_count;
	unsigned frame_count;
	int r;
	int lch;

	init_completion(&compl);

	r = omap_request_dma(OMAP_DMA_NO_DEVICE, "VRAM DMA",
			_omap_vram_dma_cb,
			&compl, &lch);
	if (r) {
		pr_err("VRAM: request_dma failed for memory clear\n");
		return -EBUSY;
	}

	elem_count = pages * PAGE_SIZE / 4;
	frame_count = 1;

	omap_set_dma_transfer_params(lch, OMAP_DMA_DATA_TYPE_S32,
			elem_count, frame_count,
			OMAP_DMA_SYNC_ELEMENT,
			0, 0);

	omap_set_dma_dest_params(lch, 0, OMAP_DMA_AMODE_POST_INC,
			paddr, 0, 0);

	omap_set_dma_color_mode(lch, OMAP_DMA_CONSTANT_FILL, 0x000000);

	omap_start_dma(lch);

	if (wait_for_completion_timeout(&compl, msecs_to_jiffies(1000)) == 0) {
		omap_stop_dma(lch);
		pr_err("VRAM: dma timeout while clearing memory\n");
		r = -EIO;
		goto err;
	}

	r = 0;
err:
	omap_free_dma(lch);

	return r;
}

static int _omap_vram_alloc(int mtype, unsigned pages, unsigned long *paddr)
{
	struct vram_region *rm;
	struct vram_alloc *alloc;

	list_for_each_entry(rm, &region_list, list) {
		unsigned long start, end;

		DBG("checking region %lx %d\n", rm->paddr, rm->pages);

		if (region_mem_type(rm->paddr) != mtype)
			continue;

		start = rm->paddr;

		list_for_each_entry(alloc, &rm->alloc_list, list) {
			end = alloc->paddr;

			if (end - start >= pages << PAGE_SHIFT)
				goto found;

			start = alloc->paddr + (alloc->pages << PAGE_SHIFT);
		}

		end = rm->paddr + (rm->pages << PAGE_SHIFT);
found:
		if (end - start < pages << PAGE_SHIFT)
			continue;

		DBG("found %lx, end %lx\n", start, end);

		alloc = omap_vram_create_allocation(rm, start, pages);
		if (alloc == NULL)
			return -ENOMEM;

		*paddr = start;

		_omap_vram_clear(start, pages);

		return 0;
	}

	return -ENOMEM;
}

int omap_vram_alloc(int mtype, size_t size, unsigned long *paddr)
{
	unsigned pages;
	int r;

	BUG_ON(mtype > OMAP_VRAM_MEMTYPE_MAX || !size);

	DBG("alloc mem type %d size %d\n", mtype, size);

	size = PAGE_ALIGN(size);
	pages = size >> PAGE_SHIFT;

	mutex_lock(&region_mutex);

	r = _omap_vram_alloc(mtype, pages, paddr);

	mutex_unlock(&region_mutex);

	return r;
}
EXPORT_SYMBOL(omap_vram_alloc);

void omap_vram_get_info(unsigned long *vram,
		unsigned long *free_vram,
		unsigned long *largest_free_block)
{
	struct vram_region *vr;
	struct vram_alloc *va;

	*vram = 0;
	*free_vram = 0;
	*largest_free_block = 0;

	mutex_lock(&region_mutex);

	list_for_each_entry(vr, &region_list, list) {
		unsigned free;
		unsigned long pa;

		pa = vr->paddr;
		*vram += vr->pages << PAGE_SHIFT;

		list_for_each_entry(va, &vr->alloc_list, list) {
			free = va->paddr - pa;
			*free_vram += free;
			if (free > *largest_free_block)
				*largest_free_block = free;
			pa = va->paddr + (va->pages << PAGE_SHIFT);
		}

		free = vr->paddr + (vr->pages << PAGE_SHIFT) - pa;
		*free_vram += free;
		if (free > *largest_free_block)
			*largest_free_block = free;
	}

	mutex_unlock(&region_mutex);
}
EXPORT_SYMBOL(omap_vram_get_info);

#if defined(CONFIG_DEBUG_FS)
static int vram_debug_show(struct seq_file *s, void *unused)
{
	struct vram_region *vr;
	struct vram_alloc *va;
	unsigned size;

	mutex_lock(&region_mutex);

	list_for_each_entry(vr, &region_list, list) {
		size = vr->pages << PAGE_SHIFT;
		seq_printf(s, "%08lx-%08lx (%d bytes)\n",
				vr->paddr, vr->paddr + size - 1,
				size);

		list_for_each_entry(va, &vr->alloc_list, list) {
			size = va->pages << PAGE_SHIFT;
			seq_printf(s, "    %08lx-%08lx (%d bytes)\n",
					va->paddr, va->paddr + size - 1,
					size);
		}
	}

	mutex_unlock(&region_mutex);

	return 0;
}

static int vram_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, vram_debug_show, inode->i_private);
}

static const struct file_operations vram_debug_fops = {
	.open           = vram_debug_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int __init omap_vram_create_debugfs(void)
{
	struct dentry *d;

	d = debugfs_create_file("vram", S_IRUGO, NULL,
			NULL, &vram_debug_fops);
	if (IS_ERR(d))
		return PTR_ERR(d);

	return 0;
}
#endif

static __init int omap_vram_init(void)
{
	int i;

	vram_initialized = 1;

	for (i = 0; i < postponed_cnt; i++)
		omap_vram_add_region(postponed_regions[i].paddr,
				postponed_regions[i].size);

#ifdef CONFIG_DEBUG_FS
	if (omap_vram_create_debugfs())
		pr_err("VRAM: Failed to create debugfs file\n");
#endif

	return 0;
}

arch_initcall(omap_vram_init);

/* boottime vram alloc stuff */

/* set from board file */
static u32 omap_vram_sram_start __initdata;
static u32 omap_vram_sram_size __initdata;

/* set from board file */
static u32 omap_vram_sdram_start __initdata;
static u32 omap_vram_sdram_size __initdata;

/* set from kernel cmdline */
static u32 omap_vram_def_sdram_size __initdata;
static u32 omap_vram_def_sdram_start __initdata;

static int __init omap_vram_early_vram(char *p)
{
	omap_vram_def_sdram_size = memparse(p, &p);
	if (*p == ',')
		omap_vram_def_sdram_start = simple_strtoul(p + 1, &p, 16);
	return 0;
}
early_param("vram", omap_vram_early_vram);

/*
 * Called from map_io. We need to call to this early enough so that we
 * can reserve the fixed SDRAM regions before VM could get hold of them.
 */
void __init omap_vram_reserve_sdram_memblock(void)
{
	u32 paddr;
	u32 size = 0;

	/* cmdline arg overrides the board file definition */
	if (omap_vram_def_sdram_size) {
		size = omap_vram_def_sdram_size;
		paddr = omap_vram_def_sdram_start;
	}

	if (!size) {
		size = omap_vram_sdram_size;
		paddr = omap_vram_sdram_start;
	}

#ifdef CONFIG_OMAP2_VRAM_SIZE
	if (!size) {
		size = CONFIG_OMAP2_VRAM_SIZE * 1024 * 1024;
		paddr = 0;
	}
#endif

	if (!size)
		return;

	size = PAGE_ALIGN(size);

	if (paddr) {
		struct memblock_property res;

		res.base = paddr;
		res.size = size;
		if ((paddr & ~PAGE_MASK) || memblock_find(&res) ||
		    res.base != paddr || res.size != size) {
			pr_err("Illegal SDRAM region for VRAM\n");
			return;
		}

		if (memblock_is_region_reserved(paddr, size)) {
			pr_err("FB: failed to reserve VRAM - busy\n");
			return;
		}

		if (memblock_reserve(paddr, size) < 0) {
			pr_err("FB: failed to reserve VRAM - no memory\n");
			return;
		}
	} else {
		paddr = memblock_alloc_base(size, PAGE_SIZE, MEMBLOCK_REAL_LIMIT);
	}

	omap_vram_add_region(paddr, size);

	pr_info("Reserving %u bytes SDRAM for VRAM\n", size);
}

/*
 * Called at sram init time, before anything is pushed to the SRAM stack.
 * Because of the stack scheme, we will allocate everything from the
 * start of the lowest address region to the end of SRAM. This will also
 * include padding for page alignment and possible holes between regions.
 *
 * As opposed to the SDRAM case, we'll also do any dynamic allocations at
 * this point, since the driver built as a module would have problem with
 * freeing / reallocating the regions.
 */
unsigned long __init omap_vram_reserve_sram(unsigned long sram_pstart,
				  unsigned long sram_vstart,
				  unsigned long sram_size,
				  unsigned long pstart_avail,
				  unsigned long size_avail)
{
	unsigned long			pend_avail;
	unsigned long			reserved;
	u32 paddr;
	u32 size;

	paddr = omap_vram_sram_start;
	size = omap_vram_sram_size;

	if (!size)
		return 0;

	reserved = 0;
	pend_avail = pstart_avail + size_avail;

	if (!paddr) {
		/* Dynamic allocation */
		if ((size_avail & PAGE_MASK) < size) {
			pr_err("Not enough SRAM for VRAM\n");
			return 0;
		}
		size_avail = (size_avail - size) & PAGE_MASK;
		paddr = pstart_avail + size_avail;
	}

	if (paddr < sram_pstart ||
			paddr + size > sram_pstart + sram_size) {
		pr_err("Illegal SRAM region for VRAM\n");
		return 0;
	}

	/* Reserve everything above the start of the region. */
	if (pend_avail - paddr > reserved)
		reserved = pend_avail - paddr;
	size_avail = pend_avail - reserved - pstart_avail;

	omap_vram_add_region(paddr, size);

	if (reserved)
		pr_info("Reserving %lu bytes SRAM for VRAM\n", reserved);

	return reserved;
}

void __init omap_vram_set_sdram_vram(u32 size, u32 start)
{
	omap_vram_sdram_start = start;
	omap_vram_sdram_size = size;
}

void __init omap_vram_set_sram_vram(u32 size, u32 start)
{
	omap_vram_sram_start = start;
	omap_vram_sram_size = size;
}
