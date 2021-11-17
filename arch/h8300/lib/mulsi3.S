; SPDX-License-Identifier: GPL-2.0
;
; mulsi3 for H8/300H - based on Renesas SH implementation
;
; by Toshiyasu Morita
;
; Old code:
;
; 16b * 16b = 372 states (worst case)
; 32b * 32b = 724 states (worst case)
;
; New code:
;
; 16b * 16b =  48 states
; 16b * 32b =  72 states
; 32b * 32b =  92 states
;

	.global __mulsi3
__mulsi3:
	mov.w	r1,r2   ; ( 2 states) b * d
	mulxu	r0,er2  ; (22 states)

	mov.w	e0,r3   ; ( 2 states) a * d
	beq	L_skip1 ; ( 4 states)
	mulxu	r1,er3  ; (22 states)
	add.w	r3,e2   ; ( 2 states)

L_skip1:
	mov.w	e1,r3   ; ( 2 states) c * b
	beq	L_skip2 ; ( 4 states)
	mulxu	r0,er3  ; (22 states)
	add.w	r3,e2   ; ( 2 states)

L_skip2:
	mov.l	er2,er0	; ( 2 states)
	rts		; (10 states)

	.end
