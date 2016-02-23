/*
 * Cache control for MicroBlaze cache memories
 *
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2007-2009 John Williams <john.williams@petalogix.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <asm/cacheflush.h>
#include <linux/cache.h>
#include <asm/cpuinfo.h>
#include <asm/pvr.h>

static inline void __enable_icache_msr(void)
{
	__asm__ __volatile__ ("	 msrset	r0, %0;"	\
				"nop;"			\
			: : "i" (MSR_ICE) : "memory");
}

static inline void __disable_icache_msr(void)
{
	__asm__ __volatile__ ("	 msrclr	r0, %0;"	\
				"nop;"			\
			: : "i" (MSR_ICE) : "memory");
}

static inline void __enable_dcache_msr(void)
{
	__asm__ __volatile__ ("	 msrset	r0, %0;"	\
				"nop;"			\
			: : "i" (MSR_DCE) : "memory");
}

static inline void __disable_dcache_msr(void)
{
	__asm__ __volatile__ ("	 msrclr	r0, %0;"	\
				"nop; "			\
			: : "i" (MSR_DCE) : "memory");
}

static inline void __enable_icache_nomsr(void)
{
	__asm__ __volatile__ ("	 mfs	r12, rmsr;"	\
				"nop;"			\
				"ori	r12, r12, %0;"	\
				"mts	rmsr, r12;"	\
				"nop;"			\
			: : "i" (MSR_ICE) : "memory", "r12");
}

static inline void __disable_icache_nomsr(void)
{
	__asm__ __volatile__ ("	 mfs	r12, rmsr;"	\
				"nop;"			\
				"andi	r12, r12, ~%0;"	\
				"mts	rmsr, r12;"	\
				"nop;"			\
			: : "i" (MSR_ICE) : "memory", "r12");
}

static inline void __enable_dcache_nomsr(void)
{
	__asm__ __volatile__ ("	 mfs	r12, rmsr;"	\
				"nop;"			\
				"ori	r12, r12, %0;"	\
				"mts	rmsr, r12;"	\
				"nop;"			\
			: : "i" (MSR_DCE) : "memory", "r12");
}

static inline void __disable_dcache_nomsr(void)
{
	__asm__ __volatile__ ("	 mfs	r12, rmsr;"	\
				"nop;"			\
				"andi	r12, r12, ~%0;"	\
				"mts	rmsr, r12;"	\
				"nop;"			\
			: : "i" (MSR_DCE) : "memory", "r12");
}


/* Helper macro for computing the limits of cache range loops
 *
 * End address can be unaligned which is OK for C implementation.
 * ASM implementation align it in ASM macros
 */
#define CACHE_LOOP_LIMITS(start, end, cache_line_length, cache_size)	\
do {									\
	int align = ~(cache_line_length - 1);				\
	end = min(start + cache_size, end);				\
	start &= align;							\
} while (0)

/*
 * Helper macro to loop over the specified cache_size/line_length and
 * execute 'op' on that cacheline
 */
#define CACHE_ALL_LOOP(cache_size, line_length, op)			\
do {									\
	unsigned int len = cache_size - line_length;			\
	int step = -line_length;					\
	WARN_ON(step >= 0);						\
									\
	__asm__ __volatile__ (" 1:      " #op " %0, r0;"		\
					"bgtid   %0, 1b;"		\
					"addk    %0, %0, %1;"		\
					: : "r" (len), "r" (step)	\
					: "memory");			\
} while (0)

/* Used for wdc.flush/clear which can use rB for offset which is not possible
 * to use for simple wdc or wic.
 *
 * start address is cache aligned
 * end address is not aligned, if end is aligned then I have to subtract
 * cacheline length because I can't flush/invalidate the next cacheline.
 * If is not, I align it because I will flush/invalidate whole line.
 */
#define CACHE_RANGE_LOOP_2(start, end, line_length, op)			\
do {									\
	int step = -line_length;					\
	int align = ~(line_length - 1);					\
	int count;							\
	end = ((end & align) == end) ? end - line_length : end & align;	\
	count = end - start;						\
	WARN_ON(count < 0);						\
									\
	__asm__ __volatile__ (" 1:	" #op "	%0, %1;"		\
					"bgtid	%1, 1b;"		\
					"addk	%1, %1, %2;"		\
					: : "r" (start), "r" (count),	\
					"r" (step) : "memory");		\
} while (0)

