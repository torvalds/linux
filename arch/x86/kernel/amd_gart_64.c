// SPDX-License-Identifier: GPL-2.0-only
/*
 * Dynamic DMA mapping support for AMD Hammer.
 *
 * Use the integrated AGP GART in the Hammer northbridge as an IOMMU for PCI.
 * This allows to use PCI devices that only support 32bit addresses on systems
 * with more than 4GB.
 *
 * See Documentation/core-api/dma-api-howto.rst for the interface specification.
 *
 * Copyright 2002 Andi Kleen, SuSE Labs.
 */

#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/agp_backend.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/topology.h>
#include <linux/interrupt.h>
#include <linux/bitmap.h>
#include <linux/kdebug.h>
#include <linux/scatterlist.h>
#include <linux/iommu-helper.h>
#include <linux/syscore_ops.h>
#include <linux/io.h>
#include <linux/gfp.h>
#include <linux/atomic.h>
#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <asm/mtrr.h>
#include <asm/proto.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/set_memory.h>
#include <asm/dma.h>
#include <asm/amd/nb.h>
#include <asm/x86_init.h>

static unsigned long iommu_bus_base;	/* GART remapping area (physical) */
static unsigned long iommu_size;	/* size of remapping area bytes */
static unsigned long iommu_pages;	/* .. and in pages */

static u32 *iommu_gatt_base;		/* Remapping table */

/*
 * If this is disabled the IOMMU will use an optimized flushing strategy
 * of only flushing when an mapping is reused. With it true the GART is
 * flushed for every mapping. Problem is that doing the lazy flush seems
 * to trigger bugs with some popular PCI cards, in particular 3ware (but
 * has been also seen with Qlogic at least).
 */
static int iommu_fullflush = 1;

/* Allocation bitmap for the remapping area: */
static DEFINE_SPINLOCK(iommu_bitmap_lock);
/* Guarded by iommu_bitmap_lock: */
static unsigned long *iommu_gart_bitmap;

static u32 gart_unmapped_entry;

#define GPTE_VALID    1
#define GPTE_COHERENT 2
#define GPTE_ENCODE(x) \
	(((x) & 0xfffff000) | (((x) >> 32) << 4) | GPTE_VALID | GPTE_COHERENT)
#define GPTE_DECODE(x) (((x) & 0xfffff000) | (((u64)(x) & 0xff0) << 28))

#ifdef CONFIG_AGP
#define AGPEXTERN extern
#else
#define AGPEXTERN
#endif

/* GART can only remap to physical addresses < 1TB */
#define GART_MAX_PHYS_ADDR	(1ULL << 40)

/* backdoor interface to AGP driver */
AGPEXTERN int agp_memory_reserved;
AGPEXTERN __u32 *agp_gatt_table;

static unsigned long next_bit;  /* protected by iommu_bitmap_lock */
static bool need_flush;		/* global flush state. set for each gart wrap */

static unsigned long alloc_iommu(struct device *dev, int size,
				 unsigned long align_mask)
{
	unsigned long offset, flags;
	unsigned long boundary_size;
	unsigned long base_index;

	base_index = ALIGN(iommu_bus_base & dma_get_seg_boundary(dev),
			   PAGE_SIZE) >> PAGE_SHIFT;
	boundary_size = dma_get_seg_boundary_nr_pages(dev, PAGE_SHIFT);

	spin_lock_irqsave(&iommu_bitmap_lock, flags);
	offset = iommu_area_alloc(iommu_gart_bitmap, iommu_pages, next_bit,
				  size, base_index, boundary_size, align_mask);
	if (offset == -1) {
		need_flush = true;
		offset = iommu_area_alloc(iommu_gart_bitmap, iommu_pages, 0,
					  size, base_index, boundary_size,
					  align_mask);
	}
	if (offset != -1) {
		next_bit = offset+size;
		if (next_bit >= iommu_pages) {
			next_bit = 0;
			need_flush = true;
		}
	}
	if (iommu_fullflush)
		need_flush = true;
	spin_unlock_irqrestore(&iommu_bitmap_lock, flags);

	return offset;
}

static void free_iommu(unsigned long offset, int size)
{
	unsigned long flags;

	spin_lock_irqsave(&iommu_bitmap_lock, flags);
	bitmap_clear(iommu_gart_bitmap, offset, size);
	if (offset >= next_bit)
		next_bit = offset + size;
	spin_unlock_irqrestore(&iommu_bitmap_lock, flags);
}

