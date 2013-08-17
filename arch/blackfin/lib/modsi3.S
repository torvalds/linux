/*
 * This program computes 32 bit signed remainder. It calls div32 function
 * for quotient estimation.
 *   Registers in:  R0, R1 = Numerator/ Denominator
 *   Registers out: R0     = Remainder
 *
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

.global ___modsi3;
.type ___modsi3, STT_FUNC;
.extern ___divsi3;
.type ___divsi3, STT_FUNC;

#ifdef CONFIG_ARITHMETIC_OPS_L1
.section .l1.text
#else
.text
#endif

___modsi3:

	CC=R0==0;
	IF CC JUMP .LRETURN_R0;		/* Return 0, if numerator  == 0 */
	CC=R1==0;
	IF CC JUMP .LRETURN_ZERO;		/* Return 0, if denominator == 0 */
	CC=R0==R1;
	IF CC JUMP .LRETURN_ZERO;		/* Return 0, if numerator == denominator */
	CC = R1 == 1;
	IF CC JUMP .LRETURN_ZERO;		/* Return 0, if denominator ==  1 */
	CC = R1 == -1;
	IF CC JUMP .LRETURN_ZERO;		/* Return 0, if denominator == -1 */

	/* Valid input. Use __divsi3() to compute the quotient, and then
	 * derive the remainder from that. */

	[--SP] = (R7:6);		/* Push  R7 and R6 */
	[--SP] = RETS;			/* and return address */
	R7 = R0;			/* Copy of R0 */
	R6 = R1;			/* Save for later */
	SP += -12;			/* Should always provide this space */
	CALL ___divsi3;			/* Compute signed quotient using ___divsi3()*/
	SP += 12;
	R0 *= R6;			/* Quotient * divisor */
	R0 = R7 - R0;			/* Dividend - (quotient * divisor) */
	RETS = [SP++];			/* Get back return address */
	(R7:6) = [SP++];		/* Pop registers R7 and R4 */
	RTS;				/* Store remainder    */

.LRETURN_ZERO:
	R0 = 0;
.LRETURN_R0:
	RTS;

.size ___modsi3, .-___modsi3
