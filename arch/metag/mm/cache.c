/*
 * arch/metag/mm/cache.c
 *
 * Copyright (C) 2001, 2002, 2005, 2007, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Cache control code
 */

#include <linux/export.h>
#include <linux/io.h>
#include <asm/cacheflush.h>
#include <asm/core_reg.h>
#include <asm/global_lock.h>
#include <asm/metag_isa.h>
#include <asm/metag_mem.h>
#include <asm/metag_regs.h>

#define DEFAULT_CACHE_WAYS_LOG2	2

/*
 * Size of a set in the caches. Initialised for default 16K stride, adjusted
 * according to values passed through TBI global heap segment via LDLK (on ATP)
 * or config registers (on HTP/MTP)
 */
static int dcache_set_shift = METAG_TBI_CACHE_SIZE_BASE_LOG2
					- DEFAULT_CACHE_WAYS_LOG2;
static int icache_set_shift = METAG_TBI_CACHE_SIZE_BASE_LOG2
					- DEFAULT_CACHE_WAYS_LOG2;
/*
 * The number of sets in the caches. Initialised for HTP/ATP, adjusted
 * according to NOMMU setting in config registers
 */
static unsigned char dcache_sets_log2 = DEFAULT_CACHE_WAYS_LOG2;
static unsigned char icache_sets_log2 = DEFAULT_CACHE_WAYS_LOG2;

#ifndef CONFIG_METAG_META12
/**
 * metag_lnkget_probe() - Probe whether lnkget/lnkset go around the cache
 */
static volatile u32 lnkget_testdata[16] __initdata __aligned(64);

#define LNKGET_CONSTANT 0xdeadbeef

void __init metag_lnkget_probe(void)
{
	int temp;
	long flags;

	/*
	 * It's conceivable the user has configured a globally coherent cache
	 * shared with non-Linux hardware threads, so use LOCK2 to prevent them
	 * from executing and causing cache eviction during the test.
	 */
	__global_lock2(flags);

	/* read a value to bring it into the cache */
	(void)lnkget_testdata[0];
	lnkget_testdata[0] = 0;

	/* lnkget/lnkset it to modify it */
	asm volatile(
		"1:	LNKGETD %0, [%1]\n"
		"	LNKSETD [%1], %2\n"
		"	DEFR	%0, TXSTAT\n"
		"	ANDT	%0, %0, #HI(0x3f000000)\n"
		"	CMPT	%0, #HI(0x02000000)\n"
		"	BNZ	1b\n"
		: "=&d" (temp)
		: "da" (&lnkget_testdata[0]), "bd" (LNKGET_CONSTANT)
		: "cc");

	/* re-read it to see if the cached value changed */
	temp = lnkget_testdata[0];

	__global_unlock2(flags);

	/* flush the cache line to fix any incoherency */
	__builtin_dcache_flush((void *)&lnkget_testdata[0]);

#if defined(CONFIG_METAG_LNKGET_AROUND_CACHE)
	/* if the cache is right, LNKGET_AROUND_CACHE is unnecessary */
	if (temp == LNKGET_CONSTANT)
		pr_info("LNKGET/SET go through cache but CONFIG_METAG_LNKGET_AROUND_CACHE=y\n");
#elif defined(CONFIG_METAG_ATOMICITY_LNKGET)
	/*
	 * if the cache is wrong, LNKGET_AROUND_CACHE is really necessary
	 * because the kernel is configured to use LNKGET/SET for atomicity
	 */
	WARN(temp != LNKGET_CONSTANT,
	     "LNKGET/SET go around cache but CONFIG_METAG_LNKGET_AROUND_CACHE=n\n"
	     "Expect kernel failure as it's used for atomicity primitives\n");
#elif defined(CONFIG_SMP)
	/*
	 * if the cache is wrong, LNKGET_AROUND_CACHE should be used or the
	 * gateway page won't flush and userland could break.
	 */
	WARN(temp != LNKGET_CONSTANT,
	     "LNKGET/SET go around cache but CONFIG_METAG_LNKGET_AROUND_CACHE=n\n"
	     "Expect userland failure as it's used for user gateway page\n");
#else
	/*
	 * if the cache is wrong, LNKGET_AROUND_CACHE is set wrong, but it
	 * doesn't actually matter as it doesn't have any effect on !SMP &&
	 * !ATOMICITY_LNKGET.
	 */
	if (temp != LNKGET_CONSTANT)
		pr_warn("LNKGET/SET go around cache but CONFIG_METAG_LNKGET_AROUND_CACHE=n\n");
#endif
}
#endif /* !CONFIG_METAG_META12 */

