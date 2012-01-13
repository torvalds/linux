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

	.text
ENTRY(__c6xabi_divremi)
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
	sub	.l1	A5, A6, A5
	nop	4
ENDPROC(__c6xabi_divremi)
