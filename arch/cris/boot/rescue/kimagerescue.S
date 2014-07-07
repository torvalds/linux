/*
 * Rescue code to be prepended on a kimage and copied to the
 * rescue serial port.
 * This is called from the rescue code, it will copy received data to
 * 4004000 and after a timeout jump to it.
 */

#define ASSEMBLER_MACROS_ONLY
#include <arch/sv_addr_ag.h>

#define CODE_START 0x40004000
#define CODE_LENGTH 784
#define TIMEOUT_VALUE 1000


#ifdef CONFIG_ETRAX_RESCUE_SER0
#define SERXOFF R_SERIAL0_XOFF
#define SERBAUD R_SERIAL0_BAUD
#define SERRECC R_SERIAL0_REC_CTRL
#define SERRDAT R_SERIAL0_REC_DATA
#define SERSTAT R_SERIAL0_STATUS
#endif
#ifdef CONFIG_ETRAX_RESCUE_SER1
#define SERXOFF R_SERIAL1_XOFF
#define SERBAUD R_SERIAL1_BAUD
#define SERRECC R_SERIAL1_REC_CTRL
#define SERRDAT R_SERIAL1_REC_DATA
#define SERSTAT R_SERIAL1_STATUS
#endif
#ifdef CONFIG_ETRAX_RESCUE_SER2
#define SERXOFF R_SERIAL2_XOFF
#define SERBAUD R_SERIAL2_BAUD
#define SERRECC R_SERIAL2_REC_CTRL
#define SERRDAT R_SERIAL2_REC_DATA
#define SERSTAT R_SERIAL2_STATUS
#endif
#ifdef CONFIG_ETRAX_RESCUE_SER3
#define SERXOFF R_SERIAL3_XOFF
#define SERBAUD R_SERIAL3_BAUD
#define SERRECC R_SERIAL3_REC_CTRL
#define SERRDAT R_SERIAL3_REC_DATA
#define SERSTAT R_SERIAL3_STATUS
#endif

	.text
	;; This is the entry point of the rescue code
	;; 0x80000000 if loaded in flash (as it should be)
	;; since etrax actually starts at address 2 when booting from flash, we
	;; put a nop (2 bytes) here first so we dont accidentally skip the di

	nop
	di
	;; setup port PA and PB default initial directions and data
	;; (so we can flash LEDs, and so that DTR and others are set)

	move.b	CONFIG_ETRAX_DEF_R_PORT_PA_DIR, $r0
	move.b	$r0, [R_PORT_PA_DIR]
	move.b	CONFIG_ETRAX_DEF_R_PORT_PA_DATA, $r0
	move.b	$r0, [R_PORT_PA_DATA]

	move.b	CONFIG_ETRAX_DEF_R_PORT_PB_DIR, $r0
	move.b	$r0, [R_PORT_PB_DIR]
	move.b	CONFIG_ETRAX_DEF_R_PORT_PB_DATA, $r0
	move.b	$r0, [R_PORT_PB_DATA]

	;; We need to setup the bus registers before we start using the DRAM
#include "../../lib/dram_init.S"

	;; Setup the stack to a suitably high address.
	;; We assume 8 MB is the minimum DRAM in an eLinux
	;; product and put the sp at the top for now.

	move.d	0x40800000, $sp

	;; setup the serial port at 115200 baud

	moveq	0, $r0
	move.d	$r0, [SERXOFF]

	move.b	0x99, $r0
	move.b	$r0, [SERBAUD]		; 115.2kbaud for both transmit
					; and receive

	move.b	0x40, $r0		; rec enable
	move.b	$r0, [SERRECC]


	moveq	0, $r1			; "timer" to clock out a LED red flash
	move.d	CODE_START, $r3		; destination counter
	move.d	CODE_LENGTH, $r4	; length
	move.d	TIMEOUT_VALUE, $r5	; "timeout" until jump

wait_ser:
	addq	1, $r1
	subq	1, $r5			; decrease timeout
	beq	jump_start		; timed out
	nop
#ifndef CONFIG_ETRAX_NO_LEDS
#ifdef CONFIG_ETRAX_PA_LEDS
	move.b	CONFIG_ETRAX_DEF_R_PORT_PA_DATA, $r2
#endif
#ifdef CONFIG_ETRAX_PB_LEDS
	move.b	CONFIG_ETRAX_DEF_R_PORT_PB_DATA, $r2
#endif
	move.d	(1 << CONFIG_ETRAX_LED1R) | (1 << CONFIG_ETRAX_LED2R), $r0
	btstq	16, $r1
	bpl	1f
	nop
	or.d	$r0, $r2		; set bit
	ba	2f
	nop
1:	not	$r0			; clear bit
	and.d	$r0, $r2
2:
#ifdef CONFIG_ETRAX_PA_LEDS
	move.b	$r2, [R_PORT_PA_DATA]
#endif
#ifdef CONFIG_ETRAX_PB_LEDS
	move.b	$r2, [R_PORT_PB_DATA]
#endif
#endif

	;; check if we got something on the serial port

	move.b	[SERSTAT], $r0
	btstq	0, $r0			; data_avail
	bpl	wait_ser
	nop

	;; got something - copy the byte and loop

	move.b	[SERRDAT], $r0
	move.b	$r0, [$r3+]
	move.d	TIMEOUT_VALUE, $r5	; reset "timeout"
	subq	1, $r4			; decrease length
	bne	wait_ser
	nop
jump_start:
	;; jump into downloaded code

	jump	CODE_START
