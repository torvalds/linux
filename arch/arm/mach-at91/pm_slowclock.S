/*
 * arch/arm/mach-at91/pm_slow_clock.S
 *
 *  Copyright (C) 2006 Savin Zlobec
 *
 * AT91SAM9 support:
 *  Copyright (C) 2007 Anti Sullin <anti.sullin@artecdesign.ee
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/linkage.h>
#include <mach/hardware.h>
#include <mach/at91_pmc.h>

#if defined(CONFIG_ARCH_AT91RM9200)
#include <mach/at91rm9200_mc.h>
#elif defined(CONFIG_ARCH_AT91CAP9)
#include <mach/at91cap9_ddrsdr.h>
#elif defined(CONFIG_ARCH_AT91SAM9G45)
#include <mach/at91sam9_ddrsdr.h>
#else
#include <mach/at91sam9_sdramc.h>
#endif


#ifdef CONFIG_ARCH_AT91SAM9263
/*
 * FIXME either or both the SDRAM controllers (EB0, EB1) might be in use;
 * handle those cases both here and in the Suspend-To-RAM support.
 */
#warning Assuming EB1 SDRAM controller is *NOT* used
#endif

/*
 * When SLOWDOWN_MASTER_CLOCK is defined we will also slow down the Master
 * clock during suspend by adjusting its prescalar and divisor.
 * NOTE: This hasn't been shown to be stable on SAM9s; and on the RM9200 there
 *       are errata regarding adjusting the prescalar and divisor.
 */
#undef SLOWDOWN_MASTER_CLOCK

#define MCKRDY_TIMEOUT		1000
#define MOSCRDY_TIMEOUT 	1000
#define PLLALOCK_TIMEOUT	1000
#define PLLBLOCK_TIMEOUT	1000


/*
 * Wait until master clock is ready (after switching master clock source)
 */
	.macro wait_mckrdy
	mov	r4, #MCKRDY_TIMEOUT
