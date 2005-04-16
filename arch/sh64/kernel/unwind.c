/*
 * arch/sh64/kernel/unwind.c
 *
 * Copyright (C) 2004  Paul Mundt
 * Copyright (C) 2004  Richard Curnow
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/io.h>

static u8 regcache[63];

/*
 * Finding the previous stack frame isn't horribly straightforward as it is
 * on some other platforms. In the sh64 case, we don't have "linked" stack
 * frames, so we need to do a bit of work to determine the previous frame,
 * and in turn, the previous r14/r18 pair.
 *
 * There are generally a few cases which determine where we can find out
 * the r14/r18 values. In the general case, this can be determined by poking
 * around the prologue of the symbol PC is in (note that we absolutely must
 * have frame pointer support as well as the kernel symbol table mapped,
 * otherwise we can't even get this far).
 *
 * In other cases, such as the interrupt/exception path, we can poke around
 * the sp/fp.
 *
 * Notably, this entire approach is somewhat error prone, and in the event
 * that the previous frame cannot be determined, that's all we can do.
 * Either way, this still leaves us with a more correct backtrace then what
 * we would be able to come up with by walking the stack (which is garbage
 * for anything beyond the first frame).
 *						-- PFM.
 */
static int lookup_prev_stack_frame(unsigned long fp, unsigned long pc,
		      unsigned long *pprev_fp, unsigned long *pprev_pc,
		      struct pt_regs *regs)
{
	const char *sym;
	char *modname, namebuf[128];
	unsigned long offset, size;
	unsigned long prologue = 0;
	unsigned long fp_displacement = 0;
	unsigned long fp_prev = 0;
	unsigned long offset_r14 = 0, offset_r18 = 0;
	int i, found_prologue_end = 0;

	sym = kallsyms_lookup(pc, &size, &offset, &modname, namebuf);
	if (!sym)
		return -EINVAL;

	prologue = pc - offset;
	if (!prologue)
		return -EINVAL;

	/* Validate fp, to avoid risk of dereferencing a bad pointer later.
	   Assume 128Mb since that's the amount of RAM on a Cayman.  Modify
	   when there is an SH-5 board with more. */
	if ((fp < (unsigned long) phys_to_virt(__MEMORY_START)) ||
	    (fp >= (unsigned long)(phys_to_virt(__MEMORY_START)) + 128*1024*1024) ||
	    ((fp & 7) != 0)) {
		return -EINVAL;
	}

	/*
	 * Depth to walk, depth is completely arbitrary.
	 */
	for (i = 0; i < 100; i++, prologue += sizeof(unsigned long)) {
		unsigned long op;
		u8 major, minor;
		u8 src, dest, disp;

		op = *(unsigned long *)prologue;

		major = (op >> 26) & 0x3f;
		src   = (op >> 20) & 0x3f;
		minor = (op >> 16) & 0xf;
		disp  = (op >> 10) & 0x3f;
		dest  = (op >>  4) & 0x3f;

		/*
		 * Stack frame creation happens in a number of ways.. in the
		 * general case when the stack frame is less than 511 bytes,
		 * it's generally created by an addi or addi.l:
		 *
		 *	addi/addi.l r15, -FRAME_SIZE, r15
		 *
		 * in the event that the frame size is bigger than this, it's
		 * typically created using a movi/sub pair as follows:
		 *
		 *	movi	FRAME_SIZE, rX
		 *	sub	r15, rX, r15
		 */

		switch (major) {
		case (0x00 >> 2):
			switch (minor) {
			case 0x8: /* add.l */
			case 0x9: /* add */
				/* Look for r15, r63, r14 */
				if (src == 15 && disp == 63 && dest == 14)
					found_prologue_end = 1;

				break;
			case 0xa: /* sub.l */
			case 0xb: /* sub */
				if (src != 15 || dest != 15)
					continue;

				fp_displacement -= regcache[disp];
				fp_prev = fp - fp_displacement;
				break;
			}
			break;
		case (0xa8 >> 2): /* st.l */
			if (src != 15)
				continue;

			switch (dest) {
			case 14:
				if (offset_r14 || fp_displacement == 0)
					continue;

				offset_r14 = (u64)(((((s64)op >> 10) & 0x3ff) << 54) >> 54);
				offset_r14 *= sizeof(unsigned long);
				offset_r14 += fp_displacement;
				break;
			case 18:
				if (offset_r18 || fp_displacement == 0)
					continue;

				offset_r18 = (u64)(((((s64)op >> 10) & 0x3ff) << 54) >> 54);
				offset_r18 *= sizeof(unsigned long);
				offset_r18 += fp_displacement;
				break;
			}

			break;
		case (0xcc >> 2): /* movi */
			if (dest >= 63) {
				printk(KERN_NOTICE "%s: Invalid dest reg %d "
				       "specified in movi handler. Failed "
				       "opcode was 0x%lx: ", __FUNCTION__,
				       dest, op);

				continue;
			}

			/* Sign extend */
			regcache[dest] =
				((((s64)(u64)op >> 10) & 0xffff) << 54) >> 54;
			break;
		case (0xd0 >> 2): /* addi */
		case (0xd4 >> 2): /* addi.l */
			/* Look for r15, -FRAME_SIZE, r15 */
			if (src != 15 || dest != 15)
				continue;

			/* Sign extended frame size.. */
			fp_displacement +=
				(u64)(((((s64)op >> 10) & 0x3ff) << 54) >> 54);
			fp_prev = fp - fp_displacement;
			break;
		}

		if (found_prologue_end && offset_r14 && (offset_r18 || *pprev_pc) && fp_prev)
			break;
	}

