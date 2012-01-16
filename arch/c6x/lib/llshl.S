;;  Copyright (C) 2010 Texas Instruments Incorporated
;;  Contributed by Mark Salter <msalter@redhat.com>.
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

;;  uint64_t __c6xabi_llshl(uint64_t val, uint shift)

#include <linux/linkage.h>

	.text
ENTRY(__c6xabi_llshl)
	 mv	.l1x	B4,A1
   [!A1] b	.s2	B3		; just return if zero shift
	 mvk	.s1	32,A0
	 sub	.d1	A0,A1,A0
	 cmplt	.l1	0,A0,A2
   [A2]	 shru	.s1	A4,A0,A0
   [!A2] neg	.l1	A0,A5
|| [A2]  shl	.s1	A5,A1,A5
   [!A2] shl	.s1	A4,A5,A5
|| [A2]  or	.d1	A5,A0,A5
|| [!A2] mvk	.l1	0,A4
   [A2]	 shl	.s1	A4,A1,A4
	 bnop	.s2	B3,5
ENDPROC(__c6xabi_llshl)