/* It is used only first parameter for OP - for wic, wdc */
#define CACHE_RANGE_LOOP_1(start, end, line_length, op)			\
do {									\
	unsigned int volatile temp = 0;						\
	unsigned int align = ~(line_length - 1);					\
	end = ((end & align) == end) ? end - line_length : end & align;	\
	WARN_ON(end < start);					\
									\
	__asm__ __volatile__ (" 1:	" #op "	%1, r0;"		\
					"cmpu	%0, %1, %2;"		\
					"bgtid	%0, 1b;"		\
					"addk	%1, %1, %3;"		\
				: : "r" (temp), "r" (start), "r" (end),	\
					"r" (line_length) : "memory");	\
} while (0)

#define ASM_LOOP

static void __flush_icache_range_msr_irq(unsigned long start, unsigned long end)
{
	unsigned long flags;
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s: start 0x%x, end 0x%x\n", __func__,
				(unsigned int)start, (unsigned int) end);

	CACHE_LOOP_LIMITS(start, end,
			cpuinfo.icache_line_length, cpuinfo.icache_size);

	local_irq_save(flags);
	__disable_icache_msr();

#ifdef ASM_LOOP
	CACHE_RANGE_LOOP_1(start, end, cpuinfo.icache_line_length, wic);
#else
	for (i = start; i < end; i += cpuinfo.icache_line_length)
		__asm__ __volatile__ ("wic	%0, r0;"	\
				: : "r" (i));
#endif
	__enable_icache_msr();
	local_irq_restore(flags);
}

static void __flush_icache_range_nomsr_irq(unsigned long start,
				unsigned long end)
{
	unsigned long flags;
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s: start 0x%x, end 0x%x\n", __func__,
				(unsigned int)start, (unsigned int) end);

	CACHE_LOOP_LIMITS(start, end,
			cpuinfo.icache_line_length, cpuinfo.icache_size);

	local_irq_save(flags);
	__disable_icache_nomsr();

#ifdef ASM_LOOP
	CACHE_RANGE_LOOP_1(start, end, cpuinfo.icache_line_length, wic);
#else
	for (i = start; i < end; i += cpuinfo.icache_line_length)
		__asm__ __volatile__ ("wic	%0, r0;"	\
				: : "r" (i));
#endif

	__enable_icache_nomsr();
	local_irq_restore(flags);
}

static void __flush_icache_range_noirq(unsigned long start,
				unsigned long end)
{
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s: start 0x%x, end 0x%x\n", __func__,
				(unsigned int)start, (unsigned int) end);

	CACHE_LOOP_LIMITS(start, end,
			cpuinfo.icache_line_length, cpuinfo.icache_size);
#ifdef ASM_LOOP
	CACHE_RANGE_LOOP_1(start, end, cpuinfo.icache_line_length, wic);
#else
	for (i = start; i < end; i += cpuinfo.icache_line_length)
		__asm__ __volatile__ ("wic	%0, r0;"	\
				: : "r" (i));
#endif
}

static void __flush_icache_all_msr_irq(void)
{
	unsigned long flags;
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s\n", __func__);

	local_irq_save(flags);
	__disable_icache_msr();
#ifdef ASM_LOOP
	CACHE_ALL_LOOP(cpuinfo.icache_size, cpuinfo.icache_line_length, wic);
#else
	for (i = 0; i < cpuinfo.icache_size;
		 i += cpuinfo.icache_line_length)
			__asm__ __volatile__ ("wic	%0, r0;" \
					: : "r" (i));
#endif
	__enable_icache_msr();
	local_irq_restore(flags);
}

static void __flush_icache_all_nomsr_irq(void)
{
	unsigned long flags;
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s\n", __func__);

	local_irq_save(flags);
	__disable_icache_nomsr();
#ifdef ASM_LOOP
	CACHE_ALL_LOOP(cpuinfo.icache_size, cpuinfo.icache_line_length, wic);
#else
	for (i = 0; i < cpuinfo.icache_size;
		 i += cpuinfo.icache_line_length)
			__asm__ __volatile__ ("wic	%0, r0;" \
					: : "r" (i));
#endif
	__enable_icache_nomsr();
	local_irq_restore(flags);
}

