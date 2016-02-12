#include <asm/branch.h>
#include <asm/cacheflush.h>
#include <asm/fpu_emulator.h>
#include <asm/inst.h>
#include <asm/mipsregs.h>
#include <asm/uaccess.h>

#include "ieee754.h"

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

/*
 * Set up an emulation frame for instruction IR, from a delay slot of
 * a branch jumping to CPC.  Return 0 if successful, -1 if no emulation
 * required, otherwise a signal number causing a frame setup failure.
 */
int mips_dsemul(struct pt_regs *regs, mips_instruction ir, unsigned long cpc)
{
	int isa16 = get_isa16_mode(regs->cp0_epc);
	mips_instruction break_math;
	struct emuframe __user *fr;
	int err;

	/* NOP is easy */
	if (ir == 0)
		return -1;

	/* microMIPS instructions */
	if (isa16) {
		union mips_instruction insn = { .word = ir };

		/* NOP16 aka MOVE16 $0, $0 */
		if ((ir >> 16) == MM_NOP16)
			return -1;

		/* ADDIUPC */
		if (insn.mm_a_format.opcode == mm_addiupc_op) {
			unsigned int rs;
			s32 v;

			rs = (((insn.mm_a_format.rs + 0x1e) & 0xf) + 2);
			v = regs->cp0_epc & ~3;
			v += insn.mm_a_format.simmediate << 2;
			regs->regs[rs] = (long)v;
			return -1;
		}
	}

	pr_debug("dsemul %lx %lx\n", regs->cp0_epc, cpc);

	/*
	 * The strategy is to push the instruction onto the user stack
	 * and put a trap after it which we can catch and jump to
	 * the required address any alternative apart from full
	 * instruction emulation!!.
	 *
	 * Algorithmics used a system call instruction, and
	 * borrowed that vector.  MIPS/Linux version is a bit
	 * more heavyweight in the interests of portability and
	 * multiprocessor support.  For Linux we use a BREAK 514
	 * instruction causing a breakpoint exception.
	 */
	break_math = BREAK_MATH(isa16);

	/* Ensure that the two instructions are in the same cache line */
	fr = (struct emuframe __user *)
		((regs->regs[29] - sizeof(struct emuframe)) & ~0x7);

	/* Verify that the stack pointer is not competely insane */
	if (unlikely(!access_ok(VERIFY_WRITE, fr, sizeof(struct emuframe))))
		return SIGBUS;

	if (isa16) {
		err = __put_user(ir >> 16,
				 (u16 __user *)(&fr->emul));
		err |= __put_user(ir & 0xffff,
				  (u16 __user *)((long)(&fr->emul) + 2));
		err |= __put_user(break_math >> 16,
				  (u16 __user *)(&fr->badinst));
		err |= __put_user(break_math & 0xffff,
				  (u16 __user *)((long)(&fr->badinst) + 2));
	} else {
		err = __put_user(ir, &fr->emul);
		err |= __put_user(break_math, &fr->badinst);
	}

	err |= __put_user((mips_instruction)BD_COOKIE, &fr->cookie);
	err |= __put_user(cpc, &fr->epc);

	if (unlikely(err)) {
		MIPS_FPU_EMU_INC_STATS(errors);
		return SIGBUS;
	}

	regs->cp0_epc = (unsigned long)&fr->emul | isa16;

	flush_cache_sigtramp((unsigned long)&fr->emul);

	return 0;
}

int do_dsemulret(struct pt_regs *xcp)
{
	int isa16 = get_isa16_mode(xcp->cp0_epc);
	struct emuframe __user *fr;
	unsigned long epc;
	u32 insn, cookie;
	int err = 0;
	u16 instr[2];

	fr = (struct emuframe __user *)
		(msk_isa16_mode(xcp->cp0_epc) - sizeof(mips_instruction));

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
	if (isa16) {
		err = __get_user(instr[0],
				 (u16 __user *)(&fr->badinst));
		err |= __get_user(instr[1],
				  (u16 __user *)((long)(&fr->badinst) + 2));
		insn = (instr[0] << 16) | instr[1];
	} else {
		err = __get_user(insn, &fr->badinst);
	}
	err |= __get_user(cookie, &fr->cookie);

	if (unlikely(err ||
		     insn != BREAK_MATH(isa16) || cookie != BD_COOKIE)) {
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

	pr_debug("dsemulret\n");

	if (__get_user(epc, &fr->epc)) {		/* Saved EPC */
		/* This is not a good situation to be in */
		force_sig(SIGBUS, current);

		return 0;
	}

	/* Set EPC to return to post-branch instruction */
	xcp->cp0_epc = epc;
	MIPS_FPU_EMU_INC_STATS(ds_emul);
	return 1;
}
