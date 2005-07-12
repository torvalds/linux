/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_STUB_H
#define __SYSDEP_STUB_H

#include <asm/ptrace.h>
#include <asm/unistd.h>
#include <sysdep/ptrace_user.h>

extern void stub_segv_handler(int sig);
extern void stub_clone_handler(void);

#define STUB_SYSCALL_RET PT_INDEX(RAX)
#define STUB_MMAP_NR __NR_mmap
#define MMAP_OFFSET(o) (o)

static inline long stub_syscall2(long syscall, long arg1, long arg2)
{
	long ret;

	__asm__("movq %0, %%rsi; " : : "g" (arg2) : "%rsi");
	__asm__("movq %0, %%rdi; " : : "g" (arg1) : "%rdi");
	__asm__("movq %0, %%rax; " : : "g" (syscall) : "%rax");
	__asm__("syscall;" : : : "%rax", "%r11", "%rcx");
	__asm__ __volatile__("movq %%rax, %0; " : "=g" (ret) :);
	return(ret);
}

static inline long stub_syscall3(long syscall, long arg1, long arg2, long arg3)
{
	__asm__("movq %0, %%rdx; " : : "g" (arg3) : "%rdx");
	return(stub_syscall2(syscall, arg1, arg2));
}

static inline long stub_syscall4(long syscall, long arg1, long arg2, long arg3,
				 long arg4)
{
	__asm__("movq %0, %%r10; " : : "g" (arg4) : "%r10");
	return(stub_syscall3(syscall, arg1, arg2, arg3));
}

static inline long stub_syscall6(long syscall, long arg1, long arg2, long arg3,
				 long arg4, long arg5, long arg6)
{
	__asm__("movq %0, %%r9; " : : "g" (arg6) : "%r9");
	__asm__("movq %0, %%r8; " : : "g" (arg5) : "%r8");
	return(stub_syscall4(syscall, arg1, arg2, arg3, arg4));
}

static inline void trap_myself(void)
{
	__asm("int3");
}

#endif
