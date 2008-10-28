/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_STUB_H
#define __SYSDEP_STUB_H

#include <sys/mman.h>
#include <asm/ptrace.h>
#include <asm/unistd.h>
#include "as-layout.h"
#include "stub-data.h"
#include "kern_constants.h"

extern void stub_segv_handler(int sig);
extern void stub_clone_handler(void);

#define STUB_SYSCALL_RET EAX
#define STUB_MMAP_NR __NR_mmap2
#define MMAP_OFFSET(o) ((o) >> UM_KERN_PAGE_SHIFT)

static inline long stub_syscall0(long syscall)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall));

	return ret;
}

static inline long stub_syscall1(long syscall, long arg1)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1));

	return ret;
}

static inline long stub_syscall2(long syscall, long arg1, long arg2)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1),
			"c" (arg2));

	return ret;
}

static inline long stub_syscall3(long syscall, long arg1, long arg2, long arg3)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1),
			"c" (arg2), "d" (arg3));

	return ret;
}

static inline long stub_syscall4(long syscall, long arg1, long arg2, long arg3,
				 long arg4)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1),
			"c" (arg2), "d" (arg3), "S" (arg4));

	return ret;
}

static inline long stub_syscall5(long syscall, long arg1, long arg2, long arg3,
				 long arg4, long arg5)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1),
			"c" (arg2), "d" (arg3), "S" (arg4), "D" (arg5));

	return ret;
}

static inline void trap_myself(void)
{
	__asm("int3");
}

static inline void remap_stack(int fd, unsigned long offset)
{
	__asm__ volatile ("movl %%eax,%%ebp ; movl %0,%%eax ; int $0x80 ;"
			  "movl %7, %%ebx ; movl %%eax, (%%ebx)"
			  : : "g" (STUB_MMAP_NR), "b" (STUB_DATA),
			    "c" (UM_KERN_PAGE_SIZE),
			    "d" (PROT_READ | PROT_WRITE),
			    "S" (MAP_FIXED | MAP_SHARED), "D" (fd),
			    "a" (offset),
			    "i" (&((struct stub_data *) STUB_DATA)->err)
			  : "memory");
}

#endif
