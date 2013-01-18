/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARC_IRQ_H
#define __ASM_ARC_IRQ_H

/* Platform Independent IRQs */
#define TIMER0_IRQ      3
#define TIMER1_IRQ      4

#include <asm-generic/irq.h>

extern void __init arc_init_IRQ(void);
extern int __init get_hw_config_num_irq(void);

void __cpuinit arc_local_timer_setup(unsigned int cpu);

#endif
