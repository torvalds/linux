/* SPDX-License-Identifier: GPL-2.0 */
#include "libgcc.h"

; numerator in A0/A1
; denominator in A2/A3
	.global	__modsi3
__modsi3:
	PUSHP	S2P
	bsr	modnorm
	bsr	__divsi3
	mov.l	er3,er0
	bra	exitdiv

	.global	__umodsi3
__umodsi3:
	bsr	__udivsi3
	mov.l	er3,er0
	rts

	.global	__divsi3
__divsi3:
	PUSHP	S2P
	jsr	divnorm
	bsr	__udivsi3

	; examine what the sign should be
exitdiv:
	btst	#3,S2L
	beq	reti

	; should be -ve
	neg.l	A0P

reti:
	POPP	S2P
	rts

divnorm:
	mov.l	A0P,A0P		; is the numerator -ve
	stc	ccr,S2L		; keep the sign in bit 3 of S2L
	bge	postive

	neg.l	A0P		; negate arg

postive:
	mov.l	A1P,A1P		; is the denominator -ve
	bge	postive2

	neg.l	A1P		; negate arg
	xor.b	#0x08,S2L	; toggle the result sign

postive2:
	rts

;; Basically the same, except that the sign of the divisor determines
;; the sign.
modnorm:
	mov.l	A0P,A0P		; is the numerator -ve
	stc	ccr,S2L		; keep the sign in bit 3 of S2L
	bge	mpostive

	neg.l	A0P		; negate arg

mpostive:
	mov.l	A1P,A1P		; is the denominator -ve
	bge	mpostive2

	neg.l	A1P		; negate arg

mpostive2:
	rts

	.end
