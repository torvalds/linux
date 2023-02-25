// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/arch/alpha/kernel/pci_iommu.c
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/gfp.h>
#include <linux/memblock.h>
#include <linux/export.h>
#include <linux/scatterlist.h>
#include <linux/log2.h>
#include <linux/dma-map-ops.h>
#include <linux/iommu-helper.h>

#include <asm/io.h>
#include <asm/hwrpb.h>

#include "proto.h"
#include "pci_impl.h"


#define DEBUG_ALLOC 0
#if DEBUG_ALLOC > 0
# define DBGA(args...)		printk(KERN_DEBUG args)
#else
# define DBGA(args...)
#endif
#if DEBUG_ALLOC > 1
# define DBGA2(args...)		printk(KERN_DEBUG args)
#else
# define DBGA2(args...)
#endif

#define DEBUG_NODIRECT 0

#define ISA_DMA_MASK		0x00ffffff

static inline unsigned long
mk_iommu_pte(unsigned long paddr)
{
	return (paddr >> (PAGE_SHIFT-1)) | 1;
}

/* Return the minimum of MAX or the first power of two larger
   than main memory.  */

unsigned long
size_for_memory(unsigned long max)
{
	unsigned long mem = max_low_pfn << PAGE_SHIFT;
	if (mem < max)
		max = roundup_pow_of_two(mem);
	return max;
}

struct pci_iommu_arena * __init
iommu_arena_new_node(int nid, struct pci_controller *hose, dma_addr_t base,
		     unsigned long window_size, unsigned long align)
{
	unsigned long mem_size;
	struct pci_iommu_arena *arena;

	mem_size = window_size / (PAGE_SIZE / sizeof(unsigned long));

	/* Note that the TLB lookup logic uses bitwise concatenation,
	   not addition, so the required arena alignment is based on
	   the size of the window.  Retain the align parameter so that
	   particular systems can over-align the arena.  */
	if (align < mem_size)
		align = mem_size;

	arena = memblock_alloc(sizeof(*arena), SMP_CACHE_BYTES);
	if (!arena)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      sizeof(*arena));
	arena->ptes = memblock_alloc(mem_size, align);
	if (!arena->ptes)
		panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
		      __func__, mem_size, align);

	spin_lock_init(&arena->lock);
	arena->hose = hose;
	arena->dma_base = base;
	arena->size = window_size;
	arena->next_entry = 0;

	/* Align allocations to a multiple of a page size.  Not needed
	   unless there are chip bugs.  */
	arena->align_entry = 1;

	return arena;
}

struct pci_iommu_arena * __init
iommu_arena_new(struct pci_controller *hose, dma_addr_t base,
		unsigned long window_size, unsigned long align)
{
	return iommu_arena_new_node(0, hose, base, window_size, align);
}

/* Must be called with the arena lock held */
static long
iommu_arena_find_pages(struct device *dev, struct pci_iommu_arena *arena,
		       long n, long mask)
{
	unsigned long *ptes;
	long i, p, nent;
	int pass = 0;
	unsigned long base;
	unsigned long boundary_size;

	base = arena->dma_base >> PAGE_SHIFT;
	boundary_size = dma_get_seg_boundary_nr_pages(dev, PAGE_SHIFT);

	/* Search forward for the first mask-aligned sequence of N free ptes */
	ptes = arena->ptes;
	nent = arena->size >> PAGE_SHIFT;
	p = ALIGN(arena->next_entry, mask + 1);
	i = 0;

again:
	while (i < n && p+i < nent) {
		if (!i && iommu_is_span_boundary(p, n, base, boundary_size)) {
			p = ALIGN(p + 1, mask + 1);
			goto again;
		}

		if (ptes[p+i]) {
			p = ALIGN(p + i + 1, mask + 1);
			i = 0;
		} else {
			i = i + 1;
		}
	}

	if (i < n) {
		if (pass < 1) {
			/*
			 * Reached the end.  Flush the TLB and restart
			 * the search from the beginning.
			*/
			alpha_mv.mv_pci_tbi(arena->hose, 0, -1);

			pass++;
			p = 0;
			i = 0;
			goto again;
		} else
			return -1;
	}

	/* Success. It's the responsibility of the caller to mark them
	   in use before releasing the lock */
	return p;
}

