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

/*
 * Armada XP specific entry point for secondary CPUs.
 * We add the CPU to the coherency fabric and then jump to secondary
 * startup
 */
ENTRY(armada_xp_secondary_startup)
	/* Get coherency fabric base physical address */
	adr	r0, 1f
	ldr	r1, [r0]
	ldr	r0, [r0, r1]

	/* Read CPU id */
	mrc     p15, 0, r1, c0, c0, 5
	and     r1, r1, #0xF

	/* Add CPU to coherency fabric */
	bl	ll_set_cpu_coherent
	b	secondary_startup

ENDPROC(armada_xp_secondary_startup)

	.align 2
1:
	.long	coherency_phys_base - .
