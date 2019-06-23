; SPDX-License-Identifier: GPL-2.0-only
;  Port on Texas Instruments TMS320C6x architecture
;
;  Copyright (C) 2006, 2009, 2010 Texas Instruments Incorporated
;  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
;

#include <linux/linkage.h>

	.text

ENTRY(memcpy)
	AND	.L1	0x1,A6,A0
 ||	AND	.S1	0x2,A6,A1
 ||	AND	.L2X	0x4,A6,B0
 ||	MV	.D1	A4,A3
 ||	MVC	.S2	ILC,B2

   [A0] LDB	.D2T1	*B4++,A5
   [A1] LDB	.D2T1	*B4++,A7
   [A1] LDB	.D2T1	*B4++,A8
   [B0] LDNW	.D2T1	*B4++,A9
 ||	SHRU	.S2X	A6,0x3,B1
  [!B1] BNOP	.S2	B3,1

   [A0] STB	.D1T1	A5,*A3++
 ||[B1] MVC	.S2	B1,ILC
   [A1] STB	.D1T1	A7,*A3++
   [A1] STB	.D1T1	A8,*A3++
   [B0] STNW	.D1T1	A9,*A3++	; return when len < 8

	SPLOOP	2

	LDNDW	.D2T1	*B4++,A9:A8
	NOP	3

	NOP
	SPKERNEL	0,0
 ||	STNDW	.D1T1	A9:A8,*A3++

	BNOP	.S2	B3,4
	MVC	.S2	B2,ILC
ENDPROC(memcpy)
