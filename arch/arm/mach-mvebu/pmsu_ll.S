/*
 * Copyright (C) 2014 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 * Gregory Clement <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * This is the entry point through which CPUs exiting cpuidle deep
 * idle state are going.
 */
ENTRY(armada_370_xp_cpu_resume)
ARM_BE8(setend	be )			@ go BE8 if entered LE
	bl	ll_add_cpu_to_smp_group
	bl	ll_enable_coherency
	b	cpu_resume
ENDPROC(armada_370_xp_cpu_resume)

