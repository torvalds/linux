/*
 * linux/arch/unicore32/kernel/fpu-ucf64.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched/signal.h>
#include <linux/init.h>

#include <asm/fpu-ucf64.h>

/*
 * A special flag to tell the normalisation code not to normalise.
 */
#define F64_NAN_FLAG	0x100

/*
 * A bit pattern used to indicate the initial (unset) value of the
 * exception mask, in case nothing handles an instruction.  This
 * doesn't include the NAN flag, which get masked out before
 * we check for an error.
 */
#define F64_EXCEPTION_ERROR	((u32)-1 & ~F64_NAN_FLAG)

/*
 * Since we aren't building with -mfpu=f64, we need to code
 * these instructions using their MRC/MCR equivalents.
 */
#define f64reg(_f64_) #_f64_

#define cff(_f64_) ({			\
	u32 __v;			\
	asm("cff %0, " f64reg(_f64_) "@ fmrx	%0, " #_f64_	\
	    : "=r" (__v) : : "cc");	\
	__v;				\
	})

#define ctf(_f64_, _var_)		\
	asm("ctf %0, " f64reg(_f64_) "@ fmxr	" #_f64_ ", %0"	\
	   : : "r" (_var_) : "cc")

/*
 * Raise a SIGFPE for the current process.
 * sicode describes the signal being raised.
 */
void ucf64_raise_sigfpe(struct pt_regs *regs)
{
	/*
	 * This is the same as NWFPE, because it's not clear what
	 * this is used for
	 */
	current->thread.error_code = 0;
	current->thread.trap_no = 6;

	send_sig_fault(SIGFPE, FPE_FLTUNK,
		       (void __user *)(instruction_pointer(regs) - 4),
		       current);
}

/*
 * Handle exceptions of UniCore-F64.
 */
void ucf64_exchandler(u32 inst, u32 fpexc, struct pt_regs *regs)
{
	u32 tmp = fpexc;
	u32 exc = F64_EXCEPTION_ERROR & fpexc;

	pr_debug("UniCore-F64: instruction %08x fpscr %08x\n",
			inst, fpexc);

	if (exc & FPSCR_CMPINSTR_BIT) {
		if (exc & FPSCR_CON)
			tmp |= FPSCR_CON;
		else
			tmp &= ~(FPSCR_CON);
		exc &= ~(FPSCR_CMPINSTR_BIT | FPSCR_CON);
	} else {
		pr_debug("UniCore-F64 Error: unhandled exceptions\n");
		pr_debug("UniCore-F64 FPSCR 0x%08x INST 0x%08x\n",
				cff(FPSCR), inst);

		ucf64_raise_sigfpe(regs);
		return;
	}

	/*
	 * Update the FPSCR with the additional exception flags.
	 * Comparison instructions always return at least one of
	 * these flags set.
	 */
	tmp &= ~(FPSCR_TRAP | FPSCR_IOS | FPSCR_OFS | FPSCR_UFS |
			FPSCR_IXS | FPSCR_HIS | FPSCR_IOC | FPSCR_OFC |
			FPSCR_UFC | FPSCR_IXC | FPSCR_HIC);

	tmp |= exc;
	ctf(FPSCR, tmp);
}

/*
 * F64 support code initialisation.
 */
static int __init ucf64_init(void)
{
	ctf(FPSCR, 0x0);     /* FPSCR_UFE | FPSCR_NDE perhaps better */

	printk(KERN_INFO "Enable UniCore-F64 support.\n");

	return 0;
}

late_initcall(ucf64_init);
