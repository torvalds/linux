/*
 * IOMMU implementation for Cell Broadband Processor Architecture
 * We just establish a linear mapping at boot by setting all the
 * IOPT cache entries in the CPU.
 * The mapping functions should be identical to pci_direct_iommu, 
 * except for the handling of the high order bit that is required
 * by the Spider bridge. These should be split into a separate
 * file at the point where we get a different bridge chip.
 *
 * Copyright (C) 2005 IBM Deutschland Entwicklung GmbH,
 *			 Arnd Bergmann <arndb@de.ibm.com>
 *
 * Based on linear mapping
 * Copyright (C) 2003 Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/compiler.h>

#include <asm/sections.h>
#include <asm/iommu.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/abs_addr.h>
#include <asm/system.h>
#include <asm/ppc-pci.h>
#include <asm/udbg.h>

#include "iommu.h"

static inline unsigned long 
get_iopt_entry(unsigned long real_address, unsigned long ioid,
			 unsigned long prot)
{
	return (prot & IOPT_PROT_MASK)
	     | (IOPT_COHERENT)
	     | (IOPT_ORDER_VC)
	     | (real_address & IOPT_RPN_MASK)
	     | (ioid & IOPT_IOID_MASK);
}

typedef struct {
	unsigned long val;
} ioste;

static inline ioste
mk_ioste(unsigned long val)
{
	ioste ioste = { .val = val, };
	return ioste;
}

static inline ioste
get_iost_entry(unsigned long iopt_base, unsigned long io_address, unsigned page_size)
{
	unsigned long ps;
	unsigned long iostep;
	unsigned long nnpt;
	unsigned long shift;

	switch (page_size) {
	case 0x1000000:
		ps = IOST_PS_16M;
		nnpt = 0;  /* one page per segment */
		shift = 5; /* segment has 16 iopt entries */
		break;

	case 0x100000:
		ps = IOST_PS_1M;
		nnpt = 0;  /* one page per segment */
		shift = 1; /* segment has 256 iopt entries */
		break;

	case 0x10000:
		ps = IOST_PS_64K;
		nnpt = 0x07; /* 8 pages per io page table */
		shift = 0;   /* all entries are used */
		break;

	case 0x1000:
		ps = IOST_PS_4K;
		nnpt = 0x7f; /* 128 pages per io page table */
		shift = 0;   /* all entries are used */
		break;

	default: /* not a known compile time constant */
		{
			/* BUILD_BUG_ON() is not usable here */
			extern void __get_iost_entry_bad_page_size(void);
			__get_iost_entry_bad_page_size();
		}
		break;
	}

	iostep = iopt_base +
			 /* need 8 bytes per iopte */
			(((io_address / page_size * 8)
			 /* align io page tables on 4k page boundaries */
				 << shift) 
			 /* nnpt+1 pages go into each iopt */
				 & ~(nnpt << 12));

	nnpt++; /* this seems to work, but the documentation is not clear
		   about wether we put nnpt or nnpt-1 into the ioste bits.
		   In theory, this can't work for 4k pages. */
	return mk_ioste(IOST_VALID_MASK
			| (iostep & IOST_PT_BASE_MASK)
			| ((nnpt << 5) & IOST_NNPT_MASK)
			| (ps & IOST_PS_MASK));
}

/* compute the address of an io pte */
static inline unsigned long
get_ioptep(ioste iost_entry, unsigned long io_address)
{
	unsigned long iopt_base;
	unsigned long page_size;
	unsigned long page_number;
	unsigned long iopt_offset;

	iopt_base = iost_entry.val & IOST_PT_BASE_MASK;
	page_size = iost_entry.val & IOST_PS_MASK;

	/* decode page size to compute page number */
	page_number = (io_address & 0x0fffffff) >> (10 + 2 * page_size);
	/* page number is an offset into the io page table */
	iopt_offset = (page_number << 3) & 0x7fff8ul;
	return iopt_base + iopt_offset;
}

/* compute the tag field of the iopt cache entry */
static inline unsigned long
get_ioc_tag(ioste iost_entry, unsigned long io_address)
{
	unsigned long iopte = get_ioptep(iost_entry, io_address);

	return IOPT_VALID_MASK
	     | ((iopte & 0x00000000000000ff8ul) >> 3)
	     | ((iopte & 0x0000003fffffc0000ul) >> 9);
}

