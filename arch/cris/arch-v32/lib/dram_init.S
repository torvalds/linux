/* $Id: dram_init.S,v 1.4 2005/04/24 18:48:32 starvik Exp $
 *
 * DRAM/SDRAM initialization - alter with care
 * This file is intended to be included from other assembler files
 *
 * Note: This file may not modify r8 or r9 because they are used to
 * carry information from the decompresser to the kernel
 *
 * Copyright (C) 2000-2003 Axis Communications AB
 *
 * Authors:  Mikael Starvik (starvik@axis.com)
 */

/* Just to be certain the config file is included, we include it here
 * explicitely instead of depending on it being included in the file that
 * uses this code.
 */

#include <linux/config.h>
#include <asm/arch/hwregs/asm/reg_map_asm.h>
#include <asm/arch/hwregs/asm/bif_core_defs_asm.h>

	;; WARNING! The registers r8 and r9 are used as parameters carrying
	;; information from the decompressor (if the kernel was compressed).
	;; They should not be used in the code below.

	; Refer to BIF MDS for a description of SDRAM initialization

	; Bank configuration
	move.d   REG_ADDR(bif_core, regi_bif_core, rw_sdram_cfg_grp0), $r0
	move.d   CONFIG_ETRAX_SDRAM_GRP0_CONFIG, $r1
	move.d   $r1, [$r0]
	move.d   REG_ADDR(bif_core, regi_bif_core, rw_sdram_cfg_grp1), $r0
	move.d   CONFIG_ETRAX_SDRAM_GRP1_CONFIG, $r1
	move.d   $r1, [$r0]

	; Calculate value of mrs_data
	; CAS latency = 2 && bus_width = 32 => 0x40
	; CAS latency = 3 && bus_width = 32 => 0x60
	; CAS latency = 2 && bus_width = 16 => 0x20
	; CAS latency = 3 && bus_width = 16 => 0x30

	; Check if value is already supplied in kernel config
	move.d   CONFIG_ETRAX_SDRAM_COMMAND, $r2
	bne	 _set_timing
	nop

	move.d   0x40, $r4       ; Assume 32 bits and CAS latency = 2
	move.d   CONFIG_ETRAX_SDRAM_TIMING, $r1
 	and.d    0x07, $r1       ; Get CAS latency
	cmpq	 2, $r1		 ; CL = 2 ?
	beq	 _bw_check
	nop
	move.d   0x60, $r4

_bw_check:
	; Assume that group 0 width is equal to group 1. This assumption
	; is wrong for a group 1 only hardware (such as the grand old
	; StorPoint+).
	move.d   CONFIG_ETRAX_SDRAM_GRP0_CONFIG, $r1
	and.d    0x200, $r1	; DRAM width is bit 9
	beq      _set_timing
	lslq	 2, $r4		;  mrs_data starts at bit 2
	lsrq     1, $r4		;  16 bits. Shift down value.

	; Set timing parameters (refresh off to avoid Guinness TR 83)
_set_timing:
	move.d   CONFIG_ETRAX_SDRAM_TIMING, $r1
	and.d    ~(3 << reg_bif_core_rw_sdram_timing___ref___lsb), $r1
	move.d   REG_ADDR(bif_core, regi_bif_core, rw_sdram_timing), $r0
	move.d   $r1, [$r0]

	; Issue NOP command
	move.d REG_ADDR(bif_core, regi_bif_core, rw_sdram_cmd), $r5
	moveq regk_bif_core_nop, $r1
	move.d $r1, [$r5]

	; Wait 200us
	move.d   10000, $r2
1:	bne      1b
	subq     1, $r2

	; Issue initialization command sequence
	move.d   _sdram_commands_start, $r2
	and.d    0x000fffff, $r2 ; Make sure commands are read from flash
	move.d   _sdram_commands_end,  $r3
	and.d    0x000fffff, $r3
1:	clear.d  $r6
	move.b   [$r2+], $r6 	; Load command
	or.d     $r4, $r6	; Add calculated mrs
	move.d   $r6, [$r5]	; Write rw_sdram_cmd
	; Wait 80 ns between each command
	move.d	 4000, $r7
2:	bne	 2b
	subq	 1, $r7
	cmp.d    $r2, $r3	; Last command?
	bne      1b
	nop

	; Start refresh
	move.d   CONFIG_ETRAX_SDRAM_TIMING, $r1
	move.d   REG_ADDR(bif_core, regi_bif_core, rw_sdram_timing), $r0
	move.d   $r1, [$r0]

	; Initialization finished
	ba       _sdram_commands_end
	nop

_sdram_commands_start:
	.byte   regk_bif_core_pre ; Precharge
	.byte   regk_bif_core_ref ; refresh
	.byte   regk_bif_core_ref ; refresh
	.byte   regk_bif_core_ref ; refresh
	.byte   regk_bif_core_ref ; refresh
	.byte   regk_bif_core_ref ; refresh
	.byte   regk_bif_core_ref ; refresh
	.byte   regk_bif_core_ref ; refresh
	.byte   regk_bif_core_ref ; refresh
	.byte   regk_bif_core_mrs ; mrs
_sdram_commands_end:
