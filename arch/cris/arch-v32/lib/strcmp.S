; strcmp.S -- CRISv32 version.
; Copyright (C) 2008 AXIS Communications AB
; Written by Edgar E. Iglesias
;
; This source code is licensed under the GNU General Public License,
; Version 2.  See the file COPYING for more details.

	.global	strcmp
	.type	strcmp,@function
strcmp:
1:
	move.b	[$r10+], $r12
	seq	$r13
	sub.b	[$r11+], $r12
	or.b	$r12, $r13
	beq	1b
	nop

	ret
	movs.b	$r12, $r10
	.size	strcmp, . - strcmp
