/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 *
 * 16 / 32 bit signed division.
 *                 Special cases :
 *                      1)  If(numerator == 0)
 *                             return 0
 *                      2)  If(denominator ==0)
 *                             return positive max = 0x7fffffff
 *                      3)  If(numerator == denominator)
 *                             return 1
 *                      4)  If(denominator ==1)
 *                             return numerator
 *                      5)  If(denominator == -1)
 *                             return -numerator
 *
 *                 Operand         : R0 - Numerator   (i)
 *                                   R1 - Denominator (i)
 *                                   R0 - Quotient    (o)
 *                 Registers Used : R2-R7,P0-P2
 *
 */

.global   ___divsi3;
.type ___divsi3, STT_FUNC;

#ifdef CONFIG_ARITHMETIC_OPS_L1
.section .l1.text
#else
.text
#endif

.align 2;
___divsi3 :


  R3 = R0 ^ R1;
  R0 = ABS R0;

  CC = V;

  r3 = rot r3 by -1;
  r1 = abs r1;      /* now both positive, r3.30 means "negate result",
                    ** r3.31 means overflow, add one to result
                    */
  cc = r0 < r1;
  if cc jump .Lret_zero;
  r2 = r1 >> 15;
  cc = r2;
  if cc jump .Lidents;
  r2 = r1 << 16;
  cc = r2 <= r0;
  if cc jump .Lidents;

  DIVS(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);
  DIVQ(R0, R1);

  R0 = R0.L (Z);
  r1 = r3 >> 31;    /* add overflow issue back in */
  r0 = r0 + r1;
  r1 = -r0;
  cc = bittst(r3, 30);
  if cc r0 = r1;
  RTS;

/* Can't use the primitives. Test common identities.
** If the identity is true, return the value in R2.
*/

.Lidents:
  CC = R1 == 0;                   /* check for divide by zero */
  IF CC JUMP .Lident_return;

  CC = R0 == 0;                   /* check for division of zero */
  IF CC JUMP .Lzero_return;

  CC = R0 == R1;                  /* check for identical operands */
  IF CC JUMP .Lident_return;

  CC = R1 == 1;                   /* check for divide by 1 */
  IF CC JUMP .Lident_return;

  R2.L = ONES R1;
  R2 = R2.L (Z);
  CC = R2 == 1;
  IF CC JUMP .Lpower_of_two;

  /* Identities haven't helped either.
  ** Perform the full division process.
  */

  P1 = 31;                        /* Set loop counter   */

  [--SP] = (R7:5);                /* Push registers R5-R7 */
  R2 = -R1;
  [--SP] = R2;
  R2 = R0 << 1;                   /* R2 lsw of dividend  */
  R6 = R0 ^ R1;                   /* Get sign */
  R5 = R6 >> 31;                  /* Shift sign to LSB */

  R0 = 0 ;                        /* Clear msw partial remainder */
  R2 = R2 | R5;                   /* Shift quotient bit */
  R6 = R0 ^ R1;                   /* Get new quotient bit */

  LSETUP(.Llst,.Llend)  LC0 = P1;   /* Setup loop */
.Llst:   R7 = R2 >> 31;            /* record copy of carry from R2 */
        R2 = R2 << 1;             /* Shift 64 bit dividend up by 1 bit */
        R0 = R0 << 1 || R5 = [SP];
        R0 = R0 | R7;             /* and add carry */
        CC = R6 < 0;              /* Check quotient(AQ) */
                                  /* we might be subtracting divisor (AQ==0) */
        IF CC R5 = R1;            /* or we might be adding divisor  (AQ==1)*/
        R0 = R0 + R5;             /* do add or subtract, as indicated by AQ */
        R6 = R0 ^ R1;             /* Generate next quotient bit */
        R5 = R6 >> 31;
                                  /* Assume AQ==1, shift in zero */
        BITTGL(R5,0);             /* tweak AQ to be what we want to shift in */
.Llend:  R2 = R2 + R5;             /* and then set shifted-in value to
                                  ** tweaked AQ.
                                  */
  r1 = r3 >> 31;
  r2 = r2 + r1;
  cc = bittst(r3,30);
  r0 = -r2;
  if !cc r0 = r2;
  SP += 4;
  (R7:5)= [SP++];                 /* Pop registers R6-R7 */
  RTS;

.Lident_return:
  CC = R1 == 0;                   /* check for divide by zero  => 0x7fffffff */
  R2 = -1 (X);
  R2 >>= 1;
  IF CC JUMP .Ltrue_ident_return;

  CC = R0 == R1;                  /* check for identical operands => 1 */
  R2 = 1 (Z);
  IF CC JUMP .Ltrue_ident_return;

  R2 = R0;                        /* assume divide by 1 => numerator */
  /*FALLTHRU*/

.Ltrue_ident_return:
  R0 = R2;                        /* Return an identity value */
  R2 = -R2;
  CC = bittst(R3,30);
  IF CC R0 = R2;
.Lzero_return:
  RTS;                            /* ...including zero */

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
  IF CC JUMP .Ltrue_ident_return;

  R1.l = SIGNBITS R1;
  R1 = R1.L (Z);
  R1 += -30;
  R0 = LSHIFT R0 by R1.L;
  r1 = r3 >> 31;
  r0 = r0 + r1;
  R2 = -R0;                       // negate result if necessary
  CC = bittst(R3,30);
  IF CC R0 = R2;
  RTS;

.Lret_zero:
  R0 = 0;
  RTS;

.size ___divsi3, .-___divsi3