	if (offset_r14 == 0 || fp_prev == 0) {
		if (!offset_r14)
			pr_debug("Unable to find r14 offset\n");
		if (!fp_prev)
			pr_debug("Unable to find previous fp\n");

		return -EINVAL;
	}

	/* For innermost leaf function, there might not be a offset_r18 */
	if (!*pprev_pc && (offset_r18 == 0))
		return -EINVAL;

	*pprev_fp = *(unsigned long *)(fp_prev + offset_r14);

	if (offset_r18)
		*pprev_pc = *(unsigned long *)(fp_prev + offset_r18);

	*pprev_pc &= ~1;

	return 0;
}

/* Don't put this on the stack since we'll want to call sh64_unwind
 * when we're close to underflowing the stack anyway. */
static struct pt_regs here_regs;

extern const char syscall_ret;
extern const char ret_from_syscall;
extern const char ret_from_exception;
extern const char ret_from_irq;

static void sh64_unwind_inner(struct pt_regs *regs);

static void unwind_nested (unsigned long pc, unsigned long fp)
{
	if ((fp >= __MEMORY_START) &&
	    ((fp & 7) == 0)) {
		sh64_unwind_inner((struct pt_regs *) fp);
	}
}

static void sh64_unwind_inner(struct pt_regs *regs)
{
	unsigned long pc, fp;
	int ofs = 0;
	int first_pass;

	pc = regs->pc & ~1;
	fp = regs->regs[14];

	first_pass = 1;
	for (;;) {
		int cond;
		unsigned long next_fp, next_pc;

		if (pc == ((unsigned long) &syscall_ret & ~1)) {
			printk("SYSCALL\n");
			unwind_nested(pc,fp);
			return;
		}

		if (pc == ((unsigned long) &ret_from_syscall & ~1)) {
			printk("SYSCALL (PREEMPTED)\n");
			unwind_nested(pc,fp);
			return;
		}

		/* In this case, the PC is discovered by lookup_prev_stack_frame but
		   it has 4 taken off it to look like the 'caller' */
		if (pc == ((unsigned long) &ret_from_exception & ~1)) {
			printk("EXCEPTION\n");
			unwind_nested(pc,fp);
			return;
		}

		if (pc == ((unsigned long) &ret_from_irq & ~1)) {
			printk("IRQ\n");
			unwind_nested(pc,fp);
			return;
		}

		cond = ((pc >= __MEMORY_START) && (fp >= __MEMORY_START) &&
			((pc & 3) == 0) && ((fp & 7) == 0));

		pc -= ofs;

		printk("[<%08lx>] ", pc);
		print_symbol("%s\n", pc);

		if (first_pass) {
			/* If the innermost frame is a leaf function, it's
			 * possible that r18 is never saved out to the stack.
			 */
			next_pc = regs->regs[18];
		} else {
			next_pc = 0;
		}

		if (lookup_prev_stack_frame(fp, pc, &next_fp, &next_pc, regs) == 0) {
			ofs = sizeof(unsigned long);
			pc = next_pc & ~1;
			fp = next_fp;
		} else {
			printk("Unable to lookup previous stack frame\n");
			break;
		}
		first_pass = 0;
	}

	printk("\n");

}

void sh64_unwind(struct pt_regs *regs)
{
	if (!regs) {
		/*
		 * Fetch current regs if we have no other saved state to back
		 * trace from.
		 */
		regs = &here_regs;

		__asm__ __volatile__ ("ori r14, 0, %0" : "=r" (regs->regs[14]));
		__asm__ __volatile__ ("ori r15, 0, %0" : "=r" (regs->regs[15]));
		__asm__ __volatile__ ("ori r18, 0, %0" : "=r" (regs->regs[18]));

		__asm__ __volatile__ ("gettr tr0, %0" : "=r" (regs->tregs[0]));
		__asm__ __volatile__ ("gettr tr1, %0" : "=r" (regs->tregs[1]));
		__asm__ __volatile__ ("gettr tr2, %0" : "=r" (regs->tregs[2]));
		__asm__ __volatile__ ("gettr tr3, %0" : "=r" (regs->tregs[3]));
		__asm__ __volatile__ ("gettr tr4, %0" : "=r" (regs->tregs[4]));
		__asm__ __volatile__ ("gettr tr5, %0" : "=r" (regs->tregs[5]));
		__asm__ __volatile__ ("gettr tr6, %0" : "=r" (regs->tregs[6]));
		__asm__ __volatile__ ("gettr tr7, %0" : "=r" (regs->tregs[7]));

		__asm__ __volatile__ (
			"pta 0f, tr0\n\t"
			"blink tr0, %0\n\t"
			"0: nop"
			: "=r" (regs->pc)
		);
	}

	printk("\nCall Trace:\n");
	sh64_unwind_inner(regs);
}

