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
#include <asm/hugetlb.h>

#include <mm/mmu_decl.h>

#ifdef CONFIG_SPARSEMEM_VMEMMAP
/*
 * Given an address within the vmemmap, determine the page that
 * represents the start of the subsection it is within.  Note that we have to
 * do this by hand as the proffered address may not be correctly aligned.
 * Subtraction of non-aligned pointers produces undefined results.
 */
static struct page * __meminit vmemmap_subsection_start(unsigned long vmemmap_addr)
{
	unsigned long start_pfn;
	unsigned long offset = vmemmap_addr - ((unsigned long)(vmemmap));

	/* Return the pfn of the start of the section. */
	start_pfn = (offset / sizeof(struct page)) & PAGE_SUBSECTION_MASK;
	return pfn_to_page(start_pfn);
}

/*
 * Since memory is added in sub-section chunks, before creating a new vmemmap
 * mapping, the kernel should check whether there is an existing memmap mapping
 * covering the new subsection added. This is needed because kernel can map
 * vmemmap area using 16MB pages which will cover a memory range of 16G. Such
 * a range covers multiple subsections (2M)
 *
 * If any subsection in the 16G range mapped by vmemmap is valid we consider the
 * vmemmap populated (There is a page table entry already present). We can't do
 * a page table lookup here because with the hash translation we don't keep
 * vmemmap details in linux page table.
 */
static int __meminit vmemmap_populated(unsigned long vmemmap_addr, int vmemmap_map_size)
{
	struct page *start;
	unsigned long vmemmap_end = vmemmap_addr + vmemmap_map_size;
	start = vmemmap_subsection_start(vmemmap_addr);

	for (; (unsigned long)start < vmemmap_end; start += PAGES_PER_SUBSECTION)
		/*
		 * pfn valid check here is intended to really check
		 * whether we have any subsection already initialized
		 * in this range.
		 */
		if (pfn_valid(page_to_pfn(start)))
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

static __meminit int vmemmap_list_populate(unsigned long phys,
					   unsigned long start,
					   int node)
{
	struct vmemmap_backing *vmem_back;

	vmem_back = vmemmap_list_alloc(node);
	if (unlikely(!vmem_back)) {
		pr_debug("vmemap list allocation failed\n");
		return -ENOMEM;
	}

	vmem_back->phys = phys;
	vmem_back->virt_addr = start;
	vmem_back->list = vmemmap_list;

	vmemmap_list = vmem_back;
	return 0;
}

static bool altmap_cross_boundary(struct vmem_altmap *altmap, unsigned long start,
				unsigned long page_size)
{
	unsigned long nr_pfn = page_size / sizeof(struct page);
	unsigned long start_pfn = page_to_pfn((struct page *)start);

	if ((start_pfn + nr_pfn) > altmap->end_pfn)
		return true;

	if (start_pfn < altmap->base_pfn)
		return true;

	return false;
}

int __meminit vmemmap_populate(unsigned long start, unsigned long end, int node,
		struct vmem_altmap *altmap)
{
	bool altmap_alloc;
	unsigned long page_size = 1 << mmu_psize_defs[mmu_vmemmap_psize].shift;

	/* Align to the page size of the linear mapping. */
	start = ALIGN_DOWN(start, page_size);

	pr_debug("vmemmap_populate %lx..%lx, node %d\n", start, end, node);

	for (; start < end; start += page_size) {
		void *p = NULL;
		int rc;

		/*
		 * This vmemmap range is backing different subsections. If any
		 * of that subsection is marked valid, that means we already
		 * have initialized a page table covering this range and hence
		 * the vmemmap range is populated.
		 */
		if (vmemmap_populated(start, page_size))
			continue;

		/*
		 * Allocate from the altmap first if we have one. This may
		 * fail due to alignment issues when using 16MB hugepages, so
		 * fall back to system memory if the altmap allocation fail.
		 */
		if (altmap && !altmap_cross_boundary(altmap, start, page_size)) {
			p = vmemmap_alloc_block_buf(page_size, node, altmap);
			if (!p)
				pr_debug("altmap block allocation failed, falling back to system memory");
			else
				altmap_alloc = true;
		}
		if (!p) {
			p = vmemmap_alloc_block_buf(page_size, node, NULL);
			altmap_alloc = false;
		}
		if (!p)
			return -ENOMEM;

		if (vmemmap_list_populate(__pa(p), start, node)) {
			/*
			 * If we don't populate vmemap list, we don't have
			 * the ability to free the allocated vmemmap
			 * pages in section_deactivate. Hence free them
			 * here.
			 */
			int nr_pfns = page_size >> PAGE_SHIFT;
			unsigned long page_order = get_order(page_size);

			if (altmap_alloc)
				vmem_altmap_free(altmap, nr_pfns);
			else
				free_pages((unsigned long)p, page_order);
			return -ENOMEM;
		}

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

	if (unlikely(!vmem_back))
		return 0;

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

	start = ALIGN_DOWN(start, page_size);
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
		 * We have already marked the subsection we are trying to remove
		 * invalid. So if we want to remove the vmemmap range, we
		 * need to make sure there is no subsection marked valid
		 * in this range.
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
unsigned int mmu_lpid_bits;
unsigned int mmu_pid_bits;

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
			cur_cpu_spec->mmu_features &= ~MMU_FTR_GTSE;
		} else
			cur_cpu_spec->mmu_features |= MMU_FTR_GTSE;
		/* Do radix anyway - the hypervisor said we had to */
		cur_cpu_spec->mmu_features |= MMU_FTR_TYPE_RADIX;
	} else if (mmu_supported == OV5_FEAT(OV5_MMU_HASH)) {
		/* Hypervisor only supports hash - disable radix */
		cur_cpu_spec->mmu_features &= ~MMU_FTR_TYPE_RADIX;
		cur_cpu_spec->mmu_features &= ~MMU_FTR_GTSE;
	}
}

static int __init dt_scan_mmu_pid_width(unsigned long node,
					   const char *uname, int depth,
					   void *data)
{
	int size = 0;
	const __be32 *prop;
	const char *type = of_get_flat_dt_prop(node, "device_type", NULL);

	/* We are scanning "cpu" nodes only */
	if (type == NULL || strcmp(type, "cpu") != 0)
		return 0;

	/* Find MMU LPID, PID register size */
	prop = of_get_flat_dt_prop(node, "ibm,mmu-lpid-bits", &size);
	if (prop && size == 4)
		mmu_lpid_bits = be32_to_cpup(prop);

	prop = of_get_flat_dt_prop(node, "ibm,mmu-pid-bits", &size);
	if (prop && size == 4)
		mmu_pid_bits = be32_to_cpup(prop);

	if (!mmu_pid_bits && !mmu_lpid_bits)
		return 0;

	return 1;
}

void __init mmu_early_init_devtree(void)
{
	bool hvmode = !!(mfmsr() & MSR_HV);

	/* Disable radix mode based on kernel command line. */
	if (disable_radix) {
		if (IS_ENABLED(CONFIG_PPC_64S_HASH_MMU))
			cur_cpu_spec->mmu_features &= ~MMU_FTR_TYPE_RADIX;
		else
			pr_warn("WARNING: Ignoring cmdline option disable_radix\n");
	}

	of_scan_flat_dt(dt_scan_mmu_pid_width, NULL);
	if (hvmode && !mmu_lpid_bits) {
		if (early_cpu_has_feature(CPU_FTR_ARCH_207S))
			mmu_lpid_bits = 12; /* POWER8-10 */
		else
			mmu_lpid_bits = 10; /* POWER7 */
	}
	if (!mmu_pid_bits) {
		if (early_cpu_has_feature(CPU_FTR_ARCH_300))
			mmu_pid_bits = 20; /* POWER9-10 */
	}

	/*
	 * Check /chosen/ibm,architecture-vec-5 if running as a guest.
	 * When running bare-metal, we can use radix if we like
	 * even though the ibm,architecture-vec-5 property created by
	 * skiboot doesn't have the necessary bits set.
	 */
	if (!hvmode)
		early_check_vec5();

	if (early_radix_enabled()) {
		radix__early_init_devtree();

		/*
		 * We have finalized the translation we are going to use by now.
		 * Radix mode is not limited by RMA / VRMA addressing.
		 * Hence don't limit memblock allocations.
		 */
		ppc64_rma_size = ULONG_MAX;
		memblock_set_current_limit(MEMBLOCK_ALLOC_ANYWHERE);
	} else
		hash__early_init_devtree();

	if (IS_ENABLED(CONFIG_HUGETLB_PAGE_SIZE_VARIABLE))
		hugetlbpage_init_defaultsize();

	if (!(cur_cpu_spec->mmu_features & MMU_FTR_HPTE_TABLE) &&
	    !(cur_cpu_spec->mmu_features & MMU_FTR_TYPE_RADIX))
		panic("kernel does not support any MMU type offered by platform");
}
#endif /* CONFIG_PPC_BOOK3S_64 */
