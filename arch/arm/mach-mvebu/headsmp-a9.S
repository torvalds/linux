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
#define CPU_RESUME_ADDR_REG 0xf10182d4

.global armada_375_smp_cpu1_enable_code_start
.global armada_375_smp_cpu1_enable_code_end

armada_375_smp_cpu1_enable_code_start:
	ldr     r0, [pc, #4]
	ldr     r1, [r0]
	ret     r1
	.word   CPU_RESUME_ADDR_REG
armada_375_smp_cpu1_enable_code_end:

ENTRY(mvebu_cortex_a9_secondary_startup)
	bl      v7_invalidate_l1
	b	secondary_startup
ENDPROC(mvebu_cortex_a9_secondary_startup)
