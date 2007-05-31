/*
 *	linux/arch/alpha/kernel/pci_iommu.c
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/bootmem.h>
#include <linux/log2.h>

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
#define DEBUG_FORCEDAC 0

#define ISA_DMA_MASK		0x00ffffff

static inline unsigned long
mk_iommu_pte(unsigned long paddr)
{
	return (paddr >> (PAGE_SHIFT-1)) | 1;
}

static inline long
calc_npages(long bytes)
{
	return (bytes + PAGE_SIZE - 1) >> PAGE_SHIFT;
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

struct pci_iommu_arena *
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


#ifdef CONFIG_DISCONTIGMEM

        if (!NODE_DATA(nid) ||
            (NULL == (arena = alloc_bootmem_node(NODE_DATA(nid),
                                                 sizeof(*arena))))) {
                printk("%s: couldn't allocate arena from node %d\n"
                       "    falling back to system-wide allocation\n",
                       __FUNCTION__, nid);
                arena = alloc_bootmem(sizeof(*arena));
        }

        if (!NODE_DATA(nid) ||
            (NULL == (arena->ptes = __alloc_bootmem_node(NODE_DATA(nid),
                                                         mem_size,
                                                         align,
                                                         0)))) {
                printk("%s: couldn't allocate arena ptes from node %d\n"
                       "    falling back to system-wide allocation\n",
                       __FUNCTION__, nid);
                arena->ptes = __alloc_bootmem(mem_size, align, 0);
        }

#else /* CONFIG_DISCONTIGMEM */

	arena = alloc_bootmem(sizeof(*arena));
	arena->ptes = __alloc_bootmem(mem_size, align, 0);

#endif /* CONFIG_DISCONTIGMEM */

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

struct pci_iommu_arena *
iommu_arena_new(struct pci_controller *hose, dma_addr_t base,
		unsigned long window_size, unsigned long align)
{
	return iommu_arena_new_node(0, hose, base, window_size, align);
}

/* Must be called with the arena lock held */
static long
iommu_arena_find_pages(struct pci_iommu_arena *arena, long n, long mask)
{
	unsigned long *ptes;
	long i, p, nent;

	/* Search forward for the first mask-aligned sequence of N free ptes */
	ptes = arena->ptes;
	nent = arena->size >> PAGE_SHIFT;
	p = (arena->next_entry + mask) & ~mask;
	i = 0;
	while (i < n && p+i < nent) {
		if (ptes[p+i])
			p = (p + i + 1 + mask) & ~mask, i = 0;
		else
			i = i + 1;
	}

	if (i < n) {
                /* Reached the end.  Flush the TLB and restart the
                   search from the beginning.  */
		alpha_mv.mv_pci_tbi(arena->hose, 0, -1);

		p = 0, i = 0;
		while (i < n && p+i < nent) {
			if (ptes[p+i])
				p = (p + i + 1 + mask) & ~mask, i = 0;
			else
				i = i + 1;
		}

		if (i < n)
			return -1;
	}

	/* Success. It's the responsibility of the caller to mark them
	   in use before releasing the lock */
	return p;
}

