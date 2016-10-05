/*
 * Copyright (C) 2004 PathScale, Inc
 * Copyright (C) 2004 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <errno.h>
#include <sys/ptrace.h>
#ifdef __i386__
#include <sys/user.h>
#endif
#include <longjmp.h>
#include <sysdep/ptrace_user.h>
#include <sys/uio.h>
#include <asm/sigcontext.h>
#include <linux/elf.h>

int have_xstate_support;

int save_i387_registers(int pid, unsigned long *fp_regs)
{
	if (ptrace(PTRACE_GETFPREGS, pid, 0, fp_regs) < 0)
		return -errno;
	return 0;
}

int save_fp_registers(int pid, unsigned long *fp_regs)
{
	struct iovec iov;

	if (have_xstate_support) {
		iov.iov_base = fp_regs;
		iov.iov_len = sizeof(struct _xstate);
		if (ptrace(PTRACE_GETREGSET, pid, NT_X86_XSTATE, &iov) < 0)
			return -errno;
		return 0;
	} else {
		return save_i387_registers(pid, fp_regs);
	}
}

int restore_i387_registers(int pid, unsigned long *fp_regs)
{
	if (ptrace(PTRACE_SETFPREGS, pid, 0, fp_regs) < 0)
		return -errno;
	return 0;
}

int restore_fp_registers(int pid, unsigned long *fp_regs)
{
	struct iovec iov;

	if (have_xstate_support) {
		iov.iov_base = fp_regs;
		iov.iov_len = sizeof(struct _xstate);
		if (ptrace(PTRACE_SETREGSET, pid, NT_X86_XSTATE, &iov) < 0)
			return -errno;
		return 0;
	} else {
		return restore_i387_registers(pid, fp_regs);
	}
}

#ifdef __i386__
int have_fpx_regs = 1;
int save_fpx_registers(int pid, unsigned long *fp_regs)
{
	if (ptrace(PTRACE_GETFPXREGS, pid, 0, fp_regs) < 0)
		return -errno;
	return 0;
}

int restore_fpx_registers(int pid, unsigned long *fp_regs)
{
	if (ptrace(PTRACE_SETFPXREGS, pid, 0, fp_regs) < 0)
		return -errno;
	return 0;
}

int get_fp_registers(int pid, unsigned long *regs)
{
	if (have_fpx_regs)
		return save_fpx_registers(pid, regs);
	else
		return save_fp_registers(pid, regs);
}

int put_fp_registers(int pid, unsigned long *regs)
{
	if (have_fpx_regs)
		return restore_fpx_registers(pid, regs);
	else
		return restore_fp_registers(pid, regs);
}

void arch_init_registers(int pid)
{
	struct user_fpxregs_struct fpx_regs;
	int err;

	err = ptrace(PTRACE_GETFPXREGS, pid, 0, &fpx_regs);
	if (!err)
		return;

	if (errno != EIO)
		panic("check_ptrace : PTRACE_GETFPXREGS failed, errno = %d",
		      errno);

	have_fpx_regs = 0;
}
#else

int get_fp_registers(int pid, unsigned long *regs)
{
	return save_fp_registers(pid, regs);
}

int put_fp_registers(int pid, unsigned long *regs)
{
	return restore_fp_registers(pid, regs);
}

void arch_init_registers(int pid)
{
	struct _xstate fp_regs;
	struct iovec iov;

	iov.iov_base = &fp_regs;
	iov.iov_len = sizeof(struct _xstate);
	if (ptrace(PTRACE_GETREGSET, pid, NT_X86_XSTATE, &iov) == 0)
		have_xstate_support = 1;
}
#endif

unsigned long get_thread_reg(int reg, jmp_buf *buf)
{
	switch (reg) {
#ifdef __i386__
	case HOST_IP:
		return buf[0]->__eip;
	case HOST_SP:
		return buf[0]->__esp;
	case HOST_BP:
		return buf[0]->__ebp;
#else
	case HOST_IP:
		return buf[0]->__rip;
	case HOST_SP:
		return buf[0]->__rsp;
	case HOST_BP:
		return buf[0]->__rbp;
#endif
	default:
		printk(UM_KERN_ERR "get_thread_regs - unknown register %d\n",
		       reg);
		return 0;
	}
}