static long
iommu_arena_alloc(struct device *dev, struct pci_iommu_arena *arena, long n,
		  unsigned int align)
{
	unsigned long flags;
	unsigned long *ptes;
	long i, p, mask;

	spin_lock_irqsave(&arena->lock, flags);

	/* Search for N empty ptes */
	ptes = arena->ptes;
	mask = max(align, arena->align_entry) - 1;
	p = iommu_arena_find_pages(dev, arena, n, mask);
	if (p < 0) {
		spin_unlock_irqrestore(&arena->lock, flags);
		return -1;
	}

	/* Success.  Mark them all in use, ie not zero and invalid
	   for the iommu tlb that could load them from under us.
	   The chip specific bits will fill this in with something
	   kosher when we return.  */
	for (i = 0; i < n; ++i)
		ptes[p+i] = IOMMU_INVALID_PTE;

	arena->next_entry = p + n;
	spin_unlock_irqrestore(&arena->lock, flags);

	return p;
}

static void
iommu_arena_free(struct pci_iommu_arena *arena, long ofs, long n)
{
	unsigned long *p;
	long i;

	p = arena->ptes + ofs;
	for (i = 0; i < n; ++i)
		p[i] = 0;
}

/*
 * True if the machine supports DAC addressing, and DEV can
 * make use of it given MASK.
 */
static int pci_dac_dma_supported(struct pci_dev *dev, u64 mask)
{
	dma_addr_t dac_offset = alpha_mv.pci_dac_offset;
	int ok = 1;

	/* If this is not set, the machine doesn't support DAC at all.  */
	if (dac_offset == 0)
		ok = 0;

	/* The device has to be able to address our DAC bit.  */
	if ((dac_offset & dev->dma_mask) != dac_offset)
		ok = 0;

	/* If both conditions above are met, we are fine. */
	DBGA("pci_dac_dma_supported %s from %ps\n",
	     ok ? "yes" : "no", __builtin_return_address(0));

	return ok;
}

/* Map a single buffer of the indicated size for PCI DMA in streaming
   mode.  The 32-bit PCI bus mastering address to use is returned.
   Once the device is given the dma address, the device owns this memory
   until either pci_unmap_single or pci_dma_sync_single is performed.  */

