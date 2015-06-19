#ifndef _ASM_POWERPC_SECCOMP_H
#define _ASM_POWERPC_SECCOMP_H

#include <linux/unistd.h>

#define __NR_seccomp_sigreturn_32 __NR_sigreturn

#include <asm-generic/seccomp.h>

#endif	/* _ASM_POWERPC_SECCOMP_H */
