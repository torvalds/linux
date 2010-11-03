/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/types.h>
#include "qmath.h"

/*
Description: This function saturate input 32 bit number into a 16 bit number.
If input number is greater than 0x7fff then output is saturated to 0x7fff.
else if input number is less than 0xffff8000 then output is saturated to 0xffff8000
else output is same as input.
*/
s16 qm_sat32(s32 op)
{
	s16 result;
	if (op > (s32) 0x7fff) {
		result = 0x7fff;
	} else if (op < (s32) 0xffff8000) {
		result = (s16) (0x8000);
	} else {
		result = (s16) op;
	}
	return result;
}

/*
Description: This function multiply two input 16 bit numbers and return the 32 bit result.
This multiplication is similar to compiler multiplication. This operation is defined if
16 bit multiplication on the processor platform is cheaper than 32 bit multiplication (as
the most of qmath functions can be replaced with processor intrinsic instructions).
*/
s32 qm_mul321616(s16 op1, s16 op2)
{
	return (s32) (op1) * (s32) (op2);
}

/*
Description: This function make 16 bit multiplication and return the result in 16 bits.
To fit the result into 16 bits the 32 bit multiplication result is right
shifted by 16 bits.
*/
s16 qm_mul16(s16 op1, s16 op2)
{
	s32 result;
	result = ((s32) (op1) * (s32) (op2));
	return (s16) (result >> 16);
}

/*
Description: This function multiply two 16 bit numbers and return the result in 32 bits.
This function remove the extra sign bit created by the multiplication by leftshifting the
32 bit multiplication result by 1 bit before returning the result. So the output is
twice that of compiler multiplication. (i.e. qm_muls321616(2,3)=12).
When both input 16 bit numbers are 0x8000, then the result is saturated to 0x7fffffff.
*/
s32 qm_muls321616(s16 op1, s16 op2)
{
	s32 result;
	if (op1 == (s16) (0x8000) && op2 == (s16) (0x8000)) {
		result = 0x7fffffff;
	} else {
		result = ((s32) (op1) * (s32) (op2));
		result = result << 1;
	}
	return result;
}

/*
Description: This function make 16 bit unsigned multiplication. To fit the output into
16 bits the 32 bit multiplication result is right shifted by 16 bits.
*/
u16 qm_mulu16(u16 op1, u16 op2)
{
	return (u16) (((u32) op1 * (u32) op2) >> 16);
}

/*
Description: This function make 16 bit multiplication and return the result in 16 bits.
To fit the multiplication result into 16 bits the multiplication result is right shifted by
15 bits. Right shifting 15 bits instead of 16 bits is done to remove the extra sign bit formed
due to the multiplication.
When both the 16bit inputs are 0x8000 then the output is saturated to 0x7fffffff.
*/
s16 qm_muls16(s16 op1, s16 op2)
{
	s32 result;
	if (op1 == (s16) 0x8000 && op2 == (s16) 0x8000) {
		result = 0x7fffffff;
	} else {
		result = ((s32) (op1) * (s32) (op2));
	}
	return (s16) (result >> 15);
}

/*
Description: This function add two 32 bit numbers and return the 32bit result.
If the result overflow 32 bits, the output will be saturated to 32bits.
*/
s32 qm_add32(s32 op1, s32 op2)
{
	s32 result;
	result = op1 + op2;
	if (op1 < 0 && op2 < 0 && result > 0) {
		result = 0x80000000;
	} else if (op1 > 0 && op2 > 0 && result < 0) {
		result = 0x7fffffff;
	}
	return result;
}

/*
Description: This function add two 16 bit numbers and return the 16bit result.
If the result overflow 16 bits, the output will be saturated to 16bits.
*/
s16 qm_add16(s16 op1, s16 op2)
{
	s16 result;
	s32 temp = (s32) op1 + (s32) op2;
	if (temp > (s32) 0x7fff) {
		result = (s16) 0x7fff;
	} else if (temp < (s32) 0xffff8000) {
		result = (s16) 0xffff8000;
	} else {
		result = (s16) temp;
	}
	return result;
}

/*
Description: This function make 16 bit subtraction and return the 16bit result.
If the result overflow 16 bits, the output will be saturated to 16bits.
*/
s16 qm_sub16(s16 op1, s16 op2)
{
	s16 result;
	s32 temp = (s32) op1 - (s32) op2;
	if (temp > (s32) 0x7fff) {
		result = (s16) 0x7fff;
	} else if (temp < (s32) 0xffff8000) {
		result = (s16) 0xffff8000;
	} else {
		result = (s16) temp;
	}
	return result;
}

