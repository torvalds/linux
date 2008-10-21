/*
 * linux/arch/arm/mach-omap3/sram.S
 *
 * Omap3 specific functions that need to be run in internal SRAM
 *
 * (C) Copyright 2007
 * Texas Instruments Inc.
 * Rajendra Nayak <rnayak@ti.com>
 *
 * (C) Copyright 2004
 * Texas Instruments, <www.ti.com>
 * Richard Woodruff <r-woodruff2@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR /PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <mach/hardware.h>

#include <mach/io.h>

#include "sdrc.h"
#include "cm.h"

	.text

/*
 * Change frequency of core dpll
 * r0 = sdrc_rfr_ctrl r1 = sdrc_actim_ctrla r2 = sdrc_actim_ctrlb r3 = M2
 */
ENTRY(omap3_sram_configure_core_dpll)
	stmfd	sp!, {r1-r12, lr}	@ store regs to stack
	cmp	r3, #0x2
	blne	configure_sdrc
	cmp	r3, #0x2
	blne	lock_dll
	cmp	r3, #0x1
	blne	unlock_dll
	bl	sdram_in_selfrefresh	@ put the SDRAM in self refresh
	bl 	configure_core_dpll
	bl	enable_sdrc
	cmp	r3, #0x1
	blne	wait_dll_unlock
	cmp	r3, #0x2
	blne	wait_dll_lock
	cmp	r3, #0x1
	blne	configure_sdrc
	mov 	r0, #0 			@ return value
	ldmfd	sp!, {r1-r12, pc}	@ restore regs and return
unlock_dll:
	ldr	r4, omap3_sdrc_dlla_ctrl
	ldr	r5, [r4]
	orr	r5, r5, #0x4
	str	r5, [r4]
	bx	lr
lock_dll:
	ldr	r4, omap3_sdrc_dlla_ctrl
	ldr	r5, [r4]
	bic	r5, r5, #0x4
	str	r5, [r4]
	bx	lr
sdram_in_selfrefresh:
	mov	r5, #0x0		@ Move 0 to R5
	mcr	p15, 0, r5, c7, c10, 5	@ memory barrier
	ldr	r4, omap3_sdrc_power	@ read the SDRC_POWER register
	ldr	r5, [r4]		@ read the contents of SDRC_POWER
	orr 	r5, r5, #0x40		@ enable self refresh on idle req
	str 	r5, [r4]		@ write back to SDRC_POWER register
	ldr	r4, omap3_cm_iclken1_core	@ read the CM_ICLKEN1_CORE reg
	ldr	r5, [r4]
	bic	r5, r5, #0x2		@ disable iclk bit for SRDC
	str 	r5, [r4]
wait_sdrc_idle:
	ldr 	r4, omap3_cm_idlest1_core
	ldr 	r5, [r4]
	and 	r5, r5, #0x2		@ check for SDRC idle
	cmp 	r5, #2
	bne 	wait_sdrc_idle
	bx 	lr
configure_core_dpll:
	ldr 	r4, omap3_cm_clksel1_pll
	ldr	r5, [r4]
	ldr	r6, core_m2_mask_val	@ modify m2 for core dpll
	and	r5, r5, r6
	orr	r5, r5, r3, lsl #0x1B	@ r3 contains the M2 val
	str	r5, [r4]
	mov 	r5, #0x800		@ wait for the clock to stabilise
	cmp	r3, #2
	bne	wait_clk_stable
	bx	lr
wait_clk_stable:
	subs 	r5, r5, #1
	bne	wait_clk_stable
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	bx	lr
enable_sdrc:
	ldr 	r4, omap3_cm_iclken1_core
	ldr	r5, [r4]
	orr 	r5, r5, #0x2		@ enable iclk bit for SDRC
	str 	r5, [r4]
wait_sdrc_idle1:
	ldr 	r4, omap3_cm_idlest1_core
	ldr	r5, [r4]
	and 	r5, r5, #0x2
	cmp	r5, #0
	bne	wait_sdrc_idle1
	ldr	r4, omap3_sdrc_power
	ldr	r5, [r4]
	bic 	r5, r5, #0x40
	str 	r5, [r4]
	bx	lr
wait_dll_lock:
	ldr	r4, omap3_sdrc_dlla_status
	ldr	r5, [r4]
	and 	r5, r5, #0x4
	cmp	r5, #0x4
	bne	wait_dll_lock
	bx	lr
wait_dll_unlock:
	ldr	r4, omap3_sdrc_dlla_status
	ldr	r5, [r4]
	and	r5, r5, #0x4
	cmp	r5, #0x0
	bne	wait_dll_unlock
	bx	lr
configure_sdrc:
	ldr	r4, omap3_sdrc_rfr_ctrl
	str	r0, [r4]
	ldr	r4, omap3_sdrc_actim_ctrla
	str	r1, [r4]
	ldr	r4, omap3_sdrc_actim_ctrlb
	str	r2, [r4]
	bx	lr

omap3_sdrc_power:
	.word OMAP34XX_SDRC_REGADDR(SDRC_POWER)
omap3_cm_clksel1_pll:
	.word OMAP34XX_CM_REGADDR(PLL_MOD, CM_CLKSEL1)
omap3_cm_idlest1_core:
	.word OMAP34XX_CM_REGADDR(CORE_MOD, CM_IDLEST)
omap3_cm_iclken1_core:
	.word OMAP34XX_CM_REGADDR(CORE_MOD, CM_ICLKEN1)
omap3_sdrc_rfr_ctrl:
	.word OMAP34XX_SDRC_REGADDR(SDRC_RFR_CTRL_0)
omap3_sdrc_actim_ctrla:
	.word OMAP34XX_SDRC_REGADDR(SDRC_ACTIM_CTRL_A_0)
omap3_sdrc_actim_ctrlb:
	.word OMAP34XX_SDRC_REGADDR(SDRC_ACTIM_CTRL_B_0)
omap3_sdrc_dlla_status:
	.word OMAP34XX_SDRC_REGADDR(SDRC_DLLA_STATUS)
omap3_sdrc_dlla_ctrl:
	.word OMAP34XX_SDRC_REGADDR(SDRC_DLLA_CTRL)
core_m2_mask_val:
	.word 0x07FFFFFF

ENTRY(omap3_sram_configure_core_dpll_sz)
	.word	. - omap3_sram_configure_core_dpll
