/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#define __FRAME_OFFSETS
#include "asm/ptrace.h"
#include "linux/sched.h"
#include "linux/errno.h"
#include "asm/elf.h"

/* XXX x86_64 */
unsigned long not_ss;
unsigned long not_ds;
unsigned long not_es;

#define SC_SS(r) (not_ss)
#define SC_DS(r) (not_ds)
#define SC_ES(r) (not_es)

/* determines which flags the user has access to. */
/* 1 = access 0 = no access */
#define FLAG_MASK 0x44dd5UL

int putreg(struct task_struct *child, int regno, unsigned long value)
{
	unsigned long tmp;

#ifdef TIF_IA32
	/* Some code in the 64bit emulation may not be 64bit clean.
	   Don't take any chances. */
	if (test_tsk_thread_flag(child, TIF_IA32))
		value &= 0xffffffff;
#endif
	switch (regno){
	case FS:
	case GS:
	case DS:
	case ES:
	case SS:
	case CS:
		if (value && (value & 3) != 3)
			return -EIO;
		value &= 0xffff;
		break;

	case FS_BASE:
	case GS_BASE:
		if (!((value >> 48) == 0 || (value >> 48) == 0xffff))
			return -EIO;
		break;

	case EFLAGS:
		value &= FLAG_MASK;
		tmp = PT_REGS_EFLAGS(&child->thread.regs) & ~FLAG_MASK;
		value |= tmp;
		break;
	}

	PT_REGS_SET(&child->thread.regs, regno, value);
	return 0;
}

unsigned long getreg(struct task_struct *child, int regno)
{
	unsigned long retval = ~0UL;
	switch (regno) {
	case FS:
	case GS:
	case DS:
	case ES:
	case SS:
	case CS:
		retval = 0xffff;
		/* fall through */
	default:
		retval &= PT_REG(&child->thread.regs, regno);
#ifdef TIF_IA32
		if (test_tsk_thread_flag(child, TIF_IA32))
			retval &= 0xffffffff;
#endif
	}
	return retval;
}

void arch_switch(void)
{
/* XXX
	printk("arch_switch\n");
*/
}

int is_syscall(unsigned long addr)
{
	panic("is_syscall");
}

int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu )
{
	panic("dump_fpu");
	return(1);
}

int get_fpregs(unsigned long buf, struct task_struct *child)
{
	panic("get_fpregs");
	return(0);
}

int set_fpregs(unsigned long buf, struct task_struct *child)
{
	panic("set_fpregs");
	return(0);
}

int get_fpxregs(unsigned long buf, struct task_struct *tsk)
{
	panic("get_fpxregs");
	return(0);
}

int set_fpxregs(unsigned long buf, struct task_struct *tsk)
{
	panic("set_fxpregs");
	return(0);
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
