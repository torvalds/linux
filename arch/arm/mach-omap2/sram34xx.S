/*
 * linux/arch/arm/mach-omap3/sram.S
 *
 * Omap3 specific functions that need to be run in internal SRAM
 *
 * Copyright (C) 2004, 2007, 2008 Texas Instruments, Inc.
 * Copyright (C) 2008 Nokia Corporation
 *
 * Rajendra Nayak <rnayak@ti.com>
 * Richard Woodruff <r-woodruff2@ti.com>
 * Paul Walmsley
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

/* r4 parameters */
#define SDRC_NO_UNLOCK_DLL		0x0
#define SDRC_UNLOCK_DLL			0x1

/* SDRC_DLLA_CTRL bit settings */
#define FIXEDDELAY_SHIFT		24
#define FIXEDDELAY_MASK			(0xff << FIXEDDELAY_SHIFT)
#define DLLIDLE_MASK			0x4

/*
 * SDRC_DLLA_CTRL default values: TI hardware team indicates that
 * FIXEDDELAY should be initialized to 0xf.  This apparently was
 * empirically determined during process testing, so no derivation
 * was provided.
 */
#define FIXEDDELAY_DEFAULT		(0x0f << FIXEDDELAY_SHIFT)

/* SDRC_DLLA_STATUS bit settings */
#define LOCKSTATUS_MASK			0x4

/* SDRC_POWER bit settings */
#define SRFRONIDLEREQ_MASK		0x40
#define PWDENA_MASK			0x4

/* CM_IDLEST1_CORE bit settings */
#define ST_SDRC_MASK			0x2

/* CM_ICLKEN1_CORE bit settings */
#define EN_SDRC_MASK			0x2

/* CM_CLKSEL1_PLL bit settings */
#define CORE_DPLL_CLKOUT_DIV_SHIFT	0x1b

/*
 * omap3_sram_configure_core_dpll - change DPLL3 M2 divider
 * r0 = new SDRC_RFR_CTRL register contents
 * r1 = new SDRC_ACTIM_CTRLA register contents
 * r2 = new SDRC_ACTIM_CTRLB register contents
 * r3 = new M2 divider setting (only 1 and 2 supported right now)
 * r4 = unlock SDRC DLL? (1 = yes, 0 = no).  Only unlock DLL for
 *      SDRC rates < 83MHz
 * r5 = number of MPU cycles to wait for SDRC to stabilize after
 *      reprogramming the SDRC when switching to a slower MPU speed
 * r6 = new SDRC_MR_0 register value
 * r7 = increasing SDRC rate? (1 = yes, 0 = no)
 *
 */
