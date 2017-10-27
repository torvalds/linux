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

#include "mm.h"

unsigned long vectors_base;

#ifdef CONFIG_ARM_MPU
struct mpu_rgn_info mpu_rgn_info;

/* Region number */
static void rgnr_write(u32 v)
{
	asm("mcr        p15, 0, %0, c6, c2, 0" : : "r" (v));
}

/* Data-side / unified region attributes */

/* Region access control register */
static void dracr_write(u32 v)
{
	asm("mcr        p15, 0, %0, c6, c1, 4" : : "r" (v));
}

/* Region size register */
static void drsr_write(u32 v)
{
	asm("mcr        p15, 0, %0, c6, c1, 2" : : "r" (v));
}

/* Region base address register */
static void drbar_write(u32 v)
{
	asm("mcr        p15, 0, %0, c6, c1, 0" : : "r" (v));
}

static u32 drbar_read(void)
{
	u32 v;
	asm("mrc        p15, 0, %0, c6, c1, 0" : "=r" (v));
	return v;
}
/* Optional instruction-side region attributes */

/* I-side Region access control register */
static void iracr_write(u32 v)
{
	asm("mcr        p15, 0, %0, c6, c1, 5" : : "r" (v));
}

/* I-side Region size register */
static void irsr_write(u32 v)
{
	asm("mcr        p15, 0, %0, c6, c1, 3" : : "r" (v));
}

/* I-side Region base address register */
static void irbar_write(u32 v)
{
	asm("mcr        p15, 0, %0, c6, c1, 1" : : "r" (v));
}

static unsigned long irbar_read(void)
{
	unsigned long v;
	asm("mrc        p15, 0, %0, c6, c1, 1" : "=r" (v));
	return v;
}

/* MPU initialisation functions */
void __init adjust_lowmem_bounds_mpu(void)
{
	phys_addr_t phys_offset = PHYS_OFFSET;
	phys_addr_t aligned_region_size, specified_mem_size, rounded_mem_size;
	struct memblock_region *reg;
	bool first = true;
	phys_addr_t mem_start;
	phys_addr_t mem_end;

	for_each_memblock(memory, reg) {
		if (first) {
			/*
			 * Initially only use memory continuous from
			 * PHYS_OFFSET */
			if (reg->base != phys_offset)
				panic("First memory bank must be contiguous from PHYS_OFFSET");

			mem_start = reg->base;
			mem_end = reg->base + reg->size;
			specified_mem_size = reg->size;
			first = false;
		} else {
			/*
			 * memblock auto merges contiguous blocks, remove
			 * all blocks afterwards in one go (we can't remove
			 * blocks separately while iterating)
			 */
			pr_notice("Ignoring RAM after %pa, memory at %pa ignored\n",
				  &mem_end, &reg->base);
			memblock_remove(reg->base, 0 - reg->base);
			break;
		}
	}

	/*
	 * MPU has curious alignment requirements: Size must be power of 2, and
	 * region start must be aligned to the region size
	 */
	if (phys_offset != 0)
		pr_info("PHYS_OFFSET != 0 => MPU Region size constrained by alignment requirements\n");

	/*
	 * Maximum aligned region might overflow phys_addr_t if phys_offset is
	 * 0. Hence we keep everything below 4G until we take the smaller of
	 * the aligned_region_size and rounded_mem_size, one of which is
	 * guaranteed to be smaller than the maximum physical address.
	 */
	aligned_region_size = (phys_offset - 1) ^ (phys_offset);
	/* Find the max power-of-two sized region that fits inside our bank */
	rounded_mem_size = (1 <<  __fls(specified_mem_size)) - 1;

	/* The actual region size is the smaller of the two */
	aligned_region_size = aligned_region_size < rounded_mem_size
				? aligned_region_size + 1
				: rounded_mem_size + 1;

	if (aligned_region_size != specified_mem_size) {
		pr_warn("Truncating memory from %pa to %pa (MPU region constraints)",
				&specified_mem_size, &aligned_region_size);
		memblock_remove(mem_start + aligned_region_size,
				specified_mem_size - aligned_region_size);

		mem_end = mem_start + aligned_region_size;
	}

	pr_debug("MPU Region from %pa size %pa (end %pa))\n",
		&phys_offset, &aligned_region_size, &mem_end);

}

static int mpu_present(void)
{
	return ((read_cpuid_ext(CPUID_EXT_MMFR0) & MMFR0_PMSA) == MMFR0_PMSAv7);
}

static int mpu_max_regions(void)
{
	/*
	 * We don't support a different number of I/D side regions so if we
	 * have separate instruction and data memory maps then return
	 * whichever side has a smaller number of supported regions.
	 */
	u32 dregions, iregions, mpuir;
	mpuir = read_cpuid(CPUID_MPUIR);

	dregions = iregions = (mpuir & MPUIR_DREGION_SZMASK) >> MPUIR_DREGION;

	/* Check for separate d-side and i-side memory maps */
	if (mpuir & MPUIR_nU)
		iregions = (mpuir & MPUIR_IREGION_SZMASK) >> MPUIR_IREGION;

	/* Use the smallest of the two maxima */
	return min(dregions, iregions);
}