static dma_addr_t
pci_map_single_1(struct pci_dev *pdev, void *cpu_addr, size_t size,
		 int dac_allowed)
{
	struct pci_controller *hose = pdev ? pdev->sysdata : pci_isa_hose;
	dma_addr_t max_dma = pdev ? pdev->dma_mask : ISA_DMA_MASK;
	struct pci_iommu_arena *arena;
	long npages, dma_ofs, i;
	unsigned long paddr;
	dma_addr_t ret;
	unsigned int align = 0;
	struct device *dev = pdev ? &pdev->dev : NULL;

	paddr = __pa(cpu_addr);

#if !DEBUG_NODIRECT
	/* First check to see if we can use the direct map window.  */
	if (paddr + size + __direct_map_base - 1 <= max_dma
	    && paddr + size <= __direct_map_size) {
		ret = paddr + __direct_map_base;

		DBGA2("pci_map_single: [%p,%zx] -> direct %llx from %ps\n",
		      cpu_addr, size, ret, __builtin_return_address(0));

		return ret;
	}
#endif

	/* Next, use DAC if selected earlier.  */
	if (dac_allowed) {
		ret = paddr + alpha_mv.pci_dac_offset;

		DBGA2("pci_map_single: [%p,%zx] -> DAC %llx from %ps\n",
		      cpu_addr, size, ret, __builtin_return_address(0));

		return ret;
	}

	/* If the machine doesn't define a pci_tbi routine, we have to
	   assume it doesn't support sg mapping, and, since we tried to
	   use direct_map above, it now must be considered an error. */
	if (! alpha_mv.mv_pci_tbi) {
		printk_once(KERN_WARNING "pci_map_single: no HW sg\n");
		return DMA_MAPPING_ERROR;
	}

	arena = hose->sg_pci;
	if (!arena || arena->dma_base + arena->size - 1 > max_dma)
		arena = hose->sg_isa;

	npages = iommu_num_pages(paddr, size, PAGE_SIZE);

	/* Force allocation to 64KB boundary for ISA bridges. */
	if (pdev && pdev == isa_bridge)
		align = 8;
	dma_ofs = iommu_arena_alloc(dev, arena, npages, align);
	if (dma_ofs < 0) {
		printk(KERN_WARNING "pci_map_single failed: "
		       "could not allocate dma page tables\n");
		return DMA_MAPPING_ERROR;
	}

	paddr &= PAGE_MASK;
	for (i = 0; i < npages; ++i, paddr += PAGE_SIZE)
		arena->ptes[i + dma_ofs] = mk_iommu_pte(paddr);

	ret = arena->dma_base + dma_ofs * PAGE_SIZE;
	ret += (unsigned long)cpu_addr & ~PAGE_MASK;

	DBGA2("pci_map_single: [%p,%zx] np %ld -> sg %llx from %ps\n",
	      cpu_addr, size, npages, ret, __builtin_return_address(0));

	return ret;
}

/* Helper for generic DMA-mapping functions. */
static struct pci_dev *alpha_gendev_to_pci(struct device *dev)
{
	if (dev && dev_is_pci(dev))
		return to_pci_dev(dev);

	/* Assume that non-PCI devices asking for DMA are either ISA or EISA,
	   BUG() otherwise. */
	BUG_ON(!isa_bridge);

	/* Assume non-busmaster ISA DMA when dma_mask is not set (the ISA
	   bridge is bus master then). */
	if (!dev || !dev->dma_mask || !*dev->dma_mask)
		return isa_bridge;

	/* For EISA bus masters, return isa_bridge (it might have smaller
	   dma_mask due to wiring limitations). */
	if (*dev->dma_mask >= isa_bridge->dma_mask)
		return isa_bridge;

	/* This assumes ISA bus master with dma_mask 0xffffff. */
	return NULL;
}

static dma_addr_t alpha_pci_map_page(struct device *dev, struct page *page,
				     unsigned long offset, size_t size,
				     enum dma_data_direction dir,
				     unsigned long attrs)
{
	struct pci_dev *pdev = alpha_gendev_to_pci(dev);
	int dac_allowed;

	BUG_ON(dir == DMA_NONE);

	dac_allowed = pdev ? pci_dac_dma_supported(pdev, pdev->dma_mask) : 0; 
	return pci_map_single_1(pdev, (char *)page_address(page) + offset, 
				size, dac_allowed);
}

/* Unmap a single streaming mode DMA translation.  The DMA_ADDR and
   SIZE must match what was provided for in a previous pci_map_single
   call.  All other usages are undefined.  After this call, reads by
   the cpu to the buffer are guaranteed to see whatever the device
   wrote there.  */