/*
 * Use global flush state to avoid races with multiple flushers.
 */
static void flush_gart(void)
{
	unsigned long flags;

	spin_lock_irqsave(&iommu_bitmap_lock, flags);
	if (need_flush) {
		amd_flush_garts();
		need_flush = false;
	}
	spin_unlock_irqrestore(&iommu_bitmap_lock, flags);
}

#ifdef CONFIG_IOMMU_LEAK
/* Debugging aid for drivers that don't free their IOMMU tables */
static void dump_leak(void)
{
	static int dump;

	if (dump)
		return;
	dump = 1;

	show_stack(NULL, NULL, KERN_ERR);
	debug_dma_dump_mappings(NULL);
}
#endif

static void iommu_full(struct device *dev, size_t size, int dir)
{
	/*
	 * Ran out of IOMMU space for this operation. This is very bad.
	 * Unfortunately the drivers cannot handle this operation properly.
	 * Return some non mapped prereserved space in the aperture and
	 * let the Northbridge deal with it. This will result in garbage
	 * in the IO operation. When the size exceeds the prereserved space
	 * memory corruption will occur or random memory will be DMAed
	 * out. Hopefully no network devices use single mappings that big.
	 */

	dev_err(dev, "PCI-DMA: Out of IOMMU space for %lu bytes\n", size);
#ifdef CONFIG_IOMMU_LEAK
	dump_leak();
#endif
}

static inline int
need_iommu(struct device *dev, unsigned long addr, size_t size)
{
	return force_iommu || !dma_capable(dev, addr, size, true);
}

static inline int
nonforced_iommu(struct device *dev, unsigned long addr, size_t size)
{
	return !dma_capable(dev, addr, size, true);
}

/* Map a single continuous physical area into the IOMMU.
 * Caller needs to check if the iommu is needed and flush.
 */
static dma_addr_t dma_map_area(struct device *dev, dma_addr_t phys_mem,
				size_t size, int dir, unsigned long align_mask)
{
	unsigned long npages = iommu_num_pages(phys_mem, size, PAGE_SIZE);
	unsigned long iommu_page;
	int i;

	if (unlikely(phys_mem + size > GART_MAX_PHYS_ADDR))
		return DMA_MAPPING_ERROR;

	iommu_page = alloc_iommu(dev, npages, align_mask);
	if (iommu_page == -1) {
		if (!nonforced_iommu(dev, phys_mem, size))
			return phys_mem;
		if (panic_on_overflow)
			panic("dma_map_area overflow %lu bytes\n", size);
		iommu_full(dev, size, dir);
		return DMA_MAPPING_ERROR;
	}

	for (i = 0; i < npages; i++) {
		iommu_gatt_base[iommu_page + i] = GPTE_ENCODE(phys_mem);
		phys_mem += PAGE_SIZE;
	}
	return iommu_bus_base + iommu_page*PAGE_SIZE + (phys_mem & ~PAGE_MASK);
}

/* Map a single area into the IOMMU */
static dma_addr_t gart_map_page(struct device *dev, struct page *page,
				unsigned long offset, size_t size,
				enum dma_data_direction dir,
				unsigned long attrs)
{
	unsigned long bus;
	phys_addr_t paddr = page_to_phys(page) + offset;

	if (!need_iommu(dev, paddr, size))
		return paddr;

	bus = dma_map_area(dev, paddr, size, dir, 0);
	flush_gart();

	return bus;
}

/*
 * Free a DMA mapping.
 */
static void gart_unmap_page(struct device *dev, dma_addr_t dma_addr,
			    size_t size, enum dma_data_direction dir,
			    unsigned long attrs)
{
	unsigned long iommu_page;
	int npages;
	int i;

	if (WARN_ON_ONCE(dma_addr == DMA_MAPPING_ERROR))
		return;

	/*
	 * This driver will not always use a GART mapping, but might have
	 * created a direct mapping instead.  If that is the case there is
	 * nothing to unmap here.
	 */
	if (dma_addr < iommu_bus_base ||
	    dma_addr >= iommu_bus_base + iommu_size)
		return;

	iommu_page = (dma_addr - iommu_bus_base)>>PAGE_SHIFT;
	npages = iommu_num_pages(dma_addr, size, PAGE_SIZE);
	for (i = 0; i < npages; i++) {
		iommu_gatt_base[iommu_page + i] = gart_unmapped_entry;
	}
	free_iommu(iommu_page, npages);
}

/*
 * Wrapper for pci_unmap_single working with scatterlists.
 */
