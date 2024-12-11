/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_STUB_H
#define __SYSDEP_STUB_H

#include <stddef.h>
#include <asm/ptrace.h>
#include <generated/asm-offsets.h>

#define STUB_MMAP_NR __NR_mmap2
#define MMAP_OFFSET(o) ((o) >> UM_KERN_PAGE_SHIFT)

static __always_inline long stub_syscall0(long syscall)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall)
			: "memory");

	return ret;
}

static __always_inline long stub_syscall1(long syscall, long arg1)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1)
			: "memory");

	return ret;
}

static __always_inline long stub_syscall2(long syscall, long arg1, long arg2)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1),
			"c" (arg2)
			: "memory");

	return ret;
}

static __always_inline long stub_syscall3(long syscall, long arg1, long arg2,
					  long arg3)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1),
			"c" (arg2), "d" (arg3)
			: "memory");

	return ret;
}

static __always_inline long stub_syscall4(long syscall, long arg1, long arg2,
					  long arg3, long arg4)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1),
			"c" (arg2), "d" (arg3), "S" (arg4)
			: "memory");

	return ret;
}

static __always_inline long stub_syscall5(long syscall, long arg1, long arg2,
					  long arg3, long arg4, long arg5)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1),
			"c" (arg2), "d" (arg3), "S" (arg4), "D" (arg5)
			: "memory");

	return ret;
}

static __always_inline long stub_syscall6(long syscall, long arg1, long arg2,
					  long arg3, long arg4, long arg5,
					  long arg6)
{
	struct syscall_args {
		int ebx, ebp;
	} args = { arg1, arg6 };
	long ret;

	__asm__ volatile ("pushl %%ebp;"
			"movl 0x4(%%ebx),%%ebp;"
			"movl (%%ebx),%%ebx;"
			"int $0x80;"
			"popl %%ebp"
			: "=a" (ret)
			: "0" (syscall), "b" (&args),
			"c" (arg2), "d" (arg3), "S" (arg4), "D" (arg5)
			: "memory");

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
		"call _here_%=;"
		"_here_%=:"
		"popl %0;"
		"andl %1, %0 ;"
		"addl %2, %0 ;"
		: "=a" (ret)
		: "g" (~(UM_KERN_PAGE_SIZE - 1)),
		  "g" (UM_KERN_PAGE_SIZE));

	return (void *)ret;
}

#define stub_start(fn)							\
	asm volatile (							\
		"subl %0,%%esp ;"					\
		"movl %1, %%eax ; "					\
		"call *%%eax ;"						\
		:: "i" ((1 + STUB_DATA_PAGES) * UM_KERN_PAGE_SIZE),	\
		   "i" (&fn))
#endif
