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

ENTRY(__c6xabi_strasgi_64plus)
	shru	.s2x	a6, 2, b31
||	mv	.s1	a4, a30
||	mv	.d2	b4, b30

	add	.s2	-4, b31, b31

	sploopd		1
||	mvc	.s2	b31, ilc
	ldw	.d2t2	*b30++, b31
	nop	4
	mv	.s1x	b31,a31
	spkernel	6, 0
||	stw	.d1t1	a31, *a30++

	ret	.s2	b3
	nop 5
ENDPROC(__c6xabi_strasgi_64plus)