static long
iommu_arena_alloc(struct pci_iommu_arena *arena, long n, unsigned int align)
{
	unsigned long flags;
	unsigned long *ptes;
	long i, p, mask;

	spin_lock_irqsave(&arena->lock, flags);

	/* Search for N empty ptes */
	ptes = arena->ptes;
	mask = max(align, arena->align_entry) - 1;
	p = iommu_arena_find_pages(arena, n, mask);
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

	paddr = __pa(cpu_addr);

#if !DEBUG_NODIRECT
	/* First check to see if we can use the direct map window.  */
	if (paddr + size + __direct_map_base - 1 <= max_dma
	    && paddr + size <= __direct_map_size) {
		ret = paddr + __direct_map_base;

		DBGA2("pci_map_single: [%p,%lx] -> direct %lx from %p\n",
		      cpu_addr, size, ret, __builtin_return_address(0));

		return ret;
	}
#endif

	/* Next, use DAC if selected earlier.  */
	if (dac_allowed) {
		ret = paddr + alpha_mv.pci_dac_offset;

		DBGA2("pci_map_single: [%p,%lx] -> DAC %lx from %p\n",
		      cpu_addr, size, ret, __builtin_return_address(0));

		return ret;
	}

	/* If the machine doesn't define a pci_tbi routine, we have to
	   assume it doesn't support sg mapping, and, since we tried to
	   use direct_map above, it now must be considered an error. */
	if (! alpha_mv.mv_pci_tbi) {
		static int been_here = 0; /* Only print the message once. */
		if (!been_here) {
		    printk(KERN_WARNING "pci_map_single: no HW sg\n");
		    been_here = 1;
		}
		return 0;
	}

	arena = hose->sg_pci;
	if (!arena || arena->dma_base + arena->size - 1 > max_dma)
		arena = hose->sg_isa;

	npages = calc_npages((paddr & ~PAGE_MASK) + size);

	/* Force allocation to 64KB boundary for ISA bridges. */
	if (pdev && pdev == isa_bridge)
		align = 8;
	dma_ofs = iommu_arena_alloc(arena, npages, align);
	if (dma_ofs < 0) {
		printk(KERN_WARNING "pci_map_single failed: "
		       "could not allocate dma page tables\n");
		return 0;
	}

	paddr &= PAGE_MASK;
	for (i = 0; i < npages; ++i, paddr += PAGE_SIZE)
		arena->ptes[i + dma_ofs] = mk_iommu_pte(paddr);

	ret = arena->dma_base + dma_ofs * PAGE_SIZE;
	ret += (unsigned long)cpu_addr & ~PAGE_MASK;

	DBGA2("pci_map_single: [%p,%lx] np %ld -> sg %lx from %p\n",
	      cpu_addr, size, npages, ret, __builtin_return_address(0));

	return ret;
}

dma_addr_t
pci_map_single(struct pci_dev *pdev, void *cpu_addr, size_t size, int dir)
{
	int dac_allowed; 

	if (dir == PCI_DMA_NONE)
		BUG();

	dac_allowed = pdev ? pci_dac_dma_supported(pdev, pdev->dma_mask) : 0; 
	return pci_map_single_1(pdev, cpu_addr, size, dac_allowed);
}
EXPORT_SYMBOL(pci_map_single);

dma_addr_t
pci_map_page(struct pci_dev *pdev, struct page *page, unsigned long offset,
	     size_t size, int dir)
{
	int dac_allowed;

	if (dir == PCI_DMA_NONE)
		BUG();

	dac_allowed = pdev ? pci_dac_dma_supported(pdev, pdev->dma_mask) : 0; 
	return pci_map_single_1(pdev, (char *)page_address(page) + offset, 
				size, dac_allowed);
}
EXPORT_SYMBOL(pci_map_page);

/* Unmap a single streaming mode DMA translation.  The DMA_ADDR and
   SIZE must match what was provided for in a previous pci_map_single
   call.  All other usages are undefined.  After this call, reads by
   the cpu to the buffer are guaranteed to see whatever the device
   wrote there.  */

void
pci_unmap_single(struct pci_dev *pdev, dma_addr_t dma_addr, size_t size,
		 int direction)
{
	unsigned long flags;
	struct pci_controller *hose = pdev ? pdev->sysdata : pci_isa_hose;
	struct pci_iommu_arena *arena;
	long dma_ofs, npages;

	if (direction == PCI_DMA_NONE)
		BUG();

	if (dma_addr >= __direct_map_base
	    && dma_addr < __direct_map_base + __direct_map_size) {
		/* Nothing to do.  */

		DBGA2("pci_unmap_single: direct [%lx,%lx] from %p\n",
		      dma_addr, size, __builtin_return_address(0));

		return;
	}

	if (dma_addr > 0xffffffff) {
		DBGA2("pci64_unmap_single: DAC [%lx,%lx] from %p\n",
		      dma_addr, size, __builtin_return_address(0));
		return;
	}

	arena = hose->sg_pci;
	if (!arena || dma_addr < arena->dma_base)
		arena = hose->sg_isa;

	dma_ofs = (dma_addr - arena->dma_base) >> PAGE_SHIFT;
	if (dma_ofs * PAGE_SIZE >= arena->size) {
		printk(KERN_ERR "Bogus pci_unmap_single: dma_addr %lx "
		       " base %lx size %x\n", dma_addr, arena->dma_base,
		       arena->size);
		return;
		BUG();
	}

	npages = calc_npages((dma_addr & ~PAGE_MASK) + size);

	spin_lock_irqsave(&arena->lock, flags);

	iommu_arena_free(arena, dma_ofs, npages);

        /* If we're freeing ptes above the `next_entry' pointer (they
           may have snuck back into the TLB since the last wrap flush),
           we need to flush the TLB before reallocating the latter.  */
	if (dma_ofs >= arena->next_entry)
		alpha_mv.mv_pci_tbi(hose, dma_addr, dma_addr + size - 1);

	spin_unlock_irqrestore(&arena->lock, flags);

	DBGA2("pci_unmap_single: sg [%lx,%lx] np %ld from %p\n",
	      dma_addr, size, npages, __builtin_return_address(0));
}
EXPORT_SYMBOL(pci_unmap_single);

