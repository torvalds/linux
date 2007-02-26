
/*    low-level asm for "intrigue" (PA8500-8700 CPU perf counters)
 * 
 *    Copyright (C) 2001 Randolph Chung <tausq at parisc-linux.org>
 *    Copyright (C) 2001 Hewlett-Packard (Grant Grundler)
 * 
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 * 
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 * 
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <asm/assembly.h>
#include <linux/linkage.h>

#ifdef CONFIG_64BIT
	.level		2.0w
#endif /* CONFIG_64BIT */

#define MTDIAG_1(gr)    .word 0x14201840 + gr*0x10000
#define MTDIAG_2(gr)    .word 0x14401840 + gr*0x10000
#define MFDIAG_1(gr)    .word 0x142008A0 + gr
#define MFDIAG_2(gr)    .word 0x144008A0 + gr
#define STDIAG(dr)      .word 0x14000AA0 + dr*0x200000
#define SFDIAG(dr)      .word 0x14000BA0 + dr*0x200000
#define DR2_SLOW_RET    53


;
; Enable the performance counters
;
; The coprocessor only needs to be enabled when
; starting/stopping the coprocessor with the pmenb/pmdis.
;
	.text

ENTRY(perf_intrigue_enable_perf_counters)
	.proc
	.callinfo  frame=0,NO_CALLS
	.entry

	ldi     0x20,%r25                ; load up perfmon bit
	mfctl   ccr,%r26                 ; get coprocessor register
	or      %r25,%r26,%r26             ; set bit
	mtctl   %r26,ccr                 ; turn on performance coprocessor
	pmenb                           ; enable performance monitor
	ssm     0,0                     ; dummy op to ensure completion
	sync                            ; follow ERS
	andcm   %r26,%r25,%r26             ; clear bit now 
	mtctl   %r26,ccr                 ; turn off performance coprocessor
	nop                             ; NOPs as specified in ERS
	nop
	nop
	nop
	nop
	nop
	nop
	bve    (%r2)
	nop
	.exit
	.procend
ENDPROC(perf_intrigue_enable_perf_counters)

ENTRY(perf_intrigue_disable_perf_counters)
	.proc
	.callinfo  frame=0,NO_CALLS
	.entry
	ldi     0x20,%r25                ; load up perfmon bit
	mfctl   ccr,%r26                 ; get coprocessor register
	or      %r25,%r26,%r26             ; set bit
	mtctl   %r26,ccr                 ; turn on performance coprocessor
	pmdis                           ; disable performance monitor
	ssm     0,0                     ; dummy op to ensure completion
	andcm   %r26,%r25,%r26             ; clear bit now 
	bve    (%r2)
	mtctl   %r26,ccr                 ; turn off performance coprocessor
	.exit
	.procend
ENDPROC(perf_intrigue_disable_perf_counters)

;***********************************************************************
;*
;* Name: perf_rdr_shift_in_W
;*
;* Description:
;*	This routine shifts data in from the RDR in arg0 and returns
;*	the result in ret0.  If the RDR is <= 64 bits in length, it
;*	is shifted shifted backup immediately.  This is to compensate
;*	for RDR10 which has bits that preclude PDC stack operations
;*	when they are in the wrong state.
;*
;* Arguments:
;*	arg0 : rdr to be read
;*	arg1 : bit length of rdr
;*
;* Returns:
;*	ret0 = next 64 bits of rdr data from staging register
;*
;* Register usage:
;*	arg0 : rdr to be read
;*	arg1 : bit length of rdr
;*	%r24  - original DR2 value
;*	%r1   - scratch
;*  %r29  - scratch
;*
;* Returns:
;*	ret0 = RDR data (right justified)
;*
;***********************************************************************

ENTRY(perf_rdr_shift_in_W)
	.proc
	.callinfo frame=0,NO_CALLS
	.entry
;
; read(shift in) the RDR.
;

; NOTE: The PCX-W ERS states that DR2_SLOW_RET must be set before any
; shifting is done, from or to, remote diagnose registers.
;

	depdi,z		1,DR2_SLOW_RET,1,%r29
	MFDIAG_2	(24)
	or		    %r24,%r29,%r29
	MTDIAG_2	(29)			; set DR2_SLOW_RET

	nop
	nop
	nop
	nop

