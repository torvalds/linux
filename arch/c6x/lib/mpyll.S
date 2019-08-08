;; SPDX-License-Identifier: GPL-2.0-or-later
;;  Copyright (C) 2010 Texas Instruments Incorporated
;;  Contributed by Mark Salter <msalter@redhat.com>.
;;

#include <linux/linkage.h>

	;; uint64_t __c6xabi_mpyll(uint64_t x, uint64_t y)
	;;
	;; 64x64 multiply
	;; First compute partial results using 32-bit parts of x and y:
	;;
	;;   b63	 b32 b31	  b0
	;;    -----------------------------
	;;    |      1	    |	   0	  |
	;;    -----------------------------
	;;
	;;   P0 = X0*Y0
	;;   P1 = X0*Y1 + X1*Y0
	;;   P2 = X1*Y1
	;;
	;;   result = (P2 << 64) + (P1 << 32) + P0
	;;
	;; Since the result is also 64-bit, we can skip the P2 term.

	.text
ENTRY(__c6xabi_mpyll)
	mpy32u	.m1x	A4,B4,A1:A0	; X0*Y0
	b	.s2	B3
 ||	mpy32u	.m2x	B5,A4,B1:B0	; X0*Y1 (don't need upper 32-bits)
 ||	mpy32u	.m1x	A5,B4,A3:A2	; X1*Y0 (don't need upper 32-bits)
	nop
	nop
	mv	.s1	A0,A4
	add	.l1x	A2,B0,A5
	add	.s1	A1,A5,A5
ENDPROC(__c6xabi_mpyll)
