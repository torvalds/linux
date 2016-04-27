#ifndef _ASM_LKL_SIGINFO_H
#define _ASM_LKL_SIGINFO_H

#ifdef CONFIG_64BIT
#define __ARCH_SI_PREAMBLE_SIZE	(4 * sizeof(int))
#endif

#include <asm-generic/siginfo.h>

#endif /* _ASM_LKL_SIGINFO_H */
