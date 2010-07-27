/*
 * Copyright 2005-2010 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

#include <linux/linkage.h>

/* void *strncpy(char *s1, const char *s2, size_t n);
 * R0 = address (dest)
 * R1 = address (src)
 * R2 = size (n)
 * Returns a pointer to the destination string dest
 */

#ifdef CONFIG_STRNCMP_L1
.section .l1.text
#else
.text
#endif

.align 2

ENTRY(_strncmp)
	CC = R2 == 0;
	if CC JUMP 5f;

	P0 = R0 ;       /* s1 */
	P1 = R1 ;       /* s2 */
1:
	R0 = B[P0++] (Z);      /* get *s1 */
	R1 = B[P1++] (Z);      /* get *s2 */
	CC = R0 == R1;         /* compare a byte */
	if ! cc jump 3f;       /* not equal, break out */
	CC = R0;               /* at end of s1? */
	if ! cc jump 4f;       /* yes, all done */
	R2 += -1;              /* no, adjust count */
	CC = R2 == 0;
	if ! cc jump 1b (bp);  /* more to do, keep going */
2:
	R0 = 0;                /* strings are equal */
	jump.s 4f;
3:
	R0 = R0 - R1;          /* *s1 - *s2 */
4:
	RTS;

5:
	R0 = 0;
	RTS;

ENDPROC(_strncmp)
