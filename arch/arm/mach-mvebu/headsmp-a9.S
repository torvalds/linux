/*
 * SMP support: Entry point for secondary CPUs of Marvell EBU
 * Cortex-A9 based SOCs (Armada 375 and Armada 38x).
 *
 * Copyright (C) 2014 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>

	__CPUINIT

ENTRY(mvebu_cortex_a9_secondary_startup)
ARM_BE8(setend	be)
	bl      v7_invalidate_l1
	b	secondary_startup
ENDPROC(mvebu_cortex_a9_secondary_startup)
