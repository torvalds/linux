/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __ISP_OP1W_H_INCLUDED__
#define __ISP_OP1W_H_INCLUDED__

/*
 * This file is part of the Multi-precision vector operations exstension package.
 */

/*
 * Single-precision vector operations
 */

/*
 * Prerequisites:
 *
 */
#include "storage_class.h"

#ifdef INLINE_ISP_OP1W
#define STORAGE_CLASS_ISP_OP1W_FUNC_H STORAGE_CLASS_INLINE
#define STORAGE_CLASS_ISP_OP1W_DATA_H STORAGE_CLASS_INLINE_DATA
#else /* INLINE_ISP_OP1W */
#define STORAGE_CLASS_ISP_OP1W_FUNC_H STORAGE_CLASS_EXTERN
#define STORAGE_CLASS_ISP_OP1W_DATA_H STORAGE_CLASS_EXTERN_DATA
#endif  /* INLINE_ISP_OP1W */

/*
 * Single-precision data type specification
 */

#include "isp_op1w_types.h"
#include "isp_op2w_types.h" // for doubling operations.

/*
 * Single-precision prototype specification
 */

/* Arithmetic */

/** @brief bitwise AND
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		bitwise and of both input arguments
 *
 * This function will calculate the bitwise and.
 * result = _a & _b
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_and(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief bitwise OR
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		bitwise or of both input arguments
 *
 * This function will calculate the bitwise or.
 * result = _a | _b
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_or(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief bitwise XOR
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		bitwise xor of both input arguments
 *
 * This function will calculate the bitwise xor.
 * result = _a ^ _b
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_xor(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief bitwise inverse
 *
 * @param[in] _a	first argument
 *
 * @return		bitwise inverse of both input arguments
 *
 * This function will calculate the bitwise inverse.
 * result = ~_a
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_inv(
    const tvector1w     _a);

/* Additive */

/** @brief addition
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		sum of both input arguments
 *
 * This function will calculate the sum of the input arguments.
 * in case of overflow it will wrap around.
 * result = _a + _b
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_add(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief subtraction
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_b subtracted from _a.
 *
 * This function will subtract _b from _a.
 * in case of overflow it will wrap around.
 * result = _a - _b
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_sub(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief saturated addition
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		saturated sum of both input arguments
 *
 * This function will calculate the sum of the input arguments.
 * in case of overflow it will saturate.
 * result = CLIP(_a + _b, MIN_RANGE, MAX_RANGE);
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_addsat(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief saturated subtraction
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		saturated subtraction of both input arguments
 *
 * This function will subtract _b from _a.
 * in case of overflow it will saturate.
 * result = CLIP(_a - _b, MIN_RANGE, MAX_RANGE);
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_subsat(
    const tvector1w     _a,
    const tvector1w     _b);

#ifdef ISP2401
/** @brief Unsigned saturated subtraction
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		saturated subtraction of both input arguments
 *
 * This function will subtract _b from _a.
 * in case of overflow it will saturate.
 * result = CLIP(_a - _b, 0, MAX_RANGE);
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w_unsigned OP_1w_subsat_u(
    const tvector1w_unsigned _a,
    const tvector1w_unsigned _b);

#endif
/** @brief subtraction with shift right and rounding
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		(a - b) >> 1
 *
 * This function subtracts _b from _a and right shifts
 * the result by 1 bit with rounding.
 * No overflow can occur.
 * result = (_a - _b) >> 1
 *
 * Note: This function will be deprecated due to
 * the naming confusion and it will be replaced
 * by "OP_1w_subhalfrnd".
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_subasr1(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief Subtraction with shift right and rounding
 *
 * @param[in] _a	first operand
 * @param[in] _b	second operand
 *
 * @return		(_a - _b) >> 1
 *
 * This function subtracts _b from _a and right shifts
 * the result by 1 bit with rounding.
 * No overflow can occur.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_subhalfrnd(
    const tvector1w	_a,
    const tvector1w	_b);

/** @brief Subtraction with shift right and no rounding
 *
 * @param[in] _a	first operand
 * @param[in] _b	second operand
 *
 * @return		(_a - _b) >> 1
 *
 * This function subtracts _b from _a and right shifts
 * the result by 1 bit without rounding (i.e. truncation).
 * No overflow can occur.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_subhalf(
    const tvector1w	_a,
    const tvector1w	_b);


/** @brief saturated absolute value
 *
 * @param[in] _a	input
 *
 * @return		saturated absolute value of the input
 *
 * This function will calculate the saturated absolute value of the input.
 * in case of overflow it will saturate.
 * if (_a > 0) return _a;<br>
 * else return CLIP(-_a, MIN_RANGE, MAX_RANGE);<br>
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_abs(
    const tvector1w     _a);

/** @brief saturated absolute difference
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		sat(abs(a-b));
 *
 * This function will calculate the saturated absolute value
 * of the saturated difference of both inputs.
 * result = sat(abs(sat(_a - _b)));
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_subabssat(
    const tvector1w     _a,
    const tvector1w     _b);

/* Multiplicative */

