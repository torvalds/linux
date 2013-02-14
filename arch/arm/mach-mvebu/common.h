/*
 * Core functions for Marvell System On Chip
 *
 * Copyright (C) 2012 Marvell
 *
 * Lior Amsalem <alior@marvell.com>
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ARCH_MVEBU_COMMON_H
#define __ARCH_MVEBU_COMMON_H

void mvebu_restart(char mode, const char *cmd);

void armada_370_xp_init_irq(void);
void armada_370_xp_handle_irq(struct pt_regs *regs);

void armada_xp_cpu_die(unsigned int cpu);
int armada_370_xp_coherency_init(void);
int armada_370_xp_pmsu_init(void);
void armada_xp_secondary_startup(void);
extern struct smp_operations armada_xp_smp_ops;
#endif
