/* SPDX-License-Identifier: GPL-2.0 */
/* hardirq.h: 64-bit Sparc hard IRQ support.
 *
 * Copyright (C) 1997, 1998, 2005 David S. Miller (davem@davemloft.net)
 */

#ifndef __SPARC64_HARDIRQ_H
#define __SPARC64_HARDIRQ_H

#include <asm/cpudata.h>

#define __ARCH_IRQ_STAT
#define local_softirq_pending() \
	(*this_cpu_ptr(&__cpu_data.__softirq_pending))

void ack_bad_irq(unsigned int irq);

#endif /* !(__SPARC64_HARDIRQ_H) */
