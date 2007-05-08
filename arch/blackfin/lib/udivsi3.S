/*
 * File:         arch/blackfin/lib/udivsi3.S
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/linkage.h>

#define CARRY AC0

#ifdef CONFIG_ARITHMETIC_OPS_L1
.section .l1.text
#else
.text
#endif


ENTRY(___udivsi3)

  CC = R0 < R1 (IU);    /* If X < Y, always return 0 */
  IF CC JUMP .Lreturn_ident;

  R2 = R1 << 16;
  CC = R2 <= R0 (IU);
  IF CC JUMP .Lidents;

  R2 = R0 >> 31;       /* if X is a 31-bit number */
  R3 = R1 >> 15;       /* and Y is a 15-bit number */
  R2 = R2 | R3;        /* then it's okay to use the DIVQ builtins (fallthrough to fast)*/
  CC = R2;
  IF CC JUMP .Ly_16bit;

/* METHOD 1: FAST DIVQ
   We know we have a 31-bit dividend, and 15-bit divisor so we can use the
   simple divq approach (first setting AQ to 0 - implying unsigned division,
   then 16 DIVQ's).
*/

  AQ = CC;             /* Clear AQ (CC==0) */

/* ISR States: When dividing two integers (32.0/16.0) using divide primitives,
   we need to shift the dividend one bit to the left.
   We have already checked that we have a 31-bit number so we are safe to do
   that.
*/
  R0 <<= 1;
  DIVQ(R0, R1); // 1
  DIVQ(R0, R1); // 2
  DIVQ(R0, R1); // 3
  DIVQ(R0, R1); // 4
  DIVQ(R0, R1); // 5
  DIVQ(R0, R1); // 6
  DIVQ(R0, R1); // 7
  DIVQ(R0, R1); // 8
  DIVQ(R0, R1); // 9
  DIVQ(R0, R1); // 10
  DIVQ(R0, R1); // 11
  DIVQ(R0, R1); // 12
  DIVQ(R0, R1); // 13
  DIVQ(R0, R1); // 14
  DIVQ(R0, R1); // 15
  DIVQ(R0, R1); // 16
  R0 = R0.L (Z);
  RTS;

.Ly_16bit:
  /* We know that the upper 17 bits of Y might have bits set,
  ** or that the sign bit of X might have a bit. If Y is a
  ** 16-bit number, but not bigger, then we can use the builtins
  ** with a post-divide correction.
  ** R3 currently holds Y>>15, which means R3's LSB is the
  ** bit we're interested in.
  */

  /* According to the ISR, to use the Divide primitives for
  ** unsigned integer divide, the useable range is 31 bits
  */
  CC = ! BITTST(R0, 31);

  /* IF condition is true we can scale our inputs and use the divide primitives,
  ** with some post-adjustment
  */
  R3 += -1;		/* if so, Y is 0x00008nnn */
  CC &= AZ;

  /* If condition is true we can scale our inputs and use the divide primitives,
  ** with some post-adjustment
  */
  R3 = R1 >> 1;		/* Pre-scaled divisor for primitive case */
  R2 = R0 >> 16;

  R2 = R3 - R2;		/* shifted divisor < upper 16 bits of dividend */
  CC &= CARRY;
  IF CC JUMP .Lshift_and_correct;

  /* Fall through to the identities */

/* METHOD 2: identities and manual calculation
   We are not able to use the divide primites, but may still catch some special
   cases.
*/
.Lidents:
  /* Test for common identities. Value to be returned is placed in R2. */
  CC = R0 == 0;        /* 0/Y => 0 */
  IF CC JUMP .Lreturn_r0;
  CC = R0 == R1;       /* X==Y => 1 */
  IF CC JUMP .Lreturn_ident;
  CC = R1 == 1;        /* X/1 => X */
  IF CC JUMP .Lreturn_ident;

  R2.L = ONES R1;
  R2 = R2.L (Z);
  CC = R2 == 1;
  IF CC JUMP .Lpower_of_two;

  [--SP] = (R7:5);                /* Push registers R5-R7 */

  /* Idents don't match. Go for the full operation. */


  R6 = 2;                         /* assume we'll shift two */
  R3 = 1;

  P2 = R1;
                                  /* If either R0 or R1 have sign set, */
                                  /* divide them by two, and note it's */
                                  /* been done. */
  CC = R1 < 0;
  R2 = R1 >> 1;
  IF CC R1 = R2;                  /* Possibly-shifted R1 */
  IF !CC R6 = R3;                 /* R1 doesn't, so at most 1 shifted */

  P0 = 0;
  R3 = -R1;
  [--SP] = R3;
  R2 = R0 >> 1;
  R2 = R0 >> 1;
  CC = R0 < 0;
  IF CC P0 = R6;                  /* Number of values divided */
  IF !CC R2 = R0;                 /* Shifted R0 */

                                  /* P0 is 0, 1 (NR/=2) or 2 (NR/=2, DR/=2) */

                                  /* r2 holds Copy dividend  */
  R3 = 0;                         /* Clear partial remainder */
  R7 = 0;                         /* Initialise quotient bit */

  P1 = 32;                        /* Set loop counter */
  LSETUP(.Lulst, .Lulend) LC0 = P1; /* Set loop counter */
