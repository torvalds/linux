/*
 * IOMMU implementation for Cell Broadband Processor Architecture
 *
 * (C) Copyright IBM Corporation 2006-2008
 *
 * Author: Jeremy Kerr <jk@ozlabs.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/memblock.h>

#include <asm/prom.h>
#include <asm/iommu.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/udbg.h>
#include <asm/firmware.h>
#include <asm/cell-regs.h>

#include "cell.h"
#include "interrupt.h"

/* Define CELL_IOMMU_REAL_UNMAP to actually unmap non-used pages
 * instead of leaving them mapped to some dummy page. This can be
 * enabled once the appropriate workarounds for spider bugs have
 * been enabled
 */
#define CELL_IOMMU_REAL_UNMAP

/* Define CELL_IOMMU_STRICT_PROTECTION to enforce protection of
 * IO PTEs based on the transfer direction. That can be enabled
 * once spider-net has been fixed to pass the correct direction
 * to the DMA mapping functions
 */
#define CELL_IOMMU_STRICT_PROTECTION


#define NR_IOMMUS			2

/* IOC mmap registers */
#define IOC_Reg_Size			0x2000

#define IOC_IOPT_CacheInvd		0x908
#define IOC_IOPT_CacheInvd_NE_Mask	0xffe0000000000000ul
#define IOC_IOPT_CacheInvd_IOPTE_Mask	0x000003fffffffff8ul
#define IOC_IOPT_CacheInvd_Busy		0x0000000000000001ul

#define IOC_IOST_Origin			0x918
#define IOC_IOST_Origin_E		0x8000000000000000ul
#define IOC_IOST_Origin_HW		0x0000000000000800ul
#define IOC_IOST_Origin_HL		0x0000000000000400ul

#define IOC_IO_ExcpStat			0x920
#define IOC_IO_ExcpStat_V		0x8000000000000000ul
#define IOC_IO_ExcpStat_SPF_Mask	0x6000000000000000ul
#define IOC_IO_ExcpStat_SPF_S		0x6000000000000000ul
#define IOC_IO_ExcpStat_SPF_P		0x2000000000000000ul
#define IOC_IO_ExcpStat_ADDR_Mask	0x00000007fffff000ul
#define IOC_IO_ExcpStat_RW_Mask		0x0000000000000800ul
#define IOC_IO_ExcpStat_IOID_Mask	0x00000000000007fful

#define IOC_IO_ExcpMask			0x928
#define IOC_IO_ExcpMask_SFE		0x4000000000000000ul
#define IOC_IO_ExcpMask_PFE		0x2000000000000000ul

#define IOC_IOCmd_Offset		0x1000

#define IOC_IOCmd_Cfg			0xc00
#define IOC_IOCmd_Cfg_TE		0x0000800000000000ul


/* Segment table entries */
#define IOSTE_V			0x8000000000000000ul /* valid */
#define IOSTE_H			0x4000000000000000ul /* cache hint */
#define IOSTE_PT_Base_RPN_Mask  0x3ffffffffffff000ul /* base RPN of IOPT */
#define IOSTE_NPPT_Mask		0x0000000000000fe0ul /* no. pages in IOPT */
#define IOSTE_PS_Mask		0x0000000000000007ul /* page size */
#define IOSTE_PS_4K		0x0000000000000001ul /*   - 4kB  */
#define IOSTE_PS_64K		0x0000000000000003ul /*   - 64kB */
#define IOSTE_PS_1M		0x0000000000000005ul /*   - 1MB  */
#define IOSTE_PS_16M		0x0000000000000007ul /*   - 16MB */


/* IOMMU sizing */
#define IO_SEGMENT_SHIFT	28
#define IO_PAGENO_BITS(shift)	(IO_SEGMENT_SHIFT - (shift))

/* The high bit needs to be set on every DMA address */
#define SPIDER_DMA_OFFSET	0x80000000ul

struct iommu_window {
	struct list_head list;
	struct cbe_iommu *iommu;
	unsigned long offset;
	unsigned long size;
	unsigned int ioid;
	struct iommu_table table;
};

#define NAMESIZE 8
struct cbe_iommu {
	int nid;
	char name[NAMESIZE];
	void __iomem *xlate_regs;
	void __iomem *cmd_regs;
	unsigned long *stab;
	unsigned long *ptab;
	void *pad_page;
	struct list_head windows;
};

/* Static array of iommus, one per node
 *   each contains a list of windows, keyed from dma_window property
 *   - on bus setup, look for a matching window, or create one
 *   - on dev setup, assign iommu_table ptr
 */
static struct cbe_iommu iommus[NR_IOMMUS];
static int cbe_nr_iommus;

static void invalidate_tce_cache(struct cbe_iommu *iommu, unsigned long *pte,
		long n_ptes)
{
	u64 __iomem *reg;
	u64 val;
	long n;

	reg = iommu->xlate_regs + IOC_IOPT_CacheInvd;

	while (n_ptes > 0) {
		/* we can invalidate up to 1 << 11 PTEs at once */
		n = min(n_ptes, 1l << 11);
		val = (((n /*- 1*/) << 53) & IOC_IOPT_CacheInvd_NE_Mask)
			| (__pa(pte) & IOC_IOPT_CacheInvd_IOPTE_Mask)
		        | IOC_IOPT_CacheInvd_Busy;

		out_be64(reg, val);
		while (in_be64(reg) & IOC_IOPT_CacheInvd_Busy)
			;

		n_ptes -= n;
		pte += n;
	}
}

