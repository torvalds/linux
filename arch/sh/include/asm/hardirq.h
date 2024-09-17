/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_HARDIRQ_H
#define __ASM_SH_HARDIRQ_H

extern void ack_bad_irq(unsigned int irq);
#define ack_bad_irq ack_bad_irq
#define ARCH_WANTS_NMI_IRQSTAT

#include <asm-generic/hardirq.h>

#endif /* __ASM_SH_HARDIRQ_H */