static void alpha_pci_unmap_page(struct device *dev, dma_addr_t dma_addr,
				 size_t size, enum dma_data_direction dir,
				 unsigned long attrs)
{
	unsigned long flags;
	struct pci_dev *pdev = alpha_gendev_to_pci(dev);
	struct pci_controller *hose = pdev ? pdev->sysdata : pci_isa_hose;
	struct pci_iommu_arena *arena;
	long dma_ofs, npages;

	BUG_ON(dir == DMA_NONE);

	if (dma_addr >= __direct_map_base
	    && dma_addr < __direct_map_base + __direct_map_size) {
		/* Nothing to do.  */

		DBGA2("pci_unmap_single: direct [%llx,%zx] from %ps\n",
		      dma_addr, size, __builtin_return_address(0));

		return;
	}

	if (dma_addr > 0xffffffff) {
		DBGA2("pci64_unmap_single: DAC [%llx,%zx] from %ps\n",
		      dma_addr, size, __builtin_return_address(0));
		return;
	}

	arena = hose->sg_pci;
	if (!arena || dma_addr < arena->dma_base)
		arena = hose->sg_isa;

	dma_ofs = (dma_addr - arena->dma_base) >> PAGE_SHIFT;
	if (dma_ofs * PAGE_SIZE >= arena->size) {
		printk(KERN_ERR "Bogus pci_unmap_single: dma_addr %llx "
		       " base %llx size %x\n",
		       dma_addr, arena->dma_base, arena->size);
		return;
		BUG();
	}

	npages = iommu_num_pages(dma_addr, size, PAGE_SIZE);

	spin_lock_irqsave(&arena->lock, flags);

	iommu_arena_free(arena, dma_ofs, npages);

        /* If we're freeing ptes above the `next_entry' pointer (they
           may have snuck back into the TLB since the last wrap flush),
           we need to flush the TLB before reallocating the latter.  */
	if (dma_ofs >= arena->next_entry)
		alpha_mv.mv_pci_tbi(hose, dma_addr, dma_addr + size - 1);

	spin_unlock_irqrestore(&arena->lock, flags);

	DBGA2("pci_unmap_single: sg [%llx,%zx] np %ld from %ps\n",
	      dma_addr, size, npages, __builtin_return_address(0));
}

/* Allocate and map kernel buffer using consistent mode DMA for PCI
   device.  Returns non-NULL cpu-view pointer to the buffer if
   successful and sets *DMA_ADDRP to the pci side dma address as well,
   else DMA_ADDRP is undefined.  */

static void *alpha_pci_alloc_coherent(struct device *dev, size_t size,
				      dma_addr_t *dma_addrp, gfp_t gfp,
				      unsigned long attrs)
{
	struct pci_dev *pdev = alpha_gendev_to_pci(dev);
	void *cpu_addr;
	long order = get_order(size);

	gfp &= ~GFP_DMA;

try_again:
	cpu_addr = (void *)__get_free_pages(gfp | __GFP_ZERO, order);
	if (! cpu_addr) {
		printk(KERN_INFO "pci_alloc_consistent: "
		       "get_free_pages failed from %ps\n",
			__builtin_return_address(0));
		/* ??? Really atomic allocation?  Otherwise we could play
		   with vmalloc and sg if we can't find contiguous memory.  */
		return NULL;
	}
	memset(cpu_addr, 0, size);

	*dma_addrp = pci_map_single_1(pdev, cpu_addr, size, 0);
	if (*dma_addrp == DMA_MAPPING_ERROR) {
		free_pages((unsigned long)cpu_addr, order);
		if (alpha_mv.mv_pci_tbi || (gfp & GFP_DMA))
			return NULL;
		/* The address doesn't fit required mask and we
		   do not have iommu. Try again with GFP_DMA. */
		gfp |= GFP_DMA;
		goto try_again;
	}

	DBGA2("pci_alloc_consistent: %zx -> [%p,%llx] from %ps\n",
	      size, cpu_addr, *dma_addrp, __builtin_return_address(0));

	return cpu_addr;
}

/* Free and unmap a consistent DMA buffer.  CPU_ADDR and DMA_ADDR must
   be values that were returned from pci_alloc_consistent.  SIZE must
   be the same as what as passed into pci_alloc_consistent.
   References to the memory and mappings associated with CPU_ADDR or
   DMA_ADDR past this call are illegal.  */

static void alpha_pci_free_coherent(struct device *dev, size_t size,
				    void *cpu_addr, dma_addr_t dma_addr,
				    unsigned long attrs)
{
	struct pci_dev *pdev = alpha_gendev_to_pci(dev);
	dma_unmap_single(&pdev->dev, dma_addr, size, DMA_BIDIRECTIONAL);
	free_pages((unsigned long)cpu_addr, get_order(size));

	DBGA2("pci_free_consistent: [%llx,%zx] from %ps\n",
	      dma_addr, size, __builtin_return_address(0));
}

