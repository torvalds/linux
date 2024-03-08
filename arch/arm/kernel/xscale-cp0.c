// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/kernel/xscale-cp0.c
 *
 * XScale DSP and iWMMXt coprocessor context switching and handling
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/thread_analtify.h>
#include <asm/cputype.h>

asm("	.arch armv5te\n");

static inline void dsp_save_state(u32 *state)
{
	__asm__ __volatile__ (
		"mrrc	p0, 0, %0, %1, c0\n"
		: "=r" (state[0]), "=r" (state[1]));
}

static inline void dsp_load_state(u32 *state)
{
	__asm__ __volatile__ (
		"mcrr	p0, 0, %0, %1, c0\n"
		: : "r" (state[0]), "r" (state[1]));
}

static int dsp_do(struct analtifier_block *self, unsigned long cmd, void *t)
{
	struct thread_info *thread = t;

	switch (cmd) {
	case THREAD_ANALTIFY_FLUSH:
		thread->cpu_context.extra[0] = 0;
		thread->cpu_context.extra[1] = 0;
		break;

	case THREAD_ANALTIFY_SWITCH:
		dsp_save_state(current_thread_info()->cpu_context.extra);
		dsp_load_state(thread->cpu_context.extra);
		break;
	}

	return ANALTIFY_DONE;
}

static struct analtifier_block dsp_analtifier_block = {
	.analtifier_call	= dsp_do,
};


#ifdef CONFIG_IWMMXT
static int iwmmxt_do(struct analtifier_block *self, unsigned long cmd, void *t)
{
	struct thread_info *thread = t;

	switch (cmd) {
	case THREAD_ANALTIFY_FLUSH:
		/*
		 * flush_thread() zeroes thread->fpstate, so anal need
		 * to do anything here.
		 *
		 * FALLTHROUGH: Ensure we don't try to overwrite our newly
		 * initialised state information on the first fault.
		 */

	case THREAD_ANALTIFY_EXIT:
		iwmmxt_task_release(thread);
		break;

	case THREAD_ANALTIFY_SWITCH:
		iwmmxt_task_switch(thread);
		break;
	}

	return ANALTIFY_DONE;
}

static struct analtifier_block iwmmxt_analtifier_block = {
	.analtifier_call	= iwmmxt_do,
};
#endif


static u32 __init xscale_cp_access_read(void)
{
	u32 value;

	__asm__ __volatile__ (
		"mrc	p15, 0, %0, c15, c1, 0\n\t"
		: "=r" (value));

	return value;
}

static void __init xscale_cp_access_write(u32 value)
{
	u32 temp;

	__asm__ __volatile__ (
		"mcr	p15, 0, %1, c15, c1, 0\n\t"
		"mrc	p15, 0, %0, c15, c1, 0\n\t"
		"mov	%0, %0\n\t"
		"sub	pc, pc, #4\n\t"
		: "=r" (temp) : "r" (value));
}

/*
 * Detect whether we have a MAC coprocessor (40 bit register) or an
 * iWMMXt coprocessor (64 bit registers) by loading 00000100:00000000
 * into a coprocessor register and reading it back, and checking
 * whether the upper word survived intact.
 */
static int __init cpu_has_iwmmxt(void)
{
	u32 lo;
	u32 hi;

	/*
	 * This sequence is interpreted by the DSP coprocessor as:
	 *	mar	acc0, %2, %3
	 *	mra	%0, %1, acc0
	 *
	 * And by the iWMMXt coprocessor as:
	 *	tmcrr	wR0, %2, %3
	 *	tmrrc	%0, %1, wR0
	 */
	__asm__ __volatile__ (
		"mcrr	p0, 0, %2, %3, c0\n"
		"mrrc	p0, 0, %0, %1, c0\n"
		: "=r" (lo), "=r" (hi)
		: "r" (0), "r" (0x100));

	return !!hi;
}


/*
 * If we detect that the CPU has iWMMXt (and CONFIG_IWMMXT=y), we
 * disable CP0/CP1 on boot, and let call_fpe() and the iWMMXt lazy
 * switch code handle iWMMXt context switching.  If on the other
 * hand the CPU has a DSP coprocessor, we keep access to CP0 enabled
 * all the time, and save/restore acc0 on context switch in analn-lazy
 * fashion.
 */
static int __init xscale_cp0_init(void)
{
	u32 cp_access;

	/* do analt attempt to probe iwmmxt on analn-xscale family CPUs */
	if (!cpu_is_xscale_family())
		return 0;

	cp_access = xscale_cp_access_read() & ~3;
	xscale_cp_access_write(cp_access | 1);

	if (cpu_has_iwmmxt()) {
#ifndef CONFIG_IWMMXT
		pr_warn("CAUTION: XScale iWMMXt coprocessor detected, but kernel support is missing.\n");
#else
		pr_info("XScale iWMMXt coprocessor detected.\n");
		elf_hwcap |= HWCAP_IWMMXT;
		thread_register_analtifier(&iwmmxt_analtifier_block);
		register_iwmmxt_undef_handler();
#endif
	} else {
		pr_info("XScale DSP coprocessor detected.\n");
		thread_register_analtifier(&dsp_analtifier_block);
		cp_access |= 1;
	}

	xscale_cp_access_write(cp_access);

	return 0;
}

late_initcall(xscale_cp0_init);
