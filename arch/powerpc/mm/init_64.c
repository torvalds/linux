// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Dave Engebretsen <engebret@us.ibm.com>
 *      Rework for PPC64 port.
 */

#undef DEBUG

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/idr.h>
#include <linux/nodemask.h>
#include <linux/module.h>
#include <linux/poison.h>
#include <linux/memblock.h>
#include <linux/hugetlb.h>
#include <linux/slab.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <linux/memremap.h>

#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <linux/uaccess.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/tlb.h>
#include <asm/eeh.h>
#include <asm/processor.h>
#include <asm/mmzone.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/iommu.h>
#include <asm/vdso.h>

#include <mm/mmu_decl.h>

phys_addr_t memstart_addr = ~0;
EXPORT_SYMBOL_GPL(memstart_addr);
phys_addr_t kernstart_addr;
EXPORT_SYMBOL_GPL(kernstart_addr);

#ifdef CONFIG_SPARSEMEM_VMEMMAP
/*
 * Given an address within the vmemmap, determine the pfn of the page that
 * represents the start of the section it is within.  Note that we have to
 * do this by hand as the proffered address may not be correctly aligned.
 * Subtraction of non-aligned pointers produces undefined results.
 */
static unsigned long __meminit vmemmap_section_start(unsigned long page)
{
	unsigned long offset = page - ((unsigned long)(vmemmap));

	/* Return the pfn of the start of the section. */
	return (offset / sizeof(struct page)) & PAGE_SECTION_MASK;
}

/*
 * Check if this vmemmap page is already initialised.  If any section
 * which overlaps this vmemmap page is initialised then this page is
 * initialised already.
 */
static int __meminit vmemmap_populated(unsigned long start, int page_size)
{
	unsigned long end = start + page_size;
	start = (unsigned long)(pfn_to_page(vmemmap_section_start(start)));

	for (; start < end; start += (PAGES_PER_SECTION * sizeof(struct page)))
		if (pfn_valid(page_to_pfn((struct page *)start)))
			return 1;

	return 0;
}

/*
 * vmemmap virtual address space management does not have a traditonal page
 * table to track which virtual struct pages are backed by physical mapping.
 * The virtual to physical mappings are tracked in a simple linked list
 * format. 'vmemmap_list' maintains the entire vmemmap physical mapping at
 * all times where as the 'next' list maintains the available
 * vmemmap_backing structures which have been deleted from the
 * 'vmemmap_global' list during system runtime (memory hotplug remove
 * operation). The freed 'vmemmap_backing' structures are reused later when
 * new requests come in without allocating fresh memory. This pointer also
 * tracks the allocated 'vmemmap_backing' structures as we allocate one
 * full page memory at a time when we dont have any.
 */
struct vmemmap_backing *vmemmap_list;
static struct vmemmap_backing *next;

/*
 * The same pointer 'next' tracks individual chunks inside the allocated
 * full page during the boot time and again tracks the freeed nodes during
 * runtime. It is racy but it does not happen as they are separated by the
 * boot process. Will create problem if some how we have memory hotplug
 * operation during boot !!
 */
static int num_left;
static int num_freed;

static __meminit struct vmemmap_backing * vmemmap_list_alloc(int node)
{
	struct vmemmap_backing *vmem_back;
	/* get from freed entries first */
	if (num_freed) {
		num_freed--;
		vmem_back = next;
		next = next->list;

		return vmem_back;
	}

	/* allocate a page when required and hand out chunks */
	if (!num_left) {
		next = vmemmap_alloc_block(PAGE_SIZE, node);
		if (unlikely(!next)) {
			WARN_ON(1);
			return NULL;
		}
		num_left = PAGE_SIZE / sizeof(struct vmemmap_backing);
	}

	num_left--;

	return next++;
}

static __meminit void vmemmap_list_populate(unsigned long phys,
					    unsigned long start,
					    int node)
{
	struct vmemmap_backing *vmem_back;

	vmem_back = vmemmap_list_alloc(node);
	if (unlikely(!vmem_back)) {
		WARN_ON(1);
		return;
	}

	vmem_back->phys = phys;
	vmem_back->virt_addr = start;
	vmem_back->list = vmemmap_list;

	vmemmap_list = vmem_back;
}