static int tce_build_cell(struct iommu_table *tbl, long index, long npages,
		unsigned long uaddr, enum dma_data_direction direction,
		unsigned long attrs)
{
	int i;
	unsigned long *io_pte, base_pte;
	struct iommu_window *window =
		container_of(tbl, struct iommu_window, table);

	/* implementing proper protection causes problems with the spidernet
	 * driver - check mapping directions later, but allow read & write by
	 * default for now.*/
#ifdef CELL_IOMMU_STRICT_PROTECTION
	/* to avoid referencing a global, we use a trick here to setup the
	 * protection bit. "prot" is setup to be 3 fields of 4 bits appended
	 * together for each of the 3 supported direction values. It is then
	 * shifted left so that the fields matching the desired direction
	 * lands on the appropriate bits, and other bits are masked out.
	 */
	const unsigned long prot = 0xc48;
	base_pte =
		((prot << (52 + 4 * direction)) &
		 (CBE_IOPTE_PP_W | CBE_IOPTE_PP_R)) |
		CBE_IOPTE_M | CBE_IOPTE_SO_RW |
		(window->ioid & CBE_IOPTE_IOID_Mask);
#else
	base_pte = CBE_IOPTE_PP_W | CBE_IOPTE_PP_R | CBE_IOPTE_M |
		CBE_IOPTE_SO_RW | (window->ioid & CBE_IOPTE_IOID_Mask);
#endif
	if (unlikely(attrs & DMA_ATTR_WEAK_ORDERING))
		base_pte &= ~CBE_IOPTE_SO_RW;

	io_pte = (unsigned long *)tbl->it_base + (index - tbl->it_offset);

	for (i = 0; i < npages; i++, uaddr += (1 << tbl->it_page_shift))
		io_pte[i] = base_pte | (__pa(uaddr) & CBE_IOPTE_RPN_Mask);

	mb();

	invalidate_tce_cache(window->iommu, io_pte, npages);

	pr_debug("tce_build_cell(index=%lx,n=%lx,dir=%d,base_pte=%lx)\n",
		 index, npages, direction, base_pte);
	return 0;
}

static void tce_free_cell(struct iommu_table *tbl, long index, long npages)
{

	int i;
	unsigned long *io_pte, pte;
	struct iommu_window *window =
		container_of(tbl, struct iommu_window, table);

	pr_debug("tce_free_cell(index=%lx,n=%lx)\n", index, npages);

#ifdef CELL_IOMMU_REAL_UNMAP
	pte = 0;
#else
	/* spider bridge does PCI reads after freeing - insert a mapping
	 * to a scratch page instead of an invalid entry */
	pte = CBE_IOPTE_PP_R | CBE_IOPTE_M | CBE_IOPTE_SO_RW |
		__pa(window->iommu->pad_page) |
		(window->ioid & CBE_IOPTE_IOID_Mask);
#endif

	io_pte = (unsigned long *)tbl->it_base + (index - tbl->it_offset);

	for (i = 0; i < npages; i++)
		io_pte[i] = pte;

	mb();

	invalidate_tce_cache(window->iommu, io_pte, npages);
}

static irqreturn_t ioc_interrupt(int irq, void *data)
{
	unsigned long stat, spf;
	struct cbe_iommu *iommu = data;

	stat = in_be64(iommu->xlate_regs + IOC_IO_ExcpStat);
	spf = stat & IOC_IO_ExcpStat_SPF_Mask;

	/* Might want to rate limit it */
	printk(KERN_ERR "iommu: DMA exception 0x%016lx\n", stat);
	printk(KERN_ERR "  V=%d, SPF=[%c%c], RW=%s, IOID=0x%04x\n",
	       !!(stat & IOC_IO_ExcpStat_V),
	       (spf == IOC_IO_ExcpStat_SPF_S) ? 'S' : ' ',
	       (spf == IOC_IO_ExcpStat_SPF_P) ? 'P' : ' ',
	       (stat & IOC_IO_ExcpStat_RW_Mask) ? "Read" : "Write",
	       (unsigned int)(stat & IOC_IO_ExcpStat_IOID_Mask));
	printk(KERN_ERR "  page=0x%016lx\n",
	       stat & IOC_IO_ExcpStat_ADDR_Mask);

	/* clear interrupt */
	stat &= ~IOC_IO_ExcpStat_V;
	out_be64(iommu->xlate_regs + IOC_IO_ExcpStat, stat);

	return IRQ_HANDLED;
}

static int cell_iommu_find_ioc(int nid, unsigned long *base)
{
	struct device_node *np;
	struct resource r;

	*base = 0;

	/* First look for new style /be nodes */
	for_each_node_by_name(np, "ioc") {
		if (of_node_to_nid(np) != nid)
			continue;
		if (of_address_to_resource(np, 0, &r)) {
			printk(KERN_ERR "iommu: can't get address for %s\n",
			       np->full_name);
			continue;
		}
		*base = r.start;
		of_node_put(np);
		return 0;
	}

	/* Ok, let's try the old way */
	for_each_node_by_type(np, "cpu") {
		const unsigned int *nidp;
		const unsigned long *tmp;

		nidp = of_get_property(np, "node-id", NULL);
		if (nidp && *nidp == nid) {
			tmp = of_get_property(np, "ioc-translation", NULL);
			if (tmp) {
				*base = *tmp;
				of_node_put(np);
				return 0;
			}
		}
	}

	return -ENODEV;
}

