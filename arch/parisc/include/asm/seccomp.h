/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_SECCOMP_H
#define _ASM_SECCOMP_H

#include <asm-generic/seccomp.h>

#ifdef CONFIG_64BIT
# define SECCOMP_ARCH_NATIVE		AUDIT_ARCH_PARISC64
# define SECCOMP_ARCH_NATIVE_NR		NR_syscalls
# define SECCOMP_ARCH_NATIVE_NAME	"parisc64"
# ifdef CONFIG_COMPAT
#  define SECCOMP_ARCH_COMPAT		AUDIT_ARCH_PARISC
#  define SECCOMP_ARCH_COMPAT_NR	NR_syscalls
#  define SECCOMP_ARCH_COMPAT_NAME	"parisc"
# endif
#else /* !CONFIG_64BIT */
# define SECCOMP_ARCH_NATIVE		AUDIT_ARCH_PARISC
# define SECCOMP_ARCH_NATIVE_NR		NR_syscalls
# define SECCOMP_ARCH_NATIVE_NAME	"parisc"
#endif

#endif /* _ASM_SECCOMP_H */
