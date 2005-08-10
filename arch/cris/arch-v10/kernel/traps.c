/* $Id: traps.c,v 1.4 2005/04/24 18:47:55 starvik Exp $
 *
 *  linux/arch/cris/arch-v10/traps.c
 *
 *  Heler functions for trap handlers
 * 
 *  Copyright (C) 2000-2002 Axis Communications AB
 *
 *  Authors:   Bjorn Wesen
 *  	       Hans-Peter Nilsson
 *
 */

#include <linux/config.h>
#include <linux/ptrace.h>
#include <asm/uaccess.h>
#include <asm/arch/sv_addr_ag.h>

extern int raw_printk(const char *fmt, ...);

void 
show_registers(struct pt_regs * regs)
{
	/* We either use rdusp() - the USP register, which might not
	   correspond to the current process for all cases we're called,
	   or we use the current->thread.usp, which is not up to date for
	   the current process.  Experience shows we want the USP
	   register.  */
	unsigned long usp = rdusp();

	raw_printk("IRP: %08lx SRP: %08lx DCCR: %08lx USP: %08lx MOF: %08lx\n",
	       regs->irp, regs->srp, regs->dccr, usp, regs->mof );
	raw_printk(" r0: %08lx  r1: %08lx   r2: %08lx  r3: %08lx\n",
	       regs->r0, regs->r1, regs->r2, regs->r3);
	raw_printk(" r4: %08lx  r5: %08lx   r6: %08lx  r7: %08lx\n",
	       regs->r4, regs->r5, regs->r6, regs->r7);
	raw_printk(" r8: %08lx  r9: %08lx  r10: %08lx r11: %08lx\n",
	       regs->r8, regs->r9, regs->r10, regs->r11);
	raw_printk("r12: %08lx r13: %08lx oR10: %08lx  sp: %08lx\n",
	       regs->r12, regs->r13, regs->orig_r10, regs);
	raw_printk("R_MMU_CAUSE: %08lx\n", (unsigned long)*R_MMU_CAUSE);
	raw_printk("Process %s (pid: %d, stackpage=%08lx)\n",
	       current->comm, current->pid, (unsigned long)current);

	/*
         * When in-kernel, we also print out the stack and code at the
         * time of the fault..
         */
        if (! user_mode(regs)) {
	  	int i;

                show_stack(NULL, (unsigned long*)usp);

		/* Dump kernel stack if the previous dump wasn't one.  */
		if (usp != 0)
			show_stack (NULL, NULL);

                raw_printk("\nCode: ");
                if(regs->irp < PAGE_OFFSET)
                        goto bad;

		/* Often enough the value at regs->irp does not point to
		   the interesting instruction, which is most often the
		   _previous_ instruction.  So we dump at an offset large
		   enough that instruction decoding should be in sync at
		   the interesting point, but small enough to fit on a row
		   (sort of).  We point out the regs->irp location in a
		   ksymoops-friendly way by wrapping the byte for that
		   address in parentheses.  */
                for(i = -12; i < 12; i++)
                {
                        unsigned char c;
                        if(__get_user(c, &((unsigned char*)regs->irp)[i])) {
bad:
                                raw_printk(" Bad IP value.");
                                break;
                        }

			if (i == 0)
			  raw_printk("(%02x) ", c);
			else
			  raw_printk("%02x ", c);
                }
		raw_printk("\n");
        }
}

/* Called from entry.S when the watchdog has bitten
 * We print out something resembling an oops dump, and if
 * we have the nice doggy development flag set, we halt here
 * instead of rebooting.
 */

extern void reset_watchdog(void);
extern void stop_watchdog(void);


void
watchdog_bite_hook(struct pt_regs *regs)
{
#ifdef CONFIG_ETRAX_WATCHDOG_NICE_DOGGY
	local_irq_disable();
	stop_watchdog();
	show_registers(regs);
	while(1) /* nothing */;
#else
	show_registers(regs);
#endif	
}

/* This is normally the 'Oops' routine */
void 
die_if_kernel(const char * str, struct pt_regs * regs, long err)
{
	if(user_mode(regs))
		return;

#ifdef CONFIG_ETRAX_WATCHDOG_NICE_DOGGY
	/* This printout might take too long and trigger the 
	 * watchdog normally. If we're in the nice doggy
	 * development mode, stop the watchdog during printout.
	 */
	stop_watchdog();
#endif

	raw_printk("%s: %04lx\n", str, err & 0xffff);

	show_registers(regs);

#ifdef CONFIG_ETRAX_WATCHDOG_NICE_DOGGY
	reset_watchdog();
#endif
	do_exit(SIGSEGV);
}

void arch_enable_nmi(void)
{
  asm volatile("setf m");
}
