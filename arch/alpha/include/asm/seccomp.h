/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ALPHA_SECCOMP_H
#define _ASM_ALPHA_SECCOMP_H

#include <asm/unistd.h>
#include <asm-generic/seccomp.h>
#include <uapi/linux/audit.h>

#define SECCOMP_ARCH_NATIVE            AUDIT_ARCH_ALPHA
#define SECCOMP_ARCH_NATIVE_NR         NR_syscalls
#define SECCOMP_ARCH_NATIVE_NAME       "alpha"

#endif /* _ASM_ALPHA_SECCOMP_H */
