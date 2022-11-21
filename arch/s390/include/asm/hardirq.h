/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  S390 version
 *    Copyright IBM Corp. 1999, 2000
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *  Derived from "include/asm-i386/hardirq.h"
 */

#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <asm/lowcore.h>

#define local_softirq_pending() (S390_lowcore.softirq_pending)
#define set_softirq_pending(x) (S390_lowcore.softirq_pending = (x))
#define or_softirq_pending(x)  (S390_lowcore.softirq_pending |= (x))

#define __ARCH_IRQ_STAT
#define __ARCH_IRQ_EXIT_IRQS_DISABLED

static inline void ack_bad_irq(unsigned int irq)
{
	printk(KERN_CRIT "unexpected IRQ trap at vector %02x\n", irq);
}

#endif /* __ASM_HARDIRQ_H */