static void cell_iommu_setup_stab(struct cbe_iommu *iommu,
				unsigned long dbase, unsigned long dsize,
				unsigned long fbase, unsigned long fsize)
{
	struct page *page;
	unsigned long segments, stab_size;

	segments = max(dbase + dsize, fbase + fsize) >> IO_SEGMENT_SHIFT;

	pr_debug("%s: iommu[%d]: segments: %lu\n",
			__func__, iommu->nid, segments);

	/* set up the segment table */
	stab_size = segments * sizeof(unsigned long);
	page = alloc_pages_node(iommu->nid, GFP_KERNEL, get_order(stab_size));
	BUG_ON(!page);
	iommu->stab = page_address(page);
	memset(iommu->stab, 0, stab_size);
}

static unsigned long *cell_iommu_alloc_ptab(struct cbe_iommu *iommu,
		unsigned long base, unsigned long size, unsigned long gap_base,
		unsigned long gap_size, unsigned long page_shift)
{
	struct page *page;
	int i;
	unsigned long reg, segments, pages_per_segment, ptab_size,
		      n_pte_pages, start_seg, *ptab;

	start_seg = base >> IO_SEGMENT_SHIFT;
	segments  = size >> IO_SEGMENT_SHIFT;
	pages_per_segment = 1ull << IO_PAGENO_BITS(page_shift);
	/* PTEs for each segment must start on a 4K boundary */
	pages_per_segment = max(pages_per_segment,
				(1 << 12) / sizeof(unsigned long));

	ptab_size = segments * pages_per_segment * sizeof(unsigned long);
	pr_debug("%s: iommu[%d]: ptab_size: %lu, order: %d\n", __func__,
			iommu->nid, ptab_size, get_order(ptab_size));
	page = alloc_pages_node(iommu->nid, GFP_KERNEL, get_order(ptab_size));
	BUG_ON(!page);

	ptab = page_address(page);
	memset(ptab, 0, ptab_size);

	/* number of 4K pages needed for a page table */
	n_pte_pages = (pages_per_segment * sizeof(unsigned long)) >> 12;

	pr_debug("%s: iommu[%d]: stab at %p, ptab at %p, n_pte_pages: %lu\n",
			__func__, iommu->nid, iommu->stab, ptab,
			n_pte_pages);

	/* initialise the STEs */
	reg = IOSTE_V | ((n_pte_pages - 1) << 5);

	switch (page_shift) {
	case 12: reg |= IOSTE_PS_4K;  break;
	case 16: reg |= IOSTE_PS_64K; break;
	case 20: reg |= IOSTE_PS_1M;  break;
	case 24: reg |= IOSTE_PS_16M; break;
	default: BUG();
	}

	gap_base = gap_base >> IO_SEGMENT_SHIFT;
	gap_size = gap_size >> IO_SEGMENT_SHIFT;

	pr_debug("Setting up IOMMU stab:\n");
	for (i = start_seg; i < (start_seg + segments); i++) {
		if (i >= gap_base && i < (gap_base + gap_size)) {
			pr_debug("\toverlap at %d, skipping\n", i);
			continue;
		}
		iommu->stab[i] = reg | (__pa(ptab) + (n_pte_pages << 12) *
					(i - start_seg));
		pr_debug("\t[%d] 0x%016lx\n", i, iommu->stab[i]);
	}

	return ptab;
}

static void cell_iommu_enable_hardware(struct cbe_iommu *iommu)
{
	int ret;
	unsigned long reg, xlate_base;
	unsigned int virq;

	if (cell_iommu_find_ioc(iommu->nid, &xlate_base))
		panic("%s: missing IOC register mappings for node %d\n",
		      __func__, iommu->nid);

	iommu->xlate_regs = ioremap(xlate_base, IOC_Reg_Size);
	iommu->cmd_regs = iommu->xlate_regs + IOC_IOCmd_Offset;

	/* ensure that the STEs have updated */
	mb();

	/* setup interrupts for the iommu. */
	reg = in_be64(iommu->xlate_regs + IOC_IO_ExcpStat);
	out_be64(iommu->xlate_regs + IOC_IO_ExcpStat,
			reg & ~IOC_IO_ExcpStat_V);
	out_be64(iommu->xlate_regs + IOC_IO_ExcpMask,
			IOC_IO_ExcpMask_PFE | IOC_IO_ExcpMask_SFE);

	virq = irq_create_mapping(NULL,
			IIC_IRQ_IOEX_ATI | (iommu->nid << IIC_IRQ_NODE_SHIFT));
	BUG_ON(!virq);

	ret = request_irq(virq, ioc_interrupt, 0, iommu->name, iommu);
	BUG_ON(ret);

	/* set the IOC segment table origin register (and turn on the iommu) */
	reg = IOC_IOST_Origin_E | __pa(iommu->stab) | IOC_IOST_Origin_HW;
	out_be64(iommu->xlate_regs + IOC_IOST_Origin, reg);
	in_be64(iommu->xlate_regs + IOC_IOST_Origin);

	/* turn on IO translation */
	reg = in_be64(iommu->cmd_regs + IOC_IOCmd_Cfg) | IOC_IOCmd_Cfg_TE;
	out_be64(iommu->cmd_regs + IOC_IOCmd_Cfg, reg);
}

static void cell_iommu_setup_hardware(struct cbe_iommu *iommu,
	unsigned long base, unsigned long size)
{
	cell_iommu_setup_stab(iommu, base, size, 0, 0);
	iommu->ptab = cell_iommu_alloc_ptab(iommu, base, size, 0, 0,
					    IOMMU_PAGE_SHIFT_4K);
	cell_iommu_enable_hardware(iommu);
}

