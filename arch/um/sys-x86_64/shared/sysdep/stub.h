/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_STUB_H
#define __SYSDEP_STUB_H

#include <sys/mman.h>
#include <asm/unistd.h>
#include <sysdep/ptrace_user.h>
#include "as-layout.h"
#include "stub-data.h"
#include "kern_constants.h"

extern void stub_segv_handler(int sig);
extern void stub_clone_handler(void);

#define STUB_SYSCALL_RET PT_INDEX(RAX)
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

static inline void remap_stack(long fd, unsigned long offset)
{
	__asm__ volatile ("movq %4,%%r10 ; movq %5,%%r8 ; "
			  "movq %6, %%r9; " __syscall "; movq %7, %%rbx ; "
			  "movq %%rax, (%%rbx)":
			  : "a" (STUB_MMAP_NR), "D" (STUB_DATA),
			    "S" (UM_KERN_PAGE_SIZE),
			    "d" (PROT_READ | PROT_WRITE),
                            "g" (MAP_FIXED | MAP_SHARED), "g" (fd),
			    "g" (offset),
			    "i" (&((struct stub_data *) STUB_DATA)->err)
			  : __syscall_clobber, "r10", "r8", "r9" );
}

#endif
