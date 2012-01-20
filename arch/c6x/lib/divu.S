;;  Copyright 2010  Free Software Foundation, Inc.
;;  Contributed by Bernd Schmidt <bernds@codesourcery.com>.
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

	;; ABI considerations for the divide functions
	;; The following registers are call-used:
	;; __c6xabi_divi A0,A1,A2,A4,A6,B0,B1,B2,B4,B5
	;; __c6xabi_divu A0,A1,A2,A4,A6,B0,B1,B2,B4
	;; __c6xabi_remi A1,A2,A4,A5,A6,B0,B1,B2,B4
	;; __c6xabi_remu A1,A4,A5,A7,B0,B1,B2,B4
	;;
	;; In our implementation, divu and remu are leaf functions,
	;; while both divi and remi call into divu.
	;; A0 is not clobbered by any of the functions.
	;; divu does not clobber B2 either, which is taken advantage of
	;; in remi.
	;; divi uses B5 to hold the original return address during
	;; the call to divu.
	;; remi uses B2 and A5 to hold the input values during the
	;; call to divu.  It stores B3 in on the stack.

	.text
ENTRY(__c6xabi_divu)
	;; We use a series of up to 31 subc instructions.  First, we find
	;; out how many leading zero bits there are in the divisor.  This
	;; gives us both a shift count for aligning (shifting) the divisor
	;; to the, and the number of times we have to execute subc.

	;; At the end, we have both the remainder and most of the quotient
	;; in A4.  The top bit of the quotient is computed first and is
	;; placed in A2.

	;; Return immediately if the dividend is zero.
	 mv	.s2x	A4, B1
   [B1]	 lmbd	.l2	1, B4, B1
|| [!B1] b	.s2	B3	; RETURN A
|| [!B1] mvk	.d2	1, B4
	 mv	.l1x	B1, A6
||	 shl	.s2	B4, B1, B4

	;; The loop performs a maximum of 28 steps, so we do the
	;; first 3 here.
	 cmpltu	.l1x	A4, B4, A2
   [!A2] sub	.l1x	A4, B4, A4
||	 shru	.s2	B4, 1, B4
||	 xor	.s1	1, A2, A2

	 shl	.s1	A2, 31, A2
|| [B1]	 subc	.l1x	A4,B4,A4
|| [B1]	 add	.s2	-1, B1, B1
   [B1]	 subc	.l1x	A4,B4,A4
|| [B1]	 add	.s2	-1, B1, B1

	;; RETURN A may happen here (note: must happen before the next branch)
_divu_loop:
	 cmpgt	.l2	B1, 7, B0
|| [B1]	 subc	.l1x	A4,B4,A4
|| [B1]	 add	.s2	-1, B1, B1
   [B1]	 subc	.l1x	A4,B4,A4
|| [B1]	 add	.s2	-1, B1, B1
|| [B0]  b	.s1	_divu_loop
   [B1]	 subc	.l1x	A4,B4,A4
|| [B1]	 add	.s2	-1, B1, B1
   [B1]	 subc	.l1x	A4,B4,A4
|| [B1]	 add	.s2	-1, B1, B1
   [B1]	 subc	.l1x	A4,B4,A4
|| [B1]	 add	.s2	-1, B1, B1
   [B1]	 subc	.l1x	A4,B4,A4
|| [B1]	 add	.s2	-1, B1, B1
   [B1]	 subc	.l1x	A4,B4,A4
|| [B1]	 add	.s2	-1, B1, B1
	;; loop backwards branch happens here

	 ret	.s2	B3
||	 mvk	.s1	32, A1
	 sub	.l1	A1, A6, A6
	 shl	.s1	A4, A6, A4
	 shru	.s1	A4, 1, A4
||	 sub	.l1	A6, 1, A6
	 or	.l1	A2, A4, A4
	 shru	.s1	A4, A6, A4
	 nop
ENDPROC(__c6xabi_divu)
