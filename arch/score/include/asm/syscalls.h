#ifndef _ASM_SCORE_SYSCALLS_H
#define _ASM_SCORE_SYSCALLS_H

asmlinkage long sys_clone(int flags, unsigned long stack, struct pt_regs *regs);
#define sys_clone sys_clone

#include <asm-generic/syscalls.h>

#endif /* _ASM_SCORE_SYSCALLS_H */
