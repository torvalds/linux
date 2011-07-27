#ifndef __MACH_PXA25x_H
#define __MACH_PXA25x_H

#include <mach/hardware.h>
#include <mach/pxa2xx-regs.h>
#include <mach/mfp-pxa25x.h>
#include <mach/irqs.h>

extern void __init pxa25x_map_io(void);
extern void __init pxa25x_init_irq(void);
#ifdef CONFIG_CPU_PXA26x
extern void __init pxa26x_init_irq(void);
#endif

#define pxa25x_handle_irq	icip_handle_irq

#endif /* __MACH_PXA25x_H */