int __meminit vmemmap_populate(unsigned long start, unsigned long end, int node,
		struct vmem_altmap *altmap)
{
	unsigned long page_size = 1 << mmu_psize_defs[mmu_vmemmap_psize].shift;

	/* Align to the page size of the linear mapping. */
	start = _ALIGN_DOWN(start, page_size);

	pr_debug("vmemmap_populate %lx..%lx, node %d\n", start, end, node);

	for (; start < end; start += page_size) {
		void *p = NULL;
		int rc;

		if (vmemmap_populated(start, page_size))
			continue;

		/*
		 * Allocate from the altmap first if we have one. This may
		 * fail due to alignment issues when using 16MB hugepages, so
		 * fall back to system memory if the altmap allocation fail.
		 */
		if (altmap)
			p = altmap_alloc_block_buf(page_size, altmap);
		if (!p)
			p = vmemmap_alloc_block_buf(page_size, node);
		if (!p)
			return -ENOMEM;

		vmemmap_list_populate(__pa(p), start, node);

		pr_debug("      * %016lx..%016lx allocated at %p\n",
			 start, start + page_size, p);

		rc = vmemmap_create_mapping(start, page_size, __pa(p));
		if (rc < 0) {
			pr_warn("%s: Unable to create vmemmap mapping: %d\n",
				__func__, rc);
			return -EFAULT;
		}
	}

	return 0;
}

#ifdef CONFIG_MEMORY_HOTPLUG
static unsigned long vmemmap_list_free(unsigned long start)
{
	struct vmemmap_backing *vmem_back, *vmem_back_prev;

	vmem_back_prev = vmem_back = vmemmap_list;

	/* look for it with prev pointer recorded */
	for (; vmem_back; vmem_back = vmem_back->list) {
		if (vmem_back->virt_addr == start)
			break;
		vmem_back_prev = vmem_back;
	}

	if (unlikely(!vmem_back)) {
		WARN_ON(1);
		return 0;
	}

	/* remove it from vmemmap_list */
	if (vmem_back == vmemmap_list) /* remove head */
		vmemmap_list = vmem_back->list;
	else
		vmem_back_prev->list = vmem_back->list;

	/* next point to this freed entry */
	vmem_back->list = next;
	next = vmem_back;
	num_freed++;

	return vmem_back->phys;
}

void __ref vmemmap_free(unsigned long start, unsigned long end,
		struct vmem_altmap *altmap)
{
	unsigned long page_size = 1 << mmu_psize_defs[mmu_vmemmap_psize].shift;
	unsigned long page_order = get_order(page_size);
	unsigned long alt_start = ~0, alt_end = ~0;
	unsigned long base_pfn;

	start = _ALIGN_DOWN(start, page_size);
	if (altmap) {
		alt_start = altmap->base_pfn;
		alt_end = altmap->base_pfn + altmap->reserve +
			  altmap->free + altmap->alloc + altmap->align;
	}

	pr_debug("vmemmap_free %lx...%lx\n", start, end);

	for (; start < end; start += page_size) {
		unsigned long nr_pages, addr;
		struct page *page;

		/*
		 * the section has already be marked as invalid, so
		 * vmemmap_populated() true means some other sections still
		 * in this page, so skip it.
		 */
		if (vmemmap_populated(start, page_size))
			continue;

		addr = vmemmap_list_free(start);
		if (!addr)
			continue;

		page = pfn_to_page(addr >> PAGE_SHIFT);
		nr_pages = 1 << page_order;
		base_pfn = PHYS_PFN(addr);

		if (base_pfn >= alt_start && base_pfn < alt_end) {
			vmem_altmap_free(altmap, nr_pages);
		} else if (PageReserved(page)) {
			/* allocated from bootmem */
			if (page_size < PAGE_SIZE) {
				/*
				 * this shouldn't happen, but if it is
				 * the case, leave the memory there
				 */
				WARN_ON_ONCE(1);
			} else {
				while (nr_pages--)
					free_reserved_page(page++);
			}
		} else {
			free_pages((unsigned long)(__va(addr)), page_order);
		}

		vmemmap_remove_mapping(start, page_size);
	}
}
#endif
void register_page_bootmem_memmap(unsigned long section_nr,
				  struct page *start_page, unsigned long size)
{
}