static void gart_unmap_sg(struct device *dev, struct scatterlist *sg, int nents,
			  enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		if (!s->dma_length || !s->length)
			break;
		gart_unmap_page(dev, s->dma_address, s->dma_length, dir, 0);
	}
}

/* Fallback for dma_map_sg in case of overflow */
static int dma_map_sg_nonforce(struct device *dev, struct scatterlist *sg,
			       int nents, int dir)
{
	struct scatterlist *s;
	int i;

#ifdef CONFIG_IOMMU_DEBUG
	pr_debug("dma_map_sg overflow\n");
#endif

	for_each_sg(sg, s, nents, i) {
		unsigned long addr = sg_phys(s);

		if (nonforced_iommu(dev, addr, s->length)) {
			addr = dma_map_area(dev, addr, s->length, dir, 0);
			if (addr == DMA_MAPPING_ERROR) {
				if (i > 0)
					gart_unmap_sg(dev, sg, i, dir, 0);
				nents = 0;
				sg[0].dma_length = 0;
				break;
			}
		}
		s->dma_address = addr;
		s->dma_length = s->length;
	}
	flush_gart();

	return nents;
}

/* Map multiple scatterlist entries continuous into the first. */
static int __dma_map_cont(struct device *dev, struct scatterlist *start,
			  int nelems, struct scatterlist *sout,
			  unsigned long pages)
{
	unsigned long iommu_start = alloc_iommu(dev, pages, 0);
	unsigned long iommu_page = iommu_start;
	struct scatterlist *s;
	int i;

	if (iommu_start == -1)
		return -ENOMEM;

	for_each_sg(start, s, nelems, i) {
		unsigned long pages, addr;
		unsigned long phys_addr = s->dma_address;

		BUG_ON(s != start && s->offset);
		if (s == start) {
			sout->dma_address = iommu_bus_base;
			sout->dma_address += iommu_page*PAGE_SIZE + s->offset;
			sout->dma_length = s->length;
		} else {
			sout->dma_length += s->length;
		}

		addr = phys_addr;
		pages = iommu_num_pages(s->offset, s->length, PAGE_SIZE);
		while (pages--) {
			iommu_gatt_base[iommu_page] = GPTE_ENCODE(addr);
			addr += PAGE_SIZE;
			iommu_page++;
		}
	}
	BUG_ON(iommu_page - iommu_start != pages);

	return 0;
}

static inline int
dma_map_cont(struct device *dev, struct scatterlist *start, int nelems,
	     struct scatterlist *sout, unsigned long pages, int need)
{
	if (!need) {
		BUG_ON(nelems != 1);
		sout->dma_address = start->dma_address;
		sout->dma_length = start->length;
		return 0;
	}
	return __dma_map_cont(dev, start, nelems, sout, pages);
}

/*
 * DMA map all entries in a scatterlist.
 * Merge chunks that have page aligned sizes into a continuous mapping.
 */
static int gart_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		       enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *s, *ps, *start_sg, *sgmap;
	int need = 0, nextneed, i, out, start, ret;
	unsigned long pages = 0;
	unsigned int seg_size;
	unsigned int max_seg_size;

	if (nents == 0)
		return -EINVAL;

	out		= 0;
	start		= 0;
	start_sg	= sg;
	sgmap		= sg;
	seg_size	= 0;
	max_seg_size	= dma_get_max_seg_size(dev);
	ps		= NULL; /* shut up gcc */

	for_each_sg(sg, s, nents, i) {
		dma_addr_t addr = sg_phys(s);

		s->dma_address = addr;
		BUG_ON(s->length == 0);

		nextneed = need_iommu(dev, addr, s->length);

		/* Handle the previous not yet processed entries */
		if (i > start) {
			/*
			 * Can only merge when the last chunk ends on a
			 * page boundary and the new one doesn't have an
			 * offset.
			 */
			if (!iommu_merge || !nextneed || !need || s->offset ||
			    (s->length + seg_size > max_seg_size) ||
			    (ps->offset + ps->length) % PAGE_SIZE) {
				ret = dma_map_cont(dev, start_sg, i - start,
						   sgmap, pages, need);
				if (ret < 0)
					goto error;
				out++;

				seg_size	= 0;
				sgmap		= sg_next(sgmap);
				pages		= 0;
				start		= i;
				start_sg	= s;
			}
		}

		seg_size += s->length;
		need = nextneed;
		pages += iommu_num_pages(s->offset, s->length, PAGE_SIZE);
		ps = s;
	}
	ret = dma_map_cont(dev, start_sg, i - start, sgmap, pages, need);
	if (ret < 0)
		goto error;
	out++;
	flush_gart();
	if (out < nents) {
		sgmap = sg_next(sgmap);
		sgmap->dma_length = 0;
	}
	return out;