1:	sub	r4, r4, #1
	cmp	r4, #0
	beq	2f
	ldr	r3, [r1, #(AT91_PMC_SR - AT91_PMC)]
	tst	r3, #AT91_PMC_MCKRDY
	beq	1b
2:
	.endm

/*
 * Wait until master oscillator has stabilized.
 */
	.macro wait_moscrdy
	mov	r4, #MOSCRDY_TIMEOUT
1:	sub	r4, r4, #1
	cmp	r4, #0
	beq	2f
	ldr	r3, [r1, #(AT91_PMC_SR - AT91_PMC)]
	tst	r3, #AT91_PMC_MOSCS
	beq	1b
2:
	.endm

/*
 * Wait until PLLA has locked.
 */
	.macro wait_pllalock
	mov	r4, #PLLALOCK_TIMEOUT
1:	sub	r4, r4, #1
	cmp	r4, #0
	beq	2f
	ldr	r3, [r1, #(AT91_PMC_SR - AT91_PMC)]
	tst	r3, #AT91_PMC_LOCKA
	beq	1b
2:
	.endm

/*
 * Wait until PLLB has locked.
 */
	.macro wait_pllblock
	mov	r4, #PLLBLOCK_TIMEOUT
1:	sub	r4, r4, #1
	cmp	r4, #0
	beq	2f
	ldr	r3, [r1, #(AT91_PMC_SR - AT91_PMC)]
	tst	r3, #AT91_PMC_LOCKB
	beq	1b
2:
	.endm

	.text

ENTRY(at91_slow_clock)
	/* Save registers on stack */
	stmfd	sp!, {r0 - r12, lr}

	/*
	 * Register usage:
	 *  R1 = Base address of AT91_PMC
	 *  R2 = Base address of RAM Controller (SDRAM, DDRSDR, or AT91_SYS)
	 *  R3 = temporary register
	 *  R4 = temporary register
	 *  R5 = Base address of second RAM Controller or 0 if not present
	 */
	ldr	r1, .at91_va_base_pmc
	ldr	r2, .at91_va_base_sdramc
	ldr	r5, .at91_va_base_ramc1

	/* Drain write buffer */
	mov	r0, #0
	mcr	p15, 0, r0, c7, c10, 4

#ifdef CONFIG_ARCH_AT91RM9200
	/* Put SDRAM in self-refresh mode */
	mov	r3, #1
	str	r3, [r2, #AT91_SDRAMC_SRR]
#elif defined(CONFIG_ARCH_AT91CAP9) \
	|| defined(CONFIG_ARCH_AT91SAM9G45)

	/* prepare for DDRAM self-refresh mode */
	ldr	r3, [r2, #AT91_DDRSDRC_LPR]
	str	r3, .saved_sam9_lpr
	bic	r3, #AT91_DDRSDRC_LPCB
	orr	r3, #AT91_DDRSDRC_LPCB_SELF_REFRESH

	/* figure out if we use the second ram controller */
	cmp	r5, #0
	ldrne	r4, [r5, #AT91_DDRSDRC_LPR]
	strne	r4, .saved_sam9_lpr1
	bicne	r4, #AT91_DDRSDRC_LPCB
	orrne	r4, #AT91_DDRSDRC_LPCB_SELF_REFRESH

	/* Enable DDRAM self-refresh mode */
	str	r3, [r2, #AT91_DDRSDRC_LPR]
	strne	r4, [r5, #AT91_DDRSDRC_LPR]
#else
	/* Enable SDRAM self-refresh mode */
	ldr	r3, [r2, #AT91_SDRAMC_LPR]
	str	r3, .saved_sam9_lpr

	bic	r3, #AT91_SDRAMC_LPCB
	orr	r3, #AT91_SDRAMC_LPCB_SELF_REFRESH
	str	r3, [r2, #AT91_SDRAMC_LPR]
#endif

	/* Save Master clock setting */
	ldr	r3, [r1, #(AT91_PMC_MCKR - AT91_PMC)]
	str	r3, .saved_mckr

	/*
	 * Set the Master clock source to slow clock
	 */
	bic	r3, r3, #AT91_PMC_CSS
	str	r3, [r1, #(AT91_PMC_MCKR - AT91_PMC)]

	wait_mckrdy

#ifdef SLOWDOWN_MASTER_CLOCK
	/*
	 * Set the Master Clock PRES and MDIV fields.
	 *
	 * See AT91RM9200 errata #27 and #28 for details.
	 */
	mov	r3, #0
	str	r3, [r1, #(AT91_PMC_MCKR - AT91_PMC)]

	wait_mckrdy
#endif

	/* Save PLLA setting and disable it */
	ldr	r3, [r1, #(AT91_CKGR_PLLAR - AT91_PMC)]
	str	r3, .saved_pllar

	mov	r3, #AT91_PMC_PLLCOUNT
	orr	r3, r3, #(1 << 29)		/* bit 29 always set */
	str	r3, [r1, #(AT91_CKGR_PLLAR - AT91_PMC)]

	/* Save PLLB setting and disable it */
	ldr	r3, [r1, #(AT91_CKGR_PLLBR - AT91_PMC)]
	str	r3, .saved_pllbr

	mov	r3, #AT91_PMC_PLLCOUNT
	str	r3, [r1, #(AT91_CKGR_PLLBR - AT91_PMC)]

	/* Turn off the main oscillator */
	ldr	r3, [r1, #(AT91_CKGR_MOR - AT91_PMC)]
	bic	r3, r3, #AT91_PMC_MOSCEN
	str	r3, [r1, #(AT91_CKGR_MOR - AT91_PMC)]

	/* Wait for interrupt */
	mcr	p15, 0, r0, c7, c0, 4

	/* Turn on the main oscillator */
	ldr	r3, [r1, #(AT91_CKGR_MOR - AT91_PMC)]
	orr	r3, r3, #AT91_PMC_MOSCEN
	str	r3, [r1, #(AT91_CKGR_MOR - AT91_PMC)]

	wait_moscrdy

	/* Restore PLLB setting */
	ldr	r3, .saved_pllbr
	str	r3, [r1, #(AT91_CKGR_PLLBR - AT91_PMC)]

	tst	r3, #(AT91_PMC_MUL &  0xff0000)
	bne	1f
	tst	r3, #(AT91_PMC_MUL & ~0xff0000)
	beq	2f
1:
	wait_pllblock
2:

	/* Restore PLLA setting */
	ldr	r3, .saved_pllar
	str	r3, [r1, #(AT91_CKGR_PLLAR - AT91_PMC)]

	tst	r3, #(AT91_PMC_MUL &  0xff0000)
	bne	3f
	tst	r3, #(AT91_PMC_MUL & ~0xff0000)
	beq	4f
3:
	wait_pllalock
4:

#ifdef SLOWDOWN_MASTER_CLOCK
	/*
	 * First set PRES if it was not 0,
	 * than set CSS and MDIV fields.
	 *
	 * See AT91RM9200 errata #27 and #28 for details.
	 */
	ldr	r3, .saved_mckr
	tst	r3, #AT91_PMC_PRES
	beq	2f
	and	r3, r3, #AT91_PMC_PRES
	str	r3, [r1, #(AT91_PMC_MCKR - AT91_PMC)]

	wait_mckrdy
#endif

	/*
	 * Restore master clock setting
	 */
2:	ldr	r3, .saved_mckr
	str	r3, [r1, #(AT91_PMC_MCKR - AT91_PMC)]

	wait_mckrdy

#ifdef CONFIG_ARCH_AT91RM9200
	/* Do nothing - self-refresh is automatically disabled. */
#elif defined(CONFIG_ARCH_AT91CAP9) \
	|| defined(CONFIG_ARCH_AT91SAM9G45)
	/* Restore LPR on AT91 with DDRAM */
	ldr	r3, .saved_sam9_lpr
	str	r3, [r2, #AT91_DDRSDRC_LPR]

	/* if we use the second ram controller */
	cmp	r5, #0
	ldrne	r4, .saved_sam9_lpr1
	strne	r4, [r5, #AT91_DDRSDRC_LPR]

#else
	/* Restore LPR on AT91 with SDRAM */
	ldr	r3, .saved_sam9_lpr
	str	r3, [r2, #AT91_SDRAMC_LPR]
#endif

	/* Restore registers, and return */
	ldmfd	sp!, {r0 - r12, pc}


.saved_mckr:
	.word 0

.saved_pllar:
	.word 0

.saved_pllbr:
	.word 0

.saved_sam9_lpr:
	.word 0

.saved_sam9_lpr1:
	.word 0

.at91_va_base_pmc:
	.word AT91_VA_BASE_SYS + AT91_PMC

#ifdef CONFIG_ARCH_AT91RM9200
.at91_va_base_sdramc:
	.word AT91_VA_BASE_SYS
#elif defined(CONFIG_ARCH_AT91CAP9) \
	|| defined(CONFIG_ARCH_AT91SAM9G45)
.at91_va_base_sdramc:
	.word AT91_VA_BASE_SYS + AT91_DDRSDRC0
#else
.at91_va_base_sdramc:
	.word AT91_VA_BASE_SYS + AT91_SDRAMC0
#endif

.at91_va_base_ramc1:
#if defined(CONFIG_ARCH_AT91SAM9G45)
	.word AT91_VA_BASE_SYS + AT91_DDRSDRC1
#else
	.word 0
#endif

ENTRY(at91_slow_clock_sz)
	.word .-at91_slow_clock