#if 0/* Unused for now */
static struct iommu_window *find_window(struct cbe_iommu *iommu,
		unsigned long offset, unsigned long size)
{
	struct iommu_window *window;

	/* todo: check for overlapping (but not equal) windows) */

	list_for_each_entry(window, &(iommu->windows), list) {
		if (window->offset == offset && window->size == size)
			return window;
	}

	return NULL;
}
#endif

static inline u32 cell_iommu_get_ioid(struct device_node *np)
{
	const u32 *ioid;

	ioid = of_get_property(np, "ioid", NULL);
	if (ioid == NULL) {
		printk(KERN_WARNING "iommu: missing ioid for %s using 0\n",
		       np->full_name);
		return 0;
	}

	return *ioid;
}

static struct iommu_table_ops cell_iommu_ops = {
	.set = tce_build_cell,
	.clear = tce_free_cell
};

static struct iommu_window * __init
cell_iommu_setup_window(struct cbe_iommu *iommu, struct device_node *np,
			unsigned long offset, unsigned long size,
			unsigned long pte_offset)
{
	struct iommu_window *window;
	struct page *page;
	u32 ioid;

	ioid = cell_iommu_get_ioid(np);

	window = kzalloc_node(sizeof(*window), GFP_KERNEL, iommu->nid);
	BUG_ON(window == NULL);

	window->offset = offset;
	window->size = size;
	window->ioid = ioid;
	window->iommu = iommu;

	window->table.it_blocksize = 16;
	window->table.it_base = (unsigned long)iommu->ptab;
	window->table.it_index = iommu->nid;
	window->table.it_page_shift = IOMMU_PAGE_SHIFT_4K;
	window->table.it_offset =
		(offset >> window->table.it_page_shift) + pte_offset;
	window->table.it_size = size >> window->table.it_page_shift;
	window->table.it_ops = &cell_iommu_ops;

	iommu_init_table(&window->table, iommu->nid);

	pr_debug("\tioid      %d\n", window->ioid);
	pr_debug("\tblocksize %ld\n", window->table.it_blocksize);
	pr_debug("\tbase      0x%016lx\n", window->table.it_base);
	pr_debug("\toffset    0x%lx\n", window->table.it_offset);
	pr_debug("\tsize      %ld\n", window->table.it_size);

	list_add(&window->list, &iommu->windows);

	if (offset != 0)
		return window;

	/* We need to map and reserve the first IOMMU page since it's used
	 * by the spider workaround. In theory, we only need to do that when
	 * running on spider but it doesn't really matter.
	 *
	 * This code also assumes that we have a window that starts at 0,
	 * which is the case on all spider based blades.
	 */
	page = alloc_pages_node(iommu->nid, GFP_KERNEL, 0);
	BUG_ON(!page);
	iommu->pad_page = page_address(page);
	clear_page(iommu->pad_page);

	__set_bit(0, window->table.it_map);
	tce_build_cell(&window->table, window->table.it_offset, 1,
		       (unsigned long)iommu->pad_page, DMA_TO_DEVICE, 0);

	return window;
}

static struct cbe_iommu *cell_iommu_for_node(int nid)
{
	int i;

	for (i = 0; i < cbe_nr_iommus; i++)
		if (iommus[i].nid == nid)
			return &iommus[i];
	return NULL;
}

static unsigned long cell_dma_direct_offset;

static unsigned long dma_iommu_fixed_base;

/* iommu_fixed_is_weak is set if booted with iommu_fixed=weak */
static int iommu_fixed_is_weak;

static struct iommu_table *cell_get_iommu_table(struct device *dev)
{
	struct iommu_window *window;
	struct cbe_iommu *iommu;

	/* Current implementation uses the first window available in that
	 * node's iommu. We -might- do something smarter later though it may
	 * never be necessary
	 */
	iommu = cell_iommu_for_node(dev_to_node(dev));
	if (iommu == NULL || list_empty(&iommu->windows)) {
		dev_err(dev, "iommu: missing iommu for %s (node %d)\n",
		       of_node_full_name(dev->of_node), dev_to_node(dev));
		return NULL;
	}
	window = list_entry(iommu->windows.next, struct iommu_window, list);

	return &window->table;
}

/* A coherent allocation implies strong ordering */

static void *dma_fixed_alloc_coherent(struct device *dev, size_t size,
				      dma_addr_t *dma_handle, gfp_t flag,
				      unsigned long attrs)
{
	if (iommu_fixed_is_weak)
		return iommu_alloc_coherent(dev, cell_get_iommu_table(dev),
					    size, dma_handle,
					    device_to_mask(dev), flag,
					    dev_to_node(dev));
	else
		return dma_direct_ops.alloc(dev, size, dma_handle, flag,
					    attrs);
}

static void dma_fixed_free_coherent(struct device *dev, size_t size,
				    void *vaddr, dma_addr_t dma_handle,
				    unsigned long attrs)
{
	if (iommu_fixed_is_weak)
		iommu_free_coherent(cell_get_iommu_table(dev), size, vaddr,
				    dma_handle);
	else
		dma_direct_ops.free(dev, size, vaddr, dma_handle, attrs);
}

static dma_addr_t dma_fixed_map_page(struct device *dev, struct page *page,
				     unsigned long offset, size_t size,
				     enum dma_data_direction direction,
				     unsigned long attrs)
{
	if (iommu_fixed_is_weak == (attrs & DMA_ATTR_WEAK_ORDERING))
		return dma_direct_ops.map_page(dev, page, offset, size,
					       direction, attrs);
	else
		return iommu_map_page(dev, cell_get_iommu_table(dev), page,
				      offset, size, device_to_mask(dev),
				      direction, attrs);
}

