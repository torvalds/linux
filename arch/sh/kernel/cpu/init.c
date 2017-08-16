/*
 * arch/sh/kernel/cpu/init.c
 *
 * CPU init code
 *
 * Copyright (C) 2002 - 2009  Paul Mundt
 * Copyright (C) 2003  Richard Curnow
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/log2.h>
#include <asm/mmu_context.h>
#include <asm/processor.h>
#include <linux/uaccess.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/cache.h>
#include <asm/elf.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/sh_bios.h>
#include <asm/setup.h>

#ifdef CONFIG_SH_FPU
#define cpu_has_fpu	1
#else
#define cpu_has_fpu	0
#endif

#ifdef CONFIG_SH_DSP
#define cpu_has_dsp	1
#else
#define cpu_has_dsp	0
#endif

/*
 * Generic wrapper for command line arguments to disable on-chip
 * peripherals (nofpu, nodsp, and so forth).
 */
#define onchip_setup(x)					\
static int x##_disabled = !cpu_has_##x;			\
							\
static int x##_setup(char *opts)			\
{							\
	x##_disabled = 1;				\
	return 1;					\
}							\
__setup("no" __stringify(x), x##_setup);

onchip_setup(fpu);
onchip_setup(dsp);

#ifdef CONFIG_SPECULATIVE_EXECUTION
#define CPUOPM		0xff2f0000
#define CPUOPM_RABD	(1 << 5)

static void speculative_execution_init(void)
{
	/* Clear RABD */
	__raw_writel(__raw_readl(CPUOPM) & ~CPUOPM_RABD, CPUOPM);

	/* Flush the update */
	(void)__raw_readl(CPUOPM);
	ctrl_barrier();
}
#else
#define speculative_execution_init()	do { } while (0)
#endif

#ifdef CONFIG_CPU_SH4A
#define EXPMASK			0xff2f0004
#define EXPMASK_RTEDS		(1 << 0)
#define EXPMASK_BRDSSLP		(1 << 1)
#define EXPMASK_MMCAW		(1 << 4)

static void expmask_init(void)
{
	unsigned long expmask = __raw_readl(EXPMASK);

	/*
	 * Future proofing.
	 *
	 * Disable support for slottable sleep instruction, non-nop
	 * instructions in the rte delay slot, and associative writes to
	 * the memory-mapped cache array.
	 */
	expmask &= ~(EXPMASK_RTEDS | EXPMASK_BRDSSLP | EXPMASK_MMCAW);

	__raw_writel(expmask, EXPMASK);
	ctrl_barrier();
}
#else
#define expmask_init()	do { } while (0)
#endif

/* 2nd-level cache init */
void __attribute__ ((weak)) l2_cache_init(void)
{
}

/*
 * Generic first-level cache init
 */
#if defined(CONFIG_SUPERH32) && !defined(CONFIG_CPU_J2)
static void cache_init(void)
{
	unsigned long ccr, flags;

	jump_to_uncached();
	ccr = __raw_readl(SH_CCR);

	/*
	 * At this point we don't know whether the cache is enabled or not - a
	 * bootloader may have enabled it.  There are at least 2 things that
	 * could be dirty in the cache at this point:
	 * 1. kernel command line set up by boot loader
	 * 2. spilled registers from the prolog of this function
	 * => before re-initialising the cache, we must do a purge of the whole
	 * cache out to memory for safety.  As long as nothing is spilled
	 * during the loop to lines that have already been done, this is safe.
	 * - RPC
	 */
	if (ccr & CCR_CACHE_ENABLE) {
		unsigned long ways, waysize, addrstart;

		waysize = current_cpu_data.dcache.sets;

#ifdef CCR_CACHE_ORA
		/*
		 * If the OC is already in RAM mode, we only have
		 * half of the entries to flush..
		 */
		if (ccr & CCR_CACHE_ORA)
			waysize >>= 1;
#endif

		waysize <<= current_cpu_data.dcache.entry_shift;

#ifdef CCR_CACHE_EMODE
		/* If EMODE is not set, we only have 1 way to flush. */
		if (!(ccr & CCR_CACHE_EMODE))
			ways = 1;
		else
#endif
			ways = current_cpu_data.dcache.ways;

		addrstart = CACHE_OC_ADDRESS_ARRAY;
		do {
			unsigned long addr;

			for (addr = addrstart;
			     addr < addrstart + waysize;
			     addr += current_cpu_data.dcache.linesz)
				__raw_writel(0, addr);

			addrstart += current_cpu_data.dcache.way_incr;
		} while (--ways);
	}

	/*
	 * Default CCR values .. enable the caches
	 * and invalidate them immediately..
	 */
	flags = CCR_CACHE_ENABLE | CCR_CACHE_INVALIDATE;

#ifdef CCR_CACHE_EMODE
	/* Force EMODE if possible */
	if (current_cpu_data.dcache.ways > 1)
		flags |= CCR_CACHE_EMODE;
	else
		flags &= ~CCR_CACHE_EMODE;
#endif

#if defined(CONFIG_CACHE_WRITETHROUGH)
	/* Write-through */
	flags |= CCR_CACHE_WT;
#elif defined(CONFIG_CACHE_WRITEBACK)
	/* Write-back */
	flags |= CCR_CACHE_CB;
#else
	/* Off */
	flags &= ~CCR_CACHE_ENABLE;
#endif

	l2_cache_init();

	__raw_writel(flags, SH_CCR);
	back_to_cached();
}
#else
#define cache_init()	do { } while (0)
#endif

#define CSHAPE(totalsize, linesize, assoc) \
	((totalsize & ~0xff) | (linesize << 4) | assoc)

#define CACHE_DESC_SHAPE(desc)	\
	CSHAPE((desc).way_size * (desc).ways, ilog2((desc).linesz), (desc).ways)

static void detect_cache_shape(void)
{
	l1d_cache_shape = CACHE_DESC_SHAPE(current_cpu_data.dcache);

	if (current_cpu_data.dcache.flags & SH_CACHE_COMBINED)
		l1i_cache_shape = l1d_cache_shape;
	else
		l1i_cache_shape = CACHE_DESC_SHAPE(current_cpu_data.icache);

	if (current_cpu_data.flags & CPU_HAS_L2_CACHE)
		l2_cache_shape = CACHE_DESC_SHAPE(current_cpu_data.scache);
	else
		l2_cache_shape = -1; /* No S-cache */
}

static void fpu_init(void)
{
	/* Disable the FPU */
	if (fpu_disabled && (current_cpu_data.flags & CPU_HAS_FPU)) {
		printk("FPU Disabled\n");
		current_cpu_data.flags &= ~CPU_HAS_FPU;
	}

	disable_fpu();
	clear_used_math();
}

#ifdef CONFIG_SH_DSP
static void release_dsp(void)
{
	unsigned long sr;

	/* Clear SR.DSP bit */
	__asm__ __volatile__ (
		"stc\tsr, %0\n\t"
		"and\t%1, %0\n\t"
		"ldc\t%0, sr\n\t"
		: "=&r" (sr)
		: "r" (~SR_DSP)
	);
}

static void dsp_init(void)
{
	unsigned long sr;

	/*
	 * Set the SR.DSP bit, wait for one instruction, and then read
	 * back the SR value.
	 */
	__asm__ __volatile__ (
		"stc\tsr, %0\n\t"
		"or\t%1, %0\n\t"
		"ldc\t%0, sr\n\t"
		"nop\n\t"
		"stc\tsr, %0\n\t"
		: "=&r" (sr)
		: "r" (SR_DSP)
	);

	/* If the DSP bit is still set, this CPU has a DSP */
	if (sr & SR_DSP)
		current_cpu_data.flags |= CPU_HAS_DSP;

	/* Disable the DSP */
	if (dsp_disabled && (current_cpu_data.flags & CPU_HAS_DSP)) {
		printk("DSP Disabled\n");
		current_cpu_data.flags &= ~CPU_HAS_DSP;
	}

	/* Now that we've determined the DSP status, clear the DSP bit. */
	release_dsp();
}
#else
static inline void dsp_init(void) { }
#endif /* CONFIG_SH_DSP */

/**
 * cpu_init
 *
 * This is our initial entry point for each CPU, and is invoked on the
 * boot CPU prior to calling start_kernel(). For SMP, a combination of
 * this and start_secondary() will bring up each processor to a ready
 * state prior to hand forking the idle loop.
 *
 * We do all of the basic processor init here, including setting up
 * the caches, FPU, DSP, etc. By the time start_kernel() is hit (and
 * subsequently platform_setup()) things like determining the CPU
 * subtype and initial configuration will all be done.
 *
 * Each processor family is still responsible for doing its own probing
 * and cache configuration in cpu_probe().
 */
asmlinkage void cpu_init(void)
{
	current_thread_info()->cpu = hard_smp_processor_id();

	/* First, probe the CPU */
	cpu_probe();

	if (current_cpu_data.type == CPU_SH_NONE)
		panic("Unknown CPU");

	/* First setup the rest of the I-cache info */
	current_cpu_data.icache.entry_mask = current_cpu_data.icache.way_incr -
				      current_cpu_data.icache.linesz;

	current_cpu_data.icache.way_size = current_cpu_data.icache.sets *
				    current_cpu_data.icache.linesz;

	/* And the D-cache too */
	current_cpu_data.dcache.entry_mask = current_cpu_data.dcache.way_incr -
				      current_cpu_data.dcache.linesz;

	current_cpu_data.dcache.way_size = current_cpu_data.dcache.sets *
				    current_cpu_data.dcache.linesz;

	/* Init the cache */
	cache_init();

	if (raw_smp_processor_id() == 0) {
#ifdef CONFIG_MMU
		shm_align_mask = max_t(unsigned long,
				       current_cpu_data.dcache.way_size - 1,
				       PAGE_SIZE - 1);
#else
		shm_align_mask = PAGE_SIZE - 1;
#endif

		/* Boot CPU sets the cache shape */
		detect_cache_shape();
	}

	fpu_init();
	dsp_init();

	/*
	 * Initialize the per-CPU ASID cache very early, since the
	 * TLB flushing routines depend on this being setup.
	 */
	current_cpu_data.asid_cache = NO_CONTEXT;

	current_cpu_data.phys_bits = __in_29bit_mode() ? 29 : 32;

	speculative_execution_init();
	expmask_init();

	/* Do the rest of the boot processor setup */
	if (raw_smp_processor_id() == 0) {
		/* Save off the BIOS VBR, if there is one */
		sh_bios_vbr_init();

		/*
		 * Setup VBR for boot CPU. Secondary CPUs do this through
		 * start_secondary().
		 */
		per_cpu_trap_init();

		/*
		 * Boot processor to setup the FP and extended state
		 * context info.
		 */
		init_thread_xstate();
	}
}
