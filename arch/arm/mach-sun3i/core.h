#ifndef __ASM_ARCH_SOFTWINNER_H
#define __ASM_ARCH_SOFTWINNERE_H

#include <linux/amba/bus.h>

extern void __init softwinner_init(void);
extern void __init softwinner_init_irq(void);
extern void __init softwinner_map_io(void);
extern struct sys_timer softwinner_timer;

#endif
