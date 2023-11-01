/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68K_IRQ_H_
#define _M68K_IRQ_H_

#include <linux/atomic.h>
#include <linux/linkage.h>

/*
 * This should be the same as the max(NUM_X_SOURCES) for all the
 * different m68k hosts compiled into the kernel.
 * Currently the Atari has 72 and the Amiga 24, but if both are
 * supported in the kernel it is better to make room for 72.
 * With EtherNAT add-on card on Atari, the highest interrupt
 * number is 140 so NR_IRQS needs to be 141.
 */
#if defined(CONFIG_COLDFIRE)
#define NR_IRQS 256
#elif defined(CONFIG_VME) || defined(CONFIG_SUN3) || \
      defined(CONFIG_SUN3X) || defined(CONFIG_VIRT)
#define NR_IRQS 200
#elif defined(CONFIG_ATARI)
#define NR_IRQS 141
#elif defined(CONFIG_MAC)
#define NR_IRQS 72
#elif defined(CONFIG_Q40)
#define NR_IRQS	43
#elif defined(CONFIG_AMIGA) || !defined(CONFIG_MMU)
#define NR_IRQS	32
#elif defined(CONFIG_APOLLO)
#define NR_IRQS	24
#elif defined(CONFIG_HP300)
#define NR_IRQS	8
#else
#define NR_IRQS	0
#endif

#if defined(CONFIG_M68020) || defined(CONFIG_M68030) || \
    defined(CONFIG_M68040) || defined(CONFIG_M68060)

/*
 * Interrupt source definitions
 * General interrupt sources are the level 1-7.
 * Adding an interrupt service routine for one of these sources
 * results in the addition of that routine to a chain of routines.
 * Each one is called in succession.  Each individual interrupt
 * service routine should determine if the device associated with
 * that routine requires service.
 */

#define IRQ_SPURIOUS	0

#define IRQ_AUTO_1	1	/* level 1 interrupt */
#define IRQ_AUTO_2	2	/* level 2 interrupt */
#define IRQ_AUTO_3	3	/* level 3 interrupt */
#define IRQ_AUTO_4	4	/* level 4 interrupt */
#define IRQ_AUTO_5	5	/* level 5 interrupt */
#define IRQ_AUTO_6	6	/* level 6 interrupt */
#define IRQ_AUTO_7	7	/* level 7 interrupt (non-maskable) */

#define IRQ_USER	8

struct irq_data;
struct irq_chip;
struct irq_desc;
struct pt_regs;

extern unsigned int m68k_irq_startup(struct irq_data *data);
extern unsigned int m68k_irq_startup_irq(unsigned int irq);
extern void m68k_irq_shutdown(struct irq_data *data);
extern void m68k_setup_auto_interrupt(void (*handler)(unsigned int,
						      struct pt_regs *));
extern void m68k_setup_user_interrupt(unsigned int vec, unsigned int cnt);
extern void m68k_setup_irq_controller(struct irq_chip *,
				      void (*handle)(struct irq_desc *desc),
				      unsigned int irq, unsigned int cnt);

extern unsigned int irq_canonicalize(unsigned int irq);

#else
#define irq_canonicalize(irq)  (irq)
#endif /* !(CONFIG_M68020 || CONFIG_M68030 || CONFIG_M68040 || CONFIG_M68060) */

asmlinkage void do_IRQ(int irq, struct pt_regs *regs);
extern atomic_t irq_err_count;

#endif /* _M68K_IRQ_H_ */
