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

#include <linux/irqflags.h>

/* SYS_IRQS and NR_IRQS are defined in <mach-bf5xx/irq.h> */
#include <mach/irq.h>

/* Xenomai IPIPE helpers */
#define local_irq_restore_hw(x) local_irq_restore(x)
#define local_irq_save_hw(x)    local_irq_save(x)
#define local_irq_enable_hw(x)  local_irq_enable(x)
#define local_irq_disable_hw(x) local_irq_disable(x)
#define irqs_disabled_hw(x)     irqs_disabled(x)

#if ANOMALY_05000244 && defined(CONFIG_BFIN_ICACHE)
# define NOP_PAD_ANOMALY_05000244 "nop; nop;"
#else
# define NOP_PAD_ANOMALY_05000244
#endif

#define idle_with_irq_disabled() \
	__asm__ __volatile__( \
		NOP_PAD_ANOMALY_05000244 \
		".align 8;" \
		"sti %0;" \
		"idle;" \
		: \
		: "d" (bfin_irq_flags) \
	)

static inline int irq_canonicalize(int irq)
{
	return irq;
}

#endif				/* _BFIN_IRQ_H_ */
