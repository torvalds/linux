/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/******** no-legacy-syscalls-ABI *******/

/*
 * Non-typical guard macro to enable inclusion twice in ARCH sys.c
 * That is how the Generic syscall wrapper generator works
 */
#if !defined(_UAPI_ASM_ARC_UNISTD_H) || defined(__SYSCALL)
#define _UAPI_ASM_ARC_UNISTD_H

#define __ARCH_WANT_RENAMEAT
#define __ARCH_WANT_SYS_EXECVE
#define __ARCH_WANT_SYS_CLONE
#define __ARCH_WANT_SYS_VFORK
#define __ARCH_WANT_SYS_FORK

#define sys_mmap2 sys_mmap_pgoff

#include <asm-generic/unistd.h>

#define NR_syscalls	__NR_syscalls

/* ARC specific syscall */
#define __NR_cacheflush		(__NR_arch_specific_syscall + 0)
#define __NR_arc_settls		(__NR_arch_specific_syscall + 1)
#define __NR_arc_gettls		(__NR_arch_specific_syscall + 2)

__SYSCALL(__NR_cacheflush, sys_cacheflush)
__SYSCALL(__NR_arc_settls, sys_arc_settls)
__SYSCALL(__NR_arc_gettls, sys_arc_gettls)


/* Generic syscall (fs/filesystems.c - lost in asm-generic/unistd.h */
#define __NR_sysfs		(__NR_arch_specific_syscall + 3)
__SYSCALL(__NR_sysfs, sys_sysfs)

#undef __SYSCALL

#endif