/**
 * metag_cache_probe() - Probe L1 cache configuration.
 *
 * Probe the L1 cache configuration to aid the L1 physical cache flushing
 * functions.
 */
void __init metag_cache_probe(void)
{
#ifndef CONFIG_METAG_META12
	int coreid = metag_in32(METAC_CORE_ID);
	int config = metag_in32(METAC_CORE_CONFIG2);
	int cfgcache = coreid & METAC_COREID_CFGCACHE_BITS;

	if (cfgcache == METAC_COREID_CFGCACHE_TYPE0 ||
	    cfgcache == METAC_COREID_CFGCACHE_PRIVNOMMU) {
		icache_sets_log2 = 1;
		dcache_sets_log2 = 1;
	}

	/* For normal size caches, the smallest size is 4Kb.
	   For small caches, the smallest size is 64b */
	icache_set_shift = (config & METAC_CORECFG2_ICSMALL_BIT)
				? 6 : 12;
	icache_set_shift += (config & METAC_CORE_C2ICSZ_BITS)
				>> METAC_CORE_C2ICSZ_S;
	icache_set_shift -= icache_sets_log2;

	dcache_set_shift = (config & METAC_CORECFG2_DCSMALL_BIT)
				? 6 : 12;
	dcache_set_shift += (config & METAC_CORECFG2_DCSZ_BITS)
				>> METAC_CORECFG2_DCSZ_S;
	dcache_set_shift -= dcache_sets_log2;

	metag_lnkget_probe();
#else
	/* Extract cache sizes from global heap segment */
	unsigned long val, u;
	int width, shift, addend;
	PTBISEG seg;

	seg = __TBIFindSeg(NULL, TBID_SEG(TBID_THREAD_GLOBAL,
					  TBID_SEGSCOPE_GLOBAL,
					  TBID_SEGTYPE_HEAP));
	if (seg != NULL) {
		val = seg->Data[1];

		/* Work out width of I-cache size bit-field */
		u = ((unsigned long) METAG_TBI_ICACHE_SIZE_BITS)
		       >> METAG_TBI_ICACHE_SIZE_S;
		width = 0;
		while (u & 1) {
			width++;
			u >>= 1;
		}
		/* Extract sign-extended size addend value */
		shift = 32 - (METAG_TBI_ICACHE_SIZE_S + width);
		addend = (long) ((val & METAG_TBI_ICACHE_SIZE_BITS)
				 << shift)
			>> (shift + METAG_TBI_ICACHE_SIZE_S);
		/* Now calculate I-cache set size */
		icache_set_shift = (METAG_TBI_CACHE_SIZE_BASE_LOG2
				    - DEFAULT_CACHE_WAYS_LOG2)
					+ addend;

		/* Similarly for D-cache */
		u = ((unsigned long) METAG_TBI_DCACHE_SIZE_BITS)
		       >> METAG_TBI_DCACHE_SIZE_S;
		width = 0;
		while (u & 1) {
			width++;
			u >>= 1;
		}
		shift = 32 - (METAG_TBI_DCACHE_SIZE_S + width);
		addend = (long) ((val & METAG_TBI_DCACHE_SIZE_BITS)
				 << shift)
			>> (shift + METAG_TBI_DCACHE_SIZE_S);
		dcache_set_shift = (METAG_TBI_CACHE_SIZE_BASE_LOG2
				    - DEFAULT_CACHE_WAYS_LOG2)
					+ addend;
	}
#endif
}

