#ifndef __ASM_IRQ_H
#define __ASM_IRQ_H

#include <linux/irqchip/arm-gic-acpi.h>

#include <asm-generic/irq.h>

struct pt_regs;

extern void migrate_irqs(void);
extern void set_handle_irq(void (*handle_irq)(struct pt_regs *));

#endif