/*
Description: This function make 32 bit subtraction and return the 32bit result.
If the result overflow 32 bits, the output will be saturated to 32bits.
*/
s32 qm_sub32(s32 op1, s32 op2)
{
	s32 result;
	result = op1 - op2;
	if (op1 >= 0 && op2 < 0 && result < 0) {
		result = 0x7fffffff;
	} else if (op1 < 0 && op2 > 0 && result > 0) {
		result = 0x80000000;
	}
	return result;
}

/*
Description: This function multiply input 16 bit numbers and accumulate the result
into the input 32 bit number and return the 32 bit accumulated result.
If the accumulation result in overflow, then the output will be saturated.
*/
s32 qm_mac321616(s32 acc, s16 op1, s16 op2)
{
	s32 result;
	result = qm_add32(acc, qm_mul321616(op1, op2));
	return result;
}

/*
Description: This function make a 32 bit saturated left shift when the specified shift
is +ve. This function will make a 32 bit right shift when the specified shift is -ve.
This function return the result after shifting operation.
*/
s32 qm_shl32(s32 op, int shift)
{
	int i;
	s32 result;
	result = op;
	if (shift > 31)
		shift = 31;
	else if (shift < -31)
		shift = -31;
	if (shift >= 0) {
		for (i = 0; i < shift; i++) {
			result = qm_add32(result, result);
		}
	} else {
		result = result >> (-shift);
	}
	return result;
}

/*
Description: This function make a 32 bit right shift when shift is +ve.
This function make a 32 bit saturated left shift when shift is -ve. This function
return the result of the shift operation.
*/
s32 qm_shr32(s32 op, int shift)
{
	return qm_shl32(op, -shift);
}

/*
Description: This function make a 16 bit saturated left shift when the specified shift
is +ve. This function will make a 16 bit right shift when the specified shift is -ve.
This function return the result after shifting operation.
*/
s16 qm_shl16(s16 op, int shift)
{
	int i;
	s16 result;
	result = op;
	if (shift > 15)
		shift = 15;
	else if (shift < -15)
		shift = -15;
	if (shift > 0) {
		for (i = 0; i < shift; i++) {
			result = qm_add16(result, result);
		}
	} else {
		result = result >> (-shift);
	}
	return result;
}

/*
Description: This function make a 16 bit right shift when shift is +ve.
This function make a 16 bit saturated left shift when shift is -ve. This function
return the result of the shift operation.
*/
s16 qm_shr16(s16 op, int shift)
{
	return qm_shl16(op, -shift);
}

/*
Description: This function return the number of redundant sign bits in a 16 bit number.
Example: qm_norm16(0x0080) = 7.
*/
s16 qm_norm16(s16 op)
{
	u16 u16extraSignBits;
	if (op == 0) {
		return 15;
	} else {
		u16extraSignBits = 0;
		while ((op >> 15) == (op >> 14)) {
			u16extraSignBits++;
			op = op << 1;
		}
	}
	return u16extraSignBits;
}

/*
Description: This function return the number of redundant sign bits in a 32 bit number.
Example: qm_norm32(0x00000080) = 23
*/
s16 qm_norm32(s32 op)
{
	u16 u16extraSignBits;
	if (op == 0) {
		return 31;
	} else {
		u16extraSignBits = 0;
		while ((op >> 31) == (op >> 30)) {
			u16extraSignBits++;
			op = op << 1;
		}
	}
	return u16extraSignBits;
}

/*
Description: This function divide two 16 bit unsigned numbers.
The numerator should be less than denominator. So the quotient is always less than 1.
This function return the quotient in q.15 format.
*/
s16 qm_div_s(s16 num, s16 denom)
{
	s16 var_out;
	s16 iteration;
	s32 L_num;
	s32 L_denom;
	L_num = (num) << 15;
	L_denom = (denom) << 15;
	for (iteration = 0; iteration < 15; iteration++) {
		L_num <<= 1;
		if (L_num >= L_denom) {
			L_num = qm_sub32(L_num, L_denom);
			L_num = qm_add32(L_num, 1);
		}
	}
	var_out = (s16) (L_num & 0x7fff);
	return var_out;
}

