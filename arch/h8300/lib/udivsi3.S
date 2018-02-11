/* SPDX-License-Identifier: GPL-2.0 */
#include "libgcc.h"

	;; This function also computes the remainder and stores it in er3.
	.global	__udivsi3
__udivsi3:
	mov.w	A1E,A1E		; denominator top word 0?
	bne	DenHighNonZero

	; do it the easy way, see page 107 in manual
	mov.w	A0E,A2
	extu.l	A2P
	divxu.w	A1,A2P
	mov.w	A2E,A0E
	divxu.w	A1,A0P
	mov.w	A0E,A3
	mov.w	A2,A0E
	extu.l	A3P
	rts

	; er0 = er0 / er1
	; er3 = er0 % er1
	; trashes er1 er2
	; expects er1 >= 2^16
DenHighNonZero:
	mov.l	er0,er3
	mov.l	er1,er2
#ifdef CONFIG_CPU_H8300H
divmod_L21:
	shlr.l	er0
	shlr.l	er2		; make divisor < 2^16
	mov.w	e2,e2
	bne	divmod_L21
#else
	shlr.l	#2,er2		; make divisor < 2^16
	mov.w	e2,e2
	beq	divmod_L22A
divmod_L21:
	shlr.l	#2,er0
divmod_L22:
	shlr.l	#2,er2		; make divisor < 2^16
	mov.w	e2,e2
	bne	divmod_L21
divmod_L22A:
	rotxl.w	r2
	bcs	divmod_L23
	shlr.l	er0
	bra	divmod_L24
divmod_L23:
	rotxr.w	r2
	shlr.l	#2,er0
divmod_L24:
#endif
	;; At this point,
	;;  er0 contains shifted dividend
	;;  er1 contains divisor
	;;  er2 contains shifted divisor
	;;  er3 contains dividend, later remainder
	divxu.w	r2,er0		; r0 now contains the approximate quotient (AQ)
	extu.l	er0
	beq	divmod_L25
	subs	#1,er0		; er0 = AQ - 1
	mov.w	e1,r2
	mulxu.w	r0,er2		; er2 = upper (AQ - 1) * divisor
	sub.w	r2,e3		; dividend - 65536 * er2
	mov.w	r1,r2
	mulxu.w	r0,er2		; compute er3 = remainder (tentative)
	sub.l	er2,er3		; er3 = dividend - (AQ - 1) * divisor
divmod_L25:
	cmp.l	er1,er3		; is divisor < remainder?
	blo	divmod_L26
	adds	#1,er0
	sub.l	er1,er3		; correct the remainder
divmod_L26:
	rts

	.end
