;; SPDX-License-Identifier: GPL-2.0-or-later
;;  Copyright 2010  Free Software Foundation, Inc.
;;  Contributed by Bernd Schmidt <bernds@codesourcery.com>.
;;

#include <linux/linkage.h>

	.text

ENTRY(__c6xabi_pop_rts)
	lddw	.d2t2	*++B15, B3:B2
	lddw	.d2t1	*++B15, A11:A10
	lddw	.d2t2	*++B15, B11:B10
	lddw	.d2t1	*++B15, A13:A12
	lddw	.d2t2	*++B15, B13:B12
	lddw	.d2t1	*++B15, A15:A14
||	b	.s2	B3
	ldw	.d2t2	*++B15[2], B14
	nop	4
ENDPROC(__c6xabi_pop_rts)
