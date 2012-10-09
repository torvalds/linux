/*
 *  Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_IRQS_H__
#define __ASM_ARCH_MXC_IRQS_H__

extern int imx_irq_set_priority(unsigned char irq, unsigned char prio);

/* all normal IRQs can be FIQs */
#define FIQ_START	0
/* switch between IRQ and FIQ */
extern int mxc_set_irq_fiq(unsigned int irq, unsigned int type);

#endif /* __ASM_ARCH_MXC_IRQS_H__ */
