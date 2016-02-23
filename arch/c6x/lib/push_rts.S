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
