#ifndef __ASM_SH_SYSCALL_H
#define __ASM_SH_SYSCALL_H

extern const unsigned long sys_call_table[];

#ifdef CONFIG_SUPERH32
# include "syscall_32.h"
#else
# include "syscall_64.h"
#endif

#endif /* __ASM_SH_SYSCALL_H */
