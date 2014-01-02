/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2012 ARM Limited
 */


#ifndef __SPC_H_
#define __SPC_H_

int __init ve_spc_init(void __iomem *base, u32 a15_clusid, int irq);
void ve_spc_global_wakeup_irq(bool set);
void ve_spc_cpu_wakeup_irq(u32 cluster, u32 cpu, bool set);
void ve_spc_set_resume_addr(u32 cluster, u32 cpu, u32 addr);
void ve_spc_powerdown(u32 cluster, bool enable);

#endif