/** @brief doubling multiply
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		product of _a and _b
 *
 * This function will calculate the product
 * of the input arguments and returns a double
 * precision result.
 * No overflow can occur.
 * result = _a * _b;
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector2w OP_1w_muld(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief integer multiply
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		product of _a and _b
 *
 * This function will calculate the product
 * of the input arguments and returns the LSB
 * aligned single precision result.
 * In case of overflow it will wrap around.
 * result = _a * _b;
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_mul(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief fractional saturating multiply
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		saturated product of _a and _b
 *
 * This function will calculate the fixed point
 * product of the input arguments
 * and returns a single precision result.
 * In case of overflow it will saturate.
 * FP_UNITY * FP_UNITY => FP_UNITY.
 * result = CLIP(_a * _b >> (NUM_BITS-1), MIN_RANGE, MAX_RANGE);
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_qmul(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief fractional saturating multiply with rounding
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		product of _a and _b
 *
 * This function will calculate the fixed point
 * product of the input arguments
 * and returns a single precision result.
 * FP_UNITY * FP_UNITY => FP_UNITY.
 * Depending on the rounding mode of the core
 * it will round to nearest or to nearest even.
 * result = CLIP(_a * _b >> (NUM_BITS-1), MIN_RANGE, MAX_RANGE);
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_qrmul(
    const tvector1w     _a,
    const tvector1w     _b);

/* Comparative */

/** @brief equal
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a == _b
 *
 * This function will return true if both inputs
 * are equal, and false if not equal.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tflags OP_1w_eq(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief not equal
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a != _b
 *
 * This function will return false if both inputs
 * are equal, and true if not equal.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tflags OP_1w_ne(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief less or equal
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a <= _b
 *
 * This function will return true if _a is smaller
 * or equal than _b.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tflags OP_1w_le(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief less then
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a < _b
 *
 * This function will return true if _a is smaller
 * than _b.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tflags OP_1w_lt(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief greater or equal
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a >= _b
 *
 * This function will return true if _a is greater
 * or equal than _b.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tflags OP_1w_ge(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief greater than
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a > _b
 *
 * This function will return true if _a is greater
 * than _b.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tflags OP_1w_gt(
    const tvector1w     _a,
    const tvector1w     _b);

/* Shift */

