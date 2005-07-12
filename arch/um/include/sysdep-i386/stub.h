/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_STUB_H
#define __SYSDEP_STUB_H

#include <asm/ptrace.h>
#include <asm/unistd.h>

extern void stub_segv_handler(int sig);
extern void stub_clone_handler(void);

#define STUB_SYSCALL_RET EAX
#define STUB_MMAP_NR __NR_mmap2
#define MMAP_OFFSET(o) ((o) >> PAGE_SHIFT)

static inline long stub_syscall2(long syscall, long arg1, long arg2)
{
	long ret;

	__asm__("movl %0, %%ecx; " : : "g" (arg2) : "%ecx");
	__asm__("movl %0, %%ebx; " : : "g" (arg1) : "%ebx");
	__asm__("movl %0, %%eax; " : : "g" (syscall) : "%eax");
	__asm__("int $0x80;" : : : "%eax");
	__asm__ __volatile__("movl %%eax, %0; " : "=g" (ret) :);
	return(ret);
}

static inline long stub_syscall3(long syscall, long arg1, long arg2, long arg3)
{
	__asm__("movl %0, %%edx; " : : "g" (arg3) : "%edx");
	return(stub_syscall2(syscall, arg1, arg2));
}

static inline long stub_syscall4(long syscall, long arg1, long arg2, long arg3,
				 long arg4)
{
	__asm__("movl %0, %%esi; " : : "g" (arg4) : "%esi");
	return(stub_syscall3(syscall, arg1, arg2, arg3));
}

static inline long stub_syscall6(long syscall, long arg1, long arg2, long arg3,
				 long arg4, long arg5, long arg6)
{
	long ret;
	__asm__("movl %0, %%eax; " : : "g" (syscall) : "%eax");
	__asm__("movl %0, %%ebx; " : : "g" (arg1) : "%ebx");
	__asm__("movl %0, %%ecx; " : : "g" (arg2) : "%ecx");
	__asm__("movl %0, %%edx; " : : "g" (arg3) : "%edx");
	__asm__("movl %0, %%esi; " : : "g" (arg4) : "%esi");
	__asm__("movl %0, %%edi; " : : "g" (arg5) : "%edi");
	__asm__ __volatile__("pushl %%ebp ; movl %1, %%ebp; "
		"int $0x80; popl %%ebp ; "
		"movl %%eax, %0; " : "=g" (ret) : "g" (arg6) : "%eax");
	return(ret);
}

static inline void trap_myself(void)
{
	__asm("int3");
}

#endif
