#ifndef __ASM_MICROBLAZE_SYSCALLS_H

asmlinkage long sys_clone(int flags, unsigned long stack, struct pt_regs *regs);
#define sys_clone sys_clone

#include <asm-generic/syscalls.h>

#endif /* __ASM_MICROBLAZE_SYSCALLS_H */
