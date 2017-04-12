/*
 * DDR SDRAM initialization - alter with care
 * This file is intended to be included from other assembler files
 *
 * Note: This file may not modify r8 or r9 because they are used to
 * carry information from the decompressor to the kernel
 *
 * Copyright (C) 2005-2007 Axis Communications AB
 *
 * Authors:  Mikael Starvik <starvik@axis.com>
 */

/* Just to be certain the config file is included, we include it here
 * explicitly instead of depending on it being included in the file that
 * uses this code.
 */

#include <hwregs/asm/reg_map_asm.h>
#include <hwregs/asm/ddr2_defs_asm.h>

	;; WARNING! The registers r8 and r9 are used as parameters carrying
	;; information from the decompressor (if the kernel was compressed).
	;; They should not be used in the code below.

	;; Refer to ddr2 MDS for initialization sequence

	; 2. Wait 200us
	move.d   10000, $r2
1:	bne      1b
	subq     1, $r2

	; Start clock
	move.d   REG_ADDR(ddr2, regi_ddr2_ctrl, rw_phy_cfg), $r0
	move.d   REG_STATE(ddr2, rw_phy_cfg, en, yes), $r1
	move.d   $r1, [$r0]

	; 2. Wait 200us
	move.d   10000, $r2
1:	bne      1b
	subq     1, $r2

	; Reset phy and start calibration
	move.d   REG_ADDR(ddr2, regi_ddr2_ctrl, rw_phy_ctrl), $r0
	move.d   REG_STATE(ddr2, rw_phy_ctrl, rst, yes) | \
		 REG_STATE(ddr2, rw_phy_ctrl, cal_rst, yes), $r1
	move.d   $r1, [$r0]
	move.d	 REG_STATE(ddr2, rw_phy_ctrl, cal_start, yes), $r1
	move.d   $r1, [$r0]

	; 2. Wait 200us
	move.d   10000, $r2
1:	bne      1b
	subq     1, $r2

	; Issue commands
	move.d   REG_ADDR(ddr2, regi_ddr2_ctrl, rw_ctrl), $r0
	move.d   sdram_commands_start, $r2
command_loop:
	movu.b  [$r2+], $r1
	movu.w  [$r2+], $r3
do_cmd:
	lslq     16, $r1
	or.d     $r3, $r1
	move.d   $r1, [$r0]
	; 2. Wait 200us
	move.d   10000, $r4
1:	bne      1b
	subq     1, $r4
	cmp.d    sdram_commands_end, $r2
	blo      command_loop
	nop

	; Set timing
	move.d   REG_ADDR(ddr2, regi_ddr2_ctrl, rw_timing), $r0
	move.d   CONFIG_ETRAX_DDR2_TIMING, $r1
	move.d   $r1, [$r0]

	; Set latency
	move.d   REG_ADDR(ddr2, regi_ddr2_ctrl, rw_latency), $r0
	move.d   CONFIG_ETRAX_DDR2_LATENCY, $r1
	move.d   $r1, [$r0]

	; Set configuration
	move.d   REG_ADDR(ddr2, regi_ddr2_ctrl, rw_cfg), $r0
	move.d   CONFIG_ETRAX_DDR2_CONFIG, $r1
	move.d   $r1, [$r0]

	ba after_sdram_commands
	nop

sdram_commands_start:
	.byte regk_ddr2_deselect
	.word 0
	.byte regk_ddr2_pre
	.word regk_ddr2_pre_all
	.byte regk_ddr2_emrs2
	.word 0
	.byte regk_ddr2_emrs3
	.word 0
	.byte regk_ddr2_emrs
	.word regk_ddr2_dll_en
	.byte regk_ddr2_mrs
	.word regk_ddr2_dll_rst
	.byte regk_ddr2_pre
	.word regk_ddr2_pre_all
	.byte regk_ddr2_ref
	.word 0
	.byte regk_ddr2_ref
	.word 0
	.byte regk_ddr2_mrs
	.word CONFIG_ETRAX_DDR2_MRS & 0xffff
	.byte regk_ddr2_emrs
	.word regk_ddr2_ocd_default | regk_ddr2_dll_en
	.byte regk_ddr2_emrs
	.word regk_ddr2_ocd_exit | regk_ddr2_dll_en | (CONFIG_ETRAX_DDR2_MRS >> 16)
sdram_commands_end:
	.align 1
after_sdram_commands:
