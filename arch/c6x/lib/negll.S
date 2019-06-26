;; SPDX-License-Identifier: GPL-2.0-or-later
;;  Copyright (C) 2010 Texas Instruments Incorporated
;;  Contributed by Mark Salter <msalter@redhat.com>.
;;

;;  int64_t __c6xabi_negll(int64_t val)

#include <linux/linkage.h>

	.text
ENTRY(__c6xabi_negll)
	b	.s2	B3
	mvk	.l1	0,A0
	subu	.l1	A0,A4,A3:A2
	sub	.l1	A0,A5,A0
||	ext	.s1	A3,24,24,A5
	add	.l1	A5,A0,A5
	mv	.s1	A2,A4
ENDPROC(__c6xabi_negll)
