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
#include <asm/ptrace.h>

static __inline__ int irq_canonicalize(int irq)
{
	return irq;
}

#endif				/* _BFIN_IRQ_H_ */
