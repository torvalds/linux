#ifndef __ASM_SH_SYSCALL_H
#define __ASM_SH_SYSCALL_H

#ifdef CONFIG_SUPERH32
# include "syscall_32.h"
#else
# include "syscall_64.h"
#endif

#endif /* __ASM_SH_SYSCALL_H */
