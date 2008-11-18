/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Changed by HuTao Apr18, 2003
 *
 * Copyright was missing when I got the code so took from MIPS arch ...MaTed---
 * Copyright (C) 1994 by Waldorf GMBH, written by Ralf Baechle
 * Copyright (C) 1995, 96, 97, 98, 99, 2000, 2001 by Ralf Baechle
 *
 * Adapted for BlackFin (ADI) by Ted Ma <mated@sympatico.ca>
 * Copyright (c) 2002 Arcturus Networks Inc. (www.arcturusnetworks.com)
 * Copyright (c) 2002 Lineo, Inc. <mattw@lineo.com>
 */

#ifndef _BFIN_IRQ_H_
#define _BFIN_IRQ_H_

/* SYS_IRQS and NR_IRQS are defined in <mach-bf5xx/irq.h>*/
#include <mach/irq.h>
#include <asm/pda.h>
#include <asm/processor.h>

static __inline__ int irq_canonicalize(int irq)
{
	return irq;
}

/*
 * Interrupt configuring macros.
 */
#define local_irq_disable() \
	do { \
		int __tmp_dummy; \
		__asm__ __volatile__( \
			"cli %0;" \
			: "=d" (__tmp_dummy) \
		); \
	} while (0)

#if ANOMALY_05000244 && defined(CONFIG_BFIN_ICACHE)
# define NOP_PAD_ANOMALY_05000244 "nop; nop;"
#else
# define NOP_PAD_ANOMALY_05000244
#endif

#ifdef CONFIG_SMP
/* Forward decl needed due to cdef inter dependencies */
static inline uint32_t __pure bfin_dspid(void);
# define blackfin_core_id() (bfin_dspid() & 0xff)
# define bfin_irq_flags cpu_pda[blackfin_core_id()].imask
#else
extern unsigned long bfin_irq_flags;
#endif

#define local_irq_enable() \
	__asm__ __volatile__( \
		"sti %0;" \
		: \
		: "d" (bfin_irq_flags) \
	)

#define idle_with_irq_disabled() \
	__asm__ __volatile__( \
		NOP_PAD_ANOMALY_05000244 \
		".align 8;" \
		"sti %0;" \
		"idle;" \
		: \
		: "d" (bfin_irq_flags) \
	)

#ifdef CONFIG_DEBUG_HWERR
# define __save_and_cli(x) \
	__asm__ __volatile__( \
		"cli %0;" \
		"sti %1;" \
		: "=&d" (x) \
		: "d" (0x3F) \
	)
#else
# define __save_and_cli(x) \
	__asm__ __volatile__( \
		"cli %0;" \
		: "=&d" (x) \
	)
#endif

#define local_save_flags(x) \
	__asm__ __volatile__( \
		"cli %0;" \
		"sti %0;" \
		: "=d" (x) \
	)

#ifdef CONFIG_DEBUG_HWERR
#define irqs_enabled_from_flags(x) (((x) & ~0x3f) != 0)
#else
#define irqs_enabled_from_flags(x) ((x) != 0x1f)
#endif

#define local_irq_restore(x) \
	do { \
		if (irqs_enabled_from_flags(x)) \
			local_irq_enable(); \
	} while (0)

/* For spinlocks etc */
#define local_irq_save(x) __save_and_cli(x)

#define irqs_disabled()				\
({						\
	unsigned long flags;			\
	local_save_flags(flags);		\
	!irqs_enabled_from_flags(flags);	\
})

#endif				/* _BFIN_IRQ_H_ */
