#ifndef _ASM_UAPI_LKL_BITSPERLONG_H
#define _ASM_UAPI_LKL_BITSPERLONG_H

#include <asm/config.h>

#if defined(LKL_CONFIG_64BIT)
#define __BITS_PER_LONG 64
#else
#define __BITS_PER_LONG 32
#endif

#define __ARCH_WANT_STAT64

#endif /* _ASM_UAPI_LKL_BITSPERLONG_H */
