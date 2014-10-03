/*
 * linux/arch/arm/mach-omap2/sram242x.S
 *
 * Omap2 specific functions that need to be run in internal SRAM
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
 *
 * Richard Woodruff notes that any changes to this code must be carefully
 * audited and tested to ensure that they don't cause a TLB miss while
 * the SDRAM is inaccessible.  Such a situation will crash the system
 * since it will cause the ARM MMU to attempt to walk the page tables.
 * These crashes may be intermittent.
 */
#include <linux/linkage.h>

#include <asm/assembler.h>

#include "soc.h"
#include "iomap.h"
#include "prm2xxx.h"
#include "cm2xxx.h"
#include "sdrc.h"

	.text

	.align	3
ENTRY(omap242x_sram_ddr_init)
	stmfd	sp!, {r0 - r12, lr}	@ save registers on stack

	mov	r12, r2			@ capture CS1 vs CS0
	mov	r8, r3			@ capture force parameter

	/* frequency shift down */
	ldr	r2, omap242x_sdi_cm_clksel2_pll	@ get address of dpllout reg
	mov	r3, #0x1		@ value for 1x operation
	str	r3, [r2]		@ go to L1-freq operation

	/* voltage shift down */
	mov r9, #0x1			@ set up for L1 voltage call
	bl voltage_shift		@ go drop voltage

	/* dll lock mode */
	ldr	r11, omap242x_sdi_sdrc_dlla_ctrl	@ addr of dlla ctrl
	ldr	r10, [r11]		@ get current val
	cmp	r12, #0x1		@ cs1 base (2422 es2.05/1)
	addeq	r11, r11, #0x8		@ if cs1 base, move to DLLB
	mvn	r9, #0x4		@ mask to get clear bit2
	and	r10, r10, r9		@ clear bit2 for lock mode.
	orr	r10, r10, #0x8		@ make sure DLL on (es2 bit pos)
	orr	r10, r10, #0x2		@ 90 degree phase for all below 133Mhz
	str	r10, [r11]		@ commit to DLLA_CTRL
	bl	i_dll_wait		@ wait for dll to lock

	/* get dll value */
	add	r11, r11, #0x4		@ get addr of status reg
	ldr	r10, [r11]		@ get locked value

	/* voltage shift up */
	mov r9, #0x0			@ shift back to L0-voltage
	bl voltage_shift		@ go raise voltage

	/* frequency shift up */
	mov	r3, #0x2		@ value for 2x operation
	str	r3, [r2]		@ go to L0-freq operation

	/* reset entry mode for dllctrl */
	sub	r11, r11, #0x4		@ move from status to ctrl
	cmp	r12, #0x1		@ normalize if cs1 based
	subeq	r11, r11, #0x8		@ possibly back to DLLA
	cmp	r8, #0x1		@ if forced unlock exit
	orreq	r1, r1, #0x4		@ make sure exit with unlocked value
	str	r1, [r11]		@ restore DLLA_CTRL high value
	add	r11, r11, #0x8		@ move to DLLB_CTRL addr
	str	r1, [r11]		@ set value DLLB_CTRL
	bl	i_dll_wait		@ wait for possible lock

	/* set up for return, DDR should be good */
	str r10, [r0]			@ write dll_status and return counter
	ldmfd	sp!, {r0 - r12, pc}	@ restore regs and return

	/* ensure the DLL has relocked */
i_dll_wait:
	mov	r4, #0x800		@ delay DLL relock, min 0x400 L3 clocks
i_dll_delay:
	subs	r4, r4, #0x1
	bne	i_dll_delay
	ret	lr

	/*
	 * shift up or down voltage, use R9 as input to tell level.
	 * wait for it to finish, use 32k sync counter, 1tick=31uS.
	 */
voltage_shift:
	ldr	r4, omap242x_sdi_prcm_voltctrl	@ get addr of volt ctrl.
	ldr	r5, [r4]		@ get value.
	ldr	r6, prcm_mask_val	@ get value of mask
	and	r5, r5, r6		@ apply mask to clear bits
	orr	r5, r5, r9		@ bulld value for L0/L1-volt operation.
	str	r5, [r4]		@ set up for change.
	mov	r3, #0x4000		@ get val for force
	orr	r5, r5, r3		@ build value for force
	str	r5, [r4]		@ Force transition to L1

	ldr	r3, omap242x_sdi_timer_32ksynct_cr	@ get addr of counter
	ldr	r5, [r3]		@ get value
	add	r5, r5, #0x3		@ give it at most 93uS
volt_delay:
	ldr	r7, [r3]		@ get timer value
	cmp	r5, r7			@ time up?
	bhi	volt_delay		@ not yet->branch
	ret	lr			@ back to caller.

omap242x_sdi_cm_clksel2_pll:
	.word OMAP2420_CM_REGADDR(PLL_MOD, CM_CLKSEL2)
