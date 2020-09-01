// SPDX-License-Identifier: GPL-2.0
/*
 * Mips Jazz DMA controller support
 * Copyright (C) 1995, 1996 by Andreas Busse
 *
 * NOTE: Some of the argument checking could be removed when
 * things have settled down. Also, instead of returning 0xffffffff
 * on failure of vdma_alloc() one could leave page #0 unused
 * and return the more usual NULL pointer as logical address.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/spinlock.h>
#include <linux/gfp.h>
#include <linux/dma-noncoherent.h>
#include <asm/mipsregs.h>
#include <asm/jazz.h>
#include <asm/io.h>
#include <linux/uaccess.h>
#include <asm/dma.h>
#include <asm/jazzdma.h>

/*
 * Set this to one to enable additional vdma debug code.
 */
#define CONF_DEBUG_VDMA 0

static VDMA_PGTBL_ENTRY *pgtbl;

static DEFINE_SPINLOCK(vdma_lock);

/*
 * Debug stuff
 */
#define vdma_debug     ((CONF_DEBUG_VDMA) ? debuglvl : 0)

static int debuglvl = 3;

/*
 * Initialize the pagetable with a one-to-one mapping of
 * the first 16 Mbytes of main memory and declare all
 * entries to be unused. Using this method will at least
 * allow some early device driver operations to work.
 */
static inline void vdma_pgtbl_init(void)
{
	unsigned long paddr = 0;
	int i;

	for (i = 0; i < VDMA_PGTBL_ENTRIES; i++) {
		pgtbl[i].frame = paddr;
		pgtbl[i].owner = VDMA_PAGE_EMPTY;
		paddr += VDMA_PAGESIZE;
	}
}

/*
 * Initialize the Jazz R4030 dma controller
 */
static int __init vdma_init(void)
{
	/*
	 * Allocate 32k of memory for DMA page tables.	This needs to be page
	 * aligned and should be uncached to avoid cache flushing after every
	 * update.
	 */
	pgtbl = (VDMA_PGTBL_ENTRY *)__get_free_pages(GFP_KERNEL | GFP_DMA,
						    get_order(VDMA_PGTBL_SIZE));
	BUG_ON(!pgtbl);
	dma_cache_wback_inv((unsigned long)pgtbl, VDMA_PGTBL_SIZE);
	pgtbl = (VDMA_PGTBL_ENTRY *)CKSEG1ADDR((unsigned long)pgtbl);

	/*
	 * Clear the R4030 translation table
	 */
	vdma_pgtbl_init();

	r4030_write_reg32(JAZZ_R4030_TRSTBL_BASE,
			  CPHYSADDR((unsigned long)pgtbl));
	r4030_write_reg32(JAZZ_R4030_TRSTBL_LIM, VDMA_PGTBL_SIZE);
	r4030_write_reg32(JAZZ_R4030_TRSTBL_INV, 0);

	printk(KERN_INFO "VDMA: R4030 DMA pagetables initialized.\n");
	return 0;
}
arch_initcall(vdma_init);

/*
 * Allocate DMA pagetables using a simple first-fit algorithm
 */
