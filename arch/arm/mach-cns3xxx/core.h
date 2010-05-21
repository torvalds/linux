/*
 * Copyright 2000 Deep Blue Solutions Ltd
 * Copyright 2004 ARM Limited
 * Copyright 2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

#ifndef __CNS3XXX_CORE_H
#define __CNS3XXX_CORE_H

extern void __iomem *gic_cpu_base_addr;
extern struct sys_timer cns3xxx_timer;

void __init cns3xxx_map_io(void);
void __init cns3xxx_init_irq(void);
void cns3xxx_power_off(void);
void cns3xxx_pwr_power_up(unsigned int block);
void cns3xxx_pwr_power_down(unsigned int block);

#endif /* __CNS3XXX_CORE_H */