/* Classify the elements of the scatterlist.  Write dma_address
   of each element with:
	0   : Followers all physically adjacent.
	1   : Followers all virtually adjacent.
	-1  : Not leader, physically adjacent to previous.
	-2  : Not leader, virtually adjacent to previous.
   Write dma_length of each leader with the combined lengths of
   the mergable followers.  */

#define SG_ENT_VIRT_ADDRESS(SG) (sg_virt((SG)))
#define SG_ENT_PHYS_ADDRESS(SG) __pa(SG_ENT_VIRT_ADDRESS(SG))

static void
sg_classify(struct device *dev, struct scatterlist *sg, struct scatterlist *end,
	    int virt_ok)
{
	unsigned long next_paddr;
	struct scatterlist *leader;
	long leader_flag, leader_length;
	unsigned int max_seg_size;

	leader = sg;
	leader_flag = 0;
	leader_length = leader->length;
	next_paddr = SG_ENT_PHYS_ADDRESS(leader) + leader_length;

	/* we will not marge sg without device. */
	max_seg_size = dev ? dma_get_max_seg_size(dev) : 0;
	for (++sg; sg < end; ++sg) {
		unsigned long addr, len;
		addr = SG_ENT_PHYS_ADDRESS(sg);
		len = sg->length;

		if (leader_length + len > max_seg_size)
			goto new_segment;

		if (next_paddr == addr) {
			sg->dma_address = -1;
			leader_length += len;
		} else if (((next_paddr | addr) & ~PAGE_MASK) == 0 && virt_ok) {
			sg->dma_address = -2;
			leader_flag = 1;
			leader_length += len;
		} else {
new_segment:
			leader->dma_address = leader_flag;
			leader->dma_length = leader_length;
			leader = sg;
			leader_flag = 0;
			leader_length = len;
		}

		next_paddr = addr + len;
	}

	leader->dma_address = leader_flag;
	leader->dma_length = leader_length;
}

/* Given a scatterlist leader, choose an allocation method and fill
   in the blanks.  */

