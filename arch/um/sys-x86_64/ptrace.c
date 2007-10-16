/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#define __FRAME_OFFSETS
#include <asm/ptrace.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <asm/elf.h>

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

int poke_user(struct task_struct *child, long addr, long data)
{
        if ((addr & 3) || addr < 0)
                return -EIO;

        if (addr < MAX_REG_OFFSET)
                return putreg(child, addr, data);
        else if((addr >= offsetof(struct user, u_debugreg[0])) &&
                (addr <= offsetof(struct user, u_debugreg[7]))){
                addr -= offsetof(struct user, u_debugreg[0]);
                addr = addr >> 2;
                if((addr == 4) || (addr == 5)) return -EIO;
                child->thread.arch.debugregs[addr] = data;
                return 0;
        }
        return -EIO;
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

int peek_user(struct task_struct *child, long addr, long data)
{
	/* read the word at location addr in the USER area. */
        unsigned long tmp;

        if ((addr & 3) || addr < 0)
                return -EIO;

        tmp = 0;  /* Default return condition */
        if(addr < MAX_REG_OFFSET){
                tmp = getreg(child, addr);
        }
        else if((addr >= offsetof(struct user, u_debugreg[0])) &&
                (addr <= offsetof(struct user, u_debugreg[7]))){
                addr -= offsetof(struct user, u_debugreg[0]);
                addr = addr >> 2;
                tmp = child->thread.arch.debugregs[addr];
        }
        return put_user(tmp, (unsigned long *) data);
}

void arch_switch(void)
{
/* XXX
	printk("arch_switch\n");
*/
}

/* XXX Mostly copied from sys-i386 */
int is_syscall(unsigned long addr)
{
	unsigned short instr;
	int n;

	n = copy_from_user(&instr, (void __user *) addr, sizeof(instr));
	if(n){
		/* access_process_vm() grants access to vsyscall and stub,
		 * while copy_from_user doesn't. Maybe access_process_vm is
		 * slow, but that doesn't matter, since it will be called only
		 * in case of singlestepping, if copy_from_user failed.
		 */
		n = access_process_vm(current, addr, &instr, sizeof(instr), 0);
		if(n != sizeof(instr)) {
			printk("is_syscall : failed to read instruction from "
			       "0x%lx\n", addr);
			return(1);
		}
	}
	/* sysenter */
	return(instr == 0x050f);
}

int get_fpregs(struct user_i387_struct __user *buf, struct task_struct *child)
{
	int err, n, cpu = ((struct thread_info *) child->stack)->cpu;
	long fpregs[HOST_FP_SIZE];

	BUG_ON(sizeof(*buf) != sizeof(fpregs));
	err = save_fp_registers(userspace_pid[cpu], fpregs);
	if (err)
		return err;

	n = copy_to_user((void *) buf, fpregs, sizeof(fpregs));
	if(n > 0)
		return -EFAULT;

	return n;
}

int set_fpregs(struct user_i387_struct __user *buf, struct task_struct *child)
{
	int n, cpu = ((struct thread_info *) child->stack)->cpu;
	long fpregs[HOST_FP_SIZE];

	BUG_ON(sizeof(*buf) != sizeof(fpregs));
	n = copy_from_user(fpregs, (void *) buf, sizeof(fpregs));
	if (n > 0)
		return -EFAULT;

	return restore_fp_registers(userspace_pid[cpu], fpregs);
}

long subarch_ptrace(struct task_struct *child, long request, long addr,
		    long data)
{
	int ret = -EIO;

	switch (request) {
	case PTRACE_GETFPXREGS: /* Get the child FPU state. */
		ret = get_fpregs((struct user_i387_struct __user *) data,
				 child);
		break;
	case PTRACE_SETFPXREGS: /* Set the child FPU state. */
		ret = set_fpregs((struct user_i387_struct __user *) data,
				 child);
		break;
	}

	return ret;
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