/*
Description: This function compute the absolute value of a 16 bit number.
*/
s16 qm_abs16(s16 op)
{
	if (op < 0) {
		if (op == (s16) 0xffff8000) {
			return 0x7fff;
		} else {
			return -op;
		}
	} else {
		return op;
	}
}

/*
Description: This function divide two 16 bit numbers.
The quotient is returned through return value.
The qformat of the quotient is returned through the pointer (qQuotient) passed
to this function. The qformat of quotient is adjusted appropriately such that
the quotient occupies all 16 bits.
*/
s16 qm_div16(s16 num, s16 denom, s16 *qQuotient)
{
	s16 sign;
	s16 nNum, nDenom;
	sign = num ^ denom;
	num = qm_abs16(num);
	denom = qm_abs16(denom);
	nNum = qm_norm16(num);
	nDenom = qm_norm16(denom);
	num = qm_shl16(num, nNum - 1);
	denom = qm_shl16(denom, nDenom);
	*qQuotient = nNum - 1 - nDenom + 15;
	if (sign >= 0) {
		return qm_div_s(num, denom);
	} else {
		return -qm_div_s(num, denom);
	}
}

/*
Description: This function compute absolute value of a 32 bit number.
*/
s32 qm_abs32(s32 op)
{
	if (op < 0) {
		if (op == (s32) 0x80000000) {
			return 0x7fffffff;
		} else {
			return -op;
		}
	} else {
		return op;
	}
}

/*
Description: This function divide two 32 bit numbers. The division is performed
by considering only important 16 bits in 32 bit numbers.
The quotient is returned through return value.
The qformat of the quotient is returned through the pointer (qquotient) passed
to this function. The qformat of quotient is adjusted appropriately such that
the quotient occupies all 16 bits.
*/
s16 qm_div163232(s32 num, s32 denom, s16 *qquotient)
{
	s32 sign;
	s16 nNum, nDenom;
	sign = num ^ denom;
	num = qm_abs32(num);
	denom = qm_abs32(denom);
	nNum = qm_norm32(num);
	nDenom = qm_norm32(denom);
	num = qm_shl32(num, nNum - 1);
	denom = qm_shl32(denom, nDenom);
	*qquotient = nNum - 1 - nDenom + 15;
	if (sign >= 0) {
		return qm_div_s((s16) (num >> 16), (s16) (denom >> 16));
	} else {
		return -qm_div_s((s16) (num >> 16), (s16) (denom >> 16));
	}
}

/*
Description: This function multiply a 32 bit number with a 16 bit number.
The multiplicaton result is right shifted by 16 bits to fit the result
into 32 bit output.
*/
s32 qm_mul323216(s32 op1, s16 op2)
{
	s16 hi;
	u16 lo;
	s32 result;
	hi = op1 >> 16;
	lo = (s16) (op1 & 0xffff);
	result = qm_mul321616(hi, op2);
	result = result + (qm_mulsu321616(op2, lo) >> 16);
	return result;
}

/*
Description: This function multiply signed 16 bit number with unsigned 16 bit number and return
the result in 32 bits.
*/
s32 qm_mulsu321616(s16 op1, u16 op2)
{
	return (s32) (op1) * op2;
}

/*
Description: This function multiply 32 bit number with 16 bit number. The multiplication result is
right shifted by 15 bits to fit the result into 32 bits. Right shifting by only 15 bits instead of
16 bits is done to remove the extra sign bit formed by multiplication from the return value.
When the input numbers are 0x80000000, 0x8000 the return value is saturated to 0x7fffffff.
*/
s32 qm_muls323216(s32 op1, s16 op2)
{
	s16 hi;
	u16 lo;
	s32 result;
	hi = op1 >> 16;
	lo = (s16) (op1 & 0xffff);
	result = qm_muls321616(hi, op2);
	result = qm_add32(result, (qm_mulsu321616(op2, lo) >> 15));
	return result;
}

/*
Description: This function multiply two 32 bit numbers. The multiplication result is right
shifted by 32 bits to fit the multiplication result into 32 bits. The right shifted
multiplication result is returned as output.
*/
s32 qm_mul32(s32 a, s32 b)
{
	s16 hi1, hi2;
	u16 lo1, lo2;
	s32 result;
	hi1 = a >> 16;
	hi2 = b >> 16;
	lo1 = (u16) (a & 0xffff);
	lo2 = (u16) (b & 0xffff);
	result = qm_mul321616(hi1, hi2);
	result = result + (qm_mulsu321616(hi1, lo2) >> 16);
	result = result + (qm_mulsu321616(hi2, lo1) >> 16);
	return result;
}

