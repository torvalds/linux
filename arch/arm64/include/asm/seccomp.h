/*
 * arch/arm64/include/asm/seccomp.h
 *
 * Copyright (C) 2014 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi <at> linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _ASM_SECCOMP_H
#define _ASM_SECCOMP_H

#include <asm/unistd.h>

#ifdef CONFIG_COMPAT
#define __NR_seccomp_read_32		__NR_compat_read
#define __NR_seccomp_write_32		__NR_compat_write
#define __NR_seccomp_exit_32		__NR_compat_exit
#define __NR_seccomp_sigreturn_32	__NR_compat_rt_sigreturn
#endif /* CONFIG_COMPAT */

#include <asm-generic/seccomp.h>

#endif /* _ASM_SECCOMP_H */
