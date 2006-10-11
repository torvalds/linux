/*
 *  linux/arch/arm/vfp/vfpmodule.c
 *
 *  Copyright (C) 2004 ARM Limited.
 *  Written by Deep Blue Solutions Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/thread_notify.h>
#include <asm/vfp.h>

#include "vfpinstr.h"
#include "vfp.h"

/*
 * Our undef handlers (in entry.S)
 */
void vfp_testing_entry(void);
void vfp_support_entry(void);

void (*vfp_vector)(void) = vfp_testing_entry;
union vfp_state *last_VFP_context;

/*
 * Dual-use variable.
 * Used in startup: set to non-zero if VFP checks fail
 * After startup, holds VFP architecture
 */
unsigned int VFP_arch;

static int vfp_notifier(struct notifier_block *self, unsigned long cmd, void *v)
{
	struct thread_info *thread = v;
	union vfp_state *vfp;

	if (likely(cmd == THREAD_NOTIFY_SWITCH)) {
		/*
		 * Always disable VFP so we can lazily save/restore the
		 * old state.
		 */
		fmxr(FPEXC, fmrx(FPEXC) & ~FPEXC_ENABLE);
		return NOTIFY_DONE;
	}

	vfp = &thread->vfpstate;
	if (cmd == THREAD_NOTIFY_FLUSH) {
		/*
		 * Per-thread VFP initialisation.
		 */
		memset(vfp, 0, sizeof(union vfp_state));

		vfp->hard.fpexc = FPEXC_ENABLE;
		vfp->hard.fpscr = FPSCR_ROUND_NEAREST;

		/*
		 * Disable VFP to ensure we initialise it first.
		 */
		fmxr(FPEXC, fmrx(FPEXC) & ~FPEXC_ENABLE);
	}

	/* flush and release case: Per-thread VFP cleanup. */
	if (last_VFP_context == vfp)
		last_VFP_context = NULL;

	return NOTIFY_DONE;
}

static struct notifier_block vfp_notifier_block = {
	.notifier_call	= vfp_notifier,
};

/*
 * Raise a SIGFPE for the current process.
 * sicode describes the signal being raised.
 */
void vfp_raise_sigfpe(unsigned int sicode, struct pt_regs *regs)
{
	siginfo_t info;

	memset(&info, 0, sizeof(info));

	info.si_signo = SIGFPE;
	info.si_code = sicode;
	info.si_addr = (void __user *)(instruction_pointer(regs) - 4);

	/*
	 * This is the same as NWFPE, because it's not clear what
	 * this is used for
	 */
	current->thread.error_code = 0;
	current->thread.trap_no = 6;

	send_sig_info(SIGFPE, &info, current);
}

static void vfp_panic(char *reason)
{
	int i;

	printk(KERN_ERR "VFP: Error: %s\n", reason);
	printk(KERN_ERR "VFP: EXC 0x%08x SCR 0x%08x INST 0x%08x\n",
		fmrx(FPEXC), fmrx(FPSCR), fmrx(FPINST));
	for (i = 0; i < 32; i += 2)
		printk(KERN_ERR "VFP: s%2u: 0x%08x s%2u: 0x%08x\n",
		       i, vfp_get_float(i), i+1, vfp_get_float(i+1));
}

/*
 * Process bitmask of exception conditions.
 */
static void vfp_raise_exceptions(u32 exceptions, u32 inst, u32 fpscr, struct pt_regs *regs)
{
	int si_code = 0;

	pr_debug("VFP: raising exceptions %08x\n", exceptions);

	if (exceptions == VFP_EXCEPTION_ERROR) {
		vfp_panic("unhandled bounce");
		vfp_raise_sigfpe(0, regs);
		return;
	}

	/*
	 * If any of the status flags are set, update the FPSCR.
	 * Comparison instructions always return at least one of
	 * these flags set.
	 */
	if (exceptions & (FPSCR_N|FPSCR_Z|FPSCR_C|FPSCR_V))
		fpscr &= ~(FPSCR_N|FPSCR_Z|FPSCR_C|FPSCR_V);

	fpscr |= exceptions;

	fmxr(FPSCR, fpscr);

#define RAISE(stat,en,sig)				\
	if (exceptions & stat && fpscr & en)		\
		si_code = sig;

	/*
	 * These are arranged in priority order, least to highest.
	 */
	RAISE(FPSCR_IXC, FPSCR_IXE, FPE_FLTRES);
	RAISE(FPSCR_UFC, FPSCR_UFE, FPE_FLTUND);
	RAISE(FPSCR_OFC, FPSCR_OFE, FPE_FLTOVF);
	RAISE(FPSCR_IOC, FPSCR_IOE, FPE_FLTINV);

	if (si_code)
		vfp_raise_sigfpe(si_code, regs);
}

/*
 * Emulate a VFP instruction.
 */
