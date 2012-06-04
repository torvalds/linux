/*
 * Copyright 2005-2010 Analog Devices Inc.
 *
 * Licensed under the Clear BSD license or the GPL-2 (or later)
 */

#include <linux/linkage.h>

/* void *strcmp(char *s1, const char *s2);
 * R0 = address (s1)
 * R1 = address (s2)
 *
 * Returns an integer less than, equal to, or greater than zero if s1
 *  (or the first n  bytes thereof) is found, respectively, to be less
 *  than, to match, or be greater than s2.
 */

#ifdef CONFIG_STRCMP_L1
.section .l1.text
#else
.text
#endif

.align 2

ENTRY(_strcmp)
	P0 = R0 ;       /* s1 */
	P1 = R1 ;       /* s2 */

1:
	R0 = B[P0++] (Z);      /* get *s1 */
	R1 = B[P1++] (Z);      /* get *s2 */
	CC = R0 == R1;         /* compare a byte */
	if ! cc jump 2f;       /* not equal, break out */
	CC = R0;               /* at end of s1? */
	if cc jump 1b (bp);    /* no, keep going */
	jump.s 3f;             /* strings are equal */
2:
	R0 = R0 - R1;          /* *s1 - *s2 */
3:
	RTS;

ENDPROC(_strcmp)
