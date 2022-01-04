/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  arch/arm/include/asm/unistd.h
 *
 *  Copyright (C) 2001-2005 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Please forward _all_ changes to this file to rmk@arm.linux.org.uk,
 * no matter what the change is.  Thanks!
 */
#ifndef _UAPI__ASM_ARM_UNISTD_H
#define _UAPI__ASM_ARM_UNISTD_H

#define __NR_OABI_SYSCALL_BASE	0x900000

#if defined(__thumb__) || defined(__ARM_EABI__)
#define __NR_SYSCALL_BASE	0
#include <asm/unistd-eabi.h>
#else
#define __NR_SYSCALL_BASE	__NR_OABI_SYSCALL_BASE
#include <asm/unistd-oabi.h>
#endif

#define __NR_sync_file_range2		__NR_arm_sync_file_range

/*
 * The following SWIs are ARM private.
 */
#define __ARM_NR_BASE			(__NR_SYSCALL_BASE+0x0f0000)
#define __ARM_NR_breakpoint		(__ARM_NR_BASE+1)
#define __ARM_NR_cacheflush		(__ARM_NR_BASE+2)
#define __ARM_NR_usr26			(__ARM_NR_BASE+3)
#define __ARM_NR_usr32			(__ARM_NR_BASE+4)
#define __ARM_NR_set_tls		(__ARM_NR_BASE+5)
#define __ARM_NR_get_tls		(__ARM_NR_BASE+6)

#endif /* _UAPI__ASM_ARM_UNISTD_H */
