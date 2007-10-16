/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include "linux/mm.h"
#include "linux/sched.h"
#include "asm/uaccess.h"
#include "skas.h"

extern int arch_switch_tls(struct task_struct *from, struct task_struct *to);

void arch_switch_to(struct task_struct *from, struct task_struct *to)
{
	int err = arch_switch_tls(from, to);
	if (!err)
		return;

	if (err != -EINVAL)
		printk(KERN_WARNING "arch_switch_tls failed, errno %d, "
		       "not EINVAL\n", -err);
	else
		printk(KERN_WARNING "arch_switch_tls failed, errno = EINVAL\n");
}

int is_syscall(unsigned long addr)
{
	unsigned short instr;
	int n;

	n = copy_from_user(&instr, (void __user *) addr, sizeof(instr));
	if (n) {
		/* access_process_vm() grants access to vsyscall and stub,
		 * while copy_from_user doesn't. Maybe access_process_vm is
		 * slow, but that doesn't matter, since it will be called only
		 * in case of singlestepping, if copy_from_user failed.
		 */
		n = access_process_vm(current, addr, &instr, sizeof(instr), 0);
		if (n != sizeof(instr)) {
			printk(KERN_ERR "is_syscall : failed to read "
			       "instruction from 0x%lx\n", addr);
			return 1;
		}
	}
	/* int 0x80 or sysenter */
	return (instr == 0x80cd) || (instr == 0x340f);
}

/* determines which flags the user has access to. */
/* 1 = access 0 = no access */
#define FLAG_MASK 0x00044dd5

int putreg(struct task_struct *child, int regno, unsigned long value)
{
	regno >>= 2;
	switch (regno) {
	case FS:
		if (value && (value & 3) != 3)
			return -EIO;
		PT_REGS_FS(&child->thread.regs) = value;
		return 0;
	case GS:
		if (value && (value & 3) != 3)
			return -EIO;
		PT_REGS_GS(&child->thread.regs) = value;
		return 0;
	case DS:
	case ES:
		if (value && (value & 3) != 3)
			return -EIO;
		value &= 0xffff;
		break;
	case SS:
	case CS:
		if ((value & 3) != 3)
			return -EIO;
		value &= 0xffff;
		break;
	case EFL:
		value &= FLAG_MASK;
		value |= PT_REGS_EFLAGS(&child->thread.regs);
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
	else if ((addr >= offsetof(struct user, u_debugreg[0])) &&
		 (addr <= offsetof(struct user, u_debugreg[7]))) {
		addr -= offsetof(struct user, u_debugreg[0]);
		addr = addr >> 2;
		if ((addr == 4) || (addr == 5))
			return -EIO;
		child->thread.arch.debugregs[addr] = data;
		return 0;
	}
	return -EIO;
}

unsigned long getreg(struct task_struct *child, int regno)
{
	unsigned long retval = ~0UL;

	regno >>= 2;
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
	}
	return retval;
}

/* read the word at location addr in the USER area. */
int peek_user(struct task_struct *child, long addr, long data)
{
	unsigned long tmp;

	if ((addr & 3) || addr < 0)
		return -EIO;

	tmp = 0;  /* Default return condition */
	if (addr < MAX_REG_OFFSET) {
		tmp = getreg(child, addr);
	}
	else if ((addr >= offsetof(struct user, u_debugreg[0])) &&
		 (addr <= offsetof(struct user, u_debugreg[7]))) {
		addr -= offsetof(struct user, u_debugreg[0]);
		addr = addr >> 2;
		tmp = child->thread.arch.debugregs[addr];
	}
	return put_user(tmp, (unsigned long __user *) data);
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

int get_fpxregs(struct user_fxsr_struct __user *buf, struct task_struct *child)
{
	int err, n, cpu = ((struct thread_info *) child->stack)->cpu;
	long fpregs[HOST_XFP_SIZE];

	BUG_ON(sizeof(*buf) != sizeof(fpregs));
	err = save_fpx_registers(userspace_pid[cpu], fpregs);
	if (err)
		return err;

	n = copy_to_user((void *) buf, fpregs, sizeof(fpregs));
	if(n > 0)
		return -EFAULT;

	return n;
}

int set_fpxregs(struct user_fxsr_struct __user *buf, struct task_struct *child)
{
	int n, cpu = ((struct thread_info *) child->stack)->cpu;
	long fpregs[HOST_XFP_SIZE];

	BUG_ON(sizeof(*buf) != sizeof(fpregs));
	n = copy_from_user(fpregs, (void *) buf, sizeof(fpregs));
	if (n > 0)
		return -EFAULT;

	return restore_fpx_registers(userspace_pid[cpu], fpregs);
}

#ifdef notdef
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu)
{
	fpu->cwd = (((SC_FP_CW(PT_REGS_SC(regs)) & 0xffff) << 16) |
		    (SC_FP_SW(PT_REGS_SC(regs)) & 0xffff));
	fpu->swd = SC_FP_CSSEL(PT_REGS_SC(regs)) & 0xffff;
	fpu->twd = SC_FP_IPOFF(PT_REGS_SC(regs));
	fpu->fip = SC_FP_CSSEL(PT_REGS_SC(regs)) & 0xffff;
	fpu->fcs = SC_FP_DATAOFF(PT_REGS_SC(regs));
	fpu->foo = SC_FP_DATASEL(PT_REGS_SC(regs));
	fpu->fos = 0;
	memcpy(fpu->st_space, (void *) SC_FP_ST(PT_REGS_SC(regs)),
	       sizeof(fpu->st_space));
	return 1;
}
#endif

int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu )
{
	return 1;
}

long subarch_ptrace(struct task_struct *child, long request, long addr,
		    long data)
{
	return -EIO;
}
