/*
 * Based on linux/arch/arm/pmsa-v7.c
 *
 * ARM PMSAv8 supporting functions.
 */

#include <linux/memblock.h>
#include <linux/range.h>

#include <asm/cp15.h>
#include <asm/cputype.h>
#include <asm/mpu.h>

#include <asm/page.h>
#include <asm/sections.h>

#include "mm.h"

#ifndef CONFIG_CPU_V7M

#define PRSEL	__ACCESS_CP15(c6, 0, c2, 1)
#define PRBAR	__ACCESS_CP15(c6, 0, c3, 0)
#define PRLAR	__ACCESS_CP15(c6, 0, c3, 1)

static inline u32 prlar_read(void)
{
	return read_sysreg(PRLAR);
}

static inline u32 prbar_read(void)
{
	return read_sysreg(PRBAR);
}

static inline void prsel_write(u32 v)
{
	write_sysreg(v, PRSEL);
}

static inline void prbar_write(u32 v)
{
	write_sysreg(v, PRBAR);
}

static inline void prlar_write(u32 v)
{
	write_sysreg(v, PRLAR);
}
#else

static inline u32 prlar_read(void)
{
	return readl_relaxed(BASEADDR_V7M_SCB + PMSAv8_RLAR);
}

static inline u32 prbar_read(void)
{
	return readl_relaxed(BASEADDR_V7M_SCB + PMSAv8_RBAR);
}

static inline void prsel_write(u32 v)
{
	writel_relaxed(v, BASEADDR_V7M_SCB + PMSAv8_RNR);
}

static inline void prbar_write(u32 v)
{
	writel_relaxed(v, BASEADDR_V7M_SCB + PMSAv8_RBAR);
}

static inline void prlar_write(u32 v)
{
	writel_relaxed(v, BASEADDR_V7M_SCB + PMSAv8_RLAR);
}

#endif

static struct range __initdata io[MPU_MAX_REGIONS];
static struct range __initdata mem[MPU_MAX_REGIONS];

static unsigned int __initdata mpu_max_regions;

static __init bool is_region_fixed(int number)
{
	switch (number) {
	case PMSAv8_XIP_REGION:
	case PMSAv8_KERNEL_REGION:
		return true;
	default:
		return false;
	}
}

void __init pmsav8_adjust_lowmem_bounds(void)
{
	phys_addr_t mem_end;
	phys_addr_t reg_start, reg_end;
	bool first = true;
	u64 i;

	for_each_mem_range(i, &reg_start, &reg_end) {
		if (first) {
			phys_addr_t phys_offset = PHYS_OFFSET;

			/*
			 * Initially only use memory continuous from
			 * PHYS_OFFSET */
			if (reg_start != phys_offset)
				panic("First memory bank must be contiguous from PHYS_OFFSET");
			mem_end = reg_end;
			first = false;
		} else {
			/*
			 * memblock auto merges contiguous blocks, remove
			 * all blocks afterwards in one go (we can't remove
			 * blocks separately while iterating)
			 */
			pr_notice("Ignoring RAM after %pa, memory at %pa ignored\n",
				  &mem_end, &reg_start);
			memblock_remove(reg_start, 0 - reg_start);
			break;
		}
	}
}

static int __init __mpu_max_regions(void)
{
	static int max_regions;
	u32 mpuir;

	if (max_regions)
		return max_regions;

	mpuir = read_cpuid_mputype();

	max_regions  = (mpuir & MPUIR_DREGION_SZMASK) >> MPUIR_DREGION;

	return max_regions;
}

static int __init __pmsav8_setup_region(unsigned int number, u32 bar, u32 lar)
{
	if (number > mpu_max_regions
	    || number >= MPU_MAX_REGIONS)
		return -ENOENT;

	dsb();
	prsel_write(number);
	isb();
	prbar_write(bar);
	prlar_write(lar);

	mpu_rgn_info.rgns[number].prbar = bar;
	mpu_rgn_info.rgns[number].prlar = lar;

	mpu_rgn_info.used++;

	return 0;
}

static int __init pmsav8_setup_ram(unsigned int number, phys_addr_t start,phys_addr_t end)
{
	u32 bar, lar;

	if (is_region_fixed(number))
		return -EINVAL;

	bar = start;
	lar = (end - 1) & ~(PMSAv8_MINALIGN - 1);

	bar |= PMSAv8_AP_PL1RW_PL0RW | PMSAv8_RGN_SHARED;
	lar |= PMSAv8_LAR_IDX(PMSAv8_RGN_NORMAL) | PMSAv8_LAR_EN;

	return __pmsav8_setup_region(number, bar, lar);
}

static int __init pmsav8_setup_io(unsigned int number, phys_addr_t start,phys_addr_t end)
{
	u32 bar, lar;

	if (is_region_fixed(number))
		return -EINVAL;

	bar = start;
	lar = (end - 1) & ~(PMSAv8_MINALIGN - 1);

	bar |= PMSAv8_AP_PL1RW_PL0RW | PMSAv8_RGN_SHARED | PMSAv8_BAR_XN;
	lar |= PMSAv8_LAR_IDX(PMSAv8_RGN_DEVICE_nGnRnE) | PMSAv8_LAR_EN;

	return __pmsav8_setup_region(number, bar, lar);
}