void
pci_unmap_page(struct pci_dev *pdev, dma_addr_t dma_addr,
	       size_t size, int direction)
{
	pci_unmap_single(pdev, dma_addr, size, direction);
}
EXPORT_SYMBOL(pci_unmap_page);

/* Allocate and map kernel buffer using consistent mode DMA for PCI
   device.  Returns non-NULL cpu-view pointer to the buffer if
   successful and sets *DMA_ADDRP to the pci side dma address as well,
   else DMA_ADDRP is undefined.  */

void *
pci_alloc_consistent(struct pci_dev *pdev, size_t size, dma_addr_t *dma_addrp)
{
	void *cpu_addr;
	long order = get_order(size);
	gfp_t gfp = GFP_ATOMIC;

try_again:
	cpu_addr = (void *)__get_free_pages(gfp, order);
	if (! cpu_addr) {
		printk(KERN_INFO "pci_alloc_consistent: "
		       "get_free_pages failed from %p\n",
			__builtin_return_address(0));
		/* ??? Really atomic allocation?  Otherwise we could play
		   with vmalloc and sg if we can't find contiguous memory.  */
		return NULL;
	}
	memset(cpu_addr, 0, size);

	*dma_addrp = pci_map_single_1(pdev, cpu_addr, size, 0);
	if (*dma_addrp == 0) {
		free_pages((unsigned long)cpu_addr, order);
		if (alpha_mv.mv_pci_tbi || (gfp & GFP_DMA))
			return NULL;
		/* The address doesn't fit required mask and we
		   do not have iommu. Try again with GFP_DMA. */
		gfp |= GFP_DMA;
		goto try_again;
	}
		
	DBGA2("pci_alloc_consistent: %lx -> [%p,%x] from %p\n",
	      size, cpu_addr, *dma_addrp, __builtin_return_address(0));

	return cpu_addr;
}
EXPORT_SYMBOL(pci_alloc_consistent);

/* Free and unmap a consistent DMA buffer.  CPU_ADDR and DMA_ADDR must
   be values that were returned from pci_alloc_consistent.  SIZE must
   be the same as what as passed into pci_alloc_consistent.
   References to the memory and mappings associated with CPU_ADDR or
   DMA_ADDR past this call are illegal.  */

void
pci_free_consistent(struct pci_dev *pdev, size_t size, void *cpu_addr,
		    dma_addr_t dma_addr)
{
	pci_unmap_single(pdev, dma_addr, size, PCI_DMA_BIDIRECTIONAL);
	free_pages((unsigned long)cpu_addr, get_order(size));

	DBGA2("pci_free_consistent: [%x,%lx] from %p\n",
	      dma_addr, size, __builtin_return_address(0));
}
EXPORT_SYMBOL(pci_free_consistent);

/* Classify the elements of the scatterlist.  Write dma_address
   of each element with:
	0   : Followers all physically adjacent.
	1   : Followers all virtually adjacent.
	-1  : Not leader, physically adjacent to previous.
	-2  : Not leader, virtually adjacent to previous.
   Write dma_length of each leader with the combined lengths of
   the mergable followers.  */

#define SG_ENT_VIRT_ADDRESS(SG) (page_address((SG)->page) + (SG)->offset)
#define SG_ENT_PHYS_ADDRESS(SG) __pa(SG_ENT_VIRT_ADDRESS(SG))

