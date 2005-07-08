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

#define STUB_SYSCALL_RET PT_INDEX(RAX)
#define STUB_MMAP_NR __NR_mmap
#define MMAP_OFFSET(o) (o)

#endif
