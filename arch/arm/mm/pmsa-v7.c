/*
 * Based on linux/arch/arm/mm/nommu.c
 *
 * ARM PMSAv7 supporting functions.
 */

#include <linux/memblock.h>

#include <asm/cp15.h>
#include <asm/cputype.h>
#include <asm/mpu.h>

#include "mm.h"

static unsigned int __initdata mpu_min_region_order;
static unsigned int __initdata mpu_max_regions;

#ifndef CONFIG_CPU_V7M

#define DRBAR	__ACCESS_CP15(c6, 0, c1, 0)
#define IRBAR	__ACCESS_CP15(c6, 0, c1, 1)
#define DRSR	__ACCESS_CP15(c6, 0, c1, 2)
#define IRSR	__ACCESS_CP15(c6, 0, c1, 3)
#define DRACR	__ACCESS_CP15(c6, 0, c1, 4)
#define IRACR	__ACCESS_CP15(c6, 0, c1, 5)
#define RNGNR	__ACCESS_CP15(c6, 0, c2, 0)

/* Region number */
static inline void rgnr_write(u32 v)
{
	write_sysreg(v, RNGNR);
}

/* Data-side / unified region attributes */

/* Region access control register */
static inline void dracr_write(u32 v)
{
	write_sysreg(v, DRACR);
}

/* Region size register */
static inline void drsr_write(u32 v)
{
	write_sysreg(v, DRSR);
}

/* Region base address register */
static inline void drbar_write(u32 v)
{
	write_sysreg(v, DRBAR);
}

static inline u32 drbar_read(void)
{
	return read_sysreg(DRBAR);
}
/* Optional instruction-side region attributes */

/* I-side Region access control register */
static inline void iracr_write(u32 v)
{
	write_sysreg(v, IRACR);
}

/* I-side Region size register */
static inline void irsr_write(u32 v)
{
	write_sysreg(v, IRSR);
}

/* I-side Region base address register */
static inline void irbar_write(u32 v)
{
	write_sysreg(v, IRBAR);
}

static inline u32 irbar_read(void)
{
	return read_sysreg(IRBAR);
}

#else

static inline void rgnr_write(u32 v)
{
	writel_relaxed(v, BASEADDR_V7M_SCB + MPU_RNR);
}

/* Data-side / unified region attributes */

/* Region access control register */
static inline void dracr_write(u32 v)
{
	u32 rsr = readl_relaxed(BASEADDR_V7M_SCB + MPU_RASR) & GENMASK(15, 0);

	writel_relaxed((v << 16) | rsr, BASEADDR_V7M_SCB + MPU_RASR);
}

/* Region size register */
static inline void drsr_write(u32 v)
{
	u32 racr = readl_relaxed(BASEADDR_V7M_SCB + MPU_RASR) & GENMASK(31, 16);

	writel_relaxed(v | racr, BASEADDR_V7M_SCB + MPU_RASR);
}

/* Region base address register */
static inline void drbar_write(u32 v)
{
	writel_relaxed(v, BASEADDR_V7M_SCB + MPU_RBAR);
}

static inline u32 drbar_read(void)
{
	return readl_relaxed(BASEADDR_V7M_SCB + MPU_RBAR);
}

/* ARMv7-M only supports a unified MPU, so I-side operations are nop */

static inline void iracr_write(u32 v) {}
static inline void irsr_write(u32 v) {}
static inline void irbar_write(u32 v) {}
static inline unsigned long irbar_read(void) {return 0;}

#endif

static int __init mpu_present(void)
{
	return ((read_cpuid_ext(CPUID_EXT_MMFR0) & MMFR0_PMSA) == MMFR0_PMSAv7);
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

	if (!mpu_present())
		return;

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

static int __init __mpu_max_regions(void)
{
	/*
	 * We don't support a different number of I/D side regions so if we
	 * have separate instruction and data memory maps then return
	 * whichever side has a smaller number of supported regions.
	 */
	u32 dregions, iregions, mpuir;

	mpuir = read_cpuid_mputype();

	dregions = iregions = (mpuir & MPUIR_DREGION_SZMASK) >> MPUIR_DREGION;

	/* Check for separate d-side and i-side memory maps */
	if (mpuir & MPUIR_nU)
		iregions = (mpuir & MPUIR_IREGION_SZMASK) >> MPUIR_IREGION;

	/* Use the smallest of the two maxima */
	return min(dregions, iregions);
}

static int __init mpu_iside_independent(void)
{
	/* MPUIR.nU specifies whether there is *not* a unified memory map */
	return read_cpuid_mputype() & MPUIR_nU;
}

static int __init __mpu_min_region_order(void)
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

static int __init mpu_setup_region(unsigned int number, phys_addr_t start,
			unsigned int size_order, unsigned int properties)
{
	u32 size_data;

	/* We kept a region free for probing resolution of MPU regions*/
	if (number > mpu_max_regions
	    || number >= MPU_MAX_REGIONS)
		return -ENOENT;

	if (size_order > 32)
		return -ENOMEM;

	if (size_order < mpu_min_region_order)
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

	mpu_rgn_info.used++;

	return 0;
}

/*
* Set up default MPU regions, doing nothing if there is no MPU
*/
void __init mpu_setup(void)
{
	int region = 0, err = 0;

	if (!mpu_present())
		return;

	/* Free-up MPU_PROBE_REGION */
	mpu_min_region_order = __mpu_min_region_order();

	/* How many regions are supported */
	mpu_max_regions = __mpu_max_regions();

	/* Now setup MPU (order is important) */

	/* Background */
	err |= mpu_setup_region(region++, 0, 32,
				MPU_ACR_XN | MPU_RGN_STRONGLY_ORDERED | MPU_AP_PL1RW_PL0NA);

	/* RAM */
	err |= mpu_setup_region(region++, PHYS_OFFSET,
				ilog2(memblock.memory.regions[0].size),
				MPU_AP_PL1RW_PL0RW | MPU_RGN_NORMAL);

	/* Vectors */
#ifndef CONFIG_CPU_V7M
	err |= mpu_setup_region(region++, vectors_base,
				ilog2(2 * PAGE_SIZE),
				MPU_AP_PL1RW_PL0NA | MPU_RGN_NORMAL);
#endif
	if (err) {
		panic("MPU region initialization failure! %d", err);
	} else {
		pr_info("Using ARMv7 PMSA Compliant MPU. "
			 "Region independence: %s, Used %d of %d regions\n",
			mpu_iside_independent() ? "Yes" : "No",
			mpu_rgn_info.used, mpu_max_regions);
	}
}