error:
	flush_gart();
	gart_unmap_sg(dev, sg, out, dir, 0);

	/* When it was forced or merged try again in a dumb way */
	if (force_iommu || iommu_merge) {
		out = dma_map_sg_nonforce(dev, sg, nents, dir);
		if (out > 0)
			return out;
	}
	if (panic_on_overflow)
		panic("dma_map_sg: overflow on %lu pages\n", pages);

	iommu_full(dev, pages << PAGE_SHIFT, dir);
	return ret;
}

/* allocate and map a coherent mapping */
static void *
gart_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_addr,
		    gfp_t flag, unsigned long attrs)
{
	void *vaddr;

	vaddr = dma_direct_alloc(dev, size, dma_addr, flag, attrs);
	if (!vaddr ||
	    !force_iommu || dev->coherent_dma_mask <= DMA_BIT_MASK(24))
		return vaddr;

	*dma_addr = dma_map_area(dev, virt_to_phys(vaddr), size,
			DMA_BIDIRECTIONAL, (1UL << get_order(size)) - 1);
	flush_gart();
	if (unlikely(*dma_addr == DMA_MAPPING_ERROR))
		goto out_free;
	return vaddr;
out_free:
	dma_direct_free(dev, size, vaddr, *dma_addr, attrs);
	return NULL;
}

/* free a coherent mapping */
static void
gart_free_coherent(struct device *dev, size_t size, void *vaddr,
		   dma_addr_t dma_addr, unsigned long attrs)
{
	gart_unmap_page(dev, dma_addr, size, DMA_BIDIRECTIONAL, 0);
	dma_direct_free(dev, size, vaddr, dma_addr, attrs);
}

static int no_agp;

static __init unsigned long check_iommu_size(unsigned long aper, u64 aper_size)
{
	unsigned long a;

	if (!iommu_size) {
		iommu_size = aper_size;
		if (!no_agp)
			iommu_size /= 2;
	}

	a = aper + iommu_size;
	iommu_size -= round_up(a, PMD_SIZE) - a;

	if (iommu_size < 64*1024*1024) {
		pr_warn("PCI-DMA: Warning: Small IOMMU %luMB."
			" Consider increasing the AGP aperture in BIOS\n",
			iommu_size >> 20);
	}

	return iommu_size;
}

static __init unsigned read_aperture(struct pci_dev *dev, u32 *size)
{
	unsigned aper_size = 0, aper_base_32, aper_order;
	u64 aper_base;

	pci_read_config_dword(dev, AMD64_GARTAPERTUREBASE, &aper_base_32);
	pci_read_config_dword(dev, AMD64_GARTAPERTURECTL, &aper_order);
	aper_order = (aper_order >> 1) & 7;

	aper_base = aper_base_32 & 0x7fff;
	aper_base <<= 25;

	aper_size = (32 * 1024 * 1024) << aper_order;
	if (aper_base + aper_size > 0x100000000UL || !aper_size)
		aper_base = 0;

	*size = aper_size;
	return aper_base;
}

static void enable_gart_translations(void)
{
	int i;

	if (!amd_nb_has_feature(AMD_NB_GART))
		return;

	for (i = 0; i < amd_nb_num(); i++) {
		struct pci_dev *dev = node_to_amd_nb(i)->misc;

		enable_gart_translation(dev, __pa(agp_gatt_table));
	}

	/* Flush the GART-TLB to remove stale entries */
	amd_flush_garts();
}

/*
 * If fix_up_north_bridges is set, the north bridges have to be fixed up on
 * resume in the same way as they are handled in gart_iommu_hole_init().
 */
static bool fix_up_north_bridges;
static u32 aperture_order;
static u32 aperture_alloc;

void set_up_gart_resume(u32 aper_order, u32 aper_alloc)
{
	fix_up_north_bridges = true;
	aperture_order = aper_order;
	aperture_alloc = aper_alloc;
}