/** @brief aritmetic shift right
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a >> _b
 *
 * This function will shift _a with _b bits to the right,
 * preserving the sign bit.
 * It asserts 0 <= _b <= MAX_SHIFT_1W.
 *
 * The operation count for this function assumes that
 * the shift amount is a cloned scalar input.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_asr(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief aritmetic shift right with rounding
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a >> _b
 *
 * If _b < NUM_BITS, this function will shift _a with _b bits to the right,
 * preserving the sign bit, and depending on the rounding mode of the core
 * it will round to nearest or to nearest even.
 * If _b >= NUM_BITS, this function will return 0.
 * It asserts 0 <= _b <= MAX_SHIFT_1W.
 * The operation count for this function assumes that
 * the shift amount is a cloned scalar input.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_asrrnd(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief saturating arithmetic shift left
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a << _b
 *
 * If _b < MAX_BITDEPTH, this function will shift _a with _b bits to the left,
 * saturating at MIN_RANGE/MAX_RANGE in case of overflow.
 * If _b >= MAX_BITDEPTH, this function will return MIN_RANGE if _a < 0,
 * MAX_RANGE if _a > 0, 0 if _a == 0.
 * (with MAX_BITDEPTH=64)
 * It asserts 0 <= _b <= MAX_SHIFT_1W.
 * The operation count for this function assumes that
 * the shift amount is a cloned scalar input.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_asl(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief saturating aritmetic shift left
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a << _b
 *
 * This function is identical to OP_1w_asl( )
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_aslsat(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief logical shift left
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a << _b
 *
 * This function will shift _a with _b bits to the left.
 * It will insert zeroes on the right.
 * It asserts 0 <= _b <= MAX_SHIFT_1W.
 * The operation count for this function assumes that
 * the shift amount is a cloned scalar input.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_lsl(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief logical shift right
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a >> _b
 *
 * This function will shift _a with _b bits to the right.
 * It will insert zeroes on the left.
 * It asserts 0 <= _b <= MAX_SHIFT_1W.
 * The operation count for this function assumes that
 * the shift amount is a cloned scalar input.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_lsr(
    const tvector1w     _a,
    const tvector1w     _b);

#ifdef ISP2401
/** @brief bidirectional saturating arithmetic shift
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a << |_b| if _b is positive
 *			_a >> |_b| if _b is negative
 *
 * If _b > 0, this function will shift _a with _b bits to the left,
 * saturating at MIN_RANGE/MAX_RANGE in case of overflow.
 * if _b < 0, this function will shift _a with _b bits to the right.
 * It asserts -MAX_SHIFT_1W <= _b <= MAX_SHIFT_1W.
 * If _b = 0, it returns _a.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_ashift_sat(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief bidirectional non-saturating arithmetic shift
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a << |_b| if _b is positive
 *			_a >> |_b| if _b is negative
 *
 * If _b > 0, this function will shift _a with _b bits to the left,
 * no saturation is performed in case of overflow.
 * if _b < 0, this function will shift _a with _b bits to the right.
 * It asserts -MAX_SHIFT_1W <= _b <= MAX_SHIFT_1W.
 * If _b = 0, it returns _a.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_ashift(
    const tvector1w     _a,
    const tvector1w     _b);


/** @brief bidirectional logical shift
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a << |_b| if _b is positive
 *			_a >> |_b| if _b is negative
 *
 * This function will shift _a with _b bits to the left if _b is positive.
 * This function will shift _a with _b bits to the right if _b is negative.
 * It asserts -MAX_SHIFT_1W <= _b <= MAX_SHIFT_1W.
 * It inserts zeros on the left or right depending on the shift direction: 
 * right or left.
 * The operation count for this function assumes that
 * the shift amount is a cloned scalar input.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_lshift(
    const tvector1w     _a,
    const tvector1w     _b);

#endif
/* Cast */

/** @brief Cast from int to 1w
 *
 * @param[in] _a	input
 *
 * @return		_a
 *
 * This function casts the input from integer type to
 * single precision. It asserts there is no overflow.
 *
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_int_cast_to_1w(
    const int           _a);

/** @brief Cast from 1w to int
 *
 * @param[in] _a	input
 *
 * @return		_a
 *
 * This function casts the input from single precision type to
 * integer, preserving value and sign.
 *
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H int OP_1w_cast_to_int(
    const tvector1w      _a);

/** @brief Cast from 1w to 2w
 *
 * @param[in] _a	input
 *
 * @return		_a
 *
 * This function casts the input from single precision type to
 * double precision, preserving value and sign.
 *
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector2w OP_1w_cast_to_2w(
    const tvector1w     _a);

/** @brief Cast from 2w to 1w
 *
 * @param[in] _a	input
 *
 * @return		_a
 *
 * This function casts the input from double precision type to
 * single precision. In case of overflow it will wrap around.
 *
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_2w_cast_to_1w(
    const tvector2w    _a);


/** @brief Cast from 2w to 1w with saturation
 *
 * @param[in] _a	input
 *
 * @return		_a
 *
 * This function casts the input from double precision type to
 * single precision after saturating it to the range of single
 * precision.
 *
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_2w_sat_cast_to_1w(
    const tvector2w    _a);

/* clipping */

