/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include <asm/div64.h>

#define SHIFT_AMOUNT 16 /* We multiply all original integers with 2^SHIFT_AMOUNT to get the fInt representation */

#define PRECISION 5 /* Change this value to change the number of decimal places in the final output - 5 is a good default */

#define SHIFTED_2 (2 << SHIFT_AMOUNT)
#define MAX (1 << (SHIFT_AMOUNT - 1)) - 1 /* 32767 - Might change in the future */

/* -------------------------------------------------------------------------------
 * NEW TYPE - fINT
 * -------------------------------------------------------------------------------
 * A variable of type fInt can be accessed in 3 ways using the dot (.) operator
 * fInt A;
 * A.full => The full number as it is. Generally not easy to read
 * A.partial.real => Only the integer portion
 * A.partial.decimal => Only the fractional portion
 */
typedef union _fInt {
    int full;
    struct _partial {
        unsigned int decimal: SHIFT_AMOUNT; /*Needs to always be unsigned*/
        int real: 32 - SHIFT_AMOUNT;
    } partial;
} fInt;

/* -------------------------------------------------------------------------------
 * Function Declarations
 *  -------------------------------------------------------------------------------
 */
static fInt ConvertToFraction(int);                       /* Use this to convert an INT to a FINT */
static fInt Convert_ULONG_ToFraction(uint32_t);           /* Use this to convert an uint32_t to a FINT */
static fInt GetScaledFraction(int, int);                  /* Use this to convert an INT to a FINT after scaling it by a factor */
static int ConvertBackToInteger(fInt);                    /* Convert a FINT back to an INT that is scaled by 1000 (i.e. last 3 digits are the decimal digits) */

static fInt fNegate(fInt);                                /* Returns -1 * input fInt value */
static fInt fAdd (fInt, fInt);                            /* Returns the sum of two fInt numbers */
static fInt fSubtract (fInt A, fInt B);                   /* Returns A-B - Sometimes easier than Adding negative numbers */
static fInt fMultiply (fInt, fInt);                       /* Returns the product of two fInt numbers */
static fInt fDivide (fInt A, fInt B);                     /* Returns A/B */
static fInt fGetSquare(fInt);                             /* Returns the square of a fInt number */
static fInt fSqrt(fInt);                                  /* Returns the Square Root of a fInt number */

static int uAbs(int);                                     /* Returns the Absolute value of the Int */
static int uPow(int base, int exponent);                  /* Returns base^exponent an INT */

static void SolveQuadracticEqn(fInt, fInt, fInt, fInt[]); /* Returns the 2 roots via the array */
static bool Equal(fInt, fInt);                            /* Returns true if two fInts are equal to each other */
static bool GreaterThan(fInt A, fInt B);                  /* Returns true if A > B */

static fInt fExponential(fInt exponent);                  /* Can be used to calculate e^exponent */
static fInt fNaturalLog(fInt value);                      /* Can be used to calculate ln(value) */

/* Fuse decoding functions
 * -------------------------------------------------------------------------------------
 */
static fInt fDecodeLinearFuse(uint32_t fuse_value, fInt f_min, fInt f_range, uint32_t bitlength);
static fInt fDecodeLogisticFuse(uint32_t fuse_value, fInt f_average, fInt f_range, uint32_t bitlength);
static fInt fDecodeLeakageID (uint32_t leakageID_fuse, fInt ln_max_div_min, fInt f_min, uint32_t bitlength);

/* Internal Support Functions - Use these ONLY for testing or adding to internal functions
 * -------------------------------------------------------------------------------------
 * Some of the following functions take two INTs as their input - This is unsafe for a variety of reasons.
 */
static fInt Divide (int, int);                            /* Divide two INTs and return result as FINT */
static fInt fNegate(fInt);

static int uGetScaledDecimal (fInt);                      /* Internal function */
static int GetReal (fInt A);                              /* Internal function */

