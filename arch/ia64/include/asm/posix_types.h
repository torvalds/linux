#ifndef _ASM_IA64_POSIX_TYPES_H
#define _ASM_IA64_POSIX_TYPES_H

typedef unsigned int	__kernel_nlink_t;
#define __kernel_nlink_t __kernel_nlink_t

typedef unsigned long	__kernel_sigset_t;	/* at least 32 bits */

#include <asm-generic/posix_types.h>

#endif /* _ASM_IA64_POSIX_TYPES_H */
