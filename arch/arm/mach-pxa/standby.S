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
#include <mach/hardware.h>

#include <mach/pxa-regs.h>
#include <mach/pxa2xx-regs.h>

		.text

#ifdef CONFIG_PXA27x
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

#endif

#ifdef CONFIG_PXA3xx

#define PXA3_MDCNFG		0x0000
#define PXA3_MDCNFG_DMCEN	(1 << 30)
#define PXA3_DDR_HCAL		0x0060
#define PXA3_DDR_HCAL_HCRNG	0x1f
#define PXA3_DDR_HCAL_HCPROG	(1 << 28)
#define PXA3_DDR_HCAL_HCEN	(1 << 31)
#define PXA3_DMCIER		0x0070
#define PXA3_DMCIER_EDLP	(1 << 29)
#define PXA3_DMCISR		0x0078
#define PXA3_RCOMP		0x0100
#define PXA3_RCOMP_SWEVAL	(1 << 31)

ENTRY(pm_enter_standby_start)
	mov	r1, #0xf6000000			@ DMEMC_REG_BASE (PXA3_MDCNFG)
	add	r1, r1, #0x00100000

	/*
	 * Preload the TLB entry for accessing the dynamic memory
	 * controller registers.  Note that page table lookups will
	 * fail until the dynamic memory controller has been
	 * reinitialised - and that includes MMU page table walks.
	 * This also means that only the dynamic memory controller
	 * can be reliably accessed in the code following standby.
	 */
	ldr	r2, [r1]			@ Dummy read PXA3_MDCNFG

	mcr	p14, 0, r0, c7, c0, 0
	.rept	8
	nop
	.endr

	ldr	r0, [r1, #PXA3_DDR_HCAL]	@ Clear (and wait for) HCEN
	bic	r0, r0, #PXA3_DDR_HCAL_HCEN
	str	r0, [r1, #PXA3_DDR_HCAL]
1:	ldr	r0, [r1, #PXA3_DDR_HCAL]
	tst	r0, #PXA3_DDR_HCAL_HCEN
	bne	1b

	ldr	r0, [r1, #PXA3_RCOMP]		@ Initiate RCOMP
	orr	r0, r0, #PXA3_RCOMP_SWEVAL
	str	r0, [r1, #PXA3_RCOMP]

	mov	r0, #~0				@ Clear interrupts
	str	r0, [r1, #PXA3_DMCISR]

	ldr	r0, [r1, #PXA3_DMCIER]		@ set DMIER[EDLP]
	orr	r0, r0, #PXA3_DMCIER_EDLP
	str	r0, [r1, #PXA3_DMCIER]

	ldr	r0, [r1, #PXA3_DDR_HCAL]	@ clear HCRNG, set HCPROG, HCEN
	bic	r0, r0, #PXA3_DDR_HCAL_HCRNG
	orr	r0, r0, #PXA3_DDR_HCAL_HCEN | PXA3_DDR_HCAL_HCPROG
	str	r0, [r1, #PXA3_DDR_HCAL]

1:	ldr	r0, [r1, #PXA3_DMCISR]
	tst	r0, #PXA3_DMCIER_EDLP
	beq	1b

	ldr	r0, [r1, #PXA3_MDCNFG]		@ set PXA3_MDCNFG[DMCEN]
	orr	r0, r0, #PXA3_MDCNFG_DMCEN
	str	r0, [r1, #PXA3_MDCNFG]
1:	ldr	r0, [r1, #PXA3_MDCNFG]
	tst	r0, #PXA3_MDCNFG_DMCEN
	beq	1b

	ldr	r0, [r1, #PXA3_DDR_HCAL]	@ set PXA3_DDR_HCAL[HCRNG]
	orr	r0, r0, #2 @ HCRNG
	str	r0, [r1, #PXA3_DDR_HCAL]

	ldr	r0, [r1, #PXA3_DMCIER]		@ Clear the interrupt
	bic	r0, r0, #0x20000000
	str	r0, [r1, #PXA3_DMCIER]

	mov	pc, lr
ENTRY(pm_enter_standby_end)

#endif