omap242x_sdi_sdrc_dlla_ctrl:
	.word OMAP242X_SDRC_REGADDR(SDRC_DLLA_CTRL)
omap242x_sdi_prcm_voltctrl:
	.word OMAP2420_PRCM_VOLTCTRL
prcm_mask_val:
	.word 0xFFFF3FFC
omap242x_sdi_timer_32ksynct_cr:
	.word OMAP2_L4_IO_ADDRESS(OMAP2420_32KSYNCT_BASE + 0x010)
ENTRY(omap242x_sram_ddr_init_sz)
	.word	. - omap242x_sram_ddr_init

/*
 * Reprograms memory timings.
 * r0 = [PRCM_FULL | PRCM_HALF] r1 = SDRC_DLLA_CTRL value r2 = [DDR | SDR]
 * PRCM_FULL = 2, PRCM_HALF = 1, DDR = 1, SDR = 0
 */
	.align	3
ENTRY(omap242x_sram_reprogram_sdrc)
	stmfd	sp!, {r0 - r10, lr}	@ save registers on stack
	mov	r3, #0x0		@ clear for mrc call
	mcr	p15, 0, r3, c7, c10, 4	@ memory barrier, finish ARM SDR/DDR
	nop
	nop
	ldr	r6, omap242x_srs_sdrc_rfr_ctrl	@ get addr of refresh reg
	ldr	r5, [r6]		@ get value
	mov	r5, r5, lsr #8		@ isolate rfr field and drop burst

	cmp	r0, #0x1		@ going to half speed?
	movne	r9, #0x0		@ if up set flag up for pre up, hi volt

	blne	voltage_shift_c		@ adjust voltage

	cmp	r0, #0x1		@ going to half speed (post branch link)
	moveq	r5, r5, lsr #1		@ divide by 2 if to half
	movne	r5, r5, lsl #1		@ mult by 2 if to full
	mov	r5, r5, lsl #8		@ put rfr field back into place
	add	r5, r5, #0x1		@ turn on burst of 1
	ldr	r4, omap242x_srs_cm_clksel2_pll	@ get address of out reg
	ldr	r3, [r4]		@ get curr value
	orr	r3, r3, #0x3
	bic	r3, r3, #0x3		@ clear lower bits
	orr	r3, r3, r0		@ new state value
	str	r3, [r4]		@ set new state (pll/x, x=1 or 2)
	nop
	nop

	moveq	r9, #0x1		@ if speed down, post down, drop volt
	bleq	voltage_shift_c

	mcr	p15, 0, r3, c7, c10, 4	@ memory barrier
	str	r5, [r6]		@ set new RFR_1 value
	add	r6, r6, #0x30		@ get RFR_2 addr
	str	r5, [r6]		@ set RFR_2
	nop
	cmp	r2, #0x1		@ (SDR or DDR) do we need to adjust DLL
	bne	freq_out		@ leave if SDR, no DLL function

	/* With DDR, we need to take care of the DLL for the frequency change */
	ldr	r2, omap242x_srs_sdrc_dlla_ctrl	@ addr of dlla ctrl
	str	r1, [r2]		@ write out new SDRC_DLLA_CTRL
	add	r2, r2, #0x8		@ addr to SDRC_DLLB_CTRL
	str	r1, [r2]		@ commit to SDRC_DLLB_CTRL
	mov	r1, #0x2000		@ wait DLL relock, min 0x400 L3 clocks
dll_wait:
	subs	r1, r1, #0x1
	bne	dll_wait
freq_out:
	ldmfd	sp!, {r0 - r10, pc}	@ restore regs and return

    /*
     * shift up or down voltage, use R9 as input to tell level.
     *	wait for it to finish, use 32k sync counter, 1tick=31uS.
     */
voltage_shift_c:
	ldr	r10, omap242x_srs_prcm_voltctrl	@ get addr of volt ctrl
	ldr	r8, [r10]		@ get value
	ldr	r7, ddr_prcm_mask_val	@ get value of mask
	and	r8, r8, r7		@ apply mask to clear bits
	orr	r8, r8, r9		@ bulld value for L0/L1-volt operation.
	str	r8, [r10]		@ set up for change.
	mov	r7, #0x4000		@ get val for force
	orr	r8, r8, r7		@ build value for force
	str	r8, [r10]		@ Force transition to L1

	ldr	r10, omap242x_srs_timer_32ksynct	@ get addr of counter
	ldr	r8, [r10]		@ get value
	add	r8, r8, #0x2		@ give it at most 62uS (min 31+)
volt_delay_c:
	ldr	r7, [r10]		@ get timer value
	cmp	r8, r7			@ time up?
	bhi	volt_delay_c		@ not yet->branch
	ret	lr			@ back to caller

omap242x_srs_cm_clksel2_pll:
	.word OMAP2420_CM_REGADDR(PLL_MOD, CM_CLKSEL2)
omap242x_srs_sdrc_dlla_ctrl:
	.word OMAP242X_SDRC_REGADDR(SDRC_DLLA_CTRL)
