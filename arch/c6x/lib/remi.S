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

ENTRY(__c6xabi_remi)
	stw	.d2t2	B3, *B15--[2]
||	cmpgt	.l1	0, A4, A1
||	cmpgt	.l2	0, B4, B2
||	mv	.s1	A4, A5
||	call	.s2	__c6xabi_divu

   [A1]	neg	.l1	A4, A4
|| [B2]	neg	.l2	B4, B4
||	xor	.s2x	B2, A1, B0
||	mv	.d2	B4, B2

   [B0]	addkpc	.s2	_divu_ret_1, B3, 1
  [!B0] addkpc	.s2	_divu_ret_2, B3, 1
	nop	2
_divu_ret_1:
	neg	.l1	A4, A4
_divu_ret_2:
	ldw	.d2t2	*++B15[2], B3

	mpy32	.m1x	A4, B2, A6
	nop		3
	ret	.s2	B3
	sub	.l1	A5, A6, A4
	nop	4
ENDPROC(__c6xabi_remi)
