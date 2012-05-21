/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the Clear BSD license or the GPL-2 (or later)
 */

#include <linux/linkage.h>

.align 2

#ifdef CONFIG_MEMSET_L1
.section .l1.text
#else
.text
#endif

/*
 * C Library function MEMSET
 * R0 = address (leave unchanged to form result)
 * R1 = filler byte
 * R2 = count
 * Favours word aligned data.
 * The strncpy assumes that I0 and I1 are not used in this function
 */

ENTRY(_memset)
	P0 = R0 ;              /* P0 = address */
	P2 = R2 ;              /* P2 = count   */
	R3 = R0 + R2;          /* end          */
	CC = R2 <= 7(IU);
	IF CC JUMP  .Ltoo_small;
	R1 = R1.B (Z);         /* R1 = fill char */
	R2 =  3;
	R2 = R0 & R2;          /* addr bottom two bits */
	CC =  R2 == 0;             /* AZ set if zero.	*/
	IF !CC JUMP  .Lforce_align ;  /* Jump if addr not aligned. */

.Laligned:
	P1 = P2 >> 2;          /* count = n/4        */
	R2 = R1 <<  8;         /* create quad filler */
	R2.L = R2.L + R1.L(NS);
	R2.H = R2.L + R1.H(NS);
	P2 = R3;

	LSETUP (.Lquad_loop , .Lquad_loop) LC0=P1;
.Lquad_loop:
	[P0++] = R2;

	CC = P0 == P2;
	IF !CC JUMP .Lbytes_left;
	RTS;

.Lbytes_left:
	R2 = R3;                /* end point */
	R3 = P0;                /* current position */
	R2 = R2 - R3;           /* bytes left */
	P2 = R2;

.Ltoo_small:
	CC = P2 == 0;           /* Check zero count */
	IF CC JUMP .Lfinished;    /* Unusual */

.Lbytes:
	LSETUP (.Lbyte_loop , .Lbyte_loop) LC0=P2;
.Lbyte_loop:
	B[P0++] = R1;

.Lfinished:
	RTS;

.Lforce_align:
	CC = BITTST (R0, 0);  /* odd byte */
	R0 = 4;
	R0 = R0 - R2;
	P1 = R0;
	R0 = P0;		    /* Recover return address */
	IF !CC JUMP .Lskip1;
	B[P0++] = R1;
.Lskip1:
	CC = R2 <= 2;          /* 2 bytes */
	P2 -= P1;              /* reduce count */
	IF !CC JUMP .Laligned;
	B[P0++] = R1;
	B[P0++] = R1;
	JUMP .Laligned;

ENDPROC(_memset)