/* compute the hashed 6 bit index for the 4-way associative pte cache */
static inline unsigned long
get_ioc_hash(ioste iost_entry, unsigned long io_address)
{
	unsigned long iopte = get_ioptep(iost_entry, io_address);

	return ((iopte & 0x000000000000001f8ul) >> 3)
	     ^ ((iopte & 0x00000000000020000ul) >> 17)
	     ^ ((iopte & 0x00000000000010000ul) >> 15)
	     ^ ((iopte & 0x00000000000008000ul) >> 13)
	     ^ ((iopte & 0x00000000000004000ul) >> 11)
	     ^ ((iopte & 0x00000000000002000ul) >> 9)
	     ^ ((iopte & 0x00000000000001000ul) >> 7);
}

/* same as above, but pretend that we have a simpler 1-way associative
   pte cache with an 8 bit index */
static inline unsigned long
get_ioc_hash_1way(ioste iost_entry, unsigned long io_address)
{
	unsigned long iopte = get_ioptep(iost_entry, io_address);

	return ((iopte & 0x000000000000001f8ul) >> 3)
	     ^ ((iopte & 0x00000000000020000ul) >> 17)
	     ^ ((iopte & 0x00000000000010000ul) >> 15)
	     ^ ((iopte & 0x00000000000008000ul) >> 13)
	     ^ ((iopte & 0x00000000000004000ul) >> 11)
	     ^ ((iopte & 0x00000000000002000ul) >> 9)
	     ^ ((iopte & 0x00000000000001000ul) >> 7)
	     ^ ((iopte & 0x0000000000000c000ul) >> 8);
}

static inline ioste
get_iost_cache(void __iomem *base, unsigned long index)
{
	unsigned long __iomem *p = (base + IOC_ST_CACHE_DIR);
	return mk_ioste(in_be64(&p[index]));
}

static inline void
set_iost_cache(void __iomem *base, unsigned long index, ioste ste)
{
	unsigned long __iomem *p = (base + IOC_ST_CACHE_DIR);
	pr_debug("ioste %02lx was %016lx, store %016lx", index,
			get_iost_cache(base, index).val, ste.val);
	out_be64(&p[index], ste.val);
	pr_debug(" now %016lx\n", get_iost_cache(base, index).val);
}

static inline unsigned long
get_iopt_cache(void __iomem *base, unsigned long index, unsigned long *tag)
{
	unsigned long __iomem *tags = (void *)(base + IOC_PT_CACHE_DIR);
	unsigned long __iomem *p = (void *)(base + IOC_PT_CACHE_REG);	

	*tag = tags[index];
	rmb();
	return *p;
}

static inline void
set_iopt_cache(void __iomem *base, unsigned long index,
		 unsigned long tag, unsigned long val)
{
	unsigned long __iomem *tags = base + IOC_PT_CACHE_DIR;
	unsigned long __iomem *p = base + IOC_PT_CACHE_REG;

	out_be64(p, val);
	out_be64(&tags[index], tag);
}

static inline void
set_iost_origin(void __iomem *base)
{
	unsigned long __iomem *p = base + IOC_ST_ORIGIN;
	unsigned long origin = IOSTO_ENABLE | IOSTO_SW;

	pr_debug("iost_origin %016lx, now %016lx\n", in_be64(p), origin);
	out_be64(p, origin);
}

static inline void
set_iocmd_config(void __iomem *base)
{
	unsigned long __iomem *p = base + 0xc00;
	unsigned long conf;

	conf = in_be64(p);
	pr_debug("iost_conf %016lx, now %016lx\n", conf, conf | IOCMD_CONF_TE);
	out_be64(p, conf | IOCMD_CONF_TE);
}

static void enable_mapping(void __iomem *base, void __iomem *mmio_base)
{
	set_iocmd_config(base);
	set_iost_origin(mmio_base);
}

static void iommu_dev_setup_null(struct pci_dev *d) { }
static void iommu_bus_setup_null(struct pci_bus *b) { }

struct cell_iommu {
	unsigned long base;
	unsigned long mmio_base;
	void __iomem *mapped_base;
	void __iomem *mapped_mmio_base;
};

