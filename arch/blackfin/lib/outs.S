/*
 * Implementation of outs{bwl} for BlackFin processors using zero overhead loops.
 *
 * Copyright 2005-2009 Analog Devices Inc.
 *                2005 BuyWays BV
 *                      Bas Vermeulen <bas@buyways.nl>
 *
 * Licensed under the GPL-2.
 */

#include <linux/linkage.h>

.align 2

ENTRY(_outsl)
	CC = R2 == 0;
	IF CC JUMP 1f;
	P0 = R0;	/* P0 = port */
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */

	LSETUP( .Llong_loop_s, .Llong_loop_e) LC0 = P2;
.Llong_loop_s: R0 = [P1++];
.Llong_loop_e: [P0] = R0;
1:	RTS;
ENDPROC(_outsl)

ENTRY(_outsw)
	CC = R2 == 0;
	IF CC JUMP 1f;
	P0 = R0;	/* P0 = port */
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */

	LSETUP( .Lword_loop_s, .Lword_loop_e) LC0 = P2;
.Lword_loop_s: R0 = W[P1++];
.Lword_loop_e: W[P0] = R0;
1:	RTS;
ENDPROC(_outsw)

ENTRY(_outsb)
	CC = R2 == 0;
	IF CC JUMP 1f;
	P0 = R0;	/* P0 = port */
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */

	LSETUP( .Lbyte_loop_s, .Lbyte_loop_e) LC0 = P2;
.Lbyte_loop_s: R0 = B[P1++];
.Lbyte_loop_e: B[P0] = R0;
1:	RTS;
ENDPROC(_outsb)

ENTRY(_outsw_8)
	CC = R2 == 0;
	IF CC JUMP 1f;
	P0 = R0;	/* P0 = port */
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */

	LSETUP( .Lword8_loop_s, .Lword8_loop_e) LC0 = P2;
.Lword8_loop_s: R1 = B[P1++];
		R0 = B[P1++];
		R0 = R0 << 8;
		R0 = R0 + R1;
.Lword8_loop_e: W[P0] = R0;
1:	RTS;
ENDPROC(_outsw_8)