static void gart_fixup_northbridges(void)
{
	int i;

	if (!fix_up_north_bridges)
		return;

	if (!amd_nb_has_feature(AMD_NB_GART))
		return;

	pr_info("PCI-DMA: Restoring GART aperture settings\n");

	for (i = 0; i < amd_nb_num(); i++) {
		struct pci_dev *dev = node_to_amd_nb(i)->misc;

		/*
		 * Don't enable translations just yet.  That is the next
		 * step.  Restore the pre-suspend aperture settings.
		 */
		gart_set_size_and_enable(dev, aperture_order);
		pci_write_config_dword(dev, AMD64_GARTAPERTUREBASE, aperture_alloc >> 25);
	}
}

static void gart_resume(void)
{
	pr_info("PCI-DMA: Resuming GART IOMMU\n");

	gart_fixup_northbridges();

	enable_gart_translations();
}

static struct syscore_ops gart_syscore_ops = {
	.resume		= gart_resume,

};

/*
 * Private Northbridge GATT initialization in case we cannot use the
 * AGP driver for some reason.
 */
static __init int init_amd_gatt(struct agp_kern_info *info)
{
	unsigned aper_size, gatt_size, new_aper_size;
	unsigned aper_base, new_aper_base;
	struct pci_dev *dev;
	void *gatt;
	int i;

	pr_info("PCI-DMA: Disabling AGP.\n");

	aper_size = aper_base = info->aper_size = 0;
	dev = NULL;
	for (i = 0; i < amd_nb_num(); i++) {
		dev = node_to_amd_nb(i)->misc;
		new_aper_base = read_aperture(dev, &new_aper_size);
		if (!new_aper_base)
			goto nommu;

		if (!aper_base) {
			aper_size = new_aper_size;
			aper_base = new_aper_base;
		}
		if (aper_size != new_aper_size || aper_base != new_aper_base)
			goto nommu;
	}
	if (!aper_base)
		goto nommu;

	info->aper_base = aper_base;
	info->aper_size = aper_size >> 20;

	gatt_size = (aper_size >> PAGE_SHIFT) * sizeof(u32);
	gatt = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					get_order(gatt_size));
	if (!gatt)
		panic("Cannot allocate GATT table");
	if (set_memory_uc((unsigned long)gatt, gatt_size >> PAGE_SHIFT))
		panic("Could not set GART PTEs to uncacheable pages");

	agp_gatt_table = gatt;

	register_syscore_ops(&gart_syscore_ops);

	flush_gart();

	pr_info("PCI-DMA: aperture base @ %x size %u KB\n",
	       aper_base, aper_size>>10);

	return 0;

 nommu:
	/* Should not happen anymore */
	pr_warn("PCI-DMA: More than 4GB of RAM and no IOMMU - falling back to iommu=soft.\n");
	return -1;
}

static const struct dma_map_ops gart_dma_ops = {
	.map_sg				= gart_map_sg,
	.unmap_sg			= gart_unmap_sg,
	.map_page			= gart_map_page,
	.unmap_page			= gart_unmap_page,
	.alloc				= gart_alloc_coherent,
	.free				= gart_free_coherent,
	.mmap				= dma_common_mmap,
	.get_sgtable			= dma_common_get_sgtable,
	.dma_supported			= dma_direct_supported,
	.get_required_mask		= dma_direct_get_required_mask,
	.alloc_pages_op			= dma_direct_alloc_pages,
	.free_pages			= dma_direct_free_pages,
};

static void gart_iommu_shutdown(void)
{
	struct pci_dev *dev;
	int i;

	/* don't shutdown it if there is AGP installed */
	if (!no_agp)
		return;

	if (!amd_nb_has_feature(AMD_NB_GART))
		return;

	for (i = 0; i < amd_nb_num(); i++) {
		u32 ctl;

		dev = node_to_amd_nb(i)->misc;
		pci_read_config_dword(dev, AMD64_GARTAPERTURECTL, &ctl);

		ctl &= ~GARTEN;

		pci_write_config_dword(dev, AMD64_GARTAPERTURECTL, ctl);
	}
}

