/*
 * SMP support: Entry point for secondary CPUs
 *
 * Copyright (C) 2012 Marvell
 *
 * Yehuda Yitschak <yehuday@marvell.com>
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * This file implements the assembly entry point for secondary CPUs in
 * an SMP kernel. The only thing we need to do is to add the CPU to
 * the coherency fabric by writing to 2 registers. Currently the base
 * register addresses are hard coded due to the early initialisation
 * problems.
 */

#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>

/*
 * Armada XP specific entry point for secondary CPUs.
 * We add the CPU to the coherency fabric and then jump to secondary
 * startup
 */
ENTRY(armada_xp_secondary_startup)
 ARM_BE8(setend	be )			@ go BE8 if entered LE

	bl	ll_add_cpu_to_smp_group

	bl	ll_enable_coherency

	b	secondary_startup

ENDPROC(armada_xp_secondary_startup)