static void metag_phys_data_cache_flush(const void *start)
{
	unsigned long flush0, flush1, flush2, flush3;
	int loops, step;
	int thread;
	int part, offset;
	int set_shift;

	/* Use a sequence of writes to flush the cache region requested */
	thread = (__core_reg_get(TXENABLE) & TXENABLE_THREAD_BITS)
					  >> TXENABLE_THREAD_S;

	/* Cache is broken into sets which lie in contiguous RAMs */
	set_shift = dcache_set_shift;

	/* Move to the base of the physical cache flush region */
	flush0 = LINSYSCFLUSH_DCACHE_LINE;
	step   = 64;

	/* Get partition data for this thread */
	part = metag_in32(SYSC_DCPART0 +
			      (SYSC_xCPARTn_STRIDE * thread));

	if ((int)start < 0)
		/* Access Global vs Local partition */
		part >>= SYSC_xCPARTG_AND_S
			- SYSC_xCPARTL_AND_S;

	/* Extract offset and move SetOff */
	offset = (part & SYSC_xCPARTL_OR_BITS)
			>> SYSC_xCPARTL_OR_S;
	flush0 += (offset << (set_shift - 4));

	/* Shrink size */
	part = (part & SYSC_xCPARTL_AND_BITS)
			>> SYSC_xCPARTL_AND_S;
	loops = ((part + 1) << (set_shift - 4));

	/* Reduce loops by step of cache line size */
	loops /= step;

	flush1 = flush0 + (1 << set_shift);
	flush2 = flush0 + (2 << set_shift);
	flush3 = flush0 + (3 << set_shift);

	if (dcache_sets_log2 == 1) {
		flush2 = flush1;
		flush3 = flush1 + step;
		flush1 = flush0 + step;
		step  <<= 1;
		loops >>= 1;
	}

	/* Clear loops ways in cache */
	while (loops-- != 0) {
		/* Clear the ways. */
#if 0
		/*
		 * GCC doesn't generate very good code for this so we
		 * provide inline assembly instead.
		 */
		metag_out8(0, flush0);
		metag_out8(0, flush1);
		metag_out8(0, flush2);
		metag_out8(0, flush3);

		flush0 += step;
		flush1 += step;
		flush2 += step;
		flush3 += step;
#else
		asm volatile (
			"SETB\t[%0+%4++],%5\n"
			"SETB\t[%1+%4++],%5\n"
			"SETB\t[%2+%4++],%5\n"
			"SETB\t[%3+%4++],%5\n"
			: "+e" (flush0),
			  "+e" (flush1),
			  "+e" (flush2),
			  "+e" (flush3)
			: "e" (step), "a" (0));
#endif
	}
}

void metag_data_cache_flush_all(const void *start)
{
	if ((metag_in32(SYSC_CACHE_MMU_CONFIG) & SYSC_CMMUCFG_DC_ON_BIT) == 0)
		/* No need to flush the data cache it's not actually enabled */
		return;

	metag_phys_data_cache_flush(start);
}

void metag_data_cache_flush(const void *start, int bytes)
{
	unsigned long flush0;
	int loops, step;

	if ((metag_in32(SYSC_CACHE_MMU_CONFIG) & SYSC_CMMUCFG_DC_ON_BIT) == 0)
		/* No need to flush the data cache it's not actually enabled */
		return;

	if (bytes >= 4096) {
		metag_phys_data_cache_flush(start);
		return;
	}

	/* Use linear cache flush mechanism on META IP */
	flush0 = (int)start;
	loops  = ((int)start & (DCACHE_LINE_BYTES - 1)) + bytes +
					(DCACHE_LINE_BYTES - 1);
	loops  >>= DCACHE_LINE_S;

#define PRIM_FLUSH(addr, offset) do {			\
	int __addr = ((int) (addr)) + ((offset) * 64);	\
	__builtin_dcache_flush((void *)(__addr));	\
	} while (0)

#define LOOP_INC (4*64)

	do {
		/* By default stop */
		step = 0;

		switch (loops) {
		/* Drop Thru Cases! */
		default:
			PRIM_FLUSH(flush0, 3);
			loops -= 4;
			step = 1;
		case 3:
			PRIM_FLUSH(flush0, 2);
		case 2:
			PRIM_FLUSH(flush0, 1);
		case 1:
			PRIM_FLUSH(flush0, 0);
			flush0 += LOOP_INC;
		case 0:
			break;
		}
	} while (step);
}
EXPORT_SYMBOL(metag_data_cache_flush);

