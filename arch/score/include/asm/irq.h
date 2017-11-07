/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SCORE_IRQ_H
#define _ASM_SCORE_IRQ_H

#define EXCEPTION_VECTOR_BASE_ADDR	0xa0000000
#define VECTOR_ADDRESS_OFFSET_MODE4	0
#define VECTOR_ADDRESS_OFFSET_MODE16	1

#define DEBUG_VECTOR_SIZE		(0x4)
#define DEBUG_VECTOR_BASE_ADDR		((EXCEPTION_VECTOR_BASE_ADDR) + 0x1fc)

#define GENERAL_VECTOR_SIZE		(0x10)
#define GENERAL_VECTOR_BASE_ADDR	((EXCEPTION_VECTOR_BASE_ADDR) + 0x200)

#define NR_IRQS				64
#define IRQ_VECTOR_SIZE			(0x10)
#define IRQ_VECTOR_BASE_ADDR		((EXCEPTION_VECTOR_BASE_ADDR) + 0x210)
#define IRQ_VECTOR_END_ADDR		((EXCEPTION_VECTOR_BASE_ADDR) + 0x5f0)

#define irq_canonicalize(irq)	(irq)

#define IRQ_TIMER (7)		/* Timer IRQ number of SPCT6600 */

extern void interrupt_exception_vector(void);

#endif /* _ASM_SCORE_IRQ_H */