static void
sg_classify(struct scatterlist *sg, struct scatterlist *end, int virt_ok)
{
	unsigned long next_paddr;
	struct scatterlist *leader;
	long leader_flag, leader_length;

	leader = sg;
	leader_flag = 0;
	leader_length = leader->length;
	next_paddr = SG_ENT_PHYS_ADDRESS(leader) + leader_length;

	for (++sg; sg < end; ++sg) {
		unsigned long addr, len;
		addr = SG_ENT_PHYS_ADDRESS(sg);
		len = sg->length;

		if (next_paddr == addr) {
			sg->dma_address = -1;
			leader_length += len;
		} else if (((next_paddr | addr) & ~PAGE_MASK) == 0 && virt_ok) {
			sg->dma_address = -2;
			leader_flag = 1;
			leader_length += len;
		} else {
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
sg_fill(struct scatterlist *leader, struct scatterlist *end,
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

		DBGA("    sg_fill: [%p,%lx] -> direct %lx\n",
		     __va(paddr), size, out->dma_address);

		return 0;
	}
#endif

	/* If physically contiguous and DAC is available, use it.  */
	if (leader->dma_address == 0 && dac_allowed) {
		out->dma_address = paddr + alpha_mv.pci_dac_offset;
		out->dma_length = size;

		DBGA("    sg_fill: [%p,%lx] -> DAC %lx\n",
		     __va(paddr), size, out->dma_address);

		return 0;
	}

	/* Otherwise, we'll use the iommu to make the pages virtually
	   contiguous.  */

	paddr &= ~PAGE_MASK;
	npages = calc_npages(paddr + size);
	dma_ofs = iommu_arena_alloc(arena, npages, 0);
	if (dma_ofs < 0) {
		/* If we attempted a direct map above but failed, die.  */
		if (leader->dma_address == 0)
			return -1;

		/* Otherwise, break up the remaining virtually contiguous
		   hunks into individual direct maps and retry.  */
		sg_classify(leader, end, 0);
		return sg_fill(leader, end, out, arena, max_dma, dac_allowed);
	}

	out->dma_address = arena->dma_base + dma_ofs*PAGE_SIZE + paddr;
	out->dma_length = size;

	DBGA("    sg_fill: [%p,%lx] -> sg %lx np %ld\n",
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
			sg++;
		}

		npages = calc_npages((paddr & ~PAGE_MASK) + size);

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

int
pci_map_sg(struct pci_dev *pdev, struct scatterlist *sg, int nents,
	   int direction)
{
	struct scatterlist *start, *end, *out;
	struct pci_controller *hose;
	struct pci_iommu_arena *arena;
	dma_addr_t max_dma;
	int dac_allowed;

	if (direction == PCI_DMA_NONE)
		BUG();

	dac_allowed = pdev ? pci_dac_dma_supported(pdev, pdev->dma_mask) : 0;

	/* Fast path single entry scatterlists.  */
	if (nents == 1) {
		sg->dma_length = sg->length;
		sg->dma_address
		  = pci_map_single_1(pdev, SG_ENT_VIRT_ADDRESS(sg),
				     sg->length, dac_allowed);
		return sg->dma_address != 0;
	}

	start = sg;
	end = sg + nents;

	/* First, prepare information about the entries.  */
	sg_classify(sg, end, alpha_mv.mv_pci_tbi != 0);

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
		if (sg_fill(sg, end, out, arena, max_dma, dac_allowed) < 0)
			goto error;
		out++;
	}

	/* Mark the end of the list for pci_unmap_sg.  */
	if (out < end)
		out->dma_length = 0;

	if (out - start == 0)
		printk(KERN_WARNING "pci_map_sg failed: no entries?\n");
	DBGA("pci_map_sg: %ld entries\n", out - start);

	return out - start;

 error:
	printk(KERN_WARNING "pci_map_sg failed: "
	       "could not allocate dma page tables\n");

	/* Some allocation failed while mapping the scatterlist
	   entries.  Unmap them now.  */
	if (out > start)
		pci_unmap_sg(pdev, start, out - start, direction);
	return 0;
}
EXPORT_SYMBOL(pci_map_sg);

/* Unmap a set of streaming mode DMA translations.  Again, cpu read
   rules concerning calls here are the same as for pci_unmap_single()
   above.  */

void
pci_unmap_sg(struct pci_dev *pdev, struct scatterlist *sg, int nents,
	     int direction)
{
	unsigned long flags;
	struct pci_controller *hose;
	struct pci_iommu_arena *arena;
	struct scatterlist *end;
	dma_addr_t max_dma;
	dma_addr_t fbeg, fend;

	if (direction == PCI_DMA_NONE)
		BUG();

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
		dma64_addr_t addr;
		size_t size;
		long npages, ofs;
		dma_addr_t tend;

		addr = sg->dma_address;
		size = sg->dma_length;
		if (!size)
			break;

		if (addr > 0xffffffff) {
			/* It's a DAC address -- nothing to do.  */
			DBGA("    (%ld) DAC [%lx,%lx]\n",
			      sg - end + nents, addr, size);
			continue;
		}

		if (addr >= __direct_map_base
		    && addr < __direct_map_base + __direct_map_size) {
			/* Nothing to do.  */
			DBGA("    (%ld) direct [%lx,%lx]\n",
			      sg - end + nents, addr, size);
			continue;
		}

		DBGA("    (%ld) sg [%lx,%lx]\n",
		     sg - end + nents, addr, size);

		npages = calc_npages((addr & ~PAGE_MASK) + size);
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
EXPORT_SYMBOL(pci_unmap_sg);


/* Return whether the given PCI device DMA address mask can be
   supported properly.  */

int
pci_dma_supported(struct pci_dev *pdev, u64 mask)
{
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
EXPORT_SYMBOL(pci_dma_supported);


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
	p = iommu_arena_find_pages(arena, pg_count, align_mask);
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
	   unsigned long *physaddrs)
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
		ptes[j] = mk_iommu_pte(physaddrs[i]);

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

/* True if the machine supports DAC addressing, and DEV can
   make use of it given MASK.  */

int
pci_dac_dma_supported(struct pci_dev *dev, u64 mask)
{
	dma64_addr_t dac_offset = alpha_mv.pci_dac_offset;
	int ok = 1;

	/* If this is not set, the machine doesn't support DAC at all.  */
	if (dac_offset == 0)
		ok = 0;

	/* The device has to be able to address our DAC bit.  */
	if ((dac_offset & dev->dma_mask) != dac_offset)
		ok = 0;

	/* If both conditions above are met, we are fine. */
	DBGA("pci_dac_dma_supported %s from %p\n",
	     ok ? "yes" : "no", __builtin_return_address(0));

	return ok;
}
EXPORT_SYMBOL(pci_dac_dma_supported);

dma64_addr_t
pci_dac_page_to_dma(struct pci_dev *pdev, struct page *page,
		    unsigned long offset, int direction)
{
	return (alpha_mv.pci_dac_offset
		+ __pa(page_address(page)) 
		+ (dma64_addr_t) offset);
}
EXPORT_SYMBOL(pci_dac_page_to_dma);

struct page *
pci_dac_dma_to_page(struct pci_dev *pdev, dma64_addr_t dma_addr)
{
	unsigned long paddr = (dma_addr & PAGE_MASK) - alpha_mv.pci_dac_offset;
	return virt_to_page(__va(paddr));
}
EXPORT_SYMBOL(pci_dac_dma_to_page);

unsigned long
pci_dac_dma_to_offset(struct pci_dev *pdev, dma64_addr_t dma_addr)
{
	return (dma_addr & ~PAGE_MASK);
}
EXPORT_SYMBOL(pci_dac_dma_to_offset);

/* Helper for generic DMA-mapping functions. */

struct pci_dev *
alpha_gendev_to_pci(struct device *dev)
{
	if (dev && dev->bus == &pci_bus_type)
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
EXPORT_SYMBOL(alpha_gendev_to_pci);

int
dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask ||
	    !pci_dma_supported(alpha_gendev_to_pci(dev), mask))
		return -EIO;

	*dev->dma_mask = mask;

	return 0;
}
EXPORT_SYMBOL(dma_set_mask);
