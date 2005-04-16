/*
 * Copyright (C) 2003, 2004  Maciej W. Rozycki
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/stddef.h>

#include <asm/bugs.h>
#include <asm/compiler.h>
#include <asm/cpu.h>
#include <asm/fpu.h>
#include <asm/mipsregs.h>
#include <asm/system.h>

static inline void align_mod(const int align, const int mod)
{
	asm volatile(
		".set	push\n\t"
		".set	noreorder\n\t"
		".balign %0\n\t"
		".rept	%1\n\t"
		"nop\n\t"
		".endr\n\t"
		".set	pop"
		:
		: "n" (align), "n" (mod));
}

static inline void mult_sh_align_mod(long *v1, long *v2, long *w,
				     const int align, const int mod)
{
	unsigned long flags;
	int m1, m2;
	long p, s, lv1, lv2, lw;

	/*
	 * We want the multiply and the shift to be isolated from the
	 * rest of the code to disable gcc optimizations.  Hence the
	 * asm statements that execute nothing, but make gcc not know
	 * what the values of m1, m2 and s are and what lv2 and p are
	 * used for.
	 */

	local_irq_save(flags);
	/*
	 * The following code leads to a wrong result of the first
	 * dsll32 when executed on R4000 rev. 2.2 or 3.0 (PRId
	 * 00000422 or 00000430, respectively).
	 *
	 * See "MIPS R4000PC/SC Errata, Processor Revision 2.2 and
	 * 3.0" by MIPS Technologies, Inc., errata #16 and #28 for
	 * details.  I got no permission to duplicate them here,
	 * sigh... --macro
	 */
	asm volatile(
		""
		: "=r" (m1), "=r" (m2), "=r" (s)
		: "0" (5), "1" (8), "2" (5));
	align_mod(align, mod);
	/*
	 * The trailing nop is needed to fullfill the two-instruction
	 * requirement between reading hi/lo and staring a mult/div.
	 * Leaving it out may cause gas insert a nop itself breaking
	 * the desired alignment of the next chunk.
	 */
	asm volatile(
		".set	push\n\t"
		".set	noat\n\t"
		".set	noreorder\n\t"
		".set	nomacro\n\t"
		"mult	%2, %3\n\t"
		"dsll32	%0, %4, %5\n\t"
		"mflo	$0\n\t"
		"dsll32	%1, %4, %5\n\t"
		"nop\n\t"
		".set	pop"
		: "=&r" (lv1), "=r" (lw)
		: "r" (m1), "r" (m2), "r" (s), "I" (0)
		: "hi", "lo", GCC_REG_ACCUM);
	/* We have to use single integers for m1 and m2 and a double
	 * one for p to be sure the mulsidi3 gcc's RTL multiplication
	 * instruction has the workaround applied.  Older versions of
	 * gcc have correct umulsi3 and mulsi3, but other
	 * multiplication variants lack the workaround.
	 */
	asm volatile(
		""
		: "=r" (m1), "=r" (m2), "=r" (s)
		: "0" (m1), "1" (m2), "2" (s));
	align_mod(align, mod);
	p = m1 * m2;
	lv2 = s << 32;
	asm volatile(
		""
		: "=r" (lv2)
		: "0" (lv2), "r" (p));
	local_irq_restore(flags);

	*v1 = lv1;
	*v2 = lv2;
	*w = lw;
}

static inline void check_mult_sh(void)
{
	long v1[8], v2[8], w[8];
	int bug, fix, i;

	printk("Checking for the multiply/shift bug... ");

	/*
	 * Testing discovered false negatives for certain code offsets
	 * into cache lines.  Hence we test all possible offsets for
	 * the worst assumption of an R4000 I-cache line width of 32
	 * bytes.
	 *
	 * We can't use a loop as alignment directives need to be
	 * immediates.
	 */
	mult_sh_align_mod(&v1[0], &v2[0], &w[0], 32, 0);
	mult_sh_align_mod(&v1[1], &v2[1], &w[1], 32, 1);
	mult_sh_align_mod(&v1[2], &v2[2], &w[2], 32, 2);
	mult_sh_align_mod(&v1[3], &v2[3], &w[3], 32, 3);
	mult_sh_align_mod(&v1[4], &v2[4], &w[4], 32, 4);
	mult_sh_align_mod(&v1[5], &v2[5], &w[5], 32, 5);
	mult_sh_align_mod(&v1[6], &v2[6], &w[6], 32, 6);
	mult_sh_align_mod(&v1[7], &v2[7], &w[7], 32, 7);

	bug = 0;
	for (i = 0; i < 8; i++)
		if (v1[i] != w[i])
			bug = 1;
		
	if (bug == 0) {
		printk("no.\n");
		return;
	}

	printk("yes, workaround... ");

	fix = 1;
	for (i = 0; i < 8; i++)
		if (v2[i] != w[i])
			fix = 0;
		
	if (fix == 1) {
		printk("yes.\n");
		return;
	}

	printk("no.\n");
	panic("Reliable operation impossible!\n"
#ifndef CONFIG_CPU_R4000
	      "Configure for R4000 to enable the workaround."
#else
	      "Please report to <linux-mips@linux-mips.org>."
#endif
	      );
}

