;; SPDX-License-Identifier: GPL-2.0-or-later
;;  Copyright 2010  Free Software Foundation, Inc.
;;  Contributed by Bernd Schmidt <bernds@codesourcery.com>.
;;

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
ENTRY(__c6xabi_divi)
	call	.s2	__c6xabi_divu
||	mv	.d2	B3, B5
||	cmpgt	.l1	0, A4, A1
||	cmpgt	.l2	0, B4, B1

   [A1]	neg	.l1	A4, A4
|| [B1]	neg	.l2	B4, B4
||	xor	.s1x	A1, B1, A1
   [A1] addkpc	.s2	_divu_ret, B3, 4
_divu_ret:
	neg	.l1	A4, A4
||	mv	.l2	B3,B5
||	ret	.s2	B5
	nop		5
ENDPROC(__c6xabi_divi)