#endif /* CONFIG_SPARSEMEM_VMEMMAP */

#ifdef CONFIG_PPC_BOOK3S_64
static bool disable_radix = !IS_ENABLED(CONFIG_PPC_RADIX_MMU_DEFAULT);

static int __init parse_disable_radix(char *p)
{
	bool val;

	if (!p)
		val = true;
	else if (kstrtobool(p, &val))
		return -EINVAL;

	disable_radix = val;

	return 0;
}
early_param("disable_radix", parse_disable_radix);

/*
 * If we're running under a hypervisor, we need to check the contents of
 * /chosen/ibm,architecture-vec-5 to see if the hypervisor is willing to do
 * radix.  If not, we clear the radix feature bit so we fall back to hash.
 */
static void __init early_check_vec5(void)
{
	unsigned long root, chosen;
	int size;
	const u8 *vec5;
	u8 mmu_supported;

	root = of_get_flat_dt_root();
	chosen = of_get_flat_dt_subnode_by_name(root, "chosen");
	if (chosen == -FDT_ERR_NOTFOUND) {
		cur_cpu_spec->mmu_features &= ~MMU_FTR_TYPE_RADIX;
		return;
	}
	vec5 = of_get_flat_dt_prop(chosen, "ibm,architecture-vec-5", &size);
	if (!vec5) {
		cur_cpu_spec->mmu_features &= ~MMU_FTR_TYPE_RADIX;
		return;
	}
	if (size <= OV5_INDX(OV5_MMU_SUPPORT)) {
		cur_cpu_spec->mmu_features &= ~MMU_FTR_TYPE_RADIX;
		return;
	}

	/* Check for supported configuration */
	mmu_supported = vec5[OV5_INDX(OV5_MMU_SUPPORT)] &
			OV5_FEAT(OV5_MMU_SUPPORT);
	if (mmu_supported == OV5_FEAT(OV5_MMU_RADIX)) {
		/* Hypervisor only supports radix - check enabled && GTSE */
		if (!early_radix_enabled()) {
			pr_warn("WARNING: Ignoring cmdline option disable_radix\n");
		}
		if (!(vec5[OV5_INDX(OV5_RADIX_GTSE)] &
						OV5_FEAT(OV5_RADIX_GTSE))) {
			pr_warn("WARNING: Hypervisor doesn't support RADIX with GTSE\n");
		}
		/* Do radix anyway - the hypervisor said we had to */
		cur_cpu_spec->mmu_features |= MMU_FTR_TYPE_RADIX;
	} else if (mmu_supported == OV5_FEAT(OV5_MMU_HASH)) {
		/* Hypervisor only supports hash - disable radix */
		cur_cpu_spec->mmu_features &= ~MMU_FTR_TYPE_RADIX;
	}
}

void __init mmu_early_init_devtree(void)
{
	/* Disable radix mode based on kernel command line. */
	if (disable_radix)
		cur_cpu_spec->mmu_features &= ~MMU_FTR_TYPE_RADIX;

	/*
	 * Check /chosen/ibm,architecture-vec-5 if running as a guest.
	 * When running bare-metal, we can use radix if we like
	 * even though the ibm,architecture-vec-5 property created by
	 * skiboot doesn't have the necessary bits set.
	 */
	if (!(mfmsr() & MSR_HV))
		early_check_vec5();

	if (early_radix_enabled())
		radix__early_init_devtree();
	else
		hash__early_init_devtree();
}
#endif /* CONFIG_PPC_BOOK3S_64 */