static void __flush_icache_all_noirq(void)
{
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s\n", __func__);
#ifdef ASM_LOOP
	CACHE_ALL_LOOP(cpuinfo.icache_size, cpuinfo.icache_line_length, wic);
#else
	for (i = 0; i < cpuinfo.icache_size;
		 i += cpuinfo.icache_line_length)
			__asm__ __volatile__ ("wic	%0, r0;" \
					: : "r" (i));
#endif
}

static void __invalidate_dcache_all_msr_irq(void)
{
	unsigned long flags;
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s\n", __func__);

	local_irq_save(flags);
	__disable_dcache_msr();
#ifdef ASM_LOOP
	CACHE_ALL_LOOP(cpuinfo.dcache_size, cpuinfo.dcache_line_length, wdc);
#else
	for (i = 0; i < cpuinfo.dcache_size;
		 i += cpuinfo.dcache_line_length)
			__asm__ __volatile__ ("wdc	%0, r0;" \
					: : "r" (i));
#endif
	__enable_dcache_msr();
	local_irq_restore(flags);
}

static void __invalidate_dcache_all_nomsr_irq(void)
{
	unsigned long flags;
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s\n", __func__);

	local_irq_save(flags);
	__disable_dcache_nomsr();
#ifdef ASM_LOOP
	CACHE_ALL_LOOP(cpuinfo.dcache_size, cpuinfo.dcache_line_length, wdc);
#else
	for (i = 0; i < cpuinfo.dcache_size;
		 i += cpuinfo.dcache_line_length)
			__asm__ __volatile__ ("wdc	%0, r0;" \
					: : "r" (i));
#endif
	__enable_dcache_nomsr();
	local_irq_restore(flags);
}

static void __invalidate_dcache_all_noirq_wt(void)
{
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s\n", __func__);
#ifdef ASM_LOOP
	CACHE_ALL_LOOP(cpuinfo.dcache_size, cpuinfo.dcache_line_length, wdc);
#else
	for (i = 0; i < cpuinfo.dcache_size;
		 i += cpuinfo.dcache_line_length)
			__asm__ __volatile__ ("wdc	%0, r0;" \
					: : "r" (i));
#endif
}

/*
 * FIXME It is blindly invalidation as is expected
 * but can't be called on noMMU in microblaze_cache_init below
 *
 * MS: noMMU kernel won't boot if simple wdc is used
 * The reason should be that there are discared data which kernel needs
 */
static void __invalidate_dcache_all_wb(void)
{
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s\n", __func__);
#ifdef ASM_LOOP
	CACHE_ALL_LOOP(cpuinfo.dcache_size, cpuinfo.dcache_line_length,
					wdc);
#else
	for (i = 0; i < cpuinfo.dcache_size;
		 i += cpuinfo.dcache_line_length)
			__asm__ __volatile__ ("wdc	%0, r0;" \
					: : "r" (i));
#endif
}

static void __invalidate_dcache_range_wb(unsigned long start,
						unsigned long end)
{
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s: start 0x%x, end 0x%x\n", __func__,
				(unsigned int)start, (unsigned int) end);

	CACHE_LOOP_LIMITS(start, end,
			cpuinfo.dcache_line_length, cpuinfo.dcache_size);
#ifdef ASM_LOOP
	CACHE_RANGE_LOOP_2(start, end, cpuinfo.dcache_line_length, wdc.clear);
#else
	for (i = start; i < end; i += cpuinfo.dcache_line_length)
		__asm__ __volatile__ ("wdc.clear	%0, r0;"	\
				: : "r" (i));
#endif
}

static void __invalidate_dcache_range_nomsr_wt(unsigned long start,
							unsigned long end)
{
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s: start 0x%x, end 0x%x\n", __func__,
				(unsigned int)start, (unsigned int) end);
	CACHE_LOOP_LIMITS(start, end,
			cpuinfo.dcache_line_length, cpuinfo.dcache_size);

#ifdef ASM_LOOP
	CACHE_RANGE_LOOP_1(start, end, cpuinfo.dcache_line_length, wdc);
#else
	for (i = start; i < end; i += cpuinfo.dcache_line_length)
		__asm__ __volatile__ ("wdc	%0, r0;"	\
				: : "r" (i));
#endif
}

static void __invalidate_dcache_range_msr_irq_wt(unsigned long start,
							unsigned long end)
{
	unsigned long flags;
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s: start 0x%x, end 0x%x\n", __func__,
				(unsigned int)start, (unsigned int) end);
	CACHE_LOOP_LIMITS(start, end,
			cpuinfo.dcache_line_length, cpuinfo.dcache_size);

	local_irq_save(flags);
	__disable_dcache_msr();