static int
sg_fill(struct device *dev, struct scatterlist *leader, struct scatterlist *end,
	struct scatterlist *out, struct pci_iommu_arena *arena,
	dma_addr_t max_dma, int dac_allowed)
{
	unsigned long paddr = SG_ENT_PHYS_ADDRESS(leader);
	long size = leader->dma_length;
	struct scatterlist *sg;
	unsigned long *ptes;
	long npages, dma_ofs, i;

#if !DEBUG_NODIRECT
	/* If everything is physically contiguous, and the addresses
	   fall into the direct-map window, use it.  */
	if (leader->dma_address == 0
	    && paddr + size + __direct_map_base - 1 <= max_dma
	    && paddr + size <= __direct_map_size) {
		out->dma_address = paddr + __direct_map_base;
		out->dma_length = size;

		DBGA("    sg_fill: [%p,%lx] -> direct %llx\n",
		     __va(paddr), size, out->dma_address);

		return 0;
	}
#endif

	/* If physically contiguous and DAC is available, use it.  */
	if (leader->dma_address == 0 && dac_allowed) {
		out->dma_address = paddr + alpha_mv.pci_dac_offset;
		out->dma_length = size;

		DBGA("    sg_fill: [%p,%lx] -> DAC %llx\n",
		     __va(paddr), size, out->dma_address);

		return 0;
	}

	/* Otherwise, we'll use the iommu to make the pages virtually
	   contiguous.  */

	paddr &= ~PAGE_MASK;
	npages = iommu_num_pages(paddr, size, PAGE_SIZE);
	dma_ofs = iommu_arena_alloc(dev, arena, npages, 0);
	if (dma_ofs < 0) {
		/* If we attempted a direct map above but failed, die.  */
		if (leader->dma_address == 0)
			return -1;

		/* Otherwise, break up the remaining virtually contiguous
		   hunks into individual direct maps and retry.  */
		sg_classify(dev, leader, end, 0);
		return sg_fill(dev, leader, end, out, arena, max_dma, dac_allowed);
	}

	out->dma_address = arena->dma_base + dma_ofs*PAGE_SIZE + paddr;
	out->dma_length = size;

	DBGA("    sg_fill: [%p,%lx] -> sg %llx np %ld\n",
	     __va(paddr), size, out->dma_address, npages);

	/* All virtually contiguous.  We need to find the length of each
	   physically contiguous subsegment to fill in the ptes.  */
	ptes = &arena->ptes[dma_ofs];
	sg = leader;
	do {
#if DEBUG_ALLOC > 0
		struct scatterlist *last_sg = sg;
#endif

		size = sg->length;
		paddr = SG_ENT_PHYS_ADDRESS(sg);

		while (sg+1 < end && (int) sg[1].dma_address == -1) {
			size += sg[1].length;
			sg = sg_next(sg);
		}

		npages = iommu_num_pages(paddr, size, PAGE_SIZE);

		paddr &= PAGE_MASK;
		for (i = 0; i < npages; ++i, paddr += PAGE_SIZE)
			*ptes++ = mk_iommu_pte(paddr);

#if DEBUG_ALLOC > 0
		DBGA("    (%ld) [%p,%x] np %ld\n",
		     last_sg - leader, SG_ENT_VIRT_ADDRESS(last_sg),
		     last_sg->length, npages);
		while (++last_sg <= sg) {
			DBGA("        (%ld) [%p,%x] cont\n",
			     last_sg - leader, SG_ENT_VIRT_ADDRESS(last_sg),
			     last_sg->length);
		}
#endif
	} while (++sg < end && (int) sg->dma_address < 0);

	return 1;
}

static int alpha_pci_map_sg(struct device *dev, struct scatterlist *sg,
			    int nents, enum dma_data_direction dir,
			    unsigned long attrs)
{
	struct pci_dev *pdev = alpha_gendev_to_pci(dev);
	struct scatterlist *start, *end, *out;
	struct pci_controller *hose;
	struct pci_iommu_arena *arena;
	dma_addr_t max_dma;
	int dac_allowed;

	BUG_ON(dir == DMA_NONE);

	dac_allowed = dev ? pci_dac_dma_supported(pdev, pdev->dma_mask) : 0;

	/* Fast path single entry scatterlists.  */
	if (nents == 1) {
		sg->dma_length = sg->length;
		sg->dma_address
		  = pci_map_single_1(pdev, SG_ENT_VIRT_ADDRESS(sg),
				     sg->length, dac_allowed);
		if (sg->dma_address == DMA_MAPPING_ERROR)
			return -EIO;
		return 1;
	}

	start = sg;
	end = sg + nents;

	/* First, prepare information about the entries.  */
	sg_classify(dev, sg, end, alpha_mv.mv_pci_tbi != 0);

	/* Second, figure out where we're going to map things.  */
	if (alpha_mv.mv_pci_tbi) {
		hose = pdev ? pdev->sysdata : pci_isa_hose;
		max_dma = pdev ? pdev->dma_mask : ISA_DMA_MASK;
		arena = hose->sg_pci;
		if (!arena || arena->dma_base + arena->size - 1 > max_dma)
			arena = hose->sg_isa;
	} else {
		max_dma = -1;
		arena = NULL;
		hose = NULL;
	}

	/* Third, iterate over the scatterlist leaders and allocate
	   dma space as needed.  */
	for (out = sg; sg < end; ++sg) {
		if ((int) sg->dma_address < 0)
			continue;
		if (sg_fill(dev, sg, end, out, arena, max_dma, dac_allowed) < 0)
			goto error;
		out++;
	}

	/* Mark the end of the list for pci_unmap_sg.  */
	if (out < end)
		out->dma_length = 0;

	if (out - start == 0) {
		printk(KERN_WARNING "pci_map_sg failed: no entries?\n");
		return -ENOMEM;
	}
	DBGA("pci_map_sg: %ld entries\n", out - start);

	return out - start;

 error:
	printk(KERN_WARNING "pci_map_sg failed: "
	       "could not allocate dma page tables\n");

	/* Some allocation failed while mapping the scatterlist
	   entries.  Unmap them now.  */
	if (out > start)
		dma_unmap_sg(&pdev->dev, start, out - start, dir);
	return -ENOMEM;
}