unsigned long vdma_alloc(unsigned long paddr, unsigned long size)
{
	int first, last, pages, frame, i;
	unsigned long laddr, flags;

	/* check arguments */

	if (paddr > 0x1fffffff) {
		if (vdma_debug)
			printk("vdma_alloc: Invalid physical address: %08lx\n",
			       paddr);
		return DMA_MAPPING_ERROR;	/* invalid physical address */
	}
	if (size > 0x400000 || size == 0) {
		if (vdma_debug)
			printk("vdma_alloc: Invalid size: %08lx\n", size);
		return DMA_MAPPING_ERROR;	/* invalid physical address */
	}

	spin_lock_irqsave(&vdma_lock, flags);
	/*
	 * Find free chunk
	 */
	pages = VDMA_PAGE(paddr + size) - VDMA_PAGE(paddr) + 1;
	first = 0;
	while (1) {
		while (pgtbl[first].owner != VDMA_PAGE_EMPTY &&
		       first < VDMA_PGTBL_ENTRIES) first++;
		if (first + pages > VDMA_PGTBL_ENTRIES) {	/* nothing free */
			spin_unlock_irqrestore(&vdma_lock, flags);
			return DMA_MAPPING_ERROR;
		}

		last = first + 1;
		while (pgtbl[last].owner == VDMA_PAGE_EMPTY
		       && last - first < pages)
			last++;

		if (last - first == pages)
			break;	/* found */
		first = last + 1;
	}

	/*
	 * Mark pages as allocated
	 */
	laddr = (first << 12) + (paddr & (VDMA_PAGESIZE - 1));
	frame = paddr & ~(VDMA_PAGESIZE - 1);

	for (i = first; i < last; i++) {
		pgtbl[i].frame = frame;
		pgtbl[i].owner = laddr;
		frame += VDMA_PAGESIZE;
	}

	/*
	 * Update translation table and return logical start address
	 */
	r4030_write_reg32(JAZZ_R4030_TRSTBL_INV, 0);

	if (vdma_debug > 1)
		printk("vdma_alloc: Allocated %d pages starting from %08lx\n",
		     pages, laddr);

	if (vdma_debug > 2) {
		printk("LADDR: ");
		for (i = first; i < last; i++)
			printk("%08x ", i << 12);
		printk("\nPADDR: ");
		for (i = first; i < last; i++)
			printk("%08x ", pgtbl[i].frame);
		printk("\nOWNER: ");
		for (i = first; i < last; i++)
			printk("%08x ", pgtbl[i].owner);
		printk("\n");
	}

	spin_unlock_irqrestore(&vdma_lock, flags);

	return laddr;
}

EXPORT_SYMBOL(vdma_alloc);

/*
 * Free previously allocated dma translation pages
 * Note that this does NOT change the translation table,
 * it just marks the free'd pages as unused!
 */
int vdma_free(unsigned long laddr)
{
	int i;

	i = laddr >> 12;

	if (pgtbl[i].owner != laddr) {
		printk
		    ("vdma_free: trying to free other's dma pages, laddr=%8lx\n",
		     laddr);
		return -1;
	}

	while (i < VDMA_PGTBL_ENTRIES && pgtbl[i].owner == laddr) {
		pgtbl[i].owner = VDMA_PAGE_EMPTY;
		i++;
	}

	if (vdma_debug > 1)
		printk("vdma_free: freed %ld pages starting from %08lx\n",
		       i - (laddr >> 12), laddr);

	return 0;
}

EXPORT_SYMBOL(vdma_free);

/*
 * Translate a physical address to a logical address.
 * This will return the logical address of the first
 * match.
 */
unsigned long vdma_phys2log(unsigned long paddr)
{
	int i;
	int frame;

	frame = paddr & ~(VDMA_PAGESIZE - 1);

	for (i = 0; i < VDMA_PGTBL_ENTRIES; i++) {
		if (pgtbl[i].frame == frame)
			break;
	}

	if (i == VDMA_PGTBL_ENTRIES)
		return ~0UL;

	return (i << 12) + (paddr & (VDMA_PAGESIZE - 1));
}

EXPORT_SYMBOL(vdma_phys2log);

/*
 * Translate a logical DMA address to a physical address
 */
unsigned long vdma_log2phys(unsigned long laddr)
{
	return pgtbl[laddr >> 12].frame + (laddr & (VDMA_PAGESIZE - 1));
}

EXPORT_SYMBOL(vdma_log2phys);

/*
 * Print DMA statistics
 */
