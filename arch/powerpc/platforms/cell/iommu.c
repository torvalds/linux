/*
 * IOMMU implementation for Cell Broadband Processor Architecture
 *
 * (C) Copyright IBM Corporation 2006
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

#include <asm/prom.h>
#include <asm/iommu.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/udbg.h>
#include <asm/of_platform.h>
#include <asm/lmb.h>
#include <asm/cell-regs.h>

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
#define IOC_IO_ExcpStat_SPF_P		0x4000000000000000ul
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

/* Page table entries */
#define IOPTE_PP_W		0x8000000000000000ul /* protection: write */
#define IOPTE_PP_R		0x4000000000000000ul /* protection: read */
#define IOPTE_M			0x2000000000000000ul /* coherency required */
#define IOPTE_SO_R		0x1000000000000000ul /* ordering: writes */
#define IOPTE_SO_RW             0x1800000000000000ul /* ordering: r & w */
#define IOPTE_RPN_Mask		0x07fffffffffff000ul /* RPN */
#define IOPTE_H			0x0000000000000800ul /* cache hint */
#define IOPTE_IOID_Mask		0x00000000000007fful /* ioid */


/* IOMMU sizing */
#define IO_SEGMENT_SHIFT	28
#define IO_PAGENO_BITS		(IO_SEGMENT_SHIFT - IOMMU_PAGE_SHIFT)

/* The high bit needs to be set on every DMA address */
#define SPIDER_DMA_OFFSET	0x80000000ul

struct iommu_window {
	struct list_head list;
	struct cbe_iommu *iommu;
	unsigned long offset;
	unsigned long size;
	unsigned long pte_offset;
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
	unsigned long __iomem *reg;
	unsigned long val;
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

static void tce_build_cell(struct iommu_table *tbl, long index, long npages,
		unsigned long uaddr, enum dma_data_direction direction)
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
	 * protection bit. "prot" is setup to be 3 fields of 4 bits apprended
	 * together for each of the 3 supported direction values. It is then
	 * shifted left so that the fields matching the desired direction
	 * lands on the appropriate bits, and other bits are masked out.
	 */
	const unsigned long prot = 0xc48;
	base_pte =
		((prot << (52 + 4 * direction)) & (IOPTE_PP_W | IOPTE_PP_R))
		| IOPTE_M | IOPTE_SO_RW | (window->ioid & IOPTE_IOID_Mask);
#else
	base_pte = IOPTE_PP_W | IOPTE_PP_R | IOPTE_M | IOPTE_SO_RW |
		(window->ioid & IOPTE_IOID_Mask);
#endif

	io_pte = (unsigned long *)tbl->it_base + (index - window->pte_offset);

	for (i = 0; i < npages; i++, uaddr += IOMMU_PAGE_SIZE)
		io_pte[i] = base_pte | (__pa(uaddr) & IOPTE_RPN_Mask);

	mb();

	invalidate_tce_cache(window->iommu, io_pte, npages);

	pr_debug("tce_build_cell(index=%lx,n=%lx,dir=%d,base_pte=%lx)\n",
		 index, npages, direction, base_pte);
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
	pte = IOPTE_PP_R | IOPTE_M | IOPTE_SO_RW | __pa(window->iommu->pad_page)
		| (window->ioid & IOPTE_IOID_Mask);
#endif

	io_pte = (unsigned long *)tbl->it_base + (index - window->pte_offset);

	for (i = 0; i < npages; i++)
		io_pte[i] = pte;

	mb();

	invalidate_tce_cache(window->iommu, io_pte, npages);
}

