;; SPDX-License-Identifier: GPL-2.0-or-later
;;  Copyright 2010  Free Software Foundation, Inc.
;;  Contributed by Bernd Schmidt <bernds@codesourcery.com>.
;;

#include <linux/linkage.h>

	.text

ENTRY(__c6xabi_push_rts)
	stw	.d2t2	B14, *B15--[2]
	stdw	.d2t1	A15:A14, *B15--
||	b	.s2x	A3
	stdw	.d2t2	B13:B12, *B15--
	stdw	.d2t1	A13:A12, *B15--
	stdw	.d2t2	B11:B10, *B15--
	stdw	.d2t1	A11:A10, *B15--
	stdw	.d2t2	B3:B2, *B15--
ENDPROC(__c6xabi_push_rts)
