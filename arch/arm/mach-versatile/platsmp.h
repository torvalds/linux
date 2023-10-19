/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2011 ARM Ltd.
 *  All Rights Reserved
 */
extern volatile int versatile_cpu_release;

extern void versatile_secondary_startup(void);
extern void versatile_secondary_init(unsigned int cpu);
extern int  versatile_boot_secondary(unsigned int cpu, struct task_struct *idle);
void versatile_immitation_cpu_die(unsigned int cpu, unsigned int actrl_mask);