static void dma_fixed_unmap_page(struct device *dev, dma_addr_t dma_addr,
				 size_t size, enum dma_data_direction direction,
				 unsigned long attrs)
{
	if (iommu_fixed_is_weak == (attrs & DMA_ATTR_WEAK_ORDERING))
		dma_direct_ops.unmap_page(dev, dma_addr, size, direction,
					  attrs);
	else
		iommu_unmap_page(cell_get_iommu_table(dev), dma_addr, size,
				 direction, attrs);
}

static int dma_fixed_map_sg(struct device *dev, struct scatterlist *sg,
			   int nents, enum dma_data_direction direction,
			   unsigned long attrs)
{
	if (iommu_fixed_is_weak == (attrs & DMA_ATTR_WEAK_ORDERING))
		return dma_direct_ops.map_sg(dev, sg, nents, direction, attrs);
	else
		return ppc_iommu_map_sg(dev, cell_get_iommu_table(dev), sg,
					nents, device_to_mask(dev),
					direction, attrs);
}

static void dma_fixed_unmap_sg(struct device *dev, struct scatterlist *sg,
			       int nents, enum dma_data_direction direction,
			       unsigned long attrs)
{
	if (iommu_fixed_is_weak == (attrs & DMA_ATTR_WEAK_ORDERING))
		dma_direct_ops.unmap_sg(dev, sg, nents, direction, attrs);
	else
		ppc_iommu_unmap_sg(cell_get_iommu_table(dev), sg, nents,
				   direction, attrs);
}

static int dma_fixed_dma_supported(struct device *dev, u64 mask)
{
	return mask == DMA_BIT_MASK(64);
}

static int dma_set_mask_and_switch(struct device *dev, u64 dma_mask);

static const struct dma_map_ops dma_iommu_fixed_ops = {
	.alloc          = dma_fixed_alloc_coherent,
	.free           = dma_fixed_free_coherent,
	.map_sg         = dma_fixed_map_sg,
	.unmap_sg       = dma_fixed_unmap_sg,
	.dma_supported  = dma_fixed_dma_supported,
	.set_dma_mask   = dma_set_mask_and_switch,
	.map_page       = dma_fixed_map_page,
	.unmap_page     = dma_fixed_unmap_page,
};

static void cell_dma_dev_setup_fixed(struct device *dev);

static void cell_dma_dev_setup(struct device *dev)
{
	/* Order is important here, these are not mutually exclusive */
	if (get_dma_ops(dev) == &dma_iommu_fixed_ops)
		cell_dma_dev_setup_fixed(dev);
	else if (get_pci_dma_ops() == &dma_iommu_ops)
		set_iommu_table_base(dev, cell_get_iommu_table(dev));
	else if (get_pci_dma_ops() == &dma_direct_ops)
		set_dma_offset(dev, cell_dma_direct_offset);
	else
		BUG();
}

static void cell_pci_dma_dev_setup(struct pci_dev *dev)
{
	cell_dma_dev_setup(&dev->dev);
}

static int cell_of_bus_notify(struct notifier_block *nb, unsigned long action,
			      void *data)
{
	struct device *dev = data;

	/* We are only intereted in device addition */
	if (action != BUS_NOTIFY_ADD_DEVICE)
		return 0;

	/* We use the PCI DMA ops */
	dev->dma_ops = get_pci_dma_ops();

	cell_dma_dev_setup(dev);

	return 0;
}

static struct notifier_block cell_of_bus_notifier = {
	.notifier_call = cell_of_bus_notify
};

static int __init cell_iommu_get_window(struct device_node *np,
					 unsigned long *base,
					 unsigned long *size)
{
	const __be32 *dma_window;
	unsigned long index;

	/* Use ibm,dma-window if available, else, hard code ! */
	dma_window = of_get_property(np, "ibm,dma-window", NULL);
	if (dma_window == NULL) {
		*base = 0;
		*size = 0x80000000u;
		return -ENODEV;
	}

	of_parse_dma_window(np, dma_window, &index, base, size);
	return 0;
}

static struct cbe_iommu * __init cell_iommu_alloc(struct device_node *np)
{
	struct cbe_iommu *iommu;
	int nid, i;

	/* Get node ID */
	nid = of_node_to_nid(np);
	if (nid < 0) {
		printk(KERN_ERR "iommu: failed to get node for %s\n",
		       np->full_name);
		return NULL;
	}
	pr_debug("iommu: setting up iommu for node %d (%s)\n",
		 nid, np->full_name);

	/* XXX todo: If we can have multiple windows on the same IOMMU, which
	 * isn't the case today, we probably want here to check whether the
	 * iommu for that node is already setup.
	 * However, there might be issue with getting the size right so let's
	 * ignore that for now. We might want to completely get rid of the
	 * multiple window support since the cell iommu supports per-page ioids
	 */

	if (cbe_nr_iommus >= NR_IOMMUS) {
		printk(KERN_ERR "iommu: too many IOMMUs detected ! (%s)\n",
		       np->full_name);
		return NULL;
	}

	/* Init base fields */
	i = cbe_nr_iommus++;
	iommu = &iommus[i];
	iommu->stab = NULL;
	iommu->nid = nid;
	snprintf(iommu->name, sizeof(iommu->name), "iommu%d", i);
	INIT_LIST_HEAD(&iommu->windows);

	return iommu;
}