omap242x_srs_sdrc_rfr_ctrl:
	.word OMAP242X_SDRC_REGADDR(SDRC_RFR_CTRL_0)
omap242x_srs_prcm_voltctrl:
	.word OMAP2420_PRCM_VOLTCTRL
ddr_prcm_mask_val:
	.word 0xFFFF3FFC
omap242x_srs_timer_32ksynct:
	.word OMAP2_L4_IO_ADDRESS(OMAP2420_32KSYNCT_BASE + 0x010)

ENTRY(omap242x_sram_reprogram_sdrc_sz)
	.word	. - omap242x_sram_reprogram_sdrc

/*
 * Set dividers and pll. Also recalculate DLL value for DDR and unlock mode.
 */
	.align	3
ENTRY(omap242x_sram_set_prcm)
	stmfd	sp!, {r0-r12, lr}	@ regs to stack
	adr	r4, pbegin		@ addr of preload start
	adr	r8, pend		@ addr of preload end
	mcrr	p15, 1, r8, r4, c12	@ preload into icache
pbegin:
	/* move into fast relock bypass */
	ldr	r8, omap242x_ssp_pll_ctl	@ get addr
	ldr	r5, [r8]		@ get val
	mvn	r6, #0x3		@ clear mask
	and	r5, r5, r6		@ clear field
	orr	r7, r5, #0x2		@ fast relock val
	str	r7, [r8]		@ go to fast relock
	ldr	r4, omap242x_ssp_pll_stat	@ addr of stat
block:
	/* wait for bypass */
	ldr	r8, [r4]		@ stat value
	and	r8, r8, #0x3		@ mask for stat
	cmp	r8, #0x1		@ there yet
	bne	block			@ loop if not

	/* set new dpll dividers _after_ in bypass */
	ldr	r4, omap242x_ssp_pll_div	@ get addr
	str	r0, [r4]		@ set dpll ctrl val

	ldr	r4, omap242x_ssp_set_config	@ get addr
	mov	r8, #1			@ valid cfg msk
	str	r8, [r4]		@ make dividers take

	mov	r4, #100		@ dead spin a bit
wait_a_bit:
	subs	r4, r4, #1		@ dec loop
	bne	wait_a_bit		@ delay done?

	/* check if staying in bypass */
	cmp	r2, #0x1		@ stay in bypass?
	beq	pend			@ jump over dpll relock

	/* relock DPLL with new vals */
	ldr	r5, omap242x_ssp_pll_stat	@ get addr
	ldr	r4, omap242x_ssp_pll_ctl	@ get addr
	orr	r8, r7, #0x3		@ val for lock dpll
	str	r8, [r4]		@ set val
	mov	r0, #1000		@ dead spin a bit
wait_more:
	subs	r0, r0, #1		@ dec loop
	bne	wait_more		@ delay done?
wait_lock:
	ldr	r8, [r5]		@ get lock val
	and	r8, r8, #3		@ isolate field
	cmp	r8, #2			@ locked?
	bne	wait_lock		@ wait if not
pend:
	/* update memory timings & briefly lock dll */
	ldr	r4, omap242x_ssp_sdrc_rfr	@ get addr
	str	r1, [r4]		@ update refresh timing
	ldr	r11, omap242x_ssp_dlla_ctrl	@ get addr of DLLA ctrl
	ldr	r10, [r11]		@ get current val
	mvn	r9, #0x4		@ mask to get clear bit2
	and	r10, r10, r9		@ clear bit2 for lock mode
	orr	r10, r10, #0x8		@ make sure DLL on (es2 bit pos)
	str	r10, [r11]		@ commit to DLLA_CTRL
	add	r11, r11, #0x8		@ move to dllb
	str	r10, [r11]		@ hit DLLB also

	mov	r4, #0x800		@ relock time (min 0x400 L3 clocks)
wait_dll_lock:
	subs	r4, r4, #0x1
	bne	wait_dll_lock
	nop
	ldmfd	sp!, {r0-r12, pc}	@ restore regs and return

omap242x_ssp_set_config:
	.word OMAP2420_PRCM_CLKCFG_CTRL
omap242x_ssp_pll_ctl:
	.word OMAP2420_CM_REGADDR(PLL_MOD, CM_CLKEN)
omap242x_ssp_pll_stat:
	.word OMAP2420_CM_REGADDR(PLL_MOD, CM_IDLEST)
omap242x_ssp_pll_div:
	.word OMAP2420_CM_REGADDR(PLL_MOD, CM_CLKSEL1)
omap242x_ssp_sdrc_rfr:
	.word OMAP242X_SDRC_REGADDR(SDRC_RFR_CTRL_0)
omap242x_ssp_dlla_ctrl:
	.word OMAP242X_SDRC_REGADDR(SDRC_DLLA_CTRL)

ENTRY(omap242x_sram_set_prcm_sz)
	.word	. - omap242x_sram_set_prcm
