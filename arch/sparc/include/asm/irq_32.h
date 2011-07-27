/* irq.h: IRQ registers on the Sparc.
 *
 * Copyright (C) 1995, 2007 David S. Miller (davem@davemloft.net)
 */

#ifndef _SPARC_IRQ_H
#define _SPARC_IRQ_H

/* Allocated number of logical irq numbers.
 * sun4d boxes (ss2000e) should be OK with ~32.
 * Be on the safe side and make room for 64
 */
#define NR_IRQS    64

#include <linux/interrupt.h>

#define irq_canonicalize(irq)	(irq)

extern void __init init_IRQ(void);

#define NO_IRQ		0xffffffff

#endif