static int mpu_iside_independent(void)
{
	/* MPUIR.nU specifies whether there is *not* a unified memory map */
	return read_cpuid(CPUID_MPUIR) & MPUIR_nU;
}

static int mpu_min_region_order(void)
{
	u32 drbar_result, irbar_result;
	/* We've kept a region free for this probing */
	rgnr_write(MPU_PROBE_REGION);
	isb();
	/*
	 * As per ARM ARM, write 0xFFFFFFFC to DRBAR to find the minimum
	 * region order
	*/
	drbar_write(0xFFFFFFFC);
	drbar_result = irbar_result = drbar_read();
	drbar_write(0x0);
	/* If the MPU is non-unified, we use the larger of the two minima*/
	if (mpu_iside_independent()) {
		irbar_write(0xFFFFFFFC);
		irbar_result = irbar_read();
		irbar_write(0x0);
	}
	isb(); /* Ensure that MPU region operations have completed */
	/* Return whichever result is larger */
	return __ffs(max(drbar_result, irbar_result));
}

static int mpu_setup_region(unsigned int number, phys_addr_t start,
			unsigned int size_order, unsigned int properties)
{
	u32 size_data;

	/* We kept a region free for probing resolution of MPU regions*/
	if (number > mpu_max_regions() || number == MPU_PROBE_REGION)
		return -ENOENT;

	if (size_order > 32)
		return -ENOMEM;

	if (size_order < mpu_min_region_order())
		return -ENOMEM;

	/* Writing N to bits 5:1 (RSR_SZ)  specifies region size 2^N+1 */
	size_data = ((size_order - 1) << MPU_RSR_SZ) | 1 << MPU_RSR_EN;

	dsb(); /* Ensure all previous data accesses occur with old mappings */
	rgnr_write(number);
	isb();
	drbar_write(start);
	dracr_write(properties);
	isb(); /* Propagate properties before enabling region */
	drsr_write(size_data);

	/* Check for independent I-side registers */
	if (mpu_iside_independent()) {
		irbar_write(start);
		iracr_write(properties);
		isb();
		irsr_write(size_data);
	}
	isb();

	/* Store region info (we treat i/d side the same, so only store d) */
	mpu_rgn_info.rgns[number].dracr = properties;
	mpu_rgn_info.rgns[number].drbar = start;
	mpu_rgn_info.rgns[number].drsr = size_data;
	return 0;
}

/*
* Set up default MPU regions, doing nothing if there is no MPU
*/
void __init mpu_setup(void)
{
	int region_err;
	if (!mpu_present())
		return;

	region_err = mpu_setup_region(MPU_RAM_REGION, PHYS_OFFSET,
					ilog2(memblock.memory.regions[0].size),
					MPU_AP_PL1RW_PL0RW | MPU_RGN_NORMAL);
	if (region_err) {
		panic("MPU region initialization failure! %d", region_err);
	} else {
		pr_info("Using ARMv7 PMSA Compliant MPU. "
			 "Region independence: %s, Max regions: %d\n",
			mpu_iside_independent() ? "Yes" : "No",
			mpu_max_regions());
	}
}
#else
static void adjust_lowmem_bounds_mpu(void) {}
static void __init mpu_setup(void) {}
#endif /* CONFIG_ARM_MPU */

#ifdef CONFIG_CPU_CP15
#ifdef CONFIG_CPU_HIGH_VECTOR
static unsigned long __init setup_vectors_base(void)
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
		return !!cpuid_feature_extract(CPUID_EXT_PFR1, 4);
	return 0;
}

static unsigned long __init setup_vectors_base(void)
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
	early_trap_init((void *)vectors_base);
	mpu_setup();
	bootmem_init();
}

/*
 * We don't need to do anything here for nommu machines.
 */
void setup_mm_for_reboot(void)
{
}

void flush_dcache_page(struct page *page)
{
	__cpuc_flush_dcache_area(page_address(page), PAGE_SIZE);
}
EXPORT_SYMBOL(flush_dcache_page);

void flush_kernel_dcache_page(struct page *page)
{
	__cpuc_flush_dcache_area(page_address(page), PAGE_SIZE);
}
EXPORT_SYMBOL(flush_kernel_dcache_page);

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
	__alias(ioremap_cached);

void __iomem *ioremap_cached(resource_size_t res_cookie, size_t size)
{
	return __arm_ioremap_caller(res_cookie, size, MT_DEVICE_CACHED,
				    __builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_cache);
EXPORT_SYMBOL(ioremap_cached);

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

void __iounmap(volatile void __iomem *addr)
{
}
EXPORT_SYMBOL(__iounmap);

void (*arch_iounmap)(volatile void __iomem *);

void iounmap(volatile void __iomem *addr)
{
}
EXPORT_SYMBOL(iounmap);
