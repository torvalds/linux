/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_SECCOMP_H
#define _ASM_POWERPC_SECCOMP_H

#include <linux/unistd.h>

#define __NR_seccomp_sigreturn_32 __NR_sigreturn

#include <asm-generic/seccomp.h>

#ifdef __LITTLE_ENDIAN__
#define __SECCOMP_ARCH_LE		__AUDIT_ARCH_LE
#define __SECCOMP_ARCH_LE_NAME		"le"
#else
#define __SECCOMP_ARCH_LE		0
#define __SECCOMP_ARCH_LE_NAME
#endif

#ifdef CONFIG_PPC64
# define SECCOMP_ARCH_NATIVE		(AUDIT_ARCH_PPC64 | __SECCOMP_ARCH_LE)
# define SECCOMP_ARCH_NATIVE_NR		NR_syscalls
# define SECCOMP_ARCH_NATIVE_NAME	"ppc64" __SECCOMP_ARCH_LE_NAME
# ifdef CONFIG_COMPAT
#  define SECCOMP_ARCH_COMPAT		(AUDIT_ARCH_PPC | __SECCOMP_ARCH_LE)
#  define SECCOMP_ARCH_COMPAT_NR	NR_syscalls
#  define SECCOMP_ARCH_COMPAT_NAME	"ppc" __SECCOMP_ARCH_LE_NAME
# endif
#else /* !CONFIG_PPC64 */
# define SECCOMP_ARCH_NATIVE		(AUDIT_ARCH_PPC | __SECCOMP_ARCH_LE)
# define SECCOMP_ARCH_NATIVE_NR		NR_syscalls
# define SECCOMP_ARCH_NATIVE_NAME	"ppc" __SECCOMP_ARCH_LE_NAME
#endif

#endif	/* _ASM_POWERPC_SECCOMP_H */