static volatile int daddi_ov __initdata = 0;

asmlinkage void __init do_daddi_ov(struct pt_regs *regs)
{
	daddi_ov = 1;
	regs->cp0_epc += 4;
}

static inline void check_daddi(void)
{
	extern asmlinkage void handle_daddi_ov(void);
	unsigned long flags;
	void *handler;
	long v, tmp;

	printk("Checking for the daddi bug... ");

	local_irq_save(flags);
	handler = set_except_vector(12, handle_daddi_ov);
	/*
	 * The following code fails to trigger an overflow exception
	 * when executed on R4000 rev. 2.2 or 3.0 (PRId 00000422 or
	 * 00000430, respectively).
	 *
	 * See "MIPS R4000PC/SC Errata, Processor Revision 2.2 and
	 * 3.0" by MIPS Technologies, Inc., erratum #23 for details.
	 * I got no permission to duplicate it here, sigh... --macro
	 */
	asm volatile(
		".set	push\n\t"
		".set	noat\n\t"
		".set	noreorder\n\t"
		".set	nomacro\n\t"
		"addiu	%1, $0, %2\n\t"
		"dsrl	%1, %1, 1\n\t"
#ifdef HAVE_AS_SET_DADDI
		".set	daddi\n\t"
#endif
		"daddi	%0, %1, %3\n\t"
		".set	pop"
		: "=r" (v), "=&r" (tmp)
		: "I" (0xffffffffffffdb9a), "I" (0x1234));
	set_except_vector(12, handler);
	local_irq_restore(flags);

	if (daddi_ov) {
		printk("no.\n");
		return;
	}

	printk("yes, workaround... ");

	local_irq_save(flags);
	handler = set_except_vector(12, handle_daddi_ov);
	asm volatile(
		"addiu	%1, $0, %2\n\t"
		"dsrl	%1, %1, 1\n\t"
		"daddi	%0, %1, %3"
		: "=r" (v), "=&r" (tmp)
		: "I" (0xffffffffffffdb9a), "I" (0x1234));
	set_except_vector(12, handler);
	local_irq_restore(flags);

	if (daddi_ov) {
		printk("yes.\n");
		return;
	}

	printk("no.\n");
	panic("Reliable operation impossible!\n"
#if !defined(CONFIG_CPU_R4000) && !defined(CONFIG_CPU_R4400)
	      "Configure for R4000 or R4400 to enable the workaround."
#else
	      "Please report to <linux-mips@linux-mips.org>."
#endif
	      );
}

static inline void check_daddiu(void)
{
	long v, w, tmp;

	printk("Checking for the daddiu bug... ");

	/*
	 * The following code leads to a wrong result of daddiu when
	 * executed on R4400 rev. 1.0 (PRId 00000440).
	 *
	 * See "MIPS R4400PC/SC Errata, Processor Revision 1.0" by
	 * MIPS Technologies, Inc., erratum #7 for details.
	 *
	 * According to "MIPS R4000PC/SC Errata, Processor Revision
	 * 2.2 and 3.0" by MIPS Technologies, Inc., erratum #41 this
	 * problem affects R4000 rev. 2.2 and 3.0 (PRId 00000422 and
	 * 00000430, respectively), too.  Testing failed to trigger it
	 * so far.
	 *
	 * I got no permission to duplicate the errata here, sigh...
	 * --macro
	 */
	asm volatile(
		".set	push\n\t"
		".set	noat\n\t"
		".set	noreorder\n\t"
		".set	nomacro\n\t"
		"addiu	%2, $0, %3\n\t"
		"dsrl	%2, %2, 1\n\t"
#ifdef HAVE_AS_SET_DADDI
		".set	daddi\n\t"
#endif
		"daddiu	%0, %2, %4\n\t"
		"addiu	%1, $0, %4\n\t"
		"daddu	%1, %2\n\t"
		".set	pop"
		: "=&r" (v), "=&r" (w), "=&r" (tmp)
		: "I" (0xffffffffffffdb9a), "I" (0x1234));

	if (v == w) {
		printk("no.\n");
		return;
	}

	printk("yes, workaround... ");

	asm volatile(
		"addiu	%2, $0, %3\n\t"
		"dsrl	%2, %2, 1\n\t"
		"daddiu	%0, %2, %4\n\t"
		"addiu	%1, $0, %4\n\t"
		"daddu	%1, %2"
		: "=&r" (v), "=&r" (w), "=&r" (tmp)
		: "I" (0xffffffffffffdb9a), "I" (0x1234));

	if (v == w) {
		printk("yes.\n");
		return;
	}

	printk("no.\n");
	panic("Reliable operation impossible!\n"
#if !defined(CONFIG_CPU_R4000) && !defined(CONFIG_CPU_R4400)
	      "Configure for R4000 or R4400 to enable the workaround."
#else
	      "Please report to <linux-mips@linux-mips.org>."
#endif
	      );
}

void __init check_bugs64(void)
{
	check_mult_sh();
	check_daddi();
	check_daddiu();
}
