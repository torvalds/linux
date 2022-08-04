/* SPDX-License-Identifier: GPL-2.0-only */
/*
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
int ve_spc_cpu_in_wfi(u32 cpu, u32 cluster);

#endif