static void __init cell_iommu_init_one(struct device_node *np,
				       unsigned long offset)
{
	struct cbe_iommu *iommu;
	unsigned long base, size;

	iommu = cell_iommu_alloc(np);
	if (!iommu)
		return;

	/* Obtain a window for it */
	cell_iommu_get_window(np, &base, &size);

	pr_debug("\ttranslating window 0x%lx...0x%lx\n",
		 base, base + size - 1);

	/* Initialize the hardware */
	cell_iommu_setup_hardware(iommu, base, size);

	/* Setup the iommu_table */
	cell_iommu_setup_window(iommu, np, base, size,
				offset >> IOMMU_PAGE_SHIFT_4K);
}

static void __init cell_disable_iommus(void)
{
	int node;
	unsigned long base, val;
	void __iomem *xregs, *cregs;

	/* Make sure IOC translation is disabled on all nodes */
	for_each_online_node(node) {
		if (cell_iommu_find_ioc(node, &base))
			continue;
		xregs = ioremap(base, IOC_Reg_Size);
		if (xregs == NULL)
			continue;
		cregs = xregs + IOC_IOCmd_Offset;

		pr_debug("iommu: cleaning up iommu on node %d\n", node);

		out_be64(xregs + IOC_IOST_Origin, 0);
		(void)in_be64(xregs + IOC_IOST_Origin);
		val = in_be64(cregs + IOC_IOCmd_Cfg);
		val &= ~IOC_IOCmd_Cfg_TE;
		out_be64(cregs + IOC_IOCmd_Cfg, val);
		(void)in_be64(cregs + IOC_IOCmd_Cfg);

		iounmap(xregs);
	}
}

static int __init cell_iommu_init_disabled(void)
{
	struct device_node *np = NULL;
	unsigned long base = 0, size;

	/* When no iommu is present, we use direct DMA ops */
	set_pci_dma_ops(&dma_direct_ops);

	/* First make sure all IOC translation is turned off */
	cell_disable_iommus();

	/* If we have no Axon, we set up the spider DMA magic offset */
	if (of_find_node_by_name(NULL, "axon") == NULL)
		cell_dma_direct_offset = SPIDER_DMA_OFFSET;

	/* Now we need to check to see where the memory is mapped
	 * in PCI space. We assume that all busses use the same dma
	 * window which is always the case so far on Cell, thus we
	 * pick up the first pci-internal node we can find and check
	 * the DMA window from there.
	 */
	for_each_node_by_name(np, "axon") {
		if (np->parent == NULL || np->parent->parent != NULL)
			continue;
		if (cell_iommu_get_window(np, &base, &size) == 0)
			break;
	}
	if (np == NULL) {
		for_each_node_by_name(np, "pci-internal") {
			if (np->parent == NULL || np->parent->parent != NULL)
				continue;
			if (cell_iommu_get_window(np, &base, &size) == 0)
				break;
		}
	}
	of_node_put(np);

	/* If we found a DMA window, we check if it's big enough to enclose
	 * all of physical memory. If not, we force enable IOMMU
	 */
	if (np && size < memblock_end_of_DRAM()) {
		printk(KERN_WARNING "iommu: force-enabled, dma window"
		       " (%ldMB) smaller than total memory (%lldMB)\n",
		       size >> 20, memblock_end_of_DRAM() >> 20);
		return -ENODEV;
	}

	cell_dma_direct_offset += base;

	if (cell_dma_direct_offset != 0)
		cell_pci_controller_ops.dma_dev_setup = cell_pci_dma_dev_setup;

	printk("iommu: disabled, direct DMA offset is 0x%lx\n",
	       cell_dma_direct_offset);

	return 0;
}

/*
 *  Fixed IOMMU mapping support
 *
 *  This code adds support for setting up a fixed IOMMU mapping on certain
 *  cell machines. For 64-bit devices this avoids the performance overhead of
 *  mapping and unmapping pages at runtime. 32-bit devices are unable to use
 *  the fixed mapping.
 *
 *  The fixed mapping is established at boot, and maps all of physical memory
 *  1:1 into device space at some offset. On machines with < 30 GB of memory
 *  we setup the fixed mapping immediately above the normal IOMMU window.
 *
 *  For example a machine with 4GB of memory would end up with the normal
 *  IOMMU window from 0-2GB and the fixed mapping window from 2GB to 6GB. In
 *  this case a 64-bit device wishing to DMA to 1GB would be told to DMA to
 *  3GB, plus any offset required by firmware. The firmware offset is encoded
 *  in the "dma-ranges" property.
 *
 *  On machines with 30GB or more of memory, we are unable to place the fixed
 *  mapping above the normal IOMMU window as we would run out of address space.
 *  Instead we move the normal IOMMU window to coincide with the hash page
 *  table, this region does not need to be part of the fixed mapping as no
 *  device should ever be DMA'ing to it. We then setup the fixed mapping
 *  from 0 to 32GB.
 */

