/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_STUB_H
#define __SYSDEP_STUB_H

#include <stddef.h>
#include <sysdep/ptrace_user.h>
#include <generated/asm-offsets.h>
#include <linux/stddef.h>

#define STUB_MMAP_NR __NR_mmap
#define MMAP_OFFSET(o) (o)

#define __syscall_clobber "r11","rcx","memory"
#define __syscall "syscall"

static __always_inline long stub_syscall0(long syscall)
{
	long ret;

	__asm__ volatile (__syscall
		: "=a" (ret)
		: "0" (syscall) : __syscall_clobber );

	return ret;
}

static __always_inline long stub_syscall2(long syscall, long arg1, long arg2)
{
	long ret;

	__asm__ volatile (__syscall
		: "=a" (ret)
		: "0" (syscall), "D" (arg1), "S" (arg2) : __syscall_clobber );

	return ret;
}

static __always_inline long stub_syscall3(long syscall, long arg1, long arg2,
					  long arg3)
{
	long ret;

	__asm__ volatile (__syscall
		: "=a" (ret)
		: "0" (syscall), "D" (arg1), "S" (arg2), "d" (arg3)
		: __syscall_clobber );

	return ret;
}

static __always_inline long stub_syscall4(long syscall, long arg1, long arg2, long arg3,
				 long arg4)
{
	long ret;

	__asm__ volatile ("movq %5,%%r10 ; " __syscall
		: "=a" (ret)
		: "0" (syscall), "D" (arg1), "S" (arg2), "d" (arg3),
		  "g" (arg4)
		: __syscall_clobber, "r10" );

	return ret;
}

static __always_inline long stub_syscall5(long syscall, long arg1, long arg2,
					  long arg3, long arg4, long arg5)
{
	long ret;

	__asm__ volatile ("movq %5,%%r10 ; movq %6,%%r8 ; " __syscall
		: "=a" (ret)
		: "0" (syscall), "D" (arg1), "S" (arg2), "d" (arg3),
		  "g" (arg4), "g" (arg5)
		: __syscall_clobber, "r10", "r8" );

	return ret;
}

static __always_inline long stub_syscall6(long syscall, long arg1, long arg2,
					  long arg3, long arg4, long arg5,
					  long arg6)
{
	long ret;

	__asm__ volatile ("movq %5,%%r10 ; movq %6,%%r8 ; movq %7,%%r9 ; "
		__syscall
		: "=a" (ret)
		: "0" (syscall), "D" (arg1), "S" (arg2), "d" (arg3),
		  "g" (arg4), "g" (arg5), "g" (arg6)
		: __syscall_clobber, "r10", "r8", "r9");

	return ret;
}

static __always_inline void trap_myself(void)
{
	__asm("int3");
}

static __always_inline void *get_stub_data(void)
{
	unsigned long ret;

	asm volatile (
		"movq %%rsp,%0 ;"
		"andq %1,%0"
		: "=a" (ret)
		: "g" (~(STUB_DATA_PAGES * UM_KERN_PAGE_SIZE - 1)));

	return (void *)ret;
}
#endif