static struct cell_iommu cell_iommus[NR_CPUS];

/* initialize the iommu to support a simple linear mapping
 * for each DMA window used by any device. For now, we
 * happen to know that there is only one DMA window in use,
 * starting at iopt_phys_offset. */
static void cell_do_map_iommu(struct cell_iommu *iommu,
			      unsigned int ioid,
			      unsigned long map_start,
			      unsigned long map_size)
{
	unsigned long io_address, real_address;
	void __iomem *ioc_base, *ioc_mmio_base;
	ioste ioste;
	unsigned long index;

	/* we pretend the io page table was at a very high address */
	const unsigned long fake_iopt = 0x10000000000ul;
	const unsigned long io_page_size = 0x1000000; /* use 16M pages */
	const unsigned long io_segment_size = 0x10000000; /* 256M */

	ioc_base = iommu->mapped_base;
	ioc_mmio_base = iommu->mapped_mmio_base;

	for (real_address = 0, io_address = map_start;
	     io_address <= map_start + map_size;
	     real_address += io_page_size, io_address += io_page_size) {
		ioste = get_iost_entry(fake_iopt, io_address, io_page_size);
		if ((real_address % io_segment_size) == 0) /* segment start */
			set_iost_cache(ioc_mmio_base,
				       io_address >> 28, ioste);
		index = get_ioc_hash_1way(ioste, io_address);
		pr_debug("addr %08lx, index %02lx, ioste %016lx\n",
					 io_address, index, ioste.val);
		set_iopt_cache(ioc_mmio_base,
			get_ioc_hash_1way(ioste, io_address),
			get_ioc_tag(ioste, io_address),
			get_iopt_entry(real_address, ioid, IOPT_PROT_RW));
	}
}

static void iommu_devnode_setup(struct device_node *d)
{
	unsigned int *ioid;
	unsigned long *dma_window, map_start, map_size, token;
	struct cell_iommu *iommu;

	ioid = (unsigned int *)get_property(d, "ioid", NULL);
	if (!ioid)
		pr_debug("No ioid entry found !\n");

	dma_window = (unsigned long *)get_property(d, "ibm,dma-window", NULL);
	if (!dma_window)
		pr_debug("No ibm,dma-window entry found !\n");

	map_start = dma_window[1];
	map_size = dma_window[2];
	token = dma_window[0] >> 32;

	iommu = &cell_iommus[token];

	cell_do_map_iommu(iommu, *ioid, map_start, map_size);
}

static void iommu_bus_setup(struct pci_bus *b)
{
	struct device_node *d = (struct device_node *)b->sysdata;
	iommu_devnode_setup(d);
}


static int cell_map_iommu_hardcoded(int num_nodes)
{
	struct cell_iommu *iommu = NULL;

	pr_debug("%s(%d): Using hardcoded defaults\n", __FUNCTION__, __LINE__);

	/* node 0 */
	iommu = &cell_iommus[0];
	iommu->mapped_base = ioremap(0x20000511000, 0x1000);
	iommu->mapped_mmio_base = ioremap(0x20000510000, 0x1000);

	enable_mapping(iommu->mapped_base, iommu->mapped_mmio_base);

	cell_do_map_iommu(iommu, 0x048a,
			  0x20000000ul,0x20000000ul);

	if (num_nodes < 2)
		return 0;

	/* node 1 */
	iommu = &cell_iommus[1];
	iommu->mapped_base = ioremap(0x30000511000, 0x1000);
	iommu->mapped_mmio_base = ioremap(0x30000510000, 0x1000);

	enable_mapping(iommu->mapped_base, iommu->mapped_mmio_base);

	cell_do_map_iommu(iommu, 0x048a,
			  0x20000000,0x20000000ul);

	return 0;
}


