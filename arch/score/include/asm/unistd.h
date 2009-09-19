#if !defined(_ASM_SCORE_UNISTD_H) || defined(__SYSCALL)
#define _ASM_SCORE_UNISTD_H

#define __ARCH_HAVE_MMU

#define __ARCH_WANT_SYSCALL_NO_AT
#define __ARCH_WANT_SYSCALL_NO_FLAGS
#define __ARCH_WANT_SYSCALL_OFF_T
#define __ARCH_WANT_SYSCALL_DEPRECATED

#include <asm-generic/unistd.h>

#endif /* _ASM_SCORE_UNISTD_H */
