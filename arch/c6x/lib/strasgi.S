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

ENTRY(__c6xabi_strasgi)
	;; This is essentially memcpy, with alignment known to be at least
	;; 4, and the size a multiple of 4 greater than or equal to 28.
	 ldw	.d2t1	*B4++, A0
||	 mvk	.s2	16, B1
	 ldw	.d2t1	*B4++, A1
||	 mvk	.s2	20, B2
||	 sub	.d1	A6, 24, A6
	 ldw	.d2t1	*B4++, A5
	 ldw	.d2t1	*B4++, A7
||	 mv	.l2x	A6, B7
	 ldw	.d2t1	*B4++, A8
	 ldw	.d2t1	*B4++, A9
||	 mv	.s2x	A0, B5
||	 cmpltu	.l2	B2, B7, B0

_strasgi_loop:
	 stw	.d1t2	B5, *A4++
|| [B0]	 ldw	.d2t1	*B4++, A0
||	 mv	.s2x	A1, B5
||	 mv	.l2	B7, B6

   [B0]	 sub	.d2	B6, 24, B7
|| [B0]	 b	.s2	_strasgi_loop
||	 cmpltu	.l2	B1, B6, B0

   [B0]	 ldw	.d2t1	*B4++, A1
||	 stw	.d1t2	B5, *A4++
||	 mv	.s2x	A5, B5
||	 cmpltu	.l2	12, B6, B0

   [B0]	 ldw	.d2t1	*B4++, A5
||	 stw	.d1t2	B5, *A4++
||	 mv	.s2x	A7, B5
||	 cmpltu	.l2	8, B6, B0

   [B0]	 ldw	.d2t1	*B4++, A7
||	 stw	.d1t2	B5, *A4++
||	 mv	.s2x	A8, B5
||	 cmpltu	.l2	4, B6, B0

   [B0]	 ldw	.d2t1	*B4++, A8
||	 stw	.d1t2	B5, *A4++
||	 mv	.s2x	A9, B5
||	 cmpltu	.l2	0, B6, B0

   [B0]	 ldw	.d2t1	*B4++, A9
||	 stw	.d1t2	B5, *A4++
||	 mv	.s2x	A0, B5
||	 cmpltu	.l2	B2, B7, B0

	;; loop back branch happens here

	 cmpltu	.l2	B1, B6, B0
||	 ret	.s2	b3

   [B0]	 stw	.d1t1	A1, *A4++
||	 cmpltu	.l2	12, B6, B0
   [B0]	 stw	.d1t1	A5, *A4++
||	 cmpltu	.l2	8, B6, B0
   [B0]	 stw	.d1t1	A7, *A4++
||	 cmpltu	.l2	4, B6, B0
   [B0]	 stw	.d1t1	A8, *A4++
||	 cmpltu	.l2	0, B6, B0
   [B0]	 stw	.d1t1	A9, *A4++

	;; return happens here
ENDPROC(__c6xabi_strasgi)
