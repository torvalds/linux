/*
 * Copyright (C) 2004 PathScale, Inc
 * Copyright (C) 2004 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <errno.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#ifdef __i386__
#include <sys/user.h>
#endif
#include <longjmp.h>
#include <sysdep/ptrace_user.h>
#include <sys/uio.h>
#include <asm/sigcontext.h>
#include <linux/elf.h>
#include <registers.h>
#include <sys/mman.h>

static unsigned long ptrace_regset;
unsigned long host_fp_size;

int get_fp_registers(int pid, unsigned long *regs)
{
	struct iovec iov = {
		.iov_base = regs,
		.iov_len = host_fp_size,
	};

	if (ptrace(PTRACE_GETREGSET, pid, ptrace_regset, &iov) < 0)
		return -errno;
	return 0;
}

int put_fp_registers(int pid, unsigned long *regs)
{
	struct iovec iov = {
		.iov_base = regs,
		.iov_len = host_fp_size,
	};

	if (ptrace(PTRACE_SETREGSET, pid, ptrace_regset, &iov) < 0)
		return -errno;
	return 0;
}

int arch_init_registers(int pid)
{
	struct iovec iov = {
		/* Just use plenty of space, it does not cost us anything */
		.iov_len = 2 * 1024 * 1024,
	};
	int ret;

	iov.iov_base = mmap(NULL, iov.iov_len, PROT_WRITE | PROT_READ,
			    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (iov.iov_base == MAP_FAILED)
		return -ENOMEM;

	/* GDB has x86_xsave_length, which uses x86_cpuid_count */
	ptrace_regset = NT_X86_XSTATE;
	ret = ptrace(PTRACE_GETREGSET, pid, ptrace_regset, &iov);
	if (ret)
		ret = -errno;

	if (ret == -ENODEV) {
#ifdef CONFIG_X86_32
		ptrace_regset = NT_PRXFPREG;
#else
		ptrace_regset = NT_PRFPREG;
#endif
		iov.iov_len = 2 * 1024 * 1024;
		ret = ptrace(PTRACE_GETREGSET, pid, ptrace_regset, &iov);
		if (ret)
			ret = -errno;
	}

	munmap(iov.iov_base, 2 * 1024 * 1024);

	host_fp_size = iov.iov_len;

	return ret;
}

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