;
; Cacheline start (32-byte cacheline)
;
	nop
	nop
	nop
	extrd,u		arg1,63,6,%r1	; setup shift amount by bits to move 

	mtsar		%r1
	shladd		arg0,2,%r0,%r1	; %r1 = 4 * RDR number
	blr  		%r1,%r0		; branch to 8-instruction sequence
	nop

;
; Cacheline start (32-byte cacheline)
;

	;
	; RDR 0 sequence
	;
	SFDIAG		(0)
	ssm		    0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)			; mtdiag %dr1, %r1 
	STDIAG		(0)
	ssm		    0,0
	b,n         perf_rdr_shift_in_W_leave

	;
	; RDR 1 sequence
	;
	sync
	ssm		    0,0
	SFDIAG		(1)
	ssm		    0,0
	MFDIAG_1	(28)
	ssm		    0,0
	b,n         perf_rdr_shift_in_W_leave
	nop

	;
	; RDR 2 read sequence
	;
	SFDIAG		(2)
	ssm		    0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(2)
	ssm		    0,0
	b,n         perf_rdr_shift_in_W_leave

	;
	; RDR 3 read sequence
	;
	b,n         perf_rdr_shift_in_W_leave
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	;
	; RDR 4 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(4)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_W_leave
	ssm		0,0
	nop

	; 
	; RDR 5 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(5)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_W_leave
	ssm		0,0
	nop

	;
	; RDR 6 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(6)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_W_leave
	ssm		0,0
	nop

	;
	; RDR 7 read sequence
	;
	b,n         perf_rdr_shift_in_W_leave
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	;
	; RDR 8 read sequence
	;
	b,n         perf_rdr_shift_in_W_leave
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	;
	; RDR 9 read sequence
	;
	b,n         perf_rdr_shift_in_W_leave
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	;
	; RDR 10 read sequence
	;
	SFDIAG		(10)
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(10)
	ssm		0,0
	b,n         perf_rdr_shift_in_W_leave

	;
	; RDR 11 read sequence
	;
	SFDIAG		(11)
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(11)
	ssm		0,0
	b,n         perf_rdr_shift_in_W_leave

	;
	; RDR 12 read sequence
	;
	b,n         perf_rdr_shift_in_W_leave
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	;
	; RDR 13 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(13)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_W_leave
	ssm		0,0
	nop

	;
	; RDR 14 read sequence
	;
	SFDIAG		(14)
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(14)
	ssm		0,0
	b,n         perf_rdr_shift_in_W_leave

	;
	; RDR 15 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(15)
	ssm		0,0
	MFDIAG_1	(28)
	ssm		0,0
	b,n         perf_rdr_shift_in_W_leave
	nop

	;
	; RDR 16 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(16)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_W_leave
	ssm		0,0
	nop

	;
	; RDR 17 read sequence
	;
	SFDIAG		(17)
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(17)
	ssm		0,0
	b,n         perf_rdr_shift_in_W_leave

	;
	; RDR 18 read sequence
	;
	SFDIAG		(18)
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(18)
	ssm		0,0
	b,n         perf_rdr_shift_in_W_leave

	;
	; RDR 19 read sequence
	;
	b,n         perf_rdr_shift_in_W_leave
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	;
	; RDR 20 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(20)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_W_leave
	ssm		0,0
	nop

	;
	; RDR 21 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(21)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_W_leave
	ssm		0,0
	nop

	;
	; RDR 22 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(22)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_W_leave
	ssm		0,0
	nop

	;
	; RDR 23 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(23)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_W_leave
	ssm		0,0
	nop

	;
	; RDR 24 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(24)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_W_leave
	ssm		0,0
	nop

	;
	; RDR 25 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(25)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_W_leave
	ssm		0,0
	nop

	;
	; RDR 26 read sequence
	;
	SFDIAG		(26)
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(26)
	ssm		0,0
	b,n         perf_rdr_shift_in_W_leave

	;
	; RDR 27 read sequence
	;
	SFDIAG		(27)
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(27)
	ssm		0,0
	b,n         perf_rdr_shift_in_W_leave

	;
	; RDR 28 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(28)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_W_leave
	ssm		0,0
	nop

	;
	; RDR 29 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(29)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_W_leave
	ssm		0,0
	nop

	;
	; RDR 30 read sequence
	;
	SFDIAG		(30)
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(30)
	ssm		0,0
	b,n         perf_rdr_shift_in_W_leave

	;
	; RDR 31 read sequence
	;
	sync
	ssm		0,0
	SFDIAG		(31)
	ssm		0,0
	MFDIAG_1	(28)
	nop
	ssm		0,0
	nop

	;
	; Fallthrough
	;