#ifdef ASM_LOOP
	CACHE_RANGE_LOOP_1(start, end, cpuinfo.dcache_line_length, wdc);
#else
	for (i = start; i < end; i += cpuinfo.dcache_line_length)
		__asm__ __volatile__ ("wdc	%0, r0;"	\
				: : "r" (i));
#endif

	__enable_dcache_msr();
	local_irq_restore(flags);
}

static void __invalidate_dcache_range_nomsr_irq(unsigned long start,
							unsigned long end)
{
	unsigned long flags;
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s: start 0x%x, end 0x%x\n", __func__,
				(unsigned int)start, (unsigned int) end);

	CACHE_LOOP_LIMITS(start, end,
			cpuinfo.dcache_line_length, cpuinfo.dcache_size);

	local_irq_save(flags);
	__disable_dcache_nomsr();

#ifdef ASM_LOOP
	CACHE_RANGE_LOOP_1(start, end, cpuinfo.dcache_line_length, wdc);
#else
	for (i = start; i < end; i += cpuinfo.dcache_line_length)
		__asm__ __volatile__ ("wdc	%0, r0;"	\
				: : "r" (i));
#endif

	__enable_dcache_nomsr();
	local_irq_restore(flags);
}

static void __flush_dcache_all_wb(void)
{
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s\n", __func__);
#ifdef ASM_LOOP
	CACHE_ALL_LOOP(cpuinfo.dcache_size, cpuinfo.dcache_line_length,
				wdc.flush);
#else
	for (i = 0; i < cpuinfo.dcache_size;
		 i += cpuinfo.dcache_line_length)
			__asm__ __volatile__ ("wdc.flush	%0, r0;" \
					: : "r" (i));
#endif
}

static void __flush_dcache_range_wb(unsigned long start, unsigned long end)
{
#ifndef ASM_LOOP
	int i;
#endif
	pr_debug("%s: start 0x%x, end 0x%x\n", __func__,
				(unsigned int)start, (unsigned int) end);

	CACHE_LOOP_LIMITS(start, end,
			cpuinfo.dcache_line_length, cpuinfo.dcache_size);
#ifdef ASM_LOOP
	CACHE_RANGE_LOOP_2(start, end, cpuinfo.dcache_line_length, wdc.flush);
#else
	for (i = start; i < end; i += cpuinfo.dcache_line_length)
		__asm__ __volatile__ ("wdc.flush	%0, r0;"	\
				: : "r" (i));
#endif
}

/* struct for wb caches and for wt caches */
struct scache *mbc;

/* new wb cache model */
static const struct scache wb_msr = {
	.ie = __enable_icache_msr,
	.id = __disable_icache_msr,
	.ifl = __flush_icache_all_noirq,
	.iflr = __flush_icache_range_noirq,
	.iin = __flush_icache_all_noirq,
	.iinr = __flush_icache_range_noirq,
	.de = __enable_dcache_msr,
	.dd = __disable_dcache_msr,
	.dfl = __flush_dcache_all_wb,
	.dflr = __flush_dcache_range_wb,
	.din = __invalidate_dcache_all_wb,
	.dinr = __invalidate_dcache_range_wb,
};

/* There is only difference in ie, id, de, dd functions */
static const struct scache wb_nomsr = {
	.ie = __enable_icache_nomsr,
	.id = __disable_icache_nomsr,
	.ifl = __flush_icache_all_noirq,
	.iflr = __flush_icache_range_noirq,
	.iin = __flush_icache_all_noirq,
	.iinr = __flush_icache_range_noirq,
	.de = __enable_dcache_nomsr,
	.dd = __disable_dcache_nomsr,
	.dfl = __flush_dcache_all_wb,
	.dflr = __flush_dcache_range_wb,
	.din = __invalidate_dcache_all_wb,
	.dinr = __invalidate_dcache_range_wb,
};

/* Old wt cache model with disabling irq and turn off cache */
static const struct scache wt_msr = {
	.ie = __enable_icache_msr,
	.id = __disable_icache_msr,
	.ifl = __flush_icache_all_msr_irq,
	.iflr = __flush_icache_range_msr_irq,
	.iin = __flush_icache_all_msr_irq,
	.iinr = __flush_icache_range_msr_irq,
	.de = __enable_dcache_msr,
	.dd = __disable_dcache_msr,
	.dfl = __invalidate_dcache_all_msr_irq,
	.dflr = __invalidate_dcache_range_msr_irq_wt,
	.din = __invalidate_dcache_all_msr_irq,
	.dinr = __invalidate_dcache_range_msr_irq_wt,
};

