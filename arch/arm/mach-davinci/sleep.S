/*
 * (C) Copyright 2009, Texas Instruments, Inc. http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR /PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/* replicated define because linux/bitops.h cannot be included in assembly */
#define BIT(nr)			(1 << (nr))

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <mach/psc.h>
#include <mach/memory.h>

#include "clock.h"

/* Arbitrary, hardware currently does not update PHYRDY correctly */
#define PHYRDY_CYCLES		0x1000

/* Assume 25 MHz speed for the cycle conversions since PLLs are bypassed */
#define PLL_BYPASS_CYCLES	(PLL_BYPASS_TIME * 25)
#define PLL_RESET_CYCLES	(PLL_RESET_TIME	* 25)
#define PLL_LOCK_CYCLES		(PLL_LOCK_TIME * 25)

#define DEEPSLEEP_SLEEPENABLE_BIT	BIT(31)

	.text
/*
 * Move DaVinci into deep sleep state
 *
 * Note: This code is copied to internal SRAM by PM code. When the DaVinci
 *	 wakes up it continues execution at the point it went to sleep.
 * Register Usage:
 * 	r0: contains virtual base for DDR2 controller
 * 	r1: contains virtual base for DDR2 Power and Sleep controller (PSC)
 * 	r2: contains PSC number for DDR2
 * 	r3: contains virtual base DDR2 PLL controller
 * 	r4: contains virtual address of the DEEPSLEEP register
 */