perf_rdr_shift_in_W_leave:
	bve		    (%r2)
	.exit
	MTDIAG_2	(24)			; restore DR2
	.procend
ENDPROC(perf_rdr_shift_in_W)


;***********************************************************************
;*
;* Name: perf_rdr_shift_out_W
;*
;* Description:
;*	This routine moves data to the RDR's.  The double-word that
;*	arg1 points to is loaded and moved into the staging register.
;*	Then the STDIAG instruction for the RDR # in arg0 is called
;*	to move the data to the RDR.
;*
;* Arguments:
;*	arg0 = rdr number
;*	arg1 = 64-bit value to write
;*	%r24 - DR2 | DR2_SLOW_RET
;*	%r23 - original DR2 value
;*
;* Returns:
;*	None
;*
;* Register usage:
;*
;***********************************************************************

ENTRY(perf_rdr_shift_out_W)
	.proc
	.callinfo frame=0,NO_CALLS
	.entry
;
; NOTE: The PCX-W ERS states that DR2_SLOW_RET must be set before any
; shifting is done, from or to, the remote diagnose registers.
;

	depdi,z		1,DR2_SLOW_RET,1,%r24
	MFDIAG_2	(23)
	or		%r24,%r23,%r24
	MTDIAG_2	(24)		; set DR2_SLOW_RET
	MTDIAG_1	(25)		; data to the staging register
	shladd		arg0,2,%r0,%r1	; %r1 = 4 * RDR number
	blr		    %r1,%r0	; branch to 8-instruction sequence
	nop

	;
	; RDR 0 write sequence
	;
	sync				; RDR 0 write sequence
	ssm		0,0
	STDIAG		(0)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 1 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(1)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 2 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(2)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 3 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(3)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 4 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(4)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 5 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(5)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 6 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(6)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 7 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(7)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 8 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(8)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 9 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(9)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 10 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(10)
	STDIAG		(26)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	ssm		0,0
	nop

	;
	; RDR 11 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(11)
	STDIAG		(27)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	ssm		0,0
	nop

	;
	; RDR 12 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(12)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 13 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(13)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 14 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(14)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 15 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(15)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 16 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(16)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 17 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(17)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 18 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(18)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 19 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(19)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 20 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(20)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 21 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(21)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 22 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(22)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 23 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(23)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 24 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(24)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 25 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(25)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 26 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(10)
	STDIAG		(26)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	ssm		0,0
	nop

	;
	; RDR 27 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(11)
	STDIAG		(27)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	ssm		0,0
	nop

	;
	; RDR 28 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(28)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 29 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(29)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 30 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(30)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

	;
	; RDR 31 write sequence
	;
	sync
	ssm		0,0
	STDIAG		(31)
	ssm		0,0
	b,n         perf_rdr_shift_out_W_leave
	nop
	ssm		0,0
	nop

perf_rdr_shift_out_W_leave:
	bve		(%r2)
	.exit
	MTDIAG_2	(23)			; restore DR2
	.procend
ENDPROC(perf_rdr_shift_out_W)


;***********************************************************************
;*
;* Name: rdr_shift_in_U
;*
;* Description:
;*	This routine shifts data in from the RDR in arg0 and returns
;*	the result in ret0.  If the RDR is <= 64 bits in length, it
;*	is shifted shifted backup immediately.  This is to compensate
;*	for RDR10 which has bits that preclude PDC stack operations
;*	when they are in the wrong state.
;*
;* Arguments:
;*	arg0 : rdr to be read
;*	arg1 : bit length of rdr
;*
;* Returns:
;*	ret0 = next 64 bits of rdr data from staging register
;*
;* Register usage:
;*	arg0 : rdr to be read						                        
;*	arg1 : bit length of rdr					                        
;*	%r24 - original DR2 value
;*	%r23 - DR2 | DR2_SLOW_RET
;*	%r1  - scratch
;*
;***********************************************************************

ENTRY(perf_rdr_shift_in_U)
	.proc
	.callinfo frame=0,NO_CALLS
	.entry

