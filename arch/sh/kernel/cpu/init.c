/*
 * arch/sh/kernel/cpu/init.c
 *
 * CPU init code
 *
 * Copyright (C) 2002, 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/cacheflush.h>
#include <asm/cache.h>
#include <asm/io.h>

extern void detect_cpu_and_cache_system(void);

/*
 * Generic wrapper for command line arguments to disable on-chip
 * peripherals (nofpu, nodsp, and so forth).
 */
#define onchip_setup(x)				\
static int x##_disabled __initdata = 0;		\
						\
static int __init x##_setup(char *opts)		\
{						\
	x##_disabled = 1;			\
	return 1;				\
}						\
__setup("no" __stringify(x), x##_setup);

onchip_setup(fpu);
onchip_setup(dsp);

/*
 * Generic first-level cache init
 */
static void __init cache_init(void)
{
	unsigned long ccr, flags;

	if (cpu_data->type == CPU_SH_NONE)
		panic("Unknown CPU");

	jump_to_P2();
	ccr = ctrl_inl(CCR);

	/*
	 * If the cache is already enabled .. flush it.
	 */
	if (ccr & CCR_CACHE_ENABLE) {
		unsigned long ways, waysize, addrstart;

		waysize = cpu_data->dcache.sets;

		/*
		 * If the OC is already in RAM mode, we only have
		 * half of the entries to flush..
		 */
		if (ccr & CCR_CACHE_ORA)
			waysize >>= 1;

		waysize <<= cpu_data->dcache.entry_shift;

#ifdef CCR_CACHE_EMODE
		/* If EMODE is not set, we only have 1 way to flush. */
		if (!(ccr & CCR_CACHE_EMODE))
			ways = 1;
		else
#endif
			ways = cpu_data->dcache.ways;

		addrstart = CACHE_OC_ADDRESS_ARRAY;
		do {
			unsigned long addr;

			for (addr = addrstart;
			     addr < addrstart + waysize;
			     addr += cpu_data->dcache.linesz)
				ctrl_outl(0, addr);

			addrstart += cpu_data->dcache.way_incr;
		} while (--ways);
	}

	/*
	 * Default CCR values .. enable the caches
	 * and invalidate them immediately..
	 */
	flags = CCR_CACHE_ENABLE | CCR_CACHE_INVALIDATE;

#ifdef CCR_CACHE_EMODE
	/* Force EMODE if possible */
	if (cpu_data->dcache.ways > 1)
		flags |= CCR_CACHE_EMODE;
#endif

#ifdef CONFIG_SH_WRITETHROUGH
	/* Turn on Write-through caching */
	flags |= CCR_CACHE_WT;
#else
	/* .. or default to Write-back */
	flags |= CCR_CACHE_CB;
#endif

#ifdef CONFIG_SH_OCRAM
	/* Turn on OCRAM -- halve the OC */
	flags |= CCR_CACHE_ORA;
	cpu_data->dcache.sets >>= 1;
#endif

	ctrl_outl(flags, CCR);
	back_to_P1();
}

#ifdef CONFIG_SH_DSP
static void __init release_dsp(void)
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

static void __init dsp_init(void)
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
		cpu_data->flags |= CPU_HAS_DSP;

	/* Now that we've determined the DSP status, clear the DSP bit. */
	release_dsp();
}
#endif /* CONFIG_SH_DSP */

/**
 * sh_cpu_init
 *
 * This is our initial entry point for each CPU, and is invoked on the boot
 * CPU prior to calling start_kernel(). For SMP, a combination of this and
 * start_secondary() will bring up each processor to a ready state prior
 * to hand forking the idle loop.
 *
 * We do all of the basic processor init here, including setting up the
 * caches, FPU, DSP, kicking the UBC, etc. By the time start_kernel() is
 * hit (and subsequently platform_setup()) things like determining the
 * CPU subtype and initial configuration will all be done.
 *
 * Each processor family is still responsible for doing its own probing
 * and cache configuration in detect_cpu_and_cache_system().
 */
asmlinkage void __init sh_cpu_init(void)
{
	/* First, probe the CPU */
	detect_cpu_and_cache_system();

	/* Init the cache */
	cache_init();

	/* Disable the FPU */
	if (fpu_disabled) {
		printk("FPU Disabled\n");
		cpu_data->flags &= ~CPU_HAS_FPU;
		disable_fpu();
	}

	/* FPU initialization */
	if ((cpu_data->flags & CPU_HAS_FPU)) {
		clear_thread_flag(TIF_USEDFPU);
		clear_used_math();
	}

#ifdef CONFIG_SH_DSP
	/* Probe for DSP */
	dsp_init();

	/* Disable the DSP */
	if (dsp_disabled) {
		printk("DSP Disabled\n");
		cpu_data->flags &= ~CPU_HAS_DSP;
		release_dsp();
	}
#endif

#ifdef CONFIG_UBC_WAKEUP
	/*
	 * Some brain-damaged loaders decided it would be a good idea to put
	 * the UBC to sleep. This causes some issues when it comes to things
	 * like PTRACE_SINGLESTEP or doing hardware watchpoints in GDB.  So ..
	 * we wake it up and hope that all is well.
	 */
	ubc_wakeup();
#endif
}