/* -------------------------------------------------------------------------------------
 * TROUBLESHOOTING INFORMATION
 * -------------------------------------------------------------------------------------
 * 1) ConvertToFraction - InputOutOfRangeException: Only accepts numbers smaller than MAX (default: 32767)
 * 2) fAdd - OutputOutOfRangeException: Output bigger than MAX (default: 32767)
 * 3) fMultiply - OutputOutOfRangeException:
 * 4) fGetSquare - OutputOutOfRangeException:
 * 5) fDivide - DivideByZeroException
 * 6) fSqrt - NegativeSquareRootException: Input cannot be a negative number
 */

/* -------------------------------------------------------------------------------------
 * START OF CODE
 * -------------------------------------------------------------------------------------
 */
static fInt fExponential(fInt exponent)        /*Can be used to calculate e^exponent*/
{
	uint32_t i;
	bool bNegated = false;

	fInt fPositiveOne = ConvertToFraction(1);
	fInt fZERO = ConvertToFraction(0);

	fInt lower_bound = Divide(78, 10000);
	fInt solution = fPositiveOne; /*Starting off with baseline of 1 */
	fInt error_term;

	static const uint32_t k_array[11] = {55452, 27726, 13863, 6931, 4055, 2231, 1178, 606, 308, 155, 78};
	static const uint32_t expk_array[11] = {2560000, 160000, 40000, 20000, 15000, 12500, 11250, 10625, 10313, 10156, 10078};

	if (GreaterThan(fZERO, exponent)) {
		exponent = fNegate(exponent);
		bNegated = true;
	}

	while (GreaterThan(exponent, lower_bound)) {
		for (i = 0; i < 11; i++) {
			if (GreaterThan(exponent, GetScaledFraction(k_array[i], 10000))) {
				exponent = fSubtract(exponent, GetScaledFraction(k_array[i], 10000));
				solution = fMultiply(solution, GetScaledFraction(expk_array[i], 10000));
			}
		}
	}

	error_term = fAdd(fPositiveOne, exponent);

	solution = fMultiply(solution, error_term);

	if (bNegated)
		solution = fDivide(fPositiveOne, solution);

	return solution;
}

static fInt fNaturalLog(fInt value)
{
	uint32_t i;
	fInt upper_bound = Divide(8, 1000);
	fInt fNegativeOne = ConvertToFraction(-1);
	fInt solution = ConvertToFraction(0); /*Starting off with baseline of 0 */
	fInt error_term;

	static const uint32_t k_array[10] = {160000, 40000, 20000, 15000, 12500, 11250, 10625, 10313, 10156, 10078};
	static const uint32_t logk_array[10] = {27726, 13863, 6931, 4055, 2231, 1178, 606, 308, 155, 78};

	while (GreaterThan(fAdd(value, fNegativeOne), upper_bound)) {
		for (i = 0; i < 10; i++) {
			if (GreaterThan(value, GetScaledFraction(k_array[i], 10000))) {
				value = fDivide(value, GetScaledFraction(k_array[i], 10000));
				solution = fAdd(solution, GetScaledFraction(logk_array[i], 10000));
			}
		}
	}

	error_term = fAdd(fNegativeOne, value);

	return (fAdd(solution, error_term));
}

static fInt fDecodeLinearFuse(uint32_t fuse_value, fInt f_min, fInt f_range, uint32_t bitlength)
{
	fInt f_fuse_value = Convert_ULONG_ToFraction(fuse_value);
	fInt f_bit_max_value = Convert_ULONG_ToFraction((uPow(2, bitlength)) - 1);

	fInt f_decoded_value;

	f_decoded_value = fDivide(f_fuse_value, f_bit_max_value);
	f_decoded_value = fMultiply(f_decoded_value, f_range);
	f_decoded_value = fAdd(f_decoded_value, f_min);

	return f_decoded_value;
}