/* Unmap a set of streaming mode DMA translations.  Again, cpu read
   rules concerning calls here are the same as for pci_unmap_single()
   above.  */

static void alpha_pci_unmap_sg(struct device *dev, struct scatterlist *sg,
			       int nents, enum dma_data_direction dir,
			       unsigned long attrs)
{
	struct pci_dev *pdev = alpha_gendev_to_pci(dev);
	unsigned long flags;
	struct pci_controller *hose;
	struct pci_iommu_arena *arena;
	struct scatterlist *end;
	dma_addr_t max_dma;
	dma_addr_t fbeg, fend;

	BUG_ON(dir == DMA_NONE);

	if (! alpha_mv.mv_pci_tbi)
		return;

	hose = pdev ? pdev->sysdata : pci_isa_hose;
	max_dma = pdev ? pdev->dma_mask : ISA_DMA_MASK;
	arena = hose->sg_pci;
	if (!arena || arena->dma_base + arena->size - 1 > max_dma)
		arena = hose->sg_isa;

	fbeg = -1, fend = 0;

	spin_lock_irqsave(&arena->lock, flags);

	for (end = sg + nents; sg < end; ++sg) {
		dma_addr_t addr;
		size_t size;
		long npages, ofs;
		dma_addr_t tend;

		addr = sg->dma_address;
		size = sg->dma_length;
		if (!size)
			break;

		if (addr > 0xffffffff) {
			/* It's a DAC address -- nothing to do.  */
			DBGA("    (%ld) DAC [%llx,%zx]\n",
			      sg - end + nents, addr, size);
			continue;
		}

		if (addr >= __direct_map_base
		    && addr < __direct_map_base + __direct_map_size) {
			/* Nothing to do.  */
			DBGA("    (%ld) direct [%llx,%zx]\n",
			      sg - end + nents, addr, size);
			continue;
		}

		DBGA("    (%ld) sg [%llx,%zx]\n",
		     sg - end + nents, addr, size);

		npages = iommu_num_pages(addr, size, PAGE_SIZE);
		ofs = (addr - arena->dma_base) >> PAGE_SHIFT;
		iommu_arena_free(arena, ofs, npages);

		tend = addr + size - 1;
		if (fbeg > addr) fbeg = addr;
		if (fend < tend) fend = tend;
	}

        /* If we're freeing ptes above the `next_entry' pointer (they
           may have snuck back into the TLB since the last wrap flush),
           we need to flush the TLB before reallocating the latter.  */
	if ((fend - arena->dma_base) >> PAGE_SHIFT >= arena->next_entry)
		alpha_mv.mv_pci_tbi(hose, fbeg, fend);

	spin_unlock_irqrestore(&arena->lock, flags);

	DBGA("pci_unmap_sg: %ld entries\n", nents - (end - sg));
}

/* Return whether the given PCI device DMA address mask can be
   supported properly.  */

