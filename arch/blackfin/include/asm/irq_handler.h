/*
 * Copyright 2007-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _IRQ_HANDLER_H
#define _IRQ_HANDLER_H

#include <linux/types.h>
#include <linux/linkage.h>

/* init functions only */
extern int __init init_arch_irq(void);
extern void init_exception_vectors(void);
extern void __init program_IAR(void);
#ifdef init_mach_irq
extern void __init init_mach_irq(void);
#else
# define init_mach_irq()
#endif

/* BASE LEVEL interrupt handler routines */
asmlinkage void evt_exception(void);
asmlinkage void trap(void);
asmlinkage void evt_ivhw(void);
asmlinkage void evt_timer(void);
asmlinkage void evt_nmi(void);
asmlinkage void evt_evt7(void);
asmlinkage void evt_evt8(void);
asmlinkage void evt_evt9(void);
asmlinkage void evt_evt10(void);
asmlinkage void evt_evt11(void);
asmlinkage void evt_evt12(void);
asmlinkage void evt_evt13(void);
asmlinkage void evt_evt14(void);
asmlinkage void evt_soft_int1(void);
asmlinkage void evt_system_call(void);
asmlinkage void init_exception_buff(void);
asmlinkage void trap_c(struct pt_regs *fp);
asmlinkage void ex_replaceable(void);
asmlinkage void early_trap(void);

extern void *ex_table[];
extern void return_from_exception(void);

extern int bfin_request_exception(unsigned int exception, void (*handler)(void));
extern int bfin_free_exception(unsigned int exception, void (*handler)(void));

extern asmlinkage void lower_to_irq14(void);
extern asmlinkage void bfin_return_from_exception(void);
extern asmlinkage void asm_do_IRQ(unsigned int irq, struct pt_regs *regs);
extern int bfin_internal_set_wake(unsigned int irq, unsigned int state);

struct irq_data;
extern void bfin_handle_irq(unsigned irq);
extern void bfin_ack_noop(struct irq_data *);
extern void bfin_internal_mask_irq(unsigned int irq);
extern void bfin_internal_unmask_irq(unsigned int irq);

struct irq_desc;
extern void bfin_demux_mac_status_irq(unsigned int, struct irq_desc *);
extern void bfin_demux_gpio_irq(unsigned int, struct irq_desc *);

#endif