.Lulst:  R6 = R2 >> 31;             /* R6 = sign bit of R2, for carry */
       R2 = R2 << 1;              /* Shift 64 bit dividend up by 1 bit */
       R3 = R3 << 1 || R5 = [SP];
       R3 = R3 | R6;              /* Include any carry */
       CC = R7 < 0;               /* Check quotient(AQ) */
                                  /* If AQ==0, we'll sub divisor */
       IF CC R5 = R1;             /* and if AQ==1, we'll add it. */
       R3 = R3 + R5;              /* Add/sub divsor to partial remainder */
       R7 = R3 ^ R1;              /* Generate next quotient bit */

       R5 = R7 >> 31;             /* Get AQ */
       BITTGL(R5, 0);             /* Invert it, to get what we'll shift */
.Lulend: R2 = R2 + R5;              /* and "shift" it in. */

  CC = P0 == 0;                   /* Check how many inputs we shifted */
  IF CC JUMP .Lno_mult;            /* if none... */
  R6 = R2 << 1;
  CC = P0 == 1;
  IF CC R2 = R6;                  /* if 1, Q = Q*2 */
  IF !CC R1 = P2;                 /* if 2, restore stored divisor */

  R3 = R2;                        /* Copy of R2 */
  R3 *= R1;                       /* Q * divisor */
  R5 = R0 - R3;                   /* Z = (dividend - Q * divisor) */
  CC = R1 <= R5 (IU);             /* Check if divisor <= Z? */
  R6 = CC;                        /* if yes, R6 = 1 */
  R2 = R2 + R6;                   /* if yes, add one to quotient(Q) */
.Lno_mult:
  SP += 4;
  (R7:5) = [SP++];                /* Pop registers R5-R7 */
  R0 = R2;                        /* Store quotient */
  RTS;

.Lreturn_ident:
  CC = R0 < R1 (IU);    /* If X < Y, always return 0 */
  R2 = 0;
  IF CC JUMP .Ltrue_return_ident;
  R2 = -1 (X);         /* X/0 => 0xFFFFFFFF */
  CC = R1 == 0;
  IF CC JUMP .Ltrue_return_ident;
  R2 = -R2;            /* R2 now 1 */
  CC = R0 == R1;       /* X==Y => 1 */
  IF CC JUMP .Ltrue_return_ident;
  R2 = R0;             /* X/1 => X */
  /*FALLTHRU*/

.Ltrue_return_ident:
  R0 = R2;
.Lreturn_r0:
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

  R2 = R0 >> 31;
  CC = R1 < 0;
  IF CC JUMP .Ltrue_return_ident;

  R1.l = SIGNBITS R1;
  R1 = R1.L (Z);
  R1 += -30;
  R0 = LSHIFT R0 by R1.L;
  RTS;

/* METHOD 3: PRESCALE AND USE THE DIVIDE PRIMITIVES WITH SOME POST-CORRECTION
  Two scaling operations are required to use the divide primitives with a
  divisor > 0x7FFFF.
  Firstly (as in method 1) we need to shift the dividend 1 to the left for
  integer division.
  Secondly we need to shift both the divisor and dividend 1 to the right so
  both are in range for the primitives.
  The left/right shift of the dividend does nothing so we can skip it.
*/
.Lshift_and_correct:
  R2 = R0;
  // R3 is already R1 >> 1
  CC=!CC;
  AQ = CC;                        /* Clear AQ, got here with CC = 0 */
  DIVQ(R2, R3); // 1
  DIVQ(R2, R3); // 2
  DIVQ(R2, R3); // 3
  DIVQ(R2, R3); // 4
  DIVQ(R2, R3); // 5
  DIVQ(R2, R3); // 6
  DIVQ(R2, R3); // 7
  DIVQ(R2, R3); // 8
  DIVQ(R2, R3); // 9
  DIVQ(R2, R3); // 10
  DIVQ(R2, R3); // 11
  DIVQ(R2, R3); // 12
  DIVQ(R2, R3); // 13
  DIVQ(R2, R3); // 14
  DIVQ(R2, R3); // 15
  DIVQ(R2, R3); // 16

  /* According to the Instruction Set Reference:
     To divide by a divisor > 0x7FFF,
     1. prescale and perform divide to obtain quotient (Q) (done above),
     2. multiply quotient by unscaled divisor (result M)
     3. subtract the product from the divident to get an error (E = X - M)
     4. if E < divisor (Y) subtract 1, if E > divisor (Y) add 1, else return quotient (Q)
   */
  R3 = R2.L (Z);		/* Q = X' / Y' */
  R2 = R3;		/* Preserve Q */
  R2 *= R1;		/* M = Q * Y */
  R2 = R0 - R2;		/* E = X - M */
  R0 = R3;		/* Copy Q into result reg */

/* Correction: If result of the multiply is negative, we overflowed
   and need to correct the result by subtracting 1 from the result.*/
  R3 = 0xFFFF (Z);
  R2 = R2 >> 16;		/* E >> 16 */
  CC = R2 == R3;
  R3 = 1 ;
  R1 = R0 - R3;
  IF CC R0 = R1;
  RTS;