ENTRY(davinci_cpu_suspend)
	stmfd	sp!, {r0-r12, lr}		@ save registers on stack

	ldr 	ip, CACHE_FLUSH
	blx	ip

	ldmia	r0, {r0-r4}

	/*
	 * Switch DDR to self-refresh mode.
	 */

	/* calculate SDRCR address */
	ldr	ip, [r0, #DDR2_SDRCR_OFFSET]
	bic	ip, ip, #DDR2_SRPD_BIT
	orr	ip, ip, #DDR2_LPMODEN_BIT
	str	ip, [r0, #DDR2_SDRCR_OFFSET]

	ldr	ip, [r0, #DDR2_SDRCR_OFFSET]
	orr	ip, ip, #DDR2_MCLKSTOPEN_BIT
	str	ip, [r0, #DDR2_SDRCR_OFFSET]

       mov	ip, #PHYRDY_CYCLES
1:     subs	ip, ip, #0x1
       bne	1b

       /* Disable DDR2 LPSC */
	mov	r7, r0
	mov	r0, #0x2
	bl davinci_ddr_psc_config
	mov	r0, r7

	/* Disable clock to DDR PHY */
	ldr	ip, [r3, #PLLDIV1]
	bic	ip, ip, #PLLDIV_EN
	str	ip, [r3, #PLLDIV1]

	/* Put the DDR PLL in bypass and power down */
	ldr	ip, [r3, #PLLCTL]
	bic	ip, ip, #PLLCTL_PLLENSRC
	bic	ip, ip, #PLLCTL_PLLEN
	str	ip, [r3, #PLLCTL]

	/* Wait for PLL to switch to bypass */
       mov	ip, #PLL_BYPASS_CYCLES
2:     subs	ip, ip, #0x1
       bne	2b

       /* Power down the PLL */
	ldr	ip, [r3, #PLLCTL]
	orr	ip, ip, #PLLCTL_PLLPWRDN
	str	ip, [r3, #PLLCTL]

	/* Go to deep sleep */
	ldr	ip, [r4]
	orr	ip, ip, #DEEPSLEEP_SLEEPENABLE_BIT
	/* System goes to sleep beyond after this instruction */
	str	ip, [r4]

	/* Wake up from sleep */

	/* Clear sleep enable */
	ldr	ip, [r4]
	bic	ip, ip, #DEEPSLEEP_SLEEPENABLE_BIT
	str	ip, [r4]

	/* initialize the DDR PLL controller */

	/* Put PLL in reset */
	ldr	ip, [r3, #PLLCTL]
	bic	ip, ip, #PLLCTL_PLLRST
	str	ip, [r3, #PLLCTL]

	/* Clear PLL power down */
	ldr	ip, [r3, #PLLCTL]
	bic	ip, ip, #PLLCTL_PLLPWRDN
	str	ip, [r3, #PLLCTL]

       mov	ip, #PLL_RESET_CYCLES
3:     subs	ip, ip, #0x1
       bne	3b

       /* Bring PLL out of reset */
	ldr	ip, [r3, #PLLCTL]
	orr	ip, ip, #PLLCTL_PLLRST
	str	ip, [r3, #PLLCTL]

	/* Wait for PLL to lock (assume prediv = 1, 25MHz OSCIN) */
       mov	ip, #PLL_LOCK_CYCLES
4:     subs	ip, ip, #0x1
       bne	4b

       /* Remove PLL from bypass mode */
	ldr	ip, [r3, #PLLCTL]
	bic	ip, ip, #PLLCTL_PLLENSRC
	orr	ip, ip, #PLLCTL_PLLEN
	str	ip, [r3, #PLLCTL]

	/* Start 2x clock to DDR2 */

	ldr	ip, [r3, #PLLDIV1]
	orr	ip, ip, #PLLDIV_EN
	str	ip, [r3, #PLLDIV1]

	/* Enable VCLK */

       /* Enable DDR2 LPSC */
	mov	r7, r0
	mov	r0, #0x3
	bl davinci_ddr_psc_config
	mov	r0, r7

	/* clear  MCLKSTOPEN */

	ldr	ip, [r0, #DDR2_SDRCR_OFFSET]
	bic	ip, ip, #DDR2_MCLKSTOPEN_BIT
	str	ip, [r0, #DDR2_SDRCR_OFFSET]

	ldr	ip, [r0, #DDR2_SDRCR_OFFSET]
	bic	ip, ip, #DDR2_LPMODEN_BIT
	str	ip, [r0, #DDR2_SDRCR_OFFSET]

	/* Restore registers and return */
	ldmfd   sp!, {r0-r12, pc}

ENDPROC(davinci_cpu_suspend)

/*
 * Disables or Enables DDR2 LPSC
 * Register Usage:
 * 	r0: Enable or Disable LPSC r0 = 0x3 => Enable, r0 = 0x2 => Disable LPSC
 * 	r1: contains virtual base for DDR2 Power and Sleep controller (PSC)
 * 	r2: contains PSC number for DDR2
 */
ENTRY(davinci_ddr_psc_config)
	/* Set next state in mdctl for DDR2 */
	mov	r6, #MDCTL
	add	r6, r6, r2, lsl #2
	ldr	ip, [r1, r6]
	bic	ip, ip, #MDSTAT_STATE_MASK
	orr	ip, ip, r0
	str	ip, [r1, r6]

	/* Enable the Power Domain Transition Command */
	ldr	ip, [r1, #PTCMD]
	orr	ip, ip, #0x1
	str	ip, [r1, #PTCMD]

	/* Check for Transition Complete (PTSTAT) */
ptstat_done:
	ldr	ip, [r1, #PTSTAT]
	and	ip, ip, #0x1
	cmp 	ip, #0x0
	bne	ptstat_done

	/* Check for DDR2 clock disable completion; */
	mov	r6, #MDSTAT
	add	r6, r6, r2, lsl #2
ddr2clk_stop_done:
	ldr	ip, [r1, r6]
	and	ip, ip, #MDSTAT_STATE_MASK
	cmp	ip, r0
	bne	ddr2clk_stop_done

	mov	pc, lr
ENDPROC(davinci_ddr_psc_config)

CACHE_FLUSH:
#ifdef CONFIG_CPU_V6
	.word	v6_flush_kern_cache_all
#else
	.word   arm926_flush_kern_cache_all
#endif

ENTRY(davinci_cpu_suspend_sz)
	.word	. - davinci_cpu_suspend
ENDPROC(davinci_cpu_suspend_sz)