/** @brief Clip asymmetrical
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a clipped between ~_b and b
 *
 * This function will clip the first argument between
 * (-_b - 1) and _b.
 * It asserts _b >= 0.
 *
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_clip_asym(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief Clip zero
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a clipped beteween 0 and _b
 *
 * This function will clip the first argument between
 * zero and _b.
 * It asserts _b >= 0.
 *
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_clipz(
    const tvector1w     _a,
    const tvector1w     _b);

/* division */

/** @brief Truncated division
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		trunc( _a / _b )
 *
 * This function will divide the first argument by
 * the second argument, with rounding toward 0.
 * If _b == 0 and _a <  0, the function will return MIN_RANGE.
 * If _b == 0 and _a == 0, the function will return 0.
 * If _b == 0 and _a >  0, the function will return MAX_RANGE.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_div(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief Fractional saturating divide
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a / _b
 *
 * This function will perform fixed point division of
 * the first argument by the second argument, with rounding toward 0.
 * In case of overflow it will saturate.
 * If _b == 0 and _a <  0, the function will return MIN_RANGE.
 * If _b == 0 and _a == 0, the function will return 0.
 * If _b == 0 and _a >  0, the function will return MAX_RANGE.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_qdiv(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief Modulo
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a % _b
 *
 * This function will return the remainder r = _a - _b * trunc( _a / _b ),
 * Note that the sign of the remainder is always equal to the sign of _a.
 * If _b == 0 the function will return _a.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_mod(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief Unsigned integer Square root
 *
 * @param[in] _a	input
 *
 * @return		Integer square root of _a
 *
 * This function will calculate the Integer square root of _a
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w_unsigned OP_1w_sqrt_u(
	const tvector1w_unsigned     _a);

/* Miscellaneous */

/** @brief Multiplexer
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 * @param[in] _c	condition
 *
 * @return		_c ? _a : _b
 *
 * This function will return _a if the condition _c
 * is true and _b otherwise.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_mux(
    const tvector1w     _a,
    const tvector1w     _b,
    const tflags           _c);

/** @brief Average without rounding
 *
 * @param[in] _a	first operand
 * @param[in] _b	second operand
 *
 * @return		(_a + _b) >> 1
 *
 * This function will add _a and _b, and right shift
 * the result by one without rounding. No overflow
 * will occur because addition is performed in the
 * proper precision.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w  OP_1w_avg(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief Average with rounding
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		(_a + _b) >> 1
 *
 * This function will add _a and _b at full precision,
 * and right shift with rounding the result with 1 bit.
 * Depending on the rounding mode of the core
 * it will round to nearest or to nearest even.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_avgrnd(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief Minimum
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		(_a < _b) ? _a : _b;
 *
 * This function will return the smallest of both
 * input arguments.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_min(
    const tvector1w     _a,
    const tvector1w     _b);

/** @brief Maximum
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		(_a > _b) ? _a : _b;
 *
 * This function will return the largest of both
 * input arguments.
 */
STORAGE_CLASS_ISP_OP1W_FUNC_H tvector1w OP_1w_max(
    const tvector1w     _a,
    const tvector1w     _b);

#ifndef INLINE_ISP_OP1W
#define STORAGE_CLASS_ISP_OP1W_FUNC_C
#define STORAGE_CLASS_ISP_OP1W_DATA_C const
#else /* INLINE_ISP_OP1W */
#define STORAGE_CLASS_ISP_OP1W_FUNC_C STORAGE_CLASS_ISP_OP1W_FUNC_H
#define STORAGE_CLASS_ISP_OP1W_DATA_C STORAGE_CLASS_ISP_OP1W_DATA_H
#include "isp_op1w.c"
#define ISP_OP1W_INLINED
#endif  /* INLINE_ISP_OP1W */

#endif /* __ISP_OP1W_H_INCLUDED__ */