; read(shift in) the RDR.
;
; NOTE: The PCX-U ERS states that DR2_SLOW_RET must be set before any
; shifting is done, from or to, remote diagnose registers.

	depdi,z		1,DR2_SLOW_RET,1,%r29
	MFDIAG_2	(24)
	or			%r24,%r29,%r29
	MTDIAG_2	(29)			; set DR2_SLOW_RET

	nop
	nop
	nop
	nop

;
; Start of next 32-byte cacheline
;
	nop
	nop
	nop
	extrd,u		arg1,63,6,%r1

	mtsar		%r1
	shladd		arg0,2,%r0,%r1	; %r1 = 4 * RDR number
	blr 		%r1,%r0		; branch to 8-instruction sequence
	nop

;
; Start of next 32-byte cacheline
;
	SFDIAG		(0)		; RDR 0 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(0)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave

	SFDIAG		(1)		; RDR 1 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(1)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave

	sync				; RDR 2 read sequence
	ssm		0,0
	SFDIAG		(4)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_U_leave
	ssm		0,0
	nop

	sync				; RDR 3 read sequence
	ssm		0,0
	SFDIAG		(3)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_U_leave
	ssm		0,0
	nop

	sync				; RDR 4 read sequence
	ssm		0,0
	SFDIAG		(4)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_U_leave
	ssm		0,0
	nop

	sync				; RDR 5 read sequence
	ssm		0,0
	SFDIAG		(5)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_U_leave
	ssm		0,0
	nop

	sync				; RDR 6 read sequence
	ssm		0,0
	SFDIAG		(6)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_U_leave
	ssm		0,0
	nop

	sync				; RDR 7 read sequence
	ssm		0,0
	SFDIAG		(7)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_U_leave
	ssm		0,0
	nop

	b,n         perf_rdr_shift_in_U_leave
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	SFDIAG		(9)		; RDR 9 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(9)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave

	SFDIAG		(10)		; RDR 10 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(10)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave

	SFDIAG		(11)		; RDR 11 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(11)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave

	SFDIAG		(12)		; RDR 12 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(12)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave

	SFDIAG		(13)		; RDR 13 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(13)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave

	SFDIAG		(14)		; RDR 14 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(14)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave

	SFDIAG		(15)		; RDR 15 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(15)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave

	sync				; RDR 16 read sequence
	ssm		0,0
	SFDIAG		(16)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_U_leave
	ssm		0,0
	nop

	SFDIAG		(17)		; RDR 17 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(17)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave

	SFDIAG		(18)		; RDR 18 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(18)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave

	b,n         perf_rdr_shift_in_U_leave
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	sync				; RDR 20 read sequence
	ssm		0,0
	SFDIAG		(20)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_U_leave
	ssm		0,0
	nop

	sync				; RDR 21 read sequence
	ssm		0,0
	SFDIAG		(21)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_U_leave
	ssm		0,0
	nop

	sync				; RDR 22 read sequence
	ssm		0,0
	SFDIAG		(22)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_U_leave
	ssm		0,0
	nop

	sync				; RDR 23 read sequence
	ssm		0,0
	SFDIAG		(23)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_U_leave
	ssm		0,0
	nop

	sync				; RDR 24 read sequence
	ssm		0,0
	SFDIAG		(24)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_U_leave
	ssm		0,0
	nop

	sync				; RDR 25 read sequence
	ssm		0,0
	SFDIAG		(25)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_U_leave
	ssm		0,0
	nop

	SFDIAG		(26)		; RDR 26 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(26)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave

	SFDIAG		(27)		; RDR 27 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(27)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave

	sync				; RDR 28 read sequence
	ssm		0,0
	SFDIAG		(28)
	ssm		0,0
	MFDIAG_1	(28)
	b,n         perf_rdr_shift_in_U_leave
	ssm		0,0
	nop

	b,n         perf_rdr_shift_in_U_leave
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	SFDIAG		(30)		; RDR 30 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(30)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave

	SFDIAG		(31)		; RDR 31 read sequence
	ssm		0,0
	MFDIAG_1	(28)
	shrpd		ret0,%r0,%sar,%r1
	MTDIAG_1	(1)
	STDIAG		(31)
	ssm		0,0
	b,n         perf_rdr_shift_in_U_leave
	nop

