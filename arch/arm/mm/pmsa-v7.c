/*
 * Based on linux/arch/arm/mm/nommu.c
 *
 * ARM PMSAv7 supporting functions.
 */

#include <linux/bitops.h>
#include <linux/memblock.h>
#include <linux/string.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/cputype.h>
#include <asm/mpu.h>
#include <asm/sections.h>

#include "mm.h"

struct region {
	phys_addr_t base;
	phys_addr_t size;
	unsigned long subreg;
};

static struct region __initdata mem[MPU_MAX_REGIONS];
#ifdef CONFIG_XIP_KERNEL
static struct region __initdata xip[MPU_MAX_REGIONS];
#endif

static unsigned int __initdata mpu_min_region_order;
static unsigned int __initdata mpu_max_regions;

static int __init __mpu_min_region_order(void);
static int __init __mpu_max_regions(void);

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

static bool __init try_split_region(phys_addr_t base, phys_addr_t size, struct region *region)
{
	unsigned long  subreg, bslots, sslots;
	phys_addr_t abase = base & ~(size - 1);
	phys_addr_t asize = base + size - abase;
	phys_addr_t p2size = 1 << __fls(asize);
	phys_addr_t bdiff, sdiff;

	if (p2size != asize)
		p2size *= 2;

	bdiff = base - abase;
	sdiff = p2size - asize;
	subreg = p2size / MPU_NR_SUBREGS;

	if ((bdiff % subreg) || (sdiff % subreg))
		return false;

	bslots = bdiff / subreg;
	sslots = sdiff / subreg;

	if (bslots || sslots) {
		int i;

		if (subreg < MPU_MIN_SUBREG_SIZE)
			return false;

		if (bslots + sslots > MPU_NR_SUBREGS)
			return false;

		for (i = 0; i < bslots; i++)
			_set_bit(i, &region->subreg);

		for (i = 1; i <= sslots; i++)
			_set_bit(MPU_NR_SUBREGS - i, &region->subreg);
	}

	region->base = abase;
	region->size = p2size;

	return true;
}

static int __init allocate_region(phys_addr_t base, phys_addr_t size,
				  unsigned int limit, struct region *regions)
{
	int count = 0;
	phys_addr_t diff = size;
	int attempts = MPU_MAX_REGIONS;

	while (diff) {
		/* Try cover region as is (maybe with help of subregions) */
		if (try_split_region(base, size, &regions[count])) {
			count++;
			base += size;
			diff -= size;
			size = diff;
		} else {
			/*
			 * Maximum aligned region might overflow phys_addr_t
			 * if "base" is 0. Hence we keep everything below 4G
			 * until we take the smaller of the aligned region
			 * size ("asize") and rounded region size ("p2size"),
			 * one of which is guaranteed to be smaller than the
			 * maximum physical address.
			 */
			phys_addr_t asize = (base - 1) ^ base;
			phys_addr_t p2size = (1 <<  __fls(diff)) - 1;

			size = asize < p2size ? asize + 1 : p2size + 1;
		}

		if (count > limit)
			break;

		if (!attempts)
			break;

		attempts--;
	}

	return count;
}

/* MPU initialisation functions */
void __init adjust_lowmem_bounds_mpu(void)
{
	phys_addr_t  specified_mem_size = 0, total_mem_size = 0;
	struct memblock_region *reg;
	bool first = true;
	phys_addr_t mem_start;
	phys_addr_t mem_end;
	unsigned int mem_max_regions;
	int num, i;

	if (!mpu_present())
		return;

	/* Free-up MPU_PROBE_REGION */
	mpu_min_region_order = __mpu_min_region_order();

	/* How many regions are supported */
	mpu_max_regions = __mpu_max_regions();

	mem_max_regions = min((unsigned int)MPU_MAX_REGIONS, mpu_max_regions);

	/* We need to keep one slot for background region */
	mem_max_regions--;

#ifndef CONFIG_CPU_V7M
	/* ... and one for vectors */
	mem_max_regions--;
#endif

#ifdef CONFIG_XIP_KERNEL
	/* plus some regions to cover XIP ROM */
	num = allocate_region(CONFIG_XIP_PHYS_ADDR, __pa(_exiprom) - CONFIG_XIP_PHYS_ADDR,
			      mem_max_regions, xip);

	mem_max_regions -= num;
#endif

	for_each_memblock(memory, reg) {
		if (first) {
			phys_addr_t phys_offset = PHYS_OFFSET;

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

	memset(mem, 0, sizeof(mem));
	num = allocate_region(mem_start, specified_mem_size, mem_max_regions, mem);

	for (i = 0; i < num; i++) {
		unsigned long  subreg = mem[i].size / MPU_NR_SUBREGS;

		total_mem_size += mem[i].size - subreg * hweight_long(mem[i].subreg);

		pr_debug("MPU: base %pa size %pa disable subregions: %*pbl\n",
			 &mem[i].base, &mem[i].size, MPU_NR_SUBREGS, &mem[i].subreg);
	}

	if (total_mem_size != specified_mem_size) {
		pr_warn("Truncating memory from %pa to %pa (MPU region constraints)",
				&specified_mem_size, &total_mem_size);
		memblock_remove(mem_start + total_mem_size,
				specified_mem_size - total_mem_size);
	}
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
				   unsigned int size_order, unsigned int properties,
				   unsigned int subregions, bool need_flush)
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
	size_data |= subregions << MPU_RSR_SD;

	if (need_flush)
		flush_cache_all();

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
	int i, region = 0, err = 0;

	if (!mpu_present())
		return;

	/* Setup MPU (order is important) */

	/* Background */
	err |= mpu_setup_region(region++, 0, 32,
				MPU_ACR_XN | MPU_RGN_STRONGLY_ORDERED | MPU_AP_PL1RW_PL0RW,
				0, false);

#ifdef CONFIG_XIP_KERNEL
	/* ROM */
	for (i = 0; i < ARRAY_SIZE(xip); i++) {
		/*
                 * In case we overwrite RAM region we set earlier in
                 * head-nommu.S (which is cachable) all subsequent
                 * data access till we setup RAM bellow would be done
                 * with BG region (which is uncachable), thus we need
                 * to clean and invalidate cache.
		 */
		bool need_flush = region == MPU_RAM_REGION;

		if (!xip[i].size)
			continue;

		err |= mpu_setup_region(region++, xip[i].base, ilog2(xip[i].size),
					MPU_AP_PL1RO_PL0NA | MPU_RGN_NORMAL,
					xip[i].subreg, need_flush);
	}
#endif

	/* RAM */
	for (i = 0; i < ARRAY_SIZE(mem); i++) {
		if (!mem[i].size)
			continue;

		err |= mpu_setup_region(region++, mem[i].base, ilog2(mem[i].size),
					MPU_AP_PL1RW_PL0RW | MPU_RGN_NORMAL,
					mem[i].subreg, false);
	}

	/* Vectors */
#ifndef CONFIG_CPU_V7M
	err |= mpu_setup_region(region++, vectors_base, ilog2(2 * PAGE_SIZE),
				MPU_AP_PL1RW_PL0NA | MPU_RGN_NORMAL,
				0, false);
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
