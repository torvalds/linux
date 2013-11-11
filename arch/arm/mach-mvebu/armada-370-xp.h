/*
 * Generic definitions for Marvell Armada_370_XP SoCs
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

#ifndef __MACH_ARMADA_370_XP_H
#define __MACH_ARMADA_370_XP_H

#define ARMADA_370_XP_REGS_PHYS_BASE	0xd0000000
#define ARMADA_370_XP_REGS_VIRT_BASE	IOMEM(0xfec00000)
#define ARMADA_370_XP_REGS_SIZE		SZ_1M

/* These defines can go away once mvebu-mbus has a DT binding */
#define ARMADA_370_XP_MBUS_WINS_BASE    (ARMADA_370_XP_REGS_PHYS_BASE + 0x20000)
#define ARMADA_370_XP_MBUS_WINS_SIZE    0x100
#define ARMADA_370_XP_SDRAM_WINS_BASE   (ARMADA_370_XP_REGS_PHYS_BASE + 0x20180)
#define ARMADA_370_XP_SDRAM_WINS_SIZE   0x20

#ifdef CONFIG_SMP
#include <linux/cpumask.h>

void armada_mpic_send_doorbell(const struct cpumask *mask, unsigned int irq);
void armada_xp_mpic_smp_cpu_init(void);
#endif

#endif /* __MACH_ARMADA_370_XP_H */