perf_rdr_shift_in_U_leave:
	bve		    (%r2)
	.exit
	MTDIAG_2	(24)			; restore DR2
	.procend
ENDPROC(perf_rdr_shift_in_U)

;***********************************************************************
;*
;* Name: rdr_shift_out_U
;*
;* Description:
;*	This routine moves data to the RDR's.  The double-word that
;*	arg1 points to is loaded and moved into the staging register.
;*	Then the STDIAG instruction for the RDR # in arg0 is called
;*	to move the data to the RDR.
;*
;* Arguments:
;*	arg0 = rdr target
;*	arg1 = buffer pointer
;*
;* Returns:
;*	None
;*
;* Register usage:
;*	arg0 = rdr target
;*	arg1 = buffer pointer
;*	%r24 - DR2 | DR2_SLOW_RET
;*	%r23 - original DR2 value
;*
;***********************************************************************

ENTRY(perf_rdr_shift_out_U)
	.proc
	.callinfo frame=0,NO_CALLS
	.entry

;
; NOTE: The PCX-U ERS states that DR2_SLOW_RET must be set before any
; shifting is done, from or to, the remote diagnose registers.
;

	depdi,z		1,DR2_SLOW_RET,1,%r24
	MFDIAG_2	(23)
	or		%r24,%r23,%r24
	MTDIAG_2	(24)		; set DR2_SLOW_RET

	MTDIAG_1	(25)		; data to the staging register
	shladd		arg0,2,%r0,%r1	; %r1 = 4 * RDR number
	blr		%r1,%r0		; branch to 8-instruction sequence
	nop

;
; 32-byte cachline aligned
;

	sync				; RDR 0 write sequence
	ssm		0,0
	STDIAG		(0)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 1 write sequence
	ssm		0,0
	STDIAG		(1)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 2 write sequence
	ssm		0,0
	STDIAG		(2)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 3 write sequence
	ssm		0,0
	STDIAG		(3)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 4 write sequence
	ssm		0,0
	STDIAG		(4)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 5 write sequence
	ssm		0,0
	STDIAG		(5)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 6 write sequence
	ssm		0,0
	STDIAG		(6)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 7 write sequence
	ssm		0,0
	STDIAG		(7)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 8 write sequence
	ssm		0,0
	STDIAG		(8)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 9 write sequence
	ssm		0,0
	STDIAG		(9)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 10 write sequence
	ssm		0,0
	STDIAG		(10)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 11 write sequence
	ssm		0,0
	STDIAG		(11)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 12 write sequence
	ssm		0,0
	STDIAG		(12)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 13 write sequence
	ssm		0,0
	STDIAG		(13)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 14 write sequence
	ssm		0,0
	STDIAG		(14)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 15 write sequence
	ssm		0,0
	STDIAG		(15)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 16 write sequence
	ssm		0,0
	STDIAG		(16)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 17 write sequence
	ssm		0,0
	STDIAG		(17)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 18 write sequence
	ssm		0,0
	STDIAG		(18)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 19 write sequence
	ssm		0,0
	STDIAG		(19)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 20 write sequence
	ssm		0,0
	STDIAG		(20)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 21 write sequence
	ssm		0,0
	STDIAG		(21)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 22 write sequence
	ssm		0,0
	STDIAG		(22)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 23 write sequence
	ssm		0,0
	STDIAG		(23)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 24 write sequence
	ssm		0,0
	STDIAG		(24)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 25 write sequence
	ssm		0,0
	STDIAG		(25)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 26 write sequence
	ssm		0,0
	STDIAG		(26)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 27 write sequence
	ssm		0,0
	STDIAG		(27)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 28 write sequence
	ssm		0,0
	STDIAG		(28)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 29 write sequence
	ssm		0,0
	STDIAG		(29)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 30 write sequence
	ssm		0,0
	STDIAG		(30)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

	sync				; RDR 31 write sequence
	ssm		0,0
	STDIAG		(31)
	ssm		0,0
	b,n         perf_rdr_shift_out_U_leave
	nop
	ssm		0,0
	nop

perf_rdr_shift_out_U_leave:
	bve		(%r2)
	.exit
	MTDIAG_2	(23)			; restore DR2
	.procend
ENDPROC(perf_rdr_shift_out_U)

