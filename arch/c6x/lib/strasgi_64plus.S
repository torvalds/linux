;; SPDX-License-Identifier: GPL-2.0-or-later
;;  Copyright 2010  Free Software Foundation, Inc.
;;  Contributed by Bernd Schmidt <bernds@codesourcery.com>.
;;

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