static u32 vfp_emulate_instruction(u32 inst, u32 fpscr, struct pt_regs *regs)
{
	u32 exceptions = VFP_EXCEPTION_ERROR;

	pr_debug("VFP: emulate: INST=0x%08x SCR=0x%08x\n", inst, fpscr);

	if (INST_CPRTDO(inst)) {
		if (!INST_CPRT(inst)) {
			/*
			 * CPDO
			 */
			if (vfp_single(inst)) {
				exceptions = vfp_single_cpdo(inst, fpscr);
			} else {
				exceptions = vfp_double_cpdo(inst, fpscr);
			}
		} else {
			/*
			 * A CPRT instruction can not appear in FPINST2, nor
			 * can it cause an exception.  Therefore, we do not
			 * have to emulate it.
			 */
		}
	} else {
		/*
		 * A CPDT instruction can not appear in FPINST2, nor can
		 * it cause an exception.  Therefore, we do not have to
		 * emulate it.
		 */
	}
	return exceptions & ~VFP_NAN_FLAG;
}

/*
 * Package up a bounce condition.
 */
void VFP9_bounce(u32 trigger, u32 fpexc, struct pt_regs *regs)
{
	u32 fpscr, orig_fpscr, exceptions, inst;

	pr_debug("VFP: bounce: trigger %08x fpexc %08x\n", trigger, fpexc);

	/*
	 * Enable access to the VFP so we can handle the bounce.
	 */
	fmxr(FPEXC, fpexc & ~(FPEXC_EXCEPTION|FPEXC_INV|FPEXC_UFC|FPEXC_IOC));

	orig_fpscr = fpscr = fmrx(FPSCR);

	/*
	 * If we are running with inexact exceptions enabled, we need to
	 * emulate the trigger instruction.  Note that as we're emulating
	 * the trigger instruction, we need to increment PC.
	 */
	if (fpscr & FPSCR_IXE) {
		regs->ARM_pc += 4;
		goto emulate;
	}

	barrier();

	/*
	 * Modify fpscr to indicate the number of iterations remaining
	 */
	if (fpexc & FPEXC_EXCEPTION) {
		u32 len;

		len = fpexc + (1 << FPEXC_LENGTH_BIT);

		fpscr &= ~FPSCR_LENGTH_MASK;
		fpscr |= (len & FPEXC_LENGTH_MASK) << (FPSCR_LENGTH_BIT - FPEXC_LENGTH_BIT);
	}

	/*
	 * Handle the first FP instruction.  We used to take note of the
	 * FPEXC bounce reason, but this appears to be unreliable.
	 * Emulate the bounced instruction instead.
	 */
	inst = fmrx(FPINST);
	exceptions = vfp_emulate_instruction(inst, fpscr, regs);
	if (exceptions)
		vfp_raise_exceptions(exceptions, inst, orig_fpscr, regs);

	/*
	 * If there isn't a second FP instruction, exit now.
	 */
	if (!(fpexc & FPEXC_FPV2))
		return;

	/*
	 * The barrier() here prevents fpinst2 being read
	 * before the condition above.
	 */
	barrier();
	trigger = fmrx(FPINST2);
	orig_fpscr = fpscr = fmrx(FPSCR);

 emulate:
	exceptions = vfp_emulate_instruction(trigger, fpscr, regs);
	if (exceptions)
		vfp_raise_exceptions(exceptions, trigger, orig_fpscr, regs);
}
 
/*
 * VFP support code initialisation.
 */
static int __init vfp_init(void)
{
	unsigned int vfpsid;

	/*
	 * First check that there is a VFP that we can use.
	 * The handler is already setup to just log calls, so
	 * we just need to read the VFPSID register.
	 */
	vfpsid = fmrx(FPSID);

	printk(KERN_INFO "VFP support v0.3: ");
	if (VFP_arch) {
		printk("not present\n");
	} else if (vfpsid & FPSID_NODOUBLE) {
		printk("no double precision support\n");
	} else {
		VFP_arch = (vfpsid & FPSID_ARCH_MASK) >> FPSID_ARCH_BIT;  /* Extract the architecture version */
		printk("implementor %02x architecture %d part %02x variant %x rev %x\n",
			(vfpsid & FPSID_IMPLEMENTER_MASK) >> FPSID_IMPLEMENTER_BIT,
			(vfpsid & FPSID_ARCH_MASK) >> FPSID_ARCH_BIT,
			(vfpsid & FPSID_PART_MASK) >> FPSID_PART_BIT,
			(vfpsid & FPSID_VARIANT_MASK) >> FPSID_VARIANT_BIT,
			(vfpsid & FPSID_REV_MASK) >> FPSID_REV_BIT);
		vfp_vector = vfp_support_entry;

		thread_register_notifier(&vfp_notifier_block);
	}
	return 0;
}

late_initcall(vfp_init);
