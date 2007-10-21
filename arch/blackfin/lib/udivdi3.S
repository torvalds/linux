/*
 * udivdi3.S - unsigned long long division
 *
 * Copyright 2003-2007 Analog Devices Inc.
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPLv2 or later.
 */

#include <linux/linkage.h>

#define CARRY AC0

#ifdef CONFIG_ARITHMETIC_OPS_L1
.section .l1.text
#else
.text
#endif


ENTRY(___udivdi3)
   R3 = [SP + 12];
   [--SP] = (R7:4, P5:3);

   /* Attempt to use divide primitive first; these will handle
   **  most cases, and they're quick - avoids stalls incurred by
   ** testing for identities.
   */

   R4 = R2 | R3;
   CC = R4 == 0;
   IF CC JUMP .LDIV_BY_ZERO;

   R4.H = 0x8000;
   R4 >>>= 16;                  // R4 now 0xFFFF8000
   R5 = R0 | R2;                // If either dividend or
   R4 = R5 & R4;                // divisor have bits in
   CC = R4;                     // top half or low half's sign
   IF CC JUMP .LIDENTS;          // bit, skip builtins.
   R4 = R1 | R3;                // Also check top halves
   CC = R4;
   IF CC JUMP .LIDENTS;

   /* Can use the builtins. */

   AQ = CC;                     // Clear AQ (CC==0)
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   DIVQ(R0, R2);
   R0 = R0.L (Z);
   R1 = 0;
   (R7:4, P5:3) = [SP++];
   RTS;

.LIDENTS:
   /* Test for common identities. Value to be returned is
   ** placed in R6,R7.
   */
                                // Check for 0/y, return 0
   R4 = R0 | R1;
   CC = R4 == 0;
   IF CC JUMP .LRETURN_R0;

                                // Check for x/x, return 1
   R6 = R0 - R2;                // If x == y, then both R6 and R7 will be zero
   R7 = R1 - R3;
   R4 = R6 | R7;                // making R4 zero.
   R6 += 1;                     // which would now make R6:R7==1.
   CC = R4 == 0;
   IF CC JUMP .LRETURN_IDENT;

                                // Check for x/1, return x
   R6 = R0;
   R7 = R1;
   CC = R3 == 0;
   IF !CC JUMP .Lnexttest;
   CC = R2 == 1;
   IF CC JUMP .LRETURN_IDENT;

.Lnexttest:
   R4.L = ONES R2;              // check for div by power of two which
   R5.L = ONES R3;              // can be done using a shift
   R6 = PACK (R5.L, R4.L);
   CC = R6 == 1;
   IF CC JUMP .Lpower_of_two_upper_zero;
   R6 = PACK (R4.L, R5.L);
   CC = R6 == 1;
   IF CC JUMP .Lpower_of_two_lower_zero;

                                // Check for x < y, return 0
   R6 = 0;
   R7 = R6;
   CC = R1 < R3 (IU);
   IF CC JUMP .LRETURN_IDENT;
   CC = R1 == R3;
   IF !CC JUMP .Lno_idents;
   CC = R0 < R2 (IU);
   IF CC JUMP .LRETURN_IDENT;

.Lno_idents:                    // Idents don't match. Go for the full operation


   // If X, or X and Y have high bit set, it'll affect the
   // results, so shift right one to stop this. Note: we've already
   // checked that X >= Y, so Y's msb won't be set unless X's
   // is.

   R4 = 0;
   CC = R1 < 0;
   IF !CC JUMP .Lx_msb_clear;
   CC = !CC;                   // 1 -> 0;
   R1 = ROT R1 BY -1;          // Shift X >> 1
   R0 = ROT R0 BY -1;          // lsb -> CC
   BITSET(R4,31);              // to record only x msb was set
   CC = R3 < 0;
   IF !CC JUMP .Ly_msb_clear;
   CC = !CC;
   R3 = ROT R3 BY -1;          // Shift Y >> 1
   R2 = ROT R2 BY -1;
   BITCLR(R4,31);              // clear bit to record only x msb was set

.Ly_msb_clear:
.Lx_msb_clear:
   // Bit 31 in R4 indicates X msb set, but Y msb wasn't, and no bits
   // were lost, so we should shift result left by one.

   [--SP] = R4;                // save for later

   // In the loop that follows, each iteration we add
   // either Y' or -Y' to the Remainder. We compute the
   // negated Y', and store, for convenience. Y' goes
   // into P0:P1, while -Y' goes into P2:P3.

   P0 = R2;
   P1 = R3;
   R2 = -R2;
   CC = CARRY;
   CC = !CC;
   R4 = CC;
   R3 = -R3;
   R3 = R3 - R4;

   R6 = 0;                     // remainder = 0
   R7 = R6;

   [--SP] = R2; P2 = SP;
   [--SP] = R3; P3 = SP;
   [--SP] = R6; P5 = SP;       // AQ = 0
   [--SP] = P1;

   /* In the loop that follows, we use the following
   ** register assignments:
   ** R0,R1 X, workspace
   ** R2,R3 Y, workspace
   ** R4,R5 partial Div
   ** R6,R7 partial remainder
   ** P5 AQ
   ** The remainder and div form a 128-bit number, with
   ** the remainder in the high 64-bits.
   */
   R4 = R0;                    // Div = X'
   R5 = R1;
   R3 = 0;

   P4 = 64;                    // Iterate once per bit
   LSETUP(.LULST,.LULEND) LC0 = P4;
