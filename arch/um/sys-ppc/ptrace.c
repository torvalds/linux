#include "linux/sched.h"
#include "asm/ptrace.h"

int putreg(struct task_struct *child, unsigned long regno, 
		  unsigned long value)
{
	child->thread.process_regs.regs[regno >> 2] = value;
	return 0;
}

unsigned long getreg(struct task_struct *child, unsigned long regno)
{
	unsigned long retval = ~0UL;

	retval &= child->thread.process_regs.regs[regno >> 2];
	return retval;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