/*
Description: This function multiply two 32 bit numbers. The multiplication result is
right shifted by 31 bits to fit the multiplication result into 32 bits. The right
shifted multiplication result is returned as output. Right shifting by only 31 bits
instead of 32 bits is done to remove the extra sign bit formed by multiplication.
When the input numbers are 0x80000000, 0x80000000 the return value is saturated to
0x7fffffff.
*/
s32 qm_muls32(s32 a, s32 b)
{
	s16 hi1, hi2;
	u16 lo1, lo2;
	s32 result;
	hi1 = a >> 16;
	hi2 = b >> 16;
	lo1 = (u16) (a & 0xffff);
	lo2 = (u16) (b & 0xffff);
	result = qm_muls321616(hi1, hi2);
	result = qm_add32(result, (qm_mulsu321616(hi1, lo2) >> 15));
	result = qm_add32(result, (qm_mulsu321616(hi2, lo1) >> 15));
	result = qm_add32(result, (qm_mulu16(lo1, lo2) >> 15));
	return result;
}

/* This table is log2(1+(i/32)) where i=[0:1:31], in q.15 format */
static const s16 log_table[] = {
	0,
	1455,
	2866,
	4236,
	5568,
	6863,
	8124,
	9352,
	10549,
	11716,
	12855,
	13968,
	15055,
	16117,
	17156,
	18173,
	19168,
	20143,
	21098,
	22034,
	22952,
	23852,
	24736,
	25604,
	26455,
	27292,
	28114,
	28922,
	29717,
	30498,
	31267,
	32024
};

#define LOG_TABLE_SIZE 32	/* log_table size */
#define LOG2_LOG_TABLE_SIZE 5	/* log2(log_table size) */
#define Q_LOG_TABLE 15		/* qformat of log_table */
#define LOG10_2		19728	/* log10(2) in q.16 */

/*
Description:
This routine takes the input number N and its q format qN and compute
the log10(N). This routine first normalizes the input no N.	Then N is in mag*(2^x) format.
mag is any number in the range 2^30-(2^31 - 1). Then log2(mag * 2^x) = log2(mag) + x is computed.
From that log10(mag * 2^x) = log2(mag * 2^x) * log10(2) is computed.
This routine looks the log2 value in the table considering LOG2_LOG_TABLE_SIZE+1 MSBs.
As the MSB is always 1, only next LOG2_OF_LOG_TABLE_SIZE MSBs are used for table lookup.
Next 16 MSBs are used for interpolation.
Inputs:
N - number to which log10 has to be found.
qN - q format of N
log10N - address where log10(N) will be written.
qLog10N - address where log10N qformat will be written.
Note/Problem:
For accurate results input should be in normalized or near normalized form.
*/
void qm_log10(s32 N, s16 qN, s16 *log10N, s16 *qLog10N)
{
	s16 s16norm, s16tableIndex, s16errorApproximation;
	u16 u16offset;
	s32 s32log;

	/* Logerithm of negative values is undefined.
	 * assert N is greater than 0.
	 */
	/* ASSERT(N > 0); */

	/* normalize the N. */
	s16norm = qm_norm32(N);
	N = N << s16norm;

	/* The qformat of N after normalization.
	 * -30 is added to treat the no as between 1.0 to 2.0
	 * i.e. after adding the -30 to the qformat the decimal point will be
	 * just rigtht of the MSB. (i.e. after sign bit and 1st MSB). i.e.
	 * at the right side of 30th bit.
	 */
	qN = qN + s16norm - 30;

	/* take the table index as the LOG2_OF_LOG_TABLE_SIZE bits right of the MSB */
	s16tableIndex = (s16) (N >> (32 - (2 + LOG2_LOG_TABLE_SIZE)));

	/* remove the MSB. the MSB is always 1 after normalization. */
	s16tableIndex =
	    s16tableIndex & (s16) ((1 << LOG2_LOG_TABLE_SIZE) - 1);

	/* remove the (1+LOG2_OF_LOG_TABLE_SIZE) MSBs in the N. */
	N = N & ((1 << (32 - (2 + LOG2_LOG_TABLE_SIZE))) - 1);

	/* take the offset as the 16 MSBS after table index.
	 */
	u16offset = (u16) (N >> (32 - (2 + LOG2_LOG_TABLE_SIZE + 16)));

	/* look the log value in the table. */
	s32log = log_table[s16tableIndex];	/* q.15 format */

	/* interpolate using the offset. */
	s16errorApproximation = (s16) qm_mulu16(u16offset, (u16) (log_table[s16tableIndex + 1] - log_table[s16tableIndex]));	/* q.15 */

	s32log = qm_add16((s16) s32log, s16errorApproximation);	/* q.15 format */

	/* adjust for the qformat of the N as
	 * log2(mag * 2^x) = log2(mag) + x
	 */
	s32log = qm_add32(s32log, ((s32) -qN) << 15);	/* q.15 format */

	/* normalize the result. */
	s16norm = qm_norm32(s32log);

	/* bring all the important bits into lower 16 bits */
	s32log = qm_shl32(s32log, s16norm - 16);	/* q.15+s16norm-16 format */

	/* compute the log10(N) by multiplying log2(N) with log10(2).
	 * as log10(mag * 2^x) = log2(mag * 2^x) * log10(2)
	 * log10N in q.15+s16norm-16+1 (LOG10_2 is in q.16)
	 */
	*log10N = qm_muls16((s16) s32log, (s16) LOG10_2);

	/* write the q format of the result. */
	*qLog10N = 15 + s16norm - 16 + 1;

	return;
}