.LULST:
        /* Shift Div and remainder up by one. The bit shifted
        ** out of the top of the quotient is shifted into the bottom
        ** of the remainder.
        */
        CC = R3;
        R4 = ROT R4 BY 1;
        R5 = ROT R5 BY 1 ||        // low q to high q
             R2 = [P5];            // load saved AQ
        R6 = ROT R6 BY 1 ||        // high q to low r
             R0 = [P2];            // load -Y'
        R7 = ROT R7 BY 1 ||        // low r to high r
             R1 = [P3];

                                   // Assume add -Y'
        CC = R2 < 0;               // But if AQ is set...
        IF CC R0 = P0;             // then add Y' instead
        IF CC R1 = P1;

        R6 = R6 + R0;              // Rem += (Y' or -Y')
        CC = CARRY;
        R0 = CC;
        R7 = R7 + R1;
        R7 = R7 + R0 (NS) ||
             R1 = [SP];
                                   // Set the next AQ bit
        R1 = R7 ^ R1;              // from Remainder and Y'
        R1 = R1 >> 31 ||           // Negate AQ's value, and
             [P5] = R1;            // save next AQ
        BITTGL(R1, 0);             // add neg AQ  to the Div
.LULEND: R4 = R4 + R1;

   R6 = [SP + 16];

   R0 = R4;
   R1 = R5;
   CC = BITTST(R6,30);         // Just set CC=0
   R4 = ROT R0 BY 1;           // but if we had to shift X,
   R5 = ROT R1 BY 1;           // and didn't shift any bits out,
   CC = BITTST(R6,31);         // then the result will be half as
   IF CC R0 = R4;              // much as required, so shift left
   IF CC R1 = R5;              // one space.

   SP += 20;
   (R7:4, P5:3) = [SP++];
   RTS;

.Lpower_of_two:
   /* Y has a single bit set, which means it's a power of two.
   ** That means we can perform the division just by shifting
   ** X to the right the appropriate number of bits
   */

   /* signbits returns the number of sign bits, minus one.
   ** 1=>30, 2=>29, ..., 0x40000000=>0. Which means we need
   ** to shift right n-signbits spaces. It also means 0x80000000
   ** is a special case, because that *also* gives a signbits of 0
   */
.Lpower_of_two_lower_zero:
   R7 = 0;
   R6 = R1 >> 31;
   CC = R3 < 0;
   IF CC JUMP .LRETURN_IDENT;

   R2.L = SIGNBITS R3;
   R2 = R2.L (Z);
   R2 += -62;
   (R7:4, P5:3) = [SP++];
   JUMP ___lshftli;

.Lpower_of_two_upper_zero:
   CC = R2 < 0;
   IF CC JUMP .Lmaxint_shift;

   R2.L = SIGNBITS R2;
   R2 = R2.L (Z);
   R2 += -30;
   (R7:4, P5:3) = [SP++];
   JUMP ___lshftli;

.Lmaxint_shift:
   R2 = -31;
   (R7:4, P5:3) = [SP++];
   JUMP ___lshftli;

.LRETURN_IDENT:
   R0 = R6;
   R1 = R7;
.LRETURN_R0:
   (R7:4, P5:3) = [SP++];
   RTS;
.LDIV_BY_ZERO:
   R0 = ~R2;
   R1 = R0;
   (R7:4, P5:3) = [SP++];
   RTS;

ENDPROC(___udivdi3)


ENTRY(___lshftli)
	CC = R2 == 0;
	IF CC JUMP .Lfinished;	// nothing to do
	CC = R2 < 0;
	IF CC JUMP .Lrshift;
	R3 = 64;
	CC = R2 < R3;
	IF !CC JUMP .Lretzero;

	// We're shifting left, and it's less than 64 bits, so
	// a valid result will be returned.

	R3 >>= 1;	// R3 now 32
	CC = R2 < R3;

	IF !CC JUMP .Lzerohalf;

	// We're shifting left, between 1 and 31 bits, which means
	// some of the low half will be shifted into the high half.
	// Work out how much.

	R3 = R3 - R2;

	// Save that much data from the bottom half.

	P1 = R7;
	R7 = R0;
	R7 >>= R3;

	// Adjust both parts of the parameter.

	R0 <<= R2;
	R1 <<= R2;

	// And include the bits moved across.

	R1 = R1 | R7;
	R7 = P1;
	RTS;

.Lzerohalf:
	// We're shifting left, between 32 and 63 bits, so the
	// bottom half will become zero, and the top half will
	// lose some bits. How many?

	R2 = R2 - R3;	// N - 32
	R1 = LSHIFT R0 BY R2.L;
	R0 = R0 - R0;
	RTS;

.Lretzero:
	R0 = R0 - R0;
	R1 = R0;
.Lfinished:
	RTS;

.Lrshift:
	// We're shifting right, but by how much?
	R2 = -R2;
	R3 = 64;
	CC = R2 < R3;
	IF !CC JUMP .Lretzero;

	// Shifting right less than 64 bits, so some result bits will
	// be retained.

	R3 >>= 1;	// R3 now 32
	CC = R2 < R3;
	IF !CC JUMP .Lsignhalf;

	// Shifting right between 1 and 31 bits, so need to copy
	// data across words.

	P1 = R7;
	R3 = R3 - R2;
	R7 = R1;
	R7 <<= R3;
	R1 >>= R2;
	R0 >>= R2;
	R0 = R7 | R0;
	R7 = P1;
	RTS;

.Lsignhalf:
	// Shifting right between 32 and 63 bits, so the top half
	// will become all zero-bits, and the bottom half is some
	// of the top half. But how much?

	R2 = R2 - R3;
	R0 = R1;
	R0 >>= R2;
	R1 = 0;
	RTS;

ENDPROC(___lshftli)
