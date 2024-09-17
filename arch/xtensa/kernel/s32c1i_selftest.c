/*
 * S32C1I selftest.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2016 Cadence Design Systems Inc.
 */

#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/traps.h>

#if XCHAL_HAVE_S32C1I

static int __initdata rcw_word, rcw_probe_pc, rcw_exc;

/*
 * Basic atomic compare-and-swap, that records PC of S32C1I for probing.
 *
 * If *v == cmp, set *v = set.  Return previous *v.
 */
static inline int probed_compare_swap(int *v, int cmp, int set)
{
	int tmp;

	__asm__ __volatile__(
			"	movi	%1, 1f\n"
			"	s32i	%1, %4, 0\n"
			"	wsr	%2, scompare1\n"
			"1:	s32c1i	%0, %3, 0\n"
			: "=a" (set), "=&a" (tmp)
			: "a" (cmp), "a" (v), "a" (&rcw_probe_pc), "0" (set)
			: "memory"
			);
	return set;
}

/* Handle probed exception */

static void __init do_probed_exception(struct pt_regs *regs)
{
	if (regs->pc == rcw_probe_pc) {	/* exception on s32c1i ? */
		regs->pc += 3;		/* skip the s32c1i instruction */
		rcw_exc = regs->exccause;
	} else {
		do_unhandled(regs);
	}
}

/* Simple test of S32C1I (soc bringup assist) */

static int __init check_s32c1i(void)
{
	int n, cause1, cause2;
	void *handbus, *handdata, *handaddr; /* temporarily saved handlers */

	rcw_probe_pc = 0;
	handbus  = trap_set_handler(EXCCAUSE_LOAD_STORE_ERROR,
			do_probed_exception);
	handdata = trap_set_handler(EXCCAUSE_LOAD_STORE_DATA_ERROR,
			do_probed_exception);
	handaddr = trap_set_handler(EXCCAUSE_LOAD_STORE_ADDR_ERROR,
			do_probed_exception);

	/* First try an S32C1I that does not store: */
	rcw_exc = 0;
	rcw_word = 1;
	n = probed_compare_swap(&rcw_word, 0, 2);
	cause1 = rcw_exc;

	/* took exception? */
	if (cause1 != 0) {
		/* unclean exception? */
		if (n != 2 || rcw_word != 1)
			panic("S32C1I exception error");
	} else if (rcw_word != 1 || n != 1) {
		panic("S32C1I compare error");
	}

	/* Then an S32C1I that stores: */
	rcw_exc = 0;
	rcw_word = 0x1234567;
	n = probed_compare_swap(&rcw_word, 0x1234567, 0xabcde);
	cause2 = rcw_exc;

	if (cause2 != 0) {
		/* unclean exception? */
		if (n != 0xabcde || rcw_word != 0x1234567)
			panic("S32C1I exception error (b)");
	} else if (rcw_word != 0xabcde || n != 0x1234567) {
		panic("S32C1I store error");
	}

	/* Verify consistency of exceptions: */
	if (cause1 || cause2) {
		pr_warn("S32C1I took exception %d, %d\n", cause1, cause2);
		/* If emulation of S32C1I upon bus error gets implemented,
		 * we can get rid of this panic for single core (not SMP)
		 */
		panic("S32C1I exceptions not currently supported");
	}
	if (cause1 != cause2)
		panic("inconsistent S32C1I exceptions");

	trap_set_handler(EXCCAUSE_LOAD_STORE_ERROR, handbus);
	trap_set_handler(EXCCAUSE_LOAD_STORE_DATA_ERROR, handdata);
	trap_set_handler(EXCCAUSE_LOAD_STORE_ADDR_ERROR, handaddr);
	return 0;
}

#else /* XCHAL_HAVE_S32C1I */

/* This condition should not occur with a commercially deployed processor.
 * Display reminder for early engr test or demo chips / FPGA bitstreams
 */
static int __init check_s32c1i(void)
{
	pr_warn("Processor configuration lacks atomic compare-and-swap support!\n");
	return 0;
}

#endif /* XCHAL_HAVE_S32C1I */

early_initcall(check_s32c1i);
