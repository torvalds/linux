/* $Id: dram_init.S,v 1.4 2003/09/22 09:21:59 starvik Exp $
 * 
 * DRAM/SDRAM initialization - alter with care
 * This file is intended to be included from other assembler files
 *
 * Note: This file may not modify r9 because r9 is used to carry
 *       information from the decompresser to the kernel
 *
 * Copyright (C) 2000, 2001 Axis Communications AB
 *
 * Authors:  Mikael Starvik (starvik@axis.com)	
 * 
 * $Log: dram_init.S,v $
 * Revision 1.4  2003/09/22 09:21:59  starvik
 * Decompresser is linked to 0x407xxxxx and sdram commands are at 0x000xxxxx
 * so we need to mask off 12 bits.
 *
 * Revision 1.3  2003/03/31 09:38:37  starvik
 * Corrected calculation of end of sdram init commands
 *
 * Revision 1.2  2002/11/19 13:33:29  starvik
 * Changes from Linux 2.4
 *
 * Revision 1.13  2002/10/30 07:42:28  starvik
 * Always read SDRAM command sequence from flash
 *
 * Revision 1.12  2002/08/09 11:37:37  orjanf
 * Added double initialization work-around for Samsung SDRAMs.
 *
 * Revision 1.11  2002/06/04 11:43:21  starvik
 * Check if mrs_data is specified in kernelconfig (necessary for MCM)
 *
 * Revision 1.10  2001/10/04 12:00:21  martinnn
 * Added missing underscores.
 *
 * Revision 1.9  2001/10/01 14:47:35  bjornw
 * Added register prefixes and removed underscores
 *
 * Revision 1.8  2001/05/15 07:12:45  hp
 * Copy warning from head.S about r8 and r9
 *
 * Revision 1.7  2001/04/18 12:05:39  bjornw
 * Fixed comments, and explicitely include config.h to be sure its there
 *
 * Revision 1.6  2001/04/10 06:20:16  starvik
 * Delay should be 200us, not 200ns
 *
 * Revision 1.5  2001/04/09 06:01:13  starvik
 * Added support for 100 MHz SDRAMs
 *
 * Revision 1.4  2001/03/26 14:24:01  bjornw
 * Namechange of some config options
 *
 * Revision 1.3  2001/03/23 08:29:41  starvik
 * Corrected calculation of mrs_data
 *
 * Revision 1.2  2001/02/08 15:20:00  starvik
 * Corrected SDRAM initialization
 * Should now be included as inline
 *
 * Revision 1.1  2001/01/29 13:08:02  starvik
 * Initial version
 * This file should be included from all assembler files that needs to
 * initialize DRAM/SDRAM.
 *
 */

/* Just to be certain the config file is included, we include it here
 * explicitely instead of depending on it being included in the file that
 * uses this code.
 */


	;; WARNING! The registers r8 and r9 are used as parameters carrying
	;; information from the decompressor (if the kernel was compressed). 
	;; They should not be used in the code below.

#ifndef CONFIG_SVINTO_SIM	
	move.d   CONFIG_ETRAX_DEF_R_WAITSTATES, $r0
	move.d   $r0, [R_WAITSTATES]

	move.d   CONFIG_ETRAX_DEF_R_BUS_CONFIG, $r0
	move.d   $r0, [R_BUS_CONFIG]
	
#ifndef CONFIG_ETRAX_SDRAM
	move.d   CONFIG_ETRAX_DEF_R_DRAM_CONFIG, $r0
	move.d   $r0, [R_DRAM_CONFIG]

	move.d   CONFIG_ETRAX_DEF_R_DRAM_TIMING, $r0
	move.d   $r0, [R_DRAM_TIMING]
#else
	;; Samsung SDRAMs seem to require to be initialized twice to work properly.
	moveq    2, $r6	
_sdram_init:
	
	; Refer to ETRAX 100LX Designers Reference for a description of SDRAM initialization
	
	; Bank configuration
	move.d   CONFIG_ETRAX_DEF_R_SDRAM_CONFIG, $r0
	move.d   $r0, [R_SDRAM_CONFIG]

	; Calculate value of mrs_data 
	; CAS latency = 2 && bus_width = 32 => 0x40
	; CAS latency = 3 && bus_width = 32 => 0x60
	; CAS latency = 2 && bus_width = 16 => 0x20
	; CAS latency = 3 && bus_width = 16 => 0x30

	; Check if value is already supplied in kernel config
	move.d   CONFIG_ETRAX_DEF_R_SDRAM_TIMING, $r2
	and.d    0x00ff0000, $r2
	bne	 _set_timing
	lsrq     16, $r2
	
	move.d   0x40, $r2       ; Assume 32 bits and CAS latency = 2
	move.d   CONFIG_ETRAX_DEF_R_SDRAM_TIMING, $r1
	move.d   $r1, $r3
 	and.d    0x03, $r1       ; Get CAS latency
	and.d    0x1000, $r3     ; 50 or 100 MHz?
	beq      _speed_50
	nop
_speed_100:		
	cmp.d    0x00, $r1	; CAS latency = 2?
	beq      _bw_check
	nop
	or.d     0x20, $r2	; CAS latency = 3 
	ba       _bw_check
	nop
_speed_50:			
	cmp.d    0x01, $r1	; CAS latency = 2?
	beq      _bw_check
	nop
	or.d     0x20, $r2       ; CAS latency = 3
_bw_check:
	move.d   CONFIG_ETRAX_DEF_R_SDRAM_CONFIG, $r1
	and.d    0x800000, $r1	; DRAM width is bit 23
	bne      _set_timing
	nop
	lsrq     1, $r2		;  16 bits. Shift down value.

	; Set timing parameters. Starts master clock
_set_timing:
	move.d   CONFIG_ETRAX_DEF_R_SDRAM_TIMING, $r1
	and.d    0x8000f9ff, $r1 ; Make sure mrs data and command is 0 
	or.d     0x80000000, $r1	; Make sure sdram enable bit is set
	move.d   $r1, $r5
	or.d     0x0000c000, $r1 ; ref = disable
	lslq     16, $r2		; mrs data starts at bit 16
	or.d     $r2, $r1 
	move.d   $r1, [R_SDRAM_TIMING]	
		
	; Wait 200us
	move.d   10000, $r2
1:	bne      1b
	subq     1, $r2
	
	; Issue initialization command sequence
	move.d   _sdram_commands_start, $r2
	and.d    0x000fffff, $r2 ; Make sure commands are read from flash
	move.d   _sdram_commands_end,  $r3
	and.d    0x000fffff, $r3
1:	clear.d  $r4
	move.b   [$r2+], $r4
	lslq     9, $r4	; Command starts at bit 9
	or.d     $r1, $r4
	move.d   $r4, [R_SDRAM_TIMING]
	nop		; Wait five nop cycles between each command
	nop
	nop
	nop
	nop
	cmp.d    $r2, $r3
	bne      1b
	nop
	move.d   $r5, [R_SDRAM_TIMING]
	subq     1, $r6
	bne      _sdram_init
	nop
	ba       _sdram_commands_end
	nop

_sdram_commands_start:
	.byte   3	; Precharge
	.byte   0       ; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   1	; mrs
	.byte   0	; nop 
_sdram_commands_end:		
#endif
#endif
