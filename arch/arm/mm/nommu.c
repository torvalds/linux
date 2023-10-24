// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mm/nommu.c
 *
 * ARM uCLinux supporting functions.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/kernel.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/sections.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/traps.h>
#include <asm/mach/arch.h>
#include <asm/cputype.h>
#include <asm/mpu.h>
#include <asm/procinfo.h>
#include <asm/idmap.h>

#include "mm.h"

unsigned long vectors_base;

/*
 * empty_zero_page is a special page that is used for
 * zero-initialized data and COW.
 */
struct page *empty_zero_page;
EXPORT_SYMBOL(empty_zero_page);

#ifdef CONFIG_ARM_MPU
struct mpu_rgn_info mpu_rgn_info;
#endif

#ifdef CONFIG_CPU_CP15
#ifdef CONFIG_CPU_HIGH_VECTOR
unsigned long setup_vectors_base(void)
{
	unsigned long reg = get_cr();

	set_cr(reg | CR_V);
	return 0xffff0000;
}
#else /* CONFIG_CPU_HIGH_VECTOR */
/* Write exception base address to VBAR */
static inline void set_vbar(unsigned long val)
{
	asm("mcr p15, 0, %0, c12, c0, 0" : : "r" (val) : "cc");
}

/*
 * Security extensions, bits[7:4], permitted values,
 * 0b0000 - not implemented, 0b0001/0b0010 - implemented
 */
static inline bool security_extensions_enabled(void)
{
	/* Check CPUID Identification Scheme before ID_PFR1 read */
	if ((read_cpuid_id() & 0x000f0000) == 0x000f0000)
		return cpuid_feature_extract(CPUID_EXT_PFR1, 4) ||
			cpuid_feature_extract(CPUID_EXT_PFR1, 20);
	return 0;
}

unsigned long setup_vectors_base(void)
{
	unsigned long base = 0, reg = get_cr();

	set_cr(reg & ~CR_V);
	if (security_extensions_enabled()) {
		if (IS_ENABLED(CONFIG_REMAP_VECTORS_TO_RAM))
			base = CONFIG_DRAM_BASE;
		set_vbar(base);
	} else if (IS_ENABLED(CONFIG_REMAP_VECTORS_TO_RAM)) {
		if (CONFIG_DRAM_BASE != 0)
			pr_err("Security extensions not enabled, vectors cannot be remapped to RAM, vectors base will be 0x00000000\n");
	}

	return base;
}
#endif /* CONFIG_CPU_HIGH_VECTOR */
#endif /* CONFIG_CPU_CP15 */

void __init arm_mm_memblock_reserve(void)
{
#ifndef CONFIG_CPU_V7M
	vectors_base = IS_ENABLED(CONFIG_CPU_CP15) ? setup_vectors_base() : 0;
	/*
	 * Register the exception vector page.
	 * some architectures which the DRAM is the exception vector to trap,
	 * alloc_page breaks with error, although it is not NULL, but "0."
	 */
	memblock_reserve(vectors_base, 2 * PAGE_SIZE);
#else /* ifndef CONFIG_CPU_V7M */
	/*
	 * There is no dedicated vector page on V7-M. So nothing needs to be
	 * reserved here.
	 */
#endif
	/*
	 * In any case, always ensure address 0 is never used as many things
	 * get very confused if 0 is returned as a legitimate address.
	 */
	memblock_reserve(0, 1);
}

static void __init adjust_lowmem_bounds_mpu(void)
{
	unsigned long pmsa = read_cpuid_ext(CPUID_EXT_MMFR0) & MMFR0_PMSA;

	switch (pmsa) {
	case MMFR0_PMSAv7:
		pmsav7_adjust_lowmem_bounds();
		break;
	case MMFR0_PMSAv8:
		pmsav8_adjust_lowmem_bounds();
		break;
	default:
		break;
	}
}

static void __init mpu_setup(void)
{
	unsigned long pmsa = read_cpuid_ext(CPUID_EXT_MMFR0) & MMFR0_PMSA;

	switch (pmsa) {
	case MMFR0_PMSAv7:
		pmsav7_setup();
		break;
	case MMFR0_PMSAv8:
		pmsav8_setup();
		break;
	default:
		break;
	}
}

void __init adjust_lowmem_bounds(void)
{
	phys_addr_t end;
	adjust_lowmem_bounds_mpu();
	end = memblock_end_of_DRAM();
	high_memory = __va(end - 1) + 1;
	memblock_set_current_limit(end);
}

/*
 * paging_init() sets up the page tables, initialises the zone memory
 * maps, and sets up the zero page, bad page and bad page tables.
 */
void __init paging_init(const struct machine_desc *mdesc)
{
	void *zero_page;

	early_trap_init((void *)vectors_base);
	mpu_setup();

	/* allocate the zero page. */
	zero_page = (void *)memblock_alloc(PAGE_SIZE, PAGE_SIZE);
	if (!zero_page)
		panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE);

	bootmem_init();

	empty_zero_page = virt_to_page(zero_page);
	flush_dcache_page(empty_zero_page);
}

/*
 * We don't need to do anything here for nommu machines.
 */
void setup_mm_for_reboot(void)
{
}

void flush_dcache_folio(struct folio *folio)
{
	__cpuc_flush_dcache_area(folio_address(folio), folio_size(folio));
}
EXPORT_SYMBOL(flush_dcache_folio);

void flush_dcache_page(struct page *page)
{
	__cpuc_flush_dcache_area(page_address(page), PAGE_SIZE);
}
EXPORT_SYMBOL(flush_dcache_page);

void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
		       unsigned long uaddr, void *dst, const void *src,
		       unsigned long len)
{
	memcpy(dst, src, len);
	if (vma->vm_flags & VM_EXEC)
		__cpuc_coherent_user_range(uaddr, uaddr + len);
}

void __iomem *__arm_ioremap_pfn(unsigned long pfn, unsigned long offset,
				size_t size, unsigned int mtype)
{
	if (pfn >= (0x100000000ULL >> PAGE_SHIFT))
		return NULL;
	return (void __iomem *) (offset + (pfn << PAGE_SHIFT));
}
EXPORT_SYMBOL(__arm_ioremap_pfn);

void __iomem *__arm_ioremap_caller(phys_addr_t phys_addr, size_t size,
				   unsigned int mtype, void *caller)
{
	return (void __iomem *)phys_addr;
}

void __iomem * (*arch_ioremap_caller)(phys_addr_t, size_t, unsigned int, void *);

void __iomem *ioremap(resource_size_t res_cookie, size_t size)
{
	return __arm_ioremap_caller(res_cookie, size, MT_DEVICE,
				    __builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap);

void __iomem *ioremap_cache(resource_size_t res_cookie, size_t size)
{
	return __arm_ioremap_caller(res_cookie, size, MT_DEVICE_CACHED,
				    __builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_cache);

void __iomem *ioremap_wc(resource_size_t res_cookie, size_t size)
{
	return __arm_ioremap_caller(res_cookie, size, MT_DEVICE_WC,
				    __builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_wc);

#ifdef CONFIG_PCI

#include <asm/mach/map.h>

void __iomem *pci_remap_cfgspace(resource_size_t res_cookie, size_t size)
{
	return arch_ioremap_caller(res_cookie, size, MT_UNCACHED,
				   __builtin_return_address(0));
}
EXPORT_SYMBOL_GPL(pci_remap_cfgspace);
#endif

void *arch_memremap_wb(phys_addr_t phys_addr, size_t size)
{
	return (void *)phys_addr;
}

void iounmap(volatile void __iomem *io_addr)
{
}
EXPORT_SYMBOL(iounmap);
