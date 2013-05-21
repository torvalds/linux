#ifndef __ASM_IDLE_H
#define __ASM_IDLE_H

#include <linux/linkage.h>

extern void (*cpu_wait)(void);
extern asmlinkage void r4k_wait(void);
extern void r4k_wait_irqoff(void);
extern void __pastwait(void);

static inline int using_rollback_handler(void)
{
	return cpu_wait == r4k_wait;
}

static inline int address_is_in_r4k_wait_irqoff(unsigned long addr)
{
	return addr >= (unsigned long)r4k_wait_irqoff &&
	       addr < (unsigned long)__pastwait;
}

#endif /* __ASM_IDLE_H  */
