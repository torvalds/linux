/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SECCOMP_H

#include <linux/unistd.h>

#define __NR_seccomp_read __NR_read
#define __NR_seccomp_write __NR_write
#define __NR_seccomp_exit __NR_exit
#define __NR_seccomp_sigreturn __NR_rt_sigreturn

#ifdef CONFIG_CPU_LITTLE_ENDIAN
#define __SECCOMP_ARCH_LE		__AUDIT_ARCH_LE
#else
#define __SECCOMP_ARCH_LE		0
#endif

#define SECCOMP_ARCH_NATIVE		(AUDIT_ARCH_SH | __SECCOMP_ARCH_LE)
#define SECCOMP_ARCH_NATIVE_NR		NR_syscalls
#define SECCOMP_ARCH_NATIVE_NAME	"sh"

#endif /* __ASM_SECCOMP_H */
