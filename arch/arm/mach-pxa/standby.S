/*
 * PXA27x standby mode
 *
 * Author: David Burrage
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/hardware.h>

#include <asm/arch/pxa-regs.h>

		.text

ENTRY(pxa_cpu_standby)
	ldr	r0, =PSSR
	mov	r1, #(PSSR_PH | PSSR_STS)
	mov	r2, #PWRMODE_STANDBY
	mov	r3, #UNCACHED_PHYS_0	@ Read mem context in.
	ldr	ip, [r3]
	b	1f

	.align	5
1:	mcr	p14, 0, r2, c7, c0, 0	@ put the system into Standby
	str	r1, [r0]		@ make sure PSSR_PH/STS are clear
	mov	pc, lr