static fInt fDecodeLogisticFuse(uint32_t fuse_value, fInt f_average, fInt f_range, uint32_t bitlength)
{
	fInt f_fuse_value = Convert_ULONG_ToFraction(fuse_value);
	fInt f_bit_max_value = Convert_ULONG_ToFraction((uPow(2, bitlength)) - 1);

	fInt f_CONSTANT_NEG13 = ConvertToFraction(-13);
	fInt f_CONSTANT1 = ConvertToFraction(1);

	fInt f_decoded_value;

	f_decoded_value = fSubtract(fDivide(f_bit_max_value, f_fuse_value), f_CONSTANT1);
	f_decoded_value = fNaturalLog(f_decoded_value);
	f_decoded_value = fMultiply(f_decoded_value, fDivide(f_range, f_CONSTANT_NEG13));
	f_decoded_value = fAdd(f_decoded_value, f_average);

	return f_decoded_value;
}

static fInt fDecodeLeakageID (uint32_t leakageID_fuse, fInt ln_max_div_min, fInt f_min, uint32_t bitlength)
{
	fInt fLeakage;
	fInt f_bit_max_value = Convert_ULONG_ToFraction((uPow(2, bitlength)) - 1);

	fLeakage = fMultiply(ln_max_div_min, Convert_ULONG_ToFraction(leakageID_fuse));
	fLeakage = fDivide(fLeakage, f_bit_max_value);
	fLeakage = fExponential(fLeakage);
	fLeakage = fMultiply(fLeakage, f_min);

	return fLeakage;
}

static fInt ConvertToFraction(int X) /*Add all range checking here. Is it possible to make fInt a private declaration? */
{
	fInt temp;

	if (X <= MAX)
		temp.full = (X << SHIFT_AMOUNT);
	else
		temp.full = 0;

	return temp;
}

static fInt fNegate(fInt X)
{
	fInt CONSTANT_NEGONE = ConvertToFraction(-1);
	return (fMultiply(X, CONSTANT_NEGONE));
}

static fInt Convert_ULONG_ToFraction(uint32_t X)
{
	fInt temp;

	if (X <= MAX)
		temp.full = (X << SHIFT_AMOUNT);
	else
		temp.full = 0;

	return temp;
}

static fInt GetScaledFraction(int X, int factor)
{
	int times_shifted, factor_shifted;
	bool bNEGATED;
	fInt fValue;

	times_shifted = 0;
	factor_shifted = 0;
	bNEGATED = false;

	if (X < 0) {
		X = -1*X;
		bNEGATED = true;
	}

	if (factor < 0) {
		factor = -1*factor;
		bNEGATED = !bNEGATED; /*If bNEGATED = true due to X < 0, this will cover the case of negative cancelling negative */
	}

	if ((X > MAX) || factor > MAX) {
		if ((X/factor) <= MAX) {
			while (X > MAX) {
				X = X >> 1;
				times_shifted++;
			}

			while (factor > MAX) {
				factor = factor >> 1;
				factor_shifted++;
			}
		} else {
			fValue.full = 0;
			return fValue;
		}
	}

	if (factor == 1)
		return ConvertToFraction(X);

	fValue = fDivide(ConvertToFraction(X * uPow(-1, bNEGATED)), ConvertToFraction(factor));

	fValue.full = fValue.full << times_shifted;
	fValue.full = fValue.full >> factor_shifted;

	return fValue;
}

/* Addition using two fInts */
static fInt fAdd (fInt X, fInt Y)
{
	fInt Sum;

	Sum.full = X.full + Y.full;

	return Sum;
}

/* Addition using two fInts */
static fInt fSubtract (fInt X, fInt Y)
{
	fInt Difference;

	Difference.full = X.full - Y.full;

	return Difference;
}

static bool Equal(fInt A, fInt B)
{
	if (A.full == B.full)
		return true;
	else
		return false;
}