static int alpha_pci_supported(struct device *dev, u64 mask)
{
	struct pci_dev *pdev = alpha_gendev_to_pci(dev);
	struct pci_controller *hose;
	struct pci_iommu_arena *arena;

	/* If there exists a direct map, and the mask fits either
	   the entire direct mapped space or the total system memory as
	   shifted by the map base */
	if (__direct_map_size != 0
	    && (__direct_map_base + __direct_map_size - 1 <= mask ||
		__direct_map_base + (max_low_pfn << PAGE_SHIFT) - 1 <= mask))
		return 1;

	/* Check that we have a scatter-gather arena that fits.  */
	hose = pdev ? pdev->sysdata : pci_isa_hose;
	arena = hose->sg_isa;
	if (arena && arena->dma_base + arena->size - 1 <= mask)
		return 1;
	arena = hose->sg_pci;
	if (arena && arena->dma_base + arena->size - 1 <= mask)
		return 1;

	/* As last resort try ZONE_DMA.  */
	if (!__direct_map_base && MAX_DMA_ADDRESS - IDENT_ADDR - 1 <= mask)
		return 1;

	return 0;
}


/*
 * AGP GART extensions to the IOMMU
 */
int
iommu_reserve(struct pci_iommu_arena *arena, long pg_count, long align_mask) 
{
	unsigned long flags;
	unsigned long *ptes;
	long i, p;

	if (!arena) return -EINVAL;

	spin_lock_irqsave(&arena->lock, flags);

	/* Search for N empty ptes.  */
	ptes = arena->ptes;
	p = iommu_arena_find_pages(NULL, arena, pg_count, align_mask);
	if (p < 0) {
		spin_unlock_irqrestore(&arena->lock, flags);
		return -1;
	}

	/* Success.  Mark them all reserved (ie not zero and invalid)
	   for the iommu tlb that could load them from under us.
	   They will be filled in with valid bits by _bind() */
	for (i = 0; i < pg_count; ++i)
		ptes[p+i] = IOMMU_RESERVED_PTE;

	arena->next_entry = p + pg_count;
	spin_unlock_irqrestore(&arena->lock, flags);

	return p;
}

int 
iommu_release(struct pci_iommu_arena *arena, long pg_start, long pg_count)
{
	unsigned long *ptes;
	long i;

	if (!arena) return -EINVAL;

	ptes = arena->ptes;

	/* Make sure they're all reserved first... */
	for(i = pg_start; i < pg_start + pg_count; i++)
		if (ptes[i] != IOMMU_RESERVED_PTE)
			return -EBUSY;

	iommu_arena_free(arena, pg_start, pg_count);
	return 0;
}

int
iommu_bind(struct pci_iommu_arena *arena, long pg_start, long pg_count, 
	   struct page **pages)
{
	unsigned long flags;
	unsigned long *ptes;
	long i, j;

	if (!arena) return -EINVAL;
	
	spin_lock_irqsave(&arena->lock, flags);

	ptes = arena->ptes;

	for(j = pg_start; j < pg_start + pg_count; j++) {
		if (ptes[j] != IOMMU_RESERVED_PTE) {
			spin_unlock_irqrestore(&arena->lock, flags);
			return -EBUSY;
		}
	}
		
	for(i = 0, j = pg_start; i < pg_count; i++, j++)
		ptes[j] = mk_iommu_pte(page_to_phys(pages[i]));

	spin_unlock_irqrestore(&arena->lock, flags);

	return 0;
}

int
iommu_unbind(struct pci_iommu_arena *arena, long pg_start, long pg_count)
{
	unsigned long *p;
	long i;

	if (!arena) return -EINVAL;

	p = arena->ptes + pg_start;
	for(i = 0; i < pg_count; i++)
		p[i] = IOMMU_RESERVED_PTE;

	return 0;
}

const struct dma_map_ops alpha_pci_ops = {
	.alloc			= alpha_pci_alloc_coherent,
	.free			= alpha_pci_free_coherent,
	.map_page		= alpha_pci_map_page,
	.unmap_page		= alpha_pci_unmap_page,
	.map_sg			= alpha_pci_map_sg,
	.unmap_sg		= alpha_pci_unmap_sg,
	.dma_supported		= alpha_pci_supported,
	.mmap			= dma_common_mmap,
	.get_sgtable		= dma_common_get_sgtable,
	.alloc_pages		= dma_common_alloc_pages,
	.free_pages		= dma_common_free_pages,
};
EXPORT_SYMBOL(alpha_pci_ops);