static const struct scache wt_nomsr = {
	.ie = __enable_icache_nomsr,
	.id = __disable_icache_nomsr,
	.ifl = __flush_icache_all_nomsr_irq,
	.iflr = __flush_icache_range_nomsr_irq,
	.iin = __flush_icache_all_nomsr_irq,
	.iinr = __flush_icache_range_nomsr_irq,
	.de = __enable_dcache_nomsr,
	.dd = __disable_dcache_nomsr,
	.dfl = __invalidate_dcache_all_nomsr_irq,
	.dflr = __invalidate_dcache_range_nomsr_irq,
	.din = __invalidate_dcache_all_nomsr_irq,
	.dinr = __invalidate_dcache_range_nomsr_irq,
};

/* New wt cache model for newer Microblaze versions */
static const struct scache wt_msr_noirq = {
	.ie = __enable_icache_msr,
	.id = __disable_icache_msr,
	.ifl = __flush_icache_all_noirq,
	.iflr = __flush_icache_range_noirq,
	.iin = __flush_icache_all_noirq,
	.iinr = __flush_icache_range_noirq,
	.de = __enable_dcache_msr,
	.dd = __disable_dcache_msr,
	.dfl = __invalidate_dcache_all_noirq_wt,
	.dflr = __invalidate_dcache_range_nomsr_wt,
	.din = __invalidate_dcache_all_noirq_wt,
	.dinr = __invalidate_dcache_range_nomsr_wt,
};

static const struct scache wt_nomsr_noirq = {
	.ie = __enable_icache_nomsr,
	.id = __disable_icache_nomsr,
	.ifl = __flush_icache_all_noirq,
	.iflr = __flush_icache_range_noirq,
	.iin = __flush_icache_all_noirq,
	.iinr = __flush_icache_range_noirq,
	.de = __enable_dcache_nomsr,
	.dd = __disable_dcache_nomsr,
	.dfl = __invalidate_dcache_all_noirq_wt,
	.dflr = __invalidate_dcache_range_nomsr_wt,
	.din = __invalidate_dcache_all_noirq_wt,
	.dinr = __invalidate_dcache_range_nomsr_wt,
};

/* CPU version code for 7.20.c - see arch/microblaze/kernel/cpu/cpuinfo.c */
#define CPUVER_7_20_A	0x0c
#define CPUVER_7_20_D	0x0f

void microblaze_cache_init(void)
{
	if (cpuinfo.use_instr & PVR2_USE_MSR_INSTR) {
		if (cpuinfo.dcache_wb) {
			pr_info("wb_msr\n");
			mbc = (struct scache *)&wb_msr;
			if (cpuinfo.ver_code <= CPUVER_7_20_D) {
				/* MS: problem with signal handling - hw bug */
				pr_info("WB won't work properly\n");
			}
		} else {
			if (cpuinfo.ver_code >= CPUVER_7_20_A) {
				pr_info("wt_msr_noirq\n");
				mbc = (struct scache *)&wt_msr_noirq;
			} else {
				pr_info("wt_msr\n");
				mbc = (struct scache *)&wt_msr;
			}
		}
	} else {
		if (cpuinfo.dcache_wb) {
			pr_info("wb_nomsr\n");
			mbc = (struct scache *)&wb_nomsr;
			if (cpuinfo.ver_code <= CPUVER_7_20_D) {
				/* MS: problem with signal handling - hw bug */
				pr_info("WB won't work properly\n");
			}
		} else {
			if (cpuinfo.ver_code >= CPUVER_7_20_A) {
				pr_info("wt_nomsr_noirq\n");
				mbc = (struct scache *)&wt_nomsr_noirq;
			} else {
				pr_info("wt_nomsr\n");
				mbc = (struct scache *)&wt_nomsr;
			}
		}
	}
	/*
	 * FIXME Invalidation is done in U-BOOT
	 * WT cache: Data is already written to main memory
	 * WB cache: Discard data on noMMU which caused that kernel doesn't boot
	 */
	/* invalidate_dcache(); */
	enable_dcache();

	invalidate_icache();
	enable_icache();
}
