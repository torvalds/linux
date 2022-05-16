/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_HW_IRQ_H
#define _ASM_IA64_HW_IRQ_H

/*
 * Copyright (C) 2001-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/profile.h>

#include <asm/ptrace.h>
#include <asm/smp.h>

typedef u8 ia64_vector;

/*
 * 0 special
 *
 * 1,3-14 are reserved from firmware
 *
 * 16-255 (vectored external interrupts) are available
 *
 * 15 spurious interrupt (see IVR)
 *
 * 16 lowest priority, 255 highest priority
 *
 * 15 classes of 16 interrupts each.
 */
#define IA64_MIN_VECTORED_IRQ		 16
#define IA64_MAX_VECTORED_IRQ		255
#define IA64_NUM_VECTORS		256

#define AUTO_ASSIGN			-1

#define IA64_SPURIOUS_INT_VECTOR	0x0f

/*
 * Vectors 0x10-0x1f are used for low priority interrupts, e.g. CMCI.
 */
#define IA64_CPEP_VECTOR		0x1c	/* corrected platform error polling vector */
#define IA64_CMCP_VECTOR		0x1d	/* corrected machine-check polling vector */
#define IA64_CPE_VECTOR			0x1e	/* corrected platform error interrupt vector */
#define IA64_CMC_VECTOR			0x1f	/* corrected machine-check interrupt vector */
/*
 * Vectors 0x20-0x2f are reserved for legacy ISA IRQs.
 * Use vectors 0x30-0xe7 as the default device vector range for ia64.
 * Platforms may choose to reduce this range in platform_irq_setup, but the
 * platform range must fall within
 *	[IA64_DEF_FIRST_DEVICE_VECTOR..IA64_DEF_LAST_DEVICE_VECTOR]
 */
extern int ia64_first_device_vector;
extern int ia64_last_device_vector;

#ifdef CONFIG_SMP
/* Reserve the lower priority vector than device vectors for "move IRQ" IPI */
#define IA64_IRQ_MOVE_VECTOR		0x30	/* "move IRQ" IPI */
#define IA64_DEF_FIRST_DEVICE_VECTOR	0x31
#else
#define IA64_DEF_FIRST_DEVICE_VECTOR	0x30
#endif
#define IA64_DEF_LAST_DEVICE_VECTOR	0xe7
#define IA64_FIRST_DEVICE_VECTOR	ia64_first_device_vector
#define IA64_LAST_DEVICE_VECTOR		ia64_last_device_vector
#define IA64_MAX_DEVICE_VECTORS		(IA64_DEF_LAST_DEVICE_VECTOR - IA64_DEF_FIRST_DEVICE_VECTOR + 1)
#define IA64_NUM_DEVICE_VECTORS		(IA64_LAST_DEVICE_VECTOR - IA64_FIRST_DEVICE_VECTOR + 1)

#define IA64_MCA_RENDEZ_VECTOR		0xe8	/* MCA rendez interrupt */
#define IA64_PERFMON_VECTOR		0xee	/* performance monitor interrupt vector */
#define IA64_TIMER_VECTOR		0xef	/* use highest-prio group 15 interrupt for timer */
#define	IA64_MCA_WAKEUP_VECTOR		0xf0	/* MCA wakeup (must be >MCA_RENDEZ_VECTOR) */
#define IA64_IPI_LOCAL_TLB_FLUSH	0xfc	/* SMP flush local TLB */
#define IA64_IPI_RESCHEDULE		0xfd	/* SMP reschedule */
#define IA64_IPI_VECTOR			0xfe	/* inter-processor interrupt vector */

/* Used for encoding redirected irqs */

#define IA64_IRQ_REDIRECTED		(1 << 31)

/* IA64 inter-cpu interrupt related definitions */

#define IA64_IPI_DEFAULT_BASE_ADDR	0xfee00000

/* Delivery modes for inter-cpu interrupts */
enum {
        IA64_IPI_DM_INT =       0x0,    /* pend an external interrupt */
        IA64_IPI_DM_PMI =       0x2,    /* pend a PMI */
        IA64_IPI_DM_NMI =       0x4,    /* pend an NMI (vector 2) */
        IA64_IPI_DM_INIT =      0x5,    /* pend an INIT interrupt */
        IA64_IPI_DM_EXTINT =    0x7,    /* pend an 8259-compatible interrupt. */
};

extern __u8 isa_irq_to_vector_map[16];
#define isa_irq_to_vector(x)	isa_irq_to_vector_map[(x)]

struct irq_cfg {
	ia64_vector vector;
	cpumask_t domain;
	cpumask_t old_domain;
	unsigned move_cleanup_count;
	u8 move_in_progress : 1;
};
extern spinlock_t vector_lock;
extern struct irq_cfg irq_cfg[NR_IRQS];
#define irq_to_domain(x)	irq_cfg[(x)].domain
DECLARE_PER_CPU(int[IA64_NUM_VECTORS], vector_irq);

extern struct irq_chip irq_type_ia64_lsapic;	/* CPU-internal interrupt controller */

#define ia64_register_ipi	ia64_native_register_ipi
#define assign_irq_vector	ia64_native_assign_irq_vector
#define free_irq_vector		ia64_native_free_irq_vector
#define ia64_resend_irq		ia64_native_resend_irq

extern void ia64_native_register_ipi(void);
extern int bind_irq_vector(int irq, int vector, cpumask_t domain);
extern int ia64_native_assign_irq_vector (int irq);	/* allocate a free vector */
extern void ia64_native_free_irq_vector (int vector);
extern int reserve_irq_vector (int vector);
extern void __setup_vector_irq(int cpu);
extern void ia64_send_ipi (int cpu, int vector, int delivery_mode, int redirect);
extern void destroy_and_reserve_irq (unsigned int irq);

#ifdef CONFIG_SMP
extern int irq_prepare_move(int irq, int cpu);
extern void irq_complete_move(unsigned int irq);
#else
static inline int irq_prepare_move(int irq, int cpu) { return 0; }
static inline void irq_complete_move(unsigned int irq) {}
#endif

static inline void ia64_native_resend_irq(unsigned int vector)
{
	ia64_send_ipi(smp_processor_id(), vector, IA64_IPI_DM_INT, 0);
}

/*
 * Next follows the irq descriptor interface.  On IA-64, each CPU supports 256 interrupt
 * vectors.  On smaller systems, there is a one-to-one correspondence between interrupt
 * vectors and the Linux irq numbers.  However, larger systems may have multiple interrupt
 * domains meaning that the translation from vector number to irq number depends on the
 * interrupt domain that a CPU belongs to.  This API abstracts such platform-dependent
 * differences and provides a uniform means to translate between vector and irq numbers
 * and to obtain the irq descriptor for a given irq number.
 */

/* Extract the IA-64 vector that corresponds to IRQ.  */
static inline ia64_vector
irq_to_vector (int irq)
{
	return irq_cfg[irq].vector;
}

/*
 * Convert the local IA-64 vector to the corresponding irq number.  This translation is
 * done in the context of the interrupt domain that the currently executing CPU belongs
 * to.
 */
static inline unsigned int
local_vector_to_irq (ia64_vector vec)
{
	return __this_cpu_read(vector_irq[vec]);
}

#endif /* _ASM_IA64_HW_IRQ_H */