static bool GreaterThan(fInt A, fInt B)
{
	if (A.full > B.full)
		return true;
	else
		return false;
}

static fInt fMultiply (fInt X, fInt Y) /* Uses 64-bit integers (int64_t) */
{
	fInt Product;
	int64_t tempProduct;

	/*The following is for a very specific common case: Non-zero number with ONLY fractional portion*/
	/* TEMPORARILY DISABLED - CAN BE USED TO IMPROVE PRECISION
	bool X_LessThanOne, Y_LessThanOne;

	X_LessThanOne = (X.partial.real == 0 && X.partial.decimal != 0 && X.full >= 0);
	Y_LessThanOne = (Y.partial.real == 0 && Y.partial.decimal != 0 && Y.full >= 0);

	if (X_LessThanOne && Y_LessThanOne) {
		Product.full = X.full * Y.full;
		return Product
	}*/

	tempProduct = ((int64_t)X.full) * ((int64_t)Y.full); /*Q(16,16)*Q(16,16) = Q(32, 32) - Might become a negative number! */
	tempProduct = tempProduct >> 16; /*Remove lagging 16 bits - Will lose some precision from decimal; */
	Product.full = (int)tempProduct; /*The int64_t will lose the leading 16 bits that were part of the integer portion */

	return Product;
}

static fInt fDivide (fInt X, fInt Y)
{
	fInt fZERO, fQuotient;
	int64_t longlongX, longlongY;

	fZERO = ConvertToFraction(0);

	if (Equal(Y, fZERO))
		return fZERO;

	longlongX = (int64_t)X.full;
	longlongY = (int64_t)Y.full;

	longlongX = longlongX << 16; /*Q(16,16) -> Q(32,32) */

	div64_s64(longlongX, longlongY); /*Q(32,32) divided by Q(16,16) = Q(16,16) Back to original format */

	fQuotient.full = (int)longlongX;
	return fQuotient;
}

static int ConvertBackToInteger (fInt A) /*THIS is the function that will be used to check with the Golden settings table*/
{
	fInt fullNumber, scaledDecimal, scaledReal;

	scaledReal.full = GetReal(A) * uPow(10, PRECISION-1); /* DOUBLE CHECK THISSSS!!! */

	scaledDecimal.full = uGetScaledDecimal(A);

	fullNumber = fAdd(scaledDecimal,scaledReal);

	return fullNumber.full;
}

static fInt fGetSquare(fInt A)
{
	return fMultiply(A,A);
}

/* x_new = x_old - (x_old^2 - C) / (2 * x_old) */
static fInt fSqrt(fInt num)
{
	fInt F_divide_Fprime, Fprime;
	fInt test;
	fInt twoShifted;
	int seed, counter, error;
	fInt x_new, x_old, C, y;

	fInt fZERO = ConvertToFraction(0);

	/* (0 > num) is the same as (num < 0), i.e., num is negative */

	if (GreaterThan(fZERO, num) || Equal(fZERO, num))
		return fZERO;

	C = num;

	if (num.partial.real > 3000)
		seed = 60;
	else if (num.partial.real > 1000)
		seed = 30;
	else if (num.partial.real > 100)
		seed = 10;
	else
		seed = 2;

	counter = 0;

	if (Equal(num, fZERO)) /*Square Root of Zero is zero */
		return fZERO;

	twoShifted = ConvertToFraction(2);
	x_new = ConvertToFraction(seed);

	do {
		counter++;

		x_old.full = x_new.full;

		test = fGetSquare(x_old); /*1.75*1.75 is reverting back to 1 when shifted down */
		y = fSubtract(test, C); /*y = f(x) = x^2 - C; */

		Fprime = fMultiply(twoShifted, x_old);
		F_divide_Fprime = fDivide(y, Fprime);

		x_new = fSubtract(x_old, F_divide_Fprime);

		error = ConvertBackToInteger(x_new) - ConvertBackToInteger(x_old);

		if (counter > 20) /*20 is already way too many iterations. If we dont have an answer by then, we never will*/
			return x_new;

	} while (uAbs(error) > 0);

	return (x_new);
}

