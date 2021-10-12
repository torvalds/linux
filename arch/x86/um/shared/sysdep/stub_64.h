/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_STUB_H
#define __SYSDEP_STUB_H

#include <sysdep/ptrace_user.h>
#include <generated/asm-offsets.h>

#define STUB_MMAP_NR __NR_mmap
#define MMAP_OFFSET(o) (o)

#define __syscall_clobber "r11","rcx","memory"
#define __syscall "syscall"

static inline long stub_syscall0(long syscall)
{
	long ret;

	__asm__ volatile (__syscall
		: "=a" (ret)
		: "0" (syscall) : __syscall_clobber );

	return ret;
}

static inline long stub_syscall2(long syscall, long arg1, long arg2)
{
	long ret;

	__asm__ volatile (__syscall
		: "=a" (ret)
		: "0" (syscall), "D" (arg1), "S" (arg2) : __syscall_clobber );

	return ret;
}

static inline long stub_syscall3(long syscall, long arg1, long arg2, long arg3)
{
	long ret;

	__asm__ volatile (__syscall
		: "=a" (ret)
		: "0" (syscall), "D" (arg1), "S" (arg2), "d" (arg3)
		: __syscall_clobber );

	return ret;
}

static inline long stub_syscall4(long syscall, long arg1, long arg2, long arg3,
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

static inline long stub_syscall5(long syscall, long arg1, long arg2, long arg3,
				 long arg4, long arg5)
{
	long ret;

	__asm__ volatile ("movq %5,%%r10 ; movq %6,%%r8 ; " __syscall
		: "=a" (ret)
		: "0" (syscall), "D" (arg1), "S" (arg2), "d" (arg3),
		  "g" (arg4), "g" (arg5)
		: __syscall_clobber, "r10", "r8" );

	return ret;
}

static inline void trap_myself(void)
{
	__asm("int3");
}

static inline void remap_stack_and_trap(void)
{
	__asm__ volatile (
		"movq %0,%%rax ;"
		"movq %%rsp,%%rdi ;"
		"andq %1,%%rdi ;"
		"movq %2,%%r10 ;"
		"movq %%rdi,%%r8 ; addq %3,%%r8 ; movq (%%r8),%%r8 ;"
		"movq %%rdi,%%r9 ; addq %4,%%r9 ; movq (%%r9),%%r9 ;"
		__syscall ";"
		"movq %%rsp,%%rdi ; andq %1,%%rdi ;"
		"addq %5,%%rdi ; movq %%rax, (%%rdi) ;"
		"int3"
		: :
		"g" (STUB_MMAP_NR),
		"g" (~(UM_KERN_PAGE_SIZE - 1)),
		"g" (MAP_FIXED | MAP_SHARED),
		"g" (UML_STUB_FIELD_FD),
		"g" (UML_STUB_FIELD_OFFSET),
		"g" (UML_STUB_FIELD_CHILD_ERR),
		"S" (UM_KERN_PAGE_SIZE),
		"d" (PROT_READ | PROT_WRITE)
		:
		__syscall_clobber, "r10", "r8", "r9");
}

static __always_inline void *get_stub_page(void)
{
	unsigned long ret;

	asm volatile (
		"movq %%rsp,%0 ;"
		"andq %1,%0"
		: "=a" (ret)
		: "g" (~(UM_KERN_PAGE_SIZE - 1)));

	return (void *)ret;
}
#endif