ENTRY(omap3_sram_configure_core_dpll)
	stmfd	sp!, {r1-r12, lr}	@ store regs to stack
	ldr	r4, [sp, #52]		@ pull extra args off the stack
	ldr	r5, [sp, #56]		@ load extra args from the stack
	ldr	r6, [sp, #60]		@ load extra args from the stack
	ldr	r7, [sp, #64]		@ load extra args from the stack
	dsb				@ flush buffered writes to interconnect
	cmp	r7, #1			@ if increasing SDRC clk rate,
	bleq	configure_sdrc		@ program the SDRC regs early (for RFR)
	cmp	r4, #SDRC_UNLOCK_DLL	@ set the intended DLL state
	bleq	unlock_dll
	blne	lock_dll
	bl	sdram_in_selfrefresh	@ put SDRAM in self refresh, idle SDRC
	bl 	configure_core_dpll	@ change the DPLL3 M2 divider
	bl	enable_sdrc		@ take SDRC out of idle
	cmp	r4, #SDRC_UNLOCK_DLL	@ wait for DLL status to change
	bleq	wait_dll_unlock
	blne	wait_dll_lock
	cmp	r7, #1			@ if increasing SDRC clk rate,
	beq	return_to_sdram		@ return to SDRAM code, otherwise,
	bl	configure_sdrc		@ reprogram SDRC regs now
	mov	r12, r5
	bl	wait_clk_stable		@ wait for SDRC to stabilize
return_to_sdram:
	isb				@ prevent speculative exec past here
	mov 	r0, #0 			@ return value
	ldmfd	sp!, {r1-r12, pc}	@ restore regs and return
unlock_dll:
	ldr	r11, omap3_sdrc_dlla_ctrl
	ldr	r12, [r11]
	and	r12, r12, #FIXEDDELAY_MASK
	orr	r12, r12, #FIXEDDELAY_DEFAULT
	orr	r12, r12, #DLLIDLE_MASK
	str	r12, [r11]		@ (no OCP barrier needed)
	bx	lr
lock_dll:
	ldr	r11, omap3_sdrc_dlla_ctrl
	ldr	r12, [r11]
	bic	r12, r12, #DLLIDLE_MASK
	str	r12, [r11]		@ (no OCP barrier needed)
	bx	lr
sdram_in_selfrefresh:
	ldr	r11, omap3_sdrc_power	@ read the SDRC_POWER register
	ldr	r12, [r11]		@ read the contents of SDRC_POWER
	mov	r9, r12			@ keep a copy of SDRC_POWER bits
	orr 	r12, r12, #SRFRONIDLEREQ_MASK	@ enable self refresh on idle
	bic 	r12, r12, #PWDENA_MASK	@ clear PWDENA
	str 	r12, [r11]		@ write back to SDRC_POWER register
	ldr	r12, [r11]		@ posted-write barrier for SDRC
idle_sdrc:
	ldr	r11, omap3_cm_iclken1_core	@ read the CM_ICLKEN1_CORE reg
	ldr	r12, [r11]
	bic	r12, r12, #EN_SDRC_MASK		@ disable iclk bit for SDRC
	str 	r12, [r11]
wait_sdrc_idle:
	ldr 	r11, omap3_cm_idlest1_core
	ldr 	r12, [r11]
	and 	r12, r12, #ST_SDRC_MASK		@ check for SDRC idle
	cmp 	r12, #ST_SDRC_MASK
	bne 	wait_sdrc_idle
	bx 	lr
configure_core_dpll:
	ldr 	r11, omap3_cm_clksel1_pll
	ldr	r12, [r11]
	ldr	r10, core_m2_mask_val	@ modify m2 for core dpll
	and	r12, r12, r10
	orr	r12, r12, r3, lsl #CORE_DPLL_CLKOUT_DIV_SHIFT
	str	r12, [r11]
	ldr	r12, [r11]		@ posted-write barrier for CM
	bx	lr
wait_clk_stable:
	subs 	r12, r12, #1
	bne	wait_clk_stable
	bx	lr
enable_sdrc:
	ldr 	r11, omap3_cm_iclken1_core
	ldr	r12, [r11]
	orr 	r12, r12, #EN_SDRC_MASK		@ enable iclk bit for SDRC
	str 	r12, [r11]
wait_sdrc_idle1:
	ldr 	r11, omap3_cm_idlest1_core
	ldr	r12, [r11]
	and 	r12, r12, #ST_SDRC_MASK
	cmp	r12, #0
	bne	wait_sdrc_idle1
restore_sdrc_power_val:
	ldr	r11, omap3_sdrc_power
	str	r9, [r11]		@ restore SDRC_POWER, no barrier needed
	bx	lr
wait_dll_lock:
	ldr	r11, omap3_sdrc_dlla_status
	ldr	r12, [r11]
	and 	r12, r12, #LOCKSTATUS_MASK
	cmp	r12, #LOCKSTATUS_MASK
	bne	wait_dll_lock
	bx	lr
wait_dll_unlock:
	ldr	r11, omap3_sdrc_dlla_status
	ldr	r12, [r11]
	and	r12, r12, #LOCKSTATUS_MASK
	cmp	r12, #0x0
	bne	wait_dll_unlock
	bx	lr
configure_sdrc:
	ldr	r11, omap3_sdrc_rfr_ctrl
	str	r0, [r11]
	ldr	r11, omap3_sdrc_actim_ctrla
	str	r1, [r11]
	ldr	r11, omap3_sdrc_actim_ctrlb
	str	r2, [r11]
	ldr	r11, omap3_sdrc_mr_0
	str	r6, [r11]
	ldr	r6, [r11]		@ posted-write barrier for SDRC
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
omap3_sdrc_mr_0:
	.word OMAP34XX_SDRC_REGADDR(SDRC_MR_0)
omap3_sdrc_dlla_status:
	.word OMAP34XX_SDRC_REGADDR(SDRC_DLLA_STATUS)
omap3_sdrc_dlla_ctrl:
	.word OMAP34XX_SDRC_REGADDR(SDRC_DLLA_CTRL)
core_m2_mask_val:
	.word 0x07FFFFFF

ENTRY(omap3_sram_configure_core_dpll_sz)
	.word	. - omap3_sram_configure_core_dpll
