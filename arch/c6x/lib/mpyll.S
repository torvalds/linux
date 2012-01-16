;;  Copyright (C) 2010 Texas Instruments Incorporated
;;  Contributed by Mark Salter <msalter@redhat.com>.
;;
;; This program is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2 of the License, or
;; (at your option) any later version.
;;
;; This program is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with this program; if not, write to the Free Software
;; Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

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
