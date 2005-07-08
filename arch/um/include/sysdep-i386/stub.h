/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_STUB_H
#define __SYSDEP_STUB_H

#include <asm/ptrace.h>
#include <asm/unistd.h>

extern void stub_segv_handler(int sig);

#define STUB_SYSCALL_RET EAX
#define STUB_MMAP_NR __NR_mmap2
#define MMAP_OFFSET(o) ((o) >> PAGE_SHIFT)

#endif