static u64 cell_iommu_get_fixed_address(struct device *dev)
{
	u64 cpu_addr, size, best_size, dev_addr = OF_BAD_ADDR;
	struct device_node *np;
	const u32 *ranges = NULL;
	int i, len, best, naddr, nsize, pna, range_size;

	np = of_node_get(dev->of_node);
	while (1) {
		naddr = of_n_addr_cells(np);
		nsize = of_n_size_cells(np);
		np = of_get_next_parent(np);
		if (!np)
			break;

		ranges = of_get_property(np, "dma-ranges", &len);

		/* Ignore empty ranges, they imply no translation required */
		if (ranges && len > 0)
			break;
	}

	if (!ranges) {
		dev_dbg(dev, "iommu: no dma-ranges found\n");
		goto out;
	}

	len /= sizeof(u32);

	pna = of_n_addr_cells(np);
	range_size = naddr + nsize + pna;

	/* dma-ranges format:
	 * child addr	: naddr cells
	 * parent addr	: pna cells
	 * size		: nsize cells
	 */
	for (i = 0, best = -1, best_size = 0; i < len; i += range_size) {
		cpu_addr = of_translate_dma_address(np, ranges + i + naddr);
		size = of_read_number(ranges + i + naddr + pna, nsize);

		if (cpu_addr == 0 && size > best_size) {
			best = i;
			best_size = size;
		}
	}

	if (best >= 0) {
		dev_addr = of_read_number(ranges + best, naddr);
	} else
		dev_dbg(dev, "iommu: no suitable range found!\n");

out:
	of_node_put(np);

	return dev_addr;
}

static int dma_set_mask_and_switch(struct device *dev, u64 dma_mask)
{
	if (!dev->dma_mask || !dma_supported(dev, dma_mask))
		return -EIO;

	if (dma_mask == DMA_BIT_MASK(64) &&
		cell_iommu_get_fixed_address(dev) != OF_BAD_ADDR)
	{
		dev_dbg(dev, "iommu: 64-bit OK, using fixed ops\n");
		set_dma_ops(dev, &dma_iommu_fixed_ops);
	} else {
		dev_dbg(dev, "iommu: not 64-bit, using default ops\n");
		set_dma_ops(dev, get_pci_dma_ops());
	}

	cell_dma_dev_setup(dev);

	*dev->dma_mask = dma_mask;

	return 0;
}

static void cell_dma_dev_setup_fixed(struct device *dev)
{
	u64 addr;

	addr = cell_iommu_get_fixed_address(dev) + dma_iommu_fixed_base;
	set_dma_offset(dev, addr);

	dev_dbg(dev, "iommu: fixed addr = %llx\n", addr);
}

static void insert_16M_pte(unsigned long addr, unsigned long *ptab,
			   unsigned long base_pte)
{
	unsigned long segment, offset;

	segment = addr >> IO_SEGMENT_SHIFT;
	offset = (addr >> 24) - (segment << IO_PAGENO_BITS(24));
	ptab = ptab + (segment * (1 << 12) / sizeof(unsigned long));

	pr_debug("iommu: addr %lx ptab %p segment %lx offset %lx\n",
		  addr, ptab, segment, offset);

	ptab[offset] = base_pte | (__pa(addr) & CBE_IOPTE_RPN_Mask);
}

static void cell_iommu_setup_fixed_ptab(struct cbe_iommu *iommu,
	struct device_node *np, unsigned long dbase, unsigned long dsize,
	unsigned long fbase, unsigned long fsize)
{
	unsigned long base_pte, uaddr, ioaddr, *ptab;

	ptab = cell_iommu_alloc_ptab(iommu, fbase, fsize, dbase, dsize, 24);

	dma_iommu_fixed_base = fbase;

	pr_debug("iommu: mapping 0x%lx pages from 0x%lx\n", fsize, fbase);

	base_pte = CBE_IOPTE_PP_W | CBE_IOPTE_PP_R | CBE_IOPTE_M |
		(cell_iommu_get_ioid(np) & CBE_IOPTE_IOID_Mask);

	if (iommu_fixed_is_weak)
		pr_info("IOMMU: Using weak ordering for fixed mapping\n");
	else {
		pr_info("IOMMU: Using strong ordering for fixed mapping\n");
		base_pte |= CBE_IOPTE_SO_RW;
	}

	for (uaddr = 0; uaddr < fsize; uaddr += (1 << 24)) {
		/* Don't touch the dynamic region */
		ioaddr = uaddr + fbase;
		if (ioaddr >= dbase && ioaddr < (dbase + dsize)) {
			pr_debug("iommu: fixed/dynamic overlap, skipping\n");
			continue;
		}

		insert_16M_pte(uaddr, ptab, base_pte);
	}

	mb();
}