static void metag_phys_code_cache_flush(const void *start, int bytes)
{
	unsigned long flush0, flush1, flush2, flush3, end_set;
	int loops, step;
	int thread;
	int set_shift, set_size;
	int part, offset;

	/* Use a sequence of writes to flush the cache region requested */
	thread = (__core_reg_get(TXENABLE) & TXENABLE_THREAD_BITS)
					  >> TXENABLE_THREAD_S;
	set_shift = icache_set_shift;

	/* Move to the base of the physical cache flush region */
	flush0 = LINSYSCFLUSH_ICACHE_LINE;
	step   = 64;

	/* Get partition code for this thread */
	part = metag_in32(SYSC_ICPART0 +
			  (SYSC_xCPARTn_STRIDE * thread));

	if ((int)start < 0)
		/* Access Global vs Local partition */
		part >>= SYSC_xCPARTG_AND_S-SYSC_xCPARTL_AND_S;

	/* Extract offset and move SetOff */
	offset = (part & SYSC_xCPARTL_OR_BITS)
			>> SYSC_xCPARTL_OR_S;
	flush0 += (offset << (set_shift - 4));

	/* Shrink size */
	part = (part & SYSC_xCPARTL_AND_BITS)
			>> SYSC_xCPARTL_AND_S;
	loops = ((part + 1) << (set_shift - 4));

	/* Where does the Set end? */
	end_set = flush0 + loops;
	set_size = loops;

#ifdef CONFIG_METAG_META12
	if ((bytes < 4096) && (bytes < loops)) {
		/* Unreachable on HTP/MTP */
		/* Only target the sets that could be relavent */
		flush0 += (loops - step) & ((int) start);
		loops = (((int) start) & (step-1)) + bytes + step - 1;
	}
#endif

	/* Reduce loops by step of cache line size */
	loops /= step;

	flush1 = flush0 + (1<<set_shift);
	flush2 = flush0 + (2<<set_shift);
	flush3 = flush0 + (3<<set_shift);

	if (icache_sets_log2 == 1) {
		flush2 = flush1;
		flush3 = flush1 + step;
		flush1 = flush0 + step;
#if 0
		/* flush0 will stop one line early in this case
		 * (flush1 will do the final line).
		 * However we don't correct end_set here at the moment
		 * because it will never wrap on HTP/MTP
		 */
		end_set -= step;
#endif
		step  <<= 1;
		loops >>= 1;
	}

	/* Clear loops ways in cache */
	while (loops-- != 0) {
#if 0
		/*
		 * GCC doesn't generate very good code for this so we
		 * provide inline assembly instead.
		 */
		/* Clear the ways */
		metag_out8(0, flush0);
		metag_out8(0, flush1);
		metag_out8(0, flush2);
		metag_out8(0, flush3);

		flush0 += step;
		flush1 += step;
		flush2 += step;
		flush3 += step;
#else
		asm volatile (
			"SETB\t[%0+%4++],%5\n"
			"SETB\t[%1+%4++],%5\n"
			"SETB\t[%2+%4++],%5\n"
			"SETB\t[%3+%4++],%5\n"
			: "+e" (flush0),
			  "+e" (flush1),
			  "+e" (flush2),
			  "+e" (flush3)
			: "e" (step), "a" (0));
#endif

		if (flush0 == end_set) {
			/* Wrap within Set 0 */
			flush0 -= set_size;
			flush1 -= set_size;
			flush2 -= set_size;
			flush3 -= set_size;
		}
	}
}

void metag_code_cache_flush_all(const void *start)
{
	if ((metag_in32(SYSC_CACHE_MMU_CONFIG) & SYSC_CMMUCFG_IC_ON_BIT) == 0)
		/* No need to flush the code cache it's not actually enabled */
		return;

	metag_phys_code_cache_flush(start, 4096);
}

void metag_code_cache_flush(const void *start, int bytes)
{
#ifndef CONFIG_METAG_META12
	void *flush;
	int loops, step;
#endif /* !CONFIG_METAG_META12 */

	if ((metag_in32(SYSC_CACHE_MMU_CONFIG) & SYSC_CMMUCFG_IC_ON_BIT) == 0)
		/* No need to flush the code cache it's not actually enabled */
		return;

#ifdef CONFIG_METAG_META12
	/* CACHEWD isn't available on Meta1, so always do full cache flush */
	metag_phys_code_cache_flush(start, bytes);

#else /* CONFIG_METAG_META12 */
	/* If large size do full physical cache flush */
	if (bytes >= 4096) {
		metag_phys_code_cache_flush(start, bytes);
		return;
	}

	/* Use linear cache flush mechanism on META IP */
	flush = (void *)((int)start & ~(ICACHE_LINE_BYTES-1));
	loops  = ((int)start & (ICACHE_LINE_BYTES-1)) + bytes +
		(ICACHE_LINE_BYTES-1);
	loops  >>= ICACHE_LINE_S;

#define PRIM_IFLUSH(addr, offset) \
	__builtin_meta2_cachewd(((addr) + ((offset) * 64)), CACHEW_ICACHE_BIT)

#define LOOP_INC (4*64)

	do {
		/* By default stop */
		step = 0;

		switch (loops) {
		/* Drop Thru Cases! */
		default:
			PRIM_IFLUSH(flush, 3);
			loops -= 4;
			step = 1;
		case 3:
			PRIM_IFLUSH(flush, 2);
		case 2:
			PRIM_IFLUSH(flush, 1);
		case 1:
			PRIM_IFLUSH(flush, 0);
			flush += LOOP_INC;
		case 0:
			break;
		}
	} while (step);
#endif /* !CONFIG_METAG_META12 */
}
EXPORT_SYMBOL(metag_code_cache_flush);