void vdma_stats(void)
{
	int i;

	printk("vdma_stats: CONFIG: %08x\n",
	       r4030_read_reg32(JAZZ_R4030_CONFIG));
	printk("R4030 translation table base: %08x\n",
	       r4030_read_reg32(JAZZ_R4030_TRSTBL_BASE));
	printk("R4030 translation table limit: %08x\n",
	       r4030_read_reg32(JAZZ_R4030_TRSTBL_LIM));
	printk("vdma_stats: INV_ADDR: %08x\n",
	       r4030_read_reg32(JAZZ_R4030_INV_ADDR));
	printk("vdma_stats: R_FAIL_ADDR: %08x\n",
	       r4030_read_reg32(JAZZ_R4030_R_FAIL_ADDR));
	printk("vdma_stats: M_FAIL_ADDR: %08x\n",
	       r4030_read_reg32(JAZZ_R4030_M_FAIL_ADDR));
	printk("vdma_stats: IRQ_SOURCE: %08x\n",
	       r4030_read_reg32(JAZZ_R4030_IRQ_SOURCE));
	printk("vdma_stats: I386_ERROR: %08x\n",
	       r4030_read_reg32(JAZZ_R4030_I386_ERROR));
	printk("vdma_chnl_modes:   ");
	for (i = 0; i < 8; i++)
		printk("%04x ",
		       (unsigned) r4030_read_reg32(JAZZ_R4030_CHNL_MODE +
						   (i << 5)));
	printk("\n");
	printk("vdma_chnl_enables: ");
	for (i = 0; i < 8; i++)
		printk("%04x ",
		       (unsigned) r4030_read_reg32(JAZZ_R4030_CHNL_ENABLE +
						   (i << 5)));
	printk("\n");
}

/*
 * DMA transfer functions
 */

/*
 * Enable a DMA channel. Also clear any error conditions.
 */
void vdma_enable(int channel)
{
	int status;

	if (vdma_debug)
		printk("vdma_enable: channel %d\n", channel);

	/*
	 * Check error conditions first
	 */
	status = r4030_read_reg32(JAZZ_R4030_CHNL_ENABLE + (channel << 5));
	if (status & 0x400)
		printk("VDMA: Channel %d: Address error!\n", channel);
	if (status & 0x200)
		printk("VDMA: Channel %d: Memory error!\n", channel);

	/*
	 * Clear all interrupt flags
	 */
	r4030_write_reg32(JAZZ_R4030_CHNL_ENABLE + (channel << 5),
			  r4030_read_reg32(JAZZ_R4030_CHNL_ENABLE +
					   (channel << 5)) | R4030_TC_INTR
			  | R4030_MEM_INTR | R4030_ADDR_INTR);

	/*
	 * Enable the desired channel
	 */
	r4030_write_reg32(JAZZ_R4030_CHNL_ENABLE + (channel << 5),
			  r4030_read_reg32(JAZZ_R4030_CHNL_ENABLE +
					   (channel << 5)) |
			  R4030_CHNL_ENABLE);
}

EXPORT_SYMBOL(vdma_enable);

/*
 * Disable a DMA channel
 */
void vdma_disable(int channel)
{
	if (vdma_debug) {
		int status =
		    r4030_read_reg32(JAZZ_R4030_CHNL_ENABLE +
				     (channel << 5));

		printk("vdma_disable: channel %d\n", channel);
		printk("VDMA: channel %d status: %04x (%s) mode: "
		       "%02x addr: %06x count: %06x\n",
		       channel, status,
		       ((status & 0x600) ? "ERROR" : "OK"),
		       (unsigned) r4030_read_reg32(JAZZ_R4030_CHNL_MODE +
						   (channel << 5)),
		       (unsigned) r4030_read_reg32(JAZZ_R4030_CHNL_ADDR +
						   (channel << 5)),
		       (unsigned) r4030_read_reg32(JAZZ_R4030_CHNL_COUNT +
						   (channel << 5)));
	}

	r4030_write_reg32(JAZZ_R4030_CHNL_ENABLE + (channel << 5),
			  r4030_read_reg32(JAZZ_R4030_CHNL_ENABLE +
					   (channel << 5)) &
			  ~R4030_CHNL_ENABLE);

	/*
	 * After disabling a DMA channel a remote bus register should be
	 * read to ensure that the current DMA acknowledge cycle is completed.
	 */
	*((volatile unsigned int *) JAZZ_DUMMY_DEVICE);
}

