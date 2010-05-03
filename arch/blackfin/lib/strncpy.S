/*
 * Copyright 2005-2010 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

#include <linux/linkage.h>

/* void *strncpy(char *dest, const char *src, size_t n);
 * R0 = address (dest)
 * R1 = address (src)
 * R2 = size
 * Returns a pointer to the destination string dest
 */

#ifdef CONFIG_STRNCPY_L1
.section .l1.text
#else
.text
#endif

.align 2

ENTRY(_strncpy)
	CC = R2 == 0;
	if CC JUMP 4f;
	P0 = R0 ;       /* dst*/
	P1 = R1 ;       /* src*/

1:
	R1 = B [P1++] (Z);
	B [P0++] = R1;
	CC = R1;
	if ! cc jump 2f;
	R2 += -1;
	CC = R2 == 0;
	if ! cc jump 1b (bp);
	jump 4f;
2:
	/* if src is shorter than n, we need to null pad bytes in dest */
	R1 = 0;
3:
	R2 += -1;
	CC = R2 == 0;
	if cc jump 4f;
	B [P0++] = R1;
	jump 3b;

4:
	RTS;

ENDPROC(_strncpy)
