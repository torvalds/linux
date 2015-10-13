/*
 * Copyright (C) 2014-2015 Altera Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

#define MAX_LOOP_COUNT		1000

/* Register offset */
#define SDR_CTRLGRP_LOWPWREQ_ADDR       0x54
#define SDR_CTRLGRP_LOWPWRACK_ADDR      0x58

/* Bitfield positions */
#define SELFRSHREQ_POS                  3
#define SELFRSHREQ_MASK                 0x8

#define SELFRFSHACK_POS                 1
#define SELFRFSHACK_MASK                0x2

	/*
	 * This code assumes that when the bootloader configured
	 * the sdram controller for the DDR on the board it
	 * configured the following fields depending on the DDR
	 * vendor/configuration:
	 *
	 * sdr.ctrlcfg.lowpwreq.selfrfshmask
	 * sdr.ctrlcfg.lowpwrtiming.clkdisablecycles
	 * sdr.ctrlcfg.dramtiming4.selfrfshexit
	 */

	.arch   armv7-a
	.text
	.align 3

	/*
	 * socfpga_sdram_self_refresh
	 *
	 *  r0 : sdr_ctl_base_addr
	 *  r1 : temp storage of return value
	 *  r2 : temp storage of register values
	 *  r3 : loop counter
	 *
	 *  return value: lower 16 bits: loop count going into self refresh
	 *                upper 16 bits: loop count exiting self refresh
	 */
ENTRY(socfpga_sdram_self_refresh)
	/* Enable dynamic clock gating in the Power Control Register. */
	mrc	p15, 0, r2, c15, c0, 0
	orr	r2, r2, #1
	mcr	p15, 0, r2, c15, c0, 0

	/* Enable self refresh: set sdr.ctrlgrp.lowpwreq.selfrshreq = 1 */
	ldr	r2, [r0, #SDR_CTRLGRP_LOWPWREQ_ADDR]
	orr	r2, r2, #SELFRSHREQ_MASK
	str	r2, [r0, #SDR_CTRLGRP_LOWPWREQ_ADDR]

	/* Poll until sdr.ctrlgrp.lowpwrack.selfrfshack == 1 or hit max loops */
	mov	r3, #0
while_ack_0:
	ldr	r2, [r0, #SDR_CTRLGRP_LOWPWRACK_ADDR]
	and	r2, r2, #SELFRFSHACK_MASK
	cmp	r2, #SELFRFSHACK_MASK
	beq	ack_1

	add	r3, #1
	cmp	r3, #MAX_LOOP_COUNT
	bne	while_ack_0

ack_1:
	mov	r1, r3

	/*
	 * Execute an ISB instruction to ensure that all of the
	 * CP15 register changes have been committed.
	 */
	isb

	/*
	 * Execute a barrier instruction to ensure that all cache,
	 * TLB and branch predictor maintenance operations issued
	 * by any CPU in the cluster have completed.
	 */
	dsb
	dmb

	wfi

	/* Disable self-refresh: set sdr.ctrlgrp.lowpwreq.selfrshreq = 0 */
	ldr	r2, [r0, #SDR_CTRLGRP_LOWPWREQ_ADDR]
	bic	r2, r2, #SELFRSHREQ_MASK
	str	r2, [r0, #SDR_CTRLGRP_LOWPWREQ_ADDR]

	/* Poll until sdr.ctrlgrp.lowpwrack.selfrfshack == 0 or hit max loops */
	mov	r3, #0
while_ack_1:
	ldr	r2, [r0, #SDR_CTRLGRP_LOWPWRACK_ADDR]
	and	r2, r2, #SELFRFSHACK_MASK
	cmp	r2, #SELFRFSHACK_MASK
	bne	ack_0

	add	r3, #1
	cmp	r3, #MAX_LOOP_COUNT
	bne	while_ack_1

ack_0:
	/*
	 * Prepare return value:
	 * Shift loop count for exiting self refresh into upper 16 bits.
	 * Leave loop count for requesting self refresh in lower 16 bits.
	 */
	mov	r3, r3, lsl #16
	add	r1, r1, r3

	/* Disable dynamic clock gating in the Power Control Register. */
	mrc	p15, 0, r2, c15, c0, 0
	bic	r2, r2, #1
	mcr	p15, 0, r2, c15, c0, 0

	mov     r0, r1                  @ return value
	bx	lr			@ return

ENDPROC(socfpga_sdram_self_refresh)
ENTRY(socfpga_sdram_self_refresh_sz)
	.word	. - socfpga_sdram_self_refresh
