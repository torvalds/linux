#include <linux/compiler.h>
#include <linux/mm.h>
#include <linux/signal.h>
#include <linux/smp.h>

#include <asm/asm.h>
#include <asm/bootinfo.h>
#include <asm/byteorder.h>
#include <asm/cpu.h>
#include <asm/inst.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/branch.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/cacheflush.h>

#include <asm/fpu_emulator.h>

#include "ieee754.h"

/* Strap kernel emulator for full MIPS IV emulation */

#ifdef __mips
#undef __mips
#endif
#define __mips 4

/*
 * Emulate the arbritrary instruction ir at xcp->cp0_epc.  Required when
 * we have to emulate the instruction in a COP1 branch delay slot.  Do
 * not change cp0_epc due to the instruction
 *
 * According to the spec:
 * 1) it shouldn't be a branch :-)
 * 2) it can be a COP instruction :-(
 * 3) if we are tring to run a protected memory space we must take
 *    special care on memory access instructions :-(
 */

/*
 * "Trampoline" return routine to catch exception following
 *  execution of delay-slot instruction execution.
 */

struct emuframe {
	mips_instruction	emul;
	mips_instruction	badinst;
	mips_instruction	cookie;
	unsigned long		epc;
};

int mips_dsemul(struct pt_regs *regs, mips_instruction ir, unsigned long cpc)
{
	extern asmlinkage void handle_dsemulret(void);
	struct emuframe __user *fr;
	int err;

	if (ir == 0) {		/* a nop is easy */
		regs->cp0_epc = cpc;
		regs->cp0_cause &= ~CAUSEF_BD;
		return 0;
	}
#ifdef DSEMUL_TRACE
	printk("dsemul %lx %lx\n", regs->cp0_epc, cpc);

#endif

	/*
	 * The strategy is to push the instruction onto the user stack
	 * and put a trap after it which we can catch and jump to
	 * the required address any alternative apart from full
	 * instruction emulation!!.
	 *
	 * Algorithmics used a system call instruction, and
	 * borrowed that vector.  MIPS/Linux version is a bit
	 * more heavyweight in the interests of portability and
	 * multiprocessor support.  For Linux we generate a
	 * an unaligned access and force an address error exception.
	 *
	 * For embedded systems (stand-alone) we prefer to use a
	 * non-existing CP1 instruction. This prevents us from emulating
	 * branches, but gives us a cleaner interface to the exception
	 * handler (single entry point).
	 */

	/* Ensure that the two instructions are in the same cache line */
	fr = (struct emuframe __user *)
		((regs->regs[29] - sizeof(struct emuframe)) & ~0x7);

	/* Verify that the stack pointer is not competely insane */
	if (unlikely(!access_ok(VERIFY_WRITE, fr, sizeof(struct emuframe))))
		return SIGBUS;

	err = __put_user(ir, &fr->emul);
	err |= __put_user((mips_instruction)BREAK_MATH, &fr->badinst);
	err |= __put_user((mips_instruction)BD_COOKIE, &fr->cookie);
	err |= __put_user(cpc, &fr->epc);

	if (unlikely(err)) {
		MIPS_FPU_EMU_INC_STATS(errors);
		return SIGBUS;
	}

	regs->cp0_epc = (unsigned long) &fr->emul;

	flush_cache_sigtramp((unsigned long)&fr->badinst);

	return SIGILL;		/* force out of emulation loop */
}

int do_dsemulret(struct pt_regs *xcp)
{
	struct emuframe __user *fr;
	unsigned long epc;
	u32 insn, cookie;
	int err = 0;

	fr = (struct emuframe __user *)
		(xcp->cp0_epc - sizeof(mips_instruction));

	/*
	 * If we can't even access the area, something is very wrong, but we'll
	 * leave that to the default handling
	 */
	if (!access_ok(VERIFY_READ, fr, sizeof(struct emuframe)))
		return 0;

	/*
	 * Do some sanity checking on the stackframe:
	 *
	 *  - Is the instruction pointed to by the EPC an BREAK_MATH?
	 *  - Is the following memory word the BD_COOKIE?
	 */
	err = __get_user(insn, &fr->badinst);
	err |= __get_user(cookie, &fr->cookie);

	if (unlikely(err || (insn != BREAK_MATH) || (cookie != BD_COOKIE))) {
		MIPS_FPU_EMU_INC_STATS(errors);
		return 0;
	}

	/*
	 * At this point, we are satisfied that it's a BD emulation trap.  Yes,
	 * a user might have deliberately put two malformed and useless
	 * instructions in a row in his program, in which case he's in for a
	 * nasty surprise - the next instruction will be treated as a
	 * continuation address!  Alas, this seems to be the only way that we
	 * can handle signals, recursion, and longjmps() in the context of
	 * emulating the branch delay instruction.
	 */

#ifdef DSEMUL_TRACE
	printk("dsemulret\n");
#endif
	if (__get_user(epc, &fr->epc)) {		/* Saved EPC */
		/* This is not a good situation to be in */
		force_sig(SIGBUS, current);

		return 0;
	}

	/* Set EPC to return to post-branch instruction */
	xcp->cp0_epc = epc;

	return 1;
}