int __init gart_iommu_init(void)
{
	struct agp_kern_info info;
	unsigned long iommu_start;
	unsigned long aper_base, aper_size;
	unsigned long start_pfn, end_pfn;
	unsigned long scratch;

	if (!amd_nb_has_feature(AMD_NB_GART))
		return 0;

#ifndef CONFIG_AGP_AMD64
	no_agp = 1;
#else
	/* Makefile puts PCI initialization via subsys_initcall first. */
	/* Add other AMD AGP bridge drivers here */
	no_agp = no_agp ||
		(agp_amd64_init() < 0) ||
		(agp_copy_info(agp_bridge, &info) < 0);
#endif

	if (no_iommu ||
	    (!force_iommu && max_pfn <= MAX_DMA32_PFN) ||
	    !gart_iommu_aperture ||
	    (no_agp && init_amd_gatt(&info) < 0)) {
		if (max_pfn > MAX_DMA32_PFN) {
			pr_warn("More than 4GB of memory but GART IOMMU not available.\n");
			pr_warn("falling back to iommu=soft.\n");
		}
		return 0;
	}

	/* need to map that range */
	aper_size	= info.aper_size << 20;
	aper_base	= info.aper_base;
	end_pfn		= (aper_base>>PAGE_SHIFT) + (aper_size>>PAGE_SHIFT);

	start_pfn = PFN_DOWN(aper_base);
	if (!pfn_range_is_mapped(start_pfn, end_pfn))
		init_memory_mapping(start_pfn<<PAGE_SHIFT, end_pfn<<PAGE_SHIFT,
				    PAGE_KERNEL);

	pr_info("PCI-DMA: using GART IOMMU.\n");
	iommu_size = check_iommu_size(info.aper_base, aper_size);
	iommu_pages = iommu_size >> PAGE_SHIFT;

	iommu_gart_bitmap = (void *) __get_free_pages(GFP_KERNEL | __GFP_ZERO,
						      get_order(iommu_pages/8));
	if (!iommu_gart_bitmap)
		panic("Cannot allocate iommu bitmap\n");

	pr_info("PCI-DMA: Reserving %luMB of IOMMU area in the AGP aperture\n",
	       iommu_size >> 20);

	agp_memory_reserved	= iommu_size;
	iommu_start		= aper_size - iommu_size;
	iommu_bus_base		= info.aper_base + iommu_start;
	iommu_gatt_base		= agp_gatt_table + (iommu_start>>PAGE_SHIFT);

	/*
	 * Unmap the IOMMU part of the GART. The alias of the page is
	 * always mapped with cache enabled and there is no full cache
	 * coherency across the GART remapping. The unmapping avoids
	 * automatic prefetches from the CPU allocating cache lines in
	 * there. All CPU accesses are done via the direct mapping to
	 * the backing memory. The GART address is only used by PCI
	 * devices.
	 */
	set_memory_np((unsigned long)__va(iommu_bus_base),
				iommu_size >> PAGE_SHIFT);
	/*
	 * Tricky. The GART table remaps the physical memory range,
	 * so the CPU won't notice potential aliases and if the memory
	 * is remapped to UC later on, we might surprise the PCI devices
	 * with a stray writeout of a cacheline. So play it sure and
	 * do an explicit, full-scale wbinvd() _after_ having marked all
	 * the pages as Not-Present:
	 */
	wbinvd();

	/*
	 * Now all caches are flushed and we can safely enable
	 * GART hardware.  Doing it early leaves the possibility
	 * of stale cache entries that can lead to GART PTE
	 * errors.
	 */
	enable_gart_translations();

	/*
	 * Try to workaround a bug (thanks to BenH):
	 * Set unmapped entries to a scratch page instead of 0.
	 * Any prefetches that hit unmapped entries won't get an bus abort
	 * then. (P2P bridge may be prefetching on DMA reads).
	 */
	scratch = get_zeroed_page(GFP_KERNEL);
	if (!scratch)
		panic("Cannot allocate iommu scratch page");
	gart_unmapped_entry = GPTE_ENCODE(__pa(scratch));

	flush_gart();
	dma_ops = &gart_dma_ops;
	x86_platform.iommu_shutdown = gart_iommu_shutdown;
	x86_swiotlb_enable = false;

	return 0;
}

void __init gart_parse_options(char *p)
{
	int arg;

	if (isdigit(*p) && get_option(&p, &arg))
		iommu_size = arg;
	if (!strncmp(p, "fullflush", 9))
		iommu_fullflush = 1;
	if (!strncmp(p, "nofullflush", 11))
		iommu_fullflush = 0;
	if (!strncmp(p, "noagp", 5))
		no_agp = 1;
	if (!strncmp(p, "noaperture", 10))
		fix_aperture = 0;
	/* duplicated from pci-dma.c */
	if (!strncmp(p, "force", 5))
		gart_iommu_aperture_allowed = 1;
	if (!strncmp(p, "allowed", 7))
		gart_iommu_aperture_allowed = 1;
	if (!strncmp(p, "memaper", 7)) {
		fallback_aper_force = 1;
		p += 7;
		if (*p == '=') {
			++p;
			if (get_option(&p, &arg))
				fallback_aper_order = arg;
		}
	}
}
