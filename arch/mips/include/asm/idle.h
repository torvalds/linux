#ifndef __ASM_IDLE_H
#define __ASM_IDLE_H

#include <linux/linkage.h>

extern void (*cpu_wait)(void);
extern asmlinkage void r4k_wait(void);
extern void r4k_wait_irqoff(void);
extern void __pastwait(void);

#endif /* __ASM_IDLE_H  */