/*
Description:
This routine compute 1/N.
This routine reformates the given no N as N * 2^qN where N is in between 0.5 and 1.0
in q.15 format in 16 bits. So the problem now boils down to finding the inverse of a
q.15 no in 16 bits which is in the range of 0.5 to 1.0. The output is always between
2.0 to 1. So the output is 2.0 to 1.0 in q.30 format. Once the final output format is found
by taking the qN into account. Inverse is found with newton rapson method. Initially
inverse (x) is guessed as 1/0.75 (with appropriate sign). The new guess is calculated
using the formula x' = 2*x - N*x*x. After 4 or 5 iterations the inverse is very close to
inverse of N.
Inputs:
N - number to which 1/N has to be found.
qn - q format of N.
sqrtN - address where 1/N has to be written.
qsqrtN - address where q format of 1/N has to be written.
*/
#define qx 29
void qm_1byN(s32 N, s16 qN, s32 *result, s16 *qResult)
{
	s16 normN;
	s32 s32firstTerm, s32secondTerm, x;
	int i;

	normN = qm_norm32(N);

	/* limit N to least significant 16 bits. 15th bit is the sign bit. */
	N = qm_shl32(N, normN - 16);
	qN = qN + normN - 16 - 15;
	/* -15 is added to treat N as 16 bit q.15 number in the range from 0.5 to 1 */

	/* Take the initial guess as 1/0.75 in qx format with appropriate sign. */
	if (N >= 0) {
		x = (s32) ((1 / 0.75) * (1 << qx));
		/* input no is in the range 0.5 to 1. So 1/0.75 is taken as initial guess. */
	} else {
		x = (s32) ((1 / -0.75) * (1 << qx));
		/* input no is in the range -0.5 to -1. So 1/-0.75 is taken as initial guess. */
	}

	/* iterate the equation x = 2*x - N*x*x for 4 times. */
	for (i = 0; i < 4; i++) {
		s32firstTerm = qm_shl32(x, 1);	/* s32firstTerm = 2*x in q.29 */
		s32secondTerm =
		    qm_muls321616((s16) (s32firstTerm >> 16),
				  (s16) (s32firstTerm >> 16));
		/* s32secondTerm = x*x in q.(29+1-16)*2+1 */
		s32secondTerm =
		    qm_muls321616((s16) (s32secondTerm >> 16), (s16) N);
		/* s32secondTerm = N*x*x in q.((29+1-16)*2+1)-16+15+1 i.e. in q.29 */
		x = qm_sub32(s32firstTerm, s32secondTerm);
		/* can be added directly as both are in q.29 */
	}

	/* Bring the x to q.30 format. */
	*result = qm_shl32(x, 1);
	/* giving the output in q.30 format for q.15 input in 16 bits. */

	/* compute the final q format of the result. */
	*qResult = -qN + 30;	/* adjusting the q format of actual output */

	return;
}

#undef qx
