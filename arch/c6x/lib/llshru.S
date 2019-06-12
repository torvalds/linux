;; SPDX-License-Identifier: GPL-2.0-or-later
;;  Copyright (C) 2010 Texas Instruments Incorporated
;;  Contributed by Mark Salter <msalter@redhat.com>.
;;

;;  uint64_t __c6xabi_llshru(uint64_t val, uint shift)

#include <linux/linkage.h>

	.text
ENTRY(__c6xabi_llshru)
	 mv	.l1x	B4,A1
   [!A1] b	.s2	B3		; return if zero shift count
	 mvk	.s1	32,A0
	 sub	.d1	A0,A1,A0
	 cmplt	.l1	0,A0,A2
   [A2]  shl	.s1	A5,A0,A0
	 nop
   [!A2] neg	.l1	A0,A4
|| [A2]  shru	.s1	A4,A1,A4
   [!A2] shru	.s1	A5,A4,A4
|| [A2]  or	.d1	A4,A0,A4
|| [!A2] mvk	.l1	0,A5
   [A2]  shru	.s1	A5,A1,A5
	 bnop	.s2	B3,5
ENDPROC(__c6xabi_llshru)
