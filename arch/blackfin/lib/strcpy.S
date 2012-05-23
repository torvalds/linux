/*
 * Copyright 2005-2010 Analog Devices Inc.
 *
 * Licensed under the Clear BSD license or the GPL-2 (or later)
 */

#include <linux/linkage.h>

/* void *strcpy(char *dest, const char *src);
 * R0 = address (dest)
 * R1 = address (src)
 *
 * Returns a pointer to the destination string dest
 */

#ifdef CONFIG_STRCPY_L1
.section .l1.text
#else
.text
#endif

.align 2

ENTRY(_strcpy)
	P0 = R0 ;       /* dst*/
	P1 = R1 ;       /* src*/

1:
	R1 = B [P1++] (Z);
	B [P0++] = R1;
	CC = R1;
	if cc jump 1b (bp);
	RTS;

ENDPROC(_strcpy)