static int cell_map_iommu(void)
{
	unsigned int num_nodes = 0, *node_id;
	unsigned long *base, *mmio_base;
	struct device_node *dn;
	struct cell_iommu *iommu = NULL;

	/* determine number of nodes (=iommus) */
	pr_debug("%s(%d): determining number of nodes...", __FUNCTION__, __LINE__);
	for(dn = of_find_node_by_type(NULL, "cpu");
	    dn;
	    dn = of_find_node_by_type(dn, "cpu")) {
		node_id = (unsigned int *)get_property(dn, "node-id", NULL);

		if (num_nodes < *node_id)
			num_nodes = *node_id;
		}

	num_nodes++;
	pr_debug("%i found.\n", num_nodes);

	/* map the iommu registers for each node */
	pr_debug("%s(%d): Looping through nodes\n", __FUNCTION__, __LINE__);
	for(dn = of_find_node_by_type(NULL, "cpu");
	    dn;
	    dn = of_find_node_by_type(dn, "cpu")) {

		node_id = (unsigned int *)get_property(dn, "node-id", NULL);
		base = (unsigned long *)get_property(dn, "ioc-cache", NULL);
		mmio_base = (unsigned long *)get_property(dn, "ioc-translation", NULL);

		if (!base || !mmio_base || !node_id)
			return cell_map_iommu_hardcoded(num_nodes);

		iommu = &cell_iommus[*node_id];
		iommu->base = *base;
		iommu->mmio_base = *mmio_base;

		iommu->mapped_base = ioremap(*base, 0x1000);
		iommu->mapped_mmio_base = ioremap(*mmio_base, 0x1000);

		enable_mapping(iommu->mapped_base,
			       iommu->mapped_mmio_base);

		/* everything else will be done in iommu_bus_setup */
	}

	return 1;
}

static void *cell_alloc_coherent(struct device *hwdev, size_t size,
			   dma_addr_t *dma_handle, gfp_t flag)
{
	void *ret;

	ret = (void *)__get_free_pages(flag, get_order(size));
	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_abs(ret) | CELL_DMA_VALID;
	}
	return ret;
}

static void cell_free_coherent(struct device *hwdev, size_t size,
				 void *vaddr, dma_addr_t dma_handle)
{
	free_pages((unsigned long)vaddr, get_order(size));
}

static dma_addr_t cell_map_single(struct device *hwdev, void *ptr,
		size_t size, enum dma_data_direction direction)
{
	return virt_to_abs(ptr) | CELL_DMA_VALID;
}

static void cell_unmap_single(struct device *hwdev, dma_addr_t dma_addr,
		size_t size, enum dma_data_direction direction)
{
}

static int cell_map_sg(struct device *hwdev, struct scatterlist *sg,
		int nents, enum dma_data_direction direction)
{
	int i;

	for (i = 0; i < nents; i++, sg++) {
		sg->dma_address = (page_to_phys(sg->page) + sg->offset)
					| CELL_DMA_VALID;
		sg->dma_length = sg->length;
	}

	return nents;
}

static void cell_unmap_sg(struct device *hwdev, struct scatterlist *sg,
		int nents, enum dma_data_direction direction)
{
}

static int cell_dma_supported(struct device *dev, u64 mask)
{
	return mask < 0x100000000ull;
}

static struct dma_mapping_ops cell_iommu_ops = {
	.alloc_coherent = cell_alloc_coherent,
	.free_coherent = cell_free_coherent,
	.map_single = cell_map_single,
	.unmap_single = cell_unmap_single,
	.map_sg = cell_map_sg,
	.unmap_sg = cell_unmap_sg,
	.dma_supported = cell_dma_supported,
};

void cell_init_iommu(void)
{
	int setup_bus = 0;

	if (of_find_node_by_path("/mambo")) {
		pr_info("Not using iommu on systemsim\n");
	} else {

		if (!(of_chosen &&
		      get_property(of_chosen, "linux,iommu-off", NULL)))
			setup_bus = cell_map_iommu();

		if (setup_bus) {
			pr_debug("%s: IOMMU mapping activated\n", __FUNCTION__);
			ppc_md.iommu_dev_setup = iommu_dev_setup_null;
			ppc_md.iommu_bus_setup = iommu_bus_setup;
		} else {
			pr_debug("%s: IOMMU mapping activated, "
				 "no device action necessary\n", __FUNCTION__);
			/* Direct I/O, IOMMU off */
			ppc_md.iommu_dev_setup = iommu_dev_setup_null;
			ppc_md.iommu_bus_setup = iommu_bus_setup_null;
		}
	}

	pci_dma_ops = cell_iommu_ops;
}
