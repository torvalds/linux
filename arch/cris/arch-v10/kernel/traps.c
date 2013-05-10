/*
 * Helper functions for trap handlers
 *
 * Copyright (C) 2000-2007, Axis Communications AB.
 *
 * Authors:   Bjorn Wesen
 *            Hans-Peter Nilsson
 *
 */

#include <linux/ptrace.h>
#include <asm/uaccess.h>
#include <arch/sv_addr_ag.h>

void
show_registers(struct pt_regs *regs)
{
	/*
	 * It's possible to use either the USP register or current->thread.usp.
	 * USP might not correspond to the current process for all cases this
	 * function is called, and current->thread.usp isn't up to date for the
	 * current process. Experience shows that using USP is the way to go.
	 */
	unsigned long usp = rdusp();

	printk("IRP: %08lx SRP: %08lx DCCR: %08lx USP: %08lx MOF: %08lx\n",
	       regs->irp, regs->srp, regs->dccr, usp, regs->mof);

	printk(" r0: %08lx  r1: %08lx   r2: %08lx  r3: %08lx\n",
	       regs->r0, regs->r1, regs->r2, regs->r3);

	printk(" r4: %08lx  r5: %08lx   r6: %08lx  r7: %08lx\n",
	       regs->r4, regs->r5, regs->r6, regs->r7);

	printk(" r8: %08lx  r9: %08lx  r10: %08lx r11: %08lx\n",
	       regs->r8, regs->r9, regs->r10, regs->r11);

	printk("r12: %08lx r13: %08lx oR10: %08lx  sp: %08lx\n",
	       regs->r12, regs->r13, regs->orig_r10, (long unsigned)regs);

	printk("R_MMU_CAUSE: %08lx\n", (unsigned long)*R_MMU_CAUSE);

	printk("Process %s (pid: %d, stackpage=%08lx)\n",
	       current->comm, current->pid, (unsigned long)current);

	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (!user_mode(regs)) {
		int i;

		show_stack(NULL, (unsigned long *)usp);

		/*
		 * If the previous stack-dump wasn't a kernel one, dump the
		 * kernel stack now.
		 */
		if (usp != 0)
			show_stack(NULL, NULL);

		printk("\nCode: ");

		if (regs->irp < PAGE_OFFSET)
			goto bad_value;

		/*
		 * Quite often the value at regs->irp doesn't point to the
		 * interesting instruction, which often is the previous
		 * instruction. So dump at an offset large enough that the
		 * instruction decoding should be in sync at the interesting
		 * point, but small enough to fit on a row. The regs->irp
		 * location is pointed out in a ksymoops-friendly way by
		 * wrapping the byte for that address in parenthesises.
		 */
		for (i = -12; i < 12; i++) {
			unsigned char c;

			if (__get_user(c, &((unsigned char *)regs->irp)[i])) {
bad_value:
				printk(" Bad IP value.");
				break;
			}

			if (i == 0)
				printk("(%02x) ", c);
			else
				printk("%02x ", c);
		}
		printk("\n");
	}
}

void
arch_enable_nmi(void)
{
	asm volatile ("setf m");
}

extern void (*nmi_handler)(struct pt_regs *);
void handle_nmi(struct pt_regs *regs)
{
	if (nmi_handler)
		nmi_handler(regs);

	/* Wait until nmi is no longer active. (We enable NMI immediately after
	   returning from this function, and we don't want it happening while
	   exiting from the NMI interrupt handler.) */
	while (*R_IRQ_MASK0_RD & IO_STATE(R_IRQ_MASK0_RD, nmi_pin, active))
		;
}

#ifdef CONFIG_DEBUG_BUGVERBOSE
void
handle_BUG(struct pt_regs *regs)
{
	struct bug_frame f;
	unsigned char c;
	unsigned long irp = regs->irp;

	if (__copy_from_user(&f, (const void __user *)(irp - 8), sizeof f))
		return;
	if (f.prefix != BUG_PREFIX || f.magic != BUG_MAGIC)
		return;
	if (__get_user(c, f.filename))
		f.filename = "<bad filename>";

	printk("kernel BUG at %s:%d!\n", f.filename, f.line);
}
#endif
