/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <arch/irq.h>

static inline int irq_canonicalize(int irq)
{  
  return irq; 
}

#endif  /* _ASM_IRQ_H */


