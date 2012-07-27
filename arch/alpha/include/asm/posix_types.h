#ifndef _ALPHA_POSIX_TYPES_H
#define _ALPHA_POSIX_TYPES_H

/*
 * This file is generally used by user-level software, so you need to
 * be a little careful about namespace pollution etc.  Also, we cannot
 * assume GCC is being used.
 */

typedef unsigned int	__kernel_ino_t;
#define __kernel_ino_t __kernel_ino_t

typedef unsigned long	__kernel_sigset_t;	/* at least 32 bits */

#include <asm-generic/posix_types.h>

#endif /* _ALPHA_POSIX_TYPES_H */
