/* irq.h: IRQ registers on the Sparc.
 *
 * Copyright (C) 1995, 2007 David S. Miller (davem@davemloft.net)
 */

#ifndef _SPARC_IRQ_H
#define _SPARC_IRQ_H

#include <linux/interrupt.h>

#define NR_IRQS    16

#define irq_canonicalize(irq)	(irq)

extern void __init init_IRQ(void);
#endif