EXPORT_SYMBOL(vdma_disable);

/*
 * Set DMA mode. This function accepts the mode values used
 * to set a PC-style DMA controller. For the SCSI and FDC
 * channels, we also set the default modes each time we're
 * called.
 * NOTE: The FAST and BURST dma modes are supported by the
 * R4030 Rev. 2 and PICA chipsets only. I leave them disabled
 * for now.
 */
void vdma_set_mode(int channel, int mode)
{
	if (vdma_debug)
		printk("vdma_set_mode: channel %d, mode 0x%x\n", channel,
		       mode);

	switch (channel) {
	case JAZZ_SCSI_DMA:	/* scsi */
		r4030_write_reg32(JAZZ_R4030_CHNL_MODE + (channel << 5),
/*			  R4030_MODE_FAST | */
/*			  R4030_MODE_BURST | */
				  R4030_MODE_INTR_EN |
				  R4030_MODE_WIDTH_16 |
				  R4030_MODE_ATIME_80);
		break;

	case JAZZ_FLOPPY_DMA:	/* floppy */
		r4030_write_reg32(JAZZ_R4030_CHNL_MODE + (channel << 5),
/*			  R4030_MODE_FAST | */
/*			  R4030_MODE_BURST | */
				  R4030_MODE_INTR_EN |
				  R4030_MODE_WIDTH_8 |
				  R4030_MODE_ATIME_120);
		break;

	case JAZZ_AUDIOL_DMA:
	case JAZZ_AUDIOR_DMA:
		printk("VDMA: Audio DMA not supported yet.\n");
		break;

	default:
		printk
		    ("VDMA: vdma_set_mode() called with unsupported channel %d!\n",
		     channel);
	}

	switch (mode) {
	case DMA_MODE_READ:
		r4030_write_reg32(JAZZ_R4030_CHNL_ENABLE + (channel << 5),
				  r4030_read_reg32(JAZZ_R4030_CHNL_ENABLE +
						   (channel << 5)) &
				  ~R4030_CHNL_WRITE);
		break;

	case DMA_MODE_WRITE:
		r4030_write_reg32(JAZZ_R4030_CHNL_ENABLE + (channel << 5),
				  r4030_read_reg32(JAZZ_R4030_CHNL_ENABLE +
						   (channel << 5)) |
				  R4030_CHNL_WRITE);
		break;

	default:
		printk
		    ("VDMA: vdma_set_mode() called with unknown dma mode 0x%x\n",
		     mode);
	}
}

EXPORT_SYMBOL(vdma_set_mode);

/*
 * Set Transfer Address
 */
void vdma_set_addr(int channel, long addr)
{
	if (vdma_debug)
		printk("vdma_set_addr: channel %d, addr %lx\n", channel,
		       addr);

	r4030_write_reg32(JAZZ_R4030_CHNL_ADDR + (channel << 5), addr);
}

EXPORT_SYMBOL(vdma_set_addr);

/*
 * Set Transfer Count
 */
void vdma_set_count(int channel, int count)
{
	if (vdma_debug)
		printk("vdma_set_count: channel %d, count %08x\n", channel,
		       (unsigned) count);

	r4030_write_reg32(JAZZ_R4030_CHNL_COUNT + (channel << 5), count);
}

EXPORT_SYMBOL(vdma_set_count);

/*
 * Get Residual
 */
int vdma_get_residue(int channel)
{
	int residual;

	residual = r4030_read_reg32(JAZZ_R4030_CHNL_COUNT + (channel << 5));

	if (vdma_debug)
		printk("vdma_get_residual: channel %d: residual=%d\n",
		       channel, residual);

	return residual;
}

/*
 * Get DMA channel enable register
 */