static int __init pmsav8_setup_fixed(unsigned int number, phys_addr_t start,phys_addr_t end)
{
	u32 bar, lar;

	if (!is_region_fixed(number))
		return -EINVAL;

	bar = start;
	lar = (end - 1) & ~(PMSAv8_MINALIGN - 1);

	bar |= PMSAv8_AP_PL1RW_PL0NA | PMSAv8_RGN_SHARED;
	lar |= PMSAv8_LAR_IDX(PMSAv8_RGN_NORMAL) | PMSAv8_LAR_EN;

	prsel_write(number);
	isb();

	if (prbar_read() != bar || prlar_read() != lar)
		return -EINVAL;

	/* Reserved region was set up early, we just need a record for secondaries */
	mpu_rgn_info.rgns[number].prbar = bar;
	mpu_rgn_info.rgns[number].prlar = lar;

	mpu_rgn_info.used++;

	return 0;
}

#ifndef CONFIG_CPU_V7M
static int __init pmsav8_setup_vector(unsigned int number, phys_addr_t start,phys_addr_t end)
{
	u32 bar, lar;

	if (number == PMSAv8_KERNEL_REGION)
		return -EINVAL;

	bar = start;
	lar = (end - 1) & ~(PMSAv8_MINALIGN - 1);

	bar |= PMSAv8_AP_PL1RW_PL0NA | PMSAv8_RGN_SHARED;
	lar |= PMSAv8_LAR_IDX(PMSAv8_RGN_NORMAL) | PMSAv8_LAR_EN;

	return __pmsav8_setup_region(number, bar, lar);
}
#endif

void __init pmsav8_setup(void)
{
	int i, err = 0;
	int region = PMSAv8_KERNEL_REGION;

	/* How many regions are supported ? */
	mpu_max_regions = __mpu_max_regions();

	/* RAM: single chunk of memory */
	add_range(mem,  ARRAY_SIZE(mem), 0,  memblock.memory.regions[0].base,
		  memblock.memory.regions[0].base + memblock.memory.regions[0].size);

	/* IO: cover full 4G range */
	add_range(io, ARRAY_SIZE(io), 0, 0, 0xffffffff);

	/* RAM and IO: exclude kernel */
	subtract_range(mem, ARRAY_SIZE(mem), __pa(KERNEL_START), __pa(KERNEL_END));
	subtract_range(io, ARRAY_SIZE(io),  __pa(KERNEL_START), __pa(KERNEL_END));

#ifdef CONFIG_XIP_KERNEL
	/* RAM and IO: exclude xip */
	subtract_range(mem, ARRAY_SIZE(mem), CONFIG_XIP_PHYS_ADDR, __pa(_exiprom));
	subtract_range(io, ARRAY_SIZE(io), CONFIG_XIP_PHYS_ADDR, __pa(_exiprom));
#endif

#ifndef CONFIG_CPU_V7M
	/* RAM and IO: exclude vectors */
	subtract_range(mem, ARRAY_SIZE(mem),  vectors_base, vectors_base + 2 * PAGE_SIZE);
	subtract_range(io, ARRAY_SIZE(io),  vectors_base, vectors_base + 2 * PAGE_SIZE);
#endif
	/* IO: exclude RAM */
	for (i = 0; i < ARRAY_SIZE(mem); i++)
		subtract_range(io, ARRAY_SIZE(io), mem[i].start, mem[i].end);

	/* Now program MPU */

#ifdef CONFIG_XIP_KERNEL
	/* ROM */
	err |= pmsav8_setup_fixed(PMSAv8_XIP_REGION, CONFIG_XIP_PHYS_ADDR, __pa(_exiprom));
#endif
	/* Kernel */
	err |= pmsav8_setup_fixed(region++, __pa(KERNEL_START), __pa(KERNEL_END));


	/* IO */
	for (i = 0; i < ARRAY_SIZE(io); i++) {
		if (!io[i].end)
			continue;

		err |= pmsav8_setup_io(region++, io[i].start, io[i].end);
	}

	/* RAM */
	for (i = 0; i < ARRAY_SIZE(mem); i++) {
		if (!mem[i].end)
			continue;

		err |= pmsav8_setup_ram(region++, mem[i].start, mem[i].end);
	}

	/* Vectors */
#ifndef CONFIG_CPU_V7M
	err |= pmsav8_setup_vector(region++, vectors_base, vectors_base + 2 * PAGE_SIZE);
#endif
	if (err)
		pr_warn("MPU region initialization failure! %d", err);
	else
		pr_info("Using ARM PMSAv8 Compliant MPU. Used %d of %d regions\n",
			mpu_rgn_info.used, mpu_max_regions);
}