static irqreturn_t ioc_interrupt(int irq, void *data)
{
	unsigned long stat;
	struct cbe_iommu *iommu = data;

	stat = in_be64(iommu->xlate_regs + IOC_IO_ExcpStat);

	/* Might want to rate limit it */
	printk(KERN_ERR "iommu: DMA exception 0x%016lx\n", stat);
	printk(KERN_ERR "  V=%d, SPF=[%c%c], RW=%s, IOID=0x%04x\n",
	       !!(stat & IOC_IO_ExcpStat_V),
	       (stat & IOC_IO_ExcpStat_SPF_S) ? 'S' : ' ',
	       (stat & IOC_IO_ExcpStat_SPF_P) ? 'P' : ' ',
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

static void cell_iommu_setup_hardware(struct cbe_iommu *iommu, unsigned long size)
{
	struct page *page;
	int ret, i;
	unsigned long reg, segments, pages_per_segment, ptab_size, n_pte_pages;
	unsigned long xlate_base;
	unsigned int virq;

	if (cell_iommu_find_ioc(iommu->nid, &xlate_base))
		panic("%s: missing IOC register mappings for node %d\n",
		      __FUNCTION__, iommu->nid);

	iommu->xlate_regs = ioremap(xlate_base, IOC_Reg_Size);
	iommu->cmd_regs = iommu->xlate_regs + IOC_IOCmd_Offset;

	segments = size >> IO_SEGMENT_SHIFT;
	pages_per_segment = 1ull << IO_PAGENO_BITS;

	pr_debug("%s: iommu[%d]: segments: %lu, pages per segment: %lu\n",
			__FUNCTION__, iommu->nid, segments, pages_per_segment);

	/* set up the segment table */
	page = alloc_pages_node(iommu->nid, GFP_KERNEL, 0);
	BUG_ON(!page);
	iommu->stab = page_address(page);
	clear_page(iommu->stab);

	/* ... and the page tables. Since these are contiguous, we can treat
	 * the page tables as one array of ptes, like pSeries does.
	 */
	ptab_size = segments * pages_per_segment * sizeof(unsigned long);
	pr_debug("%s: iommu[%d]: ptab_size: %lu, order: %d\n", __FUNCTION__,
			iommu->nid, ptab_size, get_order(ptab_size));
	page = alloc_pages_node(iommu->nid, GFP_KERNEL, get_order(ptab_size));
	BUG_ON(!page);

	iommu->ptab = page_address(page);
	memset(iommu->ptab, 0, ptab_size);

	/* allocate a bogus page for the end of each mapping */
	page = alloc_pages_node(iommu->nid, GFP_KERNEL, 0);
	BUG_ON(!page);
	iommu->pad_page = page_address(page);
	clear_page(iommu->pad_page);

	/* number of pages needed for a page table */
	n_pte_pages = (pages_per_segment *
		       sizeof(unsigned long)) >> IOMMU_PAGE_SHIFT;

	pr_debug("%s: iommu[%d]: stab at %p, ptab at %p, n_pte_pages: %lu\n",
			__FUNCTION__, iommu->nid, iommu->stab, iommu->ptab,
			n_pte_pages);

	/* initialise the STEs */
	reg = IOSTE_V | ((n_pte_pages - 1) << 5);

	if (IOMMU_PAGE_SIZE == 0x1000)
		reg |= IOSTE_PS_4K;
	else if (IOMMU_PAGE_SIZE == 0x10000)
		reg |= IOSTE_PS_64K;
	else {
		extern void __unknown_page_size_error(void);
		__unknown_page_size_error();
	}

	pr_debug("Setting up IOMMU stab:\n");
	for (i = 0; i * (1ul << IO_SEGMENT_SHIFT) < size; i++) {
		iommu->stab[i] = reg |
			(__pa(iommu->ptab) + n_pte_pages * IOMMU_PAGE_SIZE * i);
		pr_debug("\t[%d] 0x%016lx\n", i, iommu->stab[i]);
	}

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
	BUG_ON(virq == NO_IRQ);

	ret = request_irq(virq, ioc_interrupt, IRQF_DISABLED,
			iommu->name, iommu);
	BUG_ON(ret);

	/* set the IOC segment table origin register (and turn on the iommu) */
	reg = IOC_IOST_Origin_E | __pa(iommu->stab) | IOC_IOST_Origin_HW;
	out_be64(iommu->xlate_regs + IOC_IOST_Origin, reg);
	in_be64(iommu->xlate_regs + IOC_IOST_Origin);

	/* turn on IO translation */
	reg = in_be64(iommu->cmd_regs + IOC_IOCmd_Cfg) | IOC_IOCmd_Cfg_TE;
	out_be64(iommu->cmd_regs + IOC_IOCmd_Cfg, reg);
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

static struct iommu_window * __init
cell_iommu_setup_window(struct cbe_iommu *iommu, struct device_node *np,
			unsigned long offset, unsigned long size,
			unsigned long pte_offset)
{
	struct iommu_window *window;
	const unsigned int *ioid;

	ioid = of_get_property(np, "ioid", NULL);
	if (ioid == NULL)
		printk(KERN_WARNING "iommu: missing ioid for %s using 0\n",
		       np->full_name);

	window = kmalloc_node(sizeof(*window), GFP_KERNEL, iommu->nid);
	BUG_ON(window == NULL);

	window->offset = offset;
	window->size = size;
	window->ioid = ioid ? *ioid : 0;
	window->iommu = iommu;
	window->pte_offset = pte_offset;

	window->table.it_blocksize = 16;
	window->table.it_base = (unsigned long)iommu->ptab;
	window->table.it_index = iommu->nid;
	window->table.it_offset = (offset >> IOMMU_PAGE_SHIFT) +
		window->pte_offset;
	window->table.it_size = size >> IOMMU_PAGE_SHIFT;

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
	__set_bit(0, window->table.it_map);
	tce_build_cell(&window->table, window->table.it_offset, 1,
		       (unsigned long)iommu->pad_page, DMA_TO_DEVICE);
	window->table.it_hint = window->table.it_blocksize;

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

static void cell_dma_dev_setup(struct device *dev)
{
	struct iommu_window *window;
	struct cbe_iommu *iommu;
	struct dev_archdata *archdata = &dev->archdata;

	/* If we run without iommu, no need to do anything */
	if (get_pci_dma_ops() == &dma_direct_ops)
		return;

	/* Current implementation uses the first window available in that
	 * node's iommu. We -might- do something smarter later though it may
	 * never be necessary
	 */
	iommu = cell_iommu_for_node(archdata->numa_node);
	if (iommu == NULL || list_empty(&iommu->windows)) {
		printk(KERN_ERR "iommu: missing iommu for %s (node %d)\n",
		       archdata->of_node ? archdata->of_node->full_name : "?",
		       archdata->numa_node);
		return;
	}
	window = list_entry(iommu->windows.next, struct iommu_window, list);

	archdata->dma_data = &window->table;
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
	dev->archdata.dma_ops = get_pci_dma_ops();

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
	const void *dma_window;
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

static void __init cell_iommu_init_one(struct device_node *np, unsigned long offset)
{
	struct cbe_iommu *iommu;
	unsigned long base, size;
	int nid, i;

	/* Get node ID */
	nid = of_node_to_nid(np);
	if (nid < 0) {
		printk(KERN_ERR "iommu: failed to get node for %s\n",
		       np->full_name);
		return;
	}
	pr_debug("iommu: setting up iommu for node %d (%s)\n",
		 nid, np->full_name);

	/* XXX todo: If we can have multiple windows on the same IOMMU, which
	 * isn't the case today, we probably want here to check wether the
	 * iommu for that node is already setup.
	 * However, there might be issue with getting the size right so let's
	 * ignore that for now. We might want to completely get rid of the
	 * multiple window support since the cell iommu supports per-page ioids
	 */

	if (cbe_nr_iommus >= NR_IOMMUS) {
		printk(KERN_ERR "iommu: too many IOMMUs detected ! (%s)\n",
		       np->full_name);
		return;
	}

	/* Init base fields */
	i = cbe_nr_iommus++;
	iommu = &iommus[i];
	iommu->stab = NULL;
	iommu->nid = nid;
	snprintf(iommu->name, sizeof(iommu->name), "iommu%d", i);
	INIT_LIST_HEAD(&iommu->windows);

	/* Obtain a window for it */
	cell_iommu_get_window(np, &base, &size);

	pr_debug("\ttranslating window 0x%lx...0x%lx\n",
		 base, base + size - 1);

	/* Initialize the hardware */
	cell_iommu_setup_hardware(iommu, size);

	/* Setup the iommu_table */
	cell_iommu_setup_window(iommu, np, base, size,
				offset >> IOMMU_PAGE_SHIFT);
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
		dma_direct_offset = SPIDER_DMA_OFFSET;

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
	if (np && size < lmb_end_of_DRAM()) {
		printk(KERN_WARNING "iommu: force-enabled, dma window"
		       " (%ldMB) smaller than total memory (%ldMB)\n",
		       size >> 20, lmb_end_of_DRAM() >> 20);
		return -ENODEV;
	}

	dma_direct_offset += base;

	printk("iommu: disabled, direct DMA offset is 0x%lx\n",
	       dma_direct_offset);

	return 0;
}

static int __init cell_iommu_init(void)
{
	struct device_node *np;

	if (!machine_is(cell))
		return -ENODEV;

	/* If IOMMU is disabled or we have little enough RAM to not need
	 * to enable it, we setup a direct mapping.
	 *
	 * Note: should we make sure we have the IOMMU actually disabled ?
	 */
	if (iommu_is_off ||
	    (!iommu_force_on && lmb_end_of_DRAM() <= 0x80000000ull))
		if (cell_iommu_init_disabled() == 0)
			goto bail;

	/* Setup various ppc_md. callbacks */
	ppc_md.pci_dma_dev_setup = cell_pci_dma_dev_setup;
	ppc_md.tce_build = tce_build_cell;
	ppc_md.tce_free = tce_free_cell;

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
	bus_register_notifier(&of_platform_bus_type, &cell_of_bus_notifier);

	return 0;
}
arch_initcall(cell_iommu_init);

