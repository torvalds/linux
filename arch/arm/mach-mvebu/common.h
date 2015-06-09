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

#include <linux/reboot.h>

void mvebu_restart(enum reboot_mode mode, const char *cmd);
int mvebu_cpu_reset_deassert(int cpu);
void mvebu_pmsu_set_cpu_boot_addr(int hw_cpu, void *boot_addr);
void mvebu_system_controller_set_cpu_boot_addr(void *boot_addr);
int mvebu_system_controller_get_soc_id(u32 *dev, u32 *rev);

void __iomem *mvebu_get_scu_base(void);

int mvebu_pm_init(void (*board_pm_enter)(void __iomem *sdram_reg, u32 srcmd));

#endif