static void SolveQuadracticEqn(fInt A, fInt B, fInt C, fInt Roots[])
{
	fInt *pRoots = &Roots[0];
	fInt temp, root_first, root_second;
	fInt f_CONSTANT10, f_CONSTANT100;

	f_CONSTANT100 = ConvertToFraction(100);
	f_CONSTANT10 = ConvertToFraction(10);

	while(GreaterThan(A, f_CONSTANT100) || GreaterThan(B, f_CONSTANT100) || GreaterThan(C, f_CONSTANT100)) {
		A = fDivide(A, f_CONSTANT10);
		B = fDivide(B, f_CONSTANT10);
		C = fDivide(C, f_CONSTANT10);
	}

	temp = fMultiply(ConvertToFraction(4), A); /* root = 4*A */
	temp = fMultiply(temp, C); /* root = 4*A*C */
	temp = fSubtract(fGetSquare(B), temp); /* root = b^2 - 4AC */
	temp = fSqrt(temp); /*root = Sqrt (b^2 - 4AC); */

	root_first = fSubtract(fNegate(B), temp); /* b - Sqrt(b^2 - 4AC) */
	root_second = fAdd(fNegate(B), temp); /* b + Sqrt(b^2 - 4AC) */

	root_first = fDivide(root_first, ConvertToFraction(2)); /* [b +- Sqrt(b^2 - 4AC)]/[2] */
	root_first = fDivide(root_first, A); /*[b +- Sqrt(b^2 - 4AC)]/[2*A] */

	root_second = fDivide(root_second, ConvertToFraction(2)); /* [b +- Sqrt(b^2 - 4AC)]/[2] */
	root_second = fDivide(root_second, A); /*[b +- Sqrt(b^2 - 4AC)]/[2*A] */

	*(pRoots + 0) = root_first;
	*(pRoots + 1) = root_second;
}

/* -----------------------------------------------------------------------------
 * SUPPORT FUNCTIONS
 * -----------------------------------------------------------------------------
 */

/* Conversion Functions */
static int GetReal (fInt A)
{
	return (A.full >> SHIFT_AMOUNT);
}

static fInt Divide (int X, int Y)
{
	fInt A, B, Quotient;

	A.full = X << SHIFT_AMOUNT;
	B.full = Y << SHIFT_AMOUNT;

	Quotient = fDivide(A, B);

	return Quotient;
}

static int uGetScaledDecimal (fInt A) /*Converts the fractional portion to whole integers - Costly function */
{
	int dec[PRECISION];
	int i, scaledDecimal = 0, tmp = A.partial.decimal;

	for (i = 0; i < PRECISION; i++) {
		dec[i] = tmp / (1 << SHIFT_AMOUNT);
		tmp = tmp - ((1 << SHIFT_AMOUNT)*dec[i]);
		tmp *= 10;
		scaledDecimal = scaledDecimal + dec[i]*uPow(10, PRECISION - 1 -i);
	}

	return scaledDecimal;
}

static int uPow(int base, int power)
{
	if (power == 0)
		return 1;
	else
		return (base)*uPow(base, power - 1);
}

static int uAbs(int X)
{
	if (X < 0)
		return (X * -1);
	else
		return X;
}

static fInt fRoundUpByStepSize(fInt A, fInt fStepSize, bool error_term)
{
	fInt solution;

	solution = fDivide(A, fStepSize);
	solution.partial.decimal = 0; /*All fractional digits changes to 0 */

	if (error_term)
		solution.partial.real += 1; /*Error term of 1 added */

	solution = fMultiply(solution, fStepSize);
	solution = fAdd(solution, fStepSize);

	return solution;
}