int vdma_get_enable(int channel)
{
	int enable;

	enable = r4030_read_reg32(JAZZ_R4030_CHNL_ENABLE + (channel << 5));

	if (vdma_debug)
		printk("vdma_get_enable: channel %d: enable=%d\n", channel,
		       enable);

	return enable;
}

static void *jazz_dma_alloc(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	struct page *page;
	void *ret;

	if (attrs & DMA_ATTR_NO_WARN)
		gfp |= __GFP_NOWARN;

	size = PAGE_ALIGN(size);
	page = alloc_pages(gfp, get_order(size));
	if (!page)
		return NULL;
	ret = page_address(page);
	memset(ret, 0, size);
	*dma_handle = vdma_alloc(virt_to_phys(ret), size);
	if (*dma_handle == DMA_MAPPING_ERROR)
		goto out_free_pages;

	if (attrs & DMA_ATTR_NON_CONSISTENT)
		return ret;
	arch_dma_prep_coherent(page, size);
	return (void *)(UNCAC_BASE + __pa(ret));

out_free_pages:
	__free_pages(page, get_order(size));
	return NULL;
}

static void jazz_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, unsigned long attrs)
{
	vdma_free(dma_handle);
	if (!(attrs & DMA_ATTR_NON_CONSISTENT))
		vaddr = __va(vaddr - UNCAC_BASE);
	__free_pages(virt_to_page(vaddr), get_order(size));
}

static dma_addr_t jazz_dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs)
{
	phys_addr_t phys = page_to_phys(page) + offset;

	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		arch_sync_dma_for_device(phys, size, dir);
	return vdma_alloc(phys, size);
}

static void jazz_dma_unmap_page(struct device *dev, dma_addr_t dma_addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		arch_sync_dma_for_cpu(vdma_log2phys(dma_addr), size, dir);
	vdma_free(dma_addr);
}

static int jazz_dma_map_sg(struct device *dev, struct scatterlist *sglist,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sglist, sg, nents, i) {
		if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
			arch_sync_dma_for_device(sg_phys(sg), sg->length,
				dir);
		sg->dma_address = vdma_alloc(sg_phys(sg), sg->length);
		if (sg->dma_address == DMA_MAPPING_ERROR)
			return 0;
		sg_dma_len(sg) = sg->length;
	}

	return nents;
}

static void jazz_dma_unmap_sg(struct device *dev, struct scatterlist *sglist,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sglist, sg, nents, i) {
		if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
			arch_sync_dma_for_cpu(sg_phys(sg), sg->length, dir);
		vdma_free(sg->dma_address);
	}
}

static void jazz_dma_sync_single_for_device(struct device *dev,
		dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	arch_sync_dma_for_device(vdma_log2phys(addr), size, dir);
}

static void jazz_dma_sync_single_for_cpu(struct device *dev,
		dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	arch_sync_dma_for_cpu(vdma_log2phys(addr), size, dir);
}

static void jazz_dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i)
		arch_sync_dma_for_device(sg_phys(sg), sg->length, dir);
}

static void jazz_dma_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i)
		arch_sync_dma_for_cpu(sg_phys(sg), sg->length, dir);
}

const struct dma_map_ops jazz_dma_ops = {
	.alloc			= jazz_dma_alloc,
	.free			= jazz_dma_free,
	.map_page		= jazz_dma_map_page,
	.unmap_page		= jazz_dma_unmap_page,
	.map_sg			= jazz_dma_map_sg,
	.unmap_sg		= jazz_dma_unmap_sg,
	.sync_single_for_cpu	= jazz_dma_sync_single_for_cpu,
	.sync_single_for_device	= jazz_dma_sync_single_for_device,
	.sync_sg_for_cpu	= jazz_dma_sync_sg_for_cpu,
	.sync_sg_for_device	= jazz_dma_sync_sg_for_device,
	.mmap			= dma_common_mmap,
	.get_sgtable		= dma_common_get_sgtable,
};
EXPORT_SYMBOL(jazz_dma_ops);