static int __init cell_iommu_fixed_mapping_init(void)
{
	unsigned long dbase, dsize, fbase, fsize, hbase, hend;
	struct cbe_iommu *iommu;
	struct device_node *np;

	/* The fixed mapping is only supported on axon machines */
	np = of_find_node_by_name(NULL, "axon");
	of_node_put(np);

	if (!np) {
		pr_debug("iommu: fixed mapping disabled, no axons found\n");
		return -1;
	}

	/* We must have dma-ranges properties for fixed mapping to work */
	np = of_find_node_with_property(NULL, "dma-ranges");
	of_node_put(np);

	if (!np) {
		pr_debug("iommu: no dma-ranges found, no fixed mapping\n");
		return -1;
	}

	/* The default setup is to have the fixed mapping sit after the
	 * dynamic region, so find the top of the largest IOMMU window
	 * on any axon, then add the size of RAM and that's our max value.
	 * If that is > 32GB we have to do other shennanigans.
	 */
	fbase = 0;
	for_each_node_by_name(np, "axon") {
		cell_iommu_get_window(np, &dbase, &dsize);
		fbase = max(fbase, dbase + dsize);
	}

	fbase = _ALIGN_UP(fbase, 1 << IO_SEGMENT_SHIFT);
	fsize = memblock_phys_mem_size();

	if ((fbase + fsize) <= 0x800000000ul)
		hbase = 0; /* use the device tree window */
	else {
		/* If we're over 32 GB we need to cheat. We can't map all of
		 * RAM with the fixed mapping, and also fit the dynamic
		 * region. So try to place the dynamic region where the hash
		 * table sits, drivers never need to DMA to it, we don't
		 * need a fixed mapping for that area.
		 */
		if (!htab_address) {
			pr_debug("iommu: htab is NULL, on LPAR? Huh?\n");
			return -1;
		}
		hbase = __pa(htab_address);
		hend  = hbase + htab_size_bytes;

		/* The window must start and end on a segment boundary */
		if ((hbase != _ALIGN_UP(hbase, 1 << IO_SEGMENT_SHIFT)) ||
		    (hend != _ALIGN_UP(hend, 1 << IO_SEGMENT_SHIFT))) {
			pr_debug("iommu: hash window not segment aligned\n");
			return -1;
		}

		/* Check the hash window fits inside the real DMA window */
		for_each_node_by_name(np, "axon") {
			cell_iommu_get_window(np, &dbase, &dsize);

			if (hbase < dbase || (hend > (dbase + dsize))) {
				pr_debug("iommu: hash window doesn't fit in"
					 "real DMA window\n");
				return -1;
			}
		}

		fbase = 0;
	}

	/* Setup the dynamic regions */
	for_each_node_by_name(np, "axon") {
		iommu = cell_iommu_alloc(np);
		BUG_ON(!iommu);

		if (hbase == 0)
			cell_iommu_get_window(np, &dbase, &dsize);
		else {
			dbase = hbase;
			dsize = htab_size_bytes;
		}

		printk(KERN_DEBUG "iommu: node %d, dynamic window 0x%lx-0x%lx "
			"fixed window 0x%lx-0x%lx\n", iommu->nid, dbase,
			 dbase + dsize, fbase, fbase + fsize);

		cell_iommu_setup_stab(iommu, dbase, dsize, fbase, fsize);
		iommu->ptab = cell_iommu_alloc_ptab(iommu, dbase, dsize, 0, 0,
						    IOMMU_PAGE_SHIFT_4K);
		cell_iommu_setup_fixed_ptab(iommu, np, dbase, dsize,
					     fbase, fsize);
		cell_iommu_enable_hardware(iommu);
		cell_iommu_setup_window(iommu, np, dbase, dsize, 0);
	}

	dma_iommu_ops.set_dma_mask = dma_set_mask_and_switch;
	set_pci_dma_ops(&dma_iommu_ops);

	return 0;
}

static int iommu_fixed_disabled;

static int __init setup_iommu_fixed(char *str)
{
	struct device_node *pciep;

	if (strcmp(str, "off") == 0)
		iommu_fixed_disabled = 1;

	/* If we can find a pcie-endpoint in the device tree assume that
	 * we're on a triblade or a CAB so by default the fixed mapping
	 * should be set to be weakly ordered; but only if the boot
	 * option WASN'T set for strong ordering
	 */
	pciep = of_find_node_by_type(NULL, "pcie-endpoint");

	if (strcmp(str, "weak") == 0 || (pciep && strcmp(str, "strong") != 0))
		iommu_fixed_is_weak = DMA_ATTR_WEAK_ORDERING;

	of_node_put(pciep);

	return 1;
}
__setup("iommu_fixed=", setup_iommu_fixed);

static u64 cell_dma_get_required_mask(struct device *dev)
{
	const struct dma_map_ops *dma_ops;

	if (!dev->dma_mask)
		return 0;

	if (!iommu_fixed_disabled &&
			cell_iommu_get_fixed_address(dev) != OF_BAD_ADDR)
		return DMA_BIT_MASK(64);

	dma_ops = get_dma_ops(dev);
	if (dma_ops->get_required_mask)
		return dma_ops->get_required_mask(dev);

	WARN_ONCE(1, "no get_required_mask in %p ops", dma_ops);

	return DMA_BIT_MASK(64);
}

static int __init cell_iommu_init(void)
{
	struct device_node *np;

	/* If IOMMU is disabled or we have little enough RAM to not need
	 * to enable it, we setup a direct mapping.
	 *
	 * Note: should we make sure we have the IOMMU actually disabled ?
	 */
	if (iommu_is_off ||
	    (!iommu_force_on && memblock_end_of_DRAM() <= 0x80000000ull))
		if (cell_iommu_init_disabled() == 0)
			goto bail;

	/* Setup various callbacks */
	cell_pci_controller_ops.dma_dev_setup = cell_pci_dma_dev_setup;
	ppc_md.dma_get_required_mask = cell_dma_get_required_mask;

	if (!iommu_fixed_disabled && cell_iommu_fixed_mapping_init() == 0)
		goto bail;

	/* Create an iommu for each /axon node.  */
	for_each_node_by_name(np, "axon") {
		if (np->parent == NULL || np->parent->parent != NULL)
			continue;
		cell_iommu_init_one(np, 0);
	}

	/* Create an iommu for each toplevel /pci-internal node for
	 * old hardware/firmware
	 */
	for_each_node_by_name(np, "pci-internal") {
		if (np->parent == NULL || np->parent->parent != NULL)
			continue;
		cell_iommu_init_one(np, SPIDER_DMA_OFFSET);
	}

	/* Setup default PCI iommu ops */
	set_pci_dma_ops(&dma_iommu_ops);

 bail:
	/* Register callbacks on OF platform device addition/removal
	 * to handle linking them to the right DMA operations
	 */
	bus_register_notifier(&platform_bus_type, &cell_of_bus_notifier);

	return 0;
}
machine_arch_initcall(cell, cell_iommu_init);
